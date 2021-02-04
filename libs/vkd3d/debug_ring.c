/*
 * Copyright 2020 Hans-Kristian Arntzen for Valve Corporation
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
#include "vkd3d_debug.h"
#include "vkd3d_common.h"
#include <stdio.h>

void vkd3d_shader_debug_ring_init_spec_constant(struct d3d12_device *device,
        struct vkd3d_shader_debug_ring_spec_info *info, vkd3d_shader_hash_t hash)
{
    info->spec_info.pData = &info->constants;
    info->spec_info.dataSize = sizeof(info->constants);
    info->spec_info.pMapEntries = info->map_entries;
    info->spec_info.mapEntryCount = 4;

    info->constants.hash = hash;
    info->constants.host_bda = device->debug_ring.ring_device_address;
    info->constants.atomic_bda = device->debug_ring.atomic_device_address;
    info->constants.ring_words = device->debug_ring.ring_size / sizeof(uint32_t);

    info->map_entries[0].constantID = 0;
    info->map_entries[0].offset = offsetof(struct vkd3d_shader_debug_ring_spec_constants, hash);
    info->map_entries[0].size = sizeof(uint64_t);

    info->map_entries[1].constantID = 1;
    info->map_entries[1].offset = offsetof(struct vkd3d_shader_debug_ring_spec_constants, atomic_bda);
    info->map_entries[1].size = sizeof(uint64_t);

    info->map_entries[2].constantID = 2;
    info->map_entries[2].offset = offsetof(struct vkd3d_shader_debug_ring_spec_constants, host_bda);
    info->map_entries[2].size = sizeof(uint64_t);

    info->map_entries[3].constantID = 3;
    info->map_entries[3].offset = offsetof(struct vkd3d_shader_debug_ring_spec_constants, ring_words);
    info->map_entries[3].size = sizeof(uint32_t);
}

void *vkd3d_shader_debug_ring_thread_main(void *arg)
{
    uint32_t last_counter, new_counter, count, i, j, message_word_count, debug_instance, debug_thread_id[3], fmt;
    struct vkd3d_shader_debug_ring *ring;
    struct d3d12_device *device = arg;
    const uint32_t *ring_counter;
    const uint32_t *ring_base;
    char message_buffer[4096];
    bool is_active = true;
    uint64_t shader_hash;
    size_t ring_mask;

    ring = &device->debug_ring;
    ring_mask = ring->ring_size - 1;
    ring_counter = ring->mapped;
    ring_base = ring_counter + (ring->ring_offset / sizeof(uint32_t));
    last_counter = 0;

    vkd3d_set_thread_name("debug-ring");

    while (is_active)
    {
        pthread_mutex_lock(&ring->ring_lock);
        pthread_cond_wait(&ring->ring_cond, &ring->ring_lock);
        is_active = ring->active;
        pthread_mutex_unlock(&ring->ring_lock);

        new_counter = *ring_counter;
        if (last_counter != new_counter)
        {
            count = (new_counter - last_counter) & ring_mask;

            /* Assume that each iteration can safely use 1/4th of the buffer to avoid WAR hazards. */
            if ((new_counter - last_counter) > (ring->ring_size / 16))
            {
                ERR("Debug ring is probably too small (%u new words this iteration), increase size to avoid risk of dropping messages.\n",
                    new_counter - last_counter);
            }

            for (i = 0; i < count; )
            {
#define READ_RING_WORD(off) ring_base[((off) + i + last_counter) & ring_mask]
                message_word_count = READ_RING_WORD(0);
                if (i + message_word_count > count)
                    break;
                if (message_word_count < 8 || message_word_count > 16 + 8)
                    break;

                shader_hash = (uint64_t)READ_RING_WORD(1) | ((uint64_t)READ_RING_WORD(2) << 32);
                debug_instance = READ_RING_WORD(3);
                for (j = 0; j < 3; j++)
                    debug_thread_id[j] = READ_RING_WORD(4 + j);
                fmt = READ_RING_WORD(7);

                snprintf(message_buffer, sizeof(message_buffer), "Shader: %"PRIx64": Instance %u, ID (%u, %u, %u):",
                         shader_hash, debug_instance,
                         debug_thread_id[0], debug_thread_id[1], debug_thread_id[2]);

                i += 8;
                message_word_count -= 8;

                for (j = 0; j < message_word_count; j++)
                {
                    union
                    {
                        float f32;
                        uint32_t u32;
                        int32_t i32;
                    } u;
                    const char *delim;
                    size_t len, avail;
                    u.u32 = READ_RING_WORD(j);

                    len = strlen(message_buffer);
                    if (len + 1 >= sizeof(message_buffer))
                        break;
                    avail = sizeof(message_buffer) - len;

                    delim = j == 0 ? " " : ", ";

#define VKD3D_DEBUG_CHANNEL_FMT_HEX 0u
#define VKD3D_DEBUG_CHANNEL_FMT_I32 1u
#define VKD3D_DEBUG_CHANNEL_FMT_F32 2u
                    switch ((fmt >> (2u * j)) & 3u)
                    {
                    case VKD3D_DEBUG_CHANNEL_FMT_HEX:
                        snprintf(message_buffer + len, avail, "%s#%x", delim, u.u32);
                        break;

                    case VKD3D_DEBUG_CHANNEL_FMT_I32:
                        snprintf(message_buffer + len, avail, "%s%d", delim, u.i32);
                        break;

                    case VKD3D_DEBUG_CHANNEL_FMT_F32:
                        snprintf(message_buffer + len, avail, "%s%f", delim, u.f32);
                        break;

                    default:
                        snprintf(message_buffer + len, avail, "%s????", delim);
                        break;
                    }
                }

                INFO("%s\n", message_buffer);

#undef READ_RING_WORD
                i += message_word_count;
            }
        }
        last_counter = new_counter;
    }

    return NULL;
}

HRESULT vkd3d_shader_debug_ring_init(struct vkd3d_shader_debug_ring *ring,
                                     struct d3d12_device *device)
{
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    D3D12_HEAP_PROPERTIES heap_properties;
    D3D12_RESOURCE_DESC resource_desc;
    const char *env;

    memset(ring, 0, sizeof(*ring));
    if (!(env = getenv("VKD3D_SHADER_DEBUG_RING_SIZE_LOG2")))
        return S_OK;

    ring->active = true;

    ring->ring_size = (size_t)1 << strtoul(env, NULL, 0);
    // Reserve 4k to be used as a control block of some sort.
    ring->ring_offset = 4096;

    WARN("Enabling shader debug ring of size: %zu.\n", ring->ring_size);

    if (!device->device_info.buffer_device_address_features.bufferDeviceAddress)
    {
        ERR("Buffer device address must be supported to use VKD3D_SHADER_DEBUG_RING feature.\n");
        return E_INVALIDARG;
    }

    memset(&heap_properties, 0, sizeof(heap_properties));
    heap_properties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_WRITE_BACK;
    heap_properties.Type = D3D12_HEAP_TYPE_CUSTOM;
    heap_properties.MemoryPoolPreference = D3D12_MEMORY_POOL_L0;

    memset(&resource_desc, 0, sizeof(resource_desc));
    resource_desc.Width = ring->ring_offset + ring->ring_size;
    resource_desc.Height = 1;
    resource_desc.DepthOrArraySize = 1;
    resource_desc.MipLevels = 1;
    resource_desc.Format = DXGI_FORMAT_UNKNOWN;
    resource_desc.SampleDesc.Count = 1;
    resource_desc.SampleDesc.Quality = 0;
    resource_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    resource_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    if (FAILED(vkd3d_create_buffer(device, &heap_properties, D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS,
                                   &resource_desc, &ring->host_buffer)))
        goto err_free_buffers;

    if (FAILED(vkd3d_allocate_buffer_memory(device, ring->host_buffer,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT,
            &ring->host_buffer_memory)))
        goto err_free_buffers;

    ring->ring_device_address = vkd3d_get_buffer_device_address(device, ring->host_buffer) + ring->ring_offset;

    resource_desc.Width = ring->ring_offset;
    memset(&heap_properties, 0, sizeof(heap_properties));
    heap_properties.Type = D3D12_HEAP_TYPE_DEFAULT;

    if (FAILED(vkd3d_create_buffer(device, &heap_properties, D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS,
                                   &resource_desc, &ring->device_atomic_buffer)))
        goto err_free_buffers;

    if (FAILED(vkd3d_allocate_buffer_memory(device, ring->device_atomic_buffer,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &ring->device_atomic_buffer_memory)))
        goto err_free_buffers;

    if (VK_CALL(vkMapMemory(device->vk_device, ring->host_buffer_memory, 0, VK_WHOLE_SIZE, 0, &ring->mapped)) != VK_SUCCESS)
        goto err_free_buffers;

    ring->atomic_device_address = vkd3d_get_buffer_device_address(device, ring->device_atomic_buffer);

    if (pthread_mutex_init(&ring->ring_lock, NULL) != 0)
        goto err_free_buffers;
    if (pthread_cond_init(&ring->ring_cond, NULL) != 0)
        goto err_destroy_mutex;

    if (pthread_create(&ring->ring_thread, NULL, vkd3d_shader_debug_ring_thread_main, device) != 0)
    {
        ERR("Failed to create ring thread.\n");
        goto err_destroy_cond;
    }

    return S_OK;

err_destroy_mutex:
    pthread_mutex_destroy(&ring->ring_lock);
err_destroy_cond:
    pthread_cond_destroy(&ring->ring_cond);
err_free_buffers:
    VK_CALL(vkDestroyBuffer(device->vk_device, ring->host_buffer, NULL));
    VK_CALL(vkDestroyBuffer(device->vk_device, ring->device_atomic_buffer, NULL));
    VK_CALL(vkFreeMemory(device->vk_device, ring->host_buffer_memory, NULL));
    VK_CALL(vkFreeMemory(device->vk_device, ring->device_atomic_buffer_memory, NULL));
    memset(ring, 0, sizeof(*ring));
    return E_OUTOFMEMORY;
}

void vkd3d_shader_debug_ring_cleanup(struct vkd3d_shader_debug_ring *ring,
                                     struct d3d12_device *device)
{
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    if (!ring->active)
        return;

    pthread_mutex_lock(&ring->ring_lock);
    ring->active = false;
    pthread_cond_signal(&ring->ring_cond);
    pthread_mutex_unlock(&ring->ring_lock);
    pthread_join(ring->ring_thread, NULL);
    pthread_mutex_destroy(&ring->ring_lock);
    pthread_cond_destroy(&ring->ring_cond);

    VK_CALL(vkDestroyBuffer(device->vk_device, ring->host_buffer, NULL));
    VK_CALL(vkDestroyBuffer(device->vk_device, ring->device_atomic_buffer, NULL));
    VK_CALL(vkFreeMemory(device->vk_device, ring->host_buffer_memory, NULL));
    VK_CALL(vkFreeMemory(device->vk_device, ring->device_atomic_buffer_memory, NULL));
}

void vkd3d_shader_debug_ring_end_command_buffer(struct d3d12_command_list *list)
{
    const struct vkd3d_vk_device_procs *vk_procs = &list->device->vk_procs;
    VkBufferCopy buffer_copy;
    VkMemoryBarrier barrier;

    if (list->device->debug_ring.active &&
        list->has_replaced_shaders &&
        (list->type == D3D12_COMMAND_LIST_TYPE_DIRECT || list->type == D3D12_COMMAND_LIST_TYPE_COMPUTE))
    {
        barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        barrier.pNext = NULL;
        barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

        VK_CALL(vkCmdPipelineBarrier(list->vk_command_buffer,
                                     VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                                     1, &barrier, 0, NULL, 0, NULL));

        buffer_copy.size = list->device->debug_ring.ring_offset;
        buffer_copy.dstOffset = 0;
        buffer_copy.srcOffset = 0;

        VK_CALL(vkCmdCopyBuffer(list->vk_command_buffer,
                                list->device->debug_ring.device_atomic_buffer,
                                list->device->debug_ring.host_buffer,
                                1, &buffer_copy));

        /* Host barrier is taken care of automatically. */
    }
}
