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

#define VKD3D_DBG_CHANNEL VKD3D_DBG_CHANNEL_API

#include "vkd3d_private.h"
#include "vkd3d_swapchain_factory.h"
#ifdef VKD3D_ENABLE_RENDERDOC
#include "vkd3d_renderdoc.h"
#endif

static HRESULT d3d12_fence_signal(struct d3d12_fence *fence, uint64_t value);
static void d3d12_command_queue_add_submission(struct d3d12_command_queue *queue,
        const struct d3d12_command_queue_submission *sub);

static HRESULT vkd3d_create_binary_semaphore(struct d3d12_device *device, VkSemaphore *vk_semaphore)
{
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    VkSemaphoreCreateInfo info;
    VkResult vr;

    info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    info.pNext = NULL;
    info.flags = 0;
    vr = VK_CALL(vkCreateSemaphore(device->vk_device, &info, NULL, vk_semaphore));
    return hresult_from_vk_result(vr);
}

HRESULT vkd3d_queue_create(struct d3d12_device *device,
        uint32_t family_index, const VkQueueFamilyProperties *properties, struct vkd3d_queue **queue)
{
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    VkCommandBufferAllocateInfo allocate_info;
    VkCommandPoolCreateInfo pool_create_info;
    VkCommandBufferBeginInfo begin_info;
    VkMemoryBarrier memory_barrier;
    struct vkd3d_queue *object;
    VkResult vr;
    HRESULT hr;
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

    /* Create a reusable full barrier command buffer. This is used in submissions
     * to guarantee serialized behavior of Vulkan queues. */
    pool_create_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool_create_info.pNext = NULL;
    pool_create_info.flags = 0;
    pool_create_info.queueFamilyIndex = family_index;
    if ((vr = VK_CALL(vkCreateCommandPool(device->vk_device, &pool_create_info, NULL, &object->barrier_pool))))
    {
        hr = hresult_from_vk_result(vr);
        goto fail_destroy_mutex;
    }

    allocate_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocate_info.pNext = NULL;
    allocate_info.commandPool = object->barrier_pool;
    allocate_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocate_info.commandBufferCount = 1;
    if ((vr = VK_CALL(vkAllocateCommandBuffers(device->vk_device, &allocate_info, &object->barrier_command_buffer))))
    {
        hr = hresult_from_vk_result(vr);
        goto fail_free_command_pool;
    }

    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.pNext = NULL;
    /* It's not very meaningful to rebuild this command buffer over and over. */
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;
    begin_info.pInheritanceInfo = NULL;
    VK_CALL(vkBeginCommandBuffer(object->barrier_command_buffer, &begin_info));

    /* To avoid unnecessary tracking, just emit a host barrier on every submit. */
    memory_barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    memory_barrier.pNext = NULL;
    memory_barrier.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
    memory_barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_HOST_READ_BIT;
    VK_CALL(vkCmdPipelineBarrier(object->barrier_command_buffer,
            VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
            VK_PIPELINE_STAGE_ALL_COMMANDS_BIT | VK_PIPELINE_STAGE_HOST_BIT, 0,
            1, &memory_barrier, 0, NULL, 0, NULL));
    VK_CALL(vkEndCommandBuffer(object->barrier_command_buffer));

    if (FAILED(hr = vkd3d_create_binary_semaphore(device, &object->serializing_binary_semaphore)))
        goto fail_free_command_pool;

    *queue = object;
    return hr;

fail_free_command_pool:
    VK_CALL(vkDestroyCommandPool(device->vk_device, object->barrier_pool, NULL));
fail_destroy_mutex:
    pthread_mutex_destroy(&object->mutex);
    return hr;
}

void vkd3d_queue_destroy(struct vkd3d_queue *queue, struct d3d12_device *device)
{
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    int rc;

    if ((rc = pthread_mutex_lock(&queue->mutex)))
        ERR("Failed to lock mutex, error %d.\n", rc);

    if (!rc)
        pthread_mutex_unlock(&queue->mutex);

    VK_CALL(vkDestroyCommandPool(device->vk_device, queue->barrier_pool, NULL));
    VK_CALL(vkDestroySemaphore(device->vk_device, queue->serializing_binary_semaphore, NULL));

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

    if (!(count = vkd3d_atomic_uint32_load_explicit(&fence->pending_worker_operation_count, vkd3d_memory_order_acquire)))
        return;

    WARN("Waiting for %u pending fence operations (fence %p).\n", count, fence);

    if ((rc = pthread_mutex_lock(&worker->mutex)))
    {
        ERR("Failed to lock mutex, error %d.\n", rc);
        return;
    }

    while ((count = vkd3d_atomic_uint32_load_explicit(&fence->pending_worker_operation_count, vkd3d_memory_order_acquire)))
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

    /* This is a good time to kick the debug ring thread into action. */
    if (device->debug_ring.active)
        pthread_cond_signal(&device->debug_ring.ring_cond);

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

        if (!worker->fence_count || vkd3d_atomic_uint32_load_explicit(&worker->enqueued_fence_count, vkd3d_memory_order_acquire))
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
    return &p->descriptor_table;
}

static const struct d3d12_root_constant *root_signature_get_32bit_constants(
        const struct d3d12_root_signature *root_signature, unsigned int index)
{
    const struct d3d12_root_parameter *p = root_signature_get_parameter(root_signature, index);
    assert(p->parameter_type == D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS);
    return &p->constant;
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

        if (current->value <= fence->virtual_value)
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
    while (pending_value > fence->max_pending_virtual_timeline_value)
    {
        TRACE("Blocking wait on fence %p until it reaches 0x%"PRIx64".\n", fence, pending_value);
        pthread_cond_wait(&fence->cond, &fence->mutex);
    }
}

static void d3d12_fence_update_pending_value_locked(struct d3d12_fence *fence, uint64_t pending_value)
{
    /* If we're signalling the fence, wake up any submission threads which can now safely kick work. */
    fence->max_pending_virtual_timeline_value = pending_value;
    pthread_cond_broadcast(&fence->cond);
}

static void d3d12_fence_update_max_pending_value_locked(struct d3d12_fence *fence)
{
    uint64_t max_value = fence->virtual_value;
    size_t i;

    for (i = 0; i < fence->pending_updates_count; i++)
        if (fence->pending_updates[i].virtual_value > max_value)
            max_value = fence->pending_updates[i].virtual_value;

    d3d12_fence_update_pending_value_locked(fence, max_value);
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
    return fence->virtual_value >= value;
}

static HRESULT d3d12_fence_signal_cpu_timeline_semaphore(struct d3d12_fence *fence, uint64_t value)
{
    int rc;

    if ((rc = pthread_mutex_lock(&fence->mutex)))
    {
        ERR("Failed to lock mutex, error %d.\n", rc);
        return hresult_from_errno(rc);
    }

    /* If we signal a fence on the CPU, we implicitly discard any pending semaphore updates coming from GPU.
     * If we use the principle of "infinitely fast GPU invariance", immediately upon submitting to the GPU,
     * we must expect that the fence could be signaled. In this case, pending values should resolve immediately,
     * and thus we can safely discard them by overwriting the value with the new value. */
    fence->pending_updates_count = 0;
    fence->virtual_value = value;
    d3d12_fence_update_pending_value_locked(fence, value);
    d3d12_fence_signal_external_events_locked(fence);
    pthread_mutex_unlock(&fence->mutex);
    return S_OK;
}

static uint64_t d3d12_fence_add_pending_signal_locked(struct d3d12_fence *fence, uint64_t virtual_value)
{
    struct d3d12_fence_value *update;
    vkd3d_array_reserve((void**)&fence->pending_updates, &fence->pending_updates_size,
                        fence->pending_updates_count + 1, sizeof(*fence->pending_updates));

    update = &fence->pending_updates[fence->pending_updates_count++];
    update->virtual_value = virtual_value;
    update->physical_value = ++fence->counter;
    return fence->counter;
}

static uint64_t d3d12_fence_get_physical_wait_value_locked(struct d3d12_fence *fence, uint64_t virtual_value)
{
    size_t i;

    /* This shouldn't happen, we will have elided the wait completely in can_elide_wait_semaphore_locked. */
    assert(virtual_value > fence->virtual_value);

    /* Find the smallest physical value which is at least the virtual value. */
    for (i = 0; i < fence->pending_updates_count; i++)
        if (virtual_value <= fence->pending_updates[i].virtual_value)
            return fence->pending_updates[i].physical_value;

    FIXME("Cannot find a pending physical wait value. Emitting a noop wait.\n");
    return 0;
}

static HRESULT d3d12_fence_signal(struct d3d12_fence *fence, uint64_t physical_value)
{
    uint64_t new_max_pending_virtual_timeline_value;
    size_t i, j;
    int rc;

    if ((rc = pthread_mutex_lock(&fence->mutex)))
    {
        ERR("Failed to lock mutex, error %d.\n", rc);
        return hresult_from_errno(rc);
    }

    /* In case we have a rewind signalled from GPU, we need to recompute the max pending timeline value. */
    new_max_pending_virtual_timeline_value = 0;

    for (i = 0, j = 0; i < fence->pending_updates_count; i++)
    {
        if (physical_value >= fence->pending_updates[i].physical_value)
        {
            fence->virtual_value = fence->pending_updates[i].virtual_value;
        }
        else
        {
            if (fence->pending_updates[i].virtual_value > new_max_pending_virtual_timeline_value)
                new_max_pending_virtual_timeline_value = fence->pending_updates[i].virtual_value;

            if (i != j)
                memcpy(fence->pending_updates + j, fence->pending_updates + i, sizeof(*fence->pending_updates));
            j++;
        }
    }

    fence->pending_updates_count = j;
    fence->physical_value = physical_value;

    if (fence->virtual_value > new_max_pending_virtual_timeline_value)
        new_max_pending_virtual_timeline_value = fence->virtual_value;

    d3d12_fence_signal_external_events_locked(fence);
    d3d12_fence_update_pending_value_locked(fence, new_max_pending_virtual_timeline_value);
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
        vkd3d_free(fence->pending_updates);
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
    completed_value = fence->virtual_value;
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

    if (value <= fence->virtual_value)
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

static CONST_VTBL struct ID3D12Fence1Vtbl d3d12_fence_vtbl =
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
    fence->virtual_value = initial_value;
    fence->max_pending_virtual_timeline_value = initial_value;
    fence->physical_value = 0;
    fence->counter = 0;
    return vkd3d_create_timeline_semaphore(device, 0, &fence->timeline_semaphore);
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

    fence->pending_updates = NULL;
    fence->pending_updates_count = 0;
    fence->pending_updates_size = 0;

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

    list->vk_init_commands = VK_NULL_HANDLE;
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

static HRESULT d3d12_command_allocator_allocate_init_command_buffer(struct d3d12_command_allocator *allocator,
        struct d3d12_command_list *list)
{
    struct d3d12_device *device = allocator->device;
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    VkCommandBufferAllocateInfo command_buffer_info;
    VkCommandBufferBeginInfo begin_info;
    VkResult vr;

    TRACE("allocator %p, list %p.\n", allocator, list);

    if (list->vk_init_commands)
        return S_OK;

    command_buffer_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    command_buffer_info.pNext = NULL;
    command_buffer_info.commandPool = allocator->vk_command_pool;
    command_buffer_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    command_buffer_info.commandBufferCount = 1;

    if ((vr = VK_CALL(vkAllocateCommandBuffers(device->vk_device, &command_buffer_info,
            &list->vk_init_commands))) < 0)
    {
        WARN("Failed to allocate Vulkan command buffer, vr %d.\n", vr);
        return hresult_from_vk_result(vr);
    }

    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.pNext = NULL;
    begin_info.flags = 0;
    begin_info.pInheritanceInfo = NULL;

    if ((vr = VK_CALL(vkBeginCommandBuffer(list->vk_init_commands, &begin_info))) < 0)
    {
        WARN("Failed to begin command buffer, vr %d.\n", vr);
        VK_CALL(vkFreeCommandBuffers(device->vk_device, allocator->vk_command_pool,
                1, &list->vk_init_commands));
        return hresult_from_vk_result(vr);
    }

    return S_OK;
}

static void d3d12_command_allocator_free_vk_command_buffer(struct d3d12_command_allocator *allocator,
        VkCommandBuffer vk_command_buffer)
{
    struct d3d12_device *device = allocator->device;
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;

    if (!vk_command_buffer)
        return;

    if (!vkd3d_array_reserve((void **)&allocator->command_buffers, &allocator->command_buffers_size,
            allocator->command_buffer_count + 1, sizeof(*allocator->command_buffers)))
    {
        WARN("Failed to add command buffer.\n");
        VK_CALL(vkFreeCommandBuffers(device->vk_device, allocator->vk_command_pool,
                1, &vk_command_buffer));
    }
    else
        allocator->command_buffers[allocator->command_buffer_count++] = vk_command_buffer;
}

static void d3d12_command_allocator_free_command_buffer(struct d3d12_command_allocator *allocator,
        struct d3d12_command_list *list)
{
    TRACE("allocator %p, list %p.\n", allocator, list);

    if (allocator->current_command_list == list)
        allocator->current_command_list = NULL;

    d3d12_command_allocator_free_vk_command_buffer(allocator, list->vk_command_buffer);
    d3d12_command_allocator_free_vk_command_buffer(allocator, list->vk_init_commands);
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
        pool_desc.flags = 0;
        pool_desc.maxSets = 512;
        pool_desc.poolSizeCount = ARRAY_SIZE(pool_sizes);
        pool_desc.pPoolSizes = pool_sizes;

        if (!device->vk_info.EXT_inline_uniform_block ||
                device->vk_info.device_limits.maxPushConstantsSize >= (D3D12_MAX_ROOT_COST * sizeof(uint32_t)))
        {
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
    list->vk_init_commands = VK_NULL_HANDLE;
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

        for (i = 0; i < allocator->scratch_buffer_count; i++)
            d3d12_device_return_scratch_buffer(device, &allocator->scratch_buffers[i]);

        for (i = 0; i < allocator->query_pool_count; i++)
            d3d12_device_return_query_pool(device, &allocator->query_pools[i]);

        vkd3d_free(allocator->scratch_buffers);
        vkd3d_free(allocator->query_pools);
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
            VK_OBJECT_TYPE_COMMAND_POOL, name);
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
    size_t i;

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

    if ((pending = vkd3d_atomic_uint32_load_explicit(&allocator->outstanding_submissions_count, vkd3d_memory_order_acquire)) != 0)
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

    /* Return scratch buffers to the device */
    for (i = 0; i < allocator->scratch_buffer_count; i++)
        d3d12_device_return_scratch_buffer(device, &allocator->scratch_buffers[i]);

    allocator->scratch_buffer_count = 0;

    /* Return query pools to the device */
    for (i = 0; i < allocator->query_pool_count; i++)
        d3d12_device_return_query_pool(device, &allocator->query_pools[i]);

    allocator->query_pool_count = 0;
    memset(&allocator->active_query_pools, 0, sizeof(allocator->active_query_pools));
    return S_OK;
}

static CONST_VTBL struct ID3D12CommandAllocatorVtbl d3d12_command_allocator_vtbl =
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

    allocator->scratch_buffers = NULL;
    allocator->scratch_buffers_size = 0;
    allocator->scratch_buffer_count = 0;

    allocator->query_pools = NULL;
    allocator->query_pools_size = 0;
    allocator->query_pool_count = 0;
    memset(&allocator->active_query_pools, 0, sizeof(allocator->active_query_pools));

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

struct vkd3d_scratch_allocation
{
    VkBuffer buffer;
    VkDeviceSize offset;
    VkDeviceAddress va;
};

static bool d3d12_command_allocator_allocate_scratch_memory(struct d3d12_command_allocator *allocator,
        VkDeviceSize size, VkDeviceSize alignment, struct vkd3d_scratch_allocation *allocation)
{
    VkDeviceSize aligned_offset, aligned_size;
    struct vkd3d_scratch_buffer *scratch;
    unsigned int i;

    aligned_size = align(size, alignment);

    /* Probe last block first since the others are likely full */
    for (i = allocator->scratch_buffer_count; i; i--)
    {
        scratch = &allocator->scratch_buffers[i - 1];
        aligned_offset = align(scratch->offset, alignment);

        if (aligned_offset + aligned_size <= scratch->size)
        {
            scratch->offset = aligned_offset + aligned_size;

            allocation->buffer = scratch->vk_buffer;
            allocation->offset = aligned_offset;
            allocation->va = scratch->va + aligned_offset;
            return true;
        }
    }

    if (!vkd3d_array_reserve((void**)&allocator->scratch_buffers, &allocator->scratch_buffers_size,
            allocator->scratch_buffer_count + 1, sizeof(*allocator->scratch_buffers)))
    {
        ERR("Failed to allocate scratch buffer.\n");
        return false;
    }

    scratch = &allocator->scratch_buffers[allocator->scratch_buffer_count];
    if (FAILED(d3d12_device_get_scratch_buffer(allocator->device, aligned_size, scratch)))
    {
        ERR("Failed to create scratch buffer.\n");
        return false;
    }

    allocator->scratch_buffer_count += 1;
    scratch->offset = aligned_size;

    allocation->buffer = scratch->vk_buffer;
    allocation->offset = 0;
    allocation->va = scratch->va;
    return true;
}

static struct vkd3d_query_pool *d3d12_command_allocator_find_active_query_pool(struct d3d12_command_allocator *allocator,
        D3D12_QUERY_HEAP_TYPE heap_type)
{
    uint32_t i;

    static const struct
    {
        D3D12_QUERY_HEAP_TYPE heap_type;
        uint32_t index;
    }
    map[] =
    {
        { D3D12_QUERY_HEAP_TYPE_OCCLUSION,            VKD3D_QUERY_TYPE_INDEX_OCCLUSION            },
        { D3D12_QUERY_HEAP_TYPE_PIPELINE_STATISTICS,  VKD3D_QUERY_TYPE_INDEX_PIPELINE_STATISTICS  },
        { D3D12_QUERY_HEAP_TYPE_SO_STATISTICS,        VKD3D_QUERY_TYPE_INDEX_TRANSFORM_FEEDBACK   },
    };

    for (i = 0; i < ARRAY_SIZE(map); i++)
    {
        if (map[i].heap_type == heap_type)
            return &allocator->active_query_pools[map[i].index];
    }

    ERR("Unhandled query heap type %u.\n", heap_type);
    return NULL;
}

static bool d3d12_command_allocator_allocate_query(struct d3d12_command_allocator *allocator,
        D3D12_QUERY_HEAP_TYPE heap_type, VkQueryPool *query_pool, uint32_t *query_index)
{
    struct vkd3d_query_pool *pool = d3d12_command_allocator_find_active_query_pool(allocator, heap_type);

    if (!pool)
        return false;

    if (pool->next_index >= pool->query_count)
    {
        if (FAILED(d3d12_device_get_query_pool(allocator->device, heap_type, pool)))
            return false;

        if (vkd3d_array_reserve((void**)&allocator->query_pools, &allocator->query_pools_size,
                allocator->query_pool_count + 1, sizeof(*allocator->query_pools)))
            allocator->query_pools[allocator->query_pool_count++] = *pool;
        else
            ERR("Failed to add query pool.\n");
    }

    *query_pool = pool->vk_query_pool;
    *query_index = pool->next_index++;
    return true;
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

static void d3d12_command_list_invalidate_current_pipeline(struct d3d12_command_list *list, bool meta_shader)
{
    list->current_pipeline = VK_NULL_HANDLE;

    /* If we're binding a meta shader, invalidate everything.
     * Next time we bind a user pipeline, we need to reapply all dynamic state. */
    if (meta_shader)
    {
        list->dynamic_state.active_flags = 0;
        /* For meta shaders, just pretend we never bound anything since we don't do tracking for these pipeline binds. */
        list->command_buffer_pipeline = VK_NULL_HANDLE;
    }
}

static bool d3d12_command_list_create_framebuffer(struct d3d12_command_list *list, VkRenderPass render_pass,
        uint32_t view_count, const VkImageView *views, VkExtent3D extent, VkFramebuffer *vk_framebuffer);

static D3D12_RECT d3d12_get_image_rect(struct d3d12_resource *resource, unsigned int mip_level)
{
    D3D12_RECT rect;
    rect.left = 0;
    rect.top = 0;
    rect.right = d3d12_resource_desc_get_width(&resource->desc, mip_level);
    rect.bottom = d3d12_resource_desc_get_height(&resource->desc, mip_level);
    return rect;
}

static bool d3d12_image_copy_writes_full_subresource(struct d3d12_resource *resource,
        const VkExtent3D *extent, const VkImageSubresourceLayers *subresource)
{
    unsigned int width, height, depth;
    width = d3d12_resource_desc_get_width(&resource->desc, subresource->mipLevel);
    height = d3d12_resource_desc_get_height(&resource->desc, subresource->mipLevel);
    depth = d3d12_resource_desc_get_depth(&resource->desc, subresource->mipLevel);
    return width == extent->width && height == extent->height && depth == extent->depth;
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

static int d3d12_command_list_find_attachment_view(struct d3d12_command_list *list,
        const struct d3d12_resource *resource, const VkImageSubresourceLayers *subresource)
{
    unsigned int i;

    if (list->dsv.resource == resource)
    {
        const struct vkd3d_view *dsv = list->dsv.view;

        if (dsv->info.texture.miplevel_idx == subresource->mipLevel &&
                dsv->info.texture.layer_idx == subresource->baseArrayLayer &&
                dsv->info.texture.layer_count == subresource->layerCount)
            return D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT;
    }
    else
    {
        for (i = 0; i < D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT; i++)
        {
            const struct vkd3d_view *rtv = list->rtvs[i].view;

            if (list->rtvs[i].resource != resource)
                continue;

            if (rtv->info.texture.miplevel_idx == subresource->mipLevel &&
                    rtv->info.texture.layer_idx == subresource->baseArrayLayer &&
                    rtv->info.texture.layer_count == subresource->layerCount)
                return i;
        }
    }

    return -1;
}

static int d3d12_command_list_find_attachment(struct d3d12_command_list *list,
        const struct d3d12_resource *resource, const struct vkd3d_view *view)
{
    VkImageSubresourceLayers subresource = vk_subresource_layers_from_view(view);
    return d3d12_command_list_find_attachment_view(list, resource, &subresource);
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
        full_rect = d3d12_get_image_rect(resource, view->info.texture.miplevel_idx);
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

    extent.width = d3d12_resource_desc_get_width(&resource->desc, view->info.texture.miplevel_idx);
    extent.height = d3d12_resource_desc_get_height(&resource->desc, view->info.texture.miplevel_idx);
    extent.depth = view->info.texture.layer_count;

    if (!d3d12_command_list_create_framebuffer(list, vk_render_pass, 1, &view->vk_image_view, extent, &vk_framebuffer))
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
    attachment->discard_mask &= ~clear_aspects;

    if (clear_aspects & VK_IMAGE_ASPECT_COLOR_BIT)
        attachment->value.color = clear_value->color;

    if (clear_aspects & VK_IMAGE_ASPECT_DEPTH_BIT)
        attachment->value.depthStencil.depth = clear_value->depthStencil.depth;

    if (clear_aspects & VK_IMAGE_ASPECT_STENCIL_BIT)
        attachment->value.depthStencil.stencil = clear_value->depthStencil.stencil;
}

static void d3d12_command_list_discard_attachment_barrier(struct d3d12_command_list *list,
        struct d3d12_resource *resource, const VkImageSubresourceLayers *subresource, bool is_bound)
{
    const struct vkd3d_vk_device_procs *vk_procs = &list->device->vk_procs;
    VkImageMemoryBarrier barrier;
    VkPipelineStageFlags stages;
    VkAccessFlags access;
    VkImageLayout layout;

    /* Ignore read access bits since reads will be undefined anyway */
    if (resource->desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET)
    {
        stages = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        access = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        layout = is_bound ? d3d12_resource_pick_layout(resource, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) : resource->common_layout;
    }
    else if (resource->desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL)
    {
        stages = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        access = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        layout = is_bound && list->dsv_layout ? list->dsv_layout : resource->common_layout;
    }
    else
    {
        ERR("Unsupported resource flags %#x.\n", resource->desc.Flags);
        return;
    }
    if (resource->format->vk_aspect_mask != subresource->aspectMask)
        return;

    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.pNext = NULL;
    barrier.srcAccessMask = access;
    barrier.dstAccessMask = access;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = layout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = resource->vk_image;
    barrier.subresourceRange = vk_subresource_range_from_layers(subresource);

    VK_CALL(vkCmdPipelineBarrier(list->vk_command_buffer,
        stages, stages, 0, 0, NULL, 0, NULL, 1, &barrier));
}

static void d3d12_command_list_discard_attachment_deferred(struct d3d12_command_list *list,
        uint32_t attachment_idx, VkImageAspectFlags discard_aspects)
{
    struct vkd3d_clear_state *clear_state = &list->clear_state;
    struct vkd3d_clear_attachment *attachment = &clear_state->attachments[attachment_idx];

    clear_state->attachment_mask |= 1u << attachment_idx;
    attachment->aspect_mask &= ~discard_aspects;
    attachment->discard_mask |= discard_aspects;
}

static bool d3d12_command_list_has_render_pass_rtv_clear(struct d3d12_command_list *list, unsigned int attachment_idx)
{
    const struct d3d12_graphics_pipeline_state *graphics;

    if (!d3d12_pipeline_state_is_graphics(list->state))
        return false;

    graphics = &list->state->graphics;

    return attachment_idx < graphics->rt_count &&
            !(graphics->null_attachment_mask & (1 << attachment_idx)) &&
            (list->clear_state.attachment_mask & (1 << attachment_idx));
}

static bool d3d12_command_list_has_depth_stencil_view(struct d3d12_command_list *list);

static bool d3d12_command_list_has_render_pass_dsv_clear(struct d3d12_command_list *list)
{
    struct vkd3d_clear_attachment *clear_attachment = &list->clear_state.attachments[D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT];
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
    clear_aspects = clear_attachment->aspect_mask | clear_attachment->discard_mask;
    return (write_aspects & clear_aspects) == clear_aspects;
}

static void d3d12_command_list_emit_deferred_clear(struct d3d12_command_list *list, unsigned int attachment_idx)
{
    struct vkd3d_clear_attachment *clear_attachment = &list->clear_state.attachments[attachment_idx];
    VkImageSubresourceLayers vk_subresource_layers;
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
        if (clear_attachment->aspect_mask)
        {
            d3d12_command_list_clear_attachment_inline(list, resource,
                    view, attachment_idx, clear_attachment->aspect_mask,
                    &clear_attachment->value, 0, NULL);
        }
    }
    else
    {
        if (clear_attachment->aspect_mask)
        {
            d3d12_command_list_clear_attachment_pass(list, resource, view,
                    clear_attachment->aspect_mask, &clear_attachment->value, 0, NULL);
        }

        if (clear_attachment->discard_mask)
        {
            vk_subresource_layers = vk_subresource_layers_from_view(view);

            d3d12_command_list_discard_attachment_barrier(list, resource,
                    &vk_subresource_layers, list->render_pass_suspended);
        }
    }

    clear_attachment->aspect_mask = 0;
    clear_attachment->discard_mask = 0;
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
    vk_barrier->image = resource->vk_image;
    vk_barrier->subresourceRange = vk_subresource_range_from_view(view);
    return stages;
}

static void d3d12_command_list_emit_render_pass_transition(struct d3d12_command_list *list,
        enum vkd3d_render_pass_transition_mode mode)
{
    const struct vkd3d_vk_device_procs *vk_procs = &list->device->vk_procs;
    VkImageMemoryBarrier vk_image_barriers[D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT + 1];
    VkPipelineStageFlags stage_mask = 0;
    struct d3d12_rtv_desc *dsv;
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

static inline bool d3d12_query_type_is_indexed(D3D12_QUERY_TYPE type)
{
    return type >= D3D12_QUERY_TYPE_SO_STATISTICS_STREAM0 &&
            type <= D3D12_QUERY_TYPE_SO_STATISTICS_STREAM3;
}

static VkQueryControlFlags d3d12_query_type_get_vk_flags(D3D12_QUERY_TYPE type)
{
    return type == D3D12_QUERY_TYPE_OCCLUSION
            ? VK_QUERY_CONTROL_PRECISE_BIT : 0;
}

static bool d3d12_command_list_add_pending_query(struct d3d12_command_list *list,
        const struct vkd3d_active_query *query)
{
    if (!vkd3d_array_reserve((void **)&list->pending_queries, &list->pending_queries_size,
            list->pending_queries_count + 1, sizeof(*list->pending_queries)))

    list->pending_queries[list->pending_queries_count++] = *query;
    return true;
}

static void d3d12_command_list_begin_active_query(struct d3d12_command_list *list,
        struct vkd3d_active_query *query)
{
    const struct vkd3d_vk_device_procs *vk_procs = &list->device->vk_procs;
    VkQueryControlFlags flags = d3d12_query_type_get_vk_flags(query->type);

    assert(query->state == VKD3D_ACTIVE_QUERY_RESET);

    if (d3d12_query_type_is_indexed(query->type))
    {
        unsigned int stream = query->type - D3D12_QUERY_TYPE_SO_STATISTICS_STREAM0;
        VK_CALL(vkCmdBeginQueryIndexedEXT(list->vk_command_buffer, query->vk_pool, query->vk_index, flags, stream));
    }
    else
        VK_CALL(vkCmdBeginQuery(list->vk_command_buffer, query->vk_pool, query->vk_index, flags));

    query->state = VKD3D_ACTIVE_QUERY_BEGUN;
}

static void d3d12_command_list_end_active_query(struct d3d12_command_list *list,
        struct vkd3d_active_query *query)
{
    const struct vkd3d_vk_device_procs *vk_procs = &list->device->vk_procs;
    assert(query->state == VKD3D_ACTIVE_QUERY_BEGUN);

    if (d3d12_query_type_is_indexed(query->type))
    {
        unsigned int stream = query->type - D3D12_QUERY_TYPE_SO_STATISTICS_STREAM0;
        VK_CALL(vkCmdEndQueryIndexedEXT(list->vk_command_buffer, query->vk_pool, query->vk_index, stream));
    }
    else
        VK_CALL(vkCmdEndQuery(list->vk_command_buffer, query->vk_pool, query->vk_index));

    query->state = VKD3D_ACTIVE_QUERY_ENDED;
}

static bool d3d12_command_list_enable_query(struct d3d12_command_list *list,
        struct d3d12_query_heap *heap, uint32_t index, D3D12_QUERY_TYPE type)
{
    struct vkd3d_active_query *query;

    if (!vkd3d_array_reserve((void **)&list->active_queries, &list->active_queries_size,
            list->active_queries_count + 1, sizeof(*list->active_queries)))
    {
        ERR("Failed to add query.\n");
        return false;
    }

    query = &list->active_queries[list->active_queries_count++];
    query->heap = heap;
    query->index = index;
    query->type = type;
    query->state = VKD3D_ACTIVE_QUERY_RESET;
    query->vk_pool = heap->vk_query_pool;
    query->vk_index = index;
    return true;

/*    return d3d12_command_allocator_allocate_query(list->allocator,
            heap->desc.Type, &query->vk_pool, &query->vk_index); */
}

static bool d3d12_command_list_disable_query(struct d3d12_command_list *list,
        struct d3d12_query_heap *heap, uint32_t index)
{
    unsigned int i;

    for (i = 0; i < list->active_queries_count; i++)
    {
        struct vkd3d_active_query *query = &list->active_queries[i];

        if (query->vk_pool == vk_query_pool && query->index == index)
        {
            if (query->state == VKD3D_ACTIVE_QUERY_RESET)
                d3d12_command_list_begin_active_query(list, query);
            if (query->state == VKD3D_ACTIVE_QUERY_BEGUN)
                d3d12_command_list_end_active_query(list, query);

            *query = list->active_queries[--list->active_queries_count];
            return;
        }
    }

    WARN("Query (%#"PRIx64",%u) not active.\n", (uint64_t)vk_query_pool, index);
}

static void d3d12_command_list_handle_active_queries(struct d3d12_command_list *list, bool end)
{
    unsigned int i;

    for (i = 0; i < list->active_queries_count; i++)
    {
        struct vkd3d_active_query *query = &list->active_queries[i];

        /* It is possible (although unexpected) that an app uses an occlusion
         * query for multiple draw calls, which may require multiple render
         * pass instances. In that case, we can only handle the first instance. */
        if (query->state == VKD3D_ACTIVE_QUERY_ENDED && !end)
            FIXME("Query (%#"PRIx64",%u) already ended.\n", (uint64_t)query->vk_pool, query->vk_index);

        if (query->state == VKD3D_ACTIVE_QUERY_RESET)
            d3d12_command_list_begin_active_query(list, query);
        if (query->state == VKD3D_ACTIVE_QUERY_BEGUN && end)
            d3d12_command_list_end_active_query(list, query);
    }
}

static void d3d12_command_list_end_current_render_pass(struct d3d12_command_list *list, bool suspend)
{
    const struct vkd3d_vk_device_procs *vk_procs = &list->device->vk_procs;

    d3d12_command_list_handle_active_queries(list, true);

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

    list->render_pass_suspended = suspend && (list->current_render_pass || list->render_pass_suspended);
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

    if (bindings->root_signature->descriptor_table_count)
        bindings->dirty_flags |= VKD3D_PIPELINE_DIRTY_DESCRIPTOR_TABLE_OFFSETS;

    bindings->root_descriptor_dirty_mask =
            bindings->root_signature->root_descriptor_raw_va_mask |
            bindings->root_signature->root_descriptor_push_mask;
    bindings->root_constant_dirty_mask = bindings->root_signature->root_constant_mask;

    if (invalidate_descriptor_heaps)
    {
        struct d3d12_device *device = bindings->root_signature->device;
        bindings->descriptor_heap_dirty_mask = (1ull << device->bindless_state.set_count) - 1;
    }
}

static VkAccessFlags vk_access_flags_all_possible_for_buffer(const struct d3d12_device *device,
        VkQueueFlags vk_queue_flags, bool consider_reads)
{
    /* FIXME: Should use MEMORY_READ/WRITE_BIT here, but older RADV is buggy,
     * and does not consider these access flags at all.
     * Exhaustively enumerate all relevant access flags ...
     * TODO: Should be replaced with MEMORY_READ/WRITE_BIT eventually when
     * we're confident we don't care about older drivers.
     * Driver was fixed around 2020-07-08. */

    VkAccessFlags access = VK_ACCESS_TRANSFER_WRITE_BIT;
    if (consider_reads)
        access |= VK_ACCESS_TRANSFER_READ_BIT;

    if (vk_queue_flags & (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT))
    {
        access |= VK_ACCESS_SHADER_WRITE_BIT;
        if (consider_reads)
            access |= VK_ACCESS_SHADER_READ_BIT;
    }

    if ((vk_queue_flags & VK_QUEUE_GRAPHICS_BIT) && device->vk_info.EXT_transform_feedback)
    {
        access |= VK_ACCESS_TRANSFORM_FEEDBACK_COUNTER_WRITE_BIT_EXT |
                  VK_ACCESS_TRANSFORM_FEEDBACK_WRITE_BIT_EXT;
        if (consider_reads)
            access |= VK_ACCESS_TRANSFORM_FEEDBACK_COUNTER_READ_BIT_EXT;
    }

    if (consider_reads && (vk_queue_flags & VK_QUEUE_GRAPHICS_BIT) &&
            device->device_info.conditional_rendering_features.conditionalRendering)
        access |= VK_ACCESS_CONDITIONAL_RENDERING_READ_BIT_EXT;

    if (consider_reads && (vk_queue_flags & VK_QUEUE_GRAPHICS_BIT))
    {
        access |= VK_ACCESS_UNIFORM_READ_BIT |
                  VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT |
                  VK_ACCESS_INDEX_READ_BIT |
                  VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
    }

    return access;
}

static VkAccessFlags vk_access_flags_all_possible_for_image(const struct d3d12_device *device,
        D3D12_RESOURCE_FLAGS flags, VkQueueFlags vk_queue_flags, bool consider_reads)
{
    /* Should use MEMORY_READ/WRITE_BIT here, but current RADV is buggy,
     * and does not consider these access flags at all.
     * Exhaustively enumerate all relevant access flags ... */

    VkAccessFlags access = 0;
    if (flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS)
    {
        access |= VK_ACCESS_SHADER_WRITE_BIT;
        if (consider_reads)
            access |= VK_ACCESS_SHADER_READ_BIT;
    }

    if (consider_reads && !(flags & D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE))
        access |= VK_ACCESS_SHADER_READ_BIT;

    /* Copies, render targets and resolve related operations are handled specifically on images elsewhere.
     * The only possible access flags for images in common layouts are SHADER_READ/WRITE. */

    return access;
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
        if (d3d12_resource_is_buffer(resource))
            *access |= vk_access_flags_all_possible_for_buffer(device, vk_queue_flags, true);
        else
            *access |= vk_access_flags_all_possible_for_image(device, resource->desc.Flags, vk_queue_flags, true);
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

                if (device->bindless_state.flags & (VKD3D_BINDLESS_CBV_AS_SSBO | VKD3D_RAW_VA_ROOT_DESCRIPTOR_CBV))
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

                /* D3D12_RESOURCE_STATE_PREDICATION */
                if (device->device_info.buffer_device_address_features.bufferDeviceAddress)
                {
                    *stages |= VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
                    *access |= VK_ACCESS_SHADER_READ_BIT;
                }
                else
                {
                    *stages |= VK_PIPELINE_STAGE_TRANSFER_BIT;
                    *access |= VK_ACCESS_TRANSFER_READ_BIT;
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

static void d3d12_command_list_add_transition(struct d3d12_command_list *list, struct vkd3d_initial_transition *transition)
{
    bool skip;
    size_t i;

    /* Search in reverse as we're more likely to use same resource again. */
    for (i = list->init_transitions_count; i; i--)
    {
        if (list->init_transitions[i - 1].type != transition->type)
            continue;

        switch (transition->type)
        {
            case VKD3D_INITIAL_TRANSITION_TYPE_RESOURCE:
                skip = list->init_transitions[i - 1].resource.resource == transition->resource.resource;
                break;

            case VKD3D_INITIAL_TRANSITION_TYPE_QUERY_HEAP:
                skip = list->init_transitions[i - 1].query_heap == transition->query_heap;
                break;

            default:
                ERR("Unhandled transition type %u.\n", transition->type);
                continue;
        }

        if (skip)
            return;
    }

    if (!vkd3d_array_reserve((void**)&list->init_transitions, &list->init_transitions_size,
            list->init_transitions_count + 1, sizeof(*list->init_transitions)))
    {
        ERR("Failed to allocate memory.\n");
        return;
    }

    switch (transition->type)
    {
        case VKD3D_INITIAL_TRANSITION_TYPE_RESOURCE:
            TRACE("Adding initial resource transition for resource %p (%s).\n",
                  transition->resource.resource, transition->resource.perform_initial_transition ? "yes" : "no");
            break;

        case VKD3D_INITIAL_TRANSITION_TYPE_QUERY_HEAP:
            TRACE("Adding initialization for query heap %p.\n", transition->query_heap);
            break;

        default:
            ERR("Unhandled transition type %u.\n", transition->type);
    }

    list->init_transitions[list->init_transitions_count++] = *transition;
}

static void d3d12_command_list_track_resource_usage(struct d3d12_command_list *list,
        struct d3d12_resource *resource, bool perform_initial_transition)
{
    struct vkd3d_initial_transition transition;

    /* When a command queue has confirmed that it has received a command list for submission, this flag will eventually
     * be cleared. The command queue will only perform the transition once.
     * Until that point, we must keep submitting initial transitions like this. */
    if (vkd3d_atomic_uint32_load_explicit(&resource->initial_layout_transition, vkd3d_memory_order_relaxed))
    {
        transition.type = VKD3D_INITIAL_TRANSITION_TYPE_RESOURCE;
        transition.resource.resource = resource;
        transition.resource.perform_initial_transition = perform_initial_transition;
        d3d12_command_list_add_transition(list, &transition);
    }
}

static void d3d12_command_list_track_query_heap(struct d3d12_command_list *list,
        struct d3d12_query_heap *heap)
{
    struct vkd3d_initial_transition transition;

    if (!vkd3d_atomic_uint32_load_explicit(&heap->initialized, vkd3d_memory_order_relaxed))
    {
        transition.type = VKD3D_INITIAL_TRANSITION_TYPE_QUERY_HEAP;
        transition.query_heap = heap;
        d3d12_command_list_add_transition(list, &transition);
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

    TRACE("%p decreasing refcount to %u.\n", list, refcount);

    if (!refcount)
    {
        struct d3d12_device *device = list->device;

        vkd3d_private_store_destroy(&list->private_store);

        /* When command pool is destroyed, all command buffers are implicitly freed. */
        if (list->allocator)
            d3d12_command_allocator_free_command_buffer(list->allocator, list);

        vkd3d_free(list->init_transitions);
        vkd3d_free(list->query_ranges);
        vkd3d_free(list->active_queries);
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

static HRESULT d3d12_command_list_batch_reset_query_pools(struct d3d12_command_list *list)
{
    const struct vkd3d_vk_device_procs *vk_procs = &list->device->vk_procs;
    HRESULT hr;
    size_t i;

    for (i = 0; i < list->query_ranges_count; i++)
    {
        const struct vkd3d_query_range *range = &list->query_ranges[i];

        if (!(range->flags & VKD3D_QUERY_RANGE_RESET))
            continue;

        if (FAILED(hr = d3d12_command_allocator_allocate_init_command_buffer(list->allocator, list)))
            return hr;

        VK_CALL(vkCmdResetQueryPool(list->vk_init_commands,
                range->vk_pool, range->index, range->count));
    }

    return S_OK;
}

static HRESULT d3d12_command_list_build_init_commands(struct d3d12_command_list *list)
{
    const struct vkd3d_vk_device_procs *vk_procs = &list->device->vk_procs;
    VkResult vr;
    HRESULT hr;

    if (FAILED(hr = d3d12_command_list_batch_reset_query_pools(list)))
        return hr;

    if (!list->vk_init_commands)
        return S_OK;

    if ((vr = VK_CALL(vkEndCommandBuffer(list->vk_init_commands))) < 0)
    {
        WARN("Failed to end command buffer, vr %d.\n", vr);
        return hresult_from_vk_result(vr);
    }

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d3d12_command_list_Close(d3d12_command_list_iface *iface)
{
    struct d3d12_command_list *list = impl_from_ID3D12GraphicsCommandList(iface);
    const struct vkd3d_vk_device_procs *vk_procs = &list->device->vk_procs;
    VkResult vr;
    HRESULT hr;

    TRACE("iface %p.\n", iface);

    if (!list->is_recording)
    {
        WARN("Command list is not in the recording state.\n");
        return E_FAIL;
    }

    d3d12_command_list_end_current_render_pass(list, false);

    if (list->predicate_enabled)
        VK_CALL(vkCmdEndConditionalRenderingEXT(list->vk_command_buffer));

    vkd3d_shader_debug_ring_end_command_buffer(list);

    if (FAILED(hr = d3d12_command_list_build_init_commands(list)))
        return hr;

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

static bool d3d12_command_list_find_query(struct d3d12_command_list *list,
        VkQueryPool vk_pool, uint32_t index, size_t *out_pos)
{
    const struct vkd3d_query_range *range;
    size_t hi = list->query_ranges_count;
    size_t lo = 0;

    while (lo < hi)
    {
        size_t pos = lo + (hi - lo) / 2;
        range = &list->query_ranges[pos];

        if (vk_pool < range->vk_pool)
            hi = pos;
        else if (vk_pool > range->vk_pool)
            lo = pos + 1;
        else if (index < range->index)
            hi = pos;
        else if (index >= range->index + range->count)
            lo = pos + 1;
        else
        {
            if (out_pos)
                *out_pos = pos;
            return true;
        }
    }

    if (out_pos)
        *out_pos = lo;
    return false;
}

static void d3d12_command_list_insert_query_range(struct d3d12_command_list *list, size_t *where,
        VkQueryPool vk_pool, uint32_t index, uint32_t count, uint32_t flags)
{
    struct vkd3d_query_range *range;
    unsigned int move_count;
    bool merge_lo, merge_hi;
    size_t pos = *where;

    merge_lo = false;
    merge_hi = false;

    if (pos > 0)
    {
        range = &list->query_ranges[pos - 1];
        merge_lo = range->flags == flags
                && range->index + range->count == index;
    }

    if (pos < list->query_ranges_count)
    {
        range = &list->query_ranges[pos];
        merge_hi = range->flags == flags
                && range->index == index + count;
    }

    /* The idea is that 'where' will point to the range that contains
     * the original range it was pointing to before the insertion, which
     * may be moved around depending on which ranges get merged. */
    if (merge_lo)
    {
        range = &list->query_ranges[pos - 1];
        range[0].count += count;

        if (merge_hi)
        {
            range[0].count += range[1].count;
            move_count = (--list->query_ranges_count) - pos;
            memmove(&range[1], &range[2], sizeof(*range) * move_count);
            (*where)--;
        }
    }
    else if (merge_hi)
    {
        range = &list->query_ranges[pos];
        range->index = index;
        range->count += count;
    }
    else
    {
        vkd3d_array_reserve((void**)&list->query_ranges, &list->query_ranges_size,
                list->query_ranges_count + 1, sizeof(*list->query_ranges));

        range = &list->query_ranges[pos];
        move_count = (list->query_ranges_count++) - pos;
        memmove(range + 1, range, sizeof(*range) * move_count);

        range->vk_pool = vk_pool;
        range->index = index;
        range->count = count;
        range->flags = flags;

        (*where)++;
    }
}

static void d3d12_command_list_read_query_range(struct d3d12_command_list *list,
        VkQueryPool vk_pool, uint32_t index, uint32_t count)
{
    const struct vkd3d_query_range *range;
    size_t hi = index + count;
    size_t lo = index;
    size_t pos;

    /* pos contains either the location of an existing range
     * containing the first query of the new range, or the
     * location where we need to insert it */
    d3d12_command_list_find_query(list, vk_pool, index, &pos);

    /* Avoid overriding already existing ranges by splitting
     * this range into pieces so that each query is contained
     * in at most one range. */
    while (lo < hi)
    {
        if (pos < list->query_ranges_count)
        {
            range = &list->query_ranges[pos];

            if (lo >= range->index)
            {
                lo = max(lo, range->index + range->count);
                pos += 1;
            }
            else
            {
                size_t range_end = min(hi, range->index);
                d3d12_command_list_insert_query_range(list, &pos,
                        vk_pool, lo, range_end - lo, 0);
                lo = range_end;
            }
        }
        else
        {
            d3d12_command_list_insert_query_range(list, &pos,
                    vk_pool, lo, hi - lo, 0);
            lo = hi;
        }
    }
}

static bool d3d12_command_list_reset_query(struct d3d12_command_list *list,
        VkQueryPool vk_pool, uint32_t index)
{
    size_t pos;

    if (d3d12_command_list_find_query(list, vk_pool, index, &pos))
        return false;

    d3d12_command_list_insert_query_range(list, &pos,
            vk_pool, index, 1, VKD3D_QUERY_RANGE_RESET);
    return true;
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
    list->render_pass_suspended = false;

    list->predicate_enabled = false;
    list->predicate_va = 0;

#ifdef VKD3D_ENABLE_RENDERDOC
    list->debug_capture = vkd3d_renderdoc_active() && vkd3d_renderdoc_should_capture_shader_hash(0);
#else
    list->debug_capture = false;
#endif
    list->has_replaced_shaders = false;
    list->has_valid_index_buffer = false;

    list->current_framebuffer = VK_NULL_HANDLE;
    list->current_pipeline = VK_NULL_HANDLE;
    list->command_buffer_pipeline = VK_NULL_HANDLE;
    list->pso_render_pass = VK_NULL_HANDLE;
    list->current_render_pass = VK_NULL_HANDLE;

    memset(&list->dynamic_state, 0, sizeof(list->dynamic_state));
    list->dynamic_state.blend_constants[0] = D3D12_DEFAULT_BLEND_FACTOR_RED;
    list->dynamic_state.blend_constants[1] = D3D12_DEFAULT_BLEND_FACTOR_GREEN;
    list->dynamic_state.blend_constants[2] = D3D12_DEFAULT_BLEND_FACTOR_BLUE;
    list->dynamic_state.blend_constants[3] = D3D12_DEFAULT_BLEND_FACTOR_ALPHA;

    list->dynamic_state.min_depth_bounds = 0.0f;
    list->dynamic_state.max_depth_bounds = 1.0f;

    list->dynamic_state.primitive_topology = D3D_PRIMITIVE_TOPOLOGY_POINTLIST;
    list->dynamic_state.vk_primitive_topology = VK_PRIMITIVE_TOPOLOGY_POINT_LIST;

    memset(list->pipeline_bindings, 0, sizeof(list->pipeline_bindings));
    memset(list->descriptor_heaps, 0, sizeof(list->descriptor_heaps));

    list->state = NULL;

    list->descriptor_updates_count = 0;

    memset(list->so_counter_buffers, 0, sizeof(list->so_counter_buffers));
    memset(list->so_counter_buffer_offsets, 0, sizeof(list->so_counter_buffer_offsets));

    list->init_transitions_count = 0;
    list->query_ranges_count = 0;
    list->active_queries_count = 0;

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
    graphics = &list->state->graphics;

    return graphics->dsv_format || (d3d12_pipeline_state_has_unknown_dsv_format(list->state) && list->dsv.format);
}

static void d3d12_command_list_get_fb_extent(struct d3d12_command_list *list,
        uint32_t *width, uint32_t *height, uint32_t *layer_count)
{
    struct d3d12_graphics_pipeline_state *graphics = &list->state->graphics;
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

    graphics = &list->state->graphics;

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

        views[view_count++] = list->rtvs[i].view->vk_image_view;
    }

    if (d3d12_command_list_has_depth_stencil_view(list))
    {
        if (!list->dsv.view)
        {
            FIXME("Invalid DSV.\n");
            return false;
        }

        views[view_count++] = list->dsv.view->vk_image_view;
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

    if (list->command_buffer_pipeline != list->state->compute.vk_pipeline)
    {
        VK_CALL(vkCmdBindPipeline(list->vk_command_buffer, list->state->vk_bind_point,
                list->state->compute.vk_pipeline));
        list->command_buffer_pipeline = list->state->compute.vk_pipeline;
    }
    list->current_pipeline = list->state->compute.vk_pipeline;
    list->dynamic_state.active_flags = 0;

    return true;
}

static void d3d12_command_list_check_vbo_alignment(struct d3d12_command_list *list)
{
    const uint32_t *stride_masks;
    VkDeviceSize *offsets;
    uint32_t update_vbos;
    unsigned int index;

    stride_masks = list->state->graphics.vertex_buffer_stride_align_mask;
    update_vbos = list->state->graphics.vertex_buffer_mask;
    offsets = list->dynamic_state.vertex_offsets;

    while (update_vbos)
    {
        index = vkd3d_bitmask_iter32(&update_vbos);
        if (stride_masks[index] & offsets[index])
            list->dynamic_state.dirty_vbos |= 1u << index;
    }
}

static bool d3d12_command_list_update_graphics_pipeline(struct d3d12_command_list *list)
{
    const struct vkd3d_vk_device_procs *vk_procs = &list->device->vk_procs;
    VkRenderPass vk_render_pass;
    uint32_t new_active_flags;
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

    /* Try to grab the pipeline we compiled ahead of time. If we cannot do so, fall back. */
    if (!(vk_pipeline = d3d12_pipeline_state_get_pipeline(list->state,
            &list->dynamic_state, dsv_format, &vk_render_pass, &new_active_flags)))
    {
        if (!(vk_pipeline = d3d12_pipeline_state_get_or_create_pipeline(list->state,
                &list->dynamic_state, dsv_format,
                &vk_render_pass, &new_active_flags)))
            return false;
    }

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
        list->dsv_layout = list->state->graphics.dsv_layout;
    }

    if (list->command_buffer_pipeline != vk_pipeline)
    {
        VK_CALL(vkCmdBindPipeline(list->vk_command_buffer, list->state->vk_bind_point, vk_pipeline));

        /* If we bind a new pipeline, make sure that we end up binding VBOs that are aligned.
         * It is fine to do it here, since we are binding a pipeline right before we perform
         * a draw call. If we trip any dirty check here, VBO offsets will be fixed up when emitting
         * dynamic state after this. */
        d3d12_command_list_check_vbo_alignment(list);

        /* The application did set vertex buffers that we didn't bind because of the pipeline vbo mask.
         * The new pipeline could use those so we need to rebind vertex buffers. */
        if ((new_active_flags & (VKD3D_DYNAMIC_STATE_VERTEX_BUFFER | VKD3D_DYNAMIC_STATE_VERTEX_BUFFER_STRIDE))
          && (list->dynamic_state.dirty_vbos || list->dynamic_state.dirty_vbo_strides))
          list->dynamic_state.dirty_flags |= VKD3D_DYNAMIC_STATE_VERTEX_BUFFER | VKD3D_DYNAMIC_STATE_VERTEX_BUFFER_STRIDE;

        /* Reapply all dynamic states that were not dynamic in previously bound pipeline.
         * If we didn't use to have dynamic vertex strides, but we then bind a pipeline with dynamic strides,
         * we will need to rebind all VBOs. Mark dynamic stride as dirty in this case. */
        if (new_active_flags & ~list->dynamic_state.active_flags & VKD3D_DYNAMIC_STATE_VERTEX_BUFFER_STRIDE)
            list->dynamic_state.dirty_vbo_strides = ~0u;
        list->dynamic_state.dirty_flags |= new_active_flags & ~list->dynamic_state.active_flags;
        list->command_buffer_pipeline = vk_pipeline;
    }

    list->dynamic_state.active_flags = new_active_flags;
    list->current_pipeline = vk_pipeline;
    return true;
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

static bool vk_write_descriptor_set_from_root_descriptor(struct d3d12_command_list *list,
        VkWriteDescriptorSet *vk_descriptor_write, const struct d3d12_root_parameter *root_parameter,
        VkDescriptorSet vk_descriptor_set, const struct vkd3d_root_descriptor_info *descriptor)
{
    bool is_buffer, is_defined;

    is_buffer = descriptor->vk_descriptor_type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER
            || descriptor->vk_descriptor_type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    is_defined = is_buffer ? !!descriptor->info.buffer.buffer : !!descriptor->info.buffer_view;

    vk_descriptor_write->sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    vk_descriptor_write->pNext = NULL;
    vk_descriptor_write->dstSet = vk_descriptor_set;
    vk_descriptor_write->dstBinding = root_parameter->descriptor.binding->binding.binding;
    vk_descriptor_write->dstArrayElement = 0;
    vk_descriptor_write->descriptorType = descriptor->vk_descriptor_type;
    vk_descriptor_write->descriptorCount = 1;
    vk_descriptor_write->pImageInfo = NULL;
    vk_descriptor_write->pBufferInfo = &descriptor->info.buffer;
    vk_descriptor_write->pTexelBufferView = &descriptor->info.buffer_view;
    return is_defined || list->device->device_info.robustness2_features.nullDescriptor;
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

union root_parameter_data
{
    uint32_t root_constants[D3D12_MAX_ROOT_COST];
    VkDeviceAddress root_descriptor_vas[D3D12_MAX_ROOT_COST / 2];
};

static unsigned int d3d12_command_list_fetch_root_descriptor_vas(struct d3d12_command_list *list,
        VkPipelineBindPoint bind_point, union root_parameter_data *dst_data)
{
    struct vkd3d_pipeline_bindings *bindings = &list->pipeline_bindings[bind_point];
    const struct d3d12_root_signature *root_signature = bindings->root_signature;
    uint64_t root_descriptor_mask = root_signature->root_descriptor_raw_va_mask;
    unsigned int va_idx = 0;

    /* Ignore dirty mask. We'll always update all VAs either via push constants
     * in order to reduce API calls, or an inline uniform buffer in which case
     * we need to re-upload all data anyway. */
    while (root_descriptor_mask)
    {
        unsigned int root_parameter_index = vkd3d_bitmask_iter64(&root_descriptor_mask);
        dst_data->root_descriptor_vas[va_idx++] = bindings->root_descriptors[root_parameter_index].info.va;
    }

    return va_idx;
}

static void d3d12_command_list_fetch_inline_uniform_block_data(struct d3d12_command_list *list,
        VkPipelineBindPoint bind_point, union root_parameter_data *dst_data)
{
    struct vkd3d_pipeline_bindings *bindings = &list->pipeline_bindings[bind_point];
    const struct d3d12_root_signature *root_signature = bindings->root_signature;
    uint64_t root_constant_mask = root_signature->root_constant_mask;
    const uint32_t *src_data = bindings->root_constants;
    const struct d3d12_root_descriptor_table *table;
    const struct d3d12_root_constant *root_constant;
    const struct d3d12_desc *base_descriptor;
    unsigned int root_parameter_index;
    uint64_t descriptor_table_mask;
    uint32_t first_table_offset;

    /* Root descriptors are already filled in dst_data. */

    while (root_constant_mask)
    {
        root_parameter_index = vkd3d_bitmask_iter64(&root_constant_mask);
        root_constant = root_signature_get_32bit_constants(root_signature, root_parameter_index);

        memcpy(&dst_data->root_constants[root_constant->constant_index],
                &src_data[root_constant->constant_index],
                root_constant->constant_count * sizeof(uint32_t));
    }

    first_table_offset = root_signature->descriptor_table_offset / sizeof(uint32_t);
    descriptor_table_mask = root_signature->descriptor_table_mask & bindings->descriptor_table_active_mask;

    while (descriptor_table_mask)
    {
        root_parameter_index = vkd3d_bitmask_iter64(&descriptor_table_mask);
        base_descriptor = d3d12_desc_from_gpu_handle(bindings->descriptor_tables[root_parameter_index]);

        table = root_signature_get_descriptor_table(root_signature, root_parameter_index);

        dst_data->root_constants[first_table_offset + table->table_index] = d3d12_desc_heap_offset(base_descriptor);
    }

    /* Reset dirty flags to avoid redundant updates in the future */
    bindings->dirty_flags &= ~VKD3D_PIPELINE_DIRTY_DESCRIPTOR_TABLE_OFFSETS;
    bindings->root_constant_dirty_mask = 0;
}

static void d3d12_command_list_update_root_descriptors(struct d3d12_command_list *list,
        VkPipelineBindPoint bind_point)
{
    struct vkd3d_pipeline_bindings *bindings = &list->pipeline_bindings[bind_point];
    const struct d3d12_root_signature *root_signature = bindings->root_signature;
    const struct vkd3d_vk_device_procs *vk_procs = &list->device->vk_procs;
    VkWriteDescriptorSetInlineUniformBlockEXT inline_uniform_block_write;
    VkWriteDescriptorSet descriptor_writes[D3D12_MAX_ROOT_COST / 2 + 2];
    const struct d3d12_root_parameter *root_parameter;
    VkDescriptorSet descriptor_set = VK_NULL_HANDLE;
    union root_parameter_data root_parameter_data;
    unsigned int descriptor_write_count = 0;
    unsigned int root_parameter_index;
    unsigned int va_count = 0;
    uint64_t dirty_push_mask;

    if (root_signature->flags & VKD3D_ROOT_SIGNATURE_USE_ROOT_DESCRIPTOR_SET)
    {
        /* Ensure that we populate all descriptors if push descriptors cannot be used */
        bindings->root_descriptor_dirty_mask |=
                bindings->root_descriptor_active_mask &
                (root_signature->root_descriptor_raw_va_mask | root_signature->root_descriptor_push_mask);

        descriptor_set = d3d12_command_allocator_allocate_descriptor_set(
                list->allocator, root_signature->vk_root_descriptor_layout, VKD3D_DESCRIPTOR_POOL_TYPE_STATIC);
    }

    if (bindings->root_descriptor_dirty_mask)
    {
        /* If any raw VA descriptor is dirty, we need to update all of them. */
        if (root_signature->root_descriptor_raw_va_mask & bindings->root_descriptor_dirty_mask)
            va_count = d3d12_command_list_fetch_root_descriptor_vas(list, bind_point, &root_parameter_data);

        /* TODO bind null descriptors for inactive root descriptors. */
        dirty_push_mask =
                bindings->root_descriptor_dirty_mask &
                root_signature->root_descriptor_push_mask &
                bindings->root_descriptor_active_mask;

        while (dirty_push_mask)
        {
            root_parameter_index = vkd3d_bitmask_iter64(&dirty_push_mask);
            root_parameter = root_signature_get_root_descriptor(root_signature, root_parameter_index);

            if (!vk_write_descriptor_set_from_root_descriptor(list,
                    &descriptor_writes[descriptor_write_count], root_parameter,
                    descriptor_set, &bindings->root_descriptors[root_parameter_index]))
                continue;

            descriptor_write_count += 1;
        }

        bindings->root_descriptor_dirty_mask = 0;
    }

    if (root_signature->flags & VKD3D_ROOT_SIGNATURE_USE_INLINE_UNIFORM_BLOCK)
    {
        d3d12_command_list_fetch_inline_uniform_block_data(list, bind_point, &root_parameter_data);

        vk_write_descriptor_set_and_inline_uniform_block(&descriptor_writes[descriptor_write_count],
                &inline_uniform_block_write, descriptor_set, root_signature, &root_parameter_data);

        descriptor_write_count += 1;
    }
    else if (va_count)
    {
        VK_CALL(vkCmdPushConstants(list->vk_command_buffer,
                root_signature->vk_pipeline_layout, VK_SHADER_STAGE_ALL,
                0, va_count * sizeof(*root_parameter_data.root_descriptor_vas),
                root_parameter_data.root_descriptor_vas));
    }

    if (!descriptor_write_count)
        return;

    if (root_signature->flags & VKD3D_ROOT_SIGNATURE_USE_ROOT_DESCRIPTOR_SET)
    {
        VK_CALL(vkUpdateDescriptorSets(list->device->vk_device,
                descriptor_write_count, descriptor_writes, 0, NULL));
        VK_CALL(vkCmdBindDescriptorSets(list->vk_command_buffer, bind_point,
                root_signature->vk_pipeline_layout, root_signature->root_descriptor_set,
                1, &descriptor_set, 0, NULL));
    }
    else
    {
        VK_CALL(vkCmdPushDescriptorSetKHR(list->vk_command_buffer, bind_point,
                root_signature->vk_pipeline_layout, root_signature->root_descriptor_set,
                descriptor_write_count, descriptor_writes));
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

    if (rs->flags & VKD3D_ROOT_SIGNATURE_USE_INLINE_UNIFORM_BLOCK)
    {
        /* Root constants and descriptor table offsets are part of the root descriptor set */
        if (bindings->root_descriptor_dirty_mask || bindings->root_constant_dirty_mask
                || (bindings->dirty_flags & VKD3D_PIPELINE_DIRTY_DESCRIPTOR_TABLE_OFFSETS))
            d3d12_command_list_update_root_descriptors(list, bind_point);
    }
    else
    {
        if (bindings->root_descriptor_dirty_mask)
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
    const uint32_t *stride_align_masks;
    struct vkd3d_bitmask_range range;
    uint32_t update_vbos;
    unsigned int i;

    /* Make sure we only update states that are dynamic in the pipeline */
    dyn_state->dirty_flags &= list->dynamic_state.active_flags;

    if (dyn_state->dirty_flags & VKD3D_DYNAMIC_STATE_VIEWPORT_COUNT)
    {
        VK_CALL(vkCmdSetViewportWithCountEXT(list->vk_command_buffer,
                dyn_state->viewport_count, dyn_state->viewports));
    }
    else if (dyn_state->dirty_flags & VKD3D_DYNAMIC_STATE_VIEWPORT)
    {
        VK_CALL(vkCmdSetViewport(list->vk_command_buffer,
                0, dyn_state->viewport_count, dyn_state->viewports));
    }

    if (dyn_state->dirty_flags & VKD3D_DYNAMIC_STATE_SCISSOR_COUNT)
    {
        VK_CALL(vkCmdSetScissorWithCountEXT(list->vk_command_buffer,
                dyn_state->viewport_count, dyn_state->scissors));
    }
    else if (dyn_state->dirty_flags & VKD3D_DYNAMIC_STATE_SCISSOR)
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

    if (dyn_state->dirty_flags & VKD3D_DYNAMIC_STATE_TOPOLOGY)
    {
        VK_CALL(vkCmdSetPrimitiveTopologyEXT(list->vk_command_buffer,
                dyn_state->vk_primitive_topology));
    }

    if (dyn_state->dirty_flags & VKD3D_DYNAMIC_STATE_VERTEX_BUFFER_STRIDE)
    {
        update_vbos = (dyn_state->dirty_vbos | dyn_state->dirty_vbo_strides) & list->state->graphics.vertex_buffer_mask;
        dyn_state->dirty_vbos &= ~update_vbos;
        dyn_state->dirty_vbo_strides &= ~update_vbos;
        stride_align_masks = list->state->graphics.vertex_buffer_stride_align_mask;

        while (update_vbos)
        {
            range = vkd3d_bitmask_iter32_range(&update_vbos);

            for (i = 0; i < range.count; i++)
            {
                if (dyn_state->vertex_offsets[i + range.offset] & stride_align_masks[i + range.offset])
                {
                    FIXME("Binding VBO at offset %"PRIu64", but required alignment is %u.\n",
                          dyn_state->vertex_offsets[i + range.offset],
                          stride_align_masks[i + range.offset] + 1);

                    /* This modifies global state, but if app hits this, it's already buggy. */
                    dyn_state->vertex_offsets[i + range.offset] &= ~(VkDeviceSize)stride_align_masks[i + range.offset];
                }

                if (dyn_state->vertex_strides[i + range.offset] & stride_align_masks[i + range.offset])
                {
                    FIXME("Binding VBO with stride %"PRIu64", but required alignment is %u.\n",
                          dyn_state->vertex_strides[i + range.offset],
                          stride_align_masks[i + range.offset] + 1);

                    /* This modifies global state, but if app hits this, it's already buggy.
                     * Round up, so that we don't hit offset > size case with dynamic strides. */
                    dyn_state->vertex_strides[i + range.offset] =
                            (dyn_state->vertex_strides[i + range.offset] + stride_align_masks[i + range.offset]) &
                            ~(VkDeviceSize)stride_align_masks[i + range.offset];
                }
            }

            VK_CALL(vkCmdBindVertexBuffers2EXT(list->vk_command_buffer,
                    range.offset, range.count,
                    dyn_state->vertex_buffers + range.offset,
                    dyn_state->vertex_offsets + range.offset,
                    dyn_state->vertex_sizes + range.offset,
                    dyn_state->vertex_strides + range.offset));
        }
    }
    else if (dyn_state->dirty_flags & VKD3D_DYNAMIC_STATE_VERTEX_BUFFER)
    {
        update_vbos = dyn_state->dirty_vbos & list->state->graphics.vertex_buffer_mask;
        dyn_state->dirty_vbos &= ~update_vbos;
        dyn_state->dirty_vbo_strides &= ~update_vbos;
        stride_align_masks = list->state->graphics.vertex_buffer_stride_align_mask;

        while (update_vbos)
        {
            range = vkd3d_bitmask_iter32_range(&update_vbos);

            for (i = 0; i < range.count; i++)
            {
                if (dyn_state->vertex_offsets[i + range.offset] & stride_align_masks[i + range.offset])
                {
                    FIXME("Binding VBO at offset %"PRIu64", but required alignment is %u.\n",
                          dyn_state->vertex_offsets[i + range.offset],
                          stride_align_masks[i + range.offset] + 1);
                    dyn_state->vertex_offsets[i + range.offset] &= ~(VkDeviceSize)stride_align_masks[i + range.offset];
                }

                if (list->device->device_info.extended_dynamic_state_features.extendedDynamicState)
                {
                    if (dyn_state->vertex_strides[i + range.offset] & stride_align_masks[i + range.offset])
                    {
                        FIXME("Binding VBO with stride %"PRIu64", but required alignment is %u.\n",
                              dyn_state->vertex_strides[i + range.offset],
                              stride_align_masks[i + range.offset] + 1);

                        /* This modifies global state, but if app hits this, it's already buggy.
                         * Round up, so that we don't hit offset > size case with dynamic strides. */
                        dyn_state->vertex_strides[i + range.offset] =
                                (dyn_state->vertex_strides[i + range.offset] + stride_align_masks[i + range.offset]) &
                                ~(VkDeviceSize)stride_align_masks[i + range.offset];
                    }
                }
            }

            if (list->device->device_info.extended_dynamic_state_features.extendedDynamicState)
            {
                VK_CALL(vkCmdBindVertexBuffers2EXT(list->vk_command_buffer,
                        range.offset, range.count,
                        dyn_state->vertex_buffers + range.offset,
                        dyn_state->vertex_offsets + range.offset,
                        dyn_state->vertex_sizes + range.offset,
                        NULL));
            }
            else
            {
                VK_CALL(vkCmdBindVertexBuffers(list->vk_command_buffer,
                        range.offset, range.count,
                        dyn_state->vertex_buffers + range.offset,
                        dyn_state->vertex_offsets + range.offset));
            }
        }
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
    {
        d3d12_command_list_handle_active_queries(list, false);
        return true;
    }

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

    graphics = &list->state->graphics;
    if (graphics->xfb_enabled)
    {
        VK_CALL(vkCmdBeginTransformFeedbackEXT(list->vk_command_buffer, 0, ARRAY_SIZE(list->so_counter_buffers),
                list->so_counter_buffers, list->so_counter_buffer_offsets));

        list->xfb_enabled = true;
    }

    d3d12_command_list_handle_active_queries(list, false);
    return true;
}

static void d3d12_command_list_check_index_buffer_strip_cut_value(struct d3d12_command_list *list)
{
    struct d3d12_graphics_pipeline_state *graphics = &list->state->graphics;
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

static bool d3d12_command_list_emit_predicated_command(struct d3d12_command_list *list,
        enum vkd3d_predicate_command_type command_type, VkDeviceAddress indirect_args,
        const union vkd3d_predicate_command_direct_args *direct_args, struct vkd3d_scratch_allocation *scratch)
{
    const struct vkd3d_vk_device_procs *vk_procs = &list->device->vk_procs;
    struct vkd3d_predicate_command_info pipeline_info;
    struct vkd3d_predicate_command_args args;
    VkMemoryBarrier vk_barrier;

    vkd3d_meta_get_predicate_pipeline(&list->device->meta_ops, command_type, &pipeline_info);

    if (!d3d12_command_allocator_allocate_scratch_memory(list->allocator,
            pipeline_info.data_size, sizeof(uint32_t), scratch))
        return false;

    d3d12_command_list_end_current_render_pass(list, true);

    d3d12_command_list_invalidate_current_pipeline(list, true);
    d3d12_command_list_invalidate_root_parameters(list, VK_PIPELINE_BIND_POINT_COMPUTE, true);

    args.predicate_va = list->predicate_va;
    args.dst_arg_va = scratch->va;
    args.src_arg_va = indirect_args;
    args.args = *direct_args;

    VK_CALL(vkCmdBindPipeline(list->vk_command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE,
            pipeline_info.vk_pipeline));
    VK_CALL(vkCmdPushConstants(list->vk_command_buffer,
            pipeline_info.vk_pipeline_layout, VK_SHADER_STAGE_COMPUTE_BIT,
            0, sizeof(args), &args));
    VK_CALL(vkCmdDispatch(list->vk_command_buffer, 1, 1, 1));

    vk_barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    vk_barrier.pNext = NULL;
    vk_barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    vk_barrier.dstAccessMask = VK_ACCESS_INDIRECT_COMMAND_READ_BIT;

    VK_CALL(vkCmdPipelineBarrier(list->vk_command_buffer,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT,
            0, 1, &vk_barrier, 0, NULL, 0, NULL));
    return true;
}

static void STDMETHODCALLTYPE d3d12_command_list_DrawInstanced(d3d12_command_list_iface *iface,
        UINT vertex_count_per_instance, UINT instance_count, UINT start_vertex_location,
        UINT start_instance_location)
{
    struct d3d12_command_list *list = impl_from_ID3D12GraphicsCommandList(iface);
    const struct vkd3d_vk_device_procs *vk_procs = vk_procs = &list->device->vk_procs;
    struct vkd3d_scratch_allocation scratch;

    TRACE("iface %p, vertex_count_per_instance %u, instance_count %u, "
            "start_vertex_location %u, start_instance_location %u.\n",
            iface, vertex_count_per_instance, instance_count,
            start_vertex_location, start_instance_location);

    if (list->predicate_va)
    {
        union vkd3d_predicate_command_direct_args args;
        args.draw.vertexCount = vertex_count_per_instance;
        args.draw.instanceCount = instance_count;
        args.draw.firstVertex = start_vertex_location;
        args.draw.firstInstance = start_instance_location;

        if (!d3d12_command_list_emit_predicated_command(list, VKD3D_PREDICATE_COMMAND_DRAW, 0, &args, &scratch))
            return;
    }

    if (!d3d12_command_list_begin_render_pass(list))
    {
        WARN("Failed to begin render pass, ignoring draw call.\n");
        return;
    }

    if (!list->predicate_va)
        VK_CALL(vkCmdDraw(list->vk_command_buffer, vertex_count_per_instance,
                instance_count, start_vertex_location, start_instance_location));
    else
        VK_CALL(vkCmdDrawIndirect(list->vk_command_buffer, scratch.buffer, scratch.offset, 1, 0));
}

static void STDMETHODCALLTYPE d3d12_command_list_DrawIndexedInstanced(d3d12_command_list_iface *iface,
        UINT index_count_per_instance, UINT instance_count, UINT start_vertex_location,
        INT base_vertex_location, UINT start_instance_location)
{
    struct d3d12_command_list *list = impl_from_ID3D12GraphicsCommandList(iface);
    const struct vkd3d_vk_device_procs *vk_procs = vk_procs = &list->device->vk_procs;
    struct vkd3d_scratch_allocation scratch;

    TRACE("iface %p, index_count_per_instance %u, instance_count %u, start_vertex_location %u, "
            "base_vertex_location %d, start_instance_location %u.\n",
            iface, index_count_per_instance, instance_count, start_vertex_location,
            base_vertex_location, start_instance_location);

    if (!list->has_valid_index_buffer)
    {
        FIXME_ONCE("Application attempts to perform an indexed draw call without index buffer bound.\n");
        /* We are supposed to render all 0 indices here. However, there are several problems with emulating this approach.
         * There is no robustness support for index buffers, and if we render all 0 indices,
         * it is extremely unlikely that this would create a meaningful side effect.
         * For any line or triangle primitive, we would end up creating degenerates for every primitive.
         * The only reasonable scenarios where we will observe anything is stream-out with all duplicate values, or
         * geometry shaders where the application makes use of PrimitiveID to construct primitives.
         * Until proven to be required otherwise, we just ignore the draw call. */
        return;
    }

    if (list->predicate_va)
    {
        union vkd3d_predicate_command_direct_args args;
        args.draw_indexed.indexCount = index_count_per_instance;
        args.draw_indexed.instanceCount = instance_count;
        args.draw_indexed.firstIndex = start_vertex_location;
        args.draw_indexed.vertexOffset = base_vertex_location;
        args.draw_indexed.firstInstance = start_instance_location;

        if (!d3d12_command_list_emit_predicated_command(list, VKD3D_PREDICATE_COMMAND_DRAW_INDEXED, 0, &args, &scratch))
            return;
    }

    if (!d3d12_command_list_begin_render_pass(list))
    {
        WARN("Failed to begin render pass, ignoring draw call.\n");
        return;
    }

    d3d12_command_list_check_index_buffer_strip_cut_value(list);

    if (!list->predicate_va)
        VK_CALL(vkCmdDrawIndexed(list->vk_command_buffer, index_count_per_instance,
                instance_count, start_vertex_location, base_vertex_location, start_instance_location));
    else
        VK_CALL(vkCmdDrawIndexedIndirect(list->vk_command_buffer, scratch.buffer, scratch.offset, 1, 0));
}

static void STDMETHODCALLTYPE d3d12_command_list_Dispatch(d3d12_command_list_iface *iface,
        UINT x, UINT y, UINT z)
{
    struct d3d12_command_list *list = impl_from_ID3D12GraphicsCommandList(iface);
    const struct vkd3d_vk_device_procs *vk_procs = &list->device->vk_procs;
    struct vkd3d_scratch_allocation scratch;

    TRACE("iface %p, x %u, y %u, z %u.\n", iface, x, y, z);

    if (list->predicate_va)
    {
        union vkd3d_predicate_command_direct_args args;
        args.dispatch.x = x;
        args.dispatch.y = y;
        args.dispatch.z = z;

        if (!d3d12_command_list_emit_predicated_command(list, VKD3D_PREDICATE_COMMAND_DISPATCH, 0, &args, &scratch))
            return;
    }

    if (!d3d12_command_list_update_compute_state(list))
    {
        WARN("Failed to update compute state, ignoring dispatch.\n");
        return;
    }

    if (!list->predicate_va)
        VK_CALL(vkCmdDispatch(list->vk_command_buffer, x, y, z));
    else
        VK_CALL(vkCmdDispatchIndirect(list->vk_command_buffer, scratch.buffer, scratch.offset));
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

    d3d12_command_list_track_resource_usage(list, dst_resource, true);
    d3d12_command_list_track_resource_usage(list, src_resource, true);

    d3d12_command_list_end_current_render_pass(list, true);

    buffer_copy.srcOffset = src_offset + src_resource->heap_offset;
    buffer_copy.dstOffset = dst_offset + dst_resource->heap_offset;
    buffer_copy.size = byte_count;

    VK_CALL(vkCmdCopyBuffer(list->vk_command_buffer,
            src_resource->vk_buffer, dst_resource->vk_buffer, 1, &buffer_copy));
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
        const VkImageCopy *region, bool writes_full_subresource)
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
    /* Fully writing a subresource with a copy is a valid way to use the "advanced" aliasing model of D3D12.
     * In this model, a complete Copy command is sufficient to activate an aliased resource.
     * This is also an optimization, since we can avoid a potential decompress when entering TRANSFER_DST layout. */
    vk_image_barriers[0].oldLayout = writes_full_subresource ? VK_IMAGE_LAYOUT_UNDEFINED : dst_resource->common_layout;
    vk_image_barriers[0].newLayout = dst_layout;
    vk_image_barriers[0].image = dst_resource->vk_image;
    vk_image_barriers[0].subresourceRange = vk_subresource_range_from_layers(&region->dstSubresource);

    vk_image_barriers[1].srcAccessMask = 0;
    vk_image_barriers[1].dstAccessMask = src_access;
    vk_image_barriers[1].oldLayout = src_resource->common_layout;
    vk_image_barriers[1].newLayout = src_layout;
    vk_image_barriers[1].image = src_resource->vk_image;
    vk_image_barriers[1].subresourceRange = vk_subresource_range_from_layers(&region->srcSubresource);

    VK_CALL(vkCmdPipelineBarrier(list->vk_command_buffer,
            VK_PIPELINE_STAGE_TRANSFER_BIT, src_stages | dst_stages,
            0, 0, NULL, 0, NULL, ARRAY_SIZE(vk_image_barriers),
            vk_image_barriers));

    if (use_copy)
    {
        VK_CALL(vkCmdCopyImage(list->vk_command_buffer,
                src_resource->vk_image, src_layout,
                dst_resource->vk_image, dst_layout,
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

        d3d12_command_list_invalidate_current_pipeline(list, true);
        d3d12_command_list_invalidate_root_parameters(list, VK_PIPELINE_BIND_POINT_GRAPHICS, true);

        memset(&dst_view_desc, 0, sizeof(dst_view_desc));
        dst_view_desc.image = dst_resource->vk_image;
        dst_view_desc.view_type = pipeline_key.view_type;
        dst_view_desc.layout = dst_layout;
        dst_view_desc.format = dst_format;
        dst_view_desc.miplevel_idx = region->dstSubresource.mipLevel;
        dst_view_desc.miplevel_count = 1;
        dst_view_desc.layer_idx = region->dstSubresource.baseArrayLayer;
        dst_view_desc.layer_count = region->dstSubresource.layerCount;
        dst_view_desc.allowed_swizzle = false;

        memset(&src_view_desc, 0, sizeof(src_view_desc));
        src_view_desc.image = src_resource->vk_image;
        src_view_desc.view_type = pipeline_key.view_type;
        src_view_desc.layout = src_layout;
        src_view_desc.format = src_format;
        src_view_desc.miplevel_idx = region->srcSubresource.mipLevel;
        src_view_desc.miplevel_count = 1;
        src_view_desc.layer_idx = region->srcSubresource.baseArrayLayer;
        src_view_desc.layer_count = region->srcSubresource.layerCount;
        src_view_desc.allowed_swizzle = false;

        if (!vkd3d_create_texture_view(list->device, &dst_view_desc, &dst_view) ||
                !vkd3d_create_texture_view(list->device, &src_view_desc, &src_view))
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

        if (!d3d12_command_list_create_framebuffer(list, pipeline_info.vk_render_pass, 1, &dst_view->vk_image_view, extent, &vk_framebuffer))
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
        vk_image_info.imageView = src_view->vk_image_view;
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

    vk_image_barriers[1].srcAccessMask = 0;
    vk_image_barriers[1].dstAccessMask = 0;
    vk_image_barriers[1].oldLayout = src_layout;
    vk_image_barriers[1].newLayout = src_resource->common_layout;

    VK_CALL(vkCmdPipelineBarrier(list->vk_command_buffer,
            src_stages | dst_stages, VK_PIPELINE_STAGE_TRANSFER_BIT,
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
    bool writes_full_subresource;
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

    d3d12_command_list_track_resource_usage(list, src_resource, true);

    d3d12_command_list_end_current_render_pass(list, false);

    if (src->Type == D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX
            && dst->Type == D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT)
    {
        d3d12_command_list_track_resource_usage(list, dst_resource, true);
        assert(d3d12_resource_is_buffer(dst_resource));
        assert(d3d12_resource_is_texture(src_resource));

        if (!(dst_format = vkd3d_format_from_d3d12_resource_desc(list->device,
                &src_resource->desc, dst->PlacedFootprint.Footprint.Format)))
        {
            WARN("Invalid format %#x.\n", dst->PlacedFootprint.Footprint.Format);
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

        vk_image_buffer_copy_from_d3d12(&buffer_image_copy, &dst->PlacedFootprint,
                src->SubresourceIndex, &src_resource->desc, dst_format, src_box, dst_x, dst_y, dst_z);
        buffer_image_copy.bufferOffset += dst_resource->heap_offset;

        vk_layout = d3d12_resource_pick_layout(src_resource, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

        d3d12_command_list_transition_image_layout(list, src_resource->vk_image,
                &buffer_image_copy.imageSubresource, VK_PIPELINE_STAGE_TRANSFER_BIT,
                0, src_resource->common_layout, VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_ACCESS_TRANSFER_READ_BIT, vk_layout);

        VK_CALL(vkCmdCopyImageToBuffer(list->vk_command_buffer,
                src_resource->vk_image, vk_layout,
                dst_resource->vk_buffer, 1, &buffer_image_copy));

        d3d12_command_list_transition_image_layout(list, src_resource->vk_image,
                &buffer_image_copy.imageSubresource, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                vk_layout, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, src_resource->common_layout);
    }
    else if (src->Type == D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT
            && dst->Type == D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX)
    {
        assert(d3d12_resource_is_texture(dst_resource));
        assert(d3d12_resource_is_buffer(src_resource));

        if (!(src_format = vkd3d_format_from_d3d12_resource_desc(list->device,
                &dst_resource->desc, src->PlacedFootprint.Footprint.Format)))
        {
            WARN("Invalid format %#x.\n", src->PlacedFootprint.Footprint.Format);
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

        vk_buffer_image_copy_from_d3d12(&buffer_image_copy, &src->PlacedFootprint,
                dst->SubresourceIndex, &dst_resource->desc, src_format, src_box, dst_x, dst_y, dst_z);
        buffer_image_copy.bufferOffset += src_resource->heap_offset;

        vk_layout = d3d12_resource_pick_layout(dst_resource, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

        writes_full_subresource = d3d12_image_copy_writes_full_subresource(dst_resource,
                &buffer_image_copy.imageExtent, &buffer_image_copy.imageSubresource);

        d3d12_command_list_track_resource_usage(list, dst_resource, !writes_full_subresource);

        d3d12_command_list_transition_image_layout(list, dst_resource->vk_image,
                &buffer_image_copy.imageSubresource, VK_PIPELINE_STAGE_TRANSFER_BIT,
                0, writes_full_subresource ? VK_IMAGE_LAYOUT_UNDEFINED : dst_resource->common_layout,
                VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_ACCESS_TRANSFER_WRITE_BIT, vk_layout);

        VK_CALL(vkCmdCopyBufferToImage(list->vk_command_buffer,
                src_resource->vk_buffer, dst_resource->vk_image,
                vk_layout, 1, &buffer_image_copy));

        d3d12_command_list_transition_image_layout(list, dst_resource->vk_image,
                &buffer_image_copy.imageSubresource, VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_ACCESS_TRANSFER_WRITE_BIT, vk_layout, VK_PIPELINE_STAGE_TRANSFER_BIT,
                0, dst_resource->common_layout);
    }
    else if (src->Type == D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX
            && dst->Type == D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX)
    {
        assert(d3d12_resource_is_texture(dst_resource));
        assert(d3d12_resource_is_texture(src_resource));

        dst_format = dst_resource->format;
        src_format = src_resource->format;

        if ((dst_format->vk_aspect_mask & VK_IMAGE_ASPECT_DEPTH_BIT)
                && (dst_format->vk_aspect_mask & VK_IMAGE_ASPECT_STENCIL_BIT))
            FIXME("Depth-stencil format %#x not fully supported yet.\n", dst_format->dxgi_format);
        if ((src_format->vk_aspect_mask & VK_IMAGE_ASPECT_DEPTH_BIT)
                && (src_format->vk_aspect_mask & VK_IMAGE_ASPECT_STENCIL_BIT))
            FIXME("Depth-stencil format %#x not fully supported yet.\n", src_format->dxgi_format);

        vk_image_copy_from_d3d12(&image_copy, src->SubresourceIndex, dst->SubresourceIndex,
                 &src_resource->desc, &dst_resource->desc, src_format, dst_format,
                 src_box, dst_x, dst_y, dst_z);

        writes_full_subresource = d3d12_image_copy_writes_full_subresource(dst_resource,
                &image_copy.extent, &image_copy.dstSubresource);

        d3d12_command_list_track_resource_usage(list, dst_resource, !writes_full_subresource);

        d3d12_command_list_copy_image(list, dst_resource, dst_format,
                src_resource, src_format, &image_copy, writes_full_subresource);
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
    const struct vkd3d_vk_device_procs *vk_procs;
    VkBufferCopy vk_buffer_copy;
    VkImageCopy vk_image_copy;
    unsigned int layer_count;
    unsigned int i;

    TRACE("iface %p, dst_resource %p, src_resource %p.\n", iface, dst, src);

    vk_procs = &list->device->vk_procs;

    dst_resource = unsafe_impl_from_ID3D12Resource(dst);
    src_resource = unsafe_impl_from_ID3D12Resource(src);

    d3d12_command_list_track_resource_usage(list, dst_resource, false);
    d3d12_command_list_track_resource_usage(list, src_resource, true);

    d3d12_command_list_end_current_render_pass(list, false);

    if (d3d12_resource_is_buffer(dst_resource))
    {
        assert(d3d12_resource_is_buffer(src_resource));
        assert(src_resource->desc.Width == dst_resource->desc.Width);

        vk_buffer_copy.srcOffset = src_resource->heap_offset;
        vk_buffer_copy.dstOffset = dst_resource->heap_offset;
        vk_buffer_copy.size = dst_resource->desc.Width;
        VK_CALL(vkCmdCopyBuffer(list->vk_command_buffer,
                src_resource->vk_buffer, dst_resource->vk_buffer, 1, &vk_buffer_copy));
    }
    else
    {
        layer_count = d3d12_resource_desc_get_layer_count(&dst_resource->desc);

        assert(d3d12_resource_is_texture(dst_resource));
        assert(d3d12_resource_is_texture(src_resource));
        assert(dst_resource->desc.MipLevels == src_resource->desc.MipLevels);
        assert(layer_count == d3d12_resource_desc_get_layer_count(&src_resource->desc));

        for (i = 0; i < dst_resource->desc.MipLevels; ++i)
        {
            vk_image_copy_from_d3d12(&vk_image_copy, i, i,
                    &src_resource->desc, &dst_resource->desc, src_resource->format, dst_resource->format, NULL, 0, 0, 0);
            vk_image_copy.dstSubresource.layerCount = layer_count;
            vk_image_copy.srcSubresource.layerCount = layer_count;

            /* CopyResource() always copies all subresources, so we can safely discard the dst_resource contents. */
            d3d12_command_list_copy_image(list, dst_resource, dst_resource->format,
                    src_resource, src_resource->format, &vk_image_copy, true);
        }
    }
}

static unsigned int vkd3d_get_tile_index_from_region(const struct d3d12_sparse_info *sparse,
        const D3D12_TILED_RESOURCE_COORDINATE *coord, const D3D12_TILE_REGION_SIZE *size,
        unsigned int tile_index_in_region);

static void STDMETHODCALLTYPE d3d12_command_list_CopyTiles(d3d12_command_list_iface *iface,
        ID3D12Resource *tiled_resource, const D3D12_TILED_RESOURCE_COORDINATE *region_coord,
        const D3D12_TILE_REGION_SIZE *region_size, ID3D12Resource *buffer, UINT64 buffer_offset,
        D3D12_TILE_COPY_FLAGS flags)
{
    struct d3d12_command_list *list = impl_from_ID3D12GraphicsCommandList(iface);
    const struct vkd3d_vk_device_procs *vk_procs = &list->device->vk_procs;
    struct d3d12_resource *tiled_res, *linear_res;
    VkImageMemoryBarrier vk_image_barrier;
    VkBufferImageCopy buffer_image_copy;
    VkImageLayout vk_image_layout;
    VkBufferCopy buffer_copy;
    bool copy_to_buffer;
    unsigned int i;

    TRACE("iface %p, tiled_resource %p, region_coord %p, region_size %p, "
            "buffer %p, buffer_offset %#"PRIx64", flags %#x.\n",
            iface, tiled_resource, region_coord, region_size,
            buffer, buffer_offset, flags);

    d3d12_command_list_end_current_render_pass(list, true);

    tiled_res = unsafe_impl_from_ID3D12Resource(tiled_resource);
    linear_res = unsafe_impl_from_ID3D12Resource(buffer);

    d3d12_command_list_track_resource_usage(list, tiled_res, true);

    /* We can't rely on D3D12_TILE_COPY_FLAG_SWIZZLED_TILED_RESOURCE_TO_LINEAR_BUFFER being
     * set for the copy-to-buffer case, since D3D12_TILE_COPY_FLAG_NONE behaves the same. */
    copy_to_buffer = !(flags & D3D12_TILE_COPY_FLAG_LINEAR_BUFFER_TO_SWIZZLED_TILED_RESOURCE);

    if (d3d12_resource_is_texture(tiled_res))
    {
        /* This API cannot be used for non-tiled resources, so this is safe */
        const D3D12_TILE_SHAPE *tile_shape = &tiled_res->sparse.tile_shape;

        if (tiled_res->desc.SampleDesc.Count > 1)
        {
            FIXME("MSAA images not supported.\n");
            return;
        }

        vk_image_layout = d3d12_resource_pick_layout(tiled_res, copy_to_buffer
                ? VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL : VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

        vk_image_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        vk_image_barrier.pNext = NULL;
        vk_image_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        vk_image_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        vk_image_barrier.srcAccessMask = 0;
        vk_image_barrier.dstAccessMask = copy_to_buffer ? VK_ACCESS_TRANSFER_READ_BIT : VK_ACCESS_TRANSFER_WRITE_BIT;
        vk_image_barrier.oldLayout = tiled_res->common_layout;
        vk_image_barrier.newLayout = vk_image_layout;
        vk_image_barrier.image = tiled_res->vk_image;

        /* The entire resource must be in the appropriate copy state */
        vk_image_barrier.subresourceRange.aspectMask = tiled_res->format->vk_aspect_mask;
        vk_image_barrier.subresourceRange.baseMipLevel = 0;
        vk_image_barrier.subresourceRange.levelCount = VK_REMAINING_MIP_LEVELS;
        vk_image_barrier.subresourceRange.baseArrayLayer = 0;
        vk_image_barrier.subresourceRange.layerCount = VK_REMAINING_ARRAY_LAYERS;

        VK_CALL(vkCmdPipelineBarrier(list->vk_command_buffer,
                VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                0, 0, NULL, 0, NULL, 1, &vk_image_barrier));

        buffer_image_copy.bufferRowLength = tile_shape->WidthInTexels;
        buffer_image_copy.bufferImageHeight = tile_shape->HeightInTexels;

        for (i = 0; i < region_size->NumTiles; i++)
        {
            unsigned int tile_index = vkd3d_get_tile_index_from_region(&tiled_res->sparse, region_coord, region_size, i);
            const struct d3d12_sparse_image_region *region = &tiled_res->sparse.tiles[tile_index].image;

            buffer_image_copy.bufferOffset = buffer_offset + VKD3D_TILE_SIZE * i;
            buffer_image_copy.imageSubresource = vk_subresource_layers_from_subresource(&region->subresource);
            buffer_image_copy.imageOffset = region->offset;
            buffer_image_copy.imageExtent = region->extent;

            if (copy_to_buffer)
            {
                VK_CALL(vkCmdCopyImageToBuffer(list->vk_command_buffer,
                        tiled_res->vk_image, vk_image_layout, linear_res->vk_buffer,
                        1, &buffer_image_copy));
            }
            else
            {
                VK_CALL(vkCmdCopyBufferToImage(list->vk_command_buffer,
                        linear_res->vk_buffer, tiled_res->vk_image, vk_image_layout,
                        1, &buffer_image_copy));
            }
        }

        vk_image_barrier.srcAccessMask = copy_to_buffer ? 0 : VK_ACCESS_TRANSFER_WRITE_BIT;
        vk_image_barrier.dstAccessMask = 0;
        vk_image_barrier.oldLayout = vk_image_layout;
        vk_image_barrier.newLayout = tiled_res->common_layout;

        VK_CALL(vkCmdPipelineBarrier(list->vk_command_buffer,
                VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                0, 0, NULL, 0, NULL, 1, &vk_image_barrier));
    }
    else
    {
        buffer_copy.size = region_size->NumTiles * VKD3D_TILE_SIZE;

        if (copy_to_buffer)
        {
            buffer_copy.srcOffset = VKD3D_TILE_SIZE * region_coord->X;
            buffer_copy.dstOffset = buffer_offset;
        }
        else
        {
            buffer_copy.srcOffset = buffer_offset;
            buffer_copy.dstOffset = VKD3D_TILE_SIZE * region_coord->X;
        }

        VK_CALL(vkCmdCopyBuffer(list->vk_command_buffer,
                copy_to_buffer ? tiled_res->vk_buffer : linear_res->vk_buffer,
                copy_to_buffer ? linear_res->vk_buffer : tiled_res->vk_buffer,
                1, &buffer_copy));
    }
}

static void STDMETHODCALLTYPE d3d12_command_list_ResolveSubresource(d3d12_command_list_iface *iface,
        ID3D12Resource *dst, UINT dst_sub_resource_idx,
        ID3D12Resource *src, UINT src_sub_resource_idx, DXGI_FORMAT format)
{
    struct d3d12_command_list *list = impl_from_ID3D12GraphicsCommandList(iface);
    const struct vkd3d_format *vk_format;
    struct d3d12_resource *dst_resource, *src_resource;
    const struct vkd3d_vk_device_procs *vk_procs;
    VkImageMemoryBarrier vk_image_barriers[2];
    VkImageLayout dst_layout, src_layout;
    const struct d3d12_device *device;
    VkImageResolve vk_image_resolve;
    bool writes_full_subresource;
    unsigned int i;

    TRACE("iface %p, dst_resource %p, dst_sub_resource_idx %u, src_resource %p, src_sub_resource_idx %u, "
            "format %#x.\n", iface, dst, dst_sub_resource_idx, src, src_sub_resource_idx, format);

    device = list->device;
    vk_procs = &device->vk_procs;

    dst_resource = unsafe_impl_from_ID3D12Resource(dst);
    src_resource = unsafe_impl_from_ID3D12Resource(src);

    assert(d3d12_resource_is_texture(dst_resource));
    assert(d3d12_resource_is_texture(src_resource));

    d3d12_command_list_end_current_render_pass(list, false);

    if (dst_resource->format->type == VKD3D_FORMAT_TYPE_TYPELESS || src_resource->format->type == VKD3D_FORMAT_TYPE_TYPELESS)
    {
        if (!(vk_format = vkd3d_format_from_d3d12_resource_desc(device, &dst_resource->desc, format)))
        {
            WARN("Invalid format %#x.\n", format);
            return;
        }
        if (dst_resource->format->vk_format != src_resource->format->vk_format || dst_resource->format->vk_format != vk_format->vk_format)
        {
            FIXME("Not implemented for typeless resources.\n");
            return;
        }
    }

    /* Resolve of depth/stencil images is not supported in Vulkan. */
    if ((dst_resource->format->vk_aspect_mask & (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT))
            || (src_resource->format->vk_aspect_mask & (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT)))
    {
        FIXME("Resolve of depth/stencil images is not implemented yet.\n");
        return;
    }

    vk_image_subresource_layers_from_d3d12(&vk_image_resolve.srcSubresource,
            src_resource->format, src_sub_resource_idx, src_resource->desc.MipLevels);
    memset(&vk_image_resolve.srcOffset, 0, sizeof(vk_image_resolve.srcOffset));
    vk_image_subresource_layers_from_d3d12(&vk_image_resolve.dstSubresource,
            dst_resource->format, dst_sub_resource_idx, dst_resource->desc.MipLevels);
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

    writes_full_subresource = d3d12_image_copy_writes_full_subresource(dst_resource,
            &vk_image_resolve.extent, &vk_image_resolve.dstSubresource);

    d3d12_command_list_track_resource_usage(list, dst_resource, !writes_full_subresource);
    d3d12_command_list_track_resource_usage(list, src_resource, true);

    vk_image_barriers[0].srcAccessMask = 0;
    vk_image_barriers[0].dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vk_image_barriers[0].oldLayout = writes_full_subresource ? VK_IMAGE_LAYOUT_UNDEFINED : dst_resource->common_layout;
    vk_image_barriers[0].newLayout = dst_layout;
    vk_image_barriers[0].image = dst_resource->vk_image;
    vk_image_barriers[0].subresourceRange = vk_subresource_range_from_layers(&vk_image_resolve.dstSubresource);

    vk_image_barriers[1].srcAccessMask = 0;
    vk_image_barriers[1].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    vk_image_barriers[1].oldLayout = src_resource->common_layout;
    vk_image_barriers[1].newLayout = src_layout;
    vk_image_barriers[1].image = src_resource->vk_image;
    vk_image_barriers[1].subresourceRange = vk_subresource_range_from_layers(&vk_image_resolve.srcSubresource);

    VK_CALL(vkCmdPipelineBarrier(list->vk_command_buffer,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
            0, 0, NULL, 0, NULL, ARRAY_SIZE(vk_image_barriers), vk_image_barriers));

    VK_CALL(vkCmdResolveImage(list->vk_command_buffer, src_resource->vk_image,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dst_resource->vk_image,
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
    dyn_state->vk_primitive_topology = vk_topology_from_d3d12_topology(topology);
    d3d12_command_list_invalidate_current_pipeline(list, false);
    dyn_state->dirty_flags |= VKD3D_DYNAMIC_STATE_TOPOLOGY;
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
        VkViewport *vk_viewport = &dyn_state->viewports[i];
        vk_viewport->x = viewports[i].TopLeftX;
        vk_viewport->y = viewports[i].TopLeftY + viewports[i].Height;
        vk_viewport->width = viewports[i].Width;
        vk_viewport->height = -viewports[i].Height;
        vk_viewport->minDepth = viewports[i].MinDepth;
        vk_viewport->maxDepth = viewports[i].MaxDepth;

        if (vk_viewport->width <= 0.0f)
        {
            vk_viewport->width = 1.0f;
            vk_viewport->height = 0.0f;
        }
    }

    if (dyn_state->viewport_count != viewport_count)
    {
        dyn_state->viewport_count = viewport_count;
        dyn_state->dirty_flags |= VKD3D_DYNAMIC_STATE_SCISSOR | VKD3D_DYNAMIC_STATE_SCISSOR_COUNT;
        d3d12_command_list_invalidate_current_pipeline(list, false);
    }

    dyn_state->dirty_flags |= VKD3D_DYNAMIC_STATE_VIEWPORT | VKD3D_DYNAMIC_STATE_VIEWPORT_COUNT;
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

    dyn_state->dirty_flags |= VKD3D_DYNAMIC_STATE_SCISSOR | VKD3D_DYNAMIC_STATE_SCISSOR_COUNT;
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
    unsigned int i;

    TRACE("iface %p, pipeline_state %p.\n", iface, pipeline_state);

    if ((TRACE_ON() || list->device->debug_ring.active) && state)
    {
        if (d3d12_pipeline_state_has_replaced_shaders(state))
        {
            TRACE("Binding pipeline state %p which has replaced shader(s)!\n", pipeline_state);
            list->has_replaced_shaders = true;
        }

        if (state->vk_bind_point == VK_PIPELINE_BIND_POINT_COMPUTE)
        {
            TRACE("Binding compute module with hash: %016"PRIx64".\n", state->compute.meta.hash);
        }
        else if (state->vk_bind_point == VK_PIPELINE_BIND_POINT_GRAPHICS)
        {
            for (i = 0; i < state->graphics.stage_count; i++)
            {
                TRACE("Binding graphics module with hash: %016"PRIx64" (replaced: %s).\n",
                      state->graphics.stage_meta[i].hash,
                      state->graphics.stage_meta[i].replaced ? "yes" : "no");
            }
        }
    }

#ifdef VKD3D_ENABLE_RENDERDOC
    vkd3d_renderdoc_command_list_check_capture(list, state);
#endif

    if (list->state == state)
        return;

    d3d12_command_list_invalidate_current_pipeline(list, false);
    list->state = state;
}

static void vk_image_memory_barrier_for_after_aliasing_barrier(struct d3d12_device *device,
        VkQueueFlags vk_queue_flags, struct d3d12_resource *after, VkImageMemoryBarrier *vk_barrier)
{
    vk_barrier->sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    vk_barrier->pNext = NULL;
    vk_barrier->srcAccessMask = 0;
    vk_barrier->dstAccessMask = vk_access_flags_all_possible_for_image(device, after->desc.Flags, vk_queue_flags, true);
    vk_barrier->oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    vk_barrier->newLayout = after->common_layout;
    vk_barrier->srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    vk_barrier->dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    vk_barrier->image = after->vk_image;
    vk_barrier->subresourceRange.aspectMask = after->format->vk_aspect_mask;
    vk_barrier->subresourceRange.baseMipLevel = 0;
    vk_barrier->subresourceRange.levelCount = VK_REMAINING_MIP_LEVELS;
    vk_barrier->subresourceRange.baseArrayLayer = 0;
    vk_barrier->subresourceRange.layerCount = VK_REMAINING_ARRAY_LAYERS;
}

static bool d3d12_resource_may_alias_other_resources(struct d3d12_resource *resource)
{
    /* Treat a NULL resource as "all" resources. */
    if (!resource)
        return true;

    /* Cannot alias if the resource is allocated in a dedicated heap. */
    if (resource->flags & VKD3D_RESOURCE_DEDICATED_HEAP)
        return false;

    return true;
}

static void STDMETHODCALLTYPE d3d12_command_list_ResourceBarrier(d3d12_command_list_iface *iface,
        UINT barrier_count, const D3D12_RESOURCE_BARRIER *barriers)
{
    struct d3d12_command_list *list = impl_from_ID3D12GraphicsCommandList(iface);
    const struct vkd3d_vk_device_procs *vk_procs = &list->device->vk_procs;
    bool have_split_barriers = false;
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
        struct d3d12_resource *preserve_resource = NULL;
        struct d3d12_resource *discard_resource = NULL;

        have_split_barriers = have_split_barriers
                || (current->Flags & D3D12_RESOURCE_BARRIER_FLAG_BEGIN_ONLY)
                || (current->Flags & D3D12_RESOURCE_BARRIER_FLAG_END_ONLY);

        if (current->Flags & D3D12_RESOURCE_BARRIER_FLAG_BEGIN_ONLY)
            continue;

        switch (current->Type)
        {
            case D3D12_RESOURCE_BARRIER_TYPE_TRANSITION:
            {
                const D3D12_RESOURCE_TRANSITION_BARRIER *transition = &current->Transition;

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

                if (!(preserve_resource = unsafe_impl_from_ID3D12Resource(transition->pResource)))
                {
                    d3d12_command_list_mark_as_invalid(list, "A resource pointer is NULL.");
                    continue;
                }

                vk_access_and_stage_flags_from_d3d12_resource_state(list->device, preserve_resource,
                        transition->StateBefore, list->vk_queue_flags, &src_stage_mask,
                        &vk_memory_barrier.srcAccessMask);
                vk_access_and_stage_flags_from_d3d12_resource_state(list->device, preserve_resource,
                        transition->StateAfter, list->vk_queue_flags, &dst_stage_mask,
                        &vk_memory_barrier.dstAccessMask);

                TRACE("Transition barrier (resource %p, subresource %#x, before %#x, after %#x).\n",
                        preserve_resource, transition->Subresource, transition->StateBefore, transition->StateAfter);
                break;
            }

            case D3D12_RESOURCE_BARRIER_TYPE_UAV:
            {
                const D3D12_RESOURCE_UAV_BARRIER *uav = &current->UAV;
                preserve_resource = unsafe_impl_from_ID3D12Resource(uav->pResource);

                vk_access_and_stage_flags_from_d3d12_resource_state(list->device, preserve_resource,
                        D3D12_RESOURCE_STATE_UNORDERED_ACCESS, list->vk_queue_flags, &src_stage_mask,
                        &vk_memory_barrier.srcAccessMask);
                vk_access_and_stage_flags_from_d3d12_resource_state(list->device, preserve_resource,
                        D3D12_RESOURCE_STATE_UNORDERED_ACCESS, list->vk_queue_flags, &dst_stage_mask,
                        &vk_memory_barrier.dstAccessMask);

                TRACE("UAV barrier (resource %p).\n", preserve_resource);
                break;
            }

            case D3D12_RESOURCE_BARRIER_TYPE_ALIASING:
            {
                const D3D12_RESOURCE_ALIASING_BARRIER *alias;
                struct d3d12_resource *before, *after;
                VkMemoryBarrier alias_memory_barrier;
                VkAccessFlags alias_src_access;
                VkAccessFlags alias_dst_access;

                alias = &current->Aliasing;
                TRACE("Aliasing barrier (before %p, after %p).\n", alias->pResourceBefore, alias->pResourceAfter);
                before = unsafe_impl_from_ID3D12Resource(alias->pResourceBefore);
                after = unsafe_impl_from_ID3D12Resource(alias->pResourceAfter);

                discard_resource = after;

                if (d3d12_resource_may_alias_other_resources(before) && d3d12_resource_may_alias_other_resources(after))
                {
                    /* An aliasing barrier in D3D12 means that we should wait for all writes to complete on before resource,
                     * and then potentially emit an UNDEFINED -> default barrier on image resources.
                     * before can be NULL, which means "any" resource. */

                    if (before && d3d12_resource_is_texture(before))
                    {
                        alias_src_access = vk_access_flags_all_possible_for_image(list->device,
                                before->desc.Flags,
                                list->vk_queue_flags,
                                false);
                    }
                    else
                    {
                        alias_src_access = vk_access_flags_all_possible_for_buffer(list->device,
                                list->vk_queue_flags,
                                false);
                    }

                    if (after && d3d12_resource_is_texture(after))
                    {
                        alias_memory_barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
                        alias_memory_barrier.pNext = NULL;
                        alias_memory_barrier.srcAccessMask = alias_src_access;
                        alias_memory_barrier.dstAccessMask = 0;

                        vk_image_memory_barrier_for_after_aliasing_barrier(list->device, list->vk_queue_flags,
                                after, &vk_image_barrier);
                        VK_CALL(vkCmdPipelineBarrier(list->vk_command_buffer,
                                VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                                0, 1, &alias_memory_barrier, 0, NULL, 1, &vk_image_barrier));
                    }
                    else
                    {
                        if (!after)
                            FIXME("NULL resource for pResourceAfter. Won't be able to transition images away from UNDEFINED.\n");
                        alias_dst_access = vk_access_flags_all_possible_for_buffer(list->device,
                                list->vk_queue_flags, true);

                        src_stage_mask |= VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
                        dst_stage_mask |= VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
                        vk_memory_barrier.srcAccessMask |= alias_src_access;
                        vk_memory_barrier.dstAccessMask |= alias_dst_access;
                    }
                }
                break;
            }

            default:
                WARN("Invalid barrier type %#x.\n", current->Type);
                continue;
        }

        if (preserve_resource)
            d3d12_command_list_track_resource_usage(list, preserve_resource, true);

        /* We will need to skip any initial transition if the aliasing barrier is the first use we observe in
         * a command list.
         * Consider a scenario of two freshly allocated resources A and B which alias each other.
         * - A is used with Clear/DiscardResource.
         * - Use A.
         * - Aliasing barrier A -> B.
         * - B gets Clear/DiscardResource.
         * - Use B.
         * With initial transitions, we risk getting this order of commands if we naively queue up transitions.
         * - A UNDEFINED -> common
         * - B UNDEFINED -> common
         * - A is used (woops! B is the current owner).
         * It is critical to avoid redundant initial layout transitions if the first use transitions away from
         * UNDEFINED to make sure aliasing ownership is maintained correctly throughout a submission. */
        if (discard_resource)
            d3d12_command_list_track_resource_usage(list, discard_resource, false);
    }

    if (src_stage_mask && dst_stage_mask)
    {
        VK_CALL(vkCmdPipelineBarrier(list->vk_command_buffer,
                src_stage_mask, dst_stage_mask, 0,
                1, &vk_memory_barrier, 0, NULL, 0, NULL));
    }

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
    uint64_t dirty_mask = 0;
    unsigned int i, j;

    TRACE("iface %p, heap_count %u, heaps %p.\n", iface, heap_count, heaps);

    for (i = 0; i < heap_count; i++)
    {
        struct d3d12_descriptor_heap *heap = unsafe_impl_from_ID3D12DescriptorHeap(heaps[i]);
        unsigned int set_index = 0;

        if (!heap)
            continue;

        for (j = 0; j < bindless_state->set_count; j++)
        {
            if (bindless_state->set_info[j].heap_type != heap->desc.Type)
                continue;

            list->descriptor_heaps[j] = heap->vk_descriptor_sets[set_index++];
            dirty_mask |= 1ull << j;
        }
    }

    for (i = 0; i < ARRAY_SIZE(list->pipeline_bindings); i++)
    {
        struct vkd3d_pipeline_bindings *bindings = &list->pipeline_bindings[i];
        bindings->descriptor_heap_dirty_mask = dirty_mask;
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

    if (root_signature && root_signature->vk_sampler_set)
        bindings->static_sampler_set = root_signature->vk_sampler_set;

    d3d12_command_list_invalidate_root_parameters(list, bind_point, true);
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

static void d3d12_command_list_set_push_descriptor_info(struct d3d12_command_list *list,
        VkPipelineBindPoint bind_point, unsigned int index, D3D12_GPU_VIRTUAL_ADDRESS gpu_address)
{
    struct vkd3d_pipeline_bindings *bindings = &list->pipeline_bindings[bind_point];
    const struct d3d12_root_signature *root_signature = bindings->root_signature;
    const struct vkd3d_vk_device_procs *vk_procs = &list->device->vk_procs;
    const struct vkd3d_vulkan_info *vk_info = &list->device->vk_info;
    const struct d3d12_root_parameter *root_parameter;
    struct vkd3d_root_descriptor_info *descriptor;
    struct d3d12_resource *resource;
    VkBufferView vk_buffer_view;
    bool null_descriptors, ssbo;
    VkDeviceSize max_range;

    ssbo = d3d12_device_use_ssbo_root_descriptors(list->device);
    root_parameter = root_signature_get_root_descriptor(root_signature, index);
    descriptor = &bindings->root_descriptors[index];
    null_descriptors = list->device->device_info.robustness2_features.nullDescriptor;

    if (ssbo || root_parameter->parameter_type == D3D12_ROOT_PARAMETER_TYPE_CBV)
    {
        descriptor->vk_descriptor_type = root_parameter->parameter_type == D3D12_ROOT_PARAMETER_TYPE_CBV
                ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;

        if (gpu_address)
        {
            max_range = descriptor->vk_descriptor_type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER
                    ? vk_info->device_limits.maxUniformBufferRange
                    : vk_info->device_limits.maxStorageBufferRange;

            resource = vkd3d_gpu_va_allocator_dereference(&list->device->gpu_va_allocator, gpu_address);
            descriptor->info.buffer.buffer = resource->vk_buffer;
            descriptor->info.buffer.offset = gpu_address - resource->gpu_address;
            descriptor->info.buffer.range = min(resource->desc.Width - descriptor->info.buffer.offset, max_range);
        }
        else if (null_descriptors)
        {
            descriptor->info.buffer.buffer = VK_NULL_HANDLE;
            descriptor->info.buffer.offset = 0;
            descriptor->info.buffer.range = 0;
        }
        else
        {
            descriptor->info.buffer.buffer = list->device->null_resources.vk_buffer;
            descriptor->info.buffer.offset = 0;
            descriptor->info.buffer.range = VK_WHOLE_SIZE;
        }
    }
    else
    {
        descriptor->vk_descriptor_type = root_parameter->parameter_type == D3D12_ROOT_PARAMETER_TYPE_SRV
                ? VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER : VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER;

        if (gpu_address)
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

            descriptor->info.buffer_view = vk_buffer_view;
        }
        else if (null_descriptors)
        {
            descriptor->info.buffer_view = VK_NULL_HANDLE;
        }
        else
        {
            descriptor->info.buffer_view = root_parameter->parameter_type == D3D12_ROOT_PARAMETER_TYPE_SRV
                    ? list->device->null_resources.vk_buffer_view
                    : list->device->null_resources.vk_storage_buffer_view;
        }
    }
}

static void d3d12_command_list_set_root_descriptor_va(struct d3d12_command_list *list,
        struct vkd3d_root_descriptor_info *descriptor, D3D12_GPU_VIRTUAL_ADDRESS gpu_address)
{
    const struct d3d12_resource *resource;
    VkDeviceAddress va = 0;

    if (gpu_address)
    {
        /* We're not actually passing real VAs to the app, so we need to remap the address */
        resource = vkd3d_gpu_va_allocator_dereference(&list->device->gpu_va_allocator, gpu_address);
        va = vkd3d_get_buffer_device_address(list->device, resource->vk_buffer) + gpu_address - resource->gpu_address;
    }

    descriptor->vk_descriptor_type = VK_DESCRIPTOR_TYPE_MAX_ENUM;
    descriptor->info.va = va;
}

static void d3d12_command_list_set_root_descriptor(struct d3d12_command_list *list,
        VkPipelineBindPoint bind_point, unsigned int index, D3D12_GPU_VIRTUAL_ADDRESS gpu_address)
{
    struct vkd3d_pipeline_bindings *bindings = &list->pipeline_bindings[bind_point];
    struct vkd3d_root_descriptor_info *descriptor = &bindings->root_descriptors[index];

    if (bindings->root_signature->root_descriptor_raw_va_mask & (1ull << index))
        d3d12_command_list_set_root_descriptor_va(list, descriptor, gpu_address);
    else
        d3d12_command_list_set_push_descriptor_info(list, bind_point, index, gpu_address);

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
        WARN("Got NULL index buffer view, indexed draw calls will be dropped.\n");
        list->has_valid_index_buffer = false;
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
            FIXME_ONCE("Invalid index format %#x. This will map to R16_UINT to match observed driver behavior.\n", view->Format);
            /* D3D12 debug layer disallows this case, but it doesn't trigger a DEVICE LOST event, so we shouldn't crash and burn. */
            index_type = VK_INDEX_TYPE_UINT16;
            break;
    }

    list->index_buffer_format = view->Format;
    list->has_valid_index_buffer = view->BufferLocation != 0;
    if (list->has_valid_index_buffer)
    {
        resource = vkd3d_gpu_va_allocator_dereference(&list->device->gpu_va_allocator, view->BufferLocation);
        VK_CALL(vkCmdBindIndexBuffer(list->vk_command_buffer, resource->vk_buffer,
                view->BufferLocation - resource->gpu_address, index_type));
    }
}

static void STDMETHODCALLTYPE d3d12_command_list_IASetVertexBuffers(d3d12_command_list_iface *iface,
        UINT start_slot, UINT view_count, const D3D12_VERTEX_BUFFER_VIEW *views)
{
    struct d3d12_command_list *list = impl_from_ID3D12GraphicsCommandList(iface);
    struct vkd3d_dynamic_state *dyn_state = &list->dynamic_state;
    const struct vkd3d_null_resources *null_resources;
    struct vkd3d_gpu_va_allocator *gpu_va_allocator;
    struct d3d12_resource *resource;
    uint32_t vbo_invalidate_mask;
    bool invalidate = false;
    unsigned int i;

    TRACE("iface %p, start_slot %u, view_count %u, views %p.\n", iface, start_slot, view_count, views);

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
        bool invalid_va = false;
        VkBuffer buffer;
        VkDeviceSize offset;
        VkDeviceSize size;
        uint32_t stride;

        if (views[i].BufferLocation)
        {
            resource = vkd3d_gpu_va_allocator_dereference(gpu_va_allocator, views[i].BufferLocation);
            if (resource)
            {
                buffer = resource->vk_buffer;
                offset = views[i].BufferLocation - resource->gpu_address;
                stride = views[i].StrideInBytes;
                size = views[i].SizeInBytes;
            }
            else
            {
                invalid_va = true;
                FIXME("Attempting to bind a VBO VA that does not exist, binding NULL VA ...\n");
            }
        }
        else
            invalid_va = true;

        if (invalid_va)
        {
            bool null_descriptors = list->device->device_info.robustness2_features.nullDescriptor;
            buffer = null_descriptors ? VK_NULL_HANDLE : null_resources->vk_buffer;
            offset = 0;
            size = null_descriptors ? 0 : VKD3D_NULL_BUFFER_SIZE;
            stride = VKD3D_NULL_BUFFER_SIZE;
        }

        invalidate |= dyn_state->vertex_strides[start_slot + i] != stride;
        dyn_state->vertex_strides[start_slot + i] = stride;
        dyn_state->vertex_buffers[start_slot + i] = buffer;
        dyn_state->vertex_offsets[start_slot + i] = offset;
        dyn_state->vertex_sizes[start_slot + i] = size;
    }

    dyn_state->dirty_flags |= VKD3D_DYNAMIC_STATE_VERTEX_BUFFER | VKD3D_DYNAMIC_STATE_VERTEX_BUFFER_STRIDE;

    vbo_invalidate_mask = ((1u << view_count) - 1u) << start_slot;
    dyn_state->dirty_vbos |= vbo_invalidate_mask;
    dyn_state->dirty_vbo_strides |= vbo_invalidate_mask;

    if (invalidate)
        d3d12_command_list_invalidate_current_pipeline(list, false);
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
            buffers[count] = resource->vk_buffer;
            offsets[count] = views[i].BufferLocation - resource->gpu_address;
            sizes[count] = views[i].SizeInBytes;

            resource = vkd3d_gpu_va_allocator_dereference(gpu_va_allocator, views[i].BufferFilledSizeLocation);
            list->so_counter_buffers[start_slot + i] = resource->vk_buffer;
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

        d3d12_command_list_track_resource_usage(list, rtv_desc->resource, true);

        list->rtvs[i] = *rtv_desc;
        list->fb_width = min(list->fb_width, rtv_desc->width);
        list->fb_height = min(list->fb_height, rtv_desc->height);
        list->fb_layer_count = min(list->fb_layer_count, rtv_desc->layer_count);
    }

    if (depth_stencil_descriptor)
    {
        if ((rtv_desc = d3d12_rtv_desc_from_cpu_handle(*depth_stencil_descriptor))
                && rtv_desc->resource)
        {
            d3d12_command_list_track_resource_usage(list, rtv_desc->resource, true);

            list->dsv = *rtv_desc;
            list->fb_width = min(list->fb_width, rtv_desc->width);
            list->fb_height = min(list->fb_height, rtv_desc->height);
            list->fb_layer_count = min(list->fb_layer_count, rtv_desc->layer_count);
            next_dsv_format = rtv_desc->format->vk_format;
        }
        else
        {
            WARN("DSV descriptor is not initialized.\n");
        }
    }

    if (prev_dsv_format != next_dsv_format && d3d12_pipeline_state_has_unknown_dsv_format(list->state))
        d3d12_command_list_invalidate_current_pipeline(list, false);
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
    full_rect = d3d12_get_image_rect(resource, view->info.texture.miplevel_idx);
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
    const struct d3d12_rtv_desc *dsv_desc = d3d12_rtv_desc_from_cpu_handle(dsv);
    VkImageAspectFlags clear_aspects = 0;

    TRACE("iface %p, dsv %#lx, flags %#x, depth %.8e, stencil 0x%02x, rect_count %u, rects %p.\n",
            iface, dsv.ptr, flags, depth, stencil, rect_count, rects);

    d3d12_command_list_track_resource_usage(list, dsv_desc->resource, true);

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

    d3d12_command_list_track_resource_usage(list, rtv_desc->resource, true);

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

struct vkd3d_clear_uav_info
{
    bool has_view;
    union
    {
        struct vkd3d_view *view;
        struct
        {
            VkDeviceSize offset;
            VkDeviceSize range;
        } buffer;
    } u;
};

static void d3d12_command_list_clear_uav(struct d3d12_command_list *list, const struct d3d12_desc *desc,
        struct d3d12_resource *resource, const struct vkd3d_clear_uav_info *args,
        const VkClearColorValue *clear_color, UINT rect_count, const D3D12_RECT *rects)
{
    const struct vkd3d_vk_device_procs *vk_procs = &list->device->vk_procs;
    unsigned int i, miplevel_idx, layer_count;
    struct vkd3d_clear_uav_pipeline pipeline;
    struct vkd3d_clear_uav_args clear_args;
    VkDescriptorBufferInfo buffer_info;
    VkDescriptorImageInfo image_info;
    D3D12_RECT full_rect, curr_rect;
    VkWriteDescriptorSet write_set;
    VkExtent3D workgroup_size;
    uint32_t extra_offset;

    d3d12_command_list_track_resource_usage(list, resource, true);
    d3d12_command_list_end_current_render_pass(list, false);

    d3d12_command_list_invalidate_current_pipeline(list, true);
    d3d12_command_list_invalidate_root_parameters(list, VK_PIPELINE_BIND_POINT_COMPUTE, true);

    clear_args.clear_color = *clear_color;

    write_set.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write_set.pNext = NULL;
    write_set.dstBinding = 0;
    write_set.dstArrayElement = 0;
    write_set.descriptorCount = 1;

    if (d3d12_resource_is_texture(resource))
    {
        assert(args->has_view);
        image_info.sampler = VK_NULL_HANDLE;
        image_info.imageView = args->u.view->vk_image_view;
        image_info.imageLayout = args->u.view->info.texture.vk_layout;

        write_set.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        write_set.pImageInfo = &image_info;
        write_set.pBufferInfo = NULL;
        write_set.pTexelBufferView = NULL;

        miplevel_idx = args->u.view->info.texture.miplevel_idx;
        layer_count = args->u.view->info.texture.vk_view_type == VK_IMAGE_VIEW_TYPE_3D
                ? d3d12_resource_desc_get_depth(&resource->desc, miplevel_idx)
                : args->u.view->info.texture.layer_count;
        pipeline = vkd3d_meta_get_clear_image_uav_pipeline(
                &list->device->meta_ops, args->u.view->info.texture.vk_view_type,
                args->u.view->format->type == VKD3D_FORMAT_TYPE_UINT);
        workgroup_size = vkd3d_meta_get_clear_image_uav_workgroup_size(args->u.view->info.texture.vk_view_type);
    }
    else
    {
        write_set.pImageInfo = NULL;
        write_set.pBufferInfo = NULL;
        write_set.pTexelBufferView = NULL;

        if (args->has_view)
        {
            write_set.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER;
            write_set.pTexelBufferView = &args->u.view->vk_buffer_view;
        }
        else
        {
            write_set.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            write_set.pBufferInfo = &buffer_info;
            /* resource heap offset is already in descriptor */
            buffer_info.buffer = resource->vk_buffer;
            buffer_info.offset = args->u.buffer.offset;
            buffer_info.range = args->u.buffer.range;
        }

        miplevel_idx = 0;
        layer_count = 1;
        pipeline = vkd3d_meta_get_clear_buffer_uav_pipeline(&list->device->meta_ops,
                !args->has_view || args->u.view->format->type == VKD3D_FORMAT_TYPE_UINT,
                !args->has_view);
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
    extra_offset = 0;

    if (d3d12_resource_is_buffer(resource))
    {
        const struct vkd3d_bound_buffer_range *ranges = desc->heap->buffer_ranges.host_ptr;

        if (args->has_view)
        {
            if (list->device->bindless_state.flags & VKD3D_TYPED_OFFSET_BUFFER)
            {
                extra_offset = ranges[desc->heap_offset].element_offset;
                full_rect.right = ranges[desc->heap_offset].element_count;
            }
            else
            {
                VkDeviceSize byte_count = args->u.view->format->byte_count
                        ? args->u.view->format->byte_count
                        : sizeof(uint32_t);  /* structured buffer */
                full_rect.right = args->u.view->info.buffer.size / byte_count;
            }
        }
        else if (list->device->bindless_state.flags & VKD3D_SSBO_OFFSET_BUFFER)
        {
            extra_offset = ranges[desc->heap_offset].byte_offset / sizeof(uint32_t);
            full_rect.right = ranges[desc->heap_offset].byte_count / sizeof(uint32_t);
        }
        else
            full_rect.right = args->u.buffer.range / sizeof(uint32_t);
    }

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

        clear_args.offset.x = curr_rect.left + extra_offset;
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

static void vkd3d_clear_uav_info_from_desc(struct vkd3d_clear_uav_info *args, const struct d3d12_desc *desc)
{
    if (desc->metadata.flags & VKD3D_DESCRIPTOR_FLAG_VIEW)
    {
        args->has_view = true;
        args->u.view = desc->info.view;
    }
    else
    {
        args->has_view = false;
        args->u.buffer.offset = desc->info.buffer.offset;
        args->u.buffer.range = desc->info.buffer.range;
    }
}

static void STDMETHODCALLTYPE d3d12_command_list_ClearUnorderedAccessViewUint(d3d12_command_list_iface *iface,
        D3D12_GPU_DESCRIPTOR_HANDLE gpu_handle, D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle, ID3D12Resource *resource,
        const UINT values[4], UINT rect_count, const D3D12_RECT *rects)
{
    struct d3d12_command_list *list = impl_from_ID3D12GraphicsCommandList(iface);
    const struct d3d12_desc *desc = d3d12_desc_from_cpu_handle(cpu_handle);
    const struct vkd3d_format *uint_format;
    struct d3d12_resource *resource_impl;
    struct vkd3d_clear_uav_info args;
    VkClearColorValue color;

    TRACE("iface %p, gpu_handle %#"PRIx64", cpu_handle %lx, resource %p, values %p, rect_count %u, rects %p.\n",
            iface, gpu_handle.ptr, cpu_handle.ptr, resource, values, rect_count, rects);

    memcpy(color.uint32, values, sizeof(color.uint32));

    resource_impl = unsafe_impl_from_ID3D12Resource(resource);

    vkd3d_clear_uav_info_from_desc(&args, desc);

    if (args.has_view && desc->info.view->format->type != VKD3D_FORMAT_TYPE_UINT)
    {
        const struct vkd3d_view *base_view = desc->info.view;
        uint_format = vkd3d_find_uint_format(list->device, base_view->format->dxgi_format);

        if (!uint_format && !(uint_format = vkd3d_fixup_clear_uav_uint_color(
                list->device, base_view->format->dxgi_format, &color)))
        {
            ERR("Unhandled format %d.\n", base_view->format->dxgi_format);
            return;
        }

        if (d3d12_resource_is_texture(resource_impl))
        {
            struct vkd3d_texture_view_desc view_desc;
            memset(&view_desc, 0, sizeof(view_desc));

            view_desc.image = resource_impl->vk_image;
            view_desc.view_type = base_view->info.texture.vk_view_type;
            view_desc.layout = base_view->info.texture.vk_layout;
            view_desc.format = uint_format;
            view_desc.miplevel_idx = base_view->info.texture.miplevel_idx;
            view_desc.miplevel_count = 1;
            view_desc.layer_idx = base_view->info.texture.layer_idx;
            view_desc.layer_count = base_view->info.texture.layer_count;
            view_desc.allowed_swizzle = false;

            if (!vkd3d_create_texture_view(list->device, &view_desc, &args.u.view))
            {
                ERR("Failed to create image view.\n");
                return;
            }
        }
        else
        {
            struct vkd3d_buffer_view_desc view_desc;
            view_desc.buffer = resource_impl->vk_buffer;
            view_desc.format = uint_format;
            view_desc.offset = base_view->info.buffer.offset;
            view_desc.size = base_view->info.buffer.size;

            if (!vkd3d_create_buffer_view(list->device, &view_desc, &args.u.view))
            {
                ERR("Failed to create buffer view.\n");
                return;
            }
        }
    }

    d3d12_command_list_clear_uav(list, desc, resource_impl, &args, &color, rect_count, rects);
}

static void STDMETHODCALLTYPE d3d12_command_list_ClearUnorderedAccessViewFloat(d3d12_command_list_iface *iface,
        D3D12_GPU_DESCRIPTOR_HANDLE gpu_handle, D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle, ID3D12Resource *resource,
        const float values[4], UINT rect_count, const D3D12_RECT *rects)
{
    struct d3d12_command_list *list = impl_from_ID3D12GraphicsCommandList(iface);
    const struct d3d12_desc *desc = d3d12_desc_from_cpu_handle(cpu_handle);
    struct d3d12_resource *resource_impl;
    struct vkd3d_clear_uav_info args;
    VkClearColorValue color;

    TRACE("iface %p, gpu_handle %#"PRIx64", cpu_handle %lx, resource %p, values %p, rect_count %u, rects %p.\n",
            iface, gpu_handle.ptr, cpu_handle.ptr, resource, values, rect_count, rects);

    memcpy(color.float32, values, sizeof(color.float32));

    resource_impl = unsafe_impl_from_ID3D12Resource(resource);

    vkd3d_clear_uav_info_from_desc(&args, desc);
    d3d12_command_list_clear_uav(list, desc, resource_impl, &args, &color, rect_count, rects);
}

static void STDMETHODCALLTYPE d3d12_command_list_DiscardResource(d3d12_command_list_iface *iface,
        ID3D12Resource *resource, const D3D12_DISCARD_REGION *region)
{
    struct d3d12_command_list *list = impl_from_ID3D12GraphicsCommandList(iface);
    struct d3d12_resource *texture = unsafe_impl_from_ID3D12Resource(resource);
    unsigned int i, first_subresource, subresource_count;
    VkImageSubresourceLayers vk_subresource_layers;
    VkImageSubresource vk_subresource;
    D3D12_RECT full_rect;
    int attachment_idx;
    bool full_discard;

    TRACE("iface %p, resource %p, region %p.\n", iface, resource, region);

    /* This method is only supported on DIRECT and COMPUTE queues,
     * but we only implement it for render targets, so ignore it
     * on compute. */
    if (list->type != D3D12_COMMAND_LIST_TYPE_DIRECT)
    {
        WARN("Not supported for queue type %d.\n", list->type);
        return;
    }

    /* Ignore buffers */
    if (!d3d12_resource_is_texture(texture))
        return;

    /* D3D12 requires that the texture is either in render target
     * state or in depth-stencil state depending on usage flags */
    if (!(texture->desc.Flags & (D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL)))
    {
        WARN("Not supported for resource %p.\n", resource);
        return;
    }

    /* Assume that pRegion == NULL means that we should discard
     * the entire resource. This does not seem to be documented. */
    if (region)
    {
        first_subresource = region->FirstSubresource;
        subresource_count = region->NumSubresources;
    }
    else
    {
        first_subresource = 0;
        subresource_count = d3d12_resource_desc_get_sub_resource_count(&texture->desc);
    }

    /* We can't meaningfully discard sub-regions of an image. If rects
     * are specified, all specified subresources must have the same
     * dimensions, so just base this off the first subresource */
    if (!(full_discard = (!region || !region->NumRects)))
    {
        vk_subresource = d3d12_resource_get_vk_subresource(texture, first_subresource, false);
        full_rect = d3d12_get_image_rect(texture, vk_subresource.mipLevel);

        for (i = 0; i < region->NumRects && !full_discard; i++)
            full_discard = !memcmp(&region->pRects[i], &full_rect, sizeof(full_rect));
    }

    if (!full_discard)
        return;

    for (i = first_subresource; i < first_subresource + subresource_count; i++)
    {
        vk_subresource = d3d12_resource_get_vk_subresource(texture, i, false);
        vk_subresource_layers = vk_subresource_layers_from_subresource(&vk_subresource);
        attachment_idx = d3d12_command_list_find_attachment_view(list, texture, &vk_subresource_layers);

        if (attachment_idx < 0)
        {
            /* Image is not currently bound as an attachment */
            d3d12_command_list_end_current_render_pass(list, false);
            d3d12_command_list_discard_attachment_barrier(list, texture, &vk_subresource_layers, false);
        }
        else if (list->current_render_pass || list->render_pass_suspended)
        {
            /* Image is bound and the render pass is active */
            d3d12_command_list_end_current_render_pass(list, true);
            d3d12_command_list_discard_attachment_barrier(list, texture, &vk_subresource_layers, true);
        }
        else
        {
            /* Image is bound, but the render pass is not active */
            d3d12_command_list_discard_attachment_deferred(list, attachment_idx, vk_subresource.aspectMask);
        }
    }
}

static inline bool d3d12_query_type_is_inline(struct d3d12_device *device, D3D12_QUERY_TYPE type)
{
    if (device->vkd3d_instance->config_flags & VKD3D_CONFIG_FLAG_DISABLE_QUERY_OPTIMIZATION)
        return false;

    return type == D3D12_QUERY_TYPE_OCCLUSION ||
            type == D3D12_QUERY_TYPE_BINARY_OCCLUSION;
}

static inline bool d3d12_query_type_is_scoped(D3D12_QUERY_TYPE type)
{
    return type != D3D12_QUERY_TYPE_TIMESTAMP;
}

static void STDMETHODCALLTYPE d3d12_command_list_BeginQuery(d3d12_command_list_iface *iface,
        ID3D12QueryHeap *heap, D3D12_QUERY_TYPE type, UINT index)
{
    struct d3d12_command_list *list = impl_from_ID3D12GraphicsCommandList(iface);
    struct d3d12_query_heap *query_heap = unsafe_impl_from_ID3D12QueryHeap(heap);
    const struct vkd3d_vk_device_procs *vk_procs = &list->device->vk_procs;
    VkQueryControlFlags flags = d3d12_query_type_get_vk_flags(type);

    TRACE("iface %p, heap %p, type %#x, index %u.\n", iface, heap, type, index);

    if (!d3d12_query_type_is_scoped(type))
    {
        WARN("Query type %u is not scoped.\n", type);
        return;
    }

    d3d12_command_list_track_query_heap(list, query_heap);

    if (d3d12_query_type_is_inline(list->device, type))
    {
        if (!d3d12_command_list_reset_query(list, query_heap->vk_query_pool, index))
        {
            d3d12_command_list_end_current_render_pass(list, true);
            VK_CALL(vkCmdResetQueryPool(list->vk_command_buffer, query_heap->vk_query_pool, index, 1));
        }

        if (!d3d12_command_list_enable_query(list, query_heap, index, type))
            d3d12_command_list_mark_as_invalid(list, "Failed to enable virtual query.\n");
    }
    else
    {
        d3d12_command_list_end_current_render_pass(list, true);

        if (!d3d12_command_list_reset_query(list, query_heap->vk_query_pool, index))
            VK_CALL(vkCmdResetQueryPool(list->vk_command_buffer, query_heap->vk_query_pool, index, 1));

        if (d3d12_query_type_is_indexed(type))
        {
            unsigned int stream_index = type - D3D12_QUERY_TYPE_SO_STATISTICS_STREAM0;
            VK_CALL(vkCmdBeginQueryIndexedEXT(list->vk_command_buffer,
                    query_heap->vk_query_pool, index, flags, stream_index));
        }
        else
            VK_CALL(vkCmdBeginQuery(list->vk_command_buffer, query_heap->vk_query_pool, index, flags));
    }
}

static void STDMETHODCALLTYPE d3d12_command_list_EndQuery(d3d12_command_list_iface *iface,
        ID3D12QueryHeap *heap, D3D12_QUERY_TYPE type, UINT index)
{
    struct d3d12_command_list *list = impl_from_ID3D12GraphicsCommandList(iface);
    struct d3d12_query_heap *query_heap = unsafe_impl_from_ID3D12QueryHeap(heap);
    const struct vkd3d_vk_device_procs *vk_procs = &list->device->vk_procs;

    TRACE("iface %p, heap %p, type %#x, index %u.\n", iface, heap, type, index);

    d3d12_command_list_track_query_heap(list, query_heap);

    if (d3d12_query_type_is_inline(list->device, type))
    {
        d3d12_command_list_disable_query(list, query_heap->vk_query_pool, index);
    }
    else if (d3d12_query_type_is_scoped(type))
    {
        d3d12_command_list_end_current_render_pass(list, true);

        if (d3d12_query_type_is_indexed(type))
        {
            unsigned int stream_index = type - D3D12_QUERY_TYPE_SO_STATISTICS_STREAM0;
            VK_CALL(vkCmdEndQueryIndexedEXT(list->vk_command_buffer,
                    query_heap->vk_query_pool, index, stream_index));
        }
        else
            VK_CALL(vkCmdEndQuery(list->vk_command_buffer, query_heap->vk_query_pool, index));
    }
    else if (type == D3D12_QUERY_TYPE_TIMESTAMP)
    {
        if (!d3d12_command_list_reset_query(list, query_heap->vk_query_pool, index))
        {
            d3d12_command_list_end_current_render_pass(list, true);
            VK_CALL(vkCmdResetQueryPool(list->vk_command_buffer, query_heap->vk_query_pool, index, 1));
        }

        VK_CALL(vkCmdWriteTimestamp(list->vk_command_buffer,
                VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, query_heap->vk_query_pool, index));
    }
    else
        FIXME("Unhandled query type %u.\n", type);
}

static size_t get_query_stride(D3D12_QUERY_TYPE type)
{
    if (type == D3D12_QUERY_TYPE_PIPELINE_STATISTICS)
        return sizeof(D3D12_QUERY_DATA_PIPELINE_STATISTICS);

    if (D3D12_QUERY_TYPE_SO_STATISTICS_STREAM0 <= type && type <= D3D12_QUERY_TYPE_SO_STATISTICS_STREAM3)
        return sizeof(D3D12_QUERY_DATA_SO_STATISTICS);

    return sizeof(uint64_t);
}

static void d3d12_command_list_resolve_binary_occlusion_queries(struct d3d12_command_list *list,
        VkDeviceAddress src_va, VkDeviceAddress dst_va, UINT query_count)
{
    const struct vkd3d_query_ops *query_ops = &list->device->meta_ops.query;
    const struct vkd3d_vk_device_procs *vk_procs = &list->device->vk_procs;
    struct vkd3d_query_op_args args;
    unsigned int num_workgroups;
    VkMemoryBarrier vk_barrier;

    d3d12_command_list_invalidate_current_pipeline(list, true);
    d3d12_command_list_invalidate_root_parameters(list, VK_PIPELINE_BIND_POINT_COMPUTE, true);

    vk_barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    vk_barrier.pNext = NULL;
    vk_barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vk_barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    VK_CALL(vkCmdPipelineBarrier(list->vk_command_buffer,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            0, 1, &vk_barrier, 0, NULL, 0, NULL));

    args.src_queries = src_va;
    args.dst_queries = dst_va;
    args.query_count = query_count;

    num_workgroups = vkd3d_compute_workgroup_count(query_count, VKD3D_QUERY_OP_WORKGROUP_SIZE);
    VK_CALL(vkCmdBindPipeline(list->vk_command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE,
            query_ops->vk_resolve_binary_pipeline));
    VK_CALL(vkCmdPushConstants(list->vk_command_buffer, query_ops->vk_pipeline_layout,
            VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(args), &args));
    VK_CALL(vkCmdDispatch(list->vk_command_buffer, num_workgroups, 1, 1));

    vk_barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    vk_barrier.dstAccessMask = 0;
    VK_CALL(vkCmdPipelineBarrier(list->vk_command_buffer,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
            0, 1, &vk_barrier, 0, NULL, 0, NULL));
}

static void STDMETHODCALLTYPE d3d12_command_list_ResolveQueryData(d3d12_command_list_iface *iface,
        ID3D12QueryHeap *heap, D3D12_QUERY_TYPE type, UINT start_index, UINT query_count,
        ID3D12Resource *dst_buffer, UINT64 aligned_dst_buffer_offset)
{
    struct d3d12_query_heap *query_heap = unsafe_impl_from_ID3D12QueryHeap(heap);
    struct d3d12_command_list *list = impl_from_ID3D12GraphicsCommandList(iface);
    struct d3d12_resource *buffer = unsafe_impl_from_ID3D12Resource(dst_buffer);
    const struct vkd3d_vk_device_procs *vk_procs = &list->device->vk_procs;
    struct vkd3d_scratch_allocation scratch;
    size_t stride = get_query_stride(type);
    bool do_fixup = false;

    TRACE("iface %p, heap %p, type %#x, start_index %u, query_count %u, "
            "dst_buffer %p, aligned_dst_buffer_offset %#"PRIx64".\n",
            iface, heap, type, start_index, query_count,
            dst_buffer, aligned_dst_buffer_offset);

    if (!d3d12_resource_is_buffer(buffer))
    {
        WARN("Destination resource is not a buffer.\n");
        return;
    }

    if (type == D3D12_QUERY_TYPE_BINARY_OCCLUSION)
    {
        if (list->device->device_info.buffer_device_address_features.bufferDeviceAddress)
            do_fixup = d3d12_command_allocator_allocate_scratch_memory(list->allocator, stride * query_count, stride, &scratch);
        else
            FIXME_ONCE("bufferDeviceAddress not supported by device.\n");
    }

    if (!do_fixup)
    {
        scratch.buffer = buffer->vk_buffer;
        scratch.offset = buffer->heap_offset + aligned_dst_buffer_offset;
        scratch.va = 0;
    }

    d3d12_command_list_track_query_heap(list, query_heap);
    d3d12_command_list_read_query_range(list, query_heap->vk_query_pool, start_index, query_count);
    d3d12_command_list_end_current_render_pass(list, true);

    VK_CALL(vkCmdCopyQueryPoolResults(list->vk_command_buffer, query_heap->vk_query_pool,
            start_index, query_count, scratch.buffer, scratch.offset, stride,
            VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT));

    if (do_fixup)
    {
        d3d12_command_list_resolve_binary_occlusion_queries(list, scratch.va,
                d3d12_resource_get_va(buffer, aligned_dst_buffer_offset), query_count);
    }
}

static void STDMETHODCALLTYPE d3d12_command_list_SetPredication(d3d12_command_list_iface *iface,
        ID3D12Resource *buffer, UINT64 aligned_buffer_offset, D3D12_PREDICATION_OP operation)
{
    struct d3d12_command_list *list = impl_from_ID3D12GraphicsCommandList(iface);
    struct d3d12_resource *resource = unsafe_impl_from_ID3D12Resource(buffer);
    const struct vkd3d_vk_device_procs *vk_procs = &list->device->vk_procs;
    const struct vkd3d_predicate_ops *predicate_ops = &list->device->meta_ops.predicate;
    struct vkd3d_predicate_resolve_args resolve_args;
    VkConditionalRenderingBeginInfoEXT begin_info;
    VkPipelineStageFlags dst_stages, src_stages;
    struct vkd3d_scratch_allocation scratch;
    VkAccessFlags dst_access, src_access;
    VkMemoryBarrier vk_barrier;
    VkBufferCopy copy_region;

    TRACE("iface %p, buffer %p, aligned_buffer_offset %#"PRIx64", operation %#x.\n",
            iface, buffer, aligned_buffer_offset, operation);

    d3d12_command_list_end_current_render_pass(list, true);

    if (resource && (aligned_buffer_offset & 0x7))
        return;

    if (!list->device->device_info.buffer_device_address_features.bufferDeviceAddress &&
            !list->device->device_info.conditional_rendering_features.conditionalRendering)
    {
        FIXME_ONCE("Conditional rendering not supported by device.\n");
        return;
    }

    if (list->predicate_enabled)
        VK_CALL(vkCmdEndConditionalRenderingEXT(list->vk_command_buffer));

    if (resource)
    {
        if (!d3d12_command_allocator_allocate_scratch_memory(list->allocator,
                sizeof(uint32_t), sizeof(uint32_t), &scratch))
            return;

        begin_info.sType = VK_STRUCTURE_TYPE_CONDITIONAL_RENDERING_BEGIN_INFO_EXT;
        begin_info.pNext = NULL;
        begin_info.buffer = scratch.buffer;
        begin_info.offset = scratch.offset;
        begin_info.flags = 0;

        if (list->device->device_info.buffer_device_address_features.bufferDeviceAddress)
        {
            /* Resolve 64-bit predicate into a 32-bit location so that this works with
             * VK_EXT_conditional_rendering. We'll handle the predicate operation here
             * so setting VK_CONDITIONAL_RENDERING_INVERTED_BIT_EXT is not necessary. */
            d3d12_command_list_invalidate_current_pipeline(list, true);
            d3d12_command_list_invalidate_root_parameters(list, VK_PIPELINE_BIND_POINT_COMPUTE, true);

            resolve_args.src_va = d3d12_resource_get_va(resource, aligned_buffer_offset);
            resolve_args.dst_va = scratch.va;
            resolve_args.invert = operation != D3D12_PREDICATION_OP_EQUAL_ZERO;

            VK_CALL(vkCmdBindPipeline(list->vk_command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                    predicate_ops->vk_resolve_pipeline));
            VK_CALL(vkCmdPushConstants(list->vk_command_buffer, predicate_ops->vk_resolve_pipeline_layout,
                    VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(resolve_args), &resolve_args));
            VK_CALL(vkCmdDispatch(list->vk_command_buffer, 1, 1, 1));

            src_stages = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
            src_access = VK_ACCESS_SHADER_WRITE_BIT;
        }
        else
        {
            FIXME_ONCE("64-bit predicates not supported.\n");

            copy_region.srcOffset = resource->heap_offset + aligned_buffer_offset;
            copy_region.dstOffset = scratch.offset;
            copy_region.size = sizeof(uint32_t);

            VK_CALL(vkCmdCopyBuffer(list->vk_command_buffer,
                    resource->vk_buffer, scratch.buffer, 1, &copy_region));

            src_stages = VK_PIPELINE_STAGE_TRANSFER_BIT;
            src_access = VK_ACCESS_TRANSFER_WRITE_BIT;

            if (operation != D3D12_PREDICATION_OP_EQUAL_ZERO)
                begin_info.flags = VK_CONDITIONAL_RENDERING_INVERTED_BIT_EXT;
        }

        if (list->device->device_info.conditional_rendering_features.conditionalRendering)
        {
            dst_stages = VK_PIPELINE_STAGE_CONDITIONAL_RENDERING_BIT_EXT;
            dst_access = VK_ACCESS_CONDITIONAL_RENDERING_READ_BIT_EXT;
            list->predicate_enabled = true;
        }
        else
        {
            dst_stages = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
            dst_access = VK_ACCESS_SHADER_READ_BIT;
            list->predicate_va = scratch.va;
        }

        vk_barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        vk_barrier.pNext = NULL;
        vk_barrier.srcAccessMask = src_access;
        vk_barrier.dstAccessMask = dst_access;

        VK_CALL(vkCmdPipelineBarrier(list->vk_command_buffer,
                src_stages, dst_stages, 0, 1, &vk_barrier, 0, NULL, 0, NULL));

        if (list->predicate_enabled)
            VK_CALL(vkCmdBeginConditionalRenderingEXT(list->vk_command_buffer, &begin_info));
    }
    else
    {
        list->predicate_enabled = false;
        list->predicate_va = 0;
    }
}

static char *decode_pix_string(size_t wchar_size, UINT metadata, const void *data, size_t size)
{
    char *label_str;

#define PIX_EVENT_UNICODE_VERSION 0
#define PIX_EVENT_ANSI_VERSION 1
#define PIX_EVENT_PIX3BLOB_VERSION 2
    switch (metadata)
    {
    case PIX_EVENT_ANSI_VERSION:
        /* Be defensive in case the string is not nul-terminated. */
        label_str = vkd3d_malloc(size + 1);
        if (!label_str)
            return NULL;
        memcpy(label_str, data, size);
        label_str[size] = '\0';
        break;

    case PIX_EVENT_UNICODE_VERSION:
        label_str = vkd3d_strdup_w_utf8(data, wchar_size, size / wchar_size);
        if (!label_str)
            return NULL;
        break;

    case PIX_EVENT_PIX3BLOB_VERSION:
        FIXME("PIX3BLOB event format not supported.\n");
        return NULL;

    default:
        FIXME("Unrecognized metadata format %u for BeginEvent.\n", metadata);
        return NULL;
    }

    return label_str;
}

static void STDMETHODCALLTYPE d3d12_command_list_SetMarker(d3d12_command_list_iface *iface,
        UINT metadata, const void *data, UINT size)
{
    struct d3d12_command_list *list = impl_from_ID3D12GraphicsCommandList(iface);
    const struct vkd3d_vk_device_procs *vk_procs = &list->device->vk_procs;
    VkDebugUtilsLabelEXT label;
    char *label_str;
    unsigned int i;

    if (!list->device->vk_info.EXT_debug_utils)
        return;

    label_str = decode_pix_string(list->device->wchar_size, metadata, data, size);
    if (!label_str)
    {
        FIXME("Failed to decode PIX debug event.\n");
        return;
    }

    label.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT;
    label.pNext = NULL;
    label.pLabelName = label_str;
    for (i = 0; i < 4; i++)
        label.color[i] = 1.0f;

    VK_CALL(vkCmdInsertDebugUtilsLabelEXT(list->vk_command_buffer, &label));
    vkd3d_free(label_str);
}

static void STDMETHODCALLTYPE d3d12_command_list_BeginEvent(d3d12_command_list_iface *iface,
        UINT metadata, const void *data, UINT size)
{
    struct d3d12_command_list *list = impl_from_ID3D12GraphicsCommandList(iface);
    const struct vkd3d_vk_device_procs *vk_procs = &list->device->vk_procs;
    VkDebugUtilsLabelEXT label;
    char *label_str;
    unsigned int i;

    TRACE("iface %p, metadata %u, data %p, size %u.\n",
          iface, metadata, data, size);

    if (!list->device->vk_info.EXT_debug_utils)
        return;

    label_str = decode_pix_string(list->device->wchar_size, metadata, data, size);
    if (!label_str)
    {
        FIXME("Failed to decode PIX debug event.\n");
        return;
    }

    label.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT;
    label.pNext = NULL;
    label.pLabelName = label_str;
    for (i = 0; i < 4; i++)
        label.color[i] = 1.0f;

    VK_CALL(vkCmdBeginDebugUtilsLabelEXT(list->vk_command_buffer, &label));
    vkd3d_free(label_str);
}

static void STDMETHODCALLTYPE d3d12_command_list_EndEvent(d3d12_command_list_iface *iface)
{
    struct d3d12_command_list *list = impl_from_ID3D12GraphicsCommandList(iface);
    const struct vkd3d_vk_device_procs *vk_procs = &list->device->vk_procs;

    TRACE("iface %p.\n", iface);

    if (!list->device->vk_info.EXT_debug_utils)
        return;

    VK_CALL(vkCmdEndDebugUtilsLabelEXT(list->vk_command_buffer));
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
    const struct vkd3d_vk_device_procs *vk_procs = &list->device->vk_procs;
    const D3D12_COMMAND_SIGNATURE_DESC *signature_desc = &sig_impl->desc;
    struct vkd3d_scratch_allocation scratch;
    unsigned int i;

    TRACE("iface %p, command_signature %p, max_command_count %u, arg_buffer %p, "
            "arg_buffer_offset %#"PRIx64", count_buffer %p, count_buffer_offset %#"PRIx64".\n",
            iface, command_signature, max_command_count, arg_buffer, arg_buffer_offset,
            count_buffer, count_buffer_offset);

    if ((count_buffer || list->predicate_va) && !list->device->vk_info.KHR_draw_indirect_count)
    {
        FIXME("Count buffers not supported by Vulkan implementation.\n");
        return;
    }

    for (i = 0; i < signature_desc->NumArgumentDescs; ++i)
    {
        const D3D12_INDIRECT_ARGUMENT_DESC *arg_desc = &signature_desc->pArgumentDescs[i];

        if (list->predicate_va)
        {
            union vkd3d_predicate_command_direct_args args;
            enum vkd3d_predicate_command_type type;
            VkDeviceSize indirect_va;

            switch (arg_desc->Type)
            {
                case D3D12_INDIRECT_ARGUMENT_TYPE_DRAW:
                case D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED:
                    if (count_buffer)
                    {
                        type = VKD3D_PREDICATE_COMMAND_DRAW_INDIRECT_COUNT;
                        indirect_va = d3d12_resource_get_va(count_impl, count_buffer_offset);
                    }
                    else
                    {
                        args.draw_count = max_command_count;
                        type = VKD3D_PREDICATE_COMMAND_DRAW_INDIRECT;
                        indirect_va = 0;
                    }
                    break;

                case D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH:
                    type = VKD3D_PREDICATE_COMMAND_DISPATCH_INDIRECT;
                    indirect_va = d3d12_resource_get_va(arg_impl, arg_buffer_offset);
                    break;

                default:
                    FIXME("Ignoring unhandled argument type %#x.\n", arg_desc->Type);
                    continue;
            }

            if (!d3d12_command_list_emit_predicated_command(list, type, indirect_va, &args, &scratch))
                return;
        }
        else if (count_buffer)
        {
            scratch.buffer = count_impl->vk_buffer;
            scratch.offset = count_impl->heap_offset + count_buffer_offset;
        }
        else
        {
            scratch.buffer = arg_impl->vk_buffer;
            scratch.offset = arg_impl->heap_offset + arg_buffer_offset;
        }

        switch (arg_desc->Type)
        {
            case D3D12_INDIRECT_ARGUMENT_TYPE_DRAW:
                if (!d3d12_command_list_begin_render_pass(list))
                {
                    WARN("Failed to begin render pass, ignoring draw.\n");
                    break;
                }

                if (count_buffer || list->predicate_va)
                {
                    VK_CALL(vkCmdDrawIndirectCountKHR(list->vk_command_buffer, arg_impl->vk_buffer,
                            arg_buffer_offset + arg_impl->heap_offset, scratch.buffer, scratch.offset,
                            max_command_count, signature_desc->ByteStride));
                }
                else
                {
                    VK_CALL(vkCmdDrawIndirect(list->vk_command_buffer, arg_impl->vk_buffer,
                            arg_buffer_offset + arg_impl->heap_offset, max_command_count, signature_desc->ByteStride));
                }
                break;

            case D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED:
                if (!list->has_valid_index_buffer)
                {
                    FIXME_ONCE("Application attempts to perform an indexed draw call without index buffer bound.\n");
                    break;
                }

                if (!d3d12_command_list_begin_render_pass(list))
                {
                    WARN("Failed to begin render pass, ignoring draw.\n");
                    break;
                }

                d3d12_command_list_check_index_buffer_strip_cut_value(list);

                if (count_buffer || list->predicate_va)
                {
                    VK_CALL(vkCmdDrawIndexedIndirectCountKHR(list->vk_command_buffer, arg_impl->vk_buffer,
                            arg_buffer_offset + arg_impl->heap_offset, scratch.buffer, scratch.offset,
                            max_command_count, signature_desc->ByteStride));
                }
                else
                {
                    VK_CALL(vkCmdDrawIndexedIndirect(list->vk_command_buffer, arg_impl->vk_buffer,
                            arg_buffer_offset + arg_impl->heap_offset, max_command_count, signature_desc->ByteStride));
                }
                break;

            case D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH:
                if (max_command_count != 1)
                    FIXME("Ignoring command count %u.\n", max_command_count);

                if (count_buffer)
                    FIXME_ONCE("Count buffers not supported for indirect dispatch.\n");

                if (!d3d12_command_list_update_compute_state(list))
                {
                    WARN("Failed to update compute state, ignoring dispatch.\n");
                    return;
                }

                VK_CALL(vkCmdDispatchIndirect(list->vk_command_buffer, scratch.buffer, scratch.offset));
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

static bool vk_pipeline_stage_from_wbi_mode(D3D12_WRITEBUFFERIMMEDIATE_MODE mode, VkPipelineStageFlagBits *stage)
{
    switch (mode)
    {
        /* It is not entirely clear what DEFAULT is supposed
         * to do exactly, so treat it the same way as IN */
        case D3D12_WRITEBUFFERIMMEDIATE_MODE_DEFAULT:
        case D3D12_WRITEBUFFERIMMEDIATE_MODE_MARKER_IN:
            *stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            return true;

        case D3D12_WRITEBUFFERIMMEDIATE_MODE_MARKER_OUT:
            *stage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
            return true;

        default:
            return false;
    }
}

static void STDMETHODCALLTYPE d3d12_command_list_WriteBufferImmediate(d3d12_command_list_iface *iface,
        UINT count, const D3D12_WRITEBUFFERIMMEDIATE_PARAMETER *parameters,
        const D3D12_WRITEBUFFERIMMEDIATE_MODE *modes)
{
    struct d3d12_command_list *list = impl_from_ID3D12GraphicsCommandList(iface);
    VkPipelineStageFlags wait_stage_mask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    const struct vkd3d_vk_device_procs *vk_procs = &list->device->vk_procs;
    VkPipelineStageFlagBits stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    struct d3d12_resource *resource;
    VkDeviceSize offset;
    unsigned int i;

    TRACE("iface %p, count %u, parameters %p, modes %p.\n", iface, count, parameters, modes);

    for (i = 0; i < count; ++i)
    {
        if (!(resource = vkd3d_gpu_va_allocator_dereference(&list->device->gpu_va_allocator, parameters[i].Dest)))
        {
            d3d12_command_list_mark_as_invalid(list, "Invalid target address %p.\n", parameters[i].Dest);
            return;
        }

        offset = parameters[i].Dest - resource->gpu_address;

        if (modes && !vk_pipeline_stage_from_wbi_mode(modes[i], &stage))
        {
            d3d12_command_list_mark_as_invalid(list, "Invalid mode %u.\n", modes[i]);
            return;
        }

        if (list->device->vk_info.AMD_buffer_marker)
        {
            VK_CALL(vkCmdWriteBufferMarkerAMD(list->vk_command_buffer, stage,
                    resource->vk_buffer, offset, parameters[i].Value));
        }
        else
        {
            d3d12_command_list_end_current_render_pass(list, true);

            if (!(wait_stage_mask & stage))
            {
                VK_CALL(vkCmdPipelineBarrier(list->vk_command_buffer, stage,
                        VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 0, NULL));
                wait_stage_mask |= stage;
            }

            VK_CALL(vkCmdUpdateBuffer(list->vk_command_buffer, resource->vk_buffer,
                    offset, sizeof(parameters[i].Value), &parameters[i].Value));
        }
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

static CONST_VTBL struct ID3D12GraphicsCommandList5Vtbl d3d12_command_list_vtbl =
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

#ifdef VKD3D_ENABLE_PROFILING
#include "command_list_profiled.h"
#endif

static struct d3d12_command_list *unsafe_impl_from_ID3D12CommandList(ID3D12CommandList *iface)
{
    if (!iface)
        return NULL;
#ifdef VKD3D_ENABLE_PROFILING
    assert(iface->lpVtbl == (struct ID3D12CommandListVtbl *)&d3d12_command_list_vtbl ||
           iface->lpVtbl == (struct ID3D12CommandListVtbl *)&d3d12_command_list_vtbl_profiled);
#else
    assert(iface->lpVtbl == (struct ID3D12CommandListVtbl *)&d3d12_command_list_vtbl);
#endif
    return CONTAINING_RECORD(iface, struct d3d12_command_list, ID3D12GraphicsCommandList_iface);
}

static HRESULT d3d12_command_list_init(struct d3d12_command_list *list, struct d3d12_device *device,
        D3D12_COMMAND_LIST_TYPE type, struct d3d12_command_allocator *allocator,
        ID3D12PipelineState *initial_pipeline_state)
{
    HRESULT hr;

    memset(list, 0, sizeof(*list));

#ifdef VKD3D_ENABLE_PROFILING
    if (vkd3d_uses_profiling())
        list->ID3D12GraphicsCommandList_iface.lpVtbl = &d3d12_command_list_vtbl_profiled;
    else
        list->ID3D12GraphicsCommandList_iface.lpVtbl = &d3d12_command_list_vtbl;
#else
    list->ID3D12GraphicsCommandList_iface.lpVtbl = &d3d12_command_list_vtbl;
#endif

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

#ifdef VKD3D_BUILD_STANDALONE_D3D12
    if (IsEqualGUID(riid, &IID_IWineDXGISwapChainFactory))
    {
        struct d3d12_command_queue *command_queue = impl_from_ID3D12CommandQueue(iface);
        IWineDXGISwapChainFactory_AddRef(&command_queue->swapchain_factory.IWineDXGISwapChainFactory_iface);
        *object = &command_queue->swapchain_factory;
        return S_OK;
    }
#endif

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

        vkd3d_private_store_destroy(&command_queue->private_store);

        d3d12_command_queue_submit_stop(command_queue);
        pthread_join(command_queue->submission_thread, NULL);
        pthread_mutex_destroy(&command_queue->queue_lock);
        pthread_cond_destroy(&command_queue->queue_cond);

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
            VK_OBJECT_TYPE_QUEUE, name);
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
    sub.bind_sparse.mode = VKD3D_SPARSE_MEMORY_BIND_MODE_UPDATE;
    sub.bind_sparse.bind_count = 0;
    sub.bind_sparse.bind_infos = NULL;
    sub.bind_sparse.dst_resource = res;
    sub.bind_sparse.src_resource = NULL;

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
                if (!vkd3d_array_reserve((void **)&sub.bind_sparse.bind_infos, &bind_infos_size,
                        sub.bind_sparse.bind_count + 1, sizeof(*sub.bind_sparse.bind_infos)))
                {
                    ERR("Failed to allocate bind info array.\n");
                    goto fail;
                }

                bind = &sub.bind_sparse.bind_infos[sub.bind_sparse.bind_count++];
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
    vkd3d_free(sub.bind_sparse.bind_infos);
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
    sub.bind_sparse.mode = VKD3D_SPARSE_MEMORY_BIND_MODE_COPY;
    sub.bind_sparse.bind_count = region_size->NumTiles;
    sub.bind_sparse.bind_infos = vkd3d_malloc(region_size->NumTiles * sizeof(*sub.bind_sparse.bind_infos));
    sub.bind_sparse.dst_resource = dst_res;
    sub.bind_sparse.src_resource = src_res;

    if (!sub.bind_sparse.bind_infos)
    {
        ERR("Failed to allocate bind info array.\n");
        return;
    }

    for (i = 0; i < region_size->NumTiles; i++)
    {
        bind = &sub.bind_sparse.bind_infos[i];
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
    struct vkd3d_initial_transition *transitions;
    size_t num_transitions, num_command_buffers;
    struct d3d12_command_queue_submission sub;
    struct d3d12_command_list *cmd_list;
    VkCommandBuffer *buffers;
    LONG **outstanding;
    unsigned int i, j;

    TRACE("iface %p, command_list_count %u, command_lists %p.\n",
            iface, command_list_count, command_lists);

    if (!command_list_count)
        return;

    num_command_buffers = command_list_count + 1;

    for (i = 0; i < command_list_count; ++i)
    {
        cmd_list = unsafe_impl_from_ID3D12CommandList(command_lists[i]);

        if (cmd_list->vk_init_commands)
            num_command_buffers++;
    }

    if (!(buffers = vkd3d_calloc(num_command_buffers, sizeof(*buffers))))
    {
        ERR("Failed to allocate command buffer array.\n");
        return;
    }

    if (!(outstanding = vkd3d_calloc(command_list_count, sizeof(*outstanding))))
    {
        ERR("Failed to allocate outstanding submissions count.\n");
        return;
    }

    sub.execute.debug_capture = false;

    num_transitions = 0;

    for (i = 0, j = 0; i < command_list_count; ++i)
    {
        cmd_list = unsafe_impl_from_ID3D12CommandList(command_lists[i]);

        if (cmd_list->is_recording)
        {
            d3d12_device_mark_as_removed(command_queue->device, DXGI_ERROR_INVALID_CALL,
                    "Command list %p is in recording state.\n", command_lists[i]);
            vkd3d_free(outstanding);
            vkd3d_free(buffers);
            return;
        }

        num_transitions += cmd_list->init_transitions_count;

        outstanding[i] = cmd_list->outstanding_submissions_count;
        InterlockedIncrement(outstanding[i]);

        if (cmd_list->vk_init_commands)
            buffers[j++] = cmd_list->vk_init_commands;
        buffers[j++] = cmd_list->vk_command_buffer;
        if (cmd_list->debug_capture)
            sub.execute.debug_capture = true;
    }

    /* Append a full GPU barrier between submissions.
     * This command buffer is SIMULTANEOUS_BIT. */
    buffers[j++] = command_queue->vkd3d_queue->barrier_command_buffer;

    if (command_list_count == 1 && num_transitions != 0)
    {
        /* Pilfer directly. */
        cmd_list = unsafe_impl_from_ID3D12CommandList(command_lists[0]);
        sub.execute.transitions = cmd_list->init_transitions;
        sub.execute.transition_count = cmd_list->init_transitions_count;
        cmd_list->init_transitions = NULL;
        cmd_list->init_transitions_count = 0;
        cmd_list->init_transitions_size = 0;
    }
    else if (num_transitions != 0)
    {
        sub.execute.transitions = vkd3d_malloc(num_transitions * sizeof(*sub.execute.transitions));
        sub.execute.transition_count = num_transitions;
        transitions = sub.execute.transitions;
        for (i = 0; i < command_list_count; ++i)
        {
            cmd_list = unsafe_impl_from_ID3D12CommandList(command_lists[i]);
            memcpy(transitions, cmd_list->init_transitions,
                   cmd_list->init_transitions_count * sizeof(*transitions));
            transitions += cmd_list->init_transitions_count;
        }
    }
    else
    {
        sub.execute.transitions = NULL;
        sub.execute.transition_count = 0;
    }

    sub.type = VKD3D_SUBMISSION_EXECUTE;
    sub.execute.cmd = buffers;
    sub.execute.cmd_count = num_command_buffers;
    sub.execute.outstanding_submissions_counters = outstanding;
    sub.execute.outstanding_submissions_counter_count = command_list_count;
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
    sub.signal.fence = fence;
    sub.signal.value = value;
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
    sub.wait.fence = fence;
    sub.wait.value = value;
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
#ifdef _WIN32
    struct d3d12_command_queue *command_queue = impl_from_ID3D12CommandQueue(iface);
    struct d3d12_device *device = command_queue->device;
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    VkCalibratedTimestampInfoEXT timestamp_infos[2], *timestamp_info;
    uint64_t max_deviation, timestamps[2];
    LARGE_INTEGER qpc_begin, qpc_end;
    uint32_t count = 0;
    VkResult vr;

    TRACE("iface %p, gpu_timestamp %p, cpu_timestamp %p.\n",
            iface, gpu_timestamp, cpu_timestamp);

    if (!command_queue->vkd3d_queue->timestamp_bits)
    {
        WARN("Timestamp queries not supported.\n");
        return E_FAIL;
    }

    if (!(device->device_info.time_domains & VKD3D_TIME_DOMAIN_DEVICE))
    {
        FIXME("Calibrated timestamps not supported by device.\n");
        *gpu_timestamp = 0;
        *cpu_timestamp = 0;
        return S_OK;
    }

    timestamp_info = &timestamp_infos[count++];
    timestamp_info->sType = VK_STRUCTURE_TYPE_CALIBRATED_TIMESTAMP_INFO_EXT;
    timestamp_info->pNext = NULL;
    timestamp_info->timeDomain = VK_TIME_DOMAIN_DEVICE_EXT;

    if (device->device_info.time_domains & VKD3D_TIME_DOMAIN_QPC)
    {
        timestamp_info = &timestamp_infos[count++];
        timestamp_info->sType = VK_STRUCTURE_TYPE_CALIBRATED_TIMESTAMP_INFO_EXT;
        timestamp_info->pNext = NULL;
        timestamp_info->timeDomain = VK_TIME_DOMAIN_QUERY_PERFORMANCE_COUNTER_EXT;
    }
    else
    {
        FIXME_ONCE("QPC domain not supported by device, timestamp calibration may be inaccurate.\n");
        QueryPerformanceCounter(&qpc_begin);
    }

    if ((vr = VK_CALL(vkGetCalibratedTimestampsEXT(device->vk_device,
            2, timestamp_infos, timestamps, &max_deviation))) < 0)
    {
        ERR("Querying calibrated timestamps failed, vr %d.\n", vr);
        return hresult_from_vk_result(vr);
    }

    if (!(device->device_info.time_domains & VKD3D_TIME_DOMAIN_QPC))
    {
        QueryPerformanceCounter(&qpc_end);
        timestamps[1] = qpc_begin.QuadPart + (qpc_end.QuadPart - qpc_begin.QuadPart) / 2;
    }

    *gpu_timestamp = timestamps[0];
    *cpu_timestamp = timestamps[1];
    return S_OK;
#else
    FIXME("Calibrated timestamps not supported.\n");
    *gpu_timestamp = 0;
    *cpu_timestamp = 0;
    return S_OK;
#endif
}

static D3D12_COMMAND_QUEUE_DESC * STDMETHODCALLTYPE d3d12_command_queue_GetDesc(ID3D12CommandQueue *iface,
        D3D12_COMMAND_QUEUE_DESC *desc)
{
    struct d3d12_command_queue *command_queue = impl_from_ID3D12CommandQueue(iface);

    TRACE("iface %p, desc %p.\n", iface, desc);

    *desc = command_queue->desc;
    return desc;
}

static CONST_VTBL struct ID3D12CommandQueueVtbl d3d12_command_queue_vtbl =
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
    const VkPipelineStageFlags wait_stage_mask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    VkTimelineSemaphoreSubmitInfoKHR timeline_submit_info;
    const struct vkd3d_vk_device_procs *vk_procs;
    struct vkd3d_queue *queue;
    VkSubmitInfo submit_info;
    uint64_t wait_count;
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

    TRACE("queue %p, fence %p, value %#"PRIx64".\n", command_queue, fence, value);

    vk_procs = &command_queue->device->vk_procs;
    queue = command_queue->vkd3d_queue;

    wait_count = d3d12_fence_get_physical_wait_value_locked(fence, value);

    /* We can unlock the fence here. The queue semaphore will not be signalled to signal_value
     * until we have submitted, so the semaphore cannot be destroyed before the call to vkQueueSubmit. */
    d3d12_fence_unlock(fence);

    assert(fence->timeline_semaphore);
    timeline_submit_info.sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO_KHR;
    timeline_submit_info.pNext = NULL;
    timeline_submit_info.waitSemaphoreValueCount = 1;
    timeline_submit_info.pWaitSemaphoreValues = &wait_count;
    timeline_submit_info.signalSemaphoreValueCount = 0;
    timeline_submit_info.pSignalSemaphoreValues = NULL;

    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.pNext = &timeline_submit_info;
    submit_info.waitSemaphoreCount = 1;
    submit_info.pWaitSemaphores = &fence->timeline_semaphore;
    submit_info.pWaitDstStageMask = &wait_stage_mask;
    submit_info.commandBufferCount = 0;
    submit_info.pCommandBuffers = NULL;
    submit_info.signalSemaphoreCount = 0;
    submit_info.pSignalSemaphores = NULL;

    if (!(vk_queue = vkd3d_queue_acquire(queue)))
    {
        ERR("Failed to acquire queue %p.\n", queue);
        return;
    }

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
    uint64_t physical_value;
    uint64_t signal_value;
    VkQueue vk_queue;
    VkResult vr;
    HRESULT hr;

    device = command_queue->device;
    vk_procs = &device->vk_procs;
    vkd3d_queue = command_queue->vkd3d_queue;

    d3d12_fence_lock(fence);

    TRACE("queue %p, fence %p, value %#"PRIx64".\n", command_queue, fence, value);

    physical_value = d3d12_fence_add_pending_signal_locked(fence, value);

    signal_value = physical_value;

    /* Need to hold the fence lock while we're submitting, since another thread could come in and signal the semaphore
     * to a higher value before we call vkQueueSubmit, which creates a non-monotonically increasing value. */
    timeline_submit_info.sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO_KHR;
    timeline_submit_info.pNext = NULL;
    timeline_submit_info.waitSemaphoreValueCount = 0;
    timeline_submit_info.pWaitSemaphoreValues = NULL;
    timeline_submit_info.signalSemaphoreValueCount = 1;
    timeline_submit_info.pSignalSemaphoreValues = &signal_value;

    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.pNext = &timeline_submit_info;
    submit_info.waitSemaphoreCount = 0;
    submit_info.pWaitSemaphores = NULL;
    submit_info.pWaitDstStageMask = NULL;
    submit_info.commandBufferCount = 0;
    submit_info.pCommandBuffers = NULL;
    submit_info.signalSemaphoreCount = 1;
    submit_info.pSignalSemaphores = &fence->timeline_semaphore;

    if (!(vk_queue = vkd3d_queue_acquire(vkd3d_queue)))
    {
        ERR("Failed to acquire queue %p.\n", vkd3d_queue);
        d3d12_fence_unlock(fence);
        return;
    }

    vr = VK_CALL(vkQueueSubmit(vk_queue, 1, &submit_info, VK_NULL_HANDLE));

    if (vr == VK_SUCCESS)
        d3d12_fence_update_max_pending_value_locked(fence);
    d3d12_fence_unlock(fence);

    vkd3d_queue_release(vkd3d_queue);

    if (vr < 0)
    {
        ERR("Failed to submit signal operation, vr %d.\n", vr);
        return;
    }

    if (FAILED(hr = vkd3d_enqueue_timeline_semaphore(&device->fence_worker, fence, physical_value, vkd3d_queue)))
    {
        /* In case of an unexpected failure, try to safely destroy Vulkan objects. */
        vkd3d_queue_wait_idle(vkd3d_queue, vk_procs);
    }

    /* We should probably trigger DEVICE_REMOVED if we hit any errors in the submission thread. */
}

#define VKD3D_COMMAND_QUEUE_NUM_TRANSITION_BUFFERS 16
struct d3d12_command_queue_transition_pool
{
    VkCommandBuffer cmd[VKD3D_COMMAND_QUEUE_NUM_TRANSITION_BUFFERS];
    VkCommandPool pool;
    VkSemaphore timeline;
    uint64_t timeline_value;

    VkImageMemoryBarrier *barriers;
    size_t barriers_size;
    size_t barriers_count;

    const struct d3d12_query_heap **query_heaps;
    size_t query_heaps_size;
    size_t query_heaps_count;
};

static HRESULT d3d12_command_queue_transition_pool_init(struct d3d12_command_queue_transition_pool *pool,
        struct d3d12_command_queue *queue)
{
    const struct vkd3d_vk_device_procs *vk_procs = &queue->device->vk_procs;
    VkCommandBufferAllocateInfo alloc_info;
    VkCommandPoolCreateInfo pool_info;
    VkResult vr;
    HRESULT hr;

    memset(pool, 0, sizeof(*pool));

    pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool_info.pNext = NULL;
    pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pool_info.queueFamilyIndex = queue->vkd3d_queue->vk_family_index;

    if ((vr = VK_CALL(vkCreateCommandPool(queue->device->vk_device, &pool_info, NULL, &pool->pool))))
        return hresult_from_vk_result(vr);

    alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc_info.pNext = NULL;
    alloc_info.commandPool = pool->pool;
    alloc_info.commandBufferCount = VKD3D_COMMAND_QUEUE_NUM_TRANSITION_BUFFERS;
    alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    if ((vr = VK_CALL(vkAllocateCommandBuffers(queue->device->vk_device, &alloc_info, pool->cmd))))
        return hresult_from_vk_result(vr);

    if (FAILED(hr = vkd3d_create_timeline_semaphore(queue->device, 0, &pool->timeline)))
        return hr;

    return S_OK;
}

static void d3d12_command_queue_transition_pool_wait(struct d3d12_command_queue_transition_pool *pool,
        struct d3d12_device *device, uint64_t value)
{
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    VkSemaphoreWaitInfoKHR wait_info;

    wait_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO_KHR;
    wait_info.pNext = NULL;
    wait_info.flags = 0;
    wait_info.pSemaphores = &pool->timeline;
    wait_info.semaphoreCount = 1;
    wait_info.pValues = &value;
    VK_CALL(vkWaitSemaphoresKHR(device->vk_device, &wait_info, ~(uint64_t)0));
}

static void d3d12_command_queue_transition_pool_deinit(struct d3d12_command_queue_transition_pool *pool,
        struct d3d12_device *device)
{
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    d3d12_command_queue_transition_pool_wait(pool, device, pool->timeline_value);
    VK_CALL(vkDestroyCommandPool(device->vk_device, pool->pool, NULL));
    VK_CALL(vkDestroySemaphore(device->vk_device, pool->timeline, NULL));
    vkd3d_free(pool->barriers);
    vkd3d_free((void*)pool->query_heaps);
}

static void d3d12_command_queue_transition_pool_add_barrier(struct d3d12_command_queue_transition_pool *pool,
            const struct d3d12_resource *resource)
{
    VkImageMemoryBarrier *barrier;
    assert(d3d12_resource_is_texture(resource));

    if (!vkd3d_array_reserve((void**)&pool->barriers, &pool->barriers_size,
            pool->barriers_count + 1, sizeof(*pool->barriers)))
    {
        ERR("Failed to allocate barriers.\n");
        return;
    }

    barrier = &pool->barriers[pool->barriers_count++];

    barrier->sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier->pNext = NULL;
    barrier->srcAccessMask = 0;
    barrier->dstAccessMask = 0;
    barrier->oldLayout = d3d12_resource_is_cpu_accessible(resource)
                        ? VK_IMAGE_LAYOUT_PREINITIALIZED : VK_IMAGE_LAYOUT_UNDEFINED;
    barrier->newLayout = resource->common_layout;
    barrier->srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier->dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier->image = resource->vk_image;
    barrier->subresourceRange.aspectMask = resource->format->vk_aspect_mask;
    barrier->subresourceRange.baseMipLevel = 0;
    barrier->subresourceRange.levelCount = VK_REMAINING_MIP_LEVELS;
    barrier->subresourceRange.baseArrayLayer = 0;
    barrier->subresourceRange.layerCount = VK_REMAINING_ARRAY_LAYERS;

    /* srcAccess and dstAccess mask is 0 since we will use the timeline semaphore to synchronize anyways. */

    TRACE("Initial layout transition for resource %p (old layout %#x, new layout %#x).\n",
          resource, barrier->oldLayout, barrier->newLayout);
}

static void d3d12_command_queue_transition_pool_add_query_heap(struct d3d12_command_queue_transition_pool *pool,
            const struct d3d12_query_heap *heap)
{
    if (!vkd3d_array_reserve((void**)&pool->query_heaps, &pool->query_heaps_size,
            pool->query_heaps_count + 1, sizeof(*pool->query_heaps)))
    {
        ERR("Failed to allocate query heap list.\n");
        return;
    }

    pool->query_heaps[pool->query_heaps_count++] = heap;

    TRACE("Initialization for query heap %p.\n", heap);
}

static void d3d12_command_queue_init_query_heap(struct d3d12_device *device, VkCommandBuffer vk_cmd_buffer,
        const struct d3d12_query_heap *heap)
{
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    unsigned int i;

    VK_CALL(vkCmdResetQueryPool(vk_cmd_buffer, heap->vk_query_pool, 0, heap->desc.Count));

    for (i = 0; i < heap->desc.Count; i++)
    {
        switch (heap->desc.Type)
        {
            case D3D12_QUERY_HEAP_TYPE_OCCLUSION:
            case D3D12_QUERY_HEAP_TYPE_SO_STATISTICS:
            case D3D12_QUERY_HEAP_TYPE_PIPELINE_STATISTICS:
                VK_CALL(vkCmdBeginQuery(vk_cmd_buffer, heap->vk_query_pool, i, 0));
                VK_CALL(vkCmdEndQuery(vk_cmd_buffer, heap->vk_query_pool, i));
                break;

            case D3D12_QUERY_HEAP_TYPE_TIMESTAMP:
            case D3D12_QUERY_HEAP_TYPE_COPY_QUEUE_TIMESTAMP:
                VK_CALL(vkCmdWriteTimestamp(vk_cmd_buffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                        heap->vk_query_pool, i));
                break;

            default:
                ERR("Unhandled query pool type %u.\n", heap->desc.Type);
                return;
        }
    }
}

static void d3d12_command_queue_transition_pool_build(struct d3d12_command_queue_transition_pool *pool,
        struct d3d12_device *device, const struct vkd3d_initial_transition *transitions, size_t count,
        VkCommandBuffer *vk_cmd_buffer, uint64_t *timeline_value)
{
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    const struct vkd3d_initial_transition *transition;
    VkCommandBufferBeginInfo begin_info;
    unsigned int command_index;
    uint32_t need_transition;
    size_t i;

    pool->barriers_count = 0;
    pool->query_heaps_count = 0;

    if (!count)
    {
        *vk_cmd_buffer = VK_NULL_HANDLE;
        return;
    }

    for (i = 0; i < count; i++)
    {
        transition = &transitions[i];

        switch (transition->type)
        {
            case VKD3D_INITIAL_TRANSITION_TYPE_RESOURCE:
                /* Memory order can be relaxed since this only needs to return 1 once.
                 * Ordering is guaranteed by synchronization between queues.
                 * A Signal() -> Wait() pair on the queue will guarantee that this step is done in execution order. */
                need_transition = vkd3d_atomic_uint32_exchange_explicit(&transition->resource.resource->initial_layout_transition,
                        0, vkd3d_memory_order_relaxed);

                if (need_transition && transition->resource.perform_initial_transition)
                    d3d12_command_queue_transition_pool_add_barrier(pool, transition->resource.resource);
                break;

            case VKD3D_INITIAL_TRANSITION_TYPE_QUERY_HEAP:
                if (!vkd3d_atomic_uint32_exchange_explicit(&transition->query_heap->initialized, 1, vkd3d_memory_order_relaxed))
                    d3d12_command_queue_transition_pool_add_query_heap(pool, transition->query_heap);
                break;

            default:
                ERR("Unhandled transition type %u.\n", transition->type);
        }
    }

    if (!pool->barriers_count && !pool->query_heaps_count)
    {
        *vk_cmd_buffer = VK_NULL_HANDLE;
        return;
    }

    pool->timeline_value++;
    command_index = pool->timeline_value % VKD3D_COMMAND_QUEUE_NUM_TRANSITION_BUFFERS;

    if (pool->timeline_value > VKD3D_COMMAND_QUEUE_NUM_TRANSITION_BUFFERS)
        d3d12_command_queue_transition_pool_wait(pool, device, pool->timeline_value - VKD3D_COMMAND_QUEUE_NUM_TRANSITION_BUFFERS);

    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.pNext = NULL;
    begin_info.pInheritanceInfo = NULL;
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    VK_CALL(vkResetCommandBuffer(pool->cmd[command_index], 0));
    VK_CALL(vkBeginCommandBuffer(pool->cmd[command_index], &begin_info));
    VK_CALL(vkCmdPipelineBarrier(pool->cmd[command_index],
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
            0, 0, NULL, 0, NULL, pool->barriers_count, pool->barriers));
    for (i = 0; i < pool->query_heaps_count; i++)
        d3d12_command_queue_init_query_heap(device, pool->cmd[command_index], pool->query_heaps[i]);
    VK_CALL(vkEndCommandBuffer(pool->cmd[command_index]));

    *vk_cmd_buffer = pool->cmd[command_index];
    *timeline_value = pool->timeline_value;
}

static void d3d12_command_queue_execute(struct d3d12_command_queue *command_queue,
        VkCommandBuffer *cmd, UINT count,
        VkCommandBuffer transition_cmd, VkSemaphore transition_timeline, uint64_t transition_timeline_value,
        bool debug_capture)
{
    static const VkPipelineStageFlags wait_stage_mask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    const struct vkd3d_vk_device_procs *vk_procs = &command_queue->device->vk_procs;
    VkTimelineSemaphoreSubmitInfoKHR timeline_submit_info[2];
    VkSubmitInfo submit_desc[2];
    uint32_t num_submits;
    VkQueue vk_queue;
    unsigned int i;
    VkResult vr;

    TRACE("queue %p, command_list_count %u, command_lists %p.\n",
          command_queue, count, cmd);

    memset(timeline_submit_info, 0, sizeof(timeline_submit_info));
    memset(submit_desc, 0, sizeof(submit_desc));

    if (transition_cmd)
    {
        /* The transition cmd must happen in-order, since with the advanced aliasing model in D3D12,
         * it is enough to separate aliases with an ExecuteCommandLists.
         * A clear-like operation must still happen though in the application which would acquire the alias,
         * but we must still be somewhat careful about when we emit initial state transitions.
         * The clear requirement only exists for render targets. */
        num_submits = 2;

        submit_desc[0].signalSemaphoreCount = 1;
        submit_desc[0].pSignalSemaphores = &transition_timeline;
        submit_desc[0].commandBufferCount = 1;
        submit_desc[0].pCommandBuffers = &transition_cmd;

        timeline_submit_info[0].signalSemaphoreValueCount = 1;
        /* Could use the serializing binary semaphore here,
         * but we need to keep track of the timeline on CPU as well
         * to know when we can reset the barrier command buffer. */
        timeline_submit_info[0].pSignalSemaphoreValues = &transition_timeline_value;

        submit_desc[1].waitSemaphoreCount = 1;
        timeline_submit_info[1].waitSemaphoreValueCount = 1;
        timeline_submit_info[1].pWaitSemaphoreValues = &transition_timeline_value;
        submit_desc[1].pWaitSemaphores = &transition_timeline;
        submit_desc[1].pWaitDstStageMask = &wait_stage_mask;
    }
    else
    {
        num_submits = 1;
    }

    submit_desc[num_submits - 1].commandBufferCount = count;
    submit_desc[num_submits - 1].pCommandBuffers = cmd;

    for (i = 0; i < num_submits; i++)
    {
        submit_desc[i].sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

        if (submit_desc[i].waitSemaphoreCount || submit_desc[i].signalSemaphoreCount)
        {
            timeline_submit_info[i].sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO_KHR;
            submit_desc[i].pNext = &timeline_submit_info[i];
        }
    }

    if (!(vk_queue = vkd3d_queue_acquire(command_queue->vkd3d_queue)))
    {
        ERR("Failed to acquire queue %p.\n", command_queue->vkd3d_queue);
        return;
    }

#ifdef VKD3D_ENABLE_RENDERDOC
    /* For each submission we have marked to be captured, we will first need to filter it
     * based on VKD3D_AUTO_CAPTURE_COUNTS.
     * If a submission index is not marked to be captured after all, we drop any capture here.
     * Deciding this in the submission thread is more robust than the alternative, since the submission
     * threads are mostly serialized. */
    if (debug_capture)
        debug_capture = vkd3d_renderdoc_command_queue_begin_capture(command_queue);
#else
    (void)debug_capture;
#endif

    if ((vr = VK_CALL(vkQueueSubmit(vk_queue, num_submits, submit_desc, VK_NULL_HANDLE))) < 0)
        ERR("Failed to submit queue(s), vr %d.\n", vr);

#ifdef VKD3D_ENABLE_RENDERDOC
    if (debug_capture)
        vkd3d_renderdoc_command_queue_end_capture(command_queue);
#endif

    vkd3d_queue_release(command_queue->vkd3d_queue);
}

static void d3d12_command_queue_bind_sparse(struct d3d12_command_queue *command_queue,
        enum vkd3d_sparse_memory_bind_mode mode, struct d3d12_resource *dst_resource,
        struct d3d12_resource *src_resource, unsigned int count,
        struct vkd3d_sparse_memory_bind *bind_infos)
{
    const VkPipelineStageFlags wait_stages = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    VkSparseImageOpaqueMemoryBindInfo opaque_info;
    const struct vkd3d_vk_device_procs *vk_procs;
    VkSparseImageMemoryBind *image_binds = NULL;
    VkSparseBufferMemoryBindInfo buffer_info;
    VkSparseMemoryBind *memory_binds = NULL;
    VkSparseImageMemoryBindInfo image_info;
    VkBindSparseInfo bind_sparse_info;
    struct vkd3d_queue *queue_sparse;
    unsigned int first_packed_tile;
    struct vkd3d_queue *queue;
    VkSubmitInfo submit_info;
    VkDeviceMemory vk_memory;
    VkQueue vk_queue_sparse;
    VkDeviceSize vk_offset;
    unsigned int i, j, k;
    VkQueue vk_queue;
    VkResult vr;

    TRACE("queue %p, dst_resource %p, src_resource %p, count %u, bind_infos %p.\n",
          command_queue, dst_resource, src_resource, count, bind_infos);

    vk_procs = &command_queue->device->vk_procs;

    bind_sparse_info.sType = VK_STRUCTURE_TYPE_BIND_SPARSE_INFO;
    bind_sparse_info.pNext = NULL;
    bind_sparse_info.bufferBindCount = 0;
    bind_sparse_info.pBufferBinds = NULL;
    bind_sparse_info.imageOpaqueBindCount = 0;
    bind_sparse_info.pImageOpaqueBinds = NULL;
    bind_sparse_info.imageBindCount = 0;
    bind_sparse_info.pImageBinds = NULL;

    first_packed_tile = dst_resource->sparse.tile_count;

    if (d3d12_resource_is_buffer(dst_resource))
    {
        if (!(memory_binds = vkd3d_malloc(count * sizeof(*memory_binds))))
        {
            ERR("Failed to allocate sparse memory bind info.\n");
            goto cleanup;
        }

        buffer_info.buffer = dst_resource->vk_buffer;
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

            opaque_info.image = dst_resource->vk_image;
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

            image_info.image = dst_resource->vk_image;
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
            vk_bind->subresource = tile->image.subresource;
            vk_bind->offset = tile->image.offset;
            vk_bind->extent = tile->image.extent;
            vk_bind->memory = vk_memory;
            vk_bind->memoryOffset = vk_offset;
            vk_bind->flags = 0;
        }
        else
        {
            VkSparseMemoryBind *vk_bind = &memory_binds[k++];
            vk_bind->resourceOffset = tile->buffer.offset;
            vk_bind->size = tile->buffer.length;
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
        queue_sparse = command_queue->device->queues[VKD3D_QUEUE_FAMILY_SPARSE_BINDING];
    else
        queue_sparse = queue;

    if (!(vk_queue = vkd3d_queue_acquire(queue)))
    {
        ERR("Failed to acquire queue %p.\n", queue);
        goto cleanup;
    }

    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.pNext = NULL;
    submit_info.signalSemaphoreCount = 1;
    submit_info.pSignalSemaphores = &queue->serializing_binary_semaphore;
    submit_info.commandBufferCount = 0;
    submit_info.pCommandBuffers = NULL;
    submit_info.waitSemaphoreCount = 0;
    submit_info.pWaitDstStageMask = NULL;
    submit_info.pWaitSemaphores = NULL;

    /* We need to serialize sparse bind operations.
     * Create a roundtrip with binary semaphores. */
    if ((vr = VK_CALL(vkQueueSubmit(vk_queue, 1, &submit_info, VK_NULL_HANDLE))) < 0)
        ERR("Failed to submit signal, vr %d.\n", vr);

    if (queue != queue_sparse)
    {
        if (!(vk_queue_sparse = vkd3d_queue_acquire(queue_sparse)))
        {
            ERR("Failed to acquire queue %p.\n", queue_sparse);
            vkd3d_queue_release(queue);
            goto cleanup;
        }
    }
    else
        vk_queue_sparse = vk_queue;

    bind_sparse_info.pWaitSemaphores = &queue->serializing_binary_semaphore;
    bind_sparse_info.pSignalSemaphores = &queue->serializing_binary_semaphore;
    bind_sparse_info.waitSemaphoreCount = 1;
    bind_sparse_info.signalSemaphoreCount = 1;

    if ((vr = VK_CALL(vkQueueBindSparse(vk_queue_sparse, 1, &bind_sparse_info, VK_NULL_HANDLE))) < 0)
        ERR("Failed to perform sparse binding, vr %d.\n", vr);

    if (queue != queue_sparse)
        vkd3d_queue_release(queue_sparse);

    submit_info.pWaitSemaphores = &queue->serializing_binary_semaphore;
    submit_info.waitSemaphoreCount = 1;
    submit_info.pWaitDstStageMask = &wait_stages;
    submit_info.pSignalSemaphores = NULL;
    submit_info.signalSemaphoreCount = 0;

    if ((vr = VK_CALL(vkQueueSubmit(vk_queue, 1, &submit_info, VK_NULL_HANDLE))) < 0)
        ERR("Failed to submit signal, vr %d.\n", vr);

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
    struct d3d12_command_queue_transition_pool pool;
    struct d3d12_command_queue *queue = userdata;
    uint64_t transition_timeline_value = 0;
    VkCommandBuffer transition_cmd;
    unsigned int i;
    HRESULT hr;

    VKD3D_REGION_DECL(queue_wait);
    VKD3D_REGION_DECL(queue_signal);
    VKD3D_REGION_DECL(queue_execute);

    vkd3d_set_thread_name("vkd3d_queue");

    if (FAILED(hr = d3d12_command_queue_transition_pool_init(&pool, queue)))
        ERR("Failed to initialize transition pool.\n");

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
            goto cleanup;

        case VKD3D_SUBMISSION_WAIT:
            VKD3D_REGION_BEGIN(queue_wait);
            d3d12_command_queue_wait(queue, submission.wait.fence, submission.wait.value);
            VKD3D_REGION_END(queue_wait);
            break;

        case VKD3D_SUBMISSION_SIGNAL:
            VKD3D_REGION_BEGIN(queue_signal);
            d3d12_command_queue_signal(queue, submission.signal.fence, submission.signal.value);
            VKD3D_REGION_END(queue_signal);
            break;

        case VKD3D_SUBMISSION_EXECUTE:
            VKD3D_REGION_BEGIN(queue_execute);
            d3d12_command_queue_transition_pool_build(&pool, queue->device,
                    submission.execute.transitions,
                    submission.execute.transition_count,
                    &transition_cmd, &transition_timeline_value);
            d3d12_command_queue_execute(queue, submission.execute.cmd,
                    submission.execute.cmd_count,
                    transition_cmd, pool.timeline, transition_timeline_value,
                    submission.execute.debug_capture);
            vkd3d_free(submission.execute.cmd);
            vkd3d_free(submission.execute.transitions);
            /* TODO: The correct place to do this would be in a fence handler, but this is good enough for now. */
            for (i = 0; i < submission.execute.outstanding_submissions_counter_count; i++)
                InterlockedDecrement(submission.execute.outstanding_submissions_counters[i]);
            vkd3d_free(submission.execute.outstanding_submissions_counters);
            VKD3D_REGION_END(queue_execute);
            break;

        case VKD3D_SUBMISSION_BIND_SPARSE:
            d3d12_command_queue_bind_sparse(queue, submission.bind_sparse.mode,
                    submission.bind_sparse.dst_resource, submission.bind_sparse.src_resource,
                    submission.bind_sparse.bind_count, submission.bind_sparse.bind_infos);
            vkd3d_free(submission.bind_sparse.bind_infos);
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

cleanup:
    d3d12_command_queue_transition_pool_deinit(&pool, queue->device);
    return NULL;
}

static HRESULT d3d12_command_queue_init(struct d3d12_command_queue *queue,
        struct d3d12_device *device, const D3D12_COMMAND_QUEUE_DESC *desc)
{
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

    if ((rc = pthread_mutex_init(&queue->queue_lock, NULL)) < 0)
    {
        hr = hresult_from_errno(rc);
        goto fail;
    }

    if ((rc = pthread_cond_init(&queue->queue_cond, NULL)) < 0)
    {
        hr = hresult_from_errno(rc);
        goto fail_pthread_cond;
    }

    if (desc->Priority == D3D12_COMMAND_QUEUE_PRIORITY_GLOBAL_REALTIME)
        FIXME("Global realtime priority is not implemented.\n");
    if (desc->Priority)
        FIXME("Ignoring priority %#x.\n", desc->Priority);
    if (desc->Flags)
        FIXME("Ignoring flags %#x.\n", desc->Flags);

    if (FAILED(hr = vkd3d_private_store_init(&queue->private_store)))
        goto fail_private_store;

#ifdef VKD3D_BUILD_STANDALONE_D3D12
    if (FAILED(hr = d3d12_swapchain_factory_init(queue, &queue->swapchain_factory)))
        goto fail_swapchain_factory;
#endif

    d3d12_device_add_ref(queue->device = device);

    if ((rc = pthread_create(&queue->submission_thread, NULL, d3d12_command_queue_submission_worker_main, queue)) < 0)
    {
        d3d12_device_release(queue->device);
        hr = hresult_from_errno(rc);
        goto fail_pthread_create;
    }

    return S_OK;

fail_pthread_create:;
#ifdef VKD3D_BUILD_STANDALONE_D3D12
fail_swapchain_factory:
    vkd3d_private_store_destroy(&queue->private_store);
#endif
fail_private_store:
    pthread_cond_destroy(&queue->queue_cond);
fail_pthread_cond:
    pthread_mutex_destroy(&queue->queue_lock);
fail:
    return hr;
}

HRESULT d3d12_command_queue_create(struct d3d12_device *device,
        const D3D12_COMMAND_QUEUE_DESC *desc, struct d3d12_command_queue **queue)
{
    struct d3d12_command_queue *object;
    HRESULT hr;

    if (!(object = vkd3d_calloc(1, sizeof(*object))))
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

VKD3D_EXPORT uint32_t vkd3d_get_vk_queue_family_index(ID3D12CommandQueue *queue)
{
    struct d3d12_command_queue *d3d12_queue = impl_from_ID3D12CommandQueue(queue);

    return d3d12_queue->vkd3d_queue->vk_family_index;
}

VKD3D_EXPORT VkQueue vkd3d_acquire_vk_queue(ID3D12CommandQueue *queue)
{
    struct d3d12_command_queue *d3d12_queue;
    VkQueue vk_queue;

    /* For external users of the Vulkan queue, we must ensure that the queue is drained so that submissions happen in
     * desired order. */
    VKD3D_REGION_DECL(acquire_vk_queue);
    VKD3D_REGION_BEGIN(acquire_vk_queue);
    d3d12_queue = impl_from_ID3D12CommandQueue(queue);
    d3d12_command_queue_acquire_serialized(d3d12_queue);
    vk_queue = vkd3d_queue_acquire(d3d12_queue->vkd3d_queue);
    VKD3D_REGION_END(acquire_vk_queue);

    return vk_queue;
}

VKD3D_EXPORT void vkd3d_release_vk_queue(ID3D12CommandQueue *queue)
{
    struct d3d12_command_queue *d3d12_queue = impl_from_ID3D12CommandQueue(queue);
    vkd3d_queue_release(d3d12_queue->vkd3d_queue);
    d3d12_command_queue_release_serialized(d3d12_queue);
}

VKD3D_EXPORT void vkd3d_enqueue_initial_transition(ID3D12CommandQueue *queue, ID3D12Resource *resource)
{
    struct d3d12_command_queue_submission sub;
    struct d3d12_command_queue *d3d12_queue = impl_from_ID3D12CommandQueue(queue);
    struct d3d12_resource *d3d12_resource = unsafe_impl_from_ID3D12Resource(resource);

    memset(&sub, 0, sizeof(sub));
    sub.type = VKD3D_SUBMISSION_EXECUTE;
    sub.execute.transition_count = 1;
    sub.execute.transitions = vkd3d_malloc(sizeof(*sub.execute.transitions));
    sub.execute.transitions[0].type = VKD3D_INITIAL_TRANSITION_TYPE_RESOURCE;
    sub.execute.transitions[0].resource.resource = d3d12_resource;
    sub.execute.transitions[0].resource.perform_initial_transition = true;
    d3d12_command_queue_add_submission(d3d12_queue, &sub);
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

static CONST_VTBL struct ID3D12CommandSignatureVtbl d3d12_command_signature_vtbl =
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
