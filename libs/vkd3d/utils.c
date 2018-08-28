/*
 * Copyright 2016 Józef Kucia for CodeWeavers
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

#define COLOR         (VK_IMAGE_ASPECT_COLOR_BIT)
#define DEPTH         (VK_IMAGE_ASPECT_DEPTH_BIT)
#define STENCIL       (VK_IMAGE_ASPECT_STENCIL_BIT)
#define DEPTH_STENCIL (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT)
static const struct vkd3d_format vkd3d_formats[] =
{
    {DXGI_FORMAT_R32G32B32A32_TYPELESS, VK_FORMAT_R32G32B32A32_SFLOAT,      16, 1, 1,  1, COLOR},
    {DXGI_FORMAT_R32G32B32A32_FLOAT,    VK_FORMAT_R32G32B32A32_SFLOAT,      16, 1, 1,  1, COLOR},
    {DXGI_FORMAT_R32G32B32A32_UINT,     VK_FORMAT_R32G32B32A32_UINT,        16, 1, 1,  1, COLOR},
    {DXGI_FORMAT_R32G32B32A32_SINT,     VK_FORMAT_R32G32B32A32_SINT,        16, 1, 1,  1, COLOR},
    {DXGI_FORMAT_R32G32B32_FLOAT,       VK_FORMAT_R32G32B32_SFLOAT,         12, 1, 1,  1, COLOR},
    {DXGI_FORMAT_R32G32B32_UINT,        VK_FORMAT_R32G32B32_UINT,           12, 1, 1,  1, COLOR},
    {DXGI_FORMAT_R32G32B32_SINT,        VK_FORMAT_R32G32B32_SINT,           12, 1, 1,  1, COLOR},
    {DXGI_FORMAT_R16G16B16A16_TYPELESS, VK_FORMAT_R16G16B16A16_SFLOAT,      8,  1, 1,  1, COLOR},
    {DXGI_FORMAT_R16G16B16A16_FLOAT,    VK_FORMAT_R16G16B16A16_SFLOAT,      8,  1, 1,  1, COLOR},
    {DXGI_FORMAT_R16G16B16A16_UNORM,    VK_FORMAT_R16G16B16A16_UNORM,       8,  1, 1,  1, COLOR},
    {DXGI_FORMAT_R16G16B16A16_UINT,     VK_FORMAT_R16G16B16A16_UINT,        8,  1, 1,  1, COLOR},
    {DXGI_FORMAT_R16G16B16A16_SNORM,    VK_FORMAT_R16G16B16A16_SNORM,       8,  1, 1,  1, COLOR},
    {DXGI_FORMAT_R16G16B16A16_SINT,     VK_FORMAT_R16G16B16A16_SINT,        8,  1, 1,  1, COLOR},
    {DXGI_FORMAT_R32G32_TYPELESS,       VK_FORMAT_R32G32_SFLOAT,            8,  1, 1,  1, COLOR},
    {DXGI_FORMAT_R32G32_FLOAT,          VK_FORMAT_R32G32_SFLOAT,            8,  1, 1,  1, COLOR},
    {DXGI_FORMAT_R32G32_UINT,           VK_FORMAT_R32G32_UINT,              8,  1, 1,  1, COLOR},
    {DXGI_FORMAT_R32G32_SINT,           VK_FORMAT_R32G32_SINT,              8,  1, 1,  1, COLOR},
    {DXGI_FORMAT_R10G10B10A2_UNORM,     VK_FORMAT_A2B10G10R10_UNORM_PACK32, 4,  1, 1,  1, COLOR},
    {DXGI_FORMAT_R11G11B10_FLOAT,       VK_FORMAT_B10G11R11_UFLOAT_PACK32,  4,  1, 1,  1, COLOR},
    {DXGI_FORMAT_R8G8_TYPELESS,         VK_FORMAT_R8G8_UNORM,               2,  1, 1,  1, COLOR},
    {DXGI_FORMAT_R8G8_UNORM,            VK_FORMAT_R8G8_UNORM,               2,  1, 1,  1, COLOR},
    {DXGI_FORMAT_R8G8_UINT,             VK_FORMAT_R8G8_UINT,                2,  1, 1,  1, COLOR},
    {DXGI_FORMAT_R8G8_SNORM,            VK_FORMAT_R8G8_SNORM,               2,  1, 1,  1, COLOR},
    {DXGI_FORMAT_R8G8_SINT,             VK_FORMAT_R8G8_SINT,                2,  1, 1,  1, COLOR},
    {DXGI_FORMAT_R8G8B8A8_TYPELESS,     VK_FORMAT_R8G8B8A8_UNORM,           4,  1, 1,  1, COLOR},
    {DXGI_FORMAT_R8G8B8A8_UNORM,        VK_FORMAT_R8G8B8A8_UNORM,           4,  1, 1,  1, COLOR},
    {DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,   VK_FORMAT_R8G8B8A8_SRGB,            4,  1, 1,  1, COLOR},
    {DXGI_FORMAT_R8G8B8A8_UINT,         VK_FORMAT_R8G8B8A8_UINT,            4,  1, 1,  1, COLOR},
    {DXGI_FORMAT_R8G8B8A8_SNORM,        VK_FORMAT_R8G8B8A8_SNORM,           4,  1, 1,  1, COLOR},
    {DXGI_FORMAT_R8G8B8A8_SINT,         VK_FORMAT_R8G8B8A8_SINT,            4,  1, 1,  1, COLOR},
    {DXGI_FORMAT_R16G16_TYPELESS,       VK_FORMAT_R16G16_SFLOAT,            4,  1, 1,  1, COLOR},
    {DXGI_FORMAT_R16G16_FLOAT,          VK_FORMAT_R16G16_SFLOAT,            4,  1, 1,  1, COLOR},
    {DXGI_FORMAT_R16G16_UNORM,          VK_FORMAT_R16G16_UNORM,             4,  1, 1,  1, COLOR},
    {DXGI_FORMAT_R16G16_UINT,           VK_FORMAT_R16G16_UINT,              4,  1, 1,  1, COLOR},
    {DXGI_FORMAT_R16G16_SNORM,          VK_FORMAT_R16G16_SNORM,             4,  1, 1,  1, COLOR},
    {DXGI_FORMAT_R16G16_SINT,           VK_FORMAT_R16G16_SINT,              4,  1, 1,  1, COLOR},
    {DXGI_FORMAT_R32_TYPELESS,          VK_FORMAT_R32_UINT,                 4,  1, 1,  1, COLOR},
    {DXGI_FORMAT_D32_FLOAT,             VK_FORMAT_D32_SFLOAT,               4,  1, 1,  1, DEPTH},
    {DXGI_FORMAT_R32_FLOAT,             VK_FORMAT_R32_SFLOAT,               4,  1, 1,  1, COLOR},
    {DXGI_FORMAT_R32_UINT,              VK_FORMAT_R32_UINT,                 4,  1, 1,  1, COLOR},
    {DXGI_FORMAT_R32_SINT,              VK_FORMAT_R32_SINT,                 4,  1, 1,  1, COLOR},
    {DXGI_FORMAT_R16_TYPELESS,          VK_FORMAT_R16_UINT,                 2,  1, 1,  1, COLOR},
    {DXGI_FORMAT_R16_FLOAT,             VK_FORMAT_R16_SFLOAT,               2,  1, 1,  1, COLOR},
    {DXGI_FORMAT_D16_UNORM,             VK_FORMAT_D16_UNORM,                2,  1, 1,  1, DEPTH},
    {DXGI_FORMAT_R16_UNORM,             VK_FORMAT_R16_UNORM,                2,  1, 1,  1, COLOR},
    {DXGI_FORMAT_R16_UINT,              VK_FORMAT_R16_UINT,                 2,  1, 1,  1, COLOR},
    {DXGI_FORMAT_R16_SNORM,             VK_FORMAT_R16_SNORM,                2,  1, 1,  1, COLOR},
    {DXGI_FORMAT_R16_SINT,              VK_FORMAT_R16_SINT,                 2,  1, 1,  1, COLOR},
    {DXGI_FORMAT_R8_TYPELESS,           VK_FORMAT_R8_UNORM,                 1,  1, 1,  1, COLOR},
    {DXGI_FORMAT_R8_UNORM,              VK_FORMAT_R8_UNORM,                 1,  1, 1,  1, COLOR},
    {DXGI_FORMAT_R8_UINT,               VK_FORMAT_R8_UINT,                  1,  1, 1,  1, COLOR},
    {DXGI_FORMAT_R8_SNORM,              VK_FORMAT_R8_SNORM,                 1,  1, 1,  1, COLOR},
    {DXGI_FORMAT_R8_SINT,               VK_FORMAT_R8_SINT,                  1,  1, 1,  1, COLOR},
    {DXGI_FORMAT_A8_UNORM,              VK_FORMAT_R8_UNORM,                 1,  1, 1,  1, COLOR},
    {DXGI_FORMAT_B8G8R8A8_UNORM,        VK_FORMAT_B8G8R8A8_UNORM,           4,  1, 1,  1, COLOR},
    {DXGI_FORMAT_B8G8R8A8_TYPELESS,     VK_FORMAT_B8G8R8A8_UNORM,           4,  1, 1,  1, COLOR},
    {DXGI_FORMAT_B8G8R8A8_UNORM_SRGB,   VK_FORMAT_B8G8R8A8_SRGB,            4,  1, 1,  1, COLOR},
    {DXGI_FORMAT_BC1_TYPELESS,          VK_FORMAT_BC1_RGBA_UNORM_BLOCK,     1,  4, 4,  8, COLOR},
    {DXGI_FORMAT_BC1_UNORM,             VK_FORMAT_BC1_RGBA_UNORM_BLOCK,     1,  4, 4,  8, COLOR},
    {DXGI_FORMAT_BC1_UNORM_SRGB,        VK_FORMAT_BC1_RGBA_SRGB_BLOCK,      1,  4, 4,  8, COLOR},
    {DXGI_FORMAT_BC2_TYPELESS,          VK_FORMAT_BC2_UNORM_BLOCK,          1,  4, 4, 16, COLOR},
    {DXGI_FORMAT_BC2_UNORM,             VK_FORMAT_BC2_UNORM_BLOCK,          1,  4, 4, 16, COLOR},
    {DXGI_FORMAT_BC2_UNORM_SRGB,        VK_FORMAT_BC2_SRGB_BLOCK,           1,  4, 4, 16, COLOR},
    {DXGI_FORMAT_BC3_TYPELESS,          VK_FORMAT_BC3_UNORM_BLOCK,          1,  4, 4, 16, COLOR},
    {DXGI_FORMAT_BC3_UNORM,             VK_FORMAT_BC3_UNORM_BLOCK,          1,  4, 4, 16, COLOR},
    {DXGI_FORMAT_BC3_UNORM_SRGB,        VK_FORMAT_BC3_SRGB_BLOCK,           1,  4, 4, 16, COLOR},
    {DXGI_FORMAT_BC4_TYPELESS,          VK_FORMAT_BC4_UNORM_BLOCK,          1,  4, 4,  8, COLOR},
    {DXGI_FORMAT_BC4_UNORM,             VK_FORMAT_BC4_UNORM_BLOCK,          1,  4, 4,  8, COLOR},
    {DXGI_FORMAT_BC4_SNORM,             VK_FORMAT_BC4_SNORM_BLOCK,          1,  4, 4,  8, COLOR},
    {DXGI_FORMAT_BC5_TYPELESS,          VK_FORMAT_BC5_UNORM_BLOCK,          1,  4, 4, 16, COLOR},
    {DXGI_FORMAT_BC5_UNORM,             VK_FORMAT_BC5_UNORM_BLOCK,          1,  4, 4, 16, COLOR},
    {DXGI_FORMAT_BC5_SNORM,             VK_FORMAT_BC5_SNORM_BLOCK,          1,  4, 4, 16, COLOR},
    {DXGI_FORMAT_BC6H_UF16,             VK_FORMAT_BC6H_UFLOAT_BLOCK,        1,  4, 4, 16, COLOR},
    {DXGI_FORMAT_BC6H_SF16,             VK_FORMAT_BC6H_SFLOAT_BLOCK,        1,  4, 4, 16, COLOR},
    {DXGI_FORMAT_BC7_UNORM,             VK_FORMAT_BC7_UNORM_BLOCK,          1,  4, 4, 16, COLOR},
    {DXGI_FORMAT_BC7_UNORM_SRGB,        VK_FORMAT_BC7_SRGB_BLOCK,           1,  4, 4, 16, COLOR},
};

/* Each depth/stencil format is only compatible with itself in Vulkan. */
static const struct vkd3d_format vkd3d_depth_stencil_formats[] =
{
    {DXGI_FORMAT_R32G8X24_TYPELESS,        VK_FORMAT_D32_SFLOAT_S8_UINT, 8,  1, 1, 1, DEPTH_STENCIL},
    {DXGI_FORMAT_D32_FLOAT_S8X24_UINT,     VK_FORMAT_D32_SFLOAT_S8_UINT, 8,  1, 1, 1, DEPTH_STENCIL},
    {DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS, VK_FORMAT_D32_SFLOAT_S8_UINT, 8,  1, 1, 1, DEPTH},
    {DXGI_FORMAT_X32_TYPELESS_G8X24_UINT,  VK_FORMAT_D32_SFLOAT_S8_UINT, 8,  1, 1, 1, STENCIL},
    {DXGI_FORMAT_R32_TYPELESS,             VK_FORMAT_D32_SFLOAT,         4,  1, 1, 1, DEPTH},
    {DXGI_FORMAT_R32_FLOAT,                VK_FORMAT_D32_SFLOAT,         4,  1, 1, 1, DEPTH},
    {DXGI_FORMAT_R24G8_TYPELESS,           VK_FORMAT_D24_UNORM_S8_UINT,  4,  1, 1, 1, DEPTH_STENCIL},
    {DXGI_FORMAT_D24_UNORM_S8_UINT,        VK_FORMAT_D24_UNORM_S8_UINT,  4,  1, 1, 1, DEPTH_STENCIL},
    {DXGI_FORMAT_R24_UNORM_X8_TYPELESS,    VK_FORMAT_D24_UNORM_S8_UINT,  4,  1, 1, 1, DEPTH},
    {DXGI_FORMAT_X24_TYPELESS_G8_UINT,     VK_FORMAT_D24_UNORM_S8_UINT,  4,  1, 1, 1, STENCIL},
    {DXGI_FORMAT_R16_TYPELESS,             VK_FORMAT_D16_UNORM,          2,  1, 1, 1, DEPTH},
    {DXGI_FORMAT_R16_UNORM,                VK_FORMAT_D16_UNORM,          2,  1, 1, 1, DEPTH},
};
#undef COLOR
#undef DEPTH
#undef STENCIL
#undef DEPTH_STENCIL

/* We use overrides for depth/stencil formats. This is required in order to
 * properly support typeless formats because depth/stencil formats are only
 * compatible with themselves in Vulkan.
 */
static const struct vkd3d_format *vkd3d_get_depth_stencil_format(DXGI_FORMAT dxgi_format)
{
    unsigned int i;

    for (i = 0; i < ARRAY_SIZE(vkd3d_depth_stencil_formats); ++i)
    {
        if (vkd3d_depth_stencil_formats[i].dxgi_format == dxgi_format)
            return &vkd3d_depth_stencil_formats[i];
    }

    return NULL;
}

const struct vkd3d_format *vkd3d_get_format(DXGI_FORMAT dxgi_format, bool depth_stencil)
{
    const struct vkd3d_format *format;
    unsigned int i;

    if (depth_stencil && (format = vkd3d_get_depth_stencil_format(dxgi_format)))
        return format;

    for (i = 0; i < ARRAY_SIZE(vkd3d_formats); ++i)
    {
        if (vkd3d_formats[i].dxgi_format == dxgi_format)
            return &vkd3d_formats[i];
    }

    FIXME("Unhandled DXGI format %#x.\n", dxgi_format);
    return NULL;
}

VkFormat vkd3d_get_vk_format(DXGI_FORMAT format)
{
    const struct vkd3d_format *vkd3d_format;

    if (!(vkd3d_format = vkd3d_get_format(format, false)))
        return VK_FORMAT_UNDEFINED;

    return vkd3d_format->vk_format;
}

bool dxgi_format_is_typeless(DXGI_FORMAT dxgi_format)
{
    switch (dxgi_format)
    {
        case DXGI_FORMAT_R32G32B32A32_TYPELESS:
        case DXGI_FORMAT_R32G32B32_TYPELESS:
        case DXGI_FORMAT_R16G16B16A16_TYPELESS:
        case DXGI_FORMAT_R32G32_TYPELESS:
        case DXGI_FORMAT_R32G8X24_TYPELESS:
        case DXGI_FORMAT_R10G10B10A2_TYPELESS:
        case DXGI_FORMAT_R8G8B8A8_TYPELESS:
        case DXGI_FORMAT_R16G16_TYPELESS:
        case DXGI_FORMAT_R32_TYPELESS:
        case DXGI_FORMAT_R24G8_TYPELESS:
        case DXGI_FORMAT_R8G8_TYPELESS:
        case DXGI_FORMAT_R16_TYPELESS:
        case DXGI_FORMAT_R8_TYPELESS:
        case DXGI_FORMAT_BC1_TYPELESS:
        case DXGI_FORMAT_BC2_TYPELESS:
        case DXGI_FORMAT_BC3_TYPELESS:
        case DXGI_FORMAT_BC4_TYPELESS:
        case DXGI_FORMAT_BC5_TYPELESS:
        case DXGI_FORMAT_B8G8R8A8_TYPELESS:
        case DXGI_FORMAT_B8G8R8X8_TYPELESS:
        case DXGI_FORMAT_BC6H_TYPELESS:
        case DXGI_FORMAT_BC7_TYPELESS:
            return true;
        default:
            return false;
    }
}

bool is_valid_feature_level(D3D_FEATURE_LEVEL feature_level)
{
    static const D3D_FEATURE_LEVEL valid_feature_levels[] =
    {
        D3D_FEATURE_LEVEL_12_1,
        D3D_FEATURE_LEVEL_12_0,
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0,
        D3D_FEATURE_LEVEL_9_3,
        D3D_FEATURE_LEVEL_9_2,
        D3D_FEATURE_LEVEL_9_1,
    };
    unsigned int i;

    for (i = 0; i < ARRAY_SIZE(valid_feature_levels); ++i)
    {
        if (valid_feature_levels[i] == feature_level)
            return true;
    }

    return false;
}

bool check_feature_level_support(D3D_FEATURE_LEVEL feature_level)
{
    return feature_level <= D3D_FEATURE_LEVEL_11_0;
}

bool is_write_resource_state(D3D12_RESOURCE_STATES state)
{
    return state & (D3D12_RESOURCE_STATE_RENDER_TARGET
            | D3D12_RESOURCE_STATE_UNORDERED_ACCESS
            | D3D12_RESOURCE_STATE_DEPTH_WRITE
            | D3D12_RESOURCE_STATE_STREAM_OUT
            | D3D12_RESOURCE_STATE_COPY_DEST
            | D3D12_RESOURCE_STATE_RESOLVE_DEST);
}

static bool is_power_of_two(unsigned int x)
{
    return x && !(x & (x -1));
}

bool is_valid_resource_state(D3D12_RESOURCE_STATES state)
{
    const D3D12_RESOURCE_STATES valid_states =
            D3D12_RESOURCE_STATE_COMMON |
            D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER |
            D3D12_RESOURCE_STATE_INDEX_BUFFER |
            D3D12_RESOURCE_STATE_RENDER_TARGET |
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS |
            D3D12_RESOURCE_STATE_DEPTH_WRITE |
            D3D12_RESOURCE_STATE_DEPTH_READ |
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE |
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
            D3D12_RESOURCE_STATE_STREAM_OUT |
            D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT |
            D3D12_RESOURCE_STATE_COPY_DEST |
            D3D12_RESOURCE_STATE_COPY_SOURCE |
            D3D12_RESOURCE_STATE_RESOLVE_DEST |
            D3D12_RESOURCE_STATE_RESOLVE_SOURCE |
            D3D12_RESOURCE_STATE_GENERIC_READ |
            D3D12_RESOURCE_STATE_PRESENT |
            D3D12_RESOURCE_STATE_PREDICATION;

    if (state & ~valid_states)
    {
        WARN("Invalid resource states %#x.\n", state & ~valid_states);
        return false;
    }

    /* Exactly one bit must be set for write states. */
    if (is_write_resource_state(state) && !is_power_of_two(state))
    {
        WARN("Write state cannot be mixed with other states: %#x.\n", state);
        return false;
    }

    return true;
}

HRESULT return_interface(void *iface, REFIID iface_iid,
        REFIID requested_iid, void **object)
{
    IUnknown *unknown = iface;
    HRESULT hr;

    if (IsEqualGUID(iface_iid, requested_iid))
    {
        *object = unknown;
        return S_OK;
    }

    hr = IUnknown_QueryInterface(unknown, requested_iid, object);
    IUnknown_Release(unknown);
    return hr;
}

const char *debug_vk_extent_3d(VkExtent3D extent)
{
    return vkd3d_dbg_sprintf("(%u, %u, %u)",
            (unsigned int)extent.width,
            (unsigned int)extent.height,
            (unsigned int)extent.depth);
}

const char *debug_vk_queue_flags(VkQueueFlags flags)
{
    char buffer[120];

    buffer[0] = '\0';
#define FLAG_TO_STR(f) if (flags & f) { strcat(buffer, " | "#f); flags &= ~f; }
    FLAG_TO_STR(VK_QUEUE_GRAPHICS_BIT)
    FLAG_TO_STR(VK_QUEUE_COMPUTE_BIT)
    FLAG_TO_STR(VK_QUEUE_TRANSFER_BIT)
    FLAG_TO_STR(VK_QUEUE_SPARSE_BINDING_BIT)
#undef FLAG_TO_STR
    if (flags)
        FIXME("Unrecognized flag(s) %#x.\n", flags);

    if (!buffer[0])
        return "0";
    return vkd3d_dbg_sprintf("%s", &buffer[3]);
}

const char *debug_vk_memory_heap_flags(VkMemoryHeapFlags flags)
{
    char buffer[50];

    buffer[0] = '\0';
#define FLAG_TO_STR(f) if (flags & f) { strcat(buffer, " | "#f); flags &= ~f; }
    FLAG_TO_STR(VK_MEMORY_HEAP_DEVICE_LOCAL_BIT)
#undef FLAG_TO_STR
    if (flags)
        FIXME("Unrecognized flag(s) %#x.\n", flags);

    if (!buffer[0])
        return "0";
    return vkd3d_dbg_sprintf("%s", &buffer[3]);
}

const char *debug_vk_memory_property_flags(VkMemoryPropertyFlags flags)
{
    char buffer[200];

    buffer[0] = '\0';
#define FLAG_TO_STR(f) if (flags & f) { strcat(buffer, " | "#f); flags &= ~f; }
    FLAG_TO_STR(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
    FLAG_TO_STR(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)
    FLAG_TO_STR(VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
    FLAG_TO_STR(VK_MEMORY_PROPERTY_HOST_CACHED_BIT)
    FLAG_TO_STR(VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT)
#undef FLAG_TO_STR
    if (flags)
        FIXME("Unrecognized flag(s) %#x.\n", flags);

    if (!buffer[0])
        return "0";
    return vkd3d_dbg_sprintf("%s", &buffer[3]);
}

HRESULT hresult_from_vk_result(VkResult vr)
{
    switch (vr)
    {
        case VK_SUCCESS:
            return S_OK;
        case VK_ERROR_OUT_OF_HOST_MEMORY:
        case VK_ERROR_OUT_OF_DEVICE_MEMORY:
            return E_OUTOFMEMORY;
        default:
            FIXME("Unhandled VkResult %d.\n", vr);
        case VK_ERROR_DEVICE_LOST:
        case VK_ERROR_EXTENSION_NOT_PRESENT:
            return E_FAIL;
    }
}

HRESULT hresult_from_vkd3d_result(int vkd3d_result)
{
    switch (vkd3d_result)
    {
        case VKD3D_OK:
            return S_OK;
        case VKD3D_ERROR:
            return E_FAIL;
        case VKD3D_ERROR_OUT_OF_MEMORY:
            return E_OUTOFMEMORY;
        case VKD3D_ERROR_INVALID_ARGUMENT:
            return E_INVALIDARG;
        case VKD3D_ERROR_NOT_IMPLEMENTED:
            return E_NOTIMPL;
        default:
            FIXME("Unhandled vkd3d result %d.\n", vkd3d_result);
            return E_FAIL;
    }
}

#define LOAD_GLOBAL_PFN(name) \
    if (!(procs->name = (void *)vkGetInstanceProcAddr(NULL, #name))) \
    { \
        ERR("Could not get global proc addr for '" #name "'.\n"); \
        return E_FAIL; \
    }

HRESULT vkd3d_load_vk_global_procs(struct vkd3d_vk_global_procs *procs,
        PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr)
{
    memset(procs, 0, sizeof(*procs));

    procs->vkGetInstanceProcAddr = vkGetInstanceProcAddr;

    LOAD_GLOBAL_PFN(vkCreateInstance)
    LOAD_GLOBAL_PFN(vkEnumerateInstanceExtensionProperties)

    TRACE("Loaded global Vulkan procs.\n");
    return S_OK;
}

#define LOAD_INSTANCE_PFN(name) \
    if (!(procs->name = (void *)global_procs->vkGetInstanceProcAddr(instance, #name))) \
    { \
        ERR("Could not get instance proc addr for '" #name "'.\n"); \
        return E_FAIL; \
    }
#define LOAD_INSTANCE_OPTIONAL_PFN(name) \
    procs->name = (void *)global_procs->vkGetInstanceProcAddr(instance, #name);

HRESULT vkd3d_load_vk_instance_procs(struct vkd3d_vk_instance_procs *procs,
        const struct vkd3d_vk_global_procs *global_procs, VkInstance instance)
{
    memset(procs, 0, sizeof(*procs));

#define VK_INSTANCE_PFN     LOAD_INSTANCE_PFN
#define VK_INSTANCE_EXT_PFN LOAD_INSTANCE_OPTIONAL_PFN
#include "vulkan_procs.h"

    TRACE("Loaded procs for VkInstance %p.\n", instance);
    return S_OK;
}

#define COPY_PARENT_PFN(name) procs->name = parent_procs->name;
#define LOAD_DEVICE_PFN(name) \
    if (!(procs->name = (void *)procs->vkGetDeviceProcAddr(device, #name))) \
    { \
        ERR("Could not get device proc addr for '" #name "'.\n"); \
        return E_FAIL; \
    }
#define LOAD_DEVICE_OPTIONAL_PFN(name) \
    procs->name = (void *)procs->vkGetDeviceProcAddr(device, #name);

HRESULT vkd3d_load_vk_device_procs(struct vkd3d_vk_device_procs *procs,
        const struct vkd3d_vk_instance_procs *parent_procs, VkDevice device)
{
    memset(procs, 0, sizeof(*procs));

#define VK_INSTANCE_PFN   COPY_PARENT_PFN
#define VK_DEVICE_PFN     LOAD_DEVICE_PFN
#define VK_DEVICE_EXT_PFN LOAD_DEVICE_OPTIONAL_PFN
#include "vulkan_procs.h"

    TRACE("Loaded procs for VkDevice %p.\n", device);
    return S_OK;
}
