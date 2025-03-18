/*
 * Copyright 2024 Hans-Kristian Arntzen for Valve Corporation
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
#include <assert.h>
#include <stdio.h>

void vkd3d_shader_hash_range_parse(FILE *file, struct vkd3d_shader_hash_range **ranges,
        size_t *range_size, size_t *range_count, enum vkd3d_shader_hash_range_kind kind)
{
    vkd3d_shader_hash_t lo_hash;
    vkd3d_shader_hash_t hi_hash;
    size_t new_count = 0;
    char *old_end_ptr;
    char line[64];
    char *end_ptr;

    while (fgets(line, sizeof(line), file))
    {
        /* Look for either a single number, or lohash-hihash format. */
        if (!isalnum(*line))
            continue;

        lo_hash = strtoull(line, &end_ptr, 16);

        while (*end_ptr != '\0' && !isalnum(*end_ptr))
            end_ptr++;

        old_end_ptr = end_ptr;
        hi_hash = strtoull(end_ptr, &end_ptr, 16);

        /* If we didn't fully consume a hex number here, back up. */
        if (*end_ptr != '\0' && *end_ptr != '\n' && *end_ptr != ' ')
        {
            end_ptr = old_end_ptr;
            hi_hash = 0;
        }

        while (*end_ptr != '\0' && !isalpha(*end_ptr))
            end_ptr++;

        if (!hi_hash)
            hi_hash = lo_hash;

        if (lo_hash || hi_hash)
        {
            vkd3d_array_reserve((void **)ranges, range_size,
                    new_count + 1, sizeof(struct vkd3d_shader_hash_range));

            (*ranges)[new_count].lo = lo_hash;
            (*ranges)[new_count].hi = hi_hash;

            if (*end_ptr != '\0')
            {
                char *stray_newline = end_ptr + (strlen(end_ptr) - 1);
                if (*stray_newline == '\n')
                    *stray_newline = '\0';
            }

            if (kind == VKD3D_SHADER_HASH_RANGE_KIND_BARRIERS)
            {
                if (*end_ptr == '\0')
                {
                    (*ranges)[new_count].flags = VKD3D_SHADER_META_FLAG_FORCE_COMPUTE_BARRIER_AFTER_DISPATCH;
                    end_ptr = "post-compute (default)";
                }
                else if (strcmp(end_ptr, "pre-compute") == 0)
                    (*ranges)[new_count].flags = VKD3D_SHADER_META_FLAG_FORCE_COMPUTE_BARRIER_BEFORE_DISPATCH;
                else if (strcmp(end_ptr, "post-compute") == 0)
                    (*ranges)[new_count].flags = VKD3D_SHADER_META_FLAG_FORCE_COMPUTE_BARRIER_AFTER_DISPATCH;
                else if (strcmp(end_ptr, "pre-raster") == 0)
                    (*ranges)[new_count].flags = VKD3D_SHADER_META_FLAG_FORCE_PRE_RASTERIZATION_BEFORE_DISPATCH;
                else if (strcmp(end_ptr, "graphics") == 0)
                    (*ranges)[new_count].flags = VKD3D_SHADER_META_FLAG_FORCE_GRAPHICS_BEFORE_DISPATCH;
                else
                    end_ptr = "N/A";

                INFO("Inserting %s barrier for %016"PRIx64" - %016"PRIx64".\n",
                        end_ptr,
                        (*ranges)[new_count].lo,
                        (*ranges)[new_count].hi);
            }
            else if (kind == VKD3D_SHADER_HASH_RANGE_KIND_QA)
            {
                if (*end_ptr == '\0')
                {
                    (*ranges)[new_count].flags = VKD3D_SHADER_HASH_RANGE_QA_FLAG_ALLOW;
                    end_ptr = "allow (default)";
                }
                else if (strcmp(end_ptr, "allow") == 0)
                    (*ranges)[new_count].flags = VKD3D_SHADER_HASH_RANGE_QA_FLAG_ALLOW;
                else if (strcmp(end_ptr, "disallow") == 0)
                    (*ranges)[new_count].flags = VKD3D_SHADER_HASH_RANGE_QA_FLAG_DISALLOW;
                else if (strcmp(end_ptr, "full") == 0)
                    (*ranges)[new_count].flags = VKD3D_SHADER_HASH_RANGE_QA_FLAG_FULL_QA;
                else if (strcmp(end_ptr, "flush-nan") == 0)
                    (*ranges)[new_count].flags = VKD3D_SHADER_HASH_RANGE_QA_FLAG_FLUSH_NAN;
                else if (strcmp(end_ptr, "expect-assume") == 0)
                    (*ranges)[new_count].flags = VKD3D_SHADER_HASH_RANGE_QA_FLAG_EXPECT_ASSUME;
                else if (strcmp(end_ptr, "sync") == 0)
                    (*ranges)[new_count].flags = VKD3D_SHADER_HASH_RANGE_QA_FLAG_SYNC;
                else if (strcmp(end_ptr, "sync-compute") == 0)
                    (*ranges)[new_count].flags = VKD3D_SHADER_HASH_RANGE_QA_FLAG_SYNC_COMPUTE;
                else
                    end_ptr = "N/A";

                INFO("Inserting %s QA check for %016"PRIx64" - %016"PRIx64".\n",
                        end_ptr,
                        (*ranges)[new_count].lo,
                        (*ranges)[new_count].hi);
            }

            new_count++;
        }
    }

    *range_count = new_count;
}

VkMemoryPropertyFlags vkd3d_debug_buffer_memory_properties(struct d3d12_device *device,
        VkMemoryPropertyFlags flags, bool high_throughput)
{
    /* For high-throughput scenarios like sync-val, we absolutely cannot demote to host visible,
     * or we risk 0.00001 fps. */
    if ((vkd3d_config_flags & VKD3D_CONFIG_FLAG_BREADCRUMBS) && !high_throughput)
    {
        /* Expect crashes since we won't have time to flush caches.
         * We use coherent in the debug_channel.h header, but not necessarily guaranteed to be coherent with
         * host reads, so make extra sure. */
        if (device->device_info.device_coherent_memory_features_amd.deviceCoherentMemory)
        {
            flags |= VK_MEMORY_PROPERTY_DEVICE_UNCACHED_BIT_AMD | VK_MEMORY_PROPERTY_DEVICE_COHERENT_BIT_AMD;
            INFO("Enabling uncached device memory for debug buffer.\n");
        }
        else if (device->device_info.vulkan_1_2_properties.driverID == VK_DRIVER_ID_NVIDIA_PROPRIETARY)
        {
            /* Writes to sysmem seem to be coherent, but not ReBAR. Very slow, but hey,
             * we're desperate when we're doing breadcrumb + debug ring! */
            flags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                    VK_MEMORY_PROPERTY_HOST_CACHED_BIT |
                    VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
            INFO("Forcing (implicitly-coherent) sysmem for debug buffer.\n");
        }
    }

    return flags;
}
