/*
 * Copyright 2016 Józef Kucia for CodeWeavers
 * Copyright 2016 Henri Verbeet for CodeWeavers
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
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

static void vkd3d_fence_worker_remove_fence(struct vkd3d_fence_worker *worker, ID3D12Fence *fence)
{
    struct d3d12_device *device = worker->device;
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    unsigned int i, j;
    int rc;

    if ((rc = pthread_mutex_lock(&worker->mutex)))
    {
        ERR("Failed to lock mutex, error %d.\n", rc);
        return;
    }

    for (i = 0, j = 0; i < worker->fence_count; ++i)
    {
        if (worker->fences[i].fence == fence)
        {
            VK_CALL(vkDestroyFence(device->vk_device, worker->vk_fences[i], NULL));
            continue;
        }

        if (i != j)
        {
            worker->vk_fences[j] = worker->vk_fences[i];
            worker->fences[j] = worker->fences[i];
        }
        ++j;
    }
    worker->fence_count = j;

    pthread_mutex_unlock(&worker->mutex);
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

HRESULT vkd3d_fence_worker_start(struct vkd3d_fence_worker *worker,
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

HRESULT vkd3d_fence_worker_stop(struct vkd3d_fence_worker *worker)
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

        vkd3d_fence_worker_remove_fence(&device->fence_worker, iface);

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
static HRESULT d3d12_command_list_begin_command_buffer(struct d3d12_command_list *list)
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

    list->is_recording = true;
    list->is_valid = true;

    ID3D12GraphicsCommandList_SetPipelineState(&list->ID3D12GraphicsCommandList_iface, list->pipeline_state);

    return S_OK;
}

static HRESULT d3d12_command_allocator_allocate_command_buffer(struct d3d12_command_allocator *allocator,
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
        WARN("Command allocator is already in use.\n");
        return E_INVALIDARG;
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

    if (FAILED(hr = d3d12_command_list_begin_command_buffer(list)))
    {
        VK_CALL(vkFreeCommandBuffers(device->vk_device, allocator->vk_command_pool,
                1, &list->vk_command_buffer));
        return hr;
    }

    allocator->current_command_list = list;

    return S_OK;
}

static void d3d12_command_allocator_free_command_buffer(struct d3d12_command_allocator *allocator,
        struct d3d12_command_list *list)
{
    struct d3d12_device *device = allocator->device;
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;

    TRACE("allocator %p, list %p.\n", allocator, list);

    if (allocator->current_command_list == list)
        allocator->current_command_list = NULL;

    if (!vkd3d_array_reserve((void **)&allocator->command_buffers, &allocator->command_buffers_size,
            allocator->command_buffer_count + 1, sizeof(*allocator->command_buffers)))
    {
        WARN("Failed to add command buffer.\n");
        VK_CALL(vkFreeCommandBuffers(device->vk_device, allocator->vk_command_pool,
                1, &list->vk_command_buffer));
        return;
    }

    allocator->command_buffers[allocator->command_buffer_count++] = list->vk_command_buffer;
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

static bool d3d12_command_allocator_add_descriptor_pool(struct d3d12_command_allocator *allocator,
        VkDescriptorPool pool)
{
    if (!vkd3d_array_reserve((void **)&allocator->descriptor_pools, &allocator->descriptor_pools_size,
            allocator->descriptor_pool_count + 1, sizeof(*allocator->descriptor_pools)))
        return false;

    allocator->descriptor_pools[allocator->descriptor_pool_count++] = pool;

    return true;
}

static bool d3d12_command_allocator_add_buffer_view(struct d3d12_command_allocator *allocator,
        VkBufferView view)
{
    if (!vkd3d_array_reserve((void **)&allocator->buffer_views, &allocator->buffer_views_size,
            allocator->buffer_view_count + 1, sizeof(*allocator->buffer_views)))
        return false;

    allocator->buffer_views[allocator->buffer_view_count++] = view;

    return true;
}

static void d3d12_command_list_allocator_destroyed(struct d3d12_command_list *list)
{
    TRACE("list %p.\n", list);

    list->allocator = NULL;
    list->vk_command_buffer = VK_NULL_HANDLE;
}

static void d3d12_command_allocator_free_resources(struct d3d12_command_allocator *allocator)
{
    struct d3d12_device *device = allocator->device;
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    unsigned int i;

    for (i = 0; i < allocator->buffer_view_count; ++i)
    {
        VK_CALL(vkDestroyBufferView(device->vk_device, allocator->buffer_views[i], NULL));
    }
    allocator->buffer_view_count = 0;

    for (i = 0; i < allocator->descriptor_pool_count; ++i)
    {
        VK_CALL(vkDestroyDescriptorPool(device->vk_device, allocator->descriptor_pools[i], NULL));
    }
    allocator->descriptor_pool_count = 0;

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

        if (allocator->current_command_list)
            d3d12_command_list_allocator_destroyed(allocator->current_command_list);

        d3d12_command_allocator_free_resources(allocator);
        vkd3d_free(allocator->buffer_views);
        vkd3d_free(allocator->descriptor_pools);
        vkd3d_free(allocator->pipelines);
        vkd3d_free(allocator->framebuffers);
        vkd3d_free(allocator->passes);

        /* All command buffers are implicitly freed when a pool is destroyed. */
        vkd3d_free(allocator->command_buffers);
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

    d3d12_command_allocator_free_resources(allocator);
    if (allocator->command_buffer_count)
    {
        VK_CALL(vkFreeCommandBuffers(device->vk_device, allocator->vk_command_pool,
                allocator->command_buffer_count, allocator->command_buffers));
        allocator->command_buffer_count = 0;
    }

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
        case D3D12_COMMAND_LIST_TYPE_COMPUTE:
            command_pool_info.queueFamilyIndex = device->compute_queue_family_index;
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

    allocator->descriptor_pools = NULL;
    allocator->descriptor_pools_size = 0;
    allocator->descriptor_pool_count = 0;

    allocator->buffer_views = NULL;
    allocator->buffer_views_size = 0;
    allocator->buffer_view_count = 0;

    allocator->command_buffers = NULL;
    allocator->command_buffers_size = 0;
    allocator->command_buffer_count = 0;

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

static bool vk_barrier_parameters_from_d3d12_resource_state(unsigned int state, bool is_swapchain_image,
        VkAccessFlags *access_mask, VkPipelineStageFlags *stage_flags, VkImageLayout *image_layout)
{
    switch (state)
    {
        case D3D12_RESOURCE_STATE_COMMON: /* D3D12_RESOURCE_STATE_PRESENT */
            /* The COMMON state is used for ownership transfer between
             * DIRECT/COMPUTE and COPY queues. Additionally, a texture has to
             * be in the COMMON state to be accessed by CPU. Moreover,
             * resources can be implicitly promoted to other states out of the
             * COMMON state, and the resource state can decay to the COMMON
             * state when GPU finishes execution of a command list. */
            if (is_swapchain_image)
            {
                *access_mask = VK_ACCESS_MEMORY_READ_BIT;
                *stage_flags = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
                if (image_layout)
                    *image_layout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
            }
            else
            {
                *access_mask = VK_ACCESS_HOST_READ_BIT | VK_ACCESS_HOST_WRITE_BIT;
                *stage_flags = VK_PIPELINE_STAGE_HOST_BIT;
                if (image_layout)
                    *image_layout = VK_IMAGE_LAYOUT_GENERAL;
            }
            return true;

        /* Handle write states. */
        case D3D12_RESOURCE_STATE_RENDER_TARGET:
            *access_mask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT
                    | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            *stage_flags = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            if (image_layout)
                *image_layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            return true;

        case D3D12_RESOURCE_STATE_UNORDERED_ACCESS:
            *access_mask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
            /* FIXME: We can limit shader bits based on command list type,
             * fragmentStoresAndAtomics and vertexPipelineStoresAndAtomics. */
            *stage_flags = VK_PIPELINE_STAGE_VERTEX_SHADER_BIT
                    | VK_PIPELINE_STAGE_TESSELLATION_CONTROL_SHADER_BIT
                    | VK_PIPELINE_STAGE_TESSELLATION_EVALUATION_SHADER_BIT
                    | VK_PIPELINE_STAGE_GEOMETRY_SHADER_BIT
                    | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT
                    | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
            if (image_layout)
                *image_layout = VK_IMAGE_LAYOUT_GENERAL;
            return true;

        case D3D12_RESOURCE_STATE_DEPTH_WRITE:
            *access_mask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT
                    | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            *stage_flags = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT
                    | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
            if (image_layout)
                *image_layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            return true;

        case D3D12_RESOURCE_STATE_COPY_DEST:
        case D3D12_RESOURCE_STATE_RESOLVE_DEST:
            *access_mask = VK_ACCESS_TRANSFER_WRITE_BIT;
            *stage_flags = VK_PIPELINE_STAGE_TRANSFER_BIT;
            if (image_layout)
                *image_layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            return true;

        case D3D12_RESOURCE_STATE_STREAM_OUT:
            FIXME("Unhandled resource state %#x.\n", state);
            return false;

        /* Set the Vulkan image layout for read-only states. */
        case D3D12_RESOURCE_STATE_DEPTH_READ:
        case D3D12_RESOURCE_STATE_DEPTH_READ | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE:
        case D3D12_RESOURCE_STATE_DEPTH_READ | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE:
        case D3D12_RESOURCE_STATE_DEPTH_READ | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
                | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE:
            *access_mask = 0;
            *stage_flags = 0;
            if (image_layout)
                *image_layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
            break;

        case D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE:
        case D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE:
        case D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE:
            *access_mask = 0;
            *stage_flags = 0;
            if (image_layout)
                *image_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            break;

        case D3D12_RESOURCE_STATE_COPY_SOURCE:
            *access_mask = 0;
            *stage_flags = 0;
            if (image_layout)
                *image_layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            break;

        default:
            *access_mask = 0;
            *stage_flags = 0;
            if (image_layout)
                *image_layout = VK_IMAGE_LAYOUT_GENERAL;
            break;
    }

    /* Handle read-only states. */
    assert(!is_write_resource_state(state));

    if (state & D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER)
    {
        *access_mask |= VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT
                | VK_ACCESS_UNIFORM_READ_BIT;
        *stage_flags |= VK_PIPELINE_STAGE_VERTEX_INPUT_BIT
                | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT
                | VK_PIPELINE_STAGE_TESSELLATION_CONTROL_SHADER_BIT
                | VK_PIPELINE_STAGE_TESSELLATION_EVALUATION_SHADER_BIT
                | VK_PIPELINE_STAGE_GEOMETRY_SHADER_BIT
                | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT
                | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
        state &= ~D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
    }

    if (state & D3D12_RESOURCE_STATE_INDEX_BUFFER)
    {
        *access_mask |= VK_ACCESS_INDEX_READ_BIT;
        *stage_flags |= VK_PIPELINE_STAGE_VERTEX_INPUT_BIT;
        state &= ~D3D12_RESOURCE_STATE_INDEX_BUFFER;
    }

    if (state & D3D12_RESOURCE_STATE_DEPTH_READ)
    {
        *access_mask |= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
        *stage_flags |= VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT
                | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        state &= ~D3D12_RESOURCE_STATE_DEPTH_READ;
    }

    if (state & D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE)
    {
        *access_mask |= VK_ACCESS_SHADER_READ_BIT;
        *stage_flags |= VK_PIPELINE_STAGE_VERTEX_SHADER_BIT
                | VK_PIPELINE_STAGE_TESSELLATION_CONTROL_SHADER_BIT
                | VK_PIPELINE_STAGE_TESSELLATION_EVALUATION_SHADER_BIT
                | VK_PIPELINE_STAGE_GEOMETRY_SHADER_BIT
                | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
        state &= ~D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    }
    if (state & D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE)
    {
        *access_mask |= VK_ACCESS_SHADER_READ_BIT;
        *stage_flags |= VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        state &= ~D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    }

    if (state & D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT) /* D3D12_RESOURCE_STATE_PREDICATION */
    {
        *access_mask |= VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
        *stage_flags |= VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT;
        state &= ~D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
    }

    if (state & (D3D12_RESOURCE_STATE_COPY_SOURCE | D3D12_RESOURCE_STATE_RESOLVE_SOURCE))
    {
        *access_mask |= VK_ACCESS_TRANSFER_READ_BIT;
        *stage_flags |= VK_PIPELINE_STAGE_TRANSFER_BIT;
        state &= ~(D3D12_RESOURCE_STATE_COPY_SOURCE | D3D12_RESOURCE_STATE_RESOLVE_SOURCE);
    }

    if (state)
    {
        WARN("Invalid resource state %#x.\n", state);
        return false;
    }
    return true;
}

static void d3d12_command_list_transition_resource_to_initial_state(struct d3d12_command_list *list,
        struct d3d12_resource *resource)
{
    const struct vkd3d_vk_device_procs *vk_procs = &list->device->vk_procs;
    VkPipelineStageFlags src_stage_mask, dst_stage_mask;
    const struct vkd3d_format *format;
    VkImageMemoryBarrier barrier;

    assert(!d3d12_resource_is_buffer(resource));

    if (!(format = vkd3d_format_from_d3d12_resource_desc(&resource->desc, 0)))
    {
        ERR("Resource %p has invalid format %#x.\n", resource, resource->desc.Format);
        return;
    }

    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.pNext = NULL;

    if (is_cpu_accessible_heap(&resource->heap_properties))
    {
        barrier.srcAccessMask = VK_ACCESS_HOST_READ_BIT | VK_ACCESS_HOST_WRITE_BIT;
        barrier.oldLayout = VK_IMAGE_LAYOUT_PREINITIALIZED;
        src_stage_mask = VK_PIPELINE_STAGE_HOST_BIT;
    }
    else
    {
        barrier.srcAccessMask = 0;
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        src_stage_mask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    }

    if (!vk_barrier_parameters_from_d3d12_resource_state(resource->initial_state,
            resource->flags & VKD3D_RESOURCE_SWAPCHAIN_IMAGE,
            &barrier.dstAccessMask, &dst_stage_mask, &barrier.newLayout))
    {
        FIXME("Unhandled state %#x.\n", resource->initial_state);
        return;
    }

    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = resource->u.vk_image;
    barrier.subresourceRange.aspectMask = format->vk_aspect_mask;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = VK_REMAINING_MIP_LEVELS;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = VK_REMAINING_ARRAY_LAYERS;

    TRACE("Initial state %#x transition for resource %p (old layout %#x, new layout %#x).\n",
            resource->initial_state, resource, barrier.oldLayout, barrier.newLayout);

    VK_CALL(vkCmdPipelineBarrier(list->vk_command_buffer, src_stage_mask, dst_stage_mask, 0,
            0, NULL, 0, NULL, 1, &barrier));
}

static void d3d12_command_list_track_resource_usage(struct d3d12_command_list *list,
        struct d3d12_resource *resource)
{
    if (resource->flags & VKD3D_RESOURCE_INITIAL_STATE_TRANSITION)
    {
        d3d12_command_list_transition_resource_to_initial_state(list, resource);
        resource->flags &= ~VKD3D_RESOURCE_INITIAL_STATE_TRANSITION;
    }
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
            d3d12_command_allocator_free_command_buffer(list->allocator, list);

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

    list->is_recording = false;

    if (!list->is_valid)
    {
        WARN("Error occurred during command list recording.\n");
        return E_INVALIDARG;
    }

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d3d12_command_list_Reset(ID3D12GraphicsCommandList *iface,
        ID3D12CommandAllocator *allocator, ID3D12PipelineState *initial_state)
{
    struct d3d12_command_allocator *allocator_impl = unsafe_impl_from_ID3D12CommandAllocator(allocator);
    struct d3d12_command_list *list = impl_from_ID3D12GraphicsCommandList(iface);
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

    if (list->allocator)
    {
        d3d12_command_allocator_free_command_buffer(list->allocator, list);
        list->allocator = NULL;
    }

    if (SUCCEEDED(hr = d3d12_command_allocator_allocate_command_buffer(allocator_impl, list)))
    {
        list->allocator = allocator_impl;
        list->pipeline_state = initial_state;
    }

    memset(list->pipeline_bindings, 0, sizeof(list->pipeline_bindings));

    return hr;
}

static HRESULT STDMETHODCALLTYPE d3d12_command_list_ClearState(ID3D12GraphicsCommandList *iface,
        ID3D12PipelineState *pipeline_state)
{
    FIXME("iface %p, pipline_state %p stub!\n", iface, pipeline_state);

    return E_NOTIMPL;
}

static void d3d12_command_list_get_fb_extent(struct d3d12_command_list *list,
        uint32_t *width, uint32_t *height)
{
    struct d3d12_device *device = list->device;

    if (!list->state->u.graphics.attachment_count)
    {
        *width = device->vk_info.device_limits.maxFramebufferWidth;
        *height = device->vk_info.device_limits.maxFramebufferHeight;
        return;
    }

    *width = list->fb_width;
    *height = list->fb_height;
}

static bool d3d12_command_list_update_current_framebuffer(struct d3d12_command_list *list,
        const struct vkd3d_vk_device_procs *vk_procs)
{
    struct d3d12_device *device = list->device;
    struct VkFramebufferCreateInfo fb_desc;
    VkFramebuffer vk_framebuffer;
    size_t start_idx = 0;
    VkResult vr;

    if (list->current_framebuffer != VK_NULL_HANDLE)
        return true;

    if (!list->state->u.graphics.rt_idx)
        ++start_idx;

    fb_desc.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fb_desc.pNext = NULL;
    fb_desc.flags = 0;
    fb_desc.renderPass = list->state->u.graphics.render_pass;
    fb_desc.attachmentCount = list->state->u.graphics.attachment_count;
    fb_desc.pAttachments = &list->views[start_idx];
    d3d12_command_list_get_fb_extent(list, &fb_desc.width, &fb_desc.height);
    fb_desc.layers = 1;
    if ((vr = VK_CALL(vkCreateFramebuffer(device->vk_device, &fb_desc, NULL, &vk_framebuffer))) < 0)
    {
        WARN("Failed to create Vulkan framebuffer, vr %d.\n", vr);
        return false;
    }

    if (!d3d12_command_allocator_add_framebuffer(list->allocator, vk_framebuffer))
    {
        WARN("Failed to add framebuffer.\n");
        VK_CALL(vkDestroyFramebuffer(device->vk_device, vk_framebuffer, NULL));
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
        VK_DYNAMIC_STATE_STENCIL_REFERENCE,
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
        b->inputRate = state->input_rates[binding];
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
    blend_desc.attachmentCount = state->attachment_count - state->rt_idx;
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
    pipeline_desc.pDepthStencilState = &state->ds_desc;
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

static void d3d12_command_list_update_descriptors(struct d3d12_command_list *list,
        VkPipelineBindPoint bind_point)
{
    struct vkd3d_pipeline_bindings *bindings = &list->pipeline_bindings[bind_point];
    const struct vkd3d_vk_device_procs *vk_procs = &list->device->vk_procs;
    struct d3d12_root_signature *rs = bindings->root_signature;

    if (!rs || !rs->pool_size_count || !rs->vk_set_layout)
        return;

    bindings->in_use = true;
    VK_CALL(vkCmdBindDescriptorSets(list->vk_command_buffer, bind_point,
            rs->vk_pipeline_layout, rs->main_set, 1, &bindings->descriptor_set, 0, NULL));
}

static bool d3d12_command_list_begin_render_pass(struct d3d12_command_list *list,
        const struct vkd3d_vk_device_procs *vk_procs)
{
    struct VkRenderPassBeginInfo begin_desc;

    if (!list->state)
    {
        WARN("Pipeline state is NULL.\n");
        return false;
    }

    if (!d3d12_command_list_update_current_framebuffer(list, vk_procs))
        return false;
    if (!d3d12_command_list_update_current_pipeline(list, vk_procs))
        return false;

    d3d12_command_list_update_descriptors(list, VK_PIPELINE_BIND_POINT_GRAPHICS);

    begin_desc.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    begin_desc.pNext = NULL;
    begin_desc.renderPass = list->state->u.graphics.render_pass;
    begin_desc.framebuffer = list->current_framebuffer;
    begin_desc.renderArea.offset.x = 0;
    begin_desc.renderArea.offset.y = 0;
    d3d12_command_list_get_fb_extent(list,
            &begin_desc.renderArea.extent.width, &begin_desc.renderArea.extent.height);
    begin_desc.clearValueCount = 0;
    begin_desc.pClearValues = NULL;
    VK_CALL(vkCmdBeginRenderPass(list->vk_command_buffer, &begin_desc, VK_SUBPASS_CONTENTS_INLINE));

    return true;
}

static void STDMETHODCALLTYPE d3d12_command_list_DrawInstanced(ID3D12GraphicsCommandList *iface,
        UINT vertex_count_per_instance, UINT instance_count, UINT start_vertex_location,
        UINT start_instance_location)
{
    struct d3d12_command_list *list = impl_from_ID3D12GraphicsCommandList(iface);
    const struct vkd3d_vk_device_procs *vk_procs;

    TRACE("iface %p, vertex_count_per_instance %u, instance_count %u, "
            "start_vertex_location %u, start_instance_location %u.\n",
            iface, vertex_count_per_instance, instance_count,
            start_vertex_location, start_instance_location);

    vk_procs = &list->device->vk_procs;

    /* FIXME: We probably don't want to begin a render pass for each draw. */
    if (!d3d12_command_list_begin_render_pass(list, vk_procs))
    {
        WARN("Failed to begin render pass, ignoring draw call.\n");
        return;
    }

    VK_CALL(vkCmdDraw(list->vk_command_buffer, vertex_count_per_instance,
            instance_count, start_vertex_location, start_instance_location));

    VK_CALL(vkCmdEndRenderPass(list->vk_command_buffer));
}

static void STDMETHODCALLTYPE d3d12_command_list_DrawIndexedInstanced(ID3D12GraphicsCommandList *iface,
        UINT index_count_per_instance, UINT instance_count, UINT start_vertex_location,
        INT base_vertex_location, UINT start_instance_location)
{
    struct d3d12_command_list *list = impl_from_ID3D12GraphicsCommandList(iface);
    const struct vkd3d_vk_device_procs *vk_procs;

    TRACE("iface %p, index_count_per_instance %u, instance_count %u, start_vertex_location %u, "
            "base_vertex_location %d, start_instance_location %u.\n",
            iface, index_count_per_instance, instance_count, start_vertex_location,
            base_vertex_location, start_instance_location);

    vk_procs = &list->device->vk_procs;

    /* FIXME: We probably don't want to begin a render pass for each draw. */
    if (!d3d12_command_list_begin_render_pass(list, vk_procs))
    {
        WARN("Failed to begin render pass, ignoring draw call.\n");
        return;
    }

    VK_CALL(vkCmdDrawIndexed(list->vk_command_buffer, index_count_per_instance,
            instance_count, start_vertex_location, base_vertex_location, start_instance_location));

    VK_CALL(vkCmdEndRenderPass(list->vk_command_buffer));
}

static void STDMETHODCALLTYPE d3d12_command_list_Dispatch(ID3D12GraphicsCommandList *iface,
        UINT x, UINT y, UINT z)
{
    struct d3d12_command_list *list = impl_from_ID3D12GraphicsCommandList(iface);
    const struct vkd3d_vk_device_procs *vk_procs;

    TRACE("iface %p, x %u, y %u, z %u.\n", iface, x, y, z);

    if (list->state->vk_bind_point != VK_PIPELINE_BIND_POINT_COMPUTE)
    {
        WARN("Pipeline state %p has bind point %#x.\n", list->state, list->state->vk_bind_point);
        return;
    }

    vk_procs = &list->device->vk_procs;

    d3d12_command_list_update_descriptors(list, VK_PIPELINE_BIND_POINT_COMPUTE);

    VK_CALL(vkCmdDispatch(list->vk_command_buffer, x, y, z));
}

static void STDMETHODCALLTYPE d3d12_command_list_CopyBufferRegion(ID3D12GraphicsCommandList *iface,
        ID3D12Resource *dst, UINT64 dst_offset, ID3D12Resource *src, UINT64 src_offset, UINT64 byte_count)
{
    struct d3d12_command_list *list = impl_from_ID3D12GraphicsCommandList(iface);
    struct d3d12_resource *dst_resource, *src_resource;
    const struct vkd3d_vk_device_procs *vk_procs;
    VkBufferCopy buffer_copy;

    TRACE("iface %p, dst_resource %p, dst_offset %#"PRIx64", src_resource %p, "
            "src_offset %#"PRIx64", byte_count %#"PRIx64".\n",
            iface, dst, dst_offset, src, src_offset, byte_count);

    vk_procs = &list->device->vk_procs;

    dst_resource = unsafe_impl_from_ID3D12Resource(dst);
    assert(d3d12_resource_is_buffer(dst_resource));
    src_resource = unsafe_impl_from_ID3D12Resource(src);
    assert(d3d12_resource_is_buffer(src_resource));

    d3d12_command_list_track_resource_usage(list, dst_resource);
    d3d12_command_list_track_resource_usage(list, src_resource);

    buffer_copy.srcOffset = src_offset;
    buffer_copy.dstOffset = dst_offset;
    buffer_copy.size = byte_count;

    VK_CALL(vkCmdCopyBuffer(list->vk_command_buffer,
            src_resource->u.vk_buffer, dst_resource->u.vk_buffer, 1, &buffer_copy));
}

static void vk_buffer_image_copy_from_d3d12(VkBufferImageCopy *buffer_image_copy,
        const D3D12_PLACED_SUBRESOURCE_FOOTPRINT *footprint, unsigned int sub_resource_idx,
        const D3D12_RESOURCE_DESC *image_desc, const struct vkd3d_format *format,
        unsigned int dst_x, unsigned int dst_y, unsigned int dst_z)
{
    buffer_image_copy->bufferOffset = footprint->Offset;
    buffer_image_copy->bufferRowLength = footprint->Footprint.RowPitch /
            (format->byte_count * format->block_byte_count) * format->block_width;
    buffer_image_copy->bufferImageHeight = 0;
    buffer_image_copy->imageSubresource.aspectMask = format->vk_aspect_mask;
    buffer_image_copy->imageSubresource.mipLevel = sub_resource_idx % image_desc->MipLevels;
    buffer_image_copy->imageSubresource.baseArrayLayer = sub_resource_idx / image_desc->MipLevels;
    buffer_image_copy->imageSubresource.layerCount = 1;
    buffer_image_copy->imageOffset.x = dst_x;
    buffer_image_copy->imageOffset.y = dst_y;
    buffer_image_copy->imageOffset.z = dst_z;
    buffer_image_copy->imageExtent.width = footprint->Footprint.Width;
    buffer_image_copy->imageExtent.height = footprint->Footprint.Height;
    buffer_image_copy->imageExtent.depth = footprint->Footprint.Depth;
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

    d3d12_command_list_track_resource_usage(list, dst_resource);
    d3d12_command_list_track_resource_usage(list, src_resource);

    if (src->Type == D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX
            && dst->Type == D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT)
    {
        assert(d3d12_resource_is_buffer(dst_resource));
        assert(!d3d12_resource_is_buffer(src_resource));

        if (!(format = vkd3d_format_from_d3d12_resource_desc(&src_resource->desc,
                dst->u.PlacedFootprint.Footprint.Format)))
        {
            WARN("Invalid format %#x.\n", dst->u.PlacedFootprint.Footprint.Format);
            return;
        }

        if ((format->vk_aspect_mask & VK_IMAGE_ASPECT_DEPTH_BIT)
                && (format->vk_aspect_mask & VK_IMAGE_ASPECT_STENCIL_BIT))
            FIXME("Depth-stencil format %#x not fully supported yet.\n", format->dxgi_format);

        vk_buffer_image_copy_from_d3d12(&buffer_image_copy, &dst->u.PlacedFootprint,
                src->u.SubresourceIndex, &src_resource->desc, format, dst_x, dst_y, dst_z);
        VK_CALL(vkCmdCopyImageToBuffer(list->vk_command_buffer,
                src_resource->u.vk_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                dst_resource->u.vk_buffer, 1, &buffer_image_copy));
    }
    else if (src->Type == D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT
            && dst->Type == D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX)
    {
        assert(!d3d12_resource_is_buffer(dst_resource));
        assert(d3d12_resource_is_buffer(src_resource));

        if (!(format = vkd3d_format_from_d3d12_resource_desc(&dst_resource->desc,
                src->u.PlacedFootprint.Footprint.Format)))
        {
            WARN("Invalid format %#x.\n", src->u.PlacedFootprint.Footprint.Format);
            return;
        }

        if ((format->vk_aspect_mask & VK_IMAGE_ASPECT_DEPTH_BIT)
                && (format->vk_aspect_mask & VK_IMAGE_ASPECT_STENCIL_BIT))
            FIXME("Depth-stencil format %#x not fully supported yet.\n", format->dxgi_format);

        vk_buffer_image_copy_from_d3d12(&buffer_image_copy, &src->u.PlacedFootprint,
                dst->u.SubresourceIndex, &dst_resource->desc, format, dst_x, dst_y, dst_z);
        VK_CALL(vkCmdCopyBufferToImage(list->vk_command_buffer,
                src_resource->u.vk_buffer, dst_resource->u.vk_image,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &buffer_image_copy));
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
    VkViewport vk_viewports[D3D12_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
    struct d3d12_command_list *list = impl_from_ID3D12GraphicsCommandList(iface);
    const struct vkd3d_vk_device_procs *vk_procs;
    unsigned int i;

    TRACE("iface %p, viewport_count %u, viewports %p.\n", iface, viewport_count, viewports);

    assert(viewport_count <= D3D12_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE);

    for (i = 0; i < viewport_count; ++i)
    {
        vk_viewports[i].x = viewports[i].TopLeftX;
        vk_viewports[i].y = viewports[i].TopLeftY + viewports[i].Height;
        vk_viewports[i].width = viewports[i].Width;
        vk_viewports[i].height = -viewports[i].Height;
        vk_viewports[i].minDepth = viewports[i].MinDepth;
        vk_viewports[i].maxDepth = viewports[i].MaxDepth;
    }

    vk_procs = &list->device->vk_procs;
    VK_CALL(vkCmdSetViewport(list->vk_command_buffer, 0, viewport_count, vk_viewports));
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
    struct d3d12_command_list *list = impl_from_ID3D12GraphicsCommandList(iface);
    const struct vkd3d_vk_device_procs *vk_procs;

    TRACE("iface %p, stencil_ref %u.\n", iface, stencil_ref);

    vk_procs = &list->device->vk_procs;
    VK_CALL(vkCmdSetStencilReference(list->vk_command_buffer, VK_STENCIL_FRONT_AND_BACK, stencil_ref));
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

    if (state && state->vk_bind_point == VK_PIPELINE_BIND_POINT_COMPUTE)
    {
        const struct vkd3d_vk_device_procs *vk_procs = &list->device->vk_procs;
        VK_CALL(vkCmdBindPipeline(list->vk_command_buffer, state->vk_bind_point, state->u.compute.vk_pipeline));
    }
}

static void STDMETHODCALLTYPE d3d12_command_list_ResourceBarrier(ID3D12GraphicsCommandList *iface,
        UINT barrier_count, const D3D12_RESOURCE_BARRIER *barriers)
{
    struct d3d12_command_list *list = impl_from_ID3D12GraphicsCommandList(iface);
    bool have_aliasing_barriers = false, have_split_barriers = false;
    const struct vkd3d_vk_device_procs *vk_procs;
    unsigned int i;

    TRACE("iface %p, barrier_count %u, barriers %p.\n", iface, barrier_count, barriers);

    vk_procs = &list->device->vk_procs;

    for (i = 0; i < barrier_count; ++i)
    {
        unsigned int sub_resource_idx = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        VkPipelineStageFlags src_stage_mask = 0, dst_stage_mask = 0;
        VkAccessFlags src_access_mask = 0, dst_access_mask = 0;
        const D3D12_RESOURCE_BARRIER *current = &barriers[i];
        VkImageLayout layout_before, layout_after;
        struct d3d12_resource *resource;

        have_split_barriers = have_split_barriers
                || (current->Flags & D3D12_RESOURCE_BARRIER_FLAG_BEGIN_ONLY)
                || (current->Flags & D3D12_RESOURCE_BARRIER_FLAG_END_ONLY);

        if (current->Flags & D3D12_RESOURCE_BARRIER_FLAG_BEGIN_ONLY)
            continue;

        switch (current->Type)
        {
            case D3D12_RESOURCE_BARRIER_TYPE_TRANSITION:
            {
                const D3D12_RESOURCE_TRANSITION_BARRIER *transition = &current->u.Transition;

                if (!is_valid_resource_state(transition->StateBefore))
                {
                    WARN("Invalid StateBefore %#x (barrier %u).\n", transition->StateBefore, i);
                    list->is_valid = false;
                    continue;
                }
                if (!is_valid_resource_state(transition->StateAfter))
                {
                    WARN("Invalid StateAfter %#x (barrier %u).\n", transition->StateAfter, i);
                    list->is_valid = false;
                    continue;
                }

                resource = unsafe_impl_from_ID3D12Resource(transition->pResource);
                assert(resource);
                sub_resource_idx = transition->Subresource;

                if (!vk_barrier_parameters_from_d3d12_resource_state(transition->StateBefore,
                        resource->flags & VKD3D_RESOURCE_SWAPCHAIN_IMAGE,
                        &src_access_mask, &src_stage_mask, &layout_before))
                {
                    FIXME("Unhandled state %#x.\n", transition->StateBefore);
                    continue;
                }
                if (!vk_barrier_parameters_from_d3d12_resource_state(transition->StateAfter,
                        resource->flags & VKD3D_RESOURCE_SWAPCHAIN_IMAGE,
                        &dst_access_mask, &dst_stage_mask, &layout_after))
                {
                    FIXME("Unhandled state %#x.\n", transition->StateAfter);
                    continue;
                }

                TRACE("Transition barrier (resource %p, subresource %#x, before %#x, after %#x).\n",
                        resource, transition->Subresource, transition->StateBefore, transition->StateAfter);
                break;
            }

            case D3D12_RESOURCE_BARRIER_TYPE_UAV:
            {
                const D3D12_RESOURCE_UAV_BARRIER *uav = &current->u.UAV;
                VkPipelineStageFlags stage_mask;
                VkImageLayout image_layout;
                VkAccessFlags access_mask;

                resource = unsafe_impl_from_ID3D12Resource(uav->pResource);
                vk_barrier_parameters_from_d3d12_resource_state(D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                        resource && (resource->flags & VKD3D_RESOURCE_SWAPCHAIN_IMAGE),
                        &access_mask, &stage_mask, &image_layout);
                src_access_mask = dst_access_mask = access_mask;
                dst_stage_mask = src_stage_mask = stage_mask;
                layout_before = layout_after = image_layout;

                TRACE("UAV barrier (resource %p).\n", resource);
                break;
            }

            case D3D12_RESOURCE_BARRIER_TYPE_ALIASING:
                have_aliasing_barriers = true;
                continue;
            default:
                WARN("Invalid barrier type %#x.\n", current->Type);
                continue;
        }

        if (resource)
            d3d12_command_list_track_resource_usage(list, resource);

        if (!resource)
        {
            VkMemoryBarrier vk_barrier;

            vk_barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
            vk_barrier.pNext = NULL;
            vk_barrier.srcAccessMask = src_access_mask;
            vk_barrier.dstAccessMask = dst_access_mask;

            VK_CALL(vkCmdPipelineBarrier(list->vk_command_buffer, src_stage_mask, dst_stage_mask, 0,
                    1, &vk_barrier, 0, NULL, 0, NULL));
        }
        else if (d3d12_resource_is_buffer(resource))
        {
            VkBufferMemoryBarrier vk_barrier;

            vk_barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
            vk_barrier.pNext = NULL;
            vk_barrier.srcAccessMask = src_access_mask;
            vk_barrier.dstAccessMask = dst_access_mask;
            vk_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            vk_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            vk_barrier.buffer = resource->u.vk_buffer;
            vk_barrier.offset = 0;
            vk_barrier.size = VK_WHOLE_SIZE;

            VK_CALL(vkCmdPipelineBarrier(list->vk_command_buffer, src_stage_mask, dst_stage_mask, 0,
                    0, NULL, 1, &vk_barrier, 0, NULL));
        }
        else
        {
            const struct vkd3d_format *format;
            VkImageMemoryBarrier vk_barrier;

            if (!(format = vkd3d_format_from_d3d12_resource_desc(&resource->desc, 0)))
            {
                ERR("Resource %p has invalid format %#x.\n", resource, resource->desc.Format);
                continue;
            }

            vk_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            vk_barrier.pNext = NULL;
            vk_barrier.srcAccessMask = src_access_mask;
            vk_barrier.dstAccessMask = dst_access_mask;
            vk_barrier.oldLayout = layout_before;
            vk_barrier.newLayout = layout_after;
            vk_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            vk_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            vk_barrier.image = resource->u.vk_image;

            vk_barrier.subresourceRange.aspectMask = format->vk_aspect_mask;
            if (sub_resource_idx == D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES)
            {
                vk_barrier.subresourceRange.baseMipLevel = 0;
                vk_barrier.subresourceRange.levelCount = VK_REMAINING_MIP_LEVELS;
                vk_barrier.subresourceRange.baseArrayLayer = 0;
                vk_barrier.subresourceRange.layerCount = VK_REMAINING_ARRAY_LAYERS;
            }
            else
            {
                vk_barrier.subresourceRange.baseMipLevel = sub_resource_idx % resource->desc.MipLevels;
                vk_barrier.subresourceRange.levelCount = 1;
                vk_barrier.subresourceRange.baseArrayLayer = sub_resource_idx / resource->desc.MipLevels;
                vk_barrier.subresourceRange.layerCount = 1;
            }

            VK_CALL(vkCmdPipelineBarrier(list->vk_command_buffer, src_stage_mask, dst_stage_mask, 0,
                    0, NULL, 0, NULL, 1, &vk_barrier));
        }
    }

    if (have_aliasing_barriers)
        FIXME("Aliasing barriers not implemented yet.\n");

    /* Vulkan doesn't support split barriers. */
    if (have_split_barriers)
        WARN("Issuing split barrier(s) on D3D12_RESOURCE_BARRIER_FLAG_END_ONLY.\n");
}

static void STDMETHODCALLTYPE d3d12_command_list_ExecuteBundle(ID3D12GraphicsCommandList *iface,
        ID3D12GraphicsCommandList *command_list)
{
    FIXME("iface %p, command_list %p stub!\n", iface, command_list);
}

static void STDMETHODCALLTYPE d3d12_command_list_SetDescriptorHeaps(ID3D12GraphicsCommandList *iface,
        UINT heap_count, ID3D12DescriptorHeap *const *heaps)
{
    TRACE("iface %p, heap_count %u, heaps %p.\n", iface, heap_count, heaps);

    /* Our current implementation does not need this method.
     *
     * It could be used to validate descriptor tables but we do not have an
     * equivalent of the D3D12 Debug Layer. */
}

static VkDescriptorSet d3d12_command_list_allocate_descriptor_set(struct d3d12_command_list *list,
        struct d3d12_root_signature *root_signature)
{
    const struct vkd3d_vk_device_procs *vk_procs;
    VkDevice vk_device = list->device->vk_device;
    struct VkDescriptorSetAllocateInfo set_desc;
    struct VkDescriptorPoolCreateInfo pool_desc;
    VkDescriptorSet vk_descriptor_set;
    VkDescriptorPool vk_pool;
    VkResult vr;

    if (!root_signature->pool_size_count)
        return VK_NULL_HANDLE;

    vk_procs = &list->device->vk_procs;

    pool_desc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_desc.pNext = NULL;
    pool_desc.flags = 0;
    pool_desc.maxSets = 1;
    pool_desc.poolSizeCount = root_signature->pool_size_count;
    pool_desc.pPoolSizes = root_signature->pool_sizes;
    if ((vr = VK_CALL(vkCreateDescriptorPool(vk_device, &pool_desc, NULL, &vk_pool))) < 0)
    {
        ERR("Failed to create descriptor pool, vr %d.\n", vr);
        return VK_NULL_HANDLE;
    }

    set_desc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    set_desc.pNext = NULL;
    set_desc.descriptorPool = vk_pool;
    set_desc.descriptorSetCount = 1;
    set_desc.pSetLayouts = &root_signature->vk_set_layout;
    if ((vr = VK_CALL(vkAllocateDescriptorSets(vk_device, &set_desc, &vk_descriptor_set))) < 0)
    {
        ERR("Failed to allocate descriptor set, vr %d.\n", vr);
        VK_CALL(vkDestroyDescriptorPool(vk_device, vk_pool, NULL));
        return VK_NULL_HANDLE;
    }

    if (!(d3d12_command_allocator_add_descriptor_pool(list->allocator, vk_pool)))
    {
        ERR("Failed to add descriptor pool.\n");
        VK_CALL(vkDestroyDescriptorPool(vk_device, vk_pool, NULL));
        return VK_NULL_HANDLE;
    }

    return vk_descriptor_set;
}

static void d3d12_command_list_copy_descriptors(struct d3d12_command_list *list,
        struct d3d12_root_signature *root_signature, VkDescriptorSet dst_set, VkDescriptorSet src_set)
{
    const struct vkd3d_vk_device_procs *vk_procs = &list->device->vk_procs;
    unsigned int count = root_signature->copy_descriptor_count;
    VkDevice vk_device = list->device->vk_device;
    VkCopyDescriptorSet *descriptor_copies;
    unsigned int i;

    if (!(descriptor_copies = vkd3d_calloc(count, sizeof(*descriptor_copies))))
        return;

    for (i = 0; i < count; ++i)
    {
        descriptor_copies[i].sType = VK_STRUCTURE_TYPE_COPY_DESCRIPTOR_SET;
        descriptor_copies[i].pNext = NULL;
        descriptor_copies[i].srcSet = src_set;
        descriptor_copies[i].srcBinding = i;
        descriptor_copies[i].srcArrayElement = 0;
        descriptor_copies[i].dstSet = dst_set;
        descriptor_copies[i].dstBinding = i;
        descriptor_copies[i].dstArrayElement = 0;
        descriptor_copies[i].descriptorCount = 1;
    }
    VK_CALL(vkUpdateDescriptorSets(vk_device, 0, NULL, count, descriptor_copies));

    vkd3d_free(descriptor_copies);
}

static void d3d12_command_list_prepare_descriptors(struct d3d12_command_list *list,
        VkPipelineBindPoint bind_point)
{
    struct vkd3d_pipeline_bindings *bindings = &list->pipeline_bindings[bind_point];
    struct d3d12_root_signature *root_signature = bindings->root_signature;
    VkDescriptorSet previous_descriptor_set = bindings->descriptor_set;

    if (bindings->descriptor_set && !bindings->in_use)
        return;

    /* We cannot modify bound descriptor sets. We need a new descriptor set if
     * we are about to update resource bindings.
     *
     * The Vulkan spec says:
     *
     *   "The descriptor set contents bound by a call to
     *   vkCmdBindDescriptorSets may be consumed during host execution of the
     *   command, or during shader execution of the resulting draws, or any
     *   time in between. Thus, the contents must not be altered (overwritten
     *   by an update command, or freed) between when the command is recorded
     *   and when the command completes executing on the queue."
     */
    bindings->descriptor_set = d3d12_command_list_allocate_descriptor_set(list, root_signature);
    bindings->in_use = false;

    if (previous_descriptor_set)
        d3d12_command_list_copy_descriptors(list, root_signature,
                bindings->descriptor_set, previous_descriptor_set);
}

static void d3d12_command_list_set_root_signature(struct d3d12_command_list *list,
        VkPipelineBindPoint bind_point, struct d3d12_root_signature *root_signature)
{
    struct vkd3d_pipeline_bindings *bindings = &list->pipeline_bindings[bind_point];

    if (bindings->root_signature == root_signature)
        return;

    bindings->root_signature = root_signature;
    bindings->descriptor_set = VK_NULL_HANDLE;
}

static void STDMETHODCALLTYPE d3d12_command_list_SetComputeRootSignature(ID3D12GraphicsCommandList *iface,
        ID3D12RootSignature *root_signature)
{
    struct d3d12_command_list *list = impl_from_ID3D12GraphicsCommandList(iface);

    TRACE("iface %p, root_signature %p.\n", iface, root_signature);

    d3d12_command_list_set_root_signature(list, VK_PIPELINE_BIND_POINT_COMPUTE,
            unsafe_impl_from_ID3D12RootSignature(root_signature));
}

static void STDMETHODCALLTYPE d3d12_command_list_SetGraphicsRootSignature(ID3D12GraphicsCommandList *iface,
        ID3D12RootSignature *root_signature)
{
    struct d3d12_command_list *list = impl_from_ID3D12GraphicsCommandList(iface);

    TRACE("iface %p, root_signature %p.\n", iface, root_signature);

    d3d12_command_list_set_root_signature(list, VK_PIPELINE_BIND_POINT_GRAPHICS,
            unsafe_impl_from_ID3D12RootSignature(root_signature));
}

static bool vk_write_descriptor_set_from_d3d12_desc(VkWriteDescriptorSet *vk_descriptor_write,
        VkDescriptorImageInfo *vk_image_info, struct d3d12_desc *descriptor,
        VkDescriptorSet vk_descriptor_set, uint32_t vk_binding, unsigned int index)
{
    vk_descriptor_write->sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    vk_descriptor_write->pNext = NULL;
    vk_descriptor_write->dstSet = vk_descriptor_set;
    vk_descriptor_write->dstBinding = vk_binding + index;
    vk_descriptor_write->dstArrayElement = 0;
    vk_descriptor_write->descriptorCount = 1;
    vk_descriptor_write->descriptorType = descriptor->vk_descriptor_type;
    vk_descriptor_write->pImageInfo = NULL;
    vk_descriptor_write->pBufferInfo = NULL;
    vk_descriptor_write->pTexelBufferView = NULL;

    if (descriptor->magic == VKD3D_DESCRIPTOR_MAGIC_SRV
            || descriptor->magic == VKD3D_DESCRIPTOR_MAGIC_UAV)
    {
        /* We use separate bindings for buffer and texture SRVs/UAVs.
         * See d3d12_root_signature_init(). */
        vk_descriptor_write->dstBinding = vk_binding + 2 * index;
        if (descriptor->vk_descriptor_type != VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER
                && descriptor->vk_descriptor_type != VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER)
            ++vk_descriptor_write->dstBinding;
    }

    if (descriptor->magic == VKD3D_DESCRIPTOR_MAGIC_CBV)
    {
        vk_descriptor_write->pBufferInfo = &descriptor->u.vk_cbv_info;
    }
    else if (descriptor->magic == VKD3D_DESCRIPTOR_MAGIC_SRV)
    {
        if (descriptor->vk_descriptor_type == VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER)
        {
            vk_descriptor_write->pTexelBufferView = &descriptor->u.vk_buffer_view;
        }
        else
        {
            vk_image_info->sampler = VK_NULL_HANDLE;
            vk_image_info->imageView = descriptor->u.vk_image_view;
            vk_image_info->imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

            vk_descriptor_write->pImageInfo = vk_image_info;
        }
    }
    else if (descriptor->magic == VKD3D_DESCRIPTOR_MAGIC_UAV)
    {
        if (descriptor->vk_descriptor_type == VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER)
        {
            vk_descriptor_write->pTexelBufferView = &descriptor->u.vk_buffer_view;
        }
        else
        {
            vk_image_info->sampler = VK_NULL_HANDLE;
            vk_image_info->imageView = descriptor->u.vk_image_view;
            vk_image_info->imageLayout = VK_IMAGE_LAYOUT_GENERAL;

            vk_descriptor_write->pImageInfo = vk_image_info;
        }
    }
    else if (descriptor->magic == VKD3D_DESCRIPTOR_MAGIC_SAMPLER)
    {
        vk_image_info->sampler = descriptor->u.vk_sampler;
        vk_image_info->imageView = VK_NULL_HANDLE;
        vk_image_info->imageLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        vk_descriptor_write->pImageInfo = vk_image_info;
    }
    else
    {
        FIXME("Unhandled descriptor %#x.\n", descriptor->magic);
        return false;
    }

    return true;
}

static void d3d12_command_list_set_descriptor_table(struct d3d12_command_list *list,
        VkPipelineBindPoint bind_point, unsigned int index, D3D12_GPU_DESCRIPTOR_HANDLE base_descriptor)
{
    struct vkd3d_pipeline_bindings *bindings = &list->pipeline_bindings[bind_point];
    struct VkWriteDescriptorSet *descriptor_writes, *current_descriptor_write;
    struct d3d12_root_signature *root_signature = bindings->root_signature;
    struct VkDescriptorImageInfo *image_infos, *current_image_info;
    const struct d3d12_root_descriptor_table *descriptor_table;
    const struct d3d12_root_descriptor_table_range *range;
    const struct vkd3d_vk_device_procs *vk_procs;
    struct d3d12_device *device = list->device;
    unsigned int i, j, descriptor_count;
    struct d3d12_desc *descriptor;

    assert(root_signature->parameters[index].parameter_type == D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE);
    descriptor_table = &root_signature->parameters[index].u.descriptor_table;

    descriptor_count = 0;
    for (i = 0; i < descriptor_table->range_count; ++i)
        descriptor_count += descriptor_table->ranges[i].descriptor_count;
    if (!(descriptor_writes = vkd3d_calloc(descriptor_count, sizeof(*descriptor_writes))))
        return;
    if (!(image_infos = vkd3d_calloc(descriptor_count, sizeof(*image_infos))))
    {
        vkd3d_free(descriptor_writes);
        return;
    }

    descriptor = (struct d3d12_desc *)(intptr_t)base_descriptor.ptr;

    d3d12_command_list_prepare_descriptors(list, bind_point);

    descriptor_count = 0;
    current_descriptor_write = descriptor_writes;
    current_image_info = image_infos;
    for (i = 0; i < descriptor_table->range_count; ++i)
    {
        range = &descriptor_table->ranges[i];
        for (j = 0; j < range->descriptor_count; ++j, ++descriptor)
        {
            if (!vk_write_descriptor_set_from_d3d12_desc(current_descriptor_write,
                    current_image_info, descriptor, bindings->descriptor_set, range->binding, j))
                continue;

            ++descriptor_count;
            ++current_descriptor_write;
            ++current_image_info;
        }
    }

    vk_procs = &device->vk_procs;
    VK_CALL(vkUpdateDescriptorSets(device->vk_device, descriptor_count, descriptor_writes, 0, NULL));

    vkd3d_free(descriptor_writes);
    vkd3d_free(image_infos);
}

static void STDMETHODCALLTYPE d3d12_command_list_SetComputeRootDescriptorTable(ID3D12GraphicsCommandList *iface,
        UINT root_parameter_index, D3D12_GPU_DESCRIPTOR_HANDLE base_descriptor)
{
    struct d3d12_command_list *list = impl_from_ID3D12GraphicsCommandList(iface);

    TRACE("iface %p, root_parameter_index %u, base_descriptor %#"PRIx64".\n",
            iface, root_parameter_index, base_descriptor.ptr);

    d3d12_command_list_set_descriptor_table(list, VK_PIPELINE_BIND_POINT_COMPUTE,
            root_parameter_index, base_descriptor);
}

static void STDMETHODCALLTYPE d3d12_command_list_SetGraphicsRootDescriptorTable(ID3D12GraphicsCommandList *iface,
        UINT root_parameter_index, D3D12_GPU_DESCRIPTOR_HANDLE base_descriptor)
{
    struct d3d12_command_list *list = impl_from_ID3D12GraphicsCommandList(iface);

    TRACE("iface %p, root_parameter_index %u, base_descriptor %#"PRIx64".\n",
            iface, root_parameter_index, base_descriptor.ptr);

    d3d12_command_list_set_descriptor_table(list, VK_PIPELINE_BIND_POINT_GRAPHICS,
            root_parameter_index, base_descriptor);
}

static void d3d12_command_list_set_root_constants(struct d3d12_command_list *list,
        VkPipelineBindPoint bind_point, unsigned int index, unsigned int offset,
        unsigned int count, const void *data)
{
    struct d3d12_root_signature *root_signature = list->pipeline_bindings[bind_point].root_signature;
    const struct vkd3d_vk_device_procs *vk_procs = &list->device->vk_procs;
    const struct d3d12_root_constant *c;

    assert(root_signature->parameters[index].parameter_type == D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS);
    c = &root_signature->parameters[index].u.constant;
    VK_CALL(vkCmdPushConstants(list->vk_command_buffer, root_signature->vk_pipeline_layout,
            c->stage_flags, c->offset + offset * sizeof(uint32_t), count * sizeof(uint32_t), data));
}

static void STDMETHODCALLTYPE d3d12_command_list_SetComputeRoot32BitConstant(ID3D12GraphicsCommandList *iface,
        UINT root_parameter_index, UINT data, UINT dst_offset)
{
    struct d3d12_command_list *list = impl_from_ID3D12GraphicsCommandList(iface);

    TRACE("iface %p, root_parameter_index %u, data 0x%08x, dst_offset %u.\n",
            iface, root_parameter_index, data, dst_offset);

    d3d12_command_list_set_root_constants(list, VK_PIPELINE_BIND_POINT_COMPUTE,
            root_parameter_index, dst_offset, 1, &data);
}

static void STDMETHODCALLTYPE d3d12_command_list_SetGraphicsRoot32BitConstant(ID3D12GraphicsCommandList *iface,
        UINT root_parameter_index, UINT data, UINT dst_offset)
{
    struct d3d12_command_list *list = impl_from_ID3D12GraphicsCommandList(iface);

    TRACE("iface %p, root_parameter_index %u, data 0x%08x, dst_offset %u.\n",
            iface, root_parameter_index, data, dst_offset);

    d3d12_command_list_set_root_constants(list, VK_PIPELINE_BIND_POINT_GRAPHICS,
            root_parameter_index, dst_offset, 1, &data);
}

static void STDMETHODCALLTYPE d3d12_command_list_SetComputeRoot32BitConstants(ID3D12GraphicsCommandList *iface,
        UINT root_parameter_index, UINT constant_count, const void *data, UINT dst_offset)
{
    struct d3d12_command_list *list = impl_from_ID3D12GraphicsCommandList(iface);

    TRACE("iface %p, root_parameter_index %u, constant_count %u, data %p, dst_offset %u.\n",
            iface, root_parameter_index, constant_count, data, dst_offset);

    d3d12_command_list_set_root_constants(list, VK_PIPELINE_BIND_POINT_COMPUTE,
            root_parameter_index, dst_offset, constant_count, data);
}

static void STDMETHODCALLTYPE d3d12_command_list_SetGraphicsRoot32BitConstants(ID3D12GraphicsCommandList *iface,
        UINT root_parameter_index, UINT constant_count, const void *data, UINT dst_offset)
{
    struct d3d12_command_list *list = impl_from_ID3D12GraphicsCommandList(iface);

    TRACE("iface %p, root_parameter_index %u, constant_count %u, data %p, dst_offset %u.\n",
            iface, root_parameter_index, constant_count, data, dst_offset);

    d3d12_command_list_set_root_constants(list, VK_PIPELINE_BIND_POINT_GRAPHICS,
            root_parameter_index, dst_offset, constant_count, data);
}

static void d3d12_command_list_set_root_cbv(struct d3d12_command_list *list,
        VkPipelineBindPoint bind_point, unsigned int index, D3D12_GPU_VIRTUAL_ADDRESS gpu_address)
{
    struct vkd3d_pipeline_bindings *bindings = &list->pipeline_bindings[bind_point];
    struct d3d12_root_signature *root_signature = bindings->root_signature;
    const struct vkd3d_vk_device_procs *vk_procs = &list->device->vk_procs;
    const struct vkd3d_vulkan_info *vk_info = &list->device->vk_info;
    const struct d3d12_root_descriptor *root_descriptor;
    struct VkWriteDescriptorSet descriptor_write;
    struct VkDescriptorBufferInfo buffer_info;
    struct d3d12_resource *resource;

    assert(root_signature->parameters[index].parameter_type == D3D12_ROOT_PARAMETER_TYPE_CBV);
    root_descriptor = &root_signature->parameters[index].u.descriptor;

    resource = vkd3d_gpu_va_allocator_dereference(&list->device->gpu_va_allocator, gpu_address);
    buffer_info.buffer = resource->u.vk_buffer;
    buffer_info.offset = gpu_address - resource->gpu_address;
    buffer_info.range = VK_WHOLE_SIZE;

    if (!vk_info->KHR_push_descriptor)
        d3d12_command_list_prepare_descriptors(list, bind_point);

    descriptor_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptor_write.pNext = NULL;
    descriptor_write.dstSet = bindings->descriptor_set;
    descriptor_write.dstBinding = root_descriptor->binding;
    descriptor_write.dstArrayElement = 0;
    descriptor_write.descriptorCount = 1;
    descriptor_write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    descriptor_write.pImageInfo = NULL;
    descriptor_write.pBufferInfo = &buffer_info;
    descriptor_write.pTexelBufferView = NULL;

    if (vk_info->KHR_push_descriptor)
    {
        VK_CALL(vkCmdPushDescriptorSetKHR(list->vk_command_buffer, bind_point,
                root_signature->vk_pipeline_layout, 0, 1, &descriptor_write));
    }
    else
    {
        VK_CALL(vkUpdateDescriptorSets(list->device->vk_device, 1, &descriptor_write, 0, NULL));
    }
}

static void STDMETHODCALLTYPE d3d12_command_list_SetComputeRootConstantBufferView(
        ID3D12GraphicsCommandList *iface, UINT root_parameter_index, D3D12_GPU_VIRTUAL_ADDRESS address)
{
    struct d3d12_command_list *list = impl_from_ID3D12GraphicsCommandList(iface);

    TRACE("iface %p, root_parameter_index %u, address %#"PRIx64".\n",
            iface, root_parameter_index, address);

    d3d12_command_list_set_root_cbv(list, VK_PIPELINE_BIND_POINT_COMPUTE, root_parameter_index, address);
}

static void STDMETHODCALLTYPE d3d12_command_list_SetGraphicsRootConstantBufferView(
        ID3D12GraphicsCommandList *iface, UINT root_parameter_index, D3D12_GPU_VIRTUAL_ADDRESS address)
{
    struct d3d12_command_list *list = impl_from_ID3D12GraphicsCommandList(iface);

    TRACE("iface %p, root_parameter_index %u, address %#"PRIx64".\n",
            iface, root_parameter_index, address);

    d3d12_command_list_set_root_cbv(list, VK_PIPELINE_BIND_POINT_GRAPHICS, root_parameter_index, address);
}

static void d3d12_command_list_set_root_uav(struct d3d12_command_list *list,
        VkPipelineBindPoint bind_point, unsigned int index, D3D12_GPU_VIRTUAL_ADDRESS gpu_address)
{
    struct vkd3d_pipeline_bindings *bindings = &list->pipeline_bindings[bind_point];
    struct d3d12_root_signature *root_signature = bindings->root_signature;
    const struct vkd3d_vk_device_procs *vk_procs = &list->device->vk_procs;
    const struct vkd3d_vulkan_info *vk_info = &list->device->vk_info;
    const struct d3d12_root_descriptor *root_descriptor;
    struct VkWriteDescriptorSet descriptor_write;
    VkDevice vk_device = list->device->vk_device;
    VkBufferView vk_buffer_view;

    assert(root_signature->parameters[index].parameter_type == D3D12_ROOT_PARAMETER_TYPE_UAV);
    root_descriptor = &root_signature->parameters[index].u.descriptor;

    /* FIXME: Re-use buffer views. */
    if (!vkd3d_create_raw_buffer_uav(list->device, gpu_address, &vk_buffer_view))
    {
        ERR("Failed to create buffer view.\n");
        return;
    }

    if (!(d3d12_command_allocator_add_buffer_view(list->allocator, vk_buffer_view)))
    {
        ERR("Failed to add buffer view.\n");
        VK_CALL(vkDestroyBufferView(vk_device, vk_buffer_view, NULL));
        return;
    }

    if (!vk_info->KHR_push_descriptor)
        d3d12_command_list_prepare_descriptors(list, bind_point);

    descriptor_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptor_write.pNext = NULL;
    descriptor_write.dstSet = bindings->descriptor_set;
    descriptor_write.dstBinding = root_descriptor->binding;
    descriptor_write.dstArrayElement = 0;
    descriptor_write.descriptorCount = 1;
    descriptor_write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER;
    descriptor_write.pImageInfo = NULL;
    descriptor_write.pBufferInfo = NULL;
    descriptor_write.pTexelBufferView = &vk_buffer_view;

    if (vk_info->KHR_push_descriptor)
    {
        VK_CALL(vkCmdPushDescriptorSetKHR(list->vk_command_buffer, bind_point,
                root_signature->vk_pipeline_layout, 0, 1, &descriptor_write));
    }
    else
    {
        VK_CALL(vkUpdateDescriptorSets(list->device->vk_device, 1, &descriptor_write, 0, NULL));
    }
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
    struct d3d12_command_list *list = impl_from_ID3D12GraphicsCommandList(iface);

    TRACE("iface %p, root_parameter_index %u, address %#"PRIx64".\n",
            iface, root_parameter_index, address);

    d3d12_command_list_set_root_uav(list, VK_PIPELINE_BIND_POINT_COMPUTE, root_parameter_index, address);
}

static void STDMETHODCALLTYPE d3d12_command_list_SetGraphicsRootUnorderedAccessView(
        ID3D12GraphicsCommandList *iface, UINT root_parameter_index, D3D12_GPU_VIRTUAL_ADDRESS address)
{
    struct d3d12_command_list *list = impl_from_ID3D12GraphicsCommandList(iface);

    TRACE("iface %p, root_parameter_index %u, address %#"PRIx64".\n",
            iface, root_parameter_index, address);

    d3d12_command_list_set_root_uav(list, VK_PIPELINE_BIND_POINT_GRAPHICS, root_parameter_index, address);
}

static void STDMETHODCALLTYPE d3d12_command_list_IASetIndexBuffer(ID3D12GraphicsCommandList *iface,
        const D3D12_INDEX_BUFFER_VIEW *view)
{
    struct d3d12_command_list *list = impl_from_ID3D12GraphicsCommandList(iface);
    const struct vkd3d_vk_device_procs *vk_procs;
    struct d3d12_resource *resource;
    enum VkIndexType index_type;

    TRACE("iface %p, view %p.\n", iface, view);

    vk_procs = &list->device->vk_procs;

    switch (view->Format)
    {
        case DXGI_FORMAT_R16_UINT:
            index_type = VK_INDEX_TYPE_UINT16;
            break;
        case DXGI_FORMAT_R32_UINT:
            index_type = VK_INDEX_TYPE_UINT32;
            break;
        default:
            FIXME("Unhandled format %#x.\n", view->Format);
            return;
    }

    resource = vkd3d_gpu_va_allocator_dereference(&list->device->gpu_va_allocator, view->BufferLocation);
    VK_CALL(vkCmdBindIndexBuffer(list->vk_command_buffer, resource->u.vk_buffer,
            view->BufferLocation - resource->gpu_address, index_type));
}

static void STDMETHODCALLTYPE d3d12_command_list_IASetVertexBuffers(ID3D12GraphicsCommandList *iface,
        UINT start_slot, UINT view_count, const D3D12_VERTEX_BUFFER_VIEW *views)
{
    struct d3d12_command_list *list = impl_from_ID3D12GraphicsCommandList(iface);
    VkDeviceSize offsets[ARRAY_SIZE(list->strides)];
    const struct vkd3d_vk_device_procs *vk_procs;
    VkBuffer buffers[ARRAY_SIZE(list->strides)];
    struct d3d12_resource *resource;
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
        resource = vkd3d_gpu_va_allocator_dereference(&list->device->gpu_va_allocator, views[i].BufferLocation);
        offsets[i] = views[i].BufferLocation - resource->gpu_address;
        buffers[i] = resource->u.vk_buffer;
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

    if (render_target_descriptor_count > ARRAY_SIZE(list->views) - 1)
    {
        WARN("Descriptor count %u > %zu, ignoring extra descriptors.\n",
                render_target_descriptor_count, ARRAY_SIZE(list->views) - 1);
        render_target_descriptor_count = ARRAY_SIZE(list->views) - 1;
    }

    list->fb_width = 0;
    list->fb_height = 0;
    for (i = 0; i < render_target_descriptor_count; ++i)
    {
        const struct d3d12_rtv_desc *rtv_desc = (const struct d3d12_rtv_desc *)render_target_descriptors[i].ptr;

        d3d12_command_list_track_resource_usage(list, rtv_desc->resource);

        list->views[i + 1] = rtv_desc->vk_view;
        if (rtv_desc->width > list->fb_width)
            list->fb_width = rtv_desc->width;
        if (rtv_desc->height > list->fb_height)
            list->fb_height = rtv_desc->height;
    }

    if (depth_stencil_descriptor)
    {
        const struct d3d12_dsv_desc *dsv_desc = (const struct d3d12_dsv_desc *)depth_stencil_descriptor->ptr;

        d3d12_command_list_track_resource_usage(list, dsv_desc->resource);

        if (dsv_desc->width > list->fb_width)
            list->fb_width = dsv_desc->width;
        if (dsv_desc->height > list->fb_height)
            list->fb_height = dsv_desc->height;

        list->views[0] = dsv_desc->vk_view;
    }

    d3d12_command_list_invalidate_current_framebuffer(list);
}

static void d3d12_command_list_clear(struct d3d12_command_list *list, const struct vkd3d_vk_device_procs *vk_procs,
        const struct VkAttachmentDescription *attachment_desc, const struct VkAttachmentReference *color_reference,
        const struct VkAttachmentReference *ds_reference, VkImageView vk_view, size_t width, size_t height,
        const union VkClearValue *clear_value, unsigned int rect_count, const D3D12_RECT *rects)
{
    struct VkSubpassDescription sub_pass_desc;
    struct VkRenderPassCreateInfo pass_desc;
    struct VkRenderPassBeginInfo begin_desc;
    struct VkFramebufferCreateInfo fb_desc;
    VkFramebuffer vk_framebuffer;
    VkRenderPass vk_render_pass;
    D3D12_RECT full_rect;
    unsigned int i;
    VkResult vr;

    if (!rect_count)
    {
        full_rect.top = 0;
        full_rect.left = 0;
        full_rect.bottom = height;
        full_rect.right = width;

        rect_count = 1;
        rects = &full_rect;
    }

    sub_pass_desc.flags = 0;
    sub_pass_desc.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub_pass_desc.inputAttachmentCount = 0;
    sub_pass_desc.pInputAttachments = NULL;
    sub_pass_desc.colorAttachmentCount = !!color_reference;
    sub_pass_desc.pColorAttachments = color_reference;
    sub_pass_desc.pResolveAttachments = NULL;
    sub_pass_desc.pDepthStencilAttachment = ds_reference;
    sub_pass_desc.preserveAttachmentCount = 0;
    sub_pass_desc.pPreserveAttachments = NULL;

    pass_desc.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    pass_desc.pNext = NULL;
    pass_desc.flags = 0;
    pass_desc.attachmentCount = 1;
    pass_desc.pAttachments = attachment_desc;
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
        WARN("Failed to add render pass.\n");
        VK_CALL(vkDestroyRenderPass(list->device->vk_device, vk_render_pass, NULL));
        return;
    }

    fb_desc.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fb_desc.pNext = NULL;
    fb_desc.flags = 0;
    fb_desc.renderPass = vk_render_pass;
    fb_desc.attachmentCount = 1;
    fb_desc.pAttachments = &vk_view;
    fb_desc.width = width;
    fb_desc.height = height;
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
    begin_desc.pClearValues = clear_value;

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

static void STDMETHODCALLTYPE d3d12_command_list_ClearDepthStencilView(ID3D12GraphicsCommandList *iface,
        D3D12_CPU_DESCRIPTOR_HANDLE dsv, D3D12_CLEAR_FLAGS flags, float depth, UINT8 stencil,
        UINT rect_count, const D3D12_RECT *rects)
{
    const union VkClearValue clear_value = {.depthStencil = {depth, stencil}};
    struct d3d12_command_list *list = impl_from_ID3D12GraphicsCommandList(iface);
    struct d3d12_dsv_desc *dsv_desc = (struct d3d12_dsv_desc *)dsv.ptr;
    struct VkAttachmentDescription attachment_desc;
    struct VkAttachmentReference ds_reference;

    TRACE("iface %p, dsv %#lx, flags %#x, depth %.8e, stencil 0x%02x, rect_count %u, rects %p.\n",
            iface, dsv.ptr, flags, depth, stencil, rect_count, rects);

    d3d12_command_list_track_resource_usage(list, dsv_desc->resource);

    attachment_desc.flags = 0;
    attachment_desc.format = dsv_desc->format;
    attachment_desc.samples = VK_SAMPLE_COUNT_1_BIT;
    if (flags & D3D12_CLEAR_FLAG_DEPTH)
    {
        attachment_desc.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachment_desc.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    }
    else
    {
        attachment_desc.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachment_desc.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    }
    if (flags & D3D12_CLEAR_FLAG_STENCIL)
    {
        attachment_desc.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachment_desc.stencilStoreOp = VK_ATTACHMENT_STORE_OP_STORE;
    }
    else
    {
        attachment_desc.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachment_desc.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    }
    attachment_desc.initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    attachment_desc.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    ds_reference.attachment = 0;
    ds_reference.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    d3d12_command_list_clear(list, &list->device->vk_procs, &attachment_desc, NULL, &ds_reference,
            dsv_desc->vk_view, dsv_desc->width, dsv_desc->height, &clear_value, rect_count, rects);
}

static void STDMETHODCALLTYPE d3d12_command_list_ClearRenderTargetView(ID3D12GraphicsCommandList *iface,
        D3D12_CPU_DESCRIPTOR_HANDLE rtv, const FLOAT color[4], UINT rect_count, const D3D12_RECT *rects)
{
    const union VkClearValue clear_value = {{{color[0], color[1], color[2], color[3]}}};
    struct d3d12_command_list *list = impl_from_ID3D12GraphicsCommandList(iface);
    struct d3d12_rtv_desc *rtv_desc = (struct d3d12_rtv_desc *)rtv.ptr;
    struct VkAttachmentDescription attachment_desc;
    struct VkAttachmentReference color_reference;

    TRACE("iface %p, rtv %#lx, color %p, rect_count %u, rects %p.\n",
            iface, rtv.ptr, color, rect_count, rects);

    d3d12_command_list_track_resource_usage(list, rtv_desc->resource);

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

    d3d12_command_list_clear(list, &list->device->vk_procs, &attachment_desc, &color_reference, NULL,
            rtv_desc->vk_view, rtv_desc->width, rtv_desc->height, &clear_value, rect_count, rects);
}

static void STDMETHODCALLTYPE d3d12_command_list_ClearUnorderedAccessViewUint(ID3D12GraphicsCommandList *iface,
        D3D12_GPU_DESCRIPTOR_HANDLE gpu_handle, D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle, ID3D12Resource *resource,
        const UINT values[4], UINT rect_count, const D3D12_RECT *rects)
{
    struct d3d12_command_list *list = impl_from_ID3D12GraphicsCommandList(iface);

    FIXME("iface %p, gpu_handle %#"PRIx64", cpu_handle %lx, resource %p, values %p, rect_count %u, rects %p stub!\n",
            iface, gpu_handle.ptr, cpu_handle.ptr, resource, values, rect_count, rects);

    d3d12_command_list_track_resource_usage(list, unsafe_impl_from_ID3D12Resource(resource));
}

static void STDMETHODCALLTYPE d3d12_command_list_ClearUnorderedAccessViewFloat(ID3D12GraphicsCommandList *iface,
        D3D12_GPU_DESCRIPTOR_HANDLE gpu_handle, D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle, ID3D12Resource *resource,
        const float values[4], UINT rect_count, const D3D12_RECT *rects)
{
    struct d3d12_command_list *list = impl_from_ID3D12GraphicsCommandList(iface);

    FIXME("iface %p, gpu_handle %#"PRIx64", cpu_handle %lx, resource %p, values %p, rect_count %u, rects %p stub!\n",
            iface, gpu_handle.ptr, cpu_handle.ptr, resource, values, rect_count, rects);

    d3d12_command_list_track_resource_usage(list, unsafe_impl_from_ID3D12Resource(resource));
}

static void STDMETHODCALLTYPE d3d12_command_list_DiscardResource(ID3D12GraphicsCommandList *iface,
        ID3D12Resource *resource, const D3D12_DISCARD_REGION *region)
{
    FIXME("iface %p, resource %p, region %p stub!\n", iface, resource, region);
}

static D3D12_QUERY_HEAP_TYPE vkd3d_query_heap_type_from_query_type(D3D12_QUERY_TYPE type)
{
    switch (type)
    {
        case D3D12_QUERY_TYPE_OCCLUSION:
        case D3D12_QUERY_TYPE_BINARY_OCCLUSION:
            return D3D12_QUERY_HEAP_TYPE_OCCLUSION;

        case D3D12_QUERY_TYPE_TIMESTAMP:
            return D3D12_QUERY_HEAP_TYPE_TIMESTAMP;

        case D3D12_QUERY_TYPE_PIPELINE_STATISTICS:
            return D3D12_QUERY_HEAP_TYPE_PIPELINE_STATISTICS;

        case D3D12_QUERY_TYPE_SO_STATISTICS_STREAM0:
        case D3D12_QUERY_TYPE_SO_STATISTICS_STREAM1:
        case D3D12_QUERY_TYPE_SO_STATISTICS_STREAM2:
        case D3D12_QUERY_TYPE_SO_STATISTICS_STREAM3:
            return D3D12_QUERY_HEAP_TYPE_SO_STATISTICS;

        default:
            WARN("Invalid query type %#x.\n", type);
            return (D3D12_QUERY_HEAP_TYPE)-1;
    }
}

static void STDMETHODCALLTYPE d3d12_command_list_BeginQuery(ID3D12GraphicsCommandList *iface,
        ID3D12QueryHeap *heap, D3D12_QUERY_TYPE type, UINT index)
{
    struct d3d12_command_list *list = impl_from_ID3D12GraphicsCommandList(iface);
    struct d3d12_query_heap *query_heap = unsafe_impl_from_ID3D12QueryHeap(heap);
    const struct vkd3d_vk_device_procs *vk_procs = &list->device->vk_procs;

    TRACE("iface %p, heap %p, type %#x, index %u.\n", iface, heap, type, index);

    if (query_heap->desc.Type != vkd3d_query_heap_type_from_query_type(type))
    {
        WARN("Query type %u not supported by query heap.\n", type);
        return;
    }

    switch (type)
    {
        case D3D12_QUERY_TYPE_TIMESTAMP:
            WARN("BeginQuery does not work with timestamp queries");
            return;

        case D3D12_QUERY_TYPE_PIPELINE_STATISTICS:
            VK_CALL(vkCmdBeginQuery(list->vk_command_buffer, query_heap->vk_query_pool, index, 0));
            return;

        case D3D12_QUERY_TYPE_OCCLUSION:
        case D3D12_QUERY_TYPE_BINARY_OCCLUSION:
        case D3D12_QUERY_TYPE_SO_STATISTICS_STREAM0:
        case D3D12_QUERY_TYPE_SO_STATISTICS_STREAM1:
        case D3D12_QUERY_TYPE_SO_STATISTICS_STREAM2:
        case D3D12_QUERY_TYPE_SO_STATISTICS_STREAM3:
            FIXME("Unhandled query type %#x.\n", type);
            return;
    }
}

static void STDMETHODCALLTYPE d3d12_command_list_EndQuery(ID3D12GraphicsCommandList *iface,
        ID3D12QueryHeap *heap, D3D12_QUERY_TYPE type, UINT index)
{
    struct d3d12_command_list *list = impl_from_ID3D12GraphicsCommandList(iface);
    struct d3d12_query_heap *query_heap = unsafe_impl_from_ID3D12QueryHeap(heap);
    const struct vkd3d_vk_device_procs *vk_procs = &list->device->vk_procs;

    TRACE("iface %p, heap %p, type %#x, index %u.\n", iface, heap, type, index);

    if (query_heap->desc.Type != vkd3d_query_heap_type_from_query_type(type))
    {
        WARN("Query type %u not supported by query heap.\n", type);
        return;
    }

    switch (type)
    {
        case D3D12_QUERY_TYPE_TIMESTAMP:
            VK_CALL(vkCmdWriteTimestamp(list->vk_command_buffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                    query_heap->vk_query_pool, index));
            return;

        case D3D12_QUERY_TYPE_PIPELINE_STATISTICS:
            VK_CALL(vkCmdEndQuery(list->vk_command_buffer, query_heap->vk_query_pool, index));
            return;

        case D3D12_QUERY_TYPE_OCCLUSION:
        case D3D12_QUERY_TYPE_BINARY_OCCLUSION:
        case D3D12_QUERY_TYPE_SO_STATISTICS_STREAM0:
        case D3D12_QUERY_TYPE_SO_STATISTICS_STREAM1:
        case D3D12_QUERY_TYPE_SO_STATISTICS_STREAM2:
        case D3D12_QUERY_TYPE_SO_STATISTICS_STREAM3:
            FIXME("Unhandled query type %#x.\n", type);
            return;
    }
}

static void STDMETHODCALLTYPE d3d12_command_list_ResolveQueryData(ID3D12GraphicsCommandList *iface,
        ID3D12QueryHeap *heap, D3D12_QUERY_TYPE type, UINT start_index, UINT query_count,
        ID3D12Resource *dst_buffer, UINT64 aligned_dst_buffer_offset)
{
    struct d3d12_command_list *list = impl_from_ID3D12GraphicsCommandList(iface);
    struct d3d12_query_heap *query_heap = unsafe_impl_from_ID3D12QueryHeap(heap);
    struct d3d12_resource *buffer = unsafe_impl_from_ID3D12Resource(dst_buffer);
    const struct vkd3d_vk_device_procs *vk_procs = &list->device->vk_procs;

    TRACE("iface %p, heap %p, type %#x, start_index %u, query_count %u, "
            "dst_buffer %p, aligned_dst_buffer_offset %#"PRIx64".\n",
            iface, heap, type, start_index, query_count,
            dst_buffer, aligned_dst_buffer_offset);

    if (query_heap->desc.Type != vkd3d_query_heap_type_from_query_type(type))
    {
        WARN("Query type %u not supported by query heap.\n", type);
        return;
    }
    if ((start_index + query_count) > query_heap->desc.Count)
    {
        WARN("Query indices out of range (%u + %u > %u).\n", start_index, query_count, query_heap->desc.Count);
        return;
    }
    if ((aligned_dst_buffer_offset % 8) != 0)
    {
        WARN("Buffer offset is not a multiple of eight (%"PRIu64".\n", aligned_dst_buffer_offset);
        return;
    }
    if (buffer->desc.Dimension != D3D12_RESOURCE_DIMENSION_BUFFER)
    {
        WARN("Destination resource is not a buffer.\n");
        return;
    }
    if ((aligned_dst_buffer_offset + 8) > buffer->desc.Width)
    {
        WARN("Would overflow destination buffer (%"PRIu64" + 8 > %"PRIu64").\n",
                aligned_dst_buffer_offset, buffer->desc.Width);
        return;
    }

    if (type != D3D12_QUERY_TYPE_TIMESTAMP)
    {
        FIXME("Unhandled query type %#x.\n", type);
        return;
    }

    VK_CALL(vkCmdCopyQueryPoolResults(list->vk_command_buffer, query_heap->vk_query_pool, start_index,
            query_count, buffer->u.vk_buffer, aligned_dst_buffer_offset, 8,
            VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT));
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
    d3d12_command_list_ClearUnorderedAccessViewUint,
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

    if (FAILED(hr = d3d12_command_allocator_allocate_command_buffer(allocator, list)))
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
    memset(list->pipeline_bindings, 0, sizeof(list->pipeline_bindings));

    list->state = NULL;

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
    struct d3d12_command_queue *command_queue = impl_from_ID3D12CommandQueue(iface);
    struct d3d12_device *device = command_queue->device;

    TRACE("iface %p, frequency %p.\n", iface, frequency);

    *frequency = 1000000000 / device->vk_info.device_limits.timestampPeriod;

    return S_OK;
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
        case D3D12_COMMAND_LIST_TYPE_COMPUTE:
            queue_family_index = device->compute_queue_family_index;
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
    queue->vk_queue_family_index = queue_family_index;

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

VkQueue vkd3d_get_vk_queue(ID3D12CommandQueue *queue)
{
    struct d3d12_command_queue *d3d12_queue = impl_from_ID3D12CommandQueue(queue);

    return d3d12_queue->vk_queue;
}

uint32_t vkd3d_get_vk_queue_family_index(ID3D12CommandQueue *queue)
{
    struct d3d12_command_queue *d3d12_queue = impl_from_ID3D12CommandQueue(queue);

    return d3d12_queue->vk_queue_family_index;
}

/* ID3D12CommandSignature */
static inline struct d3d12_command_signature *impl_from_ID3D12CommandSignature(ID3D12CommandSignature *iface)
{
    return CONTAINING_RECORD(iface, struct d3d12_command_signature, ID3D12CommandSignature_iface);
}

static HRESULT STDMETHODCALLTYPE d3d12_command_signature_QueryInterface(ID3D12CommandSignature *iface,
        REFIID iid, void **out)
{
    TRACE("iface %p, iid %s, out %p.\n", iface, debugstr_guid(iid), out);

    if (IsEqualGUID(iid, &IID_ID3D12CommandSignature)
            || IsEqualGUID(iid, &IID_ID3D12Pageable)
            || IsEqualGUID(iid, &IID_ID3D12DeviceChild)
            || IsEqualGUID(iid, &IID_ID3D12Object)
            || IsEqualGUID(iid, &IID_IUnknown))
    {
        ID3D12CommandSignature_AddRef(iface);
        *out = iface;
        return S_OK;
    }

    WARN("%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid(iid));

    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE d3d12_command_signature_AddRef(ID3D12CommandSignature *iface)
{
    struct d3d12_command_signature *signature = impl_from_ID3D12CommandSignature(iface);
    ULONG refcount = InterlockedIncrement(&signature->refcount);

    TRACE("%p increasing refcount to %u.\n", signature, refcount);

    return refcount;
}

static ULONG STDMETHODCALLTYPE d3d12_command_signature_Release(ID3D12CommandSignature *iface)
{
    struct d3d12_command_signature *signature = impl_from_ID3D12CommandSignature(iface);
    ULONG refcount = InterlockedDecrement(&signature->refcount);

    TRACE("%p decreasing refcount to %u.\n", signature, refcount);

    if (!refcount)
    {
        struct d3d12_device *device = signature->device;

        vkd3d_free(signature);

        ID3D12Device_Release(&device->ID3D12Device_iface);
    }

    return refcount;
}

static HRESULT STDMETHODCALLTYPE d3d12_command_signature_GetPrivateData(ID3D12CommandSignature *iface,
        REFGUID guid, UINT *data_size, void *data)
{
    FIXME("iface %p, guid %s, data_size %p, data %p stub!", iface, debugstr_guid(guid), data_size, data);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d12_command_signature_SetPrivateData(ID3D12CommandSignature *iface,
        REFGUID guid, UINT data_size, const void *data)
{
    FIXME("iface %p, guid %s, data_size %u, data %p stub!\n", iface, debugstr_guid(guid), data_size, data);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d12_command_signature_SetPrivateDataInterface(ID3D12CommandSignature *iface,
        REFGUID guid, const IUnknown *data)
{
    FIXME("iface %p, guid %s, data %p stub!\n", iface, debugstr_guid(guid), data);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d12_command_signature_SetName(ID3D12CommandSignature *iface, const WCHAR *name)
{
    FIXME("iface %p, name %s stub!\n", iface, debugstr_w(name));

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d12_command_signature_GetDevice(ID3D12CommandSignature *iface,
        REFIID iid, void **device)
{
    struct d3d12_command_signature *signature = impl_from_ID3D12CommandSignature(iface);

    TRACE("iface %p, iid %s, device %p.\n", iface, debugstr_guid(iid), device);

    return ID3D12Device_QueryInterface(&signature->device->ID3D12Device_iface, iid, device);
}

static const struct ID3D12CommandSignatureVtbl d3d12_command_signature_vtbl =
{
    /* IUnknown methods */
    d3d12_command_signature_QueryInterface,
    d3d12_command_signature_AddRef,
    d3d12_command_signature_Release,
    /* ID3D12Object methods */
    d3d12_command_signature_GetPrivateData,
    d3d12_command_signature_SetPrivateData,
    d3d12_command_signature_SetPrivateDataInterface,
    d3d12_command_signature_SetName,
    /* ID3D12DeviceChild methods */
    d3d12_command_signature_GetDevice,
};

HRESULT d3d12_command_signature_create(struct d3d12_device *device, struct d3d12_command_signature **signature)
{
    struct d3d12_command_signature *object;

    if (!(object = vkd3d_malloc(sizeof(*object))))
        return E_OUTOFMEMORY;

    object->ID3D12CommandSignature_iface.lpVtbl = &d3d12_command_signature_vtbl;
    object->refcount = 1;
    object->device = device;
    ID3D12Device_AddRef(&device->ID3D12Device_iface);

    TRACE("Created command signature %p.\n", object);

    *signature = object;

    return S_OK;
}
