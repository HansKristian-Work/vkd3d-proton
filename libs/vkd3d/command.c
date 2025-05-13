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
#include "vkd3d_descriptor_debug.h"
#ifdef VKD3D_ENABLE_RENDERDOC
#include "vkd3d_renderdoc.h"
#endif

static HRESULT d3d12_fence_signal(struct d3d12_fence *fence, struct vkd3d_fence_worker *worker, uint64_t value);
static void d3d12_command_queue_add_submission(struct d3d12_command_queue *queue,
        const struct d3d12_command_queue_submission *sub);
static void d3d12_fence_inc_ref(struct d3d12_fence *fence);
static void d3d12_fence_dec_ref(struct d3d12_fence *fence);
static void d3d12_shared_fence_inc_ref(struct d3d12_shared_fence *fence);
static void d3d12_shared_fence_dec_ref(struct d3d12_shared_fence *fence);
static void d3d12_fence_iface_inc_ref(d3d12_fence_iface *iface);
static void d3d12_fence_iface_dec_ref(d3d12_fence_iface *iface);
static ULONG d3d12_command_allocator_dec_ref(struct d3d12_command_allocator *allocator);
static HRESULT d3d12_fence_signal_cpu_timeline_semaphore(struct d3d12_fence *fence, uint64_t value);

/* This must be at least twice the number of texture region batches, since we must be able to resolve
 * a source + destination memory barrier per copy without incurring a barrier flush.
 * A barrier flush breaks our ability to eliminate redundant transitions for e.g. source copies. */
#define MAX_BATCHED_IMAGE_BARRIERS (VKD3D_COPY_TEXTURE_REGION_MAX_BATCH_SIZE * 2)
struct d3d12_command_list_barrier_batch
{
    VkImageMemoryBarrier2 vk_image_barriers[MAX_BATCHED_IMAGE_BARRIERS];
    VkMemoryBarrier2 vk_memory_barrier;
    uint32_t image_barrier_count;
};

static void d3d12_command_list_barrier_batch_init(struct d3d12_command_list_barrier_batch *batch);
static void d3d12_command_list_barrier_batch_end(struct d3d12_command_list *list,
        struct d3d12_command_list_barrier_batch *batch);
static void d3d12_command_list_barrier_batch_add_layout_transition(
        struct d3d12_command_list *list,
        struct d3d12_command_list_barrier_batch *batch,
        const VkImageMemoryBarrier2 *image_barrier);
static void d3d12_command_list_barrier_batch_add_global_transition(
        struct d3d12_command_list *list,
        struct d3d12_command_list_barrier_batch *batch,
        VkPipelineStageFlags2 srcStageMask, VkAccessFlags2 srcAccessMask,
        VkPipelineStageFlags2 dstStageMask, VkAccessFlags2 dstAccessMask);

static uint32_t d3d12_command_list_promote_dsv_resource(struct d3d12_command_list *list,
        struct d3d12_resource *resource, uint32_t plane_optimal_mask);
static uint32_t d3d12_command_list_notify_decay_dsv_resource(struct d3d12_command_list *list,
        struct d3d12_resource *resource);
static uint32_t d3d12_command_list_notify_dsv_writes(struct d3d12_command_list *list,
        struct d3d12_resource *resource, const struct vkd3d_view *view,
        uint32_t plane_write_mask);
static void d3d12_command_list_notify_dsv_discard(struct d3d12_command_list *list,
        struct d3d12_resource *resource,
        uint32_t first_subresource, uint32_t subresource_count,
        uint32_t resource_subresource_count);
static VkImageLayout d3d12_command_list_get_depth_stencil_resource_layout(const struct d3d12_command_list *list,
        const struct d3d12_resource *resource, uint32_t *plane_optimal_mask);
static void d3d12_command_list_decay_optimal_dsv_resource(struct d3d12_command_list *list,
        const struct d3d12_resource *resource, uint32_t plane_optimal_mask,
        struct d3d12_command_list_barrier_batch *batch);
static void d3d12_command_list_end_transfer_batch(struct d3d12_command_list *list);
static void d3d12_command_list_end_wbi_batch(struct d3d12_command_list *list);
static inline void d3d12_command_list_ensure_transfer_batch(struct d3d12_command_list *list, enum vkd3d_batch_type type);
static void d3d12_command_list_free_rtas_batch(struct d3d12_command_list *list);
static void d3d12_command_list_flush_rtas_batch(struct d3d12_command_list *list);
static void d3d12_command_list_clear_rtas_batch(struct d3d12_command_list *list);

static void d3d12_command_list_flush_query_resolves(struct d3d12_command_list *list);

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

static bool vkd3d_driver_implicitly_syncs_host_readback(VkDriverId driver_id)
{
    return driver_id == VK_DRIVER_ID_MESA_RADV ||
            driver_id == VK_DRIVER_ID_AMD_PROPRIETARY ||
            driver_id == VK_DRIVER_ID_AMD_OPEN_SOURCE ||
            driver_id == VK_DRIVER_ID_NVIDIA_PROPRIETARY;
}

HRESULT vkd3d_queue_create(struct d3d12_device *device, uint32_t family_index, uint32_t queue_index,
        const VkQueueFamilyProperties *properties, struct vkd3d_queue **queue)
{
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    VkCommandBufferAllocateInfo allocate_info;
    VkCommandPoolCreateInfo pool_create_info;
    VkCommandBufferBeginInfo begin_info;
    VkMemoryBarrier2 memory_barrier;
    struct vkd3d_queue *object;
    VkDependencyInfo dep_info;
    VkResult vr;
    HRESULT hr;
    int rc;

    if (!(object = vkd3d_malloc(sizeof(*object))))
        return E_OUTOFMEMORY;

    memset(object, 0, sizeof(*object));

    if (!(vkd3d_config_flags & VKD3D_CONFIG_FLAG_SKIP_DRIVER_WORKAROUNDS))
    {
        if (device->device_info.vulkan_1_2_properties.driverID == VK_DRIVER_ID_NVIDIA_PROPRIETARY)
        {
            /* There appears to be a race condition in the driver when submitting concurrently
             * to different VkQueues. Spec allows it, but there are likely dragons lurking ... */
            WARN("Enabling global submission mutex workaround.\n");
            object->global_mutex = &device->global_submission_mutex;
        }
    }

    if ((rc = pthread_mutex_init(&object->mutex, NULL)))
    {
        ERR("Failed to initialize mutex, error %d.\n", rc);
        vkd3d_free(object);
        return hresult_from_errno(rc);
    }

    object->vk_family_index = family_index;
    object->vk_queue_index = queue_index;
    object->vk_queue_flags = properties->queueFlags;
    object->timestamp_bits = properties->timestampValidBits;

    VK_CALL(vkGetDeviceQueue(device->vk_device, family_index, queue_index, &object->vk_queue));

    TRACE("Created queue %p for queue family index %u.\n", object, family_index);

    /* Having a simultaneous use command buffer seems to cause problems. */
    if (!vkd3d_driver_implicitly_syncs_host_readback(device->device_info.vulkan_1_2_properties.driverID))
    {
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
        if ((vr = VK_CALL(
                vkAllocateCommandBuffers(device->vk_device, &allocate_info, &object->barrier_command_buffer))))
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
        memset(&memory_barrier, 0, sizeof(memory_barrier));
        memory_barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
        memory_barrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        memory_barrier.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
        memory_barrier.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT | VK_PIPELINE_STAGE_2_HOST_BIT;
        memory_barrier.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_HOST_READ_BIT;

        memset(&dep_info, 0, sizeof(dep_info));
        dep_info.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        dep_info.memoryBarrierCount = 1;
        dep_info.pMemoryBarriers = &memory_barrier;

        VK_CALL(vkCmdPipelineBarrier2(object->barrier_command_buffer, &dep_info));
        VK_CALL(vkEndCommandBuffer(object->barrier_command_buffer));
    }

    if (FAILED(hr = vkd3d_create_timeline_semaphore(device, 0, false, &object->submission_timeline)))
        goto fail_free_command_pool;

    *queue = object;
    return hr;

fail_free_command_pool:
    VK_CALL(vkDestroyCommandPool(device->vk_device, object->barrier_pool, NULL));
fail_destroy_mutex:
    pthread_mutex_destroy(&object->mutex);
    return hr;
}

void vkd3d_set_queue_out_of_band(struct d3d12_device *device, struct vkd3d_queue *queue, VkOutOfBandQueueTypeNV type)
{
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    VkOutOfBandQueueTypeInfoNV queue_info;

    if (!device->vk_info.NV_low_latency2)
        return;

    memset(&queue_info, 0, sizeof(queue_info));
    queue_info.sType = VK_STRUCTURE_TYPE_OUT_OF_BAND_QUEUE_TYPE_INFO_NV;
    queue_info.pNext = NULL;
    queue_info.queueType = type;

    VK_CALL(vkQueueNotifyOutOfBandNV(queue->vk_queue, &queue_info));
}

void vkd3d_queue_drain(struct vkd3d_queue *queue, struct d3d12_device *device)
{
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    VkSemaphoreSubmitInfo signal_semaphore;
    VkSubmitInfo2 submit_desc;
    VkResult vr = VK_SUCCESS;
    VkQueue vk_queue;

    if (!(vk_queue = vkd3d_queue_acquire(queue)))
    {
        ERR("Failed to acquire queue %p.\n", queue);
        return;
    }

    if (queue->wait_count)
    {
        memset(&signal_semaphore, 0, sizeof(signal_semaphore));
        signal_semaphore.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
        signal_semaphore.semaphore = queue->submission_timeline;
        signal_semaphore.value = ++queue->submission_timeline_count;
        signal_semaphore.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;

        memset(&submit_desc, 0, sizeof(submit_desc));
        submit_desc.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
        submit_desc.waitSemaphoreInfoCount = queue->wait_count;
        submit_desc.pWaitSemaphoreInfos = queue->wait_semaphores;
        submit_desc.signalSemaphoreInfoCount = 1;
        submit_desc.pSignalSemaphoreInfos = &signal_semaphore;

        if ((vr = VK_CALL(vkQueueSubmit2(vk_queue, 1, &submit_desc, VK_NULL_HANDLE))) < 0)
            ERR("Failed to submit queue(s), vr %d.\n", vr);
    }

    if (vr == VK_SUCCESS)
    {
        if ((VK_CALL(vkQueueWaitIdle(vk_queue))))
            WARN("Failed to wait for queue, vr %d.\n", vr);
    }

    queue->wait_count = 0u;
    vkd3d_queue_release(queue);
}

void vkd3d_queue_destroy(struct vkd3d_queue *queue, struct d3d12_device *device)
{
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;

    VK_CALL(vkDestroyCommandPool(device->vk_device, queue->barrier_pool, NULL));
    VK_CALL(vkDestroySemaphore(device->vk_device, queue->submission_timeline, NULL));

    pthread_mutex_destroy(&queue->mutex);
    vkd3d_free(queue->command_queues);
    vkd3d_free(queue->wait_semaphores);
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

    if (queue->global_mutex && (rc = pthread_mutex_lock(queue->global_mutex)))
    {
        ERR("Failed to lock mutex, error %d.\n", rc);
        pthread_mutex_unlock(&queue->mutex);
        return VK_NULL_HANDLE;
    }

    assert(queue->vk_queue);
    return queue->vk_queue;
}

void vkd3d_queue_release(struct vkd3d_queue *queue)
{
    TRACE("queue %p.\n", queue);
    if (queue->global_mutex)
        pthread_mutex_unlock(queue->global_mutex);
    pthread_mutex_unlock(&queue->mutex);
}

static void vkd3d_queue_add_wait_locked(struct vkd3d_queue *queue, VkSemaphore semaphore, uint64_t value)
{
    VkSemaphoreSubmitInfo *wait_semaphore;
    uint32_t i;

    for (i = 0; i < queue->wait_count; i++)
    {
        if (queue->wait_semaphores[i].semaphore == semaphore)
        {
            if (queue->wait_semaphores[i].value < value)
                queue->wait_semaphores[i].value = value;
            return;
        }
    }

    if (!vkd3d_array_reserve((void**)&queue->wait_semaphores, &queue->wait_semaphores_size,
            queue->wait_count + 1, sizeof(*queue->wait_semaphores)))
    {
        ERR("Failed to add semaphore wait to queue.\n");
        return;
    }


    wait_semaphore = &queue->wait_semaphores[queue->wait_count];

    memset(wait_semaphore, 0, sizeof(*wait_semaphore));
    wait_semaphore->sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    wait_semaphore->semaphore = semaphore;
    wait_semaphore->value = value;
    wait_semaphore->stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;

    queue->wait_count += 1;
}

void vkd3d_queue_add_wait(struct vkd3d_queue *queue, VkSemaphore semaphore, uint64_t value)
{
    pthread_mutex_lock(&queue->mutex);
    vkd3d_queue_add_wait_locked(queue, semaphore, value);
    pthread_mutex_unlock(&queue->mutex);
}

void vkd3d_add_wait_to_all_queues(struct d3d12_device *device, VkSemaphore vk_semaphore, uint64_t value)
{
    struct vkd3d_queue_family_info *queue_family;
    uint32_t queue_mask, queue_index;
    unsigned int i;

    queue_mask = device->unique_queue_mask;

    while (queue_mask)
    {
        queue_index = vkd3d_bitmask_iter32(&queue_mask);
        queue_family = device->queue_families[queue_index];

        for (i = 0; i < queue_family->queue_count; i++)
            vkd3d_queue_add_wait(queue_family->queues[i], vk_semaphore, value);
    }
}

HRESULT vkd3d_create_timeline_semaphore(struct d3d12_device *device, uint64_t initial_value, bool shared, VkSemaphore *vk_semaphore)
{
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    VkPhysicalDeviceExternalSemaphoreInfo external_semaphore_info;
    VkExternalSemaphoreProperties external_semaphore_properties;
    VkExportSemaphoreCreateInfo export_info;
    VkSemaphoreTypeCreateInfoKHR type_info;
    VkSemaphoreCreateInfo info;
    VkResult vr;

    type_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO_KHR;
    type_info.pNext = NULL;
    type_info.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE_KHR;
    type_info.initialValue = initial_value;

    if (shared)
    {
        external_semaphore_info.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_SEMAPHORE_INFO;
        external_semaphore_info.pNext = &type_info;
        external_semaphore_info.handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_D3D12_FENCE_BIT;

        external_semaphore_properties.sType = VK_STRUCTURE_TYPE_EXTERNAL_SEMAPHORE_PROPERTIES;
        external_semaphore_properties.pNext = NULL;

        VK_CALL(vkGetPhysicalDeviceExternalSemaphoreProperties(device->vk_physical_device,
                &external_semaphore_info, &external_semaphore_properties));

        if (!(external_semaphore_properties.externalSemaphoreFeatures & VK_EXTERNAL_SEMAPHORE_FEATURE_EXPORTABLE_BIT) ||
            !(external_semaphore_properties.externalSemaphoreFeatures & VK_EXTERNAL_SEMAPHORE_FEATURE_IMPORTABLE_BIT) ||
            !(external_semaphore_properties.exportFromImportedHandleTypes & VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_D3D12_FENCE_BIT))
        {
            WARN("D3D12-Fence shared timeline semaphores not supported by host.\n");
            return E_NOTIMPL;
        }

        export_info.sType = VK_STRUCTURE_TYPE_EXPORT_SEMAPHORE_CREATE_INFO;
        export_info.pNext = NULL;
        export_info.handleTypes = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_D3D12_FENCE_BIT;

        type_info.pNext = &export_info;
    }

    info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    info.pNext = &type_info;
    info.flags = 0;

    if ((vr = VK_CALL(vkCreateSemaphore(device->vk_device, &info, NULL, vk_semaphore))) < 0)
        ERR("Failed to create timeline semaphore, vr %d.\n", vr);

    return hresult_from_vk_result(vr);
}

static void vkd3d_waiting_fence_release(struct vkd3d_fence_worker *worker,
        const struct vkd3d_fence_wait_info *fence_info, bool complete)
{
    if (fence_info->release_callback)
        fence_info->release_callback(worker, (void *)fence_info->userdata, complete);
}

static void *vkd3d_waiting_fence_set_callback(struct vkd3d_fence_wait_info *fence_info,
        vkd3d_waiting_fence_callback callback, size_t userdata_size)
{
    assert(userdata_size <= sizeof(fence_info->userdata));

    fence_info->release_callback = callback;
    return callback ? fence_info->userdata : NULL;
}

struct vkd3d_waiting_fence_submission_info
{
    struct d3d12_command_allocator **command_allocators;
    size_t num_command_allocators;
};

static void vkd3d_waiting_fence_release_submission(struct vkd3d_fence_worker *worker,
        void *userdata, bool complete)
{
    struct vkd3d_waiting_fence_submission_info *info = userdata;
    size_t i;

    for (i = 0; i < info->num_command_allocators; i++)
        d3d12_command_allocator_dec_ref(info->command_allocators[i]);

    vkd3d_free(info->command_allocators);
}

struct vkd3d_waiting_fence_sparse_bind_info
{
    struct d3d12_resource **resources;
    size_t num_resources;
};

static void vkd3d_waiting_fence_release_sparse_resources(struct vkd3d_fence_worker *worker,
        void *userdata, bool complete)
{
    struct vkd3d_waiting_fence_sparse_bind_info *info = userdata;
    size_t i;

    for (i = 0; i < info->num_resources; i++)
        d3d12_resource_decref(info->resources[i]);

    vkd3d_free(info->resources);
}

struct vkd3d_waiting_fence_signal_info
{
    struct d3d12_fence *fence;
    uint64_t virtual_value;
    uint64_t update_count;
};

static void vkd3d_waiting_fence_signal_fence(struct vkd3d_fence_worker *worker,
        void *userdata, bool complete)
{
    struct vkd3d_waiting_fence_signal_info *info = userdata;
    HRESULT hr;

    if (complete)
    {
        TRACE("Signaling fence %p to virtual value %"PRIu64".\n", info->fence, info->virtual_value);

        if (FAILED(hr = d3d12_fence_signal(info->fence, worker, info->update_count)))
            ERR("Failed to signal D3D12 fence, hr %#x.\n", hr);
    }

    d3d12_fence_dec_ref(info->fence);
}

struct vkd3d_waiting_fence_release_info
{
    d3d12_fence_iface *fence;
};

static void vkd3d_waiting_fence_release_fence(struct vkd3d_fence_worker *worker,
        void *userdata, bool complete)
{
    struct vkd3d_waiting_fence_release_info *info = userdata;

    d3d12_fence_iface_dec_ref(info->fence);
}

HRESULT vkd3d_enqueue_timeline_semaphore(struct vkd3d_fence_worker *worker,
        const struct vkd3d_fence_wait_info *fence_info,
        const struct vkd3d_queue_timeline_trace_cookie *timeline_cookie)
{
    struct vkd3d_waiting_fence *waiting_fence;
    int rc;

    TRACE("worker %p, vk_semaphore %p, vk_semaphore_value %#"PRIx64", callback %p.\n",
            worker, fence_info->vk_semaphore, fence_info->vk_semaphore_value, fence_info->release_callback);

    if ((rc = pthread_mutex_lock(&worker->mutex)))
    {
        ERR("Failed to lock mutex, error %d.\n", rc);
        vkd3d_waiting_fence_release(worker, fence_info, false);
        if (timeline_cookie)
        {
            vkd3d_queue_timeline_trace_complete_execute(&worker->device->queue_timeline_trace,
                    NULL, *timeline_cookie);
        }
        return hresult_from_errno(rc);
    }

    if (!vkd3d_array_reserve((void **)&worker->enqueued_fences, &worker->enqueued_fences_size,
                             worker->enqueued_fence_count + 1, sizeof(*worker->enqueued_fences)))
    {
        ERR("Failed to add GPU timeline semaphore.\n");
        pthread_mutex_unlock(&worker->mutex);
        vkd3d_waiting_fence_release(worker, fence_info, false);
        if (timeline_cookie)
        {
            vkd3d_queue_timeline_trace_complete_execute(&worker->device->queue_timeline_trace,
                    NULL, *timeline_cookie);
        }
        return E_OUTOFMEMORY;
    }

    waiting_fence = &worker->enqueued_fences[worker->enqueued_fence_count];
    memcpy(&waiting_fence->fence_info, fence_info, sizeof(*fence_info));
    if (timeline_cookie)
    {
        waiting_fence->timeline_cookie = *timeline_cookie;
        vkd3d_queue_timeline_trace_begin_execute(&worker->device->queue_timeline_trace, *timeline_cookie);
    }
    else
        memset(&waiting_fence->timeline_cookie, 0, sizeof(waiting_fence->timeline_cookie));
    ++worker->enqueued_fence_count;

    pthread_cond_signal(&worker->cond);
    pthread_mutex_unlock(&worker->mutex);
    return S_OK;
}

static void vkd3d_waiting_fence_complete_submissions(struct d3d12_device *device,
        struct vkd3d_fence_worker *worker, const struct vkd3d_waiting_fence *fence, bool complete)
{
    vkd3d_waiting_fence_release(worker, &fence->fence_info, complete);
    vkd3d_queue_timeline_trace_complete_execute(&device->queue_timeline_trace, worker, fence->timeline_cookie);
}

static void vkd3d_wait_for_gpu_timeline_semaphore(struct vkd3d_fence_worker *worker, const struct vkd3d_waiting_fence *fence)
{
    struct d3d12_device *device = worker->device;
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    VkSemaphoreWaitInfo wait_info;
    uint64_t timeout = UINT64_MAX;
    VkResult vr;

    TRACE("worker %p, vk_semaphore %p, vk_semaphore_value %#"PRIx64".\n", worker,
            fence->fence_info.vk_semaphore, fence->fence_info.vk_semaphore_value);

    memset(&wait_info, 0, sizeof(wait_info));
    wait_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
    wait_info.semaphoreCount = 1;
    wait_info.pSemaphores = &fence->fence_info.vk_semaphore;
    wait_info.pValues = &fence->fence_info.vk_semaphore_value;

    /* Some drivers hang indefinitely in face of device lost.
     * If a wait here takes more than 5 seconds, this is pretty much
     * a guaranteed timeout (TDR) scenario.
     * Usually, we'd observe DEVICE_LOST in subsequent submissions,
     * but if application submits something and expects to wait on that submission
     * immediately, this can happen. */
    if (vkd3d_config_flags & (VKD3D_CONFIG_FLAG_BREADCRUMBS | VKD3D_CONFIG_FLAG_FAULT))
        timeout = 5000000000ull;

    if ((vr = VK_CALL(vkWaitSemaphores(device->vk_device, &wait_info, timeout))))
    {
        ERR("Failed to wait for Vulkan timeline semaphore, vr %d.\n", vr);
        VKD3D_DEVICE_REPORT_FAULT_AND_BREADCRUMB_IF(device, vr == VK_ERROR_DEVICE_LOST || vr == VK_TIMEOUT);
        vkd3d_waiting_fence_complete_submissions(device, worker, fence, false);
        return;
    }

    /* This is a good time to kick the debug threads into action. */
    vkd3d_shader_debug_ring_kick(&device->debug_ring, device, false);
    vkd3d_descriptor_debug_kick_qa_check(device->descriptor_qa_global_info);

    vkd3d_waiting_fence_complete_submissions(device, worker, fence, true);
}

static void *vkd3d_fence_worker_main(void *arg)
{
    struct vkd3d_waiting_fence *cur_fences, *old_fences;
    struct vkd3d_fence_worker *worker = arg;
    size_t cur_fences_size, old_fences_size;
    uint32_t cur_fence_count;
    const char *type_str;
    bool do_exit;
    uint32_t i;
    int rc;

    static const char *type_to_str[] =
    {
        "DIRECT",
        "BUNDLE",
        "COMPUTE",
        "COPY",
        "VIDEO_DECODE",
        "VIDEO_PROCESS",
        "VIDEO_ENCODE",
    };

    vkd3d_set_thread_name("vkd3d_fence");

    type_str = worker->queue->desc.Type < ARRAY_SIZE(type_to_str) ? type_to_str[worker->queue->desc.Type] : "";

    snprintf(worker->timeline.tid, sizeof(worker->timeline.tid),
#ifdef _WIN32
            "family %u, %s, tid 0x%04x, prio %d, %p",
#else
            "family %u, %s, tid %u, prio %d, %p",
#endif
            worker->queue->vkd3d_queue->vk_family_index,
            type_str,
            vkd3d_get_current_thread_id(),
            worker->queue->desc.Priority,
            (void *)worker->queue->vkd3d_queue->vk_queue);

    cur_fence_count = 0;
    cur_fences_size = 0;
    cur_fences = NULL;

    for (;;)
    {
        if ((rc = pthread_mutex_lock(&worker->mutex)))
        {
            ERR("Failed to lock mutex, error %d.\n", rc);
            break;
        }

        if (!worker->enqueued_fence_count && !worker->should_exit)
        {
            if ((rc = pthread_cond_wait(&worker->cond, &worker->mutex)))
            {
                ERR("Failed to wait on condition variable, error %d.\n", rc);
                pthread_mutex_unlock(&worker->mutex);
                break;
            }
        }

        old_fences_size = cur_fences_size;
        old_fences = cur_fences;

        cur_fence_count = worker->enqueued_fence_count;
        cur_fences_size = worker->enqueued_fences_size;
        cur_fences = worker->enqueued_fences;
        do_exit = worker->should_exit;

        worker->enqueued_fence_count = 0;
        worker->enqueued_fences_size = old_fences_size;
        worker->enqueued_fences = old_fences;

        pthread_mutex_unlock(&worker->mutex);

        for (i = 0; i < cur_fence_count; i++)
            vkd3d_wait_for_gpu_timeline_semaphore(worker, &cur_fences[i]);

        if (do_exit)
            break;
    }

    vkd3d_free(cur_fences);
    return NULL;
}

static HRESULT vkd3d_fence_worker_start(struct vkd3d_fence_worker *worker,
        struct d3d12_command_queue *queue,
        struct d3d12_device *device)
{
    int rc;

    TRACE("worker %p.\n", worker);

    worker->should_exit = false;
    worker->device = device;
    worker->queue = queue;

    worker->enqueued_fence_count = 0;
    worker->enqueued_fences = NULL;
    worker->enqueued_fences_size = 0;

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

    if (pthread_create(&worker->thread, NULL, vkd3d_fence_worker_main, worker))
    {
        pthread_mutex_destroy(&worker->mutex);
        pthread_cond_destroy(&worker->cond);
        return E_OUTOFMEMORY;
    }

    return S_OK;
}

static HRESULT vkd3d_fence_worker_stop(struct vkd3d_fence_worker *worker,
        struct d3d12_device *device)
{
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
    pthread_join(worker->thread, NULL);

    pthread_mutex_destroy(&worker->mutex);
    pthread_cond_destroy(&worker->cond);

    vkd3d_free(worker->enqueued_fences);
    vkd3d_free(worker->timeline.list_buffer);
    return S_OK;
}

static const struct vkd3d_shader_root_parameter *root_signature_get_parameter(
        const struct d3d12_root_signature *root_signature, unsigned int index)
{
    assert(index < root_signature->parameter_count);
    return &root_signature->parameters[index];
}

static const struct vkd3d_shader_descriptor_table *root_signature_get_descriptor_table(
        const struct d3d12_root_signature *root_signature, unsigned int index)
{
    const struct vkd3d_shader_root_parameter *p = root_signature_get_parameter(root_signature, index);
    assert(p->parameter_type == D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE);
    return &p->descriptor_table;
}

static const struct vkd3d_shader_root_constant *root_signature_get_32bit_constants(
        const struct d3d12_root_signature *root_signature, unsigned int index)
{
    const struct vkd3d_shader_root_parameter *p = root_signature_get_parameter(root_signature, index);
    assert(p->parameter_type == D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS);
    return &p->constant;
}

static const struct vkd3d_shader_root_parameter *root_signature_get_root_descriptor(
        const struct d3d12_root_signature *root_signature, unsigned int index)
{
    const struct vkd3d_shader_root_parameter *p = root_signature_get_parameter(root_signature, index);
    assert(p->parameter_type == D3D12_ROOT_PARAMETER_TYPE_CBV
        || p->parameter_type == D3D12_ROOT_PARAMETER_TYPE_SRV
        || p->parameter_type == D3D12_ROOT_PARAMETER_TYPE_UAV);
    return p;
}

/* ID3D12Fence */
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

static void d3d12_fence_inc_ref(struct d3d12_fence *fence)
{
    InterlockedIncrement(&fence->refcount_internal);
}

static void d3d12_fence_dec_ref(struct d3d12_fence *fence)
{
    ULONG refcount_internal = InterlockedDecrement(&fence->refcount_internal);

    if (!refcount_internal)
    {
        d3d_destruction_notifier_free(&fence->destruction_notifier);
        vkd3d_private_store_destroy(&fence->private_store);
        d3d12_fence_destroy_vk_objects(fence);

        vkd3d_free(fence->events);
        vkd3d_free(fence->pending_updates);
        pthread_mutex_destroy(&fence->mutex);
        pthread_cond_destroy(&fence->cond);
        pthread_cond_destroy(&fence->null_event_cond);
        vkd3d_free(fence);
    }
}

static HRESULT vkd3d_waiting_event_signal(struct d3d12_device *device, struct vkd3d_fence_worker *worker,
        const struct vkd3d_waiting_event *event)
{
    bool do_signal = false;
    uint32_t payload;
    HRESULT hr;

    switch (event->wait_type)
    {
        case VKD3D_WAITING_EVENT_SINGLE:
            do_signal = true;
            break;

        case VKD3D_WAITING_EVENT_MULTI_ALL:
            /* Signal the event once the counter reaches 0. The signal bit may be
             * cleared if an error happens elsewhere, so check for that too. */
            payload = vkd3d_atomic_uint32_decrement(event->payload, vkd3d_memory_order_relaxed);

            if (!(payload & (VKD3D_WAITING_EVENT_SIGNAL_BIT - 1)))
            {
                do_signal = !!(payload & VKD3D_WAITING_EVENT_SIGNAL_BIT);
                vkd3d_free(event->payload);
            }
            break;

        case VKD3D_WAITING_EVENT_MULTI_ANY:
            /* In this mode, let the first thread that clears the signal bit signal
             * the event and treat the counter simply as a reference count */
            payload = vkd3d_atomic_uint32_load_explicit(event->payload, vkd3d_memory_order_relaxed);

            if (payload & VKD3D_WAITING_EVENT_SIGNAL_BIT)
            {
                payload = vkd3d_atomic_uint32_and(event->payload, ~VKD3D_WAITING_EVENT_SIGNAL_BIT, vkd3d_memory_order_relaxed);
                do_signal = !!(payload & VKD3D_WAITING_EVENT_SIGNAL_BIT);
            }

            if (!vkd3d_atomic_uint32_decrement(event->payload, vkd3d_memory_order_relaxed))
                vkd3d_free(event->payload);
            break;

        default:
            ERR("Unhandled wait type %u.\n", event->wait_type);
            return E_INVALIDARG;
    }

    if (!do_signal)
        return S_FALSE;

    vkd3d_queue_timeline_trace_complete_event_signal(&device->queue_timeline_trace, worker, event->timeline_cookie);

    if (!vkd3d_native_sync_handle_is_valid(event->handle))
    {
        *event->latch = true;
        return S_OK;
    }

    hr = vkd3d_native_sync_handle_signal(event->handle);

    if (FAILED(hr))
        ERR("Failed to signal event, hr #%x.\n", hr);

    return hr;
}

static void d3d12_fence_signal_external_events_locked(struct d3d12_fence *fence, struct vkd3d_fence_worker *worker)
{
    bool signal_null_event_cond = false;
    unsigned int i, j;

    for (i = 0, j = 0; i < fence->event_count; ++i)
    {
        struct vkd3d_waiting_event *current = &fence->events[i];

        if (current->value <= fence->virtual_value)
        {
            vkd3d_waiting_event_signal(fence->device, worker, current);

            if (!vkd3d_native_sync_handle_is_valid(current->handle))
                signal_null_event_cond = true;
        }
        else
        {
            if (i != j)
                fence->events[j] = *current;
            ++j;
        }
    }

    fence->event_count = j;

    if (signal_null_event_cond)
        pthread_cond_broadcast(&fence->null_event_cond);
}

static void d3d12_fence_block_until_pending_value_reaches_locked(struct d3d12_fence *fence, UINT64 pending_value)
{
    while (pending_value > fence->max_pending_virtual_timeline_value)
    {
        TRACE("Blocking wait on fence %p until it reaches 0x%"PRIx64".\n", fence, pending_value);
        pthread_cond_wait(&fence->cond, &fence->mutex);
    }
}

static void d3d12_fence_update_pending_value_locked(struct d3d12_fence *fence)
{
    uint64_t new_max_pending_virtual_timeline_value = 0;
    size_t i;

    for (i = 0; i < fence->pending_updates_count; i++)
        new_max_pending_virtual_timeline_value = max(fence->pending_updates[i].virtual_value, new_max_pending_virtual_timeline_value);
    new_max_pending_virtual_timeline_value = max(fence->virtual_value, new_max_pending_virtual_timeline_value);

    /* If we're signalling the fence, wake up any submission threads which can now safely kick work. */
    fence->max_pending_virtual_timeline_value = new_max_pending_virtual_timeline_value;
    pthread_cond_broadcast(&fence->cond);
}

static void d3d12_fence_lock(struct d3d12_fence *fence)
{
    pthread_mutex_lock(&fence->mutex);
}

static void d3d12_fence_unlock(struct d3d12_fence *fence)
{
    pthread_mutex_unlock(&fence->mutex);
}

static bool d3d12_fence_can_elide_wait_semaphore_locked(struct d3d12_fence *fence, uint64_t value,
        const struct vkd3d_queue *waiting_queue)
{
    unsigned int i;

    /* Relevant if the semaphore has been signalled already on host.
     * We should not wait on the timeline semaphore directly, we can simply submit in-place. */
    if (fence->virtual_value >= value)
        return true;

    /* We can elide a wait if we can use the submission order guarantee.
     * If there is a pending signal on this queue which will satisfy the wait,
     * submission barrier will implicitly complete the wait,
     * and we don't have to eat the overhead of submitting an extra wait on top.
     * This will essentially always trigger on single-queue.
     */
    for (i = 0; i < fence->pending_updates_count; i++)
    {
        if (fence->pending_updates[i].vk_semaphore == waiting_queue->submission_timeline &&
                fence->pending_updates[i].virtual_value >= value)
            return true;
    }

    return false;
}

static HRESULT d3d12_fence_signal_cpu_timeline_semaphore(struct d3d12_fence *fence, uint64_t value)
{
    int rc;

    if ((rc = pthread_mutex_lock(&fence->mutex)))
    {
        ERR("Failed to lock mutex, error %d.\n", rc);
        return hresult_from_errno(rc);
    }

    fence->virtual_value = value;
    d3d12_fence_signal_external_events_locked(fence, NULL);
    d3d12_fence_update_pending_value_locked(fence);
    pthread_mutex_unlock(&fence->mutex);
    return S_OK;
}

static uint64_t d3d12_fence_add_pending_signal_locked(struct d3d12_fence *fence, uint64_t virtual_value,
        const struct d3d12_command_queue *signalling_queue)
{
    struct d3d12_fence_value *update;
    vkd3d_array_reserve((void**)&fence->pending_updates, &fence->pending_updates_size,
                        fence->pending_updates_count + 1, sizeof(*fence->pending_updates));

    update = &fence->pending_updates[fence->pending_updates_count++];
    update->virtual_value = virtual_value;
    update->update_count = ++fence->update_count;
    update->vk_semaphore = signalling_queue->vkd3d_queue->submission_timeline;
    update->vk_semaphore_value = signalling_queue->last_submission_timeline_value;
    return fence->update_count;
}

static bool d3d12_fence_get_physical_wait_value_locked(struct d3d12_fence *fence, uint64_t virtual_value,
        struct d3d12_fence_value *fence_value)
{
    const struct d3d12_fence_value *selected = NULL;
    size_t i;

    /* This shouldn't happen, we will have elided the wait completely in can_elide_wait_semaphore_locked. */
    assert(virtual_value > fence->virtual_value);

    /* Find the first update which signals least the virtual value. */
    for (i = 0; i < fence->pending_updates_count; i++)
    {
        if (virtual_value <= fence->pending_updates[i].virtual_value)
        {
            if (!selected || fence->pending_updates[i].update_count < selected->update_count)
                selected = &fence->pending_updates[i];
        }
    }

    if (!selected)
    {
        FIXME("Cannot find a wait for fence %p, virtual value %"PRIu64". Ignoring wait.\n", fence, virtual_value);
        return false;
    }

    *fence_value = *selected;
    return true;
}

static HRESULT d3d12_fence_signal(struct d3d12_fence *fence, struct vkd3d_fence_worker *worker, uint64_t update_count)
{
    bool did_signal;
    size_t i;
    int rc;

    if ((rc = pthread_mutex_lock(&fence->mutex)))
    {
        ERR("Failed to lock mutex, error %d.\n", rc);
        return hresult_from_errno(rc);
    }

    /* With multiple fence workers, it is possible that signal calls are
     * out of order. The physical value itself is monotonic, but we need to
     * make sure that all signals happen in correct order if there are fence rewinds.
     * We don't expect the loop to run more than once,
     * but there might be extreme edge cases where we signal 2 or more. */
    while (fence->signal_count < update_count)
    {
        fence->signal_count++;
        did_signal = false;

        for (i = 0; i < fence->pending_updates_count; i++)
        {
            if (fence->signal_count == fence->pending_updates[i].update_count)
            {
                fence->virtual_value = fence->pending_updates[i].virtual_value;
                d3d12_fence_signal_external_events_locked(fence, worker);
                fence->pending_updates[i] = fence->pending_updates[--fence->pending_updates_count];
                did_signal = true;
                break;
            }
        }

        if (!did_signal)
            FIXME("Did not signal a virtual value?\n");
    }

    /* In case we have a rewind signalled from GPU, we need to recompute the max pending timeline value. */
    d3d12_fence_update_pending_value_locked(fence);

    pthread_mutex_unlock(&fence->mutex);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d3d12_fence_QueryInterface(d3d12_fence_iface *iface,
        REFIID riid, void **object)
{
    struct d3d12_fence *fence = impl_from_ID3D12Fence1(iface);

    TRACE("iface %p, riid %s, object %p.\n", iface, debugstr_guid(riid), object);

    if (!object)
        return E_POINTER;

    if (IsEqualGUID(riid, &IID_ID3D12Fence)
            || IsEqualGUID(riid, &IID_ID3D12Fence1)
            || IsEqualGUID(riid, &IID_ID3D12Pageable)
            || IsEqualGUID(riid, &IID_ID3D12DeviceChild)
            || IsEqualGUID(riid, &IID_ID3D12Object)
            || IsEqualGUID(riid, &IID_IUnknown))
    {
        ID3D12Fence1_AddRef(iface);
        *object = iface;
        return S_OK;
    }

    if (IsEqualGUID(riid, &IID_ID3DDestructionNotifier))
    {
        ID3DDestructionNotifier_AddRef(&fence->destruction_notifier.ID3DDestructionNotifier_iface);
        *object = &fence->destruction_notifier.ID3DDestructionNotifier_iface;
        return S_OK;
    }

    WARN("%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid(riid));

    *object = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE d3d12_fence_AddRef(d3d12_fence_iface *iface)
{
    struct d3d12_fence *fence = impl_from_ID3D12Fence1(iface);
    ULONG refcount = InterlockedIncrement(&fence->refcount);

    TRACE("%p increasing refcount to %u.\n", fence, refcount);

    return refcount;
}

static ULONG STDMETHODCALLTYPE d3d12_fence_Release(d3d12_fence_iface *iface)
{
    struct d3d12_fence *fence = impl_from_ID3D12Fence1(iface);
    ULONG refcount = InterlockedDecrement(&fence->refcount);

    TRACE("%p decreasing refcount to %u.\n", fence, refcount);

    if (!refcount)
    {
        struct d3d12_device *device = fence->device;

        /* When a fence's public ref-count hits zero, all waiters must be released.
         * NOTE: For shared fences later,
         * we cannot signal here since we cannot know if there are other fences.
         * According to our tests, the fence unblocks all waiters when the last reference
         * to the shared HANDLE is released. This is completely outside the scope of what we can
         * reasonably implement ourselves. For now, the plan is to wait with timeout
         * and mark "TDR" if that ever happens in real world usage. */
        if (!(fence->d3d12_flags & D3D12_FENCE_FLAG_SHARED))
            d3d12_fence_signal_cpu_timeline_semaphore(fence, UINT64_MAX);

        d3d_destruction_notifier_notify(&fence->destruction_notifier);

        d3d12_fence_dec_ref(fence);
        d3d12_device_release(device);
    }

    return refcount;
}

static HRESULT STDMETHODCALLTYPE d3d12_fence_GetPrivateData(d3d12_fence_iface *iface,
        REFGUID guid, UINT *data_size, void *data)
{
    struct d3d12_fence *fence = impl_from_ID3D12Fence1(iface);

    TRACE("iface %p, guid %s, data_size %p, data %p.\n",
            iface, debugstr_guid(guid), data_size, data);

    return vkd3d_get_private_data(&fence->private_store, guid, data_size, data);
}

static HRESULT STDMETHODCALLTYPE d3d12_fence_SetPrivateData(d3d12_fence_iface *iface,
        REFGUID guid, UINT data_size, const void *data)
{
    struct d3d12_fence *fence = impl_from_ID3D12Fence1(iface);

    TRACE("iface %p, guid %s, data_size %u, data %p.\n",
            iface, debugstr_guid(guid), data_size, data);

    return vkd3d_set_private_data(&fence->private_store, guid, data_size, data,
            NULL, NULL);
}

static HRESULT STDMETHODCALLTYPE d3d12_fence_SetPrivateDataInterface(d3d12_fence_iface *iface,
        REFGUID guid, const IUnknown *data)
{
    struct d3d12_fence *fence = impl_from_ID3D12Fence1(iface);

    TRACE("iface %p, guid %s, data %p.\n", iface, debugstr_guid(guid), data);

    return vkd3d_set_private_data_interface(&fence->private_store, guid, data,
            NULL, NULL);
}

static HRESULT STDMETHODCALLTYPE d3d12_fence_GetDevice(d3d12_fence_iface *iface, REFIID iid, void **device)
{
    struct d3d12_fence *fence = impl_from_ID3D12Fence1(iface);

    TRACE("iface %p, iid %s, device %p.\n", iface, debugstr_guid(iid), device);

    return d3d12_device_query_interface(fence->device, iid, device);
}

static UINT64 STDMETHODCALLTYPE d3d12_fence_GetCompletedValue(d3d12_fence_iface *iface)
{
    struct d3d12_fence *fence = impl_from_ID3D12Fence1(iface);
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

static HRESULT d3d12_fence_set_native_sync_handle_on_completion_explicit(struct d3d12_fence *fence,
        enum vkd3d_waiting_event_type wait_type, UINT64 value, vkd3d_native_sync_handle handle, uint32_t *payload)
{
    struct vkd3d_waiting_event event;
    unsigned int i;
    HRESULT hr;
    bool latch;
    int rc;

    if ((rc = pthread_mutex_lock(&fence->mutex)))
    {
        ERR("Failed to lock mutex, error %d.\n", rc);
        return hresult_from_errno(rc);
    }

    memset(&event, 0, sizeof(event));
    event.wait_type = wait_type;
    event.value = value;
    event.handle = handle;
    event.latch = &latch;
    event.payload = payload;

    if (value <= fence->virtual_value)
    {
        hr = vkd3d_waiting_event_signal(fence->device, NULL, &event);
        pthread_mutex_unlock(&fence->mutex);
        return hr;
    }

    event.timeline_cookie = vkd3d_queue_timeline_trace_register_event_signal(&fence->device->queue_timeline_trace,
            handle, &fence->ID3D12Fence_iface, value);

    if (wait_type == VKD3D_WAITING_EVENT_SINGLE && vkd3d_native_sync_handle_is_valid(handle))
    {
        for (i = 0; i < fence->event_count; ++i)
        {
            struct vkd3d_waiting_event *current = &fence->events[i];
            if (current->wait_type == VKD3D_WAITING_EVENT_SINGLE && current->value == value &&
                    vkd3d_native_sync_handle_eq(current->handle, handle))
            {
                WARN("Event completion for native sync handle is already in the list.\n");
                pthread_mutex_unlock(&fence->mutex);
                return S_OK;
            }
        }
    }

    if (!vkd3d_array_reserve((void **)&fence->events, &fence->events_size,
            fence->event_count + 1, sizeof(*fence->events)))
    {
        WARN("Failed to add event.\n");
        pthread_mutex_unlock(&fence->mutex);
        return E_OUTOFMEMORY;
    }

    fence->events[fence->event_count++] = event;

    /* If event is NULL, we need to block until the fence value completes.
     * Implement this in a uniform way where we pretend we have a dummy event.
     * A NULL fence->events[].event means that we should set latch to true
     * and signal a condition variable instead of calling external signal_event callback. */
    if (!vkd3d_native_sync_handle_is_valid(handle))
    {
        latch = false;
        while (!latch)
            pthread_cond_wait(&fence->null_event_cond, &fence->mutex);
    }

    pthread_mutex_unlock(&fence->mutex);
    return S_OK;
}

HRESULT d3d12_fence_set_native_sync_handle_on_completion(struct d3d12_fence *fence,
        UINT64 value, vkd3d_native_sync_handle handle)
{
    return d3d12_fence_set_native_sync_handle_on_completion_explicit(fence,
            VKD3D_WAITING_EVENT_SINGLE, value, handle, NULL);
}

HRESULT d3d12_fence_set_event_on_completion(struct d3d12_fence *fence,
        UINT64 value, HANDLE os_event)
{
    vkd3d_native_sync_handle handle = vkd3d_native_sync_handle_wrap(os_event,
            VKD3D_NATIVE_SYNC_HANDLE_TYPE_EVENT);
    return d3d12_fence_set_native_sync_handle_on_completion(fence, value, handle);
}

static HRESULT STDMETHODCALLTYPE d3d12_fence_SetEventOnCompletion(d3d12_fence_iface *iface,
        UINT64 value, HANDLE event)
{
    struct d3d12_fence *fence = impl_from_ID3D12Fence1(iface);

    TRACE("iface %p, value %#"PRIx64", event %p.\n", iface, value, event);

    return d3d12_fence_set_event_on_completion(fence, value, event);
}

static HRESULT STDMETHODCALLTYPE d3d12_fence_Signal(d3d12_fence_iface *iface, UINT64 value)
{
    struct d3d12_fence *fence = impl_from_ID3D12Fence1(iface);

    TRACE("iface %p, value %#"PRIx64".\n", iface, value);

    return d3d12_fence_signal_cpu_timeline_semaphore(fence, value);
}

static D3D12_FENCE_FLAGS STDMETHODCALLTYPE d3d12_fence_GetCreationFlags(d3d12_fence_iface *iface)
{
    struct d3d12_fence *fence = impl_from_ID3D12Fence1(iface);

    TRACE("iface %p.\n", iface);

    return fence->d3d12_flags;
}

CONST_VTBL struct ID3D12Fence1Vtbl d3d12_fence_vtbl =
{
    /* IUnknown methods */
    d3d12_fence_QueryInterface,
    d3d12_fence_AddRef,
    d3d12_fence_Release,
    /* ID3D12Object methods */
    d3d12_fence_GetPrivateData,
    d3d12_fence_SetPrivateData,
    d3d12_fence_SetPrivateDataInterface,
    (void *)d3d12_object_SetName,
    /* ID3D12DeviceChild methods */
    d3d12_fence_GetDevice,
    /* ID3D12Fence methods */
    d3d12_fence_GetCompletedValue,
    d3d12_fence_SetEventOnCompletion,
    d3d12_fence_Signal,
    /* ID3D12Fence1 methods */
    d3d12_fence_GetCreationFlags,
};

static HRESULT d3d12_fence_init(struct d3d12_fence *fence, struct d3d12_device *device,
        UINT64 initial_value, D3D12_FENCE_FLAGS flags)
{
    HRESULT hr;
    int rc;

    memset(fence, 0, sizeof(*fence));
    fence->ID3D12Fence_iface.lpVtbl = &d3d12_fence_vtbl;
    fence->refcount_internal = 1;
    fence->refcount = 1;
    fence->d3d12_flags = flags;
    fence->max_pending_virtual_timeline_value = initial_value;
    fence->virtual_value = initial_value;

    if ((rc = pthread_mutex_init(&fence->mutex, NULL)))
    {
        ERR("Failed to initialize mutex, error %d.\n", rc);
        return hresult_from_errno(rc);
    }

    if ((rc = pthread_cond_init(&fence->cond, NULL)))
    {
        ERR("Failed to initialize cond variable, error %d.\n", rc);
        pthread_mutex_destroy(&fence->mutex);
        return hresult_from_errno(rc);
    }

    if ((rc = pthread_cond_init(&fence->null_event_cond, NULL)))
    {
        ERR("Failed to initialize cond variable, error %d.\n", rc);
        pthread_mutex_destroy(&fence->mutex);
        pthread_cond_destroy(&fence->cond);
        return hresult_from_errno(rc);
    }

    if (flags)
        FIXME("Ignoring flags %#x.\n", flags);

    fence->events = NULL;
    fence->events_size = 0;
    fence->event_count = 0;

    fence->pending_updates = NULL;
    fence->pending_updates_count = 0;
    fence->pending_updates_size = 0;

    if (FAILED(hr = vkd3d_private_store_init(&fence->private_store)))
    {
        pthread_mutex_destroy(&fence->mutex);
        pthread_cond_destroy(&fence->cond);
        pthread_cond_destroy(&fence->null_event_cond);
        return hr;
    }

    d3d_destruction_notifier_init(&fence->destruction_notifier, (IUnknown*)&fence->ID3D12Fence_iface);
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

static HRESULT STDMETHODCALLTYPE d3d12_shared_fence_QueryInterface(d3d12_fence_iface *iface,
        REFIID riid, void **object)
{
    struct d3d12_shared_fence *fence = shared_impl_from_ID3D12Fence1(iface);

    TRACE("iface %p, riid %s, object %p.\n", iface, debugstr_guid(riid), object);

    if (!object)
        return E_POINTER;

    if (IsEqualGUID(riid, &IID_ID3D12Fence)
            || IsEqualGUID(riid, &IID_ID3D12Fence1)
            || IsEqualGUID(riid, &IID_ID3D12Pageable)
            || IsEqualGUID(riid, &IID_ID3D12DeviceChild)
            || IsEqualGUID(riid, &IID_ID3D12Object)
            || IsEqualGUID(riid, &IID_IUnknown))
    {
        ID3D12Fence1_AddRef(iface);
        *object = iface;
        return S_OK;
    }

    if (IsEqualGUID(riid, &IID_ID3DDestructionNotifier))
    {
        ID3DDestructionNotifier_AddRef(&fence->destruction_notifier.ID3DDestructionNotifier_iface);
        *object = &fence->destruction_notifier.ID3DDestructionNotifier_iface;
        return S_OK;
    }

    WARN("%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid(riid));

    *object = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE d3d12_shared_fence_AddRef(d3d12_fence_iface *iface)
{
    struct d3d12_shared_fence *fence = shared_impl_from_ID3D12Fence1(iface);
    ULONG refcount = InterlockedIncrement(&fence->refcount);

    TRACE("%p increasing refcount to %u.\n", fence, refcount);

    return refcount;
}

static void d3d12_shared_fence_inc_ref(struct d3d12_shared_fence *fence)
{
    InterlockedIncrement(&fence->refcount_internal);
}

static void d3d12_shared_fence_dec_ref(struct d3d12_shared_fence *fence)
{
    ULONG refcount_internal = InterlockedDecrement(&fence->refcount_internal);
    struct vkd3d_shared_fence_waiting_event *current, *e;
    const struct vkd3d_vk_device_procs *vk_procs;
    bool is_running;

    if (!refcount_internal)
    {
        pthread_mutex_lock(&fence->mutex);
        is_running = fence->is_running;
        if (is_running)
        {
            fence->is_running = false;
            pthread_cond_signal(&fence->cond_var);
        }
        pthread_mutex_unlock(&fence->mutex);

        if (is_running)
        {
            pthread_join(fence->thread, NULL);
        }

        LIST_FOR_EACH_ENTRY_SAFE(current, e, &fence->events, struct vkd3d_shared_fence_waiting_event, entry)
        {
            vkd3d_free(current);
        }

        pthread_mutex_destroy(&fence->mutex);
        pthread_cond_destroy(&fence->cond_var);

        vk_procs = &fence->device->vk_procs;
        VK_CALL(vkDestroySemaphore(fence->device->vk_device, fence->timeline_semaphore, NULL));

        d3d_destruction_notifier_free(&fence->destruction_notifier);
        vkd3d_private_store_destroy(&fence->private_store);

        vkd3d_free(fence);
    }
}

static ULONG STDMETHODCALLTYPE d3d12_shared_fence_Release(d3d12_fence_iface *iface)
{
    struct d3d12_shared_fence *fence = shared_impl_from_ID3D12Fence1(iface);
    ULONG refcount = InterlockedDecrement(&fence->refcount);

    TRACE("%p decreasing refcount to %u.\n", fence, refcount);

    if (!refcount)
    {
        struct d3d12_device *device = fence->device;

        d3d_destruction_notifier_notify(&fence->destruction_notifier);

        d3d12_shared_fence_dec_ref(fence);
        d3d12_device_release(device);
    }

    return refcount;
}

static HRESULT STDMETHODCALLTYPE d3d12_shared_fence_GetPrivateData(d3d12_fence_iface *iface,
        REFGUID guid, UINT *data_size, void *data)
{
    struct d3d12_shared_fence *fence = shared_impl_from_ID3D12Fence1(iface);

    TRACE("iface %p, guid %s, data_size %p, data %p.\n",
            iface, debugstr_guid(guid), data_size, data);

    return vkd3d_get_private_data(&fence->private_store, guid, data_size, data);
}

static HRESULT STDMETHODCALLTYPE d3d12_shared_fence_SetPrivateData(d3d12_fence_iface *iface,
        REFGUID guid, UINT data_size, const void *data)
{
    struct d3d12_shared_fence *fence = shared_impl_from_ID3D12Fence1(iface);

    TRACE("iface %p, guid %s, data_size %u, data %p.\n",
            iface, debugstr_guid(guid), data_size, data);

    return vkd3d_set_private_data(&fence->private_store, guid, data_size, data,
            NULL, NULL);
}

static HRESULT STDMETHODCALLTYPE d3d12_shared_fence_SetPrivateDataInterface(d3d12_fence_iface *iface,
        REFGUID guid, const IUnknown *data)
{
    struct d3d12_shared_fence *fence = shared_impl_from_ID3D12Fence1(iface);

    TRACE("iface %p, guid %s, data %p.\n", iface, debugstr_guid(guid), data);

    return vkd3d_set_private_data_interface(&fence->private_store, guid, data,
            NULL, NULL);
}

static HRESULT STDMETHODCALLTYPE d3d12_shared_fence_GetDevice(d3d12_fence_iface *iface, REFIID iid, void **device)
{
    struct d3d12_shared_fence *fence = shared_impl_from_ID3D12Fence1(iface);

    TRACE("iface %p, iid %s, device %p.\n", iface, debugstr_guid(iid), device);

    return d3d12_device_query_interface(fence->device, iid, device);
}

static UINT64 STDMETHODCALLTYPE d3d12_shared_fence_GetCompletedValue(d3d12_fence_iface *iface)
{
    struct d3d12_shared_fence *fence = shared_impl_from_ID3D12Fence1(iface);
    const struct vkd3d_vk_device_procs *vk_procs = &fence->device->vk_procs;
    uint64_t completed_value;
    VkResult vr;

    TRACE("iface %p\n", iface);

    vr = VK_CALL(vkGetSemaphoreCounterValue(fence->device->vk_device, fence->timeline_semaphore, &completed_value));
    if (vr != VK_SUCCESS)
    {
        ERR("Failed to get shared fence counter value, error %d.\n", vr);
        return 0;
    }
    return completed_value;
}

static void *vkd3d_shared_fence_worker_main(void *userdata)
{
    struct vkd3d_shared_fence_waiting_event *current, *e;
    const struct vkd3d_vk_device_procs *vk_procs;
    struct d3d12_shared_fence *fence;
    VkSemaphoreWaitInfo wait_info;
    uint64_t completed_value;
    VkResult vr;

    fence = userdata;
    vk_procs = &fence->device->vk_procs;

    wait_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
    wait_info.pNext = NULL;
    wait_info.flags = 0;
    wait_info.semaphoreCount = 1;
    wait_info.pSemaphores = &fence->timeline_semaphore;
    wait_info.pValues = &completed_value;

    while (true)
    {
        pthread_mutex_lock(&fence->mutex);
        while (fence->is_running && list_empty(&fence->events))
        {
            pthread_cond_wait(&fence->cond_var, &fence->mutex);
        }

        vr = VK_CALL(vkGetSemaphoreCounterValue(fence->device->vk_device, fence->timeline_semaphore, &completed_value));
        if (vr != VK_SUCCESS)
        {
            ERR("Failed to get shared fence counter value, error %d.\n", vr);
            pthread_mutex_unlock(&fence->mutex);
            return NULL;
        }

        LIST_FOR_EACH_ENTRY_SAFE(current, e, &fence->events, struct vkd3d_shared_fence_waiting_event, entry)
        {
            if (current->wait.value <= completed_value)
            {
                vkd3d_waiting_event_signal(fence->device, NULL, &current->wait);
                list_remove(&current->entry);
                vkd3d_free(current);
            }
        }

        if (!fence->is_running)
        {
            pthread_mutex_unlock(&fence->mutex);
            return NULL;
        }

        if (list_empty(&fence->events))
        {
            pthread_mutex_unlock(&fence->mutex);
            continue;
        }

        pthread_mutex_unlock(&fence->mutex);

        completed_value++;
        vr = VK_CALL(vkWaitSemaphores(fence->device->vk_device, &wait_info, 10000000ull));
        if (vr != VK_SUCCESS && vr != VK_TIMEOUT)
        {
            ERR("Failed to wait for semaphore, error %d.\n", vr);
            return NULL;
        }
    }


    return NULL;
}

static HRESULT d3d12_shared_fence_set_native_sync_handle_on_completion_explicit(struct d3d12_shared_fence *fence,
        enum vkd3d_waiting_event_type wait_type, UINT64 value, vkd3d_native_sync_handle handle, uint32_t *payload)
{
    const struct vkd3d_vk_device_procs *vk_procs = &fence->device->vk_procs;
    struct vkd3d_shared_fence_waiting_event *waiting_event;
    struct vkd3d_waiting_event event;
    VkSemaphoreWaitInfo wait_info;
    uint64_t completed_value;
    VkResult vr;

    TRACE("fence %p, value %#"PRIx64".\n", fence, value);

    if (vkd3d_native_sync_handle_is_valid(handle))
    {
        memset(&event, 0, sizeof(event));
        event.wait_type = wait_type;
        event.value = value;
        event.handle = handle;
        event.payload = payload;

        if ((vr = VK_CALL(vkGetSemaphoreCounterValue(fence->device->vk_device, fence->timeline_semaphore, &completed_value))))
        {
            ERR("Failed to get current semaphore value, vr %d.\n", vr);
            return hresult_from_vk_result(vr);
        }

        if (completed_value >= value)
        {
            vkd3d_waiting_event_signal(fence->device, NULL, &event);
            return S_OK;
        }
        else
        {
            event.timeline_cookie = vkd3d_queue_timeline_trace_register_event_signal(&fence->device->queue_timeline_trace,
                    handle, &fence->ID3D12Fence_iface, value);

            if (!(waiting_event = vkd3d_malloc(sizeof(*waiting_event))))
            {
                ERR("Failed to register device singleton for adapter.\n");
                return E_OUTOFMEMORY;
            }

            waiting_event->wait = event;

            pthread_mutex_lock(&fence->mutex);
            list_add_head(&fence->events, &waiting_event->entry);
            if (!fence->is_running)
            {
                fence->is_running = true;
                pthread_create(&fence->thread, NULL, vkd3d_shared_fence_worker_main, fence);
            }
            else
            {
                pthread_cond_signal(&fence->cond_var);
            }
            pthread_mutex_unlock(&fence->mutex);

            return S_OK;
        }
    }
    else
    {
        event.timeline_cookie = vkd3d_queue_timeline_trace_register_event_signal(&fence->device->queue_timeline_trace,
                handle, &fence->ID3D12Fence_iface, value);

        wait_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
        wait_info.pNext = NULL;
        wait_info.flags = 0;
        wait_info.semaphoreCount = 1;
        wait_info.pSemaphores = &fence->timeline_semaphore;
        wait_info.pValues = &value;

        if ((vr = VK_CALL(vkWaitSemaphores(fence->device->vk_device, &wait_info, UINT64_MAX))))
        {
            ERR("Failed to wait on shared fence, vr %d.\n", vr);
            return E_FAIL;
        }

        vkd3d_queue_timeline_trace_complete_event_signal(&fence->device->queue_timeline_trace,
                NULL, event.timeline_cookie);

        return S_OK;
    }
}

static HRESULT STDMETHODCALLTYPE d3d12_shared_fence_SetEventOnCompletion(d3d12_fence_iface *iface,
        UINT64 value, HANDLE os_event)
{
    struct d3d12_shared_fence *shared_fence = shared_impl_from_ID3D12Fence1(iface);

    vkd3d_native_sync_handle handle = vkd3d_native_sync_handle_wrap(os_event,
            VKD3D_NATIVE_SYNC_HANDLE_TYPE_EVENT);
    return d3d12_shared_fence_set_native_sync_handle_on_completion_explicit(
            shared_fence, VKD3D_WAITING_EVENT_SINGLE, value, handle, NULL);
}

static HRESULT STDMETHODCALLTYPE d3d12_shared_fence_Signal(d3d12_fence_iface *iface, UINT64 value)
{
    struct d3d12_shared_fence *fence = shared_impl_from_ID3D12Fence1(iface);
    const struct vkd3d_vk_device_procs *vk_procs = &fence->device->vk_procs;
    VkSemaphoreSignalInfo signal_info;
    VkResult vr;

    TRACE("iface %p, value %#"PRIx64".\n", iface, value);

    signal_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO;
    signal_info.pNext = NULL;
    signal_info.semaphore = fence->timeline_semaphore;
    signal_info.value = value;

    if ((vr = VK_CALL(vkSignalSemaphore(fence->device->vk_device, &signal_info))))
    {
        ERR("Failed to signal shared fence, vr %d.\n", vr);
        return E_FAIL;
    }

    return S_OK;
}

static D3D12_FENCE_FLAGS STDMETHODCALLTYPE d3d12_shared_fence_GetCreationFlags(d3d12_fence_iface *iface)
{
    struct d3d12_shared_fence *fence = shared_impl_from_ID3D12Fence1(iface);

    TRACE("iface %p.\n", iface);

    return fence->d3d12_flags;
}

CONST_VTBL struct ID3D12Fence1Vtbl d3d12_shared_fence_vtbl =
{
    /* IUnknown methods */
    d3d12_shared_fence_QueryInterface,
    d3d12_shared_fence_AddRef,
    d3d12_shared_fence_Release,
    /* ID3D12Object methods */
    d3d12_shared_fence_GetPrivateData,
    d3d12_shared_fence_SetPrivateData,
    d3d12_shared_fence_SetPrivateDataInterface,
    (void *)d3d12_object_SetName,
    /* ID3D12DeviceChild methods */
    d3d12_shared_fence_GetDevice,
    /* ID3D12Fence methods */
    d3d12_shared_fence_GetCompletedValue,
    d3d12_shared_fence_SetEventOnCompletion,
    d3d12_shared_fence_Signal,
    /* ID3D12Fence1 methods */
    d3d12_shared_fence_GetCreationFlags,
};

HRESULT d3d12_shared_fence_create(struct d3d12_device *device,
        uint64_t initial_value, D3D12_FENCE_FLAGS flags, struct d3d12_shared_fence **fence)
{
    struct d3d12_shared_fence *object;
    HRESULT hr;

    if (!(object = vkd3d_malloc(sizeof(*object))))
        return E_OUTOFMEMORY;

    object->ID3D12Fence_iface.lpVtbl = &d3d12_shared_fence_vtbl;
    object->refcount_internal = 1;
    object->refcount = 1;
    object->d3d12_flags = flags;

    if (FAILED(hr = vkd3d_private_store_init(&object->private_store)))
    {
        vkd3d_free(object);
        return hr;
    }

    if (FAILED(hr = vkd3d_create_timeline_semaphore(device, 0, true, &object->timeline_semaphore)))
    {
        vkd3d_private_store_destroy(&object->private_store);
        vkd3d_free(object);
        return hr;
    }

    d3d_destruction_notifier_init(&object->destruction_notifier, (IUnknown*)&object->ID3D12Fence_iface);
    d3d12_device_add_ref(object->device = device);

    pthread_mutex_init(&object->mutex, NULL);
    pthread_cond_init(&object->cond_var, NULL);
    list_init(&object->events);
    object->is_running = false;

    *fence = object;
    return S_OK;
}

static void d3d12_fence_iface_inc_ref(d3d12_fence_iface *iface)
{
    struct d3d12_shared_fence *shared_fence;
    struct d3d12_fence *fence;

    if (is_shared_ID3D12Fence1(iface))
    {
        shared_fence = shared_impl_from_ID3D12Fence1(iface);
        d3d12_shared_fence_inc_ref(shared_fence);
    }
    else
    {
        fence = impl_from_ID3D12Fence1(iface);
        d3d12_fence_inc_ref(fence);
    }
}

static void d3d12_fence_iface_dec_ref(d3d12_fence_iface *iface)
{
    struct d3d12_shared_fence *shared_fence;
    struct d3d12_fence *fence;

    if (is_shared_ID3D12Fence1(iface))
    {
        shared_fence = shared_impl_from_ID3D12Fence1(iface);
        d3d12_shared_fence_dec_ref(shared_fence);
    }
    else
    {
        fence = impl_from_ID3D12Fence1(iface);
        d3d12_fence_dec_ref(fence);
    }
}

HRESULT d3d12_fence_iface_set_native_sync_handle_on_completion_explicit(ID3D12Fence *iface,
        enum vkd3d_waiting_event_type wait_type, UINT64 value, vkd3d_native_sync_handle handle, uint32_t *payload)
{
    struct d3d12_shared_fence *shared_fence;
    struct d3d12_fence *fence;

    if (is_shared_ID3D12Fence(iface))
    {
        shared_fence = shared_impl_from_ID3D12Fence(iface);
        return d3d12_shared_fence_set_native_sync_handle_on_completion_explicit(
                shared_fence, wait_type, value, handle, payload);
    }
    else
    {
        fence = impl_from_ID3D12Fence(iface);
        return d3d12_fence_set_native_sync_handle_on_completion_explicit(
                fence, wait_type, value, handle, payload);
    }
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
    begin_info.flags = (vkd3d_config_flags & VKD3D_CONFIG_FLAG_ONE_TIME_SUBMIT) ?
            VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT : 0;
    begin_info.pInheritanceInfo = NULL;

    if ((vr = VK_CALL(vkBeginCommandBuffer(list->cmd.vk_command_buffer, &begin_info))) < 0)
    {
        WARN("Failed to begin command buffer, vr %d.\n", vr);
        return hresult_from_vk_result(vr);
    }

    d3d12_command_list_debug_mark_begin_region(list, "CommandList");

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

    memset(&list->cmd, 0, sizeof(list->cmd));
    list->cmd.indirect_meta = &list->cmd.iterations[0].indirect_meta;

    if ((vr = VK_CALL(vkAllocateCommandBuffers(device->vk_device, &command_buffer_info,
            &list->cmd.iterations[0].vk_command_buffer))) < 0)
    {
        WARN("Failed to allocate Vulkan command buffer, vr %d.\n", vr);
        return hresult_from_vk_result(vr);
    }

    list->cmd.vk_command_buffer = list->cmd.iterations[0].vk_command_buffer;
    list->vk_queue_flags = allocator->vk_queue_flags;

    if (FAILED(hr = d3d12_command_list_begin_command_buffer(list)))
    {
        VK_CALL(vkFreeCommandBuffers(device->vk_device, allocator->vk_command_pool,
                1, &list->cmd.vk_command_buffer));
        return hr;
    }

    list->cmd.iteration_count = 1;
    list->cmd.estimated_cost = 0;

#ifdef VKD3D_ENABLE_BREADCRUMBS
    if (vkd3d_config_flags & VKD3D_CONFIG_FLAG_BREADCRUMBS)
    {
        vkd3d_breadcrumb_tracer_allocate_command_list(&allocator->device->breadcrumb_tracer,
                list, allocator);
        vkd3d_breadcrumb_tracer_begin_command_list(list);
    }
#endif

    allocator->current_command_list = list;
    list->timeline_cookie = vkd3d_queue_timeline_trace_register_command_list(&allocator->device->queue_timeline_trace);

    return S_OK;
}

static void d3d12_command_list_begin_new_sequence(struct d3d12_command_list *list)
{
    const struct vkd3d_vk_device_procs *vk_procs = &list->device->vk_procs;
    VkConditionalRenderingBeginInfoEXT conditional_begin_info;
    VkCommandBufferAllocateInfo command_buffer_info;
    struct d3d12_command_list_iteration *iteration;
    VkCommandBufferBeginInfo begin_info;
    VkResult vr;

    if (list->cmd.iteration_count >= VKD3D_MAX_COMMAND_LIST_SEQUENCES)
        return;

    assert(list->cmd.iteration_count);
    list->cmd.iterations[list->cmd.iteration_count - 1].estimated_cost = list->cmd.estimated_cost;
    list->cmd.estimated_cost = 0;

    iteration = &list->cmd.iterations[list->cmd.iteration_count];
    command_buffer_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    command_buffer_info.pNext = NULL;
    command_buffer_info.commandPool = list->allocator->vk_command_pool;
    command_buffer_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    command_buffer_info.commandBufferCount = 1;

    if ((vr = VK_CALL(vkAllocateCommandBuffers(list->device->vk_device,
            &command_buffer_info, &iteration->vk_command_buffer))) < 0)
    {
        ERR("Failed to allocate Vulkan command buffer, vr %d.\n", vr);
        /* Not fatal, but we don't get to split. */
        return;
    }

    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.pNext = NULL;
    begin_info.flags = (vkd3d_config_flags & VKD3D_CONFIG_FLAG_ONE_TIME_SUBMIT) ?
            VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT : 0;
    begin_info.pInheritanceInfo = NULL;
    if ((vr = VK_CALL(vkBeginCommandBuffer(iteration->vk_command_buffer, &begin_info))) < 0)
    {
        VK_CALL(vkFreeCommandBuffers(list->device->vk_device, list->allocator->vk_command_pool,
                1, &iteration->vk_command_buffer));
        ERR("Failed to begin Vulkan command buffer, vr %d.\n", vr);
        return;
    }

    /* Some things we *have* to end now because API says so.
     * Most cleanup can be deferred to Close(). */
    d3d12_command_list_end_current_render_pass(list, true);
    if (list->predication.enabled_on_command_buffer)
        VK_CALL(vkCmdEndConditionalRenderingEXT(list->cmd.vk_command_buffer));

    if ((vr = VK_CALL(vkEndCommandBuffer(list->cmd.vk_command_buffer)) < 0))
        ERR("Failed to end command buffer, vr %d.\n", vr);

    list->cmd.vk_command_buffer = iteration->vk_command_buffer;
    list->cmd.vk_init_commands_post_indirect_barrier = VK_NULL_HANDLE;
    list->cmd.indirect_meta = &list->cmd.iterations[list->cmd.iteration_count].indirect_meta;
    list->cmd.iteration_count++;

    if (list->predication.enabled_on_command_buffer)
    {
        /* Rearm the conditional rendering. */
        conditional_begin_info.sType = VK_STRUCTURE_TYPE_CONDITIONAL_RENDERING_BEGIN_INFO_EXT;
        conditional_begin_info.pNext = NULL;
        conditional_begin_info.buffer = list->predication.vk_buffer;
        conditional_begin_info.offset = list->predication.vk_buffer_offset;
        conditional_begin_info.flags = 0;
        VK_CALL(vkCmdBeginConditionalRenderingEXT(list->cmd.vk_command_buffer, &conditional_begin_info));
    }

    d3d12_command_list_invalidate_all_state(list);
    /* Extra special consideration since we're starting a fresh command buffer. */
    list->descriptor_heap.buffers.heap_dirty = true;
    d3d12_command_list_debug_mark_label(list, "Split", 0.0f, 0.0f, 0.0f, 1.0f);
}

static void d3d12_command_list_consider_new_sequence(struct d3d12_command_list *list)
{
    /* Not worth splitting if we're in the middle of a render pass already. */
    if (list->cmd.vk_command_buffer == list->cmd.vk_init_commands_post_indirect_barrier &&
            !(list->rendering_info.state_flags & VKD3D_RENDERING_ACTIVE) &&
            vkd3d_atomic_uint32_load_explicit(&list->device->device_has_dgc_templates, vkd3d_memory_order_relaxed))
    {
        /* We could in theory virtualize these queries, but that is extreme overkill. */
        if (list->cmd.active_non_inline_running_queries == 0)
            d3d12_command_list_begin_new_sequence(list);
        else
            WARN("Avoiding split due to long running scoped query.\n");
    }
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

    if (list->cmd.vk_init_commands)
        return S_OK;

    command_buffer_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    command_buffer_info.pNext = NULL;
    command_buffer_info.commandPool = allocator->vk_command_pool;
    command_buffer_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    command_buffer_info.commandBufferCount = 1;

    if ((vr = VK_CALL(vkAllocateCommandBuffers(device->vk_device, &command_buffer_info,
            &list->cmd.iterations[0].vk_init_commands))) < 0)
    {
        WARN("Failed to allocate Vulkan command buffer, vr %d.\n", vr);
        return hresult_from_vk_result(vr);
    }

    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.pNext = NULL;
    begin_info.flags = (vkd3d_config_flags & VKD3D_CONFIG_FLAG_ONE_TIME_SUBMIT) ?
            VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT : 0;
    begin_info.pInheritanceInfo = NULL;

    if ((vr = VK_CALL(vkBeginCommandBuffer(list->cmd.iterations[0].vk_init_commands, &begin_info))) < 0)
    {
        WARN("Failed to begin command buffer, vr %d.\n", vr);
        VK_CALL(vkFreeCommandBuffers(device->vk_device, allocator->vk_command_pool,
                1, &list->cmd.iterations[0].vk_init_commands));
        return hresult_from_vk_result(vr);
    }

    list->cmd.vk_init_commands = list->cmd.iterations[0].vk_init_commands;

    return S_OK;
}

static HRESULT d3d12_command_allocator_allocate_init_post_indirect_command_buffer(
        struct d3d12_command_allocator *allocator, struct d3d12_command_list *list)
{
    struct d3d12_device *device = allocator->device;
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    VkCommandBufferAllocateInfo command_buffer_info;
    struct d3d12_command_list_iteration *iteration;
    VkCommandBufferBeginInfo begin_info;
    VkResult vr;

    TRACE("allocator %p, list %p.\n", allocator, list);

    if (list->cmd.vk_init_commands_post_indirect_barrier)
        return S_OK;

    assert(list->cmd.iteration_count != 0);
    iteration = &list->cmd.iterations[list->cmd.iteration_count - 1];
    assert(!iteration->vk_init_commands);

    command_buffer_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    command_buffer_info.pNext = NULL;
    command_buffer_info.commandPool = allocator->vk_command_pool;
    command_buffer_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    command_buffer_info.commandBufferCount = 1;

    if ((vr = VK_CALL(vkAllocateCommandBuffers(device->vk_device, &command_buffer_info,
            &iteration->vk_init_commands))) < 0)
    {
        WARN("Failed to allocate Vulkan command buffer, vr %d.\n", vr);
        return hresult_from_vk_result(vr);
    }

    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.pNext = NULL;
    begin_info.flags = (vkd3d_config_flags & VKD3D_CONFIG_FLAG_ONE_TIME_SUBMIT) ?
            VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT : 0;
    begin_info.pInheritanceInfo = NULL;

    if ((vr = VK_CALL(vkBeginCommandBuffer(iteration->vk_init_commands, &begin_info))) < 0)
    {
        WARN("Failed to begin command buffer, vr %d.\n", vr);
        VK_CALL(vkFreeCommandBuffers(device->vk_device, allocator->vk_command_pool,
                1, &iteration->vk_init_commands));
        return hresult_from_vk_result(vr);
    }

    /* If we're still on the first iteration, we've also initialized the normal init commands buffer. */
    list->cmd.vk_init_commands_post_indirect_barrier = iteration->vk_init_commands;
    if (list->cmd.iteration_count == 1)
        list->cmd.vk_init_commands = iteration->vk_init_commands;

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
    unsigned int i;
    TRACE("allocator %p, list %p.\n", allocator, list);

    if (allocator->current_command_list == list)
        allocator->current_command_list = NULL;

    for (i = 0; i < list->cmd.iteration_count; i++)
    {
        d3d12_command_allocator_free_vk_command_buffer(allocator, list->cmd.iterations[i].vk_command_buffer);
        d3d12_command_allocator_free_vk_command_buffer(allocator, list->cmd.iterations[i].vk_init_commands);
    }
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

static void d3d12_command_allocator_retain_pipeline_state(struct d3d12_command_allocator *allocator,
        struct d3d12_pipeline_state *state)
{
    if (!vkd3d_array_reserve((void **)&allocator->pipelines, &allocator->pipelines_size,
            allocator->pipelines_count + 1, sizeof(*allocator->pipelines)))
        return;

    d3d12_pipeline_state_inc_ref(state);
    allocator->pipelines[allocator->pipelines_count++] = state;
}

static void d3d12_command_list_allocator_destroyed(struct d3d12_command_list *list)
{
    TRACE("list %p.\n", list);

    list->allocator = NULL;
    list->submit_allocator = NULL;
    memset(&list->cmd, 0, sizeof(list->cmd));
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

    for (i = 0; i < allocator->view_count; ++i)
    {
        vkd3d_view_decref(allocator->views[i], device);
    }
    allocator->view_count = 0;

    for (i = 0; i < allocator->pipelines_count; i++)
    {
        d3d12_pipeline_state_dec_ref(allocator->pipelines[i]);
    }
    allocator->pipelines_count = 0;
}

static void d3d12_command_allocator_set_name(struct d3d12_command_allocator *allocator, const char *name)
{
    vkd3d_set_vk_object_name(allocator->device, (uint64_t)allocator->vk_command_pool,
            VK_OBJECT_TYPE_COMMAND_POOL, name);
}

/* ID3D12CommandAllocator */
static inline struct d3d12_command_allocator *impl_from_ID3D12CommandAllocator(ID3D12CommandAllocator *iface)
{
    return CONTAINING_RECORD(iface, struct d3d12_command_allocator, ID3D12CommandAllocator_iface);
}

static HRESULT STDMETHODCALLTYPE d3d12_command_allocator_QueryInterface(ID3D12CommandAllocator *iface,
        REFIID riid, void **object)
{
    struct d3d12_command_allocator *allocator = impl_from_ID3D12CommandAllocator(iface);

    TRACE("iface %p, riid %s, object %p.\n", iface, debugstr_guid(riid), object);

    if (!object)
        return E_POINTER;

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

    if (IsEqualGUID(riid, &IID_ID3DDestructionNotifier))
    {
        ID3DDestructionNotifier_AddRef(&allocator->destruction_notifier.ID3DDestructionNotifier_iface);
        *object = &allocator->destruction_notifier.ID3DDestructionNotifier_iface;
        return S_OK;
    }

    WARN("%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid(riid));

    *object = NULL;
    return E_NOINTERFACE;
}

static ULONG d3d12_command_allocator_inc_ref(struct d3d12_command_allocator *allocator)
{
    return InterlockedIncrement(&allocator->internal_refcount);
}

static ULONG STDMETHODCALLTYPE d3d12_command_allocator_AddRef(ID3D12CommandAllocator *iface)
{
    struct d3d12_command_allocator *allocator = impl_from_ID3D12CommandAllocator(iface);
    ULONG refcount = InterlockedIncrement(&allocator->refcount);

    TRACE("%p increasing refcount to %u.\n", allocator, refcount);

    if (refcount == 1)
    {
        d3d12_command_allocator_inc_ref(allocator);
        d3d12_device_add_ref(allocator->device);
    }

    return refcount;
}

static ULONG d3d12_command_allocator_dec_ref(struct d3d12_command_allocator *allocator)
{
    unsigned int i, j;
    ULONG refcount;

    refcount = InterlockedDecrement(&allocator->internal_refcount);

    if (!refcount)
    {
        struct d3d12_device *device = allocator->device;
        const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;

        d3d_destruction_notifier_free(&allocator->destruction_notifier);
        vkd3d_private_store_destroy(&allocator->private_store);

        if (allocator->current_command_list)
            d3d12_command_list_allocator_destroyed(allocator->current_command_list);

        d3d12_command_allocator_free_resources(allocator);
        vkd3d_free(allocator->buffer_views);
        vkd3d_free(allocator->views);
        vkd3d_free(allocator->pipelines);

        if (vkd3d_config_flags & VKD3D_CONFIG_FLAG_RECYCLE_COMMAND_POOLS)
        {
            /* Don't want to do this unless we have to, so hide it behind a config.
             * For well-behaving apps, we'll just bloat memory. */
            if (pthread_mutex_lock(&device->mutex) == 0)
            {
                if (device->cached_command_allocator_count < ARRAY_SIZE(device->cached_command_allocators))
                {
                    /* Recycle the pool. Some games spam free/allocate pools,
                     * even if it completely goes against the point of the API. */

                    /* Have to free command buffers here if we're going to recycle,
                     * otherwise DestroyCommandPool takes care of it. */
                    VK_CALL(vkFreeCommandBuffers(device->vk_device, allocator->vk_command_pool,
                            allocator->command_buffer_count, allocator->command_buffers));
                    VK_CALL(vkResetCommandPool(device->vk_device, allocator->vk_command_pool, 0));

                    device->cached_command_allocators[device->cached_command_allocator_count].vk_command_pool =
                            allocator->vk_command_pool;
                    device->cached_command_allocators[device->cached_command_allocator_count].vk_family_index =
                            allocator->vk_family_index;
                    device->cached_command_allocator_count++;
                    allocator->vk_command_pool = VK_NULL_HANDLE;
                }

                pthread_mutex_unlock(&device->mutex);
            }
        }

        /* Command buffers are implicitly freed when destroying the pool. */
        vkd3d_free(allocator->command_buffers);
        VK_CALL(vkDestroyCommandPool(device->vk_device, allocator->vk_command_pool, NULL));

        for (i = 0; i < VKD3D_SCRATCH_POOL_KIND_COUNT; i++)
        {
            for (j = 0; j < allocator->scratch_pools[i].scratch_buffer_count; j++)
                d3d12_device_return_scratch_buffer(device, i, &allocator->scratch_pools[i].scratch_buffers[j]);
            vkd3d_free(allocator->scratch_pools[i].scratch_buffers);
        }

        for (i = 0; i < allocator->query_pool_count; i++)
            d3d12_device_return_query_pool(device, &allocator->query_pools[i]);

        vkd3d_free(allocator->query_pools);

#ifdef VKD3D_ENABLE_BREADCRUMBS
        if (vkd3d_config_flags & VKD3D_CONFIG_FLAG_BREADCRUMBS)
        {
            vkd3d_breadcrumb_tracer_release_command_lists(&device->breadcrumb_tracer,
                    allocator->breadcrumb_context_indices, allocator->breadcrumb_context_index_count);
            vkd3d_free(allocator->breadcrumb_context_indices);
        }
#endif

        vkd3d_free(allocator);
    }

    return refcount;
}

static ULONG STDMETHODCALLTYPE d3d12_command_allocator_Release(ID3D12CommandAllocator *iface)
{
    struct d3d12_command_allocator *allocator = impl_from_ID3D12CommandAllocator(iface);
    ULONG refcount = InterlockedDecrement(&allocator->refcount);
    unsigned int pending;

    TRACE("%p decreasing refcount to %u.\n", allocator, refcount);

    if (!refcount)
    {
        struct d3d12_device *device = allocator->device;

        d3d_destruction_notifier_notify(&allocator->destruction_notifier);
        pending = d3d12_command_allocator_dec_ref(allocator);

        /* If this does not go to zero, it means that there are still command lists in flight. */
        if (pending != 0)
        {
            ERR("Released all public references, but there are still %u pending command lists "
                "awaiting execution from command allocator iface %p! Deferring release.\n", pending, iface);
        }

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

    return vkd3d_set_private_data(&allocator->private_store, guid, data_size, data,
            (vkd3d_set_name_callback) d3d12_command_allocator_set_name, allocator);
}

static HRESULT STDMETHODCALLTYPE d3d12_command_allocator_SetPrivateDataInterface(ID3D12CommandAllocator *iface,
        REFGUID guid, const IUnknown *data)
{
    struct d3d12_command_allocator *allocator = impl_from_ID3D12CommandAllocator(iface);

    TRACE("iface %p, guid %s, data %p.\n", iface, debugstr_guid(guid), data);

    return vkd3d_set_private_data_interface(&allocator->private_store, guid, data,
            (vkd3d_set_name_callback) d3d12_command_allocator_set_name, allocator);
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
    LONG internal_refs;
    VkResult vr;
    size_t i, j;

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

    /* We only expect there to be only one internal ref live, the public refcount. */
    if ((internal_refs = vkd3d_atomic_uint32_load_explicit(&allocator->internal_refcount, vkd3d_memory_order_acquire)) > 1)
    {
        /* HACK: There are currently command lists waiting to be submitted to the queue in the submission threads.
         * Buggy application, but work around this by not resetting the command pool this time.
         * To be perfectly safe, we can only reset after the fence timeline is signalled,
         * however, this is enough to workaround SotTR which resets the command list right
         * after calling ID3D12CommandQueue::ExecuteCommandLists().
         * Only happens once or twice on bootup and doesn't cause memory leaks over time
         * since the command pool is eventually reset. */

        /* Runtime appears to detect this case, but does not return E_FAIL for whatever reason anymore. */
        ERR("There are still %u pending command lists awaiting execution from command allocator iface %p!\n",
            (unsigned int)(internal_refs - 1), iface);
        return S_OK;
    }

    device = allocator->device;
    vk_procs = &device->vk_procs;

    d3d12_command_allocator_free_resources(allocator);

    vkd3d_queue_timeline_trace_register_instantaneous(&device->queue_timeline_trace,
            VKD3D_QUEUE_TIMELINE_TRACE_STATE_TYPE_COMMAND_ALLOCATOR_RESET, allocator->command_buffer_count);

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
    for (i = 0; i < VKD3D_SCRATCH_POOL_KIND_COUNT; i++)
    {
        for (j = 0; j < allocator->scratch_pools[i].scratch_buffer_count; j++)
            d3d12_device_return_scratch_buffer(device, i, &allocator->scratch_pools[i].scratch_buffers[j]);
        allocator->scratch_pools[i].scratch_buffer_count = 0;
    }

#ifdef VKD3D_ENABLE_BREADCRUMBS
    if (vkd3d_config_flags & VKD3D_CONFIG_FLAG_BREADCRUMBS)
    {
        /* Release breadcrumb references. */
        vkd3d_breadcrumb_tracer_release_command_lists(&device->breadcrumb_tracer,
                allocator->breadcrumb_context_indices, allocator->breadcrumb_context_index_count);
        allocator->breadcrumb_context_index_count = 0;
    }
#endif

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
    (void *)d3d12_object_SetName,
    /* ID3D12DeviceChild methods */
    d3d12_command_allocator_GetDevice,
    /* ID3D12CommandAllocator methods */
    d3d12_command_allocator_Reset,
};

struct vkd3d_queue_family_info *d3d12_device_get_vkd3d_queue_family(struct d3d12_device *device,
        D3D12_COMMAND_LIST_TYPE type,
        uint32_t vk_family_index)
{
    if (vk_family_index == VK_QUEUE_FAMILY_IGNORED)
    {
        switch (type)
        {
            case D3D12_COMMAND_LIST_TYPE_DIRECT:
                return device->queue_families[VKD3D_QUEUE_FAMILY_GRAPHICS];
            case D3D12_COMMAND_LIST_TYPE_COMPUTE:
                return device->queue_families[VKD3D_QUEUE_FAMILY_COMPUTE];
            case D3D12_COMMAND_LIST_TYPE_COPY:
                return device->queue_families[VKD3D_QUEUE_FAMILY_TRANSFER];
            default:
                FIXME("Unhandled command list type %#x.\n", type);
                return device->queue_families[VKD3D_QUEUE_FAMILY_GRAPHICS];
        }
    }
    else
    {
        for (int i = 0; i < VKD3D_QUEUE_FAMILY_COUNT; i++)
        {
            if (device->queue_families[i]->vk_family_index == vk_family_index)
                return device->queue_families[i];
        }
        FIXME("Unhandled command list vk_family %#x.\n", vk_family_index);
        return device->queue_families[VKD3D_QUEUE_FAMILY_GRAPHICS];
    }
}

struct vkd3d_queue *d3d12_device_allocate_vkd3d_queue(struct vkd3d_queue_family_info *queue_family,
        struct d3d12_command_queue *command_queue)
{
    struct vkd3d_queue *queue;
    unsigned int i;

    for (i = 0; i < queue_family->queue_count; i++)
        pthread_mutex_lock(&queue_family->queues[i]->mutex);

    /* Select the queue that has the lowest number of virtual queues mapped
     * to it, in order to avoid situations where we map multiple queues to
     * the same vkd3d queue while others are unused */
    queue = queue_family->queues[0];

    for (i = 1; i < queue_family->queue_count; i++)
    {
        if (queue_family->queues[i]->virtual_queue_count < queue->virtual_queue_count)
            queue = queue_family->queues[i];
    }

    queue->virtual_queue_count++;

    if (command_queue)
    {
        vkd3d_array_reserve((void**)&queue->command_queues, &queue->command_queue_size,
                queue->command_queue_count + 1, sizeof(*queue->command_queues));

        queue->command_queues[queue->command_queue_count++] = command_queue;
    }

    for (i = 0; i < queue_family->queue_count; i++)
        pthread_mutex_unlock(&queue_family->queues[i]->mutex);

    return queue;
}

void d3d12_device_unmap_vkd3d_queue(struct vkd3d_queue *queue, struct d3d12_command_queue *command_queue)
{
    size_t i;

    pthread_mutex_lock(&queue->mutex);
    queue->virtual_queue_count--;

    if (command_queue)
    {
        for (i = 0; i < queue->command_queue_count; i++)
        {
            if (queue->command_queues[i] == command_queue)
            {
                queue->command_queue_count--;
                queue->command_queues[i] = queue->command_queues[queue->command_queue_count];
                break;
            }
        }
    }

    pthread_mutex_unlock(&queue->mutex);
}

static HRESULT d3d12_command_allocator_init(struct d3d12_command_allocator *allocator,
        struct d3d12_device *device,
        D3D12_COMMAND_LIST_TYPE type,
        struct vkd3d_queue_family_info *queue_family)
{
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    VkCommandPoolCreateInfo command_pool_info;
    VkResult vr;
    HRESULT hr;
    size_t i;

    if (FAILED(hr = vkd3d_private_store_init(&allocator->private_store)))
        return hr;

    allocator->ID3D12CommandAllocator_iface.lpVtbl = &d3d12_command_allocator_vtbl;
    allocator->refcount = 1;
    allocator->internal_refcount = 1;
    allocator->type = type;
    allocator->vk_queue_flags = queue_family->vk_queue_flags;

    command_pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    command_pool_info.pNext = NULL;
    /* Do not use RESET_COMMAND_BUFFER_BIT. This allows the CommandPool to be a D3D12-style command pool.
     * Memory is owned by the pool and CommandBuffers become lightweight handles,
     * assuming a half-decent driver implementation. */
    command_pool_info.flags = 0;
    command_pool_info.queueFamilyIndex = queue_family->vk_family_index;

    allocator->vk_command_pool = VK_NULL_HANDLE;
    allocator->vk_family_index = queue_family->vk_family_index;

    if (vkd3d_config_flags & VKD3D_CONFIG_FLAG_RECYCLE_COMMAND_POOLS)
    {
        /* Try to recycle command allocators. Some games spam free/allocate pools. */
        if (pthread_mutex_lock(&device->mutex) == 0)
        {
            for (i = 0; i < device->cached_command_allocator_count; i++)
            {
                if (device->cached_command_allocators[i].vk_family_index == queue_family->vk_family_index)
                {
                    allocator->vk_command_pool = device->cached_command_allocators[i].vk_command_pool;
                    device->cached_command_allocators[i] =
                            device->cached_command_allocators[--device->cached_command_allocator_count];
                    break;
                }
            }
            pthread_mutex_unlock(&device->mutex);
        }
    }

    if (allocator->vk_command_pool == VK_NULL_HANDLE)
    {
        if ((vr = VK_CALL(vkCreateCommandPool(device->vk_device, &command_pool_info, NULL,
                &allocator->vk_command_pool))) < 0)
        {
            WARN("Failed to create Vulkan command pool, vr %d.\n", vr);
            vkd3d_private_store_destroy(&allocator->private_store);
            return hresult_from_vk_result(vr);
        }
    }

    d3d_destruction_notifier_init(&allocator->destruction_notifier,
            (IUnknown*)&allocator->ID3D12CommandAllocator_iface);
    d3d12_device_add_ref(allocator->device = device);

    return S_OK;
}

HRESULT d3d12_command_allocator_create(struct d3d12_device *device,
        D3D12_COMMAND_LIST_TYPE type,
        uint32_t vk_family_index,
        struct d3d12_command_allocator **allocator)
{
    struct vkd3d_queue_family_info *family_info;
    struct d3d12_command_allocator *object;
    HRESULT hr;

    if (!(D3D12_COMMAND_LIST_TYPE_DIRECT <= type && type <= D3D12_COMMAND_LIST_TYPE_COPY))
    {
        WARN("Invalid type %#x.\n", type);
        return E_INVALIDARG;
    }

    if (!(object = vkd3d_calloc(1, sizeof(*object))))
        return E_OUTOFMEMORY;

    family_info = d3d12_device_get_vkd3d_queue_family(device, type, vk_family_index);
    if (FAILED(hr = d3d12_command_allocator_init(object, device, type, family_info)))
    {
        vkd3d_free(object);
        return hr;
    }

    TRACE("Created command allocator %p.\n", object);

    *allocator = object;

    return S_OK;
}

bool d3d12_command_allocator_allocate_scratch_memory(struct d3d12_command_allocator *allocator,
        enum vkd3d_scratch_pool_kind kind,
        VkDeviceSize size, VkDeviceSize alignment, uint32_t memory_types,
        struct vkd3d_scratch_allocation *allocation)
{
    struct d3d12_command_allocator_scratch_pool *pool = &allocator->scratch_pools[kind];
    VkDeviceSize aligned_offset, aligned_size;
    struct vkd3d_scratch_buffer *scratch;
    unsigned int i;

    aligned_size = align(size, alignment);

    /* Probe last block first since the others are likely full */
    for (i = pool->scratch_buffer_count; i; i--)
    {
        scratch = &pool->scratch_buffers[i - 1];

        /* Extremely unlikely to fail since we have separate lists per pool kind, but to be 100% correct ... */
        if (!(memory_types & (1u << scratch->allocation.device_allocation.vk_memory_type)))
            continue;

        aligned_offset = align(scratch->offset, alignment);

        if (aligned_offset + aligned_size <= scratch->allocation.resource.size)
        {
            scratch->offset = aligned_offset + aligned_size;

            allocation->buffer = scratch->allocation.resource.vk_buffer;
            allocation->offset = scratch->allocation.offset + aligned_offset;
            allocation->va = scratch->allocation.resource.va + aligned_offset;
            if (scratch->allocation.cpu_address)
                allocation->host_ptr = void_ptr_offset(scratch->allocation.cpu_address, aligned_offset);
            else
                allocation->host_ptr = NULL;
            return true;
        }
    }

    if (!vkd3d_array_reserve((void**)&pool->scratch_buffers, &pool->scratch_buffers_size,
            pool->scratch_buffer_count + 1, sizeof(*pool->scratch_buffers)))
    {
        ERR("Failed to allocate scratch buffer.\n");
        return false;
    }

    scratch = &pool->scratch_buffers[pool->scratch_buffer_count];
    if (FAILED(d3d12_device_get_scratch_buffer(allocator->device, kind, aligned_size, memory_types, scratch)))
    {
        ERR("Failed to create scratch buffer.\n");
        return false;
    }

    pool->scratch_buffer_count += 1;
    scratch->offset = aligned_size;

    allocation->buffer = scratch->allocation.resource.vk_buffer;
    allocation->offset = scratch->allocation.offset;
    allocation->va = scratch->allocation.resource.va;
    allocation->host_ptr = scratch->allocation.cpu_address;
    return true;
}

static struct vkd3d_query_pool *d3d12_command_allocator_get_active_query_pool_from_type_index(
        struct d3d12_command_allocator *allocator, uint32_t type_index)
{
    return &allocator->active_query_pools[type_index];
}

static uint32_t d3d12_query_heap_type_to_type_index(D3D12_QUERY_HEAP_TYPE heap_type)
{
    switch (heap_type)
    {
        case D3D12_QUERY_HEAP_TYPE_OCCLUSION:
            return VKD3D_QUERY_TYPE_INDEX_OCCLUSION;
        case D3D12_QUERY_HEAP_TYPE_PIPELINE_STATISTICS:
            return VKD3D_QUERY_TYPE_INDEX_PIPELINE_STATISTICS;
        case D3D12_QUERY_HEAP_TYPE_SO_STATISTICS:
            return VKD3D_QUERY_TYPE_INDEX_TRANSFORM_FEEDBACK;
        default:
            return UINT32_MAX;
    }
}

bool d3d12_command_allocator_allocate_query_from_type_index(
        struct d3d12_command_allocator *allocator,
        uint32_t type_index, VkQueryPool *query_pool, uint32_t *query_index)
{
    struct vkd3d_query_pool *pool = d3d12_command_allocator_get_active_query_pool_from_type_index(allocator, type_index);
    assert(pool);

    if (pool->next_index >= pool->query_count)
    {
        if (FAILED(d3d12_device_get_query_pool(allocator->device, type_index, pool)))
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

static bool d3d12_command_allocator_allocate_query_from_heap_type(struct d3d12_command_allocator *allocator,
        D3D12_QUERY_HEAP_TYPE heap_type, VkQueryPool *query_pool, uint32_t *query_index)
{
    uint32_t type_index = d3d12_query_heap_type_to_type_index(heap_type);
    return d3d12_command_allocator_allocate_query_from_type_index(allocator, type_index, query_pool, query_index);
}

static struct d3d12_command_allocator *d3d12_command_allocator_from_iface(ID3D12CommandAllocator *iface)
{
    if (!iface || iface->lpVtbl != &d3d12_command_allocator_vtbl)
        return NULL;

    return impl_from_ID3D12CommandAllocator(iface);
}

/* ID3D12CommandList */
static inline struct d3d12_command_list *impl_from_ID3D12GraphicsCommandList(d3d12_command_list_iface *iface)
{
    return CONTAINING_RECORD(iface, struct d3d12_command_list, ID3D12GraphicsCommandList_iface);
}

static void d3d12_command_list_invalidate_rendering_info(struct d3d12_command_list *list)
{
    list->rendering_info.state_flags &= ~VKD3D_RENDERING_CURRENT;
}

void d3d12_command_list_invalidate_current_pipeline(struct d3d12_command_list *list, bool meta_shader)
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

static D3D12_RECT d3d12_get_image_rect(struct d3d12_resource *resource, const VkImageSubresourceLayers *subresource)
{
    VkExtent3D mip_extent = d3d12_resource_desc_get_vk_subresource_extent(&resource->desc, resource->format, subresource);

    D3D12_RECT rect;
    rect.left = 0;
    rect.top = 0;
    rect.right = mip_extent.width;
    rect.bottom = mip_extent.height;
    return rect;
}

static bool d3d12_image_copy_writes_full_subresource(struct d3d12_resource *resource,
        const VkExtent3D *extent, const VkImageSubresourceLayers *subresource)
{
    VkExtent3D mip_extent = d3d12_resource_desc_get_vk_subresource_extent(&resource->desc, resource->format, subresource);
    return mip_extent.width == extent->width && mip_extent.height == extent->height && mip_extent.depth == extent->depth;
}

static bool vk_rect_from_d3d12(const D3D12_RECT *rect, VkRect2D *vk_rect, const D3D12_RECT *clamp_rect)
{
    D3D12_RECT clamped;

    clamped.left = max(rect->left, clamp_rect->left);
    clamped.right = min(rect->right, clamp_rect->right);
    clamped.top = max(rect->top, clamp_rect->top);
    clamped.bottom = min(rect->bottom, clamp_rect->bottom);

    if (clamped.top >= clamped.bottom || clamped.left >= clamped.right)
    {
        WARN("Empty clear rect.\n");
        return false;
    }

    vk_rect->offset.x = clamped.left;
    vk_rect->offset.y = clamped.top;
    vk_rect->extent.width = clamped.right - clamped.left;
    vk_rect->extent.height = clamped.bottom - clamped.top;
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

    if (!(list->rendering_info.state_flags & VKD3D_RENDERING_ACTIVE))
        return -1;

    if (list->dsv.resource == resource)
    {
        const struct vkd3d_view *dsv = list->dsv.view;

        if (!list->rendering_info.dsv.imageView)
            return -1;

        if (dsv->info.texture.aspect_mask == subresource->aspectMask &&
                dsv->info.texture.miplevel_idx == subresource->mipLevel &&
                dsv->info.texture.layer_idx == subresource->baseArrayLayer &&
                dsv->info.texture.layer_count == subresource->layerCount)
            return D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT;
    }
    else
    {
        for (i = 0; i < D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT; i++)
        {
            const struct vkd3d_view *rtv = list->rtvs[i].view;

            if (!(list->rendering_info.rtv_mask & (1u << i)))
                continue;

            if (list->rtvs[i].resource != resource)
                continue;

            if (rtv->info.texture.aspect_mask == subresource->aspectMask &&
                    rtv->info.texture.miplevel_idx == subresource->mipLevel &&
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
    VkImageSubresourceLayers vk_subresource;
    VkClearAttachment vk_clear_attachment;
    VkClearRect vk_clear_rect;
    D3D12_RECT full_rect;
    unsigned int i;

    vk_subresource = vk_subresource_layers_from_view(view);
    full_rect = d3d12_get_image_rect(resource, &vk_subresource);

    if (!rect_count)
    {
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
        if (vk_rect_from_d3d12(&rects[i], &vk_clear_rect.rect, &full_rect))
        {
            VK_CALL(vkCmdClearAttachments(list->cmd.vk_command_buffer,
                    1, &vk_clear_attachment, 1, &vk_clear_rect));
        }
    }

    VKD3D_BREADCRUMB_TAG("clear-view-cookie");
    VKD3D_BREADCRUMB_COOKIE(view->cookie);
    VKD3D_BREADCRUMB_RESOURCE(resource);
    VKD3D_BREADCRUMB_COMMAND(CLEAR_INLINE);
}

static void d3d12_command_list_sync_tiler_renderpass_writes(struct d3d12_command_list *list, const VkRenderingInfo *rendering_info)
{
    const struct vkd3d_vk_device_procs *vk_procs = &list->device->vk_procs;
    VkMemoryBarrier2 vk_barrier;
    VkDependencyInfo dep_info;

    /* tilers can emit these with minimal sync overhead, IMRs get annihilated */
    if (list->device->workarounds.tiler_renderpass_barriers)
    {
        memset(&vk_barrier, 0, sizeof(vk_barrier));
        vk_barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
        if (rendering_info->colorAttachmentCount)
        {
            vk_barrier.srcStageMask |= VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
            vk_barrier.srcAccessMask |= VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
            vk_barrier.dstStageMask |= VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
            vk_barrier.dstAccessMask |= VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
        }
        if (rendering_info->pDepthAttachment || rendering_info->pStencilAttachment)
        {
            vk_barrier.srcStageMask |= VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
            vk_barrier.srcAccessMask |= VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            vk_barrier.dstStageMask |= VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
            vk_barrier.dstAccessMask |= VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        }

        memset(&dep_info, 0, sizeof(dep_info));
        dep_info.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        dep_info.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
        dep_info.memoryBarrierCount = 1;
        dep_info.pMemoryBarriers = &vk_barrier;

        VK_CALL(vkCmdPipelineBarrier2(list->cmd.vk_command_buffer, &dep_info));
    }
}

static void d3d12_command_list_resolve_buffer_copy_writes(struct d3d12_command_list *list)
{
    const struct vkd3d_vk_device_procs *vk_procs = &list->device->vk_procs;
    VkMemoryBarrier2 vk_barrier;
    VkDependencyInfo dep_info;

    if (list->tracked_copy_buffer_count)
    {
        memset(&vk_barrier, 0, sizeof(vk_barrier));
        vk_barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
        vk_barrier.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
        vk_barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        vk_barrier.dstStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
        vk_barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;

        memset(&dep_info, 0, sizeof(dep_info));
        dep_info.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        dep_info.memoryBarrierCount = 1;
        dep_info.pMemoryBarriers = &vk_barrier;

        VK_CALL(vkCmdPipelineBarrier2(list->cmd.vk_command_buffer, &dep_info));

        list->tracked_copy_buffer_count = 0;
    }
}

static void d3d12_command_list_reset_buffer_copy_tracking(struct d3d12_command_list *list)
{
    list->tracked_copy_buffer_count = 0;
}

static void d3d12_command_list_mark_copy_buffer_write(struct d3d12_command_list *list, VkBuffer vk_buffer,
        VkDeviceSize offset, VkDeviceSize size, bool sparse)
{
    struct d3d12_buffer_copy_tracked_buffer *tracked_buffer;
    VkDeviceSize range_end;
    unsigned int i;

    if (sparse)
    {
        vk_buffer = VK_NULL_HANDLE;
        offset = 0;
        size = VK_WHOLE_SIZE;
    }

    range_end = offset + size;

    for (i = 0; i < list->tracked_copy_buffer_count; i++)
    {
        tracked_buffer = &list->tracked_copy_buffers[i];

        /* Any write to a sparse buffer will be considered to be aliasing with any other resource. */
        if (tracked_buffer->vk_buffer == vk_buffer || tracked_buffer->vk_buffer == VK_NULL_HANDLE || sparse)
        {
            if (range_end > tracked_buffer->hazard_begin && offset < tracked_buffer->hazard_end)
            {
                /* Hazard. Inject barrier. */
                d3d12_command_list_resolve_buffer_copy_writes(list);
                tracked_buffer = &list->tracked_copy_buffers[0];
                tracked_buffer->vk_buffer = vk_buffer;
                tracked_buffer->hazard_begin = offset;
                tracked_buffer->hazard_end = range_end;
                list->tracked_copy_buffer_count = 1;
            }
            else
            {
                tracked_buffer->hazard_begin = min(offset, tracked_buffer->hazard_begin);
                tracked_buffer->hazard_end = max(range_end, tracked_buffer->hazard_end);
            }
            return;
        }
    }

    /* Keep the tracking data structures lean and mean. If we have decent overlap, this isn't a real problem. */
    if (list->tracked_copy_buffer_count == ARRAY_SIZE(list->tracked_copy_buffers))
        d3d12_command_list_resolve_buffer_copy_writes(list);

    tracked_buffer = &list->tracked_copy_buffers[list->tracked_copy_buffer_count++];
    tracked_buffer->vk_buffer = vk_buffer;
    tracked_buffer->hazard_begin = offset;
    tracked_buffer->hazard_end = range_end;
}

static VkImageLayout dsv_plane_optimal_mask_to_layout(uint32_t plane_optimal_mask, VkImageAspectFlags image_aspects)
{
    static const VkImageLayout layouts[] = {
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
        VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_STENCIL_READ_ONLY_OPTIMAL,
        VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_STENCIL_ATTACHMENT_OPTIMAL,
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
    };

    if (plane_optimal_mask & VKD3D_DEPTH_STENCIL_PLANE_GENERAL)
        return VK_IMAGE_LAYOUT_GENERAL;

    if (image_aspects != (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT))
    {
        /* If aspects is only DEPTH or only STENCIL, we should use the OPTIMAL or READ_ONLY layout.
         * We should not use the separate layouts, or we might end up with more barriers than we need. */
        return plane_optimal_mask ?
                VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL :
                VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    }
    else
        return layouts[plane_optimal_mask];
}

static void d3d12_command_list_decay_optimal_dsv_resource(struct d3d12_command_list *list,
        const struct d3d12_resource *resource, uint32_t plane_optimal_mask,
        struct d3d12_command_list_barrier_batch *batch)
{
    bool current_layout_is_shader_visible;
    VkImageMemoryBarrier2 barrier;
    VkImageLayout layout;

    assert(!(plane_optimal_mask & ~(VKD3D_DEPTH_PLANE_OPTIMAL | VKD3D_STENCIL_PLANE_OPTIMAL)));
    layout = dsv_plane_optimal_mask_to_layout(plane_optimal_mask, resource->format->vk_aspect_mask);
    if (layout == resource->common_layout)
        return;

    current_layout_is_shader_visible = layout != VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    memset(&barrier, 0, sizeof(barrier));
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    barrier.oldLayout = layout;
    barrier.newLayout = resource->common_layout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    /* We want to wait for storeOp to complete here, and that is defined to happen in LATE_FRAGMENT_TESTS. */
    barrier.srcStageMask = VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
    barrier.srcAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    /* If one aspect was readable, we have to make it visible to shaders since the resource state might
     * have been DEPTH_READ | RESOURCE | NON_PIXEL_RESOURCE. If we transitioned from OPTIMAL,
     * there cannot possibly be shader reads until we observe a ResourceBarrier() later. */
    barrier.dstStageMask = current_layout_is_shader_visible ? VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT
            : (VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT);
    barrier.dstAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
            (current_layout_is_shader_visible ? VK_ACCESS_2_SHADER_READ_BIT : 0);
    barrier.subresourceRange.aspectMask = resource->format->vk_aspect_mask;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = VK_REMAINING_ARRAY_LAYERS;
    barrier.subresourceRange.levelCount = VK_REMAINING_MIP_LEVELS;
    barrier.image = resource->res.vk_image;

    d3d12_command_list_barrier_batch_add_layout_transition(list, batch, &barrier);
}

static uint32_t d3d12_command_list_notify_decay_dsv_resource(struct d3d12_command_list *list,
        struct d3d12_resource *resource)
{
    uint32_t decay_aspects;
    size_t i, n;

    /* No point in adding these since they are always deduced to be optimal or general. */
    if ((resource->desc.Flags & D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE) ||
            resource->common_layout == VK_IMAGE_LAYOUT_GENERAL)
        return 0;

    for (i = 0, n = list->dsv_resource_tracking_count; i < n; i++)
    {
        if (list->dsv_resource_tracking[i].resource == resource)
        {
            decay_aspects = list->dsv_resource_tracking[i].plane_optimal_mask;
            list->dsv_resource_tracking[i] = list->dsv_resource_tracking[--list->dsv_resource_tracking_count];
            return decay_aspects;
        }
    }

    return 0;
}

static uint32_t d3d12_command_list_promote_dsv_resource(struct d3d12_command_list *list,
        struct d3d12_resource *resource, uint32_t plane_optimal_mask)
{
    size_t i, n;
    assert(!(plane_optimal_mask & ~(VKD3D_DEPTH_PLANE_OPTIMAL | VKD3D_STENCIL_PLANE_OPTIMAL)));

    /* No point in adding these since they are always deduced to be optimal. */
    if (resource->desc.Flags & D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE)
        return VKD3D_DEPTH_PLANE_OPTIMAL | VKD3D_STENCIL_PLANE_OPTIMAL;
    else if (resource->common_layout == VK_IMAGE_LAYOUT_GENERAL)
        return VKD3D_DEPTH_STENCIL_PLANE_GENERAL;

    /* For single aspect images, mirror the optimal mask in the unused aspect. This avoids some
     * extra checks elsewhere (particularly graphics pipeline setup and compat render passes)
     * to handle single aspect DSVs. */
    if (!(resource->format->vk_aspect_mask & VK_IMAGE_ASPECT_STENCIL_BIT))
        plane_optimal_mask |= (plane_optimal_mask & VKD3D_DEPTH_PLANE_OPTIMAL) ? VKD3D_STENCIL_PLANE_OPTIMAL : 0;
    if (!(resource->format->vk_aspect_mask & VK_IMAGE_ASPECT_DEPTH_BIT))
        plane_optimal_mask |= (plane_optimal_mask & VKD3D_STENCIL_PLANE_OPTIMAL) ? VKD3D_DEPTH_PLANE_OPTIMAL : 0;

    for (i = 0, n = list->dsv_resource_tracking_count; i < n; i++)
    {
        if (list->dsv_resource_tracking[i].resource == resource)
        {
            list->dsv_resource_tracking[i].plane_optimal_mask |= plane_optimal_mask;
            return list->dsv_resource_tracking[i].plane_optimal_mask;
        }
    }

    vkd3d_array_reserve((void **)&list->dsv_resource_tracking, &list->dsv_resource_tracking_size,
            list->dsv_resource_tracking_count + 1, sizeof(*list->dsv_resource_tracking));
    list->dsv_resource_tracking[list->dsv_resource_tracking_count].resource = resource;
    list->dsv_resource_tracking[list->dsv_resource_tracking_count].plane_optimal_mask = plane_optimal_mask;
    list->dsv_resource_tracking_count++;
    return plane_optimal_mask;
}

static uint32_t d3d12_command_list_notify_dsv_writes(struct d3d12_command_list *list,
        struct d3d12_resource *resource, const struct vkd3d_view *view, uint32_t plane_write_mask)
{
    if (plane_write_mask & VKD3D_DEPTH_STENCIL_PLANE_GENERAL)
        return VKD3D_DEPTH_STENCIL_PLANE_GENERAL;

    assert(!(plane_write_mask & ~(VKD3D_DEPTH_PLANE_OPTIMAL | VKD3D_STENCIL_PLANE_OPTIMAL)));

    /* If we cover the entire resource, we can promote it to our target layout. */
    if (view->info.texture.layer_count == resource->desc.DepthOrArraySize &&
            resource->desc.MipLevels == 1)
    {
        return d3d12_command_list_promote_dsv_resource(list, resource, plane_write_mask);
    }
    else
    {
        d3d12_command_list_get_depth_stencil_resource_layout(list, resource, &plane_write_mask);
        return plane_write_mask;
    }
}

static void d3d12_command_list_notify_dsv_discard(struct d3d12_command_list *list,
        struct d3d12_resource *resource,
        uint32_t first_subresource, uint32_t subresource_count,
        uint32_t resource_subresource_count)
{
    if (subresource_count == resource_subresource_count)
    {
        d3d12_command_list_promote_dsv_resource(list, resource,
                VKD3D_DEPTH_PLANE_OPTIMAL | VKD3D_STENCIL_PLANE_OPTIMAL);
    }
    else if (resource->format->vk_aspect_mask == (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT))
    {
        /* Can we at least discard a plane fully? */

        if (first_subresource == 0 && subresource_count >= resource_subresource_count / 2)
        {
            if (resource->format->vk_aspect_mask & VK_IMAGE_ASPECT_DEPTH_BIT)
                d3d12_command_list_promote_dsv_resource(list, resource, VKD3D_DEPTH_PLANE_OPTIMAL);
            else if (resource->format->vk_aspect_mask & VK_IMAGE_ASPECT_STENCIL_BIT)
                d3d12_command_list_promote_dsv_resource(list, resource, VKD3D_STENCIL_PLANE_OPTIMAL);
        }
        else if (first_subresource <= resource_subresource_count / 2 &&
                first_subresource + subresource_count == resource_subresource_count)
        {
            if (resource->format->vk_aspect_mask & VK_IMAGE_ASPECT_STENCIL_BIT)
                d3d12_command_list_promote_dsv_resource(list, resource, VKD3D_STENCIL_PLANE_OPTIMAL);
        }
    }
}

/* Returns a mask of DSV aspects which should be considered to be fully transitioned for all subresources.
 * For these aspects, force layer/mip count to ALL. Only relevant for decay transitions.
 * We only promote when all subresources are transitioned as one, but if a single subresource enters read-only,
 * we decay the entire resource. */
static uint32_t d3d12_command_list_notify_dsv_state(struct d3d12_command_list *list,
        struct d3d12_resource *resource, D3D12_RESOURCE_STATES state, UINT subresource)
{
    /* Need to decide if we should promote or decay or promote DSV optimal state.
     * We can promote if we know for sure that all subresources are optimal.
     * If we observe any barrier which leaves this state, we must decay.
     *
     * Note: DEPTH_READ in isolation does not allow shaders to read a resource,
     * so we should keep it in OPTIMAL layouts. There is a certain risk of applications
     * screwing this up, but a workaround for that is to consider DEPTH_READ to be DEPTH_READ | RESOURCE
     * if applications prove to be buggy. */
    bool dsv_optimal = state == D3D12_RESOURCE_STATE_DEPTH_READ ||
            state == D3D12_RESOURCE_STATE_DEPTH_WRITE;
    uint32_t dsv_decay_mask = 0;

    if (!dsv_optimal)
    {
        dsv_decay_mask = d3d12_command_list_notify_decay_dsv_resource(list, resource);
    }
    else if (subresource == D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES)
    {
        d3d12_command_list_promote_dsv_resource(list, resource,
                VKD3D_DEPTH_PLANE_OPTIMAL | VKD3D_STENCIL_PLANE_OPTIMAL);
    }
    else if (resource->desc.MipLevels == 1 && resource->desc.DepthOrArraySize == 1)
    {
        /* For single mip/layer images (common case for depth-stencil),
         * a specific subresource can be handled correctly. */
        if (subresource == 0)
        {
            if (resource->format->vk_aspect_mask & VK_IMAGE_ASPECT_DEPTH_BIT)
                d3d12_command_list_promote_dsv_resource(list, resource, VKD3D_DEPTH_PLANE_OPTIMAL);
            else if (resource->format->vk_aspect_mask & VK_IMAGE_ASPECT_STENCIL_BIT)
                d3d12_command_list_promote_dsv_resource(list, resource, VKD3D_STENCIL_PLANE_OPTIMAL);
        }
        else if (subresource == 1)
        {
            if (resource->format->vk_aspect_mask == (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT))
                d3d12_command_list_promote_dsv_resource(list, resource, VKD3D_STENCIL_PLANE_OPTIMAL);
        }
    }

    return dsv_decay_mask;
}

static VkImageAspectFlags d3d12_barrier_subresource_range_covers_aspects(const struct d3d12_resource *resource,
        const D3D12_BARRIER_SUBRESOURCE_RANGE *range);

static uint32_t d3d12_command_list_notify_dsv_state_enhanced(struct d3d12_command_list *list,
        struct d3d12_resource *resource, const D3D12_TEXTURE_BARRIER *barrier)
{
    bool dsv_optimal = barrier->LayoutAfter == D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_WRITE ||
            barrier->LayoutAfter == D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_READ;
    VkImageAspectFlags covered_aspects;
    uint32_t dsv_promote_mask = 0;
    uint32_t dsv_decay_mask = 0;

    if (!dsv_optimal)
    {
        dsv_decay_mask = d3d12_command_list_notify_decay_dsv_resource(list, resource);
    }
    else if ((covered_aspects = d3d12_barrier_subresource_range_covers_aspects(resource, &barrier->Subresources)))
    {
        if (covered_aspects & VK_IMAGE_ASPECT_DEPTH_BIT)
            dsv_promote_mask |= VKD3D_DEPTH_PLANE_OPTIMAL;
        if (covered_aspects & VK_IMAGE_ASPECT_STENCIL_BIT)
            dsv_promote_mask |= VKD3D_STENCIL_PLANE_OPTIMAL;
        d3d12_command_list_promote_dsv_resource(list, resource, dsv_promote_mask);
    }

    return dsv_decay_mask;
}

static void d3d12_command_list_decay_optimal_dsv_resources(struct d3d12_command_list *list)
{
    struct d3d12_command_list_barrier_batch batch;
    size_t i, n;

    d3d12_command_list_barrier_batch_init(&batch);
    for (i = 0, n = list->dsv_resource_tracking_count; i < n; i++)
    {
        const struct d3d12_resource_tracking *track = &list->dsv_resource_tracking[i];
        d3d12_command_list_decay_optimal_dsv_resource(list, track->resource, track->plane_optimal_mask, &batch);
    }
    d3d12_command_list_barrier_batch_end(list, &batch);
    list->dsv_resource_tracking_count = 0;
}

static VkImageLayout d3d12_command_list_get_depth_stencil_resource_layout(const struct d3d12_command_list *list,
        const struct d3d12_resource *resource, uint32_t *plane_optimal_mask)
{
    size_t i, n;

    if (resource->desc.Flags & D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE)
    {
        if (plane_optimal_mask)
            *plane_optimal_mask = VKD3D_DEPTH_PLANE_OPTIMAL | VKD3D_STENCIL_PLANE_OPTIMAL;
        return VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    }
    else if (resource->common_layout == VK_IMAGE_LAYOUT_GENERAL)
    {
        if (plane_optimal_mask)
            *plane_optimal_mask = VKD3D_DEPTH_STENCIL_PLANE_GENERAL;
        return VK_IMAGE_LAYOUT_GENERAL;
    }

    for (i = 0, n = list->dsv_resource_tracking_count; i < n; i++)
    {
        if (resource == list->dsv_resource_tracking[i].resource)
        {
            if (plane_optimal_mask)
                *plane_optimal_mask = list->dsv_resource_tracking[i].plane_optimal_mask;
            return dsv_plane_optimal_mask_to_layout(list->dsv_resource_tracking[i].plane_optimal_mask,
                    resource->format->vk_aspect_mask);
        }
    }

    if (plane_optimal_mask)
        *plane_optimal_mask = 0;
    return resource->common_layout;
}

static VkImageLayout vk_separate_depth_layout(VkImageLayout combined_layout)
{
    if (combined_layout == VK_IMAGE_LAYOUT_GENERAL)
    {
        return combined_layout;
    }
    else
    {
        return (combined_layout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL ||
                combined_layout == VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_STENCIL_READ_ONLY_OPTIMAL) ?
                VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL;
    }
}

static VkImageLayout vk_separate_stencil_layout(VkImageLayout combined_layout)
{
    if (combined_layout == VK_IMAGE_LAYOUT_GENERAL)
    {
        return combined_layout;
    }
    else
    {
        return (combined_layout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL ||
                combined_layout == VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_STENCIL_ATTACHMENT_OPTIMAL) ?
                VK_IMAGE_LAYOUT_STENCIL_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_STENCIL_READ_ONLY_OPTIMAL;
    }
}

static bool d3d12_resource_may_alias_other_resources(struct d3d12_resource *resource)
{
    /* Treat a NULL resource as "all" resources. */
    if (!resource)
        return true;

    /* Cannot alias if the resource is allocated in a dedicated heap. */
    if (resource->flags & VKD3D_RESOURCE_ALLOCATION)
        return false;

    return true;
}

void d3d12_command_list_debug_mark_label(struct d3d12_command_list *list, const char *tag,
        float r, float g, float b, float a)
{
    const struct vkd3d_vk_device_procs *vk_procs = &list->device->vk_procs;
    VkDebugUtilsLabelEXT label;

    if ((vkd3d_config_flags & VKD3D_CONFIG_FLAG_DEBUG_UTILS) && list->device->vk_info.EXT_debug_utils)
    {
        label.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT;
        label.pNext = NULL;
        label.pLabelName = tag;
        label.color[0] = r;
        label.color[1] = g;
        label.color[2] = b;
        label.color[3] = a;
        VK_CALL(vkCmdInsertDebugUtilsLabelEXT(list->cmd.vk_command_buffer, &label));
    }
}

void d3d12_command_list_debug_mark_begin_region(
        struct d3d12_command_list *list, const char *tag)
{
    const struct vkd3d_vk_device_procs *vk_procs = &list->device->vk_procs;
    VkDebugUtilsLabelEXT label;

    if ((vkd3d_config_flags & VKD3D_CONFIG_FLAG_DEBUG_UTILS) &&
            !(vkd3d_config_flags & VKD3D_CONFIG_FLAG_APP_DEBUG_MARKER_ONLY) &&
            list->device->vk_info.EXT_debug_utils)
    {
        label.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT;
        label.pNext = NULL;
        label.pLabelName = tag;
        label.color[0] = 1.0f;
        label.color[1] = 1.0f;
        label.color[2] = 1.0f;
        label.color[3] = 1.0f;
        VK_CALL(vkCmdBeginDebugUtilsLabelEXT(list->cmd.vk_command_buffer, &label));
    }
}

void d3d12_command_list_debug_mark_end_region(struct d3d12_command_list *list)
{
    const struct vkd3d_vk_device_procs *vk_procs = &list->device->vk_procs;
    if ((vkd3d_config_flags & VKD3D_CONFIG_FLAG_DEBUG_UTILS) &&
            !(vkd3d_config_flags & VKD3D_CONFIG_FLAG_APP_DEBUG_MARKER_ONLY) &&
            list->device->vk_info.EXT_debug_utils)
    {
        VK_CALL(vkCmdEndDebugUtilsLabelEXT(list->cmd.vk_command_buffer));
    }
}

static void d3d12_command_list_load_attachment(struct d3d12_command_list *list, struct d3d12_resource *resource,
        struct vkd3d_view *view, VkImageAspectFlags clear_aspects, const VkClearValue *clear_value, UINT rect_count,
        const D3D12_RECT *rects, VkAttachmentLoadOp load_op)
{
    const struct vkd3d_vk_device_procs *vk_procs = &list->device->vk_procs;
    VkRenderingAttachmentInfo attachment_info, stencil_attachment_info;
    VkImageLayout initial_layouts[2], final_layouts[2];
    VkImageMemoryBarrier2 image_barriers[2];
    VkRenderingInfo rendering_info;
    bool requires_discard_barrier;
    VkPipelineStageFlags2 stages;
    uint32_t plane_write_mask, i;
    VkDependencyInfo dep_info;
    bool separate_ds_layouts;
    VkExtent3D view_extent;
    VkAccessFlags2 access;
    bool clear_op;

    /* This does not get called with LOAD_OP_LOAD */
    list->cmd.estimated_cost += VKD3D_COMMAND_COST_LOW;

    memset(initial_layouts, 0, sizeof(initial_layouts));
    memset(final_layouts, 0, sizeof(final_layouts));

    memset(&attachment_info, 0, sizeof(attachment_info));
    attachment_info.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    attachment_info.imageView = view->vk_image_view;
    attachment_info.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    attachment_info.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachment_info.clearValue = *clear_value;

    stencil_attachment_info = attachment_info;

    view_extent = d3d12_resource_get_view_subresource_extent(resource, view);

    memset(&rendering_info, 0, sizeof(rendering_info));
    rendering_info.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    rendering_info.renderArea.offset.x = 0;
    rendering_info.renderArea.offset.y = 0;
    rendering_info.renderArea.extent.width = view_extent.width;
    rendering_info.renderArea.extent.height = view_extent.height;
    rendering_info.layerCount = view->info.texture.layer_count;

    if (view->format->vk_aspect_mask & VK_IMAGE_ASPECT_COLOR_BIT)
    {
        rendering_info.colorAttachmentCount = 1;
        rendering_info.pColorAttachments = &attachment_info;
    }

    if (view->format->vk_aspect_mask & VK_IMAGE_ASPECT_DEPTH_BIT)
        rendering_info.pDepthAttachment = &attachment_info;
    if (view->format->vk_aspect_mask & VK_IMAGE_ASPECT_STENCIL_BIT)
        rendering_info.pStencilAttachment = &stencil_attachment_info;

    /* If we need to discard a single aspect, use separate layouts, since we have to use UNDEFINED barrier when we can. */
    separate_ds_layouts = view->format->vk_aspect_mask == (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT) &&
            clear_aspects != view->format->vk_aspect_mask;

    if (clear_aspects & (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT))
    {
        initial_layouts[0] = d3d12_command_list_get_depth_stencil_resource_layout(list, resource, NULL);

        if (separate_ds_layouts)
        {
            initial_layouts[1] = vk_separate_stencil_layout(initial_layouts[0]);
            initial_layouts[0] = vk_separate_depth_layout(initial_layouts[0]);
        }

        /* We have proven a write, try to promote the image layout to something OPTIMAL. */
        plane_write_mask = 0;
        if (clear_aspects & VK_IMAGE_ASPECT_DEPTH_BIT)
            plane_write_mask |= VKD3D_DEPTH_PLANE_OPTIMAL;
        if (clear_aspects & VK_IMAGE_ASPECT_STENCIL_BIT)
            plane_write_mask |= VKD3D_STENCIL_PLANE_OPTIMAL;

        final_layouts[0] = dsv_plane_optimal_mask_to_layout(
                d3d12_command_list_notify_dsv_writes(list, resource, view, plane_write_mask),
                resource->format->vk_aspect_mask);

        if (separate_ds_layouts)
        {
            /* Do not transition aspects that we are not supposed to clear */
            final_layouts[1] = vk_separate_stencil_layout(final_layouts[0]);
            final_layouts[0] = vk_separate_depth_layout(final_layouts[0]);

            attachment_info.imageLayout = final_layouts[0];
            stencil_attachment_info.imageLayout = final_layouts[1];
        }
        else
        {
            attachment_info.imageLayout = final_layouts[0];
            stencil_attachment_info.imageLayout = final_layouts[0];
        }
    }
    else
    {
        attachment_info.imageLayout = d3d12_resource_pick_layout(resource, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        initial_layouts[0] = attachment_info.imageLayout;
        final_layouts[0] = attachment_info.imageLayout;
    }

    if ((clear_op = !rect_count))
    {
        if (clear_aspects & (VK_IMAGE_ASPECT_COLOR_BIT | VK_IMAGE_ASPECT_DEPTH_BIT))
            attachment_info.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;

        if (clear_aspects & (VK_IMAGE_ASPECT_STENCIL_BIT))
            stencil_attachment_info.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;

        /* No need to perform extra transition barriers from UNDEFINED for committed resources.
         * The initial transition is handled by Clear*View().
         * Discarding with UNDEFINED is required to handle placed resources, however.
         * Also, if we're going to perform layout transitions anyways (for DSV),
         * might as well discard. */
        requires_discard_barrier = d3d12_resource_may_alias_other_resources(resource);
        if (separate_ds_layouts)
        {
            if (initial_layouts[0] != final_layouts[0] || initial_layouts[1] != final_layouts[1])
                requires_discard_barrier = true;
        }
        else if (initial_layouts[0] != final_layouts[0])
            requires_discard_barrier = true;

        /* Can only discard 3D images if we cover all layers at once.
         * > check takes care of REMAINING_LAYERS. */
        if (resource->desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D)
            if (view->info.texture.layer_idx != 0 || view->info.texture.layer_count < resource->desc.DepthOrArraySize)
                requires_discard_barrier = false;

        if (requires_discard_barrier)
        {
            if (separate_ds_layouts)
            {
                if (clear_aspects & VK_IMAGE_ASPECT_DEPTH_BIT)
                    initial_layouts[0] = VK_IMAGE_LAYOUT_UNDEFINED;
                if (clear_aspects & VK_IMAGE_ASPECT_STENCIL_BIT)
                    initial_layouts[1] = VK_IMAGE_LAYOUT_UNDEFINED;
            }
            else
                initial_layouts[0] = VK_IMAGE_LAYOUT_UNDEFINED;
        }
    }

    if (clear_aspects & (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT))
    {
        stages = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
        access = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

        if (!clear_op || clear_aspects != view->format->vk_aspect_mask)
            access |= VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
    }
    else
    {
        stages = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        access = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;

        if (!clear_op)
            access |= VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT;
    }

    memset(&dep_info, 0, sizeof(dep_info));
    dep_info.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dep_info.imageMemoryBarrierCount = 0;
    dep_info.pImageMemoryBarriers = image_barriers;

    for (i = 0; i < (separate_ds_layouts ? 2 : 1); i++)
    {
        if (initial_layouts[i] != final_layouts[i])
        {
            VkImageMemoryBarrier2 *barrier = &image_barriers[dep_info.imageMemoryBarrierCount++];

            memset(barrier, 0, sizeof(*barrier));
            barrier->sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
            barrier->srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier->dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier->image = resource->res.vk_image;
            barrier->srcStageMask = stages;
            barrier->dstStageMask = stages;
            barrier->dstAccessMask = access;
            barrier->oldLayout = initial_layouts[i];
            barrier->newLayout = final_layouts[i];
            barrier->subresourceRange.aspectMask = view->format->vk_aspect_mask;
            barrier->subresourceRange.baseMipLevel = view->info.texture.miplevel_idx;
            barrier->subresourceRange.levelCount = 1;
            barrier->subresourceRange.baseArrayLayer = view->info.texture.layer_idx;
            barrier->subresourceRange.layerCount = view->info.texture.layer_count;

            if (clear_op)
                barrier->srcAccessMask = access;

            if (resource->desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D)
            {
                barrier->subresourceRange.baseArrayLayer = 0;
                barrier->subresourceRange.layerCount = 1;
            }

            if (separate_ds_layouts)
                barrier->subresourceRange.aspectMask = i ? VK_IMAGE_ASPECT_STENCIL_BIT : VK_IMAGE_ASPECT_DEPTH_BIT;
        }
    }

    d3d12_command_list_debug_mark_begin_region(list, "Clear");

    if (dep_info.imageMemoryBarrierCount)
    {
        VKD3D_BREADCRUMB_TAG("clear-barrier");
        VK_CALL(vkCmdPipelineBarrier2(list->cmd.vk_command_buffer, &dep_info));
    }

    if (load_op == VK_ATTACHMENT_LOAD_OP_CLEAR)
    {
        VK_CALL(vkCmdBeginRendering(list->cmd.vk_command_buffer, &rendering_info));

        if (!clear_op)
        {
            d3d12_command_list_clear_attachment_inline(list, resource, view, 0,
                    clear_aspects, clear_value, rect_count, rects);
        }

        VK_CALL(vkCmdEndRendering(list->cmd.vk_command_buffer));
        d3d12_command_list_sync_tiler_renderpass_writes(list, &rendering_info);
    }

    VKD3D_BREADCRUMB_TAG("clear-view-cookie");
    VKD3D_BREADCRUMB_COOKIE(view->cookie);
    VKD3D_BREADCRUMB_RESOURCE(resource);
    VKD3D_BREADCRUMB_COMMAND(CLEAR_PASS);

    d3d12_command_list_debug_mark_end_region(list);
}

static VkPipelineStageFlags2 vk_queue_shader_stages(struct d3d12_device *device, VkQueueFlags vk_queue_flags)
{
    VkPipelineStageFlags2 queue_shader_stages = 0;

    if (vk_queue_flags & VK_QUEUE_GRAPHICS_BIT)
    {
        queue_shader_stages |= VK_PIPELINE_STAGE_2_PRE_RASTERIZATION_SHADERS_BIT |
                VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    }

    if (vk_queue_flags & VK_QUEUE_COMPUTE_BIT)
    {
        queue_shader_stages |= VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        if (device->device_info.ray_tracing_pipeline_features.rayTracingPipeline)
            queue_shader_stages |= VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR;
    }

    return queue_shader_stages;
}

static void d3d12_command_list_discard_attachment_barrier(struct d3d12_command_list *list,
        struct d3d12_resource *resource, const VkImageSubresourceRange *subresources, bool is_bound)
{
    const struct vkd3d_vk_device_procs *vk_procs = &list->device->vk_procs;
    VkImageMemoryBarrier2 barrier;
    VkPipelineStageFlags2 stages;
    VkDependencyInfo dep_info;
    VkAccessFlags2 access;
    VkImageLayout layout;

    /* Ignore read access bits since reads will be undefined anyway */
    if ((list->type == D3D12_COMMAND_LIST_TYPE_DIRECT) &&
            (resource->desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET))
    {
        stages = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        access = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
        layout = d3d12_resource_pick_layout(resource, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    }
    else if ((list->type == D3D12_COMMAND_LIST_TYPE_DIRECT) &&
            (resource->desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL))
    {
        stages = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
        access = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        layout = is_bound && list->dsv_layout ?
                list->dsv_layout :
                d3d12_command_list_get_depth_stencil_resource_layout(list, resource, NULL);
    }
    else if (resource->desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS)
    {
        stages = vk_queue_shader_stages(list->device, list->vk_queue_flags);
        access = VK_ACCESS_2_SHADER_WRITE_BIT | VK_ACCESS_2_SHADER_READ_BIT;
        layout = VK_IMAGE_LAYOUT_GENERAL;
    }
    else
    {
        ERR("Unsupported resource flags %#x.\n", resource->desc.Flags);
        return;
    }

    /* With separate depth stencil layouts, we can only discard the aspect we care about. */
    memset(&barrier, 0, sizeof(barrier));
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    barrier.srcStageMask = stages;
    barrier.srcAccessMask = access;
    barrier.dstStageMask = stages;
    barrier.dstAccessMask = access;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = layout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = resource->res.vk_image;
    barrier.subresourceRange = *subresources;

    memset(&dep_info, 0, sizeof(dep_info));
    dep_info.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dep_info.imageMemoryBarrierCount = 1;
    dep_info.pImageMemoryBarriers = &barrier;

    VK_CALL(vkCmdPipelineBarrier2(list->cmd.vk_command_buffer, &dep_info));
}

enum vkd3d_render_pass_transition_mode
{
    VKD3D_RENDER_PASS_TRANSITION_MODE_BEGIN,
    VKD3D_RENDER_PASS_TRANSITION_MODE_END,
};

static bool d3d12_resource_requires_shader_visibility_after_transition(
        const struct d3d12_resource *resource,
        VkImageLayout old_layout, VkImageLayout new_layout)
{
    return !(resource->desc.Flags & D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE) &&
            old_layout != VK_IMAGE_LAYOUT_UNDEFINED &&
            old_layout != new_layout &&
            (new_layout == VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL ||
                    new_layout == VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_STENCIL_READ_ONLY_OPTIMAL ||
                    new_layout == VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_STENCIL_ATTACHMENT_OPTIMAL);
}

static bool vk_render_pass_barrier_from_view(struct d3d12_command_list *list,
        const struct vkd3d_view *view, const struct d3d12_resource *resource,
        enum vkd3d_render_pass_transition_mode mode, VkImageLayout layout, VkImageMemoryBarrier2 *vk_barrier)
{
    VkImageLayout outside_render_pass_layout;
    VkPipelineStageFlags2 stages;
    VkAccessFlags2 access;

    if (view->format->vk_aspect_mask & VK_IMAGE_ASPECT_COLOR_BIT)
    {
        stages = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        access = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT;
        outside_render_pass_layout = d3d12_resource_pick_layout(resource, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    }
    else
    {
        stages = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
        access = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
        outside_render_pass_layout = d3d12_command_list_get_depth_stencil_resource_layout(list, resource, NULL);
    }

    memset(vk_barrier, 0, sizeof(*vk_barrier));
    vk_barrier->sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;

    if (mode == VKD3D_RENDER_PASS_TRANSITION_MODE_BEGIN)
    {
        vk_barrier->dstAccessMask = access;
        vk_barrier->oldLayout = outside_render_pass_layout;
        vk_barrier->newLayout = layout;

        /* If we're transitioning into depth state and we could potentially read
         * (we cannot know this here),
         * shader might want to read from it as well, so we have to make that visible here
         * if we're performing a layout transition, which nukes any existing visibility. */
        if (d3d12_resource_requires_shader_visibility_after_transition(resource,
                vk_barrier->oldLayout, vk_barrier->newLayout))
        {
            vk_barrier->dstAccessMask |= VK_ACCESS_2_SHADER_READ_BIT;
            stages = VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT;
        }
    }
    else /* if (mode == VKD3D_RENDER_PASS_TRANSITION_MODE_END) */
    {
        vk_barrier->srcAccessMask = access;
        vk_barrier->oldLayout = layout;
        vk_barrier->newLayout = outside_render_pass_layout;

        /* Dst access mask is generally 0 here since we are transitioning into an image layout
         * which only serves as a stepping stone for other layout transitions. When we use the image,
         * we are supposed to transition into another layout, and thus it is meaningless to make memory visible here.
         * The exception is depth attachments, which can be used right away without an internal transition barrier.
         * A case here is if the resource state is DEPTH_READ | RESOURCE. When we enter the enter pass,
         * we transition it into the appropriate DS state. When we leave, we would use DS_READ_ONLY_OPTIMAL,
         * which can be sampled from and used as a read-only depth attachment without any extra barrier.
         * Thus, we have to complete that barrier here. */
        if (vk_barrier->oldLayout != vk_barrier->newLayout)
        {
            if (vk_barrier->newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL)
            {
                vk_barrier->dstAccessMask =
                        VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_SHADER_READ_BIT;
                /* We don't know if we have DEPTH_READ | NON_PIXEL_RESOURCE or DEPTH_READ | PIXEL_RESOURCE. */
                stages = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
            }
            else if (vk_barrier->newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL)
            {
                vk_barrier->dstAccessMask =
                        VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            }
        }
    }

    vk_barrier->srcStageMask = stages;
    vk_barrier->dstStageMask = stages;

    /* The common case for color attachments is that this is a no-op.
     * An exception here is color attachment with SIMULTANEOUS use, where we need to decay to COMMON state.
     * Implicit decay or promotion does *not* happen for normal render targets, so we can rely on resource states.
     * For read-only depth or read-write depth for non-resource DSVs, this is also a no-op. */
    if (vk_barrier->oldLayout == vk_barrier->newLayout)
        return false;

    vk_barrier->srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    vk_barrier->dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    vk_barrier->image = resource->res.vk_image;
    vk_barrier->subresourceRange = vk_subresource_range_from_view(view);
    return true;
}

static void d3d12_command_list_track_resource_usage(struct d3d12_command_list *list,
        struct d3d12_resource *resource, bool perform_initial_transition);

static void d3d12_command_list_update_subresource_data(struct d3d12_command_list *list,
        struct d3d12_resource *resource, VkImageSubresourceLayers subresource)
{
    struct vkd3d_subresource_tracking *entry;
    VkImageAspectFlags aspect, aspect_mask;
    uint32_t i;
    bool skip;

    assert(resource->flags & VKD3D_RESOURCE_LINEAR_STAGING_COPY);
    aspect_mask = subresource.aspectMask;

    while (aspect_mask)
    {
        skip = false;

        aspect = aspect_mask & -aspect_mask;
        aspect_mask &= aspect_mask - 1;

        for (i = 0; i < list->subresource_tracking_count; )
        {
            entry = &list->subresource_tracking[i];

            if (entry->resource == resource && entry->subresource.aspectMask == aspect && entry->subresource.mipLevel == subresource.mipLevel)
            {
                if (entry->subresource.baseArrayLayer <= subresource.baseArrayLayer &&
                        entry->subresource.baseArrayLayer + entry->subresource.layerCount >= subresource.baseArrayLayer + subresource.layerCount)
                {
                    skip = true;
                    break;
                }
                else if (entry->subresource.baseArrayLayer <= subresource.baseArrayLayer + subresource.layerCount &&
                            entry->subresource.baseArrayLayer + entry->subresource.layerCount >= subresource.baseArrayLayer)
                {
                    subresource.baseArrayLayer = min(entry->subresource.baseArrayLayer, subresource.baseArrayLayer);
                    subresource.layerCount = max(entry->subresource.baseArrayLayer + entry->subresource.layerCount,
                            subresource.baseArrayLayer - subresource.layerCount) - entry->subresource.baseArrayLayer;
                    *entry = list->subresource_tracking[--list->subresource_tracking_count];
                    continue;
                }
            }

            i++;
        }

        if (skip)
            continue;

        if (!vkd3d_array_reserve((void **)&list->subresource_tracking, &list->subresource_tracking_size,
                list->subresource_tracking_count + 1, sizeof(*list->subresource_tracking)))
        {
            ERR("Failed to add subresource update.\n");
            return;
        }

        entry = &list->subresource_tracking[list->subresource_tracking_count++];
        entry->resource = resource;
        entry->subresource = subresource;
        entry->subresource.aspectMask = aspect;
    }
}

static void d3d12_command_list_flush_subresource_updates(struct d3d12_command_list *list)
{
    const struct vkd3d_vk_device_procs *vk_procs = &list->device->vk_procs;
    VkCopyImageToBufferInfo2 copy_info;
    VkBufferImageCopy2 copy_region;
    VkDependencyInfo dep_info;
    VkMemoryBarrier2 barrier;
    uint32_t i;

    if (!list->subresource_tracking_count)
        return;

    /* Images may not be in COMMON state anymore by the time the subresource
     * updates get resolved, however we should still perform the update. Emit
     * a full barrier to reduce the amount of tracking needed. */
    memset(&barrier, 0, sizeof(barrier));
    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
    barrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    barrier.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
    barrier.dstStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
    barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT | VK_ACCESS_2_TRANSFER_READ_BIT;

    memset(&dep_info, 0, sizeof(dep_info));
    dep_info.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dep_info.memoryBarrierCount = 1;
    dep_info.pMemoryBarriers = &barrier;

    VK_CALL(vkCmdPipelineBarrier2(list->cmd.vk_command_buffer, &dep_info));

    for (i = 0; i < list->subresource_tracking_count; i++)
    {
        const struct vkd3d_subresource_tracking *entry = &list->subresource_tracking[i];

        memset(&copy_region, 0, sizeof(copy_region));
        copy_region.sType = VK_STRUCTURE_TYPE_BUFFER_IMAGE_COPY_2;
        copy_region.bufferOffset = entry->resource->mem.offset;
        copy_region.imageSubresource = entry->subresource;
        copy_region.imageExtent = d3d12_resource_desc_get_vk_subresource_extent(&entry->resource->desc, entry->resource->format, &entry->subresource);

        memset(&copy_info, 0, sizeof(copy_info));
        copy_info.sType = VK_STRUCTURE_TYPE_COPY_IMAGE_TO_BUFFER_INFO_2;
        copy_info.srcImage = entry->resource->res.vk_image;
        copy_info.srcImageLayout = VK_IMAGE_LAYOUT_GENERAL;
        copy_info.dstBuffer = entry->resource->mem.resource.vk_buffer;
        copy_info.regionCount = 1;
        copy_info.pRegions = &copy_region;

        VK_CALL(vkCmdCopyImageToBuffer2(list->cmd.vk_command_buffer, &copy_info));
    }

    list->subresource_tracking_count = 0;

    /* Do not emit another barrier here, let the caller handle that as
     * necessary. If this is used at the end of a command buffer, no
     * barrier is necessary as submissions will emit a host barrier. */
}

static void d3d12_command_list_emit_render_pass_transition(struct d3d12_command_list *list,
        enum vkd3d_render_pass_transition_mode mode)
{
    const struct vkd3d_vk_device_procs *vk_procs = &list->device->vk_procs;
    VkImageMemoryBarrier2 vk_image_barriers[D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT + 2];
    VkImageSubresourceLayers vk_subresource_layers;
    struct d3d12_rtv_desc *dsv;
    VkDependencyInfo dep_info;
    uint32_t i;

    memset(&dep_info, 0, sizeof(dep_info));
    dep_info.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dep_info.imageMemoryBarrierCount = 0;
    dep_info.pImageMemoryBarriers = vk_image_barriers;

    for (i = 0; i < D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT; i++)
    {
        struct d3d12_rtv_desc *rtv = &list->rtvs[i];

        if (!rtv->view)
            continue;

        if (mode == VKD3D_RENDER_PASS_TRANSITION_MODE_BEGIN)
        {
            d3d12_command_list_track_resource_usage(list, rtv->resource, true);

            if (rtv->resource->flags & VKD3D_RESOURCE_LINEAR_STAGING_COPY)
            {
                vk_subresource_layers.aspectMask = rtv->view->info.texture.aspect_mask;
                vk_subresource_layers.mipLevel = rtv->view->info.texture.miplevel_idx;
                vk_subresource_layers.baseArrayLayer = rtv->view->info.texture.layer_idx;
                vk_subresource_layers.layerCount = rtv->view->info.texture.layer_count;

                d3d12_command_list_update_subresource_data(list, rtv->resource, vk_subresource_layers);
            }
        }

        if ((vk_render_pass_barrier_from_view(list, rtv->view, rtv->resource,
                mode, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                &vk_image_barriers[dep_info.imageMemoryBarrierCount])))
            dep_info.imageMemoryBarrierCount++;
    }

    dsv = &list->dsv;

    /* The dsv_layout is updated in d3d12_command_list_begin_render_pass(). */

    if (dsv->view && list->dsv_layout)
    {
        if ((vk_render_pass_barrier_from_view(list, dsv->view, dsv->resource,
                mode, list->dsv_layout, &vk_image_barriers[dep_info.imageMemoryBarrierCount])))
            dep_info.imageMemoryBarrierCount++;

        /* We know for sure we will write something to these attachments now, so try to promote. */
        if (mode == VKD3D_RENDER_PASS_TRANSITION_MODE_BEGIN)
        {
            d3d12_command_list_track_resource_usage(list, dsv->resource, true);
            d3d12_command_list_notify_dsv_writes(list, dsv->resource, dsv->view, list->dsv_plane_optimal_mask);
        }
    }

    /* Need to deduce DSV layouts again before we start a new render pass. */
    if (mode == VKD3D_RENDER_PASS_TRANSITION_MODE_END)
        list->dsv_layout = VK_IMAGE_LAYOUT_UNDEFINED;

    /* Ignore VRS targets. They have to be in the appropriate resource state here. */

    if (!dep_info.imageMemoryBarrierCount)
        return;

    VK_CALL(vkCmdPipelineBarrier2(list->cmd.vk_command_buffer, &dep_info));
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
    {
        ERR("Failed to add pending query.\n");
        return false;
    }

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
        VK_CALL(vkCmdBeginQueryIndexedEXT(list->cmd.vk_command_buffer, query->vk_pool, query->vk_index, flags, stream));
    }
    else
        VK_CALL(vkCmdBeginQuery(list->cmd.vk_command_buffer, query->vk_pool, query->vk_index, flags));

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
        VK_CALL(vkCmdEndQueryIndexedEXT(list->cmd.vk_command_buffer, query->vk_pool, query->vk_index, stream));
    }
    else
        VK_CALL(vkCmdEndQuery(list->cmd.vk_command_buffer, query->vk_pool, query->vk_index));

    query->state = VKD3D_ACTIVE_QUERY_ENDED;
}

static void d3d12_command_list_reset_active_query(struct d3d12_command_list *list,
        struct vkd3d_active_query *query)
{
    if (!d3d12_command_list_add_pending_query(list, query))
        return;

    if (!d3d12_command_allocator_allocate_query_from_heap_type(list->allocator,
            query->heap->desc.Type, &query->vk_pool, &query->vk_index))
        return;

    if (!d3d12_command_list_reset_query(list, query->vk_pool, query->vk_index))
        return;

    query->state = VKD3D_ACTIVE_QUERY_RESET;
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
    query->resolve_index = 0;

    if (!d3d12_command_allocator_allocate_query_from_heap_type(list->allocator,
            heap->desc.Type, &query->vk_pool, &query->vk_index))
        return false;

    return d3d12_command_list_reset_query(list, query->vk_pool, query->vk_index);
}

static bool d3d12_command_list_disable_query(struct d3d12_command_list *list,
        struct d3d12_query_heap *heap, uint32_t index)
{
    unsigned int i;

    for (i = 0; i < list->active_queries_count; i++)
    {
        struct vkd3d_active_query *query = &list->active_queries[i];

        if (query->heap == heap && query->index == index)
        {
            if (!d3d12_command_list_add_pending_query(list, query))
                return false;

            if (query->state == VKD3D_ACTIVE_QUERY_RESET)
                d3d12_command_list_begin_active_query(list, query);
            if (query->state == VKD3D_ACTIVE_QUERY_BEGUN)
                d3d12_command_list_end_active_query(list, query);

            *query = list->active_queries[--list->active_queries_count];
            return true;
        }
    }

    WARN("Query (%p, %u) not active.\n", heap, index);
    return true;
}

static void d3d12_command_list_handle_active_queries(struct d3d12_command_list *list, bool end)
{
    unsigned int i;

    for (i = 0; i < list->active_queries_count; i++)
    {
        struct vkd3d_active_query *query = &list->active_queries[i];

        if (query->state == VKD3D_ACTIVE_QUERY_ENDED && !end)
            d3d12_command_list_reset_active_query(list, query);

        if (query->state == VKD3D_ACTIVE_QUERY_RESET)
            d3d12_command_list_begin_active_query(list, query);
        if (query->state == VKD3D_ACTIVE_QUERY_BEGUN && end)
            d3d12_command_list_end_active_query(list, query);
    }
}

int vkd3d_compare_pending_query(const void* query_a, const void* query_b)
{
    const struct vkd3d_active_query *a = query_a;
    const struct vkd3d_active_query *b = query_b;

    // Sort by D3D12 heap since we need to do one compute dispatch per buffer
    if (a->heap < b->heap) return -1;
    if (a->heap > b->heap) return 1;

    // Sort by Vulkan query pool and index to batch query resolves
    if (a->vk_pool > b->vk_pool) return -1;
    if (a->vk_pool < b->vk_pool) return 1;

    return (int)(a->vk_index - b->vk_index);
}

static size_t get_query_heap_stride(D3D12_QUERY_HEAP_TYPE heap_type)
{
    if (heap_type == D3D12_QUERY_HEAP_TYPE_PIPELINE_STATISTICS)
        return sizeof(D3D12_QUERY_DATA_PIPELINE_STATISTICS);

    if (heap_type == D3D12_QUERY_HEAP_TYPE_SO_STATISTICS)
        return sizeof(D3D12_QUERY_DATA_SO_STATISTICS);

    return sizeof(uint64_t);
}

static bool d3d12_command_list_gather_pending_queries(struct d3d12_command_list *list)
{
    /* TODO allocate arrays from command allocator in case
     * games hit this path multiple times per frame */
    VkDeviceSize resolve_buffer_size, resolve_buffer_stride, ssbo_alignment, entry_buffer_size;
    const struct vkd3d_vk_device_procs *vk_procs = &list->device->vk_procs;
    struct vkd3d_scratch_allocation resolve_buffer, entry_buffer;
    struct vkd3d_query_gather_info gather_pipeline;
    const struct vkd3d_active_query *src_queries;
    unsigned int i, j, k, workgroup_count;
    uint32_t resolve_index, entry_offset;
    struct vkd3d_query_gather_args args;
    VkMemoryBarrier2 vk_barrier;
    VkDependencyInfo dep_info;
    bool result = false;

    struct dispatch_entry
    {
        struct d3d12_query_heap *heap;
        uint32_t virtual_query_count;
        uint32_t unique_query_count;
        uint32_t min_index;
        uint32_t max_index;
        VkDeviceSize resolve_buffer_offset;
        VkDeviceSize resolve_buffer_size;
    };
    
    struct dispatch_entry *dispatches = NULL;
    size_t dispatch_size = 0;
    size_t dispatch_count = 0;

    struct resolve_entry
    {
        VkQueryPool query_pool;
        uint32_t first_query;
        uint32_t query_count;
        VkDeviceSize offset;
        VkDeviceSize stride;
    };
    
    struct resolve_entry *resolves = NULL;
    size_t resolve_size = 0;
    size_t resolve_count = 0;

    struct query_entry
    {
        uint32_t dst_index;
        uint32_t src_index;
        uint32_t next;
    };

    struct query_map
    {
        struct query_entry *entry;
        unsigned int dispatch_id;
    };
    
    struct query_entry *dst_queries = NULL;
    struct query_entry *query_list = NULL;
    struct query_map *query_map = NULL;
    size_t query_map_size = 0;

    if (!list->pending_queries_count)
        return true;

    /* Sort pending query list so that we can batch commands */
    qsort(list->pending_queries, list->pending_queries_count,
            sizeof(*list->pending_queries), &vkd3d_compare_pending_query);

    ssbo_alignment = d3d12_device_get_ssbo_alignment(list->device);
    resolve_buffer_size = 0;
    resolve_buffer_stride = 0;
    resolve_index = 0;

    for (i = 0; i < list->pending_queries_count; i++)
    {
        struct dispatch_entry *d = dispatches ? &dispatches[dispatch_count - 1] : NULL;
        struct resolve_entry *r = resolves ? &resolves[resolve_count - 1] : NULL;
        struct vkd3d_active_query *q = &list->pending_queries[i];

        /* Prepare one compute dispatch per D3D12 query heap */
        if (!d || d->heap != q->heap)
        {
            if (!vkd3d_array_reserve((void **)&dispatches, &dispatch_size, dispatch_count + 1, sizeof(*dispatches)))
            {
                ERR("Failed to allocate dispatch list.\n");
                goto cleanup;
            }

            /* Force new resolve entry as well so that binding the scratch buffer
             * doesn't get overly complicated when we need to deal with potential
             * SSBO alignment issues on some hardware. */
            resolve_buffer_stride = get_query_heap_stride(q->heap->desc.Type);
            resolve_buffer_size = align(resolve_buffer_size, ssbo_alignment);
            resolve_index = 0;

            d = &dispatches[dispatch_count++];
            d->min_index = q->index;
            d->max_index = q->index;
            d->heap = q->heap;
            d->virtual_query_count = 1;
            d->unique_query_count = 0;
            d->resolve_buffer_offset = resolve_buffer_size;

            r = NULL;
        }
        else
        {
            d->virtual_query_count++;
            d->min_index = min(d->min_index, q->index);
            d->max_index = max(d->max_index, q->index);
        }

        /* Prepare one resolve entry per Vulkan query range */
        if (!r || r->query_pool != q->vk_pool || r->first_query + r->query_count != q->vk_index)
        {
            if (!vkd3d_array_reserve((void **)&resolves, &resolve_size, resolve_count + 1, sizeof(*resolves)))
            {
                ERR("Failed to allocate resolve list.\n");
                goto cleanup;
            }

            r = &resolves[resolve_count++];
            r->query_pool = q->vk_pool;
            r->first_query = q->vk_index;
            r->query_count = 1;
            r->offset = resolve_buffer_size;
            r->stride = get_query_heap_stride(q->heap->desc.Type);
        }
        else
            r->query_count++;

        resolve_buffer_size += resolve_buffer_stride;

        d->resolve_buffer_size = resolve_buffer_size - d->resolve_buffer_offset;
        q->resolve_index = resolve_index++;
    }

    /* Allocate scratch buffer and resolve virtual Vulkan queries into it */
    if (!d3d12_command_allocator_allocate_scratch_memory(list->allocator,
            VKD3D_SCRATCH_POOL_KIND_DEVICE_STORAGE,
            resolve_buffer_size, max(ssbo_alignment, sizeof(uint64_t)), ~0u, &resolve_buffer))
        goto cleanup;

    for (i = 0; i < resolve_count; i++)
    {
        const struct resolve_entry *r = &resolves[i];

        VK_CALL(vkCmdCopyQueryPoolResults(list->cmd.vk_command_buffer,
            r->query_pool, r->first_query, r->query_count,
            resolve_buffer.buffer, resolve_buffer.offset + r->offset,
            r->stride, VK_QUERY_RESULT_WAIT_BIT | VK_QUERY_RESULT_64_BIT));
    }

    /* Allocate scratch buffer for query lists */
    entry_buffer_size = sizeof(struct query_entry) * list->pending_queries_count;

    if (!d3d12_command_allocator_allocate_scratch_memory(list->allocator,
            VKD3D_SCRATCH_POOL_KIND_DEVICE_STORAGE,
            entry_buffer_size, ssbo_alignment, ~0u, &entry_buffer))
        goto cleanup;

    for (i = 0; i < dispatch_count; i++)
    {
        const struct dispatch_entry *d = &dispatches[i];
        query_map_size = max(query_map_size, d->max_index - d->min_index + 1);
    }

    if (!(query_map = vkd3d_calloc(query_map_size, sizeof(*query_map))) ||
            !(query_list = vkd3d_malloc(sizeof(*query_list) * list->pending_queries_count)))
    {
        ERR("Failed to allocate query map.\n");
        goto cleanup;
    }

    /* Active list for the current dispatch */
    src_queries = list->pending_queries;
    dst_queries = query_list;

    for (i = 0; i < dispatch_count; i++)
    {
        struct dispatch_entry *d = &dispatches[i];
        unsigned int dispatch_id = i + 1;

        /* First pass that counts unique queries since the compute
         * shader expects list heads to be packed first in the array */
        for (j = 0; j < d->virtual_query_count; j++)
        {
            const struct vkd3d_active_query *q = &src_queries[j];
            struct query_map *e = &query_map[q->index - d->min_index];

            if (e->dispatch_id != dispatch_id)
            {
                e->entry = &dst_queries[d->unique_query_count++];
                e->entry->dst_index = q->index;
                e->entry->src_index = q->resolve_index;
                e->entry->next = ~0u;
                e->dispatch_id = dispatch_id;
            }
        }

        /* Second pass that actually generates the query list. */
        for (j = 0, k = d->unique_query_count; j < d->virtual_query_count; j++)
        {
            const struct vkd3d_active_query *q = &src_queries[j];
            struct query_map *e = &query_map[q->index - d->min_index];

            /* Skip entries that we already added in the first pass */
            if (e->entry->src_index == q->resolve_index)
                continue;

            e->entry->next = k;
            e->entry = &dst_queries[k++];
            e->entry->dst_index = q->index;
            e->entry->src_index = q->resolve_index;
            e->entry->next = ~0u;
        }

        src_queries += d->virtual_query_count;
        dst_queries += d->virtual_query_count;
    }

    /* Upload query lists in chunks since vkCmdUpdateBuffer is limited to
     * 64kiB per invocation. Normally, one single iteration should suffice. */
    for (i = 0; i < list->pending_queries_count; i += 2048)
    {
        unsigned int count = min(2048, list->pending_queries_count - i);

        VK_CALL(vkCmdUpdateBuffer(list->cmd.vk_command_buffer, entry_buffer.buffer,
                sizeof(struct query_entry) * i + entry_buffer.offset,
                sizeof(struct query_entry) * count, &query_list[i]));
    }

    memset(&vk_barrier, 0, sizeof(vk_barrier));
    vk_barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
    vk_barrier.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
    vk_barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    vk_barrier.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    vk_barrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;

    memset(&dep_info, 0, sizeof(dep_info));
    dep_info.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dep_info.memoryBarrierCount = 1;
    dep_info.pMemoryBarriers = &vk_barrier;

    VK_CALL(vkCmdPipelineBarrier2(list->cmd.vk_command_buffer, &dep_info));

    /* Gather virtual query results and store
     * them in the query heap's buffer */
    entry_offset = 0;

    for (i = 0; i < dispatch_count; i++)
    {
        const struct dispatch_entry *d = &dispatches[i];

        if (!(vkd3d_meta_get_query_gather_pipeline(&list->device->meta_ops,
                d->heap->desc.Type, &gather_pipeline)))
            goto cleanup;

        VK_CALL(vkCmdBindPipeline(list->cmd.vk_command_buffer,
                VK_PIPELINE_BIND_POINT_COMPUTE, gather_pipeline.vk_pipeline));

        args.dst_va = d->heap->va;
        args.src_va = resolve_buffer.va + d->resolve_buffer_offset;
        args.map_va = entry_buffer.va + entry_offset * sizeof(struct query_entry);
        args.query_count = d->unique_query_count;

        entry_offset += d->virtual_query_count;

        VK_CALL(vkCmdPushConstants(list->cmd.vk_command_buffer,
                gather_pipeline.vk_pipeline_layout,
                VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(args), &args));

        workgroup_count = vkd3d_compute_workgroup_count(d->unique_query_count, VKD3D_QUERY_OP_WORKGROUP_SIZE);
        VK_CALL(vkCmdDispatch(list->cmd.vk_command_buffer, workgroup_count, 1, 1));
    }

    vk_barrier.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    vk_barrier.srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
    vk_barrier.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_COPY_BIT;
    vk_barrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_TRANSFER_READ_BIT;

    VK_CALL(vkCmdPipelineBarrier2(list->cmd.vk_command_buffer, &dep_info));

    list->pending_queries_count = 0;
    result = true;

    d3d12_command_list_invalidate_current_pipeline(list, true);
    d3d12_command_list_invalidate_root_parameters(list, &list->compute_bindings, true, &list->graphics_bindings);

    VKD3D_BREADCRUMB_COMMAND(GATHER_VIRTUAL_QUERY);

cleanup:
    vkd3d_free(resolves);
    vkd3d_free(dispatches);
    vkd3d_free(query_list);
    vkd3d_free(query_map);
    return result;
}

void d3d12_command_list_end_current_render_pass(struct d3d12_command_list *list, bool suspend)
{
    const struct vkd3d_vk_device_procs *vk_procs = &list->device->vk_procs;
    VkMemoryBarrier2 vk_barrier;
    VkDependencyInfo dep_info;

    d3d12_command_list_handle_active_queries(list, true);

    if (list->xfb_buffer_count)
    {
        VK_CALL(vkCmdEndTransformFeedbackEXT(list->cmd.vk_command_buffer, 0, list->xfb_buffer_count,
                list->so_counter_buffers, list->so_counter_buffer_offsets));
    }

    if (list->rendering_info.state_flags & VKD3D_RENDERING_ACTIVE)
    {
        VK_CALL(vkCmdEndRendering(list->cmd.vk_command_buffer));
        d3d12_command_list_sync_tiler_renderpass_writes(list, &list->rendering_info.info);
        d3d12_command_list_debug_mark_end_region(list);
    }

    /* Don't emit barriers for temporary suspension of the render pass */
    if (!suspend && (list->rendering_info.state_flags & (VKD3D_RENDERING_ACTIVE | VKD3D_RENDERING_SUSPENDED)))
        d3d12_command_list_emit_render_pass_transition(list, VKD3D_RENDER_PASS_TRANSITION_MODE_END);

    if (suspend && (list->rendering_info.state_flags & (VKD3D_RENDERING_ACTIVE)))
        list->rendering_info.state_flags |= VKD3D_RENDERING_SUSPENDED;
    else if (!suspend)
        list->rendering_info.state_flags &= ~VKD3D_RENDERING_SUSPENDED;

    list->rendering_info.state_flags &= ~VKD3D_RENDERING_ACTIVE;

    if (list->xfb_buffer_count)
    {
        /* We need a barrier between pause and resume. */
        memset(&vk_barrier, 0, sizeof(vk_barrier));
        vk_barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
        vk_barrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFORM_FEEDBACK_BIT_EXT;
        vk_barrier.srcAccessMask = VK_ACCESS_2_TRANSFORM_FEEDBACK_COUNTER_WRITE_BIT_EXT;
        vk_barrier.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFORM_FEEDBACK_BIT_EXT;
        vk_barrier.dstAccessMask = VK_ACCESS_2_TRANSFORM_FEEDBACK_COUNTER_READ_BIT_EXT;

        memset(&dep_info, 0, sizeof(dep_info));
        dep_info.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        dep_info.memoryBarrierCount = 1;
        dep_info.pMemoryBarriers = &vk_barrier;

        VK_CALL(vkCmdPipelineBarrier2(list->cmd.vk_command_buffer, &dep_info));

        list->xfb_buffer_count = 0u;
    }

    d3d12_command_list_flush_query_resolves(list);
    d3d12_command_list_end_wbi_batch(list);
}

static void d3d12_command_list_invalidate_push_constants(struct vkd3d_pipeline_bindings *bindings)
{
    if (bindings->root_signature->descriptor_table_count)
        bindings->dirty_flags |= VKD3D_PIPELINE_DIRTY_DESCRIPTOR_TABLE_OFFSETS;

    bindings->root_descriptor_dirty_mask =
            bindings->root_signature->root_descriptor_raw_va_mask |
            bindings->root_signature->root_descriptor_push_mask;

    if (vkd3d_descriptor_debug_active_instruction_qa_checks())
    {
        /* Even if root signature itself doesn't have root descriptors, force us to go through that path. */
        bindings->root_descriptor_dirty_mask = UINT64_MAX;
    }

    bindings->root_constant_dirty_mask = bindings->root_signature->root_constant_mask;
}

void d3d12_command_list_invalidate_root_parameters(struct d3d12_command_list *list,
        struct vkd3d_pipeline_bindings *bindings, bool invalidate_descriptor_heaps,
        struct vkd3d_pipeline_bindings *sibling_push_domain)
{
    /* For scenarios where we're emitting push constants to one bind point in meta shaders,
     * this will invalidate push constants for the other bind points as well. */
    if (sibling_push_domain && sibling_push_domain->root_signature)
        d3d12_command_list_invalidate_push_constants(sibling_push_domain);

    if (!bindings->root_signature)
        return;

    /* Previously dirty states may no longer be dirty
     * if the new root signature does not use them */
    bindings->dirty_flags = 0;

    if (bindings->root_signature->vk_sampler_descriptor_layout)
        bindings->dirty_flags |= VKD3D_PIPELINE_DIRTY_STATIC_SAMPLER_SET;
    if (bindings->root_signature->hoist_info.num_desc)
        bindings->dirty_flags |= VKD3D_PIPELINE_DIRTY_HOISTED_DESCRIPTORS;

    d3d12_command_list_invalidate_push_constants(bindings);

    if (invalidate_descriptor_heaps)
    {
        struct d3d12_device *device = bindings->root_signature->device;
        bindings->descriptor_heap_dirty_mask = (1ull << device->bindless_state.set_count) - 1;
    }
}

static void vk_access_and_stage_flags_from_d3d12_resource_state(const struct d3d12_command_list *list,
        const struct d3d12_resource *resource, uint32_t state_mask, VkQueueFlags vk_queue_flags,
        VkPipelineStageFlags2 *stages, VkAccessFlags2 *access)
{
    struct d3d12_device *device = list->device;
    VkPipelineStageFlags2 queue_shader_stages;
    uint32_t unhandled_state = 0;

    queue_shader_stages = vk_queue_shader_stages(list->device, vk_queue_flags);

    if (state_mask == D3D12_RESOURCE_STATE_COMMON)
    {
        *stages |= VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        *access |= VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
    }

    while (state_mask)
    {
        uint32_t state = state_mask & -state_mask;

        switch (state)
        {
            case D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER:
                *stages |= queue_shader_stages;
                *access |= VK_ACCESS_2_UNIFORM_READ_BIT;

                if (device->bindless_state.flags & (VKD3D_BINDLESS_CBV_AS_SSBO | VKD3D_RAW_VA_ROOT_DESCRIPTOR_CBV))
                    *access |= VK_ACCESS_2_SHADER_READ_BIT;

                if (vk_queue_flags & VK_QUEUE_GRAPHICS_BIT)
                {
                    *stages |= VK_PIPELINE_STAGE_2_VERTEX_ATTRIBUTE_INPUT_BIT;
                    *access |= VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT;
                }
                break;

            case D3D12_RESOURCE_STATE_INDEX_BUFFER:
                *stages |= VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT;
                *access |= VK_ACCESS_2_INDEX_READ_BIT;
                break;

            case D3D12_RESOURCE_STATE_RENDER_TARGET:
                *stages |= VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
                *access |= VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT;
                break;

            case D3D12_RESOURCE_STATE_UNORDERED_ACCESS:
                *stages |= queue_shader_stages;
                *access |= VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT;
                if ((vk_queue_flags & VK_QUEUE_COMPUTE_BIT) &&
                        d3d12_device_supports_ray_tracing_tier_1_0(device))
                {
                    /* UNORDERED_ACCESS state is also used for scratch buffers.
                     * Acceleration structures cannot transition their state,
                     * and must use UAV barriers. This is still relevant for scratch buffers however. */
                    *stages |= VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR;
                    *access |= VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR |
                            VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
                }
                break;

            case D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE:
                if ((vk_queue_flags & VK_QUEUE_COMPUTE_BIT) &&
                        d3d12_device_supports_ray_tracing_tier_1_0(device))
                {
                    *stages |= VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR |
                            VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR;
                    *access |= VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR |
                            VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR;
                }
                break;

            case D3D12_RESOURCE_STATE_DEPTH_WRITE:
                /* If our DS layout is attachment optimal in any way, we might not perform implicit
                 * memory barriers as part of a render pass. */
                if (d3d12_command_list_get_depth_stencil_resource_layout(list, resource, NULL) !=
                        VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL)
                {
                    *access |= VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
                }
                /* fallthrough */
            case D3D12_RESOURCE_STATE_DEPTH_READ:
                *stages |= VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
                *access |= VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
                break;

            case D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE:
                *stages |= queue_shader_stages & ~VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
                *access |= VK_ACCESS_2_SHADER_READ_BIT;
                if ((vk_queue_flags & VK_QUEUE_COMPUTE_BIT) &&
                        d3d12_device_supports_ray_tracing_tier_1_0(device))
                {
                    /* Vertex / index / transform buffer inputs are NON_PIXEL_SHADER_RESOURCES in DXR.
                     * They access SHADER_READ_BIT in Vulkan, so just need to add the stage. */
                    *stages |= VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR;
                }
                break;

            case D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE:
                *stages |= VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
                *access |= VK_ACCESS_2_SHADER_READ_BIT;
                break;

            case D3D12_RESOURCE_STATE_STREAM_OUT:
                *stages |= VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_2_TRANSFORM_FEEDBACK_BIT_EXT;
                *access |= VK_ACCESS_2_TRANSFORM_FEEDBACK_COUNTER_READ_BIT_EXT | VK_ACCESS_2_TRANSFORM_FEEDBACK_COUNTER_WRITE_BIT_EXT |
                        VK_ACCESS_2_TRANSFORM_FEEDBACK_WRITE_BIT_EXT;
                break;

            case D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT:
                *stages |= VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT;
                *access |= VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT;

                /* We might use preprocessing. */
                if (list->device->device_info.device_generated_commands_features_nv.deviceGeneratedCommands ||
                    list->device->device_info.device_generated_commands_features_ext.deviceGeneratedCommands)
                {
                    *stages |= VK_PIPELINE_STAGE_2_COMMAND_PREPROCESS_BIT_EXT;
                    *access |= VK_ACCESS_2_COMMAND_PREPROCESS_READ_BIT_EXT;
                }

                /* D3D12_RESOURCE_STATE_PREDICATION is an alias.
                 * Add SHADER_READ_BIT here since we might read the indirect buffer in compute for
                 * patching reasons. */
                *stages |= VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
                *access |= VK_ACCESS_2_SHADER_READ_BIT;
                break;

            case D3D12_RESOURCE_STATE_COPY_DEST:
                *stages |= VK_PIPELINE_STAGE_2_COPY_BIT;
                if (d3d12_resource_is_buffer(resource))
                    *access |= VK_ACCESS_2_TRANSFER_WRITE_BIT;
                break;

            case D3D12_RESOURCE_STATE_COPY_SOURCE:
                *stages |= VK_PIPELINE_STAGE_2_COPY_BIT;
                if (d3d12_resource_is_buffer(resource))
                    *access |= VK_ACCESS_2_TRANSFER_READ_BIT;
                break;

            case D3D12_RESOURCE_STATE_RESOLVE_DEST:
                if (d3d12_resource_desc_is_sampler_feedback(&resource->desc))
                {
                    /* ENCODE is always done with compute. */
                    *stages |= VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
                }
                else
                {
                    /* Needs COPY stage for D3D12_RESOLVE_MODE_DECOMPRESS */
                    *stages |= VK_PIPELINE_STAGE_2_RESOLVE_BIT | VK_PIPELINE_STAGE_2_COPY_BIT;
                }
                break;

            case D3D12_RESOURCE_STATE_RESOLVE_SOURCE:
                if (d3d12_resource_desc_is_sampler_feedback(&resource->desc))
                {
                    /* We can decode in either fragment or compute paths. */
                    *stages |= VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
                }
                else
                {
                    /* Needs COPY stage for D3D12_RESOLVE_MODE_DECOMPRESS */
                    *stages |= VK_PIPELINE_STAGE_2_RESOLVE_BIT | VK_PIPELINE_STAGE_2_COPY_BIT;
                }
                break;

            case D3D12_RESOURCE_STATE_SHADING_RATE_SOURCE:
                *stages |= VK_PIPELINE_STAGE_2_FRAGMENT_SHADING_RATE_ATTACHMENT_BIT_KHR;
                *access |= VK_ACCESS_2_FRAGMENT_SHADING_RATE_ATTACHMENT_READ_BIT_KHR;
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

extern ULONG STDMETHODCALLTYPE d3d12_command_list_vkd3d_ext_AddRef(d3d12_command_list_vkd3d_ext_iface *iface);

HRESULT STDMETHODCALLTYPE d3d12_command_list_QueryInterface(d3d12_command_list_iface *iface,
        REFIID iid, void **object)
{
    struct d3d12_command_list *command_list = impl_from_ID3D12GraphicsCommandList(iface);

    TRACE("iface %p, iid %s, object %p.\n", iface, debugstr_guid(iid), object);

    if (!object)
        return E_POINTER;

    if (IsEqualGUID(iid, &IID_ID3D12GraphicsCommandList)
            || IsEqualGUID(iid, &IID_ID3D12GraphicsCommandList1)
            || IsEqualGUID(iid, &IID_ID3D12GraphicsCommandList2)
            || IsEqualGUID(iid, &IID_ID3D12GraphicsCommandList3)
            || IsEqualGUID(iid, &IID_ID3D12GraphicsCommandList4)
            || IsEqualGUID(iid, &IID_ID3D12GraphicsCommandList5)
            || IsEqualGUID(iid, &IID_ID3D12GraphicsCommandList6)
            || IsEqualGUID(iid, &IID_ID3D12GraphicsCommandList7)
            || IsEqualGUID(iid, &IID_ID3D12GraphicsCommandList8)
            || IsEqualGUID(iid, &IID_ID3D12GraphicsCommandList9)
            || IsEqualGUID(iid, &IID_ID3D12GraphicsCommandList10)
            || IsEqualGUID(iid, &IID_ID3D12CommandList)
            || IsEqualGUID(iid, &IID_ID3D12DeviceChild)
            || IsEqualGUID(iid, &IID_ID3D12Object)
            || IsEqualGUID(iid, &IID_IUnknown))
    {
        ID3D12GraphicsCommandList10_AddRef(iface);
        *object = iface;
        return S_OK;
    }

    if (IsEqualGUID(iid, &IID_ID3D12GraphicsCommandListExt)
            || IsEqualGUID(iid, &IID_ID3D12GraphicsCommandListExt1))
    {
        d3d12_command_list_vkd3d_ext_AddRef(&command_list->ID3D12GraphicsCommandListExt_iface);
        *object = &command_list->ID3D12GraphicsCommandListExt_iface;
        return S_OK;
    }

    if (IsEqualGUID(iid, &IID_ID3DDestructionNotifier))
    {
        ID3DDestructionNotifier_AddRef(&command_list->destruction_notifier.ID3DDestructionNotifier_iface);
        *object = &command_list->destruction_notifier.ID3DDestructionNotifier_iface;
        return S_OK;
    }

    WARN("%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid(iid));

    *object = NULL;
    return E_NOINTERFACE;
}

ULONG STDMETHODCALLTYPE d3d12_command_list_AddRef(d3d12_command_list_iface *iface)
{
    struct d3d12_command_list *list = impl_from_ID3D12GraphicsCommandList(iface);
    ULONG refcount = InterlockedIncrement(&list->refcount);

    TRACE("%p increasing refcount to %u.\n", list, refcount);

    return refcount;
}

ULONG STDMETHODCALLTYPE d3d12_command_list_Release(d3d12_command_list_iface *iface)
{
    struct d3d12_command_list *list = impl_from_ID3D12GraphicsCommandList(iface);
    ULONG refcount = InterlockedDecrement(&list->refcount);

    TRACE("%p decreasing refcount to %u.\n", list, refcount);

    if (!refcount)
    {
        struct d3d12_device *device = list->device;

        d3d_destruction_notifier_free(&list->destruction_notifier);
        vkd3d_private_store_destroy(&list->private_store);

        /* When command pool is destroyed, all command buffers are implicitly freed. */
        if (list->allocator)
            d3d12_command_allocator_free_command_buffer(list->allocator, list);

        vkd3d_free(list->rtv_resolves);
        vkd3d_free(list->rtv_resolve_regions);
        vkd3d_free(list->init_transitions);
        vkd3d_free(list->query_ranges);
        vkd3d_free(list->active_queries);
        vkd3d_free(list->pending_queries);
        vkd3d_free(list->dsv_resource_tracking);
        vkd3d_free(list->subresource_tracking);
        vkd3d_free(list->query_resolves);
        hash_map_free(&list->query_resolve_lut);
        d3d12_command_list_free_rtas_batch(list);

        vkd3d_free_aligned(list);

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

    return vkd3d_set_private_data(&list->private_store, guid, data_size, data,
            NULL, NULL);
}

static HRESULT STDMETHODCALLTYPE d3d12_command_list_SetPrivateDataInterface(d3d12_command_list_iface *iface,
        REFGUID guid, const IUnknown *data)
{
    struct d3d12_command_list *list = impl_from_ID3D12GraphicsCommandList(iface);

    TRACE("iface %p, guid %s, data %p.\n", iface, debugstr_guid(guid), data);

    return vkd3d_set_private_data_interface(&list->private_store, guid, data,
            NULL, NULL);
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

        VK_CALL(vkCmdResetQueryPool(list->cmd.vk_init_commands,
                range->vk_pool, range->index, range->count));
    }

    return S_OK;
}

static HRESULT d3d12_command_list_build_init_commands(struct d3d12_command_list *list)
{
    const struct vkd3d_vk_device_procs *vk_procs = &list->device->vk_procs;
    const struct d3d12_command_list_iteration *iteration;
    VkDependencyInfo dep_info;
    VkMemoryBarrier2 barrier;
    unsigned int i;
    VkResult vr;
    HRESULT hr;

    if (FAILED(hr = d3d12_command_list_batch_reset_query_pools(list)))
        return hr;

    for (i = 0; i < list->cmd.iteration_count; i++)
    {
        iteration = &list->cmd.iterations[i];
        if (!iteration->vk_init_commands)
            continue;

        if (iteration->indirect_meta.need_compute_to_indirect_barrier)
        {
            /* We've patched an indirect command stream here, so do the final barrier now. */
            memset(&barrier, 0, sizeof(barrier));
            barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
            barrier.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
            barrier.srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
            barrier.dstStageMask = VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT;
            barrier.dstAccessMask = VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT;

            if (iteration->indirect_meta.need_compute_to_cbv_barrier)
            {
                barrier.dstAccessMask |= VK_ACCESS_2_UNIFORM_READ_BIT;
                barrier.dstStageMask |= VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
            }

            memset(&dep_info, 0, sizeof(dep_info));
            dep_info.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
            dep_info.memoryBarrierCount = 1;
            dep_info.pMemoryBarriers = &barrier;

            VK_CALL(vkCmdPipelineBarrier2(iteration->vk_init_commands, &dep_info));
        }

        if (iteration->indirect_meta.need_preprocess_barrier)
        {
            memset(&barrier, 0, sizeof(barrier));
            barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
            barrier.srcStageMask = VK_PIPELINE_STAGE_2_COMMAND_PREPROCESS_BIT_NV;
            barrier.srcAccessMask = VK_ACCESS_2_COMMAND_PREPROCESS_WRITE_BIT_NV;
            /* vkCmdExecuteGeneratedCommands consumes in draw indirect stage. */
            barrier.dstStageMask = VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT;
            barrier.dstAccessMask = VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT;

            memset(&dep_info, 0, sizeof(dep_info));
            dep_info.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
            dep_info.memoryBarrierCount = 1;
            dep_info.pMemoryBarriers = &barrier;

            VK_CALL(vkCmdPipelineBarrier2(iteration->vk_init_commands, &dep_info));
        }

        if ((vr = VK_CALL(vkEndCommandBuffer(iteration->vk_init_commands))) < 0)
        {
            WARN("Failed to end command buffer, vr %d.\n", vr);
            return hresult_from_vk_result(vr);
        }
    }

    return S_OK;
}

void d3d12_command_list_decay_tracked_state(struct d3d12_command_list *list)
{
    /* TODO: Revisit this w.r.t. splitting VkCommandBuffer */
    d3d12_command_list_end_current_render_pass(list, false);
    d3d12_command_list_end_transfer_batch(list);
    d3d12_command_list_flush_rtas_batch(list);

    /* If we have kept some DSV resources in optimal layout throughout the command buffer,
     * now is the time to decay them. */
    d3d12_command_list_decay_optimal_dsv_resources(list);

    /* If we have some pending copy barriers, need to resolve those now, since we cannot track across command lists. */
    d3d12_command_list_resolve_buffer_copy_writes(list);

    /* If there are pending subresource updates, execute them now that all other operations have completed */
    d3d12_command_list_flush_subresource_updates(list);
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

    d3d12_command_list_debug_mark_end_region(list); /* CommandList region */

    /* Ensure that any non-temporal writes from CopyDescriptors are ordered properly. */
    if (d3d12_device_use_embedded_mutable_descriptors(list->device))
        vkd3d_memcpy_non_temporal_barrier();

    d3d12_command_list_decay_tracked_state(list);

    if (list->predication.enabled_on_command_buffer)
        VK_CALL(vkCmdEndConditionalRenderingEXT(list->cmd.vk_command_buffer));

    if (!d3d12_command_list_gather_pending_queries(list))
        d3d12_command_list_mark_as_invalid(list, "Failed to gather virtual queries.\n");

    if (list->is_inside_render_pass)
        d3d12_command_list_mark_as_invalid(list, "Close called with an active render pass.\n");

#ifdef VKD3D_ENABLE_BREADCRUMBS
    if (vkd3d_config_flags & VKD3D_CONFIG_FLAG_BREADCRUMBS)
        vkd3d_breadcrumb_tracer_end_command_list(list);
#endif

    if (FAILED(hr = d3d12_command_list_build_init_commands(list)))
        return hr;

    if ((vr = VK_CALL(vkEndCommandBuffer(list->cmd.vk_command_buffer))) < 0)
    {
        WARN("Failed to end command buffer, vr %d.\n", vr);
        return hresult_from_vk_result(vr);
    }

    assert(list->cmd.iteration_count);
    list->cmd.iterations[list->cmd.iteration_count - 1].estimated_cost = list->cmd.estimated_cost;

    if (list->allocator)
    {
        d3d12_command_allocator_free_command_buffer(list->allocator, list);
        list->submit_allocator = list->allocator;
        list->allocator = NULL;
    }

    list->is_recording = false;
    vkd3d_queue_timeline_trace_close_command_list(&list->device->queue_timeline_trace, list->timeline_cookie);

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
        merge_lo = range->vk_pool == vk_pool
                && range->flags == flags
                && range->index + range->count == index;
    }

    if (pos < list->query_ranges_count)
    {
        range = &list->query_ranges[pos];
        merge_hi = range->vk_pool == vk_pool
                && range->flags == flags
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
        range = list->query_ranges + pos;

        if (pos < list->query_ranges_count && range->vk_pool == vk_pool)
        {
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

bool d3d12_command_list_reset_query(struct d3d12_command_list *list,
        VkQueryPool vk_pool, uint32_t index)
{
    size_t pos;

    if (d3d12_command_list_find_query(list, vk_pool, index, &pos))
        return false;

    d3d12_command_list_insert_query_range(list, &pos,
            vk_pool, index, 1, VKD3D_QUERY_RANGE_RESET);
    return true;
}

static void d3d12_command_list_init_default_descriptor_buffers(struct d3d12_command_list *list)
{
    if (d3d12_device_uses_descriptor_buffers(list->device))
    {
        list->descriptor_heap.buffers.heap_va_resource = list->device->global_descriptor_buffer.resource.va;
        list->descriptor_heap.buffers.heap_va_sampler = list->device->global_descriptor_buffer.sampler.va;
        list->descriptor_heap.buffers.vk_buffer_resource = list->device->global_descriptor_buffer.resource.vk_buffer;
        list->descriptor_heap.buffers.heap_dirty = true;
    }
}

static void d3d12_command_list_reset_rtv_resolves(struct d3d12_command_list *list)
{
    list->rtv_resolve_count = 0;
    list->rtv_resolve_region_count = 0;
}

static void d3d12_command_list_reset_api_state(struct d3d12_command_list *list,
        ID3D12PipelineState *initial_pipeline_state)
{
    const VkPhysicalDeviceLimits *limits = &list->device->device_info.properties2.properties.limits;
    d3d12_command_list_iface *iface = &list->ID3D12GraphicsCommandList_iface;

    list->index_buffer.dxgi_format = DXGI_FORMAT_UNKNOWN;

    list->is_inside_render_pass = false;
    list->render_pass_flags = 0;
    memset(list->rtvs, 0, sizeof(list->rtvs));
    memset(&list->dsv, 0, sizeof(list->dsv));
    d3d12_command_list_reset_rtv_resolves(list);
    list->dsv_layout = VK_IMAGE_LAYOUT_UNDEFINED;
    list->dsv_plane_optimal_mask = 0;

    /* Initial state is considered unbound render targets.
     * Also need to mark rendering_info as dirty. */
    list->rendering_info.state_flags = 0;
    list->fb_width = limits->maxFramebufferWidth;
    list->fb_height = limits->maxFramebufferHeight;
    list->fb_layer_count = limits->maxFramebufferLayers;

    list->xfb_buffer_count = 0u;

    memset(&list->predication, 0, sizeof(list->predication));

    list->index_buffer.buffer = VK_NULL_HANDLE;
    list->index_buffer.is_dirty = true;

    list->current_pipeline = VK_NULL_HANDLE;
    list->command_buffer_pipeline = VK_NULL_HANDLE;

    memset(&list->dynamic_state, 0, sizeof(list->dynamic_state));
    list->dynamic_state.blend_constants[0] = D3D12_DEFAULT_BLEND_FACTOR_RED;
    list->dynamic_state.blend_constants[1] = D3D12_DEFAULT_BLEND_FACTOR_GREEN;
    list->dynamic_state.blend_constants[2] = D3D12_DEFAULT_BLEND_FACTOR_BLUE;
    list->dynamic_state.blend_constants[3] = D3D12_DEFAULT_BLEND_FACTOR_ALPHA;

    list->dynamic_state.min_depth_bounds = 0.0f;
    list->dynamic_state.max_depth_bounds = 1.0f;

    list->dynamic_state.primitive_topology = D3D_PRIMITIVE_TOPOLOGY_POINTLIST;
    list->dynamic_state.vk_primitive_topology = VK_PRIMITIVE_TOPOLOGY_POINT_LIST;

    list->dynamic_state.fragment_shading_rate.fragment_size = (VkExtent2D) { 1u, 1u };
    list->dynamic_state.fragment_shading_rate.combiner_ops[0] = VK_FRAGMENT_SHADING_RATE_COMBINER_OP_KEEP_KHR;
    list->dynamic_state.fragment_shading_rate.combiner_ops[1] = VK_FRAGMENT_SHADING_RATE_COMBINER_OP_KEEP_KHR;

    memset(&list->graphics_bindings, 0, sizeof(list->graphics_bindings));
    memset(&list->compute_bindings, 0, sizeof(list->compute_bindings));
    memset(&list->descriptor_heap, 0, sizeof(list->descriptor_heap));

    if (vkd3d_descriptor_debug_active_instruction_qa_checks())
    {
        /* Even if root signature does not have root descriptors, force us to go through that path. */
        list->graphics_bindings.root_descriptor_dirty_mask = UINT64_MAX;
        list->compute_bindings.root_descriptor_dirty_mask = UINT64_MAX;
    }

    d3d12_command_list_init_default_descriptor_buffers(list);

    list->state = NULL;
    list->rt_state = NULL;
    list->active_pipeline_type = VKD3D_PIPELINE_TYPE_NONE;

    memset(list->so_buffers, 0, sizeof(list->so_buffers));
    memset(list->so_buffer_offsets, 0, sizeof(list->so_buffer_offsets));
    memset(list->so_buffer_sizes, 0, sizeof(list->so_buffer_sizes));
    memset(list->so_counter_buffers, 0, sizeof(list->so_counter_buffers));
    memset(list->so_counter_buffer_offsets, 0, sizeof(list->so_counter_buffer_offsets));

    list->cbv_srv_uav_descriptors_view = NULL;
    list->vrs_image = NULL;

    ID3D12GraphicsCommandList10_SetPipelineState(iface, initial_pipeline_state);
}

static void d3d12_command_list_reset_internal_state(struct d3d12_command_list *list)
{
#ifdef VKD3D_ENABLE_RENDERDOC
    list->debug_capture = vkd3d_renderdoc_active() && vkd3d_renderdoc_should_capture_shader_hash(0);
#else
    list->debug_capture = false;
#endif
    list->has_replaced_shaders = false;

    list->init_transitions_count = 0;
    list->query_ranges_count = 0;
    list->active_queries_count = 0;
    list->pending_queries_count = 0;
    list->dsv_resource_tracking_count = 0;
    list->subresource_tracking_count = 0;
    list->tracked_copy_buffer_count = 0;
    list->wbi_batch.batch_len = 0;
    list->query_resolve_count = 0;
    list->submit_allocator = NULL;

    d3d12_command_list_clear_rtas_batch(list);
}

static void d3d12_command_list_reset_state(struct d3d12_command_list *list,
        ID3D12PipelineState *initial_pipeline_state)
{
    d3d12_command_list_reset_api_state(list, initial_pipeline_state);
    d3d12_command_list_reset_internal_state(list);
}

void d3d12_command_list_invalidate_all_state(struct d3d12_command_list *list)
{
    d3d12_command_list_invalidate_current_pipeline(list, true);
    d3d12_command_list_invalidate_root_parameters(list, &list->graphics_bindings, true, NULL);
    d3d12_command_list_invalidate_root_parameters(list, &list->compute_bindings, true, NULL);
    list->index_buffer.is_dirty = true;
}

static HRESULT STDMETHODCALLTYPE d3d12_command_list_Reset(d3d12_command_list_iface *iface,
        ID3D12CommandAllocator *allocator, ID3D12PipelineState *initial_pipeline_state)
{
    struct d3d12_command_allocator *allocator_impl = d3d12_command_allocator_from_iface(allocator);
    struct d3d12_command_list *list = impl_from_ID3D12GraphicsCommandList(iface);
    HRESULT hr;

    TRACE("iface %p, allocator %p, initial_pipeline_state %p.\n",
            iface, allocator, initial_pipeline_state);

    if (!allocator_impl || allocator_impl->type != list->type)
    {
        WARN("Invalid command allocator.\n");
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

static void STDMETHODCALLTYPE d3d12_command_list_ClearState(d3d12_command_list_iface *iface,
        ID3D12PipelineState *pipeline_state)
{
    struct d3d12_command_list *list = impl_from_ID3D12GraphicsCommandList(iface);

    TRACE("iface %p, pipline_state %p!\n", iface, pipeline_state);

    if (list->is_inside_render_pass)
        d3d12_command_list_mark_as_invalid(list, "ClearState called within a render pass.\n");

    d3d12_command_list_end_current_render_pass(list, false);
    d3d12_command_list_reset_api_state(list, pipeline_state);
}

static bool d3d12_command_list_has_depth_stencil_view(struct d3d12_command_list *list)
{
    const struct d3d12_graphics_pipeline_state *graphics;

    assert(d3d12_pipeline_state_is_graphics(list->state));
    graphics = &list->state->graphics;

    return list->dsv.format && (graphics->dsv_format ||
            d3d12_graphics_pipeline_state_has_unknown_dsv_format_with_test(graphics));
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
        {
            /* Layered rendering works with no attachments. */
            *layer_count = device->vk_info.device_limits.maxFramebufferLayers;
        }
    }
}

static bool d3d12_command_list_update_rendering_info(struct d3d12_command_list *list)
{
    struct vkd3d_rendering_info *rendering_info = &list->rendering_info;
    struct d3d12_graphics_pipeline_state *graphics;
    VkExtent2D old_extent;
    unsigned int i;

    if (rendering_info->state_flags & VKD3D_RENDERING_CURRENT)
        return true;

    graphics = &list->state->graphics;

    rendering_info->rtv_mask = graphics->rtv_active_mask;
    rendering_info->info.colorAttachmentCount = graphics->rt_count;

    /* The pipeline has fallback PSO in case we're attempting to render to unbound RTV. */
    for (i = 0; i < D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT; i++)
    {
        VkRenderingAttachmentInfo *attachment = &rendering_info->rtv[i];

        if ((graphics->rtv_active_mask & (1u << i)) && list->rtvs[i].view)
        {
            attachment->imageView = list->rtvs[i].view->vk_image_view;
            attachment->imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        }
        else
        {
            attachment->imageView = VK_NULL_HANDLE;
            attachment->imageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        }
    }

    rendering_info->info.pDepthAttachment = NULL;
    rendering_info->info.pStencilAttachment = NULL;

    if (d3d12_command_list_has_depth_stencil_view(list))
    {
        rendering_info->dsv.imageView = list->dsv.view->vk_image_view;
        rendering_info->dsv.imageLayout = list->dsv_layout;

        /* Spec says that to use pDepthAttachment or pStencilAttachment, with non-NULL image view,
         * the format must have the aspect mask set. */
        if (list->dsv.view->format->vk_aspect_mask & VK_IMAGE_ASPECT_DEPTH_BIT)
            rendering_info->info.pDepthAttachment = &rendering_info->dsv;
        if (list->dsv.view->format->vk_aspect_mask & VK_IMAGE_ASPECT_STENCIL_BIT)
            rendering_info->info.pStencilAttachment = &rendering_info->dsv;
    }
    else
    {
        rendering_info->dsv.imageView = VK_NULL_HANDLE;
        rendering_info->dsv.imageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    }

    if (list->vrs_image)
    {
        rendering_info->vrs.imageView = list->vrs_image->vrs_view;
        rendering_info->vrs.imageLayout = VK_IMAGE_LAYOUT_FRAGMENT_SHADING_RATE_ATTACHMENT_OPTIMAL_KHR;
    }
    else
    {
        rendering_info->vrs.imageView = VK_NULL_HANDLE;
        rendering_info->vrs.imageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    }

    old_extent = rendering_info->info.renderArea.extent;
    d3d12_command_list_get_fb_extent(list,
            &rendering_info->info.renderArea.extent.width,
            &rendering_info->info.renderArea.extent.height,
            &rendering_info->info.layerCount);

    /* It is robust in D3D12 to render with a scissor rect that out of bounds, but not so in Vulkan,
     * so we might have to re-clamp the scissor state. */
    if (old_extent.width != rendering_info->info.renderArea.extent.width ||
            old_extent.height != rendering_info->info.renderArea.extent.height)
    {
        list->dynamic_state.dirty_flags |= VKD3D_DYNAMIC_STATE_SCISSOR;
    }

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
        VK_CALL(vkCmdBindPipeline(list->cmd.vk_command_buffer,
                vk_bind_point_from_pipeline_type(list->state->pipeline_type),
                list->state->compute.vk_pipeline));
        list->command_buffer_pipeline = list->state->compute.vk_pipeline;
    }
    list->current_pipeline = list->state->compute.vk_pipeline;
    list->dynamic_state.active_flags = 0;

    return true;
}

static bool d3d12_command_list_update_raygen_pipeline(struct d3d12_command_list *list)
{
    const struct vkd3d_vk_device_procs *vk_procs = &list->device->vk_procs;
    bool stack_size_dirty = false;
    unsigned int i;

    if (list->current_pipeline != VK_NULL_HANDLE)
        return true;

    if (!list->rt_state)
    {
        WARN("Pipeline state %p is not a raygen pipeline.\n", list->rt_state);
        return false;
    }

    /* We need to bind the RTPSO that is compatible. */
    list->rt_state_variant = NULL;
    for (i = 0; i < list->rt_state->pipelines_count; i++)
    {
        if (d3d12_root_signature_is_layout_compatible(
                list->rt_state->pipelines[i].global_root_signature,
                list->compute_bindings.root_signature))
        {
            /* We might have degenerate variants. */
            if (list->rt_state->pipelines[i].pipeline)
                list->rt_state_variant = &list->rt_state->pipelines[i];
            break;
        }
    }

    if (!list->rt_state_variant)
    {
        WARN("Global root signature compatible with the RTPSO.\n");
        return false;
    }

    if (list->command_buffer_pipeline != list->rt_state_variant->pipeline)
    {
        VK_CALL(vkCmdBindPipeline(list->cmd.vk_command_buffer,
                VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR,
                list->rt_state_variant->pipeline));
        list->command_buffer_pipeline = list->rt_state_variant->pipeline;
        list->dynamic_state.active_flags = 0;
        stack_size_dirty = true;
    }
    else
    {
        stack_size_dirty = list->dynamic_state.pipeline_stack_size != list->rt_state->pipeline_stack_size;
    }

    if (stack_size_dirty)
    {
        /* Pipeline stack size is part of the PSO, not any command buffer state for some reason ... */
        VK_CALL(vkCmdSetRayTracingPipelineStackSizeKHR(list->cmd.vk_command_buffer,
                list->rt_state->pipeline_stack_size));
        list->dynamic_state.pipeline_stack_size = list->rt_state->pipeline_stack_size;
    }

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

static bool d3d12_command_list_update_graphics_pipeline(struct d3d12_command_list *list,
        enum vkd3d_pipeline_type pipeline_type)
{
    const struct vkd3d_vk_device_procs *vk_procs = &list->device->vk_procs;
    uint32_t dsv_plane_optimal_mask;
    uint32_t new_active_flags;
    VkImageLayout dsv_layout;
    VkPipeline vk_pipeline;

    if (list->current_pipeline != VK_NULL_HANDLE)
        return true;

    if (!list->state)
    {
        WARN("No graphics pipeline bound, skipping draw.\n");
        return false;
    }

    if (list->state->pipeline_type != pipeline_type)
    {
        WARN("Pipeline state %p is is of type %u, expected type %u.\n",
                list->state, list->state->pipeline_type, pipeline_type);
        return false;
    }

    /* Try to grab the pipeline we compiled ahead of time. If we cannot do so, fall back. */
    if (!(vk_pipeline = d3d12_pipeline_state_get_pipeline(list->state,
            &list->dynamic_state, list->dsv.format, &new_active_flags)))
    {
        if (!(vk_pipeline = d3d12_pipeline_state_get_or_create_pipeline(list->state,
                &list->dynamic_state, list->dsv.format, &new_active_flags)))
            return false;
    }

    if (d3d12_command_list_has_depth_stencil_view(list))
    {
        /* Select new dsv_layout. Any new PSO write we didn't observe yet must be updated here. */
        dsv_plane_optimal_mask = list->dsv_plane_optimal_mask | list->state->graphics.dsv_plane_optimal_mask;
        dsv_layout = dsv_plane_optimal_mask_to_layout(dsv_plane_optimal_mask, list->dsv.format->vk_aspect_mask);
    }
    else
    {
        dsv_plane_optimal_mask = 0;
        dsv_layout = VK_IMAGE_LAYOUT_UNDEFINED;
    }

    /* If we need to bind or unbind certain render targets or if the DSV layout changed, interrupt rendering.
     * It's also possible that rtv_active_mask is constant, but rt_count increases (if last RT format is NULL). */
    if ((list->state->graphics.rtv_active_mask != list->rendering_info.rtv_mask) ||
            (list->state->graphics.rt_count != list->rendering_info.info.colorAttachmentCount) ||
            (dsv_layout != list->rendering_info.dsv.imageLayout))
    {
        d3d12_command_list_invalidate_rendering_info(list);
        d3d12_command_list_end_current_render_pass(list, false);
    }

    /* We can't change pipelines while transform feedback is active, so end the render pass here if necessary. */
    if (list->command_buffer_pipeline != vk_pipeline && list->xfb_buffer_count)
        d3d12_command_list_end_current_render_pass(list, true);

    list->dsv_plane_optimal_mask = dsv_plane_optimal_mask;
    list->dsv_layout = dsv_layout;

    if (list->command_buffer_pipeline != vk_pipeline)
    {
        VK_CALL(vkCmdBindPipeline(list->cmd.vk_command_buffer,
                vk_bind_point_from_pipeline_type(list->state->pipeline_type),
                vk_pipeline));

        /* If we bind a new pipeline, make sure that we end up binding VBOs that are aligned.
         * It is fine to do it here, since we are binding a pipeline right before we perform
         * a draw call. If we trip any dirty check here, VBO offsets will be fixed up when emitting
         * dynamic state after this. */
        d3d12_command_list_check_vbo_alignment(list);

        /* The application did set vertex buffers that we didn't bind because of the pipeline vbo mask.
         * The new pipeline could use those so we need to rebind vertex buffers. */
        if ((new_active_flags & VKD3D_DYNAMIC_STATE_VERTEX_BUFFER_STRIDE) && list->dynamic_state.dirty_vbos)
            list->dynamic_state.dirty_flags |= VKD3D_DYNAMIC_STATE_VERTEX_BUFFER_STRIDE;

        /* Reapply all dynamic states that were not dynamic in previously bound pipeline.
         * If we didn't use to have dynamic vertex strides, but we then bind a pipeline with dynamic strides,
         * we will need to rebind all VBOs. Mark dynamic stride as dirty in this case. */
        if (new_active_flags & ~list->dynamic_state.active_flags & VKD3D_DYNAMIC_STATE_VERTEX_BUFFER_STRIDE)
            list->dynamic_state.dirty_vbos = ~0u;
        list->dynamic_state.dirty_flags |= new_active_flags & ~list->dynamic_state.active_flags;
        list->command_buffer_pipeline = vk_pipeline;
    }

    /* If no render targets are bound, we use PSO state to determine the sample count, but we do
     * not track this as part of command list state. This is only relevant if the PSO statically
     * enables any DSV or RTV but none are bound to the command list. */
    if (!list->dynamic_state.rasterization_samples)
        list->dynamic_state.dirty_flags |= VKD3D_DYNAMIC_STATE_RASTERIZATION_SAMPLES;

    list->dynamic_state.active_flags = new_active_flags;
    list->current_pipeline = vk_pipeline;
    return true;
}

static void d3d12_command_list_update_descriptor_table_offsets(struct d3d12_command_list *list,
        struct vkd3d_pipeline_bindings *bindings, VkPipelineLayout layout, VkShaderStageFlags push_stages)
{
    const struct d3d12_root_signature *root_signature = bindings->root_signature;
    const struct vkd3d_vk_device_procs *vk_procs = &list->device->vk_procs;
    const struct vkd3d_shader_descriptor_table *table;
    uint32_t table_offsets[D3D12_MAX_ROOT_COST];
    unsigned int root_parameter_index;
    uint64_t descriptor_table_mask;

    assert(root_signature->descriptor_table_count);
    descriptor_table_mask = root_signature->descriptor_table_mask;

    while (descriptor_table_mask)
    {
        root_parameter_index = vkd3d_bitmask_iter64(&descriptor_table_mask);
        table = root_signature_get_descriptor_table(root_signature, root_parameter_index);
        table_offsets[table->table_index] = bindings->descriptor_tables[root_parameter_index];
    }

    /* Set descriptor offsets */
    if (push_stages)
    {
        VK_CALL(vkCmdPushConstants(list->cmd.vk_command_buffer,
                layout, push_stages,
                root_signature->descriptor_table_offset,
                root_signature->descriptor_table_count * sizeof(uint32_t),
                table_offsets));
    }

    bindings->dirty_flags &= ~VKD3D_PIPELINE_DIRTY_DESCRIPTOR_TABLE_OFFSETS;
}

static void vk_write_descriptor_set_from_root_descriptor(struct d3d12_command_list *list,
        VkWriteDescriptorSet *vk_descriptor_write, const struct vkd3d_shader_root_parameter *root_parameter,
        const struct vkd3d_root_descriptor_info *descriptor)
{
    vk_descriptor_write->sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    vk_descriptor_write->pNext = NULL;
    vk_descriptor_write->dstSet = VK_NULL_HANDLE;
    vk_descriptor_write->dstBinding = root_parameter->descriptor.binding->binding.binding;
    vk_descriptor_write->dstArrayElement = 0;
    vk_descriptor_write->descriptorType = descriptor->vk_descriptor_type;
    vk_descriptor_write->descriptorCount = 1;
    vk_descriptor_write->pImageInfo = NULL;
    vk_descriptor_write->pBufferInfo = &descriptor->info.buffer;
    vk_descriptor_write->pTexelBufferView = &descriptor->info.buffer_view;
}

static void vk_write_descriptor_set_from_scratch_push_ubo(VkWriteDescriptorSet *vk_descriptor_write,
        VkDescriptorBufferInfo *vk_buffer_info,
        const struct vkd3d_scratch_allocation *alloc,
        VkDeviceSize size, uint32_t vk_binding)
{
    vk_descriptor_write->sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    vk_descriptor_write->pNext = NULL;
    vk_descriptor_write->dstSet = VK_NULL_HANDLE;
    vk_descriptor_write->descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    vk_descriptor_write->dstArrayElement = 0;
    vk_descriptor_write->dstBinding = vk_binding;
    vk_descriptor_write->descriptorCount = 1;
    vk_descriptor_write->pBufferInfo = vk_buffer_info;
    vk_descriptor_write->pImageInfo = NULL;
    vk_descriptor_write->pTexelBufferView = NULL;

    vk_buffer_info->buffer = alloc->buffer;
    vk_buffer_info->offset = alloc->offset;
    vk_buffer_info->range = size;
}

/* This is a big stall on some GPUs so need to track this separately. */
void d3d12_command_list_update_descriptor_buffers(struct d3d12_command_list *list)
{
    const struct vkd3d_vk_device_procs *vk_procs = &list->device->vk_procs;
    VkDescriptorBufferBindingPushDescriptorBufferHandleEXT buffer_handle;
    VkDescriptorBufferBindingInfoEXT global_buffers[2];

    if (d3d12_device_uses_descriptor_buffers(list->device) &&
            list->descriptor_heap.buffers.heap_dirty)
    {
        global_buffers[0].sType = VK_STRUCTURE_TYPE_DESCRIPTOR_BUFFER_BINDING_INFO_EXT;
        global_buffers[0].pNext = NULL;
        global_buffers[0].usage = list->device->global_descriptor_buffer.resource.usage;
        global_buffers[0].address = list->descriptor_heap.buffers.heap_va_resource;

        if (global_buffers[0].usage & VK_BUFFER_USAGE_PUSH_DESCRIPTORS_DESCRIPTOR_BUFFER_BIT_EXT)
        {
            global_buffers[0].pNext = &buffer_handle;
            buffer_handle.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_BUFFER_BINDING_PUSH_DESCRIPTOR_BUFFER_HANDLE_EXT;
            buffer_handle.pNext = NULL;
            buffer_handle.buffer = list->descriptor_heap.buffers.vk_buffer_resource;
        }

        global_buffers[1].sType = VK_STRUCTURE_TYPE_DESCRIPTOR_BUFFER_BINDING_INFO_EXT;
        global_buffers[1].pNext = NULL;
        global_buffers[1].usage = list->device->global_descriptor_buffer.sampler.usage;
        global_buffers[1].address = list->descriptor_heap.buffers.heap_va_sampler;

        VK_CALL(vkCmdBindDescriptorBuffersEXT(list->cmd.vk_command_buffer,
                ARRAY_SIZE(global_buffers), global_buffers));

        list->descriptor_heap.buffers.heap_dirty = false;
    }
}

static void d3d12_command_list_update_descriptor_heaps(struct d3d12_command_list *list,
        struct vkd3d_pipeline_bindings *bindings, VkPipelineBindPoint vk_bind_point,
        VkPipelineLayout layout)
{
    const struct vkd3d_bindless_state *bindless_state = &list->device->bindless_state;
    const struct vkd3d_vk_device_procs *vk_procs = &list->device->vk_procs;

    if (!bindings->descriptor_heap_dirty_mask)
        return;

    if (d3d12_device_uses_descriptor_buffers(list->device))
    {
        d3d12_command_list_update_descriptor_buffers(list);

        /* Prefer binding everything in one go. There is no risk of null descriptor sets here. */
        if (bindings->descriptor_heap_dirty_mask)
        {
            VK_CALL(vkCmdSetDescriptorBufferOffsetsEXT(list->cmd.vk_command_buffer, vk_bind_point,
                    layout, 0, bindless_state->set_count,
                    bindless_state->vk_descriptor_buffer_indices,
                    list->descriptor_heap.buffers.vk_offsets));
            bindings->descriptor_heap_dirty_mask = 0;
        }
    }
    else
    {
        while (bindings->descriptor_heap_dirty_mask)
        {
            unsigned int heap_index = vkd3d_bitmask_iter64(&bindings->descriptor_heap_dirty_mask);

            if (list->descriptor_heap.sets.vk_sets[heap_index])
            {
                VK_CALL(vkCmdBindDescriptorSets(list->cmd.vk_command_buffer, vk_bind_point,
                        layout, heap_index, 1,
                        &list->descriptor_heap.sets.vk_sets[heap_index], 0, NULL));
            }
        }
    }
}

static void d3d12_command_list_update_static_samplers(struct d3d12_command_list *list,
        struct vkd3d_pipeline_bindings *bindings, VkPipelineBindPoint vk_bind_point,
        VkPipelineLayout layout)
{
    const struct d3d12_root_signature *root_signature = bindings->root_signature;
    const struct vkd3d_vk_device_procs *vk_procs = &list->device->vk_procs;

    if (bindings->static_sampler_set)
    {
        VK_CALL(vkCmdBindDescriptorSets(list->cmd.vk_command_buffer, vk_bind_point,
                layout,
                root_signature->sampler_descriptor_set,
                1, &bindings->static_sampler_set, 0, NULL));
    }
    else if (root_signature->vk_sampler_descriptor_layout)
    {
        VK_CALL(vkCmdBindDescriptorBufferEmbeddedSamplersEXT(list->cmd.vk_command_buffer, vk_bind_point,
                layout, root_signature->sampler_descriptor_set));
    }

    bindings->dirty_flags &= ~VKD3D_PIPELINE_DIRTY_STATIC_SAMPLER_SET;
}

static void d3d12_command_list_update_root_constants(struct d3d12_command_list *list,
        struct vkd3d_pipeline_bindings *bindings,
        VkPipelineLayout layout, VkShaderStageFlags push_stages)
{
    const struct d3d12_root_signature *root_signature = bindings->root_signature;
    const struct vkd3d_vk_device_procs *vk_procs = &list->device->vk_procs;
    const struct vkd3d_shader_root_constant *root_constant;
    unsigned int root_parameter_index;

    if (!push_stages)
    {
        bindings->root_constant_dirty_mask = 0;
        return;
    }

    while (bindings->root_constant_dirty_mask)
    {
        root_parameter_index = vkd3d_bitmask_iter64(&bindings->root_constant_dirty_mask);
        root_constant = root_signature_get_32bit_constants(root_signature, root_parameter_index);

        VK_CALL(vkCmdPushConstants(list->cmd.vk_command_buffer,
                layout, push_stages,
                root_constant->constant_index * sizeof(uint32_t),
                root_constant->constant_count * sizeof(uint32_t),
                &bindings->root_constants[root_constant->constant_index]));
    }
}

static unsigned int d3d12_command_list_fetch_root_descriptor_vas(struct d3d12_command_list *list,
        struct vkd3d_pipeline_bindings *bindings, union vkd3d_root_parameter_data *dst_data)
{
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

static void d3d12_command_list_fetch_root_parameter_uniform_block_data(struct d3d12_command_list *list,
        struct vkd3d_pipeline_bindings *bindings, union vkd3d_root_parameter_data *dst_data)
{
    const struct d3d12_root_signature *root_signature = bindings->root_signature;
    uint64_t root_constant_mask = root_signature->root_constant_mask;
    const struct vkd3d_shader_root_constant *root_constant;
    const uint32_t *src_data = bindings->root_constants;
    const struct vkd3d_shader_descriptor_table *table;
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
    descriptor_table_mask = root_signature->descriptor_table_mask;

    while (descriptor_table_mask)
    {
        root_parameter_index = vkd3d_bitmask_iter64(&descriptor_table_mask);
        table = root_signature_get_descriptor_table(root_signature, root_parameter_index);
        dst_data->root_constants[first_table_offset + table->table_index] =
                bindings->descriptor_tables[root_parameter_index];
    }
}

void d3d12_command_list_fetch_root_parameter_data(struct d3d12_command_list *list,
        struct vkd3d_pipeline_bindings *bindings, union vkd3d_root_parameter_data *dst_data)
{
    d3d12_command_list_fetch_root_descriptor_vas(list, bindings, dst_data);
    d3d12_command_list_fetch_root_parameter_uniform_block_data(list, bindings, dst_data);
}

static void d3d12_command_list_update_root_descriptors(struct d3d12_command_list *list,
        struct vkd3d_pipeline_bindings *bindings, VkPipelineBindPoint vk_bind_point,
        VkPipelineLayout layout, VkShaderStageFlags push_stages, uint32_t root_signature_flags)
{
    const struct d3d12_root_signature *root_signature = bindings->root_signature;
    const struct vkd3d_vk_device_procs *vk_procs = &list->device->vk_procs;
    VkWriteDescriptorSet descriptor_writes[D3D12_MAX_ROOT_COST / 2];
    const struct vkd3d_shader_root_parameter *root_parameter;
    union vkd3d_root_parameter_data *ptr_root_parameter_data;
    union vkd3d_root_parameter_data root_parameter_data;
    unsigned int descriptor_write_count = 0;
    struct vkd3d_scratch_allocation alloc;
    VkDescriptorBufferInfo buffer_info;
    unsigned int root_parameter_index;
    unsigned int va_count = 0;
    uint64_t dirty_push_mask;

    if (root_signature_flags & VKD3D_ROOT_SIGNATURE_USE_PUSH_CONSTANT_UNIFORM_BLOCK)
    {
        d3d12_command_allocator_allocate_scratch_memory(list->allocator,
                VKD3D_SCRATCH_POOL_KIND_UNIFORM_UPLOAD, sizeof(root_parameter_data),
                D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT, ~0u, &alloc);
        ptr_root_parameter_data = alloc.host_ptr;

        /* Dirty all state that enters push UBO block to make sure it's emitted.
         * Push descriptors that are not raw VA can be emitted on a partial basis.
         * Root constants and tables are always considered dirty here, so omit that. */
        bindings->root_descriptor_dirty_mask |= root_signature->root_descriptor_raw_va_mask;
    }
    else
        ptr_root_parameter_data = &root_parameter_data;

    if (bindings->root_descriptor_dirty_mask)
    {
        /* If any raw VA descriptor is dirty, we need to update all of them. */
        if (root_signature->root_descriptor_raw_va_mask & bindings->root_descriptor_dirty_mask)
            va_count = d3d12_command_list_fetch_root_descriptor_vas(list, bindings, ptr_root_parameter_data);

        /* TODO bind null descriptors for inactive root descriptors. */
        dirty_push_mask =
                bindings->root_descriptor_dirty_mask &
                root_signature->root_descriptor_push_mask &
                bindings->root_descriptor_active_mask;

        while (dirty_push_mask)
        {
            root_parameter_index = vkd3d_bitmask_iter64(&dirty_push_mask);
            root_parameter = root_signature_get_root_descriptor(root_signature, root_parameter_index);

            vk_write_descriptor_set_from_root_descriptor(list,
                    &descriptor_writes[descriptor_write_count], root_parameter,
                    &bindings->root_descriptors[root_parameter_index]);

            descriptor_write_count += 1;
        }

        bindings->root_descriptor_dirty_mask = 0;
    }

    if (root_signature_flags & VKD3D_ROOT_SIGNATURE_USE_PUSH_CONSTANT_UNIFORM_BLOCK)
    {
        d3d12_command_list_fetch_root_parameter_uniform_block_data(list, bindings, ptr_root_parameter_data);

        /* Reset dirty flags to avoid redundant updates in the future.
         * We consume all constants / tables here regardless of dirty state. */
        bindings->dirty_flags &= ~VKD3D_PIPELINE_DIRTY_DESCRIPTOR_TABLE_OFFSETS;
        bindings->root_constant_dirty_mask = 0;

        vk_write_descriptor_set_from_scratch_push_ubo(&descriptor_writes[descriptor_write_count],
                &buffer_info, &alloc, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT,
                root_signature->push_constant_ubo_binding.binding);

        descriptor_write_count += 1;
    }
    else if (va_count && push_stages)
    {
        VK_CALL(vkCmdPushConstants(list->cmd.vk_command_buffer,
                layout, push_stages,
                0, va_count * sizeof(*root_parameter_data.root_descriptor_vas),
                root_parameter_data.root_descriptor_vas));
    }

#ifdef VKD3D_ENABLE_DESCRIPTOR_QA
    if (vkd3d_descriptor_debug_active_instruction_qa_checks())
    {
        VkWriteDescriptorSet *write = &descriptor_writes[descriptor_write_count];
        memset(write, 0, 2 * sizeof(*write));
        write[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        write[0].descriptorCount = 1;
        write[0].dstBinding = root_signature->descriptor_qa_control_binding.binding;
        write[0].pBufferInfo = vkd3d_descriptor_debug_get_control_info_descriptor(list->device->descriptor_qa_global_info);

        write[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        write[1].descriptorCount = 1;
        write[1].dstBinding = root_signature->descriptor_qa_payload_binding.binding;
        write[1].pBufferInfo = vkd3d_descriptor_debug_get_payload_info_descriptor(list->device->descriptor_qa_global_info);

        descriptor_write_count += 2;
    }
#endif

    if (descriptor_write_count)
    {
        VK_CALL(vkCmdPushDescriptorSetKHR(list->cmd.vk_command_buffer, vk_bind_point,
                layout, root_signature->root_descriptor_set,
                descriptor_write_count, descriptor_writes));
    }
}

static void d3d12_command_list_update_hoisted_descriptors(struct d3d12_command_list *list,
        struct vkd3d_pipeline_bindings *bindings)
{
    const struct d3d12_root_signature *rs = bindings->root_signature;
    const struct vkd3d_descriptor_hoist_desc *hoist_desc;
    struct vkd3d_root_descriptor_info *root_parameter;
    const struct vkd3d_descriptor_metadata_view *view;
    const struct vkd3d_unique_resource *resource;
    union vkd3d_descriptor_info *info;
    unsigned int i;

    /* We don't track dirty table index, just update every hoisted descriptor.
     * Uniform buffers tend to be updated all the time anyways, so this should be fine. */
    for (i = 0; i < rs->hoist_info.num_desc; i++)
    {
        hoist_desc = &rs->hoist_info.desc[i];

        view = list->cbv_srv_uav_descriptors_view;
        if (view)
            view += bindings->descriptor_tables[hoist_desc->table_index] + hoist_desc->table_offset;

        root_parameter = &bindings->root_descriptors[hoist_desc->parameter_index];

        bindings->root_descriptor_dirty_mask |= 1ull << hoist_desc->parameter_index;
        bindings->root_descriptor_active_mask |= 1ull << hoist_desc->parameter_index;
        root_parameter->vk_descriptor_type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        info = &root_parameter->info;

        if (view && (view->info.buffer.flags & VKD3D_DESCRIPTOR_FLAG_BUFFER_VA_RANGE))
        {
            /* Buffer descriptors must be valid on recording time. */
            resource = vkd3d_va_map_deref(&list->device->memory_allocator.va_map, view->info.buffer.va);
            if (resource)
            {
                info->buffer.buffer = resource->vk_buffer;
                info->buffer.offset = view->info.buffer.va - resource->va;
                info->buffer.range = min(view->info.buffer.range, resource->size - info->buffer.offset);
            }
            else
            {
                info->buffer.buffer = VK_NULL_HANDLE;
                info->buffer.offset = 0;
                info->buffer.range = VK_WHOLE_SIZE;
            }
        }
        else
        {
            info->buffer.buffer = VK_NULL_HANDLE;
            info->buffer.offset = 0;
            info->buffer.range = VK_WHOLE_SIZE;
        }
    }

    bindings->dirty_flags &= ~VKD3D_PIPELINE_DIRTY_HOISTED_DESCRIPTORS;
}

static void d3d12_command_list_update_descriptors(struct d3d12_command_list *list)
{
    struct vkd3d_pipeline_bindings *bindings = d3d12_command_list_get_bindings(list, list->active_pipeline_type);
    const struct d3d12_root_signature *rs = bindings->root_signature;
    const struct d3d12_bind_point_layout *bind_point_layout;
    VkPipelineBindPoint vk_bind_point;
    VkShaderStageFlags push_stages;
    VkPipelineLayout layout;

    if (!rs)
        return;

    bind_point_layout = d3d12_root_signature_get_layout(rs, list->active_pipeline_type);
    layout = bind_point_layout->vk_pipeline_layout;
    push_stages = bind_point_layout->vk_push_stages;

    vk_bind_point = vk_bind_point_from_pipeline_type(list->active_pipeline_type);

    if (bindings->descriptor_heap_dirty_mask)
        d3d12_command_list_update_descriptor_heaps(list, bindings, vk_bind_point, layout);

    if (bindings->dirty_flags & VKD3D_PIPELINE_DIRTY_STATIC_SAMPLER_SET)
        d3d12_command_list_update_static_samplers(list, bindings, vk_bind_point, layout);

    /* If we can, hoist descriptors from the descriptor heap into fake root parameters. */
    if (bindings->dirty_flags & VKD3D_PIPELINE_DIRTY_HOISTED_DESCRIPTORS)
        d3d12_command_list_update_hoisted_descriptors(list, bindings);

    if (bind_point_layout->flags & VKD3D_ROOT_SIGNATURE_USE_PUSH_CONSTANT_UNIFORM_BLOCK)
    {
        /* Root constants and descriptor table offsets are part of the root descriptor set */
        if (bindings->root_descriptor_dirty_mask || bindings->root_constant_dirty_mask
                || (bindings->dirty_flags & VKD3D_PIPELINE_DIRTY_DESCRIPTOR_TABLE_OFFSETS))
        {
            d3d12_command_list_update_root_descriptors(list, bindings, vk_bind_point, layout, push_stages,
                    bind_point_layout->flags);
        }
    }
    else
    {
        if (bindings->root_descriptor_dirty_mask)
        {
            d3d12_command_list_update_root_descriptors(list, bindings, vk_bind_point, layout, push_stages,
                    bind_point_layout->flags);
        }

        if (bindings->root_constant_dirty_mask)
            d3d12_command_list_update_root_constants(list, bindings, layout, push_stages);

        if (bindings->dirty_flags & VKD3D_PIPELINE_DIRTY_DESCRIPTOR_TABLE_OFFSETS)
            d3d12_command_list_update_descriptor_table_offsets(list, bindings, layout, push_stages);
    }
}

static void d3d12_command_list_update_descriptors_post_indirect_buffer(struct d3d12_command_list *list)
{
    /* Pretend for a moment that the post indirect buffer is the main command buffer.
     * Set all dirty bits so we force-flush state to a different command buffer.
     * Not the most elegant solution, but avoids us having to reimplement everything
     * just to plumb thorugh a different set of dirty masks, etc. */
    struct vkd3d_pipeline_bindings *bindings = d3d12_command_list_get_bindings(list, list->active_pipeline_type);
    const struct d3d12_root_signature *rs = bindings->root_signature;
    uint32_t old_root_descriptor_dirty_mask;
    uint32_t old_descriptor_heap_dirty_mask;
    uint32_t old_root_constant_dirty_mask;
    VkCommandBuffer old_cmd_buffer;
    bool old_heap_dirty = false;
    uint32_t old_dirty_flags;

    if (!rs)
        return;

    old_root_descriptor_dirty_mask = bindings->root_descriptor_dirty_mask;
    old_descriptor_heap_dirty_mask = bindings->descriptor_heap_dirty_mask;
    old_root_constant_dirty_mask = bindings->root_constant_dirty_mask;
    old_dirty_flags = bindings->dirty_flags;
    old_cmd_buffer = list->cmd.vk_command_buffer;
    /* This is bad, but the current NV implementation does not actually
     * do anything bad when rebinding descriptor buffers, so just roll with it.
     * Can be fixed if necessary. */
    if (d3d12_device_uses_descriptor_buffers(list->device))
        old_heap_dirty = list->descriptor_heap.buffers.heap_dirty;

    /* Override state. */
    list->cmd.vk_command_buffer = list->cmd.vk_init_commands_post_indirect_barrier;
    if (d3d12_device_uses_descriptor_buffers(list->device))
        list->descriptor_heap.buffers.heap_dirty = true;
    d3d12_command_list_invalidate_root_parameters(list, bindings, true, NULL);
    d3d12_command_list_update_descriptors(list);

    /* Restore state. */
    bindings->root_descriptor_dirty_mask = old_root_descriptor_dirty_mask;
    bindings->descriptor_heap_dirty_mask = old_descriptor_heap_dirty_mask;
    bindings->root_constant_dirty_mask = old_root_constant_dirty_mask;
    bindings->dirty_flags = old_dirty_flags;
    list->cmd.vk_command_buffer = old_cmd_buffer;
    if (d3d12_device_uses_descriptor_buffers(list->device))
        list->descriptor_heap.buffers.heap_dirty = old_heap_dirty;
}

static void d3d12_command_list_check_pre_compute_barrier(struct d3d12_command_list *list);

static bool d3d12_command_list_update_compute_state(struct d3d12_command_list *list)
{
    d3d12_command_list_end_current_render_pass(list, false);

    if (!d3d12_command_list_update_compute_pipeline(list))
        return false;

    d3d12_command_list_check_pre_compute_barrier(list);
    d3d12_command_list_update_descriptors(list);

    return true;
}

static bool d3d12_command_list_update_raygen_state(struct d3d12_command_list *list)
{
    const struct vkd3d_vk_device_procs *vk_procs = &list->device->vk_procs;
    d3d12_command_list_end_current_render_pass(list, false);

    if (!d3d12_command_list_update_raygen_pipeline(list))
        return false;

    /* DXR uses compute bind point for descriptors, we will redirect internally to
     * raygen bind point in Vulkan. */
    d3d12_command_list_update_descriptors(list);

    /* If we have a static sampler set for local root signatures, bind it now.
     * Don't bother with dirty tracking of this for time being.
     * Should be very rare that this path is even hit. */
    if (list->rt_state_variant->local_static_sampler.set_layout)
    {
        if (list->rt_state_variant->local_static_sampler.desc_set)
        {
            VK_CALL(vkCmdBindDescriptorSets(list->cmd.vk_command_buffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR,
                    list->rt_state_variant->local_static_sampler.pipeline_layout,
                    list->rt_state_variant->local_static_sampler.set_index,
                    1, &list->rt_state_variant->local_static_sampler.desc_set,
                    0, NULL));
        }
        else
        {
            VK_CALL(vkCmdBindDescriptorBufferEmbeddedSamplersEXT(list->cmd.vk_command_buffer,
                    VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR,
                    list->rt_state_variant->local_static_sampler.pipeline_layout,
                    list->rt_state_variant->local_static_sampler.set_index));
        }
    }

    return true;
}

static void d3d12_command_list_update_dynamic_state(struct d3d12_command_list *list)
{
    const struct vkd3d_vk_device_procs *vk_procs = &list->device->vk_procs;
    struct vkd3d_dynamic_state *dyn_state = &list->dynamic_state;
    VkDepthBiasRepresentationInfoEXT depth_bias_representation;
    VkSampleCountFlagBits rasterization_samples;
    VkDepthBiasInfoEXT depth_bias_info;
    const uint32_t *stride_align_masks;
    struct vkd3d_bitmask_range range;
    uint32_t update_vbos;
    unsigned int i;

    /* Make sure we only update states that are dynamic in the pipeline */
    dyn_state->dirty_flags &= list->dynamic_state.active_flags;

    if (dyn_state->viewport_count)
    {
        if (dyn_state->dirty_flags & VKD3D_DYNAMIC_STATE_VIEWPORT)
        {
            VK_CALL(vkCmdSetViewportWithCount(list->cmd.vk_command_buffer,
                    dyn_state->viewport_count, dyn_state->viewports));
        }

        if (dyn_state->dirty_flags & VKD3D_DYNAMIC_STATE_SCISSOR)
        {
            /* Rendering with OOB scissor seems to work fine in D3D12,
             * but it's out of spec in Vulkan,
             * and NV can crash GPU if scissor is out of bounds in some cases. */
            VkRect2D clamped_scissors[D3D12_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
            VkExtent2D max_sci_extent, max_fb_extent;
            const VkRect2D *scissor;

            max_fb_extent = list->rendering_info.info.renderArea.extent;

            for (i = 0; i < dyn_state->viewport_count; i++)
            {
                scissor = &dyn_state->scissors[i];
                max_sci_extent.width = max((int)max_fb_extent.width - scissor->offset.x, 0);
                max_sci_extent.height = max((int)max_fb_extent.height - scissor->offset.y, 0);
                clamped_scissors[i].offset = scissor->offset;
                clamped_scissors[i].extent.width = min(max_sci_extent.width, scissor->extent.width);
                clamped_scissors[i].extent.height = min(max_sci_extent.height, scissor->extent.height);
            }

            VK_CALL(vkCmdSetScissorWithCount(list->cmd.vk_command_buffer,
                    dyn_state->viewport_count, clamped_scissors));
        }
    }
    else
    {
        /* Zero viewports disables rasterization. Emit dummy viewport / scissor rects.
         * For non-dynamic fallbacks, we force viewportCount to be at least 1. */
        static const VkViewport dummy_vp = { 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f };
        static const VkRect2D dummy_rect = { { 0, 0 }, { 0, 0 } };

        if (dyn_state->dirty_flags & VKD3D_DYNAMIC_STATE_VIEWPORT)
            VK_CALL(vkCmdSetViewportWithCount(list->cmd.vk_command_buffer, 1, &dummy_vp));
        if (dyn_state->dirty_flags & VKD3D_DYNAMIC_STATE_SCISSOR)
            VK_CALL(vkCmdSetScissorWithCount(list->cmd.vk_command_buffer, 1, &dummy_rect));
    }

    if (dyn_state->dirty_flags & VKD3D_DYNAMIC_STATE_BLEND_CONSTANTS)
    {
        VK_CALL(vkCmdSetBlendConstants(list->cmd.vk_command_buffer,
                dyn_state->blend_constants));
    }

    if (dyn_state->dirty_flags & VKD3D_DYNAMIC_STATE_STENCIL_REFERENCE)
    {
        VK_CALL(vkCmdSetStencilReference(list->cmd.vk_command_buffer,
                VK_STENCIL_FACE_FRONT_BIT, dyn_state->stencil_front.reference));
        VK_CALL(vkCmdSetStencilReference(list->cmd.vk_command_buffer,
                VK_STENCIL_FACE_BACK_BIT, dyn_state->stencil_back.reference));
    }

    if (dyn_state->dirty_flags & VKD3D_DYNAMIC_STATE_DEPTH_WRITE_ENABLE)
    {
        VK_CALL(vkCmdSetDepthWriteEnable(list->cmd.vk_command_buffer,
                (dyn_state->dsv_plane_write_enable & (1u << 0)) ? VK_TRUE : VK_FALSE));
    }

    if (dyn_state->dirty_flags & VKD3D_DYNAMIC_STATE_STENCIL_WRITE_MASK)
    {
        /* Binding read-only DSV for stencil disable stencil writes. */
        VK_CALL(vkCmdSetStencilWriteMask(list->cmd.vk_command_buffer, VK_STENCIL_FACE_FRONT_BIT,
                (dyn_state->dsv_plane_write_enable & (1u << 1)) ? dyn_state->stencil_front.write_mask : 0));
        VK_CALL(vkCmdSetStencilWriteMask(list->cmd.vk_command_buffer, VK_STENCIL_FACE_BACK_BIT,
                (dyn_state->dsv_plane_write_enable & (1u << 1)) ? dyn_state->stencil_back.write_mask : 0));
    }

    if (dyn_state->dirty_flags & VKD3D_DYNAMIC_STATE_DEPTH_BOUNDS)
    {
        VK_CALL(vkCmdSetDepthBounds(list->cmd.vk_command_buffer,
                dyn_state->min_depth_bounds, dyn_state->max_depth_bounds));
    }

    if (dyn_state->dirty_flags & VKD3D_DYNAMIC_STATE_DEPTH_BIAS)
    {
        VK_CALL(vkCmdSetDepthBiasEnable(list->cmd.vk_command_buffer,
                dyn_state->depth_bias.constant_factor != 0.0f ||
                dyn_state->depth_bias.slope_factor != 0.0f));

        if (list->device->device_info.depth_bias_control_features.depthBiasControl)
        {
            vkd3d_get_depth_bias_representation(&depth_bias_representation, list->device,
                    list->dsv.format ? list->dsv.format->dxgi_format : DXGI_FORMAT_UNKNOWN);

            memset(&depth_bias_info, 0, sizeof(depth_bias_info));
            depth_bias_info.sType = VK_STRUCTURE_TYPE_DEPTH_BIAS_INFO_EXT;
            depth_bias_info.pNext = &depth_bias_representation;
            depth_bias_info.depthBiasConstantFactor = dyn_state->depth_bias.constant_factor;
            depth_bias_info.depthBiasSlopeFactor = dyn_state->depth_bias.slope_factor;
            depth_bias_info.depthBiasClamp = dyn_state->depth_bias.clamp;

            VK_CALL(vkCmdSetDepthBias2EXT(list->cmd.vk_command_buffer, &depth_bias_info));
        }
        else
        {
            VK_CALL(vkCmdSetDepthBias(list->cmd.vk_command_buffer,
                    dyn_state->depth_bias.constant_factor, dyn_state->depth_bias.clamp,
                    dyn_state->depth_bias.slope_factor));
        }
    }

    if (dyn_state->dirty_flags & VKD3D_DYNAMIC_STATE_TOPOLOGY)
    {
        VK_CALL(vkCmdSetPrimitiveTopology(list->cmd.vk_command_buffer,
                dyn_state->vk_primitive_topology));
    }

    if (dyn_state->dirty_flags & VKD3D_DYNAMIC_STATE_PATCH_CONTROL_POINTS)
    {
        VK_CALL(vkCmdSetPatchControlPointsEXT(list->cmd.vk_command_buffer,
                dyn_state->primitive_topology - D3D_PRIMITIVE_TOPOLOGY_1_CONTROL_POINT_PATCHLIST + 1));
    }

    if (dyn_state->dirty_flags & VKD3D_DYNAMIC_STATE_PRIMITIVE_RESTART)
    {
        VK_CALL(vkCmdSetPrimitiveRestartEnable(list->cmd.vk_command_buffer,
                dyn_state->index_buffer_strip_cut_value != D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_DISABLED &&
                vk_primitive_topology_supports_restart(dyn_state->vk_primitive_topology)));
    }

    if (dyn_state->dirty_flags & VKD3D_DYNAMIC_STATE_VERTEX_BUFFER_STRIDE)
    {
        update_vbos = dyn_state->dirty_vbos & list->state->graphics.vertex_buffer_mask;
        dyn_state->dirty_vbos &= ~update_vbos;
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

            VK_CALL(vkCmdBindVertexBuffers2(list->cmd.vk_command_buffer,
                    range.offset, range.count,
                    dyn_state->vertex_buffers + range.offset,
                    dyn_state->vertex_offsets + range.offset,
                    dyn_state->vertex_sizes + range.offset,
                    dyn_state->vertex_strides + range.offset));
        }
    }

    if (dyn_state->dirty_flags & VKD3D_DYNAMIC_STATE_FRAGMENT_SHADING_RATE)
    {
        VK_CALL(vkCmdSetFragmentShadingRateKHR(list->cmd.vk_command_buffer,
                &dyn_state->fragment_shading_rate.fragment_size,
                dyn_state->fragment_shading_rate.combiner_ops));
    }

    if (dyn_state->dirty_flags & VKD3D_DYNAMIC_STATE_RASTERIZATION_SAMPLES)
    {
        rasterization_samples = dyn_state->rasterization_samples;

        if (!rasterization_samples)
            rasterization_samples = list->state->graphics.ms_desc.rasterizationSamples;

        VK_CALL(vkCmdSetRasterizationSamplesEXT(list->cmd.vk_command_buffer, rasterization_samples));
    }

    dyn_state->dirty_flags = 0;
}

static void d3d12_command_list_promote_dsv_layout(struct d3d12_command_list *list)
{
    /* If we know at this point that the image is DSV optimal in some way, promote the layout
     * so that we can select the appropriate render pass right away and ignore any
     * read-state shenanigans. If we cannot promote yet, the pipeline will override dsv_layout as required
     * by write enable bits. */
    if (list->dsv_layout == VK_IMAGE_LAYOUT_UNDEFINED &&
            d3d12_pipeline_state_is_graphics(list->state) &&
            d3d12_command_list_has_depth_stencil_view(list) &&
            list->dsv.resource)
    {
        list->dsv_layout = d3d12_command_list_get_depth_stencil_resource_layout(list, list->dsv.resource,
                &list->dsv_plane_optimal_mask);
    }
}

static bool d3d12_command_list_fixup_null_xfb_buffers(struct d3d12_command_list *list, unsigned int count)
{
    struct vkd3d_scratch_allocation scratch;
    unsigned int i;

    memset(&scratch, 0, sizeof(scratch));

    for (i = 0; i < count; i++)
    {
        if (list->so_buffers[i])
            continue;

        /* We can't bind actual null buffers for transform feedback, so just allocate a minimal
         * amount of scratch memory. This is safe because out-of-bounds writes will be discarded.
         * We never expect to hit this path in practice, so don't try to be clever about caching
         * the allocation or anything, just make sure not to crash. */
        if (!scratch.buffer)
        {
            if (!d3d12_command_allocator_allocate_scratch_memory(list->allocator,
                    VKD3D_SCRATCH_POOL_KIND_DEVICE_STORAGE, VKD3D_NULL_BUFFER_SIZE, 4u, ~0u, &scratch))
            {
                ERR("Failed to allocate scratch memory for null xfb buffer.\n");
                return false;
            }
        }

        list->so_buffers[i] = scratch.buffer;
        list->so_buffer_offsets[i] = scratch.offset;
        list->so_buffer_sizes[i] = VKD3D_NULL_BUFFER_SIZE;
    }

    return true;
}

static bool d3d12_command_list_begin_render_pass(struct d3d12_command_list *list,
        enum vkd3d_pipeline_type pipeline_type)
{
    const struct vkd3d_vk_device_procs *vk_procs = &list->device->vk_procs;
    struct d3d12_graphics_pipeline_state *graphics;

    d3d12_command_list_end_transfer_batch(list);
    d3d12_command_list_flush_rtas_batch(list);

    d3d12_command_list_promote_dsv_layout(list);
    if (!d3d12_command_list_update_graphics_pipeline(list, pipeline_type))
        return false;
    if (!d3d12_command_list_update_rendering_info(list))
        return false;

    if (list->dynamic_state.dirty_flags)
        d3d12_command_list_update_dynamic_state(list);

    d3d12_command_list_update_descriptors(list);

    if (list->rendering_info.state_flags & VKD3D_RENDERING_ACTIVE)
    {
        d3d12_command_list_handle_active_queries(list, false);
        return true;
    }

    if (!(list->rendering_info.state_flags & VKD3D_RENDERING_SUSPENDED))
        d3d12_command_list_emit_render_pass_transition(list, VKD3D_RENDER_PASS_TRANSITION_MODE_BEGIN);

    d3d12_command_list_debug_mark_begin_region(list, "RenderPass");
    VK_CALL(vkCmdBeginRendering(list->cmd.vk_command_buffer, &list->rendering_info.info));

    list->rendering_info.state_flags |= VKD3D_RENDERING_ACTIVE;
    list->rendering_info.state_flags &= ~VKD3D_RENDERING_SUSPENDED;

    graphics = &list->state->graphics;
    if (graphics->xfb_buffer_count)
    {
        list->xfb_buffer_count = graphics->xfb_buffer_count;

        if (!d3d12_command_list_fixup_null_xfb_buffers(list, list->xfb_buffer_count))
            return false;

        VK_CALL(vkCmdBindTransformFeedbackBuffersEXT(list->cmd.vk_command_buffer, 0, list->xfb_buffer_count,
                list->so_buffers, list->so_buffer_offsets, list->so_buffer_sizes));
        VK_CALL(vkCmdBeginTransformFeedbackEXT(list->cmd.vk_command_buffer, 0, list->xfb_buffer_count,
                list->so_counter_buffers, list->so_counter_buffer_offsets));
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
            if (list->index_buffer.dxgi_format != DXGI_FORMAT_R16_UINT)
            {
                TRACE("Strip cut value 0xffff is not supported with index buffer format %#x.\n",
                      list->index_buffer.dxgi_format);
            }
            break;

        case D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_0xFFFFFFFF:
            if (list->index_buffer.dxgi_format != DXGI_FORMAT_R32_UINT)
            {
                TRACE("Strip cut value 0xffffffff is not supported with index buffer format %#x.\n",
                      list->index_buffer.dxgi_format);
            }
            break;

        default:
            break;
        }
    }
}

static bool d3d12_command_list_emit_multi_dispatch_indirect_count(struct d3d12_command_list *list,
        VkDeviceAddress indirect_args, uint32_t stride, uint32_t max_commands,
        VkDeviceAddress count_arg,
        struct vkd3d_scratch_allocation *scratch)
{
    const struct vkd3d_vk_device_procs *vk_procs = &list->device->vk_procs;
    struct vkd3d_multi_dispatch_indirect_info pipeline_info;
    struct vkd3d_multi_dispatch_indirect_args args;
    VkCommandBuffer vk_patch_cmd_buffer;
    VkMemoryBarrier2 vk_barrier;
    VkDependencyInfo dep_info;

    vkd3d_meta_get_multi_dispatch_indirect_pipeline(&list->device->meta_ops, &pipeline_info);

    if (!d3d12_command_allocator_allocate_scratch_memory(list->allocator,
            VKD3D_SCRATCH_POOL_KIND_DEVICE_STORAGE,
            sizeof(VkDispatchIndirectCommand) * max_commands, sizeof(uint32_t), ~0u, scratch))
        return false;

    d3d12_command_list_end_current_render_pass(list, false);
    d3d12_command_list_end_transfer_batch(list);

    d3d12_command_allocator_allocate_init_post_indirect_command_buffer(list->allocator, list);
    vk_patch_cmd_buffer = list->cmd.vk_init_commands_post_indirect_barrier;

    if (vk_patch_cmd_buffer == list->cmd.vk_command_buffer)
    {
        d3d12_command_list_invalidate_current_pipeline(list, true);
        d3d12_command_list_invalidate_root_parameters(list, &list->compute_bindings, true, &list->graphics_bindings);
    }
    else
    {
        list->cmd.indirect_meta->need_compute_to_indirect_barrier = true;
        list->cmd.indirect_meta->need_compute_to_cbv_barrier = true;
    }

    args.indirect_va = indirect_args;
    args.count_va = count_arg;
    args.output_va = scratch->va;
    args.stride_words = stride / sizeof(uint32_t);
    args.max_commands = max_commands;

    VK_CALL(vkCmdBindPipeline(vk_patch_cmd_buffer, VK_PIPELINE_BIND_POINT_COMPUTE,
            pipeline_info.vk_pipeline));
    VK_CALL(vkCmdPushConstants(vk_patch_cmd_buffer,
            pipeline_info.vk_pipeline_layout, VK_SHADER_STAGE_COMPUTE_BIT,
            0, sizeof(args), &args));

    VK_CALL(vkCmdDispatch(vk_patch_cmd_buffer,
            vkd3d_compute_workgroup_count(max_commands, vkd3d_meta_get_multi_dispatch_indirect_workgroup_size()),
            1, 1));

    if (vk_patch_cmd_buffer == list->cmd.vk_command_buffer)
    {
        memset(&dep_info, 0, sizeof(dep_info));
        dep_info.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        dep_info.memoryBarrierCount = 1;
        dep_info.pMemoryBarriers = &vk_barrier;

        memset(&vk_barrier, 0, sizeof(vk_barrier));
        vk_barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
        vk_barrier.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        vk_barrier.srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
        vk_barrier.dstStageMask = VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT;
        vk_barrier.dstAccessMask = VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT;

        VK_CALL(vkCmdPipelineBarrier2(vk_patch_cmd_buffer, &dep_info));
    }

    VKD3D_BREADCRUMB_COMMAND(EXECUTE_INDIRECT_PATCH_COMPUTE);
    return true;
}

static bool d3d12_command_list_emit_multi_dispatch_indirect_count_state(struct d3d12_command_list *list,
        struct d3d12_command_signature *signature,
        VkDeviceAddress indirect_args,
        uint32_t stride, uint32_t max_commands,
        VkDeviceAddress count_arg,
        struct vkd3d_scratch_allocation *dispatch_scratch,
        struct vkd3d_scratch_allocation *ubo_scratch)
{
    const struct vkd3d_vk_device_procs *vk_procs = &list->device->vk_procs;
    struct vkd3d_multi_dispatch_indirect_info pipeline_info;
    struct vkd3d_multi_dispatch_indirect_state_args args;
    struct vkd3d_scratch_allocation template_scratch;
    VkCommandBuffer vk_patch_cmd_buffer;
    VkMemoryBarrier2 vk_barrier;
    VkDependencyInfo dep_info;

    vkd3d_meta_get_multi_dispatch_indirect_state_pipeline(&list->device->meta_ops, &pipeline_info);

    if (!d3d12_command_allocator_allocate_scratch_memory(list->allocator,
            VKD3D_SCRATCH_POOL_KIND_UNIFORM_UPLOAD,
            D3D12_MAX_ROOT_COST * sizeof(uint32_t) +
                    sizeof(signature->state_template.compute.source_offsets),
            sizeof(uint32_t), ~0u, &template_scratch))
        return false;

    if (!d3d12_command_allocator_allocate_scratch_memory(list->allocator,
            VKD3D_SCRATCH_POOL_KIND_DEVICE_STORAGE,
            sizeof(VkDispatchIndirectCommand) * max_commands,
            sizeof(uint32_t), ~0u, dispatch_scratch))
        return false;

    if (!d3d12_command_allocator_allocate_scratch_memory(list->allocator,
            VKD3D_SCRATCH_POOL_KIND_DEVICE_STORAGE,
            (D3D12_MAX_ROOT_COST * sizeof(uint32_t)) * max_commands,
            D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT,
            ~0u, ubo_scratch))
        return false;

    d3d12_command_list_fetch_root_parameter_data(list, &list->compute_bindings, template_scratch.host_ptr);
    memcpy(void_ptr_offset(template_scratch.host_ptr, D3D12_MAX_ROOT_COST * sizeof(uint32_t)),
            signature->state_template.compute.source_offsets,
            sizeof(signature->state_template.compute.source_offsets));

    args.indirect_va = indirect_args;
    args.count_va = count_arg;
    args.dispatch_va = dispatch_scratch->va;
    args.root_parameters_va = ubo_scratch->va;
    args.root_parameter_template_va = template_scratch.va;
    args.stride_words = stride / sizeof(uint32_t);
    args.dispatch_offset_words = signature->state_template.compute.dispatch_offset_words;

    d3d12_command_allocator_allocate_init_post_indirect_command_buffer(list->allocator, list);
    vk_patch_cmd_buffer = list->cmd.vk_init_commands_post_indirect_barrier;

    if (vk_patch_cmd_buffer == list->cmd.vk_command_buffer)
    {
        d3d12_command_list_invalidate_current_pipeline(list, true);
        d3d12_command_list_invalidate_root_parameters(list, &list->compute_bindings, true, &list->graphics_bindings);
    }
    else
    {
        list->cmd.indirect_meta->need_compute_to_indirect_barrier = true;
        list->cmd.indirect_meta->need_compute_to_cbv_barrier = true;
    }

    VK_CALL(vkCmdBindPipeline(vk_patch_cmd_buffer, VK_PIPELINE_BIND_POINT_COMPUTE,
            pipeline_info.vk_pipeline));
    VK_CALL(vkCmdPushConstants(vk_patch_cmd_buffer,
            pipeline_info.vk_pipeline_layout, VK_SHADER_STAGE_COMPUTE_BIT,
            0, sizeof(args), &args));

    VK_CALL(vkCmdDispatch(vk_patch_cmd_buffer, max_commands, 1, 1));

    if (vk_patch_cmd_buffer == list->cmd.vk_command_buffer)
    {
        memset(&dep_info, 0, sizeof(dep_info));
        dep_info.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        dep_info.memoryBarrierCount = 1;
        dep_info.pMemoryBarriers = &vk_barrier;

        memset(&vk_barrier, 0, sizeof(vk_barrier));
        vk_barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
        vk_barrier.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        vk_barrier.srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
        vk_barrier.dstStageMask = VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        vk_barrier.dstAccessMask = VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_2_UNIFORM_READ_BIT;

        VK_CALL(vkCmdPipelineBarrier2(vk_patch_cmd_buffer, &dep_info));
    }

    VKD3D_BREADCRUMB_COMMAND(EXECUTE_INDIRECT_PATCH_STATE_COMPUTE);
    return true;
}

static bool d3d12_command_list_emit_predicated_command(struct d3d12_command_list *list,
        enum vkd3d_predicate_command_type command_type, VkDeviceAddress indirect_args,
        const union vkd3d_predicate_command_direct_args *direct_args, struct vkd3d_scratch_allocation *scratch)
{
    const struct vkd3d_vk_device_procs *vk_procs = &list->device->vk_procs;
    struct vkd3d_predicate_command_info pipeline_info;
    struct vkd3d_predicate_command_args args;
    VkCommandBuffer vk_patch_cmd_buffer;
    VkMemoryBarrier2 vk_barrier;
    VkDependencyInfo dep_info;

    vkd3d_meta_get_predicate_pipeline(&list->device->meta_ops, command_type, &pipeline_info);

    if (!d3d12_command_allocator_allocate_scratch_memory(list->allocator,
            VKD3D_SCRATCH_POOL_KIND_DEVICE_STORAGE,
            pipeline_info.data_size, sizeof(uint32_t), ~0u, scratch))
        return false;

    d3d12_command_allocator_allocate_init_post_indirect_command_buffer(list->allocator, list);
    vk_patch_cmd_buffer = list->cmd.vk_init_commands_post_indirect_barrier;

    if (vk_patch_cmd_buffer == list->cmd.vk_command_buffer)
        d3d12_command_list_end_current_render_pass(list, true);

    args.predicate_va = list->predication.va;
    args.dst_arg_va = scratch->va;
    args.src_arg_va = indirect_args;
    args.args = *direct_args;

    VK_CALL(vkCmdBindPipeline(vk_patch_cmd_buffer, VK_PIPELINE_BIND_POINT_COMPUTE,
            pipeline_info.vk_pipeline));
    VK_CALL(vkCmdPushConstants(vk_patch_cmd_buffer,
            pipeline_info.vk_pipeline_layout, VK_SHADER_STAGE_COMPUTE_BIT,
            0, sizeof(args), &args));
    VK_CALL(vkCmdDispatch(vk_patch_cmd_buffer, 1, 1, 1));

    if (vk_patch_cmd_buffer == list->cmd.vk_command_buffer)
    {
        memset(&vk_barrier, 0, sizeof(vk_barrier));
        vk_barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
        vk_barrier.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        vk_barrier.srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
        vk_barrier.dstStageMask = VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT;
        vk_barrier.dstAccessMask = VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT;

        memset(&dep_info, 0, sizeof(dep_info));
        dep_info.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        dep_info.memoryBarrierCount = 1;
        dep_info.pMemoryBarriers = &vk_barrier;

        VK_CALL(vkCmdPipelineBarrier2(list->cmd.vk_command_buffer, &dep_info));

        d3d12_command_list_invalidate_current_pipeline(list, true);
        d3d12_command_list_invalidate_root_parameters(list, &list->compute_bindings, true, &list->graphics_bindings);
    }
    else
        list->cmd.indirect_meta->need_compute_to_indirect_barrier = true;

    return true;
}

static void STDMETHODCALLTYPE d3d12_command_list_DrawInstanced(d3d12_command_list_iface *iface,
        UINT vertex_count_per_instance, UINT instance_count, UINT start_vertex_location,
        UINT start_instance_location)
{
    struct d3d12_command_list *list = impl_from_ID3D12GraphicsCommandList(iface);
    const struct vkd3d_vk_device_procs *vk_procs = &list->device->vk_procs;
    struct vkd3d_scratch_allocation scratch;

    TRACE("iface %p, vertex_count_per_instance %u, instance_count %u, "
            "start_vertex_location %u, start_instance_location %u.\n",
            iface, vertex_count_per_instance, instance_count,
            start_vertex_location, start_instance_location);

    if (list->predication.fallback_enabled)
    {
        union vkd3d_predicate_command_direct_args args;
        args.draw.vertexCount = vertex_count_per_instance;
        args.draw.instanceCount = instance_count;
        args.draw.firstVertex = start_vertex_location;
        args.draw.firstInstance = start_instance_location;

        if (!d3d12_command_list_emit_predicated_command(list, VKD3D_PREDICATE_COMMAND_DRAW, 0, &args, &scratch))
            return;
    }

    if (!d3d12_command_list_begin_render_pass(list, VKD3D_PIPELINE_TYPE_GRAPHICS))
    {
        WARN("Failed to begin render pass, ignoring draw call.\n");
        return;
    }

    list->cmd.estimated_cost += VKD3D_COMMAND_COST_HIGH;

    if (!list->predication.fallback_enabled)
        VK_CALL(vkCmdDraw(list->cmd.vk_command_buffer, vertex_count_per_instance,
                instance_count, start_vertex_location, start_instance_location));
    else
        VK_CALL(vkCmdDrawIndirect(list->cmd.vk_command_buffer, scratch.buffer, scratch.offset, 1, 0));

    VKD3D_BREADCRUMB_AUX32(vertex_count_per_instance);
    VKD3D_BREADCRUMB_AUX32(instance_count);
    VKD3D_BREADCRUMB_AUX32(start_vertex_location);
    VKD3D_BREADCRUMB_AUX32(start_instance_location);
    VKD3D_BREADCRUMB_COMMAND(DRAW);
}

static bool d3d12_command_list_update_index_buffer(struct d3d12_command_list *list)
{
    const struct vkd3d_vk_device_procs *vk_procs = &list->device->vk_procs;

    if (list->index_buffer.is_dirty)
    {
        if (!list->index_buffer.buffer)
        {
            if (list->device->device_info.maintenance_6_features.maintenance6)
            {
                VK_CALL(vkCmdBindIndexBuffer2KHR(list->cmd.vk_command_buffer, VK_NULL_HANDLE, 0, 0, VK_INDEX_TYPE_UINT16));
            }
            else
            {
                /* NULL index buffers are expected to behave as if all indices are 0. Since this is only
                 * observable in edge cases involving streamout or geometry shaders, skip the draw call
                 * for now if the Vulkan implementation does not support this. */
                FIXME_ONCE("Application attempts to perform an indexed draw call without index buffer bound.\n");
                return false;
            }
        }
        else if (list->device->device_info.maintenance_5_features.maintenance5)
        {
            VK_CALL(vkCmdBindIndexBuffer2KHR(list->cmd.vk_command_buffer, list->index_buffer.buffer,
                    list->index_buffer.offset, list->index_buffer.size, list->index_buffer.vk_type));
        }
        else
        {
            VK_CALL(vkCmdBindIndexBuffer(list->cmd.vk_command_buffer, list->index_buffer.buffer,
                    list->index_buffer.offset, list->index_buffer.vk_type));
        }
        list->index_buffer.is_dirty = false;
    }

    return true;
}

static void STDMETHODCALLTYPE d3d12_command_list_DrawIndexedInstanced(d3d12_command_list_iface *iface,
        UINT index_count_per_instance, UINT instance_count, UINT start_vertex_location,
        INT base_vertex_location, UINT start_instance_location)
{
    struct d3d12_command_list *list = impl_from_ID3D12GraphicsCommandList(iface);
    const struct vkd3d_vk_device_procs *vk_procs = &list->device->vk_procs;
    struct vkd3d_scratch_allocation scratch;

    TRACE("iface %p, index_count_per_instance %u, instance_count %u, start_vertex_location %u, "
            "base_vertex_location %d, start_instance_location %u.\n",
            iface, index_count_per_instance, instance_count, start_vertex_location,
            base_vertex_location, start_instance_location);

    if (!d3d12_command_list_update_index_buffer(list))
        return;

    if (list->predication.fallback_enabled)
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

    if (!d3d12_command_list_begin_render_pass(list, VKD3D_PIPELINE_TYPE_GRAPHICS))
    {
        WARN("Failed to begin render pass, ignoring draw call.\n");
        return;
    }

    list->cmd.estimated_cost += VKD3D_COMMAND_COST_HIGH;

    d3d12_command_list_check_index_buffer_strip_cut_value(list);

    if (!list->predication.fallback_enabled)
        VK_CALL(vkCmdDrawIndexed(list->cmd.vk_command_buffer, index_count_per_instance,
                instance_count, start_vertex_location, base_vertex_location, start_instance_location));
    else
        VK_CALL(vkCmdDrawIndexedIndirect(list->cmd.vk_command_buffer, scratch.buffer, scratch.offset, 1, 0));

    VKD3D_BREADCRUMB_AUX32(index_count_per_instance);
    VKD3D_BREADCRUMB_AUX32(instance_count);
    VKD3D_BREADCRUMB_AUX32(start_vertex_location);
    VKD3D_BREADCRUMB_AUX32(base_vertex_location);
    VKD3D_BREADCRUMB_AUX32(start_instance_location);
    VKD3D_BREADCRUMB_COMMAND(DRAW_INDEXED);
}

static void d3d12_command_list_check_pre_compute_barrier(struct d3d12_command_list *list)
{
    vkd3d_descriptor_debug_sync_validation_barrier(list->device->descriptor_qa_global_info,
            list->device, list->cmd.vk_command_buffer);

    if ((list->current_compute_meta_flags & (VKD3D_SHADER_META_FLAG_FORCE_GRAPHICS_BEFORE_DISPATCH |
            VKD3D_SHADER_META_FLAG_FORCE_PRE_RASTERIZATION_BEFORE_DISPATCH |
            VKD3D_SHADER_META_FLAG_FORCE_COMPUTE_BARRIER_BEFORE_DISPATCH)) || list->cmd.clear_uav_pending)
    {
        const struct vkd3d_vk_device_procs *vk_procs = &list->device->vk_procs;
        VKD3D_UNUSED uint32_t cookie;
        VkMemoryBarrier2 vk_barrier;
        VkDependencyInfo dep_info;

        memset(&vk_barrier, 0, sizeof(vk_barrier));
        memset(&dep_info, 0, sizeof(dep_info));
        vk_barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;

        if (list->current_compute_meta_flags & VKD3D_SHADER_META_FLAG_FORCE_GRAPHICS_BEFORE_DISPATCH)
        {
            vk_barrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT;
            vk_barrier.srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT |
                    VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT |
                    VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            vk_barrier.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
            vk_barrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
        }
        else if (list->current_compute_meta_flags & VKD3D_SHADER_META_FLAG_FORCE_PRE_RASTERIZATION_BEFORE_DISPATCH)
        {
            vk_barrier.srcStageMask = VK_PIPELINE_STAGE_2_PRE_RASTERIZATION_SHADERS_BIT;
            vk_barrier.srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
            vk_barrier.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
            vk_barrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
        }

        if ((list->current_compute_meta_flags & VKD3D_SHADER_META_FLAG_FORCE_COMPUTE_BARRIER_BEFORE_DISPATCH) || list->cmd.clear_uav_pending)
        {
            vk_barrier.srcStageMask |= VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
            vk_barrier.srcAccessMask |= VK_ACCESS_2_SHADER_WRITE_BIT;
            vk_barrier.dstStageMask |= VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
            vk_barrier.dstAccessMask |= VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT;
        }

        dep_info.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        dep_info.memoryBarrierCount = 1;
        dep_info.pMemoryBarriers = &vk_barrier;

        VK_CALL(vkCmdPipelineBarrier2(list->cmd.vk_command_buffer, &dep_info));
        VKD3D_BREADCRUMB_TAG("ForcePreBarrier");
        VKD3D_BREADCRUMB_COMMAND(BARRIER);
        d3d12_command_list_debug_mark_label(list, "ForcePreBarrier", 1.0f, 1.0f, 0.0f, 1.0f);

        list->current_compute_meta_flags &= ~(VKD3D_SHADER_META_FLAG_FORCE_GRAPHICS_BEFORE_DISPATCH |
                VKD3D_SHADER_META_FLAG_FORCE_PRE_RASTERIZATION_BEFORE_DISPATCH |
                VKD3D_SHADER_META_FLAG_FORCE_COMPUTE_BARRIER_BEFORE_DISPATCH);
        list->cmd.clear_uav_pending = false;

        cookie = vkd3d_descriptor_debug_clear_bloom_filter(list->device->descriptor_qa_global_info,
                list->device, list->cmd.vk_command_buffer);
        VKD3D_BREADCRUMB_AUX32(cookie);
        VKD3D_BREADCRUMB_COMMAND(SYNC_VAL_CLEAR);
    }
}

static void d3d12_command_list_check_compute_barrier(struct d3d12_command_list *list)
{
    bool force_barrier = !!(list->current_compute_meta_flags & VKD3D_SHADER_META_FLAG_FORCE_COMPUTE_BARRIER_AFTER_DISPATCH);

#ifdef VKD3D_ENABLE_BREADCRUMBS
    if (vkd3d_breadcrumb_tracer_shader_hash_forces_barrier(&list->device->breadcrumb_tracer,
            list->state->compute.code.meta.hash) & VKD3D_SHADER_META_FLAG_FORCE_COMPUTE_BARRIER_AFTER_DISPATCH)
    {
        force_barrier = true;
    }
#endif

    if (force_barrier)
    {
        const struct vkd3d_vk_device_procs *vk_procs = &list->device->vk_procs;
        VKD3D_UNUSED uint32_t cookie;
        VkMemoryBarrier2 vk_barrier;
        VkDependencyInfo dep_info;

        memset(&vk_barrier, 0, sizeof(vk_barrier));
        memset(&dep_info, 0, sizeof(dep_info));
        vk_barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
        vk_barrier.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        vk_barrier.srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
        vk_barrier.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        vk_barrier.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;

        dep_info.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        dep_info.memoryBarrierCount = 1;
        dep_info.pMemoryBarriers = &vk_barrier;

        VK_CALL(vkCmdPipelineBarrier2(list->cmd.vk_command_buffer, &dep_info));
        VKD3D_BREADCRUMB_TAG("ForceBarrier");
        VKD3D_BREADCRUMB_COMMAND(BARRIER);
        d3d12_command_list_debug_mark_label(list, "ForcePostBarrier", 1.0f, 1.0f, 0.0f, 1.0f);

        cookie = vkd3d_descriptor_debug_clear_bloom_filter(list->device->descriptor_qa_global_info,
                list->device, list->cmd.vk_command_buffer);
        VKD3D_BREADCRUMB_AUX32(cookie);
        VKD3D_BREADCRUMB_COMMAND(SYNC_VAL_CLEAR);
    }

    vkd3d_descriptor_debug_sync_validation_barrier(list->device->descriptor_qa_global_info,
            list->device, list->cmd.vk_command_buffer);
}

static void STDMETHODCALLTYPE d3d12_command_list_Dispatch(d3d12_command_list_iface *iface,
        UINT x, UINT y, UINT z)
{
    struct d3d12_command_list *list = impl_from_ID3D12GraphicsCommandList(iface);
    const struct vkd3d_vk_device_procs *vk_procs = &list->device->vk_procs;
    struct vkd3d_scratch_allocation scratch;

    TRACE("iface %p, x %u, y %u, z %u.\n", iface, x, y, z);

    if (list->is_inside_render_pass)
        d3d12_command_list_mark_as_invalid(list, "Dispatch called within a render pass.\n");

    if (list->predication.fallback_enabled)
    {
        union vkd3d_predicate_command_direct_args args;
        args.dispatch.x = x;
        args.dispatch.y = y;
        args.dispatch.z = z;

        if (!d3d12_command_list_emit_predicated_command(list, VKD3D_PREDICATE_COMMAND_DISPATCH, 0, &args, &scratch))
            return;
    }

    d3d12_command_list_end_transfer_batch(list);

    if (!d3d12_command_list_update_compute_state(list))
    {
        WARN("Failed to update compute state, ignoring dispatch.\n");
        return;
    }

    list->cmd.estimated_cost += VKD3D_COMMAND_COST_HIGH;

    if (!list->predication.fallback_enabled)
        VK_CALL(vkCmdDispatch(list->cmd.vk_command_buffer, x, y, z));
    else
        VK_CALL(vkCmdDispatchIndirect(list->cmd.vk_command_buffer, scratch.buffer, scratch.offset));

    VKD3D_BREADCRUMB_AUX32(x);
    VKD3D_BREADCRUMB_AUX32(y);
    VKD3D_BREADCRUMB_AUX32(z);
    VKD3D_BREADCRUMB_COMMAND(DISPATCH);

    d3d12_command_list_check_compute_barrier(list);
}

static void STDMETHODCALLTYPE d3d12_command_list_CopyBufferRegion(d3d12_command_list_iface *iface,
        ID3D12Resource *dst, UINT64 dst_offset, ID3D12Resource *src, UINT64 src_offset, UINT64 byte_count)
{
    struct d3d12_command_list *list = impl_from_ID3D12GraphicsCommandList(iface);
    struct d3d12_resource *dst_resource, *src_resource;
    const struct vkd3d_vk_device_procs *vk_procs;
    VkCopyBufferInfo2 copy_info;
    VkBufferCopy2 buffer_copy;

    TRACE("iface %p, dst_resource %p, dst_offset %#"PRIx64", src_resource %p, "
            "src_offset %#"PRIx64", byte_count %#"PRIx64".\n",
            iface, dst, dst_offset, src, src_offset, byte_count);

    if (list->is_inside_render_pass)
        d3d12_command_list_mark_as_invalid(list, "CopyBufferRegion called within a render pass.\n");

    list->cmd.estimated_cost += VKD3D_COMMAND_COST_LOW;

    vk_procs = &list->device->vk_procs;

    dst_resource = impl_from_ID3D12Resource(dst);
    assert(d3d12_resource_is_buffer(dst_resource));
    src_resource = impl_from_ID3D12Resource(src);
    assert(d3d12_resource_is_buffer(src_resource));

    d3d12_command_list_track_resource_usage(list, dst_resource, true);
    d3d12_command_list_track_resource_usage(list, src_resource, true);

    d3d12_command_list_end_current_render_pass(list, true);
    d3d12_command_list_end_transfer_batch(list);

    buffer_copy.sType = VK_STRUCTURE_TYPE_BUFFER_COPY_2;
    buffer_copy.pNext = NULL;
    buffer_copy.srcOffset = src_offset + src_resource->mem.offset;
    buffer_copy.dstOffset = dst_offset + dst_resource->mem.offset;
    buffer_copy.size = byte_count;

    copy_info.sType = VK_STRUCTURE_TYPE_COPY_BUFFER_INFO_2;
    copy_info.pNext = NULL;
    copy_info.srcBuffer = src_resource->res.vk_buffer;
    copy_info.dstBuffer = dst_resource->res.vk_buffer;
    copy_info.regionCount = 1;
    copy_info.pRegions = &buffer_copy;

    VKD3D_BREADCRUMB_TAG("Buffer -> Buffer");
    VKD3D_BREADCRUMB_RESOURCE(src_resource);
    VKD3D_BREADCRUMB_RESOURCE(dst_resource);
    VKD3D_BREADCRUMB_BUFFER_COPY(&buffer_copy);

    d3d12_command_list_mark_copy_buffer_write(list, copy_info.dstBuffer, buffer_copy.dstOffset, buffer_copy.size,
            !!(dst_resource->flags & VKD3D_RESOURCE_RESERVED));
    VK_CALL(vkCmdCopyBuffer2(list->cmd.vk_command_buffer, &copy_info));

    VKD3D_BREADCRUMB_COMMAND(COPY);
}

VkImageSubresourceLayers vk_image_subresource_layers_from_d3d12(
        const struct vkd3d_format *format, unsigned int sub_resource_idx,
        unsigned int miplevel_count, unsigned int layer_count)
{
    VkImageSubresourceLayers layers;
    VkImageSubresource sub;

    sub = vk_image_subresource_from_d3d12(format, sub_resource_idx, miplevel_count, layer_count, false);

    layers.aspectMask = sub.aspectMask;
    layers.mipLevel = sub.mipLevel;
    layers.baseArrayLayer = sub.arrayLayer;
    layers.layerCount = 1;
    return layers;
}

static void vk_buffer_image_copy_from_d3d12(VkBufferImageCopy2 *copy,
        const D3D12_PLACED_SUBRESOURCE_FOOTPRINT *footprint, unsigned int sub_resource_idx,
        const D3D12_RESOURCE_DESC1 *image_desc, const struct vkd3d_format *src_format,
        const struct vkd3d_format *dst_format, const D3D12_BOX *src_box, unsigned int dst_x,
        unsigned int dst_y, unsigned int dst_z)
{
    copy->bufferOffset = footprint->Offset;
    if (src_box)
    {
        VkDeviceSize row_count = footprint->Footprint.Height / src_format->block_height;
        copy->bufferOffset += vkd3d_format_get_data_offset(src_format, footprint->Footprint.RowPitch,
                row_count * footprint->Footprint.RowPitch, src_box->left, src_box->top, src_box->front);
    }
    copy->bufferRowLength = footprint->Footprint.RowPitch /
            (src_format->byte_count * src_format->block_byte_count) * src_format->block_width;
    copy->bufferImageHeight = footprint->Footprint.Height;
    copy->imageSubresource = vk_image_subresource_layers_from_d3d12(
            dst_format, sub_resource_idx, image_desc->MipLevels,
            d3d12_resource_desc_get_layer_count(image_desc));
    copy->imageOffset.x = dst_x;
    copy->imageOffset.y = dst_y;
    copy->imageOffset.z = dst_z;

    copy->imageExtent = d3d12_resource_desc_get_vk_subresource_extent(image_desc, dst_format, &copy->imageSubresource);
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

static void vk_image_buffer_copy_from_d3d12(VkBufferImageCopy2 *copy,
        const D3D12_PLACED_SUBRESOURCE_FOOTPRINT *footprint, unsigned int sub_resource_idx,
        const D3D12_RESOURCE_DESC1 *image_desc,
        const struct vkd3d_format *src_format, const struct vkd3d_format *dst_format,
        const D3D12_BOX *src_box, unsigned int dst_x, unsigned int dst_y, unsigned int dst_z)
{
    VkDeviceSize row_count = footprint->Footprint.Height / dst_format->block_height;

    copy->bufferOffset = footprint->Offset + vkd3d_format_get_data_offset(dst_format,
            footprint->Footprint.RowPitch, row_count * footprint->Footprint.RowPitch, dst_x, dst_y, dst_z);
    copy->bufferRowLength = footprint->Footprint.RowPitch /
            (dst_format->byte_count * dst_format->block_byte_count) * dst_format->block_width;
    copy->bufferImageHeight = footprint->Footprint.Height;
    copy->imageSubresource = vk_image_subresource_layers_from_d3d12(
            src_format, sub_resource_idx, image_desc->MipLevels,
            d3d12_resource_desc_get_layer_count(image_desc));
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
        copy->imageExtent = d3d12_resource_desc_get_vk_subresource_extent(image_desc, src_format, &copy->imageSubresource);
    }
}

static bool vk_image_copy_from_d3d12(VkImageCopy2 *image_copy,
        unsigned int src_sub_resource_idx, unsigned int dst_sub_resource_idx,
        const D3D12_RESOURCE_DESC1 *src_desc, const D3D12_RESOURCE_DESC1 *dst_desc,
        const struct vkd3d_format *src_format, const struct vkd3d_format *dst_format,
        const D3D12_BOX *src_box, unsigned int dst_x, unsigned int dst_y, unsigned int dst_z)
{
    VkExtent3D dst_extent, src_extent, src_blocks, src_raw_extent;
    VkOffset3D dst_offset, src_offset;

    memset(image_copy, 0, sizeof(*image_copy));
    image_copy->sType = VK_STRUCTURE_TYPE_IMAGE_COPY_2;
    image_copy->srcSubresource = vk_image_subresource_layers_from_d3d12(
            src_format, src_sub_resource_idx, src_desc->MipLevels,
            d3d12_resource_desc_get_layer_count(src_desc));
    image_copy->dstSubresource = vk_image_subresource_layers_from_d3d12(
            dst_format, dst_sub_resource_idx, dst_desc->MipLevels,
            d3d12_resource_desc_get_layer_count(dst_desc));

    /* Perform all math on units of compressed blocks in order to ensure correct
     * behaviour when copying between compressed and non-compressed image formats */
    dst_offset.x = dst_x;
    dst_offset.y = dst_y;
    dst_offset.z = dst_z;

    dst_offset = vkd3d_compute_block_offset(dst_offset, dst_format);

    src_raw_extent = d3d12_resource_desc_get_vk_subresource_extent(
            src_desc, src_format, &image_copy->srcSubresource);

    src_extent = vkd3d_compute_block_count(src_raw_extent, src_format);
    dst_extent = vkd3d_compute_block_count(d3d12_resource_desc_get_vk_subresource_extent(
              dst_desc, dst_format, &image_copy->dstSubresource), dst_format);

    dst_extent.width -= dst_offset.x;
    dst_extent.height -= dst_offset.y;
    dst_extent.depth -= dst_offset.z;

    if (src_box)
    {
        src_blocks.width = max(src_box->right, src_box->left) - src_box->left;
        src_blocks.height = max(src_box->bottom, src_box->top) - src_box->top;
        src_blocks.depth = max(src_box->back, src_box->front) - src_box->front;

        src_blocks = vkd3d_compute_block_count(src_blocks, src_format);

        src_offset.x = src_box->left;
        src_offset.y = src_box->top;
        src_offset.z = src_box->front;

        src_offset = vkd3d_compute_block_offset(src_offset, src_format);

        src_offset.x = min(src_offset.x, (int32_t)src_extent.width);
        src_offset.y = min(src_offset.y, (int32_t)src_extent.height);
        src_offset.z = min(src_offset.z, (int32_t)src_extent.depth);

        src_extent.width -= src_offset.x;
        src_extent.height -= src_offset.y;
        src_extent.depth -= src_offset.z;

        src_extent.width = min(src_blocks.width, src_extent.width);
        src_extent.height = min(src_blocks.height, src_extent.height);
        src_extent.depth = min(src_blocks.depth, src_extent.depth);
    }
    else
    {
        src_offset.x = 0;
        src_offset.y = 0;
        src_offset.z = 0;
    }

    src_extent.width = min(src_extent.width, dst_extent.width);
    src_extent.height = min(src_extent.height, dst_extent.height);
    src_extent.depth = min(src_extent.depth, dst_extent.depth);

    /* Valid usage (06668, 06669, 06670) states that degenerate copy is not allowed.
     * Can happen if the clipped copy rect is out of bounds.
     * This is not allowed in D3D12 either, but e.g. Witcher 3 next-gen update can hit invalid scenarios. */
    if (!min(min(src_extent.width, src_extent.height), src_extent.depth))
        return false;

    image_copy->srcOffset = vkd3d_compute_texel_offset_from_blocks(src_offset, src_format);
    image_copy->dstOffset = vkd3d_compute_texel_offset_from_blocks(dst_offset, dst_format);

    /* Ensure that source offset + extent are within bounds of the source mip */
    src_raw_extent.width -= image_copy->srcOffset.x;
    src_raw_extent.height -= image_copy->srcOffset.y;

    image_copy->extent = vkd3d_compute_texel_count_from_blocks(src_extent, src_format);
    image_copy->extent.width = min(image_copy->extent.width, src_raw_extent.width);
    image_copy->extent.height = min(image_copy->extent.height, src_raw_extent.height);
    return true;
}

static void d3d12_command_list_transition_image_layout_with_global_memory_barrier(struct d3d12_command_list *list,
        struct d3d12_command_list_barrier_batch *batch,
        VkImage vk_image, const VkImageSubresourceLayers *vk_subresource,
        VkPipelineStageFlags2 src_stages, VkAccessFlags2 src_access, VkImageLayout old_layout,
        VkPipelineStageFlags2 dst_stages, VkAccessFlags2 dst_access, VkImageLayout new_layout,
        VkAccessFlags2 global_src_access, VkAccessFlags2 global_dst_access)
{
    VkImageMemoryBarrier2 vk_barrier;
    bool need_global_barrier;

    memset(&vk_barrier, 0, sizeof(vk_barrier));
    vk_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    vk_barrier.srcStageMask = src_stages;
    vk_barrier.srcAccessMask = src_access;
    vk_barrier.dstStageMask = dst_stages;
    vk_barrier.dstAccessMask = dst_access;
    vk_barrier.oldLayout = old_layout;
    vk_barrier.newLayout = new_layout;
    vk_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    vk_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    vk_barrier.image = vk_image;
    vk_barrier.subresourceRange = vk_subresource_range_from_layers(vk_subresource);

    need_global_barrier = global_src_access || global_dst_access;

    if (need_global_barrier)
    {
        d3d12_command_list_barrier_batch_add_global_transition(list, batch,
                src_stages, global_src_access, dst_stages, global_dst_access);
    }

    d3d12_command_list_barrier_batch_add_layout_transition(list, batch, &vk_barrier);
}

static void d3d12_command_list_transition_image_layout(struct d3d12_command_list *list,
        struct d3d12_command_list_barrier_batch *batch,
        VkImage vk_image, const VkImageSubresourceLayers *vk_subresource,
        VkPipelineStageFlags2 src_stages, VkAccessFlags2 src_access, VkImageLayout old_layout,
        VkPipelineStageFlags2 dst_stages, VkAccessFlags2 dst_access, VkImageLayout new_layout)
{
    d3d12_command_list_transition_image_layout_with_global_memory_barrier(list, batch,
            vk_image, vk_subresource,
            src_stages, src_access, old_layout,
            dst_stages, dst_access, new_layout,
            0, 0);
}

struct d3d12_image_copy_barrier
{
    /* Barrier flags that apply during the copy pass */
    struct
    {
        VkPipelineStageFlagBits2 stages;
        VkAccessFlagBits2 access;
        VkImageLayout layout;
    } src, dst;

    bool use_copy;
    bool dst_is_depth_stencil;
};

/* maintenance8 enables copies between:
    • 32-bit depth (VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT)
        ◦ VK_FORMAT_R32_SFLOAT, VK_FORMAT_R32_SINT, VK_FORMAT_R32_UINT
    • 24-bit depth (VK_FORMAT_X8_D24_UNORM_PACK32, VK_FORMAT_D24_UNORM_S8_UINT)
        ◦ VK_FORMAT_R32_SFLOAT, VK_FORMAT_R32_SINT, VK_FORMAT_R32_UINT
    • 16-bit depth (VK_FORMAT_D16_UNORM, VK_FORMAT_D16_UNORM_S8_UINT)
        ◦ VK_FORMAT_R16_SFLOAT, VK_FORMAT_R16_UNORM, VK_FORMAT_R16_SNORM, VK_FORMAT_R16_UINT, VK_FORMAT_R16_SINT
    • 8-bit stencil (VK_FORMAT_S8_UINT, VK_FORMAT_D16_UNORM_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT, VK_FORMAT_D32_SFLOAT_S8_UINT)
        ◦ VK_FORMAT_R8_UINT, VK_FORMAT_R8_SINT, VK_FORMAT_R8_UNORM, VK_FORMAT_R8_SNORM
 
 * assume if one format is depth/stencil and the other is color then it is allowed
 */
static bool d3d12_command_list_check_ds_color_copy_compatibility(struct d3d12_command_list *list,
    const struct vkd3d_format *dst_format, const struct vkd3d_format *src_format)
{
    VkImageAspectFlags ds_bits = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
    const struct vkd3d_format *ds_format = dst_format->vk_aspect_mask & ds_bits ? dst_format : src_format;
    const struct vkd3d_format *color_format = ds_format == dst_format ? src_format : dst_format;

    /* TODO: maintenance8 only allows GRAPHICS queue DS<->COLOR, but future extensions may allow on other queues */
    if (list->type != D3D12_COMMAND_LIST_TYPE_DIRECT || !list->device->device_info.maintenance_8_features.maintenance8)
        return false;

    /* ensure formats detected as expected */
    if ((ds_format->vk_aspect_mask & ds_bits) == 0 ||
        (color_format->vk_aspect_mask & VK_IMAGE_ASPECT_COLOR_BIT) == 0)
        return false;

    return true;
}

static void d3d12_command_list_copy_image_barrier_flags(struct d3d12_command_list *list,
        struct d3d12_image_copy_barrier *barrier,
        const struct d3d12_resource *dst_resource, const struct vkd3d_format *dst_format,
        const struct d3d12_resource *src_resource, const struct vkd3d_format *src_format,
        const VkImageCopy2 *region, bool overlapping_subresource)
{
    /* Individual aspects of planar images are treated as-if they were using a view-compatible
     * format, so we can copy between planes and color images as long as the formats are of the
     * same size. */
    static const VkImageAspectFlags compatible_aspects = VK_IMAGE_ASPECT_COLOR_BIT |
            VK_IMAGE_ASPECT_PLANE_0_BIT | VK_IMAGE_ASPECT_PLANE_1_BIT | VK_IMAGE_ASPECT_PLANE_2_BIT;

    barrier->use_copy = dst_format->vk_aspect_mask == src_format->vk_aspect_mask ||
            ((dst_format->vk_aspect_mask & compatible_aspects) && (src_format->vk_aspect_mask & compatible_aspects)) ||
            d3d12_command_list_check_ds_color_copy_compatibility(list, dst_format, src_format);
    barrier->dst_is_depth_stencil = !!(dst_format->vk_aspect_mask & (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT));

    if (barrier->use_copy)
    {
        if (overlapping_subresource)
        {
            barrier->src.layout = VK_IMAGE_LAYOUT_GENERAL;
            barrier->dst.layout = VK_IMAGE_LAYOUT_GENERAL;
        }
        else
        {
            barrier->src.layout = d3d12_resource_pick_layout(src_resource, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
            barrier->dst.layout = d3d12_resource_pick_layout(dst_resource, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        }

        barrier->src.stages = VK_PIPELINE_STAGE_2_COPY_BIT;
        barrier->src.access = VK_ACCESS_2_TRANSFER_READ_BIT;
        barrier->dst.stages = VK_PIPELINE_STAGE_2_COPY_BIT;
        barrier->dst.access = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    }
    else
    {
        barrier->src.layout = d3d12_resource_pick_layout(src_resource, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        barrier->src.stages = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
        barrier->src.access = VK_ACCESS_2_SHADER_READ_BIT;

        if (barrier->dst_is_depth_stencil)
        {
            /* We will only promote one aspect out of common layout. */
            if (region->dstSubresource.aspectMask == VK_IMAGE_ASPECT_DEPTH_BIT)
            {
                barrier->dst.layout = dst_resource->common_layout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL ?
                        VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
                        : VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_STENCIL_READ_ONLY_OPTIMAL;
            }
            else if (region->dstSubresource.aspectMask == VK_IMAGE_ASPECT_STENCIL_BIT)
            {
                barrier->dst.layout = dst_resource->common_layout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL ?
                        VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
                        : VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_STENCIL_ATTACHMENT_OPTIMAL;
            }
            else
                barrier->dst.layout = d3d12_resource_pick_layout(dst_resource, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);

            barrier->dst.stages = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
            barrier->dst.access = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        }
        else
        {
            barrier->dst.layout = d3d12_resource_pick_layout(dst_resource, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
            barrier->dst.stages = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
            barrier->dst.access = VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
        }
    }
}

static void d3d12_command_list_copy_image_transition_images(struct d3d12_command_list *list,
        struct d3d12_command_list_barrier_batch *batch,
        struct d3d12_resource *dst_resource, const struct vkd3d_format *dst_format,
        struct d3d12_resource *src_resource, const struct vkd3d_format *src_format,
        const VkImageCopy2 *region, bool writes_full_subresource, bool overlapping_subresource)
{
    struct d3d12_image_copy_barrier barrier;
    VkAccessFlags2 dst_copy_pass_access;

    d3d12_command_list_copy_image_barrier_flags(list, &barrier,
        dst_resource, dst_format,
        src_resource, src_format,
        region, overlapping_subresource);

    dst_copy_pass_access = barrier.dst.access;
    if (overlapping_subresource)
        dst_copy_pass_access |= barrier.src.access;

    d3d12_command_list_transition_image_layout(list, batch, dst_resource->res.vk_image,
        &region->dstSubresource,
        VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_NONE,
        writes_full_subresource ? VK_IMAGE_LAYOUT_UNDEFINED : dst_resource->common_layout,
        barrier.dst.stages, dst_copy_pass_access, barrier.dst.layout);

    if (!overlapping_subresource)
    {
        d3d12_command_list_transition_image_layout(list, batch, src_resource->res.vk_image,
            &region->srcSubresource, VK_PIPELINE_STAGE_2_COPY_BIT,
            VK_ACCESS_2_NONE, src_resource->common_layout,
            barrier.src.stages, barrier.src.access, barrier.src.layout);
    }
}

static void d3d12_command_list_copy_image(struct d3d12_command_list *list,
        struct d3d12_command_list_barrier_batch *batch,
        struct d3d12_resource *dst_resource, const struct vkd3d_format *dst_format,
        struct d3d12_resource *src_resource, const struct vkd3d_format *src_format,
        const VkImageCopy2 *region, bool writes_full_subresource, bool overlapping_subresource)
{
    const struct vkd3d_vk_device_procs *vk_procs = &list->device->vk_procs;
    struct vkd3d_texture_view_desc dst_view_desc, src_view_desc;
    struct vkd3d_copy_image_pipeline_key pipeline_key;
    struct vkd3d_copy_image_info pipeline_info;
    VkRenderingAttachmentInfo attachment_info;
    VkWriteDescriptorSet vk_descriptor_write;
    struct d3d12_image_copy_barrier barrier;
    struct vkd3d_copy_image_args push_args;
    struct vkd3d_view *dst_view, *src_view;
    VkDescriptorImageInfo vk_image_info;
    VkRenderingInfo rendering_info;
    VkCopyImageInfo2 copy_info;
    VkViewport viewport;
    unsigned int i;
    HRESULT hr;

    d3d12_command_list_copy_image_barrier_flags(list, &barrier,
        dst_resource, dst_format,
        src_resource, src_format,
        region, overlapping_subresource);

    VKD3D_BREADCRUMB_TAG("Image -> Image");
    VKD3D_BREADCRUMB_RESOURCE(src_resource);
    VKD3D_BREADCRUMB_RESOURCE(dst_resource);
    VKD3D_BREADCRUMB_IMAGE_COPY(region);

    if (barrier.use_copy)
    {
        copy_info.sType = VK_STRUCTURE_TYPE_COPY_IMAGE_INFO_2;
        copy_info.pNext = NULL;
        copy_info.srcImage = src_resource->res.vk_image;
        copy_info.srcImageLayout = barrier.src.layout;
        copy_info.dstImage = dst_resource->res.vk_image;
        copy_info.dstImageLayout = barrier.dst.layout;
        copy_info.regionCount = 1;
        copy_info.pRegions = region;

        VK_CALL(vkCmdCopyImage2(list->cmd.vk_command_buffer, &copy_info));
    }
    else
    {
        VKD3D_BREADCRUMB_TAG("CopyWithRenderpass");

        dst_view = src_view = NULL;

        if (!(dst_format = vkd3d_meta_get_copy_image_attachment_format(&list->device->meta_ops, dst_format, src_format,
                region->dstSubresource.aspectMask,
                region->srcSubresource.aspectMask)))
        {
            ERR("No attachment format found for source format %u.\n", src_format->vk_format);
            goto cleanup;
        }

        memset(&pipeline_key, 0, sizeof(pipeline_key));
        pipeline_key.format = dst_format;
        pipeline_key.view_type = vkd3d_meta_get_copy_image_view_type(dst_resource->desc.Dimension);
        pipeline_key.sample_count = vk_samples_from_dxgi_sample_desc(&dst_resource->desc.SampleDesc);
        pipeline_key.dst_aspect_mask = region->dstSubresource.aspectMask;

        if (FAILED(hr = vkd3d_meta_get_copy_image_pipeline(&list->device->meta_ops, &pipeline_key, &pipeline_info)))
        {
            ERR("Failed to obtain pipeline, format %u, view_type %u, sample_count %u.\n",
                    pipeline_key.format->vk_format, pipeline_key.view_type, pipeline_key.sample_count);
            goto cleanup;
        }

        d3d12_command_list_invalidate_current_pipeline(list, true);
        d3d12_command_list_invalidate_root_parameters(list, &list->graphics_bindings, true, &list->compute_bindings);
        d3d12_command_list_update_descriptor_buffers(list);

        memset(&dst_view_desc, 0, sizeof(dst_view_desc));
        dst_view_desc.image = dst_resource->res.vk_image;
        dst_view_desc.view_type = pipeline_key.view_type;
        dst_view_desc.format = dst_format;
        dst_view_desc.miplevel_idx = region->dstSubresource.mipLevel;
        dst_view_desc.miplevel_count = 1;
        dst_view_desc.layer_idx = region->dstSubresource.baseArrayLayer;
        dst_view_desc.layer_count = region->dstSubresource.layerCount;
        /* A render pass must cover all depth-stencil aspects. */
        dst_view_desc.aspect_mask = dst_resource->format->vk_aspect_mask;
        dst_view_desc.image_usage = (pipeline_key.dst_aspect_mask & VK_IMAGE_ASPECT_COLOR_BIT) ?
                VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT : VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        dst_view_desc.allowed_swizzle = false;

        memset(&src_view_desc, 0, sizeof(src_view_desc));
        src_view_desc.image = src_resource->res.vk_image;
        src_view_desc.view_type = pipeline_key.view_type;
        src_view_desc.format = src_format;
        src_view_desc.miplevel_idx = region->srcSubresource.mipLevel;
        src_view_desc.miplevel_count = 1;
        src_view_desc.layer_idx = region->srcSubresource.baseArrayLayer;
        src_view_desc.layer_count = region->srcSubresource.layerCount;
        src_view_desc.aspect_mask = region->srcSubresource.aspectMask;
        src_view_desc.image_usage = VK_IMAGE_USAGE_SAMPLED_BIT;
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

        memset(&attachment_info, 0, sizeof(attachment_info));
        attachment_info.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        attachment_info.imageView = dst_view->vk_image_view;
        attachment_info.imageLayout = barrier.dst.layout;
        attachment_info.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachment_info.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

        if (pipeline_info.needs_stencil_mask)
            attachment_info.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;

        memset(&rendering_info, 0, sizeof(rendering_info));
        rendering_info.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        rendering_info.renderArea.offset.x = region->dstOffset.x;
        rendering_info.renderArea.offset.y = region->dstOffset.y;
        rendering_info.renderArea.extent.width = region->extent.width;
        rendering_info.renderArea.extent.height = region->extent.height;
        rendering_info.layerCount = dst_view_desc.layer_count;

        if (region->dstSubresource.aspectMask & VK_IMAGE_ASPECT_COLOR_BIT)
        {
            rendering_info.colorAttachmentCount = 1;
            rendering_info.pColorAttachments = &attachment_info;
        }
        else
        {
            /* It is valid to not render to DS aspects we don't want to touch.
             * Otherwise, we will falsely discard aspects with LOAD_OP_DONT_CARE. */
            if (region->dstSubresource.aspectMask & VK_IMAGE_ASPECT_DEPTH_BIT)
                rendering_info.pDepthAttachment = &attachment_info;
            if (region->dstSubresource.aspectMask & VK_IMAGE_ASPECT_STENCIL_BIT)
                rendering_info.pStencilAttachment = &attachment_info;
        }

        viewport.x = (float)region->dstOffset.x;
        viewport.y = (float)region->dstOffset.y;
        viewport.width = (float)region->extent.width;
        viewport.height = (float)region->extent.height;
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;

        memset(&push_args, 0, sizeof(push_args));
        push_args.offset.x = region->srcOffset.x - region->dstOffset.x;
        push_args.offset.y = region->srcOffset.y - region->dstOffset.y;

        vk_image_info.sampler = VK_NULL_HANDLE;
        vk_image_info.imageView = src_view->vk_image_view;
        vk_image_info.imageLayout = barrier.src.layout;

        vk_descriptor_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        vk_descriptor_write.pNext = NULL;
        vk_descriptor_write.dstSet = VK_NULL_HANDLE;
        vk_descriptor_write.dstBinding = 0;
        vk_descriptor_write.dstArrayElement = 0;
        vk_descriptor_write.descriptorCount = 1;
        vk_descriptor_write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        vk_descriptor_write.pImageInfo = &vk_image_info;
        vk_descriptor_write.pBufferInfo = NULL;
        vk_descriptor_write.pTexelBufferView = NULL;

        d3d12_command_list_debug_mark_begin_region(list, "CopyRenderPass");
        VK_CALL(vkCmdBeginRendering(list->cmd.vk_command_buffer, &rendering_info));
        VK_CALL(vkCmdBindPipeline(list->cmd.vk_command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_info.vk_pipeline));
        VK_CALL(vkCmdSetViewport(list->cmd.vk_command_buffer, 0, 1, &viewport));
        VK_CALL(vkCmdSetScissor(list->cmd.vk_command_buffer, 0, 1, &rendering_info.renderArea));
        VK_CALL(vkCmdPushDescriptorSetKHR(list->cmd.vk_command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                pipeline_info.vk_pipeline_layout, 0, 1, &vk_descriptor_write));

        if (pipeline_info.needs_stencil_mask)
        {
            for (i = 0; i < 8; i++)
            {
                push_args.bit_mask = 1u << i;
                VK_CALL(vkCmdSetStencilWriteMask(list->cmd.vk_command_buffer, VK_STENCIL_FACE_FRONT_AND_BACK, push_args.bit_mask));
                VK_CALL(vkCmdPushConstants(list->cmd.vk_command_buffer, pipeline_info.vk_pipeline_layout,
                        VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push_args), &push_args));
                VK_CALL(vkCmdDraw(list->cmd.vk_command_buffer, 3, region->dstSubresource.layerCount, 0, 0));
            }
        }
        else
        {
            VK_CALL(vkCmdPushConstants(list->cmd.vk_command_buffer, pipeline_info.vk_pipeline_layout,
                    VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push_args), &push_args));
            VK_CALL(vkCmdDraw(list->cmd.vk_command_buffer, 3, region->dstSubresource.layerCount, 0, 0));
        }

        VK_CALL(vkCmdEndRendering(list->cmd.vk_command_buffer));
        d3d12_command_list_debug_mark_end_region(list);

cleanup:
        if (dst_view)
            vkd3d_view_decref(dst_view, list->device);
        if (src_view)
            vkd3d_view_decref(src_view, list->device);
    }

    d3d12_command_list_transition_image_layout(list, batch, dst_resource->res.vk_image,
        &region->dstSubresource, barrier.dst.stages,
        barrier.dst.access, barrier.dst.layout,
        VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_NONE,
        dst_resource->common_layout);

    if (!overlapping_subresource)
    {
        d3d12_command_list_transition_image_layout(list, batch, src_resource->res.vk_image,
            &region->srcSubresource, barrier.src.stages,
            VK_ACCESS_2_NONE, barrier.src.layout,
            VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_NONE,
            src_resource->common_layout);
    }

    if (dst_resource->flags & VKD3D_RESOURCE_LINEAR_STAGING_COPY)
        d3d12_command_list_update_subresource_data(list, dst_resource, region->dstSubresource);
}

static bool validate_d3d12_box(const D3D12_BOX *box)
{
    return box->right > box->left
            && box->bottom > box->top
            && box->back > box->front;
}

static bool d3d12_command_list_init_copy_texture_region(struct d3d12_command_list *list,
        const D3D12_TEXTURE_COPY_LOCATION *dst,
        UINT dst_x, UINT dst_y, UINT dst_z,
        const D3D12_TEXTURE_COPY_LOCATION *src,
        const D3D12_BOX *src_box,
        struct vkd3d_image_copy_info *out)
{
    struct d3d12_resource *dst_resource, *src_resource;
    memset(out, 0, sizeof(*out));

    out->src = *src;
    out->dst = *dst;

    dst_resource = impl_from_ID3D12Resource(dst->pResource);
    src_resource = impl_from_ID3D12Resource(src->pResource);

    out->copy.buffer_image.sType = VK_STRUCTURE_TYPE_BUFFER_IMAGE_COPY_2;
    out->copy.buffer_image.pNext = NULL;

    if (src->Type == D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX && dst->Type == D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT)
    {
        assert(d3d12_resource_is_buffer(dst_resource));
        assert(d3d12_resource_is_texture(src_resource));

        if (!(out->src_format = vkd3d_format_from_d3d12_resource_desc(list->device, &src_resource->desc,
                DXGI_FORMAT_UNKNOWN)))
        {
            WARN("Invalid format %#x.\n", dst->PlacedFootprint.Footprint.Format);
            return false;
        }

        if (!(out->dst_format = vkd3d_get_format(list->device, dst->PlacedFootprint.Footprint.Format, true)))
        {
            WARN("Invalid format %#x.\n", dst->PlacedFootprint.Footprint.Format);
            return false;
        }

        vk_image_buffer_copy_from_d3d12(&out->copy.buffer_image, &dst->PlacedFootprint, src->SubresourceIndex,
                &src_resource->desc, out->src_format, out->dst_format, src_box, dst_x, dst_y, dst_z);
        out->copy.buffer_image.bufferOffset += dst_resource->mem.offset;

        out->src_layout = d3d12_resource_pick_layout(src_resource, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        out->batch_type = VKD3D_BATCH_TYPE_COPY_IMAGE_TO_BUFFER;
    }
    else if (src->Type == D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT && dst->Type == D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX)
    {
        assert(d3d12_resource_is_texture(dst_resource));
        assert(d3d12_resource_is_buffer(src_resource));

        if (!(out->dst_format = vkd3d_format_from_d3d12_resource_desc(list->device, &dst_resource->desc,
                DXGI_FORMAT_UNKNOWN)))
        {
            WARN("Invalid format %#x.\n", dst_resource->desc.Format);
            return false;
        }

        if (!(out->src_format = vkd3d_get_format(list->device, src->PlacedFootprint.Footprint.Format, true)))
        {
            WARN("Invalid format %#x.\n", src->PlacedFootprint.Footprint.Format);
            return false;
        }

        vk_buffer_image_copy_from_d3d12(&out->copy.buffer_image, &src->PlacedFootprint, dst->SubresourceIndex,
                &dst_resource->desc, out->src_format, out->dst_format, src_box, dst_x, dst_y, dst_z);
        out->copy.buffer_image.bufferOffset += src_resource->mem.offset;

        out->dst_layout = d3d12_resource_pick_layout(dst_resource, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        out->writes_full_subresource = d3d12_image_copy_writes_full_subresource(dst_resource,
                &out->copy.buffer_image.imageExtent, &out->copy.buffer_image.imageSubresource);
        out->batch_type = VKD3D_BATCH_TYPE_COPY_BUFFER_TO_IMAGE;

        out->writes_full_resource =
                out->writes_full_subresource && d3d12_resource_get_sub_resource_count(dst_resource) == 1;
    }
    else if (src->Type == D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX && dst->Type == D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX)
    {
        assert(d3d12_resource_is_texture(dst_resource));
        assert(d3d12_resource_is_texture(src_resource));

        out->dst_format = dst_resource->format;
        out->src_format = src_resource->format;

        out->overlapping_subresource = dst_resource == src_resource && src->SubresourceIndex == dst->SubresourceIndex;

        if (!vk_image_copy_from_d3d12(&out->copy.image, src->SubresourceIndex, dst->SubresourceIndex,
                &src_resource->desc, &dst_resource->desc, out->src_format, out->dst_format,
                src_box, dst_x, dst_y, dst_z))
        {
            WARN("Degenerate copy, skipping.\n");
            return false;
        }

        /* Writing full subresource with overlapping subresource is bogus.
         * Need to skip any UNDEFINED transition here. Similar concern as in D3D12_RESOLVE_MODE_DECOMPRESS. */
        out->writes_full_subresource = d3d12_image_copy_writes_full_subresource(dst_resource,
                &out->copy.image.extent,
                &out->copy.image.dstSubresource) && !out->overlapping_subresource;
        out->batch_type = VKD3D_BATCH_TYPE_COPY_IMAGE;

        out->writes_full_resource =
                out->writes_full_subresource && d3d12_resource_get_sub_resource_count(dst_resource) == 1;
    }
    else
    {
        FIXME("Copy type %#x -> %#x not implemented.\n", src->Type, dst->Type);
        return false;
    }
    return true;
}

static void d3d12_command_list_before_copy_texture_region(struct d3d12_command_list *list,
        struct d3d12_command_list_barrier_batch *batch,
        struct vkd3d_image_copy_info *info)
{
    struct d3d12_resource *dst_resource, *src_resource;
    VkAccessFlags2 global_transfer_access;

    dst_resource = impl_from_ID3D12Resource(info->dst.pResource);
    src_resource = impl_from_ID3D12Resource(info->src.pResource);

    d3d12_command_list_track_resource_usage(list, src_resource, true);

    if (info->batch_type == VKD3D_BATCH_TYPE_COPY_IMAGE_TO_BUFFER)
    {
        d3d12_command_list_track_resource_usage(list, src_resource, true);

        /* We're going to do an image layout transition, so we can handle pending buffer barriers while we're at it.
         * After that barrier completes, we implicitly synchronize any outstanding copies, so we can drop the tracking.
         * This also avoids having to compute the destination damage region. */
        global_transfer_access = list->tracked_copy_buffer_count ? VK_ACCESS_2_TRANSFER_WRITE_BIT : VK_ACCESS_2_NONE;
        d3d12_command_list_reset_buffer_copy_tracking(list);

        d3d12_command_list_transition_image_layout_with_global_memory_barrier(list, batch, src_resource->res.vk_image,
                &info->copy.buffer_image.imageSubresource, VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_NONE,
                src_resource->common_layout, VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_READ_BIT,
                info->src_layout, global_transfer_access, global_transfer_access);
    }
    else if (info->batch_type == VKD3D_BATCH_TYPE_COPY_BUFFER_TO_IMAGE)
    {
        d3d12_command_list_track_resource_usage(list, dst_resource, !info->writes_full_resource);

        d3d12_command_list_transition_image_layout(list, batch, dst_resource->res.vk_image,
                &info->copy.buffer_image.imageSubresource, VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_NONE,
                info->writes_full_subresource ? VK_IMAGE_LAYOUT_UNDEFINED : dst_resource->common_layout,
                VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, info->dst_layout);
    }
    else if (info->batch_type == VKD3D_BATCH_TYPE_COPY_IMAGE)
    {
        d3d12_command_list_track_resource_usage(list, dst_resource, !info->writes_full_resource);

        d3d12_command_list_copy_image_transition_images(list, batch, dst_resource, info->dst_format,
            src_resource, info->src_format, &info->copy.image, info->writes_full_subresource,
            info->overlapping_subresource);
    }
}

static void d3d12_command_list_copy_texture_region(struct d3d12_command_list *list,
        struct d3d12_command_list_barrier_batch *batch,
        struct vkd3d_image_copy_info *info)
{
    struct d3d12_resource *dst_resource, *src_resource;
    const struct vkd3d_vk_device_procs *vk_procs;
    VkAccessFlags2 global_transfer_access;

    vk_procs = &list->device->vk_procs;

    dst_resource = impl_from_ID3D12Resource(info->dst.pResource);
    src_resource = impl_from_ID3D12Resource(info->src.pResource);

    if (info->batch_type == VKD3D_BATCH_TYPE_COPY_IMAGE_TO_BUFFER)
    {
        VkCopyImageToBufferInfo2 copy_info;

        global_transfer_access = VK_ACCESS_2_TRANSFER_WRITE_BIT;

        copy_info.sType = VK_STRUCTURE_TYPE_COPY_IMAGE_TO_BUFFER_INFO_2;
        copy_info.pNext = NULL;
        copy_info.srcImage = src_resource->res.vk_image;
        copy_info.srcImageLayout = info->src_layout;
        copy_info.dstBuffer = dst_resource->res.vk_buffer;
        copy_info.regionCount = 1;
        copy_info.pRegions = &info->copy.buffer_image;

        VKD3D_BREADCRUMB_TAG("Image -> Buffer");
        VKD3D_BREADCRUMB_RESOURCE(src_resource);
        VKD3D_BREADCRUMB_RESOURCE(dst_resource);
        VKD3D_BREADCRUMB_BUFFER_IMAGE_COPY(&info->copy.buffer_image);

        VK_CALL(vkCmdCopyImageToBuffer2(list->cmd.vk_command_buffer, &copy_info));

        d3d12_command_list_transition_image_layout_with_global_memory_barrier(list, batch, src_resource->res.vk_image,
                &info->copy.buffer_image.imageSubresource, VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_NONE,
                info->src_layout, VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_NONE, src_resource->common_layout,
                global_transfer_access, global_transfer_access);
    }
    else if (info->batch_type == VKD3D_BATCH_TYPE_COPY_BUFFER_TO_IMAGE)
    {
        VkCopyBufferToImageInfo2 copy_info;

        copy_info.sType = VK_STRUCTURE_TYPE_COPY_BUFFER_TO_IMAGE_INFO_2;
        copy_info.pNext = NULL;
        copy_info.srcBuffer = src_resource->res.vk_buffer;
        copy_info.dstImage = dst_resource->res.vk_image;
        copy_info.dstImageLayout = info->dst_layout;
        copy_info.regionCount = 1;
        copy_info.pRegions = &info->copy.buffer_image;

        VKD3D_BREADCRUMB_TAG("Buffer -> Image");
        VKD3D_BREADCRUMB_RESOURCE(src_resource);
        VKD3D_BREADCRUMB_RESOURCE(dst_resource);
        VKD3D_BREADCRUMB_BUFFER_IMAGE_COPY(&info->copy.buffer_image);

        VK_CALL(vkCmdCopyBufferToImage2(list->cmd.vk_command_buffer, &copy_info));

        d3d12_command_list_transition_image_layout(list, batch, dst_resource->res.vk_image,
                &info->copy.buffer_image.imageSubresource, VK_PIPELINE_STAGE_2_COPY_BIT,
                VK_ACCESS_2_TRANSFER_WRITE_BIT, info->dst_layout, VK_PIPELINE_STAGE_2_COPY_BIT,
                VK_ACCESS_2_NONE, dst_resource->common_layout);

        if (dst_resource->flags & VKD3D_RESOURCE_LINEAR_STAGING_COPY)
            d3d12_command_list_update_subresource_data(list, dst_resource, info->copy.buffer_image.imageSubresource);
    }
    else if (info->batch_type == VKD3D_BATCH_TYPE_COPY_IMAGE)
    {
        d3d12_command_list_copy_image(list, batch, dst_resource, info->dst_format,
            src_resource, info->src_format, &info->copy.image, info->writes_full_subresource,
            info->overlapping_subresource);
    }
}


static bool vk_image_copy_subresource_equals(const ID3D12Resource *res, const VkImageSubresourceLayers *subres,
        const ID3D12Resource *other_res, const VkImageSubresourceLayers *other_subres)
{
    return res == other_res &&
            subres->aspectMask == other_subres->aspectMask &&
            subres->baseArrayLayer == other_subres->baseArrayLayer &&
            subres->mipLevel == other_subres->mipLevel;
}

static void STDMETHODCALLTYPE d3d12_command_list_CopyTextureRegion(d3d12_command_list_iface *iface,
        const D3D12_TEXTURE_COPY_LOCATION *dst, UINT dst_x, UINT dst_y, UINT dst_z,
        const D3D12_TEXTURE_COPY_LOCATION *src, const D3D12_BOX *src_box)
{
    struct d3d12_command_list *list = impl_from_ID3D12GraphicsCommandList(iface);
    struct vkd3d_image_copy_info copy_info;
    bool alias;
    size_t i;

    TRACE("iface %p, dst %p, dst_x %u, dst_y %u, dst_z %u, src %p, src_box %p.\n",
            iface, dst, dst_x, dst_y, dst_z, src, src_box);

    if (list->is_inside_render_pass)
        d3d12_command_list_mark_as_invalid(list, "CopyTextureRegion called within a render pass.\n");

    if (src_box && !validate_d3d12_box(src_box))
    {
        WARN("Empty box %s.\n", debug_d3d12_box(src_box));
        return;
    }

    if (!d3d12_command_list_init_copy_texture_region(list, dst, dst_x, dst_y, dst_z, src, src_box, &copy_info))
        return;

    d3d12_command_list_ensure_transfer_batch(list, copy_info.batch_type);

    list->cmd.estimated_cost += VKD3D_COMMAND_COST_LOW;

    alias = false;
    for (i = 0; !alias && i < list->transfer_batch.batch_len; i++)
    {
        const struct vkd3d_image_copy_info *other_info = &list->transfer_batch.batch[i];
        const VkImageSubresourceLayers *subres, *other_subres;
        const struct d3d12_resource *res, *other_res;

        switch (copy_info.batch_type)
        {
            case VKD3D_BATCH_TYPE_COPY_BUFFER_TO_IMAGE:
                /* Test for destination aliasing as D3D12 requires serialization on overlapping copies (WAW hazard). */
                subres = &copy_info.copy.buffer_image.imageSubresource;
                other_subres = &other_info->copy.buffer_image.imageSubresource;
                assert(subres->layerCount == 1 && other_subres->layerCount == 1);
                alias = vk_image_copy_subresource_equals(copy_info.dst.pResource, subres,
                    other_info->dst.pResource, other_subres);
                break;
            case VKD3D_BATCH_TYPE_COPY_IMAGE_TO_BUFFER:
                /* Test for destination aliasing as D3D12 requires serialization on overlapping copies (WAW hazard). */
                /* TODO: Do more granular alias testing or merge this with d3d12_command_list_mark_copy_buffer_write. */
                res = impl_from_ID3D12Resource(copy_info.dst.pResource);
                other_res = impl_from_ID3D12Resource(other_info->dst.pResource);
                /* If either resource is sparse, consider it to alias with anything. */
                alias = copy_info.dst.pResource == other_info->dst.pResource ||
                        (res->flags & VKD3D_RESOURCE_RESERVED) || (other_res->flags & VKD3D_RESOURCE_RESERVED);
                break;
            case VKD3D_BATCH_TYPE_COPY_IMAGE:
                /* Test for destination aliasing as D3D12 requires serialization on overlapping copies (WAW hazards). */
                alias = vk_image_copy_subresource_equals(
                        copy_info.dst.pResource, &copy_info.copy.image.dstSubresource,
                        other_info->dst.pResource, &other_info->copy.image.dstSubresource);
                break;
            default:
                assert(false);
                return;
        }
    }
    if (alias)
    {
        d3d12_command_list_end_transfer_batch(list);
        /* end_transfer_batch resets the batch_type to NONE, so we need to restore it here. */
        list->transfer_batch.batch_type = copy_info.batch_type;
    }

    list->transfer_batch.batch[list->transfer_batch.batch_len++] = copy_info;

    VKD3D_BREADCRUMB_FLUSH_BATCHES(list);
    VKD3D_BREADCRUMB_COMMAND(COPY);
}

static void STDMETHODCALLTYPE d3d12_command_list_CopyResource(d3d12_command_list_iface *iface,
        ID3D12Resource *dst, ID3D12Resource *src)
{
    struct d3d12_command_list *list = impl_from_ID3D12GraphicsCommandList(iface);
    struct d3d12_resource *dst_resource, *src_resource;
    struct d3d12_command_list_barrier_batch barriers;
    const struct vkd3d_vk_device_procs *vk_procs;
    VkBufferCopy2 vk_buffer_copy;
    unsigned int subresource_idx;
    VkCopyBufferInfo2 copy_info;
    VkImageCopy2 vk_image_copy;
    unsigned int layer_count;
    unsigned int level_count;
    unsigned int plane_count;
    unsigned int i, j;

    TRACE("iface %p, dst_resource %p, src_resource %p.\n", iface, dst, src);

    if (list->is_inside_render_pass)
        d3d12_command_list_mark_as_invalid(list, "CopyResource called within a render pass.\n");

    vk_procs = &list->device->vk_procs;

    dst_resource = impl_from_ID3D12Resource(dst);
    src_resource = impl_from_ID3D12Resource(src);

    d3d12_command_list_track_resource_usage(list, dst_resource, false);
    d3d12_command_list_track_resource_usage(list, src_resource, true);

    d3d12_command_list_end_current_render_pass(list, false);
    d3d12_command_list_end_transfer_batch(list);

    list->cmd.estimated_cost += VKD3D_COMMAND_COST_LOW;

    if (d3d12_resource_is_buffer(dst_resource))
    {
        assert(d3d12_resource_is_buffer(src_resource));
        assert(src_resource->desc.Width == dst_resource->desc.Width);

        vk_buffer_copy.sType = VK_STRUCTURE_TYPE_BUFFER_COPY_2;
        vk_buffer_copy.pNext = NULL;
        vk_buffer_copy.srcOffset = src_resource->mem.offset;
        vk_buffer_copy.dstOffset = dst_resource->mem.offset;
        vk_buffer_copy.size = dst_resource->desc.Width;

        copy_info.sType = VK_STRUCTURE_TYPE_COPY_BUFFER_INFO_2;
        copy_info.pNext = NULL;
        copy_info.srcBuffer = src_resource->res.vk_buffer;
        copy_info.dstBuffer = dst_resource->res.vk_buffer;
        copy_info.regionCount = 1;
        copy_info.pRegions = &vk_buffer_copy;

        VKD3D_BREADCRUMB_TAG("Buffer -> Buffer");
        VKD3D_BREADCRUMB_RESOURCE(src_resource);
        VKD3D_BREADCRUMB_RESOURCE(dst_resource);
        VKD3D_BREADCRUMB_BUFFER_COPY(&vk_buffer_copy);

        d3d12_command_list_mark_copy_buffer_write(list, copy_info.dstBuffer, vk_buffer_copy.dstOffset, vk_buffer_copy.size,
                !!(dst_resource->flags & VKD3D_RESOURCE_RESERVED));
        VK_CALL(vkCmdCopyBuffer2(list->cmd.vk_command_buffer, &copy_info));
    }
    else
    {
        layer_count = d3d12_resource_desc_get_layer_count(&dst_resource->desc);
        level_count = d3d12_resource_desc_get_active_level_count(&dst_resource->desc);
        plane_count = 1;

        if (dst_resource->format->vk_aspect_mask & VK_IMAGE_ASPECT_PLANE_0_BIT)
            plane_count = dst_resource->format->plane_count;

        assert(d3d12_resource_is_texture(dst_resource));
        assert(d3d12_resource_is_texture(src_resource));
        assert(level_count == d3d12_resource_desc_get_active_level_count(&src_resource->desc));
        assert(layer_count == d3d12_resource_desc_get_layer_count(&src_resource->desc));

        for (j = 0; j < plane_count; j++)
        {
            for (i = 0; i < level_count; ++i)
            {
                subresource_idx = i + j * d3d12_resource_desc_get_sub_resource_count_per_plane(&dst_resource->desc);

                if (!vk_image_copy_from_d3d12(&vk_image_copy, subresource_idx, subresource_idx,
                        &src_resource->desc, &dst_resource->desc, src_resource->format, dst_resource->format, NULL, 0, 0, 0))
                {
                    WARN("Degenerate copy for level %u, skipping.\n", i);
                    continue;
                }

                /* Copying sampler feedback is only allowed in CopyResource. */
                if (d3d12_resource_desc_is_sampler_feedback(&src_resource->desc))
                    vk_image_copy.extent = d3d12_resource_desc_get_padded_feedback_extent(&src_resource->desc);

                vk_image_copy.dstSubresource.layerCount = layer_count;
                vk_image_copy.srcSubresource.layerCount = layer_count;

                if (plane_count == 1)
                {
                    vk_image_copy.dstSubresource.aspectMask = dst_resource->format->vk_aspect_mask;
                    vk_image_copy.srcSubresource.aspectMask = src_resource->format->vk_aspect_mask;
                }

                /* CopyResource() always copies all subresources, so we can safely discard the dst_resource contents. */
                d3d12_command_list_barrier_batch_init(&barriers);
                d3d12_command_list_copy_image_transition_images(list, &barriers, dst_resource, dst_resource->format,
                        src_resource, src_resource->format, &vk_image_copy, true, false);
                d3d12_command_list_barrier_batch_end(list, &barriers);
                d3d12_command_list_barrier_batch_init(&barriers);
                d3d12_command_list_copy_image(list, &barriers, dst_resource, dst_resource->format,
                        src_resource, src_resource->format, &vk_image_copy, true, false);
                d3d12_command_list_barrier_batch_end(list, &barriers);
            }
        }
    }

    VKD3D_BREADCRUMB_COMMAND(COPY);
}

static void d3d12_command_list_end_transfer_batch(struct d3d12_command_list *list)
{
    struct d3d12_command_list_barrier_batch barriers;
    size_t i;

    switch (list->transfer_batch.batch_type)
    {
        case VKD3D_BATCH_TYPE_NONE:
            return;
        case VKD3D_BATCH_TYPE_COPY_BUFFER_TO_IMAGE:
        case VKD3D_BATCH_TYPE_COPY_IMAGE_TO_BUFFER:
        case VKD3D_BATCH_TYPE_COPY_IMAGE:
            d3d12_command_list_end_current_render_pass(list, false);
            d3d12_command_list_debug_mark_begin_region(list, "CopyBatch");
            d3d12_command_list_barrier_batch_init(&barriers);
            for (i = 0; i < list->transfer_batch.batch_len; i++)
                d3d12_command_list_before_copy_texture_region(list, &barriers, &list->transfer_batch.batch[i]);
            d3d12_command_list_barrier_batch_end(list, &barriers);
            d3d12_command_list_barrier_batch_init(&barriers);
            for (i = 0; i < list->transfer_batch.batch_len; i++)
                d3d12_command_list_copy_texture_region(list, &barriers, &list->transfer_batch.batch[i]);
            d3d12_command_list_barrier_batch_end(list, &barriers);
            d3d12_command_list_debug_mark_end_region(list);
            list->transfer_batch.batch_len = 0;
            break;
        default:
            break;
    }
    list->transfer_batch.batch_type = VKD3D_BATCH_TYPE_NONE;
}

static void d3d12_command_list_end_wbi_batch(struct d3d12_command_list *list)
{
    const struct vkd3d_vk_device_procs *vk_procs = &list->device->vk_procs;
    size_t i, next, first;
    bool flush;

    if (!list->wbi_batch.batch_len)
        return;

    first = 0;

    for (i = 0; i < list->wbi_batch.batch_len; i++)
    {
        if (list->wbi_batch.stages[i] == VK_PIPELINE_STAGE_TRANSFER_BIT || !list->device->vk_info.AMD_buffer_marker)
        {
            next = i + 1;

            if (!(flush = next == list->wbi_batch.batch_len))
            {
                flush = list->wbi_batch.buffers[next] != list->wbi_batch.buffers[i] ||
                        list->wbi_batch.offsets[next] != list->wbi_batch.offsets[i] + sizeof(uint32_t) ||
                        (list->wbi_batch.stages[next] != list->wbi_batch.stages[i] && list->device->vk_info.AMD_buffer_marker);
            }

            if (flush)
            {
                VK_CALL(vkCmdUpdateBuffer(list->cmd.vk_command_buffer,
                        list->wbi_batch.buffers[first], list->wbi_batch.offsets[first],
                        (next - first) * sizeof(uint32_t), &list->wbi_batch.values[first]));

                first = next;
            }
        }
        else
        {
            VK_CALL(vkCmdWriteBufferMarkerAMD(list->cmd.vk_command_buffer,
                    list->wbi_batch.stages[i], list->wbi_batch.buffers[i],
                    list->wbi_batch.offsets[i], list->wbi_batch.values[i]));
        }
    }

    list->wbi_batch.batch_len = 0;
}

static void d3d12_command_list_ensure_transfer_batch(struct d3d12_command_list *list, enum vkd3d_batch_type type)
{
    if (list->transfer_batch.batch_type != type || list->transfer_batch.batch_len == ARRAY_SIZE(list->transfer_batch.batch))
    {
        d3d12_command_list_end_transfer_batch(list);
        list->transfer_batch.batch_type = type;
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
    VkImageMemoryBarrier2 vk_image_barrier;
    VkBufferImageCopy2 buffer_image_copy;
    VkMemoryBarrier2 vk_global_barrier;
    VkImageLayout vk_image_layout;
    VkCopyBufferInfo2 copy_info;
    VkDependencyInfo dep_info;
    VkBufferCopy2 buffer_copy;
    bool copy_to_buffer;
    unsigned int i;

    TRACE("iface %p, tiled_resource %p, region_coord %p, region_size %p, "
            "buffer %p, buffer_offset %#"PRIx64", flags %#x.\n",
            iface, tiled_resource, region_coord, region_size,
            buffer, buffer_offset, flags);

    if (list->is_inside_render_pass)
        d3d12_command_list_mark_as_invalid(list, "CopyTiles called within a render pass.\n");

    d3d12_command_list_end_current_render_pass(list, true);
    d3d12_command_list_end_transfer_batch(list);

    list->cmd.estimated_cost += VKD3D_COMMAND_COST_LOW;

    tiled_res = impl_from_ID3D12Resource(tiled_resource);
    linear_res = impl_from_ID3D12Resource(buffer);

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


        memset(&vk_image_barrier, 0, sizeof(vk_image_barrier));
        vk_image_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        vk_image_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        vk_image_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        vk_image_barrier.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
        vk_image_barrier.dstStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
        vk_image_barrier.dstAccessMask = copy_to_buffer ? VK_ACCESS_2_TRANSFER_READ_BIT : VK_ACCESS_2_TRANSFER_WRITE_BIT;
        vk_image_barrier.oldLayout = tiled_res->common_layout;
        vk_image_barrier.newLayout = vk_image_layout;
        vk_image_barrier.image = tiled_res->res.vk_image;

        /* The entire resource must be in the appropriate copy state */
        vk_image_barrier.subresourceRange.aspectMask = tiled_res->format->vk_aspect_mask;
        vk_image_barrier.subresourceRange.levelCount = VK_REMAINING_MIP_LEVELS;
        vk_image_barrier.subresourceRange.layerCount = VK_REMAINING_ARRAY_LAYERS;

        memset(&vk_global_barrier, 0, sizeof(vk_global_barrier));
        vk_global_barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
        vk_global_barrier.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
        vk_global_barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        vk_global_barrier.dstStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
        vk_global_barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;

        memset(&dep_info, 0, sizeof(dep_info));
        dep_info.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        dep_info.imageMemoryBarrierCount = 1;
        dep_info.pImageMemoryBarriers = &vk_image_barrier;
        dep_info.memoryBarrierCount = 0;
        dep_info.pMemoryBarriers = &vk_global_barrier;

        if (copy_to_buffer)
        {
            /* Need to handle hazards before the image to buffer copy. */
            if (list->tracked_copy_buffer_count)
                dep_info.memoryBarrierCount = 1;

            /* We're doing a transfer barrier anyways, so resolve buffer copy tracking in that barrier. */
            d3d12_command_list_reset_buffer_copy_tracking(list);
        }

        VK_CALL(vkCmdPipelineBarrier2(list->cmd.vk_command_buffer, &dep_info));

        buffer_image_copy.bufferRowLength = tile_shape->WidthInTexels;
        buffer_image_copy.bufferImageHeight = tile_shape->HeightInTexels;

        for (i = 0; i < region_size->NumTiles; i++)
        {
            unsigned int tile_index = vkd3d_get_tile_index_from_region(&tiled_res->sparse, region_coord, region_size, i);
            const struct d3d12_sparse_image_region *region = &tiled_res->sparse.tiles[tile_index].image;

            buffer_image_copy.sType = VK_STRUCTURE_TYPE_BUFFER_IMAGE_COPY_2;
            buffer_image_copy.pNext = NULL;
            buffer_image_copy.bufferOffset = buffer_offset + VKD3D_TILE_SIZE * i + linear_res->mem.offset;
            buffer_image_copy.imageSubresource = vk_subresource_layers_from_subresource(&region->subresource);
            buffer_image_copy.imageOffset = region->offset;
            buffer_image_copy.imageExtent = region->extent;

            if (copy_to_buffer)
            {
                VkCopyImageToBufferInfo2 copy_info;

                copy_info.sType = VK_STRUCTURE_TYPE_COPY_IMAGE_TO_BUFFER_INFO_2;
                copy_info.pNext = NULL;
                copy_info.srcImage = tiled_res->res.vk_image;
                copy_info.srcImageLayout = vk_image_layout;
                copy_info.dstBuffer = linear_res->res.vk_buffer;
                copy_info.regionCount = 1;
                copy_info.pRegions = &buffer_image_copy;

                /* Resolve hazards after the image to buffer copy since we're doing an image barrier anyways. */
                dep_info.memoryBarrierCount = 1;

                VK_CALL(vkCmdCopyImageToBuffer2(list->cmd.vk_command_buffer, &copy_info));
            }
            else
            {
                VkCopyBufferToImageInfo2 copy_info;

                copy_info.sType = VK_STRUCTURE_TYPE_COPY_BUFFER_TO_IMAGE_INFO_2;
                copy_info.pNext = NULL;
                copy_info.srcBuffer = linear_res->res.vk_buffer;
                copy_info.dstImage = tiled_res->res.vk_image;
                copy_info.dstImageLayout = vk_image_layout;
                copy_info.regionCount = 1;
                copy_info.pRegions = &buffer_image_copy;

                VK_CALL(vkCmdCopyBufferToImage2(list->cmd.vk_command_buffer, &copy_info));
            }
        }

        vk_image_barrier.srcAccessMask = copy_to_buffer ? VK_ACCESS_2_NONE : VK_ACCESS_2_TRANSFER_WRITE_BIT;
        vk_image_barrier.dstAccessMask = VK_ACCESS_2_NONE;
        vk_image_barrier.oldLayout = vk_image_layout;
        vk_image_barrier.newLayout = tiled_res->common_layout;

        VK_CALL(vkCmdPipelineBarrier2(list->cmd.vk_command_buffer, &dep_info));
    }
    else
    {
        buffer_copy.sType = VK_STRUCTURE_TYPE_BUFFER_COPY_2;
        buffer_copy.pNext = NULL;
        buffer_copy.size = region_size->NumTiles * VKD3D_TILE_SIZE;

        if (copy_to_buffer)
        {
            buffer_copy.srcOffset = VKD3D_TILE_SIZE * region_coord->X + tiled_res->mem.offset;
            buffer_copy.dstOffset = buffer_offset + linear_res->mem.offset;
        }
        else
        {
            buffer_copy.srcOffset = buffer_offset + linear_res->mem.offset;
            buffer_copy.dstOffset = VKD3D_TILE_SIZE * region_coord->X + tiled_res->mem.offset;
        }

        copy_info.sType = VK_STRUCTURE_TYPE_COPY_BUFFER_INFO_2;
        copy_info.pNext = NULL;
        copy_info.srcBuffer = copy_to_buffer ? tiled_res->res.vk_buffer : linear_res->res.vk_buffer;
        copy_info.dstBuffer = copy_to_buffer ? linear_res->res.vk_buffer : tiled_res->res.vk_buffer,
        copy_info.regionCount = 1;
        copy_info.pRegions = &buffer_copy;

        d3d12_command_list_mark_copy_buffer_write(list, copy_info.dstBuffer, buffer_copy.dstOffset, buffer_copy.size,
                !!((copy_to_buffer ? linear_res : tiled_res)->flags & VKD3D_RESOURCE_RESERVED));
        VK_CALL(vkCmdCopyBuffer2(list->cmd.vk_command_buffer, &copy_info));
    }

    VKD3D_BREADCRUMB_COMMAND(COPY_TILES);
}

static VkResolveModeFlagBits vk_resolve_mode_from_d3d12(D3D12_RESOLVE_MODE mode)
{
    switch (mode)
    {
        case D3D12_RESOLVE_MODE_AVERAGE:
            return VK_RESOLVE_MODE_AVERAGE_BIT;

        case D3D12_RESOLVE_MODE_MIN:
            return VK_RESOLVE_MODE_MIN_BIT;

        case D3D12_RESOLVE_MODE_MAX:
            return VK_RESOLVE_MODE_MAX_BIT;

        default:
            ERR("Unhandled resolve mode %u.\n", mode);
            return VK_RESOLVE_MODE_NONE;
    }
}

static const struct vkd3d_format *d3d12_command_list_get_resolve_format(struct d3d12_command_list *list,
        const struct d3d12_resource *dst_resource, const struct d3d12_resource *src_resource, DXGI_FORMAT format)
{
    if (!format)
        return NULL;

    /* Depth-stencil formats require special care since the app will need to
     * pass in the stencil plane as subresource 1, but may also use a format
     * for which we only have one aspect flag set. Just ignore the explicit
     * format in that case so that we always know the full aspect mask. */
    if (dst_resource->format->vk_aspect_mask == (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT))
        return dst_resource->format;

    return vkd3d_format_from_d3d12_resource_desc(list->device, &dst_resource->desc, format);
}

static bool d3d12_resource_view_format_is_compatible(
        const struct d3d12_resource *resource, const struct vkd3d_format *format)
{
    const struct vkd3d_format_compatibility_list *compat;
    unsigned int i;

    if (resource->format->vk_format == format->vk_format)
        return true;

    compat = &resource->format_compatibility_list;

    /* Full mutable, we can cast to whatever we want. */
    if (compat->format_count == 0)
        return true;

    for (i = 0; i < compat->format_count; i++)
    {
        if (compat->vk_formats[i] == format->vk_format)
            return true;
    }

    return false;
}

enum vkd3d_resolve_image_path d3d12_command_list_select_resolve_path(struct d3d12_command_list *list,
        struct d3d12_resource *dst_resource, struct d3d12_resource *src_resource, uint32_t region_count,
        const VkImageResolve2 *regions, DXGI_FORMAT format, D3D12_RESOLVE_MODE mode)
{
    const struct vkd3d_format *vkd3d_format = d3d12_command_list_get_resolve_format(list, dst_resource, src_resource, format);
    enum vkd3d_resolve_image_path path;
    unsigned int i;

    if (!vkd3d_format)
    {
        d3d12_command_list_mark_as_invalid(list, "Resolve format %#x not compatible with resource formats %#x, %#x.",
                format, dst_resource->format->dxgi_format, src_resource->format->dxgi_format);
        return VKD3D_RESOLVE_IMAGE_PATH_UNSUPPORTED;
    }

    if (!d3d12_resource_view_format_is_compatible(dst_resource, vkd3d_format))
    {
        ERR("Attempting to resolve to dst resource with incompatible format.\n");
        return VKD3D_RESOLVE_IMAGE_PATH_UNSUPPORTED;
    }

    if (!d3d12_resource_view_format_is_compatible(src_resource, vkd3d_format))
    {
        ERR("Attempting to resolve from src resource with incompatible format.\n");
        return VKD3D_RESOLVE_IMAGE_PATH_UNSUPPORTED;
    }

    if (dst_resource->format->vk_aspect_mask != src_resource->format->vk_aspect_mask)
    {
        /* Mismatched aspects may happen with some typeless formats */
        path = VKD3D_RESOLVE_IMAGE_PATH_RENDER_PASS_PIPELINE;
    }
    else if (vkd3d_format->vk_aspect_mask & (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT))
    {
        /* The direct path only supports color images with the AVERAGE mode */
        path = VKD3D_RESOLVE_IMAGE_PATH_RENDER_PASS_ATTACHMENT;
    }
    else if (mode != D3D12_RESOLVE_MODE_AVERAGE)
    {
        /* Vulkan only supports SAMPLE_ZERO for color images for which D3D12 does
         * not even have an equivalent, we can only implement this in shaders. */
        path = VKD3D_RESOLVE_IMAGE_PATH_RENDER_PASS_PIPELINE;
    }
    else if (dst_resource->format->vk_format != vkd3d_format->vk_format ||
            src_resource->format->vk_format != vkd3d_format->vk_format)
    {
        path = VKD3D_RESOLVE_IMAGE_PATH_RENDER_PASS_ATTACHMENT;

        if (!(vkd3d_config_flags & VKD3D_CONFIG_FLAG_SKIP_DRIVER_WORKAROUNDS))
        {
            /* Some drivers ignore the view format for resolve attachments */
            if (list->device->device_info.vulkan_1_2_properties.driverID == VK_DRIVER_ID_NVIDIA_PROPRIETARY ||
                    list->device->device_info.vulkan_1_2_properties.driverID == VK_DRIVER_ID_AMD_PROPRIETARY ||
                    list->device->device_info.vulkan_1_2_properties.driverID == VK_DRIVER_ID_AMD_OPEN_SOURCE)
                path = VKD3D_RESOLVE_IMAGE_PATH_RENDER_PASS_PIPELINE;
        }
    }
    else
    {
        path = VKD3D_RESOLVE_IMAGE_PATH_DIRECT;
    }

    /* The attachment path has a number of restrictions that may require us to fall
     * back to the shader-based path. */
    if (path == VKD3D_RESOLVE_IMAGE_PATH_RENDER_PASS_ATTACHMENT)
    {
        if (!(src_resource->desc.Flags & (D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL)))
        {
            /* If the source image somehow cannot be bound for rendering, use the
             * shader path since that will work for all source images. */
            path = VKD3D_RESOLVE_IMAGE_PATH_RENDER_PASS_PIPELINE;
        }
        else if (vkd3d_format->vk_aspect_mask & (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT))
        {
            /* Ensure that the required depth and stencil resolve modes are supported */
            const VkPhysicalDeviceVulkan12Properties *vk12 = &list->device->device_info.vulkan_1_2_properties;
            VkResolveModeFlagBits vk_mode = vk_resolve_mode_from_d3d12(mode);
            bool supports_render_pass_resolve = vk12->independentResolveNone;

            for (i = 0; i < region_count && supports_render_pass_resolve; i++)
            {
                if (regions[i].dstSubresource.aspectMask & VK_IMAGE_ASPECT_DEPTH_BIT)
                    supports_render_pass_resolve &= !!(vk12->supportedDepthResolveModes & vk_mode);
                if (regions[i].dstSubresource.aspectMask & VK_IMAGE_ASPECT_STENCIL_BIT)
                    supports_render_pass_resolve &= !!(vk12->supportedStencilResolveModes & vk_mode);
            }

            if (!supports_render_pass_resolve)
                path = VKD3D_RESOLVE_IMAGE_PATH_RENDER_PASS_PIPELINE;
        }
    }

    /* Attachment resolves cannot be used with an offset */
    if (path == VKD3D_RESOLVE_IMAGE_PATH_RENDER_PASS_ATTACHMENT)
    {
        for (i = 0; i < region_count; i++)
        {
            if (regions[i].srcOffset.x != regions[i].dstOffset.x ||
                    regions[i].srcOffset.y != regions[i].dstOffset.y)
            {
                path = VKD3D_RESOLVE_IMAGE_PATH_RENDER_PASS_PIPELINE;
                break;
            }
        }
    }

    if (dst_resource->desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS)
    {
        /* Use the compute path if we need to use a pipeline anyway, or if
         * the destination image does not support render target usage. */
        if (path == VKD3D_RESOLVE_IMAGE_PATH_RENDER_PASS_PIPELINE ||
                (path == VKD3D_RESOLVE_IMAGE_PATH_RENDER_PASS_ATTACHMENT &&
                    !(dst_resource->desc.Flags & (D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET))))
            path = VKD3D_RESOLVE_IMAGE_PATH_COMPUTE_PIPELINE;
    }

    /* All other code paths require render target usage for the destination image */
    if ((path == VKD3D_RESOLVE_IMAGE_PATH_RENDER_PASS_PIPELINE || path == VKD3D_RESOLVE_IMAGE_PATH_RENDER_PASS_ATTACHMENT) &&
            !(dst_resource->desc.Flags & (D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL)))
    {
        FIXME("Selected resolve path %u for mode %u, format %u, but destination image cannot be used as a render target.\n",
                path, mode, format);

        /* Fallback when trying to do MIN/MAX resolve on color and there is no RTV usage.
         * AVERAGE is almost correct and better than rendering nothing. */
        if (vkd3d_format->vk_aspect_mask & VK_IMAGE_ASPECT_COLOR_BIT)
            path = VKD3D_RESOLVE_IMAGE_PATH_DIRECT;
        else
            path = VKD3D_RESOLVE_IMAGE_PATH_UNSUPPORTED;
    }

    return path;
}

static void d3d12_get_resolve_barrier_for_dst_resource(struct d3d12_resource *resource, const VkImageResolve2 *region,
        enum vkd3d_resolve_image_path path, bool post_resolve, VkImageLayout outside_layout, VkPipelineStageFlags2 outside_stages,
        VkAccessFlags2 outside_access, VkImageMemoryBarrier2 *barrier)
{
    VkPipelineStageFlags2 resolve_stages;
    VkAccessFlags2 resolve_access;
    VkImageLayout resolve_layout;
    bool writes_full_subresource;

    writes_full_subresource = d3d12_image_copy_writes_full_subresource(
            resource, &region->extent, &region->dstSubresource);

    if (path == VKD3D_RESOLVE_IMAGE_PATH_DIRECT)
    {
        resolve_layout = d3d12_resource_pick_layout(resource, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        resolve_stages = VK_PIPELINE_STAGE_2_RESOLVE_BIT;
        resolve_access = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    }
    else if (path == VKD3D_RESOLVE_IMAGE_PATH_COMPUTE_PIPELINE)
    {
        resolve_layout = VK_IMAGE_LAYOUT_GENERAL;
        resolve_stages = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        resolve_access = VK_ACCESS_2_SHADER_WRITE_BIT;
    }
    else
    {
        if (resource->format->vk_aspect_mask & (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT))
        {
            resolve_layout = d3d12_resource_pick_layout(resource, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
            resolve_stages = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
            resolve_access = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        }
        else
        {
            resolve_layout = d3d12_resource_pick_layout(resource, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
            resolve_stages = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
            resolve_access = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
        }
    }

    memset(barrier, 0, sizeof(*barrier));
    barrier->sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;

    if (post_resolve)
    {
        barrier->srcStageMask = resolve_stages;
        barrier->srcAccessMask = resolve_access;
        barrier->dstStageMask = outside_stages;
        barrier->dstAccessMask = outside_access;
        barrier->oldLayout = resolve_layout;
        barrier->newLayout = outside_layout;
    }
    else
    {
        barrier->srcStageMask = outside_stages;
        barrier->srcAccessMask = outside_access;
        barrier->dstStageMask = resolve_stages;
        barrier->dstAccessMask = resolve_access;
        barrier->oldLayout = writes_full_subresource ? VK_IMAGE_LAYOUT_UNDEFINED : outside_layout;
        barrier->newLayout = resolve_layout;
    }

    barrier->srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier->dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier->image = resource->res.vk_image;
    barrier->subresourceRange = vk_subresource_range_from_layers(&region->dstSubresource);
}

static void d3d12_get_resolve_barrier_for_src_resource(struct d3d12_resource *resource, const VkImageResolve2 *region,
        enum vkd3d_resolve_image_path path, bool post_resolve, VkImageLayout outside_layout, VkPipelineStageFlags2 outside_stages,
        VkAccessFlags2 outside_access, VkImageMemoryBarrier2 *barrier)
{
    VkPipelineStageFlags2 resolve_stages;
    VkAccessFlags2 resolve_access;
    VkImageLayout resolve_layout;

    if (path == VKD3D_RESOLVE_IMAGE_PATH_DIRECT)
    {
        resolve_layout = d3d12_resource_pick_layout(resource, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        resolve_stages = VK_PIPELINE_STAGE_2_RESOLVE_BIT;
        resolve_access = VK_ACCESS_2_TRANSFER_READ_BIT;
    }
    else if (path == VKD3D_RESOLVE_IMAGE_PATH_RENDER_PASS_ATTACHMENT)
    {
        if (resource->format->vk_aspect_mask & (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT))
        {
            resolve_layout = d3d12_resource_pick_layout(resource, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
            resolve_stages = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
            resolve_access = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
        }
        else
        {
            resolve_layout = d3d12_resource_pick_layout(resource, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
            resolve_stages = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
            resolve_access = VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT;
        }
    }
    else
    {
        resolve_layout = d3d12_resource_pick_layout(resource, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        resolve_stages = (path == VKD3D_RESOLVE_IMAGE_PATH_RENDER_PASS_PIPELINE)
                ? VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT : VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        resolve_access = VK_ACCESS_2_SHADER_READ_BIT;
    }

    memset(barrier, 0, sizeof(*barrier));
    barrier->sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;

    if (post_resolve)
    {
        barrier->srcStageMask = resolve_stages;
        barrier->srcAccessMask = resolve_access;
        barrier->dstStageMask = outside_stages;
        barrier->dstAccessMask = outside_access;
        barrier->oldLayout = resolve_layout;
        barrier->newLayout = outside_layout;
    }
    else
    {
        barrier->srcStageMask = outside_stages;
        barrier->srcAccessMask = outside_access;
        barrier->dstStageMask = resolve_stages;
        barrier->dstAccessMask = resolve_access;
        barrier->oldLayout = outside_layout;
        barrier->newLayout = resolve_layout;
    }

    barrier->srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier->dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier->image = resource->res.vk_image;
    barrier->subresourceRange = vk_subresource_range_from_layers(&region->srcSubresource);
}

static const struct vkd3d_format *d3d12_get_linear_resolve_format(struct d3d12_device *device, const struct vkd3d_format *format)
{
    unsigned int i;

    static const struct
    {
        DXGI_FORMAT srgb_format;
        DXGI_FORMAT linear_format;
    }
    format_map[] =
    {
        { DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, DXGI_FORMAT_R8G8B8A8_UNORM },
        { DXGI_FORMAT_B8G8R8A8_UNORM_SRGB, DXGI_FORMAT_B8G8R8A8_UNORM },
        { DXGI_FORMAT_B8G8R8X8_UNORM_SRGB, DXGI_FORMAT_B8G8R8X8_UNORM },
    };

    for (i = 0; i < ARRAY_SIZE(format_map); i++)
    {
        if (format->dxgi_format == format_map[i].srgb_format)
            return vkd3d_get_format(device, format_map[i].linear_format, false);
    }

    return format;
}

static void d3d12_command_list_execute_resolve(struct d3d12_command_list *list,
        struct d3d12_resource *dst_resource, struct d3d12_resource *src_resource,
        uint32_t region_count, const VkImageResolve2 *regions, DXGI_FORMAT format,
        D3D12_RESOLVE_MODE mode, enum vkd3d_resolve_image_path path)
{
    const struct vkd3d_vk_device_procs *vk_procs = &list->device->vk_procs;
    struct vkd3d_resolve_image_pipeline_key resolve_pipeline_key;
    struct vkd3d_texture_view_desc dst_view_desc, src_view_desc;
    VkDescriptorImageInfo vk_src_image_info, vk_dst_image_info;
    struct vkd3d_resolve_image_info resolve_pipeline_info;
    struct vkd3d_resolve_image_compute_args compute_args;
    struct vkd3d_resolve_image_args resolve_args;
    VkWriteDescriptorSet vk_descriptor_writes[2];
    VkRenderingAttachmentInfo attachment_info;
    struct vkd3d_view *dst_view, *src_view;
    const struct vkd3d_format *vk_format;
    VkResolveImageInfo2 resolve_info;
    VkRenderingInfo rendering_info;
    VkViewport viewport;
    unsigned int i, j;

    if (path == VKD3D_RESOLVE_IMAGE_PATH_DIRECT)
    {
        memset(&resolve_info, 0, sizeof(resolve_info));
        resolve_info.sType = VK_STRUCTURE_TYPE_RESOLVE_IMAGE_INFO_2;
        resolve_info.srcImage = src_resource->res.vk_image;
        resolve_info.srcImageLayout = d3d12_resource_pick_layout(src_resource, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        resolve_info.dstImage = dst_resource->res.vk_image;
        resolve_info.dstImageLayout = d3d12_resource_pick_layout(dst_resource, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        resolve_info.regionCount = region_count;
        resolve_info.pRegions = regions;

        VK_CALL(vkCmdResolveImage2(list->cmd.vk_command_buffer, &resolve_info));
    }
    else if (path == VKD3D_RESOLVE_IMAGE_PATH_COMPUTE_PIPELINE)
    {
        d3d12_command_list_invalidate_current_pipeline(list, true);
        d3d12_command_list_invalidate_root_parameters(list, &list->compute_bindings, true, &list->graphics_bindings);
        d3d12_command_list_update_descriptor_buffers(list);

        vk_format = d3d12_command_list_get_resolve_format(list, dst_resource, src_resource, format);

        memset(&dst_view_desc, 0, sizeof(dst_view_desc));
        dst_view_desc.image = dst_resource->res.vk_image;
        dst_view_desc.view_type = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
        dst_view_desc.format = d3d12_get_linear_resolve_format(list->device, vk_format);
        dst_view_desc.image_usage = VK_IMAGE_USAGE_STORAGE_BIT;

        memset(&src_view_desc, 0, sizeof(src_view_desc));
        src_view_desc.image = src_resource->res.vk_image;
        src_view_desc.view_type = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
        src_view_desc.format = vkd3d_format_from_d3d12_resource_desc(list->device, &src_resource->desc, vk_format->dxgi_format);
        src_view_desc.image_usage = VK_IMAGE_USAGE_SAMPLED_BIT;
        src_view_desc.allowed_swizzle = true;

        memset(&resolve_pipeline_key, 0, sizeof(resolve_pipeline_key));
        resolve_pipeline_key.path = path;
        resolve_pipeline_key.compute.format_type = vk_format->type;
        resolve_pipeline_key.compute.mode = mode;
        resolve_pipeline_key.compute.srgb = dst_view_desc.format != vk_format;

        if (FAILED(vkd3d_meta_get_resolve_image_pipeline(&list->device->meta_ops, &resolve_pipeline_key, &resolve_pipeline_info)))
        {
            ERR("Failed to get resolve pipeline.\n");
            return;
        }

        VK_CALL(vkCmdBindPipeline(list->cmd.vk_command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, resolve_pipeline_info.vk_pipeline));

        for (i = 0; i < region_count; i++)
        {
            const VkImageResolve2 *region = &regions[i];

            dst_view_desc.miplevel_idx = region->dstSubresource.mipLevel;
            dst_view_desc.miplevel_count = 1;
            dst_view_desc.layer_idx = region->dstSubresource.baseArrayLayer;
            dst_view_desc.layer_count = region->dstSubresource.layerCount;
            dst_view_desc.aspect_mask = region->dstSubresource.aspectMask;

            src_view_desc.miplevel_idx = region->srcSubresource.mipLevel;
            src_view_desc.miplevel_count = 1;
            src_view_desc.layer_idx = region->srcSubresource.baseArrayLayer;
            src_view_desc.layer_count = region->srcSubresource.layerCount;
            src_view_desc.aspect_mask = region->srcSubresource.aspectMask;

            if (!vkd3d_create_texture_view(list->device, &dst_view_desc, &dst_view) ||
                    !vkd3d_create_texture_view(list->device, &src_view_desc, &src_view))
            {
                ERR("Failed to create image views.\n");
                goto cleanup_compute;
            }

            if (!d3d12_command_allocator_add_view(list->allocator, dst_view) ||
                    !d3d12_command_allocator_add_view(list->allocator, src_view))
            {
                ERR("Failed to add views.\n");
                goto cleanup_compute;
            }

            memset(&vk_src_image_info, 0, sizeof(vk_src_image_info));
            vk_src_image_info.imageView = src_view->vk_image_view;
            vk_src_image_info.imageLayout = d3d12_resource_pick_layout(src_resource, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

            memset(&vk_dst_image_info, 0, sizeof(vk_dst_image_info));
            vk_dst_image_info.imageView = dst_view->vk_image_view;
            vk_dst_image_info.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

            memset(&vk_descriptor_writes, 0, sizeof(vk_descriptor_writes));
            vk_descriptor_writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            vk_descriptor_writes[0].dstBinding = 0;
            vk_descriptor_writes[0].dstArrayElement = 0;
            vk_descriptor_writes[0].descriptorCount = 1;
            vk_descriptor_writes[0].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
            vk_descriptor_writes[0].pImageInfo = &vk_src_image_info;

            vk_descriptor_writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            vk_descriptor_writes[1].dstBinding = 1;
            vk_descriptor_writes[1].dstArrayElement = 0;
            vk_descriptor_writes[1].descriptorCount = 1;
            vk_descriptor_writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            vk_descriptor_writes[1].pImageInfo = &vk_dst_image_info;

            VK_CALL(vkCmdPushDescriptorSetKHR(list->cmd.vk_command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                            resolve_pipeline_info.vk_pipeline_layout, 0, ARRAY_SIZE(vk_descriptor_writes), vk_descriptor_writes));

            compute_args.src_offset.x = region->srcOffset.x;
            compute_args.src_offset.y = region->srcOffset.y;
            compute_args.dst_offset.x = region->dstOffset.x;
            compute_args.dst_offset.y = region->dstOffset.y;
            compute_args.extent.width = region->extent.width;
            compute_args.extent.height = region->extent.height;

            VK_CALL(vkCmdPushConstants(list->cmd.vk_command_buffer, resolve_pipeline_info.vk_pipeline_layout,
                    VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(compute_args), &compute_args));

            VK_CALL(vkCmdDispatch(list->cmd.vk_command_buffer,
                    vkd3d_compute_workgroup_count(region->extent.width, 8),
                    vkd3d_compute_workgroup_count(region->extent.height, 8),
                    region->dstSubresource.layerCount));

cleanup_compute:
            if (dst_view)
                vkd3d_view_decref(dst_view, list->device);
            if (src_view)
                vkd3d_view_decref(src_view, list->device);
        }
    }
    else
    {
        d3d12_command_list_invalidate_current_pipeline(list, true);
        d3d12_command_list_invalidate_root_parameters(list, &list->graphics_bindings, true, &list->compute_bindings);
        d3d12_command_list_update_descriptor_buffers(list);

        vk_format = d3d12_command_list_get_resolve_format(list, dst_resource, src_resource, format);

        memset(&attachment_info, 0, sizeof(attachment_info));
        attachment_info.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;

        memset(&rendering_info, 0, sizeof(rendering_info));
        rendering_info.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;

        if (vk_format->vk_aspect_mask & VK_IMAGE_ASPECT_COLOR_BIT)
        {
            rendering_info.colorAttachmentCount = 1;
            rendering_info.pColorAttachments = &attachment_info;

            if (path == VKD3D_RESOLVE_IMAGE_PATH_RENDER_PASS_ATTACHMENT)
            {
                attachment_info.imageLayout = d3d12_resource_pick_layout(src_resource, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
                attachment_info.resolveImageLayout = d3d12_resource_pick_layout(dst_resource, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
            }
            else
                attachment_info.imageLayout = d3d12_resource_pick_layout(dst_resource, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        }
        else
        {
            if (path == VKD3D_RESOLVE_IMAGE_PATH_RENDER_PASS_ATTACHMENT)
            {
                attachment_info.imageLayout = d3d12_resource_pick_layout(src_resource, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
                attachment_info.resolveImageLayout = d3d12_resource_pick_layout(dst_resource, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
            }
            else
                attachment_info.imageLayout = d3d12_resource_pick_layout(dst_resource, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
        }

        for (i = 0; i < region_count; i++)
        {
            const VkImageResolve2 *region = &regions[i];

            rendering_info.renderArea.offset.x = region->dstOffset.x;
            rendering_info.renderArea.offset.y = region->dstOffset.y;
            rendering_info.renderArea.extent.width = region->extent.width;
            rendering_info.renderArea.extent.height = region->extent.height;
            rendering_info.layerCount = region->dstSubresource.layerCount;
            rendering_info.pDepthAttachment = (region->dstSubresource.aspectMask & VK_IMAGE_ASPECT_DEPTH_BIT) ? &attachment_info : NULL;
            rendering_info.pStencilAttachment = (region->dstSubresource.aspectMask & VK_IMAGE_ASPECT_STENCIL_BIT) ? &attachment_info : NULL;

            if (path == VKD3D_RESOLVE_IMAGE_PATH_RENDER_PASS_ATTACHMENT)
                attachment_info.resolveMode = vk_resolve_mode_from_d3d12(mode);

            memset(&dst_view_desc, 0, sizeof(dst_view_desc));
            dst_view_desc.image = dst_resource->res.vk_image;
            dst_view_desc.view_type = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
            dst_view_desc.format = vk_format;
            dst_view_desc.miplevel_idx = region->dstSubresource.mipLevel;
            dst_view_desc.miplevel_count = 1;
            dst_view_desc.layer_idx = region->dstSubresource.baseArrayLayer;
            dst_view_desc.layer_count = region->dstSubresource.layerCount;
            dst_view_desc.aspect_mask = vk_format->vk_aspect_mask;
            dst_view_desc.image_usage = (vk_format->vk_aspect_mask & VK_IMAGE_ASPECT_COLOR_BIT) ?
                    VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT : VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;

            memset(&src_view_desc, 0, sizeof(src_view_desc));
            src_view_desc.image = src_resource->res.vk_image;
            src_view_desc.view_type = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
            src_view_desc.format = vkd3d_format_from_d3d12_resource_desc(list->device, &src_resource->desc, vk_format->dxgi_format);
            src_view_desc.miplevel_idx = region->srcSubresource.mipLevel;
            src_view_desc.miplevel_count = 1;
            src_view_desc.layer_idx = region->srcSubresource.baseArrayLayer;
            src_view_desc.layer_count = region->srcSubresource.layerCount;

            if (path == VKD3D_RESOLVE_IMAGE_PATH_RENDER_PASS_ATTACHMENT)
            {
                src_view_desc.aspect_mask = vk_format->vk_aspect_mask;
                src_view_desc.image_usage = dst_view_desc.image_usage;
            }
            else
            {
                src_view_desc.aspect_mask = region->srcSubresource.aspectMask;
                src_view_desc.image_usage = VK_IMAGE_USAGE_SAMPLED_BIT;
                src_view_desc.allowed_swizzle = true;
            }

            if (!vkd3d_create_texture_view(list->device, &dst_view_desc, &dst_view) ||
                    !vkd3d_create_texture_view(list->device, &src_view_desc, &src_view))
            {
                ERR("Failed to create image views.\n");
                goto cleanup_graphics;
            }

            if (!d3d12_command_allocator_add_view(list->allocator, dst_view) ||
                    !d3d12_command_allocator_add_view(list->allocator, src_view))
            {
                ERR("Failed to add views.\n");
                goto cleanup_graphics;
            }

            if (path == VKD3D_RESOLVE_IMAGE_PATH_RENDER_PASS_ATTACHMENT)
            {
                attachment_info.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
                attachment_info.storeOp = VK_ATTACHMENT_STORE_OP_NONE;
                attachment_info.imageView = src_view->vk_image_view;
                attachment_info.resolveImageView = dst_view->vk_image_view;
            }
            else
            {
                memset(&resolve_pipeline_key, 0, sizeof(resolve_pipeline_key));
                resolve_pipeline_key.path = path;
                resolve_pipeline_key.graphics.format = vk_format;
                resolve_pipeline_key.graphics.dst_aspect = (VkImageAspectFlagBits)region->dstSubresource.aspectMask;
                resolve_pipeline_key.graphics.mode = mode;

                if (FAILED(vkd3d_meta_get_resolve_image_pipeline(&list->device->meta_ops, &resolve_pipeline_key, &resolve_pipeline_info)))
                {
                    ERR("Failed to get resolve pipeline.\n");
                    return;
                }

                attachment_info.loadOp = resolve_pipeline_info.needs_stencil_mask
                        ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_DONT_CARE;
                attachment_info.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
                attachment_info.imageView = dst_view->vk_image_view;
            }

            VK_CALL(vkCmdBeginRendering(list->cmd.vk_command_buffer, &rendering_info));

            if (path == VKD3D_RESOLVE_IMAGE_PATH_RENDER_PASS_PIPELINE)
            {
                viewport.x = (float)region->dstOffset.x;
                viewport.y = (float)region->dstOffset.y;
                viewport.width = (float)region->extent.width;
                viewport.height = (float)region->extent.height;
                viewport.minDepth = 0.0f;
                viewport.maxDepth = 1.0f;

                memset(&resolve_args, 0, sizeof(resolve_args));
                resolve_args.offset.x = region->srcOffset.x - region->dstOffset.x;
                resolve_args.offset.y = region->srcOffset.y - region->dstOffset.y;

                memset(&vk_src_image_info, 0, sizeof(vk_src_image_info));
                vk_src_image_info.imageView = src_view->vk_image_view;
                vk_src_image_info.imageLayout = d3d12_resource_pick_layout(src_resource, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

                memset(&vk_descriptor_writes, 0, sizeof(vk_descriptor_writes));
                vk_descriptor_writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                vk_descriptor_writes[0].dstBinding = 0;
                vk_descriptor_writes[0].dstArrayElement = 0;
                vk_descriptor_writes[0].descriptorCount = 1;
                vk_descriptor_writes[0].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
                vk_descriptor_writes[0].pImageInfo = &vk_src_image_info;

                VK_CALL(vkCmdBindPipeline(list->cmd.vk_command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, resolve_pipeline_info.vk_pipeline));
                VK_CALL(vkCmdSetViewport(list->cmd.vk_command_buffer, 0, 1, &viewport));
                VK_CALL(vkCmdSetScissor(list->cmd.vk_command_buffer, 0, 1, &rendering_info.renderArea));
                VK_CALL(vkCmdPushDescriptorSetKHR(list->cmd.vk_command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                        resolve_pipeline_info.vk_pipeline_layout, 0, 1, &vk_descriptor_writes[0]));

                if (resolve_pipeline_info.needs_stencil_mask)
                {
                    for (j = 0; j < 8; j++)
                    {
                        resolve_args.bit_mask = 1u << j;
                        VK_CALL(vkCmdSetStencilWriteMask(list->cmd.vk_command_buffer, VK_STENCIL_FACE_FRONT_AND_BACK, resolve_args.bit_mask));
                        VK_CALL(vkCmdPushConstants(list->cmd.vk_command_buffer, resolve_pipeline_info.vk_pipeline_layout,
                                VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(resolve_args), &resolve_args));
                        VK_CALL(vkCmdDraw(list->cmd.vk_command_buffer, 3, region->dstSubresource.layerCount, 0, 0));
                    }
                }
                else
                {
                    VK_CALL(vkCmdPushConstants(list->cmd.vk_command_buffer, resolve_pipeline_info.vk_pipeline_layout,
                            VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(resolve_args), &resolve_args));
                    VK_CALL(vkCmdDraw(list->cmd.vk_command_buffer, 3, region->dstSubresource.layerCount, 0, 0));
                }
            }

            VK_CALL(vkCmdEndRendering(list->cmd.vk_command_buffer));

cleanup_graphics:
            if (dst_view)
                vkd3d_view_decref(dst_view, list->device);
            if (src_view)
                vkd3d_view_decref(src_view, list->device);
        }
    }
}

static void d3d12_command_list_resolve_subresource(struct d3d12_command_list *list,
        struct d3d12_resource *dst_resource, struct d3d12_resource *src_resource,
        const VkImageResolve2 *resolve, DXGI_FORMAT format, D3D12_RESOLVE_MODE mode)
{
    const struct vkd3d_vk_device_procs *vk_procs = &list->device->vk_procs;
    VkImageMemoryBarrier2 vk_image_barriers[2];
    enum vkd3d_resolve_image_path path;
    VkDependencyInfo dep_info;
    bool writes_full_resource;

    path = d3d12_command_list_select_resolve_path(list, dst_resource, src_resource, 1, resolve, format, mode);

    if (path == VKD3D_RESOLVE_IMAGE_PATH_UNSUPPORTED)
    {
        FIXME("Unsupported combination of resolve parameters.\n");
        return;
    }

    d3d12_command_list_end_current_render_pass(list, false);
    d3d12_command_list_end_transfer_batch(list);

    memset(&dep_info, 0, sizeof(dep_info));
    dep_info.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dep_info.imageMemoryBarrierCount = ARRAY_SIZE(vk_image_barriers);
    dep_info.pImageMemoryBarriers = vk_image_barriers;

    d3d12_get_resolve_barrier_for_dst_resource(dst_resource, resolve, path, false, dst_resource->common_layout,
            VK_PIPELINE_STAGE_2_RESOLVE_BIT, VK_ACCESS_2_NONE, &vk_image_barriers[0]);
    d3d12_get_resolve_barrier_for_src_resource(src_resource, resolve, path, false, src_resource->common_layout,
            VK_PIPELINE_STAGE_2_RESOLVE_BIT, VK_ACCESS_2_NONE, &vk_image_barriers[1]);

    VK_CALL(vkCmdPipelineBarrier2(list->cmd.vk_command_buffer, &dep_info));

    writes_full_resource = d3d12_image_copy_writes_full_subresource(
            dst_resource, &resolve->extent, &resolve->dstSubresource) &&
            d3d12_resource_get_sub_resource_count(dst_resource) == 1;

    d3d12_command_list_track_resource_usage(list, dst_resource, !writes_full_resource);
    d3d12_command_list_track_resource_usage(list, src_resource, true);

    d3d12_command_list_execute_resolve(list, dst_resource, src_resource, 1, resolve, format, mode, path);

    d3d12_get_resolve_barrier_for_dst_resource(dst_resource, resolve, path, true, dst_resource->common_layout,
            VK_PIPELINE_STAGE_2_RESOLVE_BIT, VK_ACCESS_2_NONE, &vk_image_barriers[0]);
    d3d12_get_resolve_barrier_for_src_resource(src_resource, resolve, path, true, src_resource->common_layout,
            VK_PIPELINE_STAGE_2_RESOLVE_BIT, VK_ACCESS_2_NONE, &vk_image_barriers[1]);

    VK_CALL(vkCmdPipelineBarrier2(list->cmd.vk_command_buffer, &dep_info));

    if (dst_resource->flags & VKD3D_RESOURCE_LINEAR_STAGING_COPY)
        d3d12_command_list_update_subresource_data(list, dst_resource, resolve->dstSubresource);

    VKD3D_BREADCRUMB_COOKIE(src_resource->res.cookie);
    VKD3D_BREADCRUMB_COOKIE(dst_resource->res.cookie);
    VKD3D_BREADCRUMB_AUX32(format);
    VKD3D_BREADCRUMB_AUX32(mode);
    VKD3D_BREADCRUMB_COMMAND(RESOLVE);
}

static void STDMETHODCALLTYPE d3d12_command_list_ResolveSubresource(d3d12_command_list_iface *iface,
        ID3D12Resource *dst, UINT dst_sub_resource_idx,
        ID3D12Resource *src, UINT src_sub_resource_idx, DXGI_FORMAT format)
{
    struct d3d12_command_list *list = impl_from_ID3D12GraphicsCommandList(iface);
    struct d3d12_resource *dst_resource, *src_resource;
    VkImageResolve2 vk_image_resolve;

    TRACE("iface %p, dst_resource %p, dst_sub_resource_idx %u, src_resource %p, src_sub_resource_idx %u, "
            "format %#x.\n", iface, dst, dst_sub_resource_idx, src, src_sub_resource_idx, format);

    if (list->is_inside_render_pass)
        d3d12_command_list_mark_as_invalid(list, "ResolveSubresource called within a render pass.\n");

    list->cmd.estimated_cost += VKD3D_COMMAND_COST_LOW;

    dst_resource = impl_from_ID3D12Resource(dst);
    src_resource = impl_from_ID3D12Resource(src);

    assert(d3d12_resource_is_texture(dst_resource));
    assert(d3d12_resource_is_texture(src_resource));

    vk_image_resolve.srcSubresource = vk_image_subresource_layers_from_d3d12(
            src_resource->format, src_sub_resource_idx,
            src_resource->desc.MipLevels,
            d3d12_resource_desc_get_layer_count(&src_resource->desc));
    memset(&vk_image_resolve.srcOffset, 0, sizeof(vk_image_resolve.srcOffset));
    vk_image_resolve.dstSubresource = vk_image_subresource_layers_from_d3d12(
            dst_resource->format, dst_sub_resource_idx,
            dst_resource->desc.MipLevels,
            d3d12_resource_desc_get_layer_count(&dst_resource->desc));
    memset(&vk_image_resolve.dstOffset, 0, sizeof(vk_image_resolve.dstOffset));
    vk_image_resolve.extent = d3d12_resource_desc_get_vk_subresource_extent(
            &dst_resource->desc, dst_resource->format, &vk_image_resolve.dstSubresource);

    vk_image_resolve.sType = VK_STRUCTURE_TYPE_IMAGE_RESOLVE_2_KHR;
    vk_image_resolve.pNext = NULL;

    d3d12_command_list_resolve_subresource(list, dst_resource, src_resource, &vk_image_resolve, format,
            D3D12_RESOLVE_MODE_AVERAGE);
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
    dyn_state->dirty_flags |=
            VKD3D_DYNAMIC_STATE_TOPOLOGY |
            VKD3D_DYNAMIC_STATE_PRIMITIVE_RESTART |
            VKD3D_DYNAMIC_STATE_PATCH_CONTROL_POINTS;
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
        dyn_state->dirty_flags |= VKD3D_DYNAMIC_STATE_SCISSOR;
        d3d12_command_list_invalidate_current_pipeline(list, false);
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
        vk_rect->offset.x = max(rects[i].left, 0);
        vk_rect->offset.y = max(rects[i].top, 0);
        vk_rect->extent.width = max(vk_rect->offset.x, rects[i].right) - vk_rect->offset.x;
        vk_rect->extent.height = max(vk_rect->offset.y, rects[i].bottom) - vk_rect->offset.y;
    }

    dyn_state->dirty_flags |= VKD3D_DYNAMIC_STATE_SCISSOR;
}

static void STDMETHODCALLTYPE d3d12_command_list_OMSetBlendFactor(d3d12_command_list_iface *iface,
        const FLOAT blend_factor[4])
{
    struct d3d12_command_list *list = impl_from_ID3D12GraphicsCommandList(iface);
    struct vkd3d_dynamic_state *dyn_state = &list->dynamic_state;

    TRACE("iface %p, blend_factor %p.\n", iface, blend_factor);

    if (memcmp(dyn_state->blend_constants, blend_factor, sizeof(dyn_state->blend_constants)) != 0)
    {
        memcpy(dyn_state->blend_constants, blend_factor, sizeof(dyn_state->blend_constants));
        dyn_state->dirty_flags |= VKD3D_DYNAMIC_STATE_BLEND_CONSTANTS;
    }
}

static void STDMETHODCALLTYPE d3d12_command_list_OMSetStencilRef(d3d12_command_list_iface *iface,
        UINT stencil_ref)
{
    struct d3d12_command_list *list = impl_from_ID3D12GraphicsCommandList(iface);
    struct vkd3d_dynamic_state *dyn_state = &list->dynamic_state;

    TRACE("iface %p, stencil_ref %u.\n", iface, stencil_ref);

    if (dyn_state->stencil_front.reference != stencil_ref || dyn_state->stencil_back.reference != stencil_ref)
    {
        dyn_state->stencil_front.reference = stencil_ref;
        dyn_state->stencil_back.reference = stencil_ref;
        dyn_state->dirty_flags |= VKD3D_DYNAMIC_STATE_STENCIL_REFERENCE;
    }
}

static const char *vk_stage_to_d3d(VkShaderStageFlagBits stage)
{
    switch (stage)
    {
        case VK_SHADER_STAGE_VERTEX_BIT:
            return "VS";
        case VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT:
            return "HS";
        case VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT:
            return "DS";
        case VK_SHADER_STAGE_GEOMETRY_BIT:
            return "GS";
        case VK_SHADER_STAGE_FRAGMENT_BIT:
            return "PS";
        case VK_SHADER_STAGE_TASK_BIT_EXT:
            return "AS";
        case VK_SHADER_STAGE_MESH_BIT_EXT:
            return "MS";
        case VK_SHADER_STAGE_COMPUTE_BIT:
            return "CS";
        default:
            return NULL;
    }
}

static void STDMETHODCALLTYPE d3d12_command_list_SetPipelineState(d3d12_command_list_iface *iface,
        ID3D12PipelineState *pipeline_state)
{
    struct d3d12_pipeline_state *state = impl_from_ID3D12PipelineState(pipeline_state);
    struct d3d12_command_list *list = impl_from_ID3D12GraphicsCommandList(iface);
    struct vkd3d_pipeline_bindings *bindings;
    unsigned int i;

    TRACE("iface %p, pipeline_state %p.\n", iface, pipeline_state);

    if ((TRACE_ON() || list->device->debug_ring.active) && state)
    {
        if (d3d12_pipeline_state_has_replaced_shaders(state))
        {
            TRACE("Binding pipeline state %p which has replaced shader(s)!\n", pipeline_state);
            list->has_replaced_shaders = true;
        }

        if (state->pipeline_type == VKD3D_PIPELINE_TYPE_COMPUTE)
        {
            TRACE("Binding compute module with hash: %016"PRIx64".\n", state->compute.code.meta.hash);
        }
        else if (state->pipeline_type == VKD3D_PIPELINE_TYPE_GRAPHICS ||
                state->pipeline_type == VKD3D_PIPELINE_TYPE_MESH_GRAPHICS)
        {
            for (i = 0; i < state->graphics.stage_count; i++)
            {
                TRACE("Binding graphics module with hash: %016"PRIx64" (replaced: %s).\n",
                        state->graphics.code[i].meta.hash,
                        (state->graphics.code[i].meta.flags & VKD3D_SHADER_META_FLAG_REPLACED) ? "yes" : "no");
            }
        }
    }

    if ((vkd3d_config_flags & VKD3D_CONFIG_FLAG_DEBUG_UTILS) && state &&
            list->device->vk_info.EXT_debug_utils)
    {
        char buffer[1024];
        float r, g, b, a;

        if (state->pipeline_type == VKD3D_PIPELINE_TYPE_COMPUTE)
        {
            snprintf(buffer, sizeof(buffer), "[%s %016"PRIx64" \"%s\"]",
                    vk_stage_to_d3d(VK_SHADER_STAGE_COMPUTE_BIT),
                    state->compute.code.meta.hash,
                    state->compute.code_debug.debug_entry_point_name ?
                            state->compute.code_debug.debug_entry_point_name : "N/A");
            r = 1.0f;
            g = 0.5f;
            b = 0.0f;
            a = 1.0f;
        }
        else if (state->pipeline_type == VKD3D_PIPELINE_TYPE_GRAPHICS ||
                state->pipeline_type == VKD3D_PIPELINE_TYPE_MESH_GRAPHICS)
        {
            size_t offset = 0;

            for (i = 0; i < state->graphics.stage_count && sizeof(buffer) > offset; i++)
            {
                offset += snprintf(buffer + offset, sizeof(buffer) - offset,
                        "[%s %016"PRIx64" \"%s\"] ",
                        vk_stage_to_d3d(state->graphics.stages[i].stage),
                        state->graphics.code[i].meta.hash,
                        state->graphics.code_debug[i].debug_entry_point_name ?
                                state->graphics.code_debug[i].debug_entry_point_name : "N/A");
            }

            r = 0.0f;
            g = 0.0f;
            b = 1.0f;
            a = 1.0f;
        }
        else
        {
            strcpy(buffer, "?");
            r = 0.0f;
            g = 0.0f;
            b = 0.0f;
            a = 1.0f;
        }

        d3d12_command_list_debug_mark_label(list, buffer, r, g, b, a);
    }

#ifdef VKD3D_ENABLE_BREADCRUMBS
    if ((vkd3d_config_flags & VKD3D_CONFIG_FLAG_BREADCRUMBS) && state)
    {
        struct vkd3d_breadcrumb_command cmd;
        cmd.type = VKD3D_BREADCRUMB_COMMAND_SET_SHADER_HASH;

        if (state->pipeline_type == VKD3D_PIPELINE_TYPE_COMPUTE)
        {
            cmd.shader.hash = state->compute.code.meta.hash;
            cmd.shader.stage = VK_SHADER_STAGE_COMPUTE_BIT;
            vkd3d_breadcrumb_tracer_add_command(list, &cmd);
        }
        else if (state->pipeline_type == VKD3D_PIPELINE_TYPE_GRAPHICS ||
                state->pipeline_type == VKD3D_PIPELINE_TYPE_MESH_GRAPHICS)
        {
            for (i = 0; i < state->graphics.stage_count; i++)
            {
                cmd.shader.hash = state->graphics.code[i].meta.hash;
                cmd.shader.stage = state->graphics.stages[i].stage;
                vkd3d_breadcrumb_tracer_add_command(list, &cmd);
            }
        }
    }
#endif

#ifdef VKD3D_ENABLE_RENDERDOC
    vkd3d_renderdoc_command_list_check_capture(list, state);
#endif

    if (state && state->pipeline_type != VKD3D_PIPELINE_TYPE_COMPUTE)
    {
        /* For any optionally dynamic state, we need to re-apply the corresponding static state
         * that the PSO was created with. If the same state was not dynamic in the previous
         * pipeline, the state implicitly gets marked as dirty anyway so we can ignore that here. */
        if ((state->graphics.explicit_dynamic_states & VKD3D_DYNAMIC_STATE_DEPTH_BIAS) &&
                (list->dynamic_state.depth_bias.constant_factor != state->graphics.rs_desc.depthBiasConstantFactor ||
                list->dynamic_state.depth_bias.clamp != state->graphics.rs_desc.depthBiasClamp ||
                list->dynamic_state.depth_bias.slope_factor != state->graphics.rs_desc.depthBiasSlopeFactor))
        {
            list->dynamic_state.depth_bias.constant_factor = state->graphics.rs_desc.depthBiasConstantFactor;
            list->dynamic_state.depth_bias.clamp = state->graphics.rs_desc.depthBiasClamp;
            list->dynamic_state.depth_bias.slope_factor = state->graphics.rs_desc.depthBiasSlopeFactor;
            list->dynamic_state.dirty_flags |= VKD3D_DYNAMIC_STATE_DEPTH_BIAS;
        }

        /* Primitive restart is almost always dynamic since it depends on the dynamic topology,
         * so we cannot rely on it being set in the explicit dynamic state flags. */
        if (list->dynamic_state.index_buffer_strip_cut_value != state->graphics.index_buffer_strip_cut_value)
        {
            list->dynamic_state.index_buffer_strip_cut_value = state->graphics.index_buffer_strip_cut_value;
            list->dynamic_state.dirty_flags |= VKD3D_DYNAMIC_STATE_PRIMITIVE_RESTART;
        }
    }

    if (list->state == state)
        return;

    if ((vkd3d_config_flags & VKD3D_CONFIG_FLAG_RETAIN_PSOS) && state)
        d3d12_command_allocator_retain_pipeline_state(list->allocator, state);

    d3d12_command_list_invalidate_current_pipeline(list, false);
    /* SetPSO and SetPSO1 alias the same internal active pipeline state even if they are completely different types. */
    list->state = state;
    list->rt_state = NULL;

    if (!state || list->active_pipeline_type != state->pipeline_type)
    {
        if (state)
        {
            bindings = d3d12_command_list_get_bindings(list, state->pipeline_type);
            if (bindings->root_signature)
            {
                /* We might have clobbered push constants in the new bind point,
                 * invalidate all state which can affect push constants.
                 * We might also change the pipeline layout, in case we switch between mesh and legacy graphics.
                 * In this scenario, the push constant layout will be incompatible due to stage
                 * differences, so everything must be rebound. */
                d3d12_command_list_invalidate_root_parameters(list, bindings, true, NULL);
            }

            list->active_pipeline_type = state->pipeline_type;
        }
        else
            list->active_pipeline_type = VKD3D_PIPELINE_TYPE_NONE;
    }

    if (state->pipeline_type != VKD3D_PIPELINE_TYPE_COMPUTE)
    {
        if (list->dynamic_state.stencil_front.write_mask != state->graphics.ds_desc.front.writeMask ||
                list->dynamic_state.stencil_back.write_mask != state->graphics.ds_desc.back.writeMask)
        {
            list->dynamic_state.stencil_front.write_mask = state->graphics.ds_desc.front.writeMask;
            list->dynamic_state.stencil_back.write_mask = state->graphics.ds_desc.back.writeMask;
            list->dynamic_state.dirty_flags |= VKD3D_DYNAMIC_STATE_STENCIL_WRITE_MASK;
        }
    }
    else
    {
        list->current_compute_meta_flags = state->compute.code.meta.flags;
#ifdef VKD3D_ENABLE_BREADCRUMBS
        list->current_compute_meta_flags |= vkd3d_breadcrumb_tracer_shader_hash_forces_barrier(
                &list->device->breadcrumb_tracer,
                state->compute.code.meta.hash);
#endif
    }
}

VkImageLayout vk_image_layout_from_d3d12_resource_state(
        struct d3d12_command_list *list, const struct d3d12_resource *resource, D3D12_RESOURCE_STATES state)
{
    /* Simultaneous access is always general, until we're forced to treat it differently in
     * a transfer, render pass, or similar. */
    if (resource->flags & VKD3D_RESOURCE_GENERAL_LAYOUT)
        return VK_IMAGE_LAYOUT_GENERAL;

    /* Anything generic read-related uses common layout since we get implicit promotion and decay. */
    if (state & D3D12_RESOURCE_STATE_GENERIC_READ)
        return resource->common_layout;

    switch (state)
    {
        /* These are the only layouts which cannot decay or promote,
         * and are not ambiguous in some way (depth-stencil). */
        case D3D12_RESOURCE_STATE_UNORDERED_ACCESS:
            return VK_IMAGE_LAYOUT_GENERAL;

        case D3D12_RESOURCE_STATE_RENDER_TARGET:
            return VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        case D3D12_RESOURCE_STATE_SHADING_RATE_SOURCE:
            /* This is not a promotable or decayable state, even if it's a "read-only" state.
             * VRS images also cannot be simultaneous access. */
            return VK_IMAGE_LAYOUT_FRAGMENT_SHADING_RATE_ATTACHMENT_OPTIMAL_KHR;

        case D3D12_RESOURCE_STATE_DEPTH_WRITE:
        case D3D12_RESOURCE_STATE_DEPTH_READ:
            /* DEPTH_READ only is not a shader read state, and we treat WRITE and READ more or less the same. */
            if (list)
                return d3d12_command_list_get_depth_stencil_resource_layout(list, resource, NULL);
            else
                return resource->common_layout;

        case D3D12_RESOURCE_STATE_RESOLVE_DEST:
            if (d3d12_resource_desc_is_sampler_feedback(&resource->desc))
                return VK_IMAGE_LAYOUT_GENERAL;
            else
                return resource->common_layout;

        case D3D12_RESOURCE_STATE_RESOLVE_SOURCE:
            if (d3d12_resource_desc_is_sampler_feedback(&resource->desc))
                return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            else
                return resource->common_layout;

        default:
            /* For TRANSFER or RESOLVE states, we transition in and out of common state since we have to
             * handle implicit sync anyways and TRANSFER can decay/promote. */
            return resource->common_layout;
    }
}

static VkImageLayout vk_image_layout_from_d3d12_barrier(
        struct d3d12_command_list *list, struct d3d12_resource *resource, D3D12_BARRIER_LAYOUT layout)
{
    if (layout == D3D12_BARRIER_LAYOUT_UNDEFINED)
        return VK_IMAGE_LAYOUT_UNDEFINED;

    /* Simultaneous access is always general, until we're forced to treat it differently in
     * a transfer, render pass, or similar. */
    if (resource->flags & VKD3D_RESOURCE_GENERAL_LAYOUT)
        return VK_IMAGE_LAYOUT_GENERAL;

    switch (layout)
    {
        case D3D12_BARRIER_LAYOUT_UNORDERED_ACCESS:
            return VK_IMAGE_LAYOUT_GENERAL;

        case D3D12_BARRIER_LAYOUT_RENDER_TARGET:
            return VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        case D3D12_BARRIER_LAYOUT_SHADING_RATE_SOURCE:
            return VK_IMAGE_LAYOUT_FRAGMENT_SHADING_RATE_ATTACHMENT_OPTIMAL_KHR;

        case D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_WRITE:
        case D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_READ:
            /* DEPTH_STENCIL_READ is not a shader read state (GENERIC_READ is),
             * and we treat WRITE and READ more or less the same. */
            if (list)
                return d3d12_command_list_get_depth_stencil_resource_layout(list, resource, NULL);
            else
                return resource->common_layout;

        case D3D12_BARRIER_LAYOUT_RESOLVE_DEST:
            if (d3d12_resource_desc_is_sampler_feedback(&resource->desc))
                return VK_IMAGE_LAYOUT_GENERAL;
            else
                return resource->common_layout;

        case D3D12_BARRIER_LAYOUT_RESOLVE_SOURCE:
            if (d3d12_resource_desc_is_sampler_feedback(&resource->desc))
                return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            else
                return resource->common_layout;

        default:
            return resource->common_layout;
    }
}

static void vk_image_memory_barrier_subresources_from_d3d12_texture_barrier(
        struct d3d12_command_list *list, const struct d3d12_resource *resource,
        const D3D12_BARRIER_SUBRESOURCE_RANGE *range, uint32_t dsv_decay_mask,
        VkImageSubresourceRange *vk_range)
{
    VkImageSubresource subresource;
    unsigned int i;

    if (range->NumMipLevels == 0)
    {
        /* SubresourceIndex path. */
        if (range->IndexOrFirstMipLevel == D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES)
        {
            vk_range->aspectMask = resource->format->vk_aspect_mask;
            vk_range->baseMipLevel = 0;
            vk_range->baseArrayLayer = 0;
            vk_range->levelCount = VK_REMAINING_MIP_LEVELS;
            vk_range->layerCount = VK_REMAINING_ARRAY_LAYERS;
        }
        else
        {
            subresource = d3d12_resource_get_vk_subresource(resource, range->IndexOrFirstMipLevel, false);
            vk_range->aspectMask = subresource.aspectMask;
            vk_range->baseMipLevel = subresource.mipLevel;
            vk_range->baseArrayLayer = subresource.arrayLayer;
            vk_range->levelCount = 1;
            vk_range->layerCount = 1;
        }
    }
    else
    {
        vk_range->baseMipLevel = range->IndexOrFirstMipLevel;
        vk_range->levelCount = range->NumMipLevels;
        vk_range->baseArrayLayer = range->FirstArraySlice;
        /* Oddly enough, 0 slices translates to 1 ... */
        vk_range->layerCount = max(1u, range->NumArraySlices);

        vk_range->aspectMask = 0;
        for (i = 0; i < range->NumPlanes; i++)
            vk_range->aspectMask |= vk_image_aspect_flags_from_d3d12(resource->format, i + range->FirstPlane);

        /* This is invalid in D3D12 and trips validation layers.
         * Be defensive, since apps are likely going to screw this up. */
        if (range->NumMipLevels == UINT32_MAX)
        {
            WARN("Invalid UINT32_MAX miplevels.\n");
            vk_range->levelCount = resource->desc.MipLevels - vk_range->baseMipLevel;
        }

        if (range->NumArraySlices == UINT32_MAX)
        {
            WARN("Invalid UINT32_MAX array slices.\n");
            vk_range->layerCount = d3d12_resource_desc_get_layer_count(&resource->desc) - vk_range->baseArrayLayer;
        }
    }

    /* In a decay, need to transition everything that we promoted back to the common state.
     * DSV decay is all or nothing, so just use a full transition. */
    if ((dsv_decay_mask & VKD3D_DEPTH_PLANE_OPTIMAL) &&
            (resource->format->vk_aspect_mask & VK_IMAGE_ASPECT_DEPTH_BIT))
    {
        vk_range->aspectMask |= VK_IMAGE_ASPECT_DEPTH_BIT;
        vk_range->baseMipLevel = 0;
        vk_range->baseArrayLayer = 0;
        vk_range->levelCount = VK_REMAINING_MIP_LEVELS;
        vk_range->layerCount = VK_REMAINING_ARRAY_LAYERS;
    }

    if ((dsv_decay_mask & VKD3D_STENCIL_PLANE_OPTIMAL) &&
            (resource->format->vk_aspect_mask & VK_IMAGE_ASPECT_STENCIL_BIT))
    {
        vk_range->aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
        vk_range->baseMipLevel = 0;
        vk_range->baseArrayLayer = 0;
        vk_range->levelCount = VK_REMAINING_MIP_LEVELS;
        vk_range->layerCount = VK_REMAINING_ARRAY_LAYERS;
    }
}

static void vk_image_memory_barrier_for_transition(
        VkImageMemoryBarrier2 *image_barrier, const struct d3d12_resource *resource,
        UINT subresource_idx, VkImageLayout old_layout, VkImageLayout new_layout,
        VkPipelineStageFlags2 src_stages, VkAccessFlags2 src_access,
        VkPipelineStageFlags2 dst_stages, VkAccessFlags2 dst_access,
        uint32_t dsv_decay_mask)
{
    memset(image_barrier, 0, sizeof(*image_barrier));
    image_barrier->sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    image_barrier->oldLayout = old_layout;
    image_barrier->newLayout = new_layout;
    image_barrier->srcStageMask = src_stages;
    image_barrier->srcAccessMask = src_access;
    image_barrier->dstStageMask = dst_stages;
    image_barrier->dstAccessMask = dst_access;
    image_barrier->srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    image_barrier->dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    image_barrier->image = resource->res.vk_image;

    if (subresource_idx != D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES)
    {
        VkImageSubresource subresource;

        subresource = d3d12_resource_get_vk_subresource(resource, subresource_idx, false);
        image_barrier->subresourceRange.aspectMask = subresource.aspectMask;
        image_barrier->subresourceRange.baseMipLevel = subresource.mipLevel;
        image_barrier->subresourceRange.baseArrayLayer = subresource.arrayLayer;
        image_barrier->subresourceRange.levelCount = 1;
        image_barrier->subresourceRange.layerCount = 1;

        /* In a decay, need to transition everything that we promoted back to the common state.
         * DSV decay is all or nothing, so just use a full transition. */
        if ((dsv_decay_mask & VKD3D_DEPTH_PLANE_OPTIMAL) &&
                (resource->format->vk_aspect_mask & VK_IMAGE_ASPECT_DEPTH_BIT))
        {
            image_barrier->subresourceRange.aspectMask |= VK_IMAGE_ASPECT_DEPTH_BIT;
            image_barrier->subresourceRange.baseMipLevel = 0;
            image_barrier->subresourceRange.baseArrayLayer = 0;
            image_barrier->subresourceRange.levelCount = VK_REMAINING_MIP_LEVELS;
            image_barrier->subresourceRange.layerCount = VK_REMAINING_ARRAY_LAYERS;
        }

        if ((dsv_decay_mask & VKD3D_STENCIL_PLANE_OPTIMAL) &&
                (resource->format->vk_aspect_mask & VK_IMAGE_ASPECT_STENCIL_BIT))
        {
            image_barrier->subresourceRange.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
            image_barrier->subresourceRange.baseMipLevel = 0;
            image_barrier->subresourceRange.baseArrayLayer = 0;
            image_barrier->subresourceRange.levelCount = VK_REMAINING_MIP_LEVELS;
            image_barrier->subresourceRange.layerCount = VK_REMAINING_ARRAY_LAYERS;
        }
    }
    else
    {
        image_barrier->subresourceRange.aspectMask = resource->format->vk_aspect_mask;
        image_barrier->subresourceRange.baseMipLevel = 0;
        image_barrier->subresourceRange.baseArrayLayer = 0;
        image_barrier->subresourceRange.levelCount = VK_REMAINING_MIP_LEVELS;
        image_barrier->subresourceRange.layerCount = VK_REMAINING_ARRAY_LAYERS;
    }
}

static void d3d12_command_list_barrier_batch_init(struct d3d12_command_list_barrier_batch *batch)
{
    batch->image_barrier_count = 0;

    memset(&batch->vk_memory_barrier, 0, sizeof(batch->vk_memory_barrier));
    batch->vk_memory_barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
}

static void d3d12_command_list_barrier_batch_end(struct d3d12_command_list *list,
        struct d3d12_command_list_barrier_batch *batch)
{
    const struct vkd3d_vk_device_procs *vk_procs = &list->device->vk_procs;
    VkDependencyInfo dep_info;

    memset(&dep_info, 0, sizeof(dep_info));
    dep_info.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dep_info.imageMemoryBarrierCount = batch->image_barrier_count;
    dep_info.pImageMemoryBarriers = batch->vk_image_barriers;

    if (batch->vk_memory_barrier.srcStageMask || batch->vk_memory_barrier.dstStageMask)
    {
        dep_info.memoryBarrierCount = 1;
        dep_info.pMemoryBarriers = &batch->vk_memory_barrier;

#ifdef VKD3D_ENABLE_DESCRIPTOR_QA
        /* Somewhat crude approximation. TODO: Refine as required. */
        if ((batch->vk_memory_barrier.srcAccessMask & VK_ACCESS_2_SHADER_WRITE_BIT) ||
                (batch->vk_memory_barrier.dstAccessMask &
                        (VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_UNIFORM_READ_BIT)))
        {
            uint32_t cookie;
            cookie = vkd3d_descriptor_debug_clear_bloom_filter(list->device->descriptor_qa_global_info,
                    list->device, list->cmd.vk_command_buffer);
            VKD3D_BREADCRUMB_AUX32(cookie);
            VKD3D_BREADCRUMB_COMMAND(SYNC_VAL_CLEAR);
        }
#endif
    }

    if (dep_info.imageMemoryBarrierCount || dep_info.memoryBarrierCount)
    {
        VK_CALL(vkCmdPipelineBarrier2(list->cmd.vk_command_buffer, &dep_info));

        batch->vk_memory_barrier.srcStageMask = 0;
        batch->vk_memory_barrier.srcAccessMask = 0;
        batch->vk_memory_barrier.dstStageMask = 0;
        batch->vk_memory_barrier.dstAccessMask = 0;

        batch->image_barrier_count = 0;
    }

    if (list->cmd.clear_uav_pending)
    {
        if ((batch->vk_memory_barrier.srcStageMask &
            (VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT)) &&
            (batch->vk_memory_barrier.dstStageMask &
            (VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT)))
        {
            list->cmd.clear_uav_pending = false;
        }

        if (list->cmd.clear_uav_pending)
        {
            uint32_t i;
            for (i = 0; i < batch->image_barrier_count; i++)
            {
                if ((batch->vk_image_barriers[i].srcStageMask &
                    (VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT)) &&
                    (batch->vk_image_barriers[i].dstStageMask &
                    (VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT)))
                {
                    list->cmd.clear_uav_pending = false;
                    break;
                }
            }
        }
    }
}

static bool vk_subresource_range_overlaps(uint32_t base_a, uint32_t count_a, uint32_t base_b, uint32_t count_b)
{
    uint32_t end_a, end_b;
    end_a = count_a == UINT32_MAX ? UINT32_MAX : base_a + count_a;
    end_b = count_b == UINT32_MAX ? UINT32_MAX : base_b + count_b;
    if (base_a <= base_b)
        return end_a > base_b;
    else
        return end_b > base_a;
}

static bool vk_image_barrier_overlaps_subresource(const VkImageMemoryBarrier2 *a, const VkImageMemoryBarrier2 *b,
        bool *exact_match)
{
    *exact_match = false;
    if (a->image != b->image)
        return false;
    if (!(a->subresourceRange.aspectMask & b->subresourceRange.aspectMask))
        return false;

    *exact_match = a->subresourceRange.aspectMask == b->subresourceRange.aspectMask &&
            a->subresourceRange.baseMipLevel == b->subresourceRange.baseMipLevel &&
            a->subresourceRange.levelCount == b->subresourceRange.levelCount &&
            a->subresourceRange.baseArrayLayer == b->subresourceRange.baseArrayLayer &&
            a->subresourceRange.layerCount == b->subresourceRange.layerCount;

    return vk_subresource_range_overlaps(
            a->subresourceRange.baseMipLevel, a->subresourceRange.levelCount,
            b->subresourceRange.baseMipLevel, b->subresourceRange.levelCount) &&
            vk_subresource_range_overlaps(
                    a->subresourceRange.baseArrayLayer, a->subresourceRange.layerCount,
                    b->subresourceRange.baseArrayLayer, b->subresourceRange.layerCount);
}

static void d3d12_command_list_barrier_batch_add_layout_transition(
        struct d3d12_command_list *list,
        struct d3d12_command_list_barrier_batch *batch,
        const VkImageMemoryBarrier2 *image_barrier)
{
    bool layout_match, exact_match;
    uint32_t i;

    if (batch->image_barrier_count == ARRAY_SIZE(batch->vk_image_barriers))
        d3d12_command_list_barrier_batch_end(list, batch);

    for (i = 0; i < batch->image_barrier_count; i++)
    {
        if (vk_image_barrier_overlaps_subresource(image_barrier, &batch->vk_image_barriers[i], &exact_match))
        {
            /* The barrier batch is used at two places: ResourceBarrier and CopyTextureRegion, which results in
             * different kind of overlaps.
             * In CopyTextureRegion() we can have two copies on the same src or dst resource batched into one,
             * resulting in an exact duplicate layout transition.
             * In ResourceBarrier() we won't have such exact duplicates, mainly because doing the same transition twice
             * is illegal.
             * The reason to test for overlap is that barriers in D3D12 behaves as if each transition happens in
             * order. Vulkan memory barriers do not, so if there is a race condition, we need to split the barrier.
             * As such, we need to eliminate duplicates like the former, while cutting the batch when we encounter an
             * overlap like the latter. */
            layout_match = image_barrier->oldLayout == batch->vk_image_barriers[i].oldLayout &&
                    image_barrier->newLayout == batch->vk_image_barriers[i].newLayout;
            if (exact_match && layout_match)
            {
                /* Exact duplicate, skip this barrier. */
                return;
            }
            else
            {
                /* Overlap, break the batch and add barrier. */
                d3d12_command_list_barrier_batch_end(list, batch);
                break;
            }
        }
    }

    batch->vk_image_barriers[batch->image_barrier_count++] = *image_barrier;
}

static void d3d12_command_list_barrier_batch_add_global_transition(
        struct d3d12_command_list *list,
        struct d3d12_command_list_barrier_batch *batch,
        VkPipelineStageFlags2 srcStageMask, VkAccessFlags2 srcAccessMask,
        VkPipelineStageFlags2 dstStageMask, VkAccessFlags2 dstAccessMask)
{
    batch->vk_memory_barrier.srcStageMask |= srcStageMask;
    batch->vk_memory_barrier.srcAccessMask |= srcAccessMask;
    batch->vk_memory_barrier.dstStageMask |= dstStageMask;
    batch->vk_memory_barrier.dstAccessMask |= dstAccessMask;
}

static void d3d12_command_list_merge_copy_tracking(struct d3d12_command_list *list,
        struct d3d12_command_list_barrier_batch *batch)
{
    /* If we're going to do transfer barriers and we have
     * pending copies in flight which need to be synchronized,
     * we should just resolve that while we're at it. */
    d3d12_command_list_reset_buffer_copy_tracking(list);
    d3d12_command_list_barrier_batch_add_global_transition(list, batch,
            VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
            VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
}

static void d3d12_command_list_merge_copy_tracking_transition(struct d3d12_command_list *list,
        const D3D12_RESOURCE_TRANSITION_BARRIER *transition,
        struct d3d12_command_list_barrier_batch *batch)
{
    if (list->tracked_copy_buffer_count && (
            transition->StateBefore == D3D12_RESOURCE_STATE_COPY_DEST ||
            transition->StateAfter == D3D12_RESOURCE_STATE_COPY_DEST))
    {
        d3d12_command_list_merge_copy_tracking(list, batch);
    }
}

static void STDMETHODCALLTYPE d3d12_command_list_ResourceBarrier(d3d12_command_list_iface *iface,
        UINT barrier_count, const D3D12_RESOURCE_BARRIER *barriers)
{
    struct d3d12_command_list *list = impl_from_ID3D12GraphicsCommandList(iface);
    struct d3d12_command_list_barrier_batch batch;
    bool have_split_barriers = false;
    unsigned int i, j;

    TRACE("iface %p, barrier_count %u, barriers %p.\n", iface, barrier_count, barriers);

    d3d12_command_list_end_current_render_pass(list, false);
    d3d12_command_list_end_transfer_batch(list);
    d3d12_command_list_barrier_batch_init(&batch);

    d3d12_command_list_debug_mark_begin_region(list, "ResourceBarrier");

    for (i = 0; i < barrier_count; ++i)
    {
        const D3D12_RESOURCE_BARRIER *current = &barriers[i];
        struct d3d12_resource *preserve_resource = NULL;

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
                VkAccessFlags2 transition_src_access = 0, transition_dst_access = 0;
                VkPipelineStageFlags2 transition_src_stage_mask = 0;
                VkPipelineStageFlags2 transition_dst_stage_mask = 0;
                VkImageLayout old_layout = VK_IMAGE_LAYOUT_UNDEFINED;
                VkImageLayout new_layout = VK_IMAGE_LAYOUT_UNDEFINED;
                uint32_t dsv_decay_mask = 0;

                /* If we have not observed any transition to INDIRECT_ARGUMENT it means
                 * that in this command buffer there couldn't legally have been writes to an indirect
                 * command buffer. The docs mention an implementation strategy where we can do this optimization.
                 * This is very handy when handling back-to-back ExecuteIndirects(). */
                if (transition->StateAfter == D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT)
                {
                    d3d12_command_list_debug_mark_label(list, "Indirect Argument barrier", 1.0f, 1.0f, 0.0f, 1.0f);
                    /* Any indirect patching commands now have to go to normal command buffer, unless we split the sequence. */
                    list->cmd.vk_init_commands_post_indirect_barrier = list->cmd.vk_command_buffer;
                }

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

                if (!(preserve_resource = impl_from_ID3D12Resource(transition->pResource)))
                {
                    d3d12_command_list_mark_as_invalid(list, "A resource pointer is NULL.");
                    continue;
                }

                VKD3D_BREADCRUMB_COOKIE(preserve_resource ? preserve_resource->res.cookie : 0);
                VKD3D_BREADCRUMB_AUX32(transition->Subresource);
                VKD3D_BREADCRUMB_AUX32(transition->StateBefore);
                VKD3D_BREADCRUMB_AUX32(transition->StateAfter);
                VKD3D_BREADCRUMB_TAG("Resource Transition");

                /* Flush pending RTAS updates in case a scratch buffer or input resource gets transitioned */
                if (d3d12_resource_is_buffer(preserve_resource) && (transition->StateBefore == D3D12_RESOURCE_STATE_UNORDERED_ACCESS ||
                        transition->StateBefore == D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE))
                    d3d12_command_list_flush_rtas_batch(list);

                /* If the resource is a host-visible image and has been used as a UAV, schedule a
                 * subresource update since we cannot know when it is being written in a shader. */
                if (transition->StateBefore == D3D12_RESOURCE_STATE_UNORDERED_ACCESS &&
                        (preserve_resource->flags & VKD3D_RESOURCE_LINEAR_STAGING_COPY))
                {
                    VkImageSubresourceLayers vk_subresource_layers;
                    VkImageSubresource vk_subresource;

                    if (transition->Subresource == D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES)
                    {
                        vk_subresource_layers.aspectMask = preserve_resource->format->vk_aspect_mask;
                        vk_subresource_layers.baseArrayLayer = 0;
                        vk_subresource_layers.layerCount = d3d12_resource_desc_get_layer_count(&preserve_resource->desc);

                        for (j = 0; j < preserve_resource->desc.MipLevels; j++)
                        {
                            vk_subresource_layers.mipLevel = j;
                            d3d12_command_list_update_subresource_data(list, preserve_resource, vk_subresource_layers);
                        }
                    }
                    else
                    {
                        vk_subresource = d3d12_resource_get_vk_subresource(preserve_resource, transition->Subresource, false);
                        vk_subresource_layers = vk_subresource_layers_from_subresource(&vk_subresource);
                        vk_subresource.aspectMask = preserve_resource->format->vk_aspect_mask;
                        d3d12_command_list_update_subresource_data(list, preserve_resource, vk_subresource_layers);
                    }
                }

                d3d12_command_list_merge_copy_tracking_transition(list, transition, &batch);

                vk_access_and_stage_flags_from_d3d12_resource_state(list, preserve_resource,
                        transition->StateBefore, list->vk_queue_flags, &transition_src_stage_mask,
                        &transition_src_access);
                if (d3d12_resource_is_texture(preserve_resource))
                    old_layout = vk_image_layout_from_d3d12_resource_state(list, preserve_resource, transition->StateBefore);

                if (preserve_resource->desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL)
                {
                    /* If we enter DEPTH_WRITE or DEPTH_READ we can promote to optimal. */
                    /* Depth-stencil aspects are transitioned all or nothing.
                     * If just one aspect transitions out of READ_ONLY, we have a decay situation.
                     * We must transition the entire image from optimal state to read-only state. */
                    dsv_decay_mask = d3d12_command_list_notify_dsv_state(list, preserve_resource,
                            transition->StateAfter, transition->Subresource);
                }

                vk_access_and_stage_flags_from_d3d12_resource_state(list, preserve_resource,
                        transition->StateAfter, list->vk_queue_flags, &transition_dst_stage_mask,
                        &transition_dst_access);
                if (d3d12_resource_is_texture(preserve_resource))
                    new_layout = vk_image_layout_from_d3d12_resource_state(list, preserve_resource, transition->StateAfter);

                if (old_layout != new_layout)
                {
                    VkImageMemoryBarrier2 vk_transition;
                    vk_image_memory_barrier_for_transition(&vk_transition,
                            preserve_resource,
                            transition->Subresource, old_layout, new_layout,
                            transition_src_stage_mask, transition_src_access,
                            transition_dst_stage_mask, transition_dst_access,
                            dsv_decay_mask);
                    d3d12_command_list_barrier_batch_add_layout_transition(list, &batch, &vk_transition);
                }
                else
                {
                    d3d12_command_list_barrier_batch_add_global_transition(list, &batch,
                            transition_src_stage_mask, transition_src_access,
                            transition_dst_stage_mask, transition_dst_access);
                }

                TRACE("Transition barrier (resource %p, subresource %#x, before %#x, after %#x).\n",
                        preserve_resource, transition->Subresource, transition->StateBefore, transition->StateAfter);
                break;
            }

            case D3D12_RESOURCE_BARRIER_TYPE_UAV:
            {
                const D3D12_RESOURCE_UAV_BARRIER *uav = &current->UAV;
                uint32_t state_mask;

                preserve_resource = impl_from_ID3D12Resource(uav->pResource);

                VKD3D_BREADCRUMB_COOKIE(preserve_resource ? preserve_resource->res.cookie : 0);
                VKD3D_BREADCRUMB_COOKIE(preserve_resource ? preserve_resource->mem.resource.cookie : 0);
                VKD3D_BREADCRUMB_TAG("UAV Barrier");

                /* The only way to synchronize an RTAS is UAV barriers,
                 * as their resource state must be frozen.
                 * If we don't know the resource, we must assume a global UAV transition
                 * which also includes RTAS. */
                state_mask = 0;

                /* Flush pending RTAS builds if the resource could be an RTAS or scratch buffer */
                if (!preserve_resource || d3d12_resource_is_buffer(preserve_resource))
                    d3d12_command_list_flush_rtas_batch(list);

                if (!preserve_resource || d3d12_resource_is_acceleration_structure(preserve_resource))
                    state_mask |= D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE;
                if (!preserve_resource || !d3d12_resource_is_acceleration_structure(preserve_resource))
                    state_mask |= D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

                assert(state_mask);

                vk_access_and_stage_flags_from_d3d12_resource_state(list, preserve_resource,
                        state_mask, list->vk_queue_flags,
                        &batch.vk_memory_barrier.srcStageMask,
                        &batch.vk_memory_barrier.srcAccessMask);

                vk_access_and_stage_flags_from_d3d12_resource_state(list, preserve_resource,
                        state_mask, list->vk_queue_flags,
                        &batch.vk_memory_barrier.dstStageMask,
                        &batch.vk_memory_barrier.dstAccessMask);

                TRACE("UAV barrier (resource %p).\n", preserve_resource);
                break;
            }

            case D3D12_RESOURCE_BARRIER_TYPE_ALIASING:
            {
                const D3D12_RESOURCE_ALIASING_BARRIER *alias;
                struct d3d12_resource *before, *after;
                VkAccessFlags2 alias_src_access;
                VkAccessFlags2 alias_dst_access;

                alias = &current->Aliasing;
                TRACE("Aliasing barrier (before %p, after %p).\n", alias->pResourceBefore, alias->pResourceAfter);
                before = impl_from_ID3D12Resource(alias->pResourceBefore);
                after = impl_from_ID3D12Resource(alias->pResourceAfter);

                VKD3D_BREADCRUMB_TAG("Aliasing Barrier");

                if (d3d12_resource_may_alias_other_resources(before) && d3d12_resource_may_alias_other_resources(after))
                {
                    /* Flush pending RTAS builds if we're disabling a potential input resource, scratch or RTAS buffer */
                    if (!before || d3d12_resource_is_buffer(before))
                        d3d12_command_list_flush_rtas_batch(list);

                    /* Aliasing barriers in D3D12 are extremely weird and don't behavior like you would expect.
                     * For buffer aliasing, it is basically a global memory barrier, but for images it gets
                     * quite weird. We cannot perform UNDEFINED transitions here, even if that is what makes sense.
                     * UNDEFINED transitions are deferred to their required "activation" command, which is a full-subresource
                     * command that writes every pixel. We detect those cases and perform a transition away from UNDEFINED. */
                    alias_src_access = VK_ACCESS_2_MEMORY_WRITE_BIT;

                    if (after && d3d12_resource_is_texture(after))
                    {
                        /* To correctly alias images, it is required to perform an initializing operation
                         * at a later time. This includes fully clearing a render target, full subresource CopyResource,
                         * etc. In all those cases we will handle UNDEFINED layouts. Making memory visible is redundant at this stage,
                         * since memory only needs to be available to perform transition away from UNDEFINED.
                         * Thus, at the very least, we need srcAccessMask to be correct in the aliasing barrier. */
                        alias_dst_access = 0;
                    }
                    else
                    {
                        alias_dst_access = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
                    }

                    batch.vk_memory_barrier.srcStageMask |= VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
                    batch.vk_memory_barrier.srcAccessMask |= alias_src_access;
                    batch.vk_memory_barrier.dstStageMask |= VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
                    batch.vk_memory_barrier.dstAccessMask |= alias_dst_access;

                    /* Update staging copies of aliased images before the current barrier batch gets submitted */
                    d3d12_command_list_flush_subresource_updates(list);
                }
                break;
            }

            default:
                WARN("Invalid barrier type %#x.\n", current->Type);
                continue;
        }

        if (preserve_resource)
            d3d12_command_list_track_resource_usage(list, preserve_resource, true);
    }

    d3d12_command_list_barrier_batch_end(list, &batch);

    /* Vulkan doesn't support split barriers. */
    if (have_split_barriers)
        WARN("Issuing split barrier(s) on D3D12_RESOURCE_BARRIER_FLAG_END_ONLY.\n");

    VKD3D_BREADCRUMB_COMMAND(BARRIER);

    d3d12_command_list_debug_mark_end_region(list);
}

static void STDMETHODCALLTYPE d3d12_command_list_ExecuteBundle(d3d12_command_list_iface *iface,
        ID3D12GraphicsCommandList *command_list)
{
    struct d3d12_bundle *bundle;

    TRACE("iface %p, command_list %p.\n", iface, command_list);

    if (!(bundle = d3d12_bundle_from_iface(command_list)))
    {
        WARN("Command list %p not a bundle.\n", command_list);
        return;
    }

    d3d12_bundle_execute(bundle, iface);
}

static void vkd3d_pipeline_bindings_set_dirty_sets(struct vkd3d_pipeline_bindings *bindings, uint64_t dirty_mask)
{
    bindings->descriptor_heap_dirty_mask = dirty_mask;
    bindings->dirty_flags |= VKD3D_PIPELINE_DIRTY_HOISTED_DESCRIPTORS;
}

static void d3d12_command_list_set_descriptor_heaps_buffers(struct d3d12_command_list *list,
        unsigned int heap_count, ID3D12DescriptorHeap *const *heaps)
{
    struct vkd3d_bindless_state *bindless_state = &list->device->bindless_state;
    VkDeviceAddress current_resource_va, current_sampler_va;
    struct d3d12_desc_split d;
    unsigned int i, j;

    current_resource_va = list->descriptor_heap.buffers.heap_va_resource;
    current_sampler_va = list->descriptor_heap.buffers.heap_va_sampler;

    for (i = 0; i < heap_count; i++)
    {
        struct d3d12_descriptor_heap *heap = impl_from_ID3D12DescriptorHeap(heaps[i]);
        unsigned int set_index = 0;

        if (!heap)
            continue;

        if (heap->desc.Type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV)
        {
            list->descriptor_heap.buffers.heap_va_resource = heap->descriptor_buffer.va;
            list->descriptor_heap.buffers.vk_buffer_resource = heap->descriptor_buffer.vk_buffer;

            if (!d3d12_device_use_embedded_mutable_descriptors(list->device))
            {
                /* In case we need to hoist buffer descriptors. */
                d = d3d12_desc_decode_va(heap->cpu_va.ptr);
                list->cbv_srv_uav_descriptors_view = d.view;
            }
        }
        else if (heap->desc.Type == D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER)
        {
            list->descriptor_heap.buffers.heap_va_sampler = heap->descriptor_buffer.va;
        }

        for (j = 0; j < bindless_state->set_count; j++)
            if (bindless_state->set_info[j].heap_type == heap->desc.Type)
                list->descriptor_heap.buffers.vk_offsets[j] = heap->descriptor_buffer.offsets[set_index++];
    }

    if (current_resource_va == list->descriptor_heap.buffers.heap_va_resource &&
            current_sampler_va == list->descriptor_heap.buffers.heap_va_sampler)
        return;

    list->descriptor_heap.buffers.heap_dirty = true;
    /* Invalidation is a bit more aggressive for descriptor buffers.
     * We also need to invalidate any push descriptors. */
    d3d12_command_list_invalidate_root_parameters(list, &list->graphics_bindings, true, NULL);
    d3d12_command_list_invalidate_root_parameters(list, &list->compute_bindings, true, NULL);
}

static void d3d12_command_list_set_descriptor_heaps_sets(struct d3d12_command_list *list,
        unsigned int heap_count, ID3D12DescriptorHeap *const *heaps)
{
    struct vkd3d_bindless_state *bindless_state = &list->device->bindless_state;
    uint64_t dirty_mask = 0;
    unsigned int i, j;

    for (i = 0; i < heap_count; i++)
    {
        struct d3d12_descriptor_heap *heap = impl_from_ID3D12DescriptorHeap(heaps[i]);
        unsigned int set_index = 0;

        if (!heap)
            continue;

        for (j = 0; j < bindless_state->set_count; j++)
        {
            if (bindless_state->set_info[j].heap_type != heap->desc.Type)
                continue;

            list->descriptor_heap.sets.vk_sets[j] = heap->sets[set_index++].vk_descriptor_set;
            dirty_mask |= 1ull << j;
        }

        /* In case we need to hoist buffer descriptors. */
        if (heap->desc.Type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV)
        {
            struct d3d12_desc_split d;
            d = d3d12_desc_decode_va(heap->cpu_va.ptr);
            list->cbv_srv_uav_descriptors_view = d.view;
        }
    }

    vkd3d_pipeline_bindings_set_dirty_sets(&list->graphics_bindings, dirty_mask);
    vkd3d_pipeline_bindings_set_dirty_sets(&list->compute_bindings, dirty_mask);
}

static void STDMETHODCALLTYPE d3d12_command_list_SetDescriptorHeaps(d3d12_command_list_iface *iface,
        UINT heap_count, ID3D12DescriptorHeap *const *heaps)
{
    struct d3d12_command_list *list = impl_from_ID3D12GraphicsCommandList(iface);

    TRACE("iface %p, heap_count %u, heaps %p.\n", iface, heap_count, heaps);

    if (d3d12_device_uses_descriptor_buffers(list->device))
        d3d12_command_list_set_descriptor_heaps_buffers(list, heap_count, heaps);
    else
        d3d12_command_list_set_descriptor_heaps_sets(list, heap_count, heaps);
}

static void d3d12_command_list_set_root_signature(struct d3d12_command_list *list,
        struct vkd3d_pipeline_bindings *bindings, const struct d3d12_root_signature *root_signature)
{
    if (bindings->root_signature == root_signature)
        return;

    bindings->root_signature = root_signature;
    bindings->static_sampler_set = VK_NULL_HANDLE;

    if (root_signature)
        bindings->static_sampler_set = root_signature->vk_sampler_set;

    d3d12_command_list_invalidate_root_parameters(list, bindings, true, NULL);
}

static void STDMETHODCALLTYPE d3d12_command_list_SetComputeRootSignature(d3d12_command_list_iface *iface,
        ID3D12RootSignature *root_signature)
{
    struct d3d12_command_list *list = impl_from_ID3D12GraphicsCommandList(iface);

    TRACE("iface %p, root_signature %p.\n", iface, root_signature);

    d3d12_command_list_set_root_signature(list, &list->compute_bindings,
            impl_from_ID3D12RootSignature(root_signature));

    /* Changing compute root signature means we might have to bind a different RTPSO variant. */
    if (list->rt_state)
        d3d12_command_list_invalidate_current_pipeline(list, false);
}

static void STDMETHODCALLTYPE d3d12_command_list_SetGraphicsRootSignature(d3d12_command_list_iface *iface,
        ID3D12RootSignature *root_signature)
{
    struct d3d12_command_list *list = impl_from_ID3D12GraphicsCommandList(iface);

    TRACE("iface %p, root_signature %p.\n", iface, root_signature);

    d3d12_command_list_set_root_signature(list, &list->graphics_bindings,
            impl_from_ID3D12RootSignature(root_signature));
}

static inline void d3d12_command_list_set_descriptor_table_embedded(struct d3d12_command_list *list,
        struct vkd3d_pipeline_bindings *bindings, unsigned int index,
        D3D12_GPU_DESCRIPTOR_HANDLE base_descriptor,
        unsigned int cbv_srv_uav_size_log2,
        unsigned int sampler_size_log2)
{
    const struct d3d12_root_signature *root_signature = bindings->root_signature;

    assert(index < ARRAY_SIZE(bindings->descriptor_tables));
    bindings->descriptor_tables[index] = d3d12_desc_heap_offset_from_embedded_gpu_handle(
            base_descriptor, cbv_srv_uav_size_log2, sampler_size_log2);

    if (root_signature)
    {
        if (root_signature->descriptor_table_count)
            bindings->dirty_flags |= VKD3D_PIPELINE_DIRTY_DESCRIPTOR_TABLE_OFFSETS;
        if (root_signature->hoist_info.num_desc)
            bindings->dirty_flags |= VKD3D_PIPELINE_DIRTY_HOISTED_DESCRIPTORS;
    }

    VKD3D_BREADCRUMB_AUX32(index);
    VKD3D_BREADCRUMB_AUX32(bindings->descriptor_tables[index]);
    VKD3D_BREADCRUMB_COMMAND_STATE(ROOT_TABLE);
}

static inline void d3d12_command_list_set_descriptor_table(struct d3d12_command_list *list,
        struct vkd3d_pipeline_bindings *bindings, unsigned int index, D3D12_GPU_DESCRIPTOR_HANDLE base_descriptor)
{
    const struct d3d12_root_signature *root_signature = bindings->root_signature;

    assert(index < ARRAY_SIZE(bindings->descriptor_tables));
    bindings->descriptor_tables[index] = d3d12_desc_heap_offset_from_gpu_handle(base_descriptor);

    if (root_signature)
    {
        if (root_signature->descriptor_table_count)
            bindings->dirty_flags |= VKD3D_PIPELINE_DIRTY_DESCRIPTOR_TABLE_OFFSETS;
        if (root_signature->hoist_info.num_desc)
            bindings->dirty_flags |= VKD3D_PIPELINE_DIRTY_HOISTED_DESCRIPTORS;
    }

    VKD3D_BREADCRUMB_AUX32(index);
    VKD3D_BREADCRUMB_AUX32(bindings->descriptor_tables[index]);
    VKD3D_BREADCRUMB_COMMAND_STATE(ROOT_TABLE);
}

static void STDMETHODCALLTYPE d3d12_command_list_SetComputeRootDescriptorTable_embedded_64_16(
        d3d12_command_list_iface *iface,
        UINT root_parameter_index, D3D12_GPU_DESCRIPTOR_HANDLE base_descriptor)
{
    struct d3d12_command_list *list = impl_from_ID3D12GraphicsCommandList(iface);

    TRACE("iface %p, root_parameter_index %u, base_descriptor %#"PRIx64".\n",
            iface, root_parameter_index, base_descriptor.ptr);

    d3d12_command_list_set_descriptor_table_embedded(list, &list->compute_bindings,
            root_parameter_index, base_descriptor, 6, 4);
}

static void STDMETHODCALLTYPE d3d12_command_list_SetGraphicsRootDescriptorTable_embedded_64_16(
        d3d12_command_list_iface *iface,
        UINT root_parameter_index, D3D12_GPU_DESCRIPTOR_HANDLE base_descriptor)
{
    struct d3d12_command_list *list = impl_from_ID3D12GraphicsCommandList(iface);

    TRACE("iface %p, root_parameter_index %u, base_descriptor %#"PRIx64".\n",
            iface, root_parameter_index, base_descriptor.ptr);

    d3d12_command_list_set_descriptor_table_embedded(list, &list->graphics_bindings,
            root_parameter_index, base_descriptor, 6, 4);
}

static void STDMETHODCALLTYPE d3d12_command_list_SetComputeRootDescriptorTable_embedded_32_16(
        d3d12_command_list_iface *iface,
        UINT root_parameter_index, D3D12_GPU_DESCRIPTOR_HANDLE base_descriptor)
{
    struct d3d12_command_list *list = impl_from_ID3D12GraphicsCommandList(iface);

    TRACE("iface %p, root_parameter_index %u, base_descriptor %#"PRIx64".\n",
            iface, root_parameter_index, base_descriptor.ptr);

    d3d12_command_list_set_descriptor_table_embedded(list, &list->compute_bindings,
            root_parameter_index, base_descriptor, 5, 4);
}

static void STDMETHODCALLTYPE d3d12_command_list_SetGraphicsRootDescriptorTable_embedded_32_16(
        d3d12_command_list_iface *iface,
        UINT root_parameter_index, D3D12_GPU_DESCRIPTOR_HANDLE base_descriptor)
{
    struct d3d12_command_list *list = impl_from_ID3D12GraphicsCommandList(iface);

    TRACE("iface %p, root_parameter_index %u, base_descriptor %#"PRIx64".\n",
            iface, root_parameter_index, base_descriptor.ptr);

    d3d12_command_list_set_descriptor_table_embedded(list, &list->graphics_bindings,
            root_parameter_index, base_descriptor, 5, 4);
}

static void STDMETHODCALLTYPE d3d12_command_list_SetComputeRootDescriptorTable_embedded_default(
        d3d12_command_list_iface *iface,
        UINT root_parameter_index, D3D12_GPU_DESCRIPTOR_HANDLE base_descriptor)
{
    struct d3d12_command_list *list = impl_from_ID3D12GraphicsCommandList(iface);

    TRACE("iface %p, root_parameter_index %u, base_descriptor %#"PRIx64".\n",
            iface, root_parameter_index, base_descriptor.ptr);

    d3d12_command_list_set_descriptor_table_embedded(list, &list->compute_bindings,
            root_parameter_index, base_descriptor,
            list->device->bindless_state.descriptor_buffer_cbv_srv_uav_size_log2,
            list->device->bindless_state.descriptor_buffer_sampler_size_log2);
}

static void STDMETHODCALLTYPE d3d12_command_list_SetGraphicsRootDescriptorTable_embedded_default(
        d3d12_command_list_iface *iface,
        UINT root_parameter_index, D3D12_GPU_DESCRIPTOR_HANDLE base_descriptor)
{
    struct d3d12_command_list *list = impl_from_ID3D12GraphicsCommandList(iface);

    TRACE("iface %p, root_parameter_index %u, base_descriptor %#"PRIx64".\n",
            iface, root_parameter_index, base_descriptor.ptr);

    d3d12_command_list_set_descriptor_table_embedded(list, &list->graphics_bindings,
            root_parameter_index, base_descriptor,
            list->device->bindless_state.descriptor_buffer_cbv_srv_uav_size_log2,
            list->device->bindless_state.descriptor_buffer_sampler_size_log2);
}

static void STDMETHODCALLTYPE d3d12_command_list_SetComputeRootDescriptorTable_default(
        d3d12_command_list_iface *iface,
        UINT root_parameter_index, D3D12_GPU_DESCRIPTOR_HANDLE base_descriptor)
{
    struct d3d12_command_list *list = impl_from_ID3D12GraphicsCommandList(iface);

    TRACE("iface %p, root_parameter_index %u, base_descriptor %#"PRIx64".\n",
            iface, root_parameter_index, base_descriptor.ptr);

    d3d12_command_list_set_descriptor_table(list, &list->compute_bindings,
            root_parameter_index, base_descriptor);
}

static void STDMETHODCALLTYPE d3d12_command_list_SetGraphicsRootDescriptorTable_default(
        d3d12_command_list_iface *iface,
        UINT root_parameter_index, D3D12_GPU_DESCRIPTOR_HANDLE base_descriptor)
{
    struct d3d12_command_list *list = impl_from_ID3D12GraphicsCommandList(iface);

    TRACE("iface %p, root_parameter_index %u, base_descriptor %#"PRIx64".\n",
            iface, root_parameter_index, base_descriptor.ptr);

    d3d12_command_list_set_descriptor_table(list, &list->graphics_bindings,
            root_parameter_index, base_descriptor);
}

static void d3d12_command_list_set_root_constants(struct d3d12_command_list *list,
        struct vkd3d_pipeline_bindings *bindings, unsigned int index, unsigned int offset,
        unsigned int count, const void *data)
{
    const struct d3d12_root_signature *root_signature = bindings->root_signature;
    const struct vkd3d_shader_root_constant *c;
    VKD3D_UNUSED unsigned int i;

    c = root_signature_get_32bit_constants(root_signature, index);
    memcpy(&bindings->root_constants[c->constant_index + offset], data, count * sizeof(uint32_t));

    bindings->root_constant_dirty_mask |= 1ull << index;

#ifdef VKD3D_ENABLE_BREADCRUMBS
    for (i = 0; i < count; i++)
    {
        VKD3D_BREADCRUMB_AUX32(index);
        VKD3D_BREADCRUMB_AUX32(offset + i);
        VKD3D_BREADCRUMB_AUX32(((const uint32_t *)data)[i]);
        VKD3D_BREADCRUMB_COMMAND_STATE(ROOT_CONST);
    }
#endif
}

static void STDMETHODCALLTYPE d3d12_command_list_SetComputeRoot32BitConstant(d3d12_command_list_iface *iface,
        UINT root_parameter_index, UINT data, UINT dst_offset)
{
    struct d3d12_command_list *list = impl_from_ID3D12GraphicsCommandList(iface);

    TRACE("iface %p, root_parameter_index %u, data 0x%08x, dst_offset %u.\n",
            iface, root_parameter_index, data, dst_offset);

    d3d12_command_list_set_root_constants(list, &list->compute_bindings,
            root_parameter_index, dst_offset, 1, &data);
}

static void STDMETHODCALLTYPE d3d12_command_list_SetGraphicsRoot32BitConstant(d3d12_command_list_iface *iface,
        UINT root_parameter_index, UINT data, UINT dst_offset)
{
    struct d3d12_command_list *list = impl_from_ID3D12GraphicsCommandList(iface);

    TRACE("iface %p, root_parameter_index %u, data 0x%08x, dst_offset %u.\n",
            iface, root_parameter_index, data, dst_offset);

    d3d12_command_list_set_root_constants(list, &list->graphics_bindings,
            root_parameter_index, dst_offset, 1, &data);
}

static void STDMETHODCALLTYPE d3d12_command_list_SetComputeRoot32BitConstants(d3d12_command_list_iface *iface,
        UINT root_parameter_index, UINT constant_count, const void *data, UINT dst_offset)
{
    struct d3d12_command_list *list = impl_from_ID3D12GraphicsCommandList(iface);

    TRACE("iface %p, root_parameter_index %u, constant_count %u, data %p, dst_offset %u.\n",
            iface, root_parameter_index, constant_count, data, dst_offset);

    d3d12_command_list_set_root_constants(list, &list->compute_bindings,
            root_parameter_index, dst_offset, constant_count, data);
}

static void STDMETHODCALLTYPE d3d12_command_list_SetGraphicsRoot32BitConstants(d3d12_command_list_iface *iface,
        UINT root_parameter_index, UINT constant_count, const void *data, UINT dst_offset)
{
    struct d3d12_command_list *list = impl_from_ID3D12GraphicsCommandList(iface);

    TRACE("iface %p, root_parameter_index %u, constant_count %u, data %p, dst_offset %u.\n",
            iface, root_parameter_index, constant_count, data, dst_offset);

    d3d12_command_list_set_root_constants(list, &list->graphics_bindings,
            root_parameter_index, dst_offset, constant_count, data);
}

static void d3d12_command_list_set_push_descriptor_info(struct d3d12_command_list *list,
        struct vkd3d_pipeline_bindings *bindings, unsigned int index, D3D12_GPU_VIRTUAL_ADDRESS gpu_address)
{
    const struct d3d12_root_signature *root_signature = bindings->root_signature;
    const struct vkd3d_vk_device_procs *vk_procs = &list->device->vk_procs;
    const struct vkd3d_vulkan_info *vk_info = &list->device->vk_info;
    const struct vkd3d_shader_root_parameter *root_parameter;
    struct vkd3d_root_descriptor_info *descriptor;
    const struct vkd3d_unique_resource *resource;
    VkBufferView vk_buffer_view;
    VkDeviceSize max_range;
    bool ssbo;

    ssbo = d3d12_device_use_ssbo_root_descriptors(list->device);
    root_parameter = root_signature_get_root_descriptor(root_signature, index);
    descriptor = &bindings->root_descriptors[index];

    if (ssbo || root_parameter->parameter_type == D3D12_ROOT_PARAMETER_TYPE_CBV)
    {
        descriptor->vk_descriptor_type = root_parameter->parameter_type == D3D12_ROOT_PARAMETER_TYPE_CBV
                ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;

        if (gpu_address)
        {
            max_range = descriptor->vk_descriptor_type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER
                    ? vk_info->device_limits.maxUniformBufferRange
                    : vk_info->device_limits.maxStorageBufferRange;

            resource = vkd3d_va_map_deref(&list->device->memory_allocator.va_map, gpu_address);
            descriptor->info.buffer.buffer = resource->vk_buffer;
            descriptor->info.buffer.offset = gpu_address - resource->va;
            descriptor->info.buffer.range = min(resource->size - descriptor->info.buffer.offset, max_range);
        }
        else
        {
            descriptor->info.buffer.buffer = VK_NULL_HANDLE;
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
        else
            descriptor->info.buffer_view = VK_NULL_HANDLE;
    }
}

static void d3d12_command_list_set_root_descriptor_va(struct d3d12_command_list *list,
        struct vkd3d_root_descriptor_info *descriptor, D3D12_GPU_VIRTUAL_ADDRESS gpu_address)
{
    descriptor->vk_descriptor_type = VK_DESCRIPTOR_TYPE_MAX_ENUM;
    descriptor->info.va = gpu_address;
}

static void d3d12_command_list_set_root_descriptor(struct d3d12_command_list *list,
        struct vkd3d_pipeline_bindings *bindings, unsigned int index, D3D12_GPU_VIRTUAL_ADDRESS gpu_address)
{
    struct vkd3d_root_descriptor_info *descriptor = &bindings->root_descriptors[index];

    if (bindings->root_signature->root_descriptor_raw_va_mask & (1ull << index))
        d3d12_command_list_set_root_descriptor_va(list, descriptor, gpu_address);
    else
        d3d12_command_list_set_push_descriptor_info(list, bindings, index, gpu_address);

    bindings->root_descriptor_dirty_mask |= 1ull << index;
    bindings->root_descriptor_active_mask |= 1ull << index;

    VKD3D_BREADCRUMB_AUX32(index);
    VKD3D_BREADCRUMB_AUX64(gpu_address);
    VKD3D_BREADCRUMB_COMMAND_STATE(ROOT_DESC);
}

static void STDMETHODCALLTYPE d3d12_command_list_SetComputeRootConstantBufferView(
        d3d12_command_list_iface *iface, UINT root_parameter_index, D3D12_GPU_VIRTUAL_ADDRESS address)
{
    struct d3d12_command_list *list = impl_from_ID3D12GraphicsCommandList(iface);

    TRACE("iface %p, root_parameter_index %u, address %#"PRIx64".\n",
            iface, root_parameter_index, address);

    d3d12_command_list_set_root_descriptor(list, &list->compute_bindings,
            root_parameter_index, address);
}

static void STDMETHODCALLTYPE d3d12_command_list_SetGraphicsRootConstantBufferView(
        d3d12_command_list_iface *iface, UINT root_parameter_index, D3D12_GPU_VIRTUAL_ADDRESS address)
{
    struct d3d12_command_list *list = impl_from_ID3D12GraphicsCommandList(iface);

    TRACE("iface %p, root_parameter_index %u, address %#"PRIx64".\n",
            iface, root_parameter_index, address);

    d3d12_command_list_set_root_descriptor(list, &list->graphics_bindings,
            root_parameter_index, address);
}

static void STDMETHODCALLTYPE d3d12_command_list_SetComputeRootShaderResourceView(
        d3d12_command_list_iface *iface, UINT root_parameter_index, D3D12_GPU_VIRTUAL_ADDRESS address)
{
    struct d3d12_command_list *list = impl_from_ID3D12GraphicsCommandList(iface);

    TRACE("iface %p, root_parameter_index %u, address %#"PRIx64".\n",
            iface, root_parameter_index, address);

    d3d12_command_list_set_root_descriptor(list, &list->compute_bindings,
            root_parameter_index, address);
}

static void STDMETHODCALLTYPE d3d12_command_list_SetGraphicsRootShaderResourceView(
        d3d12_command_list_iface *iface, UINT root_parameter_index, D3D12_GPU_VIRTUAL_ADDRESS address)
{
    struct d3d12_command_list *list = impl_from_ID3D12GraphicsCommandList(iface);

    TRACE("iface %p, root_parameter_index %u, address %#"PRIx64".\n",
            iface, root_parameter_index, address);

    d3d12_command_list_set_root_descriptor(list, &list->graphics_bindings,
            root_parameter_index, address);
}

static void STDMETHODCALLTYPE d3d12_command_list_SetComputeRootUnorderedAccessView(
        d3d12_command_list_iface *iface, UINT root_parameter_index, D3D12_GPU_VIRTUAL_ADDRESS address)
{
    struct d3d12_command_list *list = impl_from_ID3D12GraphicsCommandList(iface);

    TRACE("iface %p, root_parameter_index %u, address %#"PRIx64".\n",
            iface, root_parameter_index, address);

    d3d12_command_list_set_root_descriptor(list, &list->compute_bindings,
            root_parameter_index, address);
}

static void STDMETHODCALLTYPE d3d12_command_list_SetGraphicsRootUnorderedAccessView(
        d3d12_command_list_iface *iface, UINT root_parameter_index, D3D12_GPU_VIRTUAL_ADDRESS address)
{
    struct d3d12_command_list *list = impl_from_ID3D12GraphicsCommandList(iface);

    TRACE("iface %p, root_parameter_index %u, address %#"PRIx64".\n",
            iface, root_parameter_index, address);

    d3d12_command_list_set_root_descriptor(list, &list->graphics_bindings,
            root_parameter_index, address);
}

static void STDMETHODCALLTYPE d3d12_command_list_IASetIndexBuffer(d3d12_command_list_iface *iface,
        const D3D12_INDEX_BUFFER_VIEW *view)
{
    struct d3d12_command_list *list = impl_from_ID3D12GraphicsCommandList(iface);
    const struct vkd3d_unique_resource *resource = NULL;
    enum VkIndexType index_type;

    TRACE("iface %p, view %p.\n", iface, view);

    list->index_buffer.is_dirty = true;

    if (!view)
    {
        list->index_buffer.buffer = VK_NULL_HANDLE;
        VKD3D_BREADCRUMB_AUX32(0);
        VKD3D_BREADCRUMB_COMMAND_STATE(IBO);
        return;
    }

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

    list->index_buffer.dxgi_format = view->Format;
    list->index_buffer.vk_type = index_type;
    if (view->BufferLocation != 0)
    {
        resource = vkd3d_va_map_deref(&list->device->memory_allocator.va_map, view->BufferLocation);
        list->index_buffer.buffer = resource->vk_buffer;
        list->index_buffer.offset = view->BufferLocation - resource->va;
        list->index_buffer.size = view->SizeInBytes;
    }
    else
        list->index_buffer.buffer = VK_NULL_HANDLE;

    VKD3D_BREADCRUMB_AUX32(index_type == VK_INDEX_TYPE_UINT32 ? 32 : 16);
    VKD3D_BREADCRUMB_AUX64(view->BufferLocation);
    VKD3D_BREADCRUMB_AUX64(view->SizeInBytes);
    VKD3D_BREADCRUMB_COOKIE(resource ? resource->cookie : 0);
    VKD3D_BREADCRUMB_COMMAND_STATE(IBO);
}

static void STDMETHODCALLTYPE d3d12_command_list_IASetVertexBuffers(d3d12_command_list_iface *iface,
        UINT start_slot, UINT view_count, const D3D12_VERTEX_BUFFER_VIEW *views)
{
    struct d3d12_command_list *list = impl_from_ID3D12GraphicsCommandList(iface);
    struct vkd3d_dynamic_state *dyn_state = &list->dynamic_state;
    const struct vkd3d_unique_resource *resource = NULL;
    uint32_t vbo_invalidate_mask;
    bool invalidate = false;
    unsigned int i;

    TRACE("iface %p, start_slot %u, view_count %u, views %p.\n", iface, start_slot, view_count, views);

    if (start_slot >= ARRAY_SIZE(dyn_state->vertex_strides) ||
            view_count > ARRAY_SIZE(dyn_state->vertex_strides) - start_slot)
    {
        WARN("Invalid start slot %u / view count %u.\n", start_slot, view_count);
        return;
    }

    /* Native drivers appear to ignore this call. Buffer bindings are kept as-is. */
    if (!views)
        return;

    for (i = 0; i < view_count; ++i)
    {
        bool invalid_va = false;
        VkBuffer buffer;
        VkDeviceSize offset;
        VkDeviceSize size;
        uint32_t stride;

        if (views[i].BufferLocation)
        {
            if ((resource = vkd3d_va_map_deref(&list->device->memory_allocator.va_map, views[i].BufferLocation)))
            {
                buffer = resource->vk_buffer;
                offset = views[i].BufferLocation - resource->va;
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
            buffer = VK_NULL_HANDLE;
            offset = 0;
            size = 0;
            stride = VKD3D_NULL_BUFFER_SIZE;
        }

        VKD3D_BREADCRUMB_AUX32(start_slot + i);
        VKD3D_BREADCRUMB_AUX64(views[i].BufferLocation);
        VKD3D_BREADCRUMB_AUX32(views[i].StrideInBytes);
        VKD3D_BREADCRUMB_AUX64(views[i].SizeInBytes);
        VKD3D_BREADCRUMB_COOKIE(resource ? resource->cookie : 0);
        VKD3D_BREADCRUMB_COMMAND_STATE(VBO);

        invalidate |= dyn_state->vertex_strides[start_slot + i] != stride;
        dyn_state->vertex_strides[start_slot + i] = stride;
        dyn_state->vertex_buffers[start_slot + i] = buffer;
        dyn_state->vertex_offsets[start_slot + i] = offset;
        dyn_state->vertex_sizes[start_slot + i] = size;
    }

    dyn_state->dirty_flags |= VKD3D_DYNAMIC_STATE_VERTEX_BUFFER_STRIDE;

    vbo_invalidate_mask = ((1u << view_count) - 1u) << start_slot;
    dyn_state->dirty_vbos |= vbo_invalidate_mask;

    if (invalidate)
        d3d12_command_list_invalidate_current_pipeline(list, false);
}

static void STDMETHODCALLTYPE d3d12_command_list_SOSetTargets(d3d12_command_list_iface *iface,
        UINT start_slot, UINT view_count, const D3D12_STREAM_OUTPUT_BUFFER_VIEW *views)
{
    struct d3d12_command_list *list = impl_from_ID3D12GraphicsCommandList(iface);
    const struct vkd3d_unique_resource *resource;
    unsigned int i;

    TRACE("iface %p, start_slot %u, view_count %u, views %p.\n", iface, start_slot, view_count, views);

    d3d12_command_list_end_current_render_pass(list, true);

    if (!list->device->vk_info.EXT_transform_feedback)
    {
        FIXME("Transform feedback is not supported by Vulkan implementation.\n");
        return;
    }

    if (start_slot >= ARRAY_SIZE(list->so_buffers) || view_count > ARRAY_SIZE(list->so_buffers) - start_slot)
    {
        WARN("Invalid start slot %u / view count %u.\n", start_slot, view_count);
        return;
    }

    for (i = 0; i < view_count; ++i)
    {
        if (views[i].BufferLocation && views[i].SizeInBytes)
        {
            resource = vkd3d_va_map_deref(&list->device->memory_allocator.va_map, views[i].BufferLocation);
            list->so_buffers[start_slot + i] = resource->vk_buffer;
            list->so_buffer_offsets[start_slot + i] = views[i].BufferLocation - resource->va;
            list->so_buffer_sizes[start_slot + i] = views[i].SizeInBytes;

            resource = vkd3d_va_map_deref(&list->device->memory_allocator.va_map, views[i].BufferFilledSizeLocation);
            list->so_counter_buffers[start_slot + i] = resource->vk_buffer;
            list->so_counter_buffer_offsets[start_slot + i] = views[i].BufferFilledSizeLocation - resource->va;
        }
        else
        {
            list->so_buffers[start_slot + i] = VK_NULL_HANDLE;
            list->so_buffer_offsets[start_slot + i] = 0;
            list->so_buffer_sizes[start_slot + i] = 0;

            list->so_counter_buffers[start_slot + i] = VK_NULL_HANDLE;
            list->so_counter_buffer_offsets[start_slot + i] = 0;
        }
    }
}

static void d3d12_command_list_recompute_fb_size(struct d3d12_command_list *list)
{
    const VkPhysicalDeviceLimits *limits = &list->device->vk_info.device_limits;
    const struct d3d12_rtv_desc *rtv_desc;
    VkSampleCountFlagBits sample_count;
    unsigned int i;

    /* Honor PSO sample count if no render targets are bound */
    sample_count = 0;

    list->fb_width = limits->maxFramebufferWidth;
    list->fb_height = limits->maxFramebufferHeight;
    list->fb_layer_count = limits->maxFramebufferLayers;

    for (i = 0; i < ARRAY_SIZE(list->rtvs); i++)
    {
        rtv_desc = &list->rtvs[i];

        if (rtv_desc->resource)
        {
            list->fb_width = min(list->fb_width, rtv_desc->width);
            list->fb_height = min(list->fb_height, rtv_desc->height);
            list->fb_layer_count = min(list->fb_layer_count, rtv_desc->layer_count);

            sample_count = rtv_desc->sample_count;
        }
    }

    rtv_desc = &list->dsv;

    if (rtv_desc->resource)
    {
        list->fb_width = min(list->fb_width, rtv_desc->width);
        list->fb_height = min(list->fb_height, rtv_desc->height);
        list->fb_layer_count = min(list->fb_layer_count, rtv_desc->layer_count);

        sample_count = rtv_desc->sample_count;
    }

    if (list->dynamic_state.rasterization_samples != sample_count)
    {
        list->dynamic_state.rasterization_samples = sample_count;
        list->dynamic_state.dirty_flags |= VKD3D_DYNAMIC_STATE_RASTERIZATION_SAMPLES;

        if (!list->device->device_info.extended_dynamic_state3_features.extendedDynamicState3RasterizationSamples)
            d3d12_command_list_invalidate_current_pipeline(list, false);
    }
}

static void d3d12_command_list_invalidate_ds_state(struct d3d12_command_list *list, VkFormat prev_dsv_format)
{
    const struct d3d12_graphics_pipeline_state *graphics;
    unsigned int next_dsv_plane_write_enable;
    VkFormat next_dsv_format;

    next_dsv_format = list->dsv.format ? list->dsv.format->vk_format : VK_FORMAT_UNDEFINED;
    next_dsv_plane_write_enable = list->dsv.plane_write_enable;

    if (d3d12_pipeline_state_is_graphics(list->state))
    {
        graphics = &list->state->graphics;

        if (prev_dsv_format != next_dsv_format)
        {
            list->dynamic_state.dirty_flags |= VKD3D_DYNAMIC_STATE_DEPTH_BIAS;

            if (d3d12_graphics_pipeline_state_has_unknown_dsv_format_with_test(graphics))
            {
                /* If we change the NULL-ness of the depth-stencil attachment, we are
                 * at risk of having to use fallback pipelines. Invalidate the pipeline
                 * since we'll have to refresh the VkRenderingInfo and VkPipeline. */
                d3d12_command_list_invalidate_current_pipeline(list, false);
            }
        }
    }

    /* The DSV flags affect write masks. */
    if (next_dsv_plane_write_enable != list->dynamic_state.dsv_plane_write_enable)
    {
        uint32_t delta = next_dsv_plane_write_enable ^ list->dynamic_state.dsv_plane_write_enable;
        if (delta & (1u << 0))
            list->dynamic_state.dirty_flags |= VKD3D_DYNAMIC_STATE_DEPTH_WRITE_ENABLE;
        if (delta & (1u << 1))
            list->dynamic_state.dirty_flags |= VKD3D_DYNAMIC_STATE_STENCIL_WRITE_MASK;
        list->dynamic_state.dsv_plane_write_enable = next_dsv_plane_write_enable;
    }
}

static void STDMETHODCALLTYPE d3d12_command_list_OMSetRenderTargets(d3d12_command_list_iface *iface,
        UINT render_target_descriptor_count, const D3D12_CPU_DESCRIPTOR_HANDLE *render_target_descriptors,
        BOOL single_descriptor_handle, const D3D12_CPU_DESCRIPTOR_HANDLE *depth_stencil_descriptor)
{
    struct d3d12_command_list *list = impl_from_ID3D12GraphicsCommandList(iface);
    const struct d3d12_rtv_desc *rtv_desc;
    VkFormat prev_dsv_format;
    unsigned int i;

    TRACE("iface %p, render_target_descriptor_count %u, render_target_descriptors %p, "
            "single_descriptor_handle %#x, depth_stencil_descriptor %p.\n",
            iface, render_target_descriptor_count, render_target_descriptors,
            single_descriptor_handle, depth_stencil_descriptor);

    if (list->is_inside_render_pass)
        d3d12_command_list_mark_as_invalid(list, "OMSetRenderTargets called within a render pass.\n");

    d3d12_command_list_invalidate_rendering_info(list);
    d3d12_command_list_end_current_render_pass(list, false);

    if (render_target_descriptor_count > ARRAY_SIZE(list->rtvs))
    {
        WARN("Descriptor count %u > %zu, ignoring extra descriptors.\n",
                render_target_descriptor_count, ARRAY_SIZE(list->rtvs));
        render_target_descriptor_count = ARRAY_SIZE(list->rtvs);
    }

    prev_dsv_format = list->dsv.format ? list->dsv.format->vk_format : VK_FORMAT_UNDEFINED;

    memset(list->rtvs, 0, sizeof(list->rtvs));
    memset(&list->dsv, 0, sizeof(list->dsv));
    /* Need to deduce DSV layouts again. */
    list->dsv_layout = VK_IMAGE_LAYOUT_UNDEFINED;
    list->dsv_plane_optimal_mask = 0;

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
            TRACE("RTV descriptor %u is not initialized.\n", i);
            VKD3D_BREADCRUMB_AUX32(i);
            VKD3D_BREADCRUMB_TAG("RTV bind NULL");
            continue;
        }

        VKD3D_BREADCRUMB_COOKIE(rtv_desc->view->cookie);
        VKD3D_BREADCRUMB_AUX32(i);
        VKD3D_BREADCRUMB_TAG("RTV bind");

        list->rtvs[i] = *rtv_desc;
    }

    if (depth_stencil_descriptor)
    {
        if ((rtv_desc = d3d12_rtv_desc_from_cpu_handle(*depth_stencil_descriptor))
                && rtv_desc->resource)
        {
            list->dsv = *rtv_desc;

            VKD3D_BREADCRUMB_COOKIE(rtv_desc->view->cookie);
            VKD3D_BREADCRUMB_TAG("DSV bind");
        }
        else
        {
            VKD3D_BREADCRUMB_TAG("DSV bind NULL");
        }
    }

    d3d12_command_list_invalidate_ds_state(list, prev_dsv_format);
    d3d12_command_list_recompute_fb_size(list);
}

static bool d3d12_rect_fully_covers_region(const D3D12_RECT *a, const D3D12_RECT *b)
{
    return a->top <= b->top && a->bottom >= b->bottom &&
            a->left <= b->left && a->right >= b->right;
}

static bool vkd3d_rtv_and_aspects_fully_cover_resource(const struct d3d12_resource *resource,
        const struct vkd3d_view *view, VkImageAspectFlags clear_aspects)
{
    /* Check that we're clearing all aspects. */
    return resource->format->vk_aspect_mask == clear_aspects &&
            resource->desc.MipLevels == 1 &&
            view->info.texture.layer_idx == 0 &&
            view->info.texture.layer_count >= resource->desc.DepthOrArraySize; /* takes care of REMAINING_LAYERS as well. */
}

static VkImageAspectFlags d3d12_barrier_subresource_range_covers_aspects(const struct d3d12_resource *resource,
        const D3D12_BARRIER_SUBRESOURCE_RANGE *range)
{
    VkImageAspectFlags aspects;
    unsigned int i;

    /* BARRIER_SUBRESOURCE_RANGE has two paths. When mip levels is zero, we use the legacy subresource index.
     * When non-zero, it uses normal ranges a-la Vulkan. */
    if (range->NumMipLevels == 0)
    {
        if (range->IndexOrFirstMipLevel == D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES)
            return resource->format->vk_aspect_mask;
        else if (d3d12_resource_desc_get_sub_resource_count_per_plane(&resource->desc) == 1)
            return vk_image_aspect_flags_from_d3d12(resource->format, range->IndexOrFirstMipLevel);
        else
            return 0;
    }
    else
    {
        /* NumArraySlices = 0 actually means 1 slice for whatever reason. */
        aspects = 0;
        if (range->IndexOrFirstMipLevel == 0 && range->FirstArraySlice == 0 &&
                range->NumMipLevels >= resource->desc.MipLevels &&
                max(1u, range->NumArraySlices) >= d3d12_resource_desc_get_layer_count(&resource->desc))
        {
            for (i = 0; i < range->NumPlanes; i++)
                aspects |= vk_image_aspect_flags_from_d3d12(resource->format, i + range->FirstPlane);
        }
        return aspects;
    }
}

static bool d3d12_barrier_subresource_range_covers_resource(const struct d3d12_resource *resource,
        const D3D12_BARRIER_SUBRESOURCE_RANGE *range)
{
    return d3d12_barrier_subresource_range_covers_aspects(resource, range) ==
            resource->format->vk_aspect_mask;
}

static void d3d12_command_list_clear_attachment(struct d3d12_command_list *list, struct d3d12_resource *resource,
        struct vkd3d_view *view, VkImageAspectFlags clear_aspects, const VkClearValue *clear_value, UINT rect_count,
        const D3D12_RECT *rects)
{
    VkImageSubresourceLayers vk_subresource_layers;
    bool full_clear, writable = true;
    bool full_resource_clear;
    D3D12_RECT full_rect;
    int attachment_idx;
    unsigned int i;

    vk_subresource_layers = vk_subresource_layers_from_view(view);

    /* If one of the clear rectangles covers the entire image, we
     * may be able to use a fast path and re-initialize the image */
    full_rect = d3d12_get_image_rect(resource, &vk_subresource_layers);
    full_clear = !rect_count;

    for (i = 0; i < rect_count && !full_clear; i++)
        full_clear = d3d12_rect_fully_covers_region(&rects[i], &full_rect);

    if (full_clear)
        rect_count = 0;

    full_resource_clear = full_clear && vkd3d_rtv_and_aspects_fully_cover_resource(resource, view, clear_aspects);
    /* For committed (non-aliased) resources, we don't transition away from UNDEFINED, so we must do the initial transition. */
    d3d12_command_list_track_resource_usage(list, resource, !full_resource_clear ||
            !d3d12_resource_may_alias_other_resources(resource));

    attachment_idx = d3d12_command_list_find_attachment(list, resource, view);

    if (attachment_idx == D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT)
        writable = (vk_writable_aspects_from_image_layout(list->dsv_layout) & clear_aspects) == clear_aspects;

    if (attachment_idx < 0 || !writable)
    {
        /* View currently not bound as a render target, or bound but
         * the render pass isn't active and we're only going to clear
         * a sub-region of the image, or one of the aspects to clear
         * uses a read-only layout in the current render pass */
        d3d12_command_list_end_current_render_pass(list, false);
        d3d12_command_list_load_attachment(list, resource, view,
                clear_aspects, clear_value, rect_count, rects,
                VK_ATTACHMENT_LOAD_OP_CLEAR);
    }
    else
    {
        /* View bound and render pass active, just emit the clear */
        d3d12_command_list_clear_attachment_inline(list, resource, view,
                attachment_idx, clear_aspects, clear_value,
                rect_count, rects);
    }

    if (resource->flags & VKD3D_RESOURCE_LINEAR_STAGING_COPY)
    {
        /* For depth-stencil images, only mark the cleared aspects as dirty.
         * Don't apply this to planar images since clear aspect will always
         * be COLOR in those cases. */
        if (clear_aspects & (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT))
            vk_subresource_layers.aspectMask = clear_aspects;

        d3d12_command_list_update_subresource_data(list, resource, vk_subresource_layers);
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

    if (list->is_inside_render_pass)
        d3d12_command_list_mark_as_invalid(list, "ClearDepthStencilView called within a render pass.\n");

    list->cmd.estimated_cost += VKD3D_COMMAND_COST_LOW;

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

    if (list->is_inside_render_pass)
        d3d12_command_list_mark_as_invalid(list, "ClearRenderTargetView called within a render pass.\n");

    list->cmd.estimated_cost += VKD3D_COMMAND_COST_LOW;

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
    DXGI_FORMAT clear_dxgi_format;
    bool has_view;
    union
    {
        struct vkd3d_view *view;
        struct vkd3d_descriptor_metadata_buffer_view buffer;
    } u;
};

static void d3d12_command_list_clear_uav(struct d3d12_command_list *list,
        struct d3d12_resource *resource, const struct vkd3d_clear_uav_info *args,
        const VkClearColorValue *clear_color, UINT rect_count, const D3D12_RECT *rects)
{
    const struct vkd3d_vk_device_procs *vk_procs = &list->device->vk_procs;
    VkExtent3D workgroup_size, workgroup_count, view_extent;
    struct vkd3d_clear_uav_pipeline pipeline;
    struct vkd3d_clear_uav_args clear_args;
    VkDescriptorBufferInfo buffer_info;
    VkDescriptorImageInfo image_info;
    D3D12_RECT full_rect, curr_rect;
    VkWriteDescriptorSet write_set;
    unsigned int i, j, layer_count;
    uint32_t max_workgroup_count;
    bool sampler_feedback_clear;

    d3d12_command_list_track_resource_usage(list, resource, true);
    d3d12_command_list_end_current_render_pass(list, false);
    d3d12_command_list_debug_mark_begin_region(list, "ClearUAV");

    d3d12_command_list_invalidate_current_pipeline(list, true);
    d3d12_command_list_invalidate_root_parameters(list, &list->compute_bindings, true, &list->graphics_bindings);
    d3d12_command_list_update_descriptor_buffers(list);

    sampler_feedback_clear = d3d12_resource_desc_is_sampler_feedback(&resource->desc);

    max_workgroup_count = list->device->vk_info.device_limits.maxComputeWorkGroupCount[0];

    if (sampler_feedback_clear)
    {
        /* Clear color is ignored for sampler feedback. */
        memset(&clear_args, 0, sizeof(clear_args));
        /* Clear rect is also ignored. */
        rect_count = 0;
        rects = NULL;
    }
    else
        clear_args.clear_color = *clear_color;

    memset(&write_set, 0, sizeof(write_set));
    write_set.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write_set.descriptorCount = 1;

    memset(&full_rect, 0, sizeof(full_rect));

    if (d3d12_resource_is_texture(resource))
    {
        assert(args->has_view);

        image_info.sampler = VK_NULL_HANDLE;
        image_info.imageView = args->u.view->vk_image_view;
        image_info.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

        write_set.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        write_set.pImageInfo = &image_info;

        view_extent = d3d12_resource_get_view_subresource_extent(resource, args->u.view);

        full_rect.right = view_extent.width;
        full_rect.bottom = view_extent.height;

        layer_count = args->u.view->info.texture.vk_view_type == VK_IMAGE_VIEW_TYPE_3D
                ? view_extent.depth : args->u.view->info.texture.layer_count;

        if (sampler_feedback_clear)
        {
            VkExtent3D padded = d3d12_resource_desc_get_padded_feedback_extent(&resource->desc);
            full_rect.right = padded.width;
            full_rect.bottom = padded.height;
        }

        /* Robustness would take care of it, but no reason to spam more threads than needed. */
        if (args->u.view->info.texture.vk_view_type == VK_IMAGE_VIEW_TYPE_3D)
        {
            layer_count = min(layer_count - args->u.view->info.texture.w_offset, args->u.view->info.texture.w_size);
            if (layer_count >= 0x80000000u)
            {
                ERR("3D slice out of bounds.\n");
                layer_count = 0;
            }
        }

        pipeline = vkd3d_meta_get_clear_image_uav_pipeline(
                &list->device->meta_ops, args->u.view->info.texture.vk_view_type,
                args->u.view->format->type == VKD3D_FORMAT_TYPE_UINT);
        workgroup_size = vkd3d_meta_get_clear_image_uav_workgroup_size(args->u.view->info.texture.vk_view_type);
    }
    else
    {
        full_rect.bottom = 1;

        if (args->has_view)
        {
            VkDeviceSize byte_count = args->u.view->format->byte_count;
            full_rect.right = args->u.view->info.buffer.size / byte_count;

            write_set.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER;
            write_set.pTexelBufferView = &args->u.view->vk_buffer_view;
        }
        else
        {
            full_rect.right = args->u.buffer.range / sizeof(uint32_t);

            write_set.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            write_set.pBufferInfo = &buffer_info;
            /* resource heap offset is already in descriptor */
            buffer_info.buffer = resource->res.vk_buffer;
            buffer_info.offset = resource->mem.offset + (args->u.buffer.va - resource->res.va);
            buffer_info.range = args->u.buffer.range;
        }

        layer_count = 1;
        pipeline = vkd3d_meta_get_clear_buffer_uav_pipeline(&list->device->meta_ops,
                !args->has_view || args->u.view->format->type == VKD3D_FORMAT_TYPE_UINT,
                !args->has_view);
        workgroup_size = vkd3d_meta_get_clear_buffer_uav_workgroup_size();
    }

    /* clear full resource if no rects are specified */
    curr_rect = full_rect;

    VK_CALL(vkCmdBindPipeline(list->cmd.vk_command_buffer,
            VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.vk_pipeline));
    VK_CALL(vkCmdPushDescriptorSetKHR(list->cmd.vk_command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE,
            pipeline.vk_pipeline_layout, 0, 1, &write_set));

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

        workgroup_count.width = vkd3d_compute_workgroup_count(curr_rect.right - curr_rect.left, workgroup_size.width);
        workgroup_count.height = vkd3d_compute_workgroup_count(curr_rect.bottom - curr_rect.top, workgroup_size.height);
        workgroup_count.depth = vkd3d_compute_workgroup_count(layer_count, workgroup_size.depth);

        /* For very large buffers, we may end up having to dispatch more workgroups
         * than the device supports in one go. For images, this can never happen, so
         * ignore the y and z dimensions. */
        for (j = 0; j < workgroup_count.width; j += max_workgroup_count)
        {
            clear_args.offset.x = curr_rect.left + j * workgroup_size.width;
            clear_args.offset.y = curr_rect.top;
            clear_args.extent.width = curr_rect.right - clear_args.offset.x;
            clear_args.extent.height = curr_rect.bottom - clear_args.offset.y;

            VK_CALL(vkCmdPushConstants(list->cmd.vk_command_buffer,
                    pipeline.vk_pipeline_layout, VK_SHADER_STAGE_COMPUTE_BIT,
                    0, sizeof(clear_args), &clear_args));

            VK_CALL(vkCmdDispatch(list->cmd.vk_command_buffer,
                    min(workgroup_count.width - j, max_workgroup_count),
                    workgroup_count.height, workgroup_count.depth));
        }
    }

    if (vkd3d_config_flags & VKD3D_CONFIG_FLAG_CLEAR_UAV_SYNC)
        list->cmd.clear_uav_pending = true;

    d3d12_command_list_debug_mark_end_region(list);

    VKD3D_BREADCRUMB_RESOURCE(resource);
    VKD3D_BREADCRUMB_COMMAND(CLEAR_UAV);
}

static void d3d12_command_list_clear_uav_with_copy(struct d3d12_command_list *list,
        struct d3d12_resource *resource,
        const struct vkd3d_clear_uav_info *args, const VkClearColorValue *clear_value,
        const struct vkd3d_format *format, UINT rect_count, const D3D12_RECT *rects)
{
    const struct vkd3d_vk_device_procs *vk_procs = &list->device->vk_procs;
    unsigned int base_layer, layer_count, i, j;
    struct vkd3d_clear_uav_pipeline pipeline;
    struct vkd3d_scratch_allocation scratch;
    struct vkd3d_clear_uav_args clear_args;
    VkCopyBufferToImageInfo2 copy_info;
    VkDeviceSize scratch_buffer_size;
    D3D12_RECT curr_rect, full_rect;
    VkWriteDescriptorSet write_set;
    VkBufferImageCopy2 copy_region;
    VkBufferView vk_buffer_view;
    VkExtent3D workgroup_size;
    VkDependencyInfo dep_info;
    VkMemoryBarrier2 barrier;
    VkExtent3D view_extent;
    uint32_t element_count;

    d3d12_command_list_track_resource_usage(list, resource, true);
    d3d12_command_list_end_current_render_pass(list, false);
    d3d12_command_list_debug_mark_begin_region(list, "ClearUAVWithCopy");

    d3d12_command_list_invalidate_current_pipeline(list, true);
    d3d12_command_list_invalidate_root_parameters(list, &list->compute_bindings, true, &list->graphics_bindings);
    d3d12_command_list_update_descriptor_buffers(list);

    assert(args->has_view);
    assert(d3d12_resource_is_texture(resource));

    view_extent = d3d12_resource_get_view_subresource_extent(resource, args->u.view);

    full_rect.left = 0;
    full_rect.right = view_extent.width;
    full_rect.top = 0;
    full_rect.bottom = view_extent.height;

    if (rect_count)
    {
        element_count = 0;

        for (i = 0; i < rect_count; i++)
        {
            if (rects[i].right > rects[i].left && rects[i].bottom > rects[i].top)
            {
                unsigned int w = rects[i].right - rects[i].left;
                unsigned int h = rects[i].bottom - rects[i].top;
                element_count = max(element_count, w * h);
            }
        }
    }
    else
    {
        element_count = full_rect.right * full_rect.bottom;
    }

    element_count *= view_extent.depth;
    scratch_buffer_size = element_count * format->byte_count;

    if (!d3d12_command_allocator_allocate_scratch_memory(list->allocator,
            VKD3D_SCRATCH_POOL_KIND_DEVICE_STORAGE,
            scratch_buffer_size, 16, ~0u, &scratch))
    {
        ERR("Failed to allocate scratch memory for UAV clear.\n");
        return;
    }

    pipeline = vkd3d_meta_get_clear_buffer_uav_pipeline(&list->device->meta_ops, true, false);
    workgroup_size = vkd3d_meta_get_clear_buffer_uav_workgroup_size();

    if (!vkd3d_create_vk_buffer_view(list->device, scratch.buffer, format, scratch.offset, scratch_buffer_size, &vk_buffer_view))
    {
        ERR("Failed to create buffer view for UAV clear.\n");
        return;
    }

    if (!(d3d12_command_allocator_add_buffer_view(list->allocator, vk_buffer_view)))
    {
        ERR("Failed to add buffer view.\n");
        VK_CALL(vkDestroyBufferView(list->device->vk_device, vk_buffer_view, NULL));
        return;
    }

    memset(&write_set, 0, sizeof(write_set));
    write_set.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write_set.descriptorCount = 1;
    write_set.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER;
    write_set.pTexelBufferView = &vk_buffer_view;

    VK_CALL(vkCmdBindPipeline(list->cmd.vk_command_buffer,
            VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.vk_pipeline));
    VK_CALL(vkCmdPushDescriptorSetKHR(list->cmd.vk_command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE,
            pipeline.vk_pipeline_layout, 0, 1, &write_set));

    clear_args.clear_color = *clear_value;
    clear_args.offset.x = 0;
    clear_args.offset.y = 0;
    clear_args.extent.width = element_count;
    clear_args.extent.height = 1;

    VK_CALL(vkCmdPushConstants(list->cmd.vk_command_buffer,
            pipeline.vk_pipeline_layout, VK_SHADER_STAGE_COMPUTE_BIT,
            0, sizeof(clear_args), &clear_args));

    VK_CALL(vkCmdDispatch(list->cmd.vk_command_buffer,
            vkd3d_compute_workgroup_count(element_count, workgroup_size.width), 1, 1));

    /* Insert barrier to make the buffer clear visible, but also to make the
     * image safely accessible by the transfer stage. This fallback is so rare
     * that we should not pessimize regular UAV barriers. */
    memset(&dep_info, 0, sizeof(dep_info));
    dep_info.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dep_info.memoryBarrierCount = 1;
    dep_info.pMemoryBarriers = &barrier;

    memset(&barrier, 0, sizeof(barrier));
    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
    barrier.srcStageMask = vk_queue_shader_stages(list->device, list->vk_queue_flags);
    barrier.srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
    barrier.dstStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
    barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT | VK_ACCESS_2_TRANSFER_READ_BIT;

    VK_CALL(vkCmdPipelineBarrier2(list->cmd.vk_command_buffer, &dep_info));

    copy_region.sType = VK_STRUCTURE_TYPE_BUFFER_IMAGE_COPY_2;
    copy_region.pNext = NULL;
    copy_region.bufferOffset = scratch.offset;
    copy_region.bufferRowLength = 0;
    copy_region.bufferImageHeight = 0;

    copy_region.imageSubresource = vk_subresource_layers_from_view(args->u.view);

    if (args->u.view->info.texture.vk_view_type == VK_IMAGE_VIEW_TYPE_3D)
    {
        base_layer = args->u.view->info.texture.w_offset;
        layer_count = view_extent.depth;
        layer_count = min(layer_count - args->u.view->info.texture.w_offset, args->u.view->info.texture.w_size);
        if (layer_count >= 0x80000000u)
        {
            ERR("3D slice out of bounds.\n");
            layer_count = 0;
        }
    }
    else
    {
        copy_region.imageOffset.z = 0;
        base_layer = copy_region.imageSubresource.baseArrayLayer;
        layer_count = copy_region.imageSubresource.layerCount;
    }

    copy_region.imageExtent.depth = 1;
    copy_region.imageSubresource.layerCount = 1;

    copy_info.sType = VK_STRUCTURE_TYPE_COPY_BUFFER_TO_IMAGE_INFO_2;
    copy_info.pNext = NULL;
    copy_info.srcBuffer = scratch.buffer;
    copy_info.dstImage = resource->res.vk_image;
    copy_info.dstImageLayout = VK_IMAGE_LAYOUT_GENERAL;
    copy_info.regionCount = 1;
    copy_info.pRegions = &copy_region;

    curr_rect = full_rect;

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

        copy_region.imageOffset.x = curr_rect.left;
        copy_region.imageOffset.y = curr_rect.top;
        copy_region.imageExtent.width = curr_rect.right - curr_rect.left;
        copy_region.imageExtent.height = curr_rect.bottom - curr_rect.top;

        for (j = 0; j < layer_count; j++)
        {
            if (resource->desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D)
                copy_region.imageOffset.z = base_layer + j;
            else
                copy_region.imageSubresource.baseArrayLayer = base_layer + j;

            VK_CALL(vkCmdCopyBufferToImage2(list->cmd.vk_command_buffer, &copy_info));
        }
    }

    barrier.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
    barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    barrier.dstStageMask = vk_queue_shader_stages(list->device, list->vk_queue_flags);
    barrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT;

    VK_CALL(vkCmdPipelineBarrier2(list->cmd.vk_command_buffer, &dep_info));

    d3d12_command_list_debug_mark_end_region(list);

    VKD3D_BREADCRUMB_RESOURCE(resource);
    VKD3D_BREADCRUMB_COMMAND(CLEAR_UAV_COPY);
}

static VkClearColorValue vkd3d_fixup_clear_uav_swizzle(struct d3d12_device *device,
        const struct vkd3d_format *clear_format, VkClearColorValue color)
{
    if (clear_format->dxgi_format == DXGI_FORMAT_A8_UNORM && clear_format->vk_format != VK_FORMAT_A8_UNORM_KHR)
    {
        VkClearColorValue result;
        result.float32[0] = color.float32[3];
        return result;
    }

    return color;
}

static VkClearColorValue vkd3d_fixup_clear_uav_uint_color(struct d3d12_device *device,
        DXGI_FORMAT dxgi_format, VkClearColorValue color)
{
    VkClearColorValue result = {0};
    unsigned int i;

    switch (dxgi_format)
    {
        case DXGI_FORMAT_R11G11B10_FLOAT:
            result.uint32[0] = (color.uint32[0] & 0x7FF)
                    | ((color.uint32[1] & 0x7FF) << 11)
                    | ((color.uint32[2] & 0x3FF) << 22);
            return result;

        case DXGI_FORMAT_R9G9B9E5_SHAREDEXP:
            result.uint32[0] = (color.uint32[0] & 0x1FF)
                    | ((color.uint32[1] & 0x1FF) << 9)
                    | ((color.uint32[2] & 0x1FF) << 18)
                    | ((color.uint32[3] & 0x1F) << 27);
            return result;

        case DXGI_FORMAT_B8G8R8A8_UNORM:
        case DXGI_FORMAT_B8G8R8X8_UNORM:
        case DXGI_FORMAT_A8_UNORM:
            for (i = 0; i < 4; i++)
                result.float32[i] = (float)(color.uint32[i] & 0xff) / 255.0f;
            return result;

        default:
            return color;
    }
}

static const struct vkd3d_format *vkd3d_clear_uav_find_uint_format(struct d3d12_device *device, DXGI_FORMAT dxgi_format)
{
    DXGI_FORMAT uint_format = DXGI_FORMAT_UNKNOWN;

    if (dxgi_format < device->format_compatibility_list_count)
        uint_format = device->format_compatibility_lists[dxgi_format].uint_format;

    return vkd3d_get_format(device, uint_format, false);
}

static inline bool vkd3d_clear_uav_info_from_metadata(struct vkd3d_clear_uav_info *args,
        struct d3d12_desc_split_metadata metadata)
{
    if (metadata.view->info.flags & VKD3D_DESCRIPTOR_FLAG_IMAGE_VIEW)
    {
        args->has_view = true;
        args->u.view = metadata.view->info.image.view;
        args->clear_dxgi_format = metadata.view->info.image.view->format->dxgi_format;
        return true;
    }
    else if (metadata.view->info.flags & VKD3D_DESCRIPTOR_FLAG_BUFFER_VA_RANGE)
    {
        args->u.buffer = metadata.view->info.buffer;
        args->has_view = false;
        args->clear_dxgi_format = metadata.view->info.buffer.dxgi_format;
        return true;
    }
    else
    {
        return false;
    }
}

static void vkd3d_mask_uint_clear_color(uint32_t color[4], VkFormat vk_format)
{
    unsigned int component_count, i;

    switch (vk_format)
    {
        case VK_FORMAT_R8_UINT:
        case VK_FORMAT_R16_UINT:
        case VK_FORMAT_R32_UINT:
            component_count = 1;
            break;

        case VK_FORMAT_R8G8_UINT:
        case VK_FORMAT_R16G16_UINT:
        case VK_FORMAT_R32G32_UINT:
            component_count = 2;
            break;

        case VK_FORMAT_R32G32B32_UINT:
            component_count = 3;
            break;

        default:
            component_count = 4;
            break;
    }

    for (i = component_count; i < 4; i++)
        color[i] = 0x0;

    /* Need to mask the clear value, since apparently driver can saturate the clear value instead. */
    switch (vk_format)
    {
        case VK_FORMAT_R8_UINT:
        case VK_FORMAT_R8G8_UINT:
        case VK_FORMAT_R8G8B8A8_UINT:
            for (i = 0; i < 4; i++)
                color[i] &= 0xffu;
            break;

        case VK_FORMAT_R16_UINT:
        case VK_FORMAT_R16G16_UINT:
        case VK_FORMAT_R16G16B16A16_UINT:
            for (i = 0; i < 4; i++)
                color[i] &= 0xffffu;
            break;

        case VK_FORMAT_A2B10G10R10_UINT_PACK32:
            for (i = 0; i < 3; i++)
                color[i] &= 0x3ff;
            color[3] &= 0x3;
            break;

        default:
            break;
    }
}

static bool vkd3d_clear_uav_synthesize_buffer_view(struct d3d12_command_list *list,
        struct d3d12_resource *resource, const struct vkd3d_clear_uav_info *args,
        const struct vkd3d_format *override_format,
        struct vkd3d_view **inline_view)
{
    /* We don't have a view, but we need one due to formatted clear. Synthesize a buffer view.
     * Otherwise, we can just hit the raw SSBO path. */
    struct vkd3d_buffer_view_desc view_desc;

    view_desc.buffer = resource->res.vk_buffer;
    view_desc.offset = resource->mem.offset + (args->u.buffer.va - resource->res.va);
    view_desc.size = args->u.buffer.range;
    view_desc.format = override_format ? override_format :
            vkd3d_get_format(list->device, args->clear_dxgi_format, false);

    if (!view_desc.format || !vkd3d_create_buffer_view(list->device, &view_desc, inline_view))
    {
        ERR("Failed to create buffer view.\n");
        return false;
    }

    return true;
}

static void STDMETHODCALLTYPE d3d12_command_list_ClearUnorderedAccessViewUint(d3d12_command_list_iface *iface,
        D3D12_GPU_DESCRIPTOR_HANDLE gpu_handle, D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle, ID3D12Resource *resource,
        const UINT values[4], UINT rect_count, const D3D12_RECT *rects)
{
    struct d3d12_command_list *list = impl_from_ID3D12GraphicsCommandList(iface);
    struct d3d12_desc_split_metadata metadata;
    const struct vkd3d_format *clear_format;
    const struct vkd3d_format *uint_format;
    struct vkd3d_view *inline_view = NULL;
    struct d3d12_resource *resource_impl;
    struct vkd3d_clear_uav_info args;
    VkClearColorValue color;

    TRACE("iface %p, gpu_handle %#"PRIx64", cpu_handle %lx, resource %p, values %p, rect_count %u, rects %p.\n",
            iface, gpu_handle.ptr, cpu_handle.ptr, resource, values, rect_count, rects);

    if (list->is_inside_render_pass)
        d3d12_command_list_mark_as_invalid(list, "ClearUnorderedAccessViewUint called within a render pass.\n");

    list->cmd.estimated_cost += VKD3D_COMMAND_COST_LOW;

    memcpy(color.uint32, values, sizeof(color.uint32));

    metadata = d3d12_desc_decode_metadata(list->device, cpu_handle.ptr);
    resource_impl = impl_from_ID3D12Resource(resource);

    if (!resource_impl || !metadata.view)
        return;

    if (!vkd3d_clear_uav_info_from_metadata(&args, metadata))
        return;

    if (d3d12_resource_is_texture(resource_impl) && !args.has_view)
    {
        /* Theoretically possibly for buggy application that tries to clear a buffer view with a texture resource.
         * Safeguard against crash. */
        WARN("Attempted to clear buffer with image resource.\n");
        return;
    }

    if (args.clear_dxgi_format)
        clear_format = vkd3d_get_format(list->device, args.clear_dxgi_format, false);
    else
        clear_format = NULL;

    /* Handle formatted buffer clears.
     * Always defer creating the VkBufferView until this time. */
    if (!args.has_view && args.clear_dxgi_format)
    {
        uint_format = vkd3d_clear_uav_find_uint_format(list->device, args.clear_dxgi_format);
        if (!uint_format)
        {
            ERR("Unhandled format %d.\n", clear_format->dxgi_format);
            return;
        }

        color = vkd3d_fixup_clear_uav_uint_color(list->device, clear_format->dxgi_format, color);
        color = vkd3d_fixup_clear_uav_swizzle(list->device, clear_format, color);
        vkd3d_mask_uint_clear_color(color.uint32, uint_format->vk_format);

        if (!vkd3d_clear_uav_synthesize_buffer_view(list, resource_impl, &args, uint_format, &inline_view))
            return;

        args.u.view = inline_view;
        args.has_view = true;
    }
    else if (d3d12_resource_is_texture(resource_impl) && clear_format->type != VKD3D_FORMAT_TYPE_UINT)
    {
        const struct vkd3d_view *base_view = metadata.view->info.image.view;
        uint_format = vkd3d_clear_uav_find_uint_format(list->device, clear_format->dxgi_format);
        color = vkd3d_fixup_clear_uav_uint_color(list->device, clear_format->dxgi_format, color);
        color = vkd3d_fixup_clear_uav_swizzle(list->device, clear_format, color);

        if (!uint_format)
        {
            ERR("Unhandled format %d.\n", clear_format->dxgi_format);
            return;
        }

        vkd3d_mask_uint_clear_color(color.uint32, uint_format->vk_format);

        if (d3d12_resource_view_format_is_compatible(resource_impl, uint_format))
        {
            struct vkd3d_texture_view_desc view_desc;
            memset(&view_desc, 0, sizeof(view_desc));

            view_desc.image = resource_impl->res.vk_image;
            view_desc.view_type = base_view->info.texture.vk_view_type;
            view_desc.format = uint_format;
            view_desc.miplevel_idx = base_view->info.texture.miplevel_idx;
            view_desc.miplevel_count = 1;
            view_desc.layer_idx = base_view->info.texture.layer_idx;
            view_desc.layer_count = base_view->info.texture.layer_count;
            view_desc.w_offset = base_view->info.texture.w_offset;
            view_desc.w_size = base_view->info.texture.w_size;
            view_desc.aspect_mask = base_view->info.texture.aspect_mask;
            view_desc.image_usage = VK_IMAGE_USAGE_STORAGE_BIT;
            view_desc.allowed_swizzle = false;

            if (!vkd3d_create_texture_view(list->device, &view_desc, &args.u.view))
            {
                ERR("Failed to create image view.\n");
                return;
            }

            inline_view = args.u.view;
        }
        else
        {
            /* If the clear color is 0, we can safely use the existing view to perform the
             * clear since the bit pattern will not change. Otherwise, fill a scratch buffer
             * with the packed clear value and perform a buffer to image copy. */
            if (color.uint32[0] || color.uint32[1] || color.uint32[2] || color.uint32[3])
            {
                d3d12_command_list_clear_uav_with_copy(list, resource_impl,
                        &args, &color, uint_format, rect_count, rects);
                return;
            }
        }
    }
    else if (args.has_view)
    {
        vkd3d_mask_uint_clear_color(color.uint32, clear_format->vk_format);
    }

    d3d12_command_list_clear_uav(list, resource_impl, &args, &color, rect_count, rects);

    if (inline_view)
    {
        d3d12_command_allocator_add_view(list->allocator, inline_view);
        vkd3d_view_decref(inline_view, list->device);
    }
}

static void STDMETHODCALLTYPE d3d12_command_list_ClearUnorderedAccessViewFloat(d3d12_command_list_iface *iface,
        D3D12_GPU_DESCRIPTOR_HANDLE gpu_handle, D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle, ID3D12Resource *resource,
        const float values[4], UINT rect_count, const D3D12_RECT *rects)
{
    struct d3d12_command_list *list = impl_from_ID3D12GraphicsCommandList(iface);
    struct d3d12_desc_split_metadata metadata;
    const struct vkd3d_format *clear_format;
    struct vkd3d_view *inline_view = NULL;
    struct d3d12_resource *resource_impl;
    struct vkd3d_clear_uav_info args;
    VkClearColorValue color;

    TRACE("iface %p, gpu_handle %#"PRIx64", cpu_handle %lx, resource %p, values %p, rect_count %u, rects %p.\n",
            iface, gpu_handle.ptr, cpu_handle.ptr, resource, values, rect_count, rects);

    if (list->is_inside_render_pass)
        d3d12_command_list_mark_as_invalid(list, "ClearUnorderedAccessViewFloat called within a render pass.\n");

    list->cmd.estimated_cost += VKD3D_COMMAND_COST_LOW;

    metadata = d3d12_desc_decode_metadata(list->device, cpu_handle.ptr);
    memcpy(color.float32, values, sizeof(color.float32));
    resource_impl = impl_from_ID3D12Resource(resource);

    if (!resource_impl || !metadata.view)
        return;

    if (!vkd3d_clear_uav_info_from_metadata(&args, metadata))
        return;

    if (d3d12_resource_is_texture(resource_impl) && !args.has_view)
    {
        /* Theoretically possibly for buggy application that tries to clear a buffer view with a texture resource.
         * Safeguard against crash. */
        WARN("Attempted to clear buffer with image resource.\n");
        return;
    }

    if (args.clear_dxgi_format)
    {
        clear_format = vkd3d_get_format(list->device, args.clear_dxgi_format, false);
        color = vkd3d_fixup_clear_uav_swizzle(list->device, clear_format, color);
    }

    if (!args.has_view && args.clear_dxgi_format)
    {
        if (!vkd3d_clear_uav_synthesize_buffer_view(list, resource_impl, &args, NULL, &inline_view))
            return;

        args.u.view = inline_view;
        args.has_view = true;
    }

    d3d12_command_list_clear_uav(list, resource_impl, &args, &color, rect_count, rects);

    if (inline_view)
    {
        d3d12_command_allocator_add_view(list->allocator, inline_view);
        vkd3d_view_decref(inline_view, list->device);
    }
}

static bool d3d12_command_list_is_subresource_bound_as_rtv_dsv(struct d3d12_command_list *list,
        struct d3d12_resource *resource, const VkImageSubresource *subresource)
{
    unsigned int i;

    if (!(list->rendering_info.state_flags & (VKD3D_RENDERING_ACTIVE | VKD3D_RENDERING_SUSPENDED)))
        return false;

    if (list->dsv.resource == resource)
    {
        const struct vkd3d_view *dsv = list->dsv.view;

        if (subresource->mipLevel == dsv->info.texture.miplevel_idx &&
                subresource->arrayLayer >= dsv->info.texture.layer_idx &&
                subresource->arrayLayer < dsv->info.texture.layer_idx + dsv->info.texture.layer_count)
            return true;
    }
    else
    {
        for (i = 0; i < D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT; i++)
        {
            const struct vkd3d_view *rtv = list->rtvs[i].view;

            if (list->rtvs[i].resource != resource)
                continue;

            if (subresource->mipLevel == rtv->info.texture.miplevel_idx &&
                    subresource->arrayLayer >= rtv->info.texture.layer_idx &&
                    subresource->arrayLayer < rtv->info.texture.layer_idx + rtv->info.texture.layer_count)
                return true;
        }
    }

    return false;
}

static void STDMETHODCALLTYPE d3d12_command_list_DiscardResource(d3d12_command_list_iface *iface,
        ID3D12Resource *resource, const D3D12_DISCARD_REGION *region)
{
    struct d3d12_command_list *list = impl_from_ID3D12GraphicsCommandList(iface);
    struct d3d12_resource *texture = impl_from_ID3D12Resource(resource);
    unsigned int i, first_subresource, subresource_count;
    bool has_bound_subresource, has_unbound_subresource;
    VkImageSubresourceLayers vk_subresource_layers;
    VkImageSubresourceRange vk_subresource_range;
    unsigned int resource_subresource_count;
    VkImageSubresource vk_subresource;
    bool all_subresource_full_discard;
    D3D12_RECT full_rect;
    bool full_discard;

    TRACE("iface %p, resource %p, region %p.\n", iface, resource, region);

    if (list->is_inside_render_pass)
        d3d12_command_list_mark_as_invalid(list, "DiscardResource called within a render pass.\n");

    /* This method is only supported on DIRECT and COMPUTE queues,
     * but we only implement it for render targets, so ignore it
     * on compute. */
    if (list->type != D3D12_COMMAND_LIST_TYPE_DIRECT && list->type != D3D12_COMMAND_LIST_TYPE_COMPUTE)
    {
        WARN("Not supported for queue type %d.\n", list->type);
        VKD3D_BREADCRUMB_RESOURCE(texture);
        VKD3D_BREADCRUMB_TAG("discard-drop-list-type");
        return;
    }

    /* Ignore buffers */
    if (!d3d12_resource_is_texture(texture))
    {
        VKD3D_BREADCRUMB_RESOURCE(texture);
        VKD3D_BREADCRUMB_TAG("discard-drop-resource-type");
        return;
    }

    /* D3D12 requires that the texture is either in render target
     * state, in depth-stencil state, or in UAV state depending on usage flags.
     * In compute lists, we only allow UAV state. */
    if (!(texture->desc.Flags &
          (D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET |
           D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL |
           D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS)))
    {
        WARN("Not supported for resource %p.\n", resource);
        VKD3D_BREADCRUMB_RESOURCE(texture);
        VKD3D_BREADCRUMB_TAG("discard-drop-usage-flags");
        return;
    }

    /* Assume that pRegion == NULL means that we should discard
     * the entire resource. This does not seem to be documented. */
    resource_subresource_count = d3d12_resource_get_sub_resource_count(texture);
    if (region)
    {
        first_subresource = region->FirstSubresource;
        subresource_count = region->NumSubresources;
    }
    else
    {
        first_subresource = 0;
        subresource_count = resource_subresource_count;
    }

    /* If we write to all subresources, we can promote the depth image to OPTIMAL since we know the resource
     * must be in OPTIMAL state. */
    if (texture->desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL)
    {
        d3d12_command_list_notify_dsv_discard(list, texture,
                first_subresource, subresource_count, resource_subresource_count);
    }

    /* We can't meaningfully discard sub-regions of an image. If rects
     * are specified, all specified subresources must have the same
     * dimensions, so just base this off the first subresource */
    if (!(full_discard = (!region || !region->NumRects)))
    {
        vk_subresource = d3d12_resource_get_vk_subresource(texture, first_subresource, false);
        vk_subresource_layers = vk_subresource_layers_from_subresource(&vk_subresource);
        full_rect = d3d12_get_image_rect(texture, &vk_subresource_layers);

        for (i = 0; i < region->NumRects && !full_discard; i++)
            full_discard = d3d12_rect_fully_covers_region(&region->pRects[i], &full_rect);
    }

    if (!full_discard)
    {
        VKD3D_BREADCRUMB_RESOURCE(texture);
        VKD3D_BREADCRUMB_TAG("discard-drop-incomplete");
        return;
    }

    /* Resource tracking. If we do a full discard, there is no need to do initial layout transitions.
     * Partial discards on first resource use needs to be handles however,
     * so we must make sure to discard all subresources on first use. */
    all_subresource_full_discard = first_subresource == 0 && subresource_count == resource_subresource_count;
    d3d12_command_list_track_resource_usage(list, texture, !all_subresource_full_discard);

    has_bound_subresource = false;
    has_unbound_subresource = false;

    /* If we're discarding all subresources, we can only safely discard with one barrier
      * if is_bound state is the same for all subresources. */
    for (i = first_subresource; i < first_subresource + subresource_count; i++)
    {
        vk_subresource = d3d12_resource_get_vk_subresource(texture, i, false);

        if (d3d12_command_list_is_subresource_bound_as_rtv_dsv(list, texture, &vk_subresource))
            has_bound_subresource = true;
        else
            has_unbound_subresource = true;
    }

    if (all_subresource_full_discard)
        all_subresource_full_discard = !has_bound_subresource || !has_unbound_subresource;

    d3d12_command_list_end_current_render_pass(list, !has_unbound_subresource);

    VKD3D_BREADCRUMB_RESOURCE(texture);

    if (all_subresource_full_discard)
    {
        vk_subresource_range.baseMipLevel = 0;
        vk_subresource_range.baseArrayLayer = 0;
        vk_subresource_range.levelCount = VK_REMAINING_MIP_LEVELS;
        vk_subresource_range.layerCount = VK_REMAINING_ARRAY_LAYERS;
        vk_subresource_range.aspectMask = texture->format->vk_aspect_mask;

        d3d12_command_list_discard_attachment_barrier(list,
                texture, &vk_subresource_range, !has_unbound_subresource);
        VKD3D_BREADCRUMB_AUX32(~0u);
        VKD3D_BREADCRUMB_COMMAND(DISCARD);
    }
    else
    {
        for (i = first_subresource; i < first_subresource + subresource_count; i++)
        {
            vk_subresource = d3d12_resource_get_vk_subresource(texture, i, false);
            vk_subresource_range = vk_subresource_range_from_subresource(&vk_subresource);

            d3d12_command_list_discard_attachment_barrier(list,
                    texture, &vk_subresource_range, !has_unbound_subresource);
            VKD3D_BREADCRUMB_AUX32(i);
            VKD3D_BREADCRUMB_COMMAND(DISCARD);
        }
    }
}

static void d3d12_command_list_resolve_binary_occlusion_queries(struct d3d12_command_list *list,
        VkDeviceAddress src_va, VkDeviceAddress dst_va, uint32_t count);

static uint32_t vkd3d_query_lookup_entry_hash(const void *key)
{
    const struct vkd3d_query_lookup_key *k = key;

    /* The bucket index is expected to be small and may fit into the
     * lower bits of the heap address, which are expected to be zero */
    return ((uintptr_t)k->query_heap) ^ k->bucket;
}

static bool vkd3d_query_lookup_entry_compare(const void *key, const struct hash_map_entry *entry)
{
    const struct vkd3d_query_lookup_key *a = key;
    const struct vkd3d_query_lookup_key *b = &((const struct vkd3d_query_lookup_entry*)entry)->key;

    return a->query_heap == b->query_heap && a->bucket == b->bucket;
}

static bool d3d12_command_list_is_query_resolve_pending(struct d3d12_command_list *list,
        struct d3d12_query_heap *query_heap, uint32_t query_index)
{
    struct vkd3d_query_lookup_entry *e;
    struct vkd3d_query_lookup_key key;
    uint32_t bit_index;

    key.query_heap = query_heap;
    key.bucket = query_index >> VKD3D_QUERY_LOOKUP_GRANULARITY_BITS;

    e = (struct vkd3d_query_lookup_entry*)hash_map_find(&list->query_resolve_lut, &key);

    if (!e)
        return false;

    bit_index = query_index & VKD3D_QUERY_LOOKUP_INDEX_MASK;
    return !!(e->query_mask & (1ull << bit_index));
}

static void d3d12_command_list_add_query_lookup_mask(struct d3d12_command_list *list,
        struct d3d12_query_heap *query_heap, uint32_t bucket, uint64_t query_mask)
{
    struct vkd3d_query_lookup_entry entry, *e;

    entry.key.query_heap = query_heap;
    entry.key.bucket = bucket;
    entry.query_mask = 0ull;

    e = (struct vkd3d_query_lookup_entry*)hash_map_insert(&list->query_resolve_lut, &entry.key, &entry.hash_entry);
    e->query_mask |= query_mask;
}

static void d3d12_command_list_add_query_lookup_range(struct d3d12_command_list *list,
        const struct vkd3d_query_resolve_entry *entry)
{
    uint32_t query_index = entry->query_index;
    uint32_t query_count = entry->query_count;
    uint32_t advance, bucket, shift;
    uint64_t query_mask;

    /* The query lookup table is implemented as a hash map with each
     * entry ("bucket") storing a bit mask of 64 consecutive queries. */
    while (query_count)
    {
        query_mask = ~0ull;
        advance = VKD3D_QUERY_LOOKUP_GRANULARITY;

        /* last bucket may not be covered entirely */
        if (query_count < VKD3D_QUERY_LOOKUP_GRANULARITY)
            query_mask >>= (VKD3D_QUERY_LOOKUP_GRANULARITY - query_count);

        /* query_index may not be aligned to the first bucket */
        shift = query_index & VKD3D_QUERY_LOOKUP_INDEX_MASK;

        if (shift)
        {
            query_mask <<= shift;
            advance -= shift;
        }

        bucket = query_index >> VKD3D_QUERY_LOOKUP_GRANULARITY_BITS;
        d3d12_command_list_add_query_lookup_mask(list, entry->query_heap, bucket, query_mask);

        advance = min(advance, query_count);

        query_index += advance;
        query_count -= advance;
    }
}

static void d3d12_command_list_execute_query_resolve(struct d3d12_command_list *list,
        const struct vkd3d_query_resolve_entry *entry)
{
    const struct vkd3d_vk_device_procs *vk_procs = &list->device->vk_procs;
    size_t stride = d3d12_query_heap_type_get_data_size(entry->query_heap->desc.Type);
    VkCopyBufferInfo2 copy_info;
    VkBufferCopy2 copy_region;

    if (!d3d12_command_list_gather_pending_queries(list))
    {
        d3d12_command_list_mark_as_invalid(list, "Failed to gather virtual queries.\n");
        return;
    }

    if (entry->query_type != D3D12_QUERY_TYPE_BINARY_OCCLUSION)
    {
        copy_region.sType = VK_STRUCTURE_TYPE_BUFFER_COPY_2;
        copy_region.pNext = NULL;
        copy_region.srcOffset = stride * entry->query_index;
        copy_region.dstOffset = entry->dst_buffer->mem.offset + entry->dst_offset;
        copy_region.size = stride * entry->query_count;

        copy_info.sType = VK_STRUCTURE_TYPE_COPY_BUFFER_INFO_2;
        copy_info.pNext = NULL;
        copy_info.srcBuffer = entry->query_heap->vk_buffer;
        copy_info.dstBuffer = entry->dst_buffer->res.vk_buffer;
        copy_info.regionCount = 1;
        copy_info.pRegions = &copy_region;

        d3d12_command_list_mark_copy_buffer_write(list, copy_info.dstBuffer, copy_region.dstOffset, copy_region.size,
                !!(entry->dst_buffer->flags & VKD3D_RESOURCE_RESERVED));
        VK_CALL(vkCmdCopyBuffer2(list->cmd.vk_command_buffer, &copy_info));
    }
    else
    {
        d3d12_command_list_resolve_binary_occlusion_queries(list,
                entry->query_heap->va + entry->query_index * sizeof(uint64_t),
                entry->dst_buffer->res.va + entry->dst_offset, entry->query_count);
    }
}

static void d3d12_command_list_flush_query_resolves(struct d3d12_command_list *list)
{
    unsigned int i;

    if (!list->query_resolve_count)
        return;

    for (i = 0; i < list->query_resolve_count; i++)
        d3d12_command_list_execute_query_resolve(list, &list->query_resolves[i]);

    list->query_resolve_count = 0;

    hash_map_clear(&list->query_resolve_lut);
}

static void d3d12_command_list_add_query_resolve(struct d3d12_command_list *list,
        const struct vkd3d_query_resolve_entry *entry)
{
    /* Ensure that other writes to the buffer execute before the resolve */
    d3d12_command_list_end_transfer_batch(list);

    if (!vkd3d_array_reserve((void**)&list->query_resolves, &list->query_resolve_size,
            list->query_resolve_count + 1, sizeof(*list->query_resolves)))
    {
        ERR("Failed to allocate query resolve entry.\n");
        return;
    }

    list->query_resolves[list->query_resolve_count++] = *entry;

    d3d12_command_list_add_query_lookup_range(list, entry);
}

static inline bool d3d12_query_type_is_scoped(D3D12_QUERY_TYPE type)
{
    return type != D3D12_QUERY_TYPE_TIMESTAMP;
}

static void STDMETHODCALLTYPE d3d12_command_list_BeginQuery(d3d12_command_list_iface *iface,
        ID3D12QueryHeap *heap, D3D12_QUERY_TYPE type, UINT index)
{
    struct d3d12_command_list *list = impl_from_ID3D12GraphicsCommandList(iface);
    struct d3d12_query_heap *query_heap = impl_from_ID3D12QueryHeap(heap);
    const struct vkd3d_vk_device_procs *vk_procs = &list->device->vk_procs;
    VkQueryControlFlags flags = d3d12_query_type_get_vk_flags(type);

    TRACE("iface %p, heap %p, type %#x, index %u.\n", iface, heap, type, index);

    if (!d3d12_query_type_is_scoped(type))
    {
        WARN("Query type %u is not scoped.\n", type);
        return;
    }

    d3d12_command_list_track_query_heap(list, query_heap);

    if (d3d12_query_heap_type_is_inline(query_heap->desc.Type))
    {
        if (d3d12_command_list_is_query_resolve_pending(list, query_heap, index))
        {
            /* Implicitly calls flush_query_resolves */
            d3d12_command_list_end_current_render_pass(list, true);
        }

        if (!d3d12_command_list_enable_query(list, query_heap, index, type))
            d3d12_command_list_mark_as_invalid(list, "Failed to enable virtual query.\n");
    }
    else
    {
        d3d12_command_list_end_current_render_pass(list, true);

        if (!d3d12_command_list_reset_query(list, query_heap->vk_query_pool, index))
            VK_CALL(vkCmdResetQueryPool(list->cmd.vk_command_buffer, query_heap->vk_query_pool, index, 1));

        if (d3d12_query_type_is_indexed(type))
        {
            unsigned int stream_index = type - D3D12_QUERY_TYPE_SO_STATISTICS_STREAM0;
            VK_CALL(vkCmdBeginQueryIndexedEXT(list->cmd.vk_command_buffer,
                    query_heap->vk_query_pool, index, flags, stream_index));
        }
        else
            VK_CALL(vkCmdBeginQuery(list->cmd.vk_command_buffer, query_heap->vk_query_pool, index, flags));

        list->cmd.active_non_inline_running_queries++;
    }
}

static void STDMETHODCALLTYPE d3d12_command_list_EndQuery(d3d12_command_list_iface *iface,
        ID3D12QueryHeap *heap, D3D12_QUERY_TYPE type, UINT index)
{
    struct d3d12_command_list *list = impl_from_ID3D12GraphicsCommandList(iface);
    struct d3d12_query_heap *query_heap = impl_from_ID3D12QueryHeap(heap);
    const struct vkd3d_vk_device_procs *vk_procs = &list->device->vk_procs;

    TRACE("iface %p, heap %p, type %#x, index %u.\n", iface, heap, type, index);

    d3d12_command_list_track_query_heap(list, query_heap);

    if (d3d12_query_heap_type_is_inline(query_heap->desc.Type))
    {
        if (!d3d12_command_list_disable_query(list, query_heap, index))
            d3d12_command_list_mark_as_invalid(list, "Failed to disable virtual query.\n");
    }
    else if (d3d12_query_type_is_scoped(type))
    {
        d3d12_command_list_end_current_render_pass(list, true);

        if (d3d12_query_type_is_indexed(type))
        {
            unsigned int stream_index = type - D3D12_QUERY_TYPE_SO_STATISTICS_STREAM0;
            VK_CALL(vkCmdEndQueryIndexedEXT(list->cmd.vk_command_buffer,
                    query_heap->vk_query_pool, index, stream_index));
        }
        else
            VK_CALL(vkCmdEndQuery(list->cmd.vk_command_buffer, query_heap->vk_query_pool, index));

        list->cmd.active_non_inline_running_queries--;
    }
    else if (type == D3D12_QUERY_TYPE_TIMESTAMP)
    {
        if (!d3d12_command_list_reset_query(list, query_heap->vk_query_pool, index))
        {
            d3d12_command_list_end_current_render_pass(list, true);
            VK_CALL(vkCmdResetQueryPool(list->cmd.vk_command_buffer, query_heap->vk_query_pool, index, 1));
        }

        VK_CALL(vkCmdWriteTimestamp2(list->cmd.vk_command_buffer,
                VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, query_heap->vk_query_pool, index));
    }
    else
        FIXME("Unhandled query type %u.\n", type);
}

static void d3d12_command_list_resolve_binary_occlusion_queries(struct d3d12_command_list *list,
        VkDeviceAddress src_va, VkDeviceAddress dst_va, uint32_t count)
{
    const struct vkd3d_query_ops *query_ops = &list->device->meta_ops.query;
    const struct vkd3d_vk_device_procs *vk_procs = &list->device->vk_procs;
    struct vkd3d_query_resolve_args args;
    unsigned int workgroup_count;
    VkMemoryBarrier2 vk_barrier;
    VkDependencyInfo dep_info;

    d3d12_command_list_invalidate_current_pipeline(list, true);
    d3d12_command_list_invalidate_root_parameters(list, &list->compute_bindings, true, &list->graphics_bindings);

    memset(&dep_info, 0, sizeof(dep_info));
    dep_info.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dep_info.memoryBarrierCount = 1;
    dep_info.pMemoryBarriers = &vk_barrier;

    memset(&vk_barrier, 0, sizeof(vk_barrier));
    vk_barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;

    /* If there are any overlapping copy writes, handle them here since we're
     * doing a transfer barrier anyways. dst_buffer is in COPY_DEST state */
    vk_barrier.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
    vk_barrier.srcAccessMask = list->tracked_copy_buffer_count ? VK_ACCESS_2_TRANSFER_WRITE_BIT : VK_ACCESS_2_NONE;
    vk_barrier.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    vk_barrier.dstAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;

    d3d12_command_list_reset_buffer_copy_tracking(list);

    VK_CALL(vkCmdPipelineBarrier2(list->cmd.vk_command_buffer, &dep_info));

    VK_CALL(vkCmdBindPipeline(list->cmd.vk_command_buffer,
            VK_PIPELINE_BIND_POINT_COMPUTE,
            query_ops->vk_resolve_binary_pipeline));

    args.dst_va = dst_va;
    args.src_va = src_va;
    args.query_count = count;

    VK_CALL(vkCmdPushConstants(list->cmd.vk_command_buffer,
            query_ops->vk_resolve_pipeline_layout,
            VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(args), &args));

    workgroup_count = vkd3d_compute_workgroup_count(count, VKD3D_QUERY_OP_WORKGROUP_SIZE);
    VK_CALL(vkCmdDispatch(list->cmd.vk_command_buffer, workgroup_count, 1, 1));

    vk_barrier.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    vk_barrier.srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
    vk_barrier.dstStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
    vk_barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;

    VK_CALL(vkCmdPipelineBarrier2(list->cmd.vk_command_buffer, &dep_info));
}

static void STDMETHODCALLTYPE d3d12_command_list_ResolveQueryData(d3d12_command_list_iface *iface,
        ID3D12QueryHeap *heap, D3D12_QUERY_TYPE type, UINT start_index, UINT query_count,
        ID3D12Resource *dst_buffer, UINT64 aligned_dst_buffer_offset)
{
    struct d3d12_query_heap *query_heap = impl_from_ID3D12QueryHeap(heap);
    struct d3d12_command_list *list = impl_from_ID3D12GraphicsCommandList(iface);
    struct d3d12_resource *buffer = impl_from_ID3D12Resource(dst_buffer);
    const struct vkd3d_vk_device_procs *vk_procs = &list->device->vk_procs;
    size_t stride = d3d12_query_heap_type_get_data_size(query_heap->desc.Type);
    struct vkd3d_query_resolve_entry entry;

    TRACE("iface %p, heap %p, type %#x, start_index %u, query_count %u, "
            "dst_buffer %p, aligned_dst_buffer_offset %#"PRIx64".\n",
            iface, heap, type, start_index, query_count,
            dst_buffer, aligned_dst_buffer_offset);

    if (list->is_inside_render_pass)
        d3d12_command_list_mark_as_invalid(list, "ResolveQueryData called within a render pass.\n");

    /* Some games call this with a query_count of 0.
     * Avoid ending the render pass and doing worthless tracking. */
    if (!query_count)
        return;

    if (!d3d12_resource_is_buffer(buffer))
    {
        WARN("Destination resource is not a buffer.\n");
        return;
    }

    list->cmd.estimated_cost += VKD3D_COMMAND_COST_LOW;

    d3d12_command_list_track_query_heap(list, query_heap);

    if (d3d12_query_heap_type_is_inline(query_heap->desc.Type))
    {
        entry.query_type = type;
        entry.query_heap = query_heap;
        entry.query_index = start_index;
        entry.query_count = query_count;
        entry.dst_buffer = buffer;
        entry.dst_offset = aligned_dst_buffer_offset;

        if (list->rendering_info.state_flags & VKD3D_RENDERING_ACTIVE)
            d3d12_command_list_add_query_resolve(list, &entry);
        else
            d3d12_command_list_execute_query_resolve(list, &entry);
    }
    else
    {
        d3d12_command_list_end_current_render_pass(list, true);

        d3d12_command_list_read_query_range(list, query_heap->vk_query_pool, start_index, query_count);
        d3d12_command_list_mark_copy_buffer_write(list, buffer->res.vk_buffer,
                buffer->mem.offset + aligned_dst_buffer_offset, sizeof(uint64_t),
                !!(buffer->flags & VKD3D_RESOURCE_RESERVED));
        VK_CALL(vkCmdCopyQueryPoolResults(list->cmd.vk_command_buffer, query_heap->vk_query_pool,
                start_index, query_count, buffer->res.vk_buffer, buffer->mem.offset + aligned_dst_buffer_offset,
                stride, VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT));
    }

    VKD3D_BREADCRUMB_TAG("QueryResolve [Type, StartIndex, QueryCount, DstOffset, QueryHeapCookie, DstBuffer]");
    VKD3D_BREADCRUMB_AUX32(type);
    VKD3D_BREADCRUMB_AUX32(start_index);
    VKD3D_BREADCRUMB_AUX32(query_count);
    VKD3D_BREADCRUMB_AUX64(aligned_dst_buffer_offset);
    VKD3D_BREADCRUMB_COOKIE(query_heap->cookie);
    VKD3D_BREADCRUMB_RESOURCE(buffer);
    VKD3D_BREADCRUMB_COMMAND(RESOLVE_QUERY);
}

static void STDMETHODCALLTYPE d3d12_command_list_SetPredication(d3d12_command_list_iface *iface,
        ID3D12Resource *buffer, UINT64 aligned_buffer_offset, D3D12_PREDICATION_OP operation)
{
    struct d3d12_command_list *list = impl_from_ID3D12GraphicsCommandList(iface);
    struct d3d12_resource *resource = impl_from_ID3D12Resource(buffer);
    const struct vkd3d_vk_device_procs *vk_procs = &list->device->vk_procs;
    const struct vkd3d_predicate_ops *predicate_ops = &list->device->meta_ops.predicate;
    struct vkd3d_predicate_resolve_args resolve_args;
    VkConditionalRenderingBeginInfoEXT begin_info;
    struct vkd3d_scratch_allocation scratch;
    VkCommandBuffer vk_patch_cmd_buffer;
    VkMemoryBarrier2 vk_barrier;
    VkDependencyInfo dep_info;

    TRACE("iface %p, buffer %p, aligned_buffer_offset %#"PRIx64", operation %#x.\n",
            iface, buffer, aligned_buffer_offset, operation);

    d3d12_command_list_end_current_render_pass(list, true);

    if (resource && (aligned_buffer_offset & 0x7))
        return;

    if (list->predication.enabled_on_command_buffer)
        VK_CALL(vkCmdEndConditionalRenderingEXT(list->cmd.vk_command_buffer));

    if (resource)
    {
        if (!d3d12_command_allocator_allocate_scratch_memory(list->allocator,
                VKD3D_SCRATCH_POOL_KIND_DEVICE_STORAGE,
                sizeof(uint32_t), sizeof(uint32_t), ~0u, &scratch))
            return;

        begin_info.sType = VK_STRUCTURE_TYPE_CONDITIONAL_RENDERING_BEGIN_INFO_EXT;
        begin_info.pNext = NULL;
        begin_info.buffer = scratch.buffer;
        begin_info.offset = scratch.offset;
        begin_info.flags = 0;

        /* Even if it's not super relevant for performance yet, we need to hoist this to init buffer
         * since an ExecuteIndirect patch shader will need to read the predicate VA potentially. */
        d3d12_command_allocator_allocate_init_post_indirect_command_buffer(list->allocator, list);
        vk_patch_cmd_buffer = list->cmd.vk_init_commands_post_indirect_barrier;

        /* Resolve 64-bit predicate into a 32-bit location so that this works with
         * VK_EXT_conditional_rendering. We'll handle the predicate operation here
         * so setting VK_CONDITIONAL_RENDERING_INVERTED_BIT_EXT is not necessary. */

        if (vk_patch_cmd_buffer == list->cmd.vk_command_buffer)
        {
            d3d12_command_list_invalidate_current_pipeline(list, true);
            d3d12_command_list_invalidate_root_parameters(list, &list->compute_bindings, true,
                    &list->graphics_bindings);
        }

        resolve_args.src_va = resource->res.va + aligned_buffer_offset;
        resolve_args.dst_va = scratch.va;
        resolve_args.invert = operation != D3D12_PREDICATION_OP_EQUAL_ZERO;

        VK_CALL(vkCmdBindPipeline(vk_patch_cmd_buffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                predicate_ops->vk_resolve_pipeline));
        VK_CALL(vkCmdPushConstants(vk_patch_cmd_buffer, predicate_ops->vk_resolve_pipeline_layout,
                VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(resolve_args), &resolve_args));
        VK_CALL(vkCmdDispatch(vk_patch_cmd_buffer, 1, 1, 1));

        memset(&vk_barrier, 0, sizeof(vk_barrier));
        vk_barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
        vk_barrier.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        vk_barrier.srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;

        if (list->device->device_info.conditional_rendering_features.conditionalRendering)
        {
            vk_barrier.dstStageMask = VK_PIPELINE_STAGE_2_CONDITIONAL_RENDERING_BIT_EXT;
            vk_barrier.dstAccessMask = VK_ACCESS_2_CONDITIONAL_RENDERING_READ_BIT_EXT;
            list->predication.enabled_on_command_buffer = true;

            /* EXT_conditional_rendering does not play nice with NV_dgc yet
             * (somewhat ambiguous what the spec means, but NV handles it),
             * so we will need a fallback there where we read from the
             * predicate VA. INDIRECT_ACCESS barriers on Mesa imply SCACHE/VCACHE anyway, so this does not really hurt us. */
            if (!(vkd3d_config_flags & VKD3D_CONFIG_FLAG_SKIP_DRIVER_WORKAROUNDS) &&
                    !list->device->device_info.device_generated_commands_features_ext.deviceGeneratedCommands &&
                    list->device->device_info.vulkan_1_2_properties.driverID == VK_DRIVER_ID_MESA_RADV)
            {
                vk_barrier.dstStageMask |= VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
                vk_barrier.dstAccessMask |= VK_ACCESS_2_SHADER_READ_BIT;
            }
        }
        else
        {
            vk_barrier.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
            vk_barrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
            list->predication.fallback_enabled = true;
        }

        list->predication.va = scratch.va;
        list->predication.vk_buffer = scratch.buffer;
        list->predication.vk_buffer_offset = scratch.offset;

        memset(&dep_info, 0, sizeof(dep_info));
        dep_info.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        dep_info.memoryBarrierCount = 1;
        dep_info.pMemoryBarriers = &vk_barrier;

        /* We could try to defer this barrier, but SetPredication is rare enough that we ignore that for now. */
        VK_CALL(vkCmdPipelineBarrier2(vk_patch_cmd_buffer, &dep_info));

        if (list->predication.enabled_on_command_buffer)
            VK_CALL(vkCmdBeginConditionalRenderingEXT(list->cmd.vk_command_buffer, &begin_info));
    }
    else
    {
        memset(&list->predication, 0, sizeof(list->predication));
    }
}

/* *PIXEvent* enum values and consts are from PIXEventsCommon.h of winpixeventruntime package  */

static char *decode_pix_blob(const void *data, size_t size)
{
    static const UINT64 PIXEventsStringIsANSIReadMask = 0x0040000000000000;
    static const UINT64 PIXEventsTypeReadMask = 0x00000000000FFC00;
    static const UINT64 PIXEventsTypeBitShift = 10;
    UINT64 *data_uint64_aligned = (UINT64*)(data);
    size_t label_str_length;
    char *label_str = NULL;
    bool is_ansi;
    UINT64 type;

    enum PIXEventType
    {
        ePIXEvent_EndEvent = 0x000,
        ePIXEvent_BeginEvent_VarArgs = 0x001,
        ePIXEvent_BeginEvent_NoArgs = 0x002,
        ePIXEvent_SetMarker_VarArgs = 0x007,
        ePIXEvent_SetMarker_NoArgs = 0x008,

        ePIXEvent_EndEvent_OnContext = 0x010,
        ePIXEvent_BeginEvent_OnContext_VarArgs = 0x011,
        ePIXEvent_BeginEvent_OnContext_NoArgs = 0x012,
        ePIXEvent_SetMarker_OnContext_VarArgs = 0x017,
        ePIXEvent_SetMarker_OnContext_NoArgs = 0x018,
    };

    type = (*data_uint64_aligned & PIXEventsTypeReadMask) >> PIXEventsTypeBitShift;

    if (type == ePIXEvent_SetMarker_NoArgs)
        type = ePIXEvent_BeginEvent_NoArgs;

    if (type == ePIXEvent_SetMarker_VarArgs)
        type = ePIXEvent_BeginEvent_VarArgs;

    /* ePIXEvent_*_VarArgs is commonly used without actual parameters 
       String fromatting will overcomplicate things and skipped for now */
    if ((type != ePIXEvent_BeginEvent_NoArgs) && (type != ePIXEvent_BeginEvent_VarArgs))
    {
        WARN("Unexpected/unsupported PIX3Event: %#"PRIx64".\n", type);
        return NULL;
    }

    /* skip Color */
    data_uint64_aligned++;
    data_uint64_aligned++;

    is_ansi = *data_uint64_aligned & PIXEventsStringIsANSIReadMask;

    data_uint64_aligned++;
    
    if (is_ansi)
    {
        label_str_length = (size - 24);
        label_str = vkd3d_strdup_n((const char*)data_uint64_aligned, label_str_length);
    }
    else
    {
        label_str_length = (size - 24) / 2;
        label_str = vkd3d_strdup_w_utf8((const WCHAR*)data_uint64_aligned, label_str_length);
    }

    return label_str;
}

static char *decode_pix_string(UINT metadata, const void *data, size_t size)
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
        label_str = vkd3d_strdup_w_utf8(data, size / sizeof(WCHAR));
        if (!label_str)
            return NULL;
        break;

    case PIX_EVENT_PIX3BLOB_VERSION:
        label_str = decode_pix_blob(data, size);
        if (!label_str)
            return NULL;
        break;

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
    char *label_str;

    if (!list->device->vk_info.EXT_debug_utils)
        return;

    label_str = decode_pix_string(metadata, data, size);
    if (!label_str)
    {
        FIXME("Failed to decode PIX debug event.\n");
        return;
    }

    d3d12_command_list_debug_mark_label(list, label_str, 1.0f, 1.0f, 1.0f, 1.0f);
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

    label_str = decode_pix_string(metadata, data, size);
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

    VK_CALL(vkCmdBeginDebugUtilsLabelEXT(list->cmd.vk_command_buffer, &label));
    vkd3d_free(label_str);
}

static void STDMETHODCALLTYPE d3d12_command_list_EndEvent(d3d12_command_list_iface *iface)
{
    struct d3d12_command_list *list = impl_from_ID3D12GraphicsCommandList(iface);
    const struct vkd3d_vk_device_procs *vk_procs = &list->device->vk_procs;

    TRACE("iface %p.\n", iface);

    if (!list->device->vk_info.EXT_debug_utils)
        return;

    VK_CALL(vkCmdEndDebugUtilsLabelEXT(list->cmd.vk_command_buffer));
}

STATIC_ASSERT(sizeof(VkDispatchIndirectCommand) == sizeof(D3D12_DISPATCH_ARGUMENTS));
STATIC_ASSERT(sizeof(VkDrawIndexedIndirectCommand) == sizeof(D3D12_DRAW_INDEXED_ARGUMENTS));
STATIC_ASSERT(sizeof(VkDrawIndirectCommand) == sizeof(D3D12_DRAW_ARGUMENTS));
STATIC_ASSERT(offsetof(VkTraceRaysIndirectCommand2KHR, depth) == offsetof(D3D12_DISPATCH_RAYS_DESC, Depth));

static HRESULT d3d12_command_signature_allocate_stream_memory_for_list(
        struct d3d12_command_list *list,
        struct d3d12_command_signature *signature,
        uint32_t max_command_count,
        struct vkd3d_scratch_allocation *allocation);

static HRESULT d3d12_command_signature_allocate_preprocess_memory_for_list_ext(
        struct d3d12_command_list *list,
        struct d3d12_command_signature *signature, VkPipeline render_pipeline,
        bool explicit_preprocess,
        uint32_t max_command_count,
        struct vkd3d_scratch_allocation *allocation, VkDeviceSize *size);

static HRESULT d3d12_command_signature_allocate_preprocess_memory_for_list_nv(
        struct d3d12_command_list *list,
        struct d3d12_command_signature *signature, VkPipeline render_pipeline,
        bool explicit_preprocess,
        uint32_t max_command_count,
        struct vkd3d_scratch_allocation *allocation, VkDeviceSize *size);

static void d3d12_command_list_execute_indirect_state_template_compute(
        struct d3d12_command_list *list, struct d3d12_command_signature *signature,
        uint32_t max_command_count,
        struct d3d12_resource *arg_buffer, UINT64 arg_buffer_offset,
        struct d3d12_resource *count_buffer, UINT64 count_buffer_offset)
{
    const struct vkd3d_vk_device_procs *vk_procs = &list->device->vk_procs;
    VkDeviceAddress arg_va = arg_buffer->res.va + arg_buffer_offset;
    struct vkd3d_scratch_allocation dispatch_scratch, ubo_scratch;
    VkDeviceAddress count_va = 0;
    VkWriteDescriptorSet write;
    VkDescriptorBufferInfo buf;
    VkPipelineLayout vk_layout;
    uint32_t write_set;
    unsigned int i;

    d3d12_command_list_end_current_render_pass(list, false);
    d3d12_command_list_end_transfer_batch(list);

    if (count_buffer)
        count_va = count_buffer->res.va + count_buffer_offset;

    if (!d3d12_command_list_emit_multi_dispatch_indirect_count_state(list,
            signature,
            arg_va, signature->desc.ByteStride, max_command_count,
            count_va, &dispatch_scratch, &ubo_scratch))
        return;

    if (!d3d12_command_list_update_compute_state(list))
    {
        WARN("Failed to update compute state, ignoring dispatch.\n");
        return;
    }

    vk_write_descriptor_set_from_scratch_push_ubo(&write, &buf, &ubo_scratch,
            D3D12_MAX_ROOT_COST * sizeof(uint32_t),
            list->compute_bindings.root_signature->push_constant_ubo_binding.binding);

    vk_layout = list->compute_bindings.root_signature->compute.vk_pipeline_layout;
    write_set = list->compute_bindings.root_signature->push_constant_ubo_binding.set;

    /* Run indirect dispatches back to back with one push UBO per dispatch which lets us
     * update root parameters per command. */
    for (i = 0; i < max_command_count; i++)
    {
        VK_CALL(vkCmdPushDescriptorSetKHR(list->cmd.vk_command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                vk_layout, write_set, 1, &write));
        VK_CALL(vkCmdDispatchIndirect(list->cmd.vk_command_buffer, dispatch_scratch.buffer, dispatch_scratch.offset));

        VKD3D_BREADCRUMB_AUX32(i);
        VKD3D_BREADCRUMB_COMMAND(EXECUTE_INDIRECT_UNROLL_COMPUTE);

        dispatch_scratch.offset += sizeof(VkDispatchIndirectCommand);
        buf.offset += D3D12_MAX_ROOT_COST * sizeof(uint32_t);
    }

    /* Need to clear state to zero if it was part of a command signature. */
    for (i = 0; i < signature->desc.NumArgumentDescs; i++)
    {
        const D3D12_INDIRECT_ARGUMENT_DESC *arg = &signature->desc.pArgumentDescs[i];
        switch (arg->Type)
        {
            case D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT_BUFFER_VIEW:
            case D3D12_INDIRECT_ARGUMENT_TYPE_SHADER_RESOURCE_VIEW:
            case D3D12_INDIRECT_ARGUMENT_TYPE_UNORDERED_ACCESS_VIEW:
            {
                uint32_t index = arg->ConstantBufferView.RootParameterIndex;
                d3d12_command_list_set_root_descriptor(list,
                        &list->compute_bindings, index, 0);
                break;
            }

            case D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT:
            {
                uint32_t zeroes[D3D12_MAX_ROOT_COST];
                memset(zeroes, 0, sizeof(uint32_t) * arg->Constant.Num32BitValuesToSet);
                d3d12_command_list_set_root_constants(list,
                        &list->compute_bindings, arg->Constant.RootParameterIndex,
                        arg->Constant.DestOffsetIn32BitValues,
                        arg->Constant.Num32BitValuesToSet, zeroes);
                break;
            }

            default:
                break;
        }
    }

    /* No need to implicitly invalidate anything here, since we used the normal APIs. */
}

static void d3d12_command_list_execute_indirect_state_template_dgc(
        struct d3d12_command_list *list, struct d3d12_command_signature *signature,
        uint32_t max_command_count,
        struct d3d12_resource *arg_buffer, UINT64 arg_buffer_offset,
        struct d3d12_resource *count_buffer, UINT64 count_buffer_offset)
{
    const struct vkd3d_vk_device_procs *vk_procs = &list->device->vk_procs;
    static const unsigned int max_direct_commands_for_split = 64;
    VkConditionalRenderingBeginInfoEXT conditional_begin_info;
    struct vkd3d_scratch_allocation predication_allocation;
    struct vkd3d_scratch_allocation preprocess_allocation;
    struct vkd3d_scratch_allocation stream_allocation;
    uint32_t minIndirectCommandsBufferOffsetAlignment;
    struct vkd3d_scratch_allocation count_allocation;
    VkGeneratedCommandsPipelineInfoEXT pipeline_info;
    uint32_t minSequencesCountBufferOffsetAlignment;
    struct vkd3d_execute_indirect_args patch_args;
    struct vkd3d_pipeline_bindings *bindings;
    VkGeneratedCommandsInfoEXT generated_ext;
    VkGeneratedCommandsInfoNV generated_nv;
    VkCommandBuffer vk_patch_cmd_buffer;
    VkIndirectCommandsStreamNV stream;
    bool require_custom_predication;
    VkDeviceSize preprocess_size;
    VkPipeline current_pipeline;
    VkDependencyInfo dep_info;
    VkMemoryBarrier2 barrier;
    bool restart_predication;
    bool explicit_preprocess;
    bool require_ibo_update;
    bool require_patch;
    bool use_ext_dgc;
    const char *tag;
    unsigned int i;
    HRESULT hr;

    use_ext_dgc = list->device->device_info.device_generated_commands_features_ext.deviceGeneratedCommands;
    require_custom_predication = false;
    restart_predication = false;
    explicit_preprocess = false;

    if (list->predication.va)
    {
        /* Predication works on NV driver here, so we assume it's intended by spec.
         * It does not work on RADV yet, so we'll fold predication in with our optimization work which
         * generates a predicate anyway. */
        if (!(vkd3d_config_flags & VKD3D_CONFIG_FLAG_SKIP_DRIVER_WORKAROUNDS) &&
                !list->device->device_info.device_generated_commands_features_ext.deviceGeneratedCommands &&
                list->device->device_info.vulkan_1_2_properties.driverID == VK_DRIVER_ID_MESA_RADV)
        {
            union vkd3d_predicate_command_direct_args args;
            enum vkd3d_predicate_command_type type;
            VkDeviceAddress count_va;
            restart_predication = list->predication.enabled_on_command_buffer;

            if (count_buffer)
            {
                count_va = count_buffer->res.va + count_buffer_offset;
                type = VKD3D_PREDICATE_COMMAND_DRAW_INDIRECT_COUNT;
            }
            else
            {
                count_va = 0;
                type = VKD3D_PREDICATE_COMMAND_DRAW_INDIRECT;
                args.draw_count = max_command_count;
            }

            if (restart_predication)
            {
                /* Have to begin/end predication outside a render pass. */
                d3d12_command_list_end_current_render_pass(list, true);
                VK_CALL(vkCmdEndConditionalRenderingEXT(list->cmd.vk_command_buffer));
            }

            d3d12_command_list_emit_predicated_command(list, type, count_va, &args, &predication_allocation);
            require_custom_predication = true;
        }
    }
    else if (!count_buffer && max_command_count <= max_direct_commands_for_split &&
            signature->pipeline_type != VKD3D_PIPELINE_TYPE_COMPUTE &&
            list->device->device_info.vulkan_1_2_properties.driverID == VK_DRIVER_ID_MESA_RADV &&
            (list->vk_queue_flags & VK_QUEUE_GRAPHICS_BIT))
    {
        /* If we had indirect barriers earlier in the frame, now might be a good time to split. */
        d3d12_command_list_consider_new_sequence(list);

        if (list->cmd.vk_command_buffer != list->cmd.vk_init_commands_post_indirect_barrier)
        {
            /* For non-indirect execute indirect, there's a high risk of individual draws being empty,
             * since the typical use case is atomic increment X workgroup count or instanceCount.
             * Try to nop out the entire execution in one fell swoop.
             * We can either use indirectCount = 0, or conditional rendering.
             * indirectCount = 0 is easier to understand, works everywhere and slots nicely into the predication
             * fallback code we have in place. */

            /* Only attempt this if we can do it in a hoisted fashion. Otherwise, we're just introducing stalls
             * which may as well be just as bad ... */

            /* Only consider this for graphics. Graphics execute indirect is assumed
             * to be the result of (occlusion) culling, which is very likely to cause empty draws.
             * Compute is less likely, at least in the content we have looked at. */

            /* This makes sense on RADV since it will use indirectCount == 0 as a predicate to skip
             * all pre-process work. Maybe it makes sense on NV, will need further investigation.
             * This only applies to GFX queue on RADV, since async compute does not use INDIRECT_BUFFER directly. */
            union vkd3d_predicate_command_direct_args args;
            VkDeviceAddress indirect_va =
                    arg_buffer->res.va + arg_buffer_offset + signature->argument_buffer_offset_for_command;
            args.execute_indirect.max_commands = max_command_count;
            args.execute_indirect.stride_words = signature->desc.ByteStride / sizeof(uint32_t);

            d3d12_command_list_emit_predicated_command(list,
                    signature->pipeline_type == VKD3D_PIPELINE_TYPE_COMPUTE ?
                            VKD3D_PREDICATE_COMMAND_EXECUTE_INDIRECT_COMPUTE :
                            VKD3D_PREDICATE_COMMAND_EXECUTE_INDIRECT_GRAPHICS,
                    indirect_va, &args, &predication_allocation);
            require_custom_predication = true;
        }
    }

    /* If we have custom predication, we would need to introduce a barrier to synchronize with the
     * new indirect count, which is not desirable. */
    if (!require_custom_predication &&
            (signature->state_template.dgc.layout_preprocess_nv ||
             signature->state_template.dgc.layout_preprocess_ext))
    {
        /* If driver can take advantage of preprocess, we can consider preprocessing explicitly if we can hoist it.
         * If we had indirect barriers earlier in the frame, now might be a good time to split. */
        d3d12_command_list_consider_new_sequence(list);
        if (list->cmd.vk_command_buffer != list->cmd.vk_init_commands_post_indirect_barrier)
            explicit_preprocess = true;
    }

    /* To build device generated commands, we need to know the pipeline we're going to render with. */
    if (signature->pipeline_type == VKD3D_PIPELINE_TYPE_COMPUTE)
    {
        if (!d3d12_command_list_update_compute_pipeline(list))
            return;

        /* Needed for workarounds later. */
        if (!(list->vk_queue_flags & VK_QUEUE_GRAPHICS_BIT))
            list->cmd.uses_dgc_compute_in_async_compute = true;
    }
    else
    {
        d3d12_command_list_promote_dsv_layout(list);
        if (!d3d12_command_list_update_graphics_pipeline(list, signature->pipeline_type))
            return;
    }

    bindings = signature->pipeline_type == VKD3D_PIPELINE_TYPE_COMPUTE ?
          &list->compute_bindings : &list->graphics_bindings;

    current_pipeline = list->current_pipeline;

    memset(&patch_args, 0, sizeof(patch_args));
    patch_args.debug_tag = 0; /* Modify to non-zero value as desired when debugging. */

    /* If everything regarding alignment works out, we can just reuse the app indirect buffer instead. */
    require_ibo_update = false;
    require_patch = false;

    /* Bind IBO. If we always update the IBO indirectly, do not validate the index buffer here.
     * We can render fine even with a NULL IBO bound. */
    for (i = 0; i < signature->desc.NumArgumentDescs; i++)
    {
        if (signature->desc.pArgumentDescs[i].Type == D3D12_INDIRECT_ARGUMENT_TYPE_INDEX_BUFFER_VIEW)
        {
            require_ibo_update = true;
            break;
        }
    }

    /* - Stride can mismatch, i.e. we need internal alignment of arguments.
     * - Min required alignment on the indirect buffer itself might be too strict.
     * - Min required alignment on count buffer might be too strict.
     * - We require debugging. */
    if (list->device->device_info.device_generated_commands_features_ext.deviceGeneratedCommands)
    {
        /* This is implied by specification. */
        minIndirectCommandsBufferOffsetAlignment = 4;
        minSequencesCountBufferOffsetAlignment = 4;
    }
    else
    {
        const VkPhysicalDeviceDeviceGeneratedCommandsPropertiesNV *props =
                &list->device->device_info.device_generated_commands_properties_nv;
        minIndirectCommandsBufferOffsetAlignment = props->minIndirectCommandsBufferOffsetAlignment;
        minSequencesCountBufferOffsetAlignment = props->minSequencesCountBufferOffsetAlignment;
    }

    if ((signature->state_template.dgc.stride != signature->desc.ByteStride && max_command_count > 1) ||
            (arg_buffer_offset & (minIndirectCommandsBufferOffsetAlignment - 1)) ||
            (count_buffer && (count_buffer_offset & (minSequencesCountBufferOffsetAlignment - 1))) ||
            patch_args.debug_tag)
    {
        require_patch = true;
    }

    if (require_patch)
    {
        /* Patching does not really happen anymore on drivers, drop explicit preprocess in this path
         * to reduce test burden. */
        explicit_preprocess = false;

        if (FAILED(hr = d3d12_command_signature_allocate_stream_memory_for_list(
                list, signature, max_command_count, &stream_allocation)))
        {
            WARN("Failed to allocate stream memory.\n");
            return;
        }

        if (count_buffer)
        {
            if (FAILED(hr = d3d12_command_allocator_allocate_scratch_memory(list->allocator,
                    VKD3D_SCRATCH_POOL_KIND_DEVICE_STORAGE,
                    sizeof(uint32_t),
                    minSequencesCountBufferOffsetAlignment,
                    ~0u, &count_allocation)))
            {
                WARN("Failed to allocate count memory.\n");
                return;
            }
        }

        patch_args.template_va = signature->state_template.dgc.buffer_va;
        patch_args.api_buffer_va = arg_buffer->res.va + arg_buffer_offset;
        patch_args.device_generated_commands_va = stream_allocation.va;
        patch_args.indirect_count_va = count_buffer ? count_buffer->res.va + count_buffer_offset : 0;
        patch_args.dst_indirect_count_va = count_buffer ? count_allocation.va : 0;
        patch_args.api_buffer_word_stride = signature->desc.ByteStride / sizeof(uint32_t);
        patch_args.device_generated_commands_word_stride = signature->state_template.dgc.stride / sizeof(uint32_t);

        if (patch_args.debug_tag != 0)
        {
            /* Makes log easier to understand since a sorted log will appear in-order. */
            static uint32_t vkd3d_implicit_instance_count;
            patch_args.implicit_instance = vkd3d_atomic_uint32_increment(
                    &vkd3d_implicit_instance_count, vkd3d_memory_order_relaxed) - 1;
        }

        d3d12_command_allocator_allocate_init_post_indirect_command_buffer(list->allocator, list);
        vk_patch_cmd_buffer = list->cmd.vk_init_commands_post_indirect_barrier;

        if (vk_patch_cmd_buffer == list->cmd.vk_command_buffer)
        {
            d3d12_command_list_end_current_render_pass(list, true);
            d3d12_command_list_invalidate_current_pipeline(list, true);
        }
        else
            list->cmd.indirect_meta->need_compute_to_indirect_barrier = true;

        VK_CALL(vkCmdPushConstants(vk_patch_cmd_buffer, signature->state_template.dgc.pipeline.vk_pipeline_layout,
                VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(patch_args), &patch_args));
        VK_CALL(vkCmdBindPipeline(vk_patch_cmd_buffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                signature->state_template.dgc.pipeline.vk_pipeline));

        /* One workgroup processes the patching for one draw. We could potentially use indirect dispatch
         * to restrict the patching work to just the indirect count, but meh, just more barriers.
         * We'll nop out the workgroup early based on direct count, and the number of threads should be trivial either way. */
        VK_CALL(vkCmdDispatch(vk_patch_cmd_buffer, max_command_count, 1, 1));

        if (vk_patch_cmd_buffer == list->cmd.vk_command_buffer)
        {
            memset(&dep_info, 0, sizeof(dep_info));
            dep_info.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
            dep_info.memoryBarrierCount = 1;
            dep_info.pMemoryBarriers = &barrier;

            memset(&barrier, 0, sizeof(barrier));
            barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
            barrier.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
            barrier.srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
            barrier.dstStageMask = VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT;
            barrier.dstAccessMask = VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT;

            VK_CALL(vkCmdPipelineBarrier2(vk_patch_cmd_buffer, &dep_info));
            /* The barrier is deferred if we moved the dispatch to init command buffer. */
        }
    }

    if (signature->pipeline_type == VKD3D_PIPELINE_TYPE_GRAPHICS ||
            signature->pipeline_type == VKD3D_PIPELINE_TYPE_MESH_GRAPHICS)
    {
        if (!d3d12_command_list_begin_render_pass(list, signature->pipeline_type))
        {
            WARN("Failed to begin render pass, ignoring draw.\n");
            return;
        }
    }

    if (signature->pipeline_type == VKD3D_PIPELINE_TYPE_COMPUTE &&
            !d3d12_command_list_update_compute_state(list))
        return;

    if (!require_ibo_update &&
            signature->desc.pArgumentDescs[signature->desc.NumArgumentDescs - 1].Type ==
                    D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED &&
            !d3d12_command_list_update_index_buffer(list))
    {
        return;
    }

    if (use_ext_dgc)
    {
        if (FAILED(hr = d3d12_command_signature_allocate_preprocess_memory_for_list_ext(
                list, signature, current_pipeline, explicit_preprocess,
                max_command_count, &preprocess_allocation, &preprocess_size)))
        {
            WARN("Failed to allocate preprocess memory.\n");
            return;
        }

        pipeline_info.sType = VK_STRUCTURE_TYPE_GENERATED_COMMANDS_PIPELINE_INFO_EXT;
        pipeline_info.pNext = NULL;
        pipeline_info.pipeline = list->current_pipeline;

        memset(&generated_ext, 0, sizeof(generated_ext));
        generated_ext.sType = VK_STRUCTURE_TYPE_GENERATED_COMMANDS_INFO_EXT;
        generated_ext.pNext = &pipeline_info;

        if (signature->pipeline_type == VKD3D_PIPELINE_TYPE_COMPUTE)
            generated_ext.shaderStages = VK_SHADER_STAGE_COMPUTE_BIT;
        else
            generated_ext.shaderStages = list->state->graphics.stage_flags;

        generated_ext.indirectCommandsLayout = explicit_preprocess ?
                signature->state_template.dgc.layout_preprocess_ext :
                signature->state_template.dgc.layout_implicit_ext;
        generated_ext.preprocessAddress = preprocess_allocation.va;
        generated_ext.preprocessSize = preprocess_size;
        generated_ext.maxSequenceCount = max_command_count;

        if (require_custom_predication)
        {
            generated_ext.sequenceCountAddress = predication_allocation.va;
        }
        else if (count_buffer)
        {
            if (require_patch)
                generated_ext.sequenceCountAddress = count_allocation.va;
            else
                generated_ext.sequenceCountAddress = count_buffer->res.va + count_buffer_offset;
        }
    }
    else
    {
        if (FAILED(hr = d3d12_command_signature_allocate_preprocess_memory_for_list_nv(
                list, signature, current_pipeline, explicit_preprocess,
                max_command_count, &preprocess_allocation, &preprocess_size)))
        {
            WARN("Failed to allocate preprocess memory.\n");
            return;
        }

        memset(&generated_nv, 0, sizeof(generated_nv));
        generated_nv.sType = VK_STRUCTURE_TYPE_GENERATED_COMMANDS_INFO_NV;
        generated_nv.pipeline = list->current_pipeline;
        generated_nv.pipelineBindPoint = signature->pipeline_type == VKD3D_PIPELINE_TYPE_COMPUTE ?
                VK_PIPELINE_BIND_POINT_COMPUTE : VK_PIPELINE_BIND_POINT_GRAPHICS;
        generated_nv.indirectCommandsLayout = explicit_preprocess ?
                signature->state_template.dgc.layout_preprocess_nv :
                signature->state_template.dgc.layout_implicit_nv;
        generated_nv.streamCount = 1;
        generated_nv.pStreams = &stream;
        generated_nv.preprocessBuffer = preprocess_allocation.buffer;
        generated_nv.preprocessOffset = preprocess_allocation.offset;
        generated_nv.preprocessSize = preprocess_size;
        generated_nv.sequencesCount = max_command_count;

        if (require_custom_predication)
        {
            generated_nv.sequencesCountBuffer = predication_allocation.buffer;
            generated_nv.sequencesCountOffset = predication_allocation.offset;
        }
        else if (count_buffer)
        {
            if (require_patch)
            {
                generated_nv.sequencesCountBuffer = count_allocation.buffer;
                generated_nv.sequencesCountOffset = count_allocation.offset;
            }
            else
            {
                generated_nv.sequencesCountBuffer = count_buffer->res.vk_buffer;
                generated_nv.sequencesCountOffset = count_buffer->mem.offset + count_buffer_offset;
            }
        }
    }

    if (require_patch)
    {
        if (use_ext_dgc)
        {
            generated_ext.indirectAddress = stream_allocation.va;
            generated_ext.indirectAddressSize = max_command_count * signature->state_template.dgc.stride;
        }
        else
        {
            stream.buffer = stream_allocation.buffer;
            stream.offset = stream_allocation.offset;
        }
    }
    else
    {
        if (use_ext_dgc)
        {
            generated_ext.indirectAddress = arg_buffer->res.va + arg_buffer_offset;
            generated_ext.indirectAddressSize = max_command_count * signature->state_template.dgc.stride;
        }
        else
        {
            stream.buffer = arg_buffer->res.vk_buffer;
            stream.offset = arg_buffer->mem.offset + arg_buffer_offset;
        }
    }

    if (explicit_preprocess)
    {
        d3d12_command_allocator_allocate_init_post_indirect_command_buffer(list->allocator, list);

        if (use_ext_dgc)
        {
            VK_CALL(vkCmdPreprocessGeneratedCommandsEXT(list->cmd.vk_init_commands_post_indirect_barrier,
                    &generated_ext, list->cmd.vk_command_buffer));
        }
        else
        {
            /* With graphics NV_dgc, there are no requirements on bound state, except for pipeline. */
            /* NV_dgcc however requires that state in recording command buffer matches, but EXT_dgc provides a state cmd. */
            VK_CALL(vkCmdBindPipeline(list->cmd.vk_init_commands_post_indirect_barrier,
                    signature->pipeline_type == VKD3D_PIPELINE_TYPE_COMPUTE ?
                            VK_PIPELINE_BIND_POINT_COMPUTE : VK_PIPELINE_BIND_POINT_GRAPHICS, current_pipeline));

            if (signature->pipeline_type == VKD3D_PIPELINE_TYPE_COMPUTE)
            {
                /* Compute is a bit more stringent, we have to bind all state. */
                d3d12_command_list_update_descriptors_post_indirect_buffer(list);
            }

            /* Predication state also has to match. Also useful to nop out explicit preprocess too.
             * Assumption is that drivers will pull predication state from state command buffer on EXT,
             * since states have to match. */
            if (list->predication.enabled_on_command_buffer)
            {
                conditional_begin_info.sType = VK_STRUCTURE_TYPE_CONDITIONAL_RENDERING_BEGIN_INFO_EXT;
                conditional_begin_info.pNext = NULL;
                conditional_begin_info.buffer = list->predication.vk_buffer;
                conditional_begin_info.offset = list->predication.vk_buffer_offset;
                conditional_begin_info.flags = 0;
                VK_CALL(vkCmdBeginConditionalRenderingEXT(list->cmd.vk_init_commands_post_indirect_barrier,
                        &conditional_begin_info));
            }

            VK_CALL(vkCmdPreprocessGeneratedCommandsNV(list->cmd.vk_init_commands_post_indirect_barrier, &generated_nv));

            if (list->predication.enabled_on_command_buffer)
                VK_CALL(vkCmdEndConditionalRenderingEXT(list->cmd.vk_init_commands_post_indirect_barrier));
        }

        list->cmd.indirect_meta->need_preprocess_barrier = true;
    }

    if (require_patch)
        WARN("Template requires patching :(\n");

    /* Makes RGP captures easier to read. */
    if (signature->pipeline_type == VKD3D_PIPELINE_TYPE_COMPUTE)
    {
        if (count_buffer)
            tag = "EI (compute, indirect count)";
        else
            tag = "EI (compute, direct count)";
    }
    else
    {
        if (count_buffer)
            tag = "EI (gfx, indirect count)";
        else
            tag = "EI (gfx, direct count)";
    }
    d3d12_command_list_debug_mark_begin_region(list, tag);

    if (use_ext_dgc)
    {
        VK_CALL(vkCmdExecuteGeneratedCommandsEXT(list->cmd.vk_command_buffer,
                explicit_preprocess ? VK_TRUE : VK_FALSE, &generated_ext));
    }
    else
    {
        VK_CALL(vkCmdExecuteGeneratedCommandsNV(list->cmd.vk_command_buffer,
                explicit_preprocess ? VK_TRUE : VK_FALSE, &generated_nv));
    }

    d3d12_command_list_debug_mark_end_region(list);

    /* Need to clear state to zero if it was part of a command signature. */
    for (i = 0; i < signature->desc.NumArgumentDescs; i++)
    {
        const D3D12_INDIRECT_ARGUMENT_DESC *arg = &signature->desc.pArgumentDescs[i];
        switch (arg->Type)
        {
            case D3D12_INDIRECT_ARGUMENT_TYPE_INDEX_BUFFER_VIEW:
                /* Null IBO */
                list->index_buffer.buffer = VK_NULL_HANDLE;
                list->index_buffer.is_dirty = true;
                break;

            case D3D12_INDIRECT_ARGUMENT_TYPE_VERTEX_BUFFER_VIEW:
            {
                /* Null VBO */
                uint32_t slot = arg->VertexBuffer.Slot;
                list->dynamic_state.vertex_buffers[slot] = VK_NULL_HANDLE;
                list->dynamic_state.vertex_strides[slot] = 0;
                list->dynamic_state.vertex_offsets[slot] = 0;
                list->dynamic_state.vertex_sizes[slot] = 0;
                break;
            }

            case D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT_BUFFER_VIEW:
            case D3D12_INDIRECT_ARGUMENT_TYPE_SHADER_RESOURCE_VIEW:
            case D3D12_INDIRECT_ARGUMENT_TYPE_UNORDERED_ACCESS_VIEW:
            {
                uint32_t index = arg->ConstantBufferView.RootParameterIndex;
                d3d12_command_list_set_root_descriptor(list,
                        bindings, index, 0);
                break;
            }

            case D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT:
            {
                uint32_t zeroes[D3D12_MAX_ROOT_COST];
                memset(zeroes, 0, sizeof(uint32_t) * arg->Constant.Num32BitValuesToSet);
                d3d12_command_list_set_root_constants(list,
                        bindings, arg->Constant.RootParameterIndex,
                        arg->Constant.DestOffsetIn32BitValues,
                        arg->Constant.Num32BitValuesToSet, zeroes);
                break;
            }

            case D3D12_INDIRECT_ARGUMENT_TYPE_INCREMENTING_CONSTANT:
            {
                uint32_t zero = 0;
                d3d12_command_list_set_root_constants(list,
                        bindings, arg->IncrementingConstant.RootParameterIndex,
                        arg->IncrementingConstant.DestOffsetIn32BitValues,
                        1, &zero);
                break;
            }

            default:
                break;
        }
    }

    /* Spec mentions that all state related to the bind point is undefined after this, so
     * invalidate all state. Unclear exactly which state is invalidated though ...
     * Treat it as a meta shader. We need to nuke all state after running execute generated commands. */
    d3d12_command_list_invalidate_all_state(list);

    if (restart_predication)
    {
        /* Have to begin/end predication outside a render pass. */
        d3d12_command_list_end_current_render_pass(list, true);

        /* Rearm the conditional rendering. */
        conditional_begin_info.sType = VK_STRUCTURE_TYPE_CONDITIONAL_RENDERING_BEGIN_INFO_EXT;
        conditional_begin_info.pNext = NULL;
        conditional_begin_info.buffer = list->predication.vk_buffer;
        conditional_begin_info.offset = list->predication.vk_buffer_offset;
        conditional_begin_info.flags = 0;
        VK_CALL(vkCmdBeginConditionalRenderingEXT(list->cmd.vk_command_buffer, &conditional_begin_info));
    }
}

static void STDMETHODCALLTYPE d3d12_command_list_ExecuteIndirect(d3d12_command_list_iface *iface,
        ID3D12CommandSignature *command_signature, UINT max_command_count, ID3D12Resource *arg_buffer,
        UINT64 arg_buffer_offset, ID3D12Resource *count_buffer, UINT64 count_buffer_offset)
{
    struct d3d12_command_signature *sig_impl = impl_from_ID3D12CommandSignature(command_signature);
    struct d3d12_resource *count_impl = impl_from_ID3D12Resource(count_buffer);
    struct d3d12_resource *arg_impl = impl_from_ID3D12Resource(arg_buffer);
    struct d3d12_command_list *list = impl_from_ID3D12GraphicsCommandList(iface);
    const struct vkd3d_vk_device_procs *vk_procs = &list->device->vk_procs;
    const D3D12_COMMAND_SIGNATURE_DESC *signature_desc = &sig_impl->desc;
    struct vkd3d_scratch_allocation scratch;
    uint32_t unrolled_stride;
    unsigned int i;

    TRACE("iface %p, command_signature %p, max_command_count %u, arg_buffer %p, "
            "arg_buffer_offset %#"PRIx64", count_buffer %p, count_buffer_offset %#"PRIx64".\n",
            iface, command_signature, max_command_count, arg_buffer, arg_buffer_offset,
            count_buffer, count_buffer_offset);

    if (!max_command_count)
        return;

    if ((count_buffer || list->predication.fallback_enabled) && !list->device->device_info.vulkan_1_2_features.drawIndirectCount)
    {
        FIXME("Count buffers not supported by Vulkan implementation.\n");
        return;
    }

    list->cmd.estimated_cost += VKD3D_COMMAND_COST_HIGH * max_command_count;

    unrolled_stride = signature_desc->ByteStride;

    VKD3D_BREADCRUMB_TAG("ExecuteIndirect [MaxCommandCount, ArgBuffer cookie, ArgBuffer offset, Count cookie, Count offset]");
    VKD3D_BREADCRUMB_AUX32(max_command_count);
    VKD3D_BREADCRUMB_COOKIE(arg_impl->res.cookie);
    VKD3D_BREADCRUMB_AUX64(arg_buffer_offset);
    VKD3D_BREADCRUMB_COOKIE(count_impl ? count_impl->res.cookie : 0);
    VKD3D_BREADCRUMB_AUX64(count_buffer_offset);

    if (sig_impl->requires_state_template)
    {
        if (sig_impl->requires_state_template_dgc)
        {
            d3d12_command_list_execute_indirect_state_template_dgc(list, sig_impl,
                    max_command_count,
                    arg_impl, arg_buffer_offset,
                    count_impl, count_buffer_offset);
        }
        else if (sig_impl->pipeline_type == VKD3D_PIPELINE_TYPE_COMPUTE)
        {
            d3d12_command_list_execute_indirect_state_template_compute(list, sig_impl,
                    max_command_count,
                    arg_impl, arg_buffer_offset,
                    count_impl, count_buffer_offset);
        }

        VKD3D_BREADCRUMB_COMMAND(EXECUTE_INDIRECT_TEMPLATE);
        return;
    }

    /* Temporary workaround, since we cannot parse non-draw arguments yet. Point directly
     * to the first argument. Should avoid hard crashes for now. */
    arg_buffer_offset += sig_impl->argument_buffer_offset_for_command;

    for (i = 0; i < signature_desc->NumArgumentDescs; ++i)
    {
        const D3D12_INDIRECT_ARGUMENT_DESC *arg_desc = &signature_desc->pArgumentDescs[i];

        if (list->predication.fallback_enabled)
        {
            union vkd3d_predicate_command_direct_args args;
            enum vkd3d_predicate_command_type type;
            VkDeviceAddress indirect_va;

            switch (arg_desc->Type)
            {
                case D3D12_INDIRECT_ARGUMENT_TYPE_DRAW:
                case D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED:
                    if (count_buffer)
                    {
                        type = VKD3D_PREDICATE_COMMAND_DRAW_INDIRECT_COUNT;
                        indirect_va = count_impl->res.va + count_buffer_offset;
                    }
                    else
                    {
                        args.draw_count = max_command_count;
                        type = VKD3D_PREDICATE_COMMAND_DRAW_INDIRECT;
                        indirect_va = 0;
                    }
                    break;

                case D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH:
                case D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH_MESH:
                    type = VKD3D_PREDICATE_COMMAND_DISPATCH_INDIRECT;
                    indirect_va = arg_impl->res.va + arg_buffer_offset;
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
            /* Unroll to N normal indirect dispatches, use count buffer to mask dispatches to (0, 0, 0).
             * Can use this path for indirect trace rays as well since as needed. */
            if (arg_desc->Type == D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH)
            {
                if (!d3d12_command_list_emit_multi_dispatch_indirect_count(list,
                        arg_impl->res.va + arg_buffer_offset,
                        unrolled_stride, max_command_count,
                        count_impl->res.va + count_buffer_offset, &scratch))
                    return;

                unrolled_stride = sizeof(VkDispatchIndirectCommand);
            }
            else
            {
                scratch.buffer = count_impl->res.vk_buffer;
                scratch.offset = count_impl->mem.offset + count_buffer_offset;
                scratch.va = count_impl->res.va + count_buffer_offset;
            }
        }
        else
        {
            scratch.buffer = arg_impl->res.vk_buffer;
            scratch.offset = arg_impl->mem.offset + arg_buffer_offset;
            scratch.va = arg_impl->res.va + arg_buffer_offset;
        }

        d3d12_command_list_end_transfer_batch(list);
        switch (arg_desc->Type)
        {
            case D3D12_INDIRECT_ARGUMENT_TYPE_DRAW:
                if (!d3d12_command_list_begin_render_pass(list, VKD3D_PIPELINE_TYPE_GRAPHICS))
                {
                    WARN("Failed to begin render pass, ignoring draw.\n");
                    break;
                }

                if (count_buffer || list->predication.fallback_enabled)
                {
                    VK_CALL(vkCmdDrawIndirectCount(list->cmd.vk_command_buffer, arg_impl->res.vk_buffer,
                            arg_buffer_offset + arg_impl->mem.offset, scratch.buffer, scratch.offset,
                            max_command_count, signature_desc->ByteStride));
                }
                else
                {
                    VK_CALL(vkCmdDrawIndirect(list->cmd.vk_command_buffer, arg_impl->res.vk_buffer,
                            arg_buffer_offset + arg_impl->mem.offset, max_command_count, signature_desc->ByteStride));
                }
                break;

            case D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED:
                if (!d3d12_command_list_update_index_buffer(list))
                    break;

                if (!d3d12_command_list_begin_render_pass(list, VKD3D_PIPELINE_TYPE_GRAPHICS))
                {
                    WARN("Failed to begin render pass, ignoring draw.\n");
                    break;
                }

                d3d12_command_list_check_index_buffer_strip_cut_value(list);

                if (count_buffer || list->predication.fallback_enabled)
                {
                    VK_CALL(vkCmdDrawIndexedIndirectCount(list->cmd.vk_command_buffer, arg_impl->res.vk_buffer,
                            arg_buffer_offset + arg_impl->mem.offset, scratch.buffer, scratch.offset,
                            max_command_count, signature_desc->ByteStride));
                }
                else
                {
                    VK_CALL(vkCmdDrawIndexedIndirect(list->cmd.vk_command_buffer, arg_impl->res.vk_buffer,
                            arg_buffer_offset + arg_impl->mem.offset, max_command_count, signature_desc->ByteStride));
                }
                break;

            case D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH_MESH:
                if (!d3d12_command_list_begin_render_pass(list, VKD3D_PIPELINE_TYPE_MESH_GRAPHICS))
                {
                    WARN("Failed to begin render pass, ignoring draw.\n");
                    break;
                }

                if (count_buffer || list->predication.fallback_enabled)
                {
                    VK_CALL(vkCmdDrawMeshTasksIndirectCountEXT(list->cmd.vk_command_buffer, arg_impl->res.vk_buffer,
                            arg_buffer_offset + arg_impl->mem.offset, scratch.buffer, scratch.offset,
                            max_command_count, signature_desc->ByteStride));
                }
                else
                {
                    /* Not very useful to do MDI without state change with mesh shaders, but ...
                     * Has to work. */
                    VK_CALL(vkCmdDrawMeshTasksIndirectEXT(list->cmd.vk_command_buffer,
                            scratch.buffer, scratch.offset,
                            max_command_count, signature_desc->ByteStride));
                }
                break;

            case D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH:
                if (!d3d12_command_list_update_compute_state(list))
                {
                    WARN("Failed to update compute state, ignoring dispatch.\n");
                    break;
                }

                /* Without state changes, we can always just unroll the dispatches.
                 * Not the most useful feature ever, but it has to work. */
                for (i = 0; i < max_command_count; i++)
                {
                    VK_CALL(vkCmdDispatchIndirect(list->cmd.vk_command_buffer, scratch.buffer, scratch.offset));
                    VKD3D_BREADCRUMB_AUX32(i);
                    VKD3D_BREADCRUMB_COMMAND(EXECUTE_INDIRECT_UNROLL_COMPUTE);
                    scratch.offset += unrolled_stride;
                }
                break;

            case D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH_RAYS:
                if (max_command_count != 1)
                    FIXME("Ignoring command count %u.\n", max_command_count);

                if (count_buffer)
                {
                    FIXME_ONCE("Count buffers not supported for indirect ray dispatch.\n");
                    break;
                }

                if (!d3d12_command_list_update_raygen_state(list))
                {
                    WARN("Failed to update raygen state, ignoring ray dispatch.\n");
                    break;
                }

                if (!list->device->device_info.ray_tracing_maintenance1_features.rayTracingPipelineTraceRaysIndirect2)
                {
                    WARN("TraceRaysIndirect2 is not supported, ignoring ray dispatch.\n");
                    break;
                }

                VK_CALL(vkCmdTraceRaysIndirect2KHR(list->cmd.vk_command_buffer, scratch.va));
                break;

            default:
                FIXME("Ignoring unhandled argument type %#x.\n", arg_desc->Type);
                break;
        }
    }

    VKD3D_BREADCRUMB_COMMAND(EXECUTE_INDIRECT);

    if (list->state && list->state->pipeline_type == VKD3D_PIPELINE_TYPE_COMPUTE)
        d3d12_command_list_check_compute_barrier(list);
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

    if (dyn_state->min_depth_bounds != min || dyn_state->max_depth_bounds != max)
    {
        dyn_state->min_depth_bounds = min;
        dyn_state->max_depth_bounds = max;
        dyn_state->dirty_flags |= VKD3D_DYNAMIC_STATE_DEPTH_BOUNDS;
    }
}

static void STDMETHODCALLTYPE d3d12_command_list_SetSamplePositions(d3d12_command_list_iface *iface,
        UINT sample_count, UINT pixel_count, D3D12_SAMPLE_POSITION *sample_positions)
{
    FIXME("iface %p, sample_count %u, pixel_count %u, sample_positions %p stub!\n",
            iface, sample_count, pixel_count, sample_positions);
}

static bool d3d12_command_list_validate_sampler_feedback_transcode(
        struct d3d12_resource *transcoded, struct d3d12_resource *feedback)
{
    if (!d3d12_resource_desc_is_sampler_feedback(&feedback->desc))
    {
        WARN("Must decode SAMPLER_FEEDBACK formats.\n");
        return false;
    }

    if (feedback->desc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D)
    {
        WARN("Source must be 2D texture.\n");
        return false;
    }

    if (transcoded->desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER)
    {
        /* API restriction */
        if (feedback->desc.Format != DXGI_FORMAT_SAMPLER_FEEDBACK_MIN_MIP_OPAQUE ||
                feedback->desc.DepthOrArraySize != 1)
        {
            WARN("Can only decode single-layer MIN_MIP to BUFFER.\n");
            return false;
        }
    }
    else if (transcoded->desc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D)
    {
        WARN("Can only decode TEXTURE2D.\n");
        return false;
    }
    else
    {
        /* Creation dimensions must match if we're decoding texture <-> texture, even if we're just
         * resolving a single subresource. */
        if (transcoded->desc.DepthOrArraySize != feedback->desc.DepthOrArraySize)
        {
            WARN("ArraySize must match.\n");
            return false;
        }

        if (feedback->desc.Format == DXGI_FORMAT_SAMPLER_FEEDBACK_MIN_MIP_OPAQUE &&
                transcoded->desc.MipLevels != 1)
        {
            WARN("For MIN_MIP_OPAQUE, transcoded resource must be single mip.\n");
            return false;
        }
        else if (feedback->desc.Format == DXGI_FORMAT_SAMPLER_FEEDBACK_MIP_REGION_USED_OPAQUE &&
                transcoded->desc.MipLevels != feedback->desc.MipLevels)
        {
            WARN("For MIP_REGION_USED, transcoded resource must match mip levels of feedback.\n");
            return false;
        }
    }

    return true;
}

static void d3d12_command_list_encode_sampler_feedback(struct d3d12_command_list *list,
        struct d3d12_resource *dst, UINT dst_subresource_index, UINT dst_x, UINT dst_y,
        struct d3d12_resource *src, UINT src_subresource_index, const D3D12_RECT *src_rect)
{
    const struct vkd3d_vk_device_procs *vk_procs = &list->device->vk_procs;
    struct vkd3d_texture_view_desc src_image_view_desc, dst_view_desc;
    struct vkd3d_sampler_feedback_resolve_info pipeline_info;
    struct vkd3d_sampler_feedback_resolve_encode_args args;
    struct vkd3d_view *dst_view = NULL, *src_view = NULL;
    struct vkd3d_buffer_view_desc src_buffer_view_desc;
    unsigned int transcoded_width, transcoded_height;
    VkWriteDescriptorSet vk_descriptor_writes[2];
    VkDescriptorImageInfo vk_image_info[2];
    VkMemoryBarrier2 vk_memory_barrier;
    unsigned int num_mip_iterations;
    VkImageSubresource subresource;
    VkDependencyInfo dep_info;
    VkExtent3D extent;
    unsigned int i;

    if (!d3d12_command_list_validate_sampler_feedback_transcode(src, dst))
        return;

    d3d12_command_list_invalidate_current_pipeline(list, true);
    d3d12_command_list_update_descriptor_buffers(list);
    d3d12_command_list_invalidate_root_parameters(list, &list->compute_bindings, true, &list->graphics_bindings);
    d3d12_command_list_debug_mark_begin_region(list, "SamplerFeedbackEncode");

    /* Fixup subresource indices. */
    if (dst->desc.Format == DXGI_FORMAT_SAMPLER_FEEDBACK_MIN_MIP_OPAQUE)
    {
        /* src index controls what we're doing. dst_subresource_index should always be UINT_MAX according to docs. */
        dst_subresource_index = src_subresource_index;
    }
    else
    {
        /* Having mismatched all / concrete subresources breaks native drivers. Fix it up to not blow up.
         * Otherwise, it is permissible to mismatch in MIP_USED mode. */
        if (src_subresource_index != UINT32_MAX && dst_subresource_index == UINT32_MAX)
            dst_subresource_index = src_subresource_index;
        else if (dst_subresource_index != UINT32_MAX && src_subresource_index == UINT32_MAX)
            src_subresource_index = dst_subresource_index;
    }

    memset(&dst_view_desc, 0, sizeof(dst_view_desc));
    dst_view_desc.image = dst->res.vk_image;
    dst_view_desc.format = vkd3d_get_format(list->device, DXGI_FORMAT_R32G32_UINT, false);
    dst_view_desc.view_type = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    dst_view_desc.aspect_mask = VK_IMAGE_ASPECT_COLOR_BIT;
    dst_view_desc.image_usage = VK_IMAGE_USAGE_STORAGE_BIT;
    dst_view_desc.layer_idx = 0;
    dst_view_desc.layer_count = dst->desc.DepthOrArraySize;
    dst_view_desc.miplevel_idx = 0;
    dst_view_desc.miplevel_count = 1;

    memset(&args, 0, sizeof(args));

    /* Fill in image view later. */
    vk_image_info[0].imageLayout = d3d12_resource_pick_layout(src, VK_IMAGE_LAYOUT_GENERAL);
    vk_image_info[0].sampler = VK_NULL_HANDLE;

    memset(vk_descriptor_writes, 0, sizeof(vk_descriptor_writes));

    vk_descriptor_writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    vk_descriptor_writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    vk_descriptor_writes[0].dstBinding = 0;
    vk_descriptor_writes[0].pImageInfo = &vk_image_info[0];
    vk_descriptor_writes[0].descriptorCount = 1;

    /* We're doing RMW, so have to ensure ordering. Also, need to ensure ordering
     * for back-to-back RESOLVE_DEST operations. */

    memset(&vk_memory_barrier, 0, sizeof(vk_memory_barrier));
    vk_memory_barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
    vk_memory_barrier.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    vk_memory_barrier.srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
    vk_memory_barrier.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    vk_memory_barrier.dstAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT | VK_ACCESS_2_SHADER_READ_BIT;

    memset(&dep_info, 0, sizeof(dep_info));
    dep_info.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dep_info.memoryBarrierCount = 1;
    dep_info.pMemoryBarriers = &vk_memory_barrier;

    if (src->desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER)
    {
        vkd3d_meta_get_sampler_feedback_resolve_pipeline(&list->device->meta_ops,
                VKD3D_SAMPLER_FEEDBACK_RESOLVE_BUFFER_TO_MIN_MIP, &pipeline_info);

        dst_view_desc.view_type = VK_IMAGE_VIEW_TYPE_2D_ARRAY;

        extent = d3d12_resource_desc_get_active_feedback_extent(&dst->desc, 0);

        /* These make little to no sense for buffers ... Docs don't say anything about it.
         * They are ignored on some drivers, and treated as image offsets on AMD ... :< */
        if (dst_x || dst_y)
            WARN("Ignoring dst_x and dst_y.\n");

        src_buffer_view_desc.format = vkd3d_get_format(list->device, DXGI_FORMAT_R8_UINT, false);
        src_buffer_view_desc.size = extent.width * extent.height;
        src_buffer_view_desc.offset = src->mem.offset;
        src_buffer_view_desc.buffer = src->res.vk_buffer;

        if (!vkd3d_create_buffer_view(list->device, &src_buffer_view_desc, &src_view))
            goto cleanup;
        if (!vkd3d_create_texture_view(list->device, &dst_view_desc, &dst_view))
            goto cleanup;

        vk_image_info[0].imageView = dst_view->vk_image_view;

        vk_descriptor_writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        vk_descriptor_writes[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
        vk_descriptor_writes[1].dstBinding = 2;
        vk_descriptor_writes[1].pTexelBufferView = &src_view->vk_buffer_view;
        vk_descriptor_writes[1].descriptorCount = 1;

        /* MinMip does not support rect semantics, so go ahead. */

        args.resolve_width = extent.width;
        args.resolve_height = extent.height;

        VK_CALL(vkCmdBindPipeline(list->cmd.vk_command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                pipeline_info.vk_pipeline));
        VK_CALL(vkCmdPushDescriptorSetKHR(list->cmd.vk_command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                pipeline_info.vk_layout, 0, ARRAY_SIZE(vk_descriptor_writes), vk_descriptor_writes));
        VK_CALL(vkCmdPushConstants(list->cmd.vk_command_buffer, pipeline_info.vk_layout,
                VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(args), &args));

        extent.width = vkd3d_compute_workgroup_count(extent.width,
                vkd3d_meta_get_sampler_feedback_workgroup_size().width);
        extent.height = vkd3d_compute_workgroup_count(extent.height,
                vkd3d_meta_get_sampler_feedback_workgroup_size().height);
        extent.depth = vkd3d_compute_workgroup_count(extent.depth,
                vkd3d_meta_get_sampler_feedback_workgroup_size().depth);
        VK_CALL(vkCmdDispatch(list->cmd.vk_command_buffer, extent.width, extent.height, extent.depth));

        VK_CALL(vkCmdPipelineBarrier2(list->cmd.vk_command_buffer, &dep_info));
    }
    else
    {
        vkd3d_meta_get_sampler_feedback_resolve_pipeline(&list->device->meta_ops,
                dst->desc.Format == DXGI_FORMAT_SAMPLER_FEEDBACK_MIN_MIP_OPAQUE ?
                        VKD3D_SAMPLER_FEEDBACK_RESOLVE_IMAGE_TO_MIN_MIP :
                        VKD3D_SAMPLER_FEEDBACK_RESOLVE_IMAGE_TO_MIP_USED, &pipeline_info);

        memset(&src_image_view_desc, 0, sizeof(src_image_view_desc));
        src_image_view_desc.image = src->res.vk_image;
        src_image_view_desc.format = vkd3d_get_format(list->device, DXGI_FORMAT_R8_UINT, false);
        src_image_view_desc.aspect_mask = VK_IMAGE_ASPECT_COLOR_BIT;
        src_image_view_desc.image_usage = VK_IMAGE_USAGE_SAMPLED_BIT;
        src_image_view_desc.layer_idx = 0;
        src_image_view_desc.layer_count = src->desc.DepthOrArraySize;
        src_image_view_desc.miplevel_count = src->desc.MipLevels;
        src_image_view_desc.view_type = VK_IMAGE_VIEW_TYPE_2D_ARRAY;

        if (src_subresource_index == UINT32_MAX)
        {
            /* We will resolve all layers + mips in one go. */
            if (dst->desc.Format == DXGI_FORMAT_SAMPLER_FEEDBACK_MIN_MIP_OPAQUE)
                num_mip_iterations = 1;
            else
                num_mip_iterations = src->desc.MipLevels;
        }
        else
        {
            num_mip_iterations = 1;
            if (dst->desc.Format == DXGI_FORMAT_SAMPLER_FEEDBACK_MIN_MIP_OPAQUE)
            {
                src_image_view_desc.layer_idx = src_subresource_index;
                src_image_view_desc.layer_count = 1;
                dst_view_desc.layer_idx = src_subresource_index;
                dst_view_desc.layer_count = 1;
            }
            else
            {
                /* Concrete mip level. */
                subresource = vk_image_subresource_from_d3d12(src_image_view_desc.format,
                        src_subresource_index, src->desc.MipLevels, src->desc.DepthOrArraySize, false);

                src_image_view_desc.layer_idx = subresource.arrayLayer;
                src_image_view_desc.miplevel_idx = subresource.mipLevel;
                src_image_view_desc.layer_count = 1;
                src_image_view_desc.miplevel_count = 1;
                /* src_mip is implied by the view itself. */

                subresource = vk_image_subresource_from_d3d12(dst_view_desc.format,
                        dst_subresource_index, dst->desc.MipLevels, dst->desc.DepthOrArraySize, false);
                dst_view_desc.layer_idx = subresource.arrayLayer;
                dst_view_desc.layer_count = 1;
                args.dst_mip = subresource.mipLevel;
            }
        }

        if (!vkd3d_create_texture_view(list->device, &src_image_view_desc, &src_view))
            goto cleanup;
        if (!vkd3d_create_texture_view(list->device, &dst_view_desc, &dst_view))
            goto cleanup;

        vk_image_info[0].imageView = dst_view->vk_image_view;
        vk_image_info[1].imageView = src_view->vk_image_view;
        vk_image_info[1].sampler = VK_NULL_HANDLE;
        vk_image_info[1].imageLayout = d3d12_resource_pick_layout(src, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

        vk_descriptor_writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        vk_descriptor_writes[1].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        vk_descriptor_writes[1].dstBinding = 1;
        vk_descriptor_writes[1].pImageInfo = &vk_image_info[1];
        vk_descriptor_writes[1].descriptorCount = 1;

        if (dst->desc.Format == DXGI_FORMAT_SAMPLER_FEEDBACK_MIP_REGION_USED_OPAQUE)
        {
            args.dst_x = dst_x;
            args.dst_y = dst_y;
        }
        else if (dst_x || dst_y)
        {
            /* These are ignored on all drivers for MIN_MIP. */
            WARN("Ignoring dst_x and dst_y.\n");
            dst_x = 0;
            dst_y = 0;
        }

        for (i = 0; i < num_mip_iterations; i++)
        {
            extent = d3d12_resource_desc_get_active_feedback_extent(&dst->desc, args.dst_mip);
            transcoded_width = extent.width;
            transcoded_height = extent.height;
            if (dst_x >= transcoded_width || dst_y >= transcoded_height)
            {
                WARN("Trying to encode out of bounds.\n");
                goto cleanup;
            }
            transcoded_width -= dst_x;
            transcoded_height -= dst_y;

            /* Transcoded output doesn't have to cover everything. Cover minimum. */
            extent = d3d12_resource_desc_get_subresource_extent(&src->desc, src->format, src_image_view_desc.miplevel_idx);
            transcoded_width = min(transcoded_width, extent.width);
            transcoded_height = min(transcoded_height, extent.height);

            /* Source rect is not allowed for MIN_MIP mode. */
            if (dst->desc.Format == DXGI_FORMAT_SAMPLER_FEEDBACK_MIP_REGION_USED_OPAQUE && src_rect)
            {
                transcoded_width = min((int)transcoded_width, src_rect->right - src_rect->left);
                transcoded_height = min((int)transcoded_height, src_rect->bottom - src_rect->top);
                args.src_x = src_rect->left;
                args.src_y = src_rect->top;
            }

            args.resolve_width = transcoded_width;
            args.resolve_height = transcoded_height;

            VK_CALL(vkCmdBindPipeline(list->cmd.vk_command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                    pipeline_info.vk_pipeline));
            VK_CALL(vkCmdPushDescriptorSetKHR(list->cmd.vk_command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                    pipeline_info.vk_layout, 0, ARRAY_SIZE(vk_descriptor_writes), vk_descriptor_writes));
            VK_CALL(vkCmdPushConstants(list->cmd.vk_command_buffer, pipeline_info.vk_layout,
                    VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(args), &args));

            extent.width = vkd3d_compute_workgroup_count(args.resolve_width,
                    vkd3d_meta_get_sampler_feedback_workgroup_size().width);
            extent.height = vkd3d_compute_workgroup_count(args.resolve_height,
                    vkd3d_meta_get_sampler_feedback_workgroup_size().height);
            extent.depth = dst_view_desc.layer_count;
            VK_CALL(vkCmdDispatch(list->cmd.vk_command_buffer, extent.width, extent.height, extent.depth));

            VK_CALL(vkCmdPipelineBarrier2(list->cmd.vk_command_buffer, &dep_info));

            args.src_mip++;
            args.dst_mip++;
            src_image_view_desc.miplevel_idx++;
        }
    }

    d3d12_command_allocator_add_view(list->allocator, src_view);
    d3d12_command_allocator_add_view(list->allocator, dst_view);

    /* Resolve does not count as a placed initialization,
     * so don't try to be clever here and compute writes_full_subresource.
     * It gets quite difficult to reason about since the dst subresource can be padded out. */
    d3d12_command_list_track_resource_usage(list, src, true);
    d3d12_command_list_track_resource_usage(list, dst, true);

cleanup:
    d3d12_command_list_debug_mark_end_region(list);
    if (dst_view)
        vkd3d_view_decref(dst_view, list->device);
    if (src_view)
        vkd3d_view_decref(src_view, list->device);
}

static void d3d12_command_list_decode_sampler_feedback(struct d3d12_command_list *list,
        struct d3d12_resource *dst, UINT dst_subresource_index, UINT dst_x, UINT dst_y,
        struct d3d12_resource *src, UINT src_subresource_index, const D3D12_RECT *src_rect)
{
    const struct vkd3d_vk_device_procs *vk_procs = &list->device->vk_procs;
    struct vkd3d_texture_view_desc dst_image_view_desc, src_view_desc;
    struct vkd3d_sampler_feedback_resolve_info pipeline_info;
    struct vkd3d_sampler_feedback_resolve_decode_args args;
    struct vkd3d_view *dst_view = NULL, *src_view = NULL;
    struct vkd3d_buffer_view_desc dst_buffer_view_desc;
    unsigned int transcoded_width, transcoded_height;
    VkWriteDescriptorSet vk_descriptor_writes[2];
    VkRenderingAttachmentInfo attachment_info;
    VkImageMemoryBarrier2 vk_image_barrier;
    VkDescriptorImageInfo vk_image_info;
    VkMemoryBarrier2 vk_memory_barrier;
    unsigned int num_mip_iterations;
    VkImageSubresource subresource;
    VkRenderingInfo rendering_info;
    VkDependencyInfo dep_info;
    VkViewport viewport;
    VkExtent3D extent;
    unsigned int i;

    /* ResolveSubresourceRegion must run in DIRECT queue, so we can rely on COLOR_ATTACHMENT.
     * This avoids us having to add UAV usage to every R8_UINT image, we already add COLOR_ATTACHMENT
     * due to STENCIL -> COLOR copy. */

    if (!d3d12_command_list_validate_sampler_feedback_transcode(dst, src))
        return;

    d3d12_command_list_invalidate_current_pipeline(list, true);
    d3d12_command_list_update_descriptor_buffers(list);
    d3d12_command_list_debug_mark_begin_region(list, "SamplerFeedbackDecode");
    if (dst->desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER)
        d3d12_command_list_invalidate_root_parameters(list, &list->compute_bindings, true, &list->graphics_bindings);
    else
        d3d12_command_list_invalidate_root_parameters(list, &list->graphics_bindings, true, &list->compute_bindings);

    /* Fixup subresource indices. */
    if (src->desc.Format == DXGI_FORMAT_SAMPLER_FEEDBACK_MIN_MIP_OPAQUE)
    {
        /* dst index controls what we're doing. src_subresource_index should always be UINT_MAX according to docs. */
        src_subresource_index = dst_subresource_index;
    }
    else
    {
        /* Having mismatched all / concrete subresources breaks native drivers. Fix it up to not blow up.
         * Otherwise, it is permissible to mismatch in MIP_USED mode. */
        if (src_subresource_index != UINT32_MAX && dst_subresource_index == UINT32_MAX)
            dst_subresource_index = src_subresource_index;
        else if (dst_subresource_index != UINT32_MAX && src_subresource_index == UINT32_MAX)
            src_subresource_index = dst_subresource_index;
    }

    memset(&src_view_desc, 0, sizeof(src_view_desc));
    src_view_desc.image = src->res.vk_image;
    src_view_desc.format = vkd3d_get_format(list->device, DXGI_FORMAT_R32G32_UINT, false);
    src_view_desc.view_type = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    src_view_desc.aspect_mask = VK_IMAGE_ASPECT_COLOR_BIT;
    src_view_desc.image_usage = VK_IMAGE_USAGE_SAMPLED_BIT;
    src_view_desc.layer_idx = 0;
    src_view_desc.layer_count = src->desc.DepthOrArraySize;
    src_view_desc.miplevel_idx = 0;
    src_view_desc.miplevel_count = 1;

    memset(&args, 0, sizeof(args));
    args.num_mip_levels = src->desc.MipLevels;
    args.inv_paired_width = 1.0f / src->desc.Width;
    args.inv_paired_height = 1.0f / src->desc.Height;
    args.paired_width = src->desc.Width;
    args.paired_height = src->desc.Height;
    extent = d3d12_resource_desc_get_padded_feedback_extent(&src->desc);
    args.inv_feedback_width = 1.0f / extent.width;
    args.inv_feedback_height = 1.0f / extent.height;

    /* Fill in image view later. */
    vk_image_info.imageLayout = d3d12_resource_pick_layout(src, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    vk_image_info.sampler = VK_NULL_HANDLE;

    memset(vk_descriptor_writes, 0, sizeof(vk_descriptor_writes));

    vk_descriptor_writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    vk_descriptor_writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    vk_descriptor_writes[0].dstBinding = 1;
    vk_descriptor_writes[0].pImageInfo = &vk_image_info;
    vk_descriptor_writes[0].descriptorCount = 1;

    if (dst->desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER)
    {
        vkd3d_meta_get_sampler_feedback_resolve_pipeline(&list->device->meta_ops,
                VKD3D_SAMPLER_FEEDBACK_RESOLVE_MIN_MIP_TO_BUFFER, &pipeline_info);

        src_view_desc.view_type = VK_IMAGE_VIEW_TYPE_2D;

        extent = d3d12_resource_desc_get_active_feedback_extent(&src->desc, 0);

        /* These make little to no sense for buffers ... Docs don't say anything about it.
         * They are ignored on some drivers. */
        if (dst_x || dst_y)
            WARN("Ignoring dst_x and dst_y.\n");

        dst_buffer_view_desc.format = vkd3d_get_format(list->device, DXGI_FORMAT_R8_UINT, false);
        dst_buffer_view_desc.size = extent.width * extent.height;
        dst_buffer_view_desc.offset = dst->mem.offset;
        dst_buffer_view_desc.buffer = dst->res.vk_buffer;

        if (!vkd3d_create_texture_view(list->device, &src_view_desc, &src_view))
            goto cleanup;
        if (!vkd3d_create_buffer_view(list->device, &dst_buffer_view_desc, &dst_view))
            goto cleanup;

        vk_image_info.imageView = src_view->vk_image_view;

        vk_descriptor_writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        vk_descriptor_writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER;
        vk_descriptor_writes[1].dstBinding = 0;
        vk_descriptor_writes[1].pTexelBufferView = &dst_view->vk_buffer_view;
        vk_descriptor_writes[1].descriptorCount = 1;

        args.resolve_width = extent.width;
        args.resolve_height = extent.height;

        /* MinMip does not support rect semantics, so go ahead. */

        /* We're in RESOLVE_DEST state here, which means we need extra barrier to get us to COMPUTE.
         * Also, propagate SHADER_READ state since we're reading RESOLVE_SOURCE. */
        memset(&vk_memory_barrier, 0, sizeof(vk_memory_barrier));
        vk_memory_barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
        vk_memory_barrier.srcStageMask = VK_PIPELINE_STAGE_2_RESOLVE_BIT;
        vk_memory_barrier.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        vk_memory_barrier.dstAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;

        memset(&dep_info, 0, sizeof(dep_info));
        dep_info.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        dep_info.memoryBarrierCount = 1;
        dep_info.pMemoryBarriers = &vk_memory_barrier;
        VK_CALL(vkCmdPipelineBarrier2(list->cmd.vk_command_buffer, &dep_info));

        VK_CALL(vkCmdBindPipeline(list->cmd.vk_command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                pipeline_info.vk_pipeline));
        VK_CALL(vkCmdPushDescriptorSetKHR(list->cmd.vk_command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                pipeline_info.vk_layout, 0, ARRAY_SIZE(vk_descriptor_writes), vk_descriptor_writes));
        VK_CALL(vkCmdPushConstants(list->cmd.vk_command_buffer, pipeline_info.vk_layout,
                VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(args), &args));

        extent.width = vkd3d_compute_workgroup_count(extent.width,
                vkd3d_meta_get_sampler_feedback_workgroup_size().width);
        extent.height = vkd3d_compute_workgroup_count(extent.height,
                vkd3d_meta_get_sampler_feedback_workgroup_size().height);
        extent.depth = vkd3d_compute_workgroup_count(extent.depth,
                vkd3d_meta_get_sampler_feedback_workgroup_size().depth);
        VK_CALL(vkCmdDispatch(list->cmd.vk_command_buffer, extent.width, extent.height, extent.depth));

        vk_memory_barrier.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        vk_memory_barrier.srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
        vk_memory_barrier.dstStageMask = VK_PIPELINE_STAGE_2_RESOLVE_BIT;
        vk_memory_barrier.dstAccessMask = VK_ACCESS_2_NONE;
        VK_CALL(vkCmdPipelineBarrier2(list->cmd.vk_command_buffer, &dep_info));

        d3d12_command_allocator_add_view(list->allocator, src_view);
        d3d12_command_allocator_add_view(list->allocator, dst_view);
    }
    else
    {
        vkd3d_meta_get_sampler_feedback_resolve_pipeline(&list->device->meta_ops,
                src->desc.Format == DXGI_FORMAT_SAMPLER_FEEDBACK_MIN_MIP_OPAQUE ?
                        VKD3D_SAMPLER_FEEDBACK_RESOLVE_MIN_MIP_TO_IMAGE :
                        VKD3D_SAMPLER_FEEDBACK_RESOLVE_MIP_USED_TO_IMAGE, &pipeline_info);

        memset(&dst_image_view_desc, 0, sizeof(dst_image_view_desc));
        dst_image_view_desc.image = dst->res.vk_image;
        dst_image_view_desc.format = vkd3d_get_format(list->device, DXGI_FORMAT_R8_UINT, false);
        dst_image_view_desc.aspect_mask = VK_IMAGE_ASPECT_COLOR_BIT;
        dst_image_view_desc.image_usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        dst_image_view_desc.layer_idx = 0;
        dst_image_view_desc.layer_count = src->desc.DepthOrArraySize;
        dst_image_view_desc.miplevel_count = 1;
        dst_image_view_desc.view_type = VK_IMAGE_VIEW_TYPE_2D_ARRAY;

        memset(&vk_image_barrier, 0, sizeof(vk_image_barrier));
        vk_image_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        vk_image_barrier.image = dst->res.vk_image;
        vk_image_barrier.oldLayout = dst->common_layout;
        vk_image_barrier.newLayout = d3d12_resource_pick_layout(dst, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        vk_image_barrier.srcStageMask = VK_PIPELINE_STAGE_2_RESOLVE_BIT;
        vk_image_barrier.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        vk_image_barrier.srcAccessMask = 0;
        vk_image_barrier.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
        vk_image_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        vk_image_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        vk_image_barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;

        if (dst_subresource_index == UINT32_MAX)
        {
            /* We will resolve all layers + mips in one go. */
            if (src->desc.Format == DXGI_FORMAT_SAMPLER_FEEDBACK_MIN_MIP_OPAQUE)
                num_mip_iterations = 1;
            else
                num_mip_iterations = src->desc.MipLevels;

            vk_image_barrier.subresourceRange.layerCount = VK_REMAINING_ARRAY_LAYERS;
            vk_image_barrier.subresourceRange.levelCount = VK_REMAINING_MIP_LEVELS;
            args.mip_level = 0;
        }
        else
        {
            num_mip_iterations = 1;
            if (src->desc.Format == DXGI_FORMAT_SAMPLER_FEEDBACK_MIN_MIP_OPAQUE)
            {
                dst_image_view_desc.layer_idx = dst_subresource_index;
                dst_image_view_desc.layer_count = 1;
                src_view_desc.layer_idx = src_subresource_index;
                src_view_desc.layer_count = 1;
                args.mip_level = 0; /* We iterate over all mips in one pass. */
            }
            else
            {
                /* Concrete mip level. They can mismatch. */
                subresource = vk_image_subresource_from_d3d12(dst_image_view_desc.format,
                        dst_subresource_index, dst->desc.MipLevels, dst->desc.DepthOrArraySize, false);
                dst_image_view_desc.layer_idx = subresource.arrayLayer;
                dst_image_view_desc.miplevel_idx = subresource.mipLevel;
                dst_image_view_desc.layer_count = 1;
                dst_image_view_desc.miplevel_count = 1;

                subresource = vk_image_subresource_from_d3d12(src_view_desc.format,
                        src_subresource_index, src->desc.MipLevels, src->desc.DepthOrArraySize, false);
                src_view_desc.layer_idx = subresource.arrayLayer;
                src_view_desc.layer_count = 1;
                /* The feedback image is not actually mip-mapped. */
                args.mip_level = subresource.mipLevel;
            }

            vk_image_barrier.subresourceRange.baseArrayLayer = dst_image_view_desc.layer_idx;
            vk_image_barrier.subresourceRange.baseMipLevel = dst_image_view_desc.miplevel_idx;
            vk_image_barrier.subresourceRange.layerCount = 1;
            vk_image_barrier.subresourceRange.levelCount = 1;
        }

        memset(&dep_info, 0, sizeof(dep_info));
        dep_info.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        dep_info.imageMemoryBarrierCount = 1;
        dep_info.pImageMemoryBarriers = &vk_image_barrier;
        VK_CALL(vkCmdPipelineBarrier2(list->cmd.vk_command_buffer, &dep_info));

        if (src->desc.Format == DXGI_FORMAT_SAMPLER_FEEDBACK_MIP_REGION_USED_OPAQUE)
        {
            args.dst_x = dst_x;
            args.dst_y = dst_y;
        }
        else if (dst_x || dst_y)
        {
            /* These are ignored on all drivers for MIN_MIP. */
            WARN("Ignoring dst_x and dst_y.\n");
            dst_x = 0;
            dst_y = 0;
        }

        memset(&rendering_info, 0, sizeof(rendering_info));
        rendering_info.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        rendering_info.renderArea.offset.x = dst_x;
        rendering_info.renderArea.offset.y = dst_y;
        rendering_info.colorAttachmentCount = 1;
        rendering_info.pColorAttachments = &attachment_info;
        rendering_info.layerCount = dst_image_view_desc.layer_count;

        memset(&attachment_info, 0, sizeof(attachment_info));
        attachment_info.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        attachment_info.imageLayout = vk_image_barrier.newLayout;
        attachment_info.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachment_info.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

        viewport.x = (float)rendering_info.renderArea.offset.x;
        viewport.y = (float)rendering_info.renderArea.offset.y;
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;

        for (i = 0; i < num_mip_iterations; i++)
        {
            if (dst_view)
            {
                vkd3d_view_decref(dst_view, list->device);
                dst_view = NULL;
            }

            if (src_view)
            {
                vkd3d_view_decref(src_view, list->device);
                src_view = NULL;
            }

            if (!vkd3d_create_texture_view(list->device, &src_view_desc, &src_view))
                goto cleanup;
            if (!vkd3d_create_texture_view(list->device, &dst_image_view_desc, &dst_view))
                goto cleanup;

            /* Transcoded output doesn't have to cover everything. Cover minimum. */
            extent = d3d12_resource_desc_get_subresource_extent(&dst->desc, dst->format, dst_image_view_desc.miplevel_idx);
            transcoded_width = extent.width;
            transcoded_height = extent.height;
            if (dst_x >= transcoded_width || dst_y >= transcoded_height)
            {
                WARN("Trying to decode out of bounds.\n");
                goto cleanup;
            }
            transcoded_width -= dst_x;
            transcoded_height -= dst_y;

            extent = d3d12_resource_desc_get_active_feedback_extent(&src->desc, args.mip_level);
            transcoded_width = min(transcoded_width, extent.width);
            transcoded_height = min(transcoded_height, extent.height);

            /* Source rect is not allowed for MIN_MIP mode. It is actively ignored. */
            if (src->desc.Format == DXGI_FORMAT_SAMPLER_FEEDBACK_MIP_REGION_USED_OPAQUE && src_rect)
            {
                transcoded_width = min((int)transcoded_width, src_rect->right - src_rect->left);
                transcoded_height = min((int)transcoded_height, src_rect->bottom - src_rect->top);
                args.src_x = src_rect->left;
                args.src_y = src_rect->top;
            }

            args.resolve_width = transcoded_width;
            args.resolve_height = transcoded_height;

            attachment_info.imageView = dst_view->vk_image_view;
            rendering_info.renderArea.extent.width = transcoded_width;
            rendering_info.renderArea.extent.height = transcoded_height;

            viewport.width = (float)rendering_info.renderArea.extent.width;
            viewport.height = (float)rendering_info.renderArea.extent.height;

            vk_image_info.imageView = src_view->vk_image_view;

            VK_CALL(vkCmdBeginRendering(list->cmd.vk_command_buffer, &rendering_info));
            VK_CALL(vkCmdBindPipeline(list->cmd.vk_command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                    pipeline_info.vk_pipeline));
            VK_CALL(vkCmdSetViewport(list->cmd.vk_command_buffer, 0, 1, &viewport));
            VK_CALL(vkCmdSetScissor(list->cmd.vk_command_buffer, 0, 1, &rendering_info.renderArea));
            VK_CALL(vkCmdPushDescriptorSetKHR(list->cmd.vk_command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                    pipeline_info.vk_layout, 0, 1, vk_descriptor_writes));
            VK_CALL(vkCmdPushConstants(list->cmd.vk_command_buffer, pipeline_info.vk_layout,
                    VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(args), &args));
            VK_CALL(vkCmdDraw(list->cmd.vk_command_buffer, 3, dst_image_view_desc.layer_count, 0, 0));
            VK_CALL(vkCmdEndRendering(list->cmd.vk_command_buffer));

            dst_image_view_desc.miplevel_idx++;
            args.mip_level++;
            d3d12_command_allocator_add_view(list->allocator, src_view);
            d3d12_command_allocator_add_view(list->allocator, dst_view);
        }

        vk_image_barrier.oldLayout = d3d12_resource_pick_layout(dst, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        vk_image_barrier.newLayout = dst->common_layout;
        vk_image_barrier.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        vk_image_barrier.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
        vk_image_barrier.dstStageMask = VK_PIPELINE_STAGE_2_RESOLVE_BIT;
        vk_image_barrier.dstAccessMask = 0;
        VK_CALL(vkCmdPipelineBarrier2(list->cmd.vk_command_buffer, &dep_info));
    }

    /* Resolve does not count as a placed initialization,
     * so don't try to be clever here and compute writes_full_subresource.
     * It gets quite difficult to reason about since the dst subresource can be padded out. */
    d3d12_command_list_track_resource_usage(list, src, true);
    d3d12_command_list_track_resource_usage(list, dst, true);

cleanup:
    d3d12_command_list_debug_mark_end_region(list);
    if (dst_view)
        vkd3d_view_decref(dst_view, list->device);
    if (src_view)
        vkd3d_view_decref(src_view, list->device);
}

static void STDMETHODCALLTYPE d3d12_command_list_ResolveSubresourceRegion(d3d12_command_list_iface *iface,
        ID3D12Resource *dst, UINT dst_sub_resource_idx, UINT dst_x, UINT dst_y,
        ID3D12Resource *src, UINT src_sub_resource_idx,
        D3D12_RECT *src_rect, DXGI_FORMAT format, D3D12_RESOLVE_MODE mode)
{
    struct d3d12_command_list *list = impl_from_ID3D12GraphicsCommandList(iface);
    struct d3d12_resource *dst_resource, *src_resource;
    struct d3d12_command_list_barrier_batch barriers;
    VkImageSubresourceLayers src_subresource;
    VkImageSubresourceLayers dst_subresource;
    VkOffset3D src_offset;
    VkOffset3D dst_offset;
    VkExtent3D extent;

    TRACE("iface %p, dst_resource %p, dst_sub_resource_idx %u, "
            "dst_x %u, dst_y %u, src_resource %p, src_sub_resource_idx %u, "
            "src_rect %p, format %#x, mode %#x!\n",
            iface, dst, dst_sub_resource_idx, dst_x, dst_y,
            src, src_sub_resource_idx, src_rect, format, mode);

    if (list->is_inside_render_pass)
        d3d12_command_list_mark_as_invalid(list, "ResolveSubresourceRegion called within a render pass.\n");

    list->cmd.estimated_cost += VKD3D_COMMAND_COST_LOW;

    dst_resource = impl_from_ID3D12Resource(dst);
    src_resource = impl_from_ID3D12Resource(src);

    /* Sampler feedback resolves are esoteric and need their own code paths. */
    if (mode == D3D12_RESOLVE_MODE_DECODE_SAMPLER_FEEDBACK)
    {
        if (format != DXGI_FORMAT_R8_UINT)
        {
            WARN("Invalid format #%x for DECODE_SAMPLER_FEEDBACK.\n", format);
            return;
        }

        /* For decode, we use the dst index instead to determine how to interpret the subresource index
         * in MIN_MIP mode.
         * This index is then context dependent on the format.
         * -1: Decode all layer/mips.
         * N: For MIN_MIP, decode array layer N.
         * For MIP_USED, decode subresource N (specific mip/layer).
         * Spec is vague if it's allowed to have mismatching subresource IDX, but it works on native drivers. */
        d3d12_command_list_decode_sampler_feedback(list,
                dst_resource, dst_sub_resource_idx, dst_x, dst_y,
                src_resource, src_sub_resource_idx, src_rect);

        return;
    }
    else if (mode == D3D12_RESOLVE_MODE_ENCODE_SAMPLER_FEEDBACK)
    {
        if (format != DXGI_FORMAT_R8_UINT)
        {
            WARN("Invalid format #%x for ENCODE_SAMPLER_FEEDBACK.\n", format);
            return;
        }

        d3d12_command_list_encode_sampler_feedback(list,
                dst_resource, dst_sub_resource_idx, dst_x, dst_y,
                src_resource, src_sub_resource_idx, src_rect);

        return;
    }

    assert(d3d12_resource_is_texture(dst_resource));
    assert(d3d12_resource_is_texture(src_resource));

    src_subresource = vk_image_subresource_layers_from_d3d12(
            src_resource->format, src_sub_resource_idx,
            src_resource->desc.MipLevels,
            d3d12_resource_desc_get_layer_count(&src_resource->desc));
    dst_subresource = vk_image_subresource_layers_from_d3d12(
            dst_resource->format, dst_sub_resource_idx,
            dst_resource->desc.MipLevels,
            d3d12_resource_desc_get_layer_count(&dst_resource->desc));

    if (src_rect)
    {
        src_offset.x = src_rect->left;
        src_offset.y = src_rect->top;
        src_offset.z = 0;
        extent.width = src_rect->right - src_rect->left;
        extent.height = src_rect->bottom - src_rect->top;
        extent.depth = 1;
    }
    else
    {
        memset(&src_offset, 0, sizeof(src_offset));
        extent = d3d12_resource_desc_get_vk_subresource_extent(&src_resource->desc, src_resource->format, &src_subresource);
    }

    dst_offset.x = (int32_t)dst_x;
    dst_offset.y = (int32_t)dst_y;
    dst_offset.z = 0;

    if (mode == D3D12_RESOLVE_MODE_AVERAGE || mode == D3D12_RESOLVE_MODE_MIN || mode == D3D12_RESOLVE_MODE_MAX)
    {
        VkImageResolve2 vk_image_resolve;
        vk_image_resolve.sType = VK_STRUCTURE_TYPE_IMAGE_RESOLVE_2_KHR;
        vk_image_resolve.pNext = NULL;
        vk_image_resolve.srcSubresource = src_subresource;
        vk_image_resolve.dstSubresource = dst_subresource;
        vk_image_resolve.extent = extent;
        vk_image_resolve.srcOffset = src_offset;
        vk_image_resolve.dstOffset = dst_offset;
        d3d12_command_list_resolve_subresource(list, dst_resource, src_resource, &vk_image_resolve, format, mode);
    }
    else if (mode == D3D12_RESOLVE_MODE_DECOMPRESS)
    {
        /* This is a glorified copy path. The region can overlap fully, in which case we have an in-place decompress.
         * Do nothing here. We can copy within a subresource, in which case we enter GENERAL layout.
         * Otherwise, this can always map to vkCmdCopyImage2KHR, except for DEPTH -> COLOR copy.
         * In this case, just use the fallback paths as is. */
        bool writes_full_subresource;
        bool overlapping_subresource;
        bool writes_full_resource;
        VkImageCopy2 image_copy;

        overlapping_subresource = dst_resource == src_resource && dst_sub_resource_idx == src_sub_resource_idx;

        /* In place DECOMPRESS. No-op. */
        if (overlapping_subresource && memcmp(&src_offset, &dst_offset, sizeof(VkOffset3D)) == 0)
            return;

        /* Cannot discard if we're copying in-place. */
        writes_full_subresource = !overlapping_subresource &&
                d3d12_image_copy_writes_full_subresource(dst_resource,
                        &extent, &dst_subresource);

        writes_full_resource = writes_full_subresource && d3d12_resource_get_sub_resource_count(dst_resource) == 1;

        d3d12_command_list_track_resource_usage(list, src_resource, true);
        d3d12_command_list_track_resource_usage(list, dst_resource, !writes_full_resource);

        image_copy.sType = VK_STRUCTURE_TYPE_IMAGE_COPY_2;
        image_copy.pNext = NULL;
        image_copy.srcSubresource = src_subresource;
        image_copy.dstSubresource = dst_subresource;
        image_copy.srcOffset = src_offset;
        image_copy.dstOffset = dst_offset;
        image_copy.extent = extent;

        d3d12_command_list_barrier_batch_init(&barriers);
        d3d12_command_list_copy_image_transition_images(list, &barriers,
            dst_resource, dst_resource->format, src_resource,
            src_resource->format, &image_copy, writes_full_subresource,
            overlapping_subresource);
        d3d12_command_list_barrier_batch_end(list, &barriers);
        d3d12_command_list_barrier_batch_init(&barriers);
        d3d12_command_list_copy_image(list, &barriers,
            dst_resource, dst_resource->format, src_resource,
            src_resource->format, &image_copy, writes_full_subresource,
            overlapping_subresource);
        d3d12_command_list_barrier_batch_end(list, &barriers);
    }
    else
    {
        /* The "weird" resolve modes like sampler feedback encode/decode, etc. */
        FIXME("Unsupported resolve mode: %u.\n", mode);
    }
}

static void STDMETHODCALLTYPE d3d12_command_list_SetViewInstanceMask(d3d12_command_list_iface *iface, UINT mask)
{
    FIXME("iface %p, mask %#x stub!\n", iface, mask);
}

static bool vk_pipeline_stage_from_wbi_mode(D3D12_WRITEBUFFERIMMEDIATE_MODE mode, VkPipelineStageFlagBits *stage)
{
    switch (mode)
    {
        case D3D12_WRITEBUFFERIMMEDIATE_MODE_DEFAULT:
            *stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
            return true;

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
    const struct vkd3d_vk_device_procs *vk_procs = &list->device->vk_procs;
    const struct vkd3d_unique_resource *resource;
    D3D12_WRITEBUFFERIMMEDIATE_MODE mode;
    VkPipelineStageFlagBits stage;
    bool do_flush, do_batch;
    VkDeviceSize offset;
    size_t batch_entry;
    unsigned int i;

    TRACE("iface %p, count %u, parameters %p, modes %p.\n", iface, count, parameters, modes);

    /* Always flush WBI batch if we're outside a render pass instance, since
     * otherwise we're only calling end_wbi_batch in end_current_render_pass. */
    do_flush = !(list->rendering_info.state_flags & VKD3D_RENDERING_ACTIVE);

    for (i = 0; i < count; ++i)
    {
        if (!(resource = vkd3d_va_map_deref(&list->device->memory_allocator.va_map, parameters[i].Dest)))
        {
            d3d12_command_list_mark_as_invalid(list, "Invalid target address %p.\n", parameters[i].Dest);
            return;
        }

        offset = parameters[i].Dest - resource->va;
        mode = modes ? modes[i] : D3D12_WRITEBUFFERIMMEDIATE_MODE_DEFAULT;

        if (!vk_pipeline_stage_from_wbi_mode(mode, &stage))
        {
            d3d12_command_list_mark_as_invalid(list, "Invalid WBI mode %u.\n", mode);
            return;
        }

        /* Avoid ending the active render pass instance, if any */
        do_batch = mode == D3D12_WRITEBUFFERIMMEDIATE_MODE_DEFAULT
                || !list->device->vk_info.AMD_buffer_marker
                || (list->wbi_batch.batch_len && mode != D3D12_WRITEBUFFERIMMEDIATE_MODE_MARKER_IN)
                || (list->query_resolve_count)
                || (list->rtas_batch.build_info_count);

        if (do_batch)
        {
            batch_entry = list->wbi_batch.batch_len++;

            list->wbi_batch.buffers[batch_entry] = resource->vk_buffer;
            list->wbi_batch.offsets[batch_entry] = offset;
            list->wbi_batch.stages[batch_entry] = stage;
            list->wbi_batch.values[batch_entry] = parameters[i].Value;

            if (mode == D3D12_WRITEBUFFERIMMEDIATE_MODE_MARKER_IN)
                do_flush = true;
        }
        else
        {
            VK_CALL(vkCmdWriteBufferMarkerAMD(list->cmd.vk_command_buffer, stage,
                    resource->vk_buffer, offset, parameters[i].Value));
        }

        if ((do_flush && i + 1 == count) || list->wbi_batch.batch_len == VKD3D_MAX_WBI_BATCH_SIZE)
        {
            if (list->rendering_info.state_flags & VKD3D_RENDERING_ACTIVE)
            {
                /* Implicitly calls end_wbi_batch. We cannot have any pending transfers
                 * while inside a render pass instance that we would have to end. */
                d3d12_command_list_end_current_render_pass(list, true);

                /* Flush subsequent batches now that the render pass instance has ended */
                do_flush = true;
            }
            else
            {
                /* Flush pending transfers first to maintain correct order of operations */
                d3d12_command_list_end_transfer_batch(list);
                d3d12_command_list_flush_rtas_batch(list);
                d3d12_command_list_end_wbi_batch(list);
            }
        }
    }

    VKD3D_BREADCRUMB_COMMAND(WBI);
}

static void STDMETHODCALLTYPE d3d12_command_list_SetProtectedResourceSession(d3d12_command_list_iface *iface,
        ID3D12ProtectedResourceSession *protected_session)
{
    FIXME("iface %p, protected_session %p stub!\n", iface, protected_session);
}

static void vk_clear_color_value_from_d3d12(VkClearColorValue *vk_value, const D3D12_CLEAR_VALUE *d3d_value)
{
    unsigned int i;

    for (i = 0; i < 4; i++)
        vk_value->float32[i] = d3d_value->Color[i];
}

static void vk_clear_depth_stencil_value_from_d3d12(VkClearDepthStencilValue *vk_value,
        const D3D12_CLEAR_VALUE *d3d_depth_value, const D3D12_CLEAR_VALUE *d3d_stencil_value)
{
    vk_value->depth = d3d_depth_value->DepthStencil.Depth;
    vk_value->stencil = d3d_stencil_value->DepthStencil.Stencil;
}

static VkAttachmentLoadOp vk_load_op_from_d3d12(D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE access_type)
{
    if (access_type == D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_CLEAR)
        return VK_ATTACHMENT_LOAD_OP_CLEAR;

    if (access_type == D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_DISCARD)
        return VK_ATTACHMENT_LOAD_OP_DONT_CARE;

    return VK_ATTACHMENT_LOAD_OP_LOAD;
}

static void d3d12_command_list_load_render_pass_rtv(struct d3d12_command_list *list,
        struct d3d12_rtv_desc *rtv_info, const D3D12_RENDER_PASS_RENDER_TARGET_DESC *rt)
{
    VkAttachmentLoadOp load_op = vk_load_op_from_d3d12(rt->BeginningAccess.Type);
    VkClearValue clear_value;
    bool full_resource_clear;

    vk_clear_color_value_from_d3d12(&clear_value.color, &rt->BeginningAccess.Clear.ClearValue);

    full_resource_clear = vkd3d_rtv_and_aspects_fully_cover_resource(rtv_info->resource, rtv_info->view,
            load_op != VK_ATTACHMENT_LOAD_OP_LOAD ? rtv_info->format->vk_aspect_mask : 0u);
    d3d12_command_list_track_resource_usage(list, rtv_info->resource, !full_resource_clear);

    if (load_op != VK_ATTACHMENT_LOAD_OP_LOAD)
    {
        d3d12_command_list_load_attachment(list, rtv_info->resource, rtv_info->view,
                VK_IMAGE_ASPECT_COLOR_BIT, &clear_value, 0, NULL, load_op);
    }
}

static void d3d12_command_list_load_render_pass_dsv(struct d3d12_command_list *list,
        struct d3d12_rtv_desc *dsv_info, const D3D12_RENDER_PASS_DEPTH_STENCIL_DESC *ds)
{
    VkAttachmentLoadOp stencil_load_op = vk_load_op_from_d3d12(ds->StencilBeginningAccess.Type);
    VkAttachmentLoadOp depth_load_op = vk_load_op_from_d3d12(ds->DepthBeginningAccess.Type);
    VkImageAspectFlags aspect_flags = dsv_info->format->vk_aspect_mask;
    VkImageAspectFlags clear_aspects = 0u;
    VkClearValue clear_value;
    bool full_resource_clear;

    vk_clear_depth_stencil_value_from_d3d12(&clear_value.depthStencil,
            &ds->DepthBeginningAccess.Clear.ClearValue,
            &ds->StencilBeginningAccess.Clear.ClearValue);

    if (depth_load_op != VK_ATTACHMENT_LOAD_OP_LOAD)
        clear_aspects |= VK_IMAGE_ASPECT_DEPTH_BIT;
    if (stencil_load_op != VK_ATTACHMENT_LOAD_OP_LOAD)
        clear_aspects |= VK_IMAGE_ASPECT_STENCIL_BIT;

    clear_aspects &= aspect_flags;

    full_resource_clear = vkd3d_rtv_and_aspects_fully_cover_resource(dsv_info->resource, dsv_info->view, clear_aspects);
    d3d12_command_list_track_resource_usage(list, dsv_info->resource, !full_resource_clear);

    if (depth_load_op == stencil_load_op || aspect_flags != (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT))
    {
        VkAttachmentLoadOp load_op = (aspect_flags & VK_IMAGE_ASPECT_DEPTH_BIT) ? depth_load_op : stencil_load_op;

        if (load_op != VK_ATTACHMENT_LOAD_OP_LOAD)
        {
            d3d12_command_list_load_attachment(list, dsv_info->resource, dsv_info->view,
                    aspect_flags, &clear_value, 0, NULL, load_op);
        }
    }
    else
    {
        if (depth_load_op != VK_ATTACHMENT_LOAD_OP_LOAD)
        {
            d3d12_command_list_load_attachment(list, dsv_info->resource, dsv_info->view,
                    VK_IMAGE_ASPECT_DEPTH_BIT, &clear_value, 0, NULL, depth_load_op);
        }

        if (stencil_load_op != VK_ATTACHMENT_LOAD_OP_LOAD)
        {
            d3d12_command_list_load_attachment(list, dsv_info->resource, dsv_info->view,
                    VK_IMAGE_ASPECT_STENCIL_BIT, &clear_value, 0, NULL, stencil_load_op);
        }
    }
}

static void d3d12_command_list_resolve_render_pass_attachments(struct d3d12_command_list *list)
{
    const struct vkd3d_vk_device_procs *vk_procs = &list->device->vk_procs;
    enum vkd3d_resolve_image_path local_resolve_paths[8], *resolve_paths;
    VkImageMemoryBarrier2 local_barriers[32], *barriers;
    uint32_t barrier_index, barrier_count;
    VkPipelineStageFlags2 src_stages;
    VkDependencyInfo dep_info;
    VkAccessFlags2 src_access;
    VkImageLayout src_layout;
    unsigned int i, j, k;

    /* Ensure we have enough storage for two full sets of image barriers. Each set
     * needs to be large enough to hold a pair of image barriers for each unique set
     * of subresources in the resolve region arrays. Compute an upper bound here so
     * that we won't have to traverse the region arrays multiple times. */
    barrier_count = 0;

    for (i = 0; i < list->rtv_resolve_count; i++)
        barrier_count += 2 * list->rtv_resolves[i].region_count;

    if (!barrier_count)
        return;

    barriers = local_barriers;

    if (barrier_count * 2 > ARRAY_SIZE(local_barriers))
        barriers = vkd3d_malloc(sizeof(*barriers) * barrier_count * 2);

    resolve_paths = local_resolve_paths;

    if (list->rtv_resolve_count > ARRAY_SIZE(local_resolve_paths))
        resolve_paths = vkd3d_malloc(sizeof(*resolve_paths) * list->rtv_resolve_count);

    memset(&dep_info, 0, sizeof(dep_info));
    dep_info.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dep_info.pImageMemoryBarriers = barriers;

    for (i = 0; i < list->rtv_resolve_count; i++)
    {
        struct d3d12_rtv_resolve *resolve = &list->rtv_resolves[i];
        const VkImageResolve2 *regions = &list->rtv_resolve_regions[resolve->region_index];

        if (!resolve->region_count)
            continue;

        resolve_paths[i] = d3d12_command_list_select_resolve_path(list, resolve->dst_resource,
                resolve->src_resource, resolve->region_count, regions, resolve->format,
                resolve->mode);

        for (j = 0; j < resolve->region_count; j++)
        {
            bool found_src = false;
            bool found_dst = false;

            for (k = 0; k < j && (!found_src || !found_dst); k++)
            {
                found_src = found_src || !memcmp(&regions[j].srcSubresource, &regions[k].srcSubresource, sizeof(regions[j].srcSubresource));
                found_dst = found_dst || !memcmp(&regions[j].srcSubresource, &regions[k].dstSubresource, sizeof(regions[j].dstSubresource));
            }

            if (!found_src)
            {
                barrier_index = dep_info.imageMemoryBarrierCount++;

                if (resolve->src_resource->format->vk_aspect_mask & (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT))
                {
                    src_stages = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
                    src_access = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
                    src_layout = d3d12_command_list_get_depth_stencil_resource_layout(list, resolve->src_resource, NULL);
                }
                else
                {
                    src_stages = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
                    src_access = VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
                    src_layout = d3d12_resource_pick_layout(resolve->src_resource, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
                }

                d3d12_get_resolve_barrier_for_src_resource(resolve->src_resource, &regions[j],
                        resolve_paths[i], false, src_layout, src_stages, src_access, &barriers[barrier_index]);

                d3d12_get_resolve_barrier_for_src_resource(resolve->src_resource, &regions[j],
                        resolve_paths[i], true, src_layout, src_stages, src_access, &barriers[barrier_count + barrier_index]);
            }

            if (!found_dst)
            {
                barrier_index = dep_info.imageMemoryBarrierCount++;

                /* The destination image must be in RESOLVE_DEST state */
                d3d12_get_resolve_barrier_for_dst_resource(resolve->dst_resource, &regions[j],
                        resolve_paths[i], false, resolve->dst_resource->common_layout, VK_PIPELINE_STAGE_2_RESOLVE_BIT,
                        VK_ACCESS_2_NONE, &barriers[barrier_index]);

                d3d12_get_resolve_barrier_for_dst_resource(resolve->dst_resource, &regions[j],
                        resolve_paths[i], true, resolve->dst_resource->common_layout, VK_PIPELINE_STAGE_2_RESOLVE_BIT,
                        VK_ACCESS_2_NONE, &barriers[barrier_count + barrier_index]);
            }
        }
    }

    VK_CALL(vkCmdPipelineBarrier2(list->cmd.vk_command_buffer, &dep_info));

    for (i = 0; i < list->rtv_resolve_count; i++)
    {
        struct d3d12_rtv_resolve *resolve = &list->rtv_resolves[i];
        const VkImageResolve2 *regions = &list->rtv_resolve_regions[resolve->region_index];

        if (resolve->region_count)
        {
            d3d12_command_list_execute_resolve(list, resolve->dst_resource, resolve->src_resource,
                    resolve->region_count, regions, resolve->format, resolve->mode, resolve_paths[i]);
        }
    }

    dep_info.pImageMemoryBarriers = barriers + barrier_count;
    VK_CALL(vkCmdPipelineBarrier2(list->cmd.vk_command_buffer, &dep_info));

    if (barriers != local_barriers)
        vkd3d_free(barriers);

    if (resolve_paths != local_resolve_paths)
        vkd3d_free(resolve_paths);

    d3d12_command_list_reset_rtv_resolves(list);
}

static void d3d12_command_list_add_render_pass_resolve(struct d3d12_command_list *list,
        const D3D12_RENDER_PASS_ENDING_ACCESS_RESOLVE_PARAMETERS *args, VkImageAspectFlagBits aspect)
{
    VkImageResolve2 region, *src_region;
    struct d3d12_rtv_resolve *resolve;
    bool merged_regions;
    unsigned int i, j;

    list->cmd.estimated_cost += VKD3D_COMMAND_COST_LOW;

    vkd3d_array_reserve((void **)&list->rtv_resolves, &list->rtv_resolve_size,
            list->rtv_resolve_count + 1, sizeof(*list->rtv_resolves));

    resolve = &list->rtv_resolves[list->rtv_resolve_count++];
    resolve->dst_resource = impl_from_ID3D12Resource(args->pDstResource);
    resolve->src_resource = impl_from_ID3D12Resource(args->pSrcResource);
    resolve->region_index = list->rtv_resolve_region_count;
    resolve->region_count = 0;
    resolve->format = args->Format;
    resolve->mode = args->ResolveMode;

    memset(&region, 0, sizeof(region));
    region.sType = VK_STRUCTURE_TYPE_IMAGE_RESOLVE_2;

    for (i = 0; i < args->SubresourceCount; i++)
    {
        const D3D12_RENDER_PASS_ENDING_ACCESS_RESOLVE_SUBRESOURCE_PARAMETERS *subresource_args = &args->pSubresourceParameters[i];

        region.srcSubresource = vk_image_subresource_layers_from_d3d12(
                resolve->src_resource->format, subresource_args->SrcSubresource,
                resolve->src_resource->desc.MipLevels, resolve->src_resource->desc.DepthOrArraySize);

        region.dstSubresource = vk_image_subresource_layers_from_d3d12(
                resolve->dst_resource->format, subresource_args->DstSubresource,
                resolve->dst_resource->desc.MipLevels, resolve->dst_resource->desc.DepthOrArraySize);

        region.srcOffset.x = subresource_args->SrcRect.left;
        region.srcOffset.y = subresource_args->SrcRect.top;
        region.dstOffset.x = subresource_args->DstX;
        region.dstOffset.y = subresource_args->DstY;
        region.extent.width = (uint32_t)(subresource_args->SrcRect.right - subresource_args->SrcRect.left);
        region.extent.height = (uint32_t)(subresource_args->SrcRect.bottom - subresource_args->SrcRect.top);
        region.extent.depth = 1u;

        /* Merge with existing mip region if possible. To keep things simple, only consider
         * ordered subresource indices. */
        merged_regions = false;

        for (j = 0; j < resolve->region_count; j++)
        {
            src_region = &list->rtv_resolve_regions[resolve->region_index + j];

            if (src_region->srcSubresource.aspectMask == region.srcSubresource.aspectMask &&
                    src_region->srcSubresource.mipLevel == region.srcSubresource.mipLevel &&
                    src_region->srcSubresource.baseArrayLayer + src_region->srcSubresource.layerCount == region.srcSubresource.baseArrayLayer &&
                    src_region->dstSubresource.aspectMask == region.dstSubresource.aspectMask &&
                    src_region->dstSubresource.mipLevel == region.dstSubresource.mipLevel &&
                    src_region->dstSubresource.baseArrayLayer + src_region->dstSubresource.layerCount == region.dstSubresource.baseArrayLayer &&
                    src_region->srcOffset.x == region.srcOffset.x && src_region->srcOffset.y == region.srcOffset.y &&
                    src_region->dstOffset.x == region.dstOffset.x && src_region->dstOffset.y == region.dstOffset.y &&
                    src_region->extent.width == region.extent.width && src_region->extent.height == region.extent.height)
            {
                src_region->srcSubresource.layerCount += region.srcSubresource.layerCount;
                src_region->dstSubresource.layerCount += region.dstSubresource.layerCount;

                merged_regions = true;
                break;
            }
        }

        if (!merged_regions)
        {
            vkd3d_array_reserve((void **)&list->rtv_resolve_regions, &list->rtv_resolve_region_size,
                    list->rtv_resolve_region_count + 1, sizeof(*list->rtv_resolve_regions));

            list->rtv_resolve_regions[list->rtv_resolve_region_count++] = region;
            resolve->region_count += 1;
        }
    }
}

static bool d3d12_render_pass_beginning_access_binds_to_rasterizer(D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE access,
        D3D12_RENDER_PASS_FLAGS flags, VkImageAspectFlagBits aspect)
{
    UINT bind_read_only_dsv_flags;

    if (access == D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_CLEAR ||
            access == D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_DISCARD ||
            access == D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_PRESERVE ||
            access == D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_PRESERVE_LOCAL_RENDER)
        return true;

    if (access == D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_PRESERVE_LOCAL_SRV)
    {
        bind_read_only_dsv_flags = 0u;

        if (aspect == VK_IMAGE_ASPECT_DEPTH_BIT)
            bind_read_only_dsv_flags = D3D12_RENDER_PASS_FLAG_BIND_READ_ONLY_DEPTH;
        else if (aspect == VK_IMAGE_ASPECT_STENCIL_BIT)
            bind_read_only_dsv_flags = D3D12_RENDER_PASS_FLAG_BIND_READ_ONLY_STENCIL;

        return !!(flags & bind_read_only_dsv_flags);
    }

    /* NO_ACCESS, PRESERVE_LOCAL_UAV */
    return false;
}

static void STDMETHODCALLTYPE d3d12_command_list_BeginRenderPass(d3d12_command_list_iface *iface,
        UINT rt_count, const D3D12_RENDER_PASS_RENDER_TARGET_DESC *render_targets,
        const D3D12_RENDER_PASS_DEPTH_STENCIL_DESC *depth_stencil, D3D12_RENDER_PASS_FLAGS flags)
{
    struct d3d12_command_list *list = impl_from_ID3D12GraphicsCommandList(iface);
    struct d3d12_rtv_desc *rtv_desc;
    VkImageAspectFlags dsv_aspects;
    VkFormat prev_dsv_format;
    unsigned int i, rt_index;

    TRACE("iface %p, rt_count %u, render_targets %p, depth_stencil %p, flags %#x.\n",
            iface, rt_count, render_targets, depth_stencil, flags);

    if (list->is_inside_render_pass)
    {
        d3d12_command_list_mark_as_invalid(list, "BeginRenderPass called inside a render pass.\n");
        return;
    }

    d3d12_command_list_invalidate_rendering_info(list);
    d3d12_command_list_end_current_render_pass(list, false);

    prev_dsv_format = list->dsv.format ? list->dsv.format->vk_format : VK_FORMAT_UNDEFINED;

    list->is_inside_render_pass = true;
    list->render_pass_flags = flags;
    memset(list->rtvs, 0, sizeof(list->rtvs));
    memset(&list->dsv, 0, sizeof(list->dsv));

    d3d12_command_list_debug_mark_begin_region(list, "BeginRenderPass");

    for (i = 0, rt_index = 0; i < rt_count; i++)
    {
        const D3D12_RENDER_PASS_RENDER_TARGET_DESC *rt = &render_targets[i];

        if (d3d12_render_pass_beginning_access_binds_to_rasterizer(rt->BeginningAccess.Type, flags, VK_IMAGE_ASPECT_COLOR_BIT))
        {
            if (rt_index >= ARRAY_SIZE(list->rtvs))
            {
                WARN("Render target count %u > %zu, ignoring extra descriptors.\n", rt_index, ARRAY_SIZE(list->rtvs));
                continue;
            }

            if ((rtv_desc = d3d12_rtv_desc_from_cpu_handle(rt->cpuDescriptor)) && rtv_desc->resource)
            {
                list->rtvs[rt_index] = *rtv_desc;

                VKD3D_BREADCRUMB_AUX64(rtv_desc->view->cookie);
                VKD3D_BREADCRUMB_AUX32(i);
                VKD3D_BREADCRUMB_TAG("RTV bind");

                if (!(flags & D3D12_RENDER_PASS_FLAG_RESUMING_PASS))
                    d3d12_command_list_load_render_pass_rtv(list, &list->rtvs[rt_index], rt);
            }
            else
            {
                VKD3D_BREADCRUMB_AUX32(i);
                VKD3D_BREADCRUMB_TAG("RTV bind NULL");
            }

            rt_index += 1;
        }
    }

    dsv_aspects = 0;

    if (depth_stencil)
    {
        if ((rtv_desc = d3d12_rtv_desc_from_cpu_handle(depth_stencil->cpuDescriptor)) && rtv_desc->resource)
        {
            dsv_aspects = rtv_desc->format->vk_aspect_mask;

            if (d3d12_render_pass_beginning_access_binds_to_rasterizer(depth_stencil->DepthBeginningAccess.Type, flags, VK_IMAGE_ASPECT_DEPTH_BIT) ||
                    (d3d12_render_pass_beginning_access_binds_to_rasterizer(depth_stencil->StencilBeginningAccess.Type, flags, VK_IMAGE_ASPECT_STENCIL_BIT) &&
                    (dsv_aspects & VK_IMAGE_ASPECT_STENCIL_BIT)))
            {
                list->dsv = *rtv_desc;

                VKD3D_BREADCRUMB_AUX64(rtv_desc->view->cookie);
                VKD3D_BREADCRUMB_TAG("DSV bind");

                if (!(flags & D3D12_RENDER_PASS_FLAG_RESUMING_PASS))
                    d3d12_command_list_load_render_pass_dsv(list, &list->dsv, depth_stencil);
            }
        }
        else
        {
            VKD3D_BREADCRUMB_TAG("DSV bind NULL");
        }
    }

    if (!(list->render_pass_flags & D3D12_RENDER_PASS_FLAG_SUSPENDING_PASS))
    {
        for (i = 0; i < rt_count; i++)
        {
            const D3D12_RENDER_PASS_RENDER_TARGET_DESC *rt = &render_targets[i];

            if (rt->EndingAccess.Type == D3D12_RENDER_PASS_ENDING_ACCESS_TYPE_RESOLVE)
                d3d12_command_list_add_render_pass_resolve(list, &rt->EndingAccess.Resolve, VK_IMAGE_ASPECT_COLOR_BIT);
        }

        if (depth_stencil)
        {
            if (depth_stencil->DepthEndingAccess.Type == D3D12_RENDER_PASS_ENDING_ACCESS_TYPE_RESOLVE &&
                    (dsv_aspects & VK_IMAGE_ASPECT_DEPTH_BIT))
                d3d12_command_list_add_render_pass_resolve(list, &depth_stencil->DepthEndingAccess.Resolve, VK_IMAGE_ASPECT_DEPTH_BIT);

            if (depth_stencil->StencilEndingAccess.Type == D3D12_RENDER_PASS_ENDING_ACCESS_TYPE_RESOLVE &&
                    (dsv_aspects & VK_IMAGE_ASPECT_STENCIL_BIT))
                d3d12_command_list_add_render_pass_resolve(list, &depth_stencil->StencilEndingAccess.Resolve, VK_IMAGE_ASPECT_STENCIL_BIT);
        }
    }

    d3d12_command_list_invalidate_ds_state(list, prev_dsv_format);
    d3d12_command_list_recompute_fb_size(list);

    d3d12_command_list_debug_mark_end_region(list);
}

static void STDMETHODCALLTYPE d3d12_command_list_EndRenderPass(d3d12_command_list_iface *iface)
{
    struct d3d12_command_list *list = impl_from_ID3D12GraphicsCommandList(iface);

    TRACE("iface %p.\n", iface);

    if (!list->is_inside_render_pass)
    {
        d3d12_command_list_mark_as_invalid(list, "EndRenderPass called outside a render pass.\n");
        return;
    }

    d3d12_command_list_end_current_render_pass(list, false);

    d3d12_command_list_debug_mark_begin_region(list, "EndRenderPass");

    if (!(list->render_pass_flags & D3D12_RENDER_PASS_FLAG_SUSPENDING_PASS))
        d3d12_command_list_resolve_render_pass_attachments(list);

    /* Bound render targets are implicitly unbound after the render pass */
    list->is_inside_render_pass = false;
    list->render_pass_flags = 0;
    memset(list->rtvs, 0, sizeof(list->rtvs));
    memset(&list->dsv, 0, sizeof(list->dsv));

    d3d12_command_list_debug_mark_end_region(list);
}

static void STDMETHODCALLTYPE d3d12_command_list_InitializeMetaCommand(d3d12_command_list_iface *iface,
        ID3D12MetaCommand *meta_command, const void *parameter_data, SIZE_T parameter_size)
{
    struct d3d12_meta_command *meta_command_object = impl_from_ID3D12MetaCommand(meta_command);
    struct d3d12_command_list *list = impl_from_ID3D12GraphicsCommandList(iface);

    TRACE("iface %p, meta_command %p, parameter_data %p, parameter_size %lu.\n",
            iface, meta_command, parameter_data, parameter_size);

    /* Not all meta commands require initialization */
    if (!meta_command_object->init_proc)
        return;

    list->cmd.estimated_cost += VKD3D_COMMAND_COST_HIGH;

    d3d12_command_list_end_current_render_pass(list, true);
    d3d12_command_list_end_transfer_batch(list);
    d3d12_command_list_invalidate_all_state(list);

    meta_command_object->init_proc(meta_command_object, list, parameter_data, parameter_size);
}

static void STDMETHODCALLTYPE d3d12_command_list_ExecuteMetaCommand(d3d12_command_list_iface *iface,
        ID3D12MetaCommand *meta_command, const void *parameter_data, SIZE_T parameter_size)
{
    struct d3d12_meta_command *meta_command_object = impl_from_ID3D12MetaCommand(meta_command);
    struct d3d12_command_list *list = impl_from_ID3D12GraphicsCommandList(iface);

    TRACE("iface %p, meta_command %p, parameter_data %p, parameter_size %lu.\n",
            iface, meta_command, parameter_data, parameter_size);

    list->cmd.estimated_cost += VKD3D_COMMAND_COST_HIGH;

    d3d12_command_list_end_current_render_pass(list, true);
    d3d12_command_list_end_transfer_batch(list);
    d3d12_command_list_invalidate_all_state(list);

    meta_command_object->exec_proc(meta_command_object, list, parameter_data, parameter_size);
}

static void d3d12_command_list_free_rtas_batch(struct d3d12_command_list *list)
{
    struct d3d12_rtas_batch_state *rtas_batch = &list->rtas_batch;

    vkd3d_free(rtas_batch->build_infos);
    vkd3d_free(rtas_batch->geometry_infos);
    vkd3d_free(rtas_batch->range_infos);
    vkd3d_free(rtas_batch->range_ptrs);
}

static bool d3d12_command_list_allocate_rtas_build_info(struct d3d12_command_list *list, uint32_t geometry_count,
        VkAccelerationStructureBuildGeometryInfoKHR **build_info,
        VkAccelerationStructureGeometryKHR **geometry_infos,
        VkAccelerationStructureBuildRangeInfoKHR **range_infos)
{
    struct d3d12_rtas_batch_state *rtas_batch = &list->rtas_batch;

    if (!vkd3d_array_reserve((void **)&rtas_batch->build_infos, &rtas_batch->build_info_size,
            rtas_batch->build_info_count + 1, sizeof(*rtas_batch->build_infos)))
    {
        ERR("Failed to allocate build info array.\n");
        return false;
    }

    if (!vkd3d_array_reserve((void **)&rtas_batch->geometry_infos, &rtas_batch->geometry_info_size,
            rtas_batch->geometry_info_count + geometry_count, sizeof(*rtas_batch->geometry_infos)))
    {
        ERR("Failed to allocate geometry info array.\n");
        return false;
    }

    if (!vkd3d_array_reserve((void **)&rtas_batch->range_infos, &rtas_batch->range_info_size,
            rtas_batch->geometry_info_count + geometry_count, sizeof(*rtas_batch->range_infos)))
    {
        ERR("Failed to allocate range info array.\n");
        return false;
    }

    *build_info = &rtas_batch->build_infos[rtas_batch->build_info_count];
    *geometry_infos = &rtas_batch->geometry_infos[rtas_batch->geometry_info_count];
    *range_infos = &rtas_batch->range_infos[rtas_batch->geometry_info_count];

    rtas_batch->build_info_count += 1;
    rtas_batch->geometry_info_count += geometry_count;

    return true;
}

static void d3d12_command_list_clear_rtas_batch(struct d3d12_command_list *list)
{
    struct d3d12_rtas_batch_state *rtas_batch = &list->rtas_batch;

    rtas_batch->build_info_count = 0;
    rtas_batch->geometry_info_count = 0;
}

static void d3d12_command_list_flush_rtas_batch(struct d3d12_command_list *list)
{
    const struct vkd3d_vk_device_procs *vk_procs = &list->device->vk_procs;
    struct d3d12_rtas_batch_state *rtas_batch = &list->rtas_batch;
    unsigned int i, geometry_index;

    if (!rtas_batch->build_info_count)
        return;

    TRACE("list %p, build_info_count %zu.\n", list, rtas_batch->build_info_count);

    if (!vkd3d_array_reserve((void **)&rtas_batch->range_ptrs, &rtas_batch->range_ptr_size,
            rtas_batch->build_info_count, sizeof(*rtas_batch->range_ptrs)))
    {
        ERR("Failed to allocate range pointer array.\n");
        return;
    }

    /* Assign geometry and range pointers */
    geometry_index = 0;

    for (i = 0; i < rtas_batch->build_info_count; i++)
    {
        uint32_t geometry_count = rtas_batch->build_infos[i].geometryCount;
        assert(geometry_index + geometry_count <= rtas_batch->geometry_info_count);

        rtas_batch->build_infos[i].pGeometries = &rtas_batch->geometry_infos[geometry_index];
        rtas_batch->range_ptrs[i] = &rtas_batch->range_infos[geometry_index];

        geometry_index += geometry_count;
    }

    d3d12_command_list_end_current_render_pass(list, true);
    d3d12_command_list_end_transfer_batch(list);

    VK_CALL(vkCmdBuildAccelerationStructuresKHR(list->cmd.vk_command_buffer,
            rtas_batch->build_info_count, rtas_batch->build_infos, rtas_batch->range_ptrs));

    d3d12_command_list_clear_rtas_batch(list);
}

static void STDMETHODCALLTYPE d3d12_command_list_BuildRaytracingAccelerationStructure(d3d12_command_list_iface *iface,
        const D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC *desc, UINT num_postbuild_info_descs,
        const D3D12_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO_DESC *postbuild_info_descs)
{
    struct d3d12_command_list *list = impl_from_ID3D12GraphicsCommandList(iface);
    const struct vkd3d_vk_device_procs *vk_procs = &list->device->vk_procs;
    struct d3d12_rtas_batch_state *rtas_batch = &list->rtas_batch;
    VkAccelerationStructureBuildGeometryInfoKHR *build_info;
    VkAccelerationStructureBuildRangeInfoKHR *range_infos;
    VkAccelerationStructureGeometryKHR *geometry_infos;
    uint32_t *primitive_counts = NULL;
    VkMemoryBarrier2 vk_barrier;
    VkDependencyInfo dep_info;
    uint32_t geometry_count;

    TRACE("iface %p, desc %p, num_postbuild_info_descs %u, postbuild_info_descs %p\n",
            iface, desc, num_postbuild_info_descs, postbuild_info_descs);

    if (list->is_inside_render_pass)
        d3d12_command_list_mark_as_invalid(list, "BuildRaytracingAccelerationStructure called within a render pass.\n");

    if (!d3d12_device_supports_ray_tracing_tier_1_0(list->device))
    {
        WARN("Acceleration structure is not supported. Calling this is invalid.\n");
        return;
    }

    list->cmd.estimated_cost += VKD3D_COMMAND_COST_HIGH;

    /* Do not batch TLAS and BLAS builds into the same command, since doing so
     * is disallowed if there are data dependencies between the builds. This
     * happens in Cyberpunk 2077, which does not emit appropriate UAV barriers. */
    if (rtas_batch->build_info_count && rtas_batch->build_type != desc->Inputs.Type)
    {
        d3d12_command_list_flush_rtas_batch(list);

        memset(&vk_barrier, 0, sizeof(vk_barrier));
        vk_barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
        vk_barrier.srcStageMask = VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR;
        vk_barrier.srcAccessMask = VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
        vk_barrier.dstStageMask = VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR;
        vk_barrier.dstAccessMask = VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;

        memset(&dep_info, 0, sizeof(dep_info));
        dep_info.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        dep_info.memoryBarrierCount = 1;
        dep_info.pMemoryBarriers = &vk_barrier;

        VK_CALL(vkCmdPipelineBarrier2(list->cmd.vk_command_buffer, &dep_info));
    }

    rtas_batch->build_type = desc->Inputs.Type;

    geometry_count = vkd3d_acceleration_structure_get_geometry_count(&desc->Inputs);

#ifdef VKD3D_ENABLE_BREADCRUMBS
    if (vkd3d_config_flags & VKD3D_CONFIG_FLAG_BREADCRUMBS)
        primitive_counts = vkd3d_malloc(geometry_count * sizeof(*primitive_counts));
#endif

    if (!d3d12_command_list_allocate_rtas_build_info(list, geometry_count,
            &build_info, &geometry_infos, &range_infos))
        return;

    if (!vkd3d_acceleration_structure_convert_inputs(list->device, &desc->Inputs,
            build_info, geometry_infos, range_infos, primitive_counts))
    {
        ERR("Failed to convert inputs.\n");
        return;
    }

    if (desc->DestAccelerationStructureData)
    {
        build_info->dstAccelerationStructure =
                vkd3d_va_map_place_acceleration_structure(&list->device->memory_allocator.va_map,
                        list->device, desc->DestAccelerationStructureData);
        if (build_info->dstAccelerationStructure == VK_NULL_HANDLE)
        {
            ERR("Failed to place destAccelerationStructure. Dropping call.\n");
            return;
        }
    }

    if (build_info->mode == VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR &&
            desc->SourceAccelerationStructureData)
    {
        build_info->srcAccelerationStructure =
                vkd3d_va_map_place_acceleration_structure(&list->device->memory_allocator.va_map,
                        list->device, desc->SourceAccelerationStructureData);
        if (build_info->srcAccelerationStructure == VK_NULL_HANDLE)
        {
            ERR("Failed to place srcAccelerationStructure. Dropping call.\n");
            return;
        }
    }

    build_info->scratchData.deviceAddress = desc->ScratchAccelerationStructureData;

#ifdef VKD3D_ENABLE_BREADCRUMBS
    /* Immediately record the RTAS build command here so that we don't have
     * to create a deep copy of the entire D3D12 input description */
    if (vkd3d_config_flags & VKD3D_CONFIG_FLAG_BREADCRUMBS)
    {
        d3d12_command_list_flush_rtas_batch(list);

        VKD3D_BREADCRUMB_TAG("RTAS build [Dest VA, Source VA, Scratch VA]");
        VKD3D_BREADCRUMB_AUX64(desc->DestAccelerationStructureData);
        VKD3D_BREADCRUMB_AUX64(desc->SourceAccelerationStructureData);
        VKD3D_BREADCRUMB_AUX64(desc->ScratchAccelerationStructureData);
        VKD3D_BREADCRUMB_TAG((desc->Inputs.Flags & D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PERFORM_UPDATE) ?
                "Update" : "Create");
        VKD3D_BREADCRUMB_TAG(desc->Inputs.Type == D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL ? "Top" : "Bottom");
        {
            VkAccelerationStructureBuildSizesInfoKHR size_info;

            memset(&size_info, 0, sizeof(size_info));
            size_info.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;

            if (desc->Inputs.Flags & D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PERFORM_UPDATE)
            {
                build_info->mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
                build_info->flags |= VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR;
            }
            VK_CALL(vkGetAccelerationStructureBuildSizesKHR(list->device->vk_device,
                    VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, build_info,
                    primitive_counts, &size_info));
            VKD3D_BREADCRUMB_TAG("Build requirements [Size, Build Scratch, Update Scratch]");
            VKD3D_BREADCRUMB_AUX64(size_info.accelerationStructureSize);
            VKD3D_BREADCRUMB_AUX64(size_info.buildScratchSize);
            VKD3D_BREADCRUMB_AUX64(size_info.updateScratchSize);

            if (desc->Inputs.Type == D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL)
            {
                VKD3D_BREADCRUMB_AUX64(desc->Inputs.InstanceDescs);
                VKD3D_BREADCRUMB_AUX32(desc->Inputs.NumDescs);
            }
            else
            {
                unsigned int i;
                for (i = 0; i < desc->Inputs.NumDescs; i++)
                {
                    const D3D12_RAYTRACING_GEOMETRY_DESC *geom;
                    if (desc->Inputs.DescsLayout == D3D12_ELEMENTS_LAYOUT_ARRAY)
                        geom = &desc->Inputs.pGeometryDescs[i];
                    else
                        geom = desc->Inputs.ppGeometryDescs[i];

                    if (geom->Type == D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES)
                    {
                        VKD3D_BREADCRUMB_TAG("Triangle [Flags, VBO VA, VBO stride, IBO, Transform, VBO format, IBO format, V count, I count]");
                        VKD3D_BREADCRUMB_AUX32(geom->Flags);
                        VKD3D_BREADCRUMB_AUX64(geom->Triangles.VertexBuffer.StartAddress);
                        VKD3D_BREADCRUMB_AUX64(geom->Triangles.VertexBuffer.StrideInBytes);
                        VKD3D_BREADCRUMB_AUX64(geom->Triangles.IndexBuffer);
                        VKD3D_BREADCRUMB_AUX64(geom->Triangles.Transform3x4);
                        VKD3D_BREADCRUMB_AUX32(geom->Triangles.VertexFormat);
                        VKD3D_BREADCRUMB_AUX32(geom->Triangles.IndexFormat);
                        VKD3D_BREADCRUMB_AUX32(geom->Triangles.VertexCount);
                        VKD3D_BREADCRUMB_AUX32(geom->Triangles.IndexCount);
                    }
                    else
                    {
                        VKD3D_BREADCRUMB_TAG("AABB [Flags, VA, stride, count]");
                        VKD3D_BREADCRUMB_AUX32(geom->Flags);
                        VKD3D_BREADCRUMB_AUX64(geom->AABBs.AABBs.StartAddress);
                        VKD3D_BREADCRUMB_AUX64(geom->AABBs.AABBs.StrideInBytes);
                        VKD3D_BREADCRUMB_AUX64(geom->AABBs.AABBCount);
                    }
                }
            }
        }

        vkd3d_free(primitive_counts);
    }
#endif

    if (num_postbuild_info_descs)
    {
        /* This doesn't seem to get used very often, so just record the build command
         * for now. If this ever becomes a performance issue, we can add postbuild info
         * to the batch. */
        d3d12_command_list_flush_rtas_batch(list);

        vkd3d_acceleration_structure_emit_immediate_postbuild_info(list,
                num_postbuild_info_descs, postbuild_info_descs,
                build_info->dstAccelerationStructure);
    }

    VKD3D_BREADCRUMB_COMMAND(BUILD_RTAS);
}

static void STDMETHODCALLTYPE d3d12_command_list_EmitRaytracingAccelerationStructurePostbuildInfo(d3d12_command_list_iface *iface,
        const D3D12_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO_DESC *desc, UINT num_acceleration_structures,
        const D3D12_GPU_VIRTUAL_ADDRESS *src_data)
{
    struct d3d12_command_list *list = impl_from_ID3D12GraphicsCommandList(iface);
    TRACE("iface %p, desc %p, num_acceleration_structures %u, src_data %p\n",
            iface, desc, num_acceleration_structures, src_data);

    if (list->is_inside_render_pass)
        d3d12_command_list_mark_as_invalid(list, "EmitRaytracingAccelerationStructurePostbuildInfo called within a render pass.\n");

    if (!d3d12_device_supports_ray_tracing_tier_1_0(list->device))
    {
        WARN("Acceleration structure is not supported. Calling this is invalid.\n");
        return;
    }

    list->cmd.estimated_cost += VKD3D_COMMAND_COST_LOW;

    d3d12_command_list_end_current_render_pass(list, true);
    vkd3d_acceleration_structure_emit_postbuild_info(list,
            desc, num_acceleration_structures, src_data);

    VKD3D_BREADCRUMB_COMMAND(EMIT_RTAS_POSTBUILD);
}

static void STDMETHODCALLTYPE d3d12_command_list_CopyRaytracingAccelerationStructure(d3d12_command_list_iface *iface,
        D3D12_GPU_VIRTUAL_ADDRESS dst_data, D3D12_GPU_VIRTUAL_ADDRESS src_data,
        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_COPY_MODE mode)
{
    struct d3d12_command_list *list = impl_from_ID3D12GraphicsCommandList(iface);

    TRACE("iface %p, dst_data %#"PRIx64", src_data %#"PRIx64", mode %u\n",
          iface, dst_data, src_data, mode);

    if (list->is_inside_render_pass)
        d3d12_command_list_mark_as_invalid(list, "CopyRaytracingAccelerationStructure called within a render pass.\n");

    if (!d3d12_device_supports_ray_tracing_tier_1_0(list->device))
    {
        WARN("Acceleration structure is not supported. Calling this is invalid.\n");
        return;
    }

    list->cmd.estimated_cost += VKD3D_COMMAND_COST_HIGH;

    d3d12_command_list_end_current_render_pass(list, true);
    d3d12_command_list_end_transfer_batch(list);
    vkd3d_acceleration_structure_copy(list, dst_data, src_data, mode);

    VKD3D_BREADCRUMB_AUX64(dst_data);
    VKD3D_BREADCRUMB_AUX64(src_data);
    VKD3D_BREADCRUMB_AUX32(mode);
    VKD3D_BREADCRUMB_COMMAND(COPY_RTAS);
}

static void STDMETHODCALLTYPE d3d12_command_list_SetPipelineState1(d3d12_command_list_iface *iface,
        ID3D12StateObject *state_object)
{
    struct d3d12_rt_state_object *state = rt_impl_from_ID3D12StateObject(state_object);
    struct d3d12_command_list *list = impl_from_ID3D12GraphicsCommandList(iface);
    TRACE("iface %p, state_object %p\n", iface, state_object);

    if (list->rt_state == state)
        return;

    d3d12_command_list_invalidate_current_pipeline(list, false);
    /* SetPSO and SetPSO1 alias the same internal active pipeline state even if they are completely different types. */
    list->state = NULL;
    list->rt_state = state;

    /* DXR uses compute bind points for descriptors. When binding an RTPSO, invalidate all state
     * to make sure we broadcast state correctly to COMPUTE or RT bind points in Vulkan. */
    if (list->active_pipeline_type != VKD3D_PIPELINE_TYPE_RAY_TRACING)
    {
        list->active_pipeline_type = VKD3D_PIPELINE_TYPE_RAY_TRACING;
        d3d12_command_list_invalidate_root_parameters(list, &list->compute_bindings, true, NULL);
    }

#ifdef VKD3D_ENABLE_BREADCRUMBS
    if ((vkd3d_config_flags & VKD3D_CONFIG_FLAG_BREADCRUMBS) && state)
    {
        struct vkd3d_breadcrumb_command cmd;
        size_t i;

        for (i = 0; i < state->breadcrumb_shaders_count; i++)
        {
            cmd.type = VKD3D_BREADCRUMB_COMMAND_SET_SHADER_HASH;
            cmd.shader.stage = state->breadcrumb_shaders[i].stage;
            cmd.shader.hash = state->breadcrumb_shaders[i].hash;
            vkd3d_breadcrumb_tracer_add_command(list, &cmd);

            VKD3D_BREADCRUMB_TAG(state->breadcrumb_shaders[i].name);
        }
    }
#endif
}

static VkStridedDeviceAddressRegionKHR convert_strided_range(
        const D3D12_GPU_VIRTUAL_ADDRESS_RANGE_AND_STRIDE *region)
{
    VkStridedDeviceAddressRegionKHR table;
    table.deviceAddress = region->StartAddress;
    table.size = region->SizeInBytes;
    table.stride = region->StrideInBytes;
    return table;
}

static void STDMETHODCALLTYPE d3d12_command_list_DispatchRays(d3d12_command_list_iface *iface,
        const D3D12_DISPATCH_RAYS_DESC *desc)
{
    struct d3d12_command_list *list = impl_from_ID3D12GraphicsCommandList(iface);
    const struct vkd3d_vk_device_procs *vk_procs = &list->device->vk_procs;
    VkStridedDeviceAddressRegionKHR callable_table;
    VkStridedDeviceAddressRegionKHR raygen_table;
    VkStridedDeviceAddressRegionKHR miss_table;
    VkStridedDeviceAddressRegionKHR hit_table;

    TRACE("iface %p, desc %p\n", iface, desc);

    if (list->is_inside_render_pass)
        d3d12_command_list_mark_as_invalid(list, "DispatchRays called within a render pass.\n");

    if (!d3d12_device_supports_ray_tracing_tier_1_0(list->device))
    {
        WARN("Ray tracing is not supported. Calling this is invalid.\n");
        return;
    }

    list->cmd.estimated_cost += VKD3D_COMMAND_COST_HIGH;

    raygen_table.deviceAddress = desc->RayGenerationShaderRecord.StartAddress;
    raygen_table.size = desc->RayGenerationShaderRecord.SizeInBytes;
    raygen_table.stride = raygen_table.size;
    miss_table = convert_strided_range(&desc->MissShaderTable);
    hit_table = convert_strided_range(&desc->HitGroupTable);
    callable_table = convert_strided_range(&desc->CallableShaderTable);

    d3d12_command_list_end_transfer_batch(list);

    if (!d3d12_command_list_update_raygen_state(list))
    {
        WARN("Failed to update raygen state, ignoring dispatch.\n");
        return;
    }

    /* TODO: Is DispatchRays predicated? */
    VK_CALL(vkCmdTraceRaysKHR(list->cmd.vk_command_buffer,
            &raygen_table, &miss_table, &hit_table, &callable_table,
            desc->Width, desc->Height, desc->Depth));

    VKD3D_BREADCRUMB_AUX32(desc->Width);
    VKD3D_BREADCRUMB_AUX32(desc->Height);
    VKD3D_BREADCRUMB_AUX32(desc->Depth);
    VKD3D_BREADCRUMB_AUX64(raygen_table.deviceAddress);
    VKD3D_BREADCRUMB_AUX64(raygen_table.size);
    VKD3D_BREADCRUMB_AUX32(raygen_table.stride);
    VKD3D_BREADCRUMB_AUX64(miss_table.deviceAddress);
    VKD3D_BREADCRUMB_AUX64(miss_table.size);
    VKD3D_BREADCRUMB_AUX32(miss_table.stride);
    VKD3D_BREADCRUMB_AUX64(hit_table.deviceAddress);
    VKD3D_BREADCRUMB_AUX64(hit_table.size);
    VKD3D_BREADCRUMB_AUX32(hit_table.stride);
    VKD3D_BREADCRUMB_AUX64(callable_table.deviceAddress);
    VKD3D_BREADCRUMB_AUX64(callable_table.size);
    VKD3D_BREADCRUMB_AUX32(callable_table.stride);
    VKD3D_BREADCRUMB_COMMAND(TRACE_RAYS);
}

static VkFragmentShadingRateCombinerOpKHR vk_shading_rate_combiner_from_d3d12(D3D12_SHADING_RATE_COMBINER combiner)
{
    switch (combiner)
    {
        case D3D12_SHADING_RATE_COMBINER_PASSTHROUGH:
            return VK_FRAGMENT_SHADING_RATE_COMBINER_OP_KEEP_KHR;
        case D3D12_SHADING_RATE_COMBINER_OVERRIDE:
            return VK_FRAGMENT_SHADING_RATE_COMBINER_OP_REPLACE_KHR;
        case D3D12_SHADING_RATE_COMBINER_MAX:
            return VK_FRAGMENT_SHADING_RATE_COMBINER_OP_MAX_KHR;
        case D3D12_SHADING_RATE_COMBINER_MIN:
            return VK_FRAGMENT_SHADING_RATE_COMBINER_OP_MIN_KHR;
        case D3D12_SHADING_RATE_COMBINER_SUM:
            /* Undocumented log space */
            return VK_FRAGMENT_SHADING_RATE_COMBINER_OP_MUL_KHR;
        default:
            ERR("Unhandled shading rate combiner %u.\n", combiner);
            /* Default to passthrough for unknown */
            return VK_FRAGMENT_SHADING_RATE_COMBINER_OP_KEEP_KHR;
    }
}

static uint32_t vk_fragment_size_from_d3d12(D3D12_AXIS_SHADING_RATE axis_rate)
{
    switch (axis_rate)
    {
        case D3D12_AXIS_SHADING_RATE_1X: return 1;
        case D3D12_AXIS_SHADING_RATE_2X: return 2;
        case D3D12_AXIS_SHADING_RATE_4X: return 4;
        default:
            ERR("Unhandled axis shading rate %u.\n", axis_rate);
            return 1;
    }
}

static void STDMETHODCALLTYPE d3d12_command_list_RSSetShadingRate(d3d12_command_list_iface *iface,
        D3D12_SHADING_RATE base, const D3D12_SHADING_RATE_COMBINER *combiners)
{
    VkFragmentShadingRateCombinerOpKHR combiner_ops[D3D12_RS_SET_SHADING_RATE_COMBINER_COUNT];
    struct d3d12_command_list *list = impl_from_ID3D12GraphicsCommandList(iface);
    struct vkd3d_dynamic_state *dyn_state = &list->dynamic_state;
    VkExtent2D fragment_size;
    uint32_t i;

    TRACE("iface %p, base %#x, combiners %p\n", iface, base, combiners);

    fragment_size.width = vk_fragment_size_from_d3d12(D3D12_GET_COARSE_SHADING_RATE_X_AXIS(base));
    fragment_size.height = vk_fragment_size_from_d3d12(D3D12_GET_COARSE_SHADING_RATE_Y_AXIS(base));

    for (i = 0; i < D3D12_RS_SET_SHADING_RATE_COMBINER_COUNT; i++)
    {
        combiner_ops[i] = combiners ?
                vk_shading_rate_combiner_from_d3d12(combiners[i]) :
                VK_FRAGMENT_SHADING_RATE_COMBINER_OP_KEEP_KHR;
    }

    if (memcmp(&fragment_size, &dyn_state->fragment_shading_rate.fragment_size, sizeof(fragment_size)) != 0 ||
            memcmp(combiner_ops, dyn_state->fragment_shading_rate.combiner_ops, sizeof(combiner_ops)) != 0)
    {
        dyn_state->fragment_shading_rate.fragment_size = fragment_size;
        memcpy(dyn_state->fragment_shading_rate.combiner_ops, combiner_ops, sizeof(combiner_ops));
        dyn_state->dirty_flags |= VKD3D_DYNAMIC_STATE_FRAGMENT_SHADING_RATE;
    }
}

static void STDMETHODCALLTYPE d3d12_command_list_RSSetShadingRateImage(d3d12_command_list_iface *iface,
        ID3D12Resource *image)
{
    struct d3d12_command_list *list = impl_from_ID3D12GraphicsCommandList(iface);
    struct d3d12_resource *vrs_image = impl_from_ID3D12Resource(image);

    TRACE("iface %p, image %p.\n", iface, image);

    /* Handle invalid images being set here. */
    if (vrs_image && !vrs_image->vrs_view)
    {
        WARN("RSSetShadingRateImage called with invalid resource for VRS.\n");
        vrs_image = NULL;
    }

    if (vrs_image == list->vrs_image)
        return;

    /* Need to end the renderpass if we have one to make
     * way for the new VRS attachment */
    d3d12_command_list_invalidate_rendering_info(list);
    d3d12_command_list_end_current_render_pass(list, false);

    if (vrs_image)
        d3d12_command_list_track_resource_usage(list, vrs_image, true);

    list->vrs_image = vrs_image;
}

static void STDMETHODCALLTYPE d3d12_command_list_DispatchMesh(d3d12_command_list_iface *iface, UINT x, UINT y, UINT z)
{
    struct d3d12_command_list *list = impl_from_ID3D12GraphicsCommandList(iface);
    const struct vkd3d_vk_device_procs *vk_procs = &list->device->vk_procs;
    struct vkd3d_scratch_allocation scratch;

    TRACE("iface %p, x %u, y %u, z %u.\n", iface, x, y, z);

    if (list->predication.fallback_enabled)
    {
        union vkd3d_predicate_command_direct_args args;
        args.dispatch.x = x;
        args.dispatch.y = y;
        args.dispatch.z = z;

        if (!d3d12_command_list_emit_predicated_command(list, VKD3D_PREDICATE_COMMAND_DISPATCH, 0, &args, &scratch))
            return;
    }

    if (!d3d12_command_list_begin_render_pass(list, VKD3D_PIPELINE_TYPE_MESH_GRAPHICS))
    {
        WARN("Failed to begin render pass, ignoring draw call.\n");
        return;
    }

    list->cmd.estimated_cost += VKD3D_COMMAND_COST_HIGH;

    if (!list->predication.fallback_enabled)
        VK_CALL(vkCmdDrawMeshTasksEXT(list->cmd.vk_command_buffer, x, y, z));
    else
        VK_CALL(vkCmdDrawMeshTasksIndirectEXT(list->cmd.vk_command_buffer, scratch.buffer, scratch.offset, 1, 0));
}

static bool d3d12_barrier_invalidates_indirect_arguments(D3D12_BARRIER_SYNC sync, D3D12_BARRIER_ACCESS access)
{
    return (sync & (D3D12_BARRIER_SYNC_ALL | D3D12_BARRIER_SYNC_EXECUTE_INDIRECT)) &&
            (access == D3D12_BARRIER_ACCESS_COMMON || (access & D3D12_BARRIER_ACCESS_INDIRECT_ARGUMENT));
}

static bool d3d12_barrier_accesses_copy_dest(D3D12_BARRIER_SYNC sync, D3D12_BARRIER_ACCESS access)
{
    return (sync & (D3D12_BARRIER_SYNC_ALL | D3D12_BARRIER_SYNC_COPY)) &&
            (access == D3D12_BARRIER_ACCESS_COMMON || (access & D3D12_BARRIER_ACCESS_COPY_DEST));
}

static void d3d12_command_list_merge_copy_tracking_global_barrier(struct d3d12_command_list *list,
        const D3D12_GLOBAL_BARRIER *barrier,
        struct d3d12_command_list_barrier_batch *batch)
{
    /* If we're going to do transfer barriers and we have
     * pending copies in flight which need to be synchronized,
     * we should just resolve that while we're at it. */
    if (list->tracked_copy_buffer_count && (
            d3d12_barrier_accesses_copy_dest(barrier->SyncBefore, barrier->AccessBefore) ||
                    d3d12_barrier_accesses_copy_dest(barrier->SyncAfter, barrier->AccessAfter)))
    {
        d3d12_command_list_merge_copy_tracking(list, batch);
    }
}

static VkPipelineStageFlags2 vk_stage_flags_from_d3d12_barrier(struct d3d12_command_list *list,
        D3D12_BARRIER_SYNC sync, D3D12_BARRIER_ACCESS access)
{
    VkPipelineStageFlags2 stages = 0;

    /* Resolve umbrella scopes. */
    /* https://microsoft.github.io/DirectX-Specs/d3d/D3D12EnhancedBarriers.html#umbrella-synchronization-scopes */
    if (sync & D3D12_BARRIER_SYNC_ALL)
        return VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;

    /* Split barriers are currently broken in the D3D12 runtime, so they cannot be used,
     * but the spec for them is rather unfortunate, you're meant to synchronize once with
     * SyncAfter = SPLIT, and then SyncBefore = SPLIT to complete the barrier.
     * Apparently, there can only be one SPLIT barrier in flight for each (sub-)resource
     * which is extremely weird and suggests we have to track a VkEvent to make this work,
     * which is complete bogus. SPLIT barriers are allowed cross submissions even ...
     * Only reasonable solution is to force ALL_COMMANDS_BIT when SPLIT is observed. */
    if (sync == D3D12_BARRIER_SYNC_SPLIT)
        return VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;

    if (sync & D3D12_BARRIER_SYNC_DRAW)
    {
        sync |= D3D12_BARRIER_SYNC_INDEX_INPUT |
                D3D12_BARRIER_SYNC_VERTEX_SHADING |
                D3D12_BARRIER_SYNC_PIXEL_SHADING |
                D3D12_BARRIER_SYNC_DEPTH_STENCIL |
                D3D12_BARRIER_SYNC_RENDER_TARGET;
    }

    if (sync & D3D12_BARRIER_SYNC_ALL_SHADING)
    {
        sync |= D3D12_BARRIER_SYNC_NON_PIXEL_SHADING |
                D3D12_BARRIER_SYNC_PIXEL_SHADING;
    }

    if (sync & D3D12_BARRIER_SYNC_NON_PIXEL_SHADING)
    {
        sync |= D3D12_BARRIER_SYNC_VERTEX_SHADING |
                D3D12_BARRIER_SYNC_COMPUTE_SHADING;

        /* Ray tracing is not included in this list in docs,
         * but the example code for legacy UAV barrier mapping
         * implies that it should be included. */
        if ((list->vk_queue_flags & VK_QUEUE_COMPUTE_BIT) &&
                d3d12_device_supports_ray_tracing_tier_1_0(list->device))
            sync |= D3D12_BARRIER_SYNC_RAYTRACING;
    }

    if (sync & D3D12_BARRIER_SYNC_INDEX_INPUT)
        stages |= VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT;

    if (sync & D3D12_BARRIER_SYNC_VERTEX_SHADING)
    {
        stages |= VK_PIPELINE_STAGE_2_VERTEX_ATTRIBUTE_INPUT_BIT |
                VK_PIPELINE_STAGE_2_PRE_RASTERIZATION_SHADERS_BIT;
        if (access == D3D12_BARRIER_ACCESS_COMMON || (access & D3D12_BARRIER_ACCESS_STREAM_OUTPUT))
            stages |= VK_PIPELINE_STAGE_2_TRANSFORM_FEEDBACK_BIT_EXT | VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT;
    }

    if (sync & D3D12_BARRIER_SYNC_PIXEL_SHADING)
    {
        stages |= VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
        /* Only add special pipeline stages when required by access masks. */
        if (list->device->device_info.fragment_shading_rate_features.attachmentFragmentShadingRate &&
                (access == D3D12_BARRIER_ACCESS_COMMON || (access & D3D12_BARRIER_ACCESS_SHADING_RATE_SOURCE)))
            stages |= VK_PIPELINE_STAGE_2_FRAGMENT_SHADING_RATE_ATTACHMENT_BIT_KHR;
    }

    if (sync & D3D12_BARRIER_SYNC_DEPTH_STENCIL)
        stages |= VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
    if (sync & D3D12_BARRIER_SYNC_RENDER_TARGET)
        stages |= VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;

    if (sync & (D3D12_BARRIER_SYNC_COMPUTE_SHADING | D3D12_BARRIER_SYNC_CLEAR_UNORDERED_ACCESS_VIEW))
        stages |= VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;

    if (sync & D3D12_BARRIER_SYNC_RAYTRACING)
        stages |= VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR;

    if (sync & D3D12_BARRIER_SYNC_COPY)
        stages |= VK_PIPELINE_STAGE_2_COPY_BIT;

    if (sync & D3D12_BARRIER_SYNC_RESOLVE)
    {
        stages |= VK_PIPELINE_STAGE_2_COPY_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT |
                VK_PIPELINE_STAGE_2_RESOLVE_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    }

    if (sync & D3D12_BARRIER_SYNC_EXECUTE_INDIRECT) /* PREDICATION is alias for EXECUTE_INDIRECT */
    {
        stages |= VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        /* We might use explicit preprocess. */
        if (list->device->device_info.device_generated_commands_features_ext.deviceGeneratedCommands ||
                list->device->device_info.device_generated_commands_features_nv.deviceGeneratedCommands)
        {
            stages |= VK_PIPELINE_STAGE_2_COMMAND_PREPROCESS_BIT_EXT;
        }
    }

    if (sync & D3D12_BARRIER_SYNC_COPY_RAYTRACING_ACCELERATION_STRUCTURE)
        stages |= VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_COPY_BIT_KHR;
    if (sync & D3D12_BARRIER_SYNC_BUILD_RAYTRACING_ACCELERATION_STRUCTURE)
        stages |= VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR;
    if (sync & D3D12_BARRIER_SYNC_EMIT_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO)
    {
        /* Somewhat awkward, but in legacy barriers we have to consider that UNORDERED_ACCESS is used to handle
         * postinfo barrier. See vkd3d_acceleration_structure_end_barrier which broadcasts the barrier to ALL_COMMANDS
         * as a workaround.
         * Application will use UNORDERED_ACCESS access flag, which means we cannot use STAGE_2_COPY_BIT here. */
        stages |= VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    }

    return stages;
}

static VkAccessFlags2 vk_access_flags_from_d3d12_barrier(struct d3d12_command_list *list, D3D12_BARRIER_ACCESS access)
{
    VkAccessFlags2 vk_access = 0;

    if (access == D3D12_BARRIER_ACCESS_COMMON)
        return VK_ACCESS_2_MEMORY_WRITE_BIT | VK_ACCESS_2_MEMORY_READ_BIT;
    if (access == D3D12_BARRIER_ACCESS_NO_ACCESS)
        return VK_ACCESS_2_NONE;

    if (access & D3D12_BARRIER_ACCESS_VERTEX_BUFFER)
        vk_access |= VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT;
    if (access & D3D12_BARRIER_ACCESS_CONSTANT_BUFFER)
        vk_access |= VK_ACCESS_2_UNIFORM_READ_BIT;
    if (access & D3D12_BARRIER_ACCESS_INDEX_BUFFER)
        vk_access |= VK_ACCESS_2_INDEX_READ_BIT;
    if (access & D3D12_BARRIER_ACCESS_RENDER_TARGET)
        vk_access |= VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT;
    if (access & D3D12_BARRIER_ACCESS_UNORDERED_ACCESS)
        vk_access |= VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT;
    if (access & D3D12_BARRIER_ACCESS_DEPTH_STENCIL_READ)
        vk_access |= VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
    if (access & D3D12_BARRIER_ACCESS_DEPTH_STENCIL_WRITE)
        vk_access |= VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    if (access & D3D12_BARRIER_ACCESS_SHADER_RESOURCE)
        vk_access |= VK_ACCESS_2_SHADER_READ_BIT;
    if (access & D3D12_BARRIER_ACCESS_STREAM_OUTPUT)
    {
        vk_access |= VK_ACCESS_2_TRANSFORM_FEEDBACK_WRITE_BIT_EXT |
                VK_ACCESS_2_TRANSFORM_FEEDBACK_COUNTER_READ_BIT_EXT |
                VK_ACCESS_2_TRANSFORM_FEEDBACK_COUNTER_WRITE_BIT_EXT;
    }
    if (access & D3D12_BARRIER_ACCESS_INDIRECT_ARGUMENT)
    {
        /* Add SHADER_READ_BIT here since we might read the indirect buffer in compute for
         * patching reasons. */
        vk_access |= VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_2_SHADER_READ_BIT;

        /* We might use preprocessing. */
        if (list->device->device_info.device_generated_commands_features_ext.deviceGeneratedCommands ||
                list->device->device_info.device_generated_commands_features_nv.deviceGeneratedCommands)
            vk_access |= VK_ACCESS_2_COMMAND_PREPROCESS_READ_BIT_EXT;
    }

    if (access & D3D12_BARRIER_ACCESS_COPY_DEST)
        vk_access |= VK_ACCESS_2_TRANSFER_WRITE_BIT;
    if (access & D3D12_BARRIER_ACCESS_COPY_SOURCE)
        vk_access |= VK_ACCESS_2_TRANSFER_READ_BIT;
    if (access & D3D12_BARRIER_ACCESS_RESOLVE_DEST)
        vk_access |= VK_ACCESS_2_TRANSFER_WRITE_BIT | VK_ACCESS_2_SHADER_WRITE_BIT;
    if (access & D3D12_BARRIER_ACCESS_RESOLVE_SOURCE)
        vk_access |= VK_ACCESS_2_TRANSFER_READ_BIT | VK_ACCESS_2_SHADER_READ_BIT;
    if (access & D3D12_BARRIER_ACCESS_RAYTRACING_ACCELERATION_STRUCTURE_READ)
        vk_access |= VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR;
    if (access & D3D12_BARRIER_ACCESS_RAYTRACING_ACCELERATION_STRUCTURE_WRITE)
        vk_access |= VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
    if (access & D3D12_BARRIER_ACCESS_SHADING_RATE_SOURCE)
        vk_access |= VK_ACCESS_2_FRAGMENT_SHADING_RATE_ATTACHMENT_READ_BIT_KHR;

    return vk_access;
}

static VkPipelineStageFlags2 vk_sanitize_stage_flags_for_access(struct d3d12_command_list *list,
        VkPipelineStageFlags2 stages, VkAccessFlags2 access)
{
/* We can't use Vulkan's 64-bit constants to initialize an array since they
 * are not constant according to C rules, so use a macro instead. */
#define HANDLE_ACCESS(a, b)                 \
    do                                      \
    {                                       \
        if (access & (a))                   \
        {                                   \
            if (!(stages & (b)))            \
                stages |= (b);              \
            access &= ~(a);                 \
        }                                   \
    } while (0)

    VkPipelineStageFlags2 queue_shader_stages = vk_queue_shader_stages(list->device, list->vk_queue_flags);

    const VkAccessFlags2 all_shader_access = VK_ACCESS_2_UNIFORM_READ_BIT | VK_ACCESS_2_SHADER_SAMPLED_READ_BIT |
            VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT |  VK_ACCESS_2_SHADER_STORAGE_READ_BIT |
            VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT | VK_ACCESS_2_DESCRIPTOR_BUFFER_READ_BIT_EXT;

    const VkAccessFlags2 all_graphics_access =
            VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_2_INDEX_READ_BIT | VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT |
            VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT |
            VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
            VK_ACCESS_2_TRANSFORM_FEEDBACK_WRITE_BIT_EXT | VK_ACCESS_2_TRANSFORM_FEEDBACK_COUNTER_READ_BIT_EXT |
            VK_ACCESS_2_TRANSFORM_FEEDBACK_COUNTER_WRITE_BIT_EXT | VK_ACCESS_2_CONDITIONAL_RENDERING_READ_BIT_EXT |
            VK_ACCESS_2_FRAGMENT_SHADING_RATE_ATTACHMENT_READ_BIT_KHR;

    const VkAccessFlags2 all_transfer_access =
            VK_ACCESS_2_TRANSFER_READ_BIT | VK_ACCESS_2_TRANSFER_WRITE_BIT;

    const VkAccessFlags2 all_host_access =
            VK_ACCESS_2_HOST_READ_BIT | VK_ACCESS_2_HOST_WRITE_BIT;

    /* If any of the meta stage flags are set, ignore any access that is
     * already accounted for. */
    if (stages & VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT)
    {
        access &= all_host_access;
    }
    else
    {
        if (stages & VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT)
            access &= ~(all_graphics_access | all_shader_access);
        if (stages & VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT)
            access &= ~all_transfer_access;
    }

    /* Ensure that for each access flag, we have at least one stage that supports
     * the given access type. If not, be conservative and add all stages that may
     * perform that access. */
    if (access & (VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT))
    {
        if (!(stages & ~VK_PIPELINE_STAGE_2_HOST_BIT))
            stages |= VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;

        access &= ~(VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT);
    }

    HANDLE_ACCESS(all_shader_access, queue_shader_stages);

    HANDLE_ACCESS(VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT,
            VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT);

    HANDLE_ACCESS(VK_ACCESS_2_INDEX_READ_BIT,
            VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT);

    HANDLE_ACCESS(VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT,
            VK_PIPELINE_STAGE_2_VERTEX_ATTRIBUTE_INPUT_BIT);

    HANDLE_ACCESS(VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT);

    HANDLE_ACCESS(VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT);

    HANDLE_ACCESS(VK_ACCESS_2_TRANSFER_READ_BIT | VK_ACCESS_2_TRANSFER_WRITE_BIT,
            VK_PIPELINE_STAGE_2_COPY_BIT | VK_PIPELINE_STAGE_2_BLIT_BIT |
            VK_PIPELINE_STAGE_2_CLEAR_BIT | VK_PIPELINE_STAGE_2_RESOLVE_BIT);

    HANDLE_ACCESS(VK_ACCESS_2_HOST_READ_BIT | VK_ACCESS_2_HOST_WRITE_BIT,
            VK_PIPELINE_STAGE_2_HOST_BIT);

    HANDLE_ACCESS(VK_ACCESS_2_TRANSFORM_FEEDBACK_WRITE_BIT_EXT |
            VK_ACCESS_2_TRANSFORM_FEEDBACK_COUNTER_READ_BIT_EXT |
            VK_ACCESS_2_TRANSFORM_FEEDBACK_COUNTER_WRITE_BIT_EXT,
            VK_PIPELINE_STAGE_2_TRANSFORM_FEEDBACK_BIT_EXT);

    HANDLE_ACCESS(VK_ACCESS_2_CONDITIONAL_RENDERING_READ_BIT_EXT,
            VK_PIPELINE_STAGE_2_CONDITIONAL_RENDERING_BIT_EXT);

    HANDLE_ACCESS(VK_ACCESS_2_COMMAND_PREPROCESS_READ_BIT_EXT |
            VK_ACCESS_2_COMMAND_PREPROCESS_WRITE_BIT_EXT,
            VK_PIPELINE_STAGE_2_COMMAND_PREPROCESS_BIT_EXT);

    HANDLE_ACCESS(VK_ACCESS_2_FRAGMENT_SHADING_RATE_ATTACHMENT_READ_BIT_KHR,
            VK_PIPELINE_STAGE_2_FRAGMENT_SHADING_RATE_ATTACHMENT_BIT_KHR);

    HANDLE_ACCESS(VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR,
            VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR |
            VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_COPY_BIT_KHR |
            queue_shader_stages);

    HANDLE_ACCESS(VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR,
            VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR |
            VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_COPY_BIT_KHR);

    HANDLE_ACCESS(VK_ACCESS_2_SHADER_BINDING_TABLE_READ_BIT_KHR,
            VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR);

    if (access)
        FIXME("Unhandled access flags %#"PRIx64".\n", access);

    return stages;
#undef HANDLE_ACCESS
}

static void d3d12_command_list_process_enhanced_barrier_global(struct d3d12_command_list *list,
        struct d3d12_command_list_barrier_batch *batch, const D3D12_GLOBAL_BARRIER *barrier)
{
    VkPipelineStageFlags2 src_stages, dst_stages;
    VkAccessFlags2 src_access, dst_access;

    if (barrier->SyncBefore == D3D12_BARRIER_SYNC_SPLIT || barrier->SyncAfter == D3D12_BARRIER_SYNC_SPLIT)
    {
        WARN("Split barrier not allowed for GLOBAL barriers.\n");
        return;
    }

    VKD3D_BREADCRUMB_AUX32(barrier->SyncBefore);
    VKD3D_BREADCRUMB_AUX32(barrier->AccessBefore);
    VKD3D_BREADCRUMB_AUX32(barrier->SyncAfter);
    VKD3D_BREADCRUMB_AUX32(barrier->AccessAfter);
    VKD3D_BREADCRUMB_TAG("Global Barrier [SyncBefore, AccessBefore, SyncAfter, AccessAfter]");

    src_stages = vk_stage_flags_from_d3d12_barrier(list, barrier->SyncBefore, barrier->AccessBefore);
    dst_stages = vk_stage_flags_from_d3d12_barrier(list, barrier->SyncAfter, barrier->AccessAfter);
    src_access = vk_access_flags_from_d3d12_barrier(list, barrier->AccessBefore);
    dst_access = vk_access_flags_from_d3d12_barrier(list, barrier->AccessAfter);

    src_stages = vk_sanitize_stage_flags_for_access(list, src_stages, src_access);
    dst_stages = vk_sanitize_stage_flags_for_access(list, dst_stages, dst_access);

    if (d3d12_barrier_invalidates_indirect_arguments(barrier->SyncAfter, barrier->AccessAfter))
    {
        d3d12_command_list_debug_mark_label(list, "Indirect Argument barrier", 1.0f, 1.0f, 0.0f, 1.0f);
        /* Any indirect patching commands now have to go to normal command buffer, unless we split the sequence. */
        list->cmd.vk_init_commands_post_indirect_barrier = list->cmd.vk_command_buffer;
    }

    if (barrier->SyncBefore & (D3D12_BARRIER_SYNC_ALL | D3D12_BARRIER_SYNC_BUILD_RAYTRACING_ACCELERATION_STRUCTURE))
        d3d12_command_list_flush_rtas_batch(list);

    d3d12_command_list_merge_copy_tracking_global_barrier(list, barrier, batch);
    d3d12_command_list_barrier_batch_add_global_transition(list, batch,
            src_stages, src_access, dst_stages, dst_access);
}

static void d3d12_command_list_process_enhanced_barrier_buffer(struct d3d12_command_list *list,
        struct d3d12_command_list_barrier_batch *batch, const D3D12_BUFFER_BARRIER *barrier)
{
    /* We get very little out of buffer barriers since the stage flags are more granular now. */
    D3D12_GLOBAL_BARRIER global;
    global.SyncBefore = barrier->SyncBefore;
    global.SyncAfter = barrier->SyncAfter;
    global.AccessBefore = barrier->AccessBefore;
    global.AccessAfter = barrier->AccessAfter;
    d3d12_command_list_process_enhanced_barrier_global(list, batch, &global);
}

static void d3d12_command_list_process_enhanced_barrier_texture(struct d3d12_command_list *list,
        struct d3d12_command_list_barrier_batch *batch, const D3D12_TEXTURE_BARRIER *barrier)
{
    VkImageSubresourceLayers vk_subresource_layers;
    VkImageMemoryBarrier2 vk_transition;
    D3D12_GLOBAL_BARRIER global_barrier;
    struct d3d12_resource *resource;
    uint32_t dsv_decay_mask = 0;
    bool discarding_transition;
    unsigned int i;

    if (barrier->SyncBefore == D3D12_BARRIER_SYNC_SPLIT || barrier->SyncAfter == D3D12_BARRIER_SYNC_SPLIT)
        WARN("Split barriers are known to be broken on D3D12 native runtime.\n");

    if (!barrier->pResource)
    {
        WARN("No pResource.\n");
        return;
    }

    resource = impl_from_ID3D12Resource(barrier->pResource);
    if (!resource || !d3d12_resource_is_texture(resource))
    {
        WARN("Resource is not a texture.\n");
        return;
    }

    /* Split barrier. Defer this until SyncBefore = SPLIT. See notes in sync flag translation. */
    if (barrier->SyncAfter == D3D12_BARRIER_SYNC_SPLIT)
        return;

    /* This is a no-op, but zero array planes is not a noop for some bizarre reason. */
    if (barrier->Subresources.NumMipLevels != 0 && barrier->Subresources.NumPlanes == 0)
    {
        WARN("No-op texture barrier due to NumPlanes == 0.\n");
        return;
    }

    if (barrier->Subresources.NumMipLevels != 0 && barrier->Subresources.NumArraySlices == 0)
        WARN("NumArraySlices == 0 promotes to 1 slice.\n");

    global_barrier.SyncBefore = barrier->SyncBefore;
    global_barrier.SyncAfter = barrier->SyncAfter;
    global_barrier.AccessBefore = barrier->AccessBefore;
    global_barrier.AccessAfter = barrier->AccessAfter;
    d3d12_command_list_merge_copy_tracking_global_barrier(list, &global_barrier, batch);

    vk_transition.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    vk_transition.pNext = NULL;
    vk_transition.oldLayout = vk_image_layout_from_d3d12_barrier(list, resource, barrier->LayoutBefore);

    if ((resource->desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL) &&
            barrier->LayoutBefore != barrier->LayoutAfter)
    {
        dsv_decay_mask = d3d12_command_list_notify_dsv_state_enhanced(list, resource, barrier);
    }

    vk_image_memory_barrier_subresources_from_d3d12_texture_barrier(list, resource,
            &barrier->Subresources, dsv_decay_mask, &vk_transition.subresourceRange);

    vk_transition.newLayout = vk_image_layout_from_d3d12_barrier(list, resource, barrier->LayoutAfter);

    /* Aliasing is a lot trickier with enhanced barriers.
     * If aliasing is possible, i.e. there are memory flushes on the resource,
     * we must resolve subresource updates now.
     * Aliasing is possible if we flush and ensure some kind of execution barrier with the consumer. */
    if ((resource->flags & VKD3D_RESOURCE_LINEAR_STAGING_COPY) &&
            barrier->AccessBefore != D3D12_BARRIER_ACCESS_NO_ACCESS &&
            d3d12_resource_may_alias_other_resources(resource))
    {
        vk_subresource_layers.baseArrayLayer = vk_transition.subresourceRange.baseArrayLayer;
        vk_subresource_layers.layerCount = vk_transition.subresourceRange.layerCount;
        vk_subresource_layers.aspectMask = vk_transition.subresourceRange.aspectMask;
        for (i = 0; i < vk_transition.subresourceRange.levelCount; i++)
        {
            vk_subresource_layers.mipLevel = i + vk_transition.subresourceRange.baseMipLevel;
            d3d12_command_list_update_subresource_data(list, resource, vk_subresource_layers);
        }
        d3d12_command_list_flush_subresource_updates(list);
    }

    vk_transition.srcStageMask = vk_stage_flags_from_d3d12_barrier(list, barrier->SyncBefore, barrier->AccessBefore);
    vk_transition.dstStageMask = vk_stage_flags_from_d3d12_barrier(list, barrier->SyncAfter, barrier->AccessAfter);
    vk_transition.srcAccessMask = vk_access_flags_from_d3d12_barrier(list, barrier->AccessBefore);
    vk_transition.dstAccessMask = vk_access_flags_from_d3d12_barrier(list, barrier->AccessAfter);
    vk_transition.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    vk_transition.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    vk_transition.image = resource->res.vk_image;

    /* All COPY operations on images do their own barriers, so we don't have to explicitly flush or invalidate. */
    vk_transition.srcAccessMask &= ~(VK_ACCESS_2_TRANSFER_WRITE_BIT | VK_ACCESS_2_TRANSFER_READ_BIT);
    vk_transition.dstAccessMask &= ~(VK_ACCESS_2_TRANSFER_WRITE_BIT | VK_ACCESS_2_TRANSFER_READ_BIT);

    vk_transition.srcStageMask = vk_sanitize_stage_flags_for_access(list, vk_transition.srcStageMask, vk_transition.srcAccessMask);
    vk_transition.dstStageMask = vk_sanitize_stage_flags_for_access(list, vk_transition.dstStageMask, vk_transition.dstAccessMask);

    /* This works like a "deactivating" discard.
     * The behavior around UNDEFINED layout in D3D12 is ... not well explained in the docs.
     * The basic idea for aliasing seems to be that you should transition to UNDEFINED + NO_ACCESS to "deactivate",
     * and the activating resource will transition from UNDEFINED.
     * Using NO_ACCESS seems to imply that the resource can be invalidated (i.e. any data in caches can vanish),
     * so any subsequent transition from UNDEFINED should be able to discard, regardless of the DISCARD flag.
     * For deactivation, we will perform no layout transition. In sync2, this is done by setting newLayout == oldLayout. */
    if (vk_transition.newLayout == VK_IMAGE_LAYOUT_UNDEFINED)
        vk_transition.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    if (barrier->LayoutBefore == D3D12_BARRIER_LAYOUT_UNDEFINED && !(barrier->Flags & D3D12_TEXTURE_BARRIER_FLAG_DISCARD))
        FIXME("Transitioning away from UNDEFINED, but there is no DISCARD flag. Uncertain what is expected here. vkd3d-proton will force a discard.\n");

    d3d12_command_list_barrier_batch_add_layout_transition(list, batch, &vk_transition);

    discarding_transition = vk_transition.oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
            vk_transition.newLayout != VK_IMAGE_LAYOUT_UNDEFINED &&
            d3d12_barrier_subresource_range_covers_resource(resource, &barrier->Subresources);
    d3d12_command_list_track_resource_usage(list, resource, !discarding_transition);
}

static void STDMETHODCALLTYPE d3d12_command_list_Barrier(d3d12_command_list_iface *iface, UINT32 NumBarrierGroups, const D3D12_BARRIER_GROUP *pBarrierGroups)
{
    struct d3d12_command_list *list = impl_from_ID3D12GraphicsCommandList(iface);
    struct d3d12_command_list_barrier_batch batch;
    unsigned int barrier_group_index;
    const D3D12_BARRIER_GROUP *group;
    unsigned int i, num_barriers;

    TRACE("iface %p, NumBarrierGroups %u, D3D12_BARRIER_GROUP %p\n",
            iface, NumBarrierGroups, pBarrierGroups);

    d3d12_command_list_end_current_render_pass(list, false);
    d3d12_command_list_end_transfer_batch(list);
    d3d12_command_list_barrier_batch_init(&batch);

    d3d12_command_list_debug_mark_begin_region(list, "Barrier");

    for (barrier_group_index = 0; barrier_group_index < NumBarrierGroups; barrier_group_index++)
    {
        group = &pBarrierGroups[barrier_group_index];
        num_barriers = group->NumBarriers;

        switch (group->Type)
        {
            case D3D12_BARRIER_TYPE_GLOBAL:
                for (i = 0; i < num_barriers; i++)
                    d3d12_command_list_process_enhanced_barrier_global(list, &batch, &group->pGlobalBarriers[i]);

                /* GLOBAL barrier can be used as a global aliasing barrier for linear staging images as well.
                 * Flush out these now. */
                if (num_barriers)
                    d3d12_command_list_flush_subresource_updates(list);
                break;

            case D3D12_BARRIER_TYPE_BUFFER:
                for (i = 0; i < num_barriers; i++)
                    d3d12_command_list_process_enhanced_barrier_buffer(list, &batch, &group->pBufferBarriers[i]);
                break;

            case D3D12_BARRIER_TYPE_TEXTURE:
                for (i = 0; i < num_barriers; i++)
                    d3d12_command_list_process_enhanced_barrier_texture(list, &batch, &group->pTextureBarriers[i]);
                break;

            default:
                WARN("Unknown barrier group type %u.\n", group->Type);
                break;
        }
    }

    d3d12_command_list_barrier_batch_end(list, &batch);
    VKD3D_BREADCRUMB_COMMAND(BARRIER);
    d3d12_command_list_debug_mark_end_region(list);
}

static void STDMETHODCALLTYPE d3d12_command_list_OMSetFrontAndBackStencilRef(d3d12_command_list_iface *iface, UINT FrontStencilRef, UINT BackStencilRef)
{
    struct d3d12_command_list *list = impl_from_ID3D12GraphicsCommandList(iface);
    struct vkd3d_dynamic_state *dyn_state = &list->dynamic_state;

    TRACE("iface %p, FrontStencilRef %u, BackStencilRef %u.\n",
        iface, FrontStencilRef, BackStencilRef);

    if (dyn_state->stencil_front.reference != FrontStencilRef || dyn_state->stencil_back.reference != BackStencilRef)
    {
        dyn_state->stencil_front.reference = FrontStencilRef;
        dyn_state->stencil_back.reference = BackStencilRef;
        dyn_state->dirty_flags |= VKD3D_DYNAMIC_STATE_STENCIL_REFERENCE;
    }
}

static void STDMETHODCALLTYPE d3d12_command_list_RSSetDepthBias(d3d12_command_list_iface *iface, FLOAT DepthBias, FLOAT DepthBiasClamp, FLOAT SlopeScaledDepthBias)
{
    struct d3d12_command_list *list = impl_from_ID3D12GraphicsCommandList(iface);
    struct vkd3d_dynamic_state *dyn_state = &list->dynamic_state;

    TRACE("iface %p, DepthBias %f, DepthBiasClamp %f, SlopeScaledDepthBias %f.\n",
        iface, DepthBias, DepthBiasClamp, SlopeScaledDepthBias);

    if (dyn_state->depth_bias.constant_factor != DepthBias || dyn_state->depth_bias.clamp != DepthBiasClamp ||
            dyn_state->depth_bias.slope_factor != SlopeScaledDepthBias)
    {
        dyn_state->depth_bias.constant_factor = DepthBias;
        dyn_state->depth_bias.clamp = DepthBiasClamp;
        dyn_state->depth_bias.slope_factor = SlopeScaledDepthBias;
        dyn_state->dirty_flags |= VKD3D_DYNAMIC_STATE_DEPTH_BIAS;
    }
}

static void STDMETHODCALLTYPE d3d12_command_list_IASetIndexBufferStripCutValue(d3d12_command_list_iface *iface, D3D12_INDEX_BUFFER_STRIP_CUT_VALUE IBStripCutValue)
{
    struct d3d12_command_list *list = impl_from_ID3D12GraphicsCommandList(iface);
    struct vkd3d_dynamic_state *dyn_state = &list->dynamic_state;

    TRACE("iface %p, IBStripCutValue %u.\n", iface, IBStripCutValue);

    if (dyn_state->index_buffer_strip_cut_value != IBStripCutValue)
    {
        dyn_state->index_buffer_strip_cut_value = IBStripCutValue;
        dyn_state->dirty_flags |= VKD3D_DYNAMIC_STATE_PRIMITIVE_RESTART;
    }
}

static void STDMETHODCALLTYPE d3d12_command_list_SetProgram(d3d12_command_list_iface *iface, const D3D12_SET_PROGRAM_DESC *desc)
{
    FIXME("iface %p, desc %p, stub!\n", iface, desc);
}

static void STDMETHODCALLTYPE d3d12_command_list_DispatchGraph(d3d12_command_list_iface *iface, const D3D12_DISPATCH_GRAPH_DESC *desc)
{
    FIXME("iface %p, desc %p, stub!\n", iface, desc);
}

#define VKD3D_DECLARE_D3D12_GRAPHICS_COMMAND_LIST_VARIANT(name, set_table_variant) \
static CONST_VTBL struct ID3D12GraphicsCommandList10Vtbl d3d12_command_list_vtbl_##name = \
{ \
    /* IUnknown methods */ \
    d3d12_command_list_QueryInterface, \
    d3d12_command_list_AddRef, \
    d3d12_command_list_Release, \
    /* ID3D12Object methods */ \
    d3d12_command_list_GetPrivateData, \
    d3d12_command_list_SetPrivateData, \
    d3d12_command_list_SetPrivateDataInterface, \
    (void *)d3d12_object_SetName, \
    /* ID3D12DeviceChild methods */ \
    d3d12_command_list_GetDevice, \
    /* ID3D12CommandList methods */ \
    d3d12_command_list_GetType, \
    /* ID3D12GraphicsCommandList methods */ \
    d3d12_command_list_Close, \
    d3d12_command_list_Reset, \
    d3d12_command_list_ClearState, \
    d3d12_command_list_DrawInstanced, \
    d3d12_command_list_DrawIndexedInstanced, \
    d3d12_command_list_Dispatch, \
    d3d12_command_list_CopyBufferRegion, \
    d3d12_command_list_CopyTextureRegion, \
    d3d12_command_list_CopyResource, \
    d3d12_command_list_CopyTiles, \
    d3d12_command_list_ResolveSubresource, \
    d3d12_command_list_IASetPrimitiveTopology, \
    d3d12_command_list_RSSetViewports, \
    d3d12_command_list_RSSetScissorRects, \
    d3d12_command_list_OMSetBlendFactor, \
    d3d12_command_list_OMSetStencilRef, \
    d3d12_command_list_SetPipelineState, \
    d3d12_command_list_ResourceBarrier, \
    d3d12_command_list_ExecuteBundle, \
    d3d12_command_list_SetDescriptorHeaps, \
    d3d12_command_list_SetComputeRootSignature, \
    d3d12_command_list_SetGraphicsRootSignature, \
    d3d12_command_list_SetComputeRootDescriptorTable_##set_table_variant, \
    d3d12_command_list_SetGraphicsRootDescriptorTable_##set_table_variant, \
    d3d12_command_list_SetComputeRoot32BitConstant, \
    d3d12_command_list_SetGraphicsRoot32BitConstant, \
    d3d12_command_list_SetComputeRoot32BitConstants, \
    d3d12_command_list_SetGraphicsRoot32BitConstants, \
    d3d12_command_list_SetComputeRootConstantBufferView, \
    d3d12_command_list_SetGraphicsRootConstantBufferView, \
    d3d12_command_list_SetComputeRootShaderResourceView, \
    d3d12_command_list_SetGraphicsRootShaderResourceView, \
    d3d12_command_list_SetComputeRootUnorderedAccessView, \
    d3d12_command_list_SetGraphicsRootUnorderedAccessView, \
    d3d12_command_list_IASetIndexBuffer, \
    d3d12_command_list_IASetVertexBuffers, \
    d3d12_command_list_SOSetTargets, \
    d3d12_command_list_OMSetRenderTargets, \
    d3d12_command_list_ClearDepthStencilView, \
    d3d12_command_list_ClearRenderTargetView, \
    d3d12_command_list_ClearUnorderedAccessViewUint, \
    d3d12_command_list_ClearUnorderedAccessViewFloat, \
    d3d12_command_list_DiscardResource, \
    d3d12_command_list_BeginQuery, \
    d3d12_command_list_EndQuery, \
    d3d12_command_list_ResolveQueryData, \
    d3d12_command_list_SetPredication, \
    d3d12_command_list_SetMarker, \
    d3d12_command_list_BeginEvent, \
    d3d12_command_list_EndEvent, \
    d3d12_command_list_ExecuteIndirect, \
    /* ID3D12GraphicsCommandList1 methods */ \
    d3d12_command_list_AtomicCopyBufferUINT, \
    d3d12_command_list_AtomicCopyBufferUINT64, \
    d3d12_command_list_OMSetDepthBounds, \
    d3d12_command_list_SetSamplePositions, \
    d3d12_command_list_ResolveSubresourceRegion, \
    d3d12_command_list_SetViewInstanceMask, \
    /* ID3D12GraphicsCommandList2 methods */ \
    d3d12_command_list_WriteBufferImmediate, \
    /* ID3D12GraphicsCommandList3 methods */ \
    d3d12_command_list_SetProtectedResourceSession, \
    /* ID3D12GraphicsCommandList4 methods */ \
    d3d12_command_list_BeginRenderPass, \
    d3d12_command_list_EndRenderPass, \
    d3d12_command_list_InitializeMetaCommand, \
    d3d12_command_list_ExecuteMetaCommand, \
    d3d12_command_list_BuildRaytracingAccelerationStructure, \
    d3d12_command_list_EmitRaytracingAccelerationStructurePostbuildInfo, \
    d3d12_command_list_CopyRaytracingAccelerationStructure, \
    d3d12_command_list_SetPipelineState1, \
    d3d12_command_list_DispatchRays, \
    /* ID3D12GraphicsCommandList5 methods */ \
    d3d12_command_list_RSSetShadingRate, \
    d3d12_command_list_RSSetShadingRateImage, \
    /* ID3D12GraphicsCommandList6 methods */ \
    d3d12_command_list_DispatchMesh, \
    /* ID3D12GraphicsCommandList7 methods */ \
    d3d12_command_list_Barrier, \
    /* ID3D12GraphicsCommandList8 methods */ \
    d3d12_command_list_OMSetFrontAndBackStencilRef, \
    /* ID3D12GraphicsCommandList9 methods */ \
    d3d12_command_list_RSSetDepthBias, \
    d3d12_command_list_IASetIndexBufferStripCutValue, \
    /* ID3D12GraphicsCommandList10 methods */ \
    d3d12_command_list_SetProgram, \
    d3d12_command_list_DispatchGraph, \
}

VKD3D_DECLARE_D3D12_GRAPHICS_COMMAND_LIST_VARIANT(default, default);
VKD3D_DECLARE_D3D12_GRAPHICS_COMMAND_LIST_VARIANT(embedded_64_16, embedded_64_16);
VKD3D_DECLARE_D3D12_GRAPHICS_COMMAND_LIST_VARIANT(embedded_32_16, embedded_32_16);
VKD3D_DECLARE_D3D12_GRAPHICS_COMMAND_LIST_VARIANT(embedded_default, embedded_default);

#ifdef VKD3D_ENABLE_PROFILING
#include "command_list_profiled.h"
#endif

static struct d3d12_command_list *unsafe_impl_from_ID3D12CommandList(ID3D12CommandList *iface)
{
    if (!iface)
        return NULL;
    return CONTAINING_RECORD(iface, struct d3d12_command_list, ID3D12GraphicsCommandList_iface);
}

extern CONST_VTBL struct ID3D12GraphicsCommandListExt1Vtbl d3d12_command_list_vkd3d_ext_vtbl;

static void d3d12_command_list_init_attachment_info(VkRenderingAttachmentInfo *attachment_info)
{
    attachment_info->sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    attachment_info->loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    attachment_info->storeOp = VK_ATTACHMENT_STORE_OP_STORE;
}

static void d3d12_command_list_init_rendering_info(struct d3d12_device *device, struct vkd3d_rendering_info *rendering_info)
{
    unsigned int i;

    rendering_info->info.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    rendering_info->info.colorAttachmentCount = D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT;
    rendering_info->info.pColorAttachments = rendering_info->rtv;

    for (i = 0; i < D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT; i++)
        d3d12_command_list_init_attachment_info(&rendering_info->rtv[i]);

    d3d12_command_list_init_attachment_info(&rendering_info->dsv);

    if (device->device_info.fragment_shading_rate_features.attachmentFragmentShadingRate)
    {
        uint32_t tile_size = d3d12_determine_shading_rate_image_tile_size(device);

        if (tile_size)
        {
            rendering_info->vrs.sType = VK_STRUCTURE_TYPE_RENDERING_FRAGMENT_SHADING_RATE_ATTACHMENT_INFO_KHR;
            rendering_info->vrs.shadingRateAttachmentTexelSize.width = tile_size;
            rendering_info->vrs.shadingRateAttachmentTexelSize.height = tile_size;

            vk_prepend_struct(&rendering_info->info, &rendering_info->vrs);
        }
    }
}

static HRESULT d3d12_command_list_init(struct d3d12_command_list *list, struct d3d12_device *device,
        D3D12_COMMAND_LIST_TYPE type)
{
    HRESULT hr;

    memset(list, 0, sizeof(*list));

#ifdef VKD3D_ENABLE_PROFILING
    if (vkd3d_uses_profiling())
        list->ID3D12GraphicsCommandList_iface.lpVtbl = &d3d12_command_list_vtbl_profiled;
    else
#endif
    {
        list->ID3D12GraphicsCommandList_iface.lpVtbl = &d3d12_command_list_vtbl_default;

        if (d3d12_device_use_embedded_mutable_descriptors(device))
        {
            /* Specialize SetDescriptorTable calls since we need different code paths for those,
             * and they are quite hot. */
            if (device->bindless_state.descriptor_buffer_cbv_srv_uav_size == 64 &&
                    device->bindless_state.descriptor_buffer_sampler_size == 16)
            {
                list->ID3D12GraphicsCommandList_iface.lpVtbl = &d3d12_command_list_vtbl_embedded_64_16;
            }
            else if (device->bindless_state.descriptor_buffer_cbv_srv_uav_size == 32 &&
                    device->bindless_state.descriptor_buffer_sampler_size == 16)
            {
                list->ID3D12GraphicsCommandList_iface.lpVtbl = &d3d12_command_list_vtbl_embedded_32_16;
            }
            else
            {
                list->ID3D12GraphicsCommandList_iface.lpVtbl = &d3d12_command_list_vtbl_embedded_default;
            }
        }
    }

    list->refcount = 1;

    list->type = type;

    list->ID3D12GraphicsCommandListExt_iface.lpVtbl = &d3d12_command_list_vkd3d_ext_vtbl;

    hash_map_init(&list->query_resolve_lut,
            vkd3d_query_lookup_entry_hash,
            vkd3d_query_lookup_entry_compare,
            sizeof(struct vkd3d_query_lookup_entry));

    d3d12_command_list_init_rendering_info(device, &list->rendering_info);

    if (FAILED(hr = vkd3d_private_store_init(&list->private_store)))
        return hr;

    d3d_destruction_notifier_init(&list->destruction_notifier, (IUnknown*)&list->ID3D12GraphicsCommandList_iface);
    d3d12_device_add_ref(list->device = device);
    return hr;
}

HRESULT d3d12_command_list_create(struct d3d12_device *device,
        UINT node_mask, D3D12_COMMAND_LIST_TYPE type, struct d3d12_command_list **list)
{
    struct d3d12_command_list *object;
    HRESULT hr;

    debug_ignored_node_mask(node_mask);

    /* We store RTV descriptors by value, which we align to 64 bytes, so d3d12_command_list inherits this requirement.
     * Otherwise ubsan complains. */
    if (!(object = vkd3d_malloc_aligned(sizeof(*object), D3D12_DESC_ALIGNMENT)))
        return E_OUTOFMEMORY;

    if (FAILED(hr = d3d12_command_list_init(object, device, type)))
    {
        vkd3d_free_aligned(object);
        return hr;
    }

    TRACE("Created command list %p.\n", object);

    *list = object;

    return S_OK;
}

struct d3d12_command_list *d3d12_command_list_from_iface(ID3D12CommandList *iface)
{
    bool is_valid = false;
    if (!iface)
        return NULL;

#ifdef VKD3D_ENABLE_PROFILING
    is_valid |= iface->lpVtbl == (struct ID3D12CommandListVtbl *)&d3d12_command_list_vtbl_profiled;
#endif

    /* A little annoying, but we only have to validate this on submission,
     * so the overhead is irrelevant. */
    is_valid |=
            iface->lpVtbl == (struct ID3D12CommandListVtbl *)&d3d12_command_list_vtbl_default ||
            iface->lpVtbl == (struct ID3D12CommandListVtbl *)&d3d12_command_list_vtbl_embedded_64_16 ||
            iface->lpVtbl == (struct ID3D12CommandListVtbl *)&d3d12_command_list_vtbl_embedded_32_16 ||
            iface->lpVtbl == (struct ID3D12CommandListVtbl *)&d3d12_command_list_vtbl_embedded_default;

    if (!is_valid)
        return NULL;

    return CONTAINING_RECORD(iface, struct d3d12_command_list, ID3D12GraphicsCommandList_iface);
}

/* ID3D12CommandQueue */
extern ULONG STDMETHODCALLTYPE d3d12_command_queue_vkd3d_ext_AddRef(d3d12_command_queue_vkd3d_ext_iface *iface);

static inline struct d3d12_command_queue *impl_from_ID3D12CommandQueue(ID3D12CommandQueue *iface)
{
    return CONTAINING_RECORD(iface, struct d3d12_command_queue, ID3D12CommandQueue_iface);
}

HRESULT STDMETHODCALLTYPE d3d12_command_queue_QueryInterface(ID3D12CommandQueue *iface,
        REFIID riid, void **object)
{
    struct d3d12_command_queue *command_queue = impl_from_ID3D12CommandQueue(iface);

    TRACE("iface %p, riid %s, object %p.\n", iface, debugstr_guid(riid), object);

    if (!object)
        return E_POINTER;

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

    if (IsEqualGUID(riid, &IID_ID3D12CommandQueueExt))
    {
        d3d12_command_queue_vkd3d_ext_AddRef(&command_queue->ID3D12CommandQueueExt_iface);
        *object = &command_queue->ID3D12CommandQueueExt_iface;
        return S_OK;
    }

    if (IsEqualGUID(riid, &IID_IDXGIVkSwapChainFactory))
    {
        IDXGIVkSwapChainFactory_AddRef(&command_queue->vk_swap_chain_factory.IDXGIVkSwapChainFactory_iface);
        *object = &command_queue->vk_swap_chain_factory;
        return S_OK;
    }

    if (IsEqualGUID(riid, &IID_ID3DDestructionNotifier))
    {
        ID3DDestructionNotifier_AddRef(&command_queue->destruction_notifier.ID3DDestructionNotifier_iface);
        *object = &command_queue->destruction_notifier.ID3DDestructionNotifier_iface;
        return S_OK;
    }

    WARN("%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid(riid));

    *object = NULL;
    return E_NOINTERFACE;
}

ULONG STDMETHODCALLTYPE d3d12_command_queue_AddRef(ID3D12CommandQueue *iface)
{
    struct d3d12_command_queue *command_queue = impl_from_ID3D12CommandQueue(iface);
    ULONG refcount = InterlockedIncrement(&command_queue->refcount);

    TRACE("%p increasing refcount to %u.\n", command_queue, refcount);

    return refcount;
}

ULONG STDMETHODCALLTYPE d3d12_command_queue_Release(ID3D12CommandQueue *iface)
{
    struct d3d12_command_queue *command_queue = impl_from_ID3D12CommandQueue(iface);
    ULONG refcount = InterlockedDecrement(&command_queue->refcount);

    TRACE("%p decreasing refcount to %u.\n", command_queue, refcount);

    if (!refcount)
    {
        struct d3d12_device *device = command_queue->device;

        d3d_destruction_notifier_free(&command_queue->destruction_notifier);
        vkd3d_private_store_destroy(&command_queue->private_store);

        d3d12_command_queue_submit_stop(command_queue);

        pthread_join(command_queue->submission_thread, NULL);
        pthread_mutex_destroy(&command_queue->queue_lock);
        pthread_cond_destroy(&command_queue->queue_cond);

        d3d12_device_unmap_vkd3d_queue(command_queue->vkd3d_queue, command_queue);

        vkd3d_fence_worker_stop(&command_queue->fence_worker, device);

        vkd3d_free(command_queue->submissions);
        vkd3d_free(command_queue->wait_semaphores);
        vkd3d_free(command_queue->wait_fences);
        vkd3d_free(command_queue->sparse.buffer_binds);
        vkd3d_free(command_queue->sparse.image_binds);
        vkd3d_free(command_queue->sparse.image_opaque_binds);
        vkd3d_free(command_queue->sparse.tracked);
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

    return vkd3d_set_private_data(&command_queue->private_store, guid, data_size, data,
            NULL, NULL);
}

static HRESULT STDMETHODCALLTYPE d3d12_command_queue_SetPrivateDataInterface(ID3D12CommandQueue *iface,
        REFGUID guid, const IUnknown *data)
{
    struct d3d12_command_queue *command_queue = impl_from_ID3D12CommandQueue(iface);

    TRACE("iface %p, guid %s, data %p.\n", iface, debugstr_guid(guid), data);

    return vkd3d_set_private_data_interface(&command_queue->private_store, guid, data,
            NULL, NULL);
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
    unsigned int tile_index;

    if (coord->Subresource >= sparse->tiling_count)
        return VKD3D_INVALID_TILE_INDEX;

    if (!size->UseBox)
    {
        /* Tiles are already ordered by subresource and coordinates correctly,
         * so we can just add the tile index to the region's base index */
        tile_index = vkd3d_get_tile_index_from_coordinate(sparse, coord) + tile_index_in_region;
    }
    else
    {
        D3D12_TILED_RESOURCE_COORDINATE box_coord = *coord;
        box_coord.X += (tile_index_in_region % size->Width);
        box_coord.Y += (tile_index_in_region / size->Width) % size->Height;
        box_coord.Z += (tile_index_in_region / (size->Width * size->Height));

        tile_index = vkd3d_get_tile_index_from_coordinate(sparse, &box_coord);
    }

    return tile_index < sparse->tile_count ? tile_index : VKD3D_INVALID_TILE_INDEX;
}

static void STDMETHODCALLTYPE d3d12_command_queue_UpdateTileMappings(ID3D12CommandQueue *iface,
        ID3D12Resource *resource, UINT region_count, const D3D12_TILED_RESOURCE_COORDINATE *region_coords,
        const D3D12_TILE_REGION_SIZE *region_sizes, ID3D12Heap *heap, UINT range_count,
        const D3D12_TILE_RANGE_FLAGS *range_flags,
        const UINT *heap_range_offsets, const UINT *range_tile_counts,
        D3D12_TILE_MAPPING_FLAGS flags)
{
    struct d3d12_command_queue *command_queue = impl_from_ID3D12CommandQueue(iface);
    unsigned int region_tile = 0, region_idx = 0, range_tile = 0, range_idx = 0;
    struct d3d12_resource *res = impl_from_ID3D12Resource(resource);
    struct d3d12_heap *memory_heap = impl_from_ID3D12Heap(heap);
    struct d3d12_sparse_info *sparse = &res->sparse;
    D3D12_TILED_RESOURCE_COORDINATE region_coord;
    struct d3d12_command_queue_submission sub;
    struct vkd3d_sparse_memory_bind *bind;
    D3D12_TILE_REGION_SIZE region_size;
    D3D12_TILE_RANGE_FLAGS range_flag;
    UINT range_size, range_offset;
    size_t bind_infos_size = 0;
    VkDeviceSize heap_offset;
    uint32_t *bound_tiles;
    unsigned int i;

    TRACE("iface %p, resource %p, region_count %u, region_coords %p, "
            "region_sizes %p, heap %p, range_count %u, range_flags %p, heap_range_offsets %p, "
            "range_tile_counts %p, flags %#x.\n",
            iface, resource, region_count, region_coords, region_sizes, heap,
            range_count, range_flags, heap_range_offsets, range_tile_counts, flags);

    /* This can be a fallback sparse resource, just ignore any UpdateTileMapping calls on this. */
    if (!(res->flags & VKD3D_RESOURCE_RESERVED))
    {
        WARN("Ignoring UpdateTileMapping calls on fallback reserved resource.\n");
        return;
    }

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

    range_flag = D3D12_TILE_RANGE_FLAG_NONE;
    range_size = ~0u;
    range_offset = 0;

    if (!(bound_tiles = vkd3d_calloc(sparse->tile_count, sizeof(*bound_tiles))))
    {
        ERR("Failed to allocate tile mapping table.\n");
        return;
    }

    for (i = 0; i < sparse->tile_count; i++)
        bound_tiles[i] = VKD3D_INVALID_TILE_INDEX;

    while (region_idx < region_count && range_idx < range_count)
    {
        if (range_tile == 0)
        {
            if (range_flags)
            {
                range_flag = range_flags[range_idx];

                if (range_flag == D3D12_TILE_RANGE_FLAG_NULL &&
                        (vkd3d_config_flags & VKD3D_CONFIG_FLAG_SKIP_NULL_SPARSE_TILES) &&
                        command_queue->device->workarounds.amdgpu_broken_null_tile_mapping)
                {
                    range_flag = D3D12_TILE_RANGE_FLAG_SKIP;
                }
            }

            if (range_tile_counts)
                range_size = range_tile_counts[range_idx];

            if (heap_range_offsets)
                range_offset = heap_range_offsets[range_idx];
        }

        if (region_tile == 0)
        {
            if (region_coords)
                region_coord = region_coords[region_idx];

            if (region_sizes)
                region_size = region_sizes[region_idx];
        }

        if (range_flag != D3D12_TILE_RANGE_FLAG_SKIP)
        {
            unsigned int tile_index = vkd3d_get_tile_index_from_region(sparse, &region_coord, &region_size, region_tile);

            if (tile_index != VKD3D_INVALID_TILE_INDEX)
            {
                if (bound_tiles[tile_index] == VKD3D_INVALID_TILE_INDEX)
                {
                    if (!vkd3d_array_reserve((void **)&sub.bind_sparse.bind_infos, &bind_infos_size,
                            sub.bind_sparse.bind_count + 1, sizeof(*sub.bind_sparse.bind_infos)))
                    {
                        ERR("Failed to allocate bind info array.\n");
                        goto fail;
                    }

                    bound_tiles[tile_index] = sub.bind_sparse.bind_count;
                    bind = &sub.bind_sparse.bind_infos[sub.bind_sparse.bind_count++];
                }
                else
                {
                    /* App binds the same tile multiple times, use last binding. */
                    bind = &sub.bind_sparse.bind_infos[bound_tiles[tile_index]];
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
                    heap_offset = range_flag == D3D12_TILE_RANGE_FLAG_REUSE_SINGLE_TILE
                            ? VKD3D_TILE_SIZE * range_offset
                            : VKD3D_TILE_SIZE * (range_offset + range_tile);

                    if (heap_offset + VKD3D_TILE_SIZE > memory_heap->desc.SizeInBytes)
                    {
                        ERR("Heap offset %"PRIu64" out of bounds, heap size is %"PRIu64".\n", heap_offset, memory_heap->desc.SizeInBytes);
                        goto fail;
                    }

                    bind->vk_memory = memory_heap->allocation.device_allocation.vk_memory;
                    bind->vk_offset = memory_heap->allocation.offset + heap_offset;
                }
            }
            else
            {
                WARN("Tile coordinates out of bounds, subresource %u @ (%u,%u,%u), tile %u.\n",
                        region_coord.Subresource, region_coord.X, region_coord.Y, region_coord.Z, region_tile);
            }
        }

        if (++range_tile == range_size)
        {
            range_idx += 1;
            range_tile = 0;
        }

        if (++region_tile == region_size.NumTiles)
        {
            region_idx += 1;
            region_tile = 0;
        }
    }

    /* Avoids tripping validation error since it's not legal to have bindCount == 0. */
    if (sub.bind_sparse.bind_count == 0)
        goto fail;

    vkd3d_free(bound_tiles);

    d3d12_resource_incref(res);
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
    struct d3d12_resource *dst_res = impl_from_ID3D12Resource(dst_resource);
    struct d3d12_resource *src_res = impl_from_ID3D12Resource(src_resource);
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

        if (bind->dst_tile == VKD3D_INVALID_TILE_INDEX || bind->src_tile == VKD3D_INVALID_TILE_INDEX)
        {
            WARN("Tile coordinates out of bounds, src subresource %u @ (%u,%u,%u), dst subresource %u @ (%u,%u,%u), tile %u.\n",
                    src_region_start_coordinate->Subresource, src_region_start_coordinate->X, src_region_start_coordinate->Y, src_region_start_coordinate->Z,
                    dst_region_start_coordinate->Subresource, dst_region_start_coordinate->X, src_region_start_coordinate->Y, dst_region_start_coordinate->Z, i);
            goto fail;
        }
    }

    d3d12_resource_incref(dst_res);
    d3d12_command_queue_add_submission(command_queue, &sub);
    return;

fail:
    vkd3d_free(sub.bind_sparse.bind_infos);
}

static void STDMETHODCALLTYPE d3d12_command_queue_ExecuteCommandLists(ID3D12CommandQueue *iface,
        UINT command_list_count, ID3D12CommandList * const *command_lists)
{
    struct d3d12_command_queue *command_queue = impl_from_ID3D12CommandQueue(iface);
    struct vkd3d_queue_timeline_trace_cookie timeline_cookie;
    struct vkd3d_initial_transition *transitions;
    size_t num_transitions, num_command_buffers;
    VkCommandBufferSubmitInfo *buffers, *buffer;
    struct d3d12_command_allocator **allocators;
    struct d3d12_command_queue_submission sub;
    struct d3d12_command_list *cmd_list;
#ifdef VKD3D_ENABLE_BREADCRUMBS
    unsigned int *breadcrumb_indices;
#endif
    uint32_t *cmd_cost;
    unsigned int iter;
    unsigned int i, j;
    HRESULT hr;

    TRACE("iface %p, command_list_count %u, command_lists %p.\n",
            iface, command_list_count, command_lists);

    if (!command_list_count)
        return;

    if (FAILED(hr = vkd3d_memory_transfer_queue_flush(&command_queue->device->memory_transfers)))
    {
        d3d12_device_mark_as_removed(command_queue->device, hr,
                "Failed to execute pending memory clears.\n");
        return;
    }

    memset(&sub, 0, sizeof(sub));

    /* ExecuteCommandLists submission barrier buffer */
    num_command_buffers = command_queue->vkd3d_queue->barrier_command_buffer ? 1 : 0;

    for (i = 0; i < command_list_count; ++i)
    {
        cmd_list = d3d12_command_list_from_iface(command_lists[i]);

        if (!cmd_list)
        {
            WARN("Unsupported command list type %p.\n", cmd_list);
            return;
        }

        for (iter = 0; iter < cmd_list->cmd.iteration_count; iter++)
        {
            if (cmd_list->cmd.iterations[iter].vk_init_commands)
                num_command_buffers++;
            assert(cmd_list->cmd.iterations[iter].vk_command_buffer);
            num_command_buffers++;
        }
    }

    if (!(buffers = vkd3d_calloc(num_command_buffers, sizeof(*buffers))))
    {
        ERR("Failed to allocate command buffer array.\n");
        return;
    }

    if (!(allocators = vkd3d_calloc(command_list_count, sizeof(*allocators))))
    {
        ERR("Failed to allocate outstanding submissions count.\n");
        vkd3d_free(buffers);
        return;
    }

    if (!(cmd_cost = vkd3d_calloc(num_command_buffers, sizeof(*cmd_cost))))
    {
        ERR("Failed to allocate command buffer cost array.\n");
        return;
    }

#ifdef VKD3D_ENABLE_BREADCRUMBS
    if (vkd3d_config_flags & VKD3D_CONFIG_FLAG_BREADCRUMBS_TRACE)
        breadcrumb_indices = vkd3d_malloc(sizeof(unsigned int) * command_list_count);
    else
        breadcrumb_indices = NULL;
#endif

    timeline_cookie = vkd3d_queue_timeline_trace_register_execute(
            &command_queue->device->queue_timeline_trace,
            command_lists, command_list_count);

    num_transitions = 0;

    for (i = 0, j = 0; i < command_list_count; ++i)
    {
        cmd_list = unsafe_impl_from_ID3D12CommandList(command_lists[i]);

        if (cmd_list->is_recording || !cmd_list->submit_allocator)
        {
            if (cmd_list->is_recording)
            {
                d3d12_device_mark_as_removed(command_queue->device, DXGI_ERROR_INVALID_CALL,
                        "Command list %p is in recording state.\n", command_lists[i]);
            }
            else
            {
                d3d12_device_mark_as_removed(command_queue->device, DXGI_ERROR_INVALID_CALL,
                        "Command list %p is not associated with an allocator.\n", command_lists[i]);
            }

            for (j = 0; j < i; j++)
                d3d12_command_allocator_dec_ref(allocators[j]);

            vkd3d_free(allocators);
            vkd3d_free(buffers);
            vkd3d_free(cmd_cost);
#ifdef VKD3D_ENABLE_BREADCRUMBS
            vkd3d_free(breadcrumb_indices);
#endif
            vkd3d_queue_timeline_trace_complete_execute(&command_queue->device->queue_timeline_trace,
                    NULL, timeline_cookie);
            return;
        }

        num_transitions += cmd_list->init_transitions_count;

        allocators[i] = cmd_list->submit_allocator;
        d3d12_command_allocator_inc_ref(allocators[i]);

        for (iter = 0; iter < cmd_list->cmd.iteration_count; iter++)
        {
            if (cmd_list->cmd.iterations[iter].vk_init_commands)
            {
                /* Assume high cost for DGC preprocessing, everything else is cheap enough to be ignored. */
                cmd_cost[j] = cmd_list->cmd.iterations[iter].indirect_meta.need_preprocess_barrier
                        ? VKD3D_COMMAND_COST_LOW : 0u;

                buffer = &buffers[j++];
                buffer->sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
                buffer->commandBuffer = cmd_list->cmd.iterations[iter].vk_init_commands;
            }

            cmd_cost[j] = cmd_list->cmd.iterations[iter].estimated_cost;

            buffer = &buffers[j++];
            buffer->sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
            buffer->commandBuffer = cmd_list->cmd.iterations[iter].vk_command_buffer;
        }

        if (cmd_list->debug_capture)
            sub.execute.debug_capture = true;

        /* Submission logic for IB fallbacks seems to have exposed something ... very dodgy in RADV. */
        if (cmd_list->cmd.uses_dgc_compute_in_async_compute &&
                !(vkd3d_config_flags & VKD3D_CONFIG_FLAG_SKIP_DRIVER_WORKAROUNDS) &&
                command_queue->device->device_info.vulkan_1_2_properties.driverID == VK_DRIVER_ID_MESA_RADV)
        {
            sub.execute.split_submission = true;
        }

#ifdef VKD3D_ENABLE_BREADCRUMBS
        if (breadcrumb_indices)
            breadcrumb_indices[i] = cmd_list->breadcrumb_context_index;

        /* For a grouped submission, it's useful to know about the full submission when reporting failures.
         * Synchronization issues can occur between command lists.
         * It's not allowed to submit the same command list concurrently, so this should be safe. */
        if (vkd3d_config_flags & VKD3D_CONFIG_FLAG_BREADCRUMBS)
        {
            vkd3d_breadcrumb_tracer_link_submission(cmd_list,
                    i ? unsafe_impl_from_ID3D12CommandList(command_lists[i - 1]) : NULL,
                    i + 1 < command_list_count ? unsafe_impl_from_ID3D12CommandList(command_lists[i + 1]) : NULL);
        }
#endif
    }

    if (command_queue->vkd3d_queue->barrier_command_buffer)
    {
        /* Append a full GPU barrier between submissions.
         * This command buffer is SIMULTANEOUS_BIT. */
        cmd_cost[j] = 0u;

        buffer = &buffers[j++];
        buffer->sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
        buffer->commandBuffer = command_queue->vkd3d_queue->barrier_command_buffer;
    }

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
            if (cmd_list->init_transitions_count)
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
    sub.execute.cmd_cost = cmd_cost;
    sub.execute.cmd_count = num_command_buffers;
    sub.execute.command_allocators = allocators;
    sub.execute.num_command_allocators = command_list_count;
    sub.execute.low_latency_frame_id = command_queue->device->frame_markers.render;
#ifdef VKD3D_ENABLE_BREADCRUMBS
    sub.execute.breadcrumb_indices = breadcrumb_indices;
    sub.execute.breadcrumb_indices_count = breadcrumb_indices ? command_list_count : 0;
#endif
    sub.execute.timeline_cookie = timeline_cookie;
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

    TRACE("iface %p, fence %p, value %#"PRIx64".\n", iface, fence_iface, value);

    d3d12_fence_iface_inc_ref((d3d12_fence_iface *)fence_iface);

    sub.type = VKD3D_SUBMISSION_SIGNAL;
    sub.signal.fence = (d3d12_fence_iface *)fence_iface;
    sub.signal.value = value;
    d3d12_command_queue_add_submission(command_queue, &sub);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d3d12_command_queue_Wait(ID3D12CommandQueue *iface,
        ID3D12Fence *fence_iface, UINT64 value)
{
    struct d3d12_command_queue *command_queue = impl_from_ID3D12CommandQueue(iface);
    struct d3d12_command_queue_submission sub;

    TRACE("iface %p, fence %p, value %#"PRIx64".\n", iface, fence_iface, value);

    d3d12_fence_iface_inc_ref((d3d12_fence_iface *)fence_iface);

    sub.type = VKD3D_SUBMISSION_WAIT;
    sub.wait.fence = (d3d12_fence_iface *)fence_iface;
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

    memset(timestamp_infos, 0, sizeof(timestamp_infos));

    timestamp_info = &timestamp_infos[count++];
    timestamp_info->sType = VK_STRUCTURE_TYPE_CALIBRATED_TIMESTAMP_INFO_KHR;
    timestamp_info->timeDomain = VK_TIME_DOMAIN_DEVICE_KHR;

    if (device->device_info.time_domains & VKD3D_TIME_DOMAIN_QPC)
    {
        timestamp_info = &timestamp_infos[count++];
        timestamp_info->sType = VK_STRUCTURE_TYPE_CALIBRATED_TIMESTAMP_INFO_KHR;
        timestamp_info->timeDomain = VK_TIME_DOMAIN_QUERY_PERFORMANCE_COUNTER_KHR;
    }
    else
    {
        FIXME_ONCE("QPC domain not supported by device, timestamp calibration may be inaccurate.\n");
        QueryPerformanceCounter(&qpc_begin);
    }

    if ((vr = VK_CALL(vkGetCalibratedTimestampsKHR(device->vk_device,
            count, timestamp_infos, timestamps, &max_deviation))) < 0)
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

extern CONST_VTBL struct ID3D12CommandQueueExtVtbl d3d12_command_queue_vkd3d_ext_vtbl;

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
    (void *)d3d12_object_SetName,
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

static bool d3d12_command_queue_needs_cpu_waits_locked(struct d3d12_command_queue *command_queue)
{
    uint64_t current_time_ns, queue_submit_time_ns;
    unsigned int i;

    if (command_queue->vkd3d_queue->command_queue_count == 1)
        return false;

    if (vkd3d_config_flags & VKD3D_CONFIG_FLAG_NO_STAGGERED_SUBMIT)
        return false;

    current_time_ns = vkd3d_get_current_time_ns();

    for (i = 0; i < command_queue->vkd3d_queue->command_queue_count; i++)
    {
        struct d3d12_command_queue *q = command_queue->vkd3d_queue->command_queues[i];

        if (q == command_queue)
            continue;

        /* If any other virtual queue is actively doing submissions, resolve waits
         * on the CPU in order to avoid delays caused by false dependencies. */
        queue_submit_time_ns = vkd3d_atomic_uint64_load_explicit(&q->last_submission_time_ns, vkd3d_memory_order_relaxed);

        if (queue_submit_time_ns + VKD3D_QUEUE_INACTIVE_THRESHOLD_NS > current_time_ns)
            return true;
    }

    return false;
}

static void d3d12_command_queue_destroy_serializing_semaphore(struct d3d12_command_queue *command_queue)
{
    const struct vkd3d_vk_device_procs *vk_procs = &command_queue->device->vk_procs;

    VK_CALL(vkDestroySemaphore(command_queue->device->vk_device, command_queue->serializing_semaphore, NULL));
}

static void d3d12_command_queue_reset_fence_waits(struct d3d12_command_queue *command_queue)
{
    size_t i;

    for (i = 0; i < command_queue->wait_fence_count; i++)
        d3d12_fence_dec_ref(command_queue->wait_fences[i].fence);

    command_queue->wait_fence_count = 0;
}

static void d3d12_command_queue_push_fence_waits_to_worker(struct d3d12_command_queue *command_queue)
{
    struct vkd3d_fence_worker *worker = &command_queue->fence_worker;
    const struct vkd3d_fence_virtual_wait *fence_wait;
    struct vkd3d_queue_timeline_trace_cookie cookie;
    struct vkd3d_fence_wait_info fence_info;
    HRESULT hr;
    size_t i;

    for (i = 0; i < command_queue->wait_fence_count; i++)
    {
        fence_wait = &command_queue->wait_fences[i];
        cookie = vkd3d_queue_timeline_trace_register_wait(&worker->device->queue_timeline_trace,
                &fence_wait->fence->ID3D12Fence_iface, fence_wait->virtual_value);

        /* Only wait for fence completion if we're interested in it for profiling purposes.
         * Otherwise, there is no point in waiting on it since we resolved the wait to a vkd3d_queue internal timeline.
         * This also functions as a performance workaround for NV since NV seems to dislike when the same timeline is
         * waited on by multiple threads, which could happen in this code path. See issue 2256 for more details. */
        if (vkd3d_queue_timeline_trace_cookie_is_valid(cookie))
        {
            memset(&fence_info, 0, sizeof(fence_info));
            fence_info.vk_semaphore = fence_wait->vk_semaphore;
            fence_info.vk_semaphore_value = fence_wait->vk_semaphore_value;

            if (FAILED(hr = vkd3d_enqueue_timeline_semaphore(worker, &fence_info, &cookie)))
                ERR("Failed to enqueue timeline semaphore, hr %#x.\n", hr);
        }
    }
}

static void d3d12_command_queue_add_wait_semaphores(struct d3d12_command_queue *command_queue,
        unsigned int wait_count, const VkSemaphoreSubmitInfo *waits)
{
    VkSemaphoreSubmitInfo *dst_wait;
    unsigned int i, j;
    bool found;

    if (!wait_count)
        return;

    if (!vkd3d_array_reserve((void**)&command_queue->wait_semaphores, &command_queue->wait_semaphores_size,
            command_queue->wait_semaphore_count + wait_count, sizeof(*command_queue->wait_semaphores)))
    {
        ERR("Failed to allocate semaphore wait list.\n");
        return;
    }

    for (i = 0; i < wait_count; i++)
    {
        found = false;

        for (j = 0; j < command_queue->wait_semaphore_count; j++)
        {
            dst_wait = &command_queue->wait_semaphores[j];

            if ((found = (dst_wait->semaphore == waits[i].semaphore)))
            {
                dst_wait->value = max(dst_wait->value, waits[i].value);
                dst_wait->stageMask |= waits[i].stageMask;
                break;
            }
        }

        if (!found)
            command_queue->wait_semaphores[command_queue->wait_semaphore_count++] = waits[i];
    }
}

static void d3d12_command_queue_add_wait(struct d3d12_command_queue *command_queue,
        struct d3d12_fence *fence, const struct d3d12_fence_value *fence_value)
{
    struct vkd3d_fence_virtual_wait *fence_wait;
    VkSemaphoreSubmitInfo semaphore_wait;

    if (!vkd3d_array_reserve((void**)&command_queue->wait_fences, &command_queue->wait_fences_size,
            command_queue->wait_fence_count + 1, sizeof(*command_queue->wait_fences)))
    {
        ERR("Failed to add fence wait to queue.\n");
        return;
    }

    fence_wait = &command_queue->wait_fences[command_queue->wait_fence_count++];

    memset(fence_wait, 0, sizeof(*fence_wait));
    fence_wait->fence = fence;
    fence_wait->virtual_value = fence_value->virtual_value;
    fence_wait->vk_semaphore = fence_value->vk_semaphore;
    fence_wait->vk_semaphore_value = fence_value->vk_semaphore_value;

    d3d12_fence_inc_ref(fence);

    memset(&semaphore_wait, 0, sizeof(semaphore_wait));
    semaphore_wait.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    semaphore_wait.semaphore = fence_value->vk_semaphore;
    semaphore_wait.value = fence_value->vk_semaphore_value;
    semaphore_wait.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;

    d3d12_command_queue_add_wait_semaphores(command_queue, 1, &semaphore_wait);
}

static void d3d12_command_queue_eliminate_completed_waits(struct d3d12_command_queue *command_queue)
{
    const struct vkd3d_vk_device_procs *vk_procs = &command_queue->device->vk_procs;
    uint64_t semaphore_value;
    unsigned int i, j;
    for (i = 0, j = 0; i < command_queue->wait_semaphore_count; i++)
    {
        /* If value is 0, we might be dealing with a binary semaphore */
        if (command_queue->wait_semaphores[i].value)
        {
            semaphore_value = 0;
            VK_CALL(vkGetSemaphoreCounterValue(command_queue->device->vk_device,
                    command_queue->wait_semaphores[i].semaphore, &semaphore_value));

            if (semaphore_value >= command_queue->wait_semaphores[i].value)
                continue;
        }

        if (j < i)
            command_queue->wait_semaphores[j] = command_queue->wait_semaphores[i];

        j++;
    }

    command_queue->wait_semaphore_count = j;
}

static void d3d12_command_queue_gather_wait_semaphores_locked(struct d3d12_command_queue *command_queue, VkSubmitInfo2 *submit_info, uint32_t wait_flags)
{
    struct VkSemaphoreSubmitInfo serializing_semaphore;

    d3d12_command_queue_add_wait_semaphores(command_queue,
            submit_info->waitSemaphoreInfoCount,
            submit_info->pWaitSemaphoreInfos);

    if (wait_flags & VKD3D_WAIT_SEMAPHORES_EXTERNAL)
    {
        /* Add global waits from the physical queue as well. This is safe as long
         * as the caller submits pending waits inside the same locked scope. */
        d3d12_command_queue_add_wait_semaphores(command_queue,
                command_queue->vkd3d_queue->wait_count,
                command_queue->vkd3d_queue->wait_semaphores);

        command_queue->vkd3d_queue->wait_count = 0u;
    }

    if ((wait_flags & VKD3D_WAIT_SEMAPHORES_SERIALIZING) && command_queue->serializing_semaphore_signaled)
    {
        memset(&serializing_semaphore, 0, sizeof(serializing_semaphore));
        serializing_semaphore.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
        serializing_semaphore.semaphore = command_queue->serializing_semaphore;
        serializing_semaphore.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;

        d3d12_command_queue_add_wait_semaphores(command_queue, 1, &serializing_semaphore);
    }

    d3d12_command_queue_eliminate_completed_waits(command_queue);

    submit_info->waitSemaphoreInfoCount = command_queue->wait_semaphore_count;
    submit_info->pWaitSemaphoreInfos = command_queue->wait_semaphores;
}

static void d3d12_command_queue_finalize_waits(struct d3d12_command_queue *command_queue, VkResult submit_vr)
{
    command_queue->wait_semaphore_count = 0u;

    if (submit_vr == VK_SUCCESS)
        d3d12_command_queue_push_fence_waits_to_worker(command_queue);

    d3d12_command_queue_reset_fence_waits(command_queue);
}

static void d3d12_command_queue_flush_waiters(struct d3d12_command_queue *command_queue, uint32_t wait_flags)
{
    const struct vkd3d_vk_device_procs *vk_procs = &command_queue->device->vk_procs;
    VkSemaphoreSubmitInfo signal_semaphore;
    VkSubmitInfo2 submit_info;
    VkResult vr = VK_SUCCESS;
    VkQueue vk_queue;

    if (!(vk_queue = vkd3d_queue_acquire(command_queue->vkd3d_queue)))
    {
        ERR("Failed to acquire queue %p.\n", command_queue->vkd3d_queue);
        return;
    }

    memset(&submit_info, 0, sizeof(submit_info));
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;

    d3d12_command_queue_gather_wait_semaphores_locked(command_queue, &submit_info, wait_flags);

    if (submit_info.waitSemaphoreInfoCount)
    {
        command_queue->last_submission_timeline_value = ++command_queue->vkd3d_queue->submission_timeline_count;

        memset(&signal_semaphore, 0, sizeof(signal_semaphore));
        signal_semaphore.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
        signal_semaphore.semaphore = command_queue->vkd3d_queue->submission_timeline;
        signal_semaphore.value = command_queue->last_submission_timeline_value;

        submit_info.signalSemaphoreInfoCount = 1;
        submit_info.pSignalSemaphoreInfos = &signal_semaphore;

        if ((vr = VK_CALL(vkQueueSubmit2(vk_queue, 1, &submit_info, VK_NULL_HANDLE))))
            ERR("Failed to submit semaphore waits, vr %d.\n", vr);

        if (vr == VK_SUCCESS && (wait_flags & VKD3D_WAIT_SEMAPHORES_SERIALIZING))
            command_queue->serializing_semaphore_signaled = false;
    }

    vkd3d_queue_release(command_queue->vkd3d_queue);

    d3d12_command_queue_finalize_waits(command_queue, vr);
}

static void d3d12_command_queue_wait_idle(struct d3d12_command_queue *command_queue)
{
    const struct vkd3d_vk_device_procs *vk_procs = &command_queue->device->vk_procs;
    VkSemaphoreWaitInfo semaphore_wait;
    VkResult vr;

    d3d12_command_queue_flush_waiters(command_queue, VKD3D_WAIT_SEMAPHORES_SERIALIZING);

    memset(&semaphore_wait, 0, sizeof(semaphore_wait));
    semaphore_wait.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
    semaphore_wait.semaphoreCount = 1u;
    semaphore_wait.pSemaphores = &command_queue->vkd3d_queue->submission_timeline;
    semaphore_wait.pValues = &command_queue->last_submission_timeline_value;

    if ((vr = VK_CALL(vkWaitSemaphores(command_queue->device->vk_device, &semaphore_wait, UINT64_MAX))))
        ERR("Failed to wait for virtual queue idle, vr %d.\n", vr);
}

static void d3d12_command_queue_wait(struct d3d12_command_queue *command_queue,
        struct d3d12_fence *fence, UINT64 value)
{
    const struct vkd3d_vk_device_procs *vk_procs = &command_queue->device->vk_procs;
    struct vkd3d_queue_timeline_trace_cookie cookie;
    struct d3d12_fence_value fence_value;
    VkSemaphoreWaitInfo wait_info;
    struct vkd3d_queue *queue;
    bool has_wait;
    VkResult vr;

    queue = command_queue->vkd3d_queue;

    assert(!fence->timeline_semaphore);

    d3d12_fence_lock(fence);

    /* This is the critical part required to support out-of-order signal.
     * Normally we would be able to submit waits and signals out of order,
     * but we don't have virtualized queues in Vulkan, so we need to handle the case
     * where multiple queues alias over the same physical queue, so effectively, we need to manage out-of-order submits
     * ourselves. */
    cookie = vkd3d_queue_timeline_trace_register_generic_region(&fence->device->queue_timeline_trace, "WAIT BEFORE SIGNAL");
    d3d12_fence_block_until_pending_value_reaches_locked(fence, value);
    vkd3d_queue_timeline_trace_complete_execute(&fence->device->queue_timeline_trace, &command_queue->fence_worker, cookie);

    /* If a host signal unblocked us, or we know that the fence has reached a specific value, there is no need
     * to queue up a wait. */
    if (d3d12_fence_can_elide_wait_semaphore_locked(fence, value, queue))
    {
        d3d12_fence_unlock(fence);
        return;
    }

    has_wait = d3d12_fence_get_physical_wait_value_locked(fence, value, &fence_value);

    d3d12_fence_unlock(fence);

    if (!has_wait)
        return;

    TRACE("queue %p, fence %p, value %#"PRIx64", vk_semaphore %p, vk_semaphore_value %#"PRIx64".\n", command_queue,
            fence, value, fence_value.vk_semaphore, fence_value.vk_semaphore_value);

    pthread_mutex_lock(&queue->mutex);

    if (d3d12_command_queue_needs_cpu_waits_locked(command_queue))
    {
        pthread_mutex_unlock(&queue->mutex);

        memset(&wait_info, 0, sizeof(wait_info));
        wait_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
        wait_info.semaphoreCount = 1;
        wait_info.pSemaphores = &fence_value.vk_semaphore;
        wait_info.pValues = &fence_value.vk_semaphore_value;

        cookie = vkd3d_queue_timeline_trace_register_generic_region(&fence->device->queue_timeline_trace, "CPU WAIT");
        if ((vr = VK_CALL(vkWaitSemaphores(command_queue->device->vk_device, &wait_info, UINT64_MAX))))
            ERR("Failed to wait for timeline semaphore, vr %d.\n", vr);
        vkd3d_queue_timeline_trace_complete_execute(&fence->device->queue_timeline_trace, &command_queue->fence_worker, cookie);
    }
    else
    {
        pthread_mutex_unlock(&queue->mutex);

        /* Defer the wait to next submit.
         * This is also important, since we have to hold on to a private reference on the fence
         * until we have observed the wait to actually complete. */
        d3d12_command_queue_add_wait(command_queue, fence, &fence_value);
    }
}

static void d3d12_command_queue_signal(struct d3d12_command_queue *command_queue,
        struct d3d12_fence *fence, UINT64 value)
{
    struct vkd3d_waiting_fence_signal_info *signal_info;
    struct vkd3d_queue_timeline_trace_cookie cookie;
    struct vkd3d_fence_wait_info fence_info;
    uint64_t update_count;
    HRESULT hr;

    TRACE("queue %p, fence %p, value %#"PRIx64", vk_semaphore %p, vk_semaphore_value %#"PRIx64".\n", command_queue,
            fence, value, command_queue->vkd3d_queue->submission_timeline, command_queue->last_submission_timeline_value);

    assert(!fence->timeline_semaphore);

    cookie = vkd3d_queue_timeline_trace_register_signal(&command_queue->device->queue_timeline_trace,
            &fence->ID3D12Fence_iface, value);

    d3d12_fence_lock(fence);

    update_count = d3d12_fence_add_pending_signal_locked(fence, value, command_queue);

    memset(&fence_info, 0, sizeof(fence_info));
    fence_info.vk_semaphore = command_queue->vkd3d_queue->submission_timeline;
    fence_info.vk_semaphore_value = command_queue->last_submission_timeline_value;

    signal_info = vkd3d_waiting_fence_set_callback(&fence_info,
            &vkd3d_waiting_fence_signal_fence, sizeof(*signal_info));
    signal_info->fence = fence;
    signal_info->virtual_value = value;
    signal_info->update_count = update_count;

    d3d12_fence_inc_ref(signal_info->fence);

    if (FAILED(hr = vkd3d_enqueue_timeline_semaphore(&command_queue->fence_worker, &fence_info, &cookie)))
        ERR("Failed to enqueue timeline semaphore, hr #%x.\n", hr);

    d3d12_fence_update_pending_value_locked(fence);
    d3d12_fence_unlock(fence);
}

static void d3d12_command_queue_wait_shared(struct d3d12_command_queue *command_queue,
        struct d3d12_shared_fence *fence, UINT64 value)
{
    const struct vkd3d_vk_device_procs *vk_procs;
    VkSemaphoreWaitInfo wait_info;
    struct d3d12_device *device;
    VkResult vr;

    assert(fence->timeline_semaphore);

    device = command_queue->device;
    vk_procs = &device->vk_procs;

    /* Resolve the wait on the CPU rather than submitting it to the Vulkan queue.
     * For shared fences, we cannot know when signal operations for the fence get
     * queued up, so this is the only way to prevent wait-before-signal situations
     * when multiple D3D12 queues use the same Vulkan queue. */
    wait_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
    wait_info.pNext = NULL;
    wait_info.flags = 0;
    wait_info.pSemaphores = &fence->timeline_semaphore;
    wait_info.semaphoreCount = 1;
    wait_info.pValues = &value;
    vr = VK_CALL(vkWaitSemaphores(device->vk_device, &wait_info, UINT64_MAX));
    VKD3D_DEVICE_REPORT_FAULT_AND_BREADCRUMB_IF(device, vr == VK_ERROR_DEVICE_LOST);
}

static void d3d12_command_queue_signal_shared(struct d3d12_command_queue *command_queue,
        struct d3d12_shared_fence *fence, UINT64 value)
{
    struct vkd3d_waiting_fence_release_info *release_info;
    const struct vkd3d_vk_device_procs *vk_procs;
    VkSemaphoreSubmitInfo signal_semaphore_info;
    struct vkd3d_fence_wait_info fence_info;
    struct vkd3d_queue *vkd3d_queue;
    struct d3d12_device *device;
    VkSubmitInfo2 submit_info;
    VkQueue vk_queue;
    VkResult vr;
    HRESULT hr;

    device = command_queue->device;
    vk_procs = &device->vk_procs;
    vkd3d_queue = command_queue->vkd3d_queue;

    TRACE("queue %p, fence %p, value %#"PRIx64".\n", command_queue, fence, value);

    if (!(vk_queue = vkd3d_queue_acquire(vkd3d_queue)))
    {
        ERR("Failed to acquire queue %p.\n", vkd3d_queue);
        return;
    }

    memset(&signal_semaphore_info, 0, sizeof(signal_semaphore_info));
    signal_semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    signal_semaphore_info.semaphore = fence->timeline_semaphore;
    signal_semaphore_info.value = value;
    signal_semaphore_info.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;

    memset(&submit_info, 0, sizeof(submit_info));
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    submit_info.signalSemaphoreInfoCount = 1;
    submit_info.pSignalSemaphoreInfos = &signal_semaphore_info;

    vr = VK_CALL(vkQueueSubmit2(vk_queue, 1, &submit_info, VK_NULL_HANDLE));

    if (vr == VK_SUCCESS)
    {
        /* Track shared semaphore signals through the submission timeline to avoid problems with rewinding */
        vkd3d_queue->submission_timeline_count++;

        signal_semaphore_info.value = vkd3d_queue->submission_timeline_count;
        signal_semaphore_info.semaphore = vkd3d_queue->submission_timeline;

        vr = VK_CALL(vkQueueSubmit2(vk_queue, 1, &submit_info, VK_NULL_HANDLE));

        command_queue->last_submission_timeline_value = vkd3d_queue->submission_timeline_count;
    }

    vkd3d_queue_release(vkd3d_queue);

    if (vr < 0)
    {
        ERR("Failed to submit signal operation, vr %d.\n", vr);
        return;
    }

    VKD3D_DEVICE_REPORT_FAULT_AND_BREADCRUMB_IF(command_queue->device, vr == VK_ERROR_DEVICE_LOST);

    memset(&fence_info, 0, sizeof(fence_info));
    fence_info.vk_semaphore = vkd3d_queue->submission_timeline;
    fence_info.vk_semaphore_value = vkd3d_queue->submission_timeline_count;

    release_info = vkd3d_waiting_fence_set_callback(&fence_info,
            &vkd3d_waiting_fence_release_fence, sizeof(*release_info));
    release_info->fence = &fence->ID3D12Fence_iface;

    d3d12_shared_fence_inc_ref(fence);

    if (FAILED(hr = vkd3d_enqueue_timeline_semaphore(&command_queue->fence_worker, &fence_info, NULL)))
    {
        ERR("Failed to enqueue timeline semaphore, hr #%x.\n", hr);
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

    VkImageMemoryBarrier2 *barriers;
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

    if (FAILED(hr = vkd3d_create_timeline_semaphore(queue->device, 0, false, &pool->timeline)))
        return hr;

    return S_OK;
}

static void d3d12_command_queue_transition_pool_wait(struct d3d12_command_queue_transition_pool *pool,
        struct d3d12_device *device, uint64_t value)
{
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    VkSemaphoreWaitInfo wait_info;
    VkResult vr;

    wait_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
    wait_info.pNext = NULL;
    wait_info.flags = 0;
    wait_info.pSemaphores = &pool->timeline;
    wait_info.semaphoreCount = 1;
    wait_info.pValues = &value;
    vr = VK_CALL(vkWaitSemaphores(device->vk_device, &wait_info, ~(uint64_t)0));
    VKD3D_DEVICE_REPORT_FAULT_AND_BREADCRUMB_IF(device, vr == VK_ERROR_DEVICE_LOST);
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
    if (!vkd3d_array_reserve((void**)&pool->barriers, &pool->barriers_size,
            pool->barriers_count + 1, sizeof(*pool->barriers)))
    {
        ERR("Failed to allocate barriers.\n");
        return;
    }

    if (vk_image_memory_barrier_for_initial_transition(resource, &pool->barriers[pool->barriers_count]))
        pool->barriers_count++;
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
                VK_CALL(vkCmdWriteTimestamp2(vk_cmd_buffer, VK_PIPELINE_STAGE_2_NONE, heap->vk_query_pool, i));
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
    VkDependencyInfo dep_info;
    uint32_t need_transition;
    size_t i;

    pool->barriers_count = 0;
    pool->query_heaps_count = 0;

    if (!(vkd3d_config_flags & VKD3D_CONFIG_FLAG_INSTRUCTION_QA_CHECKS) && !count)
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

    if (!(vkd3d_config_flags & VKD3D_CONFIG_FLAG_INSTRUCTION_QA_CHECKS) &&
            !pool->barriers_count && !pool->query_heaps_count)
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

    memset(&dep_info, 0, sizeof(dep_info));
    dep_info.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dep_info.imageMemoryBarrierCount = pool->barriers_count;
    dep_info.pImageMemoryBarriers = pool->barriers;

    if (vkd3d_config_flags & VKD3D_CONFIG_FLAG_INSTRUCTION_QA_CHECKS)
    {
        /* Every ExecuteCommandLists is an implicit barrier, so flush the bloom filter automatically. */
        uint32_t value = vkd3d_descriptor_debug_clear_bloom_filter(device->descriptor_qa_global_info, device, pool->cmd[command_index]);
        if (vkd3d_config_flags & VKD3D_CONFIG_FLAG_BREADCRUMBS_TRACE)
            INFO("QA: Updating sync-val iteration to %u (#%x) between submission.\n", value, value);
    }

    if (pool->barriers_count)
        VK_CALL(vkCmdPipelineBarrier2(pool->cmd[command_index], &dep_info));

    for (i = 0; i < pool->query_heaps_count; i++)
        d3d12_command_queue_init_query_heap(device, pool->cmd[command_index], pool->query_heaps[i]);
    VK_CALL(vkEndCommandBuffer(pool->cmd[command_index]));

    *vk_cmd_buffer = pool->cmd[command_index];
    *timeline_value = pool->timeline_value;
}

static VkResult d3d12_command_queue_submit_split_locked(struct d3d12_device *device,
        VkQueue vk_queue, uint32_t num_submits, const VkSubmitInfo2 *submits)
{
    /* Ugly workaround when needed. Never submit more than one command buffer at a time. */
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    const VkSubmitInfo2 *input_submission;
    VkSubmitInfo2 split_submission;
    uint32_t submit_index;
    uint32_t cmd_index;
    uint32_t num_cmds;
    VkResult vr;

    memset(&split_submission, 0, sizeof(split_submission));
    split_submission.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;

    for (submit_index = 0; submit_index < num_submits; submit_index++)
    {
        input_submission = &submits[submit_index];

        if (input_submission->commandBufferInfoCount > 1)
        {
            num_cmds = input_submission->commandBufferInfoCount;
            split_submission.pSignalSemaphoreInfos = input_submission->pSignalSemaphoreInfos;
            split_submission.pWaitSemaphoreInfos = input_submission->pWaitSemaphoreInfos;
            split_submission.commandBufferInfoCount = 1;

            for (cmd_index = 0; cmd_index < num_cmds; cmd_index++)
            {
                split_submission.pCommandBufferInfos =
                        &input_submission->pCommandBufferInfos[cmd_index];
                split_submission.signalSemaphoreInfoCount =
                        cmd_index + 1 == num_cmds ? input_submission->signalSemaphoreInfoCount : 0;
                split_submission.waitSemaphoreInfoCount =
                        cmd_index == 0 ? input_submission->waitSemaphoreInfoCount : 0;

                if ((vr = VK_CALL(vkQueueSubmit2(vk_queue, 1, &split_submission, VK_NULL_HANDLE))) < 0)
                {
                    ERR("Failed to submit queue(s), vr %d.\n", vr);
                    return vr;
                }
            }
        }
        else if ((vr = VK_CALL(vkQueueSubmit2(vk_queue, 1, input_submission, VK_NULL_HANDLE))) < 0)
        {
            ERR("Failed to submit queue(s), vr %d.\n", vr);
            return vr;
        }
    }

	return VK_SUCCESS;
}

static void d3d12_command_queue_wait_staggered_submission(struct d3d12_command_queue *command_queue)
{
    const struct vkd3d_vk_device_procs *vk_procs = &command_queue->device->vk_procs;
    VkSemaphoreWaitInfo wait_info;
    VkResult vr;

    memset(&wait_info, 0, sizeof(wait_info));
    wait_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
    wait_info.semaphoreCount = 1;
    wait_info.pSemaphores = &command_queue->vkd3d_queue->submission_timeline;
    wait_info.pValues = &command_queue->last_submission_timeline_value;

    if ((vr = VK_CALL(vkWaitSemaphores(command_queue->device->vk_device, &wait_info, UINT64_MAX))))
        ERR("Failed to wait for semaphore, vr %d.\n", vr);
}

static bool d3d12_command_queue_needs_staggered_submissions_locked(struct d3d12_command_queue *command_queue)
{
    uint64_t current_time_ns, queue_submit_time_ns;
    int32_t max_priority;
    unsigned int i;

    if (command_queue->vkd3d_queue->command_queue_count == 1)
        return false;

    if (vkd3d_config_flags & VKD3D_CONFIG_FLAG_NO_STAGGERED_SUBMIT)
        return false;

    current_time_ns = vkd3d_get_current_time_ns();

    /* Make sure current command queue is recognized as busy */
    vkd3d_atomic_uint64_store_explicit(&command_queue->last_submission_time_ns,
            current_time_ns, vkd3d_memory_order_relaxed);

    /* Do not stagger submissions for the highest-priority active queue. */
    max_priority = command_queue->desc.Priority;

    for (i = 0; i < command_queue->vkd3d_queue->command_queue_count; i++)
    {
        struct d3d12_command_queue *q = command_queue->vkd3d_queue->command_queues[i];

        queue_submit_time_ns = vkd3d_atomic_uint64_load_explicit(&q->last_submission_time_ns, vkd3d_memory_order_relaxed);

        if (queue_submit_time_ns + VKD3D_QUEUE_INACTIVE_THRESHOLD_NS > current_time_ns)
        {
            max_priority = max(max_priority, q->desc.Priority);

            /* Enable staggered submission logic if graphics and non-graphics
             * queues are active at the same time. This should only happen on
             * devices with one queue family, or when setting single_queue. */
            if (q->desc.Type != command_queue->desc.Type &&
                    (q->desc.Type == D3D12_COMMAND_LIST_TYPE_DIRECT || command_queue->desc.Type == D3D12_COMMAND_LIST_TYPE_DIRECT))
                return true;
        }
    }

    if (command_queue->desc.Priority == max_priority)
        return false;

    /* Otherwise, submit command buffers from this queue one by one in order
     * to allow work from an active high-priority queue to get scheduled sooner.
     * This is relevant for FSR3 frame generation on drivers that only support
     * one graphics queue, since UI composition and presentation are submitted
     * mid-frame without any explicit synchronization between the two graphics
     * queues. */
    return true;
}

static void d3d12_command_queue_execute(struct d3d12_command_queue *command_queue,
        const struct d3d12_command_queue_submission_execute *exec,
        const VkCommandBufferSubmitInfo *transition_cmd,
        const VkSemaphoreSubmitInfo *transition_semaphore)
{
    const struct vkd3d_vk_device_procs *vk_procs = &command_queue->device->vk_procs;
    struct vkd3d_queue *vkd3d_queue = command_queue->vkd3d_queue;
    struct vkd3d_waiting_fence_submission_info *submission_info;
    VkLatencySubmissionPresentIdNV latency_submit_present_info;
    struct dxgi_vk_swap_chain *low_latency_swapchain;
    VkSemaphoreSubmitInfo signal_semaphore_infos[2];
    VkSemaphoreSubmitInfo *binary_semaphore_info;
    bool stagger_submissions, is_first, is_last;
    uint32_t cmd_index, cmd_count, total_cost;
    struct vkd3d_fence_wait_info fence_info;
    VkSubmitInfo2 submit_desc[2], *submit;
    VKD3D_UNUSED bool debug_capture;
    uint64_t consumed_present_id;
    uint32_t num_submits;
    VkQueue vk_queue;
    unsigned int i;
    VkResult vr;
    HRESULT hr;

    TRACE("queue %p, command_list_count %u, command_lists %p.\n",
          command_queue, exec->cmd_count, exec->cmd);

    if (!(vk_queue = vkd3d_queue_acquire(vkd3d_queue)))
    {
        ERR("Failed to acquire queue %p.\n", vkd3d_queue);
        for (i = 0; i < exec->num_command_allocators; i++)
            d3d12_command_allocator_dec_ref(exec->command_allocators[i]);
        vkd3d_free(exec->command_allocators);
        vkd3d_queue_timeline_trace_complete_execute(&command_queue->device->queue_timeline_trace,
                NULL, exec->timeline_cookie);
        return;
    }

#ifdef VKD3D_ENABLE_RENDERDOC
    /* For each submission we have marked to be captured, we will first need to filter it
     * based on VKD3D_AUTO_CAPTURE_COUNTS.
     * If a submission index is not marked to be captured after all, we drop any capture here.
     * Deciding this in the submission thread is more robust than the alternative, since the submission
     * threads are mostly serialized. */
    if (exec->debug_capture)
        debug_capture = vkd3d_renderdoc_command_queue_begin_capture(command_queue);
    else
        debug_capture = false;
#endif

    stagger_submissions = d3d12_command_queue_needs_staggered_submissions_locked(command_queue);

    if (command_queue->stagger_submissions != stagger_submissions)
    {
        command_queue->stagger_submissions = stagger_submissions;
        INFO("%sabling staggered submissions for command queue %p, queue family %u.\n",
                stagger_submissions ? "En" : "Dis", command_queue, command_queue->vkd3d_queue->vk_family_index);
    }

    memset(submit_desc, 0, sizeof(submit_desc));
    num_submits = 0;

    if (transition_cmd->commandBuffer)
    {
        /* The transition cmd must happen in-order, since with the advanced aliasing model in D3D12,
         * it is enough to separate aliases with an ExecuteCommandLists.
         * A clear-like operation must still happen though in the application which would acquire the alias,
         * but we must still be somewhat careful about when we emit initial state transitions.
         * The clear requirement only exists for render targets.
         * Could use the serializing binary semaphore here,
         * but we need to keep track of the timeline on CPU as well
         * to know when we can reset the barrier command buffer. */
        submit = &submit_desc[num_submits++];
        submit->sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
        submit->signalSemaphoreInfoCount = 1;
        submit->pSignalSemaphoreInfos = transition_semaphore;
        submit->commandBufferInfoCount = 1;
        submit->pCommandBufferInfos = transition_cmd;
    }

    cmd_index = 0;

    while (cmd_index < exec->cmd_count)
    {
        is_first = cmd_index == 0;

        memset(signal_semaphore_infos, 0, sizeof(signal_semaphore_infos));
        signal_semaphore_infos[0].sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
        signal_semaphore_infos[0].semaphore = vkd3d_queue->submission_timeline;
        signal_semaphore_infos[0].value = ++vkd3d_queue->submission_timeline_count;
        signal_semaphore_infos[0].stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;

        if (stagger_submissions)
        {
            /* Group up command buffers in such a way that any submission at least reaches the
             * minimum cost threshold in order to reduce delays from CPU<->GPU round-trips. */
            total_cost = exec->cmd_cost[cmd_index];
            cmd_count = 1;

            while (cmd_index + cmd_count < exec->cmd_count && total_cost < VKD3D_COMMAND_COST_MERGE_THRESHOLD)
            {
                total_cost += exec->cmd_cost[cmd_index + cmd_count];
                cmd_count++;
            }

            /* If all remaining command buffers in the set are low cost, add them as well. */
            total_cost = 0;

            for (i = cmd_index + cmd_count; i < exec->cmd_count && total_cost < VKD3D_COMMAND_COST_MERGE_THRESHOLD; i++)
                total_cost += exec->cmd_cost[i];

            if (total_cost < VKD3D_COMMAND_COST_MERGE_THRESHOLD)
                cmd_count = exec->cmd_count - cmd_index;
        }
        else
        {
            /* Submit everything at once */
            cmd_count = exec->cmd_count;
        }

        submit = &submit_desc[num_submits++];
        submit->sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
        submit->commandBufferInfoCount = cmd_count;
        submit->pCommandBufferInfos = &exec->cmd[cmd_index];
        submit->signalSemaphoreInfoCount = 1;
        submit->pSignalSemaphoreInfos = signal_semaphore_infos;

        if (transition_cmd->commandBuffer && is_first)
        {
            submit->waitSemaphoreInfoCount = 1;
            submit->pWaitSemaphoreInfos = transition_semaphore;
        }

        cmd_index += cmd_count;
        is_last = cmd_index == exec->cmd_count;

        if (command_queue->device->vk_info.NV_low_latency2)
        {
            spinlock_acquire(&command_queue->device->low_latency_swapchain_spinlock);
            if ((low_latency_swapchain = command_queue->device->swapchain_info.low_latency_swapchain))
                dxgi_vk_swap_chain_incref(low_latency_swapchain);
            consumed_present_id = command_queue->device->frame_markers.consumed_present_id;
            spinlock_release(&command_queue->device->low_latency_swapchain_spinlock);

            /* If we have submitted a swapchain blit to Vulkan,
             * it is not possible for a present ID to keep contributing to the frame's completion.
             * The likely case here is that application just forgot to signal present ID.
             * Don't bother trying to mark submission present ID if application isn't bothering to set markers properly. */
            if (low_latency_swapchain && exec->low_latency_frame_id > consumed_present_id &&
                    dxgi_vk_swap_chain_low_latency_enabled(low_latency_swapchain))
            {
                latency_submit_present_info.sType = VK_STRUCTURE_TYPE_LATENCY_SUBMISSION_PRESENT_ID_NV;
                latency_submit_present_info.pNext = NULL;
                latency_submit_present_info.presentID = exec->low_latency_frame_id;

                for (i = 0; i < num_submits; i++)
                    submit_desc[i].pNext = &latency_submit_present_info;
            }

            if (low_latency_swapchain)
                dxgi_vk_swap_chain_decref(low_latency_swapchain);
        }

        if (is_first)
        {
            vkd3d_queue_timeline_trace_begin_execute_overhead(&command_queue->device->queue_timeline_trace, exec->timeline_cookie);
            d3d12_command_queue_gather_wait_semaphores_locked(command_queue, &submit_desc[0],
                  VKD3D_WAIT_SEMAPHORES_EXTERNAL | VKD3D_WAIT_SEMAPHORES_SERIALIZING);
        }

        /* Prefer binary semaphore since timeline signal -> wait pair can cause scheduling bubbles.
         * Binary semaphores tend to be more well-behaved here since they can lower to kernel primitives
         * more easily. Must happen after setting up waits to track the binary semaphore state correctly. */
        if (command_queue->serializing_semaphore && is_last)
        {
            binary_semaphore_info = &signal_semaphore_infos[submit->signalSemaphoreInfoCount++];
            binary_semaphore_info->sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
            binary_semaphore_info->semaphore = command_queue->serializing_semaphore;
            binary_semaphore_info->stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        }

        if (exec->split_submission)
            vr = d3d12_command_queue_submit_split_locked(command_queue->device, vk_queue, num_submits, submit_desc);
        else if ((vr = VK_CALL(vkQueueSubmit2(vk_queue, num_submits, submit_desc, VK_NULL_HANDLE))) < 0)
            ERR("Failed to submit queue(s), vr %d.\n", vr);

        if (is_first)
            d3d12_command_queue_finalize_waits(command_queue, vr);

        VKD3D_DEVICE_REPORT_FAULT_AND_BREADCRUMB_IF(command_queue->device, vr == VK_ERROR_DEVICE_LOST);

        if (vr != VK_SUCCESS)
            break;

        command_queue->serializing_semaphore_signaled = command_queue->serializing_semaphore && is_last;

        memset(submit_desc, 0, sizeof(submit_desc));
        num_submits = 0;

        if (stagger_submissions)
        {
            vkd3d_queue_release(vkd3d_queue);

            /* Wait for previous submission from the current virtual queue to complete.
             * This essentially allows one command buffer in flight in order to reduce
             * GPU idle time, as well as delays when processing pending signals. */
            d3d12_command_queue_wait_staggered_submission(command_queue);

            if (!(vk_queue = vkd3d_queue_acquire(vkd3d_queue)))
            {
                ERR("Failed to acquire queue %p.\n", vkd3d_queue);
                return;
            }
        }

        /* Update timeline value *after* waiting for staggered submissions */
        command_queue->last_submission_timeline_value = signal_semaphore_infos[0].value;
    }

    vkd3d_queue_timeline_trace_end_execute_overhead(&command_queue->device->queue_timeline_trace, exec->timeline_cookie);

#ifdef VKD3D_ENABLE_RENDERDOC
    if (debug_capture)
        vkd3d_renderdoc_command_queue_end_capture(command_queue);
#endif

    vkd3d_queue_release(vkd3d_queue);

    /* After a proper submit we have to queue up some work which is tied to this submission:
     * - After the submit completes, we know it's safe to release private reference on any queue waits.
     *   D3D12 allows fences to be released at any time.
     * - Decrementing counters for submissions. This allows us to track when it's safe to reset a command pool.
     *   If there are pending submissions waiting, we are expected to ignore the reset.
     *   We will report a failure in this case. Some games run into this.
     */
    if (vr == VK_SUCCESS && exec->num_command_allocators)
    {
        memset(&fence_info, 0, sizeof(fence_info));
        fence_info.vk_semaphore = vkd3d_queue->submission_timeline;
        fence_info.vk_semaphore_value = signal_semaphore_infos[0].value;

        submission_info = vkd3d_waiting_fence_set_callback(&fence_info,
                &vkd3d_waiting_fence_release_submission, sizeof(*submission_info));
        submission_info->command_allocators = exec->command_allocators;
        submission_info->num_command_allocators = exec->num_command_allocators;

        if (FAILED(hr = vkd3d_enqueue_timeline_semaphore(&command_queue->fence_worker, &fence_info, &exec->timeline_cookie)))
        {
            ERR("Failed to enqueue timeline semaphore.\n");
        }
    }
}

static unsigned int vkd3d_compact_sparse_bind_ranges(const struct d3d12_resource *src_resource,
        struct vkd3d_sparse_memory_bind_range *bind_ranges, struct vkd3d_sparse_memory_bind *bind_infos,
        unsigned int count, enum vkd3d_sparse_memory_bind_mode mode)
{
    struct vkd3d_sparse_memory_bind_range *range = NULL;
    VkDeviceMemory vk_memory;
    VkDeviceSize vk_offset;
    unsigned int i, j;

    for (i = 0, j = 0; i < count; i++)
    {
        struct vkd3d_sparse_memory_bind *bind = &bind_infos[i];

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

        if (range && bind->dst_tile == range->tile_index + range->tile_count && vk_memory == range->vk_memory &&
                (vk_offset == range->vk_offset + range->tile_count * VKD3D_TILE_SIZE || !vk_memory))
        {
            range->tile_count++;
        }
        else
        {
            range = &bind_ranges[j++];
            range->tile_index = bind->dst_tile;
            range->tile_count = 1;
            range->vk_memory = vk_memory;
            range->vk_offset = vk_offset;
        }
    }

    return j;
}

static void d3d12_command_queue_flush_bind_sparse(struct d3d12_command_queue *command_queue);

static void d3d12_command_queue_register_sparse_hazard(
        struct d3d12_command_queue *command_queue,
        struct d3d12_resource *dst_resource,
        const struct vkd3d_sparse_memory_bind *binds, unsigned int count)
{
    const struct vkd3d_sparse_memory_bind *bind;
    unsigned int tracker_index, i;
    bool check_hazard = true;
    uint32_t *tile_mask;

    for (tracker_index = 0; tracker_index < command_queue->sparse.tracked_count; tracker_index++)
        if (command_queue->sparse.tracked[tracker_index].resource == dst_resource)
            break;

    if (tracker_index == command_queue->sparse.tracked_count)
    {
        vkd3d_array_reserve((void **)&command_queue->sparse.tracked,
                &command_queue->sparse.tracked_size,
                command_queue->sparse.tracked_count + 1,
                sizeof(*command_queue->sparse.tracked));

        command_queue->sparse.tracked[tracker_index].resource = dst_resource;
        command_queue->sparse.tracked[tracker_index].tile_mask =
                vkd3d_calloc(align(dst_resource->sparse.tile_count, 32) / 32, sizeof(uint32_t));
        command_queue->sparse.tracked_count++;
        check_hazard = false;

        d3d12_resource_incref(dst_resource);
    }

    tile_mask = command_queue->sparse.tracked[tracker_index].tile_mask;

    if (check_hazard)
    {
        for (i = 0; i < count; i++)
        {
            bind = &binds[i];
            if (tile_mask[bind->dst_tile / 32] & (1u << (bind->dst_tile & 31)))
            {
                /* Hazard detected. Flush the sparse submit and then restart. */

                /* No need to reallocate the tile mask block, temporarily pilfer it. */
                command_queue->sparse.tracked[tracker_index].tile_mask = NULL;
                d3d12_command_queue_flush_bind_sparse(command_queue);

                assert(command_queue->sparse.tracked_count == 0);
                assert(command_queue->sparse.tracked_size >= 1);
                command_queue->sparse.tracked[0].resource = dst_resource;
                command_queue->sparse.tracked[0].tile_mask = tile_mask;
                command_queue->sparse.tracked_count = 1;
                memset(tile_mask, 0, align(dst_resource->sparse.tile_count, 32) / 8);

                d3d12_resource_incref(dst_resource);
                break;
            }
        }
    }

    for (i = 0; i < count; i++)
    {
        bind = &binds[i];
        tile_mask[bind->dst_tile / 32] |= (1u << (bind->dst_tile & 31));
    }
}

static void d3d12_command_queue_bind_sparse(struct d3d12_command_queue *command_queue,
        enum vkd3d_sparse_memory_bind_mode mode, struct d3d12_resource *dst_resource,
        struct d3d12_resource *src_resource, unsigned int count,
        struct vkd3d_sparse_memory_bind *bind_infos)
{
    struct vkd3d_sparse_memory_bind_range *bind_ranges = NULL;
    unsigned int first_packed_tile, processed_tiles;
    VkSparseImageMemoryBindInfo *image_info = NULL;
    VkSparseImageMemoryBind *image_binds = NULL;
    VkSparseMemoryBind *memory_binds = NULL;
    unsigned int i, j, k;

    TRACE("queue %p, dst_resource %p, src_resource %p, count %u, bind_infos %p.\n",
          command_queue, dst_resource, src_resource, count, bind_infos);

    if (!(bind_ranges = vkd3d_malloc(count * sizeof(*bind_ranges))))
    {
        ERR("Failed to allocate bind range info.\n");
        goto cleanup;
    }

    d3d12_command_queue_register_sparse_hazard(command_queue, dst_resource, bind_infos, count);

    count = vkd3d_compact_sparse_bind_ranges(src_resource, bind_ranges, bind_infos, count, mode);

    first_packed_tile = dst_resource->sparse.tile_count;

    if (d3d12_resource_is_buffer(dst_resource))
    {
        VkSparseBufferMemoryBindInfo *buffer_info;

        if (!(memory_binds = vkd3d_malloc(count * sizeof(*memory_binds))))
        {
            ERR("Failed to allocate sparse memory bind info.\n");
            goto cleanup;
        }

        vkd3d_array_reserve((void **)&command_queue->sparse.buffer_binds,
                &command_queue->sparse.buffer_binds_size,
                command_queue->sparse.buffer_binds_count + 1,
                sizeof(command_queue->sparse.buffer_binds[0]));

        buffer_info = &command_queue->sparse.buffer_binds[command_queue->sparse.buffer_binds_count++];
        buffer_info->buffer = dst_resource->res.vk_buffer;
        buffer_info->bindCount = count;
        buffer_info->pBinds = memory_binds;
    }
    else
    {
        unsigned int opaque_bind_count = 0;
        unsigned int image_bind_count = 0;

        if (dst_resource->sparse.packed_mips.NumPackedMips)
            first_packed_tile = dst_resource->sparse.packed_mips.StartTileIndexInOverallResource;

        for (i = 0; i < count; i++)
        {
            const struct vkd3d_sparse_memory_bind_range *bind = &bind_ranges[i];

            if (bind->tile_index < first_packed_tile)
                image_bind_count += bind->tile_count;
            if (bind->tile_index + bind->tile_count > first_packed_tile)
                opaque_bind_count++;
        }

        if (opaque_bind_count)
        {
            VkSparseImageOpaqueMemoryBindInfo *opaque_info;
            if (!(memory_binds = vkd3d_malloc(opaque_bind_count * sizeof(*memory_binds))))
            {
                ERR("Failed to allocate sparse memory bind info.\n");
                goto cleanup;
            }

            vkd3d_array_reserve((void **)&command_queue->sparse.image_opaque_binds,
                    &command_queue->sparse.image_opaque_binds_size,
                    command_queue->sparse.image_opaque_binds_count + 1,
                    sizeof(command_queue->sparse.image_opaque_binds[0]));

            opaque_info = &command_queue->sparse.image_opaque_binds[command_queue->sparse.image_opaque_binds_count++];
            opaque_info->image = dst_resource->res.vk_image;
            opaque_info->bindCount = opaque_bind_count;
            opaque_info->pBinds = memory_binds;
        }

        if (image_bind_count)
        {
            if (!(image_binds = vkd3d_malloc(image_bind_count * sizeof(*image_binds))))
            {
                ERR("Failed to allocate sparse memory bind info.\n");
                goto cleanup;
            }

            vkd3d_array_reserve((void **)&command_queue->sparse.image_binds,
                    &command_queue->sparse.image_binds_size,
                    command_queue->sparse.image_binds_count + 1,
                    sizeof(command_queue->sparse.image_binds[0]));

            /* The image bind count is not exact but only an upper limit,
             * so do the actual counting while filling in bind infos */
            image_info = &command_queue->sparse.image_binds[command_queue->sparse.image_binds_count++];
            image_info->image = dst_resource->res.vk_image;
            image_info->bindCount = 0;
            image_info->pBinds = image_binds;
        }
    }

    for (i = 0, k = 0; i < count; i++)
    {
        struct vkd3d_sparse_memory_bind_range *bind = &bind_ranges[i];
        command_queue->sparse.total_tiles += bind->tile_count;

        while (bind->tile_count)
        {
            struct d3d12_sparse_tile *tile = &dst_resource->sparse.tiles[bind->tile_index];

            if (d3d12_resource_is_texture(dst_resource) && bind->tile_index < first_packed_tile)
            {
                const D3D12_SUBRESOURCE_TILING *tiling = &dst_resource->sparse.tilings[tile->image.subresource_index];
                const uint32_t tile_count = tiling->WidthInTiles * tiling->HeightInTiles * tiling->DepthInTiles;

                if (bind->tile_index == tiling->StartTileIndexInOverallResource && bind->tile_count >= tile_count)
                {
                    /* Bind entire subresource at once to reduce overhead */
                    const struct d3d12_sparse_tile *last_tile = &tile[tile_count - 1];

                    VkSparseImageMemoryBind *vk_bind = &image_binds[image_info->bindCount++];
                    vk_bind->subresource = tile->image.subresource;
                    vk_bind->offset = tile->image.offset;
                    vk_bind->extent.width = last_tile->image.offset.x + last_tile->image.extent.width;
                    vk_bind->extent.height = last_tile->image.offset.y + last_tile->image.extent.height;
                    vk_bind->extent.depth = last_tile->image.offset.z + last_tile->image.extent.depth;
                    vk_bind->memory = bind->vk_memory;
                    vk_bind->memoryOffset = bind->vk_offset;
                    vk_bind->flags = 0;

                    processed_tiles = tile_count;
                }
                else
                {
                    VkSparseImageMemoryBind *vk_bind = &image_binds[image_info->bindCount++];
                    vk_bind->subresource = tile->image.subresource;
                    vk_bind->offset = tile->image.offset;
                    vk_bind->extent = tile->image.extent;
                    vk_bind->memory = bind->vk_memory;
                    vk_bind->memoryOffset = bind->vk_offset;
                    vk_bind->flags = 0;

                    processed_tiles = 1;
                }
            }
            else
            {
                const struct d3d12_sparse_tile *last_tile = &tile[bind->tile_count - 1];

                VkSparseMemoryBind *vk_bind = &memory_binds[k++];
                vk_bind->resourceOffset = tile->buffer.offset;
                vk_bind->size = last_tile->buffer.offset
                              + last_tile->buffer.length
                              - vk_bind->resourceOffset;
                vk_bind->memory = bind->vk_memory;
                vk_bind->memoryOffset = bind->vk_offset;
                vk_bind->flags = 0;

                processed_tiles = bind->tile_count;
            }

            for (j = 0; j < processed_tiles; j++)
            {
                tile[j].vk_memory = bind->vk_memory;
                tile[j].vk_offset = bind->vk_offset + j * VKD3D_TILE_SIZE;
            }

            bind->tile_index += processed_tiles;
            bind->tile_count -= processed_tiles;
            bind->vk_offset += processed_tiles * VKD3D_TILE_SIZE;
        }
    }

cleanup:
    vkd3d_free(bind_ranges);

    d3d12_resource_decref(dst_resource);
}

static void d3d12_command_queue_flush_bind_sparse(struct d3d12_command_queue *command_queue)
{
    struct vkd3d_waiting_fence_sparse_bind_info *resource_info;
    struct vkd3d_queue_timeline_trace_cookie cookie;
    const struct vkd3d_vk_device_procs *vk_procs;
    VkTimelineSemaphoreSubmitInfo timeline_info;
    struct vkd3d_queue *queue, *queue_sparse;
    struct vkd3d_fence_wait_info fence_info;
    VkQueue vk_queue, vk_queue_sparse;
    VkSemaphoreSubmitInfo signal_info;
    VkBindSparseInfo bind_sparse_info;
    uint64_t wait_semaphore_value;
    VkResult vr;
    uint32_t i;

    if (command_queue->sparse.buffer_binds_count == 0 &&
            command_queue->sparse.image_binds_count == 0 &&
            command_queue->sparse.image_opaque_binds_count == 0)
    {
        return;
    }

    cookie = vkd3d_queue_timeline_trace_register_generic_region(&command_queue->device->queue_timeline_trace, "SPARSE FLUSH");

    vk_procs = &command_queue->device->vk_procs;

    /* Ensure that we use a queue that supports sparse binding */
    queue = command_queue->vkd3d_queue;

    if (!(queue->vk_queue_flags & VK_QUEUE_SPARSE_BINDING_BIT))
        queue_sparse = command_queue->device->internal_sparse_queue;
    else
        queue_sparse = queue;

    if (!(vk_queue = vkd3d_queue_acquire(queue)))
    {
        ERR("Failed to acquire queue %p.\n", queue);
        goto cleanup;
    }

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
    {
        vk_queue_sparse = vk_queue;
    }

    /* Use the queue's submission timeline to serialize sparse binding with
     * regular command execution. Pending waiters are flushed beforehand. */
    wait_semaphore_value = queue->submission_timeline_count++;

    memset(&timeline_info, 0, sizeof(timeline_info));
    timeline_info.sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO;

    timeline_info.waitSemaphoreValueCount = 1;
    timeline_info.pWaitSemaphoreValues = &wait_semaphore_value;
    timeline_info.signalSemaphoreValueCount = 1;
    timeline_info.pSignalSemaphoreValues = &queue->submission_timeline_count;

    memset(&bind_sparse_info, 0, sizeof(bind_sparse_info));
    bind_sparse_info.sType = VK_STRUCTURE_TYPE_BIND_SPARSE_INFO;
    bind_sparse_info.pNext = &timeline_info;
    bind_sparse_info.pWaitSemaphores = &queue->submission_timeline;
    bind_sparse_info.pSignalSemaphores = &queue->submission_timeline;
    bind_sparse_info.waitSemaphoreCount = 1;
    bind_sparse_info.signalSemaphoreCount = 1;
    bind_sparse_info.bufferBindCount = command_queue->sparse.buffer_binds_count;
    bind_sparse_info.imageOpaqueBindCount = command_queue->sparse.image_opaque_binds_count;
    bind_sparse_info.imageBindCount = command_queue->sparse.image_binds_count;
    bind_sparse_info.pBufferBinds = command_queue->sparse.buffer_binds;
    bind_sparse_info.pImageBinds = command_queue->sparse.image_binds;
    bind_sparse_info.pImageOpaqueBinds = command_queue->sparse.image_opaque_binds;

    if ((vr = VK_CALL(vkQueueBindSparse(vk_queue_sparse, 1, &bind_sparse_info, VK_NULL_HANDLE))) < 0)
        ERR("Failed to perform sparse binding, vr %d.\n", vr);

    if (queue != queue_sparse)
        vkd3d_queue_release(queue_sparse);

    /* Ensure that subsequent submissions and signals can observe the effect
     * of the sparse bind operation, but do not submit a wait immediately. */
    memset(&signal_info, 0, sizeof(signal_info));
    signal_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    signal_info.semaphore = queue->submission_timeline;
    signal_info.value = queue->submission_timeline_count;
    signal_info.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;

    d3d12_command_queue_add_wait_semaphores(command_queue, 1, &signal_info);

    command_queue->last_submission_timeline_value = queue->submission_timeline_count;

    memset(&fence_info, 0, sizeof(fence_info));
    fence_info.vk_semaphore = queue->submission_timeline;
    fence_info.vk_semaphore_value = queue->submission_timeline_count;

    resource_info = vkd3d_waiting_fence_set_callback(&fence_info,
            &vkd3d_waiting_fence_release_sparse_resources, sizeof(*resource_info));
    resource_info->num_resources = command_queue->sparse.tracked_count;
    resource_info->resources = vkd3d_calloc(resource_info->num_resources, sizeof(*resource_info->resources));

    for (i = 0; i < command_queue->sparse.tracked_count; i++)
    {
        resource_info->resources[i] = command_queue->sparse.tracked[i].resource;
        d3d12_resource_incref(resource_info->resources[i]);
    }

    vkd3d_queue_release(queue);
    VKD3D_DEVICE_REPORT_FAULT_AND_BREADCRUMB_IF(command_queue->device, vr == VK_ERROR_DEVICE_LOST);

    vkd3d_queue_timeline_trace_complete_execute(&command_queue->device->queue_timeline_trace, &command_queue->fence_worker, cookie);

    cookie = vkd3d_queue_timeline_trace_register_sparse(&command_queue->device->queue_timeline_trace, command_queue->sparse.total_tiles);

    if (FAILED(vkd3d_enqueue_timeline_semaphore(&command_queue->fence_worker, &fence_info, &cookie)))
        ERR("Failed to enqueue timeline semaphore.\n");

cleanup:
    for (i = 0; i < command_queue->sparse.buffer_binds_count; i++)
        vkd3d_free((void *)command_queue->sparse.buffer_binds[i].pBinds);
    for (i = 0; i < command_queue->sparse.image_binds_count; i++)
        vkd3d_free((void *)command_queue->sparse.image_binds[i].pBinds);
    for (i = 0; i < command_queue->sparse.image_opaque_binds_count; i++)
        vkd3d_free((void *)command_queue->sparse.image_opaque_binds[i].pBinds);
    for (i = 0; i < command_queue->sparse.tracked_count; i++)
    {
        d3d12_resource_decref(command_queue->sparse.tracked[i].resource);
        vkd3d_free((void *)command_queue->sparse.tracked[i].tile_mask);
    }

    command_queue->sparse.buffer_binds_count = 0;
    command_queue->sparse.image_binds_count = 0;
    command_queue->sparse.image_opaque_binds_count = 0;
    command_queue->sparse.tracked_count = 0;
    command_queue->sparse.total_tiles = 0;
}

void d3d12_command_queue_submit_stop(struct d3d12_command_queue *queue)
{
    struct d3d12_command_queue_submission sub;
    sub.type = VKD3D_SUBMISSION_STOP;
    d3d12_command_queue_add_submission(queue, &sub);
}

void d3d12_command_queue_enqueue_callback(struct d3d12_command_queue *queue, void (*callback)(void *), void *userdata)
{
    struct d3d12_command_queue_submission sub;
    sub.type = VKD3D_SUBMISSION_CALLBACK;
    sub.callback.callback = callback;
    sub.callback.userdata = userdata;
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
    /* Ensure that any non-temporal writes from CopyDescriptors are ordered properly
     * with the submission thread that calls vkQueueSubmit. */
    if (d3d12_device_use_embedded_mutable_descriptors(queue->device))
        vkd3d_memcpy_non_temporal_barrier();

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

void d3d12_command_queue_signal_inline(struct d3d12_command_queue *queue, d3d12_fence_iface *fence, uint64_t value)
{
    if (is_shared_ID3D12Fence1(fence))
        d3d12_command_queue_signal_shared(queue, shared_impl_from_ID3D12Fence1(fence), value);
    else
        d3d12_command_queue_signal(queue, impl_from_ID3D12Fence1(fence), value);
}

static void *d3d12_command_queue_submission_worker_main(void *userdata)
{
    struct d3d12_command_queue_submission submission;
    struct d3d12_command_queue_transition_pool pool;
    struct vkd3d_queue_timeline_trace_cookie cookie;
    struct d3d12_command_queue *queue = userdata;
    VkSemaphoreSubmitInfo transition_semaphore;
    VkCommandBufferSubmitInfo transition_cmd;
    VKD3D_UNUSED unsigned int i;
    HRESULT hr;

    VKD3D_REGION_DECL(queue_wait);
    VKD3D_REGION_DECL(queue_signal);
    VKD3D_REGION_DECL(queue_execute);

    vkd3d_set_thread_name("vkd3d_queue");
    queue->submission_thread_tid = vkd3d_get_current_thread_id();

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

        if (submission.type != VKD3D_SUBMISSION_BIND_SPARSE)
            d3d12_command_queue_flush_bind_sparse(queue);

        switch (submission.type)
        {
        case VKD3D_SUBMISSION_STOP:
            cookie = vkd3d_queue_timeline_trace_register_generic_region(&queue->device->queue_timeline_trace, "STOP");
            goto cleanup;

        case VKD3D_SUBMISSION_WAIT:
            VKD3D_REGION_BEGIN(queue_wait);
            if (is_shared_ID3D12Fence1(submission.wait.fence))
            {
                cookie = vkd3d_queue_timeline_trace_register_generic_region(&queue->device->queue_timeline_trace, "WAIT (shared)");
                d3d12_command_queue_flush_waiters(queue, 0u);
                d3d12_command_queue_wait_shared(queue, shared_impl_from_ID3D12Fence1(submission.wait.fence), submission.wait.value);
            }
            else
            {
                cookie = vkd3d_queue_timeline_trace_register_generic_region(&queue->device->queue_timeline_trace, "WAIT (normal)");
                d3d12_command_queue_wait(queue, impl_from_ID3D12Fence1(submission.wait.fence), submission.wait.value);
            }

            d3d12_fence_iface_dec_ref(submission.wait.fence);
            VKD3D_REGION_END(queue_wait);
            break;

        case VKD3D_SUBMISSION_SIGNAL:
            cookie = vkd3d_queue_timeline_trace_register_generic_region(&queue->device->queue_timeline_trace, "SIGNAL");
            d3d12_command_queue_flush_waiters(queue, 0u);

            VKD3D_REGION_BEGIN(queue_signal);
            d3d12_command_queue_signal_inline(queue, submission.signal.fence, submission.signal.value);
            d3d12_fence_iface_dec_ref(submission.signal.fence);
            VKD3D_REGION_END(queue_signal);
            break;

        case VKD3D_SUBMISSION_EXECUTE:
            VKD3D_REGION_BEGIN(queue_execute);
            cookie = vkd3d_queue_timeline_trace_register_generic_region(&queue->device->queue_timeline_trace, "EXECUTE");

            memset(&transition_cmd, 0, sizeof(transition_cmd));
            transition_cmd.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;

            memset(&transition_semaphore, 0, sizeof(transition_semaphore));
            transition_semaphore.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
            transition_semaphore.semaphore = pool.timeline;
            transition_semaphore.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;

            d3d12_command_queue_transition_pool_build(&pool, queue->device,
                    submission.execute.transitions,
                    submission.execute.transition_count,
                    &transition_cmd.commandBuffer,
                    &transition_semaphore.value);

            d3d12_command_queue_execute(queue, &submission.execute, &transition_cmd, &transition_semaphore);

            /* command_queue_execute takes ownership of the
             * outstanding_submission_counters and queue_timeline_indices allocations.
             * The atomic counters are decremented when the submission is observed to be freed.
             * On error, the counters are freed early, so there is no risk of leak. */
            vkd3d_free(submission.execute.cmd);
            vkd3d_free(submission.execute.cmd_cost);
            vkd3d_free(submission.execute.transitions);
#ifdef VKD3D_ENABLE_BREADCRUMBS
            for (i = 0; i < submission.execute.breadcrumb_indices_count; i++)
            {
                INFO("=== Executing command list %u (context %u) on VkQueue %p, queue family %u ===\n",
                        i, submission.execute.breadcrumb_indices[i],
                        (void*)queue->vkd3d_queue->vk_queue, queue->vkd3d_queue->vk_family_index);
                vkd3d_breadcrumb_tracer_dump_command_list(&queue->device->breadcrumb_tracer,
                        submission.execute.breadcrumb_indices[i]);
                INFO("============================\n");
            }
            vkd3d_free(submission.execute.breadcrumb_indices);
#endif
            VKD3D_REGION_END(queue_execute);
            break;

        case VKD3D_SUBMISSION_BIND_SPARSE:
        {
            char tag[64];
            snprintf(tag, sizeof(tag), "SPARSE %p", submission.bind_sparse.dst_resource);
            cookie = vkd3d_queue_timeline_trace_register_generic_region(&queue->device->queue_timeline_trace, tag);
            d3d12_command_queue_flush_waiters(queue, VKD3D_WAIT_SEMAPHORES_EXTERNAL);

            d3d12_command_queue_bind_sparse(queue, submission.bind_sparse.mode,
                    submission.bind_sparse.dst_resource, submission.bind_sparse.src_resource,
                    submission.bind_sparse.bind_count, submission.bind_sparse.bind_infos);
            vkd3d_free(submission.bind_sparse.bind_infos);
            break;
        }

        case VKD3D_SUBMISSION_DRAIN:
            cookie = vkd3d_queue_timeline_trace_register_generic_region(&queue->device->queue_timeline_trace, "DRAIN");
            /* Full flush needed to synchronize interop */
            d3d12_command_queue_flush_waiters(queue, VKD3D_WAIT_SEMAPHORES_EXTERNAL | VKD3D_WAIT_SEMAPHORES_SERIALIZING);

            pthread_mutex_lock(&queue->queue_lock);
            queue->queue_drain_count++;
            pthread_cond_signal(&queue->queue_cond);
            pthread_mutex_unlock(&queue->queue_lock);
            break;

        case VKD3D_SUBMISSION_CALLBACK:
            cookie = vkd3d_queue_timeline_trace_register_generic_region(&queue->device->queue_timeline_trace, "CALLBACK");
            d3d12_command_queue_flush_waiters(queue, VKD3D_WAIT_SEMAPHORES_EXTERNAL | VKD3D_WAIT_SEMAPHORES_SERIALIZING);

            submission.callback.callback(submission.callback.userdata);
            break;

        default:
            ERR("Unrecognized submission type %u.\n", submission.type);
            break;
        }

        vkd3d_queue_timeline_trace_complete_execute(&queue->device->queue_timeline_trace, &queue->fence_worker, cookie);
    }

cleanup:
    d3d12_command_queue_wait_idle(queue);
    d3d12_command_queue_transition_pool_deinit(&pool, queue->device);
    d3d12_command_queue_destroy_serializing_semaphore(queue);
    return NULL;
}

static HRESULT d3d12_command_queue_init(struct d3d12_command_queue *queue,
        struct d3d12_device *device, const D3D12_COMMAND_QUEUE_DESC *desc, struct vkd3d_queue_family_info *family_info)
{
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    HRESULT hr;
    int rc;

    queue->ID3D12CommandQueue_iface.lpVtbl = &d3d12_command_queue_vtbl;
    queue->ID3D12CommandQueueExt_iface.lpVtbl = &d3d12_command_queue_vkd3d_ext_vtbl;
    queue->refcount = 1;

    queue->desc = *desc;
    if (!queue->desc.NodeMask)
        queue->desc.NodeMask = 0x1;

    queue->vkd3d_queue = d3d12_device_allocate_vkd3d_queue(family_info, queue);
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

    if (!queue->vkd3d_queue->barrier_command_buffer)
    {
        if (FAILED(hr = vkd3d_create_binary_semaphore(device, &queue->serializing_semaphore)))
            goto fail_create_semaphore;
    }

    if (FAILED(hr = vkd3d_private_store_init(&queue->private_store)))
        goto fail_private_store;

    if (FAILED(hr = dxgi_vk_swap_chain_factory_init(queue, &queue->vk_swap_chain_factory)))
        goto fail_swapchain_factory;

    d3d12_device_add_ref(queue->device = device);

    if (FAILED(hr = vkd3d_fence_worker_start(&queue->fence_worker, queue, device)))
        goto fail_fence_worker_start;

    if ((rc = pthread_create(&queue->submission_thread, NULL, d3d12_command_queue_submission_worker_main, queue)) < 0)
    {
        d3d12_device_release(queue->device);
        hr = hresult_from_errno(rc);
        goto fail_pthread_create;
    }

    d3d_destruction_notifier_init(&queue->destruction_notifier, (IUnknown*)&queue->ID3D12CommandQueue_iface);
    return S_OK;

fail_pthread_create:
    vkd3d_fence_worker_stop(&queue->fence_worker, device);
fail_fence_worker_start:;
fail_swapchain_factory:
    vkd3d_private_store_destroy(&queue->private_store);
fail_private_store:
    pthread_cond_destroy(&queue->queue_cond);
fail_create_semaphore:
    VK_CALL(vkDestroySemaphore(device->vk_device, queue->serializing_semaphore, NULL));
fail_pthread_cond:
    pthread_mutex_destroy(&queue->queue_lock);
fail:
    d3d12_device_unmap_vkd3d_queue(queue->vkd3d_queue, queue);
    return hr;
}

HRESULT d3d12_command_queue_create(struct d3d12_device *device,
        const D3D12_COMMAND_QUEUE_DESC *desc, uint32_t vk_family_index, struct d3d12_command_queue **queue)
{
    struct vkd3d_queue_family_info *family_info;
    struct d3d12_command_queue *object;
    HRESULT hr;

    if (!(object = vkd3d_calloc(1, sizeof(*object))))
        return E_OUTOFMEMORY;

    family_info = d3d12_device_get_vkd3d_queue_family(device, desc->Type, vk_family_index);

    if (FAILED(hr = d3d12_command_queue_init(object, device, desc, family_info)))
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

uint32_t vkd3d_get_vk_queue_index(ID3D12CommandQueue *queue)
{
    struct d3d12_command_queue *d3d12_queue = impl_from_ID3D12CommandQueue(queue);

    return d3d12_queue->vkd3d_queue->vk_queue_index;
}

uint32_t vkd3d_get_vk_queue_flags(ID3D12CommandQueue *queue)
{
    struct d3d12_command_queue *d3d12_queue = impl_from_ID3D12CommandQueue(queue);

    return d3d12_queue->vkd3d_queue->vk_queue_flags;
}

VkQueue vkd3d_acquire_vk_queue(ID3D12CommandQueue *queue)
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

void vkd3d_release_vk_queue(ID3D12CommandQueue *queue)
{
    struct d3d12_command_queue *d3d12_queue = impl_from_ID3D12CommandQueue(queue);
    const struct vkd3d_vk_device_procs *vk_procs = &d3d12_queue->device->vk_procs;
    VkSemaphoreSubmitInfo semaphore_info;
    VkSubmitInfo2 submit_info;
    VkResult vr;

    /* Need to increment the submission counter here so that fence
     * signals and waits behave as expected in an interop scenario */
    memset(&semaphore_info, 0, sizeof(semaphore_info));
    semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    semaphore_info.semaphore = d3d12_queue->vkd3d_queue->submission_timeline;
    semaphore_info.value = ++d3d12_queue->vkd3d_queue->submission_timeline_count;
    semaphore_info.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;

    memset(&submit_info, 0, sizeof(submit_info));
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    submit_info.signalSemaphoreInfoCount = 1;
    submit_info.pSignalSemaphoreInfos = &semaphore_info;

    vr = VK_CALL(vkQueueSubmit2(d3d12_queue->vkd3d_queue->vk_queue, 1, &submit_info, VK_NULL_HANDLE));
    VKD3D_DEVICE_REPORT_FAULT_AND_BREADCRUMB_IF(d3d12_queue->device, vr == VK_ERROR_DEVICE_LOST);

    d3d12_queue->last_submission_timeline_value = semaphore_info.value;

    vkd3d_queue_release(d3d12_queue->vkd3d_queue);
    d3d12_command_queue_release_serialized(d3d12_queue);
}

void vkd3d_enqueue_initial_transition(ID3D12CommandQueue *queue, ID3D12Resource *resource)
{
    struct d3d12_command_queue_submission sub;
    struct d3d12_command_queue *d3d12_queue = impl_from_ID3D12CommandQueue(queue);
    struct d3d12_resource *d3d12_resource = impl_from_ID3D12Resource(resource);

    memset(&sub, 0, sizeof(sub));
    sub.type = VKD3D_SUBMISSION_EXECUTE;
    sub.execute.low_latency_frame_id = d3d12_queue->device->frame_markers.render;
    sub.execute.transition_count = 1;
    sub.execute.transitions = vkd3d_malloc(sizeof(*sub.execute.transitions));
    sub.execute.transitions[0].type = VKD3D_INITIAL_TRANSITION_TYPE_RESOURCE;
    sub.execute.transitions[0].resource.resource = d3d12_resource;
    sub.execute.transitions[0].resource.perform_initial_transition = true;
    d3d12_command_queue_add_submission(d3d12_queue, &sub);
}

/* ID3D12CommandSignature */
static HRESULT STDMETHODCALLTYPE d3d12_command_signature_QueryInterface(ID3D12CommandSignature *iface,
        REFIID iid, void **out)
{
    struct d3d12_command_signature *signature = impl_from_ID3D12CommandSignature(iface);

    TRACE("iface %p, iid %s, out %p.\n", iface, debugstr_guid(iid), out);

    if (!out)
        return E_POINTER;

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

    if (IsEqualGUID(iid, &IID_ID3DDestructionNotifier))
    {
        ID3DDestructionNotifier_AddRef(&signature->destruction_notifier.ID3DDestructionNotifier_iface);
        *out = &signature->destruction_notifier.ID3DDestructionNotifier_iface;
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

static void d3d12_command_signature_cleanup(struct d3d12_command_signature *signature)
{
    const struct vkd3d_vk_device_procs *vk_procs = &signature->device->vk_procs;

    if (signature->requires_state_template_dgc)
    {
        VK_CALL(vkDestroyBuffer(signature->device->vk_device, signature->state_template.dgc.buffer, NULL));
        vkd3d_free_device_memory(signature->device, &signature->state_template.dgc.memory);

        if (signature->device->device_info.device_generated_commands_features_ext.deviceGeneratedCommands)
        {
            VK_CALL(vkDestroyIndirectCommandsLayoutEXT(signature->device->vk_device,
                    signature->state_template.dgc.layout_implicit_ext, NULL));
            VK_CALL(vkDestroyIndirectCommandsLayoutEXT(signature->device->vk_device,
                    signature->state_template.dgc.layout_preprocess_ext, NULL));
        }

        if (signature->device->device_info.device_generated_commands_features_nv.deviceGeneratedCommands)
        {
            VK_CALL(vkDestroyIndirectCommandsLayoutNV(signature->device->vk_device,
                    signature->state_template.dgc.layout_implicit_nv, NULL));
            VK_CALL(vkDestroyIndirectCommandsLayoutNV(signature->device->vk_device,
                    signature->state_template.dgc.layout_preprocess_nv, NULL));
        }
    }

    d3d_destruction_notifier_free(&signature->destruction_notifier);
    vkd3d_private_store_destroy(&signature->private_store);
    vkd3d_free((void *)signature->desc.pArgumentDescs);
    vkd3d_free(signature);
}

static ULONG STDMETHODCALLTYPE d3d12_command_signature_Release(ID3D12CommandSignature *iface)
{
    struct d3d12_command_signature *signature = impl_from_ID3D12CommandSignature(iface);
    ULONG refcount = InterlockedDecrement(&signature->refcount);

    TRACE("%p decreasing refcount to %u.\n", signature, refcount);

    if (!refcount)
    {
        struct d3d12_device *device = signature->device;
        d3d12_command_signature_cleanup(signature);
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

    return vkd3d_set_private_data(&signature->private_store, guid, data_size, data,
            NULL, NULL);
}

static HRESULT STDMETHODCALLTYPE d3d12_command_signature_SetPrivateDataInterface(ID3D12CommandSignature *iface,
        REFGUID guid, const IUnknown *data)
{
    struct d3d12_command_signature *signature = impl_from_ID3D12CommandSignature(iface);

    TRACE("iface %p, guid %s, data %p.\n", iface, debugstr_guid(guid), data);

    return vkd3d_set_private_data_interface(&signature->private_store, guid, data,
            NULL, NULL);
}

static HRESULT STDMETHODCALLTYPE d3d12_command_signature_GetDevice(ID3D12CommandSignature *iface, REFIID iid, void **device)
{
    struct d3d12_command_signature *signature = impl_from_ID3D12CommandSignature(iface);

    TRACE("iface %p, iid %s, device %p.\n", iface, debugstr_guid(iid), device);

    return d3d12_device_query_interface(signature->device, iid, device);
}

CONST_VTBL struct ID3D12CommandSignatureVtbl d3d12_command_signature_vtbl =
{
    /* IUnknown methods */
    d3d12_command_signature_QueryInterface,
    d3d12_command_signature_AddRef,
    d3d12_command_signature_Release,
    /* ID3D12Object methods */
    d3d12_command_signature_GetPrivateData,
    d3d12_command_signature_SetPrivateData,
    d3d12_command_signature_SetPrivateDataInterface,
    (void *)d3d12_object_SetName,
    /* ID3D12DeviceChild methods */
    d3d12_command_signature_GetDevice,
};

struct vkd3d_patch_command
{
    enum vkd3d_patch_command_token token;
    uint32_t src_offset;
    uint32_t dst_offset;
};

static HRESULT d3d12_command_signature_init_patch_commands_buffer(struct d3d12_command_signature *signature,
        struct d3d12_device *device,
        const struct vkd3d_patch_command *commands, size_t command_count)
{
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    D3D12_RESOURCE_DESC1 buffer_desc;
    D3D12_HEAP_PROPERTIES heap_info;
    HRESULT hr = S_OK;
    VkResult vr;
    void *ptr;

    memset(&heap_info, 0, sizeof(heap_info));
    heap_info.Type = device->memory_info.has_gpu_upload_heap ?
            D3D12_HEAP_TYPE_GPU_UPLOAD : D3D12_HEAP_TYPE_UPLOAD;
    memset(&buffer_desc, 0, sizeof(buffer_desc));
    buffer_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    buffer_desc.Width = command_count * sizeof(struct vkd3d_patch_command);
    buffer_desc.Height = 1;
    buffer_desc.DepthOrArraySize = 1;
    buffer_desc.SampleDesc.Count = 1;
    buffer_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    buffer_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    if (FAILED(hr = vkd3d_create_buffer(device, &heap_info, D3D12_HEAP_FLAG_NONE,
            &buffer_desc, "dgc-state-template", &signature->state_template.dgc.buffer)))
        return hr;

    if (FAILED(hr = vkd3d_allocate_internal_buffer_memory(device, signature->state_template.dgc.buffer,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            &signature->state_template.dgc.memory)))
        return hr;

    signature->state_template.dgc.buffer_va = vkd3d_get_buffer_device_address(device,
            signature->state_template.dgc.buffer);

    if ((vr = VK_CALL(vkMapMemory(device->vk_device, signature->state_template.dgc.memory.vk_memory,
            0, VK_WHOLE_SIZE, 0, (void**)&ptr))))
        return hr;

    memcpy(ptr, commands, command_count * sizeof(struct vkd3d_patch_command));
    VK_CALL(vkUnmapMemory(device->vk_device, signature->state_template.dgc.memory.vk_memory));

    return hr;
}

static HRESULT d3d12_command_signature_init_indirect_commands_layout_nv(
        struct d3d12_command_signature *signature, struct d3d12_device *device,
        const VkIndirectCommandsLayoutTokenNV *tokens, uint32_t token_count,
        uint32_t stream_stride)
{
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    VkIndirectCommandsLayoutCreateInfoNV create_info;
    VkResult vr;

    memset(&create_info, 0, sizeof(create_info));
    create_info.sType = VK_STRUCTURE_TYPE_INDIRECT_COMMANDS_LAYOUT_CREATE_INFO_NV;
    create_info.pipelineBindPoint = signature->pipeline_type == VKD3D_PIPELINE_TYPE_COMPUTE ?
            VK_PIPELINE_BIND_POINT_COMPUTE : VK_PIPELINE_BIND_POINT_GRAPHICS;
    create_info.streamCount = 1;
    create_info.pStreamStrides = &stream_stride;
    create_info.tokenCount = token_count;
    create_info.pTokens = tokens;

    signature->state_template.dgc.stride = stream_stride;

    if (token_count > device->device_info.device_generated_commands_properties_nv.maxIndirectCommandsTokenCount)
    {
        FIXME("Token count %u is too large (max %u).\n",
                token_count, device->device_info.device_generated_commands_properties_nv.maxIndirectCommandsTokenCount);
        return E_NOTIMPL;
    }

    /* Need two separate DGC layouts since if we set EXPLICIT_PREPROCESS, we must use preprocess if the flag is set.
     * We don't always want to use explicit preprocess (especially when we cannot hoist), so pick the appropriate
     * layout at ExecuteIndirect time. */
    vr = VK_CALL(vkCreateIndirectCommandsLayoutNV(device->vk_device, &create_info, NULL,
            &signature->state_template.dgc.layout_implicit_nv));
    if (vr != VK_SUCCESS)
        return hresult_from_vk_result(vr);

    create_info.flags = VK_INDIRECT_COMMANDS_LAYOUT_USAGE_EXPLICIT_PREPROCESS_BIT_NV;
    vr = VK_CALL(vkCreateIndirectCommandsLayoutNV(device->vk_device, &create_info, NULL,
            &signature->state_template.dgc.layout_preprocess_nv));
    return hresult_from_vk_result(vr);
}

static HRESULT d3d12_command_signature_init_indirect_commands_layout_ext(
        struct d3d12_command_signature *signature,
        struct d3d12_root_signature *root_signature, struct d3d12_device *device,
        const VkIndirectCommandsLayoutTokenEXT *tokens, uint32_t token_count,
        uint32_t stream_stride)
{
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    VkIndirectCommandsLayoutCreateInfoEXT create_info;
    VkResult vr;

    memset(&create_info, 0, sizeof(create_info));
    create_info.sType = VK_STRUCTURE_TYPE_INDIRECT_COMMANDS_LAYOUT_CREATE_INFO_EXT;

    switch (signature->pipeline_type)
    {
        case VKD3D_PIPELINE_TYPE_COMPUTE:
            create_info.shaderStages = VK_SHADER_STAGE_COMPUTE_BIT;
            break;
        case VKD3D_PIPELINE_TYPE_GRAPHICS:
            create_info.shaderStages = VK_SHADER_STAGE_ALL_GRAPHICS;
            break;
        case VKD3D_PIPELINE_TYPE_MESH_GRAPHICS:
            create_info.shaderStages =
                    VK_SHADER_STAGE_MESH_BIT_EXT |
                    VK_SHADER_STAGE_TASK_BIT_EXT |
                    VK_SHADER_STAGE_FRAGMENT_BIT;
            break;
        default:
            FIXME("DGC layout for pipeline not implemented");
            return E_NOTIMPL;
    }

    create_info.indirectStride = stream_stride;
    if (root_signature)
        create_info.pipelineLayout = d3d12_root_signature_get_layout(root_signature, signature->pipeline_type)->vk_pipeline_layout;
    create_info.tokenCount = token_count;
    create_info.pTokens = tokens;

    signature->state_template.dgc.stride = stream_stride;

    if (token_count > device->device_info.device_generated_commands_properties_ext.maxIndirectCommandsTokenCount)
    {
        FIXME("Token count %u is too large (max %u).\n",
                token_count, device->device_info.device_generated_commands_properties_ext.maxIndirectCommandsTokenCount);
        return E_NOTIMPL;
    }

    /* Need two separate DGC layouts since if we set EXPLICIT_PREPROCESS, we must use preprocess if the flag is set.
     * We don't always want to use explicit preprocess (especially when we cannot hoist), so pick the appropriate
     * layout at ExecuteIndirect time. */
    vr = VK_CALL(vkCreateIndirectCommandsLayoutEXT(device->vk_device, &create_info, NULL,
            &signature->state_template.dgc.layout_implicit_ext));
    if (vr != VK_SUCCESS)
        return hresult_from_vk_result(vr);

    create_info.flags = VK_INDIRECT_COMMANDS_LAYOUT_USAGE_EXPLICIT_PREPROCESS_BIT_EXT;
    vr = VK_CALL(vkCreateIndirectCommandsLayoutEXT(device->vk_device, &create_info, NULL,
            &signature->state_template.dgc.layout_preprocess_ext));
    return hresult_from_vk_result(vr);
}

static HRESULT d3d12_command_signature_allocate_stream_memory_for_list(
        struct d3d12_command_list *list,
        struct d3d12_command_signature *signature,
        uint32_t max_command_count,
        struct vkd3d_scratch_allocation *allocation)
{
    if (!d3d12_command_allocator_allocate_scratch_memory(list->allocator,
            VKD3D_SCRATCH_POOL_KIND_DEVICE_STORAGE,
            max_command_count * signature->state_template.dgc.stride,
            list->device->device_info.device_generated_commands_properties_nv.minIndirectCommandsBufferOffsetAlignment,
            ~0u, allocation))
        return E_OUTOFMEMORY;

    return S_OK;
}

static HRESULT d3d12_command_signature_allocate_preprocess_memory_for_list_ext(
        struct d3d12_command_list *list,
        struct d3d12_command_signature *signature, VkPipeline render_pipeline, bool explicit_preprocess,
        uint32_t max_command_count,
        struct vkd3d_scratch_allocation *allocation, VkDeviceSize *size)
{
    const struct vkd3d_vk_device_procs *vk_procs = &list->device->vk_procs;
    VkGeneratedCommandsMemoryRequirementsInfoEXT info;
    VkGeneratedCommandsPipelineInfoEXT pipeline_info;
    VkMemoryRequirements2 memory_info;
    uint32_t alignment;

    memory_info.sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2;
    memory_info.pNext = NULL;

    memset(&pipeline_info, 0, sizeof(pipeline_info));
    pipeline_info.sType = VK_STRUCTURE_TYPE_GENERATED_COMMANDS_PIPELINE_INFO_EXT;
    pipeline_info.pipeline = render_pipeline;

    memset(&info, 0, sizeof(info));
    info.sType = VK_STRUCTURE_TYPE_GENERATED_COMMANDS_MEMORY_REQUIREMENTS_INFO_EXT;
    info.pNext = &pipeline_info;
    info.maxSequenceCount = max_command_count;
    info.indirectCommandsLayout = explicit_preprocess ?
            signature->state_template.dgc.layout_preprocess_ext :
            signature->state_template.dgc.layout_implicit_ext;

    if (max_command_count > list->device->device_info.device_generated_commands_properties_ext.maxIndirectSequenceCount)
    {
        FIXME("max_command_count %u exceeds device limit %u.\n",
                max_command_count,
                list->device->device_info.device_generated_commands_properties_ext.maxIndirectSequenceCount);
        return E_NOTIMPL;
    }

    VK_CALL(vkGetGeneratedCommandsMemoryRequirementsEXT(list->device->vk_device, &info, &memory_info));

    alignment = max(memory_info.memoryRequirements.alignment, 4);

    if (!d3d12_command_allocator_allocate_scratch_memory(list->allocator,
            VKD3D_SCRATCH_POOL_KIND_INDIRECT_PREPROCESS,
            memory_info.memoryRequirements.size,
            alignment,
            memory_info.memoryRequirements.memoryTypeBits, allocation))
        return E_OUTOFMEMORY;

    /* Going to assume the memory type is okay ... It's device local after all. */
    *size = memory_info.memoryRequirements.size;
    return S_OK;
}

static HRESULT d3d12_command_signature_allocate_preprocess_memory_for_list_nv(
        struct d3d12_command_list *list,
        struct d3d12_command_signature *signature, VkPipeline render_pipeline, bool explicit_preprocess,
        uint32_t max_command_count,
        struct vkd3d_scratch_allocation *allocation, VkDeviceSize *size)
{
    const struct vkd3d_vk_device_procs *vk_procs = &list->device->vk_procs;
    VkGeneratedCommandsMemoryRequirementsInfoNV info;
    VkMemoryRequirements2 memory_info;
    uint32_t alignment;

    memory_info.sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2;
    memory_info.pNext = NULL;

    info.pipelineBindPoint = signature->pipeline_type == VKD3D_PIPELINE_TYPE_COMPUTE ?
            VK_PIPELINE_BIND_POINT_COMPUTE : VK_PIPELINE_BIND_POINT_GRAPHICS;
    info.sType = VK_STRUCTURE_TYPE_GENERATED_COMMANDS_MEMORY_REQUIREMENTS_INFO_NV;
    info.pNext = NULL;
    info.maxSequencesCount = max_command_count;
    info.pipeline = render_pipeline;
    info.indirectCommandsLayout = explicit_preprocess ?
            signature->state_template.dgc.layout_preprocess_nv :
            signature->state_template.dgc.layout_implicit_nv;

    if (max_command_count > list->device->device_info.device_generated_commands_properties_nv.maxIndirectSequenceCount)
    {
        FIXME("max_command_count %u exceeds device limit %u.\n",
                max_command_count,
                list->device->device_info.device_generated_commands_properties_nv.maxIndirectSequenceCount);
        return E_NOTIMPL;
    }

    VK_CALL(vkGetGeneratedCommandsMemoryRequirementsNV(list->device->vk_device, &info, &memory_info));

    alignment = max(memory_info.memoryRequirements.alignment,
            list->device->device_info.device_generated_commands_properties_nv.minIndirectCommandsBufferOffsetAlignment);

    if (!d3d12_command_allocator_allocate_scratch_memory(list->allocator,
            VKD3D_SCRATCH_POOL_KIND_INDIRECT_PREPROCESS,
            memory_info.memoryRequirements.size,
            alignment,
            memory_info.memoryRequirements.memoryTypeBits, allocation))
        return E_OUTOFMEMORY;

    /* Going to assume the memory type is okay ... It's device local after all. */
    *size = memory_info.memoryRequirements.size;
    return S_OK;
}

static HRESULT d3d12_command_signature_init_state_template_compute(struct d3d12_command_signature *signature,
        const D3D12_COMMAND_SIGNATURE_DESC *desc,
        struct d3d12_root_signature *root_signature,
        struct d3d12_device *device)
{
    /* Compute templates are simpler, since the only state that can change is
     * root constants and root descriptors, so we can work around it with some heroics.
     * The implementation strategy for a non-DGC path is to upload a 256 byte buffer
     * with default command list root parameter state.
     * The input is either copied from the buffer directly, or it's read from the indirect buffer and replaces
     * the default input. This can be done in parallel with 64 threads per dispatch.
     * Some threads per workgroup will then copy the indirect dispatch parameters
     * (or clear them to 0 if indirect count needs to mask the dispatch). */
    const struct vkd3d_shader_root_parameter *root_parameter;
    const struct vkd3d_shader_root_constant *root_constant;
    uint32_t root_parameter_index;
    uint32_t src_offset_words = 0;
    uint32_t dst_offset_word;
    unsigned int i, j;

    for (i = 0; i < ARRAY_SIZE(signature->state_template.compute.source_offsets); i++)
        signature->state_template.compute.source_offsets[i] = -1;

    for (i = 0; i < desc->NumArgumentDescs; i++)
    {
        const D3D12_INDIRECT_ARGUMENT_DESC *argument_desc = &desc->pArgumentDescs[i];

        switch (argument_desc->Type)
        {
            case D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT:
                root_parameter_index = argument_desc->Constant.RootParameterIndex;
                root_constant = root_signature_get_32bit_constants(root_signature, root_parameter_index);

                dst_offset_word = root_constant->constant_index + argument_desc->Constant.DestOffsetIn32BitValues;
                for (j = 0; j < argument_desc->Constant.Num32BitValuesToSet; j++, src_offset_words++)
                    signature->state_template.compute.source_offsets[dst_offset_word + j] = (int32_t)src_offset_words;
                break;

            case D3D12_INDIRECT_ARGUMENT_TYPE_UNORDERED_ACCESS_VIEW:
            case D3D12_INDIRECT_ARGUMENT_TYPE_SHADER_RESOURCE_VIEW:
            case D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT_BUFFER_VIEW:
                root_parameter_index = argument_desc->ShaderResourceView.RootParameterIndex;
                root_parameter = root_signature_get_parameter(root_signature, root_parameter_index);

                if (!(root_signature->root_descriptor_raw_va_mask & (1ull << root_parameter_index)))
                {
                    ERR("Root parameter %u is not a raw VA. Cannot implement command signature which updates root descriptor.\n",
                            root_parameter_index);
                    return E_NOTIMPL;
                }

                dst_offset_word = root_parameter->descriptor.raw_va_root_descriptor_index * sizeof(VkDeviceAddress) / sizeof(uint32_t);
                for (j = 0; j < sizeof(VkDeviceAddress) / sizeof(uint32_t); j++, src_offset_words++)
                    signature->state_template.compute.source_offsets[dst_offset_word + j] = (int32_t)src_offset_words;
                break;

            case D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH:
                signature->state_template.compute.dispatch_offset_words = src_offset_words;
                break;

            default:
                FIXME("Unsupported token type %u.\n", argument_desc->Type);
                return E_NOTIMPL;
        }
    }

    /* No need to build a specialized pipeline here, there is a generic pipeline to handle compute. */

    return S_OK;
}

static HRESULT d3d12_command_signature_init_state_template_dgc_nv(struct d3d12_command_signature *signature,
        const D3D12_COMMAND_SIGNATURE_DESC *desc,
        struct d3d12_root_signature *root_signature,
        struct d3d12_device *device)
{
    const enum vkd3d_patch_command_token *generic_u32_copy_types;
    const struct vkd3d_shader_root_parameter *root_parameter;
    const struct d3d12_bind_point_layout *bind_point_layout;
    const struct vkd3d_shader_root_constant *root_constant;
    struct vkd3d_patch_command *patch_commands = NULL;
    VkIndirectCommandsLayoutTokenNV *tokens = NULL;
    uint32_t required_stride_alignment = 0;
    VkIndirectCommandsLayoutTokenNV token;
    uint32_t generic_u32_copy_count;
    size_t patch_commands_count = 0;
    uint32_t required_alignment = 0;
    size_t patch_commands_size = 0;
    uint32_t root_parameter_index;
    uint32_t src_word_offset = 0;
    uint32_t stream_stride = 0;
    uint32_t dst_word_offset;
    size_t token_count = 0;
    size_t token_size = 0;
    HRESULT hr = S_OK;
    uint32_t i, j;

    /* Mostly for debug. Lets debug ring report what it is writing easily. */
    static const enum vkd3d_patch_command_token ibv_types[] =
    {
        VKD3D_PATCH_COMMAND_TOKEN_COPY_IBO_VA_LO,
        VKD3D_PATCH_COMMAND_TOKEN_COPY_IBO_VA_HI,
        VKD3D_PATCH_COMMAND_TOKEN_COPY_IBO_SIZE,
        VKD3D_PATCH_COMMAND_TOKEN_COPY_INDEX_FORMAT,
    };

    static const enum vkd3d_patch_command_token vbv_types[] =
    {
        VKD3D_PATCH_COMMAND_TOKEN_COPY_VBO_VA_LO,
        VKD3D_PATCH_COMMAND_TOKEN_COPY_VBO_VA_HI,
        VKD3D_PATCH_COMMAND_TOKEN_COPY_VBO_SIZE,
        VKD3D_PATCH_COMMAND_TOKEN_COPY_VBO_STRIDE,
    };

    static const enum vkd3d_patch_command_token draw_types[] =
    {
        VKD3D_PATCH_COMMAND_TOKEN_COPY_VERTEX_COUNT,
        VKD3D_PATCH_COMMAND_TOKEN_COPY_INSTANCE_COUNT,
        VKD3D_PATCH_COMMAND_TOKEN_COPY_FIRST_VERTEX,
        VKD3D_PATCH_COMMAND_TOKEN_COPY_FIRST_INSTANCE,
    };

    static const enum vkd3d_patch_command_token draw_indexed_types[] =
    {
        VKD3D_PATCH_COMMAND_TOKEN_COPY_INDEX_COUNT,
        VKD3D_PATCH_COMMAND_TOKEN_COPY_INSTANCE_COUNT,
        VKD3D_PATCH_COMMAND_TOKEN_COPY_FIRST_INDEX,
        VKD3D_PATCH_COMMAND_TOKEN_COPY_VERTEX_OFFSET,
        VKD3D_PATCH_COMMAND_TOKEN_COPY_FIRST_INSTANCE,
    };

    static const enum vkd3d_patch_command_token draw_mesh_types[] =
    {
        VKD3D_PATCH_COMMAND_TOKEN_COPY_MESH_TASKS_X,
        VKD3D_PATCH_COMMAND_TOKEN_COPY_MESH_TASKS_Y,
        VKD3D_PATCH_COMMAND_TOKEN_COPY_MESH_TASKS_Z,
    };

    static const enum vkd3d_patch_command_token va_types[] =
    {
        VKD3D_PATCH_COMMAND_TOKEN_COPY_ROOT_VA_LO,
        VKD3D_PATCH_COMMAND_TOKEN_COPY_ROOT_VA_HI,
    };

    static const VkIndexType vk_index_types[] = { VK_INDEX_TYPE_UINT32, VK_INDEX_TYPE_UINT16 };
    static const uint32_t d3d_index_types[] = { DXGI_FORMAT_R32_UINT, DXGI_FORMAT_R16_UINT };

    bind_point_layout = d3d12_root_signature_get_layout(root_signature, signature->pipeline_type);

    for (i = 0; i < desc->NumArgumentDescs; i++)
    {
        const D3D12_INDIRECT_ARGUMENT_DESC *argument_desc = &desc->pArgumentDescs[i];
        memset(&token, 0, sizeof(token));
        token.sType = VK_STRUCTURE_TYPE_INDIRECT_COMMANDS_LAYOUT_TOKEN_NV;
        generic_u32_copy_count = 0;
        dst_word_offset = 0;

        switch (argument_desc->Type)
        {
            case D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT:
                root_parameter_index = argument_desc->Constant.RootParameterIndex;
                root_constant = root_signature_get_32bit_constants(root_signature, root_parameter_index);

                if (bind_point_layout->flags & VKD3D_ROOT_SIGNATURE_USE_PUSH_CONSTANT_UNIFORM_BLOCK)
                {
                    ERR("Root signature uses push UBO for root parameters, but this feature requires push constant path.\n");
                    hr = E_NOTIMPL;
                    goto end;
                }

                token.tokenType = VK_INDIRECT_COMMANDS_TOKEN_TYPE_PUSH_CONSTANT_NV;
                token.pushconstantPipelineLayout = bind_point_layout->vk_pipeline_layout;
                token.pushconstantShaderStageFlags = bind_point_layout->vk_push_stages;
                token.pushconstantOffset = root_constant->constant_index + argument_desc->Constant.DestOffsetIn32BitValues;
                token.pushconstantSize = argument_desc->Constant.Num32BitValuesToSet;
                token.pushconstantOffset *= sizeof(uint32_t);
                token.pushconstantSize *= sizeof(uint32_t);
                required_alignment = sizeof(uint32_t);

                stream_stride = align(stream_stride, required_alignment);
                token.offset = stream_stride;
                stream_stride += token.pushconstantSize;
                dst_word_offset = token.offset / sizeof(uint32_t);

                generic_u32_copy_count = argument_desc->Constant.Num32BitValuesToSet;
                generic_u32_copy_types = NULL;
                break;

            case D3D12_INDIRECT_ARGUMENT_TYPE_UNORDERED_ACCESS_VIEW:
            case D3D12_INDIRECT_ARGUMENT_TYPE_SHADER_RESOURCE_VIEW:
            case D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT_BUFFER_VIEW:
                root_parameter_index = argument_desc->ShaderResourceView.RootParameterIndex;
                root_parameter = root_signature_get_parameter(root_signature, root_parameter_index);

                if (bind_point_layout->flags & VKD3D_ROOT_SIGNATURE_USE_PUSH_CONSTANT_UNIFORM_BLOCK)
                {
                    ERR("Root signature uses push UBO for root parameters, but this feature requires push constant path.\n");
                    hr = E_NOTIMPL;
                    goto end;
                }

                if (!(root_signature->root_descriptor_raw_va_mask & (1ull << root_parameter_index)))
                {
                    ERR("Root parameter %u is not a raw VA. Cannot implement command signature which updates root descriptor.\n",
                            root_parameter_index);
                    hr = E_NOTIMPL;
                    goto end;
                }

                token.tokenType = VK_INDIRECT_COMMANDS_TOKEN_TYPE_PUSH_CONSTANT_NV;
                token.pushconstantPipelineLayout = bind_point_layout->vk_pipeline_layout;
                token.pushconstantShaderStageFlags = bind_point_layout->vk_push_stages;
                token.pushconstantOffset = root_parameter->descriptor.raw_va_root_descriptor_index * sizeof(VkDeviceAddress);
                token.pushconstantSize = sizeof(VkDeviceAddress);
                required_alignment = sizeof(uint32_t);

                stream_stride = align(stream_stride, required_alignment);
                token.offset = stream_stride;
                stream_stride += token.pushconstantSize;
                dst_word_offset = token.offset / sizeof(uint32_t);

                /* Simply patch by copying U32s. Need to handle unaligned U32s since everything is tightly packed. */
                generic_u32_copy_count = sizeof(VkDeviceAddress) / sizeof(uint32_t);
                generic_u32_copy_types = va_types;
                break;

            case D3D12_INDIRECT_ARGUMENT_TYPE_VERTEX_BUFFER_VIEW:
                token.tokenType = VK_INDIRECT_COMMANDS_TOKEN_TYPE_VERTEX_BUFFER_NV;
                token.vertexBindingUnit = argument_desc->VertexBuffer.Slot;
                token.vertexDynamicStride = VK_TRUE;

                /* If device exposes 4 byte alignment of the indirect command buffer, we can
                 * pack VA at sub-scalar alignment. */
                required_alignment = min(
                        device->device_info.device_generated_commands_properties_nv.minIndirectCommandsBufferOffsetAlignment,
                        sizeof(VkDeviceAddress));

                stream_stride = align(stream_stride, required_alignment);
                token.offset = stream_stride;
                stream_stride += sizeof(VkBindVertexBufferIndirectCommandNV);
                dst_word_offset = token.offset / sizeof(uint32_t);

                /* The VBV indirect layout is the same as DX, so just copy the U32s. */
                generic_u32_copy_count = sizeof(D3D12_VERTEX_BUFFER_VIEW) / sizeof(uint32_t);
                generic_u32_copy_types = vbv_types;
                break;

            case D3D12_INDIRECT_ARGUMENT_TYPE_INDEX_BUFFER_VIEW:
                token.tokenType = VK_INDIRECT_COMMANDS_TOKEN_TYPE_INDEX_BUFFER_NV;
                token.indexTypeCount = ARRAY_SIZE(vk_index_types);
                token.pIndexTypeValues = d3d_index_types;
                token.pIndexTypes = vk_index_types;

                /* If device exposes 4 byte alignment of the indirect command buffer, we can
                 * pack VA at sub-scalar alignment. */
                required_alignment = min(
                        device->device_info.device_generated_commands_properties_nv.minIndirectCommandsBufferOffsetAlignment,
                        sizeof(VkDeviceAddress));

                stream_stride = align(stream_stride, required_alignment);
                token.offset = stream_stride;
                stream_stride += sizeof(VkBindVertexBufferIndirectCommandNV);
                dst_word_offset = token.offset / sizeof(uint32_t);

                vkd3d_array_reserve((void**)&patch_commands, &patch_commands_size,
                        patch_commands_count + sizeof(D3D12_INDEX_BUFFER_VIEW) / sizeof(uint32_t),
                        sizeof(*patch_commands));

                for (j = 0; j < 4; j++)
                {
                    patch_commands[patch_commands_count].token = ibv_types[j];
                    patch_commands[patch_commands_count].src_offset = src_word_offset++;
                    patch_commands[patch_commands_count].dst_offset = dst_word_offset++;
                    patch_commands_count++;
                }
                break;

            case D3D12_INDIRECT_ARGUMENT_TYPE_DRAW:
                token.tokenType = VK_INDIRECT_COMMANDS_TOKEN_TYPE_DRAW_NV;
                required_alignment = sizeof(uint32_t);
                stream_stride = align(stream_stride, required_alignment);
                token.offset = stream_stride;
                stream_stride += sizeof(VkDrawIndirectCommand);
                dst_word_offset = token.offset / sizeof(uint32_t);
                generic_u32_copy_count = sizeof(VkDrawIndirectCommand) / sizeof(uint32_t);
                generic_u32_copy_types = draw_types;
                break;

            case D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED:
                token.tokenType = VK_INDIRECT_COMMANDS_TOKEN_TYPE_DRAW_INDEXED_NV;
                required_alignment = sizeof(uint32_t);
                stream_stride = align(stream_stride, required_alignment);
                token.offset = stream_stride;
                stream_stride += sizeof(VkDrawIndexedIndirectCommand);
                dst_word_offset = token.offset / sizeof(uint32_t);
                generic_u32_copy_count = sizeof(VkDrawIndexedIndirectCommand) / sizeof(uint32_t);
                generic_u32_copy_types = draw_indexed_types;
                break;

            case D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH_MESH:
                token.tokenType = VK_INDIRECT_COMMANDS_TOKEN_TYPE_DRAW_MESH_TASKS_NV;
                required_alignment = sizeof(uint32_t);
                stream_stride = align(stream_stride, required_alignment);
                token.offset = stream_stride;
                stream_stride += sizeof(VkDrawMeshTasksIndirectCommandEXT);
                dst_word_offset = token.offset / sizeof(uint32_t);
                generic_u32_copy_count = sizeof(VkDrawMeshTasksIndirectCommandEXT) / sizeof(uint32_t);
                generic_u32_copy_types = draw_mesh_types;
                break;

            case D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH:
                token.tokenType = VK_INDIRECT_COMMANDS_TOKEN_TYPE_DISPATCH_NV;
                required_alignment = sizeof(uint32_t);
                stream_stride = align(stream_stride, required_alignment);
                token.offset = stream_stride;
                stream_stride += sizeof(VkDispatchIndirectCommand);
                dst_word_offset = token.offset / sizeof(uint32_t);
                /* TODO: Rebase on top of debug-ring-indirect. */
                generic_u32_copy_count = 0;
                generic_u32_copy_types = NULL;
                break;

            default:
                FIXME("Unsupported token type %u.\n", argument_desc->Type);
                hr = E_NOTIMPL;
                goto end;
        }

        vkd3d_array_reserve((void**)&tokens, &token_size, token_count + 1, sizeof(*tokens));
        tokens[token_count++] = token;

        if (generic_u32_copy_count)
        {
            vkd3d_array_reserve((void**)&patch_commands, &patch_commands_size,
                    patch_commands_count + generic_u32_copy_count,
                    sizeof(*patch_commands));

            /* Simply patch by copying U32s. */
            for (j = 0; j < generic_u32_copy_count; j++, patch_commands_count++)
            {
                patch_commands[patch_commands_count].token =
                        generic_u32_copy_types ? generic_u32_copy_types[j] : VKD3D_PATCH_COMMAND_TOKEN_COPY_CONST_U32;
                patch_commands[patch_commands_count].src_offset = src_word_offset++;
                patch_commands[patch_commands_count].dst_offset = dst_word_offset++;
            }
        }

        /* Required alignment is scalar alignment rules, i.e. maximum individual alignment requirement. */
        required_stride_alignment = max(required_stride_alignment, required_alignment);
    }

    stream_stride = max(stream_stride, desc->ByteStride);
    stream_stride = align(stream_stride, required_stride_alignment);

    if (FAILED(hr = d3d12_command_signature_init_patch_commands_buffer(signature, device, patch_commands, patch_commands_count)))
        goto end;
    if (FAILED(hr = d3d12_command_signature_init_indirect_commands_layout_nv(signature, device, tokens, token_count, stream_stride)))
        goto end;
    if (FAILED(hr = vkd3d_meta_get_execute_indirect_pipeline(&device->meta_ops, patch_commands_count,
            &signature->state_template.dgc.pipeline)))
        goto end;

end:
    vkd3d_free(tokens);
    vkd3d_free(patch_commands);
    return hr;
}

static HRESULT d3d12_command_signature_init_state_template_dgc_ext(struct d3d12_command_signature *signature,
        const D3D12_COMMAND_SIGNATURE_DESC *desc,
        struct d3d12_root_signature *root_signature,
        struct d3d12_device *device)
{
    VkIndirectCommandsVertexBufferTokenEXT vb_tokens[D3D12_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT];
    VkIndirectCommandsPushConstantTokenEXT pc_tokens[D3D12_MAX_ROOT_COST];
    const enum vkd3d_patch_command_token *generic_u32_copy_types;
    const struct vkd3d_shader_root_parameter *root_parameter;
    const struct d3d12_bind_point_layout *bind_point_layout;
    const struct vkd3d_shader_root_constant *root_constant;
    struct vkd3d_patch_command *patch_commands = NULL;
    VkIndirectCommandsLayoutTokenEXT *tokens = NULL;
    VkIndirectCommandsIndexBufferTokenEXT ib_token;
    uint32_t required_stride_alignment = 0;
    VkIndirectCommandsLayoutTokenEXT token;
    uint32_t generic_u32_copy_count;
    size_t patch_commands_count = 0;
    uint32_t required_alignment = 0;
    size_t patch_commands_size = 0;
    uint32_t root_parameter_index;
    uint32_t src_word_offset = 0;
    uint32_t stream_stride = 0;
    size_t vb_token_count = 0;
    size_t pc_token_count = 0;
    uint32_t dst_word_offset;
    size_t token_count = 0;
    size_t token_size = 0;
    HRESULT hr = S_OK;
    uint32_t i, j;

    /* Mostly for debug. Lets debug ring report what it is writing easily. */
    static const enum vkd3d_patch_command_token ibv_types[] =
    {
        VKD3D_PATCH_COMMAND_TOKEN_COPY_IBO_VA_LO,
        VKD3D_PATCH_COMMAND_TOKEN_COPY_IBO_VA_HI,
        VKD3D_PATCH_COMMAND_TOKEN_COPY_IBO_SIZE,
        VKD3D_PATCH_COMMAND_TOKEN_COPY_INDEX_FORMAT,
    };

    static const enum vkd3d_patch_command_token vbv_types[] =
    {
        VKD3D_PATCH_COMMAND_TOKEN_COPY_VBO_VA_LO,
        VKD3D_PATCH_COMMAND_TOKEN_COPY_VBO_VA_HI,
        VKD3D_PATCH_COMMAND_TOKEN_COPY_VBO_SIZE,
        VKD3D_PATCH_COMMAND_TOKEN_COPY_VBO_STRIDE,
    };

    static const enum vkd3d_patch_command_token draw_types[] =
    {
        VKD3D_PATCH_COMMAND_TOKEN_COPY_VERTEX_COUNT,
        VKD3D_PATCH_COMMAND_TOKEN_COPY_INSTANCE_COUNT,
        VKD3D_PATCH_COMMAND_TOKEN_COPY_FIRST_VERTEX,
        VKD3D_PATCH_COMMAND_TOKEN_COPY_FIRST_INSTANCE,
    };

    static const enum vkd3d_patch_command_token draw_indexed_types[] =
    {
        VKD3D_PATCH_COMMAND_TOKEN_COPY_INDEX_COUNT,
        VKD3D_PATCH_COMMAND_TOKEN_COPY_INSTANCE_COUNT,
        VKD3D_PATCH_COMMAND_TOKEN_COPY_FIRST_INDEX,
        VKD3D_PATCH_COMMAND_TOKEN_COPY_VERTEX_OFFSET,
        VKD3D_PATCH_COMMAND_TOKEN_COPY_FIRST_INSTANCE,
    };

    static const enum vkd3d_patch_command_token draw_mesh_types[] =
    {
        VKD3D_PATCH_COMMAND_TOKEN_COPY_MESH_TASKS_X,
        VKD3D_PATCH_COMMAND_TOKEN_COPY_MESH_TASKS_Y,
        VKD3D_PATCH_COMMAND_TOKEN_COPY_MESH_TASKS_Z,
    };

    static const enum vkd3d_patch_command_token va_types[] =
    {
        VKD3D_PATCH_COMMAND_TOKEN_COPY_ROOT_VA_LO,
        VKD3D_PATCH_COMMAND_TOKEN_COPY_ROOT_VA_HI,
    };

    bind_point_layout = d3d12_root_signature_get_layout(root_signature, signature->pipeline_type);

    for (i = 0; i < desc->NumArgumentDescs; i++)
    {
        const D3D12_INDIRECT_ARGUMENT_DESC *argument_desc = &desc->pArgumentDescs[i];
        VkIndirectCommandsPushConstantTokenEXT *pc_token;
        VkIndirectCommandsVertexBufferTokenEXT *vb_token;

        memset(&token, 0, sizeof(token));
        token.sType = VK_STRUCTURE_TYPE_INDIRECT_COMMANDS_LAYOUT_TOKEN_EXT;
        generic_u32_copy_count = 0;
        dst_word_offset = 0;

        switch (argument_desc->Type)
        {
            case D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT:
            case D3D12_INDIRECT_ARGUMENT_TYPE_INCREMENTING_CONSTANT:
                root_parameter_index = argument_desc->Constant.RootParameterIndex;
                root_constant = root_signature_get_32bit_constants(root_signature, root_parameter_index);

                if (bind_point_layout->flags & VKD3D_ROOT_SIGNATURE_USE_PUSH_CONSTANT_UNIFORM_BLOCK)
                {
                    ERR("Root signature uses push UBO for root parameters, but this feature requires push constant path.\n");
                    hr = E_NOTIMPL;
                    goto end;
                }

                assert(pc_token_count < ARRAY_SIZE(pc_tokens));
                pc_token = &pc_tokens[pc_token_count++];
                token.data.pPushConstant = pc_token;
                pc_token->updateRange.stageFlags = bind_point_layout->vk_push_stages;
                pc_token->updateRange.offset = root_constant->constant_index + argument_desc->Constant.DestOffsetIn32BitValues;
                pc_token->updateRange.size = argument_desc->Constant.Num32BitValuesToSet;
                pc_token->updateRange.offset *= sizeof(uint32_t);
                pc_token->updateRange.size *= sizeof(uint32_t);

                if (argument_desc->Type == D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT)
                {
                    token.type = VK_INDIRECT_COMMANDS_TOKEN_TYPE_PUSH_CONSTANT_EXT;

                    required_alignment = sizeof(uint32_t);
                    stream_stride = align(stream_stride, required_alignment);
                    token.offset = stream_stride;
                    stream_stride += pc_token->updateRange.size;
                    dst_word_offset = token.offset / sizeof(uint32_t);

                    generic_u32_copy_count = argument_desc->Constant.Num32BitValuesToSet;
                }
                else
                {
                    token.type = VK_INDIRECT_COMMANDS_TOKEN_TYPE_SEQUENCE_INDEX_EXT;
                    token.offset = 0; /* ignored */
                    pc_token->updateRange.size = sizeof(uint32_t);

                    generic_u32_copy_count = 0;
                }

                generic_u32_copy_types = NULL;
                break;

            case D3D12_INDIRECT_ARGUMENT_TYPE_UNORDERED_ACCESS_VIEW:
            case D3D12_INDIRECT_ARGUMENT_TYPE_SHADER_RESOURCE_VIEW:
            case D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT_BUFFER_VIEW:
                root_parameter_index = argument_desc->ShaderResourceView.RootParameterIndex;
                root_parameter = root_signature_get_parameter(root_signature, root_parameter_index);

                if (bind_point_layout->flags & VKD3D_ROOT_SIGNATURE_USE_PUSH_CONSTANT_UNIFORM_BLOCK)
                {
                    ERR("Root signature uses push UBO for root parameters, but this feature requires push constant path.\n");
                    hr = E_NOTIMPL;
                    goto end;
                }

                if (!(root_signature->root_descriptor_raw_va_mask & (1ull << root_parameter_index)))
                {
                    ERR("Root parameter %u is not a raw VA. Cannot implement command signature which updates root descriptor.\n",
                            root_parameter_index);
                    hr = E_NOTIMPL;
                    goto end;
                }

                token.type = VK_INDIRECT_COMMANDS_TOKEN_TYPE_PUSH_CONSTANT_EXT;
                assert(pc_token_count < ARRAY_SIZE(pc_tokens));
                pc_token = &pc_tokens[pc_token_count++];
                token.data.pPushConstant = pc_token;
                pc_token->updateRange.stageFlags = bind_point_layout->vk_push_stages;
                pc_token->updateRange.offset = root_parameter->descriptor.raw_va_root_descriptor_index * sizeof(VkDeviceAddress);
                pc_token->updateRange.size = sizeof(VkDeviceAddress);
                required_alignment = sizeof(uint32_t);

                stream_stride = align(stream_stride, required_alignment);
                token.offset = stream_stride;
                stream_stride += pc_token->updateRange.size;
                dst_word_offset = token.offset / sizeof(uint32_t);

                /* Simply patch by copying U32s. Need to handle unaligned U32s since everything is tightly packed. */
                generic_u32_copy_count = sizeof(VkDeviceAddress) / sizeof(uint32_t);
                generic_u32_copy_types = va_types;
                break;

            case D3D12_INDIRECT_ARGUMENT_TYPE_VERTEX_BUFFER_VIEW:
                token.type = VK_INDIRECT_COMMANDS_TOKEN_TYPE_VERTEX_BUFFER_EXT;
                assert(vb_token_count < ARRAY_SIZE(vb_tokens));
                vb_token = &vb_tokens[vb_token_count++];
                token.data.pVertexBuffer = vb_token;
                vb_token->vertexBindingUnit = argument_desc->VertexBuffer.Slot;

                /* alignment is always 4 with EXT DGC */
                required_alignment = 4;

                stream_stride = align(stream_stride, required_alignment);
                token.offset = stream_stride;
                stream_stride += sizeof(VkBindVertexBufferIndirectCommandNV);
                dst_word_offset = token.offset / sizeof(uint32_t);

                /* The VBV indirect layout is the same as DX, so just copy the U32s. */
                generic_u32_copy_count = sizeof(D3D12_VERTEX_BUFFER_VIEW) / sizeof(uint32_t);
                generic_u32_copy_types = vbv_types;
                break;

            case D3D12_INDIRECT_ARGUMENT_TYPE_INDEX_BUFFER_VIEW:
                token.type = VK_INDIRECT_COMMANDS_TOKEN_TYPE_INDEX_BUFFER_EXT;
                ib_token.mode = VK_INDIRECT_COMMANDS_INPUT_MODE_DXGI_INDEX_BUFFER_EXT;
                token.data.pIndexBuffer = &ib_token;

                /* alignment is always 4 with EXT DGC */
                required_alignment = 4;

                stream_stride = align(stream_stride, required_alignment);
                token.offset = stream_stride;
                stream_stride += sizeof(VkBindVertexBufferIndirectCommandNV);
                dst_word_offset = token.offset / sizeof(uint32_t);

                vkd3d_array_reserve((void**)&patch_commands, &patch_commands_size,
                        patch_commands_count + sizeof(D3D12_INDEX_BUFFER_VIEW) / sizeof(uint32_t),
                        sizeof(*patch_commands));

                for (j = 0; j < 4; j++)
                {
                    patch_commands[patch_commands_count].token = ibv_types[j];
                    patch_commands[patch_commands_count].src_offset = src_word_offset++;
                    patch_commands[patch_commands_count].dst_offset = dst_word_offset++;
                    patch_commands_count++;
                }
                break;

            case D3D12_INDIRECT_ARGUMENT_TYPE_DRAW:
                token.type = VK_INDIRECT_COMMANDS_TOKEN_TYPE_DRAW_EXT;
                required_alignment = sizeof(uint32_t);
                stream_stride = align(stream_stride, required_alignment);
                token.offset = stream_stride;
                stream_stride += sizeof(VkDrawIndirectCommand);
                dst_word_offset = token.offset / sizeof(uint32_t);
                generic_u32_copy_count = sizeof(VkDrawIndirectCommand) / sizeof(uint32_t);
                generic_u32_copy_types = draw_types;
                break;

            case D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED:
                token.type = VK_INDIRECT_COMMANDS_TOKEN_TYPE_DRAW_INDEXED_EXT;
                required_alignment = sizeof(uint32_t);
                stream_stride = align(stream_stride, required_alignment);
                token.offset = stream_stride;
                stream_stride += sizeof(VkDrawIndexedIndirectCommand);
                dst_word_offset = token.offset / sizeof(uint32_t);
                generic_u32_copy_count = sizeof(VkDrawIndexedIndirectCommand) / sizeof(uint32_t);
                generic_u32_copy_types = draw_indexed_types;
                break;

            case D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH_MESH:
                token.type = VK_INDIRECT_COMMANDS_TOKEN_TYPE_DRAW_MESH_TASKS_EXT;
                required_alignment = sizeof(uint32_t);
                stream_stride = align(stream_stride, required_alignment);
                token.offset = stream_stride;
                stream_stride += sizeof(VkDrawMeshTasksIndirectCommandEXT);
                dst_word_offset = token.offset / sizeof(uint32_t);
                generic_u32_copy_count = sizeof(VkDrawMeshTasksIndirectCommandEXT) / sizeof(uint32_t);
                generic_u32_copy_types = draw_mesh_types;
                break;

            case D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH:
                token.type = VK_INDIRECT_COMMANDS_TOKEN_TYPE_DISPATCH_EXT;
                required_alignment = sizeof(uint32_t);
                stream_stride = align(stream_stride, required_alignment);
                token.offset = stream_stride;
                stream_stride += sizeof(VkDispatchIndirectCommand);
                dst_word_offset = token.offset / sizeof(uint32_t);
                /* TODO: Rebase on top of debug-ring-indirect. */
                generic_u32_copy_count = 0;
                generic_u32_copy_types = NULL;
                break;

            default:
                FIXME("Unsupported token type %u.\n", argument_desc->Type);
                hr = E_NOTIMPL;
                goto end;
        }

        vkd3d_array_reserve((void**)&tokens, &token_size, token_count + 1, sizeof(*tokens));
        tokens[token_count++] = token;

        if (generic_u32_copy_count)
        {
            vkd3d_array_reserve((void**)&patch_commands, &patch_commands_size,
                    patch_commands_count + generic_u32_copy_count,
                    sizeof(*patch_commands));

            /* Simply patch by copying U32s. */
            for (j = 0; j < generic_u32_copy_count; j++, patch_commands_count++)
            {
                patch_commands[patch_commands_count].token =
                        generic_u32_copy_types ? generic_u32_copy_types[j] : VKD3D_PATCH_COMMAND_TOKEN_COPY_CONST_U32;
                patch_commands[patch_commands_count].src_offset = src_word_offset++;
                patch_commands[patch_commands_count].dst_offset = dst_word_offset++;
            }
        }

        /* Required alignment is scalar alignment rules, i.e. maximum individual alignment requirement. */
        required_stride_alignment = max(required_stride_alignment, required_alignment);
    }

    stream_stride = max(stream_stride, desc->ByteStride);
    stream_stride = align(stream_stride, required_stride_alignment);

    if (patch_commands_count)
        if (FAILED(hr = d3d12_command_signature_init_patch_commands_buffer(signature, device, patch_commands, patch_commands_count)))
            goto end;

    if (FAILED(hr = d3d12_command_signature_init_indirect_commands_layout_ext(signature, root_signature, device, tokens, token_count, stream_stride)))
        goto end;

    if (patch_commands_count)
    {
        if (FAILED(hr = vkd3d_meta_get_execute_indirect_pipeline(&device->meta_ops, patch_commands_count,
                &signature->state_template.dgc.pipeline)))
            goto end;
    }

end:
    vkd3d_free(tokens);
    vkd3d_free(patch_commands);
    return hr;
}

HRESULT d3d12_command_signature_create(struct d3d12_device *device, struct d3d12_root_signature *root_signature,
        const D3D12_COMMAND_SIGNATURE_DESC *desc,
        struct d3d12_command_signature **signature)
{
    struct d3d12_command_signature *object;
    enum vkd3d_pipeline_type pipeline_type;
    bool requires_root_signature = false;
    bool requires_state_template = false;
    uint32_t argument_buffer_offset = 0;
    uint32_t signature_size = 0;
    bool has_action = false;
    unsigned int i;
    bool is_action;
    HRESULT hr;

    pipeline_type = VKD3D_PIPELINE_TYPE_NONE;

    for (i = 0; i < desc->NumArgumentDescs; ++i)
    {
        const D3D12_INDIRECT_ARGUMENT_DESC *argument_desc = &desc->pArgumentDescs[i];
        is_action = false;

        switch (argument_desc->Type)
        {
            case D3D12_INDIRECT_ARGUMENT_TYPE_DRAW:
                pipeline_type = VKD3D_PIPELINE_TYPE_GRAPHICS;
                argument_buffer_offset = signature_size;
                signature_size += sizeof(D3D12_DRAW_ARGUMENTS);
                is_action = true;
                break;

            case D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED:
                pipeline_type = VKD3D_PIPELINE_TYPE_GRAPHICS;
                argument_buffer_offset = signature_size;
                signature_size += sizeof(D3D12_DRAW_INDEXED_ARGUMENTS);
                is_action = true;
                break;

            case D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH:
                pipeline_type = VKD3D_PIPELINE_TYPE_COMPUTE;
                argument_buffer_offset = signature_size;
                signature_size += sizeof(D3D12_DISPATCH_ARGUMENTS);
                is_action = true;
                break;

            case D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH_RAYS:
                pipeline_type = VKD3D_PIPELINE_TYPE_RAY_TRACING;
                argument_buffer_offset = signature_size;
                signature_size += sizeof(D3D12_DISPATCH_RAYS_DESC);
                is_action = true;
                break;

            case D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH_MESH:
                pipeline_type = VKD3D_PIPELINE_TYPE_MESH_GRAPHICS;
                argument_buffer_offset = signature_size;
                signature_size += sizeof(D3D12_DISPATCH_MESH_ARGUMENTS);
                is_action = true;
                break;

            case D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT:
                requires_root_signature = true;
                requires_state_template = true;
                signature_size += argument_desc->Constant.Num32BitValuesToSet * sizeof(uint32_t);
                break;

            case D3D12_INDIRECT_ARGUMENT_TYPE_INCREMENTING_CONSTANT:
                requires_root_signature = true;
                requires_state_template = true;
                break;

            case D3D12_INDIRECT_ARGUMENT_TYPE_SHADER_RESOURCE_VIEW:
            case D3D12_INDIRECT_ARGUMENT_TYPE_UNORDERED_ACCESS_VIEW:
            case D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT_BUFFER_VIEW:
                requires_root_signature = true;
                requires_state_template = true;
                /* The command signature payload is *not* aligned. */
                signature_size += sizeof(D3D12_GPU_VIRTUAL_ADDRESS);
                break;

            case D3D12_INDIRECT_ARGUMENT_TYPE_VERTEX_BUFFER_VIEW:
                /* The command signature payload is *not* aligned. */
                signature_size += sizeof(D3D12_VERTEX_BUFFER_VIEW);
                requires_state_template = true;
                break;

            case D3D12_INDIRECT_ARGUMENT_TYPE_INDEX_BUFFER_VIEW:
                /* The command signature payload is *not* aligned. */
                signature_size += sizeof(D3D12_INDEX_BUFFER_VIEW);
                requires_state_template = true;
                break;

            default:
                FIXME("Unsupported indirect argument type: %u.\n", argument_desc->Type);
                break;
        }

        if (is_action)
        {
            if (has_action)
            {
                ERR("Using multiple action commands per command signature is invalid.\n");
                return E_INVALIDARG;
            }

            if (i != desc->NumArgumentDescs - 1)
            {
                WARN("Action command must be the last element of a command signature.\n");
                return E_INVALIDARG;
            }

            has_action = true;
        }
    }

    if (!has_action)
    {
        ERR("Command signature must have exactly one action command.\n");
        return E_INVALIDARG;
    }

    if (desc->ByteStride < signature_size)
    {
        ERR("Command signature stride %u must be at least %u bytes.\n",
                desc->ByteStride, signature_size);
        return E_INVALIDARG;
    }

    if (requires_root_signature && !root_signature)
    {
        ERR("Command signature requires root signature, but is not provided.\n");
        return E_INVALIDARG;
    }
    else if (!requires_root_signature && root_signature)
    {
        ERR("Command signature does not require root signature, root signature must be NULL.\n");
        return E_INVALIDARG;
    }

    if (!(object = vkd3d_calloc(1, sizeof(*object))))
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
        goto err;

    object->pipeline_type = pipeline_type;

    if ((object->requires_state_template = requires_state_template))
    {
        if ((pipeline_type == VKD3D_PIPELINE_TYPE_GRAPHICS || pipeline_type == VKD3D_PIPELINE_TYPE_MESH_GRAPHICS) &&
                !device->device_info.device_generated_commands_features_nv.deviceGeneratedCommands &&
                !device->device_info.device_generated_commands_features_ext.deviceGeneratedCommands)
        {
            FIXME("Device generated commands is not supported by implementation.\n");
            hr = E_NOTIMPL;
            goto err;
        }
        else if (pipeline_type == VKD3D_PIPELINE_TYPE_COMPUTE)
        {
            if (!device->device_info.device_generated_commands_compute_features_nv.deviceGeneratedCompute &&
                    !device->device_info.device_generated_commands_features_ext.deviceGeneratedCommands &&
                    !(device->bindless_state.flags & VKD3D_FORCE_COMPUTE_ROOT_PARAMETERS_PUSH_UBO))
            {
                FIXME("State template is required for compute, but VKD3D_CONFIG_FLAG_REQUIRES_COMPUTE_INDIRECT_TEMPLATES is not enabled.\n");
                hr = E_NOTIMPL;
                goto err;
            }
        }
        else if (pipeline_type == VKD3D_PIPELINE_TYPE_RAY_TRACING)
        {
            /* Very similar idea as indirect compute would be. */
            FIXME("State template is required for indirect ray tracing, but it is unimplemented.\n");
            hr = E_NOTIMPL;
            goto err;
        }

        if (device->device_info.device_generated_commands_features_ext.deviceGeneratedCommands)
        {
            if (FAILED(hr = d3d12_command_signature_init_state_template_dgc_ext(object, desc, root_signature, device)))
                goto err;
            object->requires_state_template_dgc = true;
        }
        else if (pipeline_type == VKD3D_PIPELINE_TYPE_GRAPHICS || pipeline_type == VKD3D_PIPELINE_TYPE_MESH_GRAPHICS ||
                (pipeline_type == VKD3D_PIPELINE_TYPE_COMPUTE &&
                        device->device_info.device_generated_commands_compute_features_nv.deviceGeneratedCompute))
        {
            if (FAILED(hr = d3d12_command_signature_init_state_template_dgc_nv(object, desc, root_signature, device)))
                goto err;
            object->requires_state_template_dgc = true;
        }
        else if (pipeline_type == VKD3D_PIPELINE_TYPE_COMPUTE)
        {
            if (FAILED(hr = d3d12_command_signature_init_state_template_compute(object, desc, root_signature, device)))
                goto err;
        }

        /* Heuristic. If game uses fancy execute indirect we're more inclined to split command buffers
         * for optimal reordering. */
        vkd3d_atomic_uint32_store_explicit(&device->device_has_dgc_templates, 1, vkd3d_memory_order_relaxed);
    }

    object->argument_buffer_offset_for_command = argument_buffer_offset;
    d3d_destruction_notifier_init(&object->destruction_notifier, (IUnknown*)&object->ID3D12CommandSignature_iface);
    d3d12_device_add_ref(object->device = device);

    TRACE("Created command signature %p.\n", object);

    *signature = object;

    return S_OK;

err:
    vkd3d_free((void *)object->desc.pArgumentDescs);
    vkd3d_free(object);
    return hr;
}

bool vk_image_memory_barrier_for_initial_transition(const struct d3d12_resource *resource,
        VkImageMemoryBarrier2 *barrier)
{
    assert(d3d12_resource_is_texture(resource));

#ifdef VKD3D_ENABLE_BREADCRUMBS
    if (resource->initial_layout_transition_validate_only)
    {
        if (d3d12_resource_get_sub_resource_count(resource) == 1)
        {
            ERR("Application uses placed resource (1 subresource) (cookie %"PRIu64", fmt: %s, flags: #%x)"
                    " that must be initialized explicitly.\n",
                    resource->res.cookie, debug_dxgi_format(resource->desc.Format), resource->desc.Flags);
        }
        else
        {
            WARN("Application uses placed resource (>1 subresources) (cookie %"PRIu64", fmt: %s, flags: #%x)"
                    " that must be initialized explicitly. "
                    "This warning may be a false positive due to lack of sub-resource level tracking.\n",
                    resource->res.cookie, debug_dxgi_format(resource->desc.Format), resource->desc.Flags);
        }
        return false;
    }
#endif

    memset(barrier, 0, sizeof(*barrier));
    barrier->sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    barrier->oldLayout = (resource->flags & VKD3D_RESOURCE_ZERO_INITIALIZED) ?
            VK_IMAGE_LAYOUT_ZERO_INITIALIZED_EXT : VK_IMAGE_LAYOUT_UNDEFINED;
    barrier->newLayout = vk_image_layout_from_d3d12_resource_state(NULL, resource, resource->initial_state);
    barrier->srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier->dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier->image = resource->res.vk_image;
    barrier->subresourceRange.aspectMask = resource->format->vk_aspect_mask;
    barrier->subresourceRange.levelCount = VK_REMAINING_MIP_LEVELS;
    barrier->subresourceRange.layerCount = VK_REMAINING_ARRAY_LAYERS;

    /* srcAccess and dstAccess mask is 0, which is fine if we use timeline semaphores
     * to synchronize. Otherwise, the caller will need to set stage and access masks. */

    TRACE("Initial layout transition for resource %p (old layout %#x, new layout %#x).\n",
          resource, barrier->oldLayout, barrier->newLayout);

    return true;
}
