/*
 * Copyright 2016 Józef Kucia for CodeWeavers
 * Copyright 2016 Henri Verbeet for CodeWeavers
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include "vkd3d_private.h"

/* Fence worker thread */
static HRESULT vkd3d_queue_gpu_fence(struct vkd3d_fence_worker *worker,
        VkFence vk_fence, ID3D12Fence *fence, UINT64 value)
{
    int rc;

    TRACE("worker %p, fence %p, value %#"PRIx64".\n", worker, fence, value);

    if ((rc = pthread_mutex_lock(&worker->mutex)))
    {
        ERR("Failed to lock mutex, error %d.\n", rc);
        return E_FAIL;
    }

    if (!vkd3d_array_reserve((void **)&worker->vk_fences, &worker->vk_fences_size,
            worker->fence_count + 1, sizeof(*worker->vk_fences)))
    {
        ERR("Failed to add GPU fence.\n");
        pthread_mutex_unlock(&worker->mutex);
        return E_OUTOFMEMORY;
    }
    if (!vkd3d_array_reserve((void **)&worker->fences, &worker->fences_size,
            worker->fence_count + 1, sizeof(*worker->fences)))
    {
        ERR("Failed to add GPU fence.\n");
        pthread_mutex_unlock(&worker->mutex);
        return E_OUTOFMEMORY;
    }

    worker->vk_fences[worker->fence_count] = vk_fence;
    worker->fences[worker->fence_count].fence = fence;
    worker->fences[worker->fence_count].value = value;
    ++worker->fence_count;

    pthread_cond_signal(&worker->cond);
    pthread_mutex_unlock(&worker->mutex);

    return S_OK;
}

static void vkd3d_wait_for_gpu_fences(struct vkd3d_fence_worker *worker)
{
    struct d3d12_device *device = worker->device;
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    unsigned int i, j;
    HRESULT hr;
    int vr;

    if (!worker->fence_count)
        return;

    vr = VK_CALL(vkWaitForFences(device->vk_device,
            worker->fence_count, worker->vk_fences, VK_FALSE, 0));
    if (vr == VK_TIMEOUT)
        return;
    if (vr != VK_SUCCESS)
    {
        ERR("Failed to wait for Vulkan fences, vr %d.\n", vr);
        return;
    }

    for (i = 0, j = 0; i < worker->fence_count; ++i)
    {
        if (!(vr = VK_CALL(vkGetFenceStatus(device->vk_device, worker->vk_fences[i]))))
        {
            struct vkd3d_waiting_fence *current = &worker->fences[i];
            if (FAILED(hr = ID3D12Fence_Signal(current->fence, current->value)))
                ERR("Failed to signal D3D12 fence, hr %d.\n", hr);
            VK_CALL(vkDestroyFence(device->vk_device, worker->vk_fences[i], NULL));
            continue;
        }

        if (vr != VK_NOT_READY)
            ERR("Failed to get Vulkan fence status, vr %d.\n", vr);

        if (i != j)
        {
            worker->vk_fences[j] = worker->vk_fences[i];
            worker->fences[j] = worker->fences[i];
        }
        ++j;
    }
    worker->fence_count = j;
}

static void *vkd3d_fence_worker_main(void *arg)
{
    struct vkd3d_fence_worker *worker = arg;
    int rc;

    for (;;)
    {
        if ((rc = pthread_mutex_lock(&worker->mutex)))
        {
            ERR("Failed to lock mutex, error %d.\n", rc);
            return NULL;
        }

        if (worker->should_exit && !worker->fence_count)
        {
            pthread_mutex_unlock(&worker->mutex);
            break;
        }

        if (!worker->fence_count)
        {
            if ((rc = pthread_cond_wait(&worker->cond, &worker->mutex)))
            {
                ERR("Failed to wait on condition variable, error %d.\n", rc);
                pthread_mutex_unlock(&worker->mutex);
                return NULL;
            }
        }

        vkd3d_wait_for_gpu_fences(worker);

        pthread_mutex_unlock(&worker->mutex);
    }

    return NULL;
}

HRESULT vkd3d_start_fence_worker(struct vkd3d_fence_worker *worker,
        struct d3d12_device *device)
{
    int rc;

    TRACE("worker %p.\n", worker);

    worker->should_exit = false;
    worker->device = device;

    worker->fence_count = 0;

    worker->vk_fences = NULL;
    worker->vk_fences_size = 0;
    worker->fences = NULL;
    worker->fences_size = 0;

    if ((rc = pthread_mutex_init(&worker->mutex, NULL)))
    {
        ERR("Failed to initialize mutex, error %d.\n", rc);
        return E_FAIL;
    }

    if ((rc = pthread_cond_init(&worker->cond, NULL)))
    {
        ERR("Failed to initialize condition variable, error %d.\n", rc);
        pthread_mutex_destroy(&worker->mutex);
        return E_FAIL;
    }

    if ((rc = pthread_create(&worker->thread, NULL, vkd3d_fence_worker_main, worker)))
    {
        ERR("Failed to create fence worker thread, error %d.\n", rc);
        pthread_mutex_destroy(&worker->mutex);
        pthread_cond_destroy(&worker->cond);
        return E_FAIL;
    }

    return S_OK;
}

HRESULT vkd3d_stop_fence_worker(struct vkd3d_fence_worker *worker)
{
    int rc;

    TRACE("worker %p.\n", worker);

    if ((rc = pthread_mutex_lock(&worker->mutex)))
    {
        ERR("Failed to lock mutex, error %d.\n", rc);
        return E_FAIL;
    }

    worker->should_exit = true;
    pthread_cond_signal(&worker->cond);

    pthread_mutex_unlock(&worker->mutex);

    if ((rc = pthread_join(worker->thread, NULL)))
    {
        ERR("Failed to join fence worker thread, error %d.\n", rc);
        return E_FAIL;
    }

    pthread_mutex_destroy(&worker->mutex);
    pthread_cond_destroy(&worker->cond);

    vkd3d_free(worker->vk_fences);
    vkd3d_free(worker->fences);

    return S_OK;
}

/* ID3D12Fence */
static struct d3d12_fence *impl_from_ID3D12Fence(ID3D12Fence *iface)
{
    return CONTAINING_RECORD(iface, struct d3d12_fence, ID3D12Fence_iface);
}

static HRESULT STDMETHODCALLTYPE d3d12_fence_QueryInterface(ID3D12Fence *iface,
        REFIID riid, void **object)
{
    TRACE("iface %p, riid %s, object %p.\n", iface, debugstr_guid(riid), object);

    if (IsEqualGUID(riid, &IID_ID3D12Fence)
            || IsEqualGUID(riid, &IID_ID3D12Pageable)
            || IsEqualGUID(riid, &IID_ID3D12DeviceChild)
            || IsEqualGUID(riid, &IID_ID3D12Object)
            || IsEqualGUID(riid, &IID_IUnknown))
    {
        ID3D12Fence_AddRef(iface);
        *object = iface;
        return S_OK;
    }

    WARN("%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid(riid));

    *object = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE d3d12_fence_AddRef(ID3D12Fence *iface)
{
    struct d3d12_fence *fence = impl_from_ID3D12Fence(iface);
    ULONG refcount = InterlockedIncrement(&fence->refcount);

    TRACE("%p increasing refcount to %u.\n", fence, refcount);

    return refcount;
}

static ULONG STDMETHODCALLTYPE d3d12_fence_Release(ID3D12Fence *iface)
{
    struct d3d12_fence *fence = impl_from_ID3D12Fence(iface);
    ULONG refcount = InterlockedDecrement(&fence->refcount);
    int rc;

    TRACE("%p decreasing refcount to %u.\n", fence, refcount);

    if (!refcount)
    {
        struct d3d12_device *device = fence->device;

        vkd3d_free(fence->events);
        if ((rc = pthread_mutex_destroy(&fence->mutex)))
            ERR("Failed to destroy mutex, error %d.\n", rc);
        vkd3d_free(fence);

        ID3D12Device_Release(&device->ID3D12Device_iface);
    }

    return refcount;
}

static HRESULT STDMETHODCALLTYPE d3d12_fence_GetPrivateData(ID3D12Fence *iface,
        REFGUID guid, UINT *data_size, void *data)
{
    FIXME("iface %p, guid %s, data_size %p, data %p stub!",
            iface, debugstr_guid(guid), data_size, data);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d12_fence_SetPrivateData(ID3D12Fence *iface,
        REFGUID guid, UINT data_size, const void *data)
{
    FIXME("iface %p, guid %s, data_size %u, data %p stub!\n",
            iface, debugstr_guid(guid), data_size, data);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d12_fence_SetPrivateDataInterface(ID3D12Fence *iface,
        REFGUID guid, const IUnknown *data)
{
    FIXME("iface %p, guid %s, data %p stub!\n", iface, debugstr_guid(guid), data);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d12_fence_SetName(ID3D12Fence *iface, const WCHAR *name)
{
    FIXME("iface %p, name %s stub!\n", iface, debugstr_w(name));

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d12_fence_GetDevice(ID3D12Fence *iface,
        REFIID riid, void **device)
{
    struct d3d12_fence *fence = impl_from_ID3D12Fence(iface);

    TRACE("iface %p, riid %s, device %p.\n", iface, debugstr_guid(riid), device);

    return ID3D12Device_QueryInterface(&fence->device->ID3D12Device_iface, riid, device);
}

static UINT64 STDMETHODCALLTYPE d3d12_fence_GetCompletedValue(ID3D12Fence *iface)
{
    struct d3d12_fence *fence = impl_from_ID3D12Fence(iface);
    UINT64 completed_value;
    int rc;

    TRACE("iface %p.\n", iface);

    if ((rc = pthread_mutex_lock(&fence->mutex)))
    {
        ERR("Failed to lock mutex, error %d.\n", rc);
        return 0;
    }
    completed_value = fence->value;
    pthread_mutex_unlock(&fence->mutex);
    return completed_value;
}

static HRESULT STDMETHODCALLTYPE d3d12_fence_SetEventOnCompletion(ID3D12Fence *iface,
        UINT64 value, HANDLE event)
{
    struct d3d12_fence *fence = impl_from_ID3D12Fence(iface);
    unsigned int i;
    int rc;

    TRACE("iface %p, value %#"PRIx64", event %p.\n", iface, value, event);

    if ((rc = pthread_mutex_lock(&fence->mutex)))
    {
        ERR("Failed to lock mutex, error %d.\n", rc);
        return E_FAIL;
    }

    if (value <= fence->value)
    {
        fence->device->signal_event(event);
        pthread_mutex_unlock(&fence->mutex);
        return S_OK;
    }

    for (i = 0; i < fence->event_count; ++i)
    {
        struct vkd3d_waiting_event *current = &fence->events[i];
        if (current->value == value && current->event == event)
        {
            WARN("Event completion for (%p, %#"PRIx64") is already in the list.\n",
                    event, value);
            pthread_mutex_unlock(&fence->mutex);
            return S_OK;
        }
    }

    if (!vkd3d_array_reserve((void **)&fence->events, &fence->events_size,
            fence->event_count + 1, sizeof(*fence->events)))
    {
        WARN("Failed to add event.\n");
        pthread_mutex_unlock(&fence->mutex);
        return E_OUTOFMEMORY;
    }

    fence->events[fence->event_count].value = value;
    fence->events[fence->event_count].event = event;
    ++fence->event_count;

    pthread_mutex_unlock(&fence->mutex);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d3d12_fence_Signal(ID3D12Fence *iface, UINT64 value)
{
    struct d3d12_fence *fence = impl_from_ID3D12Fence(iface);
    unsigned int i, j;
    int rc;

    TRACE("iface %p, value %#"PRIx64".\n", iface, value);

    if ((rc = pthread_mutex_lock(&fence->mutex)))
    {
        ERR("Failed to lock mutex, error %d.\n", rc);
        return E_FAIL;
    }

    fence->value = value;

    for (i = 0, j = 0; i < fence->event_count; ++i)
    {
        struct vkd3d_waiting_event *current = &fence->events[i];

        if (current->value <= value)
        {
            fence->device->signal_event(current->event);
        }
        else
        {
            if (i != j)
                fence->events[j] = *current;
            ++j;
        }
    }
    fence->event_count = j;

    pthread_mutex_unlock(&fence->mutex);

    return S_OK;
}

static const struct ID3D12FenceVtbl d3d12_fence_vtbl =
{
    /* IUnknown methods */
    d3d12_fence_QueryInterface,
    d3d12_fence_AddRef,
    d3d12_fence_Release,
    /* ID3D12Object methods */
    d3d12_fence_GetPrivateData,
    d3d12_fence_SetPrivateData,
    d3d12_fence_SetPrivateDataInterface,
    d3d12_fence_SetName,
    /* ID3D12DeviceChild methods */
    d3d12_fence_GetDevice,
    /* ID3D12Fence methods */
    d3d12_fence_GetCompletedValue,
    d3d12_fence_SetEventOnCompletion,
    d3d12_fence_Signal,
};

static HRESULT d3d12_fence_init(struct d3d12_fence *fence, struct d3d12_device *device,
        UINT64 initial_value, D3D12_FENCE_FLAGS flags)
{
    int rc;

    fence->ID3D12Fence_iface.lpVtbl = &d3d12_fence_vtbl;
    fence->refcount = 1;

    fence->value = initial_value;

    if ((rc = pthread_mutex_init(&fence->mutex, NULL)))
    {
        ERR("Failed to initialize mutex, error %d.\n", rc);
        return E_FAIL;
    }

    if (flags)
        FIXME("Ignoring flags %#x.\n", flags);

    fence->events = NULL;
    fence->events_size = 0;
    fence->event_count = 0;

    fence->device = device;
    ID3D12Device_AddRef(&device->ID3D12Device_iface);

    return S_OK;
}

HRESULT d3d12_fence_create(struct d3d12_device *device,
        UINT64 initial_value, D3D12_FENCE_FLAGS flags, struct d3d12_fence **fence)
{
    struct d3d12_fence *object;

    if (!(object = vkd3d_malloc(sizeof(*object))))
        return E_OUTOFMEMORY;

    d3d12_fence_init(object, device, initial_value, flags);

    TRACE("Created fence %p.\n", object);

    *fence = object;

    return S_OK;
}

/* Command buffers */
static HRESULT vkd3d_begin_command_buffer(struct d3d12_command_list *list)
{
    struct d3d12_device *device = list->device;
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    VkCommandBufferBeginInfo begin_info;
    VkResult vr;

    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.pNext = NULL;
    begin_info.flags = 0;
    begin_info.pInheritanceInfo = NULL;

    if ((vr = VK_CALL(vkBeginCommandBuffer(list->vk_command_buffer, &begin_info))) < 0)
    {
        WARN("Failed to begin command buffer, vr %d.\n", vr);
        return hresult_from_vk_result(vr);
    }

    list->is_recording = TRUE;

    if (list->pipeline_state)
        ID3D12GraphicsCommandList_SetPipelineState(&list->ID3D12GraphicsCommandList_iface, list->pipeline_state);

    return S_OK;
}

static HRESULT vkd3d_reset_command_buffer(struct d3d12_command_list *list)
{
    struct d3d12_device *device = list->device;
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    VkResult vr;

    if ((vr = VK_CALL(vkResetCommandBuffer(list->vk_command_buffer,
            VK_COMMAND_BUFFER_RESET_RELEASE_RESOURCES_BIT))) < 0)
    {
        WARN("Failed to reset command buffer, vr %d.\n", vr);
        return hresult_from_vk_result(vr);
    }

    return vkd3d_begin_command_buffer(list);
}

static HRESULT vkd3d_command_allocator_allocate_command_list(struct d3d12_command_allocator *allocator,
        struct d3d12_command_list *list)
{
    struct d3d12_device *device = allocator->device;
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    VkCommandBufferAllocateInfo command_buffer_info;
    VkResult vr;
    HRESULT hr;

    TRACE("allocator %p, list %p.\n", allocator, list);

    if (allocator->current_command_list)
    {
        FIXME("Allocation for multiple command list not supported.\n");
        return E_NOTIMPL;
    }

    command_buffer_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    command_buffer_info.pNext = NULL;
    command_buffer_info.commandPool = allocator->vk_command_pool;
    command_buffer_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    command_buffer_info.commandBufferCount = 1;

    if ((vr = VK_CALL(vkAllocateCommandBuffers(device->vk_device, &command_buffer_info,
            &list->vk_command_buffer))) < 0)
    {
        WARN("Failed to allocate Vulkan command buffer, vr %d.\n", vr);
        return hresult_from_vk_result(vr);
    }

    if (FAILED(hr = vkd3d_begin_command_buffer(list)))
    {
        VK_CALL(vkFreeCommandBuffers(device->vk_device, allocator->vk_command_pool,
                1, &list->vk_command_buffer));
        return hr;
    }

    allocator->current_command_list = list;

    return S_OK;
}

static void vkd3d_command_allocator_free_command_list(struct d3d12_command_allocator *allocator,
        struct d3d12_command_list *list)
{
    struct d3d12_device *device = allocator->device;
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;

    TRACE("allocator %p, list %p.\n", allocator, list);

    VK_CALL(vkFreeCommandBuffers(device->vk_device, allocator->vk_command_pool,
            1, &list->vk_command_buffer));

    if (allocator->current_command_list == list)
        allocator->current_command_list = NULL;
}

static bool d3d12_command_allocator_add_render_pass(struct d3d12_command_allocator *allocator, VkRenderPass pass)
{
    if (!vkd3d_array_reserve((void **)&allocator->passes, &allocator->passes_size,
            allocator->pass_count + 1, sizeof(*allocator->passes)))
        return false;

    allocator->passes[allocator->pass_count++] = pass;

    return true;
}

static bool d3d12_command_allocator_add_framebuffer(struct d3d12_command_allocator *allocator,
        VkFramebuffer framebuffer)
{
    if (!vkd3d_array_reserve((void **)&allocator->framebuffers, &allocator->framebuffers_size,
            allocator->framebuffer_count + 1, sizeof(*allocator->framebuffers)))
        return false;

    allocator->framebuffers[allocator->framebuffer_count++] = framebuffer;

    return true;
}

static bool d3d12_command_allocator_add_pipeline(struct d3d12_command_allocator *allocator, VkPipeline pipeline)
{
    if (!vkd3d_array_reserve((void **)&allocator->pipelines, &allocator->pipelines_size,
            allocator->pipeline_count + 1, sizeof(*allocator->pipelines)))
        return false;

    allocator->pipelines[allocator->pipeline_count++] = pipeline;

    return true;
}

static void vkd3d_command_list_destroyed(struct d3d12_command_list *list)
{
    TRACE("list %p.\n", list);

    list->allocator = NULL;
    list->vk_command_buffer = VK_NULL_HANDLE;
}

/* ID3D12CommandAllocator */
static inline struct d3d12_command_allocator *impl_from_ID3D12CommandAllocator(ID3D12CommandAllocator *iface)
{
    return CONTAINING_RECORD(iface, struct d3d12_command_allocator, ID3D12CommandAllocator_iface);
}

static HRESULT STDMETHODCALLTYPE d3d12_command_allocator_QueryInterface(ID3D12CommandAllocator *iface,
        REFIID riid, void **object)
{
    TRACE("iface %p, riid %s, object %p.\n", iface, debugstr_guid(riid), object);

    if (IsEqualGUID(riid, &IID_ID3D12CommandAllocator)
            || IsEqualGUID(riid, &IID_ID3D12Pageable)
            || IsEqualGUID(riid, &IID_ID3D12DeviceChild)
            || IsEqualGUID(riid, &IID_ID3D12Object)
            || IsEqualGUID(riid, &IID_IUnknown))
    {
        ID3D12CommandAllocator_AddRef(iface);
        *object = iface;
        return S_OK;
    }

    WARN("%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid(riid));

    *object = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE d3d12_command_allocator_AddRef(ID3D12CommandAllocator *iface)
{
    struct d3d12_command_allocator *allocator = impl_from_ID3D12CommandAllocator(iface);
    ULONG refcount = InterlockedIncrement(&allocator->refcount);

    TRACE("%p increasing refcount to %u.\n", allocator, refcount);

    return refcount;
}

static ULONG STDMETHODCALLTYPE d3d12_command_allocator_Release(ID3D12CommandAllocator *iface)
{
    struct d3d12_command_allocator *allocator = impl_from_ID3D12CommandAllocator(iface);
    ULONG refcount = InterlockedDecrement(&allocator->refcount);

    TRACE("%p decreasing refcount to %u.\n", allocator, refcount);

    if (!refcount)
    {
        struct d3d12_device *device = allocator->device;
        const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
        unsigned int i;

        if (allocator->current_command_list)
            vkd3d_command_list_destroyed(allocator->current_command_list);

        for (i = 0; i < allocator->pipeline_count; ++i)
        {
            VK_CALL(vkDestroyPipeline(device->vk_device, allocator->pipelines[i], NULL));
        }
        vkd3d_free(allocator->pipelines);

        for (i = 0; i < allocator->framebuffer_count; ++i)
        {
            VK_CALL(vkDestroyFramebuffer(device->vk_device, allocator->framebuffers[i], NULL));
        }
        vkd3d_free(allocator->framebuffers);

        for (i = 0; i < allocator->pass_count; ++i)
        {
            VK_CALL(vkDestroyRenderPass(device->vk_device, allocator->passes[i], NULL));
        }
        vkd3d_free(allocator->passes);

        VK_CALL(vkDestroyCommandPool(device->vk_device, allocator->vk_command_pool, NULL));

        vkd3d_free(allocator);

        ID3D12Device_Release(&device->ID3D12Device_iface);
    }

    return refcount;
}

static HRESULT STDMETHODCALLTYPE d3d12_command_allocator_GetPrivateData(ID3D12CommandAllocator *iface,
        REFGUID guid, UINT *data_size, void *data)
{
    FIXME("iface %p, guid %s, data_size %p, data %p stub!", iface, debugstr_guid(guid), data_size, data);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d12_command_allocator_SetPrivateData(ID3D12CommandAllocator *iface,
        REFGUID guid, UINT data_size, const void *data)
{
    FIXME("iface %p, guid %s, data_size %u, data %p stub!\n", iface, debugstr_guid(guid), data_size, data);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d12_command_allocator_SetPrivateDataInterface(ID3D12CommandAllocator *iface,
        REFGUID guid, const IUnknown *data)
{
    FIXME("iface %p, guid %s, data %p stub!\n", iface, debugstr_guid(guid), data);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d12_command_allocator_SetName(ID3D12CommandAllocator *iface, const WCHAR *name)
{
    FIXME("iface %p, name %s stub!\n", iface, debugstr_w(name));

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d12_command_allocator_GetDevice(ID3D12CommandAllocator *iface,
        REFIID riid, void **device)
{
    struct d3d12_command_allocator *allocator = impl_from_ID3D12CommandAllocator(iface);

    TRACE("iface %p, riid %s, device %p.\n", iface, debugstr_guid(riid), device);

    return ID3D12Device_QueryInterface(&allocator->device->ID3D12Device_iface, riid, device);
}

static HRESULT STDMETHODCALLTYPE d3d12_command_allocator_Reset(ID3D12CommandAllocator *iface)
{
    struct d3d12_command_allocator *allocator = impl_from_ID3D12CommandAllocator(iface);
    const struct vkd3d_vk_device_procs *vk_procs;
    struct d3d12_command_list *list;
    struct d3d12_device *device;
    unsigned int i;
    VkResult vr;

    TRACE("iface %p.\n", iface);

    if ((list = allocator->current_command_list))
    {
        if (list->is_recording)
        {
            WARN("A command list using this allocator is in the recording state.\n");
            return E_FAIL;
        }

        TRACE("Resetting command list %p.\n", list);
    }

    device = allocator->device;
    vk_procs = &device->vk_procs;

    for (i = 0; i < allocator->pipeline_count; ++i)
    {
        VK_CALL(vkDestroyPipeline(device->vk_device, allocator->pipelines[i], NULL));
    }
    allocator->pipeline_count = 0;

    for (i = 0; i < allocator->framebuffer_count; ++i)
    {
        VK_CALL(vkDestroyFramebuffer(device->vk_device, allocator->framebuffers[i], NULL));
    }
    allocator->framebuffer_count = 0;

    for (i = 0; i < allocator->pass_count; ++i)
    {
        VK_CALL(vkDestroyRenderPass(device->vk_device, allocator->passes[i], NULL));
    }
    allocator->pass_count = 0;

    if ((vr = VK_CALL(vkResetCommandPool(device->vk_device, allocator->vk_command_pool,
            VK_COMMAND_POOL_RESET_RELEASE_RESOURCES_BIT))))
    {
        WARN("Resetting command pool failed, vr %d.\n", vr);
        return hresult_from_vk_result(vr);
    }

    return S_OK;
}

static const struct ID3D12CommandAllocatorVtbl d3d12_command_allocator_vtbl =
{
    /* IUnknown methods */
    d3d12_command_allocator_QueryInterface,
    d3d12_command_allocator_AddRef,
    d3d12_command_allocator_Release,
    /* ID3D12Object methods */
    d3d12_command_allocator_GetPrivateData,
    d3d12_command_allocator_SetPrivateData,
    d3d12_command_allocator_SetPrivateDataInterface,
    d3d12_command_allocator_SetName,
    /* ID3D12DeviceChild methods */
    d3d12_command_allocator_GetDevice,
    /* ID3D12CommandAllocator methods */
    d3d12_command_allocator_Reset,
};

static struct d3d12_command_allocator *unsafe_impl_from_ID3D12CommandAllocator(ID3D12CommandAllocator *iface)
{
    if (!iface)
        return NULL;
    assert(iface->lpVtbl == &d3d12_command_allocator_vtbl);
    return impl_from_ID3D12CommandAllocator(iface);
}

static HRESULT d3d12_command_allocator_init(struct d3d12_command_allocator *allocator,
        struct d3d12_device *device, D3D12_COMMAND_LIST_TYPE type)
{
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    VkCommandPoolCreateInfo command_pool_info;
    VkResult vr;

    allocator->ID3D12CommandAllocator_iface.lpVtbl = &d3d12_command_allocator_vtbl;
    allocator->refcount = 1;

    allocator->type = type;

    command_pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    command_pool_info.pNext = NULL;
    command_pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    switch (type)
    {
        case D3D12_COMMAND_LIST_TYPE_DIRECT:
            command_pool_info.queueFamilyIndex = device->direct_queue_family_index;
            break;
        case D3D12_COMMAND_LIST_TYPE_COPY:
            command_pool_info.queueFamilyIndex = device->copy_queue_family_index;
            break;
        default:
            FIXME("Unhandled command list type %#x.\n", type);
            command_pool_info.queueFamilyIndex = device->direct_queue_family_index;
            break;
    }

    if ((vr = VK_CALL(vkCreateCommandPool(device->vk_device, &command_pool_info, NULL,
            &allocator->vk_command_pool))) < 0)
    {
        WARN("Failed to create Vulkan command pool, vr %d.\n", vr);
        return hresult_from_vk_result(vr);
    }

    allocator->passes = NULL;
    allocator->passes_size = 0;
    allocator->pass_count = 0;

    allocator->framebuffers = NULL;
    allocator->framebuffers_size = 0;
    allocator->framebuffer_count = 0;

    allocator->pipelines = NULL;
    allocator->pipelines_size = 0;
    allocator->pipeline_count = 0;

    allocator->current_command_list = NULL;

    allocator->device = device;
    ID3D12Device_AddRef(&device->ID3D12Device_iface);

    return S_OK;
}

HRESULT d3d12_command_allocator_create(struct d3d12_device *device,
        D3D12_COMMAND_LIST_TYPE type, struct d3d12_command_allocator **allocator)
{
    struct d3d12_command_allocator *object;
    HRESULT hr;

    if (!(D3D12_COMMAND_LIST_TYPE_DIRECT <= type && type <= D3D12_COMMAND_LIST_TYPE_COPY))
    {
        WARN("Invalid type %#x.\n", type);
        return E_INVALIDARG;
    }

    if (!(object = vkd3d_malloc(sizeof(*object))))
        return E_OUTOFMEMORY;

    if (FAILED(hr = d3d12_command_allocator_init(object, device, type)))
    {
        vkd3d_free(object);
        return hr;
    }

    TRACE("Created command allocator %p.\n", object);

    *allocator = object;

    return S_OK;
}

/* ID3D12CommandList */
static inline struct d3d12_command_list *impl_from_ID3D12GraphicsCommandList(ID3D12GraphicsCommandList *iface)
{
    return CONTAINING_RECORD(iface, struct d3d12_command_list, ID3D12GraphicsCommandList_iface);
}

static void d3d12_command_list_invalidate_current_framebuffer(struct d3d12_command_list *list)
{
    list->current_framebuffer = VK_NULL_HANDLE;
}

static void d3d12_command_list_invalidate_current_pipeline(struct d3d12_command_list *list)
{
    list->current_pipeline = VK_NULL_HANDLE;
}

static HRESULT STDMETHODCALLTYPE d3d12_command_list_QueryInterface(ID3D12GraphicsCommandList *iface,
        REFIID riid, void **object)
{
    TRACE("iface %p, riid %s, object %p.\n", iface, debugstr_guid(riid), object);

    if (IsEqualGUID(riid, &IID_ID3D12GraphicsCommandList)
            || IsEqualGUID(riid, &IID_ID3D12CommandList)
            || IsEqualGUID(riid, &IID_ID3D12DeviceChild)
            || IsEqualGUID(riid, &IID_ID3D12Object)
            || IsEqualGUID(riid, &IID_IUnknown))
    {
        ID3D12GraphicsCommandList_AddRef(iface);
        *object = iface;
        return S_OK;
    }

    WARN("%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid(riid));

    *object = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE d3d12_command_list_AddRef(ID3D12GraphicsCommandList *iface)
{
    struct d3d12_command_list *list = impl_from_ID3D12GraphicsCommandList(iface);
    ULONG refcount = InterlockedIncrement(&list->refcount);

    TRACE("%p increasing refcount to %u.\n", list, refcount);

    return refcount;
}

static ULONG STDMETHODCALLTYPE d3d12_command_list_Release(ID3D12GraphicsCommandList *iface)
{
    struct d3d12_command_list *list = impl_from_ID3D12GraphicsCommandList(iface);
    ULONG refcount = InterlockedDecrement(&list->refcount);

    TRACE("%p decreasing refcount to %u.\n", list, refcount);

    if (!refcount)
    {
        struct d3d12_device *device = list->device;

        /* When command pool is destroyed, all command buffers are implicitly freed. */
        if (list->allocator)
            vkd3d_command_allocator_free_command_list(list->allocator, list);

        vkd3d_free(list);

        ID3D12Device_Release(&device->ID3D12Device_iface);
    }

    return refcount;
}

static HRESULT STDMETHODCALLTYPE d3d12_command_list_GetPrivateData(ID3D12GraphicsCommandList *iface,
        REFGUID guid, UINT *data_size, void *data)
{
    FIXME("iface %p, guid %s, data_size %p, data %p stub!", iface, debugstr_guid(guid), data_size, data);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d12_command_list_SetPrivateData(ID3D12GraphicsCommandList *iface,
        REFGUID guid, UINT data_size, const void *data)
{
    FIXME("iface %p, guid %s, data_size %u, data %p stub!\n", iface, debugstr_guid(guid), data_size, data);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d12_command_list_SetPrivateDataInterface(ID3D12GraphicsCommandList *iface,
        REFGUID guid, const IUnknown *data)
{
    FIXME("iface %p, guid %s, data %p stub!\n", iface, debugstr_guid(guid), data);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d12_command_list_SetName(ID3D12GraphicsCommandList *iface, const WCHAR *name)
{
    FIXME("iface %p, name %s stub!\n", iface, debugstr_w(name));

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d12_command_list_GetDevice(ID3D12GraphicsCommandList *iface,
        REFIID riid, void **device)
{
    struct d3d12_command_list *list = impl_from_ID3D12GraphicsCommandList(iface);

    TRACE("iface %p, riid %s, device %p.\n", iface, debugstr_guid(riid), device);

    return ID3D12Device_QueryInterface(&list->device->ID3D12Device_iface, riid, device);
}

static D3D12_COMMAND_LIST_TYPE STDMETHODCALLTYPE d3d12_command_list_GetType(ID3D12GraphicsCommandList *iface)
{
    struct d3d12_command_list *list = impl_from_ID3D12GraphicsCommandList(iface);

    TRACE("iface %p.\n", iface);

    return list->type;
}

static HRESULT STDMETHODCALLTYPE d3d12_command_list_Close(ID3D12GraphicsCommandList *iface)
{
    struct d3d12_command_list *list = impl_from_ID3D12GraphicsCommandList(iface);
    const struct vkd3d_vk_device_procs *vk_procs;
    VkResult vr;

    TRACE("iface %p.\n", iface);

    if (!list->is_recording)
    {
        WARN("Command list is not in the recording state.\n");
        return E_FAIL;
    }

    vk_procs = &list->device->vk_procs;
    if ((vr = VK_CALL(vkEndCommandBuffer(list->vk_command_buffer))) < 0)
    {
        WARN("Failed to end command buffer, vr %d.\n", vr);
        return hresult_from_vk_result(vr);
    }

    list->is_recording = FALSE;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d3d12_command_list_Reset(ID3D12GraphicsCommandList *iface,
        ID3D12CommandAllocator *allocator, ID3D12PipelineState *initial_state)
{
    struct d3d12_command_list *list = impl_from_ID3D12GraphicsCommandList(iface);
    struct d3d12_command_allocator *allocator_impl = unsafe_impl_from_ID3D12CommandAllocator(allocator);
    HRESULT hr;

    TRACE("iface %p, allocator %p, initial_state %p.\n",
            iface, allocator, initial_state);

    if (!allocator_impl)
    {
        WARN("Command allocator is NULL.\n");
        return E_INVALIDARG;
    }

    if (list->is_recording)
    {
        WARN("Command list is in the recording state.\n");
        return E_FAIL;
    }

    if (list->allocator == allocator_impl)
        return vkd3d_reset_command_buffer(list);

    if (list->allocator)
        vkd3d_command_allocator_free_command_list(list->allocator, list);

    list->allocator = allocator_impl;
    list->pipeline_state = initial_state;
    if (FAILED(hr = vkd3d_command_allocator_allocate_command_list(allocator_impl, list)))
        return hr;

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d3d12_command_list_ClearState(ID3D12GraphicsCommandList *iface,
        ID3D12PipelineState *pipeline_state)
{
    FIXME("iface %p, pipline_state %p stub!\n", iface, pipeline_state);

    return E_NOTIMPL;
}

static bool d3d12_command_list_update_current_framebuffer(struct d3d12_command_list *list,
        const struct vkd3d_vk_device_procs *vk_procs)
{
    struct VkFramebufferCreateInfo fb_desc;
    VkFramebuffer vk_framebuffer;
    VkResult vr;

    if (list->current_framebuffer != VK_NULL_HANDLE)
        return true;

    fb_desc.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fb_desc.pNext = NULL;
    fb_desc.flags = 0;
    fb_desc.renderPass = list->state->u.graphics.render_pass;
    fb_desc.attachmentCount = list->state->u.graphics.attachment_count;
    fb_desc.pAttachments = list->views;
    fb_desc.width = list->fb_width;
    fb_desc.height = list->fb_height;
    fb_desc.layers = 1;
    if ((vr = VK_CALL(vkCreateFramebuffer(list->device->vk_device, &fb_desc, NULL, &vk_framebuffer))) < 0)
    {
        WARN("Failed to create Vulkan framebuffer, vr %d.\n", vr);
        return false;
    }

    if (!d3d12_command_allocator_add_framebuffer(list->allocator, vk_framebuffer))
    {
        WARN("Failed to add framebuffer.\n");
        VK_CALL(vkDestroyFramebuffer(list->device->vk_device, vk_framebuffer, NULL));
        return false;
    }

    list->current_framebuffer = vk_framebuffer;

    return true;
}

static bool d3d12_command_list_update_current_pipeline(struct d3d12_command_list *list,
        const struct vkd3d_vk_device_procs *vk_procs)
{
    struct VkVertexInputBindingDescription bindings[D3D12_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT];
    struct VkPipelineVertexInputStateCreateInfo input_desc;
    struct VkPipelineColorBlendStateCreateInfo blend_desc;
    struct VkGraphicsPipelineCreateInfo pipeline_desc;
    struct d3d12_graphics_pipeline_state *state;
    size_t binding_count = 0;
    VkPipeline vk_pipeline;
    unsigned int i;
    uint32_t mask;
    VkResult vr;

    static const struct VkPipelineViewportStateCreateInfo vp_desc =
    {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .pNext = NULL,
        .flags = 0,
        .viewportCount = 1,
        .pViewports = NULL,
        .scissorCount = 1,
        .pScissors = NULL,
    };
    static const enum VkDynamicState dynamic_states[] =
    {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR,
    };
    static const struct VkPipelineDynamicStateCreateInfo dynamic_desc =
    {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .pNext = NULL,
        .flags = 0,
        .dynamicStateCount = ARRAY_SIZE(dynamic_states),
        .pDynamicStates = dynamic_states,
    };

    if (list->current_pipeline != VK_NULL_HANDLE)
        return true;

    if (list->state->vk_bind_point != VK_PIPELINE_BIND_POINT_GRAPHICS)
    {
        WARN("Pipeline state %p has bind point %#x.\n", list->state, list->state->vk_bind_point);
        return false;
    }

    state = &list->state->u.graphics;

    for (i = 0, mask = 0; i < state->attribute_count; ++i)
    {
        struct VkVertexInputBindingDescription *b;
        uint32_t binding;

        binding = state->attributes[i].binding;
        if (mask & (1u << binding))
            continue;

        if (binding_count == ARRAY_SIZE(bindings))
        {
            FIXME("Maximum binding count exceeded.\n");
            break;
        }

        mask |= 1u << binding;
        b = &bindings[binding_count];
        b->binding = binding;
        b->stride = list->strides[binding];
        b->inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        ++binding_count;
    }

    input_desc.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    input_desc.pNext = NULL;
    input_desc.flags = 0;
    input_desc.vertexBindingDescriptionCount = binding_count;
    input_desc.pVertexBindingDescriptions = bindings;
    input_desc.vertexAttributeDescriptionCount = state->attribute_count;
    input_desc.pVertexAttributeDescriptions = state->attributes;

    blend_desc.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    blend_desc.pNext = NULL;
    blend_desc.flags = 0;
    /* FIXME: Logic ops are per-target in D3D. */
    blend_desc.logicOpEnable = VK_FALSE;
    blend_desc.logicOp = VK_LOGIC_OP_COPY;
    blend_desc.attachmentCount = state->attachment_count;
    blend_desc.pAttachments = state->blend_attachments;
    blend_desc.blendConstants[0] = D3D12_DEFAULT_BLEND_FACTOR_RED;
    blend_desc.blendConstants[1] = D3D12_DEFAULT_BLEND_FACTOR_GREEN;
    blend_desc.blendConstants[2] = D3D12_DEFAULT_BLEND_FACTOR_BLUE;
    blend_desc.blendConstants[3] = D3D12_DEFAULT_BLEND_FACTOR_ALPHA;

    pipeline_desc.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipeline_desc.pNext = NULL;
    pipeline_desc.flags = 0;
    pipeline_desc.stageCount = state->stage_count;
    pipeline_desc.pStages = state->stages;
    pipeline_desc.pVertexInputState = &input_desc;
    pipeline_desc.pInputAssemblyState = &list->ia_desc;
    pipeline_desc.pTessellationState = NULL;
    pipeline_desc.pViewportState = &vp_desc;
    pipeline_desc.pRasterizationState = &state->rs_desc;
    pipeline_desc.pMultisampleState = &state->ms_desc;
    pipeline_desc.pDepthStencilState = NULL;
    pipeline_desc.pColorBlendState = &blend_desc;
    pipeline_desc.pDynamicState = &dynamic_desc;
    pipeline_desc.layout = state->root_signature->vk_pipeline_layout;
    pipeline_desc.renderPass = state->render_pass;
    pipeline_desc.subpass = 0;
    pipeline_desc.basePipelineHandle = VK_NULL_HANDLE;
    pipeline_desc.basePipelineIndex = -1;
    if ((vr = VK_CALL(vkCreateGraphicsPipelines(list->device->vk_device, VK_NULL_HANDLE,
            1, &pipeline_desc, NULL, &vk_pipeline))) < 0)
    {
        WARN("Failed to create Vulkan graphics pipeline, vr %d.\n", vr);
        return false;
    }

    if (!d3d12_command_allocator_add_pipeline(list->allocator, vk_pipeline))
    {
        WARN("Failed to add pipeline.\n");
        VK_CALL(vkDestroyPipeline(list->device->vk_device, vk_pipeline, NULL));
        return false;
    }

    VK_CALL(vkCmdBindPipeline(list->vk_command_buffer, list->state->vk_bind_point, vk_pipeline));
    list->current_pipeline = vk_pipeline;

    return true;
}

static void STDMETHODCALLTYPE d3d12_command_list_DrawInstanced(ID3D12GraphicsCommandList *iface,
        UINT vertex_count_per_instance, UINT instance_count, UINT start_vertex_location,
        UINT start_instance_location)
{
    struct d3d12_command_list *list = impl_from_ID3D12GraphicsCommandList(iface);
    const struct vkd3d_vk_device_procs *vk_procs;
    struct VkRenderPassBeginInfo begin_desc;

    TRACE("iface %p, vertex_count_per_instance %u, instance_count %u, "
            "start_vertex_location %u, start_instance_location %u.\n",
            iface, vertex_count_per_instance, instance_count,
            start_vertex_location, start_instance_location);

    vk_procs = &list->device->vk_procs;

    if (!d3d12_command_list_update_current_framebuffer(list, vk_procs))
        return;
    if (!d3d12_command_list_update_current_pipeline(list, vk_procs))
        return;

    /* FIXME: We probably don't want to begin a render pass for each draw. */
    begin_desc.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    begin_desc.pNext = NULL;
    begin_desc.renderPass = list->state->u.graphics.render_pass;
    begin_desc.framebuffer = list->current_framebuffer;
    begin_desc.renderArea.offset.x = 0;
    begin_desc.renderArea.offset.y = 0;
    begin_desc.renderArea.extent.width = list->fb_width;
    begin_desc.renderArea.extent.height = list->fb_height;
    begin_desc.clearValueCount = 0;
    begin_desc.pClearValues = NULL;
    VK_CALL(vkCmdBeginRenderPass(list->vk_command_buffer, &begin_desc, VK_SUBPASS_CONTENTS_INLINE));

    VK_CALL(vkCmdDraw(list->vk_command_buffer, vertex_count_per_instance,
            instance_count, start_vertex_location, start_instance_location));

    VK_CALL(vkCmdEndRenderPass(list->vk_command_buffer));
}

static void STDMETHODCALLTYPE d3d12_command_list_DrawIndexedInstanced(ID3D12GraphicsCommandList *iface,
        UINT index_count_per_instance, UINT instance_count, UINT start_vertex_location,
        INT base_vertex_location, UINT start_instance_location)
{
    FIXME("iface %p, index_count_per_instance %u, instance_count %u, start_vertex_location %u, "
            "base_vertex_location %d, start_instance_location %u stub!\n",
            iface, index_count_per_instance, instance_count, start_vertex_location,
            base_vertex_location, start_instance_location);
}

static void STDMETHODCALLTYPE d3d12_command_list_Dispatch(ID3D12GraphicsCommandList *iface,
        UINT x, UINT y, UINT z)
{
    FIXME("iface %p, x %u, y %u, z %u stub!\n", iface, x, y, z);
}

static void STDMETHODCALLTYPE d3d12_command_list_CopyBufferRegion(ID3D12GraphicsCommandList *iface,
        ID3D12Resource *dst_resource, UINT64 dst_offset, ID3D12Resource *src_resource,
        UINT64 src_offset, UINT64 byte_count)
{
    struct d3d12_resource *dst_resource_impl = unsafe_impl_from_ID3D12Resource(dst_resource);
    struct d3d12_resource *src_resource_impl = unsafe_impl_from_ID3D12Resource(src_resource);
    struct d3d12_command_list *list = impl_from_ID3D12GraphicsCommandList(iface);
    const struct vkd3d_vk_device_procs *vk_procs;
    VkBufferCopy buffer_copy;

    TRACE("iface %p, dst_resource %p, dst_offset %#"PRIx64", src_resource %p, "
            "src_offset %#"PRIx64", byte_count %#"PRIx64".\n",
            iface, dst_resource, dst_offset, src_resource, src_offset, byte_count);

    vk_procs = &list->device->vk_procs;

    assert(dst_resource_impl->desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER);
    assert(src_resource_impl->desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER);

    buffer_copy.srcOffset = src_offset;
    buffer_copy.dstOffset = dst_offset;
    buffer_copy.size = byte_count;

    VK_CALL(vkCmdCopyBuffer(list->vk_command_buffer,
            src_resource_impl->u.vk_buffer, dst_resource_impl->u.vk_buffer, 1, &buffer_copy));
}

static void STDMETHODCALLTYPE d3d12_command_list_CopyTextureRegion(ID3D12GraphicsCommandList *iface,
        const D3D12_TEXTURE_COPY_LOCATION *dst, UINT dst_x, UINT dst_y, UINT dst_z,
        const D3D12_TEXTURE_COPY_LOCATION *src, const D3D12_BOX *src_box)
{
    struct d3d12_command_list *list = impl_from_ID3D12GraphicsCommandList(iface);
    struct d3d12_resource *dst_resource, *src_resource;
    const struct vkd3d_vk_device_procs *vk_procs;
    VkBufferImageCopy buffer_image_copy;
    const struct vkd3d_format *format;

    TRACE("iface %p, dst %p, dst_x %u, dst_y %u, dst_z %u, src %p, src_box %p.\n",
            iface, dst, dst_x, dst_y, dst_z, src, src_box);

    vk_procs = &list->device->vk_procs;

    dst_resource = unsafe_impl_from_ID3D12Resource(dst->pResource);
    src_resource = unsafe_impl_from_ID3D12Resource(src->pResource);

    if (src->Type == D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX
            && dst->Type == D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT)
    {
        if (!(format = vkd3d_get_format(dst->u.PlacedFootprint.Footprint.Format)))
        {
            WARN("Invalid format %#x.\n", dst->u.PlacedFootprint.Footprint.Format);
            return;
        }

        if ((format->vk_aspect_mask & VK_IMAGE_ASPECT_DEPTH_BIT)
                && (format->vk_aspect_mask & VK_IMAGE_ASPECT_STENCIL_BIT))
            FIXME("Depth-stencil format %#x not fully supported yet.\n", format->dxgi_format);

        buffer_image_copy.bufferOffset = dst->u.PlacedFootprint.Offset;
        buffer_image_copy.bufferRowLength = dst->u.PlacedFootprint.Footprint.RowPitch / format->byte_count;
        buffer_image_copy.bufferImageHeight = 0;
        buffer_image_copy.imageSubresource.aspectMask = format->vk_aspect_mask;
        buffer_image_copy.imageSubresource.mipLevel = src->u.SubresourceIndex % src_resource->desc.MipLevels;
        buffer_image_copy.imageSubresource.baseArrayLayer = src->u.SubresourceIndex / src_resource->desc.MipLevels;
        buffer_image_copy.imageSubresource.layerCount = 1;
        buffer_image_copy.imageOffset.x = dst_x;
        buffer_image_copy.imageOffset.y = dst_y;
        buffer_image_copy.imageOffset.z = dst_z;
        buffer_image_copy.imageExtent.width = dst->u.PlacedFootprint.Footprint.Width;
        buffer_image_copy.imageExtent.height = dst->u.PlacedFootprint.Footprint.Height;
        buffer_image_copy.imageExtent.depth = dst->u.PlacedFootprint.Footprint.Depth;

        VK_CALL(vkCmdCopyImageToBuffer(list->vk_command_buffer,
                src_resource->u.vk_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                dst_resource->u.vk_buffer, 1, &buffer_image_copy));
    }
    else
    {
        FIXME("Copy type %#x -> %#x not implemented.\n", src->Type, dst->Type);
    }
}

static void STDMETHODCALLTYPE d3d12_command_list_CopyResource(ID3D12GraphicsCommandList *iface,
        ID3D12Resource *dst_resource, ID3D12Resource *src_resource)
{
    FIXME("iface %p, dst_resource %p, src_resource %p stub!\n", iface, dst_resource, src_resource);
}

static void STDMETHODCALLTYPE d3d12_command_list_CopyTiles(ID3D12GraphicsCommandList *iface,
        ID3D12Resource *tiled_resource, const D3D12_TILED_RESOURCE_COORDINATE *tile_region_start_coordinate,
        const D3D12_TILE_REGION_SIZE *tile_region_size, ID3D12Resource *buffer, UINT64 buffer_offset,
        D3D12_TILE_COPY_FLAGS flags)
{
    FIXME("iface %p, tiled_resource %p, tile_region_start_coordinate %p, tile_region_size %p, "
            "buffer %p, buffer_offset %#"PRIx64", flags %#x stub!\n",
            iface, tiled_resource, tile_region_start_coordinate, tile_region_size,
            buffer, buffer_offset, flags);
}

static void STDMETHODCALLTYPE d3d12_command_list_ResolveSubresource(ID3D12GraphicsCommandList *iface,
        ID3D12Resource *dst_resource, UINT dst_sub_resource,
        ID3D12Resource *src_resource, UINT src_sub_resource, DXGI_FORMAT format)
{
    FIXME("iface %p, dst_resource %p, dst_sub_resource %u, src_resource %p, src_sub_resource %u, "
            "format %#x stub!\n",
            iface, dst_resource, dst_sub_resource, src_resource, src_sub_resource, format);
}

static enum VkPrimitiveTopology vk_topology_from_d3d12_topology(D3D12_PRIMITIVE_TOPOLOGY topology)
{
    switch (topology)
    {
        case D3D_PRIMITIVE_TOPOLOGY_POINTLIST:
            return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
        case D3D_PRIMITIVE_TOPOLOGY_LINELIST:
            return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
        case D3D_PRIMITIVE_TOPOLOGY_LINESTRIP:
            return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
        case D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST:
            return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        case D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP:
            return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
        default:
            FIXME("Unhandled primitive topology %#x.\n", topology);
            return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
    }
}

static void STDMETHODCALLTYPE d3d12_command_list_IASetPrimitiveTopology(ID3D12GraphicsCommandList *iface,
        D3D12_PRIMITIVE_TOPOLOGY topology)
{
    struct d3d12_command_list *list = impl_from_ID3D12GraphicsCommandList(iface);

    TRACE("iface %p, topology %#x.\n", iface, topology);

    list->ia_desc.topology = vk_topology_from_d3d12_topology(topology);
    d3d12_command_list_invalidate_current_pipeline(list);
}

static void STDMETHODCALLTYPE d3d12_command_list_RSSetViewports(ID3D12GraphicsCommandList *iface,
        UINT viewport_count, const D3D12_VIEWPORT *viewports)
{
    struct d3d12_command_list *list = impl_from_ID3D12GraphicsCommandList(iface);
    const struct vkd3d_vk_device_procs *vk_procs;

    TRACE("iface %p, viewport_count %u, viewports %p.\n", iface, viewport_count, viewports);

    vk_procs = &list->device->vk_procs;
    VK_CALL(vkCmdSetViewport(list->vk_command_buffer, 0,
            viewport_count, (const struct VkViewport *)viewports));
}

static void STDMETHODCALLTYPE d3d12_command_list_RSSetScissorRects(ID3D12GraphicsCommandList *iface,
        UINT rect_count, const D3D12_RECT *rects)
{
    struct d3d12_command_list *list = impl_from_ID3D12GraphicsCommandList(iface);
    const struct vkd3d_vk_device_procs *vk_procs;
    struct VkRect2D *vk_rects;
    unsigned int i;

    TRACE("iface %p, rect_count %u, rects %p.\n", iface, rect_count, rects);

    if (!(vk_rects = vkd3d_calloc(rect_count, sizeof(*vk_rects))))
    {
        ERR("Failed to allocate Vulkan scissor rects.\n");
        return;
    }

    for (i = 0; i < rect_count; ++i)
    {
        vk_rects[i].offset.x = rects[i].left;
        vk_rects[i].offset.y = rects[i].top;
        vk_rects[i].extent.width = rects[i].right - rects[i].left;
        vk_rects[i].extent.height = rects[i].bottom - rects[i].top;
    }

    vk_procs = &list->device->vk_procs;
    VK_CALL(vkCmdSetScissor(list->vk_command_buffer, 0, rect_count, vk_rects));

    free(vk_rects);
}

static void STDMETHODCALLTYPE d3d12_command_list_OMSetBlendFactor(ID3D12GraphicsCommandList *iface,
        const FLOAT blend_factor[4])
{
    FIXME("iface %p, blend_factor %p stub!\n", iface, blend_factor);
}

static void STDMETHODCALLTYPE d3d12_command_list_OMSetStencilRef(ID3D12GraphicsCommandList *iface,
        UINT stencil_ref)
{
    FIXME("iface %p, stencil_ref %u stub!\n", iface, stencil_ref);
}

static void STDMETHODCALLTYPE d3d12_command_list_SetPipelineState(ID3D12GraphicsCommandList *iface,
        ID3D12PipelineState *pipeline_state)
{
    struct d3d12_pipeline_state *state = unsafe_impl_from_ID3D12PipelineState(pipeline_state);
    struct d3d12_command_list *list = impl_from_ID3D12GraphicsCommandList(iface);

    TRACE("iface %p, pipeline_state %p.\n", iface, pipeline_state);

    list->state = state;
    d3d12_command_list_invalidate_current_framebuffer(list);
    d3d12_command_list_invalidate_current_pipeline(list);
}

static void STDMETHODCALLTYPE d3d12_command_list_ResourceBarrier(ID3D12GraphicsCommandList *iface,
        UINT barrier_count, const D3D12_RESOURCE_BARRIER *barriers)
{
    FIXME("iface %p, barrier_count %u, barriers %p stub!\n",
            iface, barrier_count, barriers);
}

static void STDMETHODCALLTYPE d3d12_command_list_ExecuteBundle(ID3D12GraphicsCommandList *iface,
        ID3D12GraphicsCommandList *command_list)
{
    FIXME("iface %p, command_list %p stub!\n", iface, command_list);
}

static void STDMETHODCALLTYPE d3d12_command_list_SetDescriptorHeaps(ID3D12GraphicsCommandList *iface,
        UINT heap_count, ID3D12DescriptorHeap *const *heaps)
{
    FIXME("iface %p, heap_count %u, heaps %p stub!\n", iface, heap_count, heaps);
}

static void STDMETHODCALLTYPE d3d12_command_list_SetComputeRootSignature(ID3D12GraphicsCommandList *iface,
        ID3D12RootSignature *root_signature)
{
    FIXME("iface %p, root_signature %p stub!\n", iface, root_signature);
}

static void STDMETHODCALLTYPE d3d12_command_list_SetGraphicsRootSignature(ID3D12GraphicsCommandList *iface,
        ID3D12RootSignature *root_signature)
{
    FIXME("iface %p, root_signature %p stub!\n", iface, root_signature);
}

static void STDMETHODCALLTYPE d3d12_command_list_SetComputeRootDescriptorTable(ID3D12GraphicsCommandList *iface,
        UINT root_parameter_index, D3D12_GPU_DESCRIPTOR_HANDLE base_descriptor)
{
    FIXME("iface %p, root_parameter_index %u, base_descriptor %#"PRIx64" stub!\n",
            iface, root_parameter_index, base_descriptor.ptr);
}

static void STDMETHODCALLTYPE d3d12_command_list_SetGraphicsRootDescriptorTable(ID3D12GraphicsCommandList *iface,
        UINT root_parameter_index, D3D12_GPU_DESCRIPTOR_HANDLE base_descriptor)
{
    FIXME("iface %p, root_parameter_index %u, base_descriptor %#"PRIx64" stub!\n",
            iface, root_parameter_index, base_descriptor.ptr);
}

static void STDMETHODCALLTYPE d3d12_command_list_SetComputeRoot32BitConstant(ID3D12GraphicsCommandList *iface,
        UINT root_parameter_index, UINT data, UINT dst_offset)
{
    FIXME("iface %p, root_parameter_index %u, data 0x%08x, dst_offset %u stub!\n",
            iface, root_parameter_index, data, dst_offset);
}

static void STDMETHODCALLTYPE d3d12_command_list_SetGraphicsRoot32BitConstant(ID3D12GraphicsCommandList *iface,
        UINT root_parameter_index, UINT data, UINT dst_offset)
{
    FIXME("iface %p, root_parameter_index %u, data 0x%08x, dst_offset %u stub!\n",
            iface, root_parameter_index, data, dst_offset);
}

static void STDMETHODCALLTYPE d3d12_command_list_SetComputeRoot32BitConstants(ID3D12GraphicsCommandList *iface,
        UINT root_parameter_index, UINT constant_count, const void *data, UINT dst_offset)
{
    FIXME("iface %p, root_parameter_index %u, constant_count %u, data %p, dst_offset %u stub!\n",
            iface, root_parameter_index, constant_count, data, dst_offset);
}

static void STDMETHODCALLTYPE d3d12_command_list_SetGraphicsRoot32BitConstants(ID3D12GraphicsCommandList *iface,
        UINT root_parameter_index, UINT constant_count, const void *data, UINT dst_offset)
{
    FIXME("iface %p, root_parameter_index %u, constant_count %u, data %p, dst_offset %u stub!\n",
            iface, root_parameter_index, constant_count, data, dst_offset);
}

static void STDMETHODCALLTYPE d3d12_command_list_SetComputeRootConstantBufferView(
        ID3D12GraphicsCommandList *iface, UINT root_parameter_index, D3D12_GPU_VIRTUAL_ADDRESS address)
{
    FIXME("iface %p, root_parameter_index %u, address %#"PRIx64" stub!\n",
            iface, root_parameter_index, address);
}

static void STDMETHODCALLTYPE d3d12_command_list_SetGraphicsRootConstantBufferView(
        ID3D12GraphicsCommandList *iface, UINT root_parameter_index, D3D12_GPU_VIRTUAL_ADDRESS address)
{
    FIXME("iface %p, root_parameter_index %u, address %#"PRIx64" stub!\n",
            iface, root_parameter_index, address);
}

static void STDMETHODCALLTYPE d3d12_command_list_SetComputeRootShaderResourceView(
        ID3D12GraphicsCommandList *iface, UINT root_parameter_index, D3D12_GPU_VIRTUAL_ADDRESS address)
{
    FIXME("iface %p, root_parameter_index %u, address %#"PRIx64" stub!\n",
            iface, root_parameter_index, address);
}

static void STDMETHODCALLTYPE d3d12_command_list_SetGraphicsRootShaderResourceView(
        ID3D12GraphicsCommandList *iface, UINT root_parameter_index, D3D12_GPU_VIRTUAL_ADDRESS address)
{
    FIXME("iface %p, root_parameter_index %u, address %#"PRIx64" stub!\n",
            iface, root_parameter_index, address);
}

static void STDMETHODCALLTYPE d3d12_command_list_SetComputeRootUnorderedAccessView(
        ID3D12GraphicsCommandList *iface, UINT root_parameter_index, D3D12_GPU_VIRTUAL_ADDRESS address)
{
    FIXME("iface %p, root_parameter_index %u, address %#"PRIx64" stub!\n",
            iface, root_parameter_index, address);
}

static void STDMETHODCALLTYPE d3d12_command_list_SetGraphicsRootUnorderedAccessView(
        ID3D12GraphicsCommandList *iface, UINT root_parameter_index, D3D12_GPU_VIRTUAL_ADDRESS address)
{
    FIXME("iface %p, root_parameter_index %u, address %#"PRIx64" stub!\n",
            iface, root_parameter_index, address);
}

static void STDMETHODCALLTYPE d3d12_command_list_IASetIndexBuffer(ID3D12GraphicsCommandList *iface,
        const D3D12_INDEX_BUFFER_VIEW *view)
{
    FIXME("iface %p, view %p stub!\n", iface, view);
}

static void STDMETHODCALLTYPE d3d12_command_list_IASetVertexBuffers(ID3D12GraphicsCommandList *iface,
        UINT start_slot, UINT view_count, const D3D12_VERTEX_BUFFER_VIEW *views)
{
    struct d3d12_command_list *list = impl_from_ID3D12GraphicsCommandList(iface);
    VkDeviceSize offsets[ARRAY_SIZE(list->strides)];
    const struct vkd3d_vk_device_procs *vk_procs;
    VkBuffer buffers[ARRAY_SIZE(list->strides)];
    unsigned int i;

    TRACE("iface %p, start_slot %u, view_count %u, views %p.\n", iface, start_slot, view_count, views);

    vk_procs = &list->device->vk_procs;

    if (start_slot >= ARRAY_SIZE(list->strides) || view_count > ARRAY_SIZE(list->strides) - start_slot)
    {
        WARN("Invalid start slot %u / view count %u.\n", start_slot, view_count);
        return;
    }

    for (i = 0; i < view_count; ++i)
    {
        offsets[i] = 0;
        buffers[i] = (VkBuffer)views[i].BufferLocation;
        list->strides[start_slot + i] = views[i].StrideInBytes;
    }

    VK_CALL(vkCmdBindVertexBuffers(list->vk_command_buffer, start_slot, view_count, buffers, offsets));

    d3d12_command_list_invalidate_current_pipeline(list);
}

static void STDMETHODCALLTYPE d3d12_command_list_SOSetTargets(ID3D12GraphicsCommandList *iface,
        UINT start_slot, UINT view_count, const D3D12_STREAM_OUTPUT_BUFFER_VIEW *views)
{
    FIXME("iface %p, start_slot %u, view_count %u, views %p stub!\n", iface, start_slot, view_count, views);
}

static void STDMETHODCALLTYPE d3d12_command_list_OMSetRenderTargets(ID3D12GraphicsCommandList *iface,
        UINT render_target_descriptor_count, const D3D12_CPU_DESCRIPTOR_HANDLE *render_target_descriptors,
        BOOL single_descriptor_handle, const D3D12_CPU_DESCRIPTOR_HANDLE *depth_stencil_descriptor)
{
    struct d3d12_command_list *list = impl_from_ID3D12GraphicsCommandList(iface);
    unsigned int i;

    TRACE("iface %p, render_target_descriptor_count %u, render_target_descriptors %p, "
            "single_descriptor_handle %#x, depth_stencil_descriptor %p.\n",
            iface, render_target_descriptor_count, render_target_descriptors,
            single_descriptor_handle, depth_stencil_descriptor);

    if (depth_stencil_descriptor)
        FIXME("Ignoring depth/stencil descriptor %p.\n", depth_stencil_descriptor);

    if (render_target_descriptor_count > ARRAY_SIZE(list->views))
    {
        WARN("Descriptor count %u > %zu, ignoring extra descriptors.\n",
                render_target_descriptor_count, ARRAY_SIZE(list->views));
        render_target_descriptor_count = ARRAY_SIZE(list->views);
    }

    list->fb_width = 0;
    list->fb_height = 0;
    for (i = 0; i < render_target_descriptor_count; ++i)
    {
        const struct d3d12_rtv_desc *rtv_desc = (const struct d3d12_rtv_desc *)render_target_descriptors[i].ptr;

        list->views[i] = rtv_desc->vk_view;
        if (rtv_desc->width > list->fb_width)
            list->fb_width = rtv_desc->width;
        if (rtv_desc->height > list->fb_height)
            list->fb_height = rtv_desc->height;
    }

    d3d12_command_list_invalidate_current_framebuffer(list);
}

static void STDMETHODCALLTYPE d3d12_command_list_ClearDepthStencilView(ID3D12GraphicsCommandList *iface,
        D3D12_CPU_DESCRIPTOR_HANDLE dsv, D3D12_CLEAR_FLAGS flags, FLOAT depth, UINT8 stencil,
        UINT rect_count, const D3D12_RECT *rects)
{
    FIXME("iface %p, dsv %#lx, flags %#x, depth %.8e, stencil 0x%02x, rect_count %u, rects %p stub!\n",
            iface, dsv.ptr, flags, depth, stencil, rect_count, rects);
}

static void STDMETHODCALLTYPE d3d12_command_list_ClearRenderTargetView(ID3D12GraphicsCommandList *iface,
        D3D12_CPU_DESCRIPTOR_HANDLE rtv, const FLOAT color[4], UINT rect_count, const D3D12_RECT *rects)
{
    const union VkClearValue clear_value = {{{color[0], color[1], color[2], color[3]}}};
    struct d3d12_command_list *list = impl_from_ID3D12GraphicsCommandList(iface);
    struct d3d12_rtv_desc *rtv_desc = (struct d3d12_rtv_desc *)rtv.ptr;
    struct VkAttachmentDescription attachment_desc;
    const struct vkd3d_vk_device_procs *vk_procs;
    struct VkAttachmentReference color_reference;
    struct VkSubpassDescription sub_pass_desc;
    struct VkRenderPassCreateInfo pass_desc;
    struct VkRenderPassBeginInfo begin_desc;
    struct VkFramebufferCreateInfo fb_desc;
    VkFramebuffer vk_framebuffer;
    VkRenderPass vk_render_pass;
    D3D12_RECT full_rect;
    unsigned int i;
    VkResult vr;

    TRACE("iface %p, rtv %#lx, color %p, rect_count %u, rects %p.\n",
            iface, rtv.ptr, color, rect_count, rects);

    vk_procs = &list->device->vk_procs;

    if (!rect_count)
    {
        full_rect.top = 0;
        full_rect.left = 0;
        full_rect.bottom = rtv_desc->height;
        full_rect.right = rtv_desc->width;

        rect_count = 1;
        rects = &full_rect;
    }

    attachment_desc.flags = 0;
    attachment_desc.format = rtv_desc->format;
    attachment_desc.samples = VK_SAMPLE_COUNT_1_BIT;
    attachment_desc.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachment_desc.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachment_desc.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachment_desc.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachment_desc.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    attachment_desc.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    color_reference.attachment = 0;
    color_reference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    sub_pass_desc.flags = 0;
    sub_pass_desc.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub_pass_desc.inputAttachmentCount = 0;
    sub_pass_desc.pInputAttachments = NULL;
    sub_pass_desc.colorAttachmentCount = 1;
    sub_pass_desc.pColorAttachments = &color_reference;
    sub_pass_desc.pResolveAttachments = NULL;
    sub_pass_desc.pDepthStencilAttachment = NULL;
    sub_pass_desc.preserveAttachmentCount = 0;
    sub_pass_desc.pPreserveAttachments = NULL;

    pass_desc.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    pass_desc.pNext = NULL;
    pass_desc.flags = 0;
    pass_desc.attachmentCount = 1;
    pass_desc.pAttachments = &attachment_desc;
    pass_desc.subpassCount = 1;
    pass_desc.pSubpasses = &sub_pass_desc;
    pass_desc.dependencyCount = 0;
    pass_desc.pDependencies = NULL;
    if ((vr = VK_CALL(vkCreateRenderPass(list->device->vk_device, &pass_desc, NULL, &vk_render_pass))) < 0)
    {
        WARN("Failed to create Vulkan render pass, vr %d.\n", vr);
        return;
    }

    if (!d3d12_command_allocator_add_render_pass(list->allocator, vk_render_pass))
    {
        WARN("Failed to add render pass,\n");
        VK_CALL(vkDestroyRenderPass(list->device->vk_device, vk_render_pass, NULL));
        return;
    }

    fb_desc.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fb_desc.pNext = NULL;
    fb_desc.flags = 0;
    fb_desc.renderPass = vk_render_pass;
    fb_desc.attachmentCount = 1;
    fb_desc.pAttachments = &rtv_desc->vk_view;
    fb_desc.width = rtv_desc->width;
    fb_desc.height = rtv_desc->height;
    fb_desc.layers = 1;
    if ((vr = VK_CALL(vkCreateFramebuffer(list->device->vk_device, &fb_desc, NULL, &vk_framebuffer))) < 0)
    {
        WARN("Failed to create Vulkan framebuffer, vr %d.\n", vr);
        return;
    }

    if (!d3d12_command_allocator_add_framebuffer(list->allocator, vk_framebuffer))
    {
        WARN("Failed to add framebuffer.\n");
        VK_CALL(vkDestroyFramebuffer(list->device->vk_device, vk_framebuffer, NULL));
        return;
    }

    begin_desc.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    begin_desc.pNext = NULL;
    begin_desc.renderPass = vk_render_pass;
    begin_desc.framebuffer = vk_framebuffer;
    begin_desc.clearValueCount = 1;
    begin_desc.pClearValues = &clear_value;

    for (i = 0; i < rect_count; ++i)
    {
        begin_desc.renderArea.offset.x = rects[i].left;
        begin_desc.renderArea.offset.y = rects[i].top;
        begin_desc.renderArea.extent.width = rects[i].right - rects[i].left;
        begin_desc.renderArea.extent.height = rects[i].bottom - rects[i].top;
        VK_CALL(vkCmdBeginRenderPass(list->vk_command_buffer, &begin_desc, VK_SUBPASS_CONTENTS_INLINE));
        VK_CALL(vkCmdEndRenderPass(list->vk_command_buffer));
    }
}

static void STDMETHODCALLTYPE d3d12_command_list_ClearUnorderedAccessViewFloat(ID3D12GraphicsCommandList *iface,
        D3D12_GPU_DESCRIPTOR_HANDLE gpu_handle, D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle,
        ID3D12Resource *resource, UINT rect_count, const D3D12_RECT *rects)
{
    FIXME("iface %p, gpu_handle %#"PRIx64", cpu_handle %lx, resource %p, rect_count %u, rects %p stub!\n",
            iface, gpu_handle.ptr, cpu_handle.ptr, resource, rect_count, rects);
}

static void STDMETHODCALLTYPE d3d12_command_list_DiscardResource(ID3D12GraphicsCommandList *iface,
        ID3D12Resource *resource, const D3D12_DISCARD_REGION *region)
{
    FIXME("iface %p, resource %p, region %p stub!\n", iface, resource, region);
}

static void STDMETHODCALLTYPE d3d12_command_list_BeginQuery(ID3D12GraphicsCommandList *iface,
        ID3D12QueryHeap *heap, D3D12_QUERY_TYPE type, UINT index)
{
    FIXME("iface %p, heap %p, type %#x, index %u stub!\n", iface, heap, type, index);
}

static void STDMETHODCALLTYPE d3d12_command_list_EndQuery(ID3D12GraphicsCommandList *iface,
        ID3D12QueryHeap *heap, D3D12_QUERY_TYPE type, UINT index)
{
    FIXME("iface %p, heap %p, type %#x, index %u stub!\n", iface, heap, type, index);
}

static void STDMETHODCALLTYPE d3d12_command_list_ResolveQueryData(ID3D12GraphicsCommandList *iface,
        ID3D12QueryHeap *heap, D3D12_QUERY_TYPE type, UINT start_index, UINT query_count,
        ID3D12Resource *dst_buffer, UINT64 aligned_dst_buffer_offset)
{
    FIXME("iface %p, heap %p, type %#x, start_index %u, query_count %u, "
            "dst_buffer %p, aligned_dst_buffer_offset %#"PRIx64" stub!\n",
            iface, heap, type, start_index, query_count,
            dst_buffer, aligned_dst_buffer_offset);
}

static void STDMETHODCALLTYPE d3d12_command_list_SetPredication(ID3D12GraphicsCommandList *iface,
        ID3D12Resource *buffer, UINT64 aligned_buffer_offset, D3D12_PREDICATION_OP operation)
{
    FIXME("iface %p, buffer %p, aligned_buffer_offset %#"PRIx64", operation %#x stub!\n",
            iface, buffer, aligned_buffer_offset, operation);
}

static void STDMETHODCALLTYPE d3d12_command_list_SetMarker(ID3D12GraphicsCommandList *iface,
        UINT metadata, const void *data, UINT size)
{
    FIXME("iface %p, metadata %#x, data %p, size %u stub!\n", iface, metadata, data, size);
}

static void STDMETHODCALLTYPE d3d12_command_list_BeginEvent(ID3D12GraphicsCommandList *iface,
        UINT metadata, const void *data, UINT size)
{
    FIXME("iface %p, metadata %#x, data %p, size %u stub!\n", iface, metadata, data, size);
}

static void STDMETHODCALLTYPE d3d12_command_list_EndEvent(ID3D12GraphicsCommandList *iface)
{
    FIXME("iface %p stub!\n", iface);
}

static void STDMETHODCALLTYPE d3d12_command_list_ExecuteIndirect(ID3D12GraphicsCommandList *iface,
        ID3D12CommandSignature *command_signature,
        UINT max_command_count, ID3D12Resource *arg_buffer,
        UINT64 arg_buffer_offset, ID3D12Resource *count_buffer, UINT64 count_buffer_offset)
{
    FIXME("iface %p, command_signature %p, max_command_count %u, arg_buffer %p, "
            "arg_buffer_offset %#"PRIx64", count_buffer %p, count_buffer_offset %#"PRIx64" stub!\n",
            iface, command_signature, max_command_count, arg_buffer, arg_buffer_offset,
            count_buffer, count_buffer_offset);
}

static const struct ID3D12GraphicsCommandListVtbl d3d12_command_list_vtbl =
{
    /* IUnknown methods */
    d3d12_command_list_QueryInterface,
    d3d12_command_list_AddRef,
    d3d12_command_list_Release,
    /* ID3D12Object methods */
    d3d12_command_list_GetPrivateData,
    d3d12_command_list_SetPrivateData,
    d3d12_command_list_SetPrivateDataInterface,
    d3d12_command_list_SetName,
    /* ID3D12DeviceChild methods */
    d3d12_command_list_GetDevice,
    /* ID3D12CommandList methods */
    d3d12_command_list_GetType,
    /* ID3D12GraphicsCommandList methods */
    d3d12_command_list_Close,
    d3d12_command_list_Reset,
    d3d12_command_list_ClearState,
    d3d12_command_list_DrawInstanced,
    d3d12_command_list_DrawIndexedInstanced,
    d3d12_command_list_Dispatch,
    d3d12_command_list_CopyBufferRegion,
    d3d12_command_list_CopyTextureRegion,
    d3d12_command_list_CopyResource,
    d3d12_command_list_CopyTiles,
    d3d12_command_list_ResolveSubresource,
    d3d12_command_list_IASetPrimitiveTopology,
    d3d12_command_list_RSSetViewports,
    d3d12_command_list_RSSetScissorRects,
    d3d12_command_list_OMSetBlendFactor,
    d3d12_command_list_OMSetStencilRef,
    d3d12_command_list_SetPipelineState,
    d3d12_command_list_ResourceBarrier,
    d3d12_command_list_ExecuteBundle,
    d3d12_command_list_SetDescriptorHeaps,
    d3d12_command_list_SetComputeRootSignature,
    d3d12_command_list_SetGraphicsRootSignature,
    d3d12_command_list_SetComputeRootDescriptorTable,
    d3d12_command_list_SetGraphicsRootDescriptorTable,
    d3d12_command_list_SetComputeRoot32BitConstant,
    d3d12_command_list_SetGraphicsRoot32BitConstant,
    d3d12_command_list_SetComputeRoot32BitConstants,
    d3d12_command_list_SetGraphicsRoot32BitConstants,
    d3d12_command_list_SetComputeRootConstantBufferView,
    d3d12_command_list_SetGraphicsRootConstantBufferView,
    d3d12_command_list_SetComputeRootShaderResourceView,
    d3d12_command_list_SetGraphicsRootShaderResourceView,
    d3d12_command_list_SetComputeRootUnorderedAccessView,
    d3d12_command_list_SetGraphicsRootUnorderedAccessView,
    d3d12_command_list_IASetIndexBuffer,
    d3d12_command_list_IASetVertexBuffers,
    d3d12_command_list_SOSetTargets,
    d3d12_command_list_OMSetRenderTargets,
    d3d12_command_list_ClearDepthStencilView,
    d3d12_command_list_ClearRenderTargetView,
    d3d12_command_list_ClearUnorderedAccessViewFloat,
    d3d12_command_list_DiscardResource,
    d3d12_command_list_BeginQuery,
    d3d12_command_list_EndQuery,
    d3d12_command_list_ResolveQueryData,
    d3d12_command_list_SetPredication,
    d3d12_command_list_SetMarker,
    d3d12_command_list_BeginEvent,
    d3d12_command_list_EndEvent,
    d3d12_command_list_ExecuteIndirect,
};

static struct d3d12_command_list *unsafe_impl_from_ID3D12CommandList(ID3D12CommandList *iface)
{
    if (!iface)
        return NULL;
    assert(iface->lpVtbl == (struct ID3D12CommandListVtbl *)&d3d12_command_list_vtbl);
    return CONTAINING_RECORD(iface, struct d3d12_command_list, ID3D12GraphicsCommandList_iface);
}

static HRESULT d3d12_command_list_init(struct d3d12_command_list *list, struct d3d12_device *device,
        D3D12_COMMAND_LIST_TYPE type, struct d3d12_command_allocator *allocator,
        ID3D12PipelineState *initial_pipeline_state)
{
    HRESULT hr;

    list->ID3D12GraphicsCommandList_iface.lpVtbl = &d3d12_command_list_vtbl;
    list->refcount = 1;

    list->type = type;
    list->device = device;
    ID3D12Device_AddRef(&device->ID3D12Device_iface);

    list->allocator = allocator;
    list->pipeline_state = initial_pipeline_state;

    if (FAILED(hr = vkd3d_command_allocator_allocate_command_list(allocator, list)))
    {
        ID3D12Device_Release(&device->ID3D12Device_iface);
        return hr;
    }

    memset(list->strides, 0, sizeof(list->strides));
    list->ia_desc.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    list->ia_desc.pNext = NULL;
    list->ia_desc.flags = 0;
    list->ia_desc.topology = VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
    list->ia_desc.primitiveRestartEnable = VK_FALSE;

    memset(list->views, 0, sizeof(list->views));
    list->fb_width = 0;
    list->fb_height = 0;

    list->current_framebuffer = VK_NULL_HANDLE;
    list->current_pipeline = VK_NULL_HANDLE;

    return S_OK;
}

HRESULT d3d12_command_list_create(struct d3d12_device *device,
        UINT node_mask, D3D12_COMMAND_LIST_TYPE type, ID3D12CommandAllocator *allocator_iface,
        ID3D12PipelineState *initial_pipeline_state, struct d3d12_command_list **list)
{
    struct d3d12_command_allocator *allocator;
    struct d3d12_command_list *object;
    HRESULT hr;

    if (!(allocator = unsafe_impl_from_ID3D12CommandAllocator(allocator_iface)))
    {
        WARN("Command allocator is NULL.\n");
        return E_INVALIDARG;
    }

    if (allocator->type != type)
    {
        WARN("Command list types do not match (allocator %#x, list %#x).\n",
                allocator->type, type);
        return E_INVALIDARG;
    }

    if (node_mask && node_mask != 1)
        FIXME("Multi-adapter not supported.\n");

    if (!(object = vkd3d_malloc(sizeof(*object))))
        return E_OUTOFMEMORY;

    if (FAILED(hr = d3d12_command_list_init(object, device, type, allocator, initial_pipeline_state)))
    {
        vkd3d_free(object);
        return hr;
    }

    TRACE("Created command list %p.\n", object);

    *list = object;

    return S_OK;
}

/* ID3D12CommandQueue */
static inline struct d3d12_command_queue *impl_from_ID3D12CommandQueue(ID3D12CommandQueue *iface)
{
    return CONTAINING_RECORD(iface, struct d3d12_command_queue, ID3D12CommandQueue_iface);
}

static HRESULT STDMETHODCALLTYPE d3d12_command_queue_QueryInterface(ID3D12CommandQueue *iface,
        REFIID riid, void **object)
{
    TRACE("iface %p, riid %s, object %p.\n", iface, debugstr_guid(riid), object);

    if (IsEqualGUID(riid, &IID_ID3D12CommandQueue)
            || IsEqualGUID(riid, &IID_ID3D12Pageable)
            || IsEqualGUID(riid, &IID_ID3D12DeviceChild)
            || IsEqualGUID(riid, &IID_ID3D12Object)
            || IsEqualGUID(riid, &IID_IUnknown))
    {
        ID3D12CommandQueue_AddRef(iface);
        *object = iface;
        return S_OK;
    }

    WARN("%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid(riid));

    *object = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE d3d12_command_queue_AddRef(ID3D12CommandQueue *iface)
{
    struct d3d12_command_queue *command_queue = impl_from_ID3D12CommandQueue(iface);
    ULONG refcount = InterlockedIncrement(&command_queue->refcount);

    TRACE("%p increasing refcount to %u.\n", command_queue, refcount);

    return refcount;
}

static ULONG STDMETHODCALLTYPE d3d12_command_queue_Release(ID3D12CommandQueue *iface)
{
    struct d3d12_command_queue *command_queue = impl_from_ID3D12CommandQueue(iface);
    ULONG refcount = InterlockedDecrement(&command_queue->refcount);

    TRACE("%p decreasing refcount to %u.\n", command_queue, refcount);

    if (!refcount)
    {
        struct d3d12_device *device = command_queue->device;

        vkd3d_free(command_queue);

        ID3D12Device_Release(&device->ID3D12Device_iface);
    }

    return refcount;
}

static HRESULT STDMETHODCALLTYPE d3d12_command_queue_GetPrivateData(ID3D12CommandQueue *iface,
        REFGUID guid, UINT *data_size, void *data)
{
    FIXME("iface %p, guid %s, data_size %p, data %p stub!", iface, debugstr_guid(guid), data_size, data);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d12_command_queue_SetPrivateData(ID3D12CommandQueue *iface,
        REFGUID guid, UINT data_size, const void *data)
{
    FIXME("iface %p, guid %s, data_size %u, data %p stub!\n", iface, debugstr_guid(guid), data_size, data);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d12_command_queue_SetPrivateDataInterface(ID3D12CommandQueue *iface,
        REFGUID guid, const IUnknown *data)
{
    FIXME("iface %p, guid %s, data %p stub!\n", iface, debugstr_guid(guid), data);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d12_command_queue_SetName(ID3D12CommandQueue *iface, const WCHAR *name)
{
    FIXME("iface %p, name %s stub!\n", iface, debugstr_w(name));

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d12_command_queue_GetDevice(ID3D12CommandQueue *iface,
        REFIID riid, void **device)
{
    struct d3d12_command_queue *command_queue = impl_from_ID3D12CommandQueue(iface);

    TRACE("iface %p, riid %s, device %p.\n", iface, debugstr_guid(riid), device);

    return ID3D12Device_QueryInterface(&command_queue->device->ID3D12Device_iface, riid, device);
}

static void STDMETHODCALLTYPE d3d12_command_queue_UpdateTileMappings(ID3D12CommandQueue *iface,
        ID3D12Resource *resource, UINT region_count,
        const D3D12_TILED_RESOURCE_COORDINATE *region_start_coordinates,
        const D3D12_TILE_REGION_SIZE *region_sizes,
        UINT range_count,
        const D3D12_TILE_RANGE_FLAGS *range_flags,
        UINT *heap_range_offsets,
        UINT *range_tile_counts,
        D3D12_TILE_MAPPING_FLAGS flags)
{
    FIXME("iface %p, resource %p, region_count %u, region_start_coordinates %p, "
            "region_sizes %p, range_count %u, range_flags %p, heap_range_offsets %p, "
            "range_tile_counts %p, flags %#x stub!\n",
            iface, resource, region_count, region_start_coordinates, region_sizes, range_count,
            range_flags, heap_range_offsets, range_tile_counts, flags);
}

static void STDMETHODCALLTYPE d3d12_command_queue_CopyTileMappings(ID3D12CommandQueue *iface,
        ID3D12Resource *dst_resource,
        const D3D12_TILED_RESOURCE_COORDINATE *dst_region_start_coordinate,
        ID3D12Resource *src_resource,
        const D3D12_TILED_RESOURCE_COORDINATE *src_region_start_coordinate,
        const D3D12_TILE_REGION_SIZE *region_size,
        D3D12_TILE_MAPPING_FLAGS flags)
{
    FIXME("iface %p, dst_resource %p, dst_region_start_coordinate %p, "
            "src_resource %p, src_region_start_coordinate %p, region_size %p, flags %#x stub!\n",
            iface, dst_resource, dst_region_start_coordinate, src_resource,
            src_region_start_coordinate, region_size, flags);
}

static void STDMETHODCALLTYPE d3d12_command_queue_ExecuteCommandLists(ID3D12CommandQueue *iface,
        UINT command_list_count, ID3D12CommandList * const *command_lists)
{
    struct d3d12_command_queue *command_queue = impl_from_ID3D12CommandQueue(iface);
    const struct vkd3d_vk_device_procs *vk_procs;
    struct VkSubmitInfo submit_desc;
    VkCommandBuffer *buffers;
    unsigned int i;
    VkResult vr;

    TRACE("iface %p, command_list_count %u, command_lists %p.\n",
            iface, command_list_count, command_lists);

    vk_procs = &command_queue->device->vk_procs;

    if (!(buffers = vkd3d_calloc(command_list_count, sizeof(*buffers))))
    {
        ERR("Failed to allocate command buffer array.\n");
        return;
    }

    for (i = 0; i < command_list_count; ++i)
    {
        buffers[i] = unsafe_impl_from_ID3D12CommandList(command_lists[i])->vk_command_buffer;
    }

    submit_desc.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_desc.pNext = NULL;
    submit_desc.waitSemaphoreCount = 0;
    submit_desc.pWaitSemaphores = NULL;
    submit_desc.pWaitDstStageMask = NULL;
    submit_desc.commandBufferCount = command_list_count;
    submit_desc.pCommandBuffers = buffers;
    submit_desc.signalSemaphoreCount = 0;
    submit_desc.pSignalSemaphores = NULL;

    if ((vr = VK_CALL(vkQueueSubmit(command_queue->vk_queue,
            1, &submit_desc, VK_NULL_HANDLE))) < 0)
        ERR("Failed to submit queue(s), vr %d.\n", vr);

    vkd3d_free(buffers);
}

static void STDMETHODCALLTYPE d3d12_command_queue_SetMarker(ID3D12CommandQueue *iface,
        UINT metadata, const void *data, UINT size)
{
    FIXME("iface %p, metadata %#x, data %p, size %u stub!\n",
            iface, metadata, data, size);
}

static void STDMETHODCALLTYPE d3d12_command_queue_BeginEvent(ID3D12CommandQueue *iface,
        UINT metadata, const void *data, UINT size)
{
    FIXME("iface %p, metatdata %#x, data %p, size %u stub!\n",
            iface, metadata, data, size);
}

static void STDMETHODCALLTYPE d3d12_command_queue_EndEvent(ID3D12CommandQueue *iface)
{
    FIXME("iface %p stub!\n", iface);
}

static HRESULT STDMETHODCALLTYPE d3d12_command_queue_Signal(ID3D12CommandQueue *iface,
        ID3D12Fence *fence, UINT64 value)
{
    struct d3d12_command_queue *command_queue = impl_from_ID3D12CommandQueue(iface);
    const struct vkd3d_vk_device_procs *vk_procs;
    VkFenceCreateInfo fence_info;
    struct d3d12_device *device;
    VkFence vk_fence;
    VkResult vr;

    TRACE("iface %p, fence %p, value %#"PRIx64".\n", iface, fence, value);

    device = command_queue->device;
    vk_procs = &device->vk_procs;

    fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fence_info.pNext = NULL;
    fence_info.flags = 0;

    /* XXX: It may be a good idea to re-use Vulkan fences. */
    if ((vr = VK_CALL(vkCreateFence(device->vk_device, &fence_info, NULL, &vk_fence))) < 0)
    {
        WARN("Failed to create Vulkan fence, vr %d.\n", vr);
        return hresult_from_vk_result(vr);
    }

    if ((vr = VK_CALL(vkQueueSubmit(command_queue->vk_queue, 0, NULL, vk_fence))) < 0)
    {
        WARN("Failed to submit fence, vr %d.\n", vr);
        VK_CALL(vkDestroyFence(device->vk_device, vk_fence, NULL));
        return hresult_from_vk_result(vr);
    }

    vr = VK_CALL(vkGetFenceStatus(device->vk_device, vk_fence));
    if (vr == VK_SUCCESS)
    {
        VK_CALL(vkDestroyFence(device->vk_device, vk_fence, NULL));
        return d3d12_fence_Signal(fence, value);
    }
    if (vr != VK_NOT_READY)
    {
        WARN("Failed to get fence status, vr %d.\n", vr);
        VK_CALL(vkDestroyFence(device->vk_device, vk_fence, NULL));
        return hresult_from_vk_result(vr);
    }

    return vkd3d_queue_gpu_fence(&device->fence_worker, vk_fence, fence, value);
}

static HRESULT STDMETHODCALLTYPE d3d12_command_queue_Wait(ID3D12CommandQueue *iface,
        ID3D12Fence *fence, UINT64 value)
{
    FIXME("iface %p, fence %p, value %#"PRIx64" stub!\n", iface, fence, value);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d12_command_queue_GetTimestampFrequency(ID3D12CommandQueue *iface,
        UINT64 *frequency)
{
    FIXME("iface %p, frequency %p stub!\n", iface, frequency);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d12_command_queue_GetClockCalibration(ID3D12CommandQueue *iface,
        UINT64 *gpu_timestamp, UINT64 *cpu_timestamp)
{
    FIXME("iface %p, gpu_timestamp %p, cpu_timestamp %p stub!\n",
            iface, gpu_timestamp, cpu_timestamp);

    return E_NOTIMPL;
}

static D3D12_COMMAND_QUEUE_DESC * STDMETHODCALLTYPE d3d12_command_queue_GetDesc(ID3D12CommandQueue *iface,
        D3D12_COMMAND_QUEUE_DESC *desc)
{
    struct d3d12_command_queue *command_queue = impl_from_ID3D12CommandQueue(iface);

    TRACE("iface %p, desc %p.\n", iface, desc);

    *desc = command_queue->desc;
    return desc;
}

static const struct ID3D12CommandQueueVtbl d3d12_command_queue_vtbl =
{
    /* IUnknown methods */
    d3d12_command_queue_QueryInterface,
    d3d12_command_queue_AddRef,
    d3d12_command_queue_Release,
    /* ID3D12Object methods */
    d3d12_command_queue_GetPrivateData,
    d3d12_command_queue_SetPrivateData,
    d3d12_command_queue_SetPrivateDataInterface,
    d3d12_command_queue_SetName,
    /* ID3D12DeviceChild methods */
    d3d12_command_queue_GetDevice,
    /* ID3D12CommandQueue methods */
    d3d12_command_queue_UpdateTileMappings,
    d3d12_command_queue_CopyTileMappings,
    d3d12_command_queue_ExecuteCommandLists,
    d3d12_command_queue_SetMarker,
    d3d12_command_queue_BeginEvent,
    d3d12_command_queue_EndEvent,
    d3d12_command_queue_Signal,
    d3d12_command_queue_Wait,
    d3d12_command_queue_GetTimestampFrequency,
    d3d12_command_queue_GetClockCalibration,
    d3d12_command_queue_GetDesc,
};

static HRESULT d3d12_command_queue_init(struct d3d12_command_queue *queue,
        struct d3d12_device *device, const D3D12_COMMAND_QUEUE_DESC *desc)
{
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    unsigned int queue_family_index;

    queue->ID3D12CommandQueue_iface.lpVtbl = &d3d12_command_queue_vtbl;
    queue->refcount = 1;

    queue->desc = *desc;
    if (!queue->desc.NodeMask)
        queue->desc.NodeMask = 0x1;

    switch (desc->Type)
    {
        case D3D12_COMMAND_LIST_TYPE_DIRECT:
            queue_family_index = device->direct_queue_family_index;
            break;
        case D3D12_COMMAND_LIST_TYPE_COPY:
            queue_family_index = device->copy_queue_family_index;
            break;
        default:
            FIXME("Unhandled command list type %#x.\n", desc->Type);
            return E_NOTIMPL;
    }

    if (desc->Priority)
        FIXME("Ignoring priority %#x.\n", desc->Priority);
    if (desc->Flags)
        FIXME("Ignoring flags %#x.\n", desc->Flags);

    /* FIXME: Access to VkQueue must be externally synchronized. */
    VK_CALL(vkGetDeviceQueue(device->vk_device, queue_family_index, 0, &queue->vk_queue));

    queue->device = device;
    ID3D12Device_AddRef(&device->ID3D12Device_iface);

    return S_OK;
}

HRESULT d3d12_command_queue_create(struct d3d12_device *device,
        const D3D12_COMMAND_QUEUE_DESC *desc, struct d3d12_command_queue **queue)
{
    struct d3d12_command_queue *object;
    HRESULT hr;

    if (!(object = vkd3d_malloc(sizeof(*object))))
        return E_OUTOFMEMORY;

    if (FAILED(hr = d3d12_command_queue_init(object, device, desc)))
    {
        vkd3d_free(object);
        return hr;
    }

    TRACE("Created command queue %p.\n", object);

    *queue = object;

    return S_OK;
}
