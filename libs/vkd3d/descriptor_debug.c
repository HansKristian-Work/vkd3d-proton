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

#include "vkd3d_descriptor_debug.h"
#include "vkd3d_threads.h"
#include <stdio.h>
#include <string.h>
#include <inttypes.h>

static pthread_once_t debug_once = PTHREAD_ONCE_INIT;
static pthread_mutex_t debug_lock = PTHREAD_MUTEX_INITIALIZER;
static bool descriptor_debug_active;
static FILE *descriptor_debug_file;

static void vkd3d_descriptor_debug_init_once(void)
{
    const char *env = getenv("VKD3D_DESCRIPTOR_QA_LOG");
    if (env)
    {
        descriptor_debug_file = fopen(env, "w");
        if (!descriptor_debug_file)
            ERR("Failed to open file: %s.\n", env);
        else
            descriptor_debug_active = true;
    }
}

void vkd3d_descriptor_debug_init(void)
{
    pthread_once(&debug_once, vkd3d_descriptor_debug_init_once);
}

bool vkd3d_descriptor_debug_active(void)
{
    return descriptor_debug_active;
}

#define DECL_BUFFER() \
    char buffer[4096]; \
    char *ptr; \
    ptr = buffer; \
    *ptr = '\0'

#define FLUSH_BUFFER() do { \
    pthread_mutex_lock(&debug_lock); \
    fprintf(descriptor_debug_file, "%s\n", buffer); \
    pthread_mutex_unlock(&debug_lock); \
    fflush(descriptor_debug_file); \
} while (0)

#define APPEND_SNPRINTF(...) do { ptr += strlen(ptr); snprintf(ptr, (buffer + ARRAY_SIZE(buffer)) - ptr, __VA_ARGS__); } while(0)

void vkd3d_descriptor_debug_register_heap(void *heap, const D3D12_DESCRIPTOR_HEAP_DESC *desc)
{
    DECL_BUFFER();
    if (!vkd3d_descriptor_debug_active())
        return;

    APPEND_SNPRINTF("REGISTER HEAP %p || COUNT = %u", heap, desc->NumDescriptors);
    if (desc->Flags & D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE)
        APPEND_SNPRINTF(" || SHADER");

    switch (desc->Type)
    {
        case D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV:
            APPEND_SNPRINTF(" || CBV_SRV_UAV");
            break;

        case D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER:
            APPEND_SNPRINTF(" || SAMPLER");
            break;

        case D3D12_DESCRIPTOR_HEAP_TYPE_RTV:
            APPEND_SNPRINTF(" || RTV");
            break;

        case D3D12_DESCRIPTOR_HEAP_TYPE_DSV:
            APPEND_SNPRINTF(" || DSV");
            break;

        default:
            APPEND_SNPRINTF(" || ?");
            break;
    }

    FLUSH_BUFFER();
}

void vkd3d_descriptor_debug_unregister_heap(void *heap)
{
    DECL_BUFFER();
    if (!vkd3d_descriptor_debug_active())
        return;

    APPEND_SNPRINTF("DESTROY HEAP %p", heap);
    FLUSH_BUFFER();
}

void vkd3d_descriptor_debug_register_resource_cookie(uint64_t cookie, const D3D12_RESOURCE_DESC *desc)
{
    const char *fmt;
    DECL_BUFFER();
    if (!vkd3d_descriptor_debug_active())
        return;

    APPEND_SNPRINTF("RESOURCE CREATE #%"PRIu64" || ", cookie);

    fmt = debug_dxgi_format(desc->Format);

    switch (desc->Dimension)
    {
        case D3D12_RESOURCE_DIMENSION_BUFFER:
            APPEND_SNPRINTF("Buffer");
            APPEND_SNPRINTF(" || Size = 0x%"PRIx64" bytes", desc->Width);
            break;

        case D3D12_RESOURCE_DIMENSION_TEXTURE1D:
            APPEND_SNPRINTF("Tex1D");
            APPEND_SNPRINTF(" || Format = %s || Levels = %u || Layers = %u || Width = %"PRIu64,
                    fmt, desc->MipLevels, desc->DepthOrArraySize, desc->Width);
            break;

        case D3D12_RESOURCE_DIMENSION_TEXTURE2D:
            APPEND_SNPRINTF("Tex2D");
            APPEND_SNPRINTF(" || Format = %s || Levels = %u || Layers = %u || Width = %"PRIu64" || Height = %u",
                    fmt, desc->MipLevels, desc->DepthOrArraySize, desc->Width, desc->Height);
            break;

        case D3D12_RESOURCE_DIMENSION_TEXTURE3D:
            APPEND_SNPRINTF("Tex3D");
            APPEND_SNPRINTF(" || Format = %s || Levels = %u || Width = %"PRIu64" || Height = %u || Depth = %u",
                    fmt, desc->MipLevels, desc->Width, desc->Height, desc->DepthOrArraySize);
            break;

        default:
            APPEND_SNPRINTF("Unknown dimension");
            break;
    }

    if (desc->Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS)
        APPEND_SNPRINTF(" || UAV");
    if (desc->Flags & D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET)
        APPEND_SNPRINTF(" || RTV");
    if (desc->Flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL)
        APPEND_SNPRINTF(" || DSV");

    FLUSH_BUFFER();
}

void vkd3d_descriptor_debug_register_allocation_cookie(uint64_t cookie, const struct vkd3d_allocate_memory_info *info)
{
    D3D12_RESOURCE_DESC desc;

    memset(&desc, 0, sizeof(desc));
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = info->memory_requirements.size;
    vkd3d_descriptor_debug_register_resource_cookie(cookie, &desc);
}

void vkd3d_descriptor_debug_register_view_cookie(uint64_t cookie, uint64_t resource_cookie)
{
    DECL_BUFFER();
    if (!vkd3d_descriptor_debug_active())
        return;
    APPEND_SNPRINTF("VIEW CREATE #%"PRIu64" <- RESOURCE #%"PRIu64, cookie, resource_cookie);
    FLUSH_BUFFER();
}

void vkd3d_descriptor_debug_unregister_cookie(uint64_t cookie)
{
    DECL_BUFFER();
    if (!vkd3d_descriptor_debug_active())
        return;
    APPEND_SNPRINTF("COOKIE DESTROY #%"PRIu64, cookie);
    FLUSH_BUFFER();
}

static const char *debug_descriptor_type(VkDescriptorType type)
{
    switch (type)
    {
        case VK_DESCRIPTOR_TYPE_SAMPLER: return "SAMPLER";
        case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE: return "SAMPLED_IMAGE";
        case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE: return "STORAGE_IMAGE";
        case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER: return "UNIFORM_BUFFER";
        case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER: return "STORAGE_BUFFER";
        case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER: return "UNIFORM_TEXEL_BUFFER";
        case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER: return "STORAGE_TEXEL_BUFFER";
        default: return "?";
    }
}

void vkd3d_descriptor_debug_write_descriptor(void *heap, uint32_t offset, VkDescriptorType type, uint64_t cookie)
{
    DECL_BUFFER();
    if (!vkd3d_descriptor_debug_active())
        return;
    APPEND_SNPRINTF("WRITE %p || OFFSET = %u || TYPE = %s || COOKIE = #%"PRIu64,
            heap, offset, debug_descriptor_type(type), cookie);
    FLUSH_BUFFER();
}

void vkd3d_descriptor_debug_copy_descriptor(void *dst_heap, uint32_t dst_offset,
                                            void *src_heap, uint32_t src_offset,
                                            uint64_t cookie)
{
    DECL_BUFFER();
    if (!vkd3d_descriptor_debug_active())
        return;
    APPEND_SNPRINTF("COPY %p || DST OFFSET = %u || COOKIE = #%"PRIu64" || SRC %p || SRC OFFSET = %u",
            dst_heap, dst_offset, cookie, src_heap, src_offset);
    FLUSH_BUFFER();
}
