/*
 * Copyright 2021 Philip Rebohle for Valve Corporation
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
#include "vkd3d_descriptor_debug.h"

static bool vkd3d_memory_transfer_queue_wait_semaphore(struct vkd3d_memory_transfer_queue *queue,
        uint64_t wait_value, uint64_t timeout);

static void vkd3d_acquire_tracked_resource(struct d3d12_resource *resource)
{
    d3d12_resource_incref(resource);
}

static void vkd3d_release_tracked_resource(struct d3d12_resource *resource)
{
    d3d12_resource_decref(resource);
}

static void *vkd3d_memory_transfer_queue_run_thread(void *userdata)
{
    struct vkd3d_memory_transfer_tracked_resource *tracked_resources;
    size_t i, tracked_resource_size, tracked_resource_count;
    struct vkd3d_memory_transfer_queue *queue = userdata;
    bool running = true;

    tracked_resources = NULL;
    tracked_resource_size = 0;
    tracked_resource_count = 0;

    while (running)
    {
        struct vkd3d_memory_transfer_tracked_resource *old_tracked_resources;
        size_t old_tracked_resource_size;

        pthread_mutex_lock(&queue->mutex);

        while (!queue->tracked_resource_count)
        {
            pthread_cond_wait(&queue->cond, &queue->mutex);
        }

        old_tracked_resources = tracked_resources;
        old_tracked_resource_size = tracked_resource_size;

        tracked_resources = queue->tracked_resources;
        tracked_resource_size = queue->tracked_resource_size;
        tracked_resource_count = queue->tracked_resource_count;

        queue->tracked_resources = old_tracked_resources;
        queue->tracked_resource_size = old_tracked_resource_size;
        queue->tracked_resource_count = 0;

        pthread_mutex_unlock(&queue->mutex);

        for (i = 0; i < tracked_resource_count; i++)
        {
            const struct vkd3d_memory_transfer_tracked_resource *entry = &tracked_resources[i];

            if (entry->resource)
            {
                vkd3d_memory_transfer_queue_wait_semaphore(queue, entry->semaphore_value, UINT64_MAX);
                vkd3d_release_tracked_resource(entry->resource);
            }
            else
                running = false;
        }
    }

    vkd3d_free(tracked_resources);
    return NULL;
}

static void vkd3d_memory_transfer_queue_track_resource_locked(struct vkd3d_memory_transfer_queue *queue,
        struct d3d12_resource *resource, UINT64 semaphore_value)
{
    struct vkd3d_memory_transfer_tracked_resource *entry;

    if (!vkd3d_array_reserve((void**)&queue->tracked_resources, &queue->tracked_resource_size,
            queue->tracked_resource_count + 1, sizeof(*queue->tracked_resources)))
    {
        ERR("Failed to track resource.\n");
        return;
    }

    if (resource)
        vkd3d_acquire_tracked_resource(resource);

    entry = &queue->tracked_resources[queue->tracked_resource_count++];
    entry->resource = resource;
    entry->semaphore_value = semaphore_value;

    pthread_cond_signal(&queue->cond);
}

void vkd3d_memory_transfer_queue_cleanup(struct vkd3d_memory_transfer_queue *queue)
{
    const struct vkd3d_vk_device_procs *vk_procs = &queue->device->vk_procs;

    pthread_mutex_lock(&queue->mutex);
    vkd3d_memory_transfer_queue_track_resource_locked(queue, NULL, 0);
    pthread_mutex_unlock(&queue->mutex);

    pthread_join(queue->thread, NULL);

    VK_CALL(vkDestroyCommandPool(queue->device->vk_device, queue->vk_command_pool, NULL));
    VK_CALL(vkDestroySemaphore(queue->device->vk_device, queue->vk_semaphore, NULL));

    vkd3d_free(queue->tracked_resources);
    vkd3d_free(queue->transfers);

    pthread_cond_destroy(&queue->cond);
    pthread_mutex_destroy(&queue->mutex);
}

HRESULT vkd3d_memory_transfer_queue_init(struct vkd3d_memory_transfer_queue *queue, struct d3d12_device *device)
{
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    VkSemaphoreTypeCreateInfoKHR semaphore_type_info;
    VkCommandBufferAllocateInfo command_buffer_info;
    VkCommandPoolCreateInfo command_pool_info;
    VkSemaphoreCreateInfo semaphore_info;
    VkResult vr;
    HRESULT hr;
    int rc;

    memset(queue, 0, sizeof(*queue));

    queue->device = device;
    queue->vkd3d_queue = d3d12_device_allocate_vkd3d_queue(device,
            device->queue_families[VKD3D_QUEUE_FAMILY_INTERNAL_COMPUTE]);

    queue->last_known_value = VKD3D_MEMORY_TRANSFER_COMMAND_BUFFER_COUNT;
    queue->next_signal_value = VKD3D_MEMORY_TRANSFER_COMMAND_BUFFER_COUNT + 1;

    if ((rc = pthread_mutex_init(&queue->mutex, NULL)))
        return hresult_from_errno(rc);

    if ((rc = pthread_cond_init(&queue->cond, NULL)))
    {
        pthread_mutex_destroy(&queue->mutex);
        return hresult_from_errno(rc);
    }

    if ((rc = pthread_create(&queue->thread, NULL,
            vkd3d_memory_transfer_queue_run_thread, queue)))
    {
        pthread_mutex_destroy(&queue->mutex);
        pthread_cond_destroy(&queue->cond);
        return hresult_from_errno(rc);
    }

    command_pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    command_pool_info.pNext = NULL;
    command_pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    command_pool_info.queueFamilyIndex = device->queue_families[VKD3D_QUEUE_FAMILY_INTERNAL_COMPUTE]->vk_family_index;

    if ((vr = VK_CALL(vkCreateCommandPool(device->vk_device, &command_pool_info,
            NULL, &queue->vk_command_pool))) < 0)
    {
        ERR("Failed to create command pool, vr %d.\n", vr);
        hr = hresult_from_vk_result(vr);
        goto fail;
    }

    command_buffer_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    command_buffer_info.pNext = NULL;
    command_buffer_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    command_buffer_info.commandPool = queue->vk_command_pool;
    command_buffer_info.commandBufferCount = VKD3D_MEMORY_TRANSFER_COMMAND_BUFFER_COUNT;

    if ((vr = VK_CALL(vkAllocateCommandBuffers(device->vk_device,
            &command_buffer_info, queue->vk_command_buffers))) < 0)
    {
        ERR("Failed to allocate command buffer, vr %d.\n", vr);
        hr = hresult_from_vk_result(vr);
        goto fail;
    }

    semaphore_type_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO_KHR;
    semaphore_type_info.pNext = NULL;
    semaphore_type_info.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE_KHR;
    semaphore_type_info.initialValue = VKD3D_MEMORY_TRANSFER_COMMAND_BUFFER_COUNT;

    semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    semaphore_info.pNext = &semaphore_type_info;
    semaphore_info.flags = 0;

    if ((vr = VK_CALL(vkCreateSemaphore(device->vk_device,
            &semaphore_info, NULL, &queue->vk_semaphore))) < 0)
    {
        ERR("Failed to create semaphore, vr %d.\n", vr);
        hr = hresult_from_vk_result(vr);
        goto fail;
    }

    return S_OK;

fail:
    vkd3d_memory_transfer_queue_cleanup(queue);
    return hr;
}

static bool vkd3d_memory_transfer_queue_wait_semaphore(struct vkd3d_memory_transfer_queue *queue,
        uint64_t wait_value, uint64_t timeout)
{
    const struct vkd3d_vk_device_procs *vk_procs = &queue->device->vk_procs;
    uint64_t old_value, new_value = 0;
    VkSemaphoreWaitInfo wait_info;
    VkResult vr = VK_TIMEOUT;

    old_value = vkd3d_atomic_uint64_load_explicit(&queue->last_known_value, vkd3d_memory_order_acquire);

    if (old_value >= wait_value)
        return true;

    if (timeout)
    {
        wait_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
        wait_info.pNext = NULL;
        wait_info.flags = 0;
        wait_info.semaphoreCount = 1;
        wait_info.pSemaphores = &queue->vk_semaphore;
        wait_info.pValues = &wait_value;

        vr = VK_CALL(vkWaitSemaphores(queue->device->vk_device, &wait_info, timeout));
        if (vr == VK_SUCCESS)
            new_value = wait_value;
    }

    if (vr != VK_SUCCESS)
    {
        vr = VK_CALL(vkGetSemaphoreCounterValue(queue->device->vk_device,
                queue->vk_semaphore, &new_value));
    }

    if (vr < 0)
    {
        ERR("Failed to wait for timeline semaphore, vr %d.\n", vr);
        return false;
    }

    while (new_value > old_value)
    {
        uint64_t cur_value = vkd3d_atomic_uint64_compare_exchange(&queue->last_known_value,
                old_value, new_value, vkd3d_memory_order_release, vkd3d_memory_order_acquire);

        if (cur_value == old_value)
            break;

        old_value = cur_value;
    }

    return new_value >= wait_value;
}

static HRESULT vkd3d_memory_transfer_queue_flush_locked(struct vkd3d_memory_transfer_queue *queue)
{
    const struct vkd3d_vk_device_procs *vk_procs = &queue->device->vk_procs;
    const struct vkd3d_subresource_layout *subresource_layout;
    VkCopyBufferToImageInfo2 buffer_to_image_copy;
    struct vkd3d_queue_family_info *queue_family;
    VkSemaphoreSubmitInfo signal_semaphore_info;
    VkCommandBufferSubmitInfo cmd_buffer_info;
    struct vkd3d_format_footprint footprint;
    VkCommandBufferBeginInfo begin_info;
    VkImageMemoryBarrier2 image_barrier;
    VkImageSubresource vk_subresource;
    uint32_t queue_mask, queue_index;
    VkBufferImageCopy2 copy_region;
    VkCommandBuffer vk_cmd_buffer;
    VkDeviceSize buffer_offset;
    VkDependencyInfo dep_info;
    VkSubmitInfo2 submit_info;
    bool need_transition;
    uint32_t plane_idx;
    VkQueue vk_queue;
    VkResult vr;
    size_t i;

    if (!queue->transfer_count)
        return S_OK;

    /* Record commands late so that we can simply remove allocations from
     * the queue if they got freed before the clear commands got dispatched,
     * rather than rewriting the command buffer or dispatching the clear */
    vk_cmd_buffer = queue->vk_command_buffers[queue->command_buffer_index];

    if (vkd3d_config_flags & VKD3D_CONFIG_FLAG_LOG_MEMORY_BUDGET)
    {
        INFO("Submitting clear command list.\n");
        for (i = 0; i < queue->transfer_count; i++)
        {
            if (queue->transfers[i].op == VKD3D_MEMORY_TRANSFER_OP_CLEAR_ALLOCATION)
                INFO("Clearing allocation %zu: %"PRIu64".\n", i, queue->transfers[i].allocation->resource.size);
        }
    }

    vkd3d_memory_transfer_queue_wait_semaphore(queue,
            queue->next_signal_value - VKD3D_MEMORY_TRANSFER_COMMAND_BUFFER_COUNT, UINT64_MAX);

    if ((vr = VK_CALL(vkResetCommandBuffer(vk_cmd_buffer, 0))))
    {
        ERR("Failed to reset command pool, vr %d.\n", vr);
        return hresult_from_vk_result(vr);
    }

    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.pNext = NULL;
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    begin_info.pInheritanceInfo = NULL;

    if ((vr = VK_CALL(vkBeginCommandBuffer(vk_cmd_buffer, &begin_info))) < 0)
    {
        ERR("Failed to begin command buffer, vr %d.\n", vr);
        return hresult_from_vk_result(vr);
    }

    memset(&dep_info, 0, sizeof(dep_info));
    dep_info.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dep_info.imageMemoryBarrierCount = 1;
    dep_info.pImageMemoryBarriers = &image_barrier;

    for (i = 0; i < queue->transfer_count; i++)
    {
        const struct vkd3d_memory_transfer_info *transfer = &queue->transfers[i];

        switch (transfer->op)
        {
            case VKD3D_MEMORY_TRANSFER_OP_CLEAR_ALLOCATION:
                VK_CALL(vkCmdFillBuffer(vk_cmd_buffer,
                        transfer->allocation->resource.vk_buffer,
                        transfer->allocation->offset,
                        transfer->allocation->resource.size, 0));
                break;

            case VKD3D_MEMORY_TRANSFER_OP_WRITE_SUBRESOURCE:
                /* This will be executed before any other command buffer that could
                 * potentially initialize the resource on the GPU timeline */
                need_transition = vkd3d_atomic_uint32_load_explicit(&transfer->resource->initial_layout_transition, vkd3d_memory_order_relaxed) &&
                        vkd3d_atomic_uint32_exchange_explicit(&transfer->resource->initial_layout_transition, 0, vkd3d_memory_order_relaxed);

                if (need_transition && vk_image_memory_barrier_for_initial_transition(transfer->resource, &image_barrier))
                {
                    image_barrier.dstStageMask |= VK_PIPELINE_STAGE_2_TRANSFER_BIT;
                    image_barrier.dstAccessMask |= VK_ACCESS_2_TRANSFER_WRITE_BIT;

                    VK_CALL(vkCmdPipelineBarrier2(vk_cmd_buffer, &dep_info));
                }

                vk_subresource = d3d12_resource_get_vk_subresource(transfer->resource, transfer->subresource_idx, false);
                plane_idx = transfer->subresource_idx / d3d12_resource_desc_get_sub_resource_count_per_plane(&transfer->resource->desc);
                footprint = vkd3d_format_footprint_for_plane(transfer->resource->format, plane_idx);

                subresource_layout = &transfer->resource->subresource_layouts[transfer->subresource_idx];
                buffer_offset = subresource_layout->offset + vkd3d_format_get_data_offset(transfer->resource->format,
                        subresource_layout->row_pitch, subresource_layout->depth_pitch, transfer->offset.x, transfer->offset.y, transfer->offset.z);

                copy_region.sType = VK_STRUCTURE_TYPE_BUFFER_IMAGE_COPY_2;
                copy_region.pNext = NULL;
                copy_region.bufferOffset = transfer->resource->mem.offset + buffer_offset;
                copy_region.bufferRowLength = d3d12_resource_desc_get_width(&transfer->resource->desc, vk_subresource.mipLevel + footprint.subsample_x_log2);
                copy_region.bufferImageHeight = d3d12_resource_desc_get_height(&transfer->resource->desc, vk_subresource.mipLevel + footprint.subsample_y_log2);
                copy_region.imageSubresource = vk_subresource_layers_from_subresource(&vk_subresource);
                copy_region.imageOffset = transfer->offset;
                copy_region.imageExtent = transfer->extent;

                buffer_to_image_copy.sType = VK_STRUCTURE_TYPE_COPY_BUFFER_TO_IMAGE_INFO_2;
                buffer_to_image_copy.pNext = NULL;
                buffer_to_image_copy.srcBuffer = transfer->resource->mem.resource.vk_buffer;
                buffer_to_image_copy.dstImage = transfer->resource->res.vk_image;
                buffer_to_image_copy.dstImageLayout = VK_IMAGE_LAYOUT_GENERAL;
                buffer_to_image_copy.regionCount = 1;
                buffer_to_image_copy.pRegions = &copy_region;

                VK_CALL(vkCmdCopyBufferToImage2(vk_cmd_buffer, &buffer_to_image_copy));

                vkd3d_memory_transfer_queue_track_resource_locked(queue,
                        transfer->resource, queue->next_signal_value);
                break;
        }
    }

    if ((vr = VK_CALL(vkEndCommandBuffer(vk_cmd_buffer))) < 0)
    {
        ERR("Failed to end command buffer, vr %d.\n", vr);
        return hresult_from_vk_result(vr);
    }

    if (!(vk_queue = vkd3d_queue_acquire(queue->vkd3d_queue)))
        return E_FAIL;

    memset(&cmd_buffer_info, 0, sizeof(cmd_buffer_info));
    cmd_buffer_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
    cmd_buffer_info.commandBuffer = vk_cmd_buffer;

    memset(&signal_semaphore_info, 0, sizeof(signal_semaphore_info));
    signal_semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    signal_semaphore_info.semaphore = queue->vk_semaphore;
    signal_semaphore_info.value = queue->next_signal_value;
    signal_semaphore_info.stageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;

    memset(&submit_info, 0, sizeof(submit_info));
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    submit_info.commandBufferInfoCount = 1;
    submit_info.pCommandBufferInfos = &cmd_buffer_info;
    submit_info.signalSemaphoreInfoCount = 1;
    submit_info.pSignalSemaphoreInfos = &signal_semaphore_info;

    vr = VK_CALL(vkQueueSubmit2(vk_queue, 1, &submit_info, VK_NULL_HANDLE));
    vkd3d_queue_release(queue->vkd3d_queue);

    VKD3D_DEVICE_REPORT_BREADCRUMB_IF(queue->device, vr == VK_ERROR_DEVICE_LOST);

    if (vr < 0)
    {
        ERR("Failed to submit command buffer, vr %d.\n", vr);
        return hresult_from_vk_result(vr);
    }

    /* Stall future submissions on other queues until the clear has finished */
    queue_mask = queue->device->unique_queue_mask;

    while (queue_mask)
    {
        queue_index = vkd3d_bitmask_iter32(&queue_mask);
        queue_family = queue->device->queue_families[queue_index];

        for (i = 0; i < queue_family->queue_count; i++)
        {
            vkd3d_queue_add_wait(queue_family->queues[i],
                    NULL,
                    queue->vk_semaphore,
                    queue->next_signal_value);
        }
    }

    /* Keep next_signal always one ahead of the last signaled value */
    queue->next_signal_value += 1;
    queue->num_bytes_pending = 0;
    queue->transfer_count = 0;
    queue->command_buffer_index += 1;
    queue->command_buffer_index %= VKD3D_MEMORY_TRANSFER_COMMAND_BUFFER_COUNT;
    return S_OK;
}

HRESULT vkd3d_memory_transfer_queue_flush(struct vkd3d_memory_transfer_queue *queue)
{
    HRESULT hr;

    pthread_mutex_lock(&queue->mutex);
    hr = vkd3d_memory_transfer_queue_flush_locked(queue);
    pthread_mutex_unlock(&queue->mutex);
    return hr;
}

#define VKD3D_MEMORY_TRANSFER_QUEUE_MAX_PENDING_BYTES (256ull << 20) /* 256 MiB */

static void vkd3d_memory_transfer_queue_execute_transfer_locked(struct vkd3d_memory_transfer_queue *queue,
        struct vkd3d_memory_transfer_info *transfer)
{
    if (!vkd3d_array_reserve((void**)&queue->transfers, &queue->transfer_size,
            queue->transfer_count + 1, sizeof(*queue->transfers)))
    {
        ERR("Failed to insert free range.\n");
        return;
    }

    memcpy(&queue->transfers[queue->transfer_count++], transfer, sizeof(*transfer));

    if (transfer->allocation)
        queue->num_bytes_pending += transfer->allocation->resource.size;

    if (queue->num_bytes_pending >= VKD3D_MEMORY_TRANSFER_QUEUE_MAX_PENDING_BYTES)
        vkd3d_memory_transfer_queue_flush_locked(queue);
}

static void vkd3d_memory_transfer_queue_clear_allocation(struct vkd3d_memory_transfer_queue *queue,
        struct vkd3d_memory_allocation *allocation)
{
    struct vkd3d_memory_transfer_info transfer;

    if (allocation->cpu_address)
    {
        /* Probably faster than doing this on the GPU
         * and having to worry about synchronization */
        memset(allocation->cpu_address, 0, allocation->resource.size);
    }
    else if (allocation->resource.vk_buffer)
    {
        pthread_mutex_lock(&queue->mutex);
        allocation->clear_semaphore_value = queue->next_signal_value;

        if (allocation->chunk)
            allocation->chunk->allocation.clear_semaphore_value = queue->next_signal_value;

        memset(&transfer, 0, sizeof(transfer));
        transfer.op = VKD3D_MEMORY_TRANSFER_OP_CLEAR_ALLOCATION;
        transfer.allocation = allocation;

        vkd3d_memory_transfer_queue_execute_transfer_locked(queue, &transfer);
        pthread_mutex_unlock(&queue->mutex);
    }
}

HRESULT vkd3d_memory_transfer_queue_write_subresource(struct vkd3d_memory_transfer_queue *queue,
        struct d3d12_resource *resource, uint32_t subresource_idx, VkOffset3D offset, VkExtent3D extent)
{
    struct vkd3d_memory_transfer_info transfer;

    memset(&transfer, 0, sizeof(transfer));
    transfer.op = VKD3D_MEMORY_TRANSFER_OP_WRITE_SUBRESOURCE;
    transfer.resource = resource;
    transfer.subresource_idx = subresource_idx;
    transfer.offset = offset;
    transfer.extent = extent;

    pthread_mutex_lock(&queue->mutex);
    vkd3d_memory_transfer_queue_execute_transfer_locked(queue, &transfer);
    pthread_mutex_unlock(&queue->mutex);
    return S_OK;
}

static void vkd3d_memory_transfer_queue_wait_allocation(struct vkd3d_memory_transfer_queue *queue,
        const struct vkd3d_memory_allocation *allocation)
{
    uint64_t wait_value = allocation->clear_semaphore_value;
    size_t i;

    /* If the clear semaphore has been signaled to the expected value,
     * the GPU is already done clearing the allocation, and it cannot
     * be in the clear queue either, so there is nothing to do. */
    if (vkd3d_memory_transfer_queue_wait_semaphore(queue, wait_value, 0))
        return;

    /* If the allocation is still in the queue, the GPU has not started
     * using it yet so we can remove it from the queue and exit. */
    pthread_mutex_lock(&queue->mutex);

    for (i = 0; i < queue->transfer_count; i++)
    {
        if (queue->transfers[i].op == VKD3D_MEMORY_TRANSFER_OP_CLEAR_ALLOCATION &&
                queue->transfers[i].allocation == allocation)
        {
            queue->transfers[i] = queue->transfers[--queue->transfer_count];
            queue->num_bytes_pending -= allocation->resource.size;
            pthread_mutex_unlock(&queue->mutex);
            return;
        }
    }

    /* If this is a chunk and a suballocation from it had been immediately
     * freed, it is possible that the suballocation got removed from the
     * clear queue so that the chunk's wait value never gets signaled. Wait
     * for the last signaled value in that case. */
    if (wait_value == queue->next_signal_value)
        wait_value = queue->next_signal_value - 1;

    pthread_mutex_unlock(&queue->mutex);

    /* If this allocation was suballocated from a chunk, we will wait
     * on the semaphore when the parent chunk itself gets destroyed. */
    if (allocation->chunk)
        return;

    /* Otherwise, we actually have to wait for the GPU. */
    WARN("Waiting for GPU to clear allocation %p.\n", allocation);

    vkd3d_memory_transfer_queue_wait_semaphore(queue, wait_value, UINT64_MAX);
}

static uint32_t vkd3d_select_memory_types(struct d3d12_device *device, const D3D12_HEAP_PROPERTIES *heap_properties, D3D12_HEAP_FLAGS heap_flags)
{
    const VkPhysicalDeviceMemoryProperties *memory_info = &device->memory_properties;
    uint32_t type_mask = (1 << memory_info->memoryTypeCount) - 1;
    const struct vkd3d_memory_info_domain *domain_info;

    domain_info = d3d12_device_get_memory_info_domain(device, heap_properties);

    if (!(heap_flags & D3D12_HEAP_FLAG_DENY_BUFFERS))
        type_mask &= domain_info->buffer_type_mask;

    if (!(heap_flags & D3D12_HEAP_FLAG_DENY_NON_RT_DS_TEXTURES))
        type_mask &= domain_info->sampled_type_mask;

    /* Render targets are not allowed on UPLOAD and READBACK heaps */
    if (!(heap_flags & D3D12_HEAP_FLAG_DENY_RT_DS_TEXTURES) &&
            heap_properties->Type != D3D12_HEAP_TYPE_UPLOAD &&
            heap_properties->Type != D3D12_HEAP_TYPE_READBACK)
        type_mask &= domain_info->rt_ds_type_mask;

    if (!type_mask)
        ERR("No memory type found for heap flags %#x.\n", heap_flags);

    return type_mask;
}

static uint32_t vkd3d_find_memory_types_with_flags(struct d3d12_device *device, VkMemoryPropertyFlags type_flags)
{
    const VkPhysicalDeviceMemoryProperties *memory_info = &device->memory_properties;
    uint32_t i, mask = 0;

    for (i = 0; i < memory_info->memoryTypeCount; i++)
    {
        if ((memory_info->memoryTypes[i].propertyFlags & type_flags) == type_flags)
            mask |= 1u << i;
    }

    return mask;
}

static HRESULT vkd3d_select_memory_flags(struct d3d12_device *device, const D3D12_HEAP_PROPERTIES *heap_properties, VkMemoryPropertyFlags *type_flags)
{
    HRESULT hr;
    switch (heap_properties->Type)
    {
        case D3D12_HEAP_TYPE_DEFAULT:
            *type_flags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
            break;

        case D3D12_HEAP_TYPE_UPLOAD:
            *type_flags = device->memory_info.upload_heap_memory_properties;
            break;

        case D3D12_HEAP_TYPE_READBACK:
            *type_flags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
            break;

        case D3D12_HEAP_TYPE_CUSTOM:
            if (FAILED(hr = d3d12_device_validate_custom_heap_type(device, heap_properties)))
                return hr;

            switch (heap_properties->CPUPageProperty)
            {
                case D3D12_CPU_PAGE_PROPERTY_WRITE_BACK:
                    *type_flags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
                    break;
                case D3D12_CPU_PAGE_PROPERTY_WRITE_COMBINE:
                    *type_flags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
                    if (vkd3d_config_flags & VKD3D_CONFIG_FLAG_FORCE_HOST_CACHED)
                        *type_flags |= VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
                    break;
                case D3D12_CPU_PAGE_PROPERTY_NOT_AVAILABLE:
                    *type_flags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
                    break;
                default:
                    return E_INVALIDARG;
            }
            break;

        default:
            WARN("Invalid heap type %#x.\n", heap_properties->Type);
            return E_INVALIDARG;
    }

    return S_OK;
}

static HRESULT vkd3d_create_global_buffer(struct d3d12_device *device, VkDeviceSize size, const D3D12_HEAP_PROPERTIES *heap_properties, D3D12_HEAP_FLAGS heap_flags, VkBuffer *vk_buffer)
{
    D3D12_RESOURCE_DESC1 resource_desc;

    memset(&resource_desc, 0, sizeof(resource_desc));
    resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    resource_desc.Width = size;
    resource_desc.Height = 1;
    resource_desc.DepthOrArraySize = 1;
    resource_desc.MipLevels = 1;
    resource_desc.SampleDesc.Count = 1;
    resource_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    if (heap_flags & D3D12_HEAP_FLAG_SHARED_CROSS_ADAPTER)
        resource_desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_CROSS_ADAPTER;

    if (heap_properties->Type != D3D12_HEAP_TYPE_UPLOAD &&
            heap_properties->Type != D3D12_HEAP_TYPE_READBACK)
        resource_desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    return vkd3d_create_buffer(device, heap_properties, heap_flags, &resource_desc, vk_buffer);
}

void vkd3d_free_device_memory(struct d3d12_device *device, const struct vkd3d_device_memory_allocation *allocation)
{
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    VkDeviceSize *type_current;
    bool budget_sensitive;

    if (allocation->vk_memory == VK_NULL_HANDLE)
    {
        /* Deferred heap. Return early to skip confusing log messages. */
        return;
    }

    VK_CALL(vkFreeMemory(device->vk_device, allocation->vk_memory, NULL));
    budget_sensitive = !!(device->memory_info.budget_sensitive_mask & (1u << allocation->vk_memory_type));
    if (budget_sensitive)
    {
        type_current = &device->memory_info.type_current[allocation->vk_memory_type];
        pthread_mutex_lock(&device->memory_info.budget_lock);
        assert(*type_current >= allocation->size);
        *type_current -= allocation->size;
        if (vkd3d_config_flags & VKD3D_CONFIG_FLAG_LOG_MEMORY_BUDGET)
        {
            INFO("Freeing memory of type %u, new total allocated size %"PRIu64" MiB.\n",
                    allocation->vk_memory_type, *type_current / (1024 * 1024));
        }
        pthread_mutex_unlock(&device->memory_info.budget_lock);
    }
    else if (vkd3d_config_flags & VKD3D_CONFIG_FLAG_LOG_MEMORY_BUDGET)
    {
        INFO("Freeing memory of type %u, %"PRIu64" KiB.\n",
                allocation->vk_memory_type, allocation->size / 1024);
    }
}

static HRESULT vkd3d_try_allocate_device_memory(struct d3d12_device *device,
        VkDeviceSize size, VkMemoryPropertyFlags type_flags, const uint32_t base_type_mask,
        void *pNext, bool respect_budget, struct vkd3d_device_memory_allocation *allocation)
{
    const VkPhysicalDeviceMemoryProperties *memory_props = &device->memory_properties;
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    struct vkd3d_memory_info *memory_info = &device->memory_info;
    VkMemoryAllocateInfo allocate_info;
    VkDeviceSize *type_current;
    VkDeviceSize *type_budget;
    bool budget_sensitive;
    uint32_t type_mask;
    VkResult vr;

    type_mask = base_type_mask;
    allocate_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocate_info.pNext = pNext;
    allocate_info.allocationSize = size;
    allocate_info.memoryTypeIndex = UINT32_MAX;

    while (type_mask)
    {
        uint32_t type_index = vkd3d_bitmask_iter32(&type_mask);
        if ((memory_props->memoryTypes[type_index].propertyFlags & type_flags) == type_flags)
        {
            allocate_info.memoryTypeIndex = type_index;
            break;
        }
    }

    if (allocate_info.memoryTypeIndex == UINT32_MAX)
    {
        /* We consider CACHED optional here if we cannot find a memory type that supports it.
         * Failing to allocate CACHED is not a scenario where we would fall back. */
        const VkMemoryPropertyFlags optional_flags =
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT;

        /* If we got here it means the requested type simply does not exist.
         * This can happen if we request PCI-e BAR,
         * but the driver somehow refuses to allow DEVICE | HOST_VISIBLE for a particular resource.
         * The fallback logic assumes that we attempted to allocate memory from a particular heap, and it will try
         * another allocation if it can identify at least 2 GPU heaps, but in this case, the calling code
         * might infer that we failed to allocate from a single supported GPU heap, and therefore there is no need
         * to try more. We still have not actually tried anything, so query the memory types again. */
        if (type_flags & optional_flags)
        {
            return vkd3d_try_allocate_device_memory(device, size,
                    type_flags & ~optional_flags,
                    base_type_mask, pNext, respect_budget, allocation);
        }
        else
        {
            FIXME("Found no suitable memory type for requested type_flags #%x.\n", type_flags);
            return E_OUTOFMEMORY;
        }
    }

    /* Once we have found a suitable memory type, only attempt to allocate that memory type.
     * This avoids some problems by design:
     * - If we want to allocate DEVICE_LOCAL memory, we don't try to fallback to PCI-e BAR memory by mistake.
     * - If we want to allocate system memory, we don't try to fallback to PCI-e BAR memory by mistake.
     * - There is no reasonable scenario where we can expect one memory type to fail, and another memory type
     *   with more memory property bits set to pass. This makes use of the rule where memory types which are a super-set
     *   of another must have a larger type index.
     * - We will only attempt to allocate PCI-e BAR memory if DEVICE_LOCAL | HOST_VISIBLE is set, otherwise we
     *   will find a candidate memory type which is either DEVICE_LOCAL or HOST_VISIBLE before we find a PCI-e BAR type.
     * - For iGPU where everything is DEVICE_LOCAL | HOST_VISIBLE, we will just find that memory type first anyways,
     *   but there we don't have anything to worry about w.r.t. PCI-e BAR.
     */

    /* Budgets only really apply to PCI-e BAR or other "special" types which always have a fallback. */
    budget_sensitive = !!(device->memory_info.budget_sensitive_mask & (1u << allocate_info.memoryTypeIndex));
    if (budget_sensitive)
    {
        type_budget = &memory_info->type_budget[allocate_info.memoryTypeIndex];
        type_current = &memory_info->type_current[allocate_info.memoryTypeIndex];

        if (respect_budget)
        {
            pthread_mutex_lock(&memory_info->budget_lock);
            if (*type_current + size > *type_budget)
            {
                if (vkd3d_config_flags & VKD3D_CONFIG_FLAG_LOG_MEMORY_BUDGET)
                {
                    INFO("Attempting to allocate from memory type %u, but exceeding fixed budget: %"PRIu64" + %"PRIu64" > %"PRIu64".\n",
                            allocate_info.memoryTypeIndex, *type_current, size, *type_budget);
                }
                pthread_mutex_unlock(&memory_info->budget_lock);
                return E_OUTOFMEMORY;
            }
        }
    }

    vr = VK_CALL(vkAllocateMemory(device->vk_device, &allocate_info, NULL, &allocation->vk_memory));

    if (budget_sensitive)
    {
        if (!respect_budget)
            pthread_mutex_lock(&memory_info->budget_lock);

        if (vr == VK_SUCCESS)
        {
            *type_current += size;
            if (vkd3d_config_flags & VKD3D_CONFIG_FLAG_LOG_MEMORY_BUDGET)
            {
                INFO("Allocated %s memory of type %u, new total allocated size %"PRIu64" MiB.\n",
                        respect_budget ? "budgeted" : "internal non-budgeted",
                        allocate_info.memoryTypeIndex, *type_current / (1024 * 1024));
            }
        }
        pthread_mutex_unlock(&memory_info->budget_lock);
    }
    else if (vkd3d_config_flags & VKD3D_CONFIG_FLAG_LOG_MEMORY_BUDGET)
    {
        INFO("%s memory of type #%u, size %"PRIu64" KiB.\n",
                (vr == VK_SUCCESS ? "Allocated" : "Failed to allocate"),
                allocate_info.memoryTypeIndex, allocate_info.allocationSize / 1024);
    }

    if (vr != VK_SUCCESS)
        return E_OUTOFMEMORY;

    allocation->vk_memory_type = allocate_info.memoryTypeIndex;
    allocation->size = size;
    return S_OK;
}

static bool vkd3d_memory_info_type_mask_covers_multiple_memory_heaps(
        const struct VkPhysicalDeviceMemoryProperties *props, uint32_t type_mask)
{
    uint32_t heap_mask = 0;
    if (!type_mask)
        return false;
    while (type_mask)
        heap_mask |= 1u << props->memoryTypes[vkd3d_bitmask_iter32(&type_mask)].heapIndex;
    return !!(heap_mask & (heap_mask - 1u));
}

HRESULT vkd3d_allocate_device_memory(struct d3d12_device *device,
        VkDeviceSize size, VkMemoryPropertyFlags type_flags, uint32_t type_mask,
        void *pNext, bool respect_budget, struct vkd3d_device_memory_allocation *allocation)
{
    const VkMemoryPropertyFlags optional_flags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    HRESULT hr;

    hr = vkd3d_try_allocate_device_memory(device, size, type_flags,
            type_mask, pNext, respect_budget, allocation);

    if (FAILED(hr) && (type_flags & optional_flags))
    {
        if (vkd3d_memory_info_type_mask_covers_multiple_memory_heaps(&device->memory_properties, type_mask))
        {
            WARN("Memory allocation failed, falling back to system memory.\n");
            hr = vkd3d_try_allocate_device_memory(device, size,
                    type_flags & ~optional_flags, type_mask, pNext, respect_budget, allocation);
        }
        else if (device->memory_properties.memoryHeapCount > 1)
        {
            /* It might be the case (NV with RT/DS heap) that we just cannot fall back in any meaningful way.
             * E.g. there exists no memory type that is not DEVICE_LOCAL and covers both RT and DS.
             * For this case, we have no choice but to not allocate,
             * and defer actual memory allocation to CreatePlacedResource() time.
			 * NVIDIA bug reference for fixing this case: 2175829. */
            WARN("Memory allocation failed, but it is not possible to fallback to system memory here. Deferring allocation.\n");
            return hr;
        }

        /* If we fail to allocate, and only have one heap to work with (iGPU),
         * falling back is meaningless, just fail. */
    }

    if (FAILED(hr))
    {
        ERR("Failed to allocate device memory (size %"PRIu64", type_flags %#x, type_mask %#x).\n",
                size, type_flags, type_mask);
    }

    return hr;
}

static HRESULT vkd3d_import_host_memory(struct d3d12_device *device, void *host_address,
        VkDeviceSize size, VkMemoryPropertyFlags type_flags, uint32_t type_mask,
        void *pNext, struct vkd3d_device_memory_allocation *allocation)
{
    VkImportMemoryHostPointerInfoEXT import_info;
    HRESULT hr = S_OK;

    import_info.sType = VK_STRUCTURE_TYPE_IMPORT_MEMORY_HOST_POINTER_INFO_EXT;
    import_info.pNext = pNext;
    import_info.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT;
    import_info.pHostPointer = host_address;

    if ((vkd3d_config_flags & VKD3D_CONFIG_FLAG_USE_HOST_IMPORT_FALLBACK) ||
        FAILED(hr = vkd3d_try_allocate_device_memory(device, size,
            type_flags, type_mask, &import_info, true, allocation)))
    {
        if (FAILED(hr))
            WARN("Failed to import host memory, hr %#x.\n", hr);
        /* If we failed, fall back to a host-visible allocation. Generally
         * the app will access the memory thorugh the main host pointer,
         * so it's fine. */
        hr = vkd3d_try_allocate_device_memory(device, size,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                type_mask, pNext, true, allocation);
    }

    return hr;
}

static HRESULT vkd3d_allocation_assign_gpu_address(struct vkd3d_memory_allocation *allocation,
        struct d3d12_device *device, struct vkd3d_memory_allocator *allocator)
{
    allocation->resource.va = vkd3d_get_buffer_device_address(device, allocation->resource.vk_buffer);

    if (!allocation->resource.va)
    {
        ERR("Failed to get GPU address for allocation.\n");
        return E_OUTOFMEMORY;
    }

    /* Internal scratch buffers are not visible to application so we never have to map it back to VkBuffer. */
    if (!(allocation->flags & VKD3D_ALLOCATION_FLAG_INTERNAL_SCRATCH))
        vkd3d_va_map_insert(&allocator->va_map, &allocation->resource);
    return S_OK;
}

static void *vkd3d_allocate_write_watch_pointer(const D3D12_HEAP_PROPERTIES *properties, VkDeviceSize size)
{
#ifdef _WIN32
    DWORD protect;
    void *ptr;

    switch (properties->Type)
    {
    case D3D12_HEAP_TYPE_DEFAULT:
        return NULL;
    case D3D12_HEAP_TYPE_UPLOAD:
        protect = PAGE_READWRITE | PAGE_WRITECOMBINE;
        break;
    case D3D12_HEAP_TYPE_READBACK:
        /* WRITE_WATCH fails for this type in native D3D12,
         * otherwise it would be PAGE_READWRITE. */
        return NULL;
    case D3D12_HEAP_TYPE_CUSTOM:
        switch (properties->CPUPageProperty)
        {
        case D3D12_CPU_PAGE_PROPERTY_UNKNOWN:
            return NULL;
        case D3D12_CPU_PAGE_PROPERTY_NOT_AVAILABLE:
            return NULL;
        case D3D12_CPU_PAGE_PROPERTY_WRITE_COMBINE:
            protect = PAGE_READWRITE | PAGE_WRITECOMBINE;
            break;
        case D3D12_CPU_PAGE_PROPERTY_WRITE_BACK:
            protect = PAGE_READWRITE;
            break;
        default:
            ERR("Invalid CPU page property %#x.\n", properties->CPUPageProperty);
            return NULL;
        }
        break;
    default:
        ERR("Invalid heap type %#x.\n", properties->Type);
        return NULL;
    }

    if (!(ptr = VirtualAlloc(NULL, (SIZE_T)size, MEM_COMMIT | MEM_RESERVE | MEM_WRITE_WATCH, protect)))
    {
        ERR("Failed to allocate write watch pointer %#x.\n", GetLastError());
        return NULL;
    }

    return ptr;
#else
    (void)properties;
    (void)size;

    ERR("WRITE_WATCH not supported on this platform.\n");
    return NULL;
#endif
}

static void vkd3d_free_write_watch_pointer(void *pointer)
{
#ifdef _WIN32
    if (!VirtualFree(pointer, 0, MEM_RELEASE))
        ERR("Failed to free write watch pointer %#x.\n", GetLastError());
#else
    /* Not supported on other platforms. */
    (void)pointer;
#endif
}

static void vkd3d_memory_allocation_free(const struct vkd3d_memory_allocation *allocation, struct d3d12_device *device, struct vkd3d_memory_allocator *allocator)
{
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;

    TRACE("allocation %p, device %p, allocator %p.\n", allocation, device, allocator);

    vkd3d_descriptor_debug_unregister_cookie(device->descriptor_qa_global_info, allocation->resource.cookie);

    if (allocation->flags & VKD3D_ALLOCATION_FLAG_ALLOW_WRITE_WATCH)
        vkd3d_free_write_watch_pointer(allocation->cpu_address);

    if ((allocation->flags & VKD3D_ALLOCATION_FLAG_GPU_ADDRESS) && allocation->resource.va &&
            !(allocation->flags & VKD3D_ALLOCATION_FLAG_INTERNAL_SCRATCH))
    {
        vkd3d_va_map_remove(&allocator->va_map, &allocation->resource);
    }

    if (allocation->resource.view_map)
    {
        vkd3d_view_map_destroy(allocation->resource.view_map, device);
        vkd3d_free(allocation->resource.view_map);
    }

    if (allocation->flags & VKD3D_ALLOCATION_FLAG_GLOBAL_BUFFER)
        VK_CALL(vkDestroyBuffer(device->vk_device, allocation->resource.vk_buffer, NULL));

    vkd3d_free_device_memory(device, &allocation->device_allocation);
}

static HRESULT vkd3d_memory_allocation_init(struct vkd3d_memory_allocation *allocation, struct d3d12_device *device,
        struct vkd3d_memory_allocator *allocator, const struct vkd3d_allocate_memory_info *info)
{
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    VkMemoryRequirements memory_requirements;
    VkMemoryAllocateFlagsInfo flags_info;
    VkMemoryPropertyFlags type_flags;
    VkBindBufferMemoryInfo bind_info;
    void *host_ptr = info->host_ptr;
    uint32_t type_mask;
    bool request_bda;
    VkResult vr;
    HRESULT hr;

    TRACE("allocation %p, device %p, allocator %p, info %p.\n", allocation, device, allocator, info);

    memset(allocation, 0, sizeof(*allocation));
    allocation->heap_type = info->heap_properties.Type;
    allocation->heap_flags = info->heap_flags;
    allocation->flags = info->flags;
    allocation->explicit_global_buffer_usage = info->explicit_global_buffer_usage;

    /* This also sort of validates the heap description,
     * so we want to do this before creating any objects */
    if (FAILED(hr = vkd3d_select_memory_flags(device, &info->heap_properties, &type_flags)))
        return hr;

    /* Mask out optional memory properties as needed.
     * This is relevant for chunk allocator fallbacks
     * since the info->memory_requirements already encodes
     * only HOST_VISIBLE types and we use NO_FALLBACK allocation mode. */
    type_flags &= ~info->optional_memory_properties;

    if (allocation->flags & VKD3D_ALLOCATION_FLAG_GLOBAL_BUFFER)
    {
        if (info->explicit_global_buffer_usage)
        {
            /* If we only need specific buffer usages (used purely to clear memory for example),
             * we can relax buffer usage and help tools. If we only use TRANSFER usage, it's trivial
             * to prove if a VkBuffer has ever been used. With BDA and bindless, not so much ... */
            if (FAILED(hr = vkd3d_create_buffer_explicit_usage(device,
                    info->explicit_global_buffer_usage,
                    info->memory_requirements.size,
                    &allocation->resource.vk_buffer)))
                return hr;

            request_bda = !!(info->explicit_global_buffer_usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT);
        }
        else
        {
            request_bda = true;
            /* If requested, create a buffer covering the entire allocation
             * and derive the exact memory requirements from that. Any buffer
             * resources are just going to use this buffer with an offset. */
            if (FAILED(hr = vkd3d_create_global_buffer(device, info->memory_requirements.size,
                    &info->heap_properties, info->heap_flags, &allocation->resource.vk_buffer)))
                return hr;
        }

        VK_CALL(vkGetBufferMemoryRequirements(device->vk_device,
                allocation->resource.vk_buffer, &memory_requirements));

        memory_requirements.memoryTypeBits &= info->memory_requirements.memoryTypeBits;
    }
    else
    {
        /* Respect existing memory requirements since there may not
         * be any buffer resource to get memory requirements from. */
        memory_requirements = info->memory_requirements;
        assert(!info->explicit_global_buffer_usage);
    }

    /* If an allocation is a dedicated fallback allocation,
     * we must not look at heap_flags, since we might end up noping out
     * the memory types we want to allocate with. */
    type_mask = memory_requirements.memoryTypeBits;
    if (!(info->flags & VKD3D_ALLOCATION_FLAG_DEDICATED))
        type_mask &= vkd3d_select_memory_types(device, &info->heap_properties, info->heap_flags);

    /* Allocate actual backing storage */
    flags_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
    flags_info.pNext = info->pNext;
    flags_info.flags = 0;

    if (allocation->resource.vk_buffer && request_bda)
    {
        allocation->flags |= VKD3D_ALLOCATION_FLAG_GPU_ADDRESS;
        flags_info.flags |= VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
    }

    allocation->resource.size = info->memory_requirements.size;

    if (info->heap_flags & D3D12_HEAP_FLAG_ALLOW_WRITE_WATCH)
    {
        assert(!host_ptr);

        allocation->flags |= VKD3D_ALLOCATION_FLAG_ALLOW_WRITE_WATCH;
        if (!(host_ptr = vkd3d_allocate_write_watch_pointer(&info->heap_properties, memory_requirements.size)))
        {
            VK_CALL(vkDestroyBuffer(device->vk_device, allocation->resource.vk_buffer, NULL));
            return E_INVALIDARG;
        }
    }

    if (host_ptr)
    {
        hr = vkd3d_import_host_memory(device, host_ptr, memory_requirements.size,
                type_flags, type_mask, &flags_info, &allocation->device_allocation);
    }
    else if (info->flags & VKD3D_ALLOCATION_FLAG_NO_FALLBACK)
    {
        hr = vkd3d_try_allocate_device_memory(device, memory_requirements.size, type_flags,
                type_mask, &flags_info, true, &allocation->device_allocation);
    }
    else
    {
        hr = vkd3d_allocate_device_memory(device, memory_requirements.size, type_flags,
                type_mask, &flags_info, true, &allocation->device_allocation);
    }

    if (FAILED(hr))
    {
        VK_CALL(vkDestroyBuffer(device->vk_device, allocation->resource.vk_buffer, NULL));
        return hr;
    }

    /* Map memory if the allocation was requested to be host-visible,
     * but do not map if the allocation was meant to be device-local
     * since that may negatively impact performance. */
    if (host_ptr)
    {
        allocation->flags |= VKD3D_ALLOCATION_FLAG_CPU_ACCESS;

        /* No need to call map here, we already know the pointer. */
        allocation->cpu_address = host_ptr;
    }
    else if (type_flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)
    {
        allocation->flags |= VKD3D_ALLOCATION_FLAG_CPU_ACCESS;

        if ((vr = VK_CALL(vkMapMemory(device->vk_device, allocation->device_allocation.vk_memory,
                0, VK_WHOLE_SIZE, 0, &allocation->cpu_address))))
        {
            ERR("Failed to map memory, vr %d.\n", vr);
            vkd3d_memory_allocation_free(allocation, device, allocator);
            return hresult_from_vk_result(vr);
        }
    }

    allocation->resource.cookie = vkd3d_allocate_cookie();

    /* Bind memory to global or dedicated buffer as needed */
    if (allocation->resource.vk_buffer)
    {
        bind_info.sType = VK_STRUCTURE_TYPE_BIND_BUFFER_MEMORY_INFO;
        bind_info.pNext = NULL;
        bind_info.buffer = allocation->resource.vk_buffer;
        bind_info.memory = allocation->device_allocation.vk_memory;
        bind_info.memoryOffset = 0;

        if ((vr = VK_CALL(vkBindBufferMemory2(device->vk_device, 1, &bind_info))) < 0)
        {
            ERR("Failed to bind buffer memory, vr %d.\n", vr);
            vkd3d_memory_allocation_free(allocation, device, allocator);
            return hresult_from_vk_result(vr);
        }

        /* Assign GPU address as necessary. */
        if (allocation->flags & VKD3D_ALLOCATION_FLAG_GPU_ADDRESS)
        {
            assert(allocation->flags & VKD3D_ALLOCATION_FLAG_GLOBAL_BUFFER);
            assert(request_bda);

            if (FAILED(hr = vkd3d_allocation_assign_gpu_address(allocation, device, allocator)))
            {
                vkd3d_memory_allocation_free(allocation, device, allocator);
                return hresult_from_vk_result(vr);
            }
        }

        if (vkd3d_config_flags & VKD3D_CONFIG_FLAG_DEBUG_UTILS)
        {
            char name_buffer[1024];
            snprintf(name_buffer, sizeof(name_buffer), "GlobalBuffer (cookie %"PRIu64")",
                    allocation->resource.cookie);
            vkd3d_set_vk_object_name(device, (uint64_t)allocation->resource.vk_buffer,
                    VK_OBJECT_TYPE_BUFFER, name_buffer);
        }
    }

    vkd3d_descriptor_debug_register_allocation_cookie(device->descriptor_qa_global_info,
            allocation->resource.cookie, info);

    TRACE("Created allocation %p on memory type %u (%"PRIu64" bytes).\n",
            allocation, allocation->device_allocation.vk_memory_type, allocation->resource.size);
    return S_OK;
}

static void vkd3d_memory_chunk_insert_range(struct vkd3d_memory_chunk *chunk,
        size_t index, VkDeviceSize offset, VkDeviceSize length)
{
    if (!vkd3d_array_reserve((void**)&chunk->free_ranges, &chunk->free_ranges_size,
            chunk->free_ranges_count + 1, sizeof(*chunk->free_ranges)))
    {
        ERR("Failed to insert free range.\n");
        return;
    }

    memmove(&chunk->free_ranges[index + 1], &chunk->free_ranges[index],
            sizeof(*chunk->free_ranges) * (chunk->free_ranges_count - index));

    chunk->free_ranges[index].offset = offset;
    chunk->free_ranges[index].length = length;
    chunk->free_ranges_count++;
}

static void vkd3d_memory_chunk_remove_range(struct vkd3d_memory_chunk *chunk, size_t index)
{
    chunk->free_ranges_count--;

    memmove(&chunk->free_ranges[index], &chunk->free_ranges[index + 1],
            sizeof(*chunk->free_ranges) * (chunk->free_ranges_count - index));
}

static HRESULT vkd3d_memory_chunk_allocate_range(struct vkd3d_memory_chunk *chunk, const VkMemoryRequirements *memory_requirements,
        struct vkd3d_memory_allocation *allocation)
{
    struct vkd3d_memory_free_range *pick_range;
    VkDeviceSize l_length, r_length;
    size_t i, pick_index;

    if (!chunk->free_ranges_count)
        return E_OUTOFMEMORY;

    pick_index = chunk->free_ranges_count;
    pick_range = NULL;

    for (i = 0; i < chunk->free_ranges_count; i++)
    {
        struct vkd3d_memory_free_range *range = &chunk->free_ranges[i];

        if (range->offset + range->length < align(range->offset, memory_requirements->alignment) + memory_requirements->size)
            continue;

        /* Exact fit leaving no gaps */
        if (range->length == memory_requirements->size)
        {
            pick_index = i;
            pick_range = range;
            break;
        }

        /* Alignment is almost always going to be 64 KiB, so
         * don't worry too much about misalignment gaps here */
        if (!pick_range || range->length > pick_range->length)
        {
            pick_index = i;
            pick_range = range;
        }
    }

    if (!pick_range)
        return E_OUTOFMEMORY;

    /* Adjust offsets and addresses of the base allocation */
    vkd3d_memory_allocation_slice(allocation, &chunk->allocation,
            align(pick_range->offset, memory_requirements->alignment),
            memory_requirements->size);
    allocation->chunk = chunk;

    /* Remove allocated range from the free list */
    l_length = allocation->offset - pick_range->offset;
    r_length = pick_range->offset + pick_range->length
            - allocation->offset - allocation->resource.size;

    if (l_length)
    {
        pick_range->length = l_length;

        if (r_length)
        {
            vkd3d_memory_chunk_insert_range(chunk, pick_index + 1,
                allocation->offset + allocation->resource.size, r_length);
        }
    }
    else if (r_length)
    {
        pick_range->offset = allocation->offset + allocation->resource.size;
        pick_range->length = r_length;
    }
    else
    {
        vkd3d_memory_chunk_remove_range(chunk, pick_index);
    }

    return S_OK;
}

static size_t vkd3d_memory_chunk_find_range(struct vkd3d_memory_chunk *chunk, VkDeviceSize offset)
{
    struct vkd3d_memory_free_range *range;
    size_t index, hi, lo;

    lo = 0;
    hi = chunk->free_ranges_count;

    while (lo < hi)
    {
        index = lo + (hi - lo) / 2;
        range = &chunk->free_ranges[index];

        if (range->offset > offset)
            hi = index;
        else
            lo = index + 1;
    }

    return lo;
}

static void vkd3d_memory_chunk_free_range(struct vkd3d_memory_chunk *chunk, const struct vkd3d_memory_allocation *allocation)
{
    struct vkd3d_memory_free_range *range;
    bool adjacent_l, adjacent_r;
    size_t index;

    index = vkd3d_memory_chunk_find_range(chunk, allocation->offset);

    adjacent_l = false;
    adjacent_r = false;

    if (index > 0)
    {
        range = &chunk->free_ranges[index - 1];
        adjacent_l = range->offset + range->length == allocation->offset;
    }

    if (index < chunk->free_ranges_count)
    {
        range = &chunk->free_ranges[index];
        adjacent_r = range->offset == allocation->offset + allocation->resource.size;
    }

    if (adjacent_l)
    {
        range = &chunk->free_ranges[index - 1];
        range->length += allocation->resource.size;

        if (adjacent_r)
        {
            range->length += chunk->free_ranges[index].length;
            vkd3d_memory_chunk_remove_range(chunk, index);
        }
    }
    else if (adjacent_r)
    {
        range = &chunk->free_ranges[index];
        range->offset = allocation->offset;
        range->length += allocation->resource.size;
    }
    else
    {
        vkd3d_memory_chunk_insert_range(chunk, index,
                allocation->offset, allocation->resource.size);
    }
}

static bool vkd3d_memory_chunk_is_free(struct vkd3d_memory_chunk *chunk)
{
    return chunk->free_ranges_count == 1 && chunk->free_ranges[0].length == chunk->allocation.resource.size;
}

static HRESULT vkd3d_memory_chunk_create(struct d3d12_device *device, struct vkd3d_memory_allocator *allocator,
        const struct vkd3d_allocate_memory_info *info, struct vkd3d_memory_chunk **chunk)
{
    struct vkd3d_memory_chunk *object;
    HRESULT hr;

    TRACE("device %p, allocator %p, info %p, chunk %p.\n", device, allocator, info, chunk);

    if (!(object = vkd3d_malloc(sizeof(*object))))
        return E_OUTOFMEMORY;

    memset(object, 0, sizeof(*object));

    if (FAILED(hr = vkd3d_memory_allocation_init(&object->allocation, device, allocator, info)))
    {
        vkd3d_free(object);
        return hr;
    }

    if (vkd3d_config_flags & VKD3D_CONFIG_FLAG_DEBUG_UTILS)
    {
        char name_buffer[1024];
        snprintf(name_buffer, sizeof(name_buffer), "Chunk (cookie %"PRIu64")",
                object->allocation.resource.cookie);
        vkd3d_set_vk_object_name(device, (uint64_t)object->allocation.device_allocation.vk_memory,
                VK_OBJECT_TYPE_DEVICE_MEMORY, name_buffer);
    }

    vkd3d_memory_chunk_insert_range(object, 0, 0, object->allocation.resource.size);
    *chunk = object;

    TRACE("Created chunk %p (allocation %p).\n", object, &object->allocation);
    return S_OK;
}

static void vkd3d_memory_chunk_destroy(struct vkd3d_memory_chunk *chunk, struct d3d12_device *device, struct vkd3d_memory_allocator *allocator)
{
    TRACE("chunk %p, device %p, allocator %p.\n", chunk, device, allocator);

    if (chunk->allocation.clear_semaphore_value)
        vkd3d_memory_transfer_queue_wait_allocation(&device->memory_transfers, &chunk->allocation);

    vkd3d_memory_allocation_free(&chunk->allocation, device, allocator);
    vkd3d_free(chunk->free_ranges);
    vkd3d_free(chunk);
}

static void vkd3d_memory_allocator_remove_chunk(struct vkd3d_memory_allocator *allocator, struct d3d12_device *device, struct vkd3d_memory_chunk *chunk)
{
    size_t i;

    for (i = 0; i < allocator->chunks_count; i++)
    {
        if (allocator->chunks[i] == chunk)
        {
            allocator->chunks[i] = allocator->chunks[--allocator->chunks_count];
            break;
        }
    }

    vkd3d_memory_chunk_destroy(chunk, device, allocator);
}

HRESULT vkd3d_memory_allocator_init(struct vkd3d_memory_allocator *allocator, struct d3d12_device *device)
{
    int rc;

    memset(allocator, 0, sizeof(*allocator));

    if ((rc = pthread_mutex_init(&allocator->mutex, NULL)))
        return hresult_from_errno(rc);

    vkd3d_va_map_init(&allocator->va_map);
    return S_OK;
}

void vkd3d_memory_allocator_cleanup(struct vkd3d_memory_allocator *allocator, struct d3d12_device *device)
{
    size_t i;

    for (i = 0; i < allocator->chunks_count; i++)
        vkd3d_memory_chunk_destroy(allocator->chunks[i], device, allocator);

    vkd3d_free(allocator->chunks);
    vkd3d_va_map_cleanup(&allocator->va_map);
    pthread_mutex_destroy(&allocator->mutex);
}

static HRESULT vkd3d_memory_allocator_try_add_chunk(struct vkd3d_memory_allocator *allocator, struct d3d12_device *device,
        const D3D12_HEAP_PROPERTIES *heap_properties, D3D12_HEAP_FLAGS heap_flags, uint32_t type_mask,
        VkMemoryPropertyFlags optional_properties,
        VkBufferUsageFlags explicit_global_buffer_usage,
        struct vkd3d_memory_chunk **chunk)
{
    struct vkd3d_allocate_memory_info alloc_info;
    struct vkd3d_memory_chunk *object;
    HRESULT hr;

    memset(&alloc_info, 0, sizeof(alloc_info));
    alloc_info.memory_requirements.size = VKD3D_MEMORY_CHUNK_SIZE;
    alloc_info.memory_requirements.alignment = 0;
    alloc_info.memory_requirements.memoryTypeBits = type_mask;
    alloc_info.heap_properties = *heap_properties;
    alloc_info.heap_flags = heap_flags;
    alloc_info.flags = VKD3D_ALLOCATION_FLAG_NO_FALLBACK;
    alloc_info.optional_memory_properties = optional_properties;

    if (!(heap_flags & D3D12_HEAP_FLAG_DENY_BUFFERS))
    {
        alloc_info.flags |= VKD3D_ALLOCATION_FLAG_GLOBAL_BUFFER;
        alloc_info.explicit_global_buffer_usage = explicit_global_buffer_usage;
    }

    if (!vkd3d_array_reserve((void**)&allocator->chunks, &allocator->chunks_size,
            allocator->chunks_count + 1, sizeof(*allocator->chunks)))
    {
        ERR("Failed to allocate space for new chunk.\n");
        return E_OUTOFMEMORY;
    }

    if (FAILED(hr = vkd3d_memory_chunk_create(device, allocator, &alloc_info, &object)))
        return hr;

    allocator->chunks[allocator->chunks_count++] = *chunk = object;
    return S_OK;
}

static HRESULT vkd3d_memory_allocator_try_suballocate_memory(struct vkd3d_memory_allocator *allocator,
        struct d3d12_device *device, const VkMemoryRequirements *memory_requirements, uint32_t type_mask,
        VkMemoryPropertyFlags optional_properties,
        const D3D12_HEAP_PROPERTIES *heap_properties, D3D12_HEAP_FLAGS heap_flags,
        VkBufferUsageFlags explicit_global_buffer_usage,
        struct vkd3d_memory_allocation *allocation)
{
    const D3D12_HEAP_FLAGS heap_flag_mask = ~(D3D12_HEAP_FLAG_CREATE_NOT_ZEROED | D3D12_HEAP_FLAG_CREATE_NOT_RESIDENT);
    struct vkd3d_memory_chunk *chunk;
    HRESULT hr;
    size_t i;

    type_mask &= memory_requirements->memoryTypeBits;

    for (i = 0; i < allocator->chunks_count; i++)
    {
        chunk = allocator->chunks[i];

        /* Match flags since otherwise the backing buffer
         * may not support our required usage flags */
        if (chunk->allocation.heap_type != heap_properties->Type ||
                chunk->allocation.explicit_global_buffer_usage != explicit_global_buffer_usage ||
                chunk->allocation.heap_flags != (heap_flags & heap_flag_mask))
            continue;

        /* Filter out unsupported memory types */
        if (!(type_mask & (1u << chunk->allocation.device_allocation.vk_memory_type)))
            continue;

        if (SUCCEEDED(hr = vkd3d_memory_chunk_allocate_range(chunk, memory_requirements, allocation)))
            return hr;
    }

    /* Try allocating a new chunk on one of the supported memory type
     * before the caller falls back to potentially slower memory */
    if (FAILED(hr = vkd3d_memory_allocator_try_add_chunk(allocator, device, heap_properties,
            heap_flags & heap_flag_mask, type_mask, optional_properties,
            explicit_global_buffer_usage, &chunk)))
        return hr;

    return vkd3d_memory_chunk_allocate_range(chunk, memory_requirements, allocation);
}

void vkd3d_free_memory(struct d3d12_device *device, struct vkd3d_memory_allocator *allocator,
        const struct vkd3d_memory_allocation *allocation)
{
    if (allocation->device_allocation.vk_memory == VK_NULL_HANDLE)
        return;

    if (allocation->clear_semaphore_value)
        vkd3d_memory_transfer_queue_wait_allocation(&device->memory_transfers, allocation);

    if (allocation->chunk)
    {
        pthread_mutex_lock(&allocator->mutex);
        vkd3d_memory_chunk_free_range(allocation->chunk, allocation);

        if (vkd3d_memory_chunk_is_free(allocation->chunk))
            vkd3d_memory_allocator_remove_chunk(allocator, device, allocation->chunk);
        pthread_mutex_unlock(&allocator->mutex);
    }
    else
        vkd3d_memory_allocation_free(allocation, device, allocator);
}

static HRESULT vkd3d_suballocate_memory(struct d3d12_device *device, struct vkd3d_memory_allocator *allocator,
        const struct vkd3d_allocate_memory_info *info, struct vkd3d_memory_allocation *allocation)
{
    const VkMemoryPropertyFlags optional_flags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    VkMemoryRequirements memory_requirements = info->memory_requirements;
    uint32_t required_mask, optional_mask;
    VkMemoryPropertyFlags type_flags;
    HRESULT hr;

    if (FAILED(hr = vkd3d_select_memory_flags(device, &info->heap_properties, &type_flags)))
        return hr;

    /* Prefer device-local memory if allowed for this allocation */
    required_mask = vkd3d_find_memory_types_with_flags(device, type_flags & ~optional_flags);
    optional_mask = vkd3d_find_memory_types_with_flags(device, type_flags);

    pthread_mutex_lock(&allocator->mutex);

    hr = vkd3d_memory_allocator_try_suballocate_memory(allocator, device,
            &memory_requirements, optional_mask, 0, &info->heap_properties,
            info->heap_flags, info->explicit_global_buffer_usage, allocation);

    if (FAILED(hr) && (required_mask & ~optional_mask))
    {
        hr = vkd3d_memory_allocator_try_suballocate_memory(allocator, device,
                &memory_requirements, required_mask & ~optional_mask,
                optional_flags,
                &info->heap_properties, info->heap_flags, info->explicit_global_buffer_usage,
                allocation);
    }

    pthread_mutex_unlock(&allocator->mutex);
    return hr;
}

static inline bool vkd3d_driver_implicitly_clears(VkDriverId driver_id)
{
    switch (driver_id)
    {
        /* Known to pass test_stress_suballocation which hits this path. */
        case VK_DRIVER_ID_MESA_RADV:
        case VK_DRIVER_ID_NVIDIA_PROPRIETARY:
        case VK_DRIVER_ID_INTEL_OPEN_SOURCE_MESA:
            return true;

        default:
            return false;
    }
}

bool vkd3d_allocate_image_memory_prefers_dedicated(struct d3d12_device *device,
        D3D12_HEAP_FLAGS heap_flags, const VkMemoryRequirements *requirements)
{
    if (vkd3d_config_flags & VKD3D_CONFIG_FLAG_FORCE_DEDICATED_IMAGE_ALLOCATION)
        return true;

    /* If we don't need to sub-allocate, and we don't need to clear any buffers
     * there is no need to allocate a GLOBAL_BUFFER. */
    return requirements->size >= VKD3D_VA_BLOCK_SIZE &&
            (vkd3d_driver_implicitly_clears(device->device_info.vulkan_1_2_properties.driverID) ||
                    (heap_flags & D3D12_HEAP_FLAG_CREATE_NOT_ZEROED));
}

HRESULT vkd3d_allocate_memory(struct d3d12_device *device, struct vkd3d_memory_allocator *allocator,
        const struct vkd3d_allocate_memory_info *info, struct vkd3d_memory_allocation *allocation)
{
    bool implementation_implicitly_clears;
    bool needs_clear;
    bool suballocate;
    HRESULT hr;

    suballocate = !info->pNext && !info->host_ptr &&
            info->memory_requirements.size < VKD3D_VA_BLOCK_SIZE &&
            !(info->heap_flags & (D3D12_HEAP_FLAG_DENY_BUFFERS | D3D12_HEAP_FLAG_ALLOW_WRITE_WATCH)) &&
            !(info->flags & VKD3D_ALLOCATION_FLAG_INTERNAL_SCRATCH);

    if (suballocate)
        hr = vkd3d_suballocate_memory(device, allocator, info, allocation);
    else
        hr = vkd3d_memory_allocation_init(allocation, device, allocator, info);

    if (FAILED(hr))
        return hr;

    /* If we're allocating Vulkan memory directly,
     * we can rely on the driver doing this for us.
     * This is relying on implementation details.
     * RADV definitely does this, and it seems like NV also does it.
     * TODO: an extension for this would be nice. */
    implementation_implicitly_clears =
            vkd3d_driver_implicitly_clears(device->device_info.vulkan_1_2_properties.driverID) &&
            !suballocate;

    needs_clear = !implementation_implicitly_clears &&
            !(info->heap_flags & D3D12_HEAP_FLAG_CREATE_NOT_ZEROED) &&
            !(vkd3d_config_flags & VKD3D_CONFIG_FLAG_MEMORY_ALLOCATOR_SKIP_CLEAR);

    if (needs_clear)
        vkd3d_memory_transfer_queue_clear_allocation(&device->memory_transfers, allocation);

    return hr;
}

static bool vkd3d_heap_allocation_accept_deferred_resource_placements(struct d3d12_device *device,
        const D3D12_HEAP_PROPERTIES *heap_properties, D3D12_HEAP_FLAGS heap_flags)
{
    uint32_t type_mask;

    /* Normally, if a memory allocation fails, we consider it an error, but there are some exceptions
     * where we can defer memory allocation, like CreateHeap where fallback system memory type is not available.
     * In this case, we will defer memory allocation until CreatePlacedResource() time, and we should
     * accept that a memory allocation failed. */

    /* Only accept deferrals for DEFAULT / CPU_NOT_AVAILABLE heaps.
     * If we're going for host memory, we have nowhere left to fall back to either way. */
    if (is_cpu_accessible_heap(heap_properties))
        return false;

    type_mask = vkd3d_select_memory_types(device, heap_properties, heap_flags);
    return device->memory_properties.memoryHeapCount > 1 &&
            !vkd3d_memory_info_type_mask_covers_multiple_memory_heaps(&device->memory_properties, type_mask);
}

HRESULT vkd3d_allocate_heap_memory(struct d3d12_device *device, struct vkd3d_memory_allocator *allocator,
        const struct vkd3d_allocate_heap_memory_info *info, struct vkd3d_memory_allocation *allocation)
{
    struct vkd3d_allocate_heap_memory_info heap_info;
    struct vkd3d_allocate_memory_info alloc_info;
    HRESULT hr;

    memset(&alloc_info, 0, sizeof(alloc_info));
    alloc_info.memory_requirements.memoryTypeBits = ~0u;
    alloc_info.memory_requirements.alignment = info->heap_desc.Alignment;
    alloc_info.memory_requirements.size = info->heap_desc.SizeInBytes;
    alloc_info.heap_properties = info->heap_desc.Properties;
    alloc_info.heap_flags = info->heap_desc.Flags;
    alloc_info.host_ptr = info->host_ptr;

    alloc_info.flags |= info->extra_allocation_flags;
    if (!(info->heap_desc.Flags & D3D12_HEAP_FLAG_DENY_BUFFERS))
        alloc_info.flags |= VKD3D_ALLOCATION_FLAG_GLOBAL_BUFFER;

    if (is_cpu_accessible_heap(&info->heap_desc.Properties))
    {
        if (info->heap_desc.Flags & D3D12_HEAP_FLAG_DENY_BUFFERS)
        {
            /* If the heap was only designed to handle images, the heap is useless,
             * and we can force everything to go through committed path. */
            memset(allocation, 0, sizeof(*allocation));
            return S_OK;
        }
        else
        {
            /* CPU visible textures are never placed on a heap directly,
             * since LINEAR images have alignment / size requirements
             * that are vastly different from OPTIMAL ones.
             * We can place buffers however. */
            heap_info = *info;
            info = &heap_info;
            heap_info.heap_desc.Flags |= D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS;
        }
    }

    hr = vkd3d_allocate_memory(device, allocator, &alloc_info, allocation);
    if (hr == E_OUTOFMEMORY && vkd3d_heap_allocation_accept_deferred_resource_placements(device,
            &info->heap_desc.Properties, info->heap_desc.Flags))
    {
        /* It's okay and sometimes expected that we fail here.
         * Defer allocation until CreatePlacedResource(). */
        memset(allocation, 0, sizeof(*allocation));
        hr = S_OK;
    }

    if ((vkd3d_config_flags & VKD3D_CONFIG_FLAG_DEBUG_UTILS) && !allocation->chunk)
    {
        char name_buffer[1024];
        snprintf(name_buffer, sizeof(name_buffer), "Heap (cookie %"PRIu64")",
                allocation->resource.cookie);
        vkd3d_set_vk_object_name(device, (uint64_t)allocation->device_allocation.vk_memory,
                VK_OBJECT_TYPE_DEVICE_MEMORY, name_buffer);
    }

    return hr;
}

HRESULT vkd3d_allocate_internal_buffer_memory(struct d3d12_device *device, VkBuffer vk_buffer,
        VkMemoryPropertyFlags type_flags,
        struct vkd3d_device_memory_allocation *allocation)
{
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    VkMemoryRequirements memory_requirements;
    VkMemoryAllocateFlagsInfo flags_info;
    VkBindBufferMemoryInfo bind_info;
    VkResult vr;
    HRESULT hr;

    flags_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
    flags_info.pNext = NULL;
    flags_info.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;

    VK_CALL(vkGetBufferMemoryRequirements(device->vk_device, vk_buffer, &memory_requirements));

    /* Internal buffer allocations should not spuriously fail due to budget.
     * We really want them to be allocated even if we have exceeded budget. */
    if (FAILED(hr = vkd3d_allocate_device_memory(device, memory_requirements.size,
            type_flags, memory_requirements.memoryTypeBits, &flags_info, false, allocation)))
        return hr;

    bind_info.sType = VK_STRUCTURE_TYPE_BIND_BUFFER_MEMORY_INFO;
    bind_info.pNext = NULL;
    bind_info.buffer = vk_buffer;
    bind_info.memory = allocation->vk_memory;
    bind_info.memoryOffset = 0;

    if (FAILED(vr = VK_CALL(vkBindBufferMemory2(device->vk_device, 1, &bind_info))))
        return hresult_from_vk_result(vr);

    return hr;
}
