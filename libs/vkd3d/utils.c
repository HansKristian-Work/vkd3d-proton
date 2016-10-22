/*
 * Copyright 2016 Józef Kucia for CodeWeavers
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include "vkd3d_private.h"

static const struct vkd3d_format vkd3d_formats[] =
{
    {DXGI_FORMAT_R32G32B32A32_FLOAT,  VK_FORMAT_R32G32B32A32_SFLOAT, 16, VK_IMAGE_ASPECT_COLOR_BIT},
    {DXGI_FORMAT_R32G32B32A32_UINT,   VK_FORMAT_R32G32B32A32_UINT,   16, VK_IMAGE_ASPECT_COLOR_BIT},
    {DXGI_FORMAT_R32G32B32A32_SINT,   VK_FORMAT_R32G32B32A32_SINT,   16, VK_IMAGE_ASPECT_COLOR_BIT},
    {DXGI_FORMAT_R32G32B32_FLOAT,     VK_FORMAT_R32G32B32_SFLOAT,    12, VK_IMAGE_ASPECT_COLOR_BIT},
    {DXGI_FORMAT_R8G8B8A8_UNORM,      VK_FORMAT_R8G8B8A8_UNORM,      4,  VK_IMAGE_ASPECT_COLOR_BIT},
    {DXGI_FORMAT_R32_FLOAT,           VK_FORMAT_R32_SFLOAT,          4,  VK_IMAGE_ASPECT_COLOR_BIT},
    {DXGI_FORMAT_B8G8R8A8_UNORM,      VK_FORMAT_B8G8R8A8_UNORM,      4,  VK_IMAGE_ASPECT_COLOR_BIT},
    {DXGI_FORMAT_B8G8R8A8_UNORM_SRGB, VK_FORMAT_B8G8R8A8_SRGB,       4,  VK_IMAGE_ASPECT_COLOR_BIT},
};

const struct vkd3d_format *vkd3d_get_format(DXGI_FORMAT dxgi_format)
{
    unsigned int i;

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

    if (!(vkd3d_format = vkd3d_get_format(format)))
        return VK_FORMAT_UNDEFINED;

    return vkd3d_format->vk_format;
}

bool vkd3d_array_reserve(void **elements, size_t *capacity, size_t element_count, size_t element_size)
{
    size_t new_capacity, max_capacity;
    void *new_elements;

    if (element_count <= *capacity)
        return true;

    max_capacity = ~(size_t)0 / element_size;
    if (max_capacity < element_count)
        return false;

    new_capacity = max(*capacity, 4);
    while (new_capacity < element_count && new_capacity <= max_capacity / 2)
        new_capacity *= 2;

    if (new_capacity < element_count)
        new_capacity = element_count;

    if (!(new_elements = vkd3d_realloc(*elements, new_capacity * element_size)))
        return false;

    *elements = new_elements;
    *capacity = new_capacity;

    return true;
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

HRESULT return_interface(IUnknown *iface, REFIID iface_riid,
        REFIID requested_riid, void **object)
{
    HRESULT hr;

    if (IsEqualGUID(iface_riid, requested_riid))
    {
        *object = iface;
        return S_OK;
    }

    hr = IUnknown_QueryInterface(iface, requested_riid, object);
    IUnknown_Release(iface);
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
            return E_FAIL;
    }
}

#define LOAD_INSTANCE_PFN(name) \
    if (!(procs->name = (void *)vkGetInstanceProcAddr(instance, #name))) \
    { \
        ERR("Could not get instance proc addr for '" #name "'.\n"); \
        return E_FAIL; \
    }

HRESULT vkd3d_load_vk_instance_procs(struct vkd3d_vk_instance_procs *procs,
        VkInstance instance)
{
    memset(procs, 0, sizeof(*procs));

#define VK_INSTANCE_PFN LOAD_INSTANCE_PFN
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

HRESULT vkd3d_load_vk_device_procs(struct vkd3d_vk_device_procs *procs,
        const struct vkd3d_vk_instance_procs *parent_procs, VkDevice device)
{
    memset(procs, 0, sizeof(*procs));

#define VK_INSTANCE_PFN COPY_PARENT_PFN
#define VK_DEVICE_PFN   LOAD_DEVICE_PFN
#include "vulkan_procs.h"

    TRACE("Loaded procs for VkDevice %p.\n", device);
    return S_OK;
}
