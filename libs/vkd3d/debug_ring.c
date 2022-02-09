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

#define READ_RING_WORD(off) ring->mapped_ring[(off) & ((ring->ring_size / sizeof(uint32_t)) - 1)]
#define READ_RING_WORD_ACQUIRE(off) \
    vkd3d_atomic_uint32_load_explicit(&ring->mapped_ring[(off) & ((ring->ring_size / sizeof(uint32_t)) - 1)], \
    vkd3d_memory_order_acquire)
#define DEBUG_CHANNEL_WORD_COOKIE 0xdeadca70u
#define DEBUG_CHANNEL_WORD_MASK 0xfffffff0u

static bool vkd3d_shader_debug_ring_print_message(struct vkd3d_shader_debug_ring *ring,
        uint32_t word_offset, uint32_t message_word_count)
{
    uint32_t i, debug_instance, debug_thread_id[3], fmt;
    char message_buffer[4096];
    uint64_t shader_hash;

    if (message_word_count < 8)
    {
        ERR("Message word count %u is invalid.\n", message_word_count);
        return false;
    }

    shader_hash = (uint64_t)READ_RING_WORD(word_offset + 1) | ((uint64_t)READ_RING_WORD(word_offset + 2) << 32);
    debug_instance = READ_RING_WORD(word_offset + 3);
    for (i = 0; i < 3; i++)
        debug_thread_id[i] = READ_RING_WORD(word_offset + 4 + i);
    fmt = READ_RING_WORD(word_offset + 7);

    snprintf(message_buffer, sizeof(message_buffer), "Shader: %"PRIx64": Instance %u, ID (%u, %u, %u):",
            shader_hash, debug_instance,
            debug_thread_id[0], debug_thread_id[1], debug_thread_id[2]);

    word_offset += 8;
    message_word_count -= 8;

    for (i = 0; i < message_word_count; i++)
    {
        union
        {
            float f32;
            uint32_t u32;
            int32_t i32;
        } u;
        const char *delim;
        size_t len, avail;
        u.u32 = READ_RING_WORD(word_offset + i);

        len = strlen(message_buffer);
        if (len + 1 >= sizeof(message_buffer))
            break;
        avail = sizeof(message_buffer) - len;

        delim = i == 0 ? " " : ", ";

#define VKD3D_DEBUG_CHANNEL_FMT_HEX 0u
#define VKD3D_DEBUG_CHANNEL_FMT_I32 1u
#define VKD3D_DEBUG_CHANNEL_FMT_F32 2u
        switch ((fmt >> (2u * i)) & 3u)
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
    return true;
}

void *vkd3d_shader_debug_ring_thread_main(void *arg)
{
    uint32_t last_counter, new_counter, count, i, cookie_word_count;
    volatile const uint32_t *ring_counter; /* Atomic updated by the GPU. */
    struct vkd3d_shader_debug_ring *ring;
    struct d3d12_device *device = arg;
    bool is_active = true;
    uint32_t *ring_base;
    uint32_t word_count;
    size_t ring_mask;

    ring = &device->debug_ring;
    ring_mask = (ring->ring_size / sizeof(uint32_t)) - 1;
    ring_counter = ring->mapped_control_block;
    ring_base = ring->mapped_ring;
    last_counter = 0;

    vkd3d_set_thread_name("debug-ring");

    while (is_active)
    {
        pthread_mutex_lock(&ring->ring_lock);
        if (ring->active)
            pthread_cond_wait(&ring->ring_cond, &ring->ring_lock);
        is_active = ring->active;
        pthread_mutex_unlock(&ring->ring_lock);

        new_counter = *ring_counter;

        if (last_counter != new_counter)
        {
            count = (new_counter - last_counter) & ring_mask;

            /* Assume that each iteration can safely use 1/4th of the buffer to avoid WAR hazards. */
            if (count > (ring->ring_size / 16))
            {
                ERR("Debug ring is probably too small (%u new words this iteration), increase size to avoid risk of dropping messages.\n",
                    count);
            }

            for (i = 0; i < count; )
            {
                /* The debug ring shader has "release" semantics for the word count write,
                 * so just make sure the reads don't get reordered here. */
                cookie_word_count = READ_RING_WORD_ACQUIRE(last_counter + i);
                word_count = cookie_word_count & ~DEBUG_CHANNEL_WORD_MASK;

                if (cookie_word_count == 0)
                {
                    ERR("Message was allocated, but write did not complete. last_counter = %u, rewrite new_counter = %u -> %u\n",
                            last_counter, new_counter, last_counter + i);
                    /* Rewind the counter, and try again later. */
                    new_counter = last_counter + i;
                    break;
                }

                /* If something is written here, it must be a cookie. */
                if ((cookie_word_count & DEBUG_CHANNEL_WORD_MASK) != DEBUG_CHANNEL_WORD_COOKIE)
                {
                    ERR("Invalid message work cookie detected, 0x%x.\n", cookie_word_count);
                    break;
                }

                if (i + word_count > count)
                {
                    ERR("Message word count %u is out of bounds (i = %u, count = %u).\n",
                            word_count, i, count);
                    break;
                }

                if (!vkd3d_shader_debug_ring_print_message(ring, last_counter + i, word_count))
                    break;

                i += word_count;
            }
        }

        /* Make sure to clear out any messages we read so that when the ring gets around to
         * this point again, we can detect unwritten memory.
         * This relies on having a ring that is large enough, but in practice, if we just make the ring
         * large enough, there is nothing to worry about. */
        while (last_counter != new_counter)
        {
            ring_base[last_counter & ring_mask] = 0;
            last_counter++;
        }
    }

    if (ring->device_lost)
    {
        INFO("Device lost detected, attempting to fish for clues.\n");
        new_counter = *ring_counter;
        if (last_counter != new_counter)
        {
            count = (new_counter - last_counter) & ring_mask;
            for (i = 0; i < count; )
            {
                cookie_word_count = READ_RING_WORD_ACQUIRE(last_counter + i);
                word_count = cookie_word_count & ~DEBUG_CHANNEL_WORD_MASK;

                /* This is considered a message if it has the marker and a word count that is in-range. */
                if ((cookie_word_count & DEBUG_CHANNEL_WORD_MASK) == DEBUG_CHANNEL_WORD_COOKIE &&
                        i + word_count <= count &&
                        vkd3d_shader_debug_ring_print_message(ring, last_counter + i, word_count))
                {
                    i += word_count;
                }
                else
                {
                    /* Keep going. */
                    i++;
                }
            }
        }
        INFO("Done fishing for clues ...\n");
    }

    return NULL;
}

HRESULT vkd3d_shader_debug_ring_init(struct vkd3d_shader_debug_ring *ring,
                                     struct d3d12_device *device)
{
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    D3D12_HEAP_PROPERTIES heap_properties;
    D3D12_RESOURCE_DESC1 resource_desc;
    VkMemoryPropertyFlags memory_props;
    const char *env;

    memset(ring, 0, sizeof(*ring));
    if (!(env = getenv("VKD3D_SHADER_DEBUG_RING_SIZE_LOG2")))
        return S_OK;

    ring->active = true;

    ring->ring_size = (size_t)1 << strtoul(env, NULL, 0);
    ring->control_block_size = 4096;

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
    resource_desc.Width = ring->ring_size;
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

    memory_props = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
            VK_MEMORY_PROPERTY_HOST_CACHED_BIT |
            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

    /* If we're doing breadcrumb debugging, we also need to be able to read debug ring messages
     * from a crash, so we cannot rely on being able to copy the device payload back to host.
     * Use PCI-e BAR + UNCACHED + DEVICE_COHERENT if we must. */
    if (vkd3d_config_flags & VKD3D_CONFIG_FLAG_BREADCRUMBS)
    {
        INFO("Using debug ring with breadcrumbs, opting in to device uncached payload buffer.\n");
        /* We use coherent in the debug_channel.h header, but not necessarily guaranteed to be coherent with
         * host reads, so make extra sure. */
        if (device->device_info.device_coherent_memory_features_amd.deviceCoherentMemory)
        {
            memory_props |= VK_MEMORY_PROPERTY_DEVICE_UNCACHED_BIT_AMD | VK_MEMORY_PROPERTY_DEVICE_COHERENT_BIT_AMD;
            INFO("Enabling uncached device memory for debug ring.\n");
        }
    }

    if (FAILED(vkd3d_allocate_buffer_memory(device, ring->host_buffer,
            memory_props, &ring->host_buffer_memory)))
        goto err_free_buffers;

    resource_desc.Width = ring->control_block_size;
    memset(&heap_properties, 0, sizeof(heap_properties));
    heap_properties.Type = D3D12_HEAP_TYPE_DEFAULT;

    if (FAILED(vkd3d_create_buffer(device, &heap_properties, D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS,
            &resource_desc, &ring->device_atomic_buffer)))
        goto err_free_buffers;

    memory_props = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT |
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

    if (vkd3d_config_flags & VKD3D_CONFIG_FLAG_BREADCRUMBS)
    {
        /* Expect crashes since we won't have time to flush caches.
         * We use coherent in the debug_channel.h header, but not necessarily guaranteed to be coherent with
         * host reads, so make extra sure. */
        if (device->device_info.device_coherent_memory_features_amd.deviceCoherentMemory)
            memory_props |= VK_MEMORY_PROPERTY_DEVICE_UNCACHED_BIT_AMD | VK_MEMORY_PROPERTY_DEVICE_COHERENT_BIT_AMD;
    }

    if (FAILED(vkd3d_allocate_buffer_memory(device, ring->device_atomic_buffer,
            memory_props, &ring->device_atomic_buffer_memory)))
        goto err_free_buffers;

    if (VK_CALL(vkMapMemory(device->vk_device, ring->host_buffer_memory.vk_memory,
            0, VK_WHOLE_SIZE, 0, (void**)&ring->mapped_ring)) != VK_SUCCESS)
        goto err_free_buffers;

    if (VK_CALL(vkMapMemory(device->vk_device, ring->device_atomic_buffer_memory.vk_memory,
            0, VK_WHOLE_SIZE, 0, (void**)&ring->mapped_control_block)) != VK_SUCCESS)
        goto err_free_buffers;

    ring->ring_device_address = vkd3d_get_buffer_device_address(device, ring->host_buffer);
    ring->atomic_device_address = vkd3d_get_buffer_device_address(device, ring->device_atomic_buffer);

    memset(ring->mapped_control_block, 0, ring->control_block_size);
    memset(ring->mapped_ring, 0, ring->ring_size);

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
    vkd3d_free_device_memory(device, &ring->host_buffer_memory);
    vkd3d_free_device_memory(device, &ring->device_atomic_buffer_memory);
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
    vkd3d_free_device_memory(device, &ring->host_buffer_memory);
    vkd3d_free_device_memory(device, &ring->device_atomic_buffer_memory);
}

static pthread_mutex_t debug_ring_teardown_lock = PTHREAD_MUTEX_INITIALIZER;

void vkd3d_shader_debug_ring_kick(struct vkd3d_shader_debug_ring *ring, struct d3d12_device *device, bool device_lost)
{
    if (device_lost)
    {
        /* Need a global lock here since multiple threads can observe device lost at the same time. */
        pthread_mutex_lock(&debug_ring_teardown_lock);
        {
            ring->device_lost = true;
            /* We're going to die or hang after this most likely, so make sure we get to see all messages the
             * GPU had to write. Just cleanup now. */
            vkd3d_shader_debug_ring_cleanup(ring, device);
        }
        pthread_mutex_unlock(&debug_ring_teardown_lock);
    }
    else
    {
        pthread_cond_signal(&ring->ring_cond);
    }
}
