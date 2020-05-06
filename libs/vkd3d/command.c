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

static HRESULT d3d12_fence_signal(struct d3d12_fence *fence, uint64_t value);
static void d3d12_command_queue_add_submission(struct d3d12_command_queue *queue,
        const struct d3d12_command_queue_submission *sub);

HRESULT vkd3d_queue_create(struct d3d12_device *device,
        uint32_t family_index, const VkQueueFamilyProperties *properties, struct vkd3d_queue **queue)
{
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    struct vkd3d_queue *object;
    int rc;

    if (!(object = vkd3d_malloc(sizeof(*object))))
        return E_OUTOFMEMORY;

    if ((rc = pthread_mutex_init(&object->mutex, NULL)))
    {
        ERR("Failed to initialize mutex, error %d.\n", rc);
        vkd3d_free(object);
        return hresult_from_errno(rc);
    }

    object->vk_family_index = family_index;
    object->vk_queue_flags = properties->queueFlags;
    object->timestamp_bits = properties->timestampValidBits;

    VK_CALL(vkGetDeviceQueue(device->vk_device, family_index, 0, &object->vk_queue));

    TRACE("Created queue %p for queue family index %u.\n", object, family_index);

    *queue = object;

    return S_OK;
}

void vkd3d_queue_destroy(struct vkd3d_queue *queue, struct d3d12_device *device)
{
    int rc;

    if ((rc = pthread_mutex_lock(&queue->mutex)))
        ERR("Failed to lock mutex, error %d.\n", rc);

    if (!rc)
        pthread_mutex_unlock(&queue->mutex);

    pthread_mutex_destroy(&queue->mutex);
    vkd3d_free(queue);
}

VkQueue vkd3d_queue_acquire(struct vkd3d_queue *queue)
{
    int rc;

    TRACE("queue %p.\n", queue);

    if ((rc = pthread_mutex_lock(&queue->mutex)))
    {
        ERR("Failed to lock mutex, error %d.\n", rc);
        return VK_NULL_HANDLE;
    }

    assert(queue->vk_queue);
    return queue->vk_queue;
}

void vkd3d_queue_release(struct vkd3d_queue *queue)
{
    TRACE("queue %p.\n", queue);

    pthread_mutex_unlock(&queue->mutex);
}

static VkResult vkd3d_queue_wait_idle(struct vkd3d_queue *queue,
        const struct vkd3d_vk_device_procs *vk_procs)
{
    VkQueue vk_queue;
    VkResult vr;

    if ((vk_queue = vkd3d_queue_acquire(queue)))
    {
        vr = VK_CALL(vkQueueWaitIdle(vk_queue));
        vkd3d_queue_release(queue);

        if (vr < 0)
            WARN("Failed to wait for queue, vr %d.\n", vr);
    }
    else
    {
        ERR("Failed to acquire queue %p.\n", queue);
        vr = VK_ERROR_OUT_OF_HOST_MEMORY;
    }

    return vr;
}

static HRESULT vkd3d_create_timeline_semaphore(struct d3d12_device *device, uint64_t initial_value, VkSemaphore *vk_semaphore)
{
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    VkSemaphoreTypeCreateInfoKHR type_info;
    VkSemaphoreCreateInfo info;
    VkResult vr;

    type_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO_KHR;
    type_info.pNext = NULL;
    type_info.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE_KHR;
    type_info.initialValue = initial_value;

    info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    info.pNext = &type_info;
    info.flags = 0;

    if ((vr = VK_CALL(vkCreateSemaphore(device->vk_device, &info, NULL, vk_semaphore))) < 0)
        ERR("Failed to create timeline semaphore, vr %d.\n", vr);

    return hresult_from_vk_result(vr);
}

static HRESULT vkd3d_enqueue_timeline_semaphore(struct vkd3d_fence_worker *worker,
        struct d3d12_fence *fence, uint64_t value, struct vkd3d_queue *queue)
{
    struct vkd3d_waiting_fence *waiting_fence;
    int rc;

    TRACE("worker %p, fence %p, value %#"PRIx64".\n", worker, fence, value);

    if ((rc = pthread_mutex_lock(&worker->mutex)))
    {
        ERR("Failed to lock mutex, error %d.\n", rc);
        return hresult_from_errno(rc);
    }

    if (!vkd3d_array_reserve((void **)&worker->enqueued_fences, &worker->enqueued_fences_size,
                             worker->enqueued_fence_count + 1, sizeof(*worker->enqueued_fences)))
    {
        ERR("Failed to add GPU timeline semaphore.\n");
        pthread_mutex_unlock(&worker->mutex);
        return E_OUTOFMEMORY;
    }

    worker->enqueued_fences[worker->enqueued_fence_count].vk_semaphore = fence->timeline_semaphore;
    waiting_fence = &worker->enqueued_fences[worker->enqueued_fence_count].waiting_fence;
    waiting_fence->fence = fence;
    waiting_fence->value = value;
    waiting_fence->queue = queue;
    ++worker->enqueued_fence_count;

    InterlockedIncrement(&fence->pending_worker_operation_count);

    pthread_cond_signal(&worker->cond);
    pthread_mutex_unlock(&worker->mutex);

    return S_OK;
}

static void vkd3d_fence_worker_remove_fence(struct vkd3d_fence_worker *worker, struct d3d12_fence *fence)
{
    LONG count;
    int rc;

    if (!(count = atomic_load_acquire(&fence->pending_worker_operation_count)))
        return;

    WARN("Waiting for %u pending fence operations (fence %p).\n", count, fence);

    if ((rc = pthread_mutex_lock(&worker->mutex)))
    {
        ERR("Failed to lock mutex, error %d.\n", rc);
        return;
    }

    while ((count = atomic_load_acquire(&fence->pending_worker_operation_count)))
    {
        TRACE("Still waiting for %u pending fence operations (fence %p).\n", count, fence);

        worker->pending_fence_destruction = true;
        pthread_cond_signal(&worker->cond);

        pthread_cond_wait(&worker->fence_destruction_cond, &worker->mutex);
    }

    TRACE("Removed fence %p.\n", fence);

    pthread_mutex_unlock(&worker->mutex);
}

static void vkd3d_fence_worker_move_enqueued_fences_locked(struct vkd3d_fence_worker *worker)
{
    unsigned int i;
    size_t count;
    bool ret;

    if (!worker->enqueued_fence_count)
        return;

    count = worker->fence_count + worker->enqueued_fence_count;

    ret = vkd3d_array_reserve((void **) &worker->fences, &worker->fences_size,
                              count, sizeof(*worker->fences));

    ret &= vkd3d_array_reserve((void **) &worker->vk_semaphores, &worker->vk_semaphores_size,
                               count, sizeof(*worker->vk_semaphores));
    ret &= vkd3d_array_reserve((void **) &worker->semaphore_wait_values, &worker->semaphore_wait_values_size,
                               count, sizeof(*worker->semaphore_wait_values));

    if (!ret)
    {
        ERR("Failed to reserve memory.\n");
        return;
    }

    for (i = 0; i < worker->enqueued_fence_count; ++i)
    {
        struct vkd3d_enqueued_fence *current = &worker->enqueued_fences[i];

        worker->vk_semaphores[worker->fence_count] = current->vk_semaphore;
        worker->semaphore_wait_values[worker->fence_count] = current->waiting_fence.value;

        worker->fences[worker->fence_count] = current->waiting_fence;
        ++worker->fence_count;
    }
    assert(worker->fence_count == count);
    worker->enqueued_fence_count = 0;
}

static void vkd3d_wait_for_gpu_timeline_semaphores(struct vkd3d_fence_worker *worker)
{
    struct d3d12_device *device = worker->device;
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    VkSemaphoreWaitInfoKHR wait_info;
    VkSemaphore vk_semaphore;
    uint64_t counter_value;
    unsigned int i, j;
    HRESULT hr;
    int vr;

    if (!worker->fence_count)
        return;

    wait_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO_KHR;
    wait_info.pNext = NULL;
    wait_info.flags = VK_SEMAPHORE_WAIT_ANY_BIT_KHR;
    wait_info.pSemaphores = worker->vk_semaphores;
    wait_info.semaphoreCount = worker->fence_count;
    wait_info.pValues = worker->semaphore_wait_values;

    vr = VK_CALL(vkWaitSemaphoresKHR(device->vk_device, &wait_info, ~(uint64_t)0));
    if (vr == VK_TIMEOUT)
        return;
    if (vr != VK_SUCCESS)
    {
        ERR("Failed to wait for Vulkan timeline semaphores, vr %d.\n", vr);
        return;
    }

    for (i = 0, j = 0; i < worker->fence_count; ++i)
    {
        struct vkd3d_waiting_fence *current = &worker->fences[i];

        vk_semaphore = worker->vk_semaphores[i];
        if (!(vr = VK_CALL(vkGetSemaphoreCounterValueKHR(device->vk_device, vk_semaphore, &counter_value))) &&
            counter_value >= current->value)
        {
            TRACE("Signaling fence %p value %#"PRIx64".\n", current->fence, current->value);
            if (FAILED(hr = d3d12_fence_signal(current->fence, counter_value)))
                ERR("Failed to signal D3D12 fence, hr %#x.\n", hr);

            InterlockedDecrement(&current->fence->pending_worker_operation_count);
            continue;
        }

        if (vr != VK_NOT_READY && vr != VK_SUCCESS)
            ERR("Failed to get Vulkan semaphore status, vr %d.\n", vr);

        if (i != j)
        {
            worker->vk_semaphores[j] = worker->vk_semaphores[i];
            worker->semaphore_wait_values[j] = worker->semaphore_wait_values[i];
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

    vkd3d_set_thread_name("vkd3d_fence");

    for (;;)
    {
        vkd3d_wait_for_gpu_timeline_semaphores(worker);

        if (!worker->fence_count || atomic_load_acquire(&worker->enqueued_fence_count))
        {
            if ((rc = pthread_mutex_lock(&worker->mutex)))
            {
                ERR("Failed to lock mutex, error %d.\n", rc);
                break;
            }

            if (worker->pending_fence_destruction)
            {
                pthread_cond_broadcast(&worker->fence_destruction_cond);
                worker->pending_fence_destruction = false;
            }

            if (worker->enqueued_fence_count)
            {
                vkd3d_fence_worker_move_enqueued_fences_locked(worker);
            }
            else
            {
                if (worker->should_exit)
                {
                    pthread_mutex_unlock(&worker->mutex);
                    break;
                }

                if ((rc = pthread_cond_wait(&worker->cond, &worker->mutex)))
                {
                    ERR("Failed to wait on condition variable, error %d.\n", rc);
                    pthread_mutex_unlock(&worker->mutex);
                    break;
                }
            }

            pthread_mutex_unlock(&worker->mutex);
        }
    }

    return NULL;
}

HRESULT vkd3d_fence_worker_start(struct vkd3d_fence_worker *worker,
        struct d3d12_device *device)
{
    HRESULT hr;
    int rc;

    TRACE("worker %p.\n", worker);

    worker->should_exit = false;
    worker->pending_fence_destruction = false;
    worker->device = device;

    worker->enqueued_fence_count = 0;
    worker->enqueued_fences = NULL;
    worker->enqueued_fences_size = 0;

    worker->fence_count = 0;

    worker->fences = NULL;
    worker->fences_size = 0;

    worker->vk_semaphores = NULL;
    worker->vk_semaphores_size = 0;
    worker->semaphore_wait_values = NULL;
    worker->semaphore_wait_values_size = 0;

    if ((rc = pthread_mutex_init(&worker->mutex, NULL)))
    {
        ERR("Failed to initialize mutex, error %d.\n", rc);
        return hresult_from_errno(rc);
    }

    if ((rc = pthread_cond_init(&worker->cond, NULL)))
    {
        ERR("Failed to initialize condition variable, error %d.\n", rc);
        pthread_mutex_destroy(&worker->mutex);
        return hresult_from_errno(rc);
    }

    if ((rc = pthread_cond_init(&worker->fence_destruction_cond, NULL)))
    {
        ERR("Failed to initialize condition variable, error %d.\n", rc);
        pthread_mutex_destroy(&worker->mutex);
        pthread_cond_destroy(&worker->cond);
        return hresult_from_errno(rc);
    }

    if (FAILED(hr = vkd3d_create_thread(device->vkd3d_instance,
            vkd3d_fence_worker_main, worker, &worker->thread)))
    {
        pthread_mutex_destroy(&worker->mutex);
        pthread_cond_destroy(&worker->cond);
        pthread_cond_destroy(&worker->fence_destruction_cond);
    }

    return hr;
}

HRESULT vkd3d_fence_worker_stop(struct vkd3d_fence_worker *worker,
        struct d3d12_device *device)
{
    HRESULT hr;
    int rc;

    TRACE("worker %p.\n", worker);

    if ((rc = pthread_mutex_lock(&worker->mutex)))
    {
        ERR("Failed to lock mutex, error %d.\n", rc);
        return hresult_from_errno(rc);
    }

    worker->should_exit = true;
    pthread_cond_signal(&worker->cond);

    pthread_mutex_unlock(&worker->mutex);

    if (FAILED(hr = vkd3d_join_thread(device->vkd3d_instance, &worker->thread)))
        return hr;

    pthread_mutex_destroy(&worker->mutex);
    pthread_cond_destroy(&worker->cond);
    pthread_cond_destroy(&worker->fence_destruction_cond);

    vkd3d_free(worker->enqueued_fences);
    vkd3d_free(worker->fences);
    vkd3d_free(worker->vk_semaphores);
    vkd3d_free(worker->semaphore_wait_values);

    return S_OK;
}

static const struct d3d12_root_parameter *root_signature_get_parameter(
        const struct d3d12_root_signature *root_signature, unsigned int index)
{
    assert(index < root_signature->parameter_count);
    return &root_signature->parameters[index];
}

static const struct d3d12_root_descriptor_table *root_signature_get_descriptor_table(
        const struct d3d12_root_signature *root_signature, unsigned int index)
{
    const struct d3d12_root_parameter *p = root_signature_get_parameter(root_signature, index);
    assert(p->parameter_type == D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE);
    return &p->u.descriptor_table;
}

static const struct d3d12_root_constant *root_signature_get_32bit_constants(
        const struct d3d12_root_signature *root_signature, unsigned int index)
{
    const struct d3d12_root_parameter *p = root_signature_get_parameter(root_signature, index);
    assert(p->parameter_type == D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS);
    return &p->u.constant;
}

static const struct d3d12_root_parameter *root_signature_get_root_descriptor(
        const struct d3d12_root_signature *root_signature, unsigned int index)
{
    const struct d3d12_root_parameter *p = root_signature_get_parameter(root_signature, index);
    assert(p->parameter_type == D3D12_ROOT_PARAMETER_TYPE_CBV
        || p->parameter_type == D3D12_ROOT_PARAMETER_TYPE_SRV
        || p->parameter_type == D3D12_ROOT_PARAMETER_TYPE_UAV);
    return p;
}

/* ID3D12Fence */
static struct d3d12_fence *impl_from_ID3D12Fence(d3d12_fence_iface *iface)
{
    return CONTAINING_RECORD(iface, struct d3d12_fence, ID3D12Fence_iface);
}

static void d3d12_fence_destroy_vk_objects(struct d3d12_fence *fence)
{
    const struct vkd3d_vk_device_procs *vk_procs;
    struct d3d12_device *device = fence->device;
    int rc;

    if ((rc = pthread_mutex_lock(&fence->mutex)))
    {
        ERR("Failed to lock mutex, error %d.\n", rc);
        return;
    }

    vk_procs = &device->vk_procs;
    VK_CALL(vkDestroySemaphore(device->vk_device, fence->timeline_semaphore, NULL));
    pthread_mutex_unlock(&fence->mutex);
}

static void d3d12_fence_signal_external_events_locked(struct d3d12_fence *fence)
{
    unsigned int i, j;

    for (i = 0, j = 0; i < fence->event_count; ++i)
    {
        struct vkd3d_waiting_event *current = &fence->events[i];

        if (current->value <= fence->value)
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
}

static void d3d12_fence_block_until_pending_value_reaches_locked(struct d3d12_fence *fence, UINT64 pending_value)
{
    while (pending_value > fence->pending_timeline_value)
    {
        TRACE("Blocking wait on fence %p until it reaches 0x%"PRIx64".\n", fence, pending_value);
        pthread_cond_wait(&fence->cond, &fence->mutex);
    }
}

static void d3d12_fence_update_pending_value_locked(struct d3d12_fence *fence, UINT64 pending_value)
{
    /* If we're signalling the fence, wake up any submission threads which can now safely kick work. */
    if (pending_value > fence->pending_timeline_value)
    {
        fence->pending_timeline_value = pending_value;
        pthread_cond_broadcast(&fence->cond);
    }
}

static void d3d12_fence_lock(struct d3d12_fence *fence)
{
    pthread_mutex_lock(&fence->mutex);
}

static void d3d12_fence_unlock(struct d3d12_fence *fence)
{
    pthread_mutex_unlock(&fence->mutex);
}

static bool d3d12_fence_can_elide_wait_semaphore_locked(struct d3d12_fence *fence, uint64_t value)
{
    /* Relevant if the semaphore has been signalled already on host.
     * We should not wait on the timeline semaphore directly, we can simply submit in-place. */
    return fence->value >= value;
}

static bool d3d12_fence_can_signal_semaphore_locked(struct d3d12_fence *fence, uint64_t value)
{
    struct d3d12_device *device = fence->device;
    bool need_signal = false;

    /* If we're attempting to async signal a fence with a value which is not monotonically increasing the payload value,
     * warn about this case. Do not treat this as an error since it might work. */
    if (value > fence->pending_timeline_value)
    {
        /* Sanity check against the delta limit. Use the current fence value. */
        if (value - fence->value > device->device_info.timeline_semaphore_properties.maxTimelineSemaphoreValueDifference)
        {
            FIXME("Timeline semaphore delta is %"PRIu64", but implementation only supports a delta of %"PRIu64".\n",
                  value - fence->value, device->device_info.timeline_semaphore_properties.maxTimelineSemaphoreValueDifference);
        }

        need_signal = true;
    }
    else
    {
        FIXME("Fence %p is being signalled non-monotonically. Old pending value %"PRIu64", new pending value %"PRIu64".\n",
              fence, fence->pending_timeline_value, value);

        /* Mostly to be safe against weird, unknown use cases.
         * The pending signal might be blocked by another fence,
         * we'll base this on the actual, currently visible count value. */
        need_signal = value > fence->value;
    }

    return need_signal;
}

static HRESULT d3d12_fence_signal_cpu_timeline_semaphore(struct d3d12_fence *fence, uint64_t value)
{
    struct d3d12_device *device = fence->device;
    VkResult vr;
    int rc;

    if ((rc = pthread_mutex_lock(&fence->mutex)))
    {
        ERR("Failed to lock mutex, error %d.\n", rc);
        return hresult_from_errno(rc);
    }

    /* We must only signal a value which is greater than the pending value.
     * The pending timeline value is the highest value which is pending execution, and thus will eventually reach that value.
     * It is unsafe to attempt to signal the fence to a lower value. */
    if (value > fence->pending_timeline_value)
    {
        /* Sanity check against the delta limit. */
        if (value - fence->value > device->device_info.timeline_semaphore_properties.maxTimelineSemaphoreValueDifference)
        {
            FIXME("Timeline semaphore delta is 0x%"PRIx64", but implementation only supports a delta of 0x%"PRIx64".\n",
                  value - fence->value, device->device_info.timeline_semaphore_properties.maxTimelineSemaphoreValueDifference);
        }

        /* Normally we would use vkSignalSemaphoreKHR here, but it has some CPU performance issues on
         * both NV and AMD, and since we have threaded submission, we can simply unblock the submission thread(s)
         * which wait for the host signal to come through.
         * Any semaphore wait can be elided if wait value <= current value, so we do not need to have an up-to-date
         * timeline semaphore object. */
        d3d12_fence_update_pending_value_locked(fence, value);
        fence->value = value;
    }
    else if (value != fence->value)
    {
        FIXME("Attempting to signal fence %p with 0x%"PRIx64", but value is currently 0x%"PRIx64", with a pending signaled to 0x%"PRIx64".\n",
              fence, value, fence->value, fence->pending_timeline_value);
        vr = VK_SUCCESS;
    }

    d3d12_fence_signal_external_events_locked(fence);

    pthread_mutex_unlock(&fence->mutex);
    return hresult_from_vk_result(vr);
}

static HRESULT d3d12_fence_signal(struct d3d12_fence *fence, uint64_t value)
{
    int rc;

    if ((rc = pthread_mutex_lock(&fence->mutex)))
    {
        ERR("Failed to lock mutex, error %d.\n", rc);
        return hresult_from_errno(rc);
    }

    if (value < fence->value)
    {
        FIXME("Fence values must be monotonically increasing. Fence %p, was %"PRIx64", now %"PRIx64".\n",
              fence, fence->value, value);
    }
    else
        fence->value = value;

    d3d12_fence_signal_external_events_locked(fence);

    pthread_mutex_unlock(&fence->mutex);

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d3d12_fence_QueryInterface(d3d12_fence_iface *iface,
        REFIID riid, void **object)
{
    TRACE("iface %p, riid %s, object %p.\n", iface, debugstr_guid(riid), object);

    if (IsEqualGUID(riid, &IID_ID3D12Fence)
            || IsEqualGUID(riid, &IID_ID3D12Fence1)
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

static ULONG STDMETHODCALLTYPE d3d12_fence_AddRef(d3d12_fence_iface *iface)
{
    struct d3d12_fence *fence = impl_from_ID3D12Fence(iface);
    ULONG refcount = InterlockedIncrement(&fence->refcount);

    TRACE("%p increasing refcount to %u.\n", fence, refcount);

    return refcount;
}

static ULONG STDMETHODCALLTYPE d3d12_fence_Release(d3d12_fence_iface *iface)
{
    struct d3d12_fence *fence = impl_from_ID3D12Fence(iface);
    ULONG refcount = InterlockedDecrement(&fence->refcount);
    int rc;

    TRACE("%p decreasing refcount to %u.\n", fence, refcount);

    if (!refcount)
    {
        struct d3d12_device *device = fence->device;

        vkd3d_private_store_destroy(&fence->private_store);

        vkd3d_fence_worker_remove_fence(&device->fence_worker, fence);

        d3d12_fence_destroy_vk_objects(fence);

        vkd3d_free(fence->events);
        if ((rc = pthread_mutex_destroy(&fence->mutex)))
            ERR("Failed to destroy mutex, error %d.\n", rc);
        if ((rc = pthread_cond_destroy(&fence->cond)))
            ERR("Failed to destroy cond, error %d.\n", rc);
        vkd3d_free(fence);

        d3d12_device_release(device);
    }

    return refcount;
}

static HRESULT STDMETHODCALLTYPE d3d12_fence_GetPrivateData(d3d12_fence_iface *iface,
        REFGUID guid, UINT *data_size, void *data)
{
    struct d3d12_fence *fence = impl_from_ID3D12Fence(iface);

    TRACE("iface %p, guid %s, data_size %p, data %p.\n",
            iface, debugstr_guid(guid), data_size, data);

    return vkd3d_get_private_data(&fence->private_store, guid, data_size, data);
}

static HRESULT STDMETHODCALLTYPE d3d12_fence_SetPrivateData(d3d12_fence_iface *iface,
        REFGUID guid, UINT data_size, const void *data)
{
    struct d3d12_fence *fence = impl_from_ID3D12Fence(iface);

    TRACE("iface %p, guid %s, data_size %u, data %p.\n",
            iface, debugstr_guid(guid), data_size, data);

    return vkd3d_set_private_data(&fence->private_store, guid, data_size, data);
}

static HRESULT STDMETHODCALLTYPE d3d12_fence_SetPrivateDataInterface(d3d12_fence_iface *iface,
        REFGUID guid, const IUnknown *data)
{
    struct d3d12_fence *fence = impl_from_ID3D12Fence(iface);

    TRACE("iface %p, guid %s, data %p.\n", iface, debugstr_guid(guid), data);

    return vkd3d_set_private_data_interface(&fence->private_store, guid, data);
}

static HRESULT STDMETHODCALLTYPE d3d12_fence_SetName(d3d12_fence_iface *iface, const WCHAR *name)
{
    struct d3d12_fence *fence = impl_from_ID3D12Fence(iface);

    TRACE("iface %p, name %s.\n", iface, debugstr_w(name, fence->device->wchar_size));

    return name ? S_OK : E_INVALIDARG;
}

static HRESULT STDMETHODCALLTYPE d3d12_fence_GetDevice(d3d12_fence_iface *iface, REFIID iid, void **device)
{
    struct d3d12_fence *fence = impl_from_ID3D12Fence(iface);

    TRACE("iface %p, iid %s, device %p.\n", iface, debugstr_guid(iid), device);

    return d3d12_device_query_interface(fence->device, iid, device);
}

static UINT64 STDMETHODCALLTYPE d3d12_fence_GetCompletedValue(d3d12_fence_iface *iface)
{
    struct d3d12_fence *fence = impl_from_ID3D12Fence(iface);
    uint64_t completed_value;
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

static HRESULT STDMETHODCALLTYPE d3d12_fence_SetEventOnCompletion(d3d12_fence_iface *iface,
        UINT64 value, HANDLE event)
{
    struct d3d12_fence *fence = impl_from_ID3D12Fence(iface);
    unsigned int i;
    int rc;

    TRACE("iface %p, value %#"PRIx64", event %p.\n", iface, value, event);

    if ((rc = pthread_mutex_lock(&fence->mutex)))
    {
        ERR("Failed to lock mutex, error %d.\n", rc);
        return hresult_from_errno(rc);
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

static HRESULT STDMETHODCALLTYPE d3d12_fence_Signal(d3d12_fence_iface *iface, UINT64 value)
{
    struct d3d12_fence *fence = impl_from_ID3D12Fence(iface);

    TRACE("iface %p, value %#"PRIx64".\n", iface, value);

    return d3d12_fence_signal_cpu_timeline_semaphore(fence, value);
}

static D3D12_FENCE_FLAGS STDMETHODCALLTYPE d3d12_fence_GetCreationFlags(d3d12_fence_iface *iface)
{
    struct d3d12_fence *fence = impl_from_ID3D12Fence(iface);

    TRACE("iface %p.\n", iface);

    return fence->d3d12_flags;
}

static const struct ID3D12Fence1Vtbl d3d12_fence_vtbl =
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
    /* ID3D12Fence1 methods */
    d3d12_fence_GetCreationFlags,
};

static struct d3d12_fence *unsafe_impl_from_ID3D12Fence1(ID3D12Fence1 *iface)
{
    if (!iface)
        return NULL;
    assert(iface->lpVtbl == &d3d12_fence_vtbl);
    return impl_from_ID3D12Fence(iface);
}

static struct d3d12_fence *unsafe_impl_from_ID3D12Fence(ID3D12Fence *iface)
{
    return unsafe_impl_from_ID3D12Fence1((ID3D12Fence1 *)iface);
}

static HRESULT d3d12_fence_init_timeline(struct d3d12_fence *fence, struct d3d12_device *device,
        UINT64 initial_value)
{
    fence->pending_timeline_value = initial_value;
    return vkd3d_create_timeline_semaphore(device, initial_value, &fence->timeline_semaphore);
}

static HRESULT d3d12_fence_init(struct d3d12_fence *fence, struct d3d12_device *device,
        UINT64 initial_value, D3D12_FENCE_FLAGS flags)
{
    HRESULT hr;
    int rc;

    fence->ID3D12Fence_iface.lpVtbl = &d3d12_fence_vtbl;
    fence->refcount = 1;
    fence->d3d12_flags = flags;

    if (FAILED(hr = d3d12_fence_init_timeline(fence, device, initial_value)))
        return hr;

    fence->value = initial_value;

    if ((rc = pthread_mutex_init(&fence->mutex, NULL)))
    {
        ERR("Failed to initialize mutex, error %d.\n", rc);
        return hresult_from_errno(rc);
    }

    if ((rc = pthread_cond_init(&fence->cond, NULL)))
    {
        ERR("Failed to initialize cond variable, error %d.\n", rc);
        return hresult_from_errno(rc);
    }

    if (flags)
        FIXME("Ignoring flags %#x.\n", flags);

    fence->events = NULL;
    fence->events_size = 0;
    fence->event_count = 0;
    fence->pending_worker_operation_count = 0;

    if (FAILED(hr = vkd3d_private_store_init(&fence->private_store)))
    {
        pthread_mutex_destroy(&fence->mutex);
        return hr;
    }

    d3d12_device_add_ref(fence->device = device);

    return S_OK;
}

HRESULT d3d12_fence_create(struct d3d12_device *device,
        uint64_t initial_value, D3D12_FENCE_FLAGS flags, struct d3d12_fence **fence)
{
    struct d3d12_fence *object;
    HRESULT hr;

    if (!(object = vkd3d_malloc(sizeof(*object))))
        return E_OUTOFMEMORY;

    if (SUCCEEDED(hr = d3d12_fence_init(object, device, initial_value, flags)))
        TRACE("Created fence %p.\n", object);
    else
        ERR("Failed to create fence.\n");

    *fence = object;
    return hr;
}

/* Command buffers */
static void d3d12_command_list_mark_as_invalid(struct d3d12_command_list *list,
        const char *message, ...)
{
    va_list args;

    va_start(args, message);
    WARN("Command list %p is invalid: \"%s\".\n", list, vkd3d_dbg_vsprintf(message, args));
    va_end(args);

    list->is_valid = false;
}

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

    list->vk_queue_flags = allocator->vk_queue_flags;

    if (FAILED(hr = d3d12_command_list_begin_command_buffer(list)))
    {
        VK_CALL(vkFreeCommandBuffers(device->vk_device, allocator->vk_command_pool,
                1, &list->vk_command_buffer));
        return hr;
    }

    allocator->current_command_list = list;
    list->outstanding_submissions_count = &allocator->outstanding_submissions_count;

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

static bool d3d12_command_allocator_add_descriptor_pool(struct d3d12_command_allocator *allocator,
        VkDescriptorPool pool, enum vkd3d_descriptor_pool_types pool_type)
{
    struct d3d12_descriptor_pool_cache *cache = &allocator->descriptor_pool_caches[pool_type];

    if (!vkd3d_array_reserve((void **)&cache->descriptor_pools, &cache->descriptor_pools_size,
            cache->descriptor_pool_count + 1, sizeof(*cache->descriptor_pools)))
        return false;

    cache->descriptor_pools[cache->descriptor_pool_count++] = pool;

    return true;
}

static bool d3d12_command_allocator_add_view(struct d3d12_command_allocator *allocator,
        struct vkd3d_view *view)
{
    if (!vkd3d_array_reserve((void **)&allocator->views, &allocator->views_size,
            allocator->view_count + 1, sizeof(*allocator->views)))
        return false;

    vkd3d_view_incref(view);
    allocator->views[allocator->view_count++] = view;

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

static VkDescriptorPool d3d12_command_allocator_allocate_descriptor_pool(
        struct d3d12_command_allocator *allocator, enum vkd3d_descriptor_pool_types pool_type)
{
    static const VkDescriptorPoolSize pool_sizes[] =
    {
        /* Must be first in the array. */
        /* Need at least 2048 so we can allocate an immutable sampler set. */
        {VK_DESCRIPTOR_TYPE_SAMPLER, 2048},

        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1024},
        {VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1024},
        {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1024},
        {VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1024},
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1024},
        /* must be last in the array */
        {VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK_EXT, 65536}
    };
    struct d3d12_descriptor_pool_cache *cache = &allocator->descriptor_pool_caches[pool_type];
    struct d3d12_device *device = allocator->device;
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    VkDescriptorPoolInlineUniformBlockCreateInfoEXT inline_uniform_desc;
    VkDescriptorPoolCreateInfo pool_desc;
    VkDevice vk_device = device->vk_device;
    VkDescriptorPool vk_pool;
    VkResult vr;

    if (cache->free_descriptor_pool_count > 0)
    {
        vk_pool = cache->free_descriptor_pools[cache->free_descriptor_pool_count - 1];
        cache->free_descriptor_pools[cache->free_descriptor_pool_count - 1] = VK_NULL_HANDLE;
        --cache->free_descriptor_pool_count;
    }
    else
    {
        inline_uniform_desc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_INLINE_UNIFORM_BLOCK_CREATE_INFO_EXT;
        inline_uniform_desc.pNext = NULL;
        inline_uniform_desc.maxInlineUniformBlockBindings = 256;

        pool_desc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pool_desc.pNext = &inline_uniform_desc;
        /* For a correct implementation of RS 1.0 we need to update packed descriptor sets late rather than on draw.
         * If device does not support descriptor indexing, we must update on draw and pray applications don't rely on RS 1.0
         * guarantees. */
        pool_desc.flags = pool_type == VKD3D_DESCRIPTOR_POOL_TYPE_VOLATILE &&
                          allocator->device->vk_info.supports_volatile_packed_descriptors ?
                          VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT_EXT : 0;
        pool_desc.maxSets = 512;
        pool_desc.poolSizeCount = ARRAY_SIZE(pool_sizes);
        pool_desc.pPoolSizes = pool_sizes;

        if (pool_type == VKD3D_DESCRIPTOR_POOL_TYPE_IMMUTABLE_SAMPLER)
        {
            /* Only allocate for samplers. */
            pool_desc.poolSizeCount = 1;
        }
        else if (pool_type == VKD3D_DESCRIPTOR_POOL_TYPE_VOLATILE ||
                 !device->vk_info.EXT_inline_uniform_block ||
                 device->vk_info.device_limits.maxPushConstantsSize >= (D3D12_MAX_ROOT_COST * sizeof(uint32_t)))
        {
            /* We don't use volatile inline uniform block descriptors. */
            pool_desc.pNext = NULL;
            pool_desc.poolSizeCount -= 1;
        }

        if ((vr = VK_CALL(vkCreateDescriptorPool(vk_device, &pool_desc, NULL, &vk_pool))) < 0)
        {
            ERR("Failed to create descriptor pool, vr %d.\n", vr);
            return VK_NULL_HANDLE;
        }
    }

    if (!(d3d12_command_allocator_add_descriptor_pool(allocator, vk_pool, pool_type)))
    {
        ERR("Failed to add descriptor pool.\n");
        VK_CALL(vkDestroyDescriptorPool(vk_device, vk_pool, NULL));
        return VK_NULL_HANDLE;
    }

    return vk_pool;
}

static VkDescriptorSet d3d12_command_allocator_allocate_descriptor_set(
        struct d3d12_command_allocator *allocator, VkDescriptorSetLayout vk_set_layout,
        enum vkd3d_descriptor_pool_types pool_type)
{
    struct d3d12_descriptor_pool_cache *cache = &allocator->descriptor_pool_caches[pool_type];
    struct d3d12_device *device = allocator->device;
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    struct VkDescriptorSetAllocateInfo set_desc;
    VkDevice vk_device = device->vk_device;
    VkDescriptorSet vk_descriptor_set;
    VkResult vr;

    if (!cache->vk_descriptor_pool)
        cache->vk_descriptor_pool = d3d12_command_allocator_allocate_descriptor_pool(allocator, pool_type);
    if (!cache->vk_descriptor_pool)
        return VK_NULL_HANDLE;

    set_desc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    set_desc.pNext = NULL;
    set_desc.descriptorPool = cache->vk_descriptor_pool;
    set_desc.descriptorSetCount = 1;
    set_desc.pSetLayouts = &vk_set_layout;
    if ((vr = VK_CALL(vkAllocateDescriptorSets(vk_device, &set_desc, &vk_descriptor_set))) >= 0)
        return vk_descriptor_set;

    cache->vk_descriptor_pool = VK_NULL_HANDLE;
    if (vr == VK_ERROR_FRAGMENTED_POOL || vr == VK_ERROR_OUT_OF_POOL_MEMORY_KHR)
        cache->vk_descriptor_pool = d3d12_command_allocator_allocate_descriptor_pool(allocator, pool_type);
    if (!cache->vk_descriptor_pool)
    {
        ERR("Failed to allocate descriptor set, vr %d.\n", vr);
        return VK_NULL_HANDLE;
    }

    set_desc.descriptorPool = cache->vk_descriptor_pool;
    if ((vr = VK_CALL(vkAllocateDescriptorSets(vk_device, &set_desc, &vk_descriptor_set))) < 0)
    {
        FIXME("Failed to allocate descriptor set from a new pool, vr %d.\n", vr);
        return VK_NULL_HANDLE;
    }

    return vk_descriptor_set;
}

static void d3d12_command_list_allocator_destroyed(struct d3d12_command_list *list)
{
    TRACE("list %p.\n", list);

    list->allocator = NULL;
    list->vk_command_buffer = VK_NULL_HANDLE;
}

static void d3d12_command_allocator_free_descriptor_pool_cache(struct d3d12_command_allocator *allocator,
        struct d3d12_descriptor_pool_cache *cache, bool keep_reusable_resources)
{
    struct d3d12_device *device = allocator->device;
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    unsigned int i, j;
    cache->vk_descriptor_pool = VK_NULL_HANDLE;

    if (keep_reusable_resources)
    {
        if (vkd3d_array_reserve((void **)&cache->free_descriptor_pools,
                                &cache->free_descriptor_pools_size,
                                cache->free_descriptor_pool_count + cache->descriptor_pool_count,
                                sizeof(*cache->free_descriptor_pools)))
        {
            for (i = 0, j = cache->free_descriptor_pool_count; i < cache->descriptor_pool_count; ++i, ++j)
            {
                VK_CALL(vkResetDescriptorPool(device->vk_device, cache->descriptor_pools[i], 0));
                cache->free_descriptor_pools[j] = cache->descriptor_pools[i];
            }
            cache->free_descriptor_pool_count += cache->descriptor_pool_count;
            cache->descriptor_pool_count = 0;
        }
    }
    else
    {
        for (i = 0; i < cache->free_descriptor_pool_count; ++i)
        {
            VK_CALL(vkDestroyDescriptorPool(device->vk_device, cache->free_descriptor_pools[i], NULL));
        }
        cache->free_descriptor_pool_count = 0;
    }

    for (i = 0; i < cache->descriptor_pool_count; ++i)
    {
        VK_CALL(vkDestroyDescriptorPool(device->vk_device, cache->descriptor_pools[i], NULL));
    }
    cache->descriptor_pool_count = 0;
}

static void d3d12_command_allocator_free_resources(struct d3d12_command_allocator *allocator,
        bool keep_reusable_resources)
{
    struct d3d12_device *device = allocator->device;
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    unsigned int i;

    for (i = 0; i < VKD3D_DESCRIPTOR_POOL_TYPE_COUNT; i++)
    {
        d3d12_command_allocator_free_descriptor_pool_cache(allocator,
                &allocator->descriptor_pool_caches[i],
                keep_reusable_resources);
    }

    for (i = 0; i < allocator->buffer_view_count; ++i)
    {
        VK_CALL(vkDestroyBufferView(device->vk_device, allocator->buffer_views[i], NULL));
    }
    allocator->buffer_view_count = 0;

    for (i = 0; i < allocator->view_count; ++i)
    {
        vkd3d_view_decref(allocator->views[i], device);
    }
    allocator->view_count = 0;

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
    unsigned int i;

    TRACE("%p decreasing refcount to %u.\n", allocator, refcount);

    if (!refcount)
    {
        struct d3d12_device *device = allocator->device;
        const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;

        vkd3d_private_store_destroy(&allocator->private_store);

        if (allocator->current_command_list)
            d3d12_command_list_allocator_destroyed(allocator->current_command_list);

        d3d12_command_allocator_free_resources(allocator, false);
        vkd3d_free(allocator->buffer_views);
        vkd3d_free(allocator->views);
        for (i = 0; i < VKD3D_DESCRIPTOR_POOL_TYPE_COUNT; i++)
        {
            vkd3d_free(allocator->descriptor_pool_caches[i].descriptor_pools);
            vkd3d_free(allocator->descriptor_pool_caches[i].free_descriptor_pools);
        }
        vkd3d_free(allocator->framebuffers);
        vkd3d_free(allocator->passes);

        /* All command buffers are implicitly freed when a pool is destroyed. */
        vkd3d_free(allocator->command_buffers);
        VK_CALL(vkDestroyCommandPool(device->vk_device, allocator->vk_command_pool, NULL));

        vkd3d_free(allocator);

        d3d12_device_release(device);
    }

    return refcount;
}

static HRESULT STDMETHODCALLTYPE d3d12_command_allocator_GetPrivateData(ID3D12CommandAllocator *iface,
        REFGUID guid, UINT *data_size, void *data)
{
    struct d3d12_command_allocator *allocator = impl_from_ID3D12CommandAllocator(iface);

    TRACE("iface %p, guid %s, data_size %p, data %p.\n", iface, debugstr_guid(guid), data_size, data);

    return vkd3d_get_private_data(&allocator->private_store, guid, data_size, data);
}

static HRESULT STDMETHODCALLTYPE d3d12_command_allocator_SetPrivateData(ID3D12CommandAllocator *iface,
        REFGUID guid, UINT data_size, const void *data)
{
    struct d3d12_command_allocator *allocator = impl_from_ID3D12CommandAllocator(iface);

    TRACE("iface %p, guid %s, data_size %u, data %p.\n", iface, debugstr_guid(guid), data_size, data);

    return vkd3d_set_private_data(&allocator->private_store, guid, data_size, data);
}

static HRESULT STDMETHODCALLTYPE d3d12_command_allocator_SetPrivateDataInterface(ID3D12CommandAllocator *iface,
        REFGUID guid, const IUnknown *data)
{
    struct d3d12_command_allocator *allocator = impl_from_ID3D12CommandAllocator(iface);

    TRACE("iface %p, guid %s, data %p.\n", iface, debugstr_guid(guid), data);

    return vkd3d_set_private_data_interface(&allocator->private_store, guid, data);
}

static HRESULT STDMETHODCALLTYPE d3d12_command_allocator_SetName(ID3D12CommandAllocator *iface, const WCHAR *name)
{
    struct d3d12_command_allocator *allocator = impl_from_ID3D12CommandAllocator(iface);

    TRACE("iface %p, name %s.\n", iface, debugstr_w(name, allocator->device->wchar_size));

    return vkd3d_set_vk_object_name(allocator->device, (uint64_t)allocator->vk_command_pool,
            VK_DEBUG_REPORT_OBJECT_TYPE_COMMAND_POOL_EXT, name);
}

static HRESULT STDMETHODCALLTYPE d3d12_command_allocator_GetDevice(ID3D12CommandAllocator *iface, REFIID iid, void **device)
{
    struct d3d12_command_allocator *allocator = impl_from_ID3D12CommandAllocator(iface);

    TRACE("iface %p, iid %s, device %p.\n", iface, debugstr_guid(iid), device);

    return d3d12_device_query_interface(allocator->device, iid, device);
}

static HRESULT STDMETHODCALLTYPE d3d12_command_allocator_Reset(ID3D12CommandAllocator *iface)
{
    struct d3d12_command_allocator *allocator = impl_from_ID3D12CommandAllocator(iface);
    const struct vkd3d_vk_device_procs *vk_procs;
    struct d3d12_command_list *list;
    struct d3d12_device *device;
    LONG pending;
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

    if ((pending = atomic_load_acquire(&allocator->outstanding_submissions_count)) != 0)
    {
        /* HACK: There are currently command lists waiting to be submitted to the queue in the submission threads.
         * Buggy application, but work around this by not resetting the command pool this time.
         * To be perfectly safe, we can only reset after the fence timeline is signalled,
         * however, this is enough to workaround SotTR which resets the command list right
         * after calling ID3D12CommandQueue::ExecuteCommandLists().
         * Only happens once or twice on bootup and doesn't cause memory leaks over time
         * since the command pool is eventually reset.
         * Game does not seem to care if E_FAIL is returned, which is the correct thing to do here.
         *
         * TODO: Guard this with actual timeline semaphores from vkQueueSubmit(). */
        ERR("There are still %u pending command lists awaiting execution from command allocator iface %p!\n",
            (unsigned int)pending, iface);
        return E_FAIL;
    }

    device = allocator->device;
    vk_procs = &device->vk_procs;

    d3d12_command_allocator_free_resources(allocator, true);
    if (allocator->command_buffer_count)
    {
        VK_CALL(vkFreeCommandBuffers(device->vk_device, allocator->vk_command_pool,
                allocator->command_buffer_count, allocator->command_buffers));
        allocator->command_buffer_count = 0;
    }

    /* The intent here is to recycle memory, so do not use RELEASE_RESOURCES_BIT here. */
    if ((vr = VK_CALL(vkResetCommandPool(device->vk_device, allocator->vk_command_pool, 0))))
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

struct d3d12_command_allocator *unsafe_impl_from_ID3D12CommandAllocator(ID3D12CommandAllocator *iface)
{
    if (!iface)
        return NULL;
    assert(iface->lpVtbl == &d3d12_command_allocator_vtbl);
    return impl_from_ID3D12CommandAllocator(iface);
}

struct vkd3d_queue *d3d12_device_get_vkd3d_queue(struct d3d12_device *device,
        D3D12_COMMAND_LIST_TYPE type)
{
    switch (type)
    {
        case D3D12_COMMAND_LIST_TYPE_DIRECT:
            return device->queues[VKD3D_QUEUE_FAMILY_GRAPHICS];
        case D3D12_COMMAND_LIST_TYPE_COMPUTE:
            return device->queues[VKD3D_QUEUE_FAMILY_COMPUTE];
        case D3D12_COMMAND_LIST_TYPE_COPY:
            return device->queues[VKD3D_QUEUE_FAMILY_TRANSFER];
        default:
            FIXME("Unhandled command list type %#x.\n", type);
            return NULL;
    }
}

static HRESULT d3d12_command_allocator_init(struct d3d12_command_allocator *allocator,
        struct d3d12_device *device, D3D12_COMMAND_LIST_TYPE type)
{
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    VkCommandPoolCreateInfo command_pool_info;
    struct vkd3d_queue *queue;
    VkResult vr;
    HRESULT hr;

    if (FAILED(hr = vkd3d_private_store_init(&allocator->private_store)))
        return hr;

    if (!(queue = d3d12_device_get_vkd3d_queue(device, type)))
        queue = device->queues[VKD3D_QUEUE_FAMILY_GRAPHICS];

    allocator->ID3D12CommandAllocator_iface.lpVtbl = &d3d12_command_allocator_vtbl;
    allocator->refcount = 1;
    allocator->outstanding_submissions_count = 0;
    allocator->type = type;
    allocator->vk_queue_flags = queue->vk_queue_flags;

    command_pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    command_pool_info.pNext = NULL;
    /* Do not use RESET_COMMAND_BUFFER_BIT. This allows the CommandPool to be a D3D12-style command pool.
     * Memory is owned by the pool and CommandBuffers become lightweight handles,
     * assuming a half-decent driver implementation. */
    command_pool_info.flags = 0;
    command_pool_info.queueFamilyIndex = queue->vk_family_index;

    if ((vr = VK_CALL(vkCreateCommandPool(device->vk_device, &command_pool_info, NULL,
            &allocator->vk_command_pool))) < 0)
    {
        WARN("Failed to create Vulkan command pool, vr %d.\n", vr);
        vkd3d_private_store_destroy(&allocator->private_store);
        return hresult_from_vk_result(vr);
    }

    memset(allocator->descriptor_pool_caches, 0, sizeof(allocator->descriptor_pool_caches));

    allocator->passes = NULL;
    allocator->passes_size = 0;
    allocator->pass_count = 0;

    allocator->framebuffers = NULL;
    allocator->framebuffers_size = 0;
    allocator->framebuffer_count = 0;


    allocator->views = NULL;
    allocator->views_size = 0;
    allocator->view_count = 0;

    allocator->buffer_views = NULL;
    allocator->buffer_views_size = 0;
    allocator->buffer_view_count = 0;

    allocator->command_buffers = NULL;
    allocator->command_buffers_size = 0;
    allocator->command_buffer_count = 0;

    allocator->current_command_list = NULL;

    d3d12_device_add_ref(allocator->device = device);

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
static inline struct d3d12_command_list *impl_from_ID3D12GraphicsCommandList(d3d12_command_list_iface *iface)
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

static bool d3d12_command_list_create_framebuffer(struct d3d12_command_list *list, VkRenderPass render_pass,
        uint32_t view_count, const VkImageView *views, VkExtent3D extent, VkFramebuffer *vk_framebuffer);

static D3D12_RECT d3d12_get_view_rect(struct d3d12_resource *resource, struct vkd3d_view *view)
{
    D3D12_RECT rect;
    rect.left = 0;
    rect.top = 0;
    rect.right = d3d12_resource_desc_get_width(&resource->desc, view->info.texture.miplevel_idx);
    rect.bottom = d3d12_resource_desc_get_height(&resource->desc, view->info.texture.miplevel_idx);
    return rect;
}

static bool vk_rect_from_d3d12(const D3D12_RECT *rect, VkRect2D *vk_rect)
{
    if (rect->top >= rect->bottom || rect->left >= rect->right)
    {
        WARN("Empty clear rect.\n");
        return false;
    }

    vk_rect->offset.x = rect->left;
    vk_rect->offset.y = rect->top;
    vk_rect->extent.width = rect->right - rect->left;
    vk_rect->extent.height = rect->bottom - rect->top;
    return true;
}

static VkImageAspectFlags vk_writable_aspects_from_image_layout(VkImageLayout vk_layout)
{
    switch (vk_layout)
    {
        case VK_IMAGE_LAYOUT_GENERAL:
            return VK_IMAGE_ASPECT_COLOR_BIT | VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
        case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
            return VK_IMAGE_ASPECT_COLOR_BIT;
        case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
            return VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
        case VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_STENCIL_ATTACHMENT_OPTIMAL:
            return VK_IMAGE_ASPECT_STENCIL_BIT;
        case VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_STENCIL_READ_ONLY_OPTIMAL:
            return VK_IMAGE_ASPECT_DEPTH_BIT;
        case VK_IMAGE_LAYOUT_UNDEFINED:
        case VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL:
            return 0;
        default:
            ERR("Unhandled image layout %u.\n", vk_layout);
            return 0;
    }
}

static int d3d12_command_list_find_attachment(struct d3d12_command_list *list,
        const struct d3d12_resource *resource, const struct vkd3d_view *view)
{
    unsigned int i;

    if (list->dsv.resource == resource)
    {
        const struct vkd3d_view *dsv = list->dsv.view;

        if (dsv->info.texture.miplevel_idx == view->info.texture.miplevel_idx &&
                dsv->info.texture.layer_idx == view->info.texture.layer_idx &&
                dsv->info.texture.layer_count == view->info.texture.layer_count)
            return D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT;
    }
    else
    {
        for (i = 0; i < D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT; i++)
        {
            const struct vkd3d_view *rtv = list->rtvs[i].view;

            if (list->rtvs[i].resource != resource)
                continue;

            if (rtv->info.texture.miplevel_idx == view->info.texture.miplevel_idx &&
                    rtv->info.texture.layer_idx == view->info.texture.layer_idx &&
                    rtv->info.texture.layer_count == view->info.texture.layer_count)
                return i;
        }
    }

    return -1;
}

static void d3d12_command_list_clear_attachment_inline(struct d3d12_command_list *list, struct d3d12_resource *resource,
        struct vkd3d_view *view, unsigned int attachment_idx, VkImageAspectFlags clear_aspects,
        const VkClearValue *clear_value, UINT rect_count, const D3D12_RECT *rects)
{
    const struct vkd3d_vk_device_procs *vk_procs = &list->device->vk_procs;
    VkClearAttachment vk_clear_attachment;
    VkClearRect vk_clear_rect;
    D3D12_RECT full_rect;
    unsigned int i;

    if (!rect_count)
    {
        full_rect = d3d12_get_view_rect(resource, view);
        rect_count = 1;
        rects = &full_rect;
    }

    /* We expect more than one clear rect to be very uncommon
     * in practice, so make no effort to batch calls for now.
     * colorAttachment is ignored for depth-stencil clears. */
    vk_clear_attachment.aspectMask = clear_aspects;
    vk_clear_attachment.colorAttachment = attachment_idx;
    vk_clear_attachment.clearValue = *clear_value;

    vk_clear_rect.baseArrayLayer = 0;
    vk_clear_rect.layerCount = view->info.texture.layer_count;

    for (i = 0; i < rect_count; i++)
    {
        if (vk_rect_from_d3d12(&rects[i], &vk_clear_rect.rect))
        {
            VK_CALL(vkCmdClearAttachments(list->vk_command_buffer,
                    1, &vk_clear_attachment, 1, &vk_clear_rect));
        }
    }
}

static void d3d12_command_list_clear_attachment_pass(struct d3d12_command_list *list, struct d3d12_resource *resource,
        struct vkd3d_view *view, VkImageAspectFlags clear_aspects, const VkClearValue *clear_value, UINT rect_count,
        const D3D12_RECT *rects)
{
    const struct vkd3d_vk_device_procs *vk_procs = &list->device->vk_procs;
    VkAttachmentDescription attachment_desc;
    VkAttachmentReference attachment_ref;
    VkSubpassDependency dependencies[2];
    VkSubpassDescription subpass_desc;
    VkRenderPassBeginInfo begin_info;
    VkRenderPassCreateInfo pass_info;
    VkFramebuffer vk_framebuffer;
    VkRenderPass vk_render_pass;
    VkPipelineStageFlags stages;
    VkAccessFlags access;
    VkExtent3D extent;
    bool clear_op;
    VkResult vr;

    attachment_desc.flags = 0;
    attachment_desc.format = view->format->vk_format;
    attachment_desc.samples = vk_samples_from_dxgi_sample_desc(&resource->desc.SampleDesc);
    attachment_desc.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    attachment_desc.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachment_desc.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    attachment_desc.stencilStoreOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachment_desc.initialLayout = resource->common_layout;
    attachment_desc.finalLayout = resource->common_layout;

    attachment_ref.attachment = 0;
    attachment_ref.layout = view->info.texture.vk_layout;

    subpass_desc.flags = 0;
    subpass_desc.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass_desc.inputAttachmentCount = 0;
    subpass_desc.pInputAttachments = NULL;
    subpass_desc.colorAttachmentCount = 0;
    subpass_desc.pColorAttachments = NULL;
    subpass_desc.pResolveAttachments = NULL;
    subpass_desc.pDepthStencilAttachment = NULL;
    subpass_desc.preserveAttachmentCount = 0;
    subpass_desc.pPreserveAttachments = NULL;

    if ((clear_op = !rect_count))
    {
        if (clear_aspects & (VK_IMAGE_ASPECT_COLOR_BIT | VK_IMAGE_ASPECT_DEPTH_BIT))
            attachment_desc.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;

        if (clear_aspects & (VK_IMAGE_ASPECT_STENCIL_BIT))
            attachment_desc.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;

        /* Ignore 3D images as re-initializing those may cause us to
         * discard the entire image, not just the layers to clear. */
        if (clear_aspects == view->format->vk_aspect_mask &&
                resource->desc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE3D)
            attachment_desc.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    }

    if (clear_aspects & (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT))
    {
        stages = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        access = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

        if (!clear_op || clear_aspects != view->format->vk_aspect_mask)
            access |= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;

        subpass_desc.pDepthStencilAttachment = &attachment_ref;
    }
    else
    {
        stages = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        access = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

        if (!clear_op)
            access |= VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;

        subpass_desc.colorAttachmentCount = 1;
        subpass_desc.pColorAttachments = &attachment_ref;
    }

    dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[0].dstSubpass = 0;
    dependencies[0].srcStageMask = stages;
    dependencies[0].dstStageMask = stages;
    dependencies[0].srcAccessMask = 0;
    dependencies[0].dstAccessMask = access;
    dependencies[0].dependencyFlags = 0;

    dependencies[1].srcSubpass = 0;
    dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[1].srcStageMask = stages;
    dependencies[1].dstStageMask = stages;
    dependencies[1].srcAccessMask = access;
    dependencies[1].dstAccessMask = 0;
    dependencies[1].dependencyFlags = 0;

    pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    pass_info.pNext = NULL;
    pass_info.flags = 0;
    pass_info.attachmentCount = 1;
    pass_info.pAttachments = &attachment_desc;
    pass_info.subpassCount = 1;
    pass_info.pSubpasses = &subpass_desc;
    pass_info.dependencyCount = ARRAY_SIZE(dependencies);
    pass_info.pDependencies = dependencies;

    if ((vr = VK_CALL(vkCreateRenderPass(list->device->vk_device, &pass_info, NULL, &vk_render_pass))) < 0)
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

    if (!d3d12_command_allocator_add_view(list->allocator, view))
        WARN("Failed to add view.\n");

    extent.width = d3d12_resource_desc_get_width(&resource->desc, view->info.texture.miplevel_idx);
    extent.height = d3d12_resource_desc_get_height(&resource->desc, view->info.texture.miplevel_idx);
    extent.depth = view->info.texture.layer_count;

    if (!d3d12_command_list_create_framebuffer(list, vk_render_pass, 1, &view->u.vk_image_view, extent, &vk_framebuffer))
    {
        ERR("Failed to create framebuffer.\n");
        return;
    }

    begin_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    begin_info.pNext = NULL;
    begin_info.renderPass = vk_render_pass;
    begin_info.framebuffer = vk_framebuffer;
    begin_info.renderArea.offset.x = 0;
    begin_info.renderArea.offset.y = 0;
    begin_info.renderArea.extent.width = extent.width;
    begin_info.renderArea.extent.height = extent.height;
    begin_info.clearValueCount = clear_op ? 1 : 0;
    begin_info.pClearValues = clear_op ? clear_value : NULL;

    VK_CALL(vkCmdBeginRenderPass(list->vk_command_buffer,
            &begin_info, VK_SUBPASS_CONTENTS_INLINE));

    if (!clear_op)
    {
        d3d12_command_list_clear_attachment_inline(list, resource, view, 0,
                clear_aspects, clear_value, rect_count, rects);
    }

    VK_CALL(vkCmdEndRenderPass(list->vk_command_buffer));
}

static void d3d12_command_list_clear_attachment_deferred(struct d3d12_command_list *list, unsigned int attachment_idx,
        VkImageAspectFlags clear_aspects, const VkClearValue *clear_value)
{
    struct vkd3d_clear_state *clear_state = &list->clear_state;
    struct vkd3d_clear_attachment *attachment = &clear_state->attachments[attachment_idx];

    /* If necessary, combine with previous clear so that e.g. a
     * depth-only clear does not override a stencil-only clear. */
    clear_state->attachment_mask |= 1u << attachment_idx;
    attachment->aspect_mask |= clear_aspects;

    if (clear_aspects & VK_IMAGE_ASPECT_COLOR_BIT)
        attachment->value.color = clear_value->color;

    if (clear_aspects & VK_IMAGE_ASPECT_DEPTH_BIT)
        attachment->value.depthStencil.depth = clear_value->depthStencil.depth;

    if (clear_aspects & VK_IMAGE_ASPECT_STENCIL_BIT)
        attachment->value.depthStencil.stencil = clear_value->depthStencil.stencil;
}

static bool d3d12_command_list_has_render_pass_rtv_clear(struct d3d12_command_list *list, unsigned int attachment_idx)
{
    const struct d3d12_graphics_pipeline_state *graphics;

    if (!d3d12_pipeline_state_is_graphics(list->state))
        return false;

    graphics = &list->state->u.graphics;

    return attachment_idx < graphics->rt_count &&
            !(graphics->null_attachment_mask & (1 << attachment_idx)) &&
            (list->clear_state.attachment_mask & (1 << attachment_idx));
}

static bool d3d12_command_list_has_depth_stencil_view(struct d3d12_command_list *list);

static bool d3d12_command_list_has_render_pass_dsv_clear(struct d3d12_command_list *list)
{
    VkImageAspectFlags clear_aspects, write_aspects;

    if (!d3d12_pipeline_state_is_graphics(list->state))
        return false;

    if (!d3d12_command_list_has_depth_stencil_view(list))
        return false;

    if (!(list->clear_state.attachment_mask & (1 << D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT)))
        return false;

    /* If any of the aspects to clear are read-only in the render
     * pass, we have to perform the DSV clear in a separate pass. */
    write_aspects = vk_writable_aspects_from_image_layout(list->dsv_layout);
    clear_aspects = list->clear_state.attachments[D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT].aspect_mask;
    return (write_aspects & clear_aspects) == clear_aspects;
}

static void d3d12_command_list_emit_deferred_clear(struct d3d12_command_list *list, unsigned int attachment_idx)
{
    struct vkd3d_clear_attachment *clear_attachment = &list->clear_state.attachments[attachment_idx];
    struct d3d12_resource *resource;
    struct vkd3d_view *view;

    if (attachment_idx < D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT)
    {
        resource = list->rtvs[attachment_idx].resource;
        view = list->rtvs[attachment_idx].view;
    }
    else
    {
        resource = list->dsv.resource;
        view = list->dsv.view;
    }

    if (list->current_render_pass)
    {
        d3d12_command_list_clear_attachment_inline(list, resource,
                view, attachment_idx, clear_attachment->aspect_mask,
                &clear_attachment->value, 0, NULL);
    }
    else
    {
        d3d12_command_list_clear_attachment_pass(list, resource, view,
                clear_attachment->aspect_mask, &clear_attachment->value,
                0, NULL);
    }

    clear_attachment->aspect_mask = 0;
}

static void d3d12_command_list_emit_render_pass_clears(struct d3d12_command_list *list, bool invert_mask)
{
    struct vkd3d_clear_state *clear_state = &list->clear_state;
    uint64_t attachment_mask = 0;
    unsigned int i;

    if (!clear_state->attachment_mask)
        return;

    for (i = 0; i < D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT; i++)
    {
        if (d3d12_command_list_has_render_pass_rtv_clear(list, i))
            attachment_mask |= 1 << i;
    }

    if (d3d12_command_list_has_render_pass_dsv_clear(list))
        attachment_mask |= 1 << D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT;

    if (invert_mask)
        attachment_mask = (~attachment_mask) & clear_state->attachment_mask;

    clear_state->attachment_mask &= ~attachment_mask;

    while (attachment_mask)
    {
        unsigned int attachment_idx = vkd3d_bitmask_iter64(&attachment_mask);
        d3d12_command_list_emit_deferred_clear(list, attachment_idx);
    }
}

static void d3d12_command_list_flush_deferred_clears(struct d3d12_command_list *list)
{
    struct vkd3d_clear_state *clear_state = &list->clear_state;

    while (clear_state->attachment_mask)
    {
        unsigned int attachment_idx = vkd3d_bitmask_iter64(&clear_state->attachment_mask);
        d3d12_command_list_emit_deferred_clear(list, attachment_idx);
    }
}

enum vkd3d_render_pass_transition_mode
{
    VKD3D_RENDER_PASS_TRANSITION_MODE_BEGIN,
    VKD3D_RENDER_PASS_TRANSITION_MODE_END,
};

static VkPipelineStageFlags vk_render_pass_barrier_from_view(const struct vkd3d_view *view, const struct d3d12_resource *resource,
        enum vkd3d_render_pass_transition_mode mode, VkImageLayout layout, bool clear, VkImageMemoryBarrier *vk_barrier)
{
    VkPipelineStageFlags stages;
    VkAccessFlags access;

    if (!layout)
        layout = view->info.texture.vk_layout;

    if (view->format->vk_aspect_mask & VK_IMAGE_ASPECT_COLOR_BIT)
    {
        stages = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        access = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;
    }
    else
    {
        stages = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        access = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
    }

    vk_barrier->sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    vk_barrier->pNext = NULL;

    if (mode == VKD3D_RENDER_PASS_TRANSITION_MODE_BEGIN)
    {
        vk_barrier->srcAccessMask = 0;
        vk_barrier->dstAccessMask = access;
        vk_barrier->oldLayout = resource->common_layout;
        vk_barrier->newLayout = layout;

        /* Ignore 3D images as re-initializing those may cause us to
         * discard the entire image, not just the layers to clear. */
        if (clear && resource->desc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE3D)
            vk_barrier->oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    }
    else /* if (mode == VKD3D_RENDER_PASS_TRANSITION_MODE_END) */
    {
        vk_barrier->srcAccessMask = access;
        vk_barrier->dstAccessMask = 0;
        vk_barrier->oldLayout = layout;
        vk_barrier->newLayout = resource->common_layout;
    }

    vk_barrier->srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    vk_barrier->dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    vk_barrier->image = resource->u.vk_image;
    vk_barrier->subresourceRange = vk_subresource_range_from_view(view);
    return stages;
}

static void d3d12_command_list_emit_render_pass_transition(struct d3d12_command_list *list,
        enum vkd3d_render_pass_transition_mode mode)
{
    const struct vkd3d_vk_device_procs *vk_procs = &list->device->vk_procs;
    VkImageMemoryBarrier vk_image_barriers[D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT + 1];
    VkPipelineStageFlags stage_mask = 0;
    struct d3d12_dsv_desc *dsv;
    bool do_clear = false;
    uint32_t i, j;

    for (i = 0, j = 0; i < D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT; i++)
    {
        struct d3d12_rtv_desc *rtv = &list->rtvs[i];

        if (!rtv->view)
            continue;

        if (mode == VKD3D_RENDER_PASS_TRANSITION_MODE_BEGIN)
            do_clear = d3d12_command_list_has_render_pass_rtv_clear(list, i);

        stage_mask |= vk_render_pass_barrier_from_view(rtv->view, rtv->resource,
                mode, VK_IMAGE_LAYOUT_UNDEFINED, do_clear, &vk_image_barriers[j++]);
    }

    dsv = &list->dsv;

    if (dsv->view && list->dsv_layout)
    {
        if (mode == VKD3D_RENDER_PASS_TRANSITION_MODE_BEGIN)
            do_clear = d3d12_command_list_has_render_pass_dsv_clear(list);

        stage_mask |= vk_render_pass_barrier_from_view(dsv->view, dsv->resource,
                mode, list->dsv_layout, do_clear, &vk_image_barriers[j++]);
    }

    if (!j)
        return;

    VK_CALL(vkCmdPipelineBarrier(list->vk_command_buffer,
        stage_mask, stage_mask, 0, 0, NULL, 0, NULL,
        j, vk_image_barriers));
}

static void d3d12_command_list_end_current_render_pass(struct d3d12_command_list *list, bool suspend)
{
    const struct vkd3d_vk_device_procs *vk_procs = &list->device->vk_procs;

    if (list->xfb_enabled)
    {
        VK_CALL(vkCmdEndTransformFeedbackEXT(list->vk_command_buffer, 0, ARRAY_SIZE(list->so_counter_buffers),
                list->so_counter_buffers, list->so_counter_buffer_offsets));
    }

    if (list->current_render_pass)
        VK_CALL(vkCmdEndRenderPass(list->vk_command_buffer));

    /* Don't emit barriers for temporary suspendion of the render pass */
    if (!suspend && (list->current_render_pass || list->render_pass_suspended))
        d3d12_command_list_emit_render_pass_transition(list, VKD3D_RENDER_PASS_TRANSITION_MODE_END);

    /* Emit pending deferred clears. This can happen if
     * no draw got executed after the clear operation. */
    d3d12_command_list_flush_deferred_clears(list);

    list->render_pass_suspended = suspend && list->current_render_pass;
    list->current_render_pass = VK_NULL_HANDLE;

    if (list->xfb_enabled)
    {
        VkMemoryBarrier vk_barrier;

        /* We need a barrier between pause and resume. */
        vk_barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        vk_barrier.pNext = NULL;
        vk_barrier.srcAccessMask = VK_ACCESS_TRANSFORM_FEEDBACK_COUNTER_WRITE_BIT_EXT;
        vk_barrier.dstAccessMask = VK_ACCESS_TRANSFORM_FEEDBACK_COUNTER_READ_BIT_EXT;
        VK_CALL(vkCmdPipelineBarrier(list->vk_command_buffer,
                VK_PIPELINE_STAGE_TRANSFORM_FEEDBACK_BIT_EXT, VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT, 0,
                1, &vk_barrier, 0, NULL, 0, NULL));

        list->xfb_enabled = false;
    }
}

static void d3d12_command_list_invalidate_current_render_pass(struct d3d12_command_list *list)
{
    d3d12_command_list_end_current_render_pass(list, false);
}

static void d3d12_command_list_invalidate_root_parameters(struct d3d12_command_list *list,
        VkPipelineBindPoint bind_point, bool invalidate_descriptor_heaps)
{
    struct vkd3d_pipeline_bindings *bindings = &list->pipeline_bindings[bind_point];

    if (!bindings->root_signature)
        return;

    /* Previously dirty states may no longer be dirty
     * if the new root signature does not use them */
    bindings->dirty_flags = 0;

    if (bindings->static_sampler_set)
        bindings->dirty_flags |= VKD3D_PIPELINE_DIRTY_STATIC_SAMPLER_SET;

    if (bindings->root_signature->vk_packed_descriptor_layout)
        bindings->dirty_flags |= VKD3D_PIPELINE_DIRTY_PACKED_DESCRIPTOR_SET;

    if (bindings->root_signature->descriptor_table_count)
        bindings->dirty_flags |= VKD3D_PIPELINE_DIRTY_DESCRIPTOR_TABLE_OFFSETS;

    if (bindings->root_signature->flags & VKD3D_ROOT_SIGNATURE_USE_BINDLESS_UAV_COUNTERS)
        bindings->dirty_flags |= VKD3D_PIPELINE_DIRTY_UAV_COUNTER_BINDING;

    bindings->root_descriptor_dirty_mask = bindings->root_signature->root_descriptor_mask;
    bindings->root_constant_dirty_mask = bindings->root_signature->root_constant_mask;

    if (invalidate_descriptor_heaps)
    {
        struct d3d12_device *device = bindings->root_signature->device;
        bindings->descriptor_heap_dirty_mask = (1ull << device->bindless_state.set_count) - 1;
    }
}

static void vk_access_and_stage_flags_from_d3d12_resource_state(const struct d3d12_device *device,
        const struct d3d12_resource *resource, uint32_t state_mask, VkQueueFlags vk_queue_flags,
        VkPipelineStageFlags *stages, VkAccessFlags *access)
{
    VkPipelineStageFlags queue_shader_stages = 0;
    uint32_t unhandled_state = 0;

    if (vk_queue_flags & VK_QUEUE_GRAPHICS_BIT)
    {
        queue_shader_stages |= VK_PIPELINE_STAGE_VERTEX_SHADER_BIT |
                VK_PIPELINE_STAGE_TESSELLATION_CONTROL_SHADER_BIT |
                VK_PIPELINE_STAGE_TESSELLATION_EVALUATION_SHADER_BIT |
                VK_PIPELINE_STAGE_GEOMETRY_SHADER_BIT |
                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    }

    if (vk_queue_flags & VK_QUEUE_COMPUTE_BIT)
        queue_shader_stages |= VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;

    if (state_mask == D3D12_RESOURCE_STATE_COMMON)
    {
        *stages |= VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        *access |= VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
        return;
    }

    while (state_mask)
    {
        uint32_t state = state_mask & -state_mask;

        switch (state)
        {
            case D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER:
                *stages |= queue_shader_stages;
                *access |= VK_ACCESS_UNIFORM_READ_BIT;

                if (device->bindless_state.flags & VKD3D_BINDLESS_CBV_AS_SSBO)
                    *access |= VK_ACCESS_SHADER_READ_BIT;

                if (vk_queue_flags & VK_QUEUE_GRAPHICS_BIT)
                {
                    *stages |= VK_PIPELINE_STAGE_VERTEX_INPUT_BIT;
                    *access |= VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
                }
                break;

            case D3D12_RESOURCE_STATE_INDEX_BUFFER:
                *stages |= VK_PIPELINE_STAGE_VERTEX_INPUT_BIT;
                *access |= VK_ACCESS_INDEX_READ_BIT;
                break;

            case D3D12_RESOURCE_STATE_RENDER_TARGET:
                *stages |= VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
                break;

            case D3D12_RESOURCE_STATE_UNORDERED_ACCESS:
                *stages |= queue_shader_stages;
                *access |= VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
                break;

            case D3D12_RESOURCE_STATE_DEPTH_WRITE:
            case D3D12_RESOURCE_STATE_DEPTH_READ:
                *stages |= VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
                break;

            case D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE:
                *stages |= queue_shader_stages & ~VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
                *access |= VK_ACCESS_SHADER_READ_BIT;
                break;

            case D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE:
                *stages |= VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
                *access |= VK_ACCESS_SHADER_READ_BIT;
                break;

            case D3D12_RESOURCE_STATE_STREAM_OUT:
                *stages |= VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_TRANSFORM_FEEDBACK_BIT_EXT;
                *access |= VK_ACCESS_TRANSFORM_FEEDBACK_COUNTER_READ_BIT_EXT | VK_ACCESS_TRANSFORM_FEEDBACK_COUNTER_WRITE_BIT_EXT |
                        VK_ACCESS_TRANSFORM_FEEDBACK_WRITE_BIT_EXT;
                break;

            case D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT:
                *stages |= VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT;
                *access |= VK_ACCESS_INDIRECT_COMMAND_READ_BIT;

                if (device->vk_info.EXT_conditional_rendering)
                {
                    *stages |= VK_PIPELINE_STAGE_CONDITIONAL_RENDERING_BIT_EXT;
                    *access |= VK_ACCESS_CONDITIONAL_RENDERING_READ_BIT_EXT;
                }
                break;

            case D3D12_RESOURCE_STATE_COPY_DEST:
                *stages |= VK_PIPELINE_STAGE_TRANSFER_BIT;
                if (d3d12_resource_is_buffer(resource))
                    *access |= VK_ACCESS_TRANSFER_WRITE_BIT;
                break;

            case D3D12_RESOURCE_STATE_COPY_SOURCE:
                *stages |= VK_PIPELINE_STAGE_TRANSFER_BIT;
                if (d3d12_resource_is_buffer(resource))
                    *access |= VK_ACCESS_TRANSFER_READ_BIT;
                break;

            case D3D12_RESOURCE_STATE_RESOLVE_DEST:
            case D3D12_RESOURCE_STATE_RESOLVE_SOURCE:
                *stages |= VK_PIPELINE_STAGE_TRANSFER_BIT;
                break;

            default:
                unhandled_state |= state;
        }

        state_mask &= ~state;
    }

    if (unhandled_state)
        FIXME("Unhandled resource state %#x.\n", unhandled_state);
}

static void d3d12_command_list_transition_resource_to_initial_state(struct d3d12_command_list *list,
        struct d3d12_resource *resource)
{
    const struct vkd3d_vk_device_procs *vk_procs = &list->device->vk_procs;
    VkPipelineStageFlags dst_stage_mask = 0;
    const struct vkd3d_format *format;
    VkImageMemoryBarrier barrier;

    assert(d3d12_resource_is_texture(resource));

    if (!(format = vkd3d_format_from_d3d12_resource_desc(list->device, &resource->desc, 0)))
    {
        ERR("Resource %p has invalid format %#x.\n", resource, resource->desc.Format);
        return;
    }

    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.pNext = NULL;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = 0;
    barrier.oldLayout = d3d12_resource_is_cpu_accessible(resource)
            ? VK_IMAGE_LAYOUT_PREINITIALIZED : VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = resource->common_layout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = resource->u.vk_image;
    barrier.subresourceRange.aspectMask = format->vk_aspect_mask;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = VK_REMAINING_MIP_LEVELS;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = VK_REMAINING_ARRAY_LAYERS;

    vk_access_and_stage_flags_from_d3d12_resource_state(list->device, resource,
            resource->initial_state, list->vk_queue_flags, &dst_stage_mask, &barrier.dstAccessMask);

    TRACE("Initial state %#x transition for resource %p (old layout %#x, new layout %#x).\n",
            resource->initial_state, resource, barrier.oldLayout, barrier.newLayout);

    VK_CALL(vkCmdPipelineBarrier(list->vk_command_buffer,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, dst_stage_mask,
            0, 0, NULL, 0, NULL, 1, &barrier));
}

static void d3d12_command_list_track_resource_usage(struct d3d12_command_list *list,
        struct d3d12_resource *resource)
{
    if (resource->flags & VKD3D_RESOURCE_INITIAL_STATE_TRANSITION)
    {
        d3d12_command_list_end_current_render_pass(list, true);

        d3d12_command_list_transition_resource_to_initial_state(list, resource);
        resource->flags &= ~VKD3D_RESOURCE_INITIAL_STATE_TRANSITION;
    }
}

static HRESULT STDMETHODCALLTYPE d3d12_command_list_QueryInterface(d3d12_command_list_iface *iface,
        REFIID iid, void **object)
{
    TRACE("iface %p, iid %s, object %p.\n", iface, debugstr_guid(iid), object);

    if (IsEqualGUID(iid, &IID_ID3D12GraphicsCommandList)
            || IsEqualGUID(iid, &IID_ID3D12GraphicsCommandList1)
            || IsEqualGUID(iid, &IID_ID3D12GraphicsCommandList2)
            || IsEqualGUID(iid, &IID_ID3D12GraphicsCommandList3)
            || IsEqualGUID(iid, &IID_ID3D12GraphicsCommandList4)
            || IsEqualGUID(iid, &IID_ID3D12GraphicsCommandList5)
            || IsEqualGUID(iid, &IID_ID3D12CommandList)
            || IsEqualGUID(iid, &IID_ID3D12DeviceChild)
            || IsEqualGUID(iid, &IID_ID3D12Object)
            || IsEqualGUID(iid, &IID_IUnknown))
    {
        ID3D12GraphicsCommandList_AddRef(iface);
        *object = iface;
        return S_OK;
    }

    WARN("%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid(iid));

    *object = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE d3d12_command_list_AddRef(d3d12_command_list_iface *iface)
{
    struct d3d12_command_list *list = impl_from_ID3D12GraphicsCommandList(iface);
    ULONG refcount = InterlockedIncrement(&list->refcount);

    TRACE("%p increasing refcount to %u.\n", list, refcount);

    return refcount;
}

static ULONG STDMETHODCALLTYPE d3d12_command_list_Release(d3d12_command_list_iface *iface)
{
    struct d3d12_command_list *list = impl_from_ID3D12GraphicsCommandList(iface);
    ULONG refcount = InterlockedDecrement(&list->refcount);
    unsigned int i;

    TRACE("%p decreasing refcount to %u.\n", list, refcount);

    if (!refcount)
    {
        struct d3d12_device *device = list->device;

        vkd3d_private_store_destroy(&list->private_store);

        /* When command pool is destroyed, all command buffers are implicitly freed. */
        if (list->allocator)
            d3d12_command_allocator_free_command_buffer(list->allocator, list);

        for (i = 0; i < ARRAY_SIZE(list->packed_descriptors); i++)
        {
            struct vkd3d_descriptor_updates *updates = &list->packed_descriptors[i];
            vkd3d_free(updates->descriptors);
            vkd3d_free(updates->descriptor_writes);
        }
        vkd3d_free(list);

        d3d12_device_release(device);
    }

    return refcount;
}

static HRESULT STDMETHODCALLTYPE d3d12_command_list_GetPrivateData(d3d12_command_list_iface *iface,
        REFGUID guid, UINT *data_size, void *data)
{
    struct d3d12_command_list *list = impl_from_ID3D12GraphicsCommandList(iface);

    TRACE("iface %p, guid %s, data_size %p, data %p.\n", iface, debugstr_guid(guid), data_size, data);

    return vkd3d_get_private_data(&list->private_store, guid, data_size, data);
}

static HRESULT STDMETHODCALLTYPE d3d12_command_list_SetPrivateData(d3d12_command_list_iface *iface,
        REFGUID guid, UINT data_size, const void *data)
{
    struct d3d12_command_list *list = impl_from_ID3D12GraphicsCommandList(iface);

    TRACE("iface %p, guid %s, data_size %u, data %p.\n", iface, debugstr_guid(guid), data_size, data);

    return vkd3d_set_private_data(&list->private_store, guid, data_size, data);
}

static HRESULT STDMETHODCALLTYPE d3d12_command_list_SetPrivateDataInterface(d3d12_command_list_iface *iface,
        REFGUID guid, const IUnknown *data)
{
    struct d3d12_command_list *list = impl_from_ID3D12GraphicsCommandList(iface);

    TRACE("iface %p, guid %s, data %p.\n", iface, debugstr_guid(guid), data);

    return vkd3d_set_private_data_interface(&list->private_store, guid, data);
}

static HRESULT STDMETHODCALLTYPE d3d12_command_list_SetName(d3d12_command_list_iface *iface, const WCHAR *name)
{
    struct d3d12_command_list *list = impl_from_ID3D12GraphicsCommandList(iface);

    TRACE("iface %p, name %s.\n", iface, debugstr_w(name, list->device->wchar_size));

    return name ? S_OK : E_INVALIDARG;
}

static HRESULT STDMETHODCALLTYPE d3d12_command_list_GetDevice(d3d12_command_list_iface *iface, REFIID iid, void **device)
{
    struct d3d12_command_list *list = impl_from_ID3D12GraphicsCommandList(iface);

    TRACE("iface %p, iid %s, device %p.\n", iface, debugstr_guid(iid), device);

    return d3d12_device_query_interface(list->device, iid, device);
}

static D3D12_COMMAND_LIST_TYPE STDMETHODCALLTYPE d3d12_command_list_GetType(d3d12_command_list_iface *iface)
{
    struct d3d12_command_list *list = impl_from_ID3D12GraphicsCommandList(iface);

    TRACE("iface %p.\n", iface);

    return list->type;
}

static HRESULT STDMETHODCALLTYPE d3d12_command_list_Close(d3d12_command_list_iface *iface)
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

    d3d12_command_list_end_current_render_pass(list, false);
    if (list->is_predicated)
        VK_CALL(vkCmdEndConditionalRenderingEXT(list->vk_command_buffer));

    if ((vr = VK_CALL(vkEndCommandBuffer(list->vk_command_buffer))) < 0)
    {
        WARN("Failed to end command buffer, vr %d.\n", vr);
        return hresult_from_vk_result(vr);
    }

    if (list->allocator)
    {
        d3d12_command_allocator_free_command_buffer(list->allocator, list);
        list->allocator = NULL;
    }

    list->is_recording = false;

    if (!list->is_valid)
    {
        WARN("Error occurred during command list recording.\n");
        return E_INVALIDARG;
    }

    return S_OK;
}

static void d3d12_command_list_reset_state(struct d3d12_command_list *list,
        ID3D12PipelineState *initial_pipeline_state)
{
    d3d12_command_list_iface *iface = &list->ID3D12GraphicsCommandList_iface;

    list->index_buffer_format = DXGI_FORMAT_UNKNOWN;

    memset(list->rtvs, 0, sizeof(list->rtvs));
    memset(&list->dsv, 0, sizeof(list->dsv));
    memset(&list->clear_state, 0, sizeof(list->clear_state));
    list->dsv_layout = VK_IMAGE_LAYOUT_UNDEFINED;
    list->fb_width = 0;
    list->fb_height = 0;
    list->fb_layer_count = 0;

    list->xfb_enabled = false;

    list->is_predicated = false;
    list->render_pass_suspended = false;

    list->current_framebuffer = VK_NULL_HANDLE;
    list->current_pipeline = VK_NULL_HANDLE;
    list->pso_render_pass = VK_NULL_HANDLE;
    list->current_render_pass = VK_NULL_HANDLE;
    list->uav_counter_address_buffer = VK_NULL_HANDLE;

    memset(&list->dynamic_state, 0, sizeof(list->dynamic_state));
    list->dynamic_state.blend_constants[0] = D3D12_DEFAULT_BLEND_FACTOR_RED;
    list->dynamic_state.blend_constants[1] = D3D12_DEFAULT_BLEND_FACTOR_GREEN;
    list->dynamic_state.blend_constants[2] = D3D12_DEFAULT_BLEND_FACTOR_BLUE;
    list->dynamic_state.blend_constants[3] = D3D12_DEFAULT_BLEND_FACTOR_ALPHA;

    list->dynamic_state.min_depth_bounds = 0.0f;
    list->dynamic_state.max_depth_bounds = 1.0f;

    list->dynamic_state.primitive_topology = D3D_PRIMITIVE_TOPOLOGY_POINTLIST;

    memset(list->pipeline_bindings, 0, sizeof(list->pipeline_bindings));
    memset(list->descriptor_heaps, 0, sizeof(list->descriptor_heaps));

    list->state = NULL;

    list->descriptor_updates_count = 0;

    memset(list->so_counter_buffers, 0, sizeof(list->so_counter_buffers));
    memset(list->so_counter_buffer_offsets, 0, sizeof(list->so_counter_buffer_offsets));

    ID3D12GraphicsCommandList_SetPipelineState(iface, initial_pipeline_state);
}

static HRESULT STDMETHODCALLTYPE d3d12_command_list_Reset(d3d12_command_list_iface *iface,
        ID3D12CommandAllocator *allocator, ID3D12PipelineState *initial_pipeline_state)
{
    struct d3d12_command_allocator *allocator_impl = unsafe_impl_from_ID3D12CommandAllocator(allocator);
    struct d3d12_command_list *list = impl_from_ID3D12GraphicsCommandList(iface);
    HRESULT hr;

    TRACE("iface %p, allocator %p, initial_pipeline_state %p.\n",
            iface, allocator, initial_pipeline_state);

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

    if (SUCCEEDED(hr = d3d12_command_allocator_allocate_command_buffer(allocator_impl, list)))
    {
        list->allocator = allocator_impl;
        d3d12_command_list_reset_state(list, initial_pipeline_state);
    }

    return hr;
}

static HRESULT STDMETHODCALLTYPE d3d12_command_list_ClearState(d3d12_command_list_iface *iface,
        ID3D12PipelineState *pipeline_state)
{
    FIXME("iface %p, pipline_state %p stub!\n", iface, pipeline_state);

    return E_NOTIMPL;
}

static bool d3d12_command_list_has_depth_stencil_view(struct d3d12_command_list *list)
{
    struct d3d12_graphics_pipeline_state *graphics;

    assert(d3d12_pipeline_state_is_graphics(list->state));
    graphics = &list->state->u.graphics;

    return graphics->dsv_format || (d3d12_pipeline_state_has_unknown_dsv_format(list->state) && list->dsv.format);
}

static void d3d12_command_list_get_fb_extent(struct d3d12_command_list *list,
        uint32_t *width, uint32_t *height, uint32_t *layer_count)
{
    struct d3d12_graphics_pipeline_state *graphics = &list->state->u.graphics;
    struct d3d12_device *device = list->device;

    if (graphics->rt_count || d3d12_command_list_has_depth_stencil_view(list))
    {
        *width = list->fb_width;
        *height = list->fb_height;
        if (layer_count)
            *layer_count = list->fb_layer_count;
    }
    else
    {
        *width = device->vk_info.device_limits.maxFramebufferWidth;
        *height = device->vk_info.device_limits.maxFramebufferHeight;
        if (layer_count)
            *layer_count = 1;
    }
}

static bool d3d12_command_list_create_framebuffer(struct d3d12_command_list *list, VkRenderPass render_pass,
        uint32_t view_count, const VkImageView *views, VkExtent3D extent, VkFramebuffer *vk_framebuffer)
{
    struct d3d12_device *device = list->device;
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    struct VkFramebufferCreateInfo fb_desc;
    VkResult vr;

    fb_desc.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fb_desc.pNext = NULL;
    fb_desc.flags = 0;
    fb_desc.renderPass = render_pass;
    fb_desc.attachmentCount = view_count;
    fb_desc.pAttachments = views;
    fb_desc.width = extent.width;
    fb_desc.height = extent.height;
    fb_desc.layers = extent.depth;

    if ((vr = VK_CALL(vkCreateFramebuffer(device->vk_device, &fb_desc, NULL, vk_framebuffer))) < 0)
    {
        ERR("Failed to create Vulkan framebuffer, vr %d.\n", vr);
        return false;
    }

    if (!d3d12_command_allocator_add_framebuffer(list->allocator, *vk_framebuffer))
    {
        WARN("Failed to add framebuffer.\n");
        VK_CALL(vkDestroyFramebuffer(device->vk_device, *vk_framebuffer, NULL));
        return false;
    }

    return true;
}

static bool d3d12_command_list_update_current_framebuffer(struct d3d12_command_list *list)
{
    VkImageView views[D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT + 1];
    struct d3d12_graphics_pipeline_state *graphics;
    VkFramebuffer vk_framebuffer;
    unsigned int view_count;
    VkExtent3D extent;
    unsigned int i;

    if (list->current_framebuffer != VK_NULL_HANDLE)
        return true;

    graphics = &list->state->u.graphics;

    for (i = 0, view_count = 0; i < graphics->rt_count; ++i)
    {
        if (graphics->null_attachment_mask & (1u << i))
        {
            if (list->rtvs[i].view)
                WARN("Expected NULL RTV for attachment %u.\n", i);
            continue;
        }

        if (!list->rtvs[i].view)
        {
            FIXME("Invalid RTV for attachment %u.\n", i);
            return false;
        }

        views[view_count++] = list->rtvs[i].view->u.vk_image_view;
    }

    if (d3d12_command_list_has_depth_stencil_view(list))
    {
        if (!list->dsv.view)
        {
            FIXME("Invalid DSV.\n");
            return false;
        }

        views[view_count++] = list->dsv.view->u.vk_image_view;
    }

    d3d12_command_list_get_fb_extent(list, &extent.width, &extent.height, &extent.depth);

    if (!d3d12_command_list_create_framebuffer(list, list->pso_render_pass, view_count, views, extent, &vk_framebuffer))
    {
        ERR("Failed to create framebuffer.\n");
        return false;
    }

    list->current_framebuffer = vk_framebuffer;

    return true;
}

static bool d3d12_command_list_update_compute_pipeline(struct d3d12_command_list *list)
{
    const struct vkd3d_vk_device_procs *vk_procs = &list->device->vk_procs;

    if (list->current_pipeline != VK_NULL_HANDLE)
        return true;

    if (!d3d12_pipeline_state_is_compute(list->state))
    {
        WARN("Pipeline state %p is not a compute pipeline.\n", list->state);
        return false;
    }

    VK_CALL(vkCmdBindPipeline(list->vk_command_buffer, list->state->vk_bind_point, list->state->u.compute.vk_pipeline));
    list->current_pipeline = list->state->u.compute.vk_pipeline;

    return true;
}

static bool d3d12_command_list_update_graphics_pipeline(struct d3d12_command_list *list)
{
    const struct vkd3d_vk_device_procs *vk_procs = &list->device->vk_procs;
    VkRenderPass vk_render_pass;
    VkPipeline vk_pipeline;
    VkFormat dsv_format;

    if (list->current_pipeline != VK_NULL_HANDLE)
        return true;

    if (!d3d12_pipeline_state_is_graphics(list->state))
    {
        WARN("Pipeline state %p is not a graphics pipeline.\n", list->state);
        return false;
    }

    dsv_format = list->dsv.format ? list->dsv.format->vk_format : VK_FORMAT_UNDEFINED;

    if (!(vk_pipeline = d3d12_pipeline_state_get_or_create_pipeline(list->state,
            &list->dynamic_state, dsv_format, &vk_render_pass)))
        return false;

    /* The render pass cache ensures that we use the same Vulkan render pass
     * object for compatible render passes. */
    if (list->pso_render_pass != vk_render_pass)
    {
        list->pso_render_pass = vk_render_pass;
        d3d12_command_list_invalidate_current_framebuffer(list);
        /* Don't end render pass if none is active, or otherwise
         * deferred clears are not going to work as intended. */
        if (list->current_render_pass || list->render_pass_suspended)
            d3d12_command_list_invalidate_current_render_pass(list);
        /* Only override this after ending the render pass. */
        list->dsv_layout = list->state->u.graphics.dsv_layout;
    }

    VK_CALL(vkCmdBindPipeline(list->vk_command_buffer, list->state->vk_bind_point, vk_pipeline));
    list->current_pipeline = vk_pipeline;

    return true;
}

static bool vkd3d_descriptor_info_from_d3d12_desc(struct d3d12_device *device,
        const struct d3d12_desc *desc, const struct vkd3d_shader_resource_binding *binding,
        union vkd3d_descriptor_info *vk_descriptor)
{
    switch (binding->type)
    {
        case VKD3D_SHADER_DESCRIPTOR_TYPE_CBV:
            if (desc->magic != VKD3D_DESCRIPTOR_MAGIC_CBV)
                return false;

            vk_descriptor->buffer = desc->u.vk_cbv_info;
            return true;

        case VKD3D_SHADER_DESCRIPTOR_TYPE_SRV:
            if (desc->magic != VKD3D_DESCRIPTOR_MAGIC_SRV)
                return false;

            if ((binding->flags & VKD3D_SHADER_BINDING_FLAG_IMAGE)
                    && (desc->u.view->type == VKD3D_VIEW_TYPE_IMAGE))
            {
                vk_descriptor->image.imageView = desc->u.view->u.vk_image_view;
                vk_descriptor->image.sampler = VK_NULL_HANDLE;
                vk_descriptor->image.imageLayout = desc->u.view->info.texture.vk_layout;
                return true;
            }
            else if ((binding->flags & VKD3D_SHADER_BINDING_FLAG_BUFFER)
                    && (desc->u.view->type == VKD3D_VIEW_TYPE_BUFFER))
            {
                vk_descriptor->buffer_view = desc->u.view->u.vk_buffer_view;
                return true;
            }
            break;

        case VKD3D_SHADER_DESCRIPTOR_TYPE_UAV:
            if (desc->magic != VKD3D_DESCRIPTOR_MAGIC_UAV)
                return false;

            if ((binding->flags & VKD3D_SHADER_BINDING_FLAG_IMAGE)
                    && (desc->u.view->type == VKD3D_VIEW_TYPE_IMAGE))
            {
                vk_descriptor->image.imageView = desc->u.view->u.vk_image_view;
                vk_descriptor->image.sampler = VK_NULL_HANDLE;
                vk_descriptor->image.imageLayout = desc->u.view->info.texture.vk_layout;
                return true;
            }
            else if ((binding->flags & VKD3D_SHADER_BINDING_FLAG_BUFFER)
                    && (desc->u.view->type == VKD3D_VIEW_TYPE_BUFFER))
            {
                vk_descriptor->buffer_view = desc->u.view->u.vk_buffer_view;
                return true;
            }
            else if ((binding->flags & VKD3D_SHADER_BINDING_FLAG_COUNTER)
                    && desc->u.view->vk_counter_view)
            {
                vk_descriptor->buffer_view = desc->u.view->vk_counter_view;
                return true;
            }
            break;

        case VKD3D_SHADER_DESCRIPTOR_TYPE_SAMPLER:
            if (desc->magic != VKD3D_DESCRIPTOR_MAGIC_SAMPLER)
                return false;

            vk_descriptor->image.sampler = desc->u.view->u.vk_sampler;
            vk_descriptor->image.imageView = VK_NULL_HANDLE;
            vk_descriptor->image.imageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            return true;

        default:
            ERR("Unhandled descriptor type %d.\n", binding->type);
    }

    return false;
}

static bool vkd3d_descriptor_updates_reserve_arrays(struct vkd3d_descriptor_updates *updates,
        unsigned int descriptor_count)
{
    /* This should grow over time to the point where no further allocations are necessary */
    if (!vkd3d_array_reserve((void **)&updates->descriptors, &updates->descriptors_size,
            descriptor_count, sizeof(*updates->descriptors)))
        return false;

    if (!vkd3d_array_reserve((void **)&updates->descriptor_writes, &updates->descriptor_writes_size,
            descriptor_count, sizeof(*updates->descriptor_writes)))
        return false;

    return true;
}

static void vk_write_descriptor_set_for_descriptor_info(VkDescriptorSet vk_descriptor_set, uint32_t vk_binding,
        VkDescriptorType vk_descriptor_type, union vkd3d_descriptor_info *vk_descriptor, VkWriteDescriptorSet *vk_write)
{
    vk_write->sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    vk_write->pNext = NULL;
    vk_write->dstSet = vk_descriptor_set;
    vk_write->dstBinding = vk_binding;
    vk_write->dstArrayElement = 0;
    vk_write->descriptorCount = 1;
    vk_write->descriptorType = vk_descriptor_type;
    vk_write->pImageInfo = &vk_descriptor->image;
    vk_write->pBufferInfo = &vk_descriptor->buffer;
    vk_write->pTexelBufferView = &vk_descriptor->buffer_view;
}

static void d3d12_command_list_update_descriptor_table(struct d3d12_command_list *list,
        VkDescriptorSet descriptor_set,
        struct vkd3d_descriptor_updates *updates,
        const struct d3d12_root_signature *root_signature,
        const struct d3d12_desc *base_descriptor,
        unsigned int root_parameter_index)
{
    const struct vkd3d_vk_device_procs *vk_procs = &list->device->vk_procs;
    const struct d3d12_root_descriptor_table *table;
    union vkd3d_descriptor_info *vk_descriptor;
    unsigned int write_count = 0;
    unsigned int i, j;

    table = root_signature_get_descriptor_table(root_signature, root_parameter_index);
    vk_descriptor = &updates->descriptors[table->first_packed_descriptor];

    for (i = 0; i < table->binding_count; i++)
    {
        const struct vkd3d_shader_resource_binding *binding = &table->first_binding[i];

        if (binding->flags & VKD3D_SHADER_BINDING_FLAG_BINDLESS)
            continue;

        for (j = 0; j < binding->register_count; j++)
        {
            const struct d3d12_desc *desc = &base_descriptor[binding->descriptor_offset + j];

            /* Skip invalid descriptors */
            if (vkd3d_descriptor_info_from_d3d12_desc(list->device, desc, binding, vk_descriptor))
            {
                vk_write_descriptor_set_for_descriptor_info(descriptor_set, binding->binding.binding,
                                                            desc->vk_descriptor_type, vk_descriptor,
                                                            &updates->descriptor_writes[write_count++]);
            }

            vk_descriptor++;
        }
    }

    if (write_count)
    {
        VK_CALL(vkUpdateDescriptorSets(list->device->vk_device,
                write_count, updates->descriptor_writes, 0, NULL));
    }
}

static void d3d12_deferred_descriptor_set_update_resolve(struct d3d12_command_list *list,
        const struct d3d12_deferred_descriptor_set_update *update)
{
    d3d12_command_list_update_descriptor_table(list,
            update->descriptor_set,
            update->updates,
            update->root_signature,
            update->base_descriptor,
            update->root_parameter_index);
}

static void d3d12_command_list_defer_update_descriptor_table(struct d3d12_command_list *list,
                                                             VkDescriptorSet descriptor_set,
                                                             struct vkd3d_descriptor_updates *updates,
                                                             const struct d3d12_root_signature *root_signature,
                                                             const struct d3d12_desc *base_descriptor,
                                                             unsigned int root_parameter_index)
{
    struct d3d12_deferred_descriptor_set_update *update;

    if (!vkd3d_array_reserve((void **)&list->descriptor_updates, &list->descriptor_updates_size,
                             list->descriptor_updates_count + 1, sizeof(*list->descriptor_updates)))
    {
        ERR("Failed to allocate space for deferred descriptor set update!\n");
        return;
    }

    update = &list->descriptor_updates[list->descriptor_updates_count++];
    update->descriptor_set = descriptor_set;
    update->root_signature = root_signature;
    update->root_parameter_index = root_parameter_index;
    update->base_descriptor = base_descriptor;
    update->updates = updates;
}

static void d3d12_command_list_update_packed_descriptors(struct d3d12_command_list *list,
        VkPipelineBindPoint bind_point)
{
    bool deferred_update = list->device->vk_info.supports_volatile_packed_descriptors;
    struct vkd3d_descriptor_updates *updates = &list->packed_descriptors[bind_point];
    struct vkd3d_pipeline_bindings *bindings = &list->pipeline_bindings[bind_point];
    const struct d3d12_root_signature *root_signature = bindings->root_signature;
    const struct vkd3d_vk_device_procs *vk_procs = &list->device->vk_procs;
    VkDescriptorSet descriptor_set = VK_NULL_HANDLE;
    const struct d3d12_desc *base_descriptor;
    unsigned int root_parameter_index;
    uint64_t descriptor_table_mask;

    /* Reserves the array for worst case. */
    if (!vkd3d_descriptor_updates_reserve_arrays(updates, root_signature->packed_descriptor_count))
    {
        ERR("Failed to resize descriptor update arrays.\n");
        return;
    }

    /* Update packed descriptor set for all active descriptor tables */
    assert(root_signature->vk_packed_descriptor_layout);
    descriptor_table_mask = root_signature->descriptor_table_mask & bindings->descriptor_table_active_mask;

    descriptor_set = d3d12_command_allocator_allocate_descriptor_set(
            list->allocator, root_signature->vk_packed_descriptor_layout, VKD3D_DESCRIPTOR_POOL_TYPE_VOLATILE);

    while (descriptor_table_mask)
    {
        root_parameter_index = vkd3d_bitmask_iter64(&descriptor_table_mask);
        base_descriptor = d3d12_desc_from_gpu_handle(bindings->descriptor_tables[root_parameter_index]);

        if (deferred_update)
        {
            /* If we have EXT_descriptor_indexing we implement RS 1.0 correctly by deferring the descriptor
             * set update until submit time. */
            d3d12_command_list_defer_update_descriptor_table(list,
                    descriptor_set, updates,
                    root_signature, base_descriptor,
                    root_parameter_index);
        }
        else
        {
            /* Fallback, we update the descriptor set here.
             * Will work in most cases, but it's not a correct implementation of RS 1.0.
             * TODO: Use this path if application uses RS 1.1 STATIC descriptors for all entries in a table. */
            d3d12_command_list_update_descriptor_table(list,
                    descriptor_set, updates,
                    root_signature, base_descriptor,
                    root_parameter_index);
        }
    }

    VK_CALL(vkCmdBindDescriptorSets(list->vk_command_buffer, bind_point,
            root_signature->vk_pipeline_layout,
            root_signature->packed_descriptor_set,
            1, &descriptor_set, 0, NULL));

    bindings->dirty_flags &= ~VKD3D_PIPELINE_DIRTY_PACKED_DESCRIPTOR_SET;
}

static void d3d12_command_list_update_descriptor_table_offsets(struct d3d12_command_list *list,
        VkPipelineBindPoint bind_point)
{
    struct vkd3d_pipeline_bindings *bindings = &list->pipeline_bindings[bind_point];
    const struct d3d12_root_signature *root_signature = bindings->root_signature;
    const struct vkd3d_vk_device_procs *vk_procs = &list->device->vk_procs;
    const struct d3d12_root_descriptor_table *table;
    const struct d3d12_desc *base_descriptor;
    uint32_t table_offsets[D3D12_MAX_ROOT_COST];
    unsigned int root_parameter_index;
    uint64_t descriptor_table_mask;

    assert(root_signature->descriptor_table_count);
    descriptor_table_mask = root_signature->descriptor_table_mask & bindings->descriptor_table_active_mask;

    while (descriptor_table_mask)
    {
        root_parameter_index = vkd3d_bitmask_iter64(&descriptor_table_mask);
        base_descriptor = d3d12_desc_from_gpu_handle(bindings->descriptor_tables[root_parameter_index]);

        table = root_signature_get_descriptor_table(root_signature, root_parameter_index);

        table_offsets[table->table_index] = d3d12_desc_heap_offset(base_descriptor);
    }

    /* Set descriptor offsets */
    VK_CALL(vkCmdPushConstants(list->vk_command_buffer,
        root_signature->vk_pipeline_layout, VK_SHADER_STAGE_ALL,
        root_signature->descriptor_table_offset,
        root_signature->descriptor_table_count * sizeof(uint32_t),
        table_offsets));

    bindings->dirty_flags &= ~VKD3D_PIPELINE_DIRTY_DESCRIPTOR_TABLE_OFFSETS;
}

static bool vk_write_descriptor_set_from_root_descriptor(VkWriteDescriptorSet *vk_descriptor_write,
        const struct d3d12_root_parameter *root_parameter, VkDescriptorSet vk_descriptor_set,
        const union vkd3d_descriptor_info *descriptors)
{
    const union vkd3d_descriptor_info *descriptor;

    descriptor = &descriptors[root_parameter->u.descriptor.packed_descriptor];

    switch (root_parameter->parameter_type)
    {
        case D3D12_ROOT_PARAMETER_TYPE_CBV:
            vk_descriptor_write->descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;

            if (!descriptor->buffer.buffer)
                return false;
            break;
        case D3D12_ROOT_PARAMETER_TYPE_SRV:
            vk_descriptor_write->descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;

            if (!descriptor->buffer_view)
                return false;
            break;
        case D3D12_ROOT_PARAMETER_TYPE_UAV:
            vk_descriptor_write->descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER;

            if (!descriptor->buffer_view)
                return false;
            break;
        default:
            ERR("Invalid root descriptor %#x.\n", root_parameter->parameter_type);
            return false;
    }

    vk_descriptor_write->sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    vk_descriptor_write->pNext = NULL;
    vk_descriptor_write->dstSet = vk_descriptor_set;
    vk_descriptor_write->dstBinding = root_parameter->u.descriptor.binding->binding.binding;
    vk_descriptor_write->dstArrayElement = 0;
    vk_descriptor_write->descriptorCount = 1;
    vk_descriptor_write->pImageInfo = NULL;
    vk_descriptor_write->pBufferInfo = &descriptor->buffer;
    vk_descriptor_write->pTexelBufferView = &descriptor->buffer_view;
    return true;
}

static bool vk_write_descriptor_set_and_inline_uniform_block(VkWriteDescriptorSet *vk_descriptor_write,
        VkWriteDescriptorSetInlineUniformBlockEXT *vk_inline_uniform_block_write,
        VkDescriptorSet vk_descriptor_set, const struct d3d12_root_signature *root_signature,
        const void* data)
{
    vk_inline_uniform_block_write->sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_INLINE_UNIFORM_BLOCK_EXT;
    vk_inline_uniform_block_write->pNext = NULL;
    vk_inline_uniform_block_write->dataSize = root_signature->push_constant_range.size;
    vk_inline_uniform_block_write->pData = data;

    vk_descriptor_write->sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    vk_descriptor_write->pNext = vk_inline_uniform_block_write;
    vk_descriptor_write->dstSet = vk_descriptor_set;
    vk_descriptor_write->dstBinding = root_signature->push_constant_ubo_binding.binding;
    vk_descriptor_write->dstArrayElement = 0;
    vk_descriptor_write->descriptorCount = root_signature->push_constant_range.size;
    vk_descriptor_write->descriptorType = VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK_EXT;
    vk_descriptor_write->pImageInfo = NULL;
    vk_descriptor_write->pBufferInfo = NULL;
    vk_descriptor_write->pTexelBufferView = NULL;
    return true;
}

static void d3d12_command_list_update_descriptor_heaps(struct d3d12_command_list *list,
        VkPipelineBindPoint bind_point)
{
    struct vkd3d_pipeline_bindings *bindings = &list->pipeline_bindings[bind_point];
    const struct d3d12_root_signature *root_signature = bindings->root_signature;
    const struct vkd3d_vk_device_procs *vk_procs = &list->device->vk_procs;

    while (bindings->descriptor_heap_dirty_mask)
    {
        unsigned int heap_index = vkd3d_bitmask_iter64(&bindings->descriptor_heap_dirty_mask);

        if (list->descriptor_heaps[heap_index])
        {
            VK_CALL(vkCmdBindDescriptorSets(list->vk_command_buffer, bind_point,
                root_signature->vk_pipeline_layout, heap_index, 1,
                &list->descriptor_heaps[heap_index], 0, NULL));
        }
    }
}

static void d3d12_command_list_update_static_samplers(struct d3d12_command_list *list,
        VkPipelineBindPoint bind_point)
{
    struct vkd3d_pipeline_bindings *bindings = &list->pipeline_bindings[bind_point];
    const struct d3d12_root_signature *root_signature = bindings->root_signature;
    const struct vkd3d_vk_device_procs *vk_procs = &list->device->vk_procs;

    VK_CALL(vkCmdBindDescriptorSets(list->vk_command_buffer, bind_point,
            root_signature->vk_pipeline_layout,
            root_signature->sampler_descriptor_set,
            1, &bindings->static_sampler_set, 0, NULL));

    bindings->dirty_flags &= ~VKD3D_PIPELINE_DIRTY_STATIC_SAMPLER_SET;
}

static void d3d12_command_list_update_root_constants(struct d3d12_command_list *list,
        VkPipelineBindPoint bind_point)
{
    struct vkd3d_pipeline_bindings *bindings = &list->pipeline_bindings[bind_point];
    const struct d3d12_root_signature *root_signature = bindings->root_signature;
    const struct vkd3d_vk_device_procs *vk_procs = &list->device->vk_procs;
    const struct d3d12_root_constant *root_constant;
    unsigned int root_parameter_index;

    while (bindings->root_constant_dirty_mask)
    {
        root_parameter_index = vkd3d_bitmask_iter64(&bindings->root_constant_dirty_mask);
        root_constant = root_signature_get_32bit_constants(root_signature, root_parameter_index);

        VK_CALL(vkCmdPushConstants(list->vk_command_buffer,
                root_signature->vk_pipeline_layout, VK_SHADER_STAGE_ALL,
                root_constant->constant_index * sizeof(uint32_t),
                root_constant->constant_count * sizeof(uint32_t),
                &bindings->root_constants[root_constant->constant_index]));
    }
}

static void d3d12_command_list_fetch_inline_uniform_block_data(struct d3d12_command_list *list,
        VkPipelineBindPoint bind_point, uint32_t *dst_data)
{
    struct vkd3d_pipeline_bindings *bindings = &list->pipeline_bindings[bind_point];
    const struct d3d12_root_signature *root_signature = bindings->root_signature;
    uint64_t descriptor_table_mask = bindings->descriptor_table_active_mask;
    uint64_t root_constant_mask = root_signature->root_constant_mask;
    const uint32_t *src_data = bindings->root_constants;
    const struct d3d12_root_descriptor_table *table;
    const struct d3d12_root_constant *root_constant;
    const struct d3d12_desc *base_descriptor;
    unsigned int root_parameter_index;
    uint32_t first_table_offset;

    while (root_constant_mask)
    {
        root_parameter_index = vkd3d_bitmask_iter64(&root_constant_mask);
        root_constant = root_signature_get_32bit_constants(root_signature, root_parameter_index);

        memcpy(&dst_data[root_constant->constant_index],
                &src_data[root_constant->constant_index],
                root_constant->constant_count * sizeof(uint32_t));
    }

    first_table_offset = root_signature->descriptor_table_offset / sizeof(uint32_t);

    while (descriptor_table_mask)
    {
        root_parameter_index = vkd3d_bitmask_iter64(&descriptor_table_mask);
        base_descriptor = d3d12_desc_from_gpu_handle(bindings->descriptor_tables[root_parameter_index]);

        table = root_signature_get_descriptor_table(root_signature, root_parameter_index);

        dst_data[first_table_offset + table->table_index] = d3d12_desc_heap_offset(base_descriptor);
    }

    /* Reset dirty flags to avoid redundant updates in the future */
    bindings->dirty_flags &= ~VKD3D_PIPELINE_DIRTY_DESCRIPTOR_TABLE_OFFSETS;
    bindings->root_constant_dirty_mask = 0;
}

static bool vk_write_descriptor_set_from_uav_counter_binding(struct d3d12_command_list *list,
        VkPipelineBindPoint bind_point, VkDescriptorSet vk_descriptor_set,
        VkWriteDescriptorSet *vk_descriptor_write, VkDescriptorBufferInfo *vk_buffer_info)
{
    struct vkd3d_pipeline_bindings *bindings = &list->pipeline_bindings[bind_point];
    const struct d3d12_root_signature *root_signature = bindings->root_signature;

    bindings->dirty_flags &= ~VKD3D_PIPELINE_DIRTY_UAV_COUNTER_BINDING;

    if (!list->uav_counter_address_buffer || !(root_signature->flags & VKD3D_ROOT_SIGNATURE_USE_BINDLESS_UAV_COUNTERS))
        return false;

    vk_buffer_info->buffer = list->uav_counter_address_buffer;
    vk_buffer_info->offset = 0;
    vk_buffer_info->range = VK_WHOLE_SIZE;

    vk_descriptor_write->sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    vk_descriptor_write->pNext = NULL;
    vk_descriptor_write->dstSet = vk_descriptor_set;
    vk_descriptor_write->dstBinding = root_signature->uav_counter_binding.binding;
    vk_descriptor_write->dstArrayElement = 0;
    vk_descriptor_write->descriptorCount = 1;
    vk_descriptor_write->descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    vk_descriptor_write->pImageInfo = NULL;
    vk_descriptor_write->pBufferInfo = vk_buffer_info;
    vk_descriptor_write->pTexelBufferView = NULL;
    return true;
}

static void d3d12_command_list_update_root_descriptors(struct d3d12_command_list *list,
        VkPipelineBindPoint bind_point)
{
    struct vkd3d_pipeline_bindings *bindings = &list->pipeline_bindings[bind_point];
    const struct d3d12_root_signature *root_signature = bindings->root_signature;
    const struct vkd3d_vk_device_procs *vk_procs = &list->device->vk_procs;
    VkWriteDescriptorSetInlineUniformBlockEXT inline_uniform_block_write;
    VkWriteDescriptorSet descriptor_writes[D3D12_MAX_ROOT_COST / 2 + 2];
    uint32_t inline_uniform_block_data[D3D12_MAX_ROOT_COST];
    const struct d3d12_root_parameter *root_parameter;
    VkDescriptorSet descriptor_set = VK_NULL_HANDLE;
    VkDescriptorBufferInfo uav_counter_descriptor;
    unsigned int descriptor_write_count = 0;
    unsigned int root_parameter_index;

    if (!(root_signature->flags & VKD3D_ROOT_SIGNATURE_USE_PUSH_DESCRIPTORS))
    {
        /* Ensure that we populate all descriptors if push descriptors cannot be used */
        bindings->dirty_flags |= VKD3D_PIPELINE_DIRTY_UAV_COUNTER_BINDING;
        bindings->root_descriptor_dirty_mask |= bindings->root_descriptor_active_mask & root_signature->root_descriptor_mask;

        descriptor_set = d3d12_command_allocator_allocate_descriptor_set(
                list->allocator, root_signature->vk_root_descriptor_layout, VKD3D_DESCRIPTOR_POOL_TYPE_STATIC);
    }

    /* TODO bind null descriptors for inactive root descriptors */
    bindings->root_descriptor_dirty_mask &= bindings->root_descriptor_active_mask;

    while (bindings->root_descriptor_dirty_mask)
    {
        root_parameter_index = vkd3d_bitmask_iter64(&bindings->root_descriptor_dirty_mask);
        root_parameter = root_signature_get_root_descriptor(root_signature, root_parameter_index);

        if (!vk_write_descriptor_set_from_root_descriptor(&descriptor_writes[descriptor_write_count],
                root_parameter, descriptor_set, bindings->root_descriptors))
            continue;

        descriptor_write_count += 1;
    }

    if (bindings->dirty_flags & VKD3D_PIPELINE_DIRTY_UAV_COUNTER_BINDING)
    {
        if (vk_write_descriptor_set_from_uav_counter_binding(list, bind_point,
                descriptor_set, &descriptor_writes[descriptor_write_count], &uav_counter_descriptor))
            descriptor_write_count += 1;
    }

    if (root_signature->flags & VKD3D_ROOT_SIGNATURE_USE_INLINE_UNIFORM_BLOCK)
    {
        d3d12_command_list_fetch_inline_uniform_block_data(list, bind_point, inline_uniform_block_data);

        vk_write_descriptor_set_and_inline_uniform_block(&descriptor_writes[descriptor_write_count],
                &inline_uniform_block_write, descriptor_set, root_signature, inline_uniform_block_data);

        descriptor_write_count += 1;
    }

    if (!descriptor_write_count)
        return;

    if (root_signature->flags & VKD3D_ROOT_SIGNATURE_USE_PUSH_DESCRIPTORS)
    {
        VK_CALL(vkCmdPushDescriptorSetKHR(list->vk_command_buffer, bind_point,
                root_signature->vk_pipeline_layout, root_signature->root_descriptor_set,
                descriptor_write_count, descriptor_writes));
    }
    else
    {
        VK_CALL(vkUpdateDescriptorSets(list->device->vk_device,
                descriptor_write_count, descriptor_writes, 0, NULL));
        VK_CALL(vkCmdBindDescriptorSets(list->vk_command_buffer, bind_point,
                root_signature->vk_pipeline_layout, root_signature->root_descriptor_set,
                1, &descriptor_set, 0, NULL));
    }
}

static void d3d12_command_list_update_descriptors(struct d3d12_command_list *list,
        VkPipelineBindPoint bind_point)
{
    struct vkd3d_pipeline_bindings *bindings = &list->pipeline_bindings[bind_point];
    const struct d3d12_root_signature *rs = bindings->root_signature;

    if (!rs)
        return;

    if (bindings->descriptor_heap_dirty_mask)
        d3d12_command_list_update_descriptor_heaps(list, bind_point);

    if (bindings->dirty_flags & VKD3D_PIPELINE_DIRTY_STATIC_SAMPLER_SET)
        d3d12_command_list_update_static_samplers(list, bind_point);

    if (bindings->dirty_flags & VKD3D_PIPELINE_DIRTY_PACKED_DESCRIPTOR_SET)
        d3d12_command_list_update_packed_descriptors(list, bind_point);

    if (rs->flags & VKD3D_ROOT_SIGNATURE_USE_INLINE_UNIFORM_BLOCK)
    {
        /* Root constants and descriptor table offsets are part of the root descriptor set */
        if (bindings->root_descriptor_dirty_mask || bindings->root_constant_dirty_mask
                || (bindings->dirty_flags & (VKD3D_PIPELINE_DIRTY_DESCRIPTOR_TABLE_OFFSETS | VKD3D_PIPELINE_DIRTY_UAV_COUNTER_BINDING)))
            d3d12_command_list_update_root_descriptors(list, bind_point);
    }
    else
    {
        if (bindings->root_descriptor_dirty_mask || (bindings->dirty_flags & VKD3D_PIPELINE_DIRTY_UAV_COUNTER_BINDING))
            d3d12_command_list_update_root_descriptors(list, bind_point);

        if (bindings->root_constant_dirty_mask)
            d3d12_command_list_update_root_constants(list, bind_point);

        if (bindings->dirty_flags & VKD3D_PIPELINE_DIRTY_DESCRIPTOR_TABLE_OFFSETS)
            d3d12_command_list_update_descriptor_table_offsets(list, bind_point);
    }
}

static bool d3d12_command_list_update_compute_state(struct d3d12_command_list *list)
{
    d3d12_command_list_end_current_render_pass(list, false);

    if (!d3d12_command_list_update_compute_pipeline(list))
        return false;

    d3d12_command_list_update_descriptors(list, VK_PIPELINE_BIND_POINT_COMPUTE);

    return true;
}

static void d3d12_command_list_update_dynamic_state(struct d3d12_command_list *list)
{
    const struct vkd3d_vk_device_procs *vk_procs = &list->device->vk_procs;
    struct vkd3d_dynamic_state *dyn_state = &list->dynamic_state;

    /* Make sure we only update states that are dynamic in the pipeline */
    dyn_state->dirty_flags &= list->state->u.graphics.dynamic_state_flags;

    if (dyn_state->dirty_flags & VKD3D_DYNAMIC_STATE_VIEWPORT)
    {
        VK_CALL(vkCmdSetViewport(list->vk_command_buffer,
                0, dyn_state->viewport_count, dyn_state->viewports));
    }

    if (dyn_state->dirty_flags & VKD3D_DYNAMIC_STATE_SCISSOR)
    {
        VK_CALL(vkCmdSetScissor(list->vk_command_buffer,
                0, dyn_state->viewport_count, dyn_state->scissors));
    }

    if (dyn_state->dirty_flags & VKD3D_DYNAMIC_STATE_BLEND_CONSTANTS)
    {
        VK_CALL(vkCmdSetBlendConstants(list->vk_command_buffer,
                dyn_state->blend_constants));
    }

    if (dyn_state->dirty_flags & VKD3D_DYNAMIC_STATE_STENCIL_REFERENCE)
    {
        VK_CALL(vkCmdSetStencilReference(list->vk_command_buffer,
                VK_STENCIL_FRONT_AND_BACK, dyn_state->stencil_reference));
    }

    if (dyn_state->dirty_flags & VKD3D_DYNAMIC_STATE_DEPTH_BOUNDS)
    {
        VK_CALL(vkCmdSetDepthBounds(list->vk_command_buffer,
                dyn_state->min_depth_bounds, dyn_state->max_depth_bounds));
    }

    dyn_state->dirty_flags = 0;
}

static bool d3d12_command_list_begin_render_pass(struct d3d12_command_list *list)
{
    const struct vkd3d_vk_device_procs *vk_procs = &list->device->vk_procs;
    struct d3d12_graphics_pipeline_state *graphics;
    struct VkRenderPassBeginInfo begin_desc;
    VkRenderPass vk_render_pass;

    if (!d3d12_command_list_update_graphics_pipeline(list))
        return false;
    if (!d3d12_command_list_update_current_framebuffer(list))
        return false;

    if (list->dynamic_state.dirty_flags)
        d3d12_command_list_update_dynamic_state(list);

    d3d12_command_list_update_descriptors(list, VK_PIPELINE_BIND_POINT_GRAPHICS);

    if (list->current_render_pass != VK_NULL_HANDLE)
        return true;

    vk_render_pass = list->pso_render_pass;
    assert(vk_render_pass);

    /* Emit deferred clears that we cannot clear inside the
     * render pass, e.g. when the attachments to clear are
     * not included in the current pipeline's render pass. */
    d3d12_command_list_emit_render_pass_clears(list, true);

    if (!list->render_pass_suspended)
        d3d12_command_list_emit_render_pass_transition(list, VKD3D_RENDER_PASS_TRANSITION_MODE_BEGIN);

    begin_desc.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    begin_desc.pNext = NULL;
    begin_desc.renderPass = vk_render_pass;
    begin_desc.framebuffer = list->current_framebuffer;
    begin_desc.renderArea.offset.x = 0;
    begin_desc.renderArea.offset.y = 0;
    d3d12_command_list_get_fb_extent(list,
            &begin_desc.renderArea.extent.width, &begin_desc.renderArea.extent.height, NULL);
    begin_desc.clearValueCount = 0;
    begin_desc.pClearValues = NULL;
    VK_CALL(vkCmdBeginRenderPass(list->vk_command_buffer, &begin_desc, VK_SUBPASS_CONTENTS_INLINE));

    list->current_render_pass = vk_render_pass;

    /* Emit deferred clears with vkCmdClearAttachment */
    d3d12_command_list_emit_render_pass_clears(list, false);

    graphics = &list->state->u.graphics;
    if (graphics->xfb_enabled)
    {
        VK_CALL(vkCmdBeginTransformFeedbackEXT(list->vk_command_buffer, 0, ARRAY_SIZE(list->so_counter_buffers),
                list->so_counter_buffers, list->so_counter_buffer_offsets));

        list->xfb_enabled = true;
    }

    return true;
}

static void d3d12_command_list_check_index_buffer_strip_cut_value(struct d3d12_command_list *list)
{
    struct d3d12_graphics_pipeline_state *graphics = &list->state->u.graphics;
    if (TRACE_ON())
    {
        /* In Vulkan, the strip cut value is derived from the index buffer format. */
        switch (graphics->index_buffer_strip_cut_value)
        {
        case D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_0xFFFF:
            if (list->index_buffer_format != DXGI_FORMAT_R16_UINT)
            {
                TRACE("Strip cut value 0xffff is not supported with index buffer format %#x.\n",
                      list->index_buffer_format);
            }
            break;

        case D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_0xFFFFFFFF:
            if (list->index_buffer_format != DXGI_FORMAT_R32_UINT)
            {
                TRACE("Strip cut value 0xffffffff is not supported with index buffer format %#x.\n",
                      list->index_buffer_format);
            }
            break;

        default:
            break;
        }
    }
}

static void STDMETHODCALLTYPE d3d12_command_list_DrawInstanced(d3d12_command_list_iface *iface,
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

    if (!d3d12_command_list_begin_render_pass(list))
    {
        WARN("Failed to begin render pass, ignoring draw call.\n");
        return;
    }

    VK_CALL(vkCmdDraw(list->vk_command_buffer, vertex_count_per_instance,
            instance_count, start_vertex_location, start_instance_location));
}

static void STDMETHODCALLTYPE d3d12_command_list_DrawIndexedInstanced(d3d12_command_list_iface *iface,
        UINT index_count_per_instance, UINT instance_count, UINT start_vertex_location,
        INT base_vertex_location, UINT start_instance_location)
{
    struct d3d12_command_list *list = impl_from_ID3D12GraphicsCommandList(iface);
    const struct vkd3d_vk_device_procs *vk_procs;

    TRACE("iface %p, index_count_per_instance %u, instance_count %u, start_vertex_location %u, "
            "base_vertex_location %d, start_instance_location %u.\n",
            iface, index_count_per_instance, instance_count, start_vertex_location,
            base_vertex_location, start_instance_location);

    if (!d3d12_command_list_begin_render_pass(list))
    {
        WARN("Failed to begin render pass, ignoring draw call.\n");
        return;
    }

    vk_procs = &list->device->vk_procs;

    d3d12_command_list_check_index_buffer_strip_cut_value(list);

    VK_CALL(vkCmdDrawIndexed(list->vk_command_buffer, index_count_per_instance,
            instance_count, start_vertex_location, base_vertex_location, start_instance_location));
}

static void STDMETHODCALLTYPE d3d12_command_list_Dispatch(d3d12_command_list_iface *iface,
        UINT x, UINT y, UINT z)
{
    struct d3d12_command_list *list = impl_from_ID3D12GraphicsCommandList(iface);
    const struct vkd3d_vk_device_procs *vk_procs;

    TRACE("iface %p, x %u, y %u, z %u.\n", iface, x, y, z);

    if (!d3d12_command_list_update_compute_state(list))
    {
        WARN("Failed to update compute state, ignoring dispatch.\n");
        return;
    }

    vk_procs = &list->device->vk_procs;

    VK_CALL(vkCmdDispatch(list->vk_command_buffer, x, y, z));
}

static void STDMETHODCALLTYPE d3d12_command_list_CopyBufferRegion(d3d12_command_list_iface *iface,
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

    d3d12_command_list_end_current_render_pass(list, true);

    buffer_copy.srcOffset = src_offset + src_resource->heap_offset;
    buffer_copy.dstOffset = dst_offset + dst_resource->heap_offset;
    buffer_copy.size = byte_count;

    VK_CALL(vkCmdCopyBuffer(list->vk_command_buffer,
            src_resource->u.vk_buffer, dst_resource->u.vk_buffer, 1, &buffer_copy));
}

static void vk_image_subresource_layers_from_d3d12(VkImageSubresourceLayers *subresource,
        const struct vkd3d_format *format, unsigned int sub_resource_idx, unsigned int miplevel_count)
{
    subresource->aspectMask = format->vk_aspect_mask;
    subresource->mipLevel = sub_resource_idx % miplevel_count;
    subresource->baseArrayLayer = sub_resource_idx / miplevel_count;
    subresource->layerCount = 1;
}

static void vk_extent_3d_from_d3d12_miplevel(VkExtent3D *extent,
        const D3D12_RESOURCE_DESC *resource_desc, unsigned int miplevel_idx)
{
    extent->width = d3d12_resource_desc_get_width(resource_desc, miplevel_idx);
    extent->height = d3d12_resource_desc_get_height(resource_desc, miplevel_idx);
    extent->depth = d3d12_resource_desc_get_depth(resource_desc, miplevel_idx);
}

static void vk_buffer_image_copy_from_d3d12(VkBufferImageCopy *copy,
        const D3D12_PLACED_SUBRESOURCE_FOOTPRINT *footprint, unsigned int sub_resource_idx,
        const D3D12_RESOURCE_DESC *image_desc, const struct vkd3d_format *format,
        const D3D12_BOX *src_box, unsigned int dst_x, unsigned int dst_y, unsigned int dst_z)
{
    copy->bufferOffset = footprint->Offset;
    if (src_box)
    {
        VkDeviceSize row_count = footprint->Footprint.Height / format->block_height;
        copy->bufferOffset += vkd3d_format_get_data_offset(format, footprint->Footprint.RowPitch,
                row_count * footprint->Footprint.RowPitch, src_box->left, src_box->top, src_box->front);
    }
    copy->bufferRowLength = footprint->Footprint.RowPitch /
            (format->byte_count * format->block_byte_count) * format->block_width;
    copy->bufferImageHeight = footprint->Footprint.Height;
    vk_image_subresource_layers_from_d3d12(&copy->imageSubresource,
            format, sub_resource_idx, image_desc->MipLevels);
    copy->imageOffset.x = dst_x;
    copy->imageOffset.y = dst_y;
    copy->imageOffset.z = dst_z;

    vk_extent_3d_from_d3d12_miplevel(&copy->imageExtent, image_desc,
            copy->imageSubresource.mipLevel);
    copy->imageExtent.width -= copy->imageOffset.x;
    copy->imageExtent.height -= copy->imageOffset.y;
    copy->imageExtent.depth -= copy->imageOffset.z;

    if (src_box)
    {
        copy->imageExtent.width = min(copy->imageExtent.width, src_box->right - src_box->left);
        copy->imageExtent.height = min(copy->imageExtent.height, src_box->bottom - src_box->top);
        copy->imageExtent.depth = min(copy->imageExtent.depth, src_box->back - src_box->front);
    }
    else
    {
        copy->imageExtent.width = min(copy->imageExtent.width, footprint->Footprint.Width);
        copy->imageExtent.height = min(copy->imageExtent.height, footprint->Footprint.Height);
        copy->imageExtent.depth = min(copy->imageExtent.depth, footprint->Footprint.Depth);
    }
}

static void vk_image_buffer_copy_from_d3d12(VkBufferImageCopy *copy,
        const D3D12_PLACED_SUBRESOURCE_FOOTPRINT *footprint, unsigned int sub_resource_idx,
        const D3D12_RESOURCE_DESC *image_desc, const struct vkd3d_format *format,
        const D3D12_BOX *src_box, unsigned int dst_x, unsigned int dst_y, unsigned int dst_z)
{
    VkDeviceSize row_count = footprint->Footprint.Height / format->block_height;

    copy->bufferOffset = footprint->Offset + vkd3d_format_get_data_offset(format,
            footprint->Footprint.RowPitch, row_count * footprint->Footprint.RowPitch, dst_x, dst_y, dst_z);
    copy->bufferRowLength = footprint->Footprint.RowPitch /
            (format->byte_count * format->block_byte_count) * format->block_width;
    copy->bufferImageHeight = footprint->Footprint.Height;
    vk_image_subresource_layers_from_d3d12(&copy->imageSubresource,
            format, sub_resource_idx, image_desc->MipLevels);
    copy->imageOffset.x = src_box ? src_box->left : 0;
    copy->imageOffset.y = src_box ? src_box->top : 0;
    copy->imageOffset.z = src_box ? src_box->front : 0;
    if (src_box)
    {
        copy->imageExtent.width = src_box->right - src_box->left;
        copy->imageExtent.height = src_box->bottom - src_box->top;
        copy->imageExtent.depth = src_box->back - src_box->front;
    }
    else
    {
        unsigned int miplevel = copy->imageSubresource.mipLevel;
        vk_extent_3d_from_d3d12_miplevel(&copy->imageExtent, image_desc, miplevel);
    }
}

static void vk_image_copy_from_d3d12(VkImageCopy *image_copy,
        unsigned int src_sub_resource_idx, unsigned int dst_sub_resource_idx,
        const D3D12_RESOURCE_DESC *src_desc, const D3D12_RESOURCE_DESC *dst_desc,
        const struct vkd3d_format *src_format, const struct vkd3d_format *dst_format,
        const D3D12_BOX *src_box, unsigned int dst_x, unsigned int dst_y, unsigned int dst_z)
{
    vk_image_subresource_layers_from_d3d12(&image_copy->srcSubresource,
            src_format, src_sub_resource_idx, src_desc->MipLevels);
    image_copy->srcOffset.x = src_box ? src_box->left : 0;
    image_copy->srcOffset.y = src_box ? src_box->top : 0;
    image_copy->srcOffset.z = src_box ? src_box->front : 0;
    vk_image_subresource_layers_from_d3d12(&image_copy->dstSubresource,
            dst_format, dst_sub_resource_idx, dst_desc->MipLevels);
    image_copy->dstOffset.x = dst_x;
    image_copy->dstOffset.y = dst_y;
    image_copy->dstOffset.z = dst_z;
    if (src_box)
    {
        image_copy->extent.width = src_box->right - src_box->left;
        image_copy->extent.height = src_box->bottom - src_box->top;
        image_copy->extent.depth = src_box->back - src_box->front;
    }
    else
    {
        unsigned int miplevel = image_copy->srcSubresource.mipLevel;
        vk_extent_3d_from_d3d12_miplevel(&image_copy->extent, src_desc, miplevel);
    }
}

static void d3d12_command_list_copy_image(struct d3d12_command_list *list,
        struct d3d12_resource *dst_resource, const struct vkd3d_format *dst_format,
        struct d3d12_resource *src_resource, const struct vkd3d_format *src_format,
        const VkImageCopy *region)
{
    const struct vkd3d_vk_device_procs *vk_procs = &list->device->vk_procs;
    struct vkd3d_texture_view_desc dst_view_desc, src_view_desc;
    struct vkd3d_copy_image_pipeline_key pipeline_key;
    VkPipelineStageFlags src_stages, dst_stages;
    struct vkd3d_copy_image_info pipeline_info;
    VkImageMemoryBarrier vk_image_barriers[2];
    VkWriteDescriptorSet vk_descriptor_write;
    struct vkd3d_copy_image_args push_args;
    struct vkd3d_view *dst_view, *src_view;
    VkAccessFlags src_access, dst_access;
    VkImageLayout src_layout, dst_layout;
    bool dst_is_depth_stencil, use_copy;
    VkDescriptorImageInfo vk_image_info;
    VkDescriptorSet vk_descriptor_set;
    VkRenderPassBeginInfo begin_info;
    VkFramebuffer vk_framebuffer;
    VkViewport viewport;
    VkExtent3D extent;
    VkRect2D scissor;
    unsigned int i;
    HRESULT hr;

    use_copy = dst_format->vk_aspect_mask == src_format->vk_aspect_mask;
    dst_is_depth_stencil = !!(dst_format->vk_aspect_mask & (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT));

    if (use_copy)
    {
        src_layout = d3d12_resource_pick_layout(src_resource, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        dst_layout = d3d12_resource_pick_layout(dst_resource, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        src_stages = VK_PIPELINE_STAGE_TRANSFER_BIT;
        dst_stages = VK_PIPELINE_STAGE_TRANSFER_BIT;
        src_access = VK_ACCESS_TRANSFER_READ_BIT;
        dst_access = VK_ACCESS_TRANSFER_WRITE_BIT;
    }
    else
    {
        src_layout = d3d12_resource_pick_layout(src_resource, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        src_stages = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        src_access = VK_ACCESS_SHADER_READ_BIT;

        if (dst_is_depth_stencil)
        {
            dst_layout = d3d12_resource_pick_layout(dst_resource, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
            dst_stages = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
            dst_access = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        }
        else
        {
            dst_layout = d3d12_resource_pick_layout(dst_resource, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
            dst_stages = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            dst_access = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        }
    }

    for (i = 0; i < ARRAY_SIZE(vk_image_barriers); i++)
    {
        vk_image_barriers[i].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        vk_image_barriers[i].pNext = NULL;
        vk_image_barriers[i].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        vk_image_barriers[i].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    }

    vk_image_barriers[0].srcAccessMask = 0;
    vk_image_barriers[0].dstAccessMask = dst_access;
    vk_image_barriers[0].oldLayout = dst_resource->common_layout;
    vk_image_barriers[0].newLayout = dst_layout;
    vk_image_barriers[0].image = dst_resource->u.vk_image;
    vk_image_barriers[0].subresourceRange = vk_subresource_range_from_layers(&region->dstSubresource);

    vk_image_barriers[1].srcAccessMask = 0;
    vk_image_barriers[1].dstAccessMask = src_access;
    vk_image_barriers[1].oldLayout = src_resource->common_layout;
    vk_image_barriers[1].newLayout = src_layout;
    vk_image_barriers[1].image = src_resource->u.vk_image;
    vk_image_barriers[1].subresourceRange = vk_subresource_range_from_layers(&region->srcSubresource);

    VK_CALL(vkCmdPipelineBarrier(list->vk_command_buffer,
            VK_PIPELINE_STAGE_TRANSFER_BIT, src_stages | dst_stages,
            0, 0, NULL, 0, NULL, ARRAY_SIZE(vk_image_barriers),
            vk_image_barriers));

    if (use_copy)
    {
        VK_CALL(vkCmdCopyImage(list->vk_command_buffer,
                src_resource->u.vk_image, src_layout,
                dst_resource->u.vk_image, dst_layout,
                1, region));
    }
    else
    {
        dst_view = src_view = NULL;

        if (!(dst_format = vkd3d_meta_get_copy_image_attachment_format(&list->device->meta_ops, dst_format, src_format)))
        {
            ERR("No attachment format found for source format %u.\n", src_format->vk_format);
            goto cleanup;
        }

        pipeline_key.format = dst_format;
        pipeline_key.view_type = vkd3d_meta_get_copy_image_view_type(dst_resource->desc.Dimension);
        pipeline_key.sample_count = vk_samples_from_dxgi_sample_desc(&dst_resource->desc.SampleDesc);

        if (FAILED(hr = vkd3d_meta_get_copy_image_pipeline(&list->device->meta_ops, &pipeline_key, &pipeline_info)))
        {
            ERR("Failed to obtain pipeline, format %u, view_type %u, sample_count %u.\n",
                    pipeline_key.format->vk_format, pipeline_key.view_type, pipeline_key.sample_count);
            goto cleanup;
        }

        d3d12_command_list_invalidate_current_pipeline(list);
        d3d12_command_list_invalidate_root_parameters(list, VK_PIPELINE_BIND_POINT_GRAPHICS, true);

        memset(&dst_view_desc, 0, sizeof(dst_view_desc));
        dst_view_desc.view_type = pipeline_key.view_type;
        dst_view_desc.layout = dst_layout;
        dst_view_desc.format = dst_format;
        dst_view_desc.miplevel_idx = region->dstSubresource.mipLevel;
        dst_view_desc.miplevel_count = 1;
        dst_view_desc.layer_idx = region->dstSubresource.baseArrayLayer;
        dst_view_desc.layer_count = region->dstSubresource.layerCount;
        dst_view_desc.allowed_swizzle = false;

        memset(&src_view_desc, 0, sizeof(src_view_desc));
        src_view_desc.view_type = pipeline_key.view_type;
        src_view_desc.layout = src_layout;
        src_view_desc.format = src_format;
        src_view_desc.miplevel_idx = region->srcSubresource.mipLevel;
        src_view_desc.miplevel_count = 1;
        src_view_desc.layer_idx = region->srcSubresource.baseArrayLayer;
        src_view_desc.layer_count = region->srcSubresource.layerCount;
        src_view_desc.allowed_swizzle = false;

        if (!vkd3d_create_texture_view(list->device, dst_resource->u.vk_image, &dst_view_desc, &dst_view) ||
                !vkd3d_create_texture_view(list->device, src_resource->u.vk_image, &src_view_desc, &src_view))
        {
            ERR("Failed to create image views.\n");
            goto cleanup;
        }

        if (!d3d12_command_allocator_add_view(list->allocator, dst_view) ||
                !d3d12_command_allocator_add_view(list->allocator, src_view))
        {
            ERR("Failed to add views.\n");
            goto cleanup;
        }

        extent.width = d3d12_resource_desc_get_width(&dst_resource->desc, dst_view_desc.miplevel_idx);
        extent.height = d3d12_resource_desc_get_height(&dst_resource->desc, dst_view_desc.miplevel_idx);
        extent.depth = dst_view_desc.layer_count;

        if (!d3d12_command_list_create_framebuffer(list, pipeline_info.vk_render_pass, 1, &dst_view->u.vk_image_view, extent, &vk_framebuffer))
        {
            ERR("Failed to create framebuffer.\n");
            goto cleanup;
        }

        begin_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        begin_info.pNext = NULL;
        begin_info.renderPass = pipeline_info.vk_render_pass;
        begin_info.framebuffer = vk_framebuffer;
        begin_info.clearValueCount = 0;
        begin_info.pClearValues = NULL;
        begin_info.renderArea.offset.x = 0;
        begin_info.renderArea.offset.y = 0;
        begin_info.renderArea.extent.width = extent.width;
        begin_info.renderArea.extent.height = extent.height;

        viewport.x = (float)region->dstOffset.x;
        viewport.y = (float)region->dstOffset.y;
        viewport.width = (float)region->extent.width;
        viewport.height = (float)region->extent.height;
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;

        scissor.offset.x = region->dstOffset.x;
        scissor.offset.y = region->dstOffset.y;
        scissor.extent.width = region->extent.width;
        scissor.extent.height = region->extent.height;

        push_args.offset.x = region->srcOffset.x - region->dstOffset.x;
        push_args.offset.y = region->srcOffset.y - region->dstOffset.y;

        vk_descriptor_set = d3d12_command_allocator_allocate_descriptor_set(
                list->allocator, pipeline_info.vk_set_layout,
                VKD3D_DESCRIPTOR_POOL_TYPE_STATIC);

        if (!vk_descriptor_set)
        {
            ERR("Failed to allocate descriptor set.\n");
            goto cleanup;
        }

        vk_image_info.sampler = VK_NULL_HANDLE;
        vk_image_info.imageView = src_view->u.vk_image_view;
        vk_image_info.imageLayout = src_view_desc.layout;

        vk_descriptor_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        vk_descriptor_write.pNext = NULL;
        vk_descriptor_write.dstSet = vk_descriptor_set;
        vk_descriptor_write.dstBinding = 0;
        vk_descriptor_write.dstArrayElement = 0;
        vk_descriptor_write.descriptorCount = 1;
        vk_descriptor_write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        vk_descriptor_write.pImageInfo = &vk_image_info;
        vk_descriptor_write.pBufferInfo = NULL;
        vk_descriptor_write.pTexelBufferView = NULL;

        VK_CALL(vkUpdateDescriptorSets(list->device->vk_device, 1, &vk_descriptor_write, 0, NULL));

        VK_CALL(vkCmdBeginRenderPass(list->vk_command_buffer, &begin_info, VK_SUBPASS_CONTENTS_INLINE));
        VK_CALL(vkCmdBindPipeline(list->vk_command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_info.vk_pipeline));
        VK_CALL(vkCmdSetViewport(list->vk_command_buffer, 0, 1, &viewport));
        VK_CALL(vkCmdSetScissor(list->vk_command_buffer, 0, 1, &scissor));
        VK_CALL(vkCmdBindDescriptorSets(list->vk_command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                pipeline_info.vk_pipeline_layout, 0, 1, &vk_descriptor_set, 0, NULL));
        VK_CALL(vkCmdPushConstants(list->vk_command_buffer, pipeline_info.vk_pipeline_layout,
                VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push_args), &push_args));
        VK_CALL(vkCmdDraw(list->vk_command_buffer, 3, region->dstSubresource.layerCount, 0, 0));
        VK_CALL(vkCmdEndRenderPass(list->vk_command_buffer));

cleanup:
        if (dst_view)
            vkd3d_view_decref(dst_view, list->device);
        if (src_view)
            vkd3d_view_decref(src_view, list->device);
    }

    vk_image_barriers[0].srcAccessMask = dst_access;
    vk_image_barriers[0].dstAccessMask = 0;
    vk_image_barriers[0].oldLayout = dst_layout;
    vk_image_barriers[0].newLayout = dst_resource->common_layout;

    vk_image_barriers[1].srcAccessMask = src_access;
    vk_image_barriers[1].dstAccessMask = 0;
    vk_image_barriers[1].oldLayout = src_layout;
    vk_image_barriers[1].newLayout = src_resource->common_layout;

    VK_CALL(vkCmdPipelineBarrier(list->vk_command_buffer,
            dst_stages, VK_PIPELINE_STAGE_TRANSFER_BIT,
            0, 0, NULL, 0, NULL, ARRAY_SIZE(vk_image_barriers),
            vk_image_barriers));
}

static bool validate_d3d12_box(const D3D12_BOX *box)
{
    return box->right > box->left
            && box->bottom > box->top
            && box->back > box->front;
}

static void d3d12_command_list_transition_image_layout(struct d3d12_command_list *list,
        VkImage vk_image, const VkImageSubresourceLayers *vk_subresource,
        VkPipelineStageFlags src_stages, VkAccessFlags src_access, VkImageLayout old_layout,
        VkPipelineStageFlags dst_stages, VkAccessFlags dst_access, VkImageLayout new_layout)
{
    const struct vkd3d_vk_device_procs *vk_procs = &list->device->vk_procs;
    VkImageMemoryBarrier vk_barrier;

    vk_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    vk_barrier.pNext = NULL;
    vk_barrier.srcAccessMask = src_access;
    vk_barrier.dstAccessMask = dst_access;
    vk_barrier.oldLayout = old_layout;
    vk_barrier.newLayout = new_layout;
    vk_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    vk_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    vk_barrier.image = vk_image;
    vk_barrier.subresourceRange = vk_subresource_range_from_layers(vk_subresource);

    VK_CALL(vkCmdPipelineBarrier(list->vk_command_buffer,
            src_stages, dst_stages, 0, 0, NULL, 0, NULL,
            1, &vk_barrier));
}

static void STDMETHODCALLTYPE d3d12_command_list_CopyTextureRegion(d3d12_command_list_iface *iface,
        const D3D12_TEXTURE_COPY_LOCATION *dst, UINT dst_x, UINT dst_y, UINT dst_z,
        const D3D12_TEXTURE_COPY_LOCATION *src, const D3D12_BOX *src_box)
{
    struct d3d12_command_list *list = impl_from_ID3D12GraphicsCommandList(iface);
    struct d3d12_resource *dst_resource, *src_resource;
    const struct vkd3d_format *src_format, *dst_format;
    const struct vkd3d_vk_device_procs *vk_procs;
    VkBufferImageCopy buffer_image_copy;
    VkImageLayout vk_layout;
    VkImageCopy image_copy;

    TRACE("iface %p, dst %p, dst_x %u, dst_y %u, dst_z %u, src %p, src_box %p.\n",
            iface, dst, dst_x, dst_y, dst_z, src, src_box);

    if (src_box && !validate_d3d12_box(src_box))
    {
        WARN("Empty box %s.\n", debug_d3d12_box(src_box));
        return;
    }

    vk_procs = &list->device->vk_procs;

    dst_resource = unsafe_impl_from_ID3D12Resource(dst->pResource);
    src_resource = unsafe_impl_from_ID3D12Resource(src->pResource);

    d3d12_command_list_track_resource_usage(list, dst_resource);
    d3d12_command_list_track_resource_usage(list, src_resource);

    d3d12_command_list_end_current_render_pass(list, false);

    if (src->Type == D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX
            && dst->Type == D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT)
    {
        assert(d3d12_resource_is_buffer(dst_resource));
        assert(d3d12_resource_is_texture(src_resource));

        if (!(dst_format = vkd3d_format_from_d3d12_resource_desc(list->device,
                &src_resource->desc, dst->u.PlacedFootprint.Footprint.Format)))
        {
            WARN("Invalid format %#x.\n", dst->u.PlacedFootprint.Footprint.Format);
            return;
        }

        if (dst_format->is_emulated)
        {
            FIXME("Format %#x is not supported yet.\n", dst_format->dxgi_format);
            return;
        }

        if ((dst_format->vk_aspect_mask & VK_IMAGE_ASPECT_DEPTH_BIT)
                && (dst_format->vk_aspect_mask & VK_IMAGE_ASPECT_STENCIL_BIT))
            FIXME("Depth-stencil format %#x not fully supported yet.\n", dst_format->dxgi_format);

        vk_image_buffer_copy_from_d3d12(&buffer_image_copy, &dst->u.PlacedFootprint,
                src->u.SubresourceIndex, &src_resource->desc, dst_format, src_box, dst_x, dst_y, dst_z);
        buffer_image_copy.bufferOffset += dst_resource->heap_offset;

        vk_layout = d3d12_resource_pick_layout(src_resource, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

        d3d12_command_list_transition_image_layout(list, src_resource->u.vk_image,
                &buffer_image_copy.imageSubresource, VK_PIPELINE_STAGE_TRANSFER_BIT,
                0, src_resource->common_layout, VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_ACCESS_TRANSFER_READ_BIT, vk_layout);

        VK_CALL(vkCmdCopyImageToBuffer(list->vk_command_buffer,
                src_resource->u.vk_image, vk_layout,
                dst_resource->u.vk_buffer, 1, &buffer_image_copy));

        d3d12_command_list_transition_image_layout(list, src_resource->u.vk_image,
                &buffer_image_copy.imageSubresource, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                vk_layout, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, src_resource->common_layout);
    }
    else if (src->Type == D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT
            && dst->Type == D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX)
    {
        assert(d3d12_resource_is_texture(dst_resource));
        assert(d3d12_resource_is_buffer(src_resource));

        if (!(src_format = vkd3d_format_from_d3d12_resource_desc(list->device,
                &dst_resource->desc, src->u.PlacedFootprint.Footprint.Format)))
        {
            WARN("Invalid format %#x.\n", src->u.PlacedFootprint.Footprint.Format);
            return;
        }

        if (src_format->is_emulated)
        {
            FIXME("Format %#x is not supported yet.\n", src_format->dxgi_format);
            return;
        }

        if ((src_format->vk_aspect_mask & VK_IMAGE_ASPECT_DEPTH_BIT)
                && (src_format->vk_aspect_mask & VK_IMAGE_ASPECT_STENCIL_BIT))
            FIXME("Depth-stencil format %#x not fully supported yet.\n", src_format->dxgi_format);

        vk_buffer_image_copy_from_d3d12(&buffer_image_copy, &src->u.PlacedFootprint,
                dst->u.SubresourceIndex, &dst_resource->desc, src_format, src_box, dst_x, dst_y, dst_z);
        buffer_image_copy.bufferOffset += src_resource->heap_offset;

        vk_layout = d3d12_resource_pick_layout(dst_resource, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

        d3d12_command_list_transition_image_layout(list, dst_resource->u.vk_image,
                &buffer_image_copy.imageSubresource, VK_PIPELINE_STAGE_TRANSFER_BIT,
                0, dst_resource->common_layout, VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_ACCESS_TRANSFER_WRITE_BIT, vk_layout);

        VK_CALL(vkCmdCopyBufferToImage(list->vk_command_buffer,
                src_resource->u.vk_buffer, dst_resource->u.vk_image,
                vk_layout, 1, &buffer_image_copy));

        d3d12_command_list_transition_image_layout(list, dst_resource->u.vk_image,
                &buffer_image_copy.imageSubresource, VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_ACCESS_TRANSFER_WRITE_BIT, vk_layout, VK_PIPELINE_STAGE_TRANSFER_BIT,
                0, dst_resource->common_layout);
    }
    else if (src->Type == D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX
            && dst->Type == D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX)
    {
        assert(d3d12_resource_is_texture(dst_resource));
        assert(d3d12_resource_is_texture(src_resource));

        if (!(dst_format = vkd3d_format_from_d3d12_resource_desc(list->device,
                &dst_resource->desc, DXGI_FORMAT_UNKNOWN)))
        {
            WARN("Invalid format %#x.\n", dst_resource->desc.Format);
            return;
        }
        if (!(src_format = vkd3d_format_from_d3d12_resource_desc(list->device,
                &src_resource->desc, DXGI_FORMAT_UNKNOWN)))
        {
            WARN("Invalid format %#x.\n", src_resource->desc.Format);
            return;
        }

        if ((dst_format->vk_aspect_mask & VK_IMAGE_ASPECT_DEPTH_BIT)
                && (dst_format->vk_aspect_mask & VK_IMAGE_ASPECT_STENCIL_BIT))
            FIXME("Depth-stencil format %#x not fully supported yet.\n", dst_format->dxgi_format);
        if ((src_format->vk_aspect_mask & VK_IMAGE_ASPECT_DEPTH_BIT)
                && (src_format->vk_aspect_mask & VK_IMAGE_ASPECT_STENCIL_BIT))
            FIXME("Depth-stencil format %#x not fully supported yet.\n", src_format->dxgi_format);

        vk_image_copy_from_d3d12(&image_copy, src->u.SubresourceIndex, dst->u.SubresourceIndex,
                 &src_resource->desc, &dst_resource->desc, src_format, dst_format,
                 src_box, dst_x, dst_y, dst_z);

        d3d12_command_list_copy_image(list, dst_resource, dst_format,
                src_resource, src_format, &image_copy);
    }
    else
    {
        FIXME("Copy type %#x -> %#x not implemented.\n", src->Type, dst->Type);
    }
}

static void STDMETHODCALLTYPE d3d12_command_list_CopyResource(d3d12_command_list_iface *iface,
        ID3D12Resource *dst, ID3D12Resource *src)
{
    struct d3d12_command_list *list = impl_from_ID3D12GraphicsCommandList(iface);
    struct d3d12_resource *dst_resource, *src_resource;
    const struct vkd3d_format *src_format, *dst_format;
    const struct vkd3d_vk_device_procs *vk_procs;
    VkBufferCopy vk_buffer_copy;
    VkImageCopy vk_image_copy;
    unsigned int layer_count;
    unsigned int i;

    TRACE("iface %p, dst_resource %p, src_resource %p.\n", iface, dst, src);

    vk_procs = &list->device->vk_procs;

    dst_resource = unsafe_impl_from_ID3D12Resource(dst);
    src_resource = unsafe_impl_from_ID3D12Resource(src);

    d3d12_command_list_track_resource_usage(list, dst_resource);
    d3d12_command_list_track_resource_usage(list, src_resource);

    d3d12_command_list_end_current_render_pass(list, false);

    if (d3d12_resource_is_buffer(dst_resource))
    {
        assert(d3d12_resource_is_buffer(src_resource));
        assert(src_resource->desc.Width == dst_resource->desc.Width);

        vk_buffer_copy.srcOffset = src_resource->heap_offset;
        vk_buffer_copy.dstOffset = dst_resource->heap_offset;
        vk_buffer_copy.size = dst_resource->desc.Width;
        VK_CALL(vkCmdCopyBuffer(list->vk_command_buffer,
                src_resource->u.vk_buffer, dst_resource->u.vk_buffer, 1, &vk_buffer_copy));
    }
    else
    {
        if (!(dst_format = vkd3d_format_from_d3d12_resource_desc(list->device,
                &dst_resource->desc, DXGI_FORMAT_UNKNOWN)))
        {
            WARN("Invalid format %#x.\n", dst_resource->desc.Format);
            return;
        }
        if (!(src_format = vkd3d_format_from_d3d12_resource_desc(list->device,
                &src_resource->desc, DXGI_FORMAT_UNKNOWN)))
        {
            WARN("Invalid format %#x.\n", src_resource->desc.Format);
            return;
        }

        layer_count = d3d12_resource_desc_get_layer_count(&dst_resource->desc);

        assert(d3d12_resource_is_texture(dst_resource));
        assert(d3d12_resource_is_texture(src_resource));
        assert(dst_resource->desc.MipLevels == src_resource->desc.MipLevels);
        assert(layer_count == d3d12_resource_desc_get_layer_count(&src_resource->desc));

        for (i = 0; i < dst_resource->desc.MipLevels; ++i)
        {
            vk_image_copy_from_d3d12(&vk_image_copy, i, i,
                    &src_resource->desc, &dst_resource->desc, src_format, dst_format, NULL, 0, 0, 0);
            vk_image_copy.dstSubresource.layerCount = layer_count;
            vk_image_copy.srcSubresource.layerCount = layer_count;

            d3d12_command_list_copy_image(list, dst_resource, dst_format,
                    src_resource, src_format, &vk_image_copy);
        }
    }
}

static void STDMETHODCALLTYPE d3d12_command_list_CopyTiles(d3d12_command_list_iface *iface,
        ID3D12Resource *tiled_resource, const D3D12_TILED_RESOURCE_COORDINATE *tile_region_start_coordinate,
        const D3D12_TILE_REGION_SIZE *tile_region_size, ID3D12Resource *buffer, UINT64 buffer_offset,
        D3D12_TILE_COPY_FLAGS flags)
{
    FIXME("iface %p, tiled_resource %p, tile_region_start_coordinate %p, tile_region_size %p, "
            "buffer %p, buffer_offset %#"PRIx64", flags %#x stub!\n",
            iface, tiled_resource, tile_region_start_coordinate, tile_region_size,
            buffer, buffer_offset, flags);
}

static void STDMETHODCALLTYPE d3d12_command_list_ResolveSubresource(d3d12_command_list_iface *iface,
        ID3D12Resource *dst, UINT dst_sub_resource_idx,
        ID3D12Resource *src, UINT src_sub_resource_idx, DXGI_FORMAT format)
{
    struct d3d12_command_list *list = impl_from_ID3D12GraphicsCommandList(iface);
    const struct vkd3d_format *src_format, *dst_format, *vk_format;
    struct d3d12_resource *dst_resource, *src_resource;
    const struct vkd3d_vk_device_procs *vk_procs;
    VkImageMemoryBarrier vk_image_barriers[2];
    VkImageLayout dst_layout, src_layout;
    const struct d3d12_device *device;
    VkImageResolve vk_image_resolve;
    unsigned int i;

    TRACE("iface %p, dst_resource %p, dst_sub_resource_idx %u, src_resource %p, src_sub_resource_idx %u, "
            "format %#x.\n", iface, dst, dst_sub_resource_idx, src, src_sub_resource_idx, format);

    device = list->device;
    vk_procs = &device->vk_procs;

    dst_resource = unsafe_impl_from_ID3D12Resource(dst);
    src_resource = unsafe_impl_from_ID3D12Resource(src);

    assert(d3d12_resource_is_texture(dst_resource));
    assert(d3d12_resource_is_texture(src_resource));

    d3d12_command_list_track_resource_usage(list, dst_resource);
    d3d12_command_list_track_resource_usage(list, src_resource);

    d3d12_command_list_end_current_render_pass(list, false);

    if (!(dst_format = vkd3d_format_from_d3d12_resource_desc(device, &dst_resource->desc, DXGI_FORMAT_UNKNOWN)))
    {
        WARN("Invalid format %#x.\n", dst_resource->desc.Format);
        return;
    }
    if (!(src_format = vkd3d_format_from_d3d12_resource_desc(device, &src_resource->desc, DXGI_FORMAT_UNKNOWN)))
    {
        WARN("Invalid format %#x.\n", src_resource->desc.Format);
        return;
    }

    if (dst_format->type == VKD3D_FORMAT_TYPE_TYPELESS || src_format->type == VKD3D_FORMAT_TYPE_TYPELESS)
    {
        if (!(vk_format = vkd3d_format_from_d3d12_resource_desc(device, &dst_resource->desc, format)))
        {
            WARN("Invalid format %#x.\n", format);
            return;
        }
        if (dst_format->vk_format != src_format->vk_format || dst_format->vk_format != vk_format->vk_format)
        {
            FIXME("Not implemented for typeless resources.\n");
            return;
        }
    }

    /* Resolve of depth/stencil images is not supported in Vulkan. */
    if ((dst_format->vk_aspect_mask & (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT))
            || (src_format->vk_aspect_mask & (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT)))
    {
        FIXME("Resolve of depth/stencil images is not implemented yet.\n");
        return;
    }

    vk_image_subresource_layers_from_d3d12(&vk_image_resolve.srcSubresource,
            src_format, src_sub_resource_idx, src_resource->desc.MipLevels);
    memset(&vk_image_resolve.srcOffset, 0, sizeof(vk_image_resolve.srcOffset));
    vk_image_subresource_layers_from_d3d12(&vk_image_resolve.dstSubresource,
            dst_format, dst_sub_resource_idx, dst_resource->desc.MipLevels);
    memset(&vk_image_resolve.dstOffset, 0, sizeof(vk_image_resolve.dstOffset));
    vk_extent_3d_from_d3d12_miplevel(&vk_image_resolve.extent,
            &dst_resource->desc, vk_image_resolve.dstSubresource.mipLevel);

    dst_layout = d3d12_resource_pick_layout(dst_resource, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    src_layout = d3d12_resource_pick_layout(src_resource, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

    for (i = 0; i < ARRAY_SIZE(vk_image_barriers); i++)
    {
        vk_image_barriers[i].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        vk_image_barriers[i].pNext = NULL;
        vk_image_barriers[i].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        vk_image_barriers[i].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    }

    vk_image_barriers[0].srcAccessMask = 0;
    vk_image_barriers[0].dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vk_image_barriers[0].oldLayout = dst_resource->common_layout;
    vk_image_barriers[0].newLayout = dst_layout;
    vk_image_barriers[0].image = dst_resource->u.vk_image;
    vk_image_barriers[0].subresourceRange = vk_subresource_range_from_layers(&vk_image_resolve.dstSubresource);

    vk_image_barriers[1].srcAccessMask = 0;
    vk_image_barriers[1].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    vk_image_barriers[1].oldLayout = src_resource->common_layout;
    vk_image_barriers[1].newLayout = src_layout;
    vk_image_barriers[1].image = src_resource->u.vk_image;
    vk_image_barriers[1].subresourceRange = vk_subresource_range_from_layers(&vk_image_resolve.srcSubresource);

    VK_CALL(vkCmdPipelineBarrier(list->vk_command_buffer,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
            0, 0, NULL, 0, NULL, ARRAY_SIZE(vk_image_barriers), vk_image_barriers));

    VK_CALL(vkCmdResolveImage(list->vk_command_buffer, src_resource->u.vk_image,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dst_resource->u.vk_image,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &vk_image_resolve));

    vk_image_barriers[0].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vk_image_barriers[0].dstAccessMask = 0;
    vk_image_barriers[0].oldLayout = dst_layout;
    vk_image_barriers[0].newLayout = dst_resource->common_layout;

    vk_image_barriers[1].srcAccessMask = 0;
    vk_image_barriers[1].dstAccessMask = 0;
    vk_image_barriers[1].oldLayout = src_layout;
    vk_image_barriers[1].newLayout = src_resource->common_layout;

    VK_CALL(vkCmdPipelineBarrier(list->vk_command_buffer,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
            0, 0, NULL, 0, NULL, ARRAY_SIZE(vk_image_barriers), vk_image_barriers));
}

static void STDMETHODCALLTYPE d3d12_command_list_IASetPrimitiveTopology(d3d12_command_list_iface *iface,
        D3D12_PRIMITIVE_TOPOLOGY topology)
{
    struct d3d12_command_list *list = impl_from_ID3D12GraphicsCommandList(iface);
    struct vkd3d_dynamic_state *dyn_state = &list->dynamic_state;

    TRACE("iface %p, topology %#x.\n", iface, topology);

    if (topology == D3D_PRIMITIVE_TOPOLOGY_UNDEFINED)
    {
        WARN("Ignoring D3D_PRIMITIVE_TOPOLOGY_UNDEFINED.\n");
        return;
    }

    if (dyn_state->primitive_topology == topology)
        return;

    dyn_state->primitive_topology = topology;
    d3d12_command_list_invalidate_current_pipeline(list);
}

static void STDMETHODCALLTYPE d3d12_command_list_RSSetViewports(d3d12_command_list_iface *iface,
        UINT viewport_count, const D3D12_VIEWPORT *viewports)
{
    struct d3d12_command_list *list = impl_from_ID3D12GraphicsCommandList(iface);
    struct vkd3d_dynamic_state *dyn_state = &list->dynamic_state;
    unsigned int i;

    TRACE("iface %p, viewport_count %u, viewports %p.\n", iface, viewport_count, viewports);

    if (viewport_count > ARRAY_SIZE(dyn_state->viewports))
    {
        FIXME_ONCE("Viewport count %u > D3D12_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE.\n", viewport_count);
        viewport_count = ARRAY_SIZE(dyn_state->viewports);
    }

    for (i = 0; i < viewport_count; ++i)
    {
        if (!viewports[i].Width || !viewports[i].Height)
        {
            FIXME_ONCE("Invalid viewport %u, ignoring RSSetViewports().\n", i);
            return;
        }
    }

    for (i = 0; i < viewport_count; ++i)
    {
        VkViewport *vk_viewport = &dyn_state->viewports[i];
        vk_viewport->x = viewports[i].TopLeftX;
        vk_viewport->y = viewports[i].TopLeftY + viewports[i].Height;
        vk_viewport->width = viewports[i].Width;
        vk_viewport->height = -viewports[i].Height;
        vk_viewport->minDepth = viewports[i].MinDepth;
        vk_viewport->maxDepth = viewports[i].MaxDepth;
    }

    if (dyn_state->viewport_count != viewport_count)
    {
        dyn_state->viewport_count = viewport_count;
        dyn_state->dirty_flags |= VKD3D_DYNAMIC_STATE_SCISSOR;
        d3d12_command_list_invalidate_current_pipeline(list);
    }

    dyn_state->dirty_flags |= VKD3D_DYNAMIC_STATE_VIEWPORT;
}

static void STDMETHODCALLTYPE d3d12_command_list_RSSetScissorRects(d3d12_command_list_iface *iface,
        UINT rect_count, const D3D12_RECT *rects)
{
    struct d3d12_command_list *list = impl_from_ID3D12GraphicsCommandList(iface);
    struct vkd3d_dynamic_state *dyn_state = &list->dynamic_state;
    unsigned int i;

    TRACE("iface %p, rect_count %u, rects %p.\n", iface, rect_count, rects);

    if (rect_count > ARRAY_SIZE(dyn_state->scissors))
    {
        FIXME("Rect count %u > D3D12_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE.\n", rect_count);
        rect_count = ARRAY_SIZE(dyn_state->scissors);
    }

    for (i = 0; i < rect_count; ++i)
    {
        VkRect2D *vk_rect = &dyn_state->scissors[i];
        vk_rect->offset.x = rects[i].left;
        vk_rect->offset.y = rects[i].top;
        vk_rect->extent.width = rects[i].right - rects[i].left;
        vk_rect->extent.height = rects[i].bottom - rects[i].top;
    }

    dyn_state->dirty_flags |= VKD3D_DYNAMIC_STATE_SCISSOR;
}

static void STDMETHODCALLTYPE d3d12_command_list_OMSetBlendFactor(d3d12_command_list_iface *iface,
        const FLOAT blend_factor[4])
{
    struct d3d12_command_list *list = impl_from_ID3D12GraphicsCommandList(iface);
    struct vkd3d_dynamic_state *dyn_state = &list->dynamic_state;
    unsigned int i;

    TRACE("iface %p, blend_factor %p.\n", iface, blend_factor);

    for (i = 0; i < 4; i++)
        dyn_state->blend_constants[i] = blend_factor[i];

    dyn_state->dirty_flags |= VKD3D_DYNAMIC_STATE_BLEND_CONSTANTS;
}

static void STDMETHODCALLTYPE d3d12_command_list_OMSetStencilRef(d3d12_command_list_iface *iface,
        UINT stencil_ref)
{
    struct d3d12_command_list *list = impl_from_ID3D12GraphicsCommandList(iface);
    struct vkd3d_dynamic_state *dyn_state = &list->dynamic_state;

    TRACE("iface %p, stencil_ref %u.\n", iface, stencil_ref);

    dyn_state->stencil_reference = stencil_ref;
    dyn_state->dirty_flags |= VKD3D_DYNAMIC_STATE_STENCIL_REFERENCE;
}

static void STDMETHODCALLTYPE d3d12_command_list_SetPipelineState(d3d12_command_list_iface *iface,
        ID3D12PipelineState *pipeline_state)
{
    struct d3d12_pipeline_state *state = unsafe_impl_from_ID3D12PipelineState(pipeline_state);
    struct d3d12_command_list *list = impl_from_ID3D12GraphicsCommandList(iface);

    TRACE("iface %p, pipeline_state %p.\n", iface, pipeline_state);

    if (list->state == state)
        return;

    d3d12_command_list_invalidate_current_pipeline(list);

    if (d3d12_pipeline_state_is_graphics(state))
    {
        uint32_t old_dynamic_state_flags = d3d12_pipeline_state_is_graphics(list->state)
            ? list->state->u.graphics.dynamic_state_flags
            : 0u;

        /* Reapply all dynamic states that were not dynamic in previously bound pipeline */
        list->dynamic_state.dirty_flags |= state->u.graphics.dynamic_state_flags & ~old_dynamic_state_flags;
    }

    list->state = state;
}

static VkImageLayout vk_image_layout_from_d3d12_resource_state(const struct d3d12_resource *resource, D3D12_RESOURCE_STATES state)
{
    if (state != D3D12_RESOURCE_STATE_PRESENT)
        return resource->common_layout;

    switch (resource->present_state)
    {
        case D3D12_RESOURCE_STATE_PRESENT:
            return VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        case D3D12_RESOURCE_STATE_COPY_SOURCE:
            return VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        default:
            FIXME("Unhandled present state %u.\n", resource->present_state);
            return resource->common_layout;
    }
}

static bool vk_image_memory_barrier_from_d3d12_transition(const struct d3d12_device *device,
        const struct d3d12_resource *resource, const D3D12_RESOURCE_TRANSITION_BARRIER *transition,
        VkQueueFlags vk_queue_flags, VkImageMemoryBarrier *vk_barrier, VkPipelineStageFlags *src_stage_mask,
        VkPipelineStageFlags *dst_stage_mask)
{
    if (transition->StateBefore != D3D12_RESOURCE_STATE_PRESENT &&
            transition->StateAfter != D3D12_RESOURCE_STATE_PRESENT)
        return false;

    vk_barrier->sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    vk_barrier->pNext = NULL;
    vk_barrier->srcAccessMask = 0;
    vk_barrier->dstAccessMask = 0;
    vk_barrier->oldLayout = vk_image_layout_from_d3d12_resource_state(resource, transition->StateBefore);
    vk_barrier->newLayout = vk_image_layout_from_d3d12_resource_state(resource, transition->StateAfter);
    vk_barrier->srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    vk_barrier->dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    vk_barrier->image = resource->u.vk_image;
    vk_barrier->subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    
    if (transition->Subresource == D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES)
    {
        vk_barrier->subresourceRange.baseMipLevel = 0;
        vk_barrier->subresourceRange.levelCount = VK_REMAINING_MIP_LEVELS;
        vk_barrier->subresourceRange.baseArrayLayer = 0;
        vk_barrier->subresourceRange.layerCount = VK_REMAINING_ARRAY_LAYERS;
    }
    else
    {
        vk_barrier->subresourceRange.baseMipLevel = transition->Subresource % resource->desc.MipLevels;
        vk_barrier->subresourceRange.levelCount = 1;
        vk_barrier->subresourceRange.baseArrayLayer = transition->Subresource / resource->desc.MipLevels;
        vk_barrier->subresourceRange.layerCount = 1;
    }

    vk_access_and_stage_flags_from_d3d12_resource_state(device, resource,
            transition->StateBefore, vk_queue_flags, src_stage_mask, &vk_barrier->srcAccessMask);
    vk_access_and_stage_flags_from_d3d12_resource_state(device, resource,
            transition->StateAfter, vk_queue_flags, dst_stage_mask, &vk_barrier->dstAccessMask);

    return vk_barrier->oldLayout != vk_barrier->newLayout;
}

static void STDMETHODCALLTYPE d3d12_command_list_ResourceBarrier(d3d12_command_list_iface *iface,
        UINT barrier_count, const D3D12_RESOURCE_BARRIER *barriers)
{
    struct d3d12_command_list *list = impl_from_ID3D12GraphicsCommandList(iface);
    const struct vkd3d_vk_device_procs *vk_procs = &list->device->vk_procs;
    bool have_aliasing_barriers = false, have_split_barriers = false;
    VkPipelineStageFlags dst_stage_mask, src_stage_mask;
    VkImageMemoryBarrier vk_image_barrier;
    VkMemoryBarrier vk_memory_barrier;
    unsigned int i;

    TRACE("iface %p, barrier_count %u, barriers %p.\n", iface, barrier_count, barriers);

    d3d12_command_list_end_current_render_pass(list, false);

    vk_memory_barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    vk_memory_barrier.pNext = NULL;
    vk_memory_barrier.srcAccessMask = 0;
    vk_memory_barrier.dstAccessMask = 0;

    src_stage_mask = 0;
    dst_stage_mask = 0;

    for (i = 0; i < barrier_count; ++i)
    {
        const D3D12_RESOURCE_BARRIER *current = &barriers[i];
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
                VkPipelineStageFlags src_image_stage_mask = 0, dst_image_stage_mask = 0;

                if (!is_valid_resource_state(transition->StateBefore))
                {
                    d3d12_command_list_mark_as_invalid(list,
                            "Invalid StateBefore %#x (barrier %u).", transition->StateBefore, i);
                    continue;
                }
                if (!is_valid_resource_state(transition->StateAfter))
                {
                    d3d12_command_list_mark_as_invalid(list,
                            "Invalid StateAfter %#x (barrier %u).", transition->StateAfter, i);
                    continue;
                }

                if (!(resource = unsafe_impl_from_ID3D12Resource(transition->pResource)))
                {
                    d3d12_command_list_mark_as_invalid(list, "A resource pointer is NULL.");
                    continue;
                }

                if (resource->flags & VKD3D_RESOURCE_PRESENT_STATE_TRANSITION &&
                        vk_image_memory_barrier_from_d3d12_transition(list->device, resource, transition,
                                 list->vk_queue_flags, &vk_image_barrier, &src_image_stage_mask, &dst_image_stage_mask))
                {
                    VK_CALL(vkCmdPipelineBarrier(list->vk_command_buffer,
                            src_image_stage_mask, dst_image_stage_mask, 0,
                            0, NULL, 0, NULL, 1, &vk_image_barrier));
                }
                else
                {
                    vk_access_and_stage_flags_from_d3d12_resource_state(list->device, resource,
                            transition->StateBefore, list->vk_queue_flags, &src_stage_mask,
                            &vk_memory_barrier.srcAccessMask);
                    vk_access_and_stage_flags_from_d3d12_resource_state(list->device, resource,
                            transition->StateAfter, list->vk_queue_flags, &dst_stage_mask,
                            &vk_memory_barrier.dstAccessMask);
                }

                TRACE("Transition barrier (resource %p, subresource %#x, before %#x, after %#x).\n",
                        resource, transition->Subresource, transition->StateBefore, transition->StateAfter);
                break;
            }

            case D3D12_RESOURCE_BARRIER_TYPE_UAV:
            {
                const D3D12_RESOURCE_UAV_BARRIER *uav = &current->u.UAV;
                resource = unsafe_impl_from_ID3D12Resource(uav->pResource);

                vk_access_and_stage_flags_from_d3d12_resource_state(list->device, resource,
                        D3D12_RESOURCE_STATE_UNORDERED_ACCESS, list->vk_queue_flags, &src_stage_mask,
                        &vk_memory_barrier.srcAccessMask);
                vk_access_and_stage_flags_from_d3d12_resource_state(list->device, resource,
                        D3D12_RESOURCE_STATE_UNORDERED_ACCESS, list->vk_queue_flags, &dst_stage_mask,
                        &vk_memory_barrier.dstAccessMask);

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
    }

    if (src_stage_mask && dst_stage_mask)
    {
        VK_CALL(vkCmdPipelineBarrier(list->vk_command_buffer,
                src_stage_mask, dst_stage_mask, 0,
                1, &vk_memory_barrier, 0, NULL, 0, NULL));
    }

    if (have_aliasing_barriers)
        FIXME_ONCE("Aliasing barriers not implemented yet.\n");

    /* Vulkan doesn't support split barriers. */
    if (have_split_barriers)
        WARN("Issuing split barrier(s) on D3D12_RESOURCE_BARRIER_FLAG_END_ONLY.\n");
}

static void STDMETHODCALLTYPE d3d12_command_list_ExecuteBundle(d3d12_command_list_iface *iface,
        ID3D12GraphicsCommandList *command_list)
{
    FIXME("iface %p, command_list %p stub!\n", iface, command_list);
}

static void STDMETHODCALLTYPE d3d12_command_list_SetDescriptorHeaps(d3d12_command_list_iface *iface,
        UINT heap_count, ID3D12DescriptorHeap *const *heaps)
{
    struct d3d12_command_list *list = impl_from_ID3D12GraphicsCommandList(iface);
    struct vkd3d_bindless_state *bindless_state = &list->device->bindless_state;
    bool dirty_uav_counters = false;
    unsigned int i, j, set_index;
    uint64_t dirty_mask = 0;

    TRACE("iface %p, heap_count %u, heaps %p.\n", iface, heap_count, heaps);

    for (i = 0; i < heap_count; i++)
    {
        struct d3d12_descriptor_heap *heap = unsafe_impl_from_ID3D12DescriptorHeap(heaps[i]);

        if (!heap)
            continue;

        for (j = 0; j < bindless_state->set_count; j++)
        {
            if (bindless_state->set_info[j].heap_type != heap->desc.Type)
                continue;

            set_index = d3d12_descriptor_heap_set_index_from_binding(&bindless_state->set_info[j]);
            list->descriptor_heaps[j] = heap->vk_descriptor_sets[set_index];
            dirty_mask |= 1ull << j;
        }

        if (heap->desc.Type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV)
        {
            list->uav_counter_address_buffer = heap->uav_counters.vk_buffer;
            dirty_uav_counters = true;
        }
    }

    for (i = 0; i < ARRAY_SIZE(list->pipeline_bindings); i++)
    {
        struct vkd3d_pipeline_bindings *bindings = &list->pipeline_bindings[i];
        bindings->descriptor_heap_dirty_mask = dirty_mask;

        if (dirty_uav_counters && bindings->root_signature &&
                (bindings->root_signature->flags & VKD3D_ROOT_SIGNATURE_USE_BINDLESS_UAV_COUNTERS))
            bindings->dirty_flags |= VKD3D_PIPELINE_DIRTY_UAV_COUNTER_BINDING;
    }
}

static void d3d12_command_list_set_root_signature(struct d3d12_command_list *list,
        VkPipelineBindPoint bind_point, const struct d3d12_root_signature *root_signature)
{
    struct vkd3d_pipeline_bindings *bindings = &list->pipeline_bindings[bind_point];

    if (bindings->root_signature == root_signature)
        return;

    bindings->root_signature = root_signature;
    bindings->static_sampler_set = VK_NULL_HANDLE;

    if (root_signature && root_signature->vk_sampler_descriptor_layout)
    {
        /* FIXME allocate static sampler sets globally */
        bindings->static_sampler_set = d3d12_command_allocator_allocate_descriptor_set(
                list->allocator, root_signature->vk_sampler_descriptor_layout,
                VKD3D_DESCRIPTOR_POOL_TYPE_IMMUTABLE_SAMPLER);
    }

    d3d12_command_list_invalidate_root_parameters(list, bind_point, false);
}

static void STDMETHODCALLTYPE d3d12_command_list_SetComputeRootSignature(d3d12_command_list_iface *iface,
        ID3D12RootSignature *root_signature)
{
    struct d3d12_command_list *list = impl_from_ID3D12GraphicsCommandList(iface);

    TRACE("iface %p, root_signature %p.\n", iface, root_signature);

    d3d12_command_list_set_root_signature(list, VK_PIPELINE_BIND_POINT_COMPUTE,
            unsafe_impl_from_ID3D12RootSignature(root_signature));
}

static void STDMETHODCALLTYPE d3d12_command_list_SetGraphicsRootSignature(d3d12_command_list_iface *iface,
        ID3D12RootSignature *root_signature)
{
    struct d3d12_command_list *list = impl_from_ID3D12GraphicsCommandList(iface);

    TRACE("iface %p, root_signature %p.\n", iface, root_signature);

    d3d12_command_list_set_root_signature(list, VK_PIPELINE_BIND_POINT_GRAPHICS,
            unsafe_impl_from_ID3D12RootSignature(root_signature));
}

static void d3d12_command_list_set_descriptor_table(struct d3d12_command_list *list,
        VkPipelineBindPoint bind_point, unsigned int index, D3D12_GPU_DESCRIPTOR_HANDLE base_descriptor)
{
    struct vkd3d_pipeline_bindings *bindings = &list->pipeline_bindings[bind_point];
    const struct d3d12_root_signature *root_signature = bindings->root_signature;
    const struct d3d12_root_descriptor_table *table;

    table = root_signature_get_descriptor_table(root_signature, index);

    assert(table && index < ARRAY_SIZE(bindings->descriptor_tables));
    bindings->descriptor_tables[index] = base_descriptor;
    bindings->descriptor_table_active_mask |= (uint64_t)1 << index;

    if (root_signature->descriptor_table_count)
        bindings->dirty_flags |= VKD3D_PIPELINE_DIRTY_DESCRIPTOR_TABLE_OFFSETS;

    if (table->flags & VKD3D_ROOT_DESCRIPTOR_TABLE_HAS_PACKED_DESCRIPTORS)
        bindings->dirty_flags |= VKD3D_PIPELINE_DIRTY_PACKED_DESCRIPTOR_SET;
}

static void STDMETHODCALLTYPE d3d12_command_list_SetComputeRootDescriptorTable(d3d12_command_list_iface *iface,
        UINT root_parameter_index, D3D12_GPU_DESCRIPTOR_HANDLE base_descriptor)
{
    struct d3d12_command_list *list = impl_from_ID3D12GraphicsCommandList(iface);

    TRACE("iface %p, root_parameter_index %u, base_descriptor %#"PRIx64".\n",
            iface, root_parameter_index, base_descriptor.ptr);

    d3d12_command_list_set_descriptor_table(list, VK_PIPELINE_BIND_POINT_COMPUTE,
            root_parameter_index, base_descriptor);
}

static void STDMETHODCALLTYPE d3d12_command_list_SetGraphicsRootDescriptorTable(d3d12_command_list_iface *iface,
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
    struct vkd3d_pipeline_bindings *bindings = &list->pipeline_bindings[bind_point];
    const struct d3d12_root_signature *root_signature = bindings->root_signature;
    const struct d3d12_root_constant *c;

    c = root_signature_get_32bit_constants(root_signature, index);
    memcpy(&bindings->root_constants[c->constant_index + offset], data, count * sizeof(uint32_t));

    bindings->root_constant_dirty_mask |= 1ull << index;
}

static void STDMETHODCALLTYPE d3d12_command_list_SetComputeRoot32BitConstant(d3d12_command_list_iface *iface,
        UINT root_parameter_index, UINT data, UINT dst_offset)
{
    struct d3d12_command_list *list = impl_from_ID3D12GraphicsCommandList(iface);

    TRACE("iface %p, root_parameter_index %u, data 0x%08x, dst_offset %u.\n",
            iface, root_parameter_index, data, dst_offset);

    d3d12_command_list_set_root_constants(list, VK_PIPELINE_BIND_POINT_COMPUTE,
            root_parameter_index, dst_offset, 1, &data);
}

static void STDMETHODCALLTYPE d3d12_command_list_SetGraphicsRoot32BitConstant(d3d12_command_list_iface *iface,
        UINT root_parameter_index, UINT data, UINT dst_offset)
{
    struct d3d12_command_list *list = impl_from_ID3D12GraphicsCommandList(iface);

    TRACE("iface %p, root_parameter_index %u, data 0x%08x, dst_offset %u.\n",
            iface, root_parameter_index, data, dst_offset);

    d3d12_command_list_set_root_constants(list, VK_PIPELINE_BIND_POINT_GRAPHICS,
            root_parameter_index, dst_offset, 1, &data);
}

static void STDMETHODCALLTYPE d3d12_command_list_SetComputeRoot32BitConstants(d3d12_command_list_iface *iface,
        UINT root_parameter_index, UINT constant_count, const void *data, UINT dst_offset)
{
    struct d3d12_command_list *list = impl_from_ID3D12GraphicsCommandList(iface);

    TRACE("iface %p, root_parameter_index %u, constant_count %u, data %p, dst_offset %u.\n",
            iface, root_parameter_index, constant_count, data, dst_offset);

    d3d12_command_list_set_root_constants(list, VK_PIPELINE_BIND_POINT_COMPUTE,
            root_parameter_index, dst_offset, constant_count, data);
}

static void STDMETHODCALLTYPE d3d12_command_list_SetGraphicsRoot32BitConstants(d3d12_command_list_iface *iface,
        UINT root_parameter_index, UINT constant_count, const void *data, UINT dst_offset)
{
    struct d3d12_command_list *list = impl_from_ID3D12GraphicsCommandList(iface);

    TRACE("iface %p, root_parameter_index %u, constant_count %u, data %p, dst_offset %u.\n",
            iface, root_parameter_index, constant_count, data, dst_offset);

    d3d12_command_list_set_root_constants(list, VK_PIPELINE_BIND_POINT_GRAPHICS,
            root_parameter_index, dst_offset, constant_count, data);
}

static void d3d12_command_list_set_root_descriptor(struct d3d12_command_list *list,
        VkPipelineBindPoint bind_point, unsigned int index, D3D12_GPU_VIRTUAL_ADDRESS gpu_address)
{
    struct vkd3d_pipeline_bindings *bindings = &list->pipeline_bindings[bind_point];
    const struct d3d12_root_signature *root_signature = bindings->root_signature;
    const struct vkd3d_vk_device_procs *vk_procs = &list->device->vk_procs;
    const struct vkd3d_vulkan_info *vk_info = &list->device->vk_info;
    const struct d3d12_root_parameter *root_parameter;
    union vkd3d_descriptor_info *descriptor;
    struct d3d12_resource *resource;
    VkBufferView vk_buffer_view;

    /* FIXME handle null descriptors */
    root_parameter = root_signature_get_root_descriptor(root_signature, index);
    descriptor = &bindings->root_descriptors[root_parameter->u.descriptor.packed_descriptor];

    if (root_parameter->parameter_type == D3D12_ROOT_PARAMETER_TYPE_CBV)
    {
        resource = vkd3d_gpu_va_allocator_dereference(&list->device->gpu_va_allocator, gpu_address);
        descriptor->buffer.buffer = resource->u.vk_buffer;
        descriptor->buffer.offset = gpu_address - resource->gpu_address;
        descriptor->buffer.range = min(resource->desc.Width - descriptor->buffer.offset,
                vk_info->device_limits.maxUniformBufferRange);
    }
    else
    {
        if (!vkd3d_create_raw_buffer_view(list->device, gpu_address, &vk_buffer_view))
        {
            ERR("Failed to create buffer view.\n");
            return;
        }

        if (!(d3d12_command_allocator_add_buffer_view(list->allocator, vk_buffer_view)))
        {
            ERR("Failed to add buffer view.\n");
            VK_CALL(vkDestroyBufferView(list->device->vk_device, vk_buffer_view, NULL));
            return;
        }
        
        descriptor->buffer_view = vk_buffer_view;
    }

    bindings->root_descriptor_dirty_mask |= 1ull << index;
    bindings->root_descriptor_active_mask |= 1ull << index;
}

static void STDMETHODCALLTYPE d3d12_command_list_SetComputeRootConstantBufferView(
        d3d12_command_list_iface *iface, UINT root_parameter_index, D3D12_GPU_VIRTUAL_ADDRESS address)
{
    struct d3d12_command_list *list = impl_from_ID3D12GraphicsCommandList(iface);

    TRACE("iface %p, root_parameter_index %u, address %#"PRIx64".\n",
            iface, root_parameter_index, address);

    d3d12_command_list_set_root_descriptor(list, VK_PIPELINE_BIND_POINT_COMPUTE,
            root_parameter_index, address);
}

static void STDMETHODCALLTYPE d3d12_command_list_SetGraphicsRootConstantBufferView(
        d3d12_command_list_iface *iface, UINT root_parameter_index, D3D12_GPU_VIRTUAL_ADDRESS address)
{
    struct d3d12_command_list *list = impl_from_ID3D12GraphicsCommandList(iface);

    TRACE("iface %p, root_parameter_index %u, address %#"PRIx64".\n",
            iface, root_parameter_index, address);

    d3d12_command_list_set_root_descriptor(list, VK_PIPELINE_BIND_POINT_GRAPHICS,
            root_parameter_index, address);
}

static void STDMETHODCALLTYPE d3d12_command_list_SetComputeRootShaderResourceView(
        d3d12_command_list_iface *iface, UINT root_parameter_index, D3D12_GPU_VIRTUAL_ADDRESS address)
{
    struct d3d12_command_list *list = impl_from_ID3D12GraphicsCommandList(iface);

    TRACE("iface %p, root_parameter_index %u, address %#"PRIx64".\n",
            iface, root_parameter_index, address);

    d3d12_command_list_set_root_descriptor(list, VK_PIPELINE_BIND_POINT_COMPUTE,
            root_parameter_index, address);
}

static void STDMETHODCALLTYPE d3d12_command_list_SetGraphicsRootShaderResourceView(
        d3d12_command_list_iface *iface, UINT root_parameter_index, D3D12_GPU_VIRTUAL_ADDRESS address)
{
    struct d3d12_command_list *list = impl_from_ID3D12GraphicsCommandList(iface);

    TRACE("iface %p, root_parameter_index %u, address %#"PRIx64".\n",
            iface, root_parameter_index, address);

    d3d12_command_list_set_root_descriptor(list, VK_PIPELINE_BIND_POINT_GRAPHICS,
            root_parameter_index, address);
}

static void STDMETHODCALLTYPE d3d12_command_list_SetComputeRootUnorderedAccessView(
        d3d12_command_list_iface *iface, UINT root_parameter_index, D3D12_GPU_VIRTUAL_ADDRESS address)
{
    struct d3d12_command_list *list = impl_from_ID3D12GraphicsCommandList(iface);

    TRACE("iface %p, root_parameter_index %u, address %#"PRIx64".\n",
            iface, root_parameter_index, address);

    d3d12_command_list_set_root_descriptor(list, VK_PIPELINE_BIND_POINT_COMPUTE,
            root_parameter_index, address);
}

static void STDMETHODCALLTYPE d3d12_command_list_SetGraphicsRootUnorderedAccessView(
        d3d12_command_list_iface *iface, UINT root_parameter_index, D3D12_GPU_VIRTUAL_ADDRESS address)
{
    struct d3d12_command_list *list = impl_from_ID3D12GraphicsCommandList(iface);

    TRACE("iface %p, root_parameter_index %u, address %#"PRIx64".\n",
            iface, root_parameter_index, address);

    d3d12_command_list_set_root_descriptor(list, VK_PIPELINE_BIND_POINT_GRAPHICS,
            root_parameter_index, address);
}

static void STDMETHODCALLTYPE d3d12_command_list_IASetIndexBuffer(d3d12_command_list_iface *iface,
        const D3D12_INDEX_BUFFER_VIEW *view)
{
    struct d3d12_command_list *list = impl_from_ID3D12GraphicsCommandList(iface);
    const struct vkd3d_vk_device_procs *vk_procs;
    struct d3d12_resource *resource;
    enum VkIndexType index_type;

    TRACE("iface %p, view %p.\n", iface, view);

    if (!view)
    {
        WARN("Ignoring NULL index buffer view.\n");
        return;
    }

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
            WARN("Invalid index format %#x.\n", view->Format);
            return;
    }

    list->index_buffer_format = view->Format;

    resource = vkd3d_gpu_va_allocator_dereference(&list->device->gpu_va_allocator, view->BufferLocation);
    VK_CALL(vkCmdBindIndexBuffer(list->vk_command_buffer, resource->u.vk_buffer,
            view->BufferLocation - resource->gpu_address, index_type));
}

static void STDMETHODCALLTYPE d3d12_command_list_IASetVertexBuffers(d3d12_command_list_iface *iface,
        UINT start_slot, UINT view_count, const D3D12_VERTEX_BUFFER_VIEW *views)
{
    struct d3d12_command_list *list = impl_from_ID3D12GraphicsCommandList(iface);
    struct vkd3d_dynamic_state *dyn_state = &list->dynamic_state;
    VkDeviceSize offsets[ARRAY_SIZE(dyn_state->vertex_strides)];
    VkBuffer buffers[ARRAY_SIZE(dyn_state->vertex_strides)];
    const struct vkd3d_null_resources *null_resources;
    struct vkd3d_gpu_va_allocator *gpu_va_allocator;
    const struct vkd3d_vk_device_procs *vk_procs;
    struct d3d12_resource *resource;
    bool invalidate = false;
    unsigned int i, stride;

    TRACE("iface %p, start_slot %u, view_count %u, views %p.\n", iface, start_slot, view_count, views);

    vk_procs = &list->device->vk_procs;
    null_resources = &list->device->null_resources;
    gpu_va_allocator = &list->device->gpu_va_allocator;

    if (start_slot >= ARRAY_SIZE(dyn_state->vertex_strides) ||
            view_count > ARRAY_SIZE(dyn_state->vertex_strides) - start_slot)
    {
        WARN("Invalid start slot %u / view count %u.\n", start_slot, view_count);
        return;
    }

    for (i = 0; i < view_count; ++i)
    {
        if (views[i].BufferLocation)
        {
            resource = vkd3d_gpu_va_allocator_dereference(gpu_va_allocator, views[i].BufferLocation);
            buffers[i] = resource->u.vk_buffer;
            offsets[i] = views[i].BufferLocation - resource->gpu_address;
            stride = views[i].StrideInBytes;
        }
        else
        {
            buffers[i] = null_resources->vk_buffer;
            offsets[i] = 0;
            stride = 0;
        }

        invalidate |= dyn_state->vertex_strides[start_slot + i] != stride;
        dyn_state->vertex_strides[start_slot + i] = stride;
    }

    if (view_count)
        VK_CALL(vkCmdBindVertexBuffers(list->vk_command_buffer, start_slot, view_count, buffers, offsets));

    if (invalidate)
        d3d12_command_list_invalidate_current_pipeline(list);
}

static void STDMETHODCALLTYPE d3d12_command_list_SOSetTargets(d3d12_command_list_iface *iface,
        UINT start_slot, UINT view_count, const D3D12_STREAM_OUTPUT_BUFFER_VIEW *views)
{
    struct d3d12_command_list *list = impl_from_ID3D12GraphicsCommandList(iface);
    VkDeviceSize offsets[ARRAY_SIZE(list->so_counter_buffers)];
    VkDeviceSize sizes[ARRAY_SIZE(list->so_counter_buffers)];
    VkBuffer buffers[ARRAY_SIZE(list->so_counter_buffers)];
    struct vkd3d_gpu_va_allocator *gpu_va_allocator;
    const struct vkd3d_vk_device_procs *vk_procs;
    struct d3d12_resource *resource;
    unsigned int i, first, count;

    TRACE("iface %p, start_slot %u, view_count %u, views %p.\n", iface, start_slot, view_count, views);

    d3d12_command_list_end_current_render_pass(list, true);

    if (!list->device->vk_info.EXT_transform_feedback)
    {
        FIXME("Transform feedback is not supported by Vulkan implementation.\n");
        return;
    }

    if (start_slot >= ARRAY_SIZE(buffers) || view_count > ARRAY_SIZE(buffers) - start_slot)
    {
        WARN("Invalid start slot %u / view count %u.\n", start_slot, view_count);
        return;
    }

    vk_procs = &list->device->vk_procs;
    gpu_va_allocator = &list->device->gpu_va_allocator;

    count = 0;
    first = start_slot;
    for (i = 0; i < view_count; ++i)
    {
        if (views[i].BufferLocation && views[i].SizeInBytes)
        {
            resource = vkd3d_gpu_va_allocator_dereference(gpu_va_allocator, views[i].BufferLocation);
            buffers[count] = resource->u.vk_buffer;
            offsets[count] = views[i].BufferLocation - resource->gpu_address;
            sizes[count] = views[i].SizeInBytes;

            resource = vkd3d_gpu_va_allocator_dereference(gpu_va_allocator, views[i].BufferFilledSizeLocation);
            list->so_counter_buffers[start_slot + i] = resource->u.vk_buffer;
            list->so_counter_buffer_offsets[start_slot + i] = views[i].BufferFilledSizeLocation - resource->gpu_address;
            ++count;
        }
        else
        {
            if (count)
                VK_CALL(vkCmdBindTransformFeedbackBuffersEXT(list->vk_command_buffer, first, count, buffers, offsets, sizes));
            count = 0;
            first = start_slot + i + 1;

            list->so_counter_buffers[start_slot + i] = VK_NULL_HANDLE;
            list->so_counter_buffer_offsets[start_slot + i] = 0;

            WARN("Trying to unbind transform feedback buffer %u. Ignoring.\n", start_slot + i);
        }
    }

    if (count)
        VK_CALL(vkCmdBindTransformFeedbackBuffersEXT(list->vk_command_buffer, first, count, buffers, offsets, sizes));
}

static void STDMETHODCALLTYPE d3d12_command_list_OMSetRenderTargets(d3d12_command_list_iface *iface,
        UINT render_target_descriptor_count, const D3D12_CPU_DESCRIPTOR_HANDLE *render_target_descriptors,
        BOOL single_descriptor_handle, const D3D12_CPU_DESCRIPTOR_HANDLE *depth_stencil_descriptor)
{
    struct d3d12_command_list *list = impl_from_ID3D12GraphicsCommandList(iface);
    const VkPhysicalDeviceLimits *limits = &list->device->vk_info.device_limits;
    VkFormat prev_dsv_format, next_dsv_format;
    const struct d3d12_rtv_desc *rtv_desc;
    const struct d3d12_dsv_desc *dsv_desc;
    unsigned int i;

    TRACE("iface %p, render_target_descriptor_count %u, render_target_descriptors %p, "
            "single_descriptor_handle %#x, depth_stencil_descriptor %p.\n",
            iface, render_target_descriptor_count, render_target_descriptors,
            single_descriptor_handle, depth_stencil_descriptor);

    d3d12_command_list_invalidate_current_framebuffer(list);
    d3d12_command_list_invalidate_current_render_pass(list);

    if (render_target_descriptor_count > ARRAY_SIZE(list->rtvs))
    {
        WARN("Descriptor count %u > %zu, ignoring extra descriptors.\n",
                render_target_descriptor_count, ARRAY_SIZE(list->rtvs));
        render_target_descriptor_count = ARRAY_SIZE(list->rtvs);
    }

    list->fb_width = limits->maxFramebufferWidth;
    list->fb_height = limits->maxFramebufferHeight;
    list->fb_layer_count = limits->maxFramebufferLayers;

    prev_dsv_format = list->dsv.format ? list->dsv.format->vk_format : VK_FORMAT_UNDEFINED;
    next_dsv_format = VK_FORMAT_UNDEFINED;

    memset(list->rtvs, 0, sizeof(list->rtvs));
    memset(&list->dsv, 0, sizeof(list->dsv));

    for (i = 0; i < render_target_descriptor_count; ++i)
    {
        if (single_descriptor_handle)
        {
            if ((rtv_desc = d3d12_rtv_desc_from_cpu_handle(*render_target_descriptors)))
                rtv_desc += i;
        }
        else
        {
            rtv_desc = d3d12_rtv_desc_from_cpu_handle(render_target_descriptors[i]);
        }

        if (!rtv_desc || !rtv_desc->resource)
        {
            WARN("RTV descriptor %u is not initialized.\n", i);
            continue;
        }

        d3d12_command_list_track_resource_usage(list, rtv_desc->resource);

        /* In D3D12 CPU descriptors are consumed when a command is recorded. */
        if (!d3d12_command_allocator_add_view(list->allocator, rtv_desc->view))
            WARN("Failed to add view.\n");

        list->rtvs[i] = *rtv_desc;
        list->fb_width = min(list->fb_width, rtv_desc->width);
        list->fb_height = min(list->fb_height, rtv_desc->height);
        list->fb_layer_count = min(list->fb_layer_count, rtv_desc->layer_count);
    }

    if (depth_stencil_descriptor)
    {
        if ((dsv_desc = d3d12_dsv_desc_from_cpu_handle(*depth_stencil_descriptor))
                && dsv_desc->resource)
        {
            d3d12_command_list_track_resource_usage(list, dsv_desc->resource);

            /* In D3D12 CPU descriptors are consumed when a command is recorded. */
            if (!d3d12_command_allocator_add_view(list->allocator, dsv_desc->view))
                WARN("Failed to add view.\n");

            list->dsv = *dsv_desc;
            list->fb_width = min(list->fb_width, dsv_desc->width);
            list->fb_height = min(list->fb_height, dsv_desc->height);
            list->fb_layer_count = min(list->fb_layer_count, dsv_desc->layer_count);
            next_dsv_format = dsv_desc->format->vk_format;
        }
        else
        {
            WARN("DSV descriptor is not initialized.\n");
        }
    }

    if (prev_dsv_format != next_dsv_format && d3d12_pipeline_state_has_unknown_dsv_format(list->state))
        d3d12_command_list_invalidate_current_pipeline(list);
}

static void d3d12_command_list_clear_attachment(struct d3d12_command_list *list, struct d3d12_resource *resource,
        struct vkd3d_view *view, VkImageAspectFlags clear_aspects, const VkClearValue *clear_value, UINT rect_count,
        const D3D12_RECT *rects)
{
    bool full_clear, writable = true;
    D3D12_RECT full_rect;
    int attachment_idx;
    unsigned int i;

    /* If one of the clear rectangles covers the entire image, we
     * may be able to use a fast path and re-initialize the image */
    full_rect = d3d12_get_view_rect(resource, view);
    full_clear = !rect_count;

    for (i = 0; i < rect_count && !full_clear; i++)
        full_clear |= !memcmp(&rects[i], &full_rect, sizeof(full_rect));

    if (full_clear)
        rect_count = 0;

    attachment_idx = d3d12_command_list_find_attachment(list, resource, view);

    if (attachment_idx == D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT && list->current_render_pass)
        writable = (vk_writable_aspects_from_image_layout(list->dsv_layout) & clear_aspects) == clear_aspects;

    if (attachment_idx < 0 || (!list->current_render_pass && !full_clear) || !writable)
    {
        /* View currently not bound as a render target, or bound but
         * the render pass isn't active and we're only going to clear
         * a sub-region of the image, or one of the aspects to clear
         * uses a read-only layout in the current render pass */
        d3d12_command_list_end_current_render_pass(list, false);
        d3d12_command_list_clear_attachment_pass(list, resource, view,
                clear_aspects, clear_value, rect_count, rects);
    }
    else if (list->current_render_pass)
    {
        /* View bound and render pass active, just emit the clear */
        d3d12_command_list_clear_attachment_inline(list, resource, view,
                attachment_idx, clear_aspects, clear_value,
                rect_count, rects);
    }
    else
    {
        /* View bound but render pass not active, and we'll clear
         * the entire image. Defer the clear until we begin the
         * render pass to avoid unnecessary barriers. */
        d3d12_command_list_clear_attachment_deferred(list, attachment_idx,
                clear_aspects, clear_value);
    }
}

static void STDMETHODCALLTYPE d3d12_command_list_ClearDepthStencilView(d3d12_command_list_iface *iface,
        D3D12_CPU_DESCRIPTOR_HANDLE dsv, D3D12_CLEAR_FLAGS flags, float depth, UINT8 stencil,
        UINT rect_count, const D3D12_RECT *rects)
{
    const union VkClearValue clear_value = {.depthStencil = {depth, stencil}};
    struct d3d12_command_list *list = impl_from_ID3D12GraphicsCommandList(iface);
    const struct d3d12_dsv_desc *dsv_desc = d3d12_dsv_desc_from_cpu_handle(dsv);
    VkImageAspectFlags clear_aspects = 0;

    TRACE("iface %p, dsv %#lx, flags %#x, depth %.8e, stencil 0x%02x, rect_count %u, rects %p.\n",
            iface, dsv.ptr, flags, depth, stencil, rect_count, rects);

    d3d12_command_list_track_resource_usage(list, dsv_desc->resource);

    if (flags & D3D12_CLEAR_FLAG_DEPTH)
        clear_aspects |= VK_IMAGE_ASPECT_DEPTH_BIT;

    if (flags & D3D12_CLEAR_FLAG_STENCIL)
        clear_aspects |= VK_IMAGE_ASPECT_STENCIL_BIT;

    clear_aspects &= dsv_desc->format->vk_aspect_mask;

    if (!clear_aspects)
    {
        WARN("Not clearing any aspects.\n");
        return;
    }

    d3d12_command_list_clear_attachment(list, dsv_desc->resource, dsv_desc->view,
            clear_aspects, &clear_value, rect_count, rects);
}

static void STDMETHODCALLTYPE d3d12_command_list_ClearRenderTargetView(d3d12_command_list_iface *iface,
        D3D12_CPU_DESCRIPTOR_HANDLE rtv, const FLOAT color[4], UINT rect_count, const D3D12_RECT *rects)
{
    struct d3d12_command_list *list = impl_from_ID3D12GraphicsCommandList(iface);
    const struct d3d12_rtv_desc *rtv_desc = d3d12_rtv_desc_from_cpu_handle(rtv);
    VkClearValue clear_value;

    TRACE("iface %p, rtv %#lx, color %p, rect_count %u, rects %p.\n",
            iface, rtv.ptr, color, rect_count, rects);

    d3d12_command_list_track_resource_usage(list, rtv_desc->resource);

    if (rtv_desc->format->type == VKD3D_FORMAT_TYPE_UINT)
    {
        clear_value.color.uint32[0] = max(0, color[0]);
        clear_value.color.uint32[1] = max(0, color[1]);
        clear_value.color.uint32[2] = max(0, color[2]);
        clear_value.color.uint32[3] = max(0, color[3]);
    }
    else if (rtv_desc->format->type == VKD3D_FORMAT_TYPE_SINT)
    {
        clear_value.color.int32[0] = color[0];
        clear_value.color.int32[1] = color[1];
        clear_value.color.int32[2] = color[2];
        clear_value.color.int32[3] = color[3];
    }
    else
    {
        clear_value.color.float32[0] = color[0];
        clear_value.color.float32[1] = color[1];
        clear_value.color.float32[2] = color[2];
        clear_value.color.float32[3] = color[3];
    }

    d3d12_command_list_clear_attachment(list, rtv_desc->resource, rtv_desc->view,
            VK_IMAGE_ASPECT_COLOR_BIT, &clear_value, rect_count, rects);
}

static void d3d12_command_list_clear_uav(struct d3d12_command_list *list,
        struct d3d12_resource *resource, struct vkd3d_view *view, const VkClearColorValue *clear_color,
        UINT rect_count, const D3D12_RECT *rects)
{
    const struct vkd3d_vk_device_procs *vk_procs = &list->device->vk_procs;
    unsigned int i, miplevel_idx, layer_count;
    struct vkd3d_clear_uav_pipeline pipeline;
    struct vkd3d_clear_uav_args clear_args;
    VkDescriptorImageInfo image_info;
    D3D12_RECT full_rect, curr_rect;
    VkWriteDescriptorSet write_set;
    VkExtent3D workgroup_size;

    d3d12_command_list_track_resource_usage(list, resource);
    d3d12_command_list_end_current_render_pass(list, false);

    d3d12_command_list_invalidate_current_pipeline(list);
    d3d12_command_list_invalidate_root_parameters(list, VK_PIPELINE_BIND_POINT_COMPUTE, true);

    if (!d3d12_command_allocator_add_view(list->allocator, view))
        WARN("Failed to add view.\n");

    clear_args.clear_color = *clear_color;

    write_set.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write_set.pNext = NULL;
    write_set.dstBinding = 0;
    write_set.dstArrayElement = 0;
    write_set.descriptorCount = 1;

    if (d3d12_resource_is_texture(resource))
    {
        image_info.sampler = VK_NULL_HANDLE;
        image_info.imageView = view->u.vk_image_view;
        image_info.imageLayout = view->info.texture.vk_layout;

        write_set.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        write_set.pImageInfo = &image_info;
        write_set.pBufferInfo = NULL;
        write_set.pTexelBufferView = NULL;

        miplevel_idx = view->info.texture.miplevel_idx;
        layer_count = view->info.texture.vk_view_type == VK_IMAGE_VIEW_TYPE_3D
                ? d3d12_resource_desc_get_depth(&resource->desc, miplevel_idx)
                : view->info.texture.layer_count;
        pipeline = vkd3d_meta_get_clear_image_uav_pipeline(
                &list->device->meta_ops, view->info.texture.vk_view_type,
                view->format->type == VKD3D_FORMAT_TYPE_UINT);
        workgroup_size = vkd3d_meta_get_clear_image_uav_workgroup_size(view->info.texture.vk_view_type);
    }
    else
    {
        write_set.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER;
        write_set.pImageInfo = NULL;
        write_set.pBufferInfo = NULL;
        write_set.pTexelBufferView = &view->u.vk_buffer_view;

        miplevel_idx = 0;
        layer_count = 1;
        pipeline = vkd3d_meta_get_clear_buffer_uav_pipeline(
                &list->device->meta_ops, view->format->type == VKD3D_FORMAT_TYPE_UINT);
        workgroup_size = vkd3d_meta_get_clear_buffer_uav_workgroup_size();
    }

    if (!(write_set.dstSet = d3d12_command_allocator_allocate_descriptor_set(
            list->allocator, pipeline.vk_set_layout, VKD3D_DESCRIPTOR_POOL_TYPE_STATIC)))
    {
        ERR("Failed to allocate descriptor set.\n");
        return;
    }

    VK_CALL(vkUpdateDescriptorSets(list->device->vk_device, 1, &write_set, 0, NULL));

    full_rect.left = 0;
    full_rect.right = d3d12_resource_desc_get_width(&resource->desc, miplevel_idx);
    full_rect.top = 0;
    full_rect.bottom = d3d12_resource_desc_get_height(&resource->desc, miplevel_idx);

    /* clear full resource if no rects are specified */
    curr_rect = full_rect;

    VK_CALL(vkCmdBindPipeline(list->vk_command_buffer,
            VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.vk_pipeline));

    VK_CALL(vkCmdBindDescriptorSets(list->vk_command_buffer,
            VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.vk_pipeline_layout,
            0, 1, &write_set.dstSet, 0, NULL));

    for (i = 0; i < rect_count || !i; i++)
    {
        if (rect_count)
        {
            /* clamp to actual resource region and skip empty rects */
            curr_rect.left = max(rects[i].left, full_rect.left);
            curr_rect.top = max(rects[i].top, full_rect.top);
            curr_rect.right = min(rects[i].right, full_rect.right);
            curr_rect.bottom = min(rects[i].bottom, full_rect.bottom);

            if (curr_rect.left >= curr_rect.right || curr_rect.top >= curr_rect.bottom)
                continue;
        }

        clear_args.offset.x = curr_rect.left;
        clear_args.offset.y = curr_rect.top;
        clear_args.extent.width = curr_rect.right - curr_rect.left;
        clear_args.extent.height = curr_rect.bottom - curr_rect.top;

        VK_CALL(vkCmdPushConstants(list->vk_command_buffer,
                pipeline.vk_pipeline_layout, VK_SHADER_STAGE_COMPUTE_BIT,
                0, sizeof(clear_args), &clear_args));

        VK_CALL(vkCmdDispatch(list->vk_command_buffer,
                vkd3d_compute_workgroup_count(clear_args.extent.width, workgroup_size.width),
                vkd3d_compute_workgroup_count(clear_args.extent.height, workgroup_size.height),
                vkd3d_compute_workgroup_count(layer_count, workgroup_size.depth)));
    }
}

static const struct vkd3d_format *vkd3d_fixup_clear_uav_uint_color(struct d3d12_device *device,
        DXGI_FORMAT dxgi_format, VkClearColorValue *color)
{
    switch (dxgi_format)
    {
        case DXGI_FORMAT_R11G11B10_FLOAT:
            color->uint32[0] = (color->uint32[0] & 0x7FF)
                    | ((color->uint32[1] & 0x7FF) << 11)
                    | ((color->uint32[2] & 0x3FF) << 22);
            return vkd3d_get_format(device, DXGI_FORMAT_R32_UINT, false);

        default:
            return NULL;
    }
}

static void STDMETHODCALLTYPE d3d12_command_list_ClearUnorderedAccessViewUint(d3d12_command_list_iface *iface,
        D3D12_GPU_DESCRIPTOR_HANDLE gpu_handle, D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle, ID3D12Resource *resource,
        const UINT values[4], UINT rect_count, const D3D12_RECT *rects)
{
    struct d3d12_command_list *list = impl_from_ID3D12GraphicsCommandList(iface);
    struct vkd3d_view *base_view, *uint_view;
    struct vkd3d_texture_view_desc view_desc;
    const struct vkd3d_format *uint_format;
    struct d3d12_resource *resource_impl;
    VkClearColorValue color;

    TRACE("iface %p, gpu_handle %#"PRIx64", cpu_handle %lx, resource %p, values %p, rect_count %u, rects %p.\n",
            iface, gpu_handle.ptr, cpu_handle.ptr, resource, values, rect_count, rects);

    memcpy(color.uint32, values, sizeof(color.uint32));

    resource_impl = unsafe_impl_from_ID3D12Resource(resource);

    base_view = d3d12_desc_from_cpu_handle(cpu_handle)->u.view;
    uint_view = NULL;

    if (base_view->format->type != VKD3D_FORMAT_TYPE_UINT)
    {
        uint_format = vkd3d_find_uint_format(list->device, base_view->format->dxgi_format);

        if (!uint_format && !(uint_format = vkd3d_fixup_clear_uav_uint_color(
                list->device, base_view->format->dxgi_format, &color)))
        {
            ERR("Unhandled format %d.\n", base_view->format->dxgi_format);
            return;
        }

        if (d3d12_resource_is_texture(resource_impl))
        {
            memset(&view_desc, 0, sizeof(view_desc));
            view_desc.view_type = base_view->info.texture.vk_view_type;
            view_desc.layout = base_view->info.texture.vk_layout;
            view_desc.format = uint_format;
            view_desc.miplevel_idx = base_view->info.texture.miplevel_idx;
            view_desc.miplevel_count = 1;
            view_desc.layer_idx = base_view->info.texture.layer_idx;
            view_desc.layer_count = base_view->info.texture.layer_count;
            view_desc.allowed_swizzle = false;

            if (!vkd3d_create_texture_view(list->device, resource_impl->u.vk_image, &view_desc, &uint_view))
            {
                ERR("Failed to create image view.\n");
                return;
            }
        }
        else
        {
            if (!vkd3d_create_buffer_view(list->device, resource_impl->u.vk_buffer, uint_format,
                    base_view->info.buffer.offset, base_view->info.buffer.size, &uint_view))
            {
                ERR("Failed to create buffer view.\n");
                return;
            }
        }
    }

    d3d12_command_list_clear_uav(list, resource_impl,
      uint_view ? uint_view : base_view, &color, rect_count, rects);

    if (uint_view)
        vkd3d_view_decref(uint_view, list->device);
}

static void STDMETHODCALLTYPE d3d12_command_list_ClearUnorderedAccessViewFloat(d3d12_command_list_iface *iface,
        D3D12_GPU_DESCRIPTOR_HANDLE gpu_handle, D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle, ID3D12Resource *resource,
        const float values[4], UINT rect_count, const D3D12_RECT *rects)
{
    struct d3d12_command_list *list = impl_from_ID3D12GraphicsCommandList(iface);
    struct d3d12_resource *resource_impl;
    struct vkd3d_view *view;
    VkClearColorValue color;

    TRACE("iface %p, gpu_handle %#"PRIx64", cpu_handle %lx, resource %p, values %p, rect_count %u, rects %p.\n",
            iface, gpu_handle.ptr, cpu_handle.ptr, resource, values, rect_count, rects);

    memcpy(color.float32, values, sizeof(color.float32));

    resource_impl = unsafe_impl_from_ID3D12Resource(resource);
    view = d3d12_desc_from_cpu_handle(cpu_handle)->u.view;

    d3d12_command_list_clear_uav(list, resource_impl, view, &color, rect_count, rects);
}

static void STDMETHODCALLTYPE d3d12_command_list_DiscardResource(d3d12_command_list_iface *iface,
        ID3D12Resource *resource, const D3D12_DISCARD_REGION *region)
{
    FIXME_ONCE("iface %p, resource %p, region %p stub!\n", iface, resource, region);
}

static void STDMETHODCALLTYPE d3d12_command_list_BeginQuery(d3d12_command_list_iface *iface,
        ID3D12QueryHeap *heap, D3D12_QUERY_TYPE type, UINT index)
{
    struct d3d12_command_list *list = impl_from_ID3D12GraphicsCommandList(iface);
    struct d3d12_query_heap *query_heap = unsafe_impl_from_ID3D12QueryHeap(heap);
    const struct vkd3d_vk_device_procs *vk_procs;
    VkQueryControlFlags flags = 0;

    TRACE("iface %p, heap %p, type %#x, index %u.\n", iface, heap, type, index);

    vk_procs = &list->device->vk_procs;

    d3d12_command_list_end_current_render_pass(list, true);

    VK_CALL(vkCmdResetQueryPool(list->vk_command_buffer, query_heap->vk_query_pool, index, 1));

    if (type == D3D12_QUERY_TYPE_OCCLUSION)
        flags = VK_QUERY_CONTROL_PRECISE_BIT;

    if (D3D12_QUERY_TYPE_SO_STATISTICS_STREAM0 <= type && type <= D3D12_QUERY_TYPE_SO_STATISTICS_STREAM3)
    {
        unsigned int stream_index = type - D3D12_QUERY_TYPE_SO_STATISTICS_STREAM0;
        VK_CALL(vkCmdBeginQueryIndexedEXT(list->vk_command_buffer,
                query_heap->vk_query_pool, index, flags, stream_index));
        return;
    }

    VK_CALL(vkCmdBeginQuery(list->vk_command_buffer, query_heap->vk_query_pool, index, flags));
}

static void STDMETHODCALLTYPE d3d12_command_list_EndQuery(d3d12_command_list_iface *iface,
        ID3D12QueryHeap *heap, D3D12_QUERY_TYPE type, UINT index)
{
    struct d3d12_command_list *list = impl_from_ID3D12GraphicsCommandList(iface);
    struct d3d12_query_heap *query_heap = unsafe_impl_from_ID3D12QueryHeap(heap);
    const struct vkd3d_vk_device_procs *vk_procs;

    TRACE("iface %p, heap %p, type %#x, index %u.\n", iface, heap, type, index);

    vk_procs = &list->device->vk_procs;

    d3d12_command_list_end_current_render_pass(list, true);

    d3d12_query_heap_mark_result_as_available(query_heap, index);

    if (type == D3D12_QUERY_TYPE_TIMESTAMP)
    {
        VK_CALL(vkCmdResetQueryPool(list->vk_command_buffer, query_heap->vk_query_pool, index, 1));
        VK_CALL(vkCmdWriteTimestamp(list->vk_command_buffer,
                VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, query_heap->vk_query_pool, index));
        return;
    }

    if (D3D12_QUERY_TYPE_SO_STATISTICS_STREAM0 <= type && type <= D3D12_QUERY_TYPE_SO_STATISTICS_STREAM3)
    {
        unsigned int stream_index = type - D3D12_QUERY_TYPE_SO_STATISTICS_STREAM0;
        VK_CALL(vkCmdEndQueryIndexedEXT(list->vk_command_buffer,
                query_heap->vk_query_pool, index, stream_index));
        return;
    }

    VK_CALL(vkCmdEndQuery(list->vk_command_buffer, query_heap->vk_query_pool, index));
}

static size_t get_query_stride(D3D12_QUERY_TYPE type)
{
    if (type == D3D12_QUERY_TYPE_PIPELINE_STATISTICS)
        return sizeof(D3D12_QUERY_DATA_PIPELINE_STATISTICS);

    if (D3D12_QUERY_TYPE_SO_STATISTICS_STREAM0 <= type && type <= D3D12_QUERY_TYPE_SO_STATISTICS_STREAM3)
        return sizeof(D3D12_QUERY_DATA_SO_STATISTICS);

    return sizeof(uint64_t);
}

static void STDMETHODCALLTYPE d3d12_command_list_ResolveQueryData(d3d12_command_list_iface *iface,
        ID3D12QueryHeap *heap, D3D12_QUERY_TYPE type, UINT start_index, UINT query_count,
        ID3D12Resource *dst_buffer, UINT64 aligned_dst_buffer_offset)
{
    const struct d3d12_query_heap *query_heap = unsafe_impl_from_ID3D12QueryHeap(heap);
    struct d3d12_command_list *list = impl_from_ID3D12GraphicsCommandList(iface);
    struct d3d12_resource *buffer = unsafe_impl_from_ID3D12Resource(dst_buffer);
    const struct vkd3d_vk_device_procs *vk_procs;
    unsigned int i, first, count;
    VkDeviceSize offset, stride;

    TRACE("iface %p, heap %p, type %#x, start_index %u, query_count %u, "
            "dst_buffer %p, aligned_dst_buffer_offset %#"PRIx64".\n",
            iface, heap, type, start_index, query_count,
            dst_buffer, aligned_dst_buffer_offset);

    vk_procs = &list->device->vk_procs;

    /* Vulkan is less strict than D3D12 here. Vulkan implementations are free
     * to return any non-zero result for binary occlusion with at least one
     * sample passing, while D3D12 guarantees that the result is 1 then.
     *
     * For example, the Nvidia binary blob drivers on Linux seem to always
     * count precisely, even when it was signalled that non-precise is enough.
     */
    if (type == D3D12_QUERY_TYPE_BINARY_OCCLUSION)
        FIXME_ONCE("D3D12 guarantees binary occlusion queries result in only 0 and 1.\n");

    if (!d3d12_resource_is_buffer(buffer))
    {
        WARN("Destination resource is not a buffer.\n");
        return;
    }

    d3d12_command_list_end_current_render_pass(list, true);

    stride = get_query_stride(type);

    count = 0;
    first = start_index;
    offset = aligned_dst_buffer_offset;
    for (i = 0; i < query_count; ++i)
    {
        if (d3d12_query_heap_is_result_available(query_heap, start_index + i))
        {
            ++count;
        }
        else
        {
            if (count)
            {
                VK_CALL(vkCmdCopyQueryPoolResults(list->vk_command_buffer,
                        query_heap->vk_query_pool, first, count, buffer->u.vk_buffer,
                        buffer->heap_offset + offset, stride, VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT));
            }
            count = 0;
            first = start_index + i;
            offset = aligned_dst_buffer_offset + i * stride;

            /* We cannot copy query results if a query was not issued:
             *
             *   "If the query does not become available in a finite amount of
             *   time (e.g. due to not issuing a query since the last reset),
             *   a VK_ERROR_DEVICE_LOST error may occur."
             */
            VK_CALL(vkCmdFillBuffer(list->vk_command_buffer,
                    buffer->u.vk_buffer, buffer->heap_offset + offset, stride, 0x00000000));

            ++first;
            offset += stride;
        }
    }

    if (count)
    {
        VK_CALL(vkCmdCopyQueryPoolResults(list->vk_command_buffer,
                query_heap->vk_query_pool, first, count, buffer->u.vk_buffer,
                buffer->heap_offset + offset, stride, VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT));
    }
}

static void STDMETHODCALLTYPE d3d12_command_list_SetPredication(d3d12_command_list_iface *iface,
        ID3D12Resource *buffer, UINT64 aligned_buffer_offset, D3D12_PREDICATION_OP operation)
{
    struct d3d12_command_list *list = impl_from_ID3D12GraphicsCommandList(iface);
    struct d3d12_resource *resource = unsafe_impl_from_ID3D12Resource(buffer);
    const struct vkd3d_vulkan_info *vk_info = &list->device->vk_info;
    const struct vkd3d_vk_device_procs *vk_procs;

    TRACE("iface %p, buffer %p, aligned_buffer_offset %#"PRIx64", operation %#x.\n",
            iface, buffer, aligned_buffer_offset, operation);

    if (!vk_info->EXT_conditional_rendering)
    {
        FIXME("Vulkan conditional rendering extension not present. Conditional rendering not supported.\n");
        return;
    }

    vk_procs = &list->device->vk_procs;

    d3d12_command_list_end_current_render_pass(list, true);

    if (resource)
    {
        VkConditionalRenderingBeginInfoEXT cond_info;

        if (aligned_buffer_offset & (sizeof(uint64_t) - 1))
        {
            WARN("Unaligned predicate argument buffer offset %#"PRIx64".\n", aligned_buffer_offset);
            return;
        }

        if (!d3d12_resource_is_buffer(resource))
        {
            WARN("Predicate arguments must be stored in a buffer resource.\n");
            return;
        }

        FIXME_ONCE("Predication doesn't support clear and copy commands, "
                "and predication values are treated as 32-bit values.\n");

        cond_info.sType = VK_STRUCTURE_TYPE_CONDITIONAL_RENDERING_BEGIN_INFO_EXT;
        cond_info.pNext = NULL;
        cond_info.buffer = resource->u.vk_buffer;
        cond_info.offset = aligned_buffer_offset;
        switch (operation)
        {
            case D3D12_PREDICATION_OP_EQUAL_ZERO:
                cond_info.flags = 0;
                break;

            case D3D12_PREDICATION_OP_NOT_EQUAL_ZERO:
                cond_info.flags = VK_CONDITIONAL_RENDERING_INVERTED_BIT_EXT;
                break;

            default:
                FIXME("Unhandled predication operation %#x.\n", operation);
                return;
        }

        if (list->is_predicated)
            VK_CALL(vkCmdEndConditionalRenderingEXT(list->vk_command_buffer));
        VK_CALL(vkCmdBeginConditionalRenderingEXT(list->vk_command_buffer, &cond_info));
        list->is_predicated = true;
    }
    else if (list->is_predicated)
    {
        VK_CALL(vkCmdEndConditionalRenderingEXT(list->vk_command_buffer));
        list->is_predicated = false;
    }
}

static void STDMETHODCALLTYPE d3d12_command_list_SetMarker(d3d12_command_list_iface *iface,
        UINT metadata, const void *data, UINT size)
{
    FIXME("iface %p, metadata %#x, data %p, size %u stub!\n", iface, metadata, data, size);
}

static void STDMETHODCALLTYPE d3d12_command_list_BeginEvent(d3d12_command_list_iface *iface,
        UINT metadata, const void *data, UINT size)
{
    FIXME("iface %p, metadata %#x, data %p, size %u stub!\n", iface, metadata, data, size);
}

static void STDMETHODCALLTYPE d3d12_command_list_EndEvent(d3d12_command_list_iface *iface)
{
    FIXME("iface %p stub!\n", iface);
}

STATIC_ASSERT(sizeof(VkDispatchIndirectCommand) == sizeof(D3D12_DISPATCH_ARGUMENTS));
STATIC_ASSERT(sizeof(VkDrawIndexedIndirectCommand) == sizeof(D3D12_DRAW_INDEXED_ARGUMENTS));
STATIC_ASSERT(sizeof(VkDrawIndirectCommand) == sizeof(D3D12_DRAW_ARGUMENTS));

static void STDMETHODCALLTYPE d3d12_command_list_ExecuteIndirect(d3d12_command_list_iface *iface,
        ID3D12CommandSignature *command_signature, UINT max_command_count, ID3D12Resource *arg_buffer,
        UINT64 arg_buffer_offset, ID3D12Resource *count_buffer, UINT64 count_buffer_offset)
{
    struct d3d12_command_signature *sig_impl = unsafe_impl_from_ID3D12CommandSignature(command_signature);
    struct d3d12_resource *count_impl = unsafe_impl_from_ID3D12Resource(count_buffer);
    struct d3d12_resource *arg_impl = unsafe_impl_from_ID3D12Resource(arg_buffer);
    struct d3d12_command_list *list = impl_from_ID3D12GraphicsCommandList(iface);
    const D3D12_COMMAND_SIGNATURE_DESC *signature_desc;
    const struct vkd3d_vk_device_procs *vk_procs;
    unsigned int i;

    TRACE("iface %p, command_signature %p, max_command_count %u, arg_buffer %p, "
            "arg_buffer_offset %#"PRIx64", count_buffer %p, count_buffer_offset %#"PRIx64".\n",
            iface, command_signature, max_command_count, arg_buffer, arg_buffer_offset,
            count_buffer, count_buffer_offset);

    vk_procs = &list->device->vk_procs;

    if (count_buffer && !list->device->vk_info.KHR_draw_indirect_count)
    {
        FIXME("Count buffers not supported by Vulkan implementation.\n");
        return;
    }

    signature_desc = &sig_impl->desc;
    for (i = 0; i < signature_desc->NumArgumentDescs; ++i)
    {
        const D3D12_INDIRECT_ARGUMENT_DESC *arg_desc = &signature_desc->pArgumentDescs[i];

        switch (arg_desc->Type)
        {
            case D3D12_INDIRECT_ARGUMENT_TYPE_DRAW:
                if (!d3d12_command_list_begin_render_pass(list))
                {
                    WARN("Failed to begin render pass, ignoring draw.\n");
                    break;
                }

                if (count_buffer)
                {
                    VK_CALL(vkCmdDrawIndirectCountKHR(list->vk_command_buffer, arg_impl->u.vk_buffer,
                            arg_buffer_offset, count_impl->u.vk_buffer, count_buffer_offset,
                            max_command_count, signature_desc->ByteStride));
                }
                else
                {
                    VK_CALL(vkCmdDrawIndirect(list->vk_command_buffer, arg_impl->u.vk_buffer,
                            arg_buffer_offset, max_command_count, signature_desc->ByteStride));
                }
                break;

            case D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED:
                if (!d3d12_command_list_begin_render_pass(list))
                {
                    WARN("Failed to begin render pass, ignoring draw.\n");
                    break;
                }

                d3d12_command_list_check_index_buffer_strip_cut_value(list);

                if (count_buffer)
                {
                    VK_CALL(vkCmdDrawIndexedIndirectCountKHR(list->vk_command_buffer, arg_impl->u.vk_buffer,
                            arg_buffer_offset, count_impl->u.vk_buffer, count_buffer_offset,
                            max_command_count, signature_desc->ByteStride));
                }
                else
                {
                    VK_CALL(vkCmdDrawIndexedIndirect(list->vk_command_buffer, arg_impl->u.vk_buffer,
                            arg_buffer_offset, max_command_count, signature_desc->ByteStride));
                }
                break;

            case D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH:
                if (max_command_count != 1)
                    FIXME("Ignoring command count %u.\n", max_command_count);

                if (count_buffer)
                {
                    FIXME("Count buffers not supported for indirect dispatch.\n");
                    break;
                }

                if (!d3d12_command_list_update_compute_state(list))
                {
                    WARN("Failed to update compute state, ignoring dispatch.\n");
                    return;
                }

                VK_CALL(vkCmdDispatchIndirect(list->vk_command_buffer,
                        arg_impl->u.vk_buffer, arg_buffer_offset));
                break;

            default:
                FIXME("Ignoring unhandled argument type %#x.\n", arg_desc->Type);
                break;
        }
    }
}

static void STDMETHODCALLTYPE d3d12_command_list_AtomicCopyBufferUINT(d3d12_command_list_iface *iface,
        ID3D12Resource *dst_buffer, UINT64 dst_offset,
        ID3D12Resource *src_buffer, UINT64 src_offset,
        UINT dependent_resource_count, ID3D12Resource * const *dependent_resources,
        const D3D12_SUBRESOURCE_RANGE_UINT64 *dependent_sub_resource_ranges)
{
    FIXME("iface %p, dst_resource %p, dst_offset %#"PRIx64", src_resource %p, "
            "src_offset %#"PRIx64", dependent_resource_count %u, "
            "dependent_resources %p, dependent_sub_resource_ranges %p stub!\n",
            iface, dst_buffer, dst_offset, src_buffer, src_offset,
            dependent_resource_count, dependent_resources, dependent_sub_resource_ranges);
}

static void STDMETHODCALLTYPE d3d12_command_list_AtomicCopyBufferUINT64(d3d12_command_list_iface *iface,
        ID3D12Resource *dst_buffer, UINT64 dst_offset,
        ID3D12Resource *src_buffer, UINT64 src_offset,
        UINT dependent_resource_count, ID3D12Resource * const *dependent_resources,
        const D3D12_SUBRESOURCE_RANGE_UINT64 *dependent_sub_resource_ranges)
{
    FIXME("iface %p, dst_resource %p, dst_offset %#"PRIx64", src_resource %p, "
            "src_offset %#"PRIx64", dependent_resource_count %u, "
            "dependent_resources %p, dependent_sub_resource_ranges %p stub!\n",
            iface, dst_buffer, dst_offset, src_buffer, src_offset,
            dependent_resource_count, dependent_resources, dependent_sub_resource_ranges);
}

static void STDMETHODCALLTYPE d3d12_command_list_OMSetDepthBounds(d3d12_command_list_iface *iface,
        FLOAT min, FLOAT max)
{
    struct d3d12_command_list *list = impl_from_ID3D12GraphicsCommandList(iface);
    struct vkd3d_dynamic_state *dyn_state = &list->dynamic_state;

    TRACE("iface %p, min %.8e, max %.8e.\n", iface, min, max);

    dyn_state->min_depth_bounds = min;
    dyn_state->max_depth_bounds = max;

    dyn_state->dirty_flags |= VKD3D_DYNAMIC_STATE_DEPTH_BOUNDS;
}

static void STDMETHODCALLTYPE d3d12_command_list_SetSamplePositions(d3d12_command_list_iface *iface,
        UINT sample_count, UINT pixel_count, D3D12_SAMPLE_POSITION *sample_positions)
{
    FIXME("iface %p, sample_count %u, pixel_count %u, sample_positions %p stub!\n",
            iface, sample_count, pixel_count, sample_positions);
}

static void STDMETHODCALLTYPE d3d12_command_list_ResolveSubresourceRegion(d3d12_command_list_iface *iface,
        ID3D12Resource *dst_resource, UINT dst_sub_resource_idx, UINT dst_x, UINT dst_y,
        ID3D12Resource *src_resource, UINT src_sub_resource_idx,
        D3D12_RECT *src_rect, DXGI_FORMAT format, D3D12_RESOLVE_MODE mode)
{
    FIXME("iface %p, dst_resource %p, dst_sub_resource_idx %u, "
            "dst_x %u, dst_y %u, src_resource %p, src_sub_resource_idx %u, "
            "src_rect %p, format %#x, mode %#x stub!\n",
            iface, dst_resource, dst_sub_resource_idx, dst_x, dst_y,
            src_resource, src_sub_resource_idx, src_rect, format, mode);
}

static void STDMETHODCALLTYPE d3d12_command_list_SetViewInstanceMask(d3d12_command_list_iface *iface, UINT mask)
{
    FIXME("iface %p, mask %#x stub!\n", iface, mask);
}

static void STDMETHODCALLTYPE d3d12_command_list_WriteBufferImmediate(d3d12_command_list_iface *iface,
        UINT count, const D3D12_WRITEBUFFERIMMEDIATE_PARAMETER *parameters,
        const D3D12_WRITEBUFFERIMMEDIATE_MODE *modes)
{
    struct d3d12_command_list *list = impl_from_ID3D12GraphicsCommandList(iface);
    struct d3d12_resource *resource;
    unsigned int i;

    FIXME("iface %p, count %u, parameters %p, modes %p stub!\n", iface, count, parameters, modes);

    for (i = 0; i < count; ++i)
    {
        resource = vkd3d_gpu_va_allocator_dereference(&list->device->gpu_va_allocator, parameters[i].Dest);
        d3d12_command_list_track_resource_usage(list, resource);
    }
}

static void STDMETHODCALLTYPE d3d12_command_list_SetProtectedResourceSession(d3d12_command_list_iface *iface,
        ID3D12ProtectedResourceSession *protected_session)
{
    FIXME("iface %p, protected_session %p stub!\n", iface, protected_session);
}

static void STDMETHODCALLTYPE d3d12_command_list_BeginRenderPass(d3d12_command_list_iface *iface,
        UINT rt_count, const D3D12_RENDER_PASS_RENDER_TARGET_DESC *render_targets,
        const D3D12_RENDER_PASS_DEPTH_STENCIL_DESC *depth_stencil, D3D12_RENDER_PASS_FLAGS flags)
{
    FIXME("iface %p, rt_count %u, render_targets %p, depth_stencil %p, flags %#x stub!\n",
            iface, rt_count, render_targets, depth_stencil, flags);
}

static void STDMETHODCALLTYPE d3d12_command_list_EndRenderPass(d3d12_command_list_iface *iface)
{
    FIXME("iface %p stub!\n", iface);
}

static void STDMETHODCALLTYPE d3d12_command_list_InitializeMetaCommand(d3d12_command_list_iface *iface,
        ID3D12MetaCommand *meta_command, const void *parameter_data, SIZE_T parameter_size)
{
    FIXME("iface %p, meta_command %p, parameter_data %p, parameter_size %lu stub!\n",
            iface, meta_command, parameter_data, parameter_size);
}

static void STDMETHODCALLTYPE d3d12_command_list_ExecuteMetaCommand(d3d12_command_list_iface *iface,
        ID3D12MetaCommand *meta_command, const void *parameter_data, SIZE_T parameter_size)
{
    FIXME("iface %p, meta_command %p, parameter_data %p, parameter_size %lu stub!\n",
            iface, meta_command, parameter_data, parameter_size);
}

static void STDMETHODCALLTYPE d3d12_command_list_BuildRaytracingAccelerationStructure(d3d12_command_list_iface *iface,
        const D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC *desc, UINT num_postbuild_info_descs,
        const D3D12_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO_DESC *postbuild_info_descs)
{
    FIXME("iface %p, desc %p, num_postbuild_info_descs %u, postbuild_info_descs %p stub!\n",
            iface, desc, num_postbuild_info_descs, postbuild_info_descs);
}

static void STDMETHODCALLTYPE d3d12_command_list_EmitRaytracingAccelerationStructurePostbuildInfo(d3d12_command_list_iface *iface,
        const D3D12_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO_DESC *desc, UINT num_acceleration_structures,
        const D3D12_GPU_VIRTUAL_ADDRESS *src_data)
{
    FIXME("iface %p, desc %p, num_acceleration_structures %u, src_data %p stub!\n",
            iface, desc, num_acceleration_structures, src_data);
}

static void STDMETHODCALLTYPE d3d12_command_list_CopyRaytracingAccelerationStructure(d3d12_command_list_iface *iface,
        D3D12_GPU_VIRTUAL_ADDRESS dst_data, D3D12_GPU_VIRTUAL_ADDRESS src_data,
        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_COPY_MODE mode)
{
    FIXME("iface %p, dst_data %#"PRIx64", src_data %#"PRIx64", mode %u stub!\n",
            iface, dst_data, src_data, mode);
}

static void STDMETHODCALLTYPE d3d12_command_list_SetPipelineState1(d3d12_command_list_iface *iface,
        ID3D12StateObject *state_object)
{
    FIXME("iface %p, state_object %p stub!\n", iface, state_object);
}

static void STDMETHODCALLTYPE d3d12_command_list_DispatchRays(d3d12_command_list_iface *iface,
        const D3D12_DISPATCH_RAYS_DESC *desc)
{
    FIXME("iface %p, desc %p stub!\n", iface, desc);
}

static void STDMETHODCALLTYPE d3d12_command_list_RSSetShadingRate(d3d12_command_list_iface *iface,
        D3D12_SHADING_RATE base, const D3D12_SHADING_RATE_COMBINER *combiners)
{
    FIXME("iface %p, base %#x, combiners %p stub!\n", iface, base, combiners);
}

static void STDMETHODCALLTYPE d3d12_command_list_RSSetShadingRateImage(d3d12_command_list_iface *iface,
        ID3D12Resource *image)
{
    FIXME("iface %p, image %p stub!\n", iface, image);
}

static const struct ID3D12GraphicsCommandList5Vtbl d3d12_command_list_vtbl =
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
    /* ID3D12GraphicsCommandList1 methods */
    d3d12_command_list_AtomicCopyBufferUINT,
    d3d12_command_list_AtomicCopyBufferUINT64,
    d3d12_command_list_OMSetDepthBounds,
    d3d12_command_list_SetSamplePositions,
    d3d12_command_list_ResolveSubresourceRegion,
    d3d12_command_list_SetViewInstanceMask,
    /* ID3D12GraphicsCommandList2 methods */
    d3d12_command_list_WriteBufferImmediate,
    /* ID3D12GraphicsCommandList3 methods */
    d3d12_command_list_SetProtectedResourceSession,
    /* ID3D12GraphicsCommandList4 methods */
    d3d12_command_list_BeginRenderPass,
    d3d12_command_list_EndRenderPass,
    d3d12_command_list_InitializeMetaCommand,
    d3d12_command_list_ExecuteMetaCommand,
    d3d12_command_list_BuildRaytracingAccelerationStructure,
    d3d12_command_list_EmitRaytracingAccelerationStructurePostbuildInfo,
    d3d12_command_list_CopyRaytracingAccelerationStructure,
    d3d12_command_list_SetPipelineState1,
    d3d12_command_list_DispatchRays,
    /* ID3D12GraphicsCommandList5 methods */
    d3d12_command_list_RSSetShadingRate,
    d3d12_command_list_RSSetShadingRateImage,
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

    memset(list, 0, sizeof(*list));
    list->ID3D12GraphicsCommandList_iface.lpVtbl = &d3d12_command_list_vtbl;
    list->refcount = 1;

    list->type = type;

    if (FAILED(hr = vkd3d_private_store_init(&list->private_store)))
        return hr;

    d3d12_device_add_ref(list->device = device);

    if ((list->allocator = allocator))
    {
        if (SUCCEEDED(hr = d3d12_command_allocator_allocate_command_buffer(allocator, list)))
        {
            d3d12_command_list_reset_state(list, initial_pipeline_state);
        }
        else
        {
            vkd3d_private_store_destroy(&list->private_store);
            d3d12_device_release(device);
        }
    }

    return hr;
}

HRESULT d3d12_command_list_create(struct d3d12_device *device,
        UINT node_mask, D3D12_COMMAND_LIST_TYPE type, ID3D12CommandAllocator *allocator_iface,
        ID3D12PipelineState *initial_pipeline_state, struct d3d12_command_list **list)
{
    struct d3d12_command_allocator *allocator = unsafe_impl_from_ID3D12CommandAllocator(allocator_iface);
    struct d3d12_command_list *object;
    HRESULT hr;

    debug_ignored_node_mask(node_mask);

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
        const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;

        vkd3d_private_store_destroy(&command_queue->private_store);

        d3d12_command_queue_submit_stop(command_queue);
        pthread_join(command_queue->submission_thread, NULL);
        pthread_mutex_destroy(&command_queue->queue_lock);
        pthread_cond_destroy(&command_queue->queue_cond);

        VK_CALL(vkDestroySemaphore(device->vk_device, command_queue->sparse_binding_signal.vk_semaphore, NULL));
        VK_CALL(vkDestroySemaphore(device->vk_device, command_queue->sparse_binding_wait.vk_semaphore, NULL));

        vkd3d_free(command_queue->submissions);
        vkd3d_free(command_queue);

        d3d12_device_release(device);
    }

    return refcount;
}

static HRESULT STDMETHODCALLTYPE d3d12_command_queue_GetPrivateData(ID3D12CommandQueue *iface,
        REFGUID guid, UINT *data_size, void *data)
{
    struct d3d12_command_queue *command_queue = impl_from_ID3D12CommandQueue(iface);

    TRACE("iface %p, guid %s, data_size %p, data %p.\n", iface, debugstr_guid(guid), data_size, data);

    return vkd3d_get_private_data(&command_queue->private_store, guid, data_size, data);
}

static HRESULT STDMETHODCALLTYPE d3d12_command_queue_SetPrivateData(ID3D12CommandQueue *iface,
        REFGUID guid, UINT data_size, const void *data)
{
    struct d3d12_command_queue *command_queue = impl_from_ID3D12CommandQueue(iface);

    TRACE("iface %p, guid %s, data_size %u, data %p.\n", iface, debugstr_guid(guid), data_size, data);

    return vkd3d_set_private_data(&command_queue->private_store, guid, data_size, data);
}

static HRESULT STDMETHODCALLTYPE d3d12_command_queue_SetPrivateDataInterface(ID3D12CommandQueue *iface,
        REFGUID guid, const IUnknown *data)
{
    struct d3d12_command_queue *command_queue = impl_from_ID3D12CommandQueue(iface);

    TRACE("iface %p, guid %s, data %p.\n", iface, debugstr_guid(guid), data);

    return vkd3d_set_private_data_interface(&command_queue->private_store, guid, data);
}

static HRESULT STDMETHODCALLTYPE d3d12_command_queue_SetName(ID3D12CommandQueue *iface, const WCHAR *name)
{
    struct d3d12_command_queue *command_queue = impl_from_ID3D12CommandQueue(iface);
    VkQueue vk_queue;
    HRESULT hr;

    TRACE("iface %p, name %s.\n", iface, debugstr_w(name, command_queue->device->wchar_size));

    if (!(vk_queue = vkd3d_queue_acquire(command_queue->vkd3d_queue)))
    {
        ERR("Failed to acquire queue %p.\n", command_queue->vkd3d_queue);
        return E_FAIL;
    }

    hr = vkd3d_set_vk_object_name(command_queue->device, (uint64_t)(uintptr_t)vk_queue,
            VK_DEBUG_REPORT_OBJECT_TYPE_QUEUE_EXT, name);
    vkd3d_queue_release(command_queue->vkd3d_queue);
    return hr;
}

static HRESULT STDMETHODCALLTYPE d3d12_command_queue_GetDevice(ID3D12CommandQueue *iface, REFIID iid, void **device)
{
    struct d3d12_command_queue *command_queue = impl_from_ID3D12CommandQueue(iface);

    TRACE("iface %p, iid %s, device %p.\n", iface, debugstr_guid(iid), device);

    return d3d12_device_query_interface(command_queue->device, iid, device);
}

static unsigned int vkd3d_get_tile_index_from_coordinate(const struct d3d12_sparse_info *sparse,
        const D3D12_TILED_RESOURCE_COORDINATE *coord)
{
    const D3D12_SUBRESOURCE_TILING *tiling = &sparse->tilings[coord->Subresource];

    if (tiling->StartTileIndexInOverallResource == ~0u)
        return sparse->packed_mips.StartTileIndexInOverallResource + coord->X;

    return tiling->StartTileIndexInOverallResource + coord->X +
            tiling->WidthInTiles * (coord->Y + tiling->HeightInTiles * coord->Z);
}

static unsigned int vkd3d_get_tile_index_from_region(const struct d3d12_sparse_info *sparse,
        const D3D12_TILED_RESOURCE_COORDINATE *coord, const D3D12_TILE_REGION_SIZE *size,
        unsigned int tile_index_in_region)
{
    if (!size->UseBox)
    {
        /* Tiles are already ordered by subresource and coordinates correctly,
         * so we can just add the tile index to the region's base index */
        return vkd3d_get_tile_index_from_coordinate(sparse, coord) + tile_index_in_region;
    }
    else
    {
        D3D12_TILED_RESOURCE_COORDINATE box_coord = *coord;
        box_coord.X += (tile_index_in_region % size->Width);
        box_coord.Y += (tile_index_in_region / size->Width) % size->Height;
        box_coord.Z += (tile_index_in_region / (size->Width * size->Height));
        return vkd3d_get_tile_index_from_coordinate(sparse, &box_coord);
    }
}

static void STDMETHODCALLTYPE d3d12_command_queue_UpdateTileMappings(ID3D12CommandQueue *iface,
        ID3D12Resource *resource, UINT region_count, const D3D12_TILED_RESOURCE_COORDINATE *region_coords,
        const D3D12_TILE_REGION_SIZE *region_sizes, ID3D12Heap *heap, UINT range_count,
        const D3D12_TILE_RANGE_FLAGS *range_flags, UINT *heap_range_offsets, UINT *range_tile_counts,
        D3D12_TILE_MAPPING_FLAGS flags)
{
    struct d3d12_command_queue *command_queue = impl_from_ID3D12CommandQueue(iface);
    unsigned int region_tile = 0, region_idx = 0, range_tile = 0, range_idx = 0;
    struct d3d12_resource *res = unsafe_impl_from_ID3D12Resource(resource);
    struct d3d12_heap *memory_heap = unsafe_impl_from_ID3D12Heap(heap);
    struct vkd3d_sparse_memory_bind *bind, **bound_tiles;
    struct d3d12_sparse_info *sparse = &res->sparse;
    D3D12_TILED_RESOURCE_COORDINATE region_coord;
    struct d3d12_command_queue_submission sub;
    D3D12_TILE_REGION_SIZE region_size;
    D3D12_TILE_RANGE_FLAGS range_flag;
    UINT range_size, range_offset;
    size_t bind_infos_size = 0;

    TRACE("iface %p, resource %p, region_count %u, region_coords %p, "
            "region_sizes %p, heap %p, range_count %u, range_flags %p, heap_range_offsets %p, "
            "range_tile_counts %p, flags %#x.\n",
            iface, resource, region_count, region_coords, region_sizes, heap,
            range_count, range_flags, heap_range_offsets, range_tile_counts, flags);

    if (!region_count || !range_count)
        return;

    sub.type = VKD3D_SUBMISSION_BIND_SPARSE;
    sub.u.bind_sparse.mode = VKD3D_SPARSE_MEMORY_BIND_MODE_UPDATE;
    sub.u.bind_sparse.bind_count = 0;
    sub.u.bind_sparse.bind_infos = NULL;
    sub.u.bind_sparse.dst_resource = res;
    sub.u.bind_sparse.src_resource = NULL;

    if (region_coords)
        region_coord = region_coords[0];
    else
    {
        region_coord.X = 0;
        region_coord.Y = 0;
        region_coord.Z = 0;
        region_coord.Subresource = 0;
    }

    if (region_sizes)
        region_size = region_sizes[0];
    else
    {
        region_size.NumTiles = region_coords ? 1 : sparse->tile_count;
        region_size.UseBox = false;
        region_size.Width = 0;
        region_size.Height = 0;
        region_size.Depth = 0;
    }

    range_flag = range_flags ? range_flags[0] : D3D12_TILE_RANGE_FLAG_NONE;
    range_size = range_tile_counts ? range_tile_counts[0] : ~0u;
    range_offset = heap_range_offsets ? heap_range_offsets[0] : 0;

    if (!(bound_tiles = vkd3d_calloc(sparse->tile_count, sizeof(*bound_tiles))))
    {
        ERR("Failed to allocate tile mapping table.\n");
        return;
    }

    while (region_idx < region_count && range_idx < range_count)
    {
        if (range_flag != D3D12_TILE_RANGE_FLAG_SKIP)
        {
            unsigned int tile_index = vkd3d_get_tile_index_from_region(sparse, &region_coord, &region_size, region_tile);

            if (!(bind = bound_tiles[tile_index]))
            {
                if (!vkd3d_array_reserve((void **)&sub.u.bind_sparse.bind_infos, &bind_infos_size,
                        sub.u.bind_sparse.bind_count + 1, sizeof(*sub.u.bind_sparse.bind_infos)))
                {
                    ERR("Failed to allocate bind info array.\n");
                    goto fail;
                }

                bind = &sub.u.bind_sparse.bind_infos[sub.u.bind_sparse.bind_count++];
                bound_tiles[tile_index] = bind;
            }

            bind->dst_tile = tile_index;
            bind->src_tile = 0;

            if (range_flag == D3D12_TILE_RANGE_FLAG_NULL)
            {
                bind->vk_memory = VK_NULL_HANDLE;
                bind->vk_offset = 0;
            }
            else
            {
                bind->vk_memory = memory_heap->vk_memory;
                bind->vk_offset = VKD3D_TILE_SIZE * range_offset;

                if (range_flag != D3D12_TILE_RANGE_FLAG_REUSE_SINGLE_TILE)
                    bind->vk_offset += VKD3D_TILE_SIZE * range_tile;
            }
        }

        if (++range_tile == range_size)
        {
            range_idx += 1;
            range_tile = 0;

            if (range_flags)
                range_flag = range_flags[range_idx];

            range_size = range_tile_counts[range_idx];
            range_offset = heap_range_offsets[range_idx];
        }

        if (++region_tile == region_size.NumTiles)
        {
            region_idx += 1;
            region_tile = 0;

            if (region_coords)
                region_coord = region_coords[region_idx];

            if (region_sizes)
                region_size = region_sizes[region_idx];
        }
    }

    vkd3d_free(bound_tiles);
    d3d12_command_queue_add_submission(command_queue, &sub);
    return;

fail:
    vkd3d_free(bound_tiles);
    vkd3d_free(sub.u.bind_sparse.bind_infos);
}

static void STDMETHODCALLTYPE d3d12_command_queue_CopyTileMappings(ID3D12CommandQueue *iface,
        ID3D12Resource *dst_resource, const D3D12_TILED_RESOURCE_COORDINATE *dst_region_start_coordinate,
        ID3D12Resource *src_resource, const D3D12_TILED_RESOURCE_COORDINATE *src_region_start_coordinate,
        const D3D12_TILE_REGION_SIZE *region_size, D3D12_TILE_MAPPING_FLAGS flags)
{
    struct d3d12_command_queue *command_queue = impl_from_ID3D12CommandQueue(iface);
    struct d3d12_resource *dst_res = unsafe_impl_from_ID3D12Resource(dst_resource);
    struct d3d12_resource *src_res = unsafe_impl_from_ID3D12Resource(src_resource);
    struct d3d12_command_queue_submission sub;
    struct vkd3d_sparse_memory_bind *bind;
    unsigned int i;

    TRACE("iface %p, dst_resource %p, dst_region_start_coordinate %p, "
            "src_resource %p, src_region_start_coordinate %p, region_size %p, flags %#x.\n",
            iface, dst_resource, dst_region_start_coordinate, src_resource,
            src_region_start_coordinate, region_size, flags);

    sub.type = VKD3D_SUBMISSION_BIND_SPARSE;
    sub.u.bind_sparse.mode = VKD3D_SPARSE_MEMORY_BIND_MODE_COPY;
    sub.u.bind_sparse.bind_count = region_size->NumTiles;
    sub.u.bind_sparse.bind_infos = vkd3d_malloc(region_size->NumTiles * sizeof(*sub.u.bind_sparse.bind_infos));
    sub.u.bind_sparse.dst_resource = dst_res;
    sub.u.bind_sparse.src_resource = src_res;

    if (!sub.u.bind_sparse.bind_infos)
    {
        ERR("Failed to allocate bind info array.\n");
        return;
    }

    for (i = 0; i < region_size->NumTiles; i++)
    {
        bind = &sub.u.bind_sparse.bind_infos[i];
        bind->dst_tile = vkd3d_get_tile_index_from_region(&dst_res->sparse, dst_region_start_coordinate, region_size, i);
        bind->src_tile = vkd3d_get_tile_index_from_region(&src_res->sparse, src_region_start_coordinate, region_size, i);
        bind->vk_memory = VK_NULL_HANDLE;
        bind->vk_offset = 0;
    }

    d3d12_command_queue_add_submission(command_queue, &sub);
}

static void STDMETHODCALLTYPE d3d12_command_queue_ExecuteCommandLists(ID3D12CommandQueue *iface,
        UINT command_list_count, ID3D12CommandList * const *command_lists)
{
    struct d3d12_command_queue *command_queue = impl_from_ID3D12CommandQueue(iface);
    struct d3d12_command_queue_submission sub;
    struct d3d12_command_list *cmd_list;
    VkCommandBuffer *buffers;
    LONG **outstanding;
    unsigned int i, j;

    TRACE("iface %p, command_list_count %u, command_lists %p.\n",
            iface, command_list_count, command_lists);

    if (!(buffers = vkd3d_calloc(command_list_count, sizeof(*buffers))))
    {
        ERR("Failed to allocate command buffer array.\n");
        return;
    }

    if (!(outstanding = vkd3d_calloc(command_list_count, sizeof(*outstanding))))
    {
        ERR("Failed to allocate outstanding submissions count.\n");
        return;
    }

    for (i = 0; i < command_list_count; ++i)
    {
        cmd_list = unsafe_impl_from_ID3D12CommandList(command_lists[i]);

        if (cmd_list->is_recording)
        {
            d3d12_device_mark_as_removed(command_queue->device, DXGI_ERROR_INVALID_CALL,
                    "Command list %p is in recording state.\n", command_lists[i]);
            vkd3d_free(buffers);
            return;
        }

        outstanding[i] = cmd_list->outstanding_submissions_count;
        InterlockedIncrement(outstanding[i]);

        for (j = 0; j < cmd_list->descriptor_updates_count; j++)
            d3d12_deferred_descriptor_set_update_resolve(cmd_list, &cmd_list->descriptor_updates[j]);
        buffers[i] = cmd_list->vk_command_buffer;
    }

    sub.type = VKD3D_SUBMISSION_EXECUTE;
    sub.u.execute.cmd = buffers;
    sub.u.execute.count = command_list_count;
    sub.u.execute.outstanding_submissions_count = outstanding;
    d3d12_command_queue_add_submission(command_queue, &sub);
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
        ID3D12Fence *fence_iface, UINT64 value)
{
    struct d3d12_command_queue *command_queue = impl_from_ID3D12CommandQueue(iface);
    struct d3d12_command_queue_submission sub;
    struct d3d12_fence *fence;

    TRACE("iface %p, fence %p, value %#"PRIx64".\n", iface, fence_iface, value);

    fence = unsafe_impl_from_ID3D12Fence(fence_iface);

    sub.type = VKD3D_SUBMISSION_SIGNAL;
    sub.u.signal.fence = fence;
    sub.u.signal.value = value;
    d3d12_command_queue_add_submission(command_queue, &sub);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d3d12_command_queue_Wait(ID3D12CommandQueue *iface,
        ID3D12Fence *fence_iface, UINT64 value)
{
    struct d3d12_command_queue *command_queue = impl_from_ID3D12CommandQueue(iface);
    struct d3d12_command_queue_submission sub;
    struct d3d12_fence *fence;

    TRACE("iface %p, fence %p, value %#"PRIx64".\n", iface, fence_iface, value);

    fence = unsafe_impl_from_ID3D12Fence(fence_iface);

    sub.type = VKD3D_SUBMISSION_WAIT;
    sub.u.wait.fence = fence;
    sub.u.wait.value = value;
    d3d12_command_queue_add_submission(command_queue, &sub);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d3d12_command_queue_GetTimestampFrequency(ID3D12CommandQueue *iface,
        UINT64 *frequency)
{
    struct d3d12_command_queue *command_queue = impl_from_ID3D12CommandQueue(iface);
    struct d3d12_device *device = command_queue->device;

    TRACE("iface %p, frequency %p.\n", iface, frequency);

    if (!command_queue->vkd3d_queue->timestamp_bits)
    {
        WARN("Timestamp queries not supported.\n");
        return E_FAIL;
    }

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

static void d3d12_command_queue_wait(struct d3d12_command_queue *command_queue,
        struct d3d12_fence *fence, UINT64 value)
{
    static const VkPipelineStageFlagBits wait_stage_mask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    VkTimelineSemaphoreSubmitInfoKHR timeline_submit_info;
    const struct vkd3d_vk_device_procs *vk_procs;
    struct vkd3d_queue *queue;
    VkSubmitInfo submit_info;
    VkQueue vk_queue;
    VkResult vr;

    d3d12_fence_lock(fence);

    /* This is the critical part required to support out-of-order signal.
     * Normally we would be able to submit waits and signals out of order,
     * but we don't have virtualized queues in Vulkan, so we need to handle the case
     * where multiple queues alias over the same physical queue, so effectively, we need to manage out-of-order submits
     * ourselves. */
    d3d12_fence_block_until_pending_value_reaches_locked(fence, value);

    /* If a host signal unblocked us, or we know that the fence has reached a specific value, there is no need
     * to queue up a wait. */
    if (d3d12_fence_can_elide_wait_semaphore_locked(fence, value))
    {
        d3d12_fence_unlock(fence);
        return;
    }
    d3d12_fence_unlock(fence);

    TRACE("queue %p, fence %p, value %#"PRIx64".\n", command_queue, fence, value);

    vk_procs = &command_queue->device->vk_procs;
    queue = command_queue->vkd3d_queue;

    assert(fence->timeline_semaphore);
    timeline_submit_info.sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO_KHR;
    timeline_submit_info.pNext = NULL;
    timeline_submit_info.signalSemaphoreValueCount = 0;
    timeline_submit_info.pSignalSemaphoreValues = NULL;
    timeline_submit_info.waitSemaphoreValueCount = 1;
    timeline_submit_info.pWaitSemaphoreValues = &value;

    if (!(vk_queue = vkd3d_queue_acquire(queue)))
    {
        ERR("Failed to acquire queue %p.\n", queue);
        return;
    }

    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.pNext = &timeline_submit_info;
    submit_info.waitSemaphoreCount = 1;
    submit_info.pWaitSemaphores = &fence->timeline_semaphore;
    submit_info.pWaitDstStageMask = &wait_stage_mask;
    submit_info.commandBufferCount = 0;
    submit_info.pCommandBuffers = NULL;
    submit_info.signalSemaphoreCount = 0;
    submit_info.pSignalSemaphores = NULL;

    vr = VK_CALL(vkQueueSubmit(vk_queue, 1, &submit_info, VK_NULL_HANDLE));

    vkd3d_queue_release(queue);

    if (vr < 0)
    {
        ERR("Failed to submit wait operation, vr %d.\n", vr);
    }

    /* We should probably trigger DEVICE_REMOVED if we hit any errors in the submission thread. */
}

static void d3d12_command_queue_signal(struct d3d12_command_queue *command_queue,
        struct d3d12_fence *fence, UINT64 value)
{
    VkTimelineSemaphoreSubmitInfoKHR timeline_submit_info;
    const struct vkd3d_vk_device_procs *vk_procs;
    struct vkd3d_queue *vkd3d_queue;
    struct d3d12_device *device;
    VkSubmitInfo submit_info;
    VkQueue vk_queue;
    VkResult vr;
    HRESULT hr;

    device = command_queue->device;
    vk_procs = &device->vk_procs;
    vkd3d_queue = command_queue->vkd3d_queue;

    d3d12_fence_lock(fence);

    if (!d3d12_fence_can_signal_semaphore_locked(fence, value))
    {
        d3d12_fence_unlock(fence);
        return;
    }

    TRACE("queue %p, fence %p, value %#"PRIx64".\n", command_queue, fence, value);

    /* Need to hold the fence lock while we're submitting, since another thread could come in and signal the semaphore
     * to a higher value before we call vkQueueSubmit, which creates a non-monotonically increasing value. */

    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.pNext = NULL;
    submit_info.waitSemaphoreCount = 0;
    submit_info.pWaitSemaphores = NULL;
    submit_info.pWaitDstStageMask = NULL;
    submit_info.commandBufferCount = 0;
    submit_info.pCommandBuffers = NULL;
    submit_info.signalSemaphoreCount = 1;
    submit_info.pSignalSemaphores = &fence->timeline_semaphore;

    timeline_submit_info.sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO_KHR;
    timeline_submit_info.pNext = NULL;
    submit_info.pNext = &timeline_submit_info;

    timeline_submit_info.pSignalSemaphoreValues = &value;
    timeline_submit_info.signalSemaphoreValueCount = 1;
    timeline_submit_info.waitSemaphoreValueCount = 0;
    timeline_submit_info.pWaitSemaphoreValues = NULL;

    if (!(vk_queue = vkd3d_queue_acquire(vkd3d_queue)))
    {
        ERR("Failed to acquire queue %p.\n", vkd3d_queue);
        d3d12_fence_unlock(fence);
        return;
    }

    vr = VK_CALL(vkQueueSubmit(vk_queue, 1, &submit_info, VK_NULL_HANDLE));

    if (vr == VK_SUCCESS)
        d3d12_fence_update_pending_value_locked(fence, value);
    d3d12_fence_unlock(fence);

    vkd3d_queue_release(vkd3d_queue);

    if (vr < 0)
    {
        ERR("Failed to submit signal operation, vr %d.\n", vr);
        return;
    }

    if (FAILED(hr = vkd3d_enqueue_timeline_semaphore(&device->fence_worker, fence, value, vkd3d_queue)))
    {
        /* In case of an unexpected failure, try to safely destroy Vulkan objects. */
        vkd3d_queue_wait_idle(vkd3d_queue, vk_procs);
    }

    /* We should probably trigger DEVICE_REMOVED if we hit any errors in the submission thread. */
}

static void d3d12_command_queue_execute(struct d3d12_command_queue *command_queue,
        VkCommandBuffer *cmd, UINT count)
{
    const struct vkd3d_vk_device_procs *vk_procs;
    struct VkSubmitInfo submit_desc;
    VkQueue vk_queue;
    VkResult vr;

    TRACE("queue %p, command_list_count %u, command_lists %p.\n",
          command_queue, count, cmd);

    vk_procs = &command_queue->device->vk_procs;

    submit_desc.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_desc.pNext = NULL;
    submit_desc.waitSemaphoreCount = 0;
    submit_desc.pWaitSemaphores = NULL;
    submit_desc.pWaitDstStageMask = NULL;
    submit_desc.commandBufferCount = count;
    submit_desc.pCommandBuffers = cmd;
    submit_desc.signalSemaphoreCount = 0;
    submit_desc.pSignalSemaphores = NULL;

    if (!(vk_queue = vkd3d_queue_acquire(command_queue->vkd3d_queue)))
    {
        ERR("Failed to acquire queue %p.\n", command_queue->vkd3d_queue);
        return;
    }

    if ((vr = VK_CALL(vkQueueSubmit(vk_queue, 1, &submit_desc, VK_NULL_HANDLE))) < 0)
        ERR("Failed to submit queue(s), vr %d.\n", vr);

    vkd3d_queue_release(command_queue->vkd3d_queue);
}

static void d3d12_command_queue_bind_sparse(struct d3d12_command_queue *command_queue,
        enum vkd3d_sparse_memory_bind_mode mode, struct d3d12_resource *dst_resource,
        struct d3d12_resource *src_resource, unsigned int count,
        struct vkd3d_sparse_memory_bind *bind_infos)
{
    VkSparseImageOpaqueMemoryBindInfo opaque_info;
    const struct vkd3d_vk_device_procs *vk_procs;
    VkSparseImageMemoryBind *image_binds = NULL;
    VkSparseBufferMemoryBindInfo buffer_info;
    VkSparseMemoryBind *memory_binds = NULL;
    VkSparseImageMemoryBindInfo image_info;
    VkBindSparseInfo bind_sparse_info;
    unsigned int first_packed_tile;
    struct vkd3d_queue *queue;
    VkDeviceMemory vk_memory;
    VkDeviceSize vk_offset;
    unsigned int i, j, k;
    VkQueue vk_queue;
    VkResult vr;

    TRACE("queue %p, dst_resource %p, src_resource %p, count %u, bind_infos %p.\n",
          command_queue, dst_resource, src_resource, count, bind_infos);

    vk_procs = &command_queue->device->vk_procs;

    bind_sparse_info.sType = VK_STRUCTURE_TYPE_BIND_SPARSE_INFO;
    bind_sparse_info.pNext = NULL;
    bind_sparse_info.waitSemaphoreCount = 0;
    bind_sparse_info.pWaitSemaphores = NULL;
    bind_sparse_info.bufferBindCount = 0;
    bind_sparse_info.pBufferBinds = NULL;
    bind_sparse_info.imageOpaqueBindCount = 0;
    bind_sparse_info.pImageOpaqueBinds = NULL;
    bind_sparse_info.imageBindCount = 0;
    bind_sparse_info.pImageBinds = NULL;
    bind_sparse_info.signalSemaphoreCount = 0;
    bind_sparse_info.pSignalSemaphores = NULL;

    first_packed_tile = dst_resource->sparse.tile_count;

    if (d3d12_resource_is_buffer(dst_resource))
    {
        if (!(memory_binds = vkd3d_malloc(count * sizeof(*memory_binds))))
        {
            ERR("Failed to allocate sparse memory bind info.\n");
            goto cleanup;
        }

        buffer_info.buffer = dst_resource->u.vk_buffer;
        buffer_info.bindCount = count;
        buffer_info.pBinds = memory_binds;

        bind_sparse_info.bufferBindCount = 1;
        bind_sparse_info.pBufferBinds = &buffer_info;
    }
    else
    {
        unsigned int opaque_bind_count = 0;
        unsigned int image_bind_count = 0;

        if (dst_resource->sparse.packed_mips.NumPackedMips)
            first_packed_tile = dst_resource->sparse.packed_mips.StartTileIndexInOverallResource;

        for (i = 0; i < count; i++)
        {
            const struct vkd3d_sparse_memory_bind *bind = &bind_infos[i];

            if (bind->dst_tile < first_packed_tile)
                image_bind_count++;
            else
                opaque_bind_count++;
        }

        if (opaque_bind_count)
        {
            if (!(memory_binds = vkd3d_malloc(opaque_bind_count * sizeof(*memory_binds))))
            {
                ERR("Failed to allocate sparse memory bind info.\n");
                goto cleanup;
            }

            opaque_info.image = dst_resource->u.vk_image;
            opaque_info.bindCount = opaque_bind_count;
            opaque_info.pBinds = memory_binds;

            bind_sparse_info.imageOpaqueBindCount = 1;
            bind_sparse_info.pImageOpaqueBinds = &opaque_info;
        }

        if (image_bind_count)
        {
            if (!(image_binds = vkd3d_malloc(image_bind_count * sizeof(*image_binds))))
            {
                ERR("Failed to allocate sparse memory bind info.\n");
                goto cleanup;
            }

            image_info.image = dst_resource->u.vk_image;
            image_info.bindCount = image_bind_count;
            image_info.pBinds = image_binds;

            bind_sparse_info.imageBindCount = 1;
            bind_sparse_info.pImageBinds = &image_info;
        }
    }

    for (i = 0, j = 0, k = 0; i < count; i++)
    {
        const struct vkd3d_sparse_memory_bind *bind = &bind_infos[i];
        struct d3d12_sparse_tile *tile = &dst_resource->sparse.tiles[bind->dst_tile];

        if (mode == VKD3D_SPARSE_MEMORY_BIND_MODE_UPDATE)
        {
            vk_memory = bind->vk_memory;
            vk_offset = bind->vk_offset;
        }
        else /* if (mode == VKD3D_SPARSE_MEMORY_BIND_MODE_COPY) */
        {
            struct d3d12_sparse_tile *src_tile = &src_resource->sparse.tiles[bind->src_tile];
            vk_memory = src_tile->vk_memory;
            vk_offset = src_tile->vk_offset;
        }

        if (d3d12_resource_is_texture(dst_resource) && bind->dst_tile < first_packed_tile)
        {
            VkSparseImageMemoryBind *vk_bind = &image_binds[j++];
            vk_bind->subresource = tile->u.image.subresource;
            vk_bind->offset = tile->u.image.offset;
            vk_bind->extent = tile->u.image.extent;
            vk_bind->memory = vk_memory;
            vk_bind->memoryOffset = vk_offset;
            vk_bind->flags = 0;
        }
        else
        {
            VkSparseMemoryBind *vk_bind = &memory_binds[k++];
            vk_bind->resourceOffset = tile->u.buffer.offset;
            vk_bind->size = tile->u.buffer.length;
            vk_bind->memory = vk_memory;
            vk_bind->memoryOffset = vk_offset;
            vk_bind->flags = 0;
        }

        tile->vk_memory = vk_memory;
        tile->vk_offset = vk_offset;
    }

    /* Ensure that we use a queue that supports sparse binding */
    queue = command_queue->vkd3d_queue;

    if (!(queue->vk_queue_flags & VK_QUEUE_SPARSE_BINDING_BIT))
        queue = command_queue->device->queues[VKD3D_QUEUE_FAMILY_SPARSE_BINDING];

    if (!(vk_queue = vkd3d_queue_acquire(queue)))
    {
        ERR("Failed to acquire queue %p.\n", queue);
        goto cleanup;
    }

    if ((vr = VK_CALL(vkQueueBindSparse(vk_queue, 1, &bind_sparse_info, VK_NULL_HANDLE))) < 0)
        ERR("Failed to perform sparse binding, vr %d.\n", vr);

    /* TODO synchronize properly with timeline semaphores */
    if ((vr = VK_CALL(vkQueueWaitIdle(vk_queue))) < 0)
        ERR("Failed to synchronize with queue, vr %d.\n", vr);

    vkd3d_queue_release(queue);

cleanup:
    vkd3d_free(memory_binds);
    vkd3d_free(image_binds);
}

void d3d12_command_queue_submit_stop(struct d3d12_command_queue *queue)
{
    struct d3d12_command_queue_submission sub;
    sub.type = VKD3D_SUBMISSION_STOP;
    d3d12_command_queue_add_submission(queue, &sub);
}

static void d3d12_command_queue_add_submission_locked(struct d3d12_command_queue *queue,
                                                      const struct d3d12_command_queue_submission *sub)
{
    vkd3d_array_reserve((void**)&queue->submissions, &queue->submissions_size,
                        queue->submissions_count + 1, sizeof(*queue->submissions));
    queue->submissions[queue->submissions_count++] = *sub;
    pthread_cond_signal(&queue->queue_cond);
}

static void d3d12_command_queue_add_submission(struct d3d12_command_queue *queue,
        const struct d3d12_command_queue_submission *sub)
{
    pthread_mutex_lock(&queue->queue_lock);
    d3d12_command_queue_add_submission_locked(queue, sub);
    pthread_mutex_unlock(&queue->queue_lock);
}

static void d3d12_command_queue_acquire_serialized(struct d3d12_command_queue *queue)
{
    /* In order to make sure all pending operations queued so far have been submitted,
     * we build a drain task which will increment the queue_drain_count once the thread has finished all its work. */
    struct d3d12_command_queue_submission sub;
    uint64_t current_drain;

    sub.type = VKD3D_SUBMISSION_DRAIN;

    pthread_mutex_lock(&queue->queue_lock);

    current_drain = ++queue->drain_count;
    d3d12_command_queue_add_submission_locked(queue, &sub);

    while (current_drain != queue->queue_drain_count)
        pthread_cond_wait(&queue->queue_cond, &queue->queue_lock);
}

static void d3d12_command_queue_release_serialized(struct d3d12_command_queue *queue)
{
    pthread_mutex_unlock(&queue->queue_lock);
}

static void *d3d12_command_queue_submission_worker_main(void *userdata)
{
    struct d3d12_command_queue_submission submission;
    struct d3d12_command_queue *queue = userdata;
    unsigned int i;

    vkd3d_set_thread_name("vkd3d_queue");

    for (;;)
    {
        pthread_mutex_lock(&queue->queue_lock);
        while (queue->submissions_count == 0)
            pthread_cond_wait(&queue->queue_cond, &queue->queue_lock);

        queue->submissions_count--;
        submission = queue->submissions[0];
        memmove(queue->submissions, queue->submissions + 1, queue->submissions_count * sizeof(submission));
        pthread_mutex_unlock(&queue->queue_lock);

        switch (submission.type)
        {
        case VKD3D_SUBMISSION_STOP:
            return NULL;

        case VKD3D_SUBMISSION_WAIT:
            d3d12_command_queue_wait(queue, submission.u.wait.fence, submission.u.wait.value);
            break;

        case VKD3D_SUBMISSION_SIGNAL:
            d3d12_command_queue_signal(queue, submission.u.signal.fence, submission.u.signal.value);
            break;

        case VKD3D_SUBMISSION_EXECUTE:
            d3d12_command_queue_execute(queue, submission.u.execute.cmd, submission.u.execute.count);
            vkd3d_free(submission.u.execute.cmd);
            /* TODO: The correct place to do this would be in a fence handler, but this is good enough for now. */
            for (i = 0; i < submission.u.execute.count; i++)
                InterlockedDecrement(submission.u.execute.outstanding_submissions_count[i]);
            vkd3d_free(submission.u.execute.outstanding_submissions_count);
            break;

        case VKD3D_SUBMISSION_BIND_SPARSE:
            d3d12_command_queue_bind_sparse(queue, submission.u.bind_sparse.mode, 
                    submission.u.bind_sparse.dst_resource, submission.u.bind_sparse.src_resource,
                    submission.u.bind_sparse.bind_count, submission.u.bind_sparse.bind_infos);
            vkd3d_free(submission.u.bind_sparse.bind_infos);
            break;

        case VKD3D_SUBMISSION_DRAIN:
        {
            pthread_mutex_lock(&queue->queue_lock);
            queue->queue_drain_count++;
            pthread_cond_signal(&queue->queue_cond);
            pthread_mutex_unlock(&queue->queue_lock);
            break;
        }

        default:
            ERR("Unrecognized submission type %u.\n", submission.type);
            break;
        }
    }
}

static HRESULT d3d12_command_queue_init(struct d3d12_command_queue *queue,
        struct d3d12_device *device, const D3D12_COMMAND_QUEUE_DESC *desc)
{
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    HRESULT hr;
    int rc;

    queue->ID3D12CommandQueue_iface.lpVtbl = &d3d12_command_queue_vtbl;
    queue->refcount = 1;

    queue->desc = *desc;
    if (!queue->desc.NodeMask)
        queue->desc.NodeMask = 0x1;

    if (!(queue->vkd3d_queue = d3d12_device_get_vkd3d_queue(device, desc->Type)))
    {
        hr = E_NOTIMPL;
        goto fail;
    }

    queue->submissions = NULL;
    queue->submissions_count = 0;
    queue->submissions_size = 0;
    queue->drain_count = 0;
    queue->queue_drain_count = 0;

    if (FAILED(hr = vkd3d_create_timeline_semaphore(device, 0, &queue->sparse_binding_wait.vk_semaphore)))
        goto fail;

    if (FAILED(hr = vkd3d_create_timeline_semaphore(device, 0, &queue->sparse_binding_signal.vk_semaphore)))
        goto fail_signal_semaphore;

    queue->sparse_binding_wait.last_signaled = 0;
    queue->sparse_binding_signal.last_signaled = 0;

    if ((rc = pthread_mutex_init(&queue->queue_lock, NULL)) < 0)
    {
        hr = hresult_from_errno(rc);
        goto fail_pthread_mutex;
    }

    if ((rc = pthread_cond_init(&queue->queue_cond, NULL)) < 0)
    {
        hr = hresult_from_errno(rc);
        goto fail_pthread_cond;
    }

    if ((rc = pthread_create(&queue->submission_thread, NULL, d3d12_command_queue_submission_worker_main, queue)) < 0)
    {
        hr = hresult_from_errno(rc);
        goto fail_pthread_create;
    }

    if (desc->Priority == D3D12_COMMAND_QUEUE_PRIORITY_GLOBAL_REALTIME)
        FIXME("Global realtime priority is not implemented.\n");
    if (desc->Priority)
        FIXME("Ignoring priority %#x.\n", desc->Priority);
    if (desc->Flags)
        FIXME("Ignoring flags %#x.\n", desc->Flags);

    if (FAILED(hr = vkd3d_private_store_init(&queue->private_store)))
        goto fail_private_store;

    d3d12_device_add_ref(queue->device = device);

    return S_OK;

fail_private_store:
    d3d12_command_queue_submit_stop(queue);
    pthread_join(queue->submission_thread, NULL);
fail_pthread_create:
    pthread_cond_destroy(&queue->queue_cond);
fail_pthread_cond:
    pthread_mutex_destroy(&queue->queue_lock);
fail_pthread_mutex:
    VK_CALL(vkDestroySemaphore(device->vk_device, queue->sparse_binding_signal.vk_semaphore, NULL));
fail_signal_semaphore:
    VK_CALL(vkDestroySemaphore(device->vk_device, queue->sparse_binding_wait.vk_semaphore, NULL));
fail:
    return hr;
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

uint32_t vkd3d_get_vk_queue_family_index(ID3D12CommandQueue *queue)
{
    struct d3d12_command_queue *d3d12_queue = impl_from_ID3D12CommandQueue(queue);

    return d3d12_queue->vkd3d_queue->vk_family_index;
}

VkQueue vkd3d_acquire_vk_queue(ID3D12CommandQueue *queue)
{
    struct d3d12_command_queue *d3d12_queue = impl_from_ID3D12CommandQueue(queue);
    /* For external users of the Vulkan queue, we must ensure that the queue is drained so that submissions happen in
     * desired order. */
    d3d12_command_queue_acquire_serialized(d3d12_queue);
    return vkd3d_queue_acquire(d3d12_queue->vkd3d_queue);
}

void vkd3d_release_vk_queue(ID3D12CommandQueue *queue)
{
    struct d3d12_command_queue *d3d12_queue = impl_from_ID3D12CommandQueue(queue);
    vkd3d_queue_release(d3d12_queue->vkd3d_queue);
    d3d12_command_queue_release_serialized(d3d12_queue);
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

        vkd3d_private_store_destroy(&signature->private_store);

        vkd3d_free((void *)signature->desc.pArgumentDescs);
        vkd3d_free(signature);

        d3d12_device_release(device);
    }

    return refcount;
}

static HRESULT STDMETHODCALLTYPE d3d12_command_signature_GetPrivateData(ID3D12CommandSignature *iface,
        REFGUID guid, UINT *data_size, void *data)
{
    struct d3d12_command_signature *signature = impl_from_ID3D12CommandSignature(iface);

    TRACE("iface %p, guid %s, data_size %p, data %p.\n", iface, debugstr_guid(guid), data_size, data);

    return vkd3d_get_private_data(&signature->private_store, guid, data_size, data);
}

static HRESULT STDMETHODCALLTYPE d3d12_command_signature_SetPrivateData(ID3D12CommandSignature *iface,
        REFGUID guid, UINT data_size, const void *data)
{
    struct d3d12_command_signature *signature = impl_from_ID3D12CommandSignature(iface);

    TRACE("iface %p, guid %s, data_size %u, data %p.\n", iface, debugstr_guid(guid), data_size, data);

    return vkd3d_set_private_data(&signature->private_store, guid, data_size, data);
}

static HRESULT STDMETHODCALLTYPE d3d12_command_signature_SetPrivateDataInterface(ID3D12CommandSignature *iface,
        REFGUID guid, const IUnknown *data)
{
    struct d3d12_command_signature *signature = impl_from_ID3D12CommandSignature(iface);

    TRACE("iface %p, guid %s, data %p.\n", iface, debugstr_guid(guid), data);

    return vkd3d_set_private_data_interface(&signature->private_store, guid, data);
}

static HRESULT STDMETHODCALLTYPE d3d12_command_signature_SetName(ID3D12CommandSignature *iface, const WCHAR *name)
{
    struct d3d12_command_signature *signature = impl_from_ID3D12CommandSignature(iface);

    TRACE("iface %p, name %s.\n", iface, debugstr_w(name, signature->device->wchar_size));

    return name ? S_OK : E_INVALIDARG;
}

static HRESULT STDMETHODCALLTYPE d3d12_command_signature_GetDevice(ID3D12CommandSignature *iface, REFIID iid, void **device)
{
    struct d3d12_command_signature *signature = impl_from_ID3D12CommandSignature(iface);

    TRACE("iface %p, iid %s, device %p.\n", iface, debugstr_guid(iid), device);

    return d3d12_device_query_interface(signature->device, iid, device);
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

struct d3d12_command_signature *unsafe_impl_from_ID3D12CommandSignature(ID3D12CommandSignature *iface)
{
    if (!iface)
        return NULL;
    assert(iface->lpVtbl == &d3d12_command_signature_vtbl);
    return CONTAINING_RECORD(iface, struct d3d12_command_signature, ID3D12CommandSignature_iface);
}

HRESULT d3d12_command_signature_create(struct d3d12_device *device, const D3D12_COMMAND_SIGNATURE_DESC *desc,
        struct d3d12_command_signature **signature)
{
    struct d3d12_command_signature *object;
    unsigned int i;
    HRESULT hr;

    for (i = 0; i < desc->NumArgumentDescs; ++i)
    {
        const D3D12_INDIRECT_ARGUMENT_DESC *argument_desc = &desc->pArgumentDescs[i];
        switch (argument_desc->Type)
        {
            case D3D12_INDIRECT_ARGUMENT_TYPE_DRAW:
            case D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED:
            case D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH:
                if (i != desc->NumArgumentDescs - 1)
                {
                    WARN("Draw/dispatch must be the last element of a command signature.\n");
                    return E_INVALIDARG;
                }
                break;
            default:
                break;
        }
    }

    if (!(object = vkd3d_malloc(sizeof(*object))))
        return E_OUTOFMEMORY;

    object->ID3D12CommandSignature_iface.lpVtbl = &d3d12_command_signature_vtbl;
    object->refcount = 1;

    object->desc = *desc;
    if (!(object->desc.pArgumentDescs = vkd3d_calloc(desc->NumArgumentDescs, sizeof(*desc->pArgumentDescs))))
    {
        vkd3d_free(object);
        return E_OUTOFMEMORY;
    }
    memcpy((void *)object->desc.pArgumentDescs, desc->pArgumentDescs,
            desc->NumArgumentDescs * sizeof(*desc->pArgumentDescs));

    if (FAILED(hr = vkd3d_private_store_init(&object->private_store)))
    {
        vkd3d_free((void *)object->desc.pArgumentDescs);
        vkd3d_free(object);
        return hr;
    }

    d3d12_device_add_ref(object->device = device);

    TRACE("Created command signature %p.\n", object);

    *signature = object;

    return S_OK;
}
