/*
 * Copyright 2016 Józef Kucia for CodeWeavers
 * Copyright 2019 Conor McCarthy for CodeWeavers
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

#include <float.h>
#include <math.h>

#include "vkd3d_private.h"
#include "vkd3d_rw_spinlock.h"
#include "vkd3d_descriptor_debug.h"
#include "hashmap.h"

#define VKD3D_NULL_SRV_FORMAT DXGI_FORMAT_R8G8B8A8_UNORM
#define VKD3D_NULL_UAV_FORMAT DXGI_FORMAT_R32_UINT

static LONG64 global_cookie_counter;

LONG64 vkd3d_allocate_cookie()
{
    return InterlockedIncrement64(&global_cookie_counter);
}

static VkImageType vk_image_type_from_d3d12_resource_dimension(D3D12_RESOURCE_DIMENSION dimension)
{
    switch (dimension)
    {
        case D3D12_RESOURCE_DIMENSION_TEXTURE1D:
            return VK_IMAGE_TYPE_1D;
        case D3D12_RESOURCE_DIMENSION_TEXTURE2D:
            return VK_IMAGE_TYPE_2D;
        case D3D12_RESOURCE_DIMENSION_TEXTURE3D:
            return VK_IMAGE_TYPE_3D;
        default:
            ERR("Invalid resource dimension %#x.\n", dimension);
            return VK_IMAGE_TYPE_2D;
    }
}

VkSampleCountFlagBits vk_samples_from_sample_count(unsigned int sample_count)
{
    switch (sample_count)
    {
        case 1:
            return VK_SAMPLE_COUNT_1_BIT;
        case 2:
            return VK_SAMPLE_COUNT_2_BIT;
        case 4:
            return VK_SAMPLE_COUNT_4_BIT;
        case 8:
            return VK_SAMPLE_COUNT_8_BIT;
        case 16:
            return VK_SAMPLE_COUNT_16_BIT;
        case 32:
            return VK_SAMPLE_COUNT_32_BIT;
        case 64:
            return VK_SAMPLE_COUNT_64_BIT;
        default:
            return 0;
    }
}

VkSampleCountFlagBits vk_samples_from_dxgi_sample_desc(const DXGI_SAMPLE_DESC *desc)
{
    VkSampleCountFlagBits vk_samples;

    if ((vk_samples = vk_samples_from_sample_count(desc->Count)))
        return vk_samples;

    FIXME("Unhandled sample count %u.\n", desc->Count);
    return VK_SAMPLE_COUNT_1_BIT;
}

HRESULT vkd3d_create_buffer_explicit_usage(struct d3d12_device *device,
        VkBufferUsageFlags2KHR vk_usage_flags, VkDeviceSize size, const char *tag, VkBuffer *vk_buffer)
{
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    VkBufferUsageFlags2CreateInfoKHR flags2;
    VkBufferCreateInfo buffer_info;
    VkResult vr;

    buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_info.pNext = NULL;
    buffer_info.flags = 0;
    buffer_info.size = size;

    if (device->device_info.maintenance_5_features.maintenance5)
    {
        flags2.sType = VK_STRUCTURE_TYPE_BUFFER_USAGE_FLAGS_2_CREATE_INFO_KHR;
        flags2.pNext = NULL;
        flags2.usage = vk_usage_flags;
        buffer_info.usage = 0;
        vk_prepend_struct(&buffer_info, &flags2);
    }
    else
    {
        buffer_info.usage = vk_usage_flags;
    }

    if (device->concurrent_queue_family_count > 1)
    {
        buffer_info.sharingMode = VK_SHARING_MODE_CONCURRENT;
        buffer_info.queueFamilyIndexCount = device->concurrent_queue_family_count;
        buffer_info.pQueueFamilyIndices = device->concurrent_queue_family_indices;
    }
    else
    {
        buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        buffer_info.queueFamilyIndexCount = 0;
        buffer_info.pQueueFamilyIndices = NULL;
    }

    /* In case we get address binding callbacks, ensure driver knows it's not a sparse bind that happens async. */
    vkd3d_address_binding_tracker_mark_user_thread();

    if ((vr = VK_CALL(vkCreateBuffer(device->vk_device, &buffer_info, NULL, vk_buffer))) < 0)
    {
        WARN("Failed to create Vulkan buffer, vr %d.\n", vr);
        *vk_buffer = VK_NULL_HANDLE;
    }

    if (vr == VK_SUCCESS && vkd3d_address_binding_tracker_active(&device->address_binding_tracker))
    {
        union vkd3d_address_binding_report_resource_info info;
        info.buffer.tag = tag;
        vkd3d_address_binding_tracker_assign_info(&device->address_binding_tracker,
                VK_OBJECT_TYPE_BUFFER, (uint64_t)*vk_buffer, &info);
    }

    return hresult_from_vk_result(vr);
}

static inline VkDeviceSize adjust_sparse_buffer_size(VkDeviceSize size)
{
    /* If we attempt to bind sparse buffer with non-64k pages, we crash drivers.
     * Specs seems a bit unclear how non-aligned VkBuffer sizes are supposed to work,
     * so be safe. Pad out sparse buffers to their natural page size. */
    size = align64(size, VKD3D_TILE_SIZE);

    /* To avoid a situation where a tiny sparse buffer needs to use fallback VA lookups.
     * Just allocate a bit more VA space to avoid this scenario. */
    size = max(VKD3D_VA_BLOCK_SIZE, size);

    return size;
}

HRESULT vkd3d_create_buffer(struct d3d12_device *device,
        const D3D12_HEAP_PROPERTIES *heap_properties, D3D12_HEAP_FLAGS heap_flags,
        const D3D12_RESOURCE_DESC1 *desc, const char *tag, VkBuffer *vk_buffer)
{
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    VkExternalMemoryBufferCreateInfo external_info;
    const bool sparse_resource = !heap_properties;
    VkBufferCreateInfo buffer_info;
    D3D12_HEAP_TYPE heap_type;
    VkResult vr;

    heap_type = heap_properties ? heap_properties->Type : D3D12_HEAP_TYPE_DEFAULT;

    buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_info.pNext = NULL;
    buffer_info.flags = 0;
    buffer_info.size = desc->Width;

    /* This is only used by OpenExistingHeapFrom*,
     * and external host memory is the only way for us to do CROSS_ADAPTER. */
    if (desc->Flags & D3D12_RESOURCE_FLAG_ALLOW_CROSS_ADAPTER)
    {
        external_info.sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_BUFFER_CREATE_INFO;
        external_info.pNext = NULL;
        external_info.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT;
        buffer_info.pNext = &external_info;
    }

    if (sparse_resource)
    {
        buffer_info.flags |= VK_BUFFER_CREATE_SPARSE_BINDING_BIT |
                VK_BUFFER_CREATE_SPARSE_RESIDENCY_BIT |
                VK_BUFFER_CREATE_SPARSE_ALIASED_BIT;
        buffer_info.size = adjust_sparse_buffer_size(buffer_info.size);
    }

    buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT
            | VK_BUFFER_USAGE_TRANSFER_DST_BIT
            | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT
            | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
            | VK_BUFFER_USAGE_INDEX_BUFFER_BIT
            | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT
            | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;

    if (device->vk_info.EXT_conditional_rendering)
        buffer_info.usage |= VK_BUFFER_USAGE_CONDITIONAL_RENDERING_BIT_EXT;

    if (device->vk_info.EXT_transform_feedback)
    {
        buffer_info.usage |= VK_BUFFER_USAGE_TRANSFORM_FEEDBACK_BUFFER_BIT_EXT
                | VK_BUFFER_USAGE_TRANSFORM_FEEDBACK_COUNTER_BUFFER_BIT_EXT;
    }

    if (d3d12_device_supports_ray_tracing_tier_1_0(device))
    {
        /* Allows us to place GENERIC acceleration structures on top of VkBuffers.
         * This should only be allowed on non-host visible heaps. UPLOAD / READBACK is banned
         * because of resource state rules, but CUSTOM might be allowed, needs to be verified. */
        if (heap_type == D3D12_HEAP_TYPE_DEFAULT || !is_cpu_accessible_heap(heap_properties))
            buffer_info.usage |= VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR;
        /* This is always allowed. Used for vertex/index buffer inputs to RTAS build. */
        buffer_info.usage |= VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
                VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR;
    }

    if (heap_type == D3D12_HEAP_TYPE_UPLOAD)
        buffer_info.usage &= ~VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    else if (heap_type == D3D12_HEAP_TYPE_READBACK)
    {
        buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    }

    buffer_info.usage |= VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;

    if (desc->Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS)
        buffer_info.usage |= VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT;

    if (!(desc->Flags & D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE))
        buffer_info.usage |= VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT;

    /* Buffers always have properties of D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS. */
    if (desc->Flags & D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS)
    {
        WARN("D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS cannot be set for buffers.\n");
        return E_INVALIDARG;
    }

    if (device->concurrent_queue_family_count > 1)
    {
        buffer_info.sharingMode = VK_SHARING_MODE_CONCURRENT;
        buffer_info.queueFamilyIndexCount = device->concurrent_queue_family_count;
        buffer_info.pQueueFamilyIndices = device->concurrent_queue_family_indices;
    }
    else
    {
        buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        buffer_info.queueFamilyIndexCount = 0;
        buffer_info.pQueueFamilyIndices = NULL;
    }

    if (desc->Flags & (D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL))
        FIXME("Unsupported resource flags %#x.\n", desc->Flags);

    if (desc->Width > device->device_info.vulkan_1_3_properties.maxBufferSize)
    {
        FIXME("Buffer size %"PRIu64" exceeds maxBufferSize of %"PRIu64".\n",
                desc->Width, device->device_info.vulkan_1_3_properties.maxBufferSize);
    }

    /* In case we get address binding callbacks, ensure driver knows it's not a sparse bind that happens async. */
    vkd3d_address_binding_tracker_mark_user_thread();

    if ((vr = VK_CALL(vkCreateBuffer(device->vk_device, &buffer_info, NULL, vk_buffer))) < 0)
    {
        WARN("Failed to create Vulkan buffer, vr %d.\n", vr);
        *vk_buffer = VK_NULL_HANDLE;
    }

    if (vkd3d_address_binding_tracker_active(&device->address_binding_tracker))
    {
        union vkd3d_address_binding_report_resource_info info;
        info.buffer.tag = tag;
        vkd3d_address_binding_tracker_assign_info(&device->address_binding_tracker,
                VK_OBJECT_TYPE_BUFFER, (uint64_t)*vk_buffer, &info);
    }

    return hresult_from_vk_result(vr);
}

static unsigned int max_miplevel_count(const D3D12_RESOURCE_DESC1 *desc)
{
    unsigned int size = max(desc->Width, desc->Height);
    if (desc->Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D)
        size = max(size, desc->DepthOrArraySize);
    return vkd3d_log2i(size) + 1;
}

static bool vkd3d_get_castable_format_compatibility_list(const struct d3d12_device *device,
        const D3D12_RESOURCE_DESC1 *desc, UINT num_castable_formats, const DXGI_FORMAT *castable_formats,
        struct vkd3d_format_compatibility_list *list, VkImageCreateFlags *vk_flags)
{
    const struct vkd3d_format *format = vkd3d_get_format(device, desc->Format, false);
    bool base_format_is_compressed;
    unsigned int i;

    base_format_is_compressed = vkd3d_format_is_compressed(format);

    memset(list, 0, sizeof(*list));

    /* Odd-ball case which is non-sense, but allowed. Base format is TYPELESS and castable list has only FLOAT.
     * We'll end up creating { UINT, FLOAT } in this case which could screw over compression,
     * but this is bizarre enough that we should not try to work around it unless this becomes an actual problem. */
    vkd3d_format_compatibility_list_add_format(list, format->vk_format);
    if (format->type == VKD3D_FORMAT_TYPE_TYPELESS)
        WARN("Using typeless base type #%x in a resource with castable formats.\n", desc->Format);

    for (i = 0; i < num_castable_formats; i++)
    {
        format = vkd3d_get_format(device, castable_formats[i], false);
        /* We have validated this already. */
        assert(format);

        /* For purposes for format list, typeless formats are ignored since you cannot create views of them,
         * but they *do* contribute to format feature checks for some reason ... >_< */
        if (format->type == VKD3D_FORMAT_TYPE_TYPELESS)
            continue;

        vkd3d_format_compatibility_list_add_format(list, format->vk_format);
        if (base_format_is_compressed && !vkd3d_format_is_compressed(format))
            *vk_flags |= VK_IMAGE_CREATE_BLOCK_TEXEL_VIEW_COMPATIBLE_BIT;
    }

    /* See vkd3d_get_format_compatibility_list for rationale. */
    if ((desc->Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS) &&
            device->device_info.shader_image_atomic_int64_features.shaderImageInt64Atomics)
    {
        for (i = 0; i < list->format_count; i++)
        {
            if (list->vk_formats[i] == VK_FORMAT_R32G32_UINT)
            {
                vkd3d_format_compatibility_list_add_format(list, VK_FORMAT_R64_UINT);
                break;
            }
        }
    }

    if (list->format_count < 2)
        return false;

    *vk_flags |= VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT;

    /* Too many formats to expect compression, just use plain mutable. */
    if (list->format_count == ARRAY_SIZE(list->vk_formats))
        list->format_count = 0;

    return list->format_count != 0;
}

static bool vkd3d_get_format_compatibility_list(const struct d3d12_device *device,
        const D3D12_RESOURCE_DESC1 *desc, struct vkd3d_format_compatibility_list *list, VkImageCreateFlags *vk_flags)
{
    static const VkFormat r32_uav_formats[] = { VK_FORMAT_R32_UINT, VK_FORMAT_R32_SINT, VK_FORMAT_R32_SFLOAT };
    const struct vkd3d_format *format = vkd3d_get_format(device, desc->Format, false);
    unsigned int i;

    /* Planar formats cannot be used to create views directly so the logic below does
     * not apply, and passing a format list for each individual plane format is also
     * not allowed. Just be conservative here. */
    if (format->vk_aspect_mask & VK_IMAGE_ASPECT_PLANE_0_BIT)
    {
        *vk_flags |= VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT;
        return false;
    }

    memset(list, 0, sizeof(*list));

    if (desc->Format < device->format_compatibility_list_count)
        *list = device->format_compatibility_lists[desc->Format];

    if (desc->Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS)
    {
        /* Legacy D3D11 compatibility rule that allows typed UAV loads on FL11.0 hardware.
         * 5.3.9.5 from D3D11 functional spec. 32-bit typeless formats can be viewed as R32{U,I,F}.*/
        if (format->byte_count == 4 && format->type == VKD3D_FORMAT_TYPE_TYPELESS)
        {
            for (i = 0; i < ARRAY_SIZE(r32_uav_formats); i++)
                vkd3d_format_compatibility_list_add_format(list, r32_uav_formats[i]);
        }

        /* 64-bit image atomics in D3D12 are done through RG32_UINT instead.
         * We don't actually create 64-bit image views correctly at the moment,
         * but adding the alias gives a clear signal to driver that we might use atomics on the image,
         * which should disable compression or similar.
         * If we can create R32G32_UINT views on this resource, we need to add R64_UINT as well as a potential
         * mutable format. */
        if (device->device_info.shader_image_atomic_int64_features.shaderImageInt64Atomics)
        {
            for (i = 0; i < list->format_count; i++)
            {
                if (list->vk_formats[i] == VK_FORMAT_R32G32_UINT)
                {
                    vkd3d_format_compatibility_list_add_format(list, VK_FORMAT_R64_UINT);
                    break;
                }
            }
        }
    }

    if (list->format_count < 2)
        return false;

    assert(list->format_count <= ARRAY_SIZE(list->vk_formats));
    *vk_flags |= VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT;
    return true;
}

static bool d3d12_device_prefers_general_depth_stencil(const struct d3d12_device *device)
{
    if (device->device_info.vulkan_1_2_properties.driverID == VK_DRIVER_ID_NVIDIA_PROPRIETARY)
    {
        /* NVIDIA doesn't really care about layouts for the most part. */
        return true;
    }
    else if (device->device_info.vulkan_1_2_properties.driverID == VK_DRIVER_ID_MESA_TURNIP ||
             device->device_info.vulkan_1_2_properties.driverID == VK_DRIVER_ID_QUALCOMM_PROPRIETARY)
    {
        /* Adreno hardware ignores layouts */
        return true;
    }
    else if (device->device_info.vulkan_1_2_properties.driverID == VK_DRIVER_ID_MESA_RADV)
    {
        /* RADV can use TC-compat HTILE without too much issues on Polaris and later.
         * Use GENERAL for these GPUs.
         * Pre-Polaris we run into issues where even read-only depth requires decompress
         * so using GENERAL shouldn't really make things worse, it's going to run pretty bad
         * either way. */
        return true;
    }

    return false;
}

static VkImageLayout vk_common_image_layout_from_d3d12_desc(const struct d3d12_device *device,
        const D3D12_RESOURCE_DESC1 *desc)
{
    /* We need aggressive decay and promotion into anything. */
    if (desc->Flags & D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS)
        return VK_IMAGE_LAYOUT_GENERAL;
    if (desc->Layout == D3D12_TEXTURE_LAYOUT_ROW_MAJOR)
        return VK_IMAGE_LAYOUT_GENERAL;

    /* This is counter-intuitive, but using GENERAL layout for depth-stencils works around
     * having to perform DSV plane tracking all the time, since we don't necessarily know at recording time
     * if a DSV image is OPTIMAL or READ_ONLY.
     * This saves us many redundant barriers while rendering, especially since games tend
     * to split their rendering across many command lists in parallel.
     * On several implementations, GENERAL is a perfectly fine layout to use,
     * on others it is a disaster since compression is disabled :') */
    if (((desc->Flags & (D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL | D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE)) ==
            D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL) &&
            d3d12_device_prefers_general_depth_stencil(device))
    {
        return VK_IMAGE_LAYOUT_GENERAL;
    }

    /* DENY_SHADER_RESOURCE only allowed with ALLOW_DEPTH_STENCIL */
    if (desc->Flags & D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE)
        return VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    if (desc->Flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL)
        return VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

    return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
}

static bool vkd3d_sparse_image_may_have_mip_tail(const D3D12_RESOURCE_DESC1 *desc,
        const struct vkd3d_format *format, const VkSparseImageFormatProperties *sparse_info)
{
    VkExtent3D mip_extent, block_extent = sparse_info->imageGranularity;
    unsigned int mip_level;

    /* probe smallest mip level in the image */
    mip_level = desc->MipLevels - 1;
    mip_extent = d3d12_resource_desc_get_subresource_extent(desc, format, mip_level);

    if (sparse_info->flags & VK_SPARSE_IMAGE_FORMAT_ALIGNED_MIP_SIZE_BIT)
    {
        return mip_extent.width % block_extent.width ||
                mip_extent.height % block_extent.height ||
                mip_extent.depth % block_extent.depth;
    }

    return mip_extent.width < block_extent.width ||
            mip_extent.height < block_extent.height ||
            mip_extent.depth < block_extent.depth;
}

static bool vkd3d_resource_can_be_vrs(struct d3d12_device *device,
        const D3D12_HEAP_PROPERTIES *heap_properties, const D3D12_RESOURCE_DESC1 *desc)
{
    /* Docs say that RTV should not be allowed for fragment shading rate images, yet it works on native,
     * Dead Space 2023 relies on it, and D3D12 debug layers don't complain.
     * Technically, it does not seem to care about SIMULTANEOUS_ACCESS either,
     * but we only workaround when it's proven to be required.
     * It would complicate things since it affects layouts, etc. */
    return device->device_info.fragment_shading_rate_features.attachmentFragmentShadingRate &&
            desc->Format == DXGI_FORMAT_R8_UINT &&
            desc->Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D &&
            desc->MipLevels == 1 &&
            desc->SampleDesc.Count == 1 &&
            desc->SampleDesc.Quality == 0 &&
            desc->Layout == D3D12_TEXTURE_LAYOUT_UNKNOWN &&
            heap_properties &&
            !is_cpu_accessible_system_memory_heap(heap_properties) &&
            !(desc->Flags & (D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL |
                D3D12_RESOURCE_FLAG_ALLOW_CROSS_ADAPTER |
                D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS |
                D3D12_RESOURCE_FLAG_VIDEO_DECODE_REFERENCE_ONLY));
}

static HRESULT vkd3d_resource_make_vrs_view(struct d3d12_device *device,
        VkImage image, VkImageView* view)
{
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    VkImageViewCreateInfo view_info;
    VkResult vr;

    view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_info.pNext = NULL;
    view_info.flags = 0;
    view_info.image = image;
    view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_info.format = VK_FORMAT_R8_UINT;
    view_info.components = (VkComponentMapping) {
        VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY,
        VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY
    };
    view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    view_info.subresourceRange.baseMipLevel = 0;
    view_info.subresourceRange.levelCount = 1;
    view_info.subresourceRange.baseArrayLayer = 0;
    view_info.subresourceRange.layerCount = 1;

    if ((vr = VK_CALL(vkCreateImageView(device->vk_device, &view_info, NULL, view))) < 0)
        ERR("Failed to create implicit VRS view, vr %d.\n", vr);

    return hresult_from_vk_result(vr);
}

static bool vkd3d_format_allows_shader_copies(DXGI_FORMAT dxgi_format)
{
    unsigned int i;

    static const DXGI_FORMAT shader_copy_formats[] = {
        DXGI_FORMAT_D32_FLOAT,
        DXGI_FORMAT_D32_FLOAT_S8X24_UINT,
        DXGI_FORMAT_D16_UNORM,
        DXGI_FORMAT_R32_TYPELESS,
        DXGI_FORMAT_R32_FLOAT,
        DXGI_FORMAT_R32_UINT,
        DXGI_FORMAT_R32_SINT,
        DXGI_FORMAT_R16_TYPELESS,
        DXGI_FORMAT_R16_FLOAT,
        DXGI_FORMAT_R16_UINT,
        DXGI_FORMAT_R16_SINT,
        DXGI_FORMAT_R16_UNORM,
        DXGI_FORMAT_R16_SNORM,
        DXGI_FORMAT_R8_TYPELESS,
        DXGI_FORMAT_R8_UINT,
        DXGI_FORMAT_R8_SINT,
        DXGI_FORMAT_R8_UNORM,
        DXGI_FORMAT_R8_SNORM,
        DXGI_FORMAT_A8_UNORM,
    };

    for (i = 0; i < ARRAY_SIZE(shader_copy_formats); i++)
    {
        if (dxgi_format == shader_copy_formats[i])
            return true;
    }

    return false;
}

static bool vkd3d_format_needs_extended_usage(const struct vkd3d_format *format, VkImageUsageFlags usage)
{
    VkFormatFeatureFlags2 required_flags, supported_flags;

    supported_flags = format->vk_format_features;
    required_flags = 0;

    if (usage & VK_IMAGE_USAGE_SAMPLED_BIT)
        required_flags |= VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_BIT;
    if (usage & VK_IMAGE_USAGE_STORAGE_BIT)
        required_flags |= VK_FORMAT_FEATURE_2_STORAGE_IMAGE_BIT;
    if (usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)
        required_flags |= VK_FORMAT_FEATURE_2_COLOR_ATTACHMENT_BIT;
    if (usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)
        required_flags |= VK_FORMAT_FEATURE_2_DEPTH_STENCIL_ATTACHMENT_BIT;
    if (usage & VK_IMAGE_USAGE_FRAGMENT_SHADING_RATE_ATTACHMENT_BIT_KHR)
        required_flags |= VK_FORMAT_FEATURE_2_FRAGMENT_SHADING_RATE_ATTACHMENT_BIT_KHR;

    return (supported_flags & required_flags) != required_flags;
}

struct vkd3d_image_create_info
{
    struct vkd3d_format_compatibility_list format_compat_list;
    VkImageCompressionControlEXT image_compression_control;
    VkExternalMemoryImageCreateInfo external_info;
    VkImageFormatListCreateInfo format_list;
    VkImageAlignmentControlCreateInfoMESA image_alignment_control;
    VkImageCreateInfo image_info;
};

static bool d3d12_device_should_use_image_compression_control(struct d3d12_device *device)
{
    /* NV does not support this extension, but if they did, we wouldn't want to use it.
     * NV does not implement compression in a way where we would want to work around issues like this.
     * Disabling compression would likely not work around anything. */
    return device->device_info.image_compression_control_features.imageCompressionControl &&
            device->device_info.vulkan_1_2_properties.driverID != VK_DRIVER_ID_NVIDIA_PROPRIETARY;
}

static HRESULT vkd3d_get_image_create_info(struct d3d12_device *device,
        const D3D12_HEAP_PROPERTIES *heap_properties, D3D12_HEAP_FLAGS heap_flags,
        const D3D12_RESOURCE_DESC1 *desc, struct d3d12_resource *resource,
        UINT num_castable_formats, const DXGI_FORMAT *castable_formats,
        struct vkd3d_image_create_info *create_info)
{
    VkImageCompressionControlEXT *image_compression_control = &create_info->image_compression_control;
    VkImageAlignmentControlCreateInfoMESA *alignment_control = &create_info->image_alignment_control;
    struct vkd3d_format_compatibility_list *compat_list = &create_info->format_compat_list;
    VkExternalMemoryImageCreateInfo *external_info = &create_info->external_info;
    VkImageFormatListCreateInfo *format_list = &create_info->format_list;
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    VkImageCreateInfo *image_info = &create_info->image_info;
    const bool sparse_resource = !heap_properties;
    const struct vkd3d_format *format;
    bool disable_compression;
    bool use_concurrent;
    unsigned int i;

    if (!resource)
    {
        if (!(format = vkd3d_format_from_d3d12_resource_desc(device, desc, 0)))
        {
            WARN("Invalid DXGI format %#x.\n", desc->Format);
            return E_INVALIDARG;
        }
    }
    else
    {
        format = resource->format;
    }

    if (heap_properties && is_cpu_accessible_heap(heap_properties) &&
            (format->vk_aspect_mask & (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT)))
    {
        FIXME("Creating depth-stencil images in system memory is not supported.\n");
        return E_NOTIMPL;
    }

    image_info->sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image_info->pNext = NULL;
    image_info->flags = 0;

    if (resource && (resource->heap_flags & D3D12_HEAP_FLAG_SHARED))
    {
        external_info->sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO;
        external_info->pNext = NULL;
        external_info->handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT;
        vk_prepend_struct(image_info, external_info);
    }

    disable_compression = false;

    /* D3D12 rules for placed resources are such that only RTV / DSV usage requires initialization.
     * UAV is left out which means that pure UAV resources are not really possible to enable DCC on.
     * This was fixed in enhanced barriers to also require discards on UAV, but we don't consider that case yet.
     * No games ship EB, so there is no point in tuning for that yet.
     * Application bugs in this area are rampant either way. On RADV at least, compression is not enabled for pure STORAGE
     * images anyway, so this is mostly to avoid future regressions.
     * What we really "want" here is a way to say that layouts don't matter, and disabling compression
     * is the pragmatic way to work around this, but there's only so much we can do when faced with bugged apps. */
    if (!(desc->Flags & (D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL | D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET)))
        disable_compression = true;

    /* Can be a performance tweak, or app workaround.
     * On AMD native drivers, UAV + RTV usage tends to disable compression, but RADV tends to enable it.
     * This is another source of application bugs. Give us an escape hatch as needed. */
    if ((vkd3d_config_flags & VKD3D_CONFIG_FLAG_DISABLE_UAV_COMPRESSION) &&
            (desc->Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS))
        disable_compression = true;

    /* Mostly for debugging, but could be relevant for app workarounds too. */
    if ((vkd3d_config_flags & VKD3D_CONFIG_FLAG_DISABLE_DEPTH_COMPRESSION) &&
            (desc->Flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL))
        disable_compression = true;

    if ((vkd3d_config_flags & VKD3D_CONFIG_FLAG_DISABLE_COLOR_COMPRESSION) &&
            !(desc->Flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL))
        disable_compression = true;

    if (!(desc->Flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL))
    {
        if (disable_compression ||
                ((vkd3d_config_flags & VKD3D_CONFIG_FLAG_DISABLE_SIMULTANEOUS_UAV_COMPRESSION) &&
                        (desc->Flags & D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS) &&
                        (desc->Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS)))
        {
            /* SIMULTANEOUS_ACCESS heavily implies that we should disable compression.
             * There is no way to do this from within Vulkan, but best effort is full mutable format,
             * which works around issues on RDNA2 at least. But it is not enough on RDNA3 (full MUTABLE is DCC compat),
             * so image_compression_control it is.
             * This works around a Witcher 3 game bug with SSR on High where DCC metadata clears
             * races with UAV write.
             * In terms of D3D12 spec, we are not required to disable compression since there can only be
             * one queue that writes to a set of pixels.
             * SIMULTANEOUS resources are always GENERAL layout, except when we do UNDEFINED -> GENERAL for purposes
             * of initial resource access. However, these patterns imply that the entire subresource is written to,
             * so it cannot be concurrently read by other queues anyways.
             * https://learn.microsoft.com/en-us/windows/win32/api/d3d12/ne-d3d12-d3d12_resource_flags
             * For now, keep this as a specific workaround until we understand the problem scope better. */

            /* We might need BLOCK_VIEW_COMPATIBLE if application is using castable formats.
             * It might have a detrimental effect on perf, so only do it when app requests it explicitly. */
            if (num_castable_formats)
            {
                vkd3d_get_castable_format_compatibility_list(device, desc,
                        num_castable_formats, castable_formats, compat_list, &image_info->flags);
            }

            image_info->flags |= VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT;
            memset(compat_list, 0, sizeof(*compat_list));
            disable_compression = true;
        }
        else
        {
            bool requires_format_list = false;
            if (num_castable_formats)
            {
                requires_format_list = vkd3d_get_castable_format_compatibility_list(device, desc,
                        num_castable_formats, castable_formats, compat_list, &image_info->flags);
            }
            else
            {
                requires_format_list = vkd3d_get_format_compatibility_list(device, desc,
                        compat_list, &image_info->flags);
            }

            if (requires_format_list)
            {
                format_list->sType = VK_STRUCTURE_TYPE_IMAGE_FORMAT_LIST_CREATE_INFO_KHR;
                format_list->pNext = NULL;
                format_list->viewFormatCount = compat_list->format_count;
                format_list->pViewFormats = compat_list->vk_formats;
                vk_prepend_struct(image_info, format_list);
            }
        }
    }

    if (disable_compression && d3d12_device_should_use_image_compression_control(device))
    {
        image_compression_control->sType = VK_STRUCTURE_TYPE_IMAGE_COMPRESSION_CONTROL_EXT;
        image_compression_control->pNext = NULL;
        image_compression_control->flags = VK_IMAGE_COMPRESSION_DISABLED_EXT;
        image_compression_control->compressionControlPlaneCount = 0;
        image_compression_control->pFixedRateFlags = NULL;
        vk_prepend_struct(image_info, image_compression_control);
    }

    if (desc->Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D
            && desc->Width == desc->Height && desc->DepthOrArraySize >= 6
            && desc->SampleDesc.Count == 1)
        image_info->flags |= VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;

    if (sparse_resource)
    {
        image_info->flags |= VK_IMAGE_CREATE_SPARSE_BINDING_BIT |
                VK_IMAGE_CREATE_SPARSE_RESIDENCY_BIT |
                VK_IMAGE_CREATE_SPARSE_ALIASED_BIT;

        if (desc->Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE1D)
        {
            WARN("Tiled 1D textures not supported.\n");
            return E_INVALIDARG;
        }

        if (desc->Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D &&
                device->d3d12_caps.options.TiledResourcesTier < D3D12_TILED_RESOURCES_TIER_3)
        {
            WARN("Tiled 3D textures not supported by device.\n");
            return E_INVALIDARG;
        }

        if (!is_power_of_two(format->vk_aspect_mask))
        {
            WARN("Multi-planar format %u not supported for tiled resources.\n", desc->Format);
            return E_INVALIDARG;
        }
    }

    image_info->imageType = vk_image_type_from_d3d12_resource_dimension(desc->Dimension);
    image_info->format = format->vk_format;
    image_info->extent.width = desc->Width;
    image_info->extent.height = desc->Height;

    if (desc->Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D)
    {
        image_info->extent.depth = desc->DepthOrArraySize;
        image_info->arrayLayers = 1;
    }
    else
    {
        image_info->extent.depth = 1;
        image_info->arrayLayers = desc->DepthOrArraySize;
    }

    image_info->mipLevels = min(desc->MipLevels, max_miplevel_count(desc));
    image_info->samples = vk_samples_from_dxgi_sample_desc(&desc->SampleDesc);
    image_info->tiling = format->vk_image_tiling;
    image_info->initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    if (sparse_resource)
    {
        if (desc->Layout != D3D12_TEXTURE_LAYOUT_64KB_UNDEFINED_SWIZZLE)
        {
            WARN("D3D12_TEXTURE_LAYOUT_64KB_UNDEFINED_SWIZZLE must be used for reserved texture.\n");
            return E_INVALIDARG;
        }
    }
    else if (desc->Layout != D3D12_TEXTURE_LAYOUT_UNKNOWN && desc->Layout != D3D12_TEXTURE_LAYOUT_64KB_UNDEFINED_SWIZZLE)
    {
        /* ROW_MAJOR is only supported for cross-adapter sharing, which we don't support */
        FIXME("Unsupported layout %#x.\n", desc->Layout);
        return E_NOTIMPL;
    }

    image_info->usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if (desc->Flags & D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET)
        image_info->usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    if (desc->Flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL)
        image_info->usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    if (desc->Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS)
        image_info->usage |= VK_IMAGE_USAGE_STORAGE_BIT;
    /* Multisample resolve may require shader access */
    if (!(desc->Flags & D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE) || desc->SampleDesc.Count > 1)
        image_info->usage |= VK_IMAGE_USAGE_SAMPLED_BIT;

    /* Additional usage flags for shader-based copies */
    if (vkd3d_format_allows_shader_copies(format->dxgi_format))
    {
        image_info->usage |= (format->vk_aspect_mask & VK_IMAGE_ASPECT_DEPTH_BIT)
                ? VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT
                : VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    }

    if (vkd3d_resource_can_be_vrs(device, heap_properties, desc))
        image_info->usage |= VK_IMAGE_USAGE_FRAGMENT_SHADING_RATE_ATTACHMENT_BIT_KHR;

    /* Additional image flags as necessary. Do not add 2D_ARRAY_COMPATIBLE for sparse due
     * to VUID 09403. */
    if (image_info->imageType == VK_IMAGE_TYPE_3D &&
            (image_info->usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) &&
            !(image_info->flags & VK_IMAGE_CREATE_SPARSE_BINDING_BIT))
    {
        image_info->flags |= VK_IMAGE_CREATE_2D_ARRAY_COMPATIBLE_BIT;
    }

    use_concurrent = !!(device->unique_queue_mask & (device->unique_queue_mask - 1)) ||
            (heap_properties && is_cpu_accessible_heap(heap_properties));

    if (use_concurrent)
    {
        /* For multi-queue, we have to use CONCURRENT since D3D does
         * not give us enough information to do ownership transfers. */
        image_info->sharingMode = VK_SHARING_MODE_CONCURRENT;
        image_info->queueFamilyIndexCount = device->concurrent_queue_family_count;
        image_info->pQueueFamilyIndices = device->concurrent_queue_family_indices;
    }
    else
    {
        image_info->sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        image_info->queueFamilyIndexCount = 0;
        image_info->pQueueFamilyIndices = NULL;
    }

    if ((image_info->flags & VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT) &&
            vkd3d_format_needs_extended_usage(format, image_info->usage))
        image_info->flags |= VK_IMAGE_CREATE_EXTENDED_USAGE_BIT;

    /* https://learn.microsoft.com/en-us/windows/win32/direct3d12/memory-aliasing-and-data-inheritance#data-inheritance
     * For exact same texture creation, aliasing is supposed to work as expected between optimally tiled resources,
     * which maps 1:1 to VK_IMAGE_CREATE_ALIAS_BIT. */
    if (!sparse_resource && (!resource || (resource->flags & VKD3D_RESOURCE_PLACED)))
        image_info->flags |= VK_IMAGE_CREATE_ALIAS_BIT;

    if (sparse_resource)
    {
        VkSparseImageFormatProperties sparse_infos[2];
        uint32_t sparse_info_count = ARRAY_SIZE(sparse_infos);

        // D3D12 only allows sparse images with one aspect, so we can only
        // get one struct for metadata aspect and one for the data aspect
        VK_CALL(vkGetPhysicalDeviceSparseImageFormatProperties(
                device->vk_physical_device, image_info->format,
                image_info->imageType, image_info->samples, image_info->usage,
                image_info->tiling, &sparse_info_count, sparse_infos));

        if (!sparse_info_count)
        {
            ERR("Sparse images not supported with format %u, type %u, samples %u, usage %#x, tiling %u.\n",
                    image_info->format, image_info->imageType, image_info->samples, image_info->usage, image_info->tiling);
            return E_INVALIDARG;
        }

        for (i = 0; i < sparse_info_count; i++)
        {
            if (sparse_infos[i].aspectMask & VK_IMAGE_ASPECT_METADATA_BIT)
                continue;

            if (vkd3d_sparse_image_may_have_mip_tail(desc, format, &sparse_infos[i]) && desc->DepthOrArraySize > 1 && desc->MipLevels > 1)
            {
                WARN("Multiple array layers not supported for sparse images with mip tail.\n");
                return E_INVALIDARG;
            }
        }
    }

    /* Sampler feedback images are special.
     * We need at least ceil(resolution / mip_region_size) of resolution.
     * In our shader interface we also use the lower 4 bits to signal mip region size.
     * We can pad the image size just fine since we won't write out of bounds. */
    if (d3d12_resource_desc_is_sampler_feedback(desc))
    {
        image_info->flags &= ~VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
        image_info->mipLevels = 1;
        /* Force the specific usage flags we need. The runtime does not fail if we forget to add UAV usage. */
        image_info->usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT |
                VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        image_info->extent = d3d12_resource_desc_get_padded_feedback_extent(desc);
    }

    if (resource)
    {
        /* Cases where we need to force images into GENERAL layout at all times.
         * Read/WriteFromSubresource essentialy require simultaneous access. */
        if ((desc->Flags & D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS) ||
                (image_info->tiling == VK_IMAGE_TILING_LINEAR) ||
                (heap_properties && is_cpu_accessible_heap(heap_properties)))
        {
            resource->flags |= VKD3D_RESOURCE_GENERAL_LAYOUT;
            resource->common_layout = VK_IMAGE_LAYOUT_GENERAL;
        }
        else
        {
            resource->common_layout = vk_common_image_layout_from_d3d12_desc(device, desc);
        }
    }

    if (device->device_info.image_alignment_control_features.imageAlignmentControl &&
            !sparse_resource && (!resource || (resource->flags & VKD3D_RESOURCE_PLACED)))
    {
        const uint32_t supported_alignment =
                device->device_info.image_alignment_control_properties.supportedImageAlignmentMask;
        uint32_t candidate_alignment = desc->Alignment ?
                desc->Alignment : D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;

        if ((vkd3d_config_flags & VKD3D_CONFIG_FLAG_PLACED_TEXTURE_ALIASING) &&
                resource && (resource->flags & VKD3D_RESOURCE_PLACED) &&
                (candidate_alignment > D3D12_SMALL_RESOURCE_PLACEMENT_ALIGNMENT) &&
                !(desc->Flags & (D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL)) &&
                (format->block_byte_count * format->byte_count == 8u || format->block_byte_count * format->byte_count == 16u))
        {
            /* Try to use consistent alignment for BC textures and 8- or 16-byte color
             * formats so that aliased textures can interpret the data consistently.
             * This is undefined behaviour in D3D12, but works on native drivers. */
            if (supported_alignment & (2u * D3D12_SMALL_RESOURCE_PLACEMENT_ALIGNMENT - 1u))
                candidate_alignment = D3D12_SMALL_RESOURCE_PLACEMENT_ALIGNMENT;
        }

        /* Only consider alignments that are <= to the requested alignment. */
        while (candidate_alignment && !(candidate_alignment & supported_alignment))
            candidate_alignment >>= 1;

        alignment_control->sType = VK_STRUCTURE_TYPE_IMAGE_ALIGNMENT_CONTROL_CREATE_INFO_MESA;
        alignment_control->pNext = NULL;
        /* 0 is fine, it's basically same as ignored. */
        alignment_control->maximumRequestedAlignment = candidate_alignment;
        vk_prepend_struct(image_info, alignment_control);
    }

    return S_OK;
}

static HRESULT vkd3d_create_image(struct d3d12_device *device,
        const D3D12_HEAP_PROPERTIES *heap_properties, D3D12_HEAP_FLAGS heap_flags,
        const D3D12_RESOURCE_DESC1 *desc, struct d3d12_resource *resource,
        UINT num_castable_formats, const DXGI_FORMAT *castable_formats,
        VkImage *vk_image)
{
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    struct vkd3d_image_create_info create_info;
    VkResult vr;
    HRESULT hr;

    if (FAILED(hr = vkd3d_get_image_create_info(device, heap_properties,
            heap_flags, desc, resource, num_castable_formats, castable_formats, &create_info)))
        return hr;

    /* In case we get address binding callbacks, ensure driver knows it's not a sparse bind that happens async. */
    vkd3d_address_binding_tracker_mark_user_thread();

    if ((vr = VK_CALL(vkCreateImage(device->vk_device, &create_info.image_info, NULL, vk_image))) < 0)
        WARN("Failed to create Vulkan image, vr %d.\n", vr);

    if (vkd3d_address_binding_tracker_active(&device->address_binding_tracker))
    {
        union vkd3d_address_binding_report_resource_info info;
        info.image.extent = create_info.image_info.extent;
        info.image.type = create_info.image_info.imageType;
        info.image.layers = create_info.image_info.arrayLayers;
        info.image.levels = create_info.image_info.mipLevels;
        info.image.format = create_info.image_info.format;
        info.image.usage = create_info.image_info.usage;
        vkd3d_address_binding_tracker_assign_info(&device->address_binding_tracker,
                VK_OBJECT_TYPE_IMAGE, (uint64_t)*vk_image, &info);
    }

    resource->format_compatibility_list = create_info.format_compat_list;

    return hresult_from_vk_result(vr);
}

static size_t vkd3d_compute_resource_layouts_from_desc(struct d3d12_device *device,
        const D3D12_RESOURCE_DESC1 *desc, struct vkd3d_subresource_layout *layouts);

HRESULT vkd3d_get_image_allocation_info(struct d3d12_device *device,
        const D3D12_RESOURCE_DESC1 *desc,
        UINT num_castable_formats, const DXGI_FORMAT *castable_formats,
        D3D12_RESOURCE_ALLOCATION_INFO *allocation_info)
{
    static const D3D12_HEAP_PROPERTIES heap_properties = {D3D12_HEAP_TYPE_DEFAULT};
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    VkDeviceImageMemoryRequirementsKHR requirement_info;
    struct vkd3d_image_create_info create_info;
    D3D12_RESOURCE_DESC1 validated_desc;
    VkMemoryRequirements2 requirements;
    VkDeviceSize target_alignment;
    bool pad_allocation;
    HRESULT hr;

    assert(desc->Dimension != D3D12_RESOURCE_DIMENSION_BUFFER);
    assert(d3d12_resource_validate_desc(desc, num_castable_formats, castable_formats, device) == S_OK);

    if (!desc->MipLevels)
    {
        validated_desc = *desc;
        validated_desc.MipLevels = max_miplevel_count(desc);
        desc = &validated_desc;
    }

    if (FAILED(hr = vkd3d_get_image_create_info(device, &heap_properties, 0, desc, NULL,
            num_castable_formats, castable_formats,
            &create_info)))
        return hr;

    requirement_info.sType = VK_STRUCTURE_TYPE_DEVICE_IMAGE_MEMORY_REQUIREMENTS;
    requirement_info.pNext = NULL;
    requirement_info.pCreateInfo = &create_info.image_info;
    requirement_info.planeAspect = 0; /* irrelevant for us */

    requirements.sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2;
    requirements.pNext = NULL;

    VK_CALL(vkGetDeviceImageMemoryRequirements(device->vk_device, &requirement_info, &requirements));

    allocation_info->SizeInBytes = requirements.memoryRequirements.size;
    allocation_info->Alignment = requirements.memoryRequirements.alignment;

    /* If we might create an image with VRS usage, need to also check memory requirements without VRS usage.
     * VRS usage can depend on heap properties and this can affect compression, tile layouts, etc. */
    if (create_info.image_info.usage & VK_IMAGE_USAGE_FRAGMENT_SHADING_RATE_ATTACHMENT_BIT_KHR)
    {
        create_info.image_info.usage &= ~VK_IMAGE_USAGE_FRAGMENT_SHADING_RATE_ATTACHMENT_BIT_KHR;
        VK_CALL(vkGetDeviceImageMemoryRequirements(device->vk_device, &requirement_info, &requirements));
        allocation_info->SizeInBytes = max(requirements.memoryRequirements.size, allocation_info->SizeInBytes);
        allocation_info->Alignment = max(requirements.memoryRequirements.alignment, allocation_info->Alignment);
    }

    /* Do not report alignments greater than DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT
     * since that might confuse apps. Instead, pad the allocation so that we can
     * align the image ourselves. */
    target_alignment = desc->Alignment ? desc->Alignment : D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;

    pad_allocation = allocation_info->Alignment > target_alignment &&
            (allocation_info->Alignment > D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT ||
                    !(vkd3d_config_flags & VKD3D_CONFIG_FLAG_REJECT_PADDED_SMALL_RESOURCE_ALIGNMENT));

    if (pad_allocation)
    {
        WARN("Padding allocation requirements. Requested alignment %u < %u (dim %u, %u x %u x %u, %u levels, %u samples, fmt #%x, flags #%x).\n",
                (unsigned int)target_alignment, (unsigned int)allocation_info->Alignment,
                desc->Dimension,
                (unsigned int)desc->Width, desc->Height, desc->DepthOrArraySize, desc->MipLevels, desc->SampleDesc.Count,
                desc->Format, desc->Flags);
        /* On Polaris, 128k alignment can happen.
         * Also, some resources which should require 4 KiB alignment may end up requiring 64 KiB on AMD.
         * One example is mip-mapped BC textures. */
        allocation_info->SizeInBytes += allocation_info->Alignment - target_alignment;
        allocation_info->Alignment = target_alignment;
    }
    else if (allocation_info->Alignment > target_alignment)
    {
        /* It is unclear from tests and documentation if implementations are allowed to return 64k alignment here.
         * There are three possible interpretations:
         * - We are forced to support 4 KiB alignment. We must pad as necessary.
         *   This however, breaks at least one game due to game bug.
         *   It is also horrible for space efficiency since we will effectively be doubling resource sizes just to handle padding.
         * - We can return 64 KiB alignment here. This has not been observed on native implementations so far.
         * - We must fail the call. This has not been observed on native implementations so far.
         *   The official sample in https://github.com/microsoft/DirectX-Graphics-Samples/blob/master/Samples/Desktop/D3D12SmallResources/src/D3D12SmallResources.cpp#L365,
         *   suggests that one of these interpretations is possible. On error, UINT64_MAX / 64k is returned, which
         *   means we cannot determine if we must trigger error, or bump the alignment requirement,
         *   since 64k alignment can mean both things :(
         * - Failing the call is the most reasonable thing to do, but some applications may rely on it always working,
         *   so we cannot take this path by default.
         *   Additionally, on CreatePlacedResource time,
         *   we can verify that alignment requirements are met if placement offset is small. */
        FIXME_ONCE("Asking for small resource alignment, but we cannot satisfy it without padding.\n");
        return E_INVALIDARG;
    }

    return hr;
}

struct vkd3d_view_entry
{
    struct hash_map_entry entry;
    struct vkd3d_view_key key;
    struct vkd3d_view *view;
};

static bool d3d12_sampler_needs_border_color(D3D12_TEXTURE_ADDRESS_MODE u,
        D3D12_TEXTURE_ADDRESS_MODE v, D3D12_TEXTURE_ADDRESS_MODE w);

static uint32_t vkd3d_view_entry_hash(const void *key)
{
    const struct vkd3d_view_key *k = key;
    uint32_t hash;

    switch (k->view_type)
    {
        case VKD3D_VIEW_TYPE_BUFFER:
        case VKD3D_VIEW_TYPE_ACCELERATION_STRUCTURE:
            hash = hash_uint64((uint64_t)k->u.buffer.buffer);
            hash = hash_combine(hash, hash_uint64(k->u.buffer.offset));
            hash = hash_combine(hash, hash_uint64(k->u.buffer.size));
            hash = hash_combine(hash, (uintptr_t)k->u.buffer.format);
            break;

        case VKD3D_VIEW_TYPE_IMAGE:
            hash = hash_uint64((uint64_t)k->u.texture.image);
            hash = hash_combine(hash, k->u.texture.view_type);
            hash = hash_combine(hash, k->u.texture.aspect_mask);
            hash = hash_combine(hash, (uintptr_t)k->u.texture.format);
            hash = hash_combine(hash, k->u.texture.miplevel_idx);
            hash = hash_combine(hash, k->u.texture.miplevel_count);
            hash = hash_combine(hash, float_bits_to_uint32(k->u.texture.miplevel_clamp));
            hash = hash_combine(hash, k->u.texture.layer_idx);
            hash = hash_combine(hash, k->u.texture.layer_count);
            hash = hash_combine(hash, k->u.texture.w_offset);
            hash = hash_combine(hash, k->u.texture.w_size);
            hash = hash_combine(hash, k->u.texture.components.r);
            hash = hash_combine(hash, k->u.texture.components.g);
            hash = hash_combine(hash, k->u.texture.components.b);
            hash = hash_combine(hash, k->u.texture.components.a);
            hash = hash_combine(hash, k->u.texture.image_usage);
            hash = hash_combine(hash, k->u.texture.allowed_swizzle);
            break;

        case VKD3D_VIEW_TYPE_SAMPLER:
            hash = (uint32_t)k->u.sampler.Filter;
            hash = hash_combine(hash, (uint32_t)k->u.sampler.AddressU);
            hash = hash_combine(hash, (uint32_t)k->u.sampler.AddressV);
            hash = hash_combine(hash, (uint32_t)k->u.sampler.AddressW);
            hash = hash_combine(hash, float_bits_to_uint32(k->u.sampler.MipLODBias));
            hash = hash_combine(hash, (uint32_t)k->u.sampler.MaxAnisotropy);
            hash = hash_combine(hash, (uint32_t)k->u.sampler.ComparisonFunc);
            if (d3d12_sampler_needs_border_color(k->u.sampler.AddressU, k->u.sampler.AddressV, k->u.sampler.AddressW))
            {
                hash = hash_combine(hash, k->u.sampler.UintBorderColor[0]);
                hash = hash_combine(hash, k->u.sampler.UintBorderColor[1]);
                hash = hash_combine(hash, k->u.sampler.UintBorderColor[2]);
                hash = hash_combine(hash, k->u.sampler.UintBorderColor[3]);
            }
            hash = hash_combine(hash, float_bits_to_uint32(k->u.sampler.MinLOD));
            hash = hash_combine(hash, float_bits_to_uint32(k->u.sampler.MaxLOD));
            hash = hash_combine(hash, k->u.sampler.Flags);
            break;

        default:
            ERR("Unexpected view type %d.\n", k->view_type);
            return 0;
    }

    return hash;
}

static bool vkd3d_view_entry_compare(const void *key, const struct hash_map_entry *entry)
{
    const struct vkd3d_view_entry *e = (const struct vkd3d_view_entry*) entry;
    const struct vkd3d_view_key *k = key;

    if (k->view_type != e->key.view_type)
        return false;

    switch (k->view_type)
    {
        case VKD3D_VIEW_TYPE_BUFFER:
        case VKD3D_VIEW_TYPE_ACCELERATION_STRUCTURE:
            return k->u.buffer.buffer == e->key.u.buffer.buffer &&
                    k->u.buffer.format == e->key.u.buffer.format &&
                    k->u.buffer.offset == e->key.u.buffer.offset &&
                    k->u.buffer.size == e->key.u.buffer.size;

        case VKD3D_VIEW_TYPE_IMAGE:
            return k->u.texture.image == e->key.u.texture.image &&
                    k->u.texture.view_type == e->key.u.texture.view_type &&
                    k->u.texture.aspect_mask == e->key.u.texture.aspect_mask &&
                    k->u.texture.format == e->key.u.texture.format &&
                    k->u.texture.miplevel_idx == e->key.u.texture.miplevel_idx &&
                    k->u.texture.miplevel_count == e->key.u.texture.miplevel_count &&
                    k->u.texture.miplevel_clamp == e->key.u.texture.miplevel_clamp &&
                    k->u.texture.layer_idx == e->key.u.texture.layer_idx &&
                    k->u.texture.layer_count == e->key.u.texture.layer_count &&
                    k->u.texture.w_offset == e->key.u.texture.w_offset &&
                    k->u.texture.w_size == e->key.u.texture.w_size &&
                    k->u.texture.components.r == e->key.u.texture.components.r &&
                    k->u.texture.components.g == e->key.u.texture.components.g &&
                    k->u.texture.components.b == e->key.u.texture.components.b &&
                    k->u.texture.components.a == e->key.u.texture.components.a &&
                    k->u.texture.image_usage == e->key.u.texture.image_usage &&
                    k->u.texture.allowed_swizzle == e->key.u.texture.allowed_swizzle;

        case VKD3D_VIEW_TYPE_SAMPLER:
            return k->u.sampler.Filter == e->key.u.sampler.Filter &&
                    k->u.sampler.AddressU == e->key.u.sampler.AddressU &&
                    k->u.sampler.AddressV == e->key.u.sampler.AddressV &&
                    k->u.sampler.AddressW == e->key.u.sampler.AddressW &&
                    k->u.sampler.MipLODBias == e->key.u.sampler.MipLODBias &&
                    k->u.sampler.MaxAnisotropy == e->key.u.sampler.MaxAnisotropy &&
                    k->u.sampler.ComparisonFunc == e->key.u.sampler.ComparisonFunc &&
                    (!d3d12_sampler_needs_border_color(k->u.sampler.AddressU, k->u.sampler.AddressV, k->u.sampler.AddressW) ||
                            memcmp(k->u.sampler.UintBorderColor, e->key.u.sampler.UintBorderColor,
                                    sizeof(e->key.u.sampler.UintBorderColor)) == 0) &&
                    k->u.sampler.MinLOD == e->key.u.sampler.MinLOD &&
                    k->u.sampler.MaxLOD == e->key.u.sampler.MaxLOD &&
                    k->u.sampler.Flags == e->key.u.sampler.Flags;
            break;

        default:
            ERR("Unexpected view type %d.\n", k->view_type);
            return false;
    }
}

HRESULT vkd3d_view_map_init(struct vkd3d_view_map *view_map)
{
    view_map->spinlock = 0;
    hash_map_init(&view_map->map, &vkd3d_view_entry_hash, &vkd3d_view_entry_compare, sizeof(struct vkd3d_view_entry));
    return S_OK;
}

static void vkd3d_view_destroy(struct vkd3d_view *view, struct d3d12_device *device);

void vkd3d_view_map_destroy(struct vkd3d_view_map *view_map, struct d3d12_device *device)
{
    uint32_t i;

    for (i = 0; i < view_map->map.entry_count; i++)
    {
        struct vkd3d_view_entry *e = (struct vkd3d_view_entry *)hash_map_get_entry(&view_map->map, i);

        if (e->entry.flags & HASH_MAP_ENTRY_OCCUPIED)
            vkd3d_view_destroy(e->view, device);
    }

    hash_map_free(&view_map->map);
}

static struct vkd3d_view *vkd3d_view_create(enum vkd3d_view_type type);

static HRESULT d3d12_create_sampler(struct d3d12_device *device,
        const D3D12_SAMPLER_DESC2 *desc, VkSampler *vk_sampler);

static void vkd3d_view_tag_debug_name(struct vkd3d_view *view, struct d3d12_device *device)
{
    VkObjectType vk_object_type = VK_OBJECT_TYPE_MAX_ENUM;
    char name_buffer[1024];
    uint64_t vk_object = 0;
    const char *tag = "";

    if (view->type == VKD3D_VIEW_TYPE_IMAGE)
    {
        tag = "ImageView";
        vk_object = (uint64_t)view->vk_image_view;
        vk_object_type = VK_OBJECT_TYPE_IMAGE_VIEW;
    }
    else if (view->type == VKD3D_VIEW_TYPE_BUFFER)
    {
        tag = "BufferView";
        vk_object = (uint64_t)view->vk_buffer_view;
        vk_object_type = VK_OBJECT_TYPE_BUFFER_VIEW;
    }
    else if (view->type == VKD3D_VIEW_TYPE_SAMPLER)
    {
        tag = "Sampler";
        vk_object = (uint64_t)view->vk_sampler;
        vk_object_type = VK_OBJECT_TYPE_SAMPLER;
    }
    else
    {
        return;
    }

    if (vk_object)
    {
        snprintf(name_buffer, sizeof(name_buffer), "%s (cookie %"PRIu64")", tag, view->cookie);
        vkd3d_set_vk_object_name(device, vk_object, vk_object_type, name_buffer);
    }
}

struct vkd3d_view *vkd3d_view_map_create_view(struct vkd3d_view_map *view_map,
        struct d3d12_device *device, const struct vkd3d_view_key *key)
{
    struct vkd3d_view_entry entry, *e;
    struct vkd3d_view *redundant_view;
    struct vkd3d_view *view;
    bool success;

    /* In the steady state, we will be reading existing entries from a view map.
     * Prefer read-write spinlocks here to reduce contention as much as possible. */
    rw_spinlock_acquire_read(&view_map->spinlock);

    if ((e = (struct vkd3d_view_entry *)hash_map_find(&view_map->map, key)))
    {
        view = e->view;
        rw_spinlock_release_read(&view_map->spinlock);
        return view;
    }

    rw_spinlock_release_read(&view_map->spinlock);

    switch (key->view_type)
    {
        case VKD3D_VIEW_TYPE_BUFFER:
            success = vkd3d_create_buffer_view(device, &key->u.buffer, &view);
            break;

        case VKD3D_VIEW_TYPE_IMAGE:
            success = vkd3d_create_texture_view(device, &key->u.texture, &view);
            break;

        case VKD3D_VIEW_TYPE_SAMPLER:
            success = (view = vkd3d_view_create(VKD3D_VIEW_TYPE_SAMPLER)) &&
                    SUCCEEDED(d3d12_create_sampler(device, &key->u.sampler, &view->vk_sampler));
            break;

        case VKD3D_VIEW_TYPE_ACCELERATION_STRUCTURE:
            success = vkd3d_create_acceleration_structure_view(device, &key->u.buffer, &view);
            break;

        default:
            ERR("Unsupported view type %u.\n", key->view_type);
            success = false;
            break;
    }

    if (!success)
        return NULL;

    if (vkd3d_config_flags & VKD3D_CONFIG_FLAG_DEBUG_UTILS)
        vkd3d_view_tag_debug_name(view, device);

    vkd3d_descriptor_debug_register_view_cookie(device->descriptor_qa_global_info,
            view->cookie, view_map->resource_cookie);

    entry.key = *key;
    entry.view = view;

    rw_spinlock_acquire_write(&view_map->spinlock);

    if (!(e = (struct vkd3d_view_entry *)hash_map_insert(&view_map->map, key, &entry.entry)))
        ERR("Failed to insert view into hash map.\n");

    if (e->view != view)
    {
        /* We yielded on the insert because another thread came in-between, and allocated a new hash map entry.
         * This can happen between releasing reader lock, and acquiring writer lock. */
        redundant_view = view;
        view = e->view;
        rw_spinlock_release_write(&view_map->spinlock);
        vkd3d_view_decref(redundant_view, device);
    }
    else
    {
        /* If we start emitting too many typed SRVs, we will eventually crash on NV, since
         * VkBufferView objects appear to consume GPU resources. */
        if ((view_map->map.used_count % 1024) == 0)
        {
            WARN("Intense view map pressure! Got %u views in hash map %p. This may lead to out-of-memory errors in the extreme case.\n",
                    view_map->map.used_count, &view_map->map);
        }

        view = e->view;
        rw_spinlock_release_write(&view_map->spinlock);
    }

    return view;
}

struct vkd3d_sampler_key
{
    D3D12_STATIC_SAMPLER_DESC1 desc;
};

struct vkd3d_sampler_entry
{
    struct hash_map_entry entry;
    D3D12_STATIC_SAMPLER_DESC1 desc;
    VkSampler vk_sampler;
};

static uint32_t vkd3d_sampler_entry_hash(const void *key)
{
    const struct vkd3d_sampler_key *k = key;
    uint32_t hash;

    hash = (uint32_t)k->desc.Filter;
    hash = hash_combine(hash, (uint32_t)k->desc.AddressU);
    hash = hash_combine(hash, (uint32_t)k->desc.AddressV);
    hash = hash_combine(hash, (uint32_t)k->desc.AddressW);
    hash = hash_combine(hash, float_bits_to_uint32(k->desc.MipLODBias));
    hash = hash_combine(hash, k->desc.MaxAnisotropy);
    hash = hash_combine(hash, (uint32_t)k->desc.ComparisonFunc);
    hash = hash_combine(hash, (uint32_t)k->desc.BorderColor);
    hash = hash_combine(hash, float_bits_to_uint32(k->desc.MinLOD));
    hash = hash_combine(hash, float_bits_to_uint32(k->desc.MaxLOD));
    hash = hash_combine(hash, k->desc.Flags);
    return hash;
}

static bool vkd3d_sampler_entry_compare(const void *key, const struct hash_map_entry *entry)
{
    const struct vkd3d_sampler_entry *e = (const struct vkd3d_sampler_entry*) entry;
    const struct vkd3d_sampler_key *k = key;

    return k->desc.Filter == e->desc.Filter &&
            k->desc.AddressU == e->desc.AddressU &&
            k->desc.AddressV == e->desc.AddressV &&
            k->desc.AddressW == e->desc.AddressW &&
            k->desc.MipLODBias == e->desc.MipLODBias &&
            k->desc.MaxAnisotropy == e->desc.MaxAnisotropy &&
            k->desc.ComparisonFunc == e->desc.ComparisonFunc &&
            k->desc.BorderColor == e->desc.BorderColor &&
            k->desc.MinLOD == e->desc.MinLOD &&
            k->desc.MaxLOD == e->desc.MaxLOD &&
            k->desc.Flags == e->desc.Flags;
}

HRESULT vkd3d_sampler_state_init(struct vkd3d_sampler_state *state,
        struct d3d12_device *device)
{
    int rc;

    memset(state, 0, sizeof(*state));

    if ((rc = pthread_mutex_init(&state->mutex, NULL)))
        return hresult_from_errno(rc);

    hash_map_init(&state->map, &vkd3d_sampler_entry_hash, &vkd3d_sampler_entry_compare, sizeof(struct vkd3d_sampler_entry));
    return S_OK;
}

void vkd3d_sampler_state_cleanup(struct vkd3d_sampler_state *state,
        struct d3d12_device *device)
{
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    uint32_t i;

    for (i = 0; i < state->vk_descriptor_pool_count; i++)
        VK_CALL(vkDestroyDescriptorPool(device->vk_device, state->vk_descriptor_pools[i], NULL));

    vkd3d_free(state->vk_descriptor_pools);

    for (i = 0; i < state->map.entry_count; i++)
    {
        struct vkd3d_sampler_entry *e = (struct vkd3d_sampler_entry *)hash_map_get_entry(&state->map, i);

        if (e->entry.flags & HASH_MAP_ENTRY_OCCUPIED)
            VK_CALL(vkDestroySampler(device->vk_device, e->vk_sampler, NULL));
    }

    hash_map_free(&state->map);

    pthread_mutex_destroy(&state->mutex);
}

HRESULT d3d12_create_static_sampler(struct d3d12_device *device,
        const D3D12_STATIC_SAMPLER_DESC1 *desc, VkSampler *vk_sampler);

HRESULT vkd3d_sampler_state_create_static_sampler(struct vkd3d_sampler_state *state,
        struct d3d12_device *device, const D3D12_STATIC_SAMPLER_DESC1 *desc, VkSampler *vk_sampler)
{
    struct vkd3d_sampler_entry entry, *e;
    HRESULT hr;
    int rc;

    if ((rc = pthread_mutex_lock(&state->mutex)))
    {
        ERR("Failed to lock mutex, rc %d.\n", rc);
        return hresult_from_errno(rc);
    }

    if ((e = (struct vkd3d_sampler_entry*)hash_map_find(&state->map, desc)))
    {
        *vk_sampler = e->vk_sampler;
        pthread_mutex_unlock(&state->mutex);
        return S_OK;
    }

    if (FAILED(hr = d3d12_create_static_sampler(device, desc, vk_sampler)))
    {
        pthread_mutex_unlock(&state->mutex);
        return hr;
    }

    entry.desc = *desc;
    entry.vk_sampler = *vk_sampler;

    if (!hash_map_insert(&state->map, desc, &entry.entry))
        ERR("Failed to insert sampler into hash map.\n");

    pthread_mutex_unlock(&state->mutex);
    return S_OK;
}

static VkResult vkd3d_sampler_state_create_descriptor_pool(struct d3d12_device *device, VkDescriptorPool *vk_pool)
{
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    VkDescriptorPoolCreateInfo pool_info;
    VkDescriptorPoolSize pool_size;

    pool_size.type = VK_DESCRIPTOR_TYPE_SAMPLER;
    pool_size.descriptorCount = 16384;

    pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.pNext = NULL;
    pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    pool_info.maxSets = 4096;
    pool_info.poolSizeCount = 1;
    pool_info.pPoolSizes = &pool_size;

    return VK_CALL(vkCreateDescriptorPool(device->vk_device, &pool_info, NULL, vk_pool));
}

HRESULT vkd3d_sampler_state_allocate_descriptor_set(struct vkd3d_sampler_state *state,
        struct d3d12_device *device, VkDescriptorSetLayout vk_layout, VkDescriptorSet *vk_set,
        VkDescriptorPool *vk_pool)
{
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    VkResult vr = VK_ERROR_OUT_OF_POOL_MEMORY;
    VkDescriptorSetAllocateInfo alloc_info;
    size_t i;
    int rc;

    if ((rc = pthread_mutex_lock(&state->mutex)))
    {
        ERR("Failed to lock mutex, rc %d.\n", rc);
        return hresult_from_errno(rc);
    }

    alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    alloc_info.pNext = NULL;
    alloc_info.descriptorSetCount = 1;
    alloc_info.pSetLayouts = &vk_layout;

    for (i = 0; i < state->vk_descriptor_pool_count; i++)
    {
        alloc_info.descriptorPool = state->vk_descriptor_pools[i];
        vr = VK_CALL(vkAllocateDescriptorSets(device->vk_device, &alloc_info, vk_set));

        if (vr == VK_SUCCESS)
        {
            *vk_pool = alloc_info.descriptorPool;
            break;
        }
    }

    if (vr == VK_ERROR_OUT_OF_POOL_MEMORY || vr == VK_ERROR_FRAGMENTED_POOL)
    {
        vr = vkd3d_sampler_state_create_descriptor_pool(device, &alloc_info.descriptorPool);

        if (vr != VK_SUCCESS)
        {
            pthread_mutex_unlock(&state->mutex);
            return hresult_from_vk_result(vr);
        }

        if (!vkd3d_array_reserve((void **)&state->vk_descriptor_pools, &state->vk_descriptor_pools_size,
                state->vk_descriptor_pool_count + 1, sizeof(*state->vk_descriptor_pools)))
        {
            VK_CALL(vkDestroyDescriptorPool(device->vk_device, alloc_info.descriptorPool, NULL));
            pthread_mutex_unlock(&state->mutex);
            return E_OUTOFMEMORY;
        }

        state->vk_descriptor_pools[state->vk_descriptor_pool_count++] = alloc_info.descriptorPool;
        vr = VK_CALL(vkAllocateDescriptorSets(device->vk_device, &alloc_info, vk_set));
        *vk_pool = alloc_info.descriptorPool;
    }

    pthread_mutex_unlock(&state->mutex);
    return hresult_from_vk_result(vr);
}

void vkd3d_sampler_state_free_descriptor_set(struct vkd3d_sampler_state *state,
        struct d3d12_device *device, VkDescriptorSet vk_set, VkDescriptorPool vk_pool)
{
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    int rc;

    if ((rc = pthread_mutex_lock(&state->mutex)))
        ERR("Failed to lock mutex, rc %d.\n", rc);

    if (vk_pool && vk_set)
        VK_CALL(vkFreeDescriptorSets(device->vk_device, vk_pool, 1, &vk_set));
    pthread_mutex_unlock(&state->mutex);
}

static void d3d12_resource_get_tiling(struct d3d12_device *device, struct d3d12_resource *resource,
        UINT *total_tile_count, D3D12_PACKED_MIP_INFO *packed_mip_info, D3D12_TILE_SHAPE *tile_shape,
        D3D12_SUBRESOURCE_TILING *tilings, VkSparseImageMemoryRequirements *vk_info)
{
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    VkSparseImageMemoryRequirements *memory_requirements = NULL;
    unsigned int i, tile_count, packed_tiles, standard_mips;
    const D3D12_RESOURCE_DESC1 *desc = &resource->desc;
    uint32_t memory_requirement_count = 0;
    const struct vkd3d_format *format;
    VkExtent3D block_extent;

    memset(vk_info, 0, sizeof(*vk_info));

    format = vkd3d_get_format(device, resource->desc.Format,
            !!(resource->desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL));

    if (desc->Dimension == D3D12_RESOURCE_DIMENSION_BUFFER)
    {
        tile_count = align(desc->Width, VKD3D_TILE_SIZE) / VKD3D_TILE_SIZE;

        packed_mip_info->NumStandardMips = 0;
        packed_mip_info->NumPackedMips = 0;
        packed_mip_info->NumTilesForPackedMips = 0;
        packed_mip_info->StartTileIndexInOverallResource = 0;

        tile_shape->WidthInTexels = VKD3D_TILE_SIZE;
        tile_shape->HeightInTexels = 1;
        tile_shape->DepthInTexels = 1;

        tilings[0].WidthInTiles = tile_count;
        tilings[0].HeightInTiles = 1;
        tilings[0].DepthInTiles = 1;
        tilings[0].StartTileIndexInOverallResource = 0;

        *total_tile_count = tile_count;
    }
    else if (resource->flags & VKD3D_RESOURCE_RESERVED)
    {
        VK_CALL(vkGetImageSparseMemoryRequirements(device->vk_device,
                resource->res.vk_image, &memory_requirement_count, NULL));

        if (!memory_requirement_count)
        {
            ERR("Failed to query sparse memory requirements.\n");
            return;
        }

        memory_requirements = vkd3d_malloc(memory_requirement_count * sizeof(*memory_requirements));

        VK_CALL(vkGetImageSparseMemoryRequirements(device->vk_device,
                resource->res.vk_image, &memory_requirement_count, memory_requirements));

        for (i = 0; i < memory_requirement_count; i++)
        {
            if (!(memory_requirements[i].formatProperties.aspectMask & VK_IMAGE_ASPECT_METADATA_BIT))
                *vk_info = memory_requirements[i];
        }

        vkd3d_free(memory_requirements);

        /* Assume that there is no mip tail if either the size is zero or
         * if the first LOD is out of range. It's not clear what drivers
         * are supposed to report here if the image has no mip tail. */
        standard_mips = vk_info->imageMipTailSize
                ? min(desc->MipLevels, vk_info->imageMipTailFirstLod)
                : desc->MipLevels;

        packed_tiles = standard_mips < desc->MipLevels
                ? align(vk_info->imageMipTailSize, VKD3D_TILE_SIZE) / VKD3D_TILE_SIZE
                : 0;

        if (!(vk_info->formatProperties.flags & VK_SPARSE_IMAGE_FORMAT_SINGLE_MIPTAIL_BIT))
            packed_tiles *= d3d12_resource_desc_get_layer_count(desc);

        block_extent = vk_info->formatProperties.imageGranularity;
        tile_count = 0;

        for (i = 0; i < d3d12_resource_desc_get_sub_resource_count_per_plane(desc); i++)
        {
            unsigned int mip_level = i % desc->MipLevels;
            VkExtent3D mip_extent = d3d12_resource_desc_get_subresource_extent(desc, format, mip_level);
            unsigned int tile_count_w = align(mip_extent.width, block_extent.width) / block_extent.width;
            unsigned int tile_count_h = align(mip_extent.height, block_extent.height) / block_extent.height;
            unsigned int tile_count_d = align(mip_extent.depth, block_extent.depth) / block_extent.depth;

            if (mip_level < standard_mips)
            {
                tilings[i].WidthInTiles = tile_count_w;
                tilings[i].HeightInTiles = tile_count_h;
                tilings[i].DepthInTiles = tile_count_d;
                tilings[i].StartTileIndexInOverallResource = tile_count;
                tile_count += tile_count_w * tile_count_h * tile_count_d;
            }
            else
            {
                tilings[i].WidthInTiles = 0;
                tilings[i].HeightInTiles = 0;
                tilings[i].DepthInTiles = 0;
                tilings[i].StartTileIndexInOverallResource = ~0u;
            }
        }

        packed_mip_info->NumStandardMips = standard_mips;
        packed_mip_info->NumTilesForPackedMips = packed_tiles;
        packed_mip_info->NumPackedMips = desc->MipLevels - standard_mips;
        packed_mip_info->StartTileIndexInOverallResource = packed_tiles ? tile_count : 0;

        tile_count += packed_tiles;

        /* Docs say that we should clear tile_shape to zero if there are no standard mips,
         * but this conflicts with all native drivers, so the docs are likely lying here.
         * See test_get_resource_tiling() for info. */
        tile_shape->WidthInTexels = block_extent.width;
        tile_shape->HeightInTexels = block_extent.height;
        tile_shape->DepthInTexels = block_extent.depth;

        *total_tile_count = tile_count;
    }
    else
    {
        /* 33.4.3 in Vulkan spec, assuming D3D12 is same. */
        static const struct standard_sizes
        {
            unsigned int byte_count;
            unsigned int samples;
            unsigned int width, height;
        } size_table[] = {
            { 1, 1, 256, 256 },
            { 1, 2, 128, 128 },
            { 1, 4, 128, 256 },
            { 1, 8, 64, 128 },
            { 1, 16, 64, 64 },

            { 2, 1, 256, 128 },
            { 2, 2, 128, 128 },
            { 2, 4, 128, 64 },
            { 2, 8, 64, 64 },
            { 2, 16, 64, 32 },

            { 4, 1, 128, 128 },
            { 4, 2, 64, 128 },
            { 4, 4, 64, 64 },
            { 4, 8, 32, 64 },
            { 4, 16, 32, 32 },

            { 8, 1, 128, 64 },
            { 8, 2, 64, 64 },
            { 8, 4, 64, 32 },
            { 8, 8, 32, 32 },
            { 8, 16, 32, 16 },

            { 16, 1, 64, 64 },
            { 16, 2, 32, 64 },
            { 16, 4, 32, 32 },
            { 16, 8, 16, 32 },
            { 16, 16, 16, 16 },
        };

        const struct standard_sizes *sizes = NULL;

        for (i = 0; i < ARRAY_SIZE(size_table) && !sizes; i++)
            if (size_table[i].byte_count == format->byte_count && desc->SampleDesc.Count == size_table[i].samples)
                sizes = &size_table[i];

        /* Just have to hallucinate something reasonable. Pretend every LOD is standard layout.
         * We only attempt this for 2D images, ignore 3D cases. */
        assert(desc->Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D);
        packed_mip_info->NumStandardMips = desc->MipLevels;
        packed_mip_info->NumTilesForPackedMips = 0;
        packed_mip_info->NumPackedMips = 0;
        packed_mip_info->StartTileIndexInOverallResource = 0;

        if (sizes)
        {
            block_extent.width = sizes->width * format->block_width;
            block_extent.height = sizes->height * format->block_height;
        }
        else
        {
            WARN("Unrecognized byte_size %u, samples %u pair.\n", format->byte_count, desc->SampleDesc.Count);
            /* Just hallucinate something so we don't crash. */
            block_extent.width = 256;
            block_extent.height = 256;
        }

        tile_shape->WidthInTexels = block_extent.width;
        tile_shape->HeightInTexels = block_extent.height;
        tile_shape->DepthInTexels = 1;

        tile_count = 0;

        for (i = 0; i < d3d12_resource_desc_get_sub_resource_count_per_plane(desc); i++)
        {
            unsigned int mip_level = i % desc->MipLevels;
            VkExtent3D mip_extent = d3d12_resource_desc_get_subresource_extent(desc, format, mip_level);
            unsigned int tile_count_w = align(mip_extent.width, block_extent.width) / block_extent.width;
            unsigned int tile_count_h = align(mip_extent.height, block_extent.height) / block_extent.height;

            tilings[i].WidthInTiles = tile_count_w;
            tilings[i].HeightInTiles = tile_count_h;
            tilings[i].DepthInTiles = 1;
            tilings[i].StartTileIndexInOverallResource = tile_count;
            tile_count += tile_count_w * tile_count_h;
        }

        *total_tile_count = tile_count;
    }
}

static void d3d12_resource_destroy(struct d3d12_resource *resource, struct d3d12_device *device);

ULONG d3d12_resource_incref(struct d3d12_resource *resource)
{
    ULONG refcount = InterlockedIncrement(&resource->internal_refcount);

    TRACE("%p increasing refcount to %u.\n", resource, refcount);

    return refcount;
}

ULONG d3d12_resource_decref(struct d3d12_resource *resource)
{
    ULONG refcount = InterlockedDecrement(&resource->internal_refcount);

    TRACE("%p decreasing refcount to %u.\n", resource, refcount);

    if (!refcount)
        d3d12_resource_destroy(resource, resource->device);

    return refcount;
}

bool d3d12_resource_is_cpu_accessible(const struct d3d12_resource *resource)
{
    return !(resource->flags & VKD3D_RESOURCE_RESERVED) &&
            is_cpu_accessible_heap(&resource->heap_properties);
}

static bool d3d12_resource_validate_box(const struct d3d12_resource *resource,
        unsigned int subresource_idx, const D3D12_BOX *box)
{
    uint32_t width_mask, height_mask;
    VkExtent3D mip_extent;

    mip_extent = d3d12_resource_desc_get_subresource_extent(&resource->desc, resource->format, subresource_idx);

    width_mask = resource->format->block_width - 1;
    height_mask = resource->format->block_height - 1;

    return box->left <= mip_extent.width && box->right <= mip_extent.width
            && box->top <= mip_extent.height && box->bottom <= mip_extent.height
            && box->front <= mip_extent.depth && box->back <= mip_extent.depth
            && !(box->left & width_mask)
            && !(box->right & width_mask)
            && !(box->top & height_mask)
            && !(box->bottom & height_mask);
}

static void d3d12_resource_get_subresource_box(const struct d3d12_resource *resource,
        unsigned int subresource_idx, D3D12_BOX *box)
{
    VkExtent3D mip_extent = d3d12_resource_desc_get_subresource_extent(&resource->desc, resource->format, subresource_idx);

    box->left = 0;
    box->top = 0;
    box->front = 0;
    box->right = mip_extent.width;
    box->bottom = mip_extent.height;
    box->back = mip_extent.depth;
}

static void d3d12_resource_set_name(struct d3d12_resource *resource, const char *name)
{
    /* Multiple committed and placed buffers may refer to the same VkBuffer,
     * which may cause race conditions if the app calls this concurrently */
    if (d3d12_resource_is_buffer(resource) && (resource->flags & VKD3D_RESOURCE_RESERVED))
        vkd3d_set_vk_object_name(resource->device, (uint64_t)resource->res.vk_buffer,
                VK_OBJECT_TYPE_BUFFER, name);
    else if (d3d12_resource_is_texture(resource))
        vkd3d_set_vk_object_name(resource->device, (uint64_t)resource->res.vk_image,
                VK_OBJECT_TYPE_IMAGE, name);
}

/* ID3D12Resource */
static HRESULT STDMETHODCALLTYPE d3d12_resource_QueryInterface(d3d12_resource_iface *iface,
        REFIID riid, void **object)
{
    struct d3d12_resource *resource = impl_from_ID3D12Resource2(iface);

    TRACE("iface %p, riid %s, object %p.\n", iface, debugstr_guid(riid), object);

    if (!object)
        return E_POINTER;

    if (IsEqualGUID(riid, &IID_ID3D12Resource)
            || IsEqualGUID(riid, &IID_ID3D12Resource1)
            || IsEqualGUID(riid, &IID_ID3D12Resource2)
            || IsEqualGUID(riid, &IID_ID3D12Pageable)
            || IsEqualGUID(riid, &IID_ID3D12DeviceChild)
            || IsEqualGUID(riid, &IID_ID3D12Object)
            || IsEqualGUID(riid, &IID_IUnknown))
    {
        ID3D12Resource2_AddRef(iface);
        *object = iface;
        return S_OK;
    }

    if (IsEqualGUID(riid, &IID_ID3DDestructionNotifier))
    {
        ID3DDestructionNotifier_AddRef(&resource->destruction_notifier.ID3DDestructionNotifier_iface);
        *object = &resource->destruction_notifier.ID3DDestructionNotifier_iface;
        return S_OK;
    }

    WARN("%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid(riid));

    *object = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE d3d12_resource_AddRef(d3d12_resource_iface *iface)
{
    struct d3d12_resource *resource = impl_from_ID3D12Resource2(iface);
    ULONG refcount = InterlockedIncrement(&resource->refcount);

    TRACE("%p increasing refcount to %u.\n", resource, refcount);

    if (refcount == 1)
    {
        struct d3d12_device *device = resource->device;

        d3d12_device_add_ref(device);
        d3d12_resource_incref(resource);
    }

    return refcount;
}

static ULONG STDMETHODCALLTYPE d3d12_resource_Release(d3d12_resource_iface *iface)
{
    struct d3d12_resource *resource = impl_from_ID3D12Resource2(iface);
    struct d3d12_device *device = resource->device;
    ULONG refcount;

    refcount = InterlockedDecrement(&resource->refcount);

    TRACE("%p decreasing refcount to %u.\n", resource, refcount);

    if (!refcount)
    {
        d3d_destruction_notifier_notify(&resource->destruction_notifier);

        d3d12_resource_decref(resource);
        d3d12_device_release(device);
    }

    return refcount;
}

static HRESULT STDMETHODCALLTYPE d3d12_resource_GetPrivateData(d3d12_resource_iface *iface,
        REFGUID guid, UINT *data_size, void *data)
{
    struct d3d12_resource *resource = impl_from_ID3D12Resource2(iface);

    TRACE("iface %p, guid %s, data_size %p, data %p.\n", iface, debugstr_guid(guid), data_size, data);

    return vkd3d_get_private_data(&resource->private_store, guid, data_size, data);
}

static HRESULT STDMETHODCALLTYPE d3d12_resource_SetPrivateData(d3d12_resource_iface *iface,
        REFGUID guid, UINT data_size, const void *data)
{
    struct d3d12_resource *resource = impl_from_ID3D12Resource2(iface);

    TRACE("iface %p, guid %s, data_size %u, data %p.\n", iface, debugstr_guid(guid), data_size, data);

    return vkd3d_set_private_data(&resource->private_store, guid, data_size, data,
            (vkd3d_set_name_callback) d3d12_resource_set_name, resource);
}

static HRESULT STDMETHODCALLTYPE d3d12_resource_SetPrivateDataInterface(d3d12_resource_iface *iface,
        REFGUID guid, const IUnknown *data)
{
    struct d3d12_resource *resource = impl_from_ID3D12Resource2(iface);

    TRACE("iface %p, guid %s, data %p.\n", iface, debugstr_guid(guid), data);

    return vkd3d_set_private_data_interface(&resource->private_store, guid, data,
            (vkd3d_set_name_callback) d3d12_resource_set_name, resource);
}

static HRESULT STDMETHODCALLTYPE d3d12_resource_GetDevice(d3d12_resource_iface *iface, REFIID iid, void **device)
{
    struct d3d12_resource *resource = impl_from_ID3D12Resource2(iface);

    TRACE("iface %p, iid %s, device %p.\n", iface, debugstr_guid(iid), device);

    return d3d12_device_query_interface(resource->device, iid, device);
}

static bool d3d12_resource_get_mapped_memory_range(struct d3d12_resource *resource,
        UINT subresource, const D3D12_RANGE *range, VkMappedMemoryRange *vk_mapped_range)
{
    const struct d3d12_device *device = resource->device;

    if (range && range->End <= range->Begin)
        return false;

    if (device->memory_properties.memoryTypes[resource->mem.device_allocation.vk_memory_type].propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
        return false;

    vk_mapped_range->sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
    vk_mapped_range->pNext = NULL;
    vk_mapped_range->memory = resource->mem.device_allocation.vk_memory;

    if (resource->desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER)
    {
        vk_mapped_range->offset = resource->mem.offset;
        vk_mapped_range->size = resource->desc.Width;
    }
    else
    {
        FIXME("Not implemented for textures.\n");
        return false;
    }

    if (range)
    {
        vk_mapped_range->offset += range->Begin;
        vk_mapped_range->size = range->End - range->Begin;
    }

    vkd3d_mapped_memory_range_align(device, vk_mapped_range, resource->mem.device_allocation.size);

    return true;
}

static void d3d12_resource_invalidate_range(struct d3d12_resource *resource,
        UINT subresource, const D3D12_RANGE *read_range)
{
    const struct vkd3d_vk_device_procs *vk_procs = &resource->device->vk_procs;
    VkMappedMemoryRange mapped_range;

    if (!d3d12_resource_get_mapped_memory_range(resource, subresource, read_range, &mapped_range))
        return;

    VK_CALL(vkInvalidateMappedMemoryRanges(resource->device->vk_device, 1, &mapped_range));
}

static void d3d12_resource_flush_range(struct d3d12_resource *resource,
        UINT subresource, const D3D12_RANGE *written_range)
{
    const struct vkd3d_vk_device_procs *vk_procs = &resource->device->vk_procs;
    VkMappedMemoryRange mapped_range;

    if (!d3d12_resource_get_mapped_memory_range(resource, subresource, written_range, &mapped_range))
        return;

    VK_CALL(vkFlushMappedMemoryRanges(resource->device->vk_device, 1, &mapped_range));
}

static void d3d12_resource_get_map_ptr(struct d3d12_resource *resource, void **data)
{
    assert(resource->mem.cpu_address);
    *data = resource->mem.cpu_address;
}

static bool d3d12_resource_texture_validate_map(struct d3d12_resource *resource)
{
    bool invalid_map;
    /* Very special case that is explicitly called out in the D3D12 validation layers. */
    invalid_map = resource->desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D &&
            resource->desc.MipLevels > 1;
    return !invalid_map;
}

static HRESULT STDMETHODCALLTYPE d3d12_resource_Map(d3d12_resource_iface *iface, UINT sub_resource,
        const D3D12_RANGE *read_range, void **data)
{
    struct d3d12_resource *resource = impl_from_ID3D12Resource2(iface);
    unsigned int sub_resource_count;

    TRACE("iface %p, sub_resource %u, read_range %p, data %p.\n",
            iface, sub_resource, read_range, data);

    if (!d3d12_resource_is_cpu_accessible(resource))
    {
        WARN("Resource is not CPU accessible.\n");
        return E_INVALIDARG;
    }

    sub_resource_count = d3d12_resource_get_sub_resource_count(resource);
    if (sub_resource >= sub_resource_count)
    {
        WARN("Sub-resource index %u is out of range (%u sub-resources).\n", sub_resource, sub_resource_count);
        return E_INVALIDARG;
    }

    if (d3d12_resource_is_texture(resource) && (data || !d3d12_resource_texture_validate_map(resource)))
    {
        /* Cannot get pointer to mapped texture.
         * It is only possible to make UNKNOWN textures host visible,
         * and only NULL map + Write/ReadSubresource is allowed in this scenario. */
        return E_INVALIDARG;
    }

    if (resource->flags & VKD3D_RESOURCE_RESERVED)
    {
        FIXME("Not implemented for this resource type.\n");
        return E_NOTIMPL;
    }

    if (data)
    {
        d3d12_resource_get_map_ptr(resource, data);
        TRACE("Returning pointer %p.\n", *data);
    }

    d3d12_resource_invalidate_range(resource, sub_resource, read_range);
    return S_OK;
}

static void STDMETHODCALLTYPE d3d12_resource_Unmap(d3d12_resource_iface *iface, UINT sub_resource,
        const D3D12_RANGE *written_range)
{
    struct d3d12_resource *resource = impl_from_ID3D12Resource2(iface);
    unsigned int sub_resource_count;

    TRACE("iface %p, sub_resource %u, written_range %p.\n",
            iface, sub_resource, written_range);

    sub_resource_count = d3d12_resource_get_sub_resource_count(resource);
    if (sub_resource >= sub_resource_count)
    {
        WARN("Sub-resource index %u is out of range (%u sub-resources).\n", sub_resource, sub_resource_count);
        return;
    }

    d3d12_resource_flush_range(resource, sub_resource, written_range);
}

static D3D12_RESOURCE_DESC * STDMETHODCALLTYPE d3d12_resource_GetDesc(d3d12_resource_iface *iface,
        D3D12_RESOURCE_DESC *resource_desc)
{
    struct d3d12_resource *resource = impl_from_ID3D12Resource2(iface);

    TRACE("iface %p, resource_desc %p.\n", iface, resource_desc);

    resource_desc->Dimension = resource->desc.Dimension;
    resource_desc->Alignment = resource->desc.Alignment;
    resource_desc->Width = resource->desc.Width;
    resource_desc->Height = resource->desc.Height;
    resource_desc->DepthOrArraySize = resource->desc.DepthOrArraySize;
    resource_desc->MipLevels = resource->desc.MipLevels;
    resource_desc->Format = resource->desc.Format;
    resource_desc->SampleDesc = resource->desc.SampleDesc;
    resource_desc->Layout = resource->desc.Layout;
    resource_desc->Flags = resource->desc.Flags;
    return resource_desc;
}

static D3D12_GPU_VIRTUAL_ADDRESS STDMETHODCALLTYPE d3d12_resource_GetGPUVirtualAddress(d3d12_resource_iface *iface)
{
    struct d3d12_resource *resource = impl_from_ID3D12Resource2(iface);

    TRACE("iface %p.\n", iface);

    return resource->res.va;
}

static HRESULT STDMETHODCALLTYPE d3d12_resource_WriteToSubresource(d3d12_resource_iface *iface,
        UINT dst_sub_resource, const D3D12_BOX *dst_box, const void *src_data,
        UINT src_row_pitch, UINT src_slice_pitch)
{
    struct d3d12_resource *resource = impl_from_ID3D12Resource2(iface);
    struct vkd3d_subresource_layout *subresource_layout;
    struct d3d12_device *device = resource->device;
    const struct vkd3d_vk_device_procs *vk_procs;
    VkMappedMemoryRange mapped_range = { 0 };
    struct vkd3d_format_footprint footprint;
    const struct vkd3d_format *format;
    uint32_t plane_idx;
    VkExtent3D extent;
    VkOffset3D offset;
    uint8_t *dst_data;
    D3D12_BOX box;

    vk_procs = &device->vk_procs;

    TRACE("iface %p, src_data %p, src_row_pitch %u, src_slice_pitch %u, "
            "dst_sub_resource %u, dst_box %s.\n",
            iface, src_data, src_row_pitch, src_slice_pitch, dst_sub_resource, debug_d3d12_box(dst_box));

    if (d3d12_resource_is_buffer(resource))
    {
        WARN("Buffers are not supported.\n");
        return E_INVALIDARG;
    }

    if (!dst_box)
    {
        d3d12_resource_get_subresource_box(resource, dst_sub_resource, &box);
        dst_box = &box;
    }
    else if (!d3d12_resource_validate_box(resource, dst_sub_resource, dst_box))
    {
        WARN("Invalid box %s.\n", debug_d3d12_box(dst_box));
        return E_INVALIDARG;
    }

    if (d3d12_box_is_empty(dst_box))
    {
        WARN("Empty box %s.\n", debug_d3d12_box(dst_box));
        return S_OK;
    }

    if (!d3d12_resource_is_cpu_accessible(resource))
    {
        FIXME_ONCE("Not implemented for this resource type.\n");
        return E_NOTIMPL;
    }

    plane_idx = dst_sub_resource / d3d12_resource_desc_get_sub_resource_count_per_plane(&resource->desc);
    footprint = vkd3d_format_footprint_for_plane(resource->format, plane_idx);
    format = vkd3d_format_from_d3d12_resource_desc(device, &resource->desc, footprint.dxgi_format);

    if (format->vk_aspect_mask != VK_IMAGE_ASPECT_COLOR_BIT)
    {
        FIXME("Not supported for format %#x.\n", format->dxgi_format);
        return E_NOTIMPL;
    }

    offset.x = dst_box->left;
    offset.y = dst_box->top;
    offset.z = dst_box->front;

    extent.width = dst_box->right - dst_box->left;
    extent.height = dst_box->bottom - dst_box->top;
    extent.depth = dst_box->back - dst_box->front;

    subresource_layout = &resource->subresource_layouts[dst_sub_resource];
    TRACE("Offset %#zx, row pitch %#zx, depth pitch %#zx.\n",
            subresource_layout->offset, subresource_layout->row_pitch, subresource_layout->depth_pitch);

    d3d12_resource_get_map_ptr(resource, (void **)&dst_data);

    dst_data += subresource_layout->offset + vkd3d_format_get_data_offset(format,
            subresource_layout->row_pitch, subresource_layout->depth_pitch, offset.x, offset.y, offset.z);

    vkd3d_format_copy_data(format, src_data, src_row_pitch, src_slice_pitch, dst_data,
            subresource_layout->row_pitch, subresource_layout->depth_pitch, extent.width, extent.height, extent.depth);

    mapped_range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
    mapped_range.memory = resource->mem.device_allocation.vk_memory;
    mapped_range.offset = resource->mem.offset + subresource_layout->offset;
    mapped_range.size = (extent.depth - 1) * subresource_layout->depth_pitch +
      extent.height * subresource_layout->row_pitch;
    vkd3d_mapped_memory_range_align(device, &mapped_range, resource->mem.device_allocation.size);
    VK_CALL(vkFlushMappedMemoryRanges(device->vk_device, 1, &mapped_range));

    return vkd3d_memory_transfer_queue_write_subresource(&device->memory_transfers,
            resource, dst_sub_resource, offset, extent);
}

static HRESULT STDMETHODCALLTYPE d3d12_resource_ReadFromSubresource(d3d12_resource_iface *iface,
        void *dst_data, UINT dst_row_pitch, UINT dst_slice_pitch,
        UINT src_sub_resource, const D3D12_BOX *src_box)
{
    struct d3d12_resource *resource = impl_from_ID3D12Resource2(iface);
    struct d3d12_device *device = resource->device;
    struct vkd3d_subresource_layout *subresource_layout;
    const struct vkd3d_vk_device_procs *vk_procs;
    VkMappedMemoryRange mapped_range = { 0 };
    struct vkd3d_format_footprint footprint;
    const struct vkd3d_format *format;
    uint32_t plane_idx;
    uint8_t *src_data;
    D3D12_BOX box;

    vk_procs = &device->vk_procs;

    TRACE("iface %p, dst_data %p, dst_row_pitch %u, dst_slice_pitch %u, "
            "src_sub_resource %u, src_box %s.\n",
            iface, dst_data, dst_row_pitch, dst_slice_pitch, src_sub_resource, debug_d3d12_box(src_box));

    if (d3d12_resource_is_buffer(resource))
    {
        WARN("Buffers are not supported.\n");
        return E_INVALIDARG;
    }

    if (!src_box)
    {
        d3d12_resource_get_subresource_box(resource, src_sub_resource, &box);
        src_box = &box;
    }
    else if (!d3d12_resource_validate_box(resource, src_sub_resource, src_box))
    {
        WARN("Invalid box %s.\n", debug_d3d12_box(src_box));
        return E_INVALIDARG;
    }

    if (d3d12_box_is_empty(src_box))
    {
        WARN("Empty box %s.\n", debug_d3d12_box(src_box));
        return S_OK;
    }

    if (!d3d12_resource_is_cpu_accessible(resource))
    {
        FIXME_ONCE("Not implemented for this resource type.\n");
        return E_NOTIMPL;
    }

    plane_idx = src_sub_resource / d3d12_resource_desc_get_sub_resource_count_per_plane(&resource->desc);
    footprint = vkd3d_format_footprint_for_plane(resource->format, plane_idx);
    format = vkd3d_format_from_d3d12_resource_desc(resource->device, &resource->desc, footprint.dxgi_format);

    if (format->vk_aspect_mask != VK_IMAGE_ASPECT_COLOR_BIT)
    {
        FIXME("Not supported for format %#x.\n", format->dxgi_format);
        return E_NOTIMPL;
    }

    subresource_layout = &resource->subresource_layouts[src_sub_resource];
    TRACE("Offset %#zx, row pitch %#zx, depth pitch %#zx.\n",
            subresource_layout->offset, subresource_layout->row_pitch, subresource_layout->depth_pitch);

    d3d12_resource_get_map_ptr(resource, (void **)&src_data);

    src_data += subresource_layout->offset + vkd3d_format_get_data_offset(format,
            subresource_layout->row_pitch, subresource_layout->depth_pitch, src_box->left, src_box->top, src_box->front);

    mapped_range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
    mapped_range.memory = resource->mem.device_allocation.vk_memory;
    mapped_range.offset = resource->mem.offset + subresource_layout->offset;
    mapped_range.size = (src_box->back - src_box->front - 1) * subresource_layout->depth_pitch +
      (src_box->bottom - src_box->top) * subresource_layout->row_pitch;
    vkd3d_mapped_memory_range_align(device, &mapped_range, resource->mem.device_allocation.size);
    VK_CALL(vkInvalidateMappedMemoryRanges(device->vk_device, 1, &mapped_range));

    vkd3d_format_copy_data(format, src_data, subresource_layout->row_pitch,
            subresource_layout->depth_pitch, dst_data, dst_row_pitch, dst_slice_pitch,
            src_box->right - src_box->left, src_box->bottom - src_box->top, src_box->back - src_box->front);

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d3d12_resource_GetHeapProperties(d3d12_resource_iface *iface,
        D3D12_HEAP_PROPERTIES *heap_properties, D3D12_HEAP_FLAGS *flags)
{
    struct d3d12_resource *resource = impl_from_ID3D12Resource2(iface);

    TRACE("iface %p, heap_properties %p, flags %p.\n",
            iface, heap_properties, flags);

    if (resource->flags & VKD3D_RESOURCE_EXTERNAL)
    {
        if (heap_properties)
        {
            memset(heap_properties, 0, sizeof(*heap_properties));
            heap_properties->Type = D3D12_HEAP_TYPE_DEFAULT;
            heap_properties->CreationNodeMask = 1;
            heap_properties->VisibleNodeMask = 1;
        }
        if (flags)
            *flags = D3D12_HEAP_FLAG_NONE;
        return S_OK;
    }

    if (resource->flags & VKD3D_RESOURCE_RESERVED)
    {
        WARN("Cannot get heap properties for reserved resources.\n");
        return E_INVALIDARG;
    }

    if (heap_properties)
        *heap_properties = resource->heap_properties;
    if (flags)
        *flags = resource->heap_flags;

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d3d12_resource_GetProtectedResourceSession(d3d12_resource_iface *iface,
        REFIID iid, void **protected_session)
{
    FIXME("iface %p, iid %s, protected_session %p stub!", iface, debugstr_guid(iid), protected_session);

    return E_NOTIMPL;
}

static D3D12_RESOURCE_DESC1 * STDMETHODCALLTYPE d3d12_resource_GetDesc1(d3d12_resource_iface *iface,
        D3D12_RESOURCE_DESC1 *resource_desc)
{
    struct d3d12_resource *resource = impl_from_ID3D12Resource2(iface);

    TRACE("iface %p, resource_desc %p.\n", iface, resource_desc);

    *resource_desc = resource->desc;
    return resource_desc;
}

CONST_VTBL struct ID3D12Resource2Vtbl d3d12_resource_vtbl =
{
    /* IUnknown methods */
    d3d12_resource_QueryInterface,
    d3d12_resource_AddRef,
    d3d12_resource_Release,
    /* ID3D12Object methods */
    d3d12_resource_GetPrivateData,
    d3d12_resource_SetPrivateData,
    d3d12_resource_SetPrivateDataInterface,
    (void *)d3d12_object_SetName,
    /* ID3D12DeviceChild methods */
    d3d12_resource_GetDevice,
    /* ID3D12Resource methods */
    d3d12_resource_Map,
    d3d12_resource_Unmap,
    d3d12_resource_GetDesc,
    d3d12_resource_GetGPUVirtualAddress,
    d3d12_resource_WriteToSubresource,
    d3d12_resource_ReadFromSubresource,
    d3d12_resource_GetHeapProperties,
    /* ID3D12Resource1 methods */
    d3d12_resource_GetProtectedResourceSession,
    /* ID3D12Resource2 methods */
    d3d12_resource_GetDesc1,
};

VkImageAspectFlags vk_image_aspect_flags_from_d3d12(
        const struct vkd3d_format *format, uint32_t plane_idx)
{
    VkImageAspectFlags aspect_mask = format->vk_aspect_mask;
    uint32_t i;

    /* For all formats we currently handle, the n-th aspect bit in Vulkan
     * corresponds to the n-th plane in D3D12, so isolate the respective
     * bit in the aspect mask. */
    for (i = 0; i < plane_idx; i++)
        aspect_mask &= aspect_mask - 1;

    if (!aspect_mask)
    {
        WARN("Invalid plane index %u for format %u.\n", plane_idx, format->vk_format);
        aspect_mask = format->vk_aspect_mask;
    }

    return aspect_mask & -aspect_mask;
}

VkImageSubresource vk_image_subresource_from_d3d12(
        const struct vkd3d_format *format, uint32_t subresource_idx,
        unsigned int miplevel_count, unsigned int layer_count,
        bool all_aspects)
{
    VkImageSubresource subresource;

    subresource.aspectMask = format->vk_aspect_mask;
    subresource.mipLevel = subresource_idx % miplevel_count;
    subresource.arrayLayer = (subresource_idx / miplevel_count) % layer_count;

    if (!all_aspects)
    {
        subresource.aspectMask = vk_image_aspect_flags_from_d3d12(
                format, subresource_idx / (miplevel_count * layer_count));
    }

    return subresource;
}

UINT d3d12_plane_index_from_vk_aspect(VkImageAspectFlagBits aspect)
{
    switch (aspect)
    {
        case VK_IMAGE_ASPECT_COLOR_BIT:
        case VK_IMAGE_ASPECT_DEPTH_BIT:
        case VK_IMAGE_ASPECT_PLANE_0_BIT:
            return 0;

        case VK_IMAGE_ASPECT_STENCIL_BIT:
        case VK_IMAGE_ASPECT_PLANE_1_BIT:
            return 1;

        case VK_IMAGE_ASPECT_PLANE_2_BIT:
            return 2;

        default:
            WARN("Unsupported image aspect: %u.\n", aspect);
            return 0;
    }
}

VkImageSubresource d3d12_resource_get_vk_subresource(const struct d3d12_resource *resource,
        uint32_t subresource_idx, bool all_aspects)
{
    return vk_image_subresource_from_d3d12(
            resource->format, subresource_idx,
            resource->desc.MipLevels, d3d12_resource_desc_get_layer_count(&resource->desc),
            all_aspects);
}

static HRESULT d3d12_validate_resource_flags(D3D12_RESOURCE_FLAGS flags)
{
    unsigned int unknown_flags = flags & ~(D3D12_RESOURCE_FLAG_NONE
            | D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET
            | D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL
            | D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS
            | D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE
            | D3D12_RESOURCE_FLAG_ALLOW_CROSS_ADAPTER
            | D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS);

    if (unknown_flags)
        FIXME("Unknown resource flags %#x.\n", unknown_flags);

    if ((flags & D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS) && (flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL))
    {
        ERR("ALLOW_SIMULTANEOUS_ACCESS and ALLOW_DEPTH_STENCIL is not allowed.\n");
        return E_INVALIDARG;
    }

    if ((flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS) && (flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL))
    {
        ERR("ALLOW_UNORDERED_ACCESS and ALLOW_DEPTH_STENCIL is not allowed.\n");
        return E_INVALIDARG;
    }

    if ((flags & D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET) && (flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL))
    {
        ERR("ALLOW_RENDER_TARGET and ALLOW_DEPTH_STENCIL is not allowed.\n");
        return E_INVALIDARG;
    }

    return S_OK;
}

static bool d3d12_resource_validate_texture_format(const D3D12_RESOURCE_DESC1 *desc,
        const struct vkd3d_format *format)
{
    if (!vkd3d_format_is_compressed(format))
        return true;

    if (desc->Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE1D && format->block_height > 1)
    {
        WARN("1D texture with a format block height > 1.\n");
        return false;
    }

    return true;
}

static bool d3d12_resource_validate_texture_alignment(const D3D12_RESOURCE_DESC1 *desc,
        const struct vkd3d_format *format)
{
    uint64_t estimated_size;

    if (!desc->Alignment)
        return true;

    if (desc->Alignment != D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT
            && desc->Alignment != D3D12_SMALL_RESOURCE_PLACEMENT_ALIGNMENT
            && (desc->SampleDesc.Count == 1 || desc->Alignment != D3D12_DEFAULT_MSAA_RESOURCE_PLACEMENT_ALIGNMENT))
    {
        WARN("Invalid resource alignment %#"PRIx64".\n", desc->Alignment);
        return false;
    }

    if (desc->Alignment < D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT)
    {
        /* Windows uses the slice size to determine small alignment eligibility. DepthOrArraySize is ignored. */
        estimated_size = desc->Width * desc->Height * format->byte_count * format->block_byte_count
                / (format->block_width * format->block_height);
        if (estimated_size > D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT)
        {
            WARN("Invalid resource alignment %#"PRIx64" (required %#x).\n",
                    desc->Alignment, D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT);
            return false;
        }
    }

    /* The size check for MSAA textures with D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT is probably
     * not important. The 4MB requirement is no longer universal and Vulkan has no such requirement. */

    return true;
}

void d3d12_resource_promote_desc(const D3D12_RESOURCE_DESC *desc, D3D12_RESOURCE_DESC1 *desc1)
{
    desc1->Dimension = desc->Dimension;
    desc1->Alignment = desc->Alignment;
    desc1->Width = desc->Width;
    desc1->Height = desc->Height;
    desc1->DepthOrArraySize = desc->DepthOrArraySize;
    desc1->MipLevels = desc->MipLevels;
    desc1->Format = desc->Format;
    desc1->SampleDesc = desc->SampleDesc;
    desc1->Layout = desc->Layout;
    desc1->Flags = desc->Flags;
    desc1->SamplerFeedbackMipRegion.Width = 0;
    desc1->SamplerFeedbackMipRegion.Height = 0;
    desc1->SamplerFeedbackMipRegion.Depth = 0;
}

static HRESULT d3d12_resource_validate_usage(const D3D12_RESOURCE_DESC1 *desc,
        UINT num_castable_formats, const DXGI_FORMAT *castable_formats,
        struct d3d12_device *device)
{
    /* Sentinel for format being supported at all. */
    VkFormatFeatureFlags required_image_flags =
            VK_FORMAT_FEATURE_TRANSFER_DST_BIT | VK_FORMAT_FEATURE_TRANSFER_SRC_BIT;
    const struct vkd3d_format *format = NULL;
    const struct vkd3d_format *cast_format;
    VkFormatFeatureFlags total_flags = 0;
    UINT i;

    /* Validate that special usage flags are satisfied. */
    if (desc->Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS)
        required_image_flags |= VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT;
    if (desc->Flags & D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET)
        required_image_flags |= VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT;
    if (desc->Flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL)
        required_image_flags |= VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT;
    if (!(desc->Flags & D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE) || desc->SampleDesc.Count > 1)
        required_image_flags |= VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;

    if (desc->Dimension != D3D12_RESOURCE_DIMENSION_BUFFER)
    {
        /* For DSV-enabled textures, UAV and RTV is banned. We only need to check if one format
         * in the cast list potentially supports DSV. Oddly enough, we can add TYPELESS formats
         * to the list, and it still counts for purposes of validation, but they don't count as being viewable,
         * so we can end up in a situation where we can create a DSV texture that can never be viewed as DSV ...
         * This is demonstrated by tests, and is likely a bug/quirk of the runtime, but applications
         * may accidentally rely on this. */
        format = vkd3d_format_from_d3d12_resource_desc(device, desc, 0);
        if (!format)
        {
            WARN("Unrecognized format #%x.\n", desc->Format);
            return E_INVALIDARG;
        }
        total_flags |= num_castable_formats ? format->vk_format_features : format->vk_format_features_castable;
    }

    /* Validate format cast list. */
    for (i = 0; i < num_castable_formats; i++)
    {
        if (desc->Dimension == D3D12_RESOURCE_DIMENSION_BUFFER)
        {
            /* D3D12 runtime allows this, even if it is nonsensical. */
            if (castable_formats[i] != DXGI_FORMAT_UNKNOWN)
                return E_INVALIDARG;
            continue;
        }

        cast_format = vkd3d_get_format(device, castable_formats[i],
                !!(desc->Flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL));
        if (!cast_format)
        {
            WARN("Unrecognized format #%x.\n", castable_formats[i]);
            return E_INVALIDARG;
        }
        total_flags |= cast_format->vk_format_features;

        if (vkd3d_format_is_compressed(format))
        {
            if (vkd3d_format_is_compressed(cast_format))
            {
                if (format->block_byte_count != cast_format->block_byte_count)
                {
                    WARN("Cannot cast to block format of different size.\n");
                    return E_INVALIDARG;
                }
            }
            else
            {
                if (format->block_byte_count != cast_format->byte_count)
                {
                    WARN("Cannot cast to non-compressed format with different size.\n");
                    return E_INVALIDARG;
                }
            }
        }
        else if (vkd3d_format_is_compressed(cast_format))
        {
            WARN("Cannot cast uncompressed to block-compressed format.\n");
            return E_INVALIDARG;
        }
        else if (format->byte_count != cast_format->byte_count)
        {
            WARN("Cannot cast to type of different bit width.\n");
            return E_INVALIDARG;
        }
    }

    if (desc->Dimension != D3D12_RESOURCE_DIMENSION_BUFFER &&
            (total_flags & required_image_flags) != required_image_flags)
    {
        WARN("Requested resource flags #%x, but no format in cast list supports it.\n",
                desc->Flags);
        return E_INVALIDARG;
    }

    return S_OK;
}

HRESULT d3d12_resource_validate_desc(const D3D12_RESOURCE_DESC1 *desc,
        UINT num_castable_formats, const DXGI_FORMAT *castable_formats,
        struct d3d12_device *device)
{
    const struct vkd3d_format *format;
    unsigned int i;
    HRESULT hr;

    if (desc->Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D && desc->SampleDesc.Count > 1)
    {
        WARN("MSAA not supported on 1D and 3D textures.\n");
        return E_INVALIDARG;
    }

    /* There are special validation rules for sampler feedback. */
    if (d3d12_resource_desc_is_sampler_feedback(desc))
    {
        if (device->d3d12_caps.options7.SamplerFeedbackTier == D3D12_SAMPLER_FEEDBACK_TIER_NOT_SUPPORTED)
        {
            WARN("Sampler feedback not supported.\n");
            return E_INVALIDARG;
        }

        if (desc->SamplerFeedbackMipRegion.Width < 4 || desc->SamplerFeedbackMipRegion.Height < 4)
        {
            WARN("Sampler feedback mip region must be at least 4x4.\n");
            return E_INVALIDARG;
        }

        if (desc->SamplerFeedbackMipRegion.Depth > 1)
        {
            WARN("Sampler feedback mip region depth must be 0 or 1.\n");
            return E_INVALIDARG;
        }

        if (desc->SamplerFeedbackMipRegion.Width * 2 > desc->Width ||
                desc->SamplerFeedbackMipRegion.Height * 2 > desc->Height)
        {
            WARN("Sampler feedback mip region must not be larger than half the texture size.\n");
            return E_INVALIDARG;
        }

        if ((desc->SamplerFeedbackMipRegion.Width & (desc->SamplerFeedbackMipRegion.Width - 1)) ||
                (desc->SamplerFeedbackMipRegion.Height & (desc->SamplerFeedbackMipRegion.Height - 1)))
        {
            WARN("Sampler feedback mip region must be POT.\n");
            return E_INVALIDARG;
        }

        if (desc->Flags & (D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL | D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET))
        {
            WARN("Sampler feedback image cannot declare RTV/DSV usage.\n");
            return E_INVALIDARG;
        }

        if (desc->Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D)
        {
            WARN("Sampler feedback image must be 2D.\n");
            return E_INVALIDARG;
        }
    }

    switch (desc->Dimension)
    {
        case D3D12_RESOURCE_DIMENSION_BUFFER:
            if (desc->MipLevels != 1)
            {
                WARN("Invalid miplevel count %u for buffer.\n", desc->MipLevels);
                return E_INVALIDARG;
            }

            if (desc->Alignment != 0 && desc->Alignment != D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT)
            {
                WARN("Invalid alignment %"PRIu64" for buffer resource. Must be 0 or D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT.\n",
                        desc->Alignment);
                return E_INVALIDARG;
            }

            if (desc->Format != DXGI_FORMAT_UNKNOWN || desc->Layout != D3D12_TEXTURE_LAYOUT_ROW_MAJOR
                    || desc->Height != 1 || desc->DepthOrArraySize != 1
                    || desc->SampleDesc.Count != 1 || desc->SampleDesc.Quality != 0)
            {
                WARN("Invalid parameters for a buffer resource.\n");
                return E_INVALIDARG;
            }

            if (desc->Flags & D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS)
            {
                WARN("D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS cannot be set for buffers.\n");
                return E_INVALIDARG;
            }
            break;

        case D3D12_RESOURCE_DIMENSION_TEXTURE1D:
            if (desc->Height != 1)
            {
                WARN("1D texture with a height of %u.\n", desc->Height);
                return E_INVALIDARG;
            }
            /* Fall through. */
        case D3D12_RESOURCE_DIMENSION_TEXTURE2D:
            if (desc->SampleDesc.Count > 1)
            {
                if (!(desc->Flags & (D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL | D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET)))
                {
                    WARN("Multi-sampled textures must be created with render target or depth attachment usage.\n");
                    return E_INVALIDARG;
                }

                if ((desc->Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS) &&
                        !device->d3d12_caps.options14.WriteableMSAATexturesSupported)
                {
                    WARN("MSAA UAV textures not supported.\n");
                    return E_INVALIDARG;
                }
            }
            /* Fall through */
        case D3D12_RESOURCE_DIMENSION_TEXTURE3D:
            if (desc->SampleDesc.Count == 0)
            {
                WARN("Invalid sample count 0.\n");
                return E_INVALIDARG;
            }

            if (!(format = vkd3d_format_from_d3d12_resource_desc(device, desc, 0)))
            {
                WARN("Invalid format %#x.\n", desc->Format);
                return E_INVALIDARG;
            }

            if (!d3d12_resource_validate_texture_format(desc, format)
                    || !d3d12_resource_validate_texture_alignment(desc, format))
                return E_INVALIDARG;

            if (format->vk_aspect_mask & VK_IMAGE_ASPECT_PLANE_0_BIT)
            {
                if (desc->MipLevels != 1)
                {
                    WARN("Invalid mip level count %u for format %#x.\n", desc->MipLevels, desc->Format);
                    return E_INVALIDARG;
                }

                for (i = 0; i < format->plane_count; i++)
                {
                    if ((desc->Width & ((1u << format->plane_footprints[i].subsample_x_log2) - 1u)) ||
                            (desc->Height & ((1u << format->plane_footprints[i].subsample_y_log2) - 1u)))
                    {
                        WARN("Image size %"PRIu64"x%u not a multiple of %ux%u for format %#x.\n", desc->Width, desc->Height,
                                1u << format->plane_footprints[i].subsample_x_log2,
                                1u << format->plane_footprints[i].subsample_y_log2, desc->Format);
                        return E_INVALIDARG;
                    }
                }
            }
            break;

        default:
            WARN("Invalid resource dimension %#x.\n", desc->Dimension);
            return E_INVALIDARG;
    }

    if (FAILED(hr = d3d12_resource_validate_usage(desc, num_castable_formats, castable_formats, device)))
        return hr;

    return d3d12_validate_resource_flags(desc->Flags);
}

static HRESULT d3d12_resource_validate_heap_properties(const D3D12_RESOURCE_DESC1 *desc,
        const D3D12_HEAP_PROPERTIES *heap_properties, D3D12_RESOURCE_STATES initial_state)
{
    if (heap_properties->Type == D3D12_HEAP_TYPE_UPLOAD
            || heap_properties->Type == D3D12_HEAP_TYPE_READBACK)
    {
        if (desc->Dimension != D3D12_RESOURCE_DIMENSION_BUFFER)
        {
            WARN("Textures cannot be created on upload/readback heaps.\n");
            return E_INVALIDARG;
        }

        if (desc->Flags & (D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS))
        {
            WARN("Render target and unordered access buffers cannot be created on upload/readback heaps.\n");
            return E_INVALIDARG;
        }
    }

    if (heap_properties->Type == D3D12_HEAP_TYPE_UPLOAD &&
            (initial_state & ~D3D12_RESOURCE_STATE_GENERIC_READ))
    {
        /* AgilitySDK 606 suddenly started allowing COMMON state in UPLOAD heaps.
         * This is not publicly documented, but it's not a big problem to allow it either.
         * It also allows any state which is read-only. */
        WARN("For D3D12_HEAP_TYPE_UPLOAD the state must be part of the D3D12_RESOURCE_STATE_GENERIC_READ bitmask (or COMMON).\n");
        return E_INVALIDARG;
    }

    if (heap_properties->Type == D3D12_HEAP_TYPE_READBACK &&
            initial_state != D3D12_RESOURCE_STATE_COPY_DEST &&
            initial_state != D3D12_RESOURCE_STATE_COMMON)
    {
        /* AgilitySDK 606 suddenly started allowing COMMON state in READBACK heaps.
         * This is not publicly documented, but it's not a big problem to allow it either.
         * F1 22 hits this case. */
        WARN("For D3D12_HEAP_TYPE_READBACK the state must be D3D12_RESOURCE_STATE_COPY_DEST (or COMMON).\n");
        return E_INVALIDARG;
    }

    if (desc->Layout == D3D12_TEXTURE_LAYOUT_ROW_MAJOR)
    {
        /* ROW_MAJOR textures are severely restricted in D3D12.
         * See test_map_texture_validation() for details. */
        if (desc->Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D)
        {
            if (!(desc->Flags & D3D12_RESOURCE_FLAG_ALLOW_CROSS_ADAPTER))
            {
                WARN("ALLOW_CROSS_ADAPTER flag must be set to use ROW_MAJOR layout on textures.\n");
                return E_INVALIDARG;
            }

            if (desc->MipLevels > 1 || desc->DepthOrArraySize > 1)
            {
                WARN("For ROW_MAJOR textures, MipLevels and DepthOrArraySize must be 1.\n");
                return E_INVALIDARG;
            }

            if (heap_properties->Type == D3D12_HEAP_TYPE_CUSTOM &&
                    heap_properties->CPUPageProperty != D3D12_CPU_PAGE_PROPERTY_NOT_AVAILABLE)
            {
                WARN("ROW_MAJOR textures cannot be CPU visible with CUSTOM heaps.\n");
                return E_INVALIDARG;
            }
        }
        else if (desc->Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE1D ||
                desc->Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D)
        {
            WARN("1D and 3D textures cannot be ROW_MAJOR layout.\n");
            return E_INVALIDARG;
        }
    }

    return S_OK;
}

static HRESULT d3d12_resource_validate_create_info(const D3D12_RESOURCE_DESC1 *desc,
        const D3D12_HEAP_PROPERTIES *heap_properties, D3D12_RESOURCE_STATES initial_state,
        const D3D12_CLEAR_VALUE *optimized_clear_value,
        UINT num_castable_formats, const DXGI_FORMAT *castable_formats,
        struct d3d12_device *device)
{
    HRESULT hr;

    if (FAILED(hr = d3d12_resource_validate_desc(desc, num_castable_formats, castable_formats, device)))
        return hr;

    if (initial_state == D3D12_RESOURCE_STATE_RENDER_TARGET && !(desc->Flags & D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET))
    {
        WARN("Creating resource in render target state, but ALLOW_RENDER_TARGET flag is not set.\n");
        return E_INVALIDARG;
    }

    if ((initial_state == D3D12_RESOURCE_STATE_DEPTH_WRITE || initial_state == D3D12_RESOURCE_STATE_DEPTH_READ) &&
            !(desc->Flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL))
    {
        WARN("Creating resource in depth-stencil state, but ALLOW_DEPTH_STENCIL flag is not set.\n");
        return E_INVALIDARG;
    }

    if (heap_properties)
    {
        if (FAILED(hr = d3d12_resource_validate_heap_properties(desc, heap_properties, initial_state)))
            return hr;
    }

    if (optimized_clear_value)
    {
        if (desc->Dimension == D3D12_RESOURCE_DIMENSION_BUFFER)
        {
            WARN("Optimized clear value must be NULL for buffers.\n");
            return E_INVALIDARG;
        }

        TRACE("Ignoring optimized clear value.\n");
    }

    if (!is_valid_resource_state(initial_state))
    {
        WARN("Invalid initial resource state %#x.\n", initial_state);
        return E_INVALIDARG;
    }

    return S_OK;
}

static bool d3d12_device_requires_explicit_sparse_init(struct d3d12_device *device)
{
    /* While it's not perfectly clear from specification, apparently the consensus is that sparse page
     * tables should be initialized to "unbound" by driver.
     * Keep this code path here until we have clarified the specification. */
    (void)device;
    return false;
}

static HRESULT d3d12_resource_init_page_table(struct d3d12_resource *resource,
        struct d3d12_device *device, struct d3d12_sparse_info *sparse)
{
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    VkSparseImageMemoryRequirements *sparse_requirements = NULL;
    unsigned int mip_tail_layer_count, standard_mip_count;
    VkSparseImageOpaqueMemoryBindInfo opaque_bind_info;
    unsigned int opaque_bind_count, image_bind_count;
    VkSparseBufferMemoryBindInfo buffer_bind_info;
    VkTimelineSemaphoreSubmitInfo timeline_info;
    VkSparseImageMemoryBindInfo image_bind_info;
    VkImageSubresourceLayers subresource_layers;
    VkSparseImageMemoryBind *image_binds = NULL;
    VkMemoryRequirements memory_requirements;
    VkSparseMemoryBind *opaque_binds = NULL;
    struct vkd3d_queue *vkd3d_queue = NULL;
    uint32_t sparse_requirement_count;
    VkQueue vk_queue = VK_NULL_HANDLE;
    VkSparseMemoryBind buffer_bind;
    VkBindSparseInfo bind_info;
    VkDeviceSize metadata_size;
    unsigned int i, j, k;
    bool explicit_init;
    HRESULT hr = S_OK;
    VkResult vr;

    /* If a fallback resource, ignore. */
    if (!(resource->flags & VKD3D_RESOURCE_RESERVED))
        return S_OK;

    memset(&bind_info, 0, sizeof(bind_info));
    bind_info.sType = VK_STRUCTURE_TYPE_BIND_SPARSE_INFO;

    explicit_init = d3d12_device_requires_explicit_sparse_init(device);

    if (d3d12_resource_is_buffer(resource))
    {
        if (explicit_init)
        {
            memset(&buffer_bind, 0, sizeof(buffer_bind));
            buffer_bind.size = align64(resource->desc.Width, VKD3D_TILE_SIZE);

            memset(&buffer_bind_info, 0, sizeof(buffer_bind_info));
            buffer_bind_info.buffer = resource->res.vk_buffer;
            buffer_bind_info.bindCount = 1;
            buffer_bind_info.pBinds = &buffer_bind;

            bind_info.bufferBindCount = 1;
            bind_info.pBufferBinds = &buffer_bind_info;
        }
    }
    else
    {
        VK_CALL(vkGetImageSparseMemoryRequirements(device->vk_device,
            resource->res.vk_image, &sparse_requirement_count, NULL));

        if (!(sparse_requirements = vkd3d_malloc(sparse_requirement_count * sizeof(*sparse_requirements))))
        {
            ERR("Failed to allocate sparse memory requirement array.\n");
            hr = E_OUTOFMEMORY;
            goto cleanup;
        }

        VK_CALL(vkGetImageSparseMemoryRequirements(device->vk_device,
            resource->res.vk_image, &sparse_requirement_count, sparse_requirements));

        /* Find out how much memory and how many bind infos we need */
        metadata_size = 0;
        opaque_bind_count = 0;
        image_bind_count = 0;

        for (i = 0; i < sparse_requirement_count; i++)
        {
            const VkSparseImageMemoryRequirements *req = &sparse_requirements[i];

            mip_tail_layer_count = (req->formatProperties.flags & VK_SPARSE_IMAGE_FORMAT_SINGLE_MIPTAIL_BIT)
                    ? 1u : d3d12_resource_desc_get_layer_count(&resource->desc);

            if (req->imageMipTailSize && explicit_init)
                opaque_bind_count += mip_tail_layer_count;

            if (req->formatProperties.aspectMask & VK_IMAGE_ASPECT_METADATA_BIT)
            {
                metadata_size += mip_tail_layer_count * req->imageMipTailSize;
            }
            else if (explicit_init)
            {
                standard_mip_count = req->imageMipTailSize
                        ? min(resource->desc.MipLevels, req->imageMipTailFirstLod)
                        : resource->desc.MipLevels;

                image_bind_count += d3d12_resource_desc_get_layer_count(&resource->desc) * standard_mip_count;
            }
        }

        if ((opaque_bind_count && !(opaque_binds = vkd3d_calloc(opaque_bind_count, sizeof(*opaque_binds)))) ||
                (image_bind_count && !(image_binds = vkd3d_calloc(image_bind_count, sizeof(*image_binds)))))
        {
            ERR("Failed to allocate sparse memory bind info arrays.\n");
            hr = E_OUTOFMEMORY;
            goto cleanup;
        }

        if (metadata_size)
        {
            /* Allocate memory for metadata mip tail */
            TRACE("Allocating sparse metadata for resource %p.\n", resource);

            VK_CALL(vkGetImageMemoryRequirements(device->vk_device, resource->res.vk_image, &memory_requirements));

            if ((vr = vkd3d_allocate_device_memory(device, metadata_size, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                    memory_requirements.memoryTypeBits, NULL, true, &sparse->vk_metadata_memory)))
            {
                ERR("Failed to allocate device memory for sparse metadata, vr %d.\n", vr);
                hr = hresult_from_vk_result(vr);
                goto cleanup;
            }
        }

        /* Fill in opaque memory bind info */
        opaque_bind_count = 0;
        image_bind_count = 0;
        metadata_size = 0;

        for (i = 0; i < sparse_requirement_count; i++)
        {
            const VkSparseImageMemoryRequirements *req = &sparse_requirements[i];

            mip_tail_layer_count = (req->formatProperties.flags & VK_SPARSE_IMAGE_FORMAT_SINGLE_MIPTAIL_BIT)
                    ? 1u : d3d12_resource_desc_get_layer_count(&resource->desc);

            if (req->formatProperties.aspectMask & VK_IMAGE_ASPECT_METADATA_BIT)
            {
                for (j = 0; j < mip_tail_layer_count; j++)
                {
                    VkSparseMemoryBind *bind = &opaque_binds[opaque_bind_count++];
                    bind->resourceOffset = req->imageMipTailOffset + req->imageMipTailStride * j;
                    bind->size = req->imageMipTailSize;
                    bind->memory = sparse->vk_metadata_memory.vk_memory;
                    bind->memoryOffset = metadata_size;
                    bind->flags = VK_SPARSE_MEMORY_BIND_METADATA_BIT;

                    metadata_size += req->imageMipTailSize;
                }
            }
            else if (explicit_init)
            {
                if (req->imageMipTailSize)
                {
                    for (j = 0; j < mip_tail_layer_count; j++)
                    {
                        VkSparseMemoryBind *bind = &opaque_binds[opaque_bind_count++];
                        bind->resourceOffset = req->imageMipTailOffset + req->imageMipTailStride * j;
                        bind->size = req->imageMipTailSize;
                    }
                }

                standard_mip_count = req->imageMipTailSize
                        ? min(resource->desc.MipLevels, req->imageMipTailFirstLod)
                        : resource->desc.MipLevels;

                for (j = 0; j < d3d12_resource_desc_get_layer_count(&resource->desc); j++)
                {
                    for (k = 0; k < standard_mip_count; k++)
                    {
                        VkSparseImageMemoryBind *bind = &image_binds[image_bind_count++];
                        bind->subresource.aspectMask = req->formatProperties.aspectMask;
                        bind->subresource.arrayLayer = j;
                        bind->subresource.mipLevel = k;

                        subresource_layers = vk_subresource_layers_from_subresource(&bind->subresource);
                        bind->extent = d3d12_resource_desc_get_vk_subresource_extent(&resource->desc, resource->format, &subresource_layers);
                    }
                }
            }
        }

        if (image_bind_count)
        {
            memset(&image_bind_info, 0, sizeof(image_bind_info));
            image_bind_info.image = resource->res.vk_image;
            image_bind_info.bindCount = image_bind_count;
            image_bind_info.pBinds = image_binds;

            bind_info.imageBindCount = 1;
            bind_info.pImageBinds = &image_bind_info;
        }

        if (opaque_bind_count)
        {
            memset(&opaque_bind_info, 0, sizeof(opaque_bind_info));
            opaque_bind_info.image = resource->res.vk_image;
            opaque_bind_info.bindCount = opaque_bind_count;
            opaque_bind_info.pBinds = opaque_binds;

            bind_info.imageOpaqueBindCount = 1;
            bind_info.pImageOpaqueBinds = &opaque_bind_info;
        }
    }

    if (bind_info.bufferBindCount || bind_info.imageBindCount || bind_info.imageOpaqueBindCount)
    {
        vkd3d_queue = device->internal_sparse_queue;

        if (!(vk_queue = vkd3d_queue_acquire(vkd3d_queue)))
        {
            ERR("Failed to acquire queue %p.\n", vkd3d_queue);
            goto cleanup;
        }

        /* This timeline will only get signaled on the internal sparse queue */
        sparse->init_timeline_value = device->sparse_init_timeline_value + 1u;

        memset(&timeline_info, 0, sizeof(timeline_info));
        timeline_info.sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO;
        timeline_info.waitSemaphoreValueCount = 1;
        timeline_info.pWaitSemaphoreValues = &device->sparse_init_timeline_value;
        timeline_info.signalSemaphoreValueCount = 1;
        timeline_info.pSignalSemaphoreValues = &sparse->init_timeline_value;

        bind_info.pNext = &timeline_info;
        bind_info.waitSemaphoreCount = 1;
        bind_info.pWaitSemaphores = &device->sparse_init_timeline;
        bind_info.signalSemaphoreCount = 1;
        bind_info.pSignalSemaphores = &device->sparse_init_timeline;

        vr = VK_CALL(vkQueueBindSparse(vk_queue, 1, &bind_info, VK_NULL_HANDLE));

        device->sparse_init_timeline_value = sparse->init_timeline_value;
        vkd3d_queue_release(vkd3d_queue);

        if (vr < 0)
        {
            ERR("Failed to initialize sparse resource, vr %d.\n", vr);
            hr = hresult_from_vk_result(vr);
            goto cleanup;
        }

        /* The application is free to use or destroy the resource immediately
         * after creation. Stall subsequent queue submissions until the resource
         * is initialized. */
        vkd3d_add_wait_to_all_queues(device, device->sparse_init_timeline,
                resource->sparse.init_timeline_value);
    }

cleanup:
    vkd3d_free(sparse_requirements);
    vkd3d_free(opaque_binds);
    vkd3d_free(image_binds);
    return hr;
}

static HRESULT d3d12_resource_init_sparse_info(struct d3d12_resource *resource,
        struct d3d12_device *device, struct d3d12_sparse_info *sparse)
{
    VkSparseImageMemoryRequirements vk_memory_requirements;
    unsigned int i, subresource;
    VkOffset3D tile_offset;
    HRESULT hr;

    memset(sparse, 0, sizeof(*sparse));

    sparse->tiling_count = d3d12_resource_desc_get_sub_resource_count_per_plane(&resource->desc);
    sparse->tile_count = 0;

    if (!(sparse->tilings = vkd3d_malloc(sparse->tiling_count * sizeof(*sparse->tilings))))
    {
        ERR("Failed to allocate subresource tiling info array.\n");
        return E_OUTOFMEMORY;
    }

    d3d12_resource_get_tiling(device, resource, &sparse->tile_count, &sparse->packed_mips,
            &sparse->tile_shape, sparse->tilings, &vk_memory_requirements);

    if (!(sparse->tiles = vkd3d_malloc(sparse->tile_count * sizeof(*sparse->tiles))))
    {
        ERR("Failed to allocate tile mapping array.\n");
        return E_OUTOFMEMORY;
    }

    tile_offset.x = 0;
    tile_offset.y = 0;
    tile_offset.z = 0;
    subresource = 0;

    for (i = 0; i < sparse->tile_count; i++)
    {
        if (d3d12_resource_is_buffer(resource))
        {
            VkDeviceSize offset = VKD3D_TILE_SIZE * i;
            sparse->tiles[i].buffer.offset = offset;
            sparse->tiles[i].buffer.length = align(min(VKD3D_TILE_SIZE, resource->desc.Width - offset),
                    VKD3D_TILE_SIZE);
        }
        else if (sparse->packed_mips.NumPackedMips && i >= sparse->packed_mips.StartTileIndexInOverallResource)
        {
            VkDeviceSize offset = VKD3D_TILE_SIZE * (i - sparse->packed_mips.StartTileIndexInOverallResource);
            sparse->tiles[i].buffer.offset = vk_memory_requirements.imageMipTailOffset + offset;
            sparse->tiles[i].buffer.length = align(min(VKD3D_TILE_SIZE, vk_memory_requirements.imageMipTailSize - offset),
                    VKD3D_TILE_SIZE);
        }
        else
        {
            struct d3d12_sparse_image_region *region = &sparse->tiles[i].image;
            VkExtent3D block_extent = vk_memory_requirements.formatProperties.imageGranularity;
            VkImageSubresourceLayers vk_subresource_layers;
            VkExtent3D mip_extent;

            assert(subresource < sparse->tiling_count && sparse->tilings[subresource].WidthInTiles &&
                    sparse->tilings[subresource].HeightInTiles && sparse->tilings[subresource].DepthInTiles);

            region->subresource.aspectMask = vk_memory_requirements.formatProperties.aspectMask;
            region->subresource.mipLevel = subresource % resource->desc.MipLevels;
            region->subresource.arrayLayer = subresource / resource->desc.MipLevels;
            region->subresource_index = subresource;

            region->offset.x = tile_offset.x * block_extent.width;
            region->offset.y = tile_offset.y * block_extent.height;
            region->offset.z = tile_offset.z * block_extent.depth;

            vk_subresource_layers = vk_subresource_layers_from_subresource(&region->subresource);
            mip_extent = d3d12_resource_desc_get_vk_subresource_extent(&resource->desc, NULL, &vk_subresource_layers);

            region->extent.width = min(block_extent.width, mip_extent.width - region->offset.x);
            region->extent.height = min(block_extent.height, mip_extent.height - region->offset.y);
            region->extent.depth = min(block_extent.depth, mip_extent.depth - region->offset.z);

            if (++tile_offset.x == (int32_t)sparse->tilings[subresource].WidthInTiles)
            {
                tile_offset.x = 0;
                if (++tile_offset.y == (int32_t)sparse->tilings[subresource].HeightInTiles)
                {
                    tile_offset.y = 0;
                    if (++tile_offset.z == (int32_t)sparse->tilings[subresource].DepthInTiles)
                    {
                        tile_offset.z = 0;

                        /* Find next subresource that is not part of the packed mip tail */
                        while ((++subresource % resource->desc.MipLevels) >= sparse->packed_mips.NumStandardMips)
                            continue;
                    }
                }
            }
        }

        sparse->tiles[i].vk_memory = VK_NULL_HANDLE;
        sparse->tiles[i].vk_offset = 0;
    }

    if (FAILED(hr = d3d12_resource_init_page_table(resource, device, sparse)))
        return hr;

    return S_OK;
}

static void d3d12_resource_wait_for_sparse_init(struct d3d12_resource *resource)
{
    const struct vkd3d_vk_device_procs *vk_procs = &resource->device->vk_procs;
    VkSemaphoreWaitInfo semaphore_wait;
    VkResult vr;

    if (!(resource->sparse.init_timeline_value))
        return;

    memset(&semaphore_wait, 0, sizeof(semaphore_wait));
    semaphore_wait.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
    semaphore_wait.semaphoreCount = 1;
    semaphore_wait.pSemaphores = &resource->device->sparse_init_timeline;
    semaphore_wait.pValues = &resource->sparse.init_timeline_value;

    if ((vr = VK_CALL(vkWaitSemaphores(resource->device->vk_device, &semaphore_wait, UINT64_MAX))))
        ERR("Failed to wait for timeline semaphore, vr %d.\n", vr);
}

static void d3d12_resource_destroy(struct d3d12_resource *resource, struct d3d12_device *device)
{
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;

    if (resource->flags & VKD3D_RESOURCE_RESERVED)
        d3d12_resource_wait_for_sparse_init(resource);

    d3d_destruction_notifier_free(&resource->destruction_notifier);

    vkd3d_view_map_destroy(&resource->view_map, resource->device);

    vkd3d_descriptor_debug_unregister_cookie(device->descriptor_qa_global_info, resource->res.cookie);

    if (resource->flags & VKD3D_RESOURCE_EXTERNAL)
        return;

    if (resource->flags & VKD3D_RESOURCE_RESERVED)
    {
        vkd3d_free_device_memory(device, &resource->sparse.vk_metadata_memory);
        vkd3d_free(resource->sparse.tiles);
        vkd3d_free(resource->sparse.tilings);

        if (resource->res.va)
            vkd3d_va_map_remove(&device->memory_allocator.va_map, &resource->res);
    }

    if (d3d12_resource_is_texture(resource))
        VK_CALL(vkDestroyImage(device->vk_device, resource->res.vk_image, NULL));
    else if (resource->flags & VKD3D_RESOURCE_RESERVED)
        VK_CALL(vkDestroyBuffer(device->vk_device, resource->res.vk_buffer, NULL));

    if ((resource->flags & VKD3D_RESOURCE_ALLOCATION) && resource->mem.device_allocation.vk_memory)
        vkd3d_free_memory(device, &device->memory_allocator, &resource->mem);

    if (resource->flags & VKD3D_RESOURCE_LINEAR_STAGING_COPY)
    {
        if (resource->private_mem.device_allocation.vk_memory)
            vkd3d_free_memory(device, &device->memory_allocator, &resource->private_mem);

        vkd3d_free(resource->subresource_layouts);
    }

    if (resource->vrs_view)
        VK_CALL(vkDestroyImageView(device->vk_device, resource->vrs_view, NULL));

    vkd3d_private_store_destroy(&resource->private_store);
    if (resource->heap)
        d3d12_heap_decref(resource->heap);
    vkd3d_free(resource);
}

static void d3d12_resource_destroy_and_release_device(struct d3d12_resource *resource,
        struct d3d12_device *device)
{
    d3d12_resource_destroy(resource, device);
    d3d12_device_release(device);
}

static HRESULT d3d12_resource_create_vk_resource(struct d3d12_resource *resource,
        UINT num_castable_formats, const DXGI_FORMAT *castable_formats,
        struct d3d12_device *device)
{
    const D3D12_HEAP_PROPERTIES *heap_properties;
    HRESULT hr;

    heap_properties = resource->flags & VKD3D_RESOURCE_RESERVED
        ? NULL : &resource->heap_properties;

    if (resource->desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER)
    {
        if (FAILED(hr = vkd3d_create_buffer(device, heap_properties,
                D3D12_HEAP_FLAG_NONE, &resource->desc,
                (resource->flags & VKD3D_RESOURCE_RESERVED) ? "sparse-buffer" : "user-buffer",
                &resource->res.vk_buffer)))
            return hr;

        if (vkd3d_address_binding_tracker_active(&device->address_binding_tracker))
        {
            vkd3d_address_binding_tracker_assign_cookie(&device->address_binding_tracker,
                    VK_OBJECT_TYPE_BUFFER, (uint64_t)resource->res.vk_buffer, resource->res.cookie);
        }
    }
    else
    {
        resource->initial_layout_transition = 1;

        if (!resource->desc.MipLevels)
            resource->desc.MipLevels = max_miplevel_count(&resource->desc);

        if (FAILED(hr = vkd3d_create_image(device, heap_properties,
                D3D12_HEAP_FLAG_NONE, &resource->desc, resource,
                num_castable_formats, castable_formats,
                &resource->res.vk_image)))
            return hr;

        if (vkd3d_address_binding_tracker_active(&device->address_binding_tracker))
        {
            vkd3d_address_binding_tracker_assign_cookie(&device->address_binding_tracker,
                    VK_OBJECT_TYPE_IMAGE, (uint64_t) resource->res.vk_image, resource->res.cookie);
        }
    }

    return S_OK;
}

static size_t vkd3d_compute_resource_layouts_from_desc(struct d3d12_device *device,
        const D3D12_RESOURCE_DESC1 *desc, struct vkd3d_subresource_layout *layouts)
{
    struct vkd3d_format_footprint format_footprint;
    const struct vkd3d_format *format;
    unsigned int i, subresource_count;
    VkExtent3D block_count;
    uint32_t plane_idx;
    size_t offset = 0;

    subresource_count = d3d12_resource_desc_get_sub_resource_count(device, desc);
    format = vkd3d_format_from_d3d12_resource_desc(device, desc, 0);

    for (i = 0; i < subresource_count; i++)
    {
        plane_idx = i / d3d12_resource_desc_get_sub_resource_count_per_plane(desc);
        format_footprint = vkd3d_format_footprint_for_plane(format, plane_idx);

        block_count = d3d12_resource_desc_get_subresource_extent(desc, format, i);
        block_count.width = align(block_count.width, format_footprint.block_width) / format_footprint.block_width;
        block_count.height = align(block_count.height, format_footprint.block_height) / format_footprint.block_height;

        if (layouts)
        {
            layouts[i].offset = offset;
            layouts[i].row_pitch = block_count.width * format_footprint.block_byte_count;
            layouts[i].depth_pitch = block_count.height * layouts[i].row_pitch;
        }

        offset += block_count.width * block_count.height * block_count.depth * format_footprint.block_byte_count;
    }

    return offset;
}

static size_t d3d12_resource_init_subresource_layouts(struct d3d12_resource *resource, struct d3d12_device *device)
{
    unsigned int subresource_count = d3d12_resource_get_sub_resource_count(resource);

    resource->subresource_layouts = vkd3d_calloc(subresource_count, sizeof(*resource->subresource_layouts));
    return vkd3d_compute_resource_layouts_from_desc(device, &resource->desc, resource->subresource_layouts);
}

static HRESULT d3d12_resource_create(struct d3d12_device *device, uint32_t flags,
        const D3D12_RESOURCE_DESC1 *desc, const D3D12_HEAP_PROPERTIES *heap_properties,
        D3D12_HEAP_FLAGS heap_flags, D3D12_RESOURCE_STATES initial_state,
        const D3D12_CLEAR_VALUE *optimized_clear_value,
        UINT num_castable_formats, const DXGI_FORMAT *castable_formats,
        struct d3d12_resource **resource)
{
    const D3D12_RESOURCE_FLAGS high_priority_resource_flags =
        D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET |
        D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL |
        D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    struct d3d12_resource *object;
    HRESULT hr;

    if (FAILED(hr = d3d12_resource_validate_create_info(desc,
            heap_properties, initial_state, optimized_clear_value,
            num_castable_formats, castable_formats, device)))
        return hr;

    if (!(object = vkd3d_malloc(sizeof(*object))))
        return E_OUTOFMEMORY;

    memset(object, 0, sizeof(*object));
    object->ID3D12Resource_iface.lpVtbl = &d3d12_resource_vtbl;

    if (FAILED(hr = vkd3d_view_map_init(&object->view_map)))
    {
        vkd3d_free(object);
        return hr;
    }

    if (FAILED(hr = vkd3d_private_store_init(&object->private_store)))
    {
        vkd3d_view_map_destroy(&object->view_map, device);
        vkd3d_free(object);
        return hr;
    }

    d3d_destruction_notifier_init(&object->destruction_notifier, (IUnknown*)&object->ID3D12Resource_iface);

    object->refcount = 1;
    object->internal_refcount = 1;
    object->desc = *desc;
    object->device = device;
    object->flags = flags;
    object->format = vkd3d_format_from_d3d12_resource_desc(device, desc, 0);
    object->res.cookie = vkd3d_allocate_cookie();
    spinlock_init(&object->priority.spinlock);
    object->priority.allows_dynamic_residency = false;
    object->priority.d3d12priority = D3D12_RESIDENCY_PRIORITY_NORMAL;
    object->priority.residency_count = 1;
#ifdef VKD3D_ENABLE_DESCRIPTOR_QA
    object->view_map.resource_cookie = object->res.cookie;
#endif

    /* RTAS are "special" buffers. They can never transition out of this state. */
    if (initial_state == D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE)
        object->flags |= VKD3D_RESOURCE_ACCELERATION_STRUCTURE;
    object->initial_state = initial_state;

    if (heap_properties)
        object->heap_properties = *heap_properties;
    object->heap_flags = heap_flags;

    /* Compute subresource layouts for CPU-accessible images. CPU-accessible
     * heaps cannot be shared, so we do not need to consider that possibility. */
    if (!(flags & VKD3D_RESOURCE_RESERVED) && d3d12_resource_is_texture(object) &&
            is_cpu_accessible_heap(heap_properties))
    {
        const UINT unsupported_flags = D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS | D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

        object->flags |= VKD3D_RESOURCE_LINEAR_STAGING_COPY | VKD3D_RESOURCE_GENERAL_LAYOUT;
        d3d12_resource_init_subresource_layouts(object, device);

        if ((desc->Flags & unsupported_flags) == unsupported_flags)
            FIXME_ONCE("ReadFromSubresource may be buggy on host-visible images with ALLOW_SIMULTANEOUS_ACCESS | ALLOW_UNORDERED_ACCESS.\n");
    }

    if ((flags & VKD3D_RESOURCE_COMMITTED) &&
        device->device_info.memory_priority_features.memoryPriority &&
        (desc->Flags & high_priority_resource_flags))
    {
        size_t resource_size = d3d12_resource_is_texture(object) ?
            vkd3d_compute_resource_layouts_from_desc(device, &object->desc, NULL) :
            object->desc.Width;
        uint32_t adjust = vkd3d_get_priority_adjust(resource_size);

        object->priority.d3d12priority = D3D12_RESIDENCY_PRIORITY_HIGH | adjust;

        if (device->device_info.pageable_device_memory_features.pageableDeviceLocalMemory)
        {
            if (object->heap_flags & D3D12_HEAP_FLAG_CREATE_NOT_RESIDENT)
                object->priority.residency_count = 0;
        }
    }

    d3d12_device_add_ref(device);

    vkd3d_descriptor_debug_register_resource_cookie(device->descriptor_qa_global_info,
            object->res.cookie, desc);

    *resource = object;
    return S_OK;
}

static void d3d12_resource_tag_debug_name(struct d3d12_resource *resource,
        struct d3d12_device *device, const char *tag)
{
    char name_buffer[1024];
    snprintf(name_buffer, sizeof(name_buffer), "%s (cookie %"PRIu64")", tag, resource->res.cookie);

    if (d3d12_resource_is_texture(resource))
        vkd3d_set_vk_object_name(device, (uint64_t)resource->res.vk_image, VK_OBJECT_TYPE_IMAGE, name_buffer);
    else if (d3d12_resource_is_buffer(resource))
        vkd3d_set_vk_object_name(device, (uint64_t)resource->res.vk_buffer, VK_OBJECT_TYPE_BUFFER, name_buffer);
}

HRESULT d3d12_resource_create_borrowed(struct d3d12_device *device, const D3D12_RESOURCE_DESC1 *desc,
        UINT64 vk_handle, struct d3d12_resource **resource)
{
    D3D12_HEAP_PROPERTIES heap_props;
    struct d3d12_resource *object;
    HRESULT hr;
    if (desc->Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D)
    {
        FIXME("Only creation of Texture2D resources are currently supported.\n");
        return E_NOTIMPL;
    }

    heap_props.Type = D3D12_HEAP_TYPE_DEFAULT;
    heap_props.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heap_props.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    heap_props.CreationNodeMask = 0;
    heap_props.VisibleNodeMask = 0;

    hr = d3d12_resource_create(device, VKD3D_RESOURCE_PLACED | VKD3D_RESOURCE_EXTERNAL,
            desc, &heap_props, D3D12_HEAP_FLAG_SHARED, D3D12_RESOURCE_STATE_COMMON, NULL, 0, NULL, &object);
    if (FAILED(hr))
        return hr;

    object->res.vk_image = (VkImage)vk_handle;
    if (!object->desc.MipLevels)
        object->desc.MipLevels = max_miplevel_count(desc);
    object->common_layout = vk_common_image_layout_from_d3d12_desc(device, desc);
    *resource = object;
    return hr;
}

HRESULT d3d12_resource_create_committed(struct d3d12_device *device, const D3D12_RESOURCE_DESC1 *desc,
        const D3D12_HEAP_PROPERTIES *heap_properties, D3D12_HEAP_FLAGS heap_flags, D3D12_RESOURCE_STATES initial_state,
        const D3D12_CLEAR_VALUE *optimized_clear_value,
        UINT num_castable_formats, const DXGI_FORMAT *castable_formats,
        HANDLE shared_handle, struct d3d12_resource **resource)
{
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    struct d3d12_resource *object;
    HRESULT hr;

    if (FAILED(hr = d3d12_resource_create(device, VKD3D_RESOURCE_COMMITTED | VKD3D_RESOURCE_ALLOCATION,
            desc, heap_properties, heap_flags, initial_state, optimized_clear_value,
            num_castable_formats, castable_formats,
            &object)))
        return hr;

    if (d3d12_resource_is_texture(object))
    {
        VkMemoryDedicatedRequirements dedicated_requirements;
        struct vkd3d_allocate_memory_info allocate_info;
        VkMemoryDedicatedAllocateInfo dedicated_info;
        struct vkd3d_memory_allocation *allocation;
        VkImageMemoryRequirementsInfo2 image_info;
        VkMemoryRequirements2 memory_requirements;
        VkBindImageMemoryInfo bind_info;
        VkResult vr;

#ifdef _WIN32
        VkImportMemoryWin32HandleInfoKHR import_info;
        VkExportMemoryAllocateInfo export_info;
#endif

        if (FAILED(hr = d3d12_resource_create_vk_resource(object, num_castable_formats, castable_formats, device)))
            goto fail;

        image_info.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_REQUIREMENTS_INFO_2;
        image_info.pNext = NULL;
        image_info.image = object->res.vk_image;

        memset(&dedicated_requirements, 0, sizeof(dedicated_requirements));
        dedicated_requirements.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS;

        memory_requirements.sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2;
        memory_requirements.pNext = &dedicated_requirements;

        VK_CALL(vkGetImageMemoryRequirements2(device->vk_device, &image_info, &memory_requirements));

        memset(&allocate_info, 0, sizeof(allocate_info));
        allocate_info.memory_requirements = memory_requirements.memoryRequirements;
        allocate_info.heap_flags = heap_flags;
        allocate_info.explicit_global_buffer_usage = 0;

        if (object->flags & VKD3D_RESOURCE_LINEAR_STAGING_COPY)
        {
            assert(!(heap_flags & D3D12_HEAP_FLAG_SHARED));
            /* For host-visible images, allocate the actual image resource in video memory */
            allocate_info.heap_properties.Type = D3D12_HEAP_TYPE_DEFAULT;
            allocation = &object->private_mem;
        }
        else
        {
            allocate_info.heap_properties = *heap_properties;
            allocation = &object->mem;
        }

        if (vkd3d_allocate_image_memory_prefers_dedicated(device, heap_flags, &allocate_info.memory_requirements))
            dedicated_requirements.prefersDedicatedAllocation = VK_TRUE;

        if (desc->Flags & (D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL))
            allocate_info.heap_flags |= D3D12_HEAP_FLAG_ALLOW_ONLY_RT_DS_TEXTURES;
        else
            allocate_info.heap_flags |= D3D12_HEAP_FLAG_ALLOW_ONLY_NON_RT_DS_TEXTURES;

        allocate_info.vk_memory_priority = object->priority.residency_count ? vkd3d_convert_to_vk_prio(object->priority.d3d12priority) : 0.f;

        if (heap_flags & D3D12_HEAP_FLAG_SHARED)
        {
#ifdef _WIN32
            dedicated_requirements.prefersDedicatedAllocation = VK_TRUE;

            if (shared_handle && shared_handle != INVALID_HANDLE_VALUE)
            {
                import_info.sType = VK_STRUCTURE_TYPE_IMPORT_MEMORY_WIN32_HANDLE_INFO_KHR;
                import_info.pNext = allocate_info.pNext;
                import_info.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT;
                import_info.handle = shared_handle;
                import_info.name = NULL;
                allocate_info.pNext = &import_info;
            }
            else
            {
                export_info.sType = VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO;
                export_info.pNext = allocate_info.pNext;
                export_info.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT;
                allocate_info.pNext = &export_info;
            }
#else
            FIXME("D3D12_HEAP_FLAG_SHARED can only be implemented in native Win32.\n");
#endif
        }

        /* Requires implies that prefers is also set by spec. */
        if (dedicated_requirements.prefersDedicatedAllocation)
        {
            dedicated_info.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO;
            dedicated_info.pNext = allocate_info.pNext;
            dedicated_info.image = object->res.vk_image;
            dedicated_info.buffer = VK_NULL_HANDLE;
            allocate_info.pNext = &dedicated_info;
            allocate_info.flags = VKD3D_ALLOCATION_FLAG_DEDICATED;
        }
        else
        {
            /* We want to allow suballocations and we need the allocation to
             * be cleared to zero, which only works if we allow buffers */
            allocate_info.flags = VKD3D_ALLOCATION_FLAG_GLOBAL_BUFFER |
                    VKD3D_ALLOCATION_FLAG_ALLOW_IMAGE_SUBALLOCATION;

            /* If image suballocation is allowed force the memory alignment to respect
             * the device buffer image granularity to prevent resource aliasing */
            allocate_info.memory_requirements.alignment = max(allocate_info.memory_requirements.alignment,
                    device->device_info.properties2.properties.limits.bufferImageGranularity);

            /* For suballocations, we only care about being able to clear the memory,
             * not anything else. */
            allocate_info.explicit_global_buffer_usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        }

        if (FAILED(hr = vkd3d_allocate_memory(device, &device->memory_allocator, &allocate_info, allocation)))
            goto fail;

        bind_info.sType = VK_STRUCTURE_TYPE_BIND_IMAGE_MEMORY_INFO;
        bind_info.pNext = NULL;
        bind_info.image = object->res.vk_image;
        bind_info.memory = allocation->device_allocation.vk_memory;
        bind_info.memoryOffset = allocation->offset;

        if ((vr = VK_CALL(vkBindImageMemory2(device->vk_device, 1, &bind_info))))
        {
            ERR("Failed to bind image memory, vr %d.\n", vr);
            hr = hresult_from_vk_result(vr);
            goto fail;
        }

        if (vkd3d_resource_can_be_vrs(device, heap_properties, desc))
        {
            /* Make the implicit VRS view here... */
            if (FAILED(hr = vkd3d_resource_make_vrs_view(device, object->res.vk_image, &object->vrs_view)))
                goto fail;
        }

        if (object->flags & VKD3D_RESOURCE_LINEAR_STAGING_COPY)
        {
            memset(&allocate_info, 0, sizeof(allocate_info));
            allocate_info.memory_requirements.size = vkd3d_compute_resource_layouts_from_desc(device, desc, NULL);
            allocate_info.memory_requirements.alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
            allocate_info.memory_requirements.memoryTypeBits = ~0u;
            allocate_info.heap_properties = *heap_properties;
            allocate_info.heap_flags = D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS;
            allocate_info.flags = VKD3D_ALLOCATION_FLAG_GLOBAL_BUFFER;

            if (FAILED(hr = vkd3d_allocate_memory(device, &device->memory_allocator, &allocate_info, &object->mem)))
                goto fail;
        }

        if (vkd3d_config_flags & VKD3D_CONFIG_FLAG_DEBUG_UTILS)
        {
            d3d12_resource_tag_debug_name(object, device,
                    (heap_flags & D3D12_HEAP_FLAG_CREATE_NOT_ZEROED) ?
                    "Committed Texture (not-zeroed)" : "Committed Texture (zeroed)");
        }
    }
    else
    {
        struct vkd3d_allocate_heap_memory_info allocate_info;

        memset(&allocate_info, 0, sizeof(allocate_info));
        allocate_info.heap_desc.Properties = *heap_properties;
        allocate_info.heap_desc.Alignment = desc->Alignment ? desc->Alignment : D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
        allocate_info.heap_desc.SizeInBytes = align(desc->Width, allocate_info.heap_desc.Alignment);
        allocate_info.heap_desc.Flags = heap_flags | D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS;
        allocate_info.vk_memory_priority = object->priority.residency_count ? vkd3d_convert_to_vk_prio(object->priority.d3d12priority) : 0.f;

        /* Be very careful with suballocated buffers. */
        if ((vkd3d_config_flags & VKD3D_CONFIG_FLAG_ZERO_MEMORY_WORKAROUNDS_COMMITTED_BUFFER_UAV) &&
                (desc->Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS) &&
                desc->Width < VKD3D_VA_BLOCK_SIZE)
        {
            allocate_info.heap_desc.Flags &= ~D3D12_HEAP_FLAG_CREATE_NOT_ZEROED;
        }

        if (FAILED(hr = vkd3d_allocate_heap_memory(device,
                &device->memory_allocator, &allocate_info, &object->mem)))
            goto fail;

        object->res.vk_buffer = object->mem.resource.vk_buffer;
        object->res.va = object->mem.resource.va;
    }

    object->priority.allows_dynamic_residency = 
        device->device_info.pageable_device_memory_features.pageableDeviceLocalMemory &&
        object->mem.chunk == NULL /* not suballocated */ &&
        (device->memory_properties.memoryTypes[object->mem.device_allocation.vk_memory_type].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    vkd3d_queue_timeline_trace_register_instantaneous(&device->queue_timeline_trace,
            VKD3D_QUEUE_TIMELINE_TRACE_STATE_TYPE_COMMITTED_RESOURCE_ALLOCATION, object->res.cookie);

    *resource = object;
    return S_OK;

fail:
    d3d12_resource_destroy_and_release_device(object, device);
    return hr;
}

static HRESULT d3d12_resource_validate_heap(const D3D12_RESOURCE_DESC1 *resource_desc, struct d3d12_heap *heap)
{
    D3D12_HEAP_FLAGS deny_flag;

    if (resource_desc->Dimension == D3D12_RESOURCE_DIMENSION_BUFFER)
        deny_flag = D3D12_HEAP_FLAG_DENY_BUFFERS;
    else if (resource_desc->Flags & (D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL))
        deny_flag = D3D12_HEAP_FLAG_DENY_RT_DS_TEXTURES;
    else
        deny_flag = D3D12_HEAP_FLAG_DENY_NON_RT_DS_TEXTURES;

    if (heap->desc.Flags & deny_flag)
    {
        WARN("Cannot create placed resource on heap that denies resource category %#x.\n", deny_flag);
        return E_INVALIDARG;
    }

    if ((heap->desc.Flags & D3D12_HEAP_FLAG_SHARED_CROSS_ADAPTER) &&
            !(resource_desc->Flags & D3D12_RESOURCE_FLAG_ALLOW_CROSS_ADAPTER))
    {
        ERR("Must declare ALLOW_CROSS_ADAPTER resource flag when heap is cross adapter.\n");
        return E_INVALIDARG;
    }

    return S_OK;
}

HRESULT d3d12_resource_create_placed(struct d3d12_device *device, const D3D12_RESOURCE_DESC1 *desc,
        struct d3d12_heap *heap, uint64_t heap_offset, D3D12_RESOURCE_STATES initial_state,
        const D3D12_CLEAR_VALUE *optimized_clear_value,
        UINT num_castable_formats, const DXGI_FORMAT *castable_formats,
        struct d3d12_resource **resource)
{
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    struct vkd3d_allocate_memory_info allocate_info;
    VkMemoryRequirements memory_requirements;
    VkBindImageMemoryInfo bind_info;
    struct d3d12_resource *object;
    VkResult vr;
    HRESULT hr;

    if (FAILED(hr = d3d12_resource_validate_heap(desc, heap)))
        return hr;

    if (heap->allocation.device_allocation.vk_memory == VK_NULL_HANDLE)
    {
        WARN("Placing resource on heap with no memory backing it. Falling back to committed resource.\n");

        if (FAILED(hr = d3d12_resource_create_committed(device, desc, &heap->desc.Properties,
                heap->desc.Flags & ~(D3D12_HEAP_FLAG_DENY_BUFFERS |
                        D3D12_HEAP_FLAG_DENY_NON_RT_DS_TEXTURES |
                        D3D12_HEAP_FLAG_DENY_RT_DS_TEXTURES),
                initial_state, optimized_clear_value,
                num_castable_formats, castable_formats,
                NULL, resource)))
        {
            ERR("Failed to create fallback committed resource.\n");
        }
        return hr;
    }

    if (FAILED(hr = d3d12_resource_create(device, VKD3D_RESOURCE_PLACED, desc,
            &heap->desc.Properties, heap->desc.Flags, initial_state, optimized_clear_value,
            num_castable_formats, castable_formats,
            &object)))
        return hr;

    /* https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12device-createheap#remarks.
     * Placed resources hold a reference on the heap. */
    d3d12_heap_incref(object->heap = heap);

    if (d3d12_resource_is_texture(object))
    {
        if (FAILED(hr = d3d12_resource_create_vk_resource(object, num_castable_formats, castable_formats, device)))
            goto fail;

        /* Align manually. This works because we padded the required allocation size reported to the app. */
        VK_CALL(vkGetImageMemoryRequirements(device->vk_device, object->res.vk_image, &memory_requirements));

        /* For SMALL_RESOURCE_PLACEMENT when we have workaround active,
         * verify that application did in fact check alignment requirements.
         * If application places 64k aligned, we will have made sure to leave room for padding below.
         * See vkd3d_get_image_allocation_info() for details. */
        if ((heap_offset & (D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT - 1)) &&
                (heap_offset & (memory_requirements.alignment - 1)) &&
                (vkd3d_config_flags & VKD3D_CONFIG_FLAG_REJECT_PADDED_SMALL_RESOURCE_ALIGNMENT))
        {
            /* Unclear if this is our bug or app bug, so FIXME seems appropriate. */
            FIXME("Application attempts to place small aligned resource at heap offset %"PRIu64", but it is not possible (requirement %u).\n",
                    heap_offset, (unsigned int)memory_requirements.alignment);
            hr = E_INVALIDARG;
            goto fail;
        }

        heap_offset = align(heap->allocation.offset + heap_offset, memory_requirements.alignment) - heap->allocation.offset;

        if (heap_offset + memory_requirements.size > heap->allocation.resource.size)
        {
            ERR("Heap too small for the texture (heap=%"PRIu64", res=%"PRIu64".\n",
                heap->allocation.resource.size, heap_offset + memory_requirements.size);
            hr = E_INVALIDARG;
            goto fail;
        }

        if (object->flags & VKD3D_RESOURCE_LINEAR_STAGING_COPY)
        {
            /* Packed linear size should never be greater than the image size */
            assert(vkd3d_compute_resource_layouts_from_desc(device, desc, NULL) <= memory_requirements.size);

            /* Allocate video memory for the actual image resource */
            memset(&allocate_info, 0, sizeof(allocate_info));
            allocate_info.memory_requirements = memory_requirements;
            allocate_info.heap_properties.Type = D3D12_HEAP_TYPE_DEFAULT;
            allocate_info.heap_flags = 0;
            allocate_info.vk_memory_priority = vkd3d_convert_to_vk_prio(D3D12_RESIDENCY_PRIORITY_NORMAL);

            if (desc->Flags & (D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL))
                allocate_info.heap_flags |= D3D12_HEAP_FLAG_ALLOW_ONLY_RT_DS_TEXTURES;
            else
                allocate_info.heap_flags |= D3D12_HEAP_FLAG_ALLOW_ONLY_NON_RT_DS_TEXTURES;

            if (FAILED(hr = vkd3d_allocate_memory(device, &device->memory_allocator, &allocate_info, &object->private_mem)))
            {
                hr = E_OUTOFMEMORY;
                goto fail;
            }
        }
    }
    else
    {
        if (heap_offset + desc->Width > heap->allocation.resource.size)
        {
            ERR("Heap too small for the buffer (heap=%"PRIu64", res=%"PRIu64".\n",
                heap->allocation.resource.size, heap_offset + desc->Width);
            hr = E_INVALIDARG;
            goto fail;
        }
    }

    vkd3d_memory_allocation_slice(&object->mem, &heap->allocation, heap_offset, 0);

    if (d3d12_resource_is_texture(object))
    {
        bind_info.sType = VK_STRUCTURE_TYPE_BIND_IMAGE_MEMORY_INFO;
        bind_info.pNext = NULL;
        bind_info.image = object->res.vk_image;

        if (object->flags & VKD3D_RESOURCE_LINEAR_STAGING_COPY)
        {
            bind_info.memory = object->private_mem.device_allocation.vk_memory;
            bind_info.memoryOffset = object->private_mem.offset;
        }
        else
        {
            bind_info.memory = object->mem.device_allocation.vk_memory;
            bind_info.memoryOffset = object->mem.offset;
        }

        if ((vr = VK_CALL(vkBindImageMemory2(device->vk_device, 1, &bind_info))) < 0)
        {
            ERR("Failed to bind image memory, vr %d.\n", vr);
            hr = hresult_from_vk_result(vr);
            goto fail;
        }

        if (vkd3d_config_flags & VKD3D_CONFIG_FLAG_DEBUG_UTILS)
            d3d12_resource_tag_debug_name(object, device, "Placed Texture");
    }
    else
    {
        object->res.vk_buffer = object->mem.resource.vk_buffer;
        object->res.va = object->mem.resource.va;
    }

    if (vkd3d_resource_can_be_vrs(device, &heap->desc.Properties, desc))
    {
        /* Make the implicit VRS view here... */
        if (FAILED(hr = vkd3d_resource_make_vrs_view(device, object->res.vk_image, &object->vrs_view)))
            goto fail;
    }

    /* Placed RTV and DSV *must* be explicitly initialized after alias barriers and first use,
     * so there is no need to do initial layout transition ourselves.
     * It is extremely dangerous to do so since the initialization will clobber other
     * aliased buffers when clearing DCC/HTILE state.
     * For details, see:
     * https://docs.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12device-createplacedresource#notes-on-the-required-resource-initialization. */
    if (desc->Flags & (D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL))
    {
        if (!(vkd3d_config_flags & VKD3D_CONFIG_FLAG_FORCE_INITIAL_TRANSITION))
        {
#ifdef VKD3D_ENABLE_BREADCRUMBS
            object->initial_layout_transition_validate_only = true;
#else
            object->initial_layout_transition = 0;
#endif
        }
    }

    *resource = object;
    return S_OK;

fail:
    d3d12_resource_destroy_and_release_device(object, device);
    return hr;
}

static HRESULT d3d12_resource_create_reserved_fallback(
        struct d3d12_device *device,
        const D3D12_RESOURCE_DESC1 *desc, D3D12_RESOURCE_STATES initial_state,
        const D3D12_CLEAR_VALUE *optimized_clear_value,
        UINT num_castable_formats, const DXGI_FORMAT *castable_formats,
        struct d3d12_resource **resource)
{
    D3D12_HEAP_PROPERTIES heap_props;
    struct d3d12_resource *object;
    HRESULT hr;

    memset(&heap_props, 0, sizeof(heap_props));
    heap_props.Type = D3D12_HEAP_TYPE_DEFAULT;

    if (FAILED(hr = d3d12_resource_create_committed(device, desc, &heap_props, D3D12_HEAP_FLAG_CREATE_NOT_ZEROED,
            initial_state, optimized_clear_value, num_castable_formats, castable_formats, NULL, &object)))
        return hr;

    if (FAILED(hr = d3d12_resource_init_sparse_info(object, device, &object->sparse)))
        goto fail;

    *resource = object;
    return S_OK;

fail:
    d3d12_resource_destroy_and_release_device(object, device);
    return hr;
}

HRESULT d3d12_resource_create_reserved(struct d3d12_device *device,
        const D3D12_RESOURCE_DESC1 *desc, D3D12_RESOURCE_STATES initial_state,
        const D3D12_CLEAR_VALUE *optimized_clear_value,
        UINT num_castable_formats, const DXGI_FORMAT *castable_formats,
        struct d3d12_resource **resource)
{
    const struct vkd3d_format *format;
    struct d3d12_resource *object;
    HRESULT hr;

    if (desc->Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D)
    {
        format = vkd3d_get_format(device, desc->Format, !!(desc->Flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL));
        if (!format)
            return E_INVALIDARG;

        /* Check that implementation supports sparse for the format at all.
         * RADV does not support depth-stencil sparse for example.
         * Letting this through is better than straight up crashing ...
         * Don't allow multi-aspect images. They have to fail. */
        if (vkd3d_popcount(format->vk_aspect_mask) == 1 &&
                !(format->supported_sparse_sample_counts & desc->SampleDesc.Count))
        {
            FIXME("Sparse is not supported for vk_format %d with %u samples, falling back to committed resource. "
                  "Dimensions: width %u, height %u, level %u, layers %u. VRAM bloat expected.\n",
                    format->vk_format, desc->SampleDesc.Count,
                    (unsigned int)desc->Width, desc->Height,
                    desc->MipLevels, desc->DepthOrArraySize);

            return d3d12_resource_create_reserved_fallback(device, desc, initial_state,
                    optimized_clear_value, num_castable_formats, castable_formats, resource);
        }
    }

    if (FAILED(hr = d3d12_resource_create(device, VKD3D_RESOURCE_RESERVED, desc,
            NULL, D3D12_HEAP_FLAG_NONE, initial_state, optimized_clear_value,
            num_castable_formats, castable_formats,
            &object)))
        return hr;

    if (FAILED(hr = d3d12_resource_create_vk_resource(object, num_castable_formats, castable_formats, device)))
        goto fail;

    if (FAILED(hr = d3d12_resource_init_sparse_info(object, device, &object->sparse)))
        goto fail;

    if (d3d12_resource_is_buffer(object))
    {
        object->res.size = adjust_sparse_buffer_size(object->desc.Width);
        object->res.va = vkd3d_get_buffer_device_address(device, object->res.vk_buffer);

        if (!object->res.va)
        {
            ERR("Failed to get VA for sparse resource.\n");
            return E_FAIL;
        }

        vkd3d_va_map_insert(&device->memory_allocator.va_map, &object->res);
    }

    if (vkd3d_config_flags & VKD3D_CONFIG_FLAG_DEBUG_UTILS)
        d3d12_resource_tag_debug_name(object, device, "Reserved Resource");

    *resource = object;
    return S_OK;

fail:
    d3d12_resource_destroy_and_release_device(object, device);
    return hr;
}

HRESULT vkd3d_create_image_resource(ID3D12Device *device,
        const struct vkd3d_image_resource_create_info *create_info, ID3D12Resource **resource)
{
    struct d3d12_device *d3d12_device = impl_from_ID3D12Device((d3d12_device_iface *)device);
    struct d3d12_resource *object;
    HRESULT hr;

    TRACE("device %p, create_info %p, resource %p.\n", device, create_info, resource);

    if (!create_info || !resource)
        return E_INVALIDARG;

    if (!(object = vkd3d_malloc(sizeof(*object))))
        return E_OUTOFMEMORY;

    memset(object, 0, sizeof(*object));

    object->ID3D12Resource_iface.lpVtbl = &d3d12_resource_vtbl;
    object->refcount = 1;
    object->internal_refcount = 1;
    object->res.vk_image = create_info->vk_image;
    object->flags = create_info->flags;
    object->flags |= VKD3D_RESOURCE_EXTERNAL;
    object->initial_layout_transition = 1;
    object->common_layout = vk_common_image_layout_from_d3d12_desc(d3d12_device, &object->desc);

    memset(&object->sparse, 0, sizeof(object->sparse));

    d3d12_resource_promote_desc(&create_info->desc, &object->desc);
    object->format = vkd3d_format_from_d3d12_resource_desc(d3d12_device, &object->desc, 0);

    if (FAILED(hr = vkd3d_view_map_init(&object->view_map)))
    {
        vkd3d_free(object);
        return hr;
    }

    if (FAILED(hr = vkd3d_private_store_init(&object->private_store)))
    {
        vkd3d_free(object);
        return hr;
    }

    d3d12_device_add_ref(object->device = d3d12_device);

    TRACE("Created resource %p.\n", object);

    *resource = (ID3D12Resource *)&object->ID3D12Resource_iface;

    return S_OK;
}

ULONG vkd3d_resource_incref(ID3D12Resource *resource)
{
    TRACE("resource %p.\n", resource);
    return d3d12_resource_incref(impl_from_ID3D12Resource(resource));
}

ULONG vkd3d_resource_decref(ID3D12Resource *resource)
{
    TRACE("resource %p.\n", resource);
    return d3d12_resource_decref(impl_from_ID3D12Resource(resource));
}

/* CBVs, SRVs, UAVs */
static struct vkd3d_view *vkd3d_view_create(enum vkd3d_view_type type)
{
    struct vkd3d_view *view;

    if ((view = vkd3d_malloc(sizeof(*view))))
    {
        view->refcount = 1;
        view->type = type;
        view->cookie = vkd3d_allocate_cookie();
    }
    return view;
}

void vkd3d_view_incref(struct vkd3d_view *view)
{
    InterlockedIncrement(&view->refcount);
}

static void vkd3d_view_destroy(struct vkd3d_view *view, struct d3d12_device *device)
{
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;

    TRACE("Destroying view %p.\n", view);

    vkd3d_descriptor_debug_unregister_cookie(device->descriptor_qa_global_info, view->cookie);

    switch (view->type)
    {
        case VKD3D_VIEW_TYPE_BUFFER:
            VK_CALL(vkDestroyBufferView(device->vk_device, view->vk_buffer_view, NULL));
            break;
        case VKD3D_VIEW_TYPE_IMAGE:
            VK_CALL(vkDestroyImageView(device->vk_device, view->vk_image_view, NULL));
            break;
        case VKD3D_VIEW_TYPE_SAMPLER:
            VK_CALL(vkDestroySampler(device->vk_device, view->vk_sampler, NULL));
            break;
        case VKD3D_VIEW_TYPE_ACCELERATION_STRUCTURE:
            VK_CALL(vkDestroyAccelerationStructureKHR(device->vk_device, view->vk_acceleration_structure, NULL));
            break;
        default:
            WARN("Unhandled view type %d.\n", view->type);
    }

    vkd3d_free(view);
}

void vkd3d_view_decref(struct vkd3d_view *view, struct d3d12_device *device)
{
    if (!InterlockedDecrement(&view->refcount))
        vkd3d_view_destroy(view, device);
}

void d3d12_desc_copy_single(vkd3d_cpu_descriptor_va_t dst_va, vkd3d_cpu_descriptor_va_t src_va,
        struct d3d12_device *device)
{
    VkCopyDescriptorSet vk_copies[VKD3D_MAX_BINDLESS_DESCRIPTOR_SETS];
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    const struct vkd3d_bindless_set_info *set_info;
    struct vkd3d_descriptor_binding binding;
    uint32_t set_mask, set_info_index;
    struct d3d12_desc_split src, dst;
    VkCopyDescriptorSet *vk_copy;
    uint32_t copy_count = 0;
    uint32_t flags;

    src = d3d12_desc_decode_va(src_va);
    dst = d3d12_desc_decode_va(dst_va);

    flags = src.view->info.flags;
    set_mask = src.types->set_info_mask;

    if (flags & VKD3D_DESCRIPTOR_FLAG_SINGLE_DESCRIPTOR)
    {
        /* Faster path, there is no need to fetch set info deep into the guts of d3d12_device,
         * and also, we don't get a dependency chain on the CTZ loop -> index, which causes OoO bubbles
         * it seems. */
        binding = src.types->single_binding;

        if (src.heap->sets[binding.set].copy_template_single)
        {
            src.heap->sets[binding.set].copy_template_single(
                    dst.heap->sets[binding.set].mapped_set,
                    src.heap->sets[binding.set].mapped_set,
                    dst.offset, src.offset);
        }
        else
        {
            vk_copy = &vk_copies[copy_count++];
            vk_copy->sType = VK_STRUCTURE_TYPE_COPY_DESCRIPTOR_SET;
            vk_copy->pNext = NULL;
            vk_copy->srcSet = src.heap->sets[binding.set].vk_descriptor_set;
            vk_copy->srcBinding = binding.binding;
            vk_copy->srcArrayElement = src.offset;
            vk_copy->dstSet = dst.heap->sets[binding.set].vk_descriptor_set;
            vk_copy->dstBinding = binding.binding;
            vk_copy->dstArrayElement = dst.offset;
            vk_copy->descriptorCount = 1;
        }
    }
    else
    {
        /* Need to copy multiple descriptors. CTZ loop. */
        while (set_mask)
        {
            set_info_index = vkd3d_bitmask_iter32(&set_mask);
            set_info = &device->bindless_state.set_info[set_info_index];

            if (set_info->host_copy_template_single)
            {
                set_info->host_copy_template_single(
                        dst.heap->sets[set_info->set_index].mapped_set,
                        src.heap->sets[set_info->set_index].mapped_set,
                        dst.offset, src.offset);
            }
            else
            {
                binding = vkd3d_bindless_state_binding_from_info_index(&device->bindless_state, set_info_index);

                vk_copy = &vk_copies[copy_count++];
                vk_copy->sType = VK_STRUCTURE_TYPE_COPY_DESCRIPTOR_SET;
                vk_copy->pNext = NULL;
                vk_copy->srcSet = src.heap->sets[binding.set].vk_descriptor_set;
                vk_copy->srcBinding = binding.binding;
                vk_copy->srcArrayElement = src.offset;
                vk_copy->dstSet = dst.heap->sets[binding.set].vk_descriptor_set;
                vk_copy->dstBinding = binding.binding;
                vk_copy->dstArrayElement = dst.offset;
                vk_copy->descriptorCount = 1;
            }
        }
    }

    if (flags & VKD3D_DESCRIPTOR_FLAG_RAW_VA_AUX_BUFFER)
    {
        const VkDeviceAddress *src_vas = src.heap->raw_va_aux_buffer.host_ptr;
        VkDeviceAddress *dst_vas = dst.heap->raw_va_aux_buffer.host_ptr;
        dst_vas[dst.offset] = src_vas[src.offset];
    }

    if (copy_count)
        VK_CALL(vkUpdateDescriptorSets(device->vk_device, 0, NULL, copy_count, vk_copies));

    if (flags & VKD3D_DESCRIPTOR_FLAG_BUFFER_OFFSET)
    {
        const struct vkd3d_bound_buffer_range *src_buffer_ranges = src.heap->buffer_ranges.host_ptr;
        struct vkd3d_bound_buffer_range *dst_buffer_ranges = dst.heap->buffer_ranges.host_ptr;
        dst_buffer_ranges[dst.offset] = src_buffer_ranges[src.offset];
    }

    *dst.types = *src.types;
    *dst.view = *src.view;
}

void d3d12_desc_copy_range(vkd3d_cpu_descriptor_va_t dst_va, vkd3d_cpu_descriptor_va_t src_va,
        unsigned int count, D3D12_DESCRIPTOR_HEAP_TYPE heap_type, struct d3d12_device *device)
{
    VkCopyDescriptorSet vk_copies[VKD3D_MAX_BINDLESS_DESCRIPTOR_SETS];
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    const struct vkd3d_bindless_set_info *set_info;
    struct vkd3d_descriptor_binding binding;
    struct d3d12_desc_split src, dst;
    VkCopyDescriptorSet *vk_copy;
    uint32_t set_info_mask = 0;
    uint32_t copy_count = 0;
    uint32_t set_info_index;
    unsigned int i;

    src = d3d12_desc_decode_va(src_va);
    dst = d3d12_desc_decode_va(dst_va);

    for (i = 0; i < count; i++)
        set_info_mask |= src.types[i].set_info_mask;

    memcpy(dst.view, src.view, sizeof(*dst.view) * count);
    memcpy(dst.types, src.types, sizeof(*dst.types) * count);

    while (set_info_mask)
    {
        set_info_index = vkd3d_bitmask_iter32(&set_info_mask);
        set_info = &device->bindless_state.set_info[set_info_index];

        if (set_info->host_copy_template)
        {
            set_info->host_copy_template(
                    dst.heap->sets[set_info->set_index].mapped_set,
                    src.heap->sets[set_info->set_index].mapped_set,
                    dst.offset, src.offset, count);
        }
        else
        {
            binding = vkd3d_bindless_state_binding_from_info_index(&device->bindless_state, set_info_index);

            vk_copy = &vk_copies[copy_count++];
            vk_copy->sType = VK_STRUCTURE_TYPE_COPY_DESCRIPTOR_SET;
            vk_copy->pNext = NULL;
            vk_copy->srcSet = src.heap->sets[binding.set].vk_descriptor_set;
            vk_copy->srcBinding = binding.binding;
            vk_copy->srcArrayElement = src.offset;
            vk_copy->dstSet = dst.heap->sets[binding.set].vk_descriptor_set;
            vk_copy->dstBinding = binding.binding;
            vk_copy->dstArrayElement = dst.offset;
            vk_copy->descriptorCount = count;
        }
    }

    if (heap_type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV)
    {
        const VkDeviceAddress *src_vas = src.heap->raw_va_aux_buffer.host_ptr;
        VkDeviceAddress *dst_vas = dst.heap->raw_va_aux_buffer.host_ptr;
        memcpy(dst_vas + dst.offset, src_vas + src.offset, sizeof(*dst_vas) * count);

        if (device->bindless_state.flags & (VKD3D_TYPED_OFFSET_BUFFER | VKD3D_SSBO_OFFSET_BUFFER))
        {
            const struct vkd3d_bound_buffer_range *src_ranges = src.heap->buffer_ranges.host_ptr;
            struct vkd3d_bound_buffer_range *dst_ranges = dst.heap->buffer_ranges.host_ptr;
            memcpy(dst_ranges + dst.offset, src_ranges + src.offset, sizeof(*dst_ranges) * count);
        }
    }

    if (copy_count)
        VK_CALL(vkUpdateDescriptorSets(device->vk_device, 0, NULL, copy_count, vk_copies));
}

void d3d12_desc_copy(vkd3d_cpu_descriptor_va_t dst_va, vkd3d_cpu_descriptor_va_t src_va,
        unsigned int count, D3D12_DESCRIPTOR_HEAP_TYPE heap_type, struct d3d12_device *device)
{
#ifdef VKD3D_ENABLE_DESCRIPTOR_QA
    if (!d3d12_device_use_embedded_mutable_descriptors(device))
    {
        struct d3d12_desc_split dst, src;
        unsigned int i;
        dst = d3d12_desc_decode_va(dst_va);
        src = d3d12_desc_decode_va(src_va);

        for (i = 0; i < count; i++)
        {
            vkd3d_descriptor_debug_copy_descriptor(
                    dst.heap->descriptor_heap_info.host_ptr, dst.heap->cookie, dst.offset + i,
                    src.heap->descriptor_heap_info.host_ptr, src.heap->cookie, src.offset + i,
                    src.view[i].qa_cookie);
        }
    }
#endif

    if (d3d12_device_use_embedded_mutable_descriptors(device))
    {
        /* Rare path. */
        if (heap_type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV)
        {
            d3d12_desc_copy_embedded_resource(dst_va, src_va,
                    device->bindless_state.descriptor_buffer_cbv_srv_uav_size * count);
        }
        else
        {
            vkd3d_memcpy_aligned_cached((void *)dst_va, (const void *)src_va,
                    device->bindless_state.descriptor_buffer_sampler_size * count);
        }
    }
    else
        d3d12_desc_copy_range(dst_va, src_va, count, heap_type, device);
}

bool vkd3d_create_raw_r32ui_vk_buffer_view(struct d3d12_device *device,
        VkBuffer vk_buffer, VkDeviceSize offset, VkDeviceSize range, VkBufferView *vk_view)
{
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    struct VkBufferViewCreateInfo view_desc;
    VkResult vr;

    if (offset % 4)
        FIXME("Offset %#"PRIx64" violates the required alignment 4.\n", offset);

    view_desc.sType = VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO;
    view_desc.pNext = NULL;
    view_desc.flags = 0;
    view_desc.buffer = vk_buffer;
    view_desc.format = VK_FORMAT_R32_UINT;
    view_desc.offset = offset;
    view_desc.range = range;
    if ((vr = VK_CALL(vkCreateBufferView(device->vk_device, &view_desc, NULL, vk_view))) < 0)
        WARN("Failed to create Vulkan buffer view, vr %d.\n", vr);
    return vr == VK_SUCCESS;
}

bool vkd3d_create_vk_buffer_view(struct d3d12_device *device,
        VkBuffer vk_buffer, const struct vkd3d_format *format,
        VkDeviceSize offset, VkDeviceSize range, VkBufferView *vk_view)
{
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    struct VkBufferViewCreateInfo view_desc;
    VkResult vr;

    if (vkd3d_format_is_compressed(format))
    {
        WARN("Invalid format for buffer view %#x.\n", format->dxgi_format);
        return false;
    }

    view_desc.sType = VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO;
    view_desc.pNext = NULL;
    view_desc.flags = 0;
    view_desc.buffer = vk_buffer;
    view_desc.format = format->vk_format;
    view_desc.offset = offset;
    view_desc.range = range;
    if ((vr = VK_CALL(vkCreateBufferView(device->vk_device, &view_desc, NULL, vk_view))) < 0)
        WARN("Failed to create Vulkan buffer view, vr %d.\n", vr);
    return vr == VK_SUCCESS;
}

bool vkd3d_create_buffer_view(struct d3d12_device *device, const struct vkd3d_buffer_view_desc *desc, struct vkd3d_view **view)
{
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    struct vkd3d_view *object;
    VkBufferView vk_view;

    if (!vkd3d_create_vk_buffer_view(device, desc->buffer, desc->format, desc->offset, desc->size, &vk_view))
        return false;

    if (!(object = vkd3d_view_create(VKD3D_VIEW_TYPE_BUFFER)))
    {
        VK_CALL(vkDestroyBufferView(device->vk_device, vk_view, NULL));
        return false;
    }

    object->vk_buffer_view = vk_view;
    object->format = desc->format;
    object->info.buffer.offset = desc->offset;
    object->info.buffer.size = desc->size;
    *view = object;
    return true;
}

bool vkd3d_create_acceleration_structure_view(struct d3d12_device *device, const struct vkd3d_buffer_view_desc *desc,
        struct vkd3d_view **view)
{
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    VkAccelerationStructureKHR vk_acceleration_structure;
    VkAccelerationStructureCreateInfoKHR create_info;
    VkDeviceAddress buffer_address;
    VkDeviceAddress rtas_address;
    struct vkd3d_view *object;
    VkResult vr;

    create_info.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
    create_info.pNext = NULL;
    create_info.type = VK_ACCELERATION_STRUCTURE_TYPE_GENERIC_KHR;
    create_info.createFlags = 0;
    create_info.deviceAddress = 0;
    create_info.buffer = desc->buffer;
    create_info.offset = desc->offset;
    create_info.size = desc->size;

    vr = VK_CALL(vkCreateAccelerationStructureKHR(device->vk_device, &create_info, NULL, &vk_acceleration_structure));
    if (vr != VK_SUCCESS)
        return false;

    if (!(object = vkd3d_view_create(VKD3D_VIEW_TYPE_ACCELERATION_STRUCTURE)))
    {
        VK_CALL(vkDestroyAccelerationStructureKHR(device->vk_device, vk_acceleration_structure, NULL));
        return false;
    }

    /* Sanity check. Spec should guarantee this.
     * There is a note in the spec for vkGetAccelerationStructureDeviceAddressKHR:
     * The acceleration structure device address may be different from the
     * buffer device address corresponding to the acceleration structure's
     * start offset in its storage buffer for acceleration structure types
     * other than VK_ACCELERATION_STRUCTURE_TYPE_GENERIC_KHR. */
    buffer_address = vkd3d_get_buffer_device_address(device, desc->buffer) + desc->offset;
    rtas_address = vkd3d_get_acceleration_structure_device_address(device, vk_acceleration_structure);
    if (buffer_address != rtas_address)
    {
        FIXME("buffer_address = 0x%"PRIx64", rtas_address = 0x%"PRIx64".\n", buffer_address, rtas_address);
    }

    object->vk_acceleration_structure = vk_acceleration_structure;
    object->format = desc->format;
    object->info.buffer.offset = desc->offset;
    object->info.buffer.size = desc->size;
    *view = object;
    return true;
}

#define VKD3D_VIEW_RAW_BUFFER 0x1

static void vkd3d_get_metadata_buffer_view_for_resource(struct d3d12_device *device,
        struct d3d12_resource *resource, DXGI_FORMAT view_format,
        VkDeviceSize offset, VkDeviceSize size, VkDeviceSize structure_stride,
        struct vkd3d_descriptor_metadata_buffer_view *view)
{
    VkDeviceSize element_size;

    element_size = view_format == DXGI_FORMAT_UNKNOWN
            ? structure_stride : vkd3d_get_format(device, view_format, false)->byte_count;

    view->va = resource->res.va + offset * element_size;
    view->range = size * element_size;
    view->dxgi_format = view_format;
    view->flags = VKD3D_DESCRIPTOR_FLAG_BUFFER_VA_RANGE | VKD3D_DESCRIPTOR_FLAG_NON_NULL;

    /* If we would need an SSBO offset buffer for whatever reason, just fallback to a typed view instead. */
    if (view_format == DXGI_FORMAT_UNKNOWN)
        if (view->va & (device->device_info.properties2.properties.limits.minStorageBufferOffsetAlignment - 1))
            view->dxgi_format = DXGI_FORMAT_R32_UINT;
}

static bool vkd3d_create_buffer_view_for_resource(struct d3d12_device *device,
        struct d3d12_resource *resource, DXGI_FORMAT view_format,
        VkDeviceSize offset, VkDeviceSize size, VkDeviceSize structure_stride,
        unsigned int flags, struct vkd3d_view **view)
{
    const struct vkd3d_format *format;
    struct vkd3d_view_key key;
    VkDeviceSize element_size;

    if (view_format == DXGI_FORMAT_R32_TYPELESS && (flags & VKD3D_VIEW_RAW_BUFFER))
    {
        format = vkd3d_get_format(device, DXGI_FORMAT_R32_UINT, false);
        element_size = format->byte_count;
    }
    else if (view_format == DXGI_FORMAT_UNKNOWN && structure_stride)
    {
        format = vkd3d_get_format(device, DXGI_FORMAT_R32_UINT, false);
        element_size = structure_stride;
    }
    else if ((format = vkd3d_format_from_d3d12_resource_desc(device, &resource->desc, view_format)))
    {
        element_size = format->byte_count;
    }
    else
    {
        WARN("Failed to find format for %#x.\n", resource->desc.Format);
        return false;
    }

    assert(d3d12_resource_is_buffer(resource));

    key.view_type = VKD3D_VIEW_TYPE_BUFFER;
    key.u.buffer.buffer = resource->res.vk_buffer;
    key.u.buffer.format = format;
    key.u.buffer.offset = resource->mem.offset + offset * element_size;
    key.u.buffer.size = size * element_size;

    return !!(*view = vkd3d_view_map_create_view(&resource->view_map, device, &key));
}

static void vkd3d_set_view_swizzle_for_format(VkComponentMapping *components,
        const struct vkd3d_format *format, bool allowed_swizzle)
{
    components->r = VK_COMPONENT_SWIZZLE_R;
    components->g = VK_COMPONENT_SWIZZLE_G;
    components->b = VK_COMPONENT_SWIZZLE_B;
    components->a = VK_COMPONENT_SWIZZLE_A;

    if (format->vk_aspect_mask == VK_IMAGE_ASPECT_STENCIL_BIT)
    {
        if (allowed_swizzle)
        {
            components->r = VK_COMPONENT_SWIZZLE_ZERO;
            components->g = VK_COMPONENT_SWIZZLE_R;
            components->b = VK_COMPONENT_SWIZZLE_ZERO;
            components->a = VK_COMPONENT_SWIZZLE_ZERO;
        }
        else
        {
            FIXME("Stencil swizzle is not supported for format %#x.\n",
                    format->dxgi_format);
        }
    }

    if (format->dxgi_format == DXGI_FORMAT_A8_UNORM && format->vk_format != VK_FORMAT_A8_UNORM_KHR)
    {
        if (allowed_swizzle)
        {
            components->r = VK_COMPONENT_SWIZZLE_ZERO;
            components->g = VK_COMPONENT_SWIZZLE_ZERO;
            components->b = VK_COMPONENT_SWIZZLE_ZERO;
            components->a = VK_COMPONENT_SWIZZLE_R;
        }
        else
        {
            FIXME("Alpha swizzle is not supported.\n");
        }
    }

    if (format->dxgi_format == DXGI_FORMAT_B8G8R8X8_UNORM
            || format->dxgi_format == DXGI_FORMAT_B8G8R8X8_UNORM_SRGB)
    {
        if (allowed_swizzle)
        {
            components->r = VK_COMPONENT_SWIZZLE_R;
            components->g = VK_COMPONENT_SWIZZLE_G;
            components->b = VK_COMPONENT_SWIZZLE_B;
            components->a = VK_COMPONENT_SWIZZLE_ONE;
        }
        else
        {
            FIXME("B8G8R8X8 swizzle is not supported.\n");
        }
    }
}

static VkComponentSwizzle vk_component_swizzle_from_d3d12(unsigned int component_mapping,
        unsigned int component_index)
{
    D3D12_SHADER_COMPONENT_MAPPING mapping
            = D3D12_DECODE_SHADER_4_COMPONENT_MAPPING(component_index, component_mapping);

    switch (mapping)
    {
        case D3D12_SHADER_COMPONENT_MAPPING_FROM_MEMORY_COMPONENT_0:
            return VK_COMPONENT_SWIZZLE_R;
        case D3D12_SHADER_COMPONENT_MAPPING_FROM_MEMORY_COMPONENT_1:
            return VK_COMPONENT_SWIZZLE_G;
        case D3D12_SHADER_COMPONENT_MAPPING_FROM_MEMORY_COMPONENT_2:
            return VK_COMPONENT_SWIZZLE_B;
        case D3D12_SHADER_COMPONENT_MAPPING_FROM_MEMORY_COMPONENT_3:
            return VK_COMPONENT_SWIZZLE_A;
        case D3D12_SHADER_COMPONENT_MAPPING_FORCE_VALUE_0:
            return VK_COMPONENT_SWIZZLE_ZERO;
        case D3D12_SHADER_COMPONENT_MAPPING_FORCE_VALUE_1:
            return VK_COMPONENT_SWIZZLE_ONE;
    }

    FIXME("Invalid component mapping %#x.\n", mapping);
    return VK_COMPONENT_SWIZZLE_IDENTITY;
}

static void vk_component_mapping_from_d3d12(VkComponentMapping *components,
        unsigned int component_mapping)
{
    components->r = vk_component_swizzle_from_d3d12(component_mapping, 0);
    components->g = vk_component_swizzle_from_d3d12(component_mapping, 1);
    components->b = vk_component_swizzle_from_d3d12(component_mapping, 2);
    components->a = vk_component_swizzle_from_d3d12(component_mapping, 3);
}

static VkComponentSwizzle swizzle_vk_component(const VkComponentMapping *components,
        VkComponentSwizzle component, VkComponentSwizzle swizzle)
{
    switch (swizzle)
    {
        case VK_COMPONENT_SWIZZLE_IDENTITY:
            break;

        case VK_COMPONENT_SWIZZLE_R:
            component = components->r;
            break;

        case VK_COMPONENT_SWIZZLE_G:
            component = components->g;
            break;

        case VK_COMPONENT_SWIZZLE_B:
            component = components->b;
            break;

        case VK_COMPONENT_SWIZZLE_A:
            component = components->a;
            break;

        case VK_COMPONENT_SWIZZLE_ONE:
        case VK_COMPONENT_SWIZZLE_ZERO:
            component = swizzle;
            break;

        default:
            FIXME("Invalid component swizzle %#x.\n", swizzle);
            break;
    }

    assert(component != VK_COMPONENT_SWIZZLE_IDENTITY);
    return component;
}

static void vk_component_mapping_compose(VkComponentMapping *dst, const VkComponentMapping *b)
{
    const VkComponentMapping a = *dst;

    dst->r = swizzle_vk_component(&a, a.r, b->r);
    dst->g = swizzle_vk_component(&a, a.g, b->g);
    dst->b = swizzle_vk_component(&a, a.b, b->b);
    dst->a = swizzle_vk_component(&a, a.a, b->a);
}

static bool init_default_texture_view_desc(struct vkd3d_texture_view_desc *desc,
        struct d3d12_resource *resource, DXGI_FORMAT view_format)
{
    const struct d3d12_device *device = resource->device;

    if (!(desc->format = vkd3d_format_from_d3d12_resource_desc(device, &resource->desc, view_format)))
    {
        FIXME("Failed to find format (resource format %#x, view format %#x).\n",
                resource->desc.Format, view_format);
        return false;
    }

    desc->aspect_mask = desc->format->vk_aspect_mask;
    desc->image = resource->res.vk_image;
    desc->miplevel_idx = 0;
    desc->miplevel_count = 1;
    desc->miplevel_clamp = 0.0f;
    desc->layer_idx = 0;
    desc->layer_count = d3d12_resource_desc_get_layer_count(&resource->desc);
    desc->image_usage = 0;
    desc->w_offset = 0;
    desc->w_size = UINT_MAX;

    switch (resource->desc.Dimension)
    {
        case D3D12_RESOURCE_DIMENSION_TEXTURE1D:
            desc->view_type = resource->desc.DepthOrArraySize > 1
                    ? VK_IMAGE_VIEW_TYPE_1D_ARRAY : VK_IMAGE_VIEW_TYPE_1D;
            break;

        case D3D12_RESOURCE_DIMENSION_TEXTURE2D:
            desc->view_type = resource->desc.DepthOrArraySize > 1
                    ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D;
            break;

        case D3D12_RESOURCE_DIMENSION_TEXTURE3D:
            desc->view_type = VK_IMAGE_VIEW_TYPE_3D;
            desc->layer_count = 1;
            break;

        default:
            FIXME("Resource dimension %#x not implemented.\n", resource->desc.Dimension);
            return false;
    }

    desc->components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
    desc->components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
    desc->components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
    desc->components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
    desc->allowed_swizzle = false;
    return true;
}

bool vkd3d_create_texture_view(struct d3d12_device *device, const struct vkd3d_texture_view_desc *desc, struct vkd3d_view **view)
{
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    VkImageViewUsageCreateInfo image_usage_create_info;
    const struct vkd3d_format *format = desc->format;
    VkImageViewMinLodCreateInfoEXT min_lod_desc;
    VkImageViewSlicedCreateInfoEXT sliced_desc;
    VkImageView vk_view = VK_NULL_HANDLE;
    VkImageViewCreateInfo view_desc;
    int32_t miplevel_clamp_fixed;
    struct vkd3d_view *object;
    uint32_t clamp_base_level;
    uint32_t end_level;
    VkResult vr;

    view_desc.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_desc.pNext = NULL;
    view_desc.flags = 0;
    view_desc.image = desc->image;
    view_desc.viewType = desc->view_type;
    view_desc.format = format->vk_format;
    vkd3d_set_view_swizzle_for_format(&view_desc.components, format, desc->allowed_swizzle);
    if (desc->allowed_swizzle)
        vk_component_mapping_compose(&view_desc.components, &desc->components);
    view_desc.subresourceRange.aspectMask = desc->aspect_mask;
    view_desc.subresourceRange.baseMipLevel = desc->miplevel_idx;
    view_desc.subresourceRange.levelCount = desc->miplevel_count;
    view_desc.subresourceRange.baseArrayLayer = desc->layer_idx;
    view_desc.subresourceRange.layerCount = desc->layer_count;

    /* If the clamp is defined such that it would only access mip levels
     * outside the view range, don't make a view and use a NULL descriptor.
     * The clamp is absolute, and not affected by the baseMipLevel. */
    miplevel_clamp_fixed = vkd3d_float_to_fixed_24_8(desc->miplevel_clamp);

    if (miplevel_clamp_fixed <= vkd3d_float_to_fixed_24_8(desc->miplevel_idx + desc->miplevel_count - 1))
    {
        if (desc->miplevel_clamp > (float)desc->miplevel_idx)
        {
            if (device->device_info.image_view_min_lod_features.minLod)
            {
                /* Clamp minLod the highest accessed mip level to stay within spec */
                min_lod_desc.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_MIN_LOD_CREATE_INFO_EXT;
                min_lod_desc.pNext = NULL;
                min_lod_desc.minLod = vkd3d_fixed_24_8_to_float(miplevel_clamp_fixed);
                vk_prepend_struct(&view_desc, &min_lod_desc);
            }
            else
            {
                FIXME_ONCE("Cannot handle MinResourceLOD clamp of %f correctly.\n", desc->miplevel_clamp);
                /* This is not correct, but it's the best we can do without VK_EXT_image_view_min_lod.
                 * It should at least avoid a scenario where implicit LOD fetches from invalid levels. */
                clamp_base_level = (uint32_t)desc->miplevel_clamp;
                end_level = view_desc.subresourceRange.baseMipLevel + view_desc.subresourceRange.levelCount;
                view_desc.subresourceRange.levelCount = end_level - clamp_base_level;
                view_desc.subresourceRange.baseMipLevel = clamp_base_level;
            }
        }

        image_usage_create_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_USAGE_CREATE_INFO;
        image_usage_create_info.pNext = NULL;
        image_usage_create_info.usage = desc->image_usage;
        vk_prepend_struct(&view_desc, &image_usage_create_info);

        if (desc->view_type == VK_IMAGE_VIEW_TYPE_3D &&
                (desc->w_offset != 0 || desc->w_size != VK_REMAINING_3D_SLICES_EXT) &&
                device->device_info.image_sliced_view_of_3d_features.imageSlicedViewOf3D)
        {
            sliced_desc.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_SLICED_CREATE_INFO_EXT;
            sliced_desc.pNext = NULL;
            sliced_desc.sliceOffset = desc->w_offset;
            sliced_desc.sliceCount = desc->w_size;
            vk_prepend_struct(&view_desc, &sliced_desc);
        }

        if ((vr = VK_CALL(vkCreateImageView(device->vk_device, &view_desc, NULL, &vk_view))) < 0)
        {
            WARN("Failed to create Vulkan image view, vr %d.\n", vr);
            return false;
        }
    }

    if (!(object = vkd3d_view_create(VKD3D_VIEW_TYPE_IMAGE)))
    {
        VK_CALL(vkDestroyImageView(device->vk_device, vk_view, NULL));
        return false;
    }

    object->vk_image_view = vk_view;
    object->format = format;
    object->info.texture.vk_view_type = desc->view_type;
    object->info.texture.aspect_mask = desc->aspect_mask;
    object->info.texture.miplevel_idx = desc->miplevel_idx;
    object->info.texture.layer_idx = desc->layer_idx;
    object->info.texture.layer_count = desc->layer_count;
    object->info.texture.w_offset = desc->w_offset;
    object->info.texture.w_size = desc->w_size;
    *view = object;
    return true;
}

static inline void vkd3d_init_write_descriptor_set(VkWriteDescriptorSet *vk_write, const struct d3d12_desc_split *split,
        struct vkd3d_descriptor_binding binding,
        VkDescriptorType vk_descriptor_type, const union vkd3d_descriptor_info *info)
{
    vk_write->sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    vk_write->pNext = NULL;
    vk_write->dstSet = split->heap->sets[binding.set].vk_descriptor_set;
    vk_write->dstBinding = binding.binding;
    vk_write->dstArrayElement = split->offset;
    vk_write->descriptorCount = 1;
    vk_write->descriptorType = vk_descriptor_type;
    vk_write->pImageInfo = &info->image;
    vk_write->pBufferInfo = &info->buffer;
    vk_write->pTexelBufferView = &info->buffer_view;
}

static void d3d12_descriptor_heap_write_null_descriptor_template_embedded_partial(struct d3d12_device *device,
        vkd3d_cpu_descriptor_va_t desc_va,
        VkDescriptorType vk_descriptor_type, size_t payload_offset, size_t size)
{
    struct d3d12_desc_split_embedded desc;
    const uint8_t *src;

    desc = d3d12_desc_decode_embedded_resource_va(desc_va);
    src = vkd3d_bindless_state_get_null_descriptor_payload(&device->bindless_state, vk_descriptor_type);
    desc.payload += payload_offset;
    src += payload_offset;

    if (VKD3D_EXPECT_TRUE(desc.metadata == NULL))
        vkd3d_memcpy_aligned_non_temporal(desc.payload, src, size);
    else
        vkd3d_memcpy_aligned_cached(desc.payload, src, size);
}

static void d3d12_descriptor_heap_write_null_descriptor_template_embedded(struct d3d12_device *device,
        vkd3d_cpu_descriptor_va_t desc_va,
        VkDescriptorType vk_descriptor_type)
{
    struct d3d12_desc_split_embedded desc;
    const uint8_t *src;

    desc = d3d12_desc_decode_embedded_resource_va(desc_va);

    src = vkd3d_bindless_state_get_null_descriptor_payload(&device->bindless_state, vk_descriptor_type);

    if (VKD3D_EXPECT_TRUE(desc.metadata == NULL))
    {
        /* If metadata is packed into the descriptor, it gets cleared to zero here in this copy. */
        vkd3d_memcpy_aligned_non_temporal(desc.payload, src,
                device->bindless_state.descriptor_buffer_cbv_srv_uav_size);
    }
    else
    {
        vkd3d_memcpy_aligned_cached(desc.payload, src,
                device->bindless_state.descriptor_buffer_cbv_srv_uav_size);

        /* If metadata is not packed, need to clear that separately. */
        memset(desc.metadata, 0, sizeof(*desc.metadata));
    }
}

static void d3d12_descriptor_heap_write_null_descriptor_template(vkd3d_cpu_descriptor_va_t desc_va,
        VkDescriptorType vk_mutable_descriptor_type)
{
    /* For null descriptors, some games don't write the correct type (usually an image SRV),
     * so we will need to splat null descriptors over all descriptor sets.
     * For MUTABLE, this would normally just be one descriptor set, but
     * we need MUTABLE + STORAGE_BUFFER, or 6 sets for non-mutable :\ */
    const struct d3d12_null_descriptor_template *null_descriptor_template;
    VkWriteDescriptorSet writes[VKD3D_MAX_BINDLESS_DESCRIPTOR_SETS];
    const struct vkd3d_vk_device_procs *vk_procs;
    struct vkd3d_bindless_state *bindless_state;
    struct d3d12_desc_split desc;
    unsigned int num_writes, i;
    unsigned int offset;
    VkDeviceAddress *va;
    const uint8_t *src;
    uint8_t *dst;

    desc = d3d12_desc_decode_va(desc_va);

    null_descriptor_template = &desc.heap->null_descriptor_template;

    /* When mutable descriptors are not supported, set a dummy type.
       This will make those drivers not care about the null type being different between
       null writes. */
    if (!null_descriptor_template->has_mutable_descriptors)
        vk_mutable_descriptor_type = 0;

    /* Skip writes with the same null type that are already null. */
    if (!(desc.view->info.flags & VKD3D_DESCRIPTOR_FLAG_NON_NULL)
            && desc.types->current_null_type == vk_mutable_descriptor_type)
        return;

    num_writes = null_descriptor_template->num_writes;
    offset = desc.offset;

    if (null_descriptor_template->has_descriptor_buffer)
    {
        bindless_state = &desc.heap->device->bindless_state;

        for (i = 0; i < num_writes; i++)
        {
            dst = desc.heap->sets[i].mapped_set;
            dst += offset * null_descriptor_template->writes.payloads[i].desc_size;
            src = null_descriptor_template->writes.payloads[i].src_payload;
            if (!src)
                src = vkd3d_bindless_state_get_null_descriptor_payload(bindless_state, vk_mutable_descriptor_type);
            memcpy(dst, src, null_descriptor_template->writes.payloads[i].desc_size);
        }
    }
    else
    {
        vk_procs = &desc.heap->device->vk_procs;

        for (i = 0; i < num_writes; i++)
        {
            writes[i] = null_descriptor_template->writes.descriptors.writes[i];
            if (writes[i].descriptorType == VK_DESCRIPTOR_TYPE_MUTABLE_EXT)
                writes[i].descriptorType = vk_mutable_descriptor_type;
            writes[i].dstArrayElement = offset;
        }

        if (num_writes)
            VK_CALL(vkUpdateDescriptorSets(desc.heap->device->vk_device, num_writes, writes, 0, NULL));
    }

    desc.view->info.flags = 0;
    desc.types->set_info_mask = null_descriptor_template->set_info_mask;
    desc.types->current_null_type = vk_mutable_descriptor_type;
    memset(desc.view, 0, sizeof(*desc.view));

    if (num_writes == 1)
    {
        desc.view->info.flags |= VKD3D_DESCRIPTOR_FLAG_SINGLE_DESCRIPTOR;
        /* If the template has one descriptor write, this is a single set descriptor heap. */
        desc.types->single_binding.set = 0;
        /* For descriptor buffer path, the binding is ignored. */
        desc.types->single_binding.binding = null_descriptor_template->writes.descriptors.writes[0].dstBinding;
    }

    va = desc.heap->raw_va_aux_buffer.host_ptr;
    if (va)
        va[offset] = 0;

    /* Notify descriptor QA that we have a universal null descriptor. */
    vkd3d_descriptor_debug_write_descriptor(desc.heap->descriptor_heap_info.host_ptr,
            desc.heap->cookie, offset,
            VKD3D_DESCRIPTOR_QA_TYPE_UNIFORM_BUFFER_BIT |
                    VKD3D_DESCRIPTOR_QA_TYPE_STORAGE_BUFFER_BIT |
                    VKD3D_DESCRIPTOR_QA_TYPE_SAMPLED_IMAGE_BIT |
                    VKD3D_DESCRIPTOR_QA_TYPE_STORAGE_IMAGE_BIT |
                    VKD3D_DESCRIPTOR_QA_TYPE_UNIFORM_TEXEL_BUFFER_BIT |
                    VKD3D_DESCRIPTOR_QA_TYPE_STORAGE_TEXEL_BUFFER_BIT |
                    VKD3D_DESCRIPTOR_QA_TYPE_RAW_VA_BIT |
                    VKD3D_DESCRIPTOR_QA_TYPE_RT_ACCELERATION_STRUCTURE_BIT, 0);
}

void d3d12_desc_create_cbv_embedded(vkd3d_cpu_descriptor_va_t desc_va,
        struct d3d12_device *device, const D3D12_CONSTANT_BUFFER_VIEW_DESC *desc)
{
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    VkDescriptorAddressInfoEXT addr_info;
    struct d3d12_desc_split_embedded d;
    VkDescriptorGetInfoEXT get_info;

    if (!desc)
    {
        WARN("Constant buffer desc is NULL.\n");
        return;
    }

    if (desc->SizeInBytes & (D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT - 1))
    {
        WARN("Size is not %u bytes aligned.\n", D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);
        return;
    }

    if (!desc->BufferLocation)
    {
        d3d12_descriptor_heap_write_null_descriptor_template_embedded(device, desc_va,
                VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
        return;
    }

    d = d3d12_desc_decode_embedded_resource_va(desc_va);

    /* Metadata is irrelevant for CBV since we don't enable table hoisting. */

    get_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_GET_INFO_EXT;
    get_info.pNext = NULL;
    get_info.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    get_info.data.pUniformBuffer = &addr_info;
    addr_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_ADDRESS_INFO_EXT;
    addr_info.pNext = NULL;
    addr_info.format = VK_FORMAT_UNDEFINED;
    addr_info.address = desc->BufferLocation;
    addr_info.range = desc->SizeInBytes;

    /* For robustness purposes. If someone tries to access a UBO as an image,
     * it should translate to a NULL descriptor. */

    /* See vkd3d_bindless_state_init_null_descriptor_payloads for details.
     * Use UNIFORM_BUFFER template here, since we've already prepared the desired NULL payload
     * at the typed offset. */
    d3d12_descriptor_heap_write_null_descriptor_template_embedded_partial(device, desc_va,
            VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 0, device->bindless_state.descriptor_buffer_packed_raw_buffer_offset);

    VK_CALL(vkGetDescriptorEXT(device->vk_device, &get_info,
            device->device_info.descriptor_buffer_properties.robustUniformBufferDescriptorSize,
            d.payload + device->bindless_state.descriptor_buffer_packed_raw_buffer_offset));
}

void d3d12_desc_create_cbv(vkd3d_cpu_descriptor_va_t desc_va,
        struct d3d12_device *device, const D3D12_CONSTANT_BUFFER_VIEW_DESC *desc)
{
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    const struct vkd3d_unique_resource *resource = NULL;
    union vkd3d_descriptor_info null_descriptor_info;
    union vkd3d_descriptor_info descriptor_info;
    struct vkd3d_descriptor_binding binding;
    VkDescriptorAddressInfoEXT addr_info;
    VkDescriptorType vk_descriptor_type;
    VkWriteDescriptorSet vk_writes[2];
    VkDescriptorGetInfoEXT get_info;
    uint32_t vk_write_count = 0;
    struct d3d12_desc_split d;
    void *payload;

    uint32_t info_index;

    if (!desc)
    {
        WARN("Constant buffer desc is NULL.\n");
        return;
    }

    if (desc->SizeInBytes & (D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT - 1))
    {
        WARN("Size is not %u bytes aligned.\n", D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);
        return;
    }

    vk_descriptor_type = vkd3d_bindless_state_get_cbv_descriptor_type(&device->bindless_state);

    if (!desc->BufferLocation)
    {
        d3d12_descriptor_heap_write_null_descriptor_template(desc_va, vk_descriptor_type);
        return;
    }

    d = d3d12_desc_decode_va(desc_va);

    info_index = vkd3d_bindless_state_find_set_info_index_fast(device,
            VKD3D_BINDLESS_STATE_INFO_INDEX_MUTABLE_SPLIT_RAW,
            VKD3D_BINDLESS_SET_CBV);

    binding = vkd3d_bindless_state_binding_from_info_index(&device->bindless_state, info_index);

    d.types->set_info_mask = 1u << info_index;
    d.types->single_binding = binding;
    d.view->info.buffer.va = desc->BufferLocation;
    d.view->info.buffer.range = desc->SizeInBytes;
    d.view->info.buffer.padding = 0;
    d.view->info.buffer.dxgi_format = DXGI_FORMAT_UNKNOWN;
    d.view->info.buffer.flags = VKD3D_DESCRIPTOR_FLAG_BUFFER_VA_RANGE | VKD3D_DESCRIPTOR_FLAG_NON_NULL;

    /* De-reffing resource in descriptor buffer path is kinda redundant but there are some scenarios where
     * it's required:
     * - Hoisted CBV descriptors. These translate to push descriptors and must be resolved to plain buffers.
     *   Possible to write the VA instead and de-ref late in theory.
     * - Descriptor QA. Can be ifdeffed. */

    if (d3d12_device_uses_descriptor_buffers(device))
    {
        get_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_GET_INFO_EXT;
        get_info.pNext = NULL;
        get_info.type = vk_descriptor_type;
        /* This is a union of pointers and UBOs are always supported here. */
        get_info.data.pUniformBuffer = &addr_info;
        addr_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_ADDRESS_INFO_EXT;
        addr_info.pNext = NULL;
        addr_info.format = VK_FORMAT_UNDEFINED;
        addr_info.address = desc->BufferLocation;
        addr_info.range = desc->SizeInBytes;
        payload = d3d12_descriptor_heap_get_mapped_payload(d.heap, binding.set, d.offset);
        VK_CALL(vkGetDescriptorEXT(device->vk_device, &get_info,
                device->device_info.descriptor_buffer_properties.robustUniformBufferDescriptorSize,
                payload));
#ifdef VKD3D_ENABLE_DESCRIPTOR_QA
        /* Only used for descriptor QA in this path. */
        resource = vkd3d_va_map_deref(&device->memory_allocator.va_map, desc->BufferLocation);
#endif
    }
    else
    {
        resource = vkd3d_va_map_deref(&device->memory_allocator.va_map, desc->BufferLocation);
        descriptor_info.buffer.buffer = resource->vk_buffer;
        descriptor_info.buffer.offset = desc->BufferLocation - resource->va;
        descriptor_info.buffer.range = min(desc->SizeInBytes, resource->size - descriptor_info.buffer.offset);

        vkd3d_init_write_descriptor_set(&vk_writes[vk_write_count++], &d, binding, vk_descriptor_type, &descriptor_info);
    }

    /* Clear out sibling typed descriptor if appropriate.
     * Native drivers are robust against scenarios like these, and so we need to be too. */
    if (device->bindless_state.flags & VKD3D_BINDLESS_MUTABLE_TYPE_SPLIT_RAW_TYPED)
    {
        /* The NULL descriptor template for non-embedded is too messy to invoke here. */
        info_index = VKD3D_BINDLESS_STATE_INFO_INDEX_MUTABLE_SPLIT_TYPED;
        binding = vkd3d_bindless_state_binding_from_info_index(&device->bindless_state, info_index);
        d.types->set_info_mask |= 1u << info_index;

        if (d3d12_device_uses_descriptor_buffers(device))
        {
            get_info.type = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
            get_info.data.pUniformTexelBuffer = NULL;
            payload = d3d12_descriptor_heap_get_mapped_payload(d.heap, binding.set, d.offset);
            VK_CALL(vkGetDescriptorEXT(device->vk_device, &get_info,
                    device->device_info.descriptor_buffer_properties.robustUniformTexelBufferDescriptorSize,
                    payload));
        }
        else
        {
            null_descriptor_info.buffer.buffer = VK_NULL_HANDLE;
            null_descriptor_info.buffer.offset = 0;
            null_descriptor_info.buffer.range = VK_WHOLE_SIZE;
            vkd3d_init_write_descriptor_set(&vk_writes[vk_write_count++], &d, binding,
                    VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, &null_descriptor_info);
        }
    }
    else
        d.view->info.buffer.flags |= VKD3D_DESCRIPTOR_FLAG_SINGLE_DESCRIPTOR;

#ifdef VKD3D_ENABLE_DESCRIPTOR_QA
    if (vkd3d_config_flags & VKD3D_CONFIG_FLAG_INSTRUCTION_QA_CHECKS)
    {
        /* We may want to peek at the buffer's raw VA when doing instrumentation. */
        VkDeviceAddress *counter_addresses = d.heap->raw_va_aux_buffer.host_ptr;
        counter_addresses[d.offset] =
                vkd3d_descriptor_debug_encode_buffer_va(d.view->info.buffer.va, sizeof(uint32_t));
        d.view->info.buffer.flags |= VKD3D_DESCRIPTOR_QA_TYPE_RAW_VA_BIT;
    }
#endif

    if (vk_write_count)
        VK_CALL(vkUpdateDescriptorSets(device->vk_device, vk_write_count, vk_writes, 0, NULL));

    vkd3d_descriptor_metadata_view_set_qa_cookie(d.view, resource ? resource->cookie : 0);
    vkd3d_descriptor_debug_write_descriptor(d.heap->descriptor_heap_info.host_ptr,
            d.heap->cookie,
            d.offset,
            vk_descriptor_type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER ?
                    VKD3D_DESCRIPTOR_QA_TYPE_UNIFORM_BUFFER_BIT :
                    VKD3D_DESCRIPTOR_QA_TYPE_STORAGE_BUFFER_BIT,
            d.view->qa_cookie);
}

static unsigned int vkd3d_view_flags_from_d3d12_buffer_srv_flags(D3D12_BUFFER_SRV_FLAGS flags)
{
    if (flags == D3D12_BUFFER_SRV_FLAG_RAW)
        return VKD3D_VIEW_RAW_BUFFER;
    if (flags)
        FIXME("Unhandled buffer SRV flags %#x.\n", flags);
    return 0;
}

static void vkd3d_buffer_view_get_bound_range_ssbo(
        struct d3d12_device *device, struct d3d12_resource *resource,
        VkDeviceSize offset, VkDeviceSize range, VkDescriptorBufferInfo *vk_buffer,
        struct vkd3d_bound_buffer_range *bound_range)
{
    if (resource)
    {
        VkDeviceSize alignment = d3d12_device_get_ssbo_alignment(device);
        VkDeviceSize aligned_begin = offset & ~(alignment - 1);
        VkDeviceSize aligned_end = min((offset + range + alignment - 1) & ~(alignment - 1), resource->desc.Width);

        /* heap_offset is guaranteed to have 64KiB alignment */
        vk_buffer->buffer = resource->res.vk_buffer;
        vk_buffer->offset = resource->mem.offset + aligned_begin;
        vk_buffer->range = aligned_end - aligned_begin;

        bound_range->byte_offset = offset - aligned_begin;
        bound_range->byte_count = range;
    }
    else
    {
        vk_buffer->buffer = VK_NULL_HANDLE;
        vk_buffer->offset = 0;
        vk_buffer->range = VK_WHOLE_SIZE;

        bound_range->byte_offset = 0;
        bound_range->byte_count = 0;
    }
}

static bool vkd3d_buffer_view_get_aligned_view(
        struct d3d12_device *device, struct d3d12_resource *resource,
        DXGI_FORMAT format, unsigned int vk_flags,
        VkDeviceSize first_element, VkDeviceSize num_elements,
        VkDeviceSize structured_stride, struct vkd3d_bound_buffer_range *bound_range,
        struct vkd3d_view **view)
{
    const struct vkd3d_format *vkd3d_format;
    VkDeviceSize max_resource_elements;
    VkDeviceSize max_element_headroom;
    VkDeviceSize element_align;
    VkDeviceSize max_elements;
    VkDeviceSize begin_range;
    VkDeviceSize end_range;

    if (device->bindless_state.flags & VKD3D_TYPED_OFFSET_BUFFER)
    {
        /* For typed buffers, we will try to remove two cases of extreme hashmap contention, i.e.
         * first_element and num_elements. By quantizing these two and relying on offset buffers,
         * we should achieve a bounded value for number of possible views we can create for a given resource. */
        max_elements = device->device_info.properties2.properties.limits.maxTexelBufferElements;

        if (format)
        {
            vkd3d_format = vkd3d_get_format(device, format, false);
            max_resource_elements = resource->desc.Width / vkd3d_format->byte_count;
        }
        else
        {
            /* For structured buffers, we need to rescale input parameters to
             * be in terms of u32 since the offset buffer must be in terms of words.
             * When using typed buffers, the offset buffer is in format of u32
             * (element offset, element size). */
            first_element = (first_element * structured_stride) / sizeof(uint32_t);
            num_elements = (num_elements * structured_stride) / sizeof(uint32_t);
            structured_stride = sizeof(uint32_t);
            max_resource_elements = resource->desc.Width / sizeof(uint32_t);
        }

        /* Requantizing the typed offset is shaky business if we overflow max_elements when doing so.
         * We can always fall back to 0 offset for the difficult and rare cases. */

        if (num_elements > max_elements)
        {
            FIXME("Application is attempting to use more elements in a typed buffer (%llu) than supported by device (%llu).\n",
                  (unsigned long long)num_elements, (unsigned long long)max_elements);
            bound_range->element_offset = 0;
            bound_range->element_count = num_elements;
        }
        else if (num_elements >= max_resource_elements)
        {
            bound_range->element_offset = 0;
            bound_range->element_count = num_elements;
        }
        else
        {
            /* Quantizing to alignment of N will at most increment number of elements in the view by N - 1. */
            max_element_headroom = max_elements - num_elements + 1;

            /* Based on headroom, align offset to the largest POT factor of N. */
            element_align = 1u << vkd3d_log2i(max_element_headroom);

            begin_range = first_element & ~(element_align - 1);
            end_range = (first_element + num_elements + element_align - 1) & ~(element_align - 1);
            end_range = min(end_range, max_resource_elements);

            bound_range->element_offset = first_element - begin_range;
            bound_range->element_count = num_elements;

            first_element = begin_range;
            num_elements = end_range - begin_range;
        }
    }

    if (!vkd3d_create_buffer_view_for_resource(device, resource, format,
            first_element, num_elements,
            structured_stride, vk_flags, view))
        return false;

    return true;
}

static void vkd3d_create_buffer_srv_embedded(vkd3d_cpu_descriptor_va_t desc_va,
        struct d3d12_device *device, struct d3d12_resource *resource,
        const D3D12_SHADER_RESOURCE_VIEW_DESC *desc)
{
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    struct vkd3d_descriptor_metadata_buffer_view view;
    VkDescriptorAddressInfoEXT addr_info;
    struct d3d12_desc_split_embedded d;
    VkDescriptorGetInfoEXT get_info;

    if (!desc)
    {
        FIXME("Default buffer SRV not supported.\n");
        return;
    }

    d = d3d12_desc_decode_embedded_resource_va(desc_va);

    if (desc->ViewDimension == D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE)
    {
        if (!desc->RaytracingAccelerationStructure.Location)
        {
            /* Clear out the entire thing to be more robust similar to null descriptor templates. */
            d3d12_descriptor_heap_write_null_descriptor_template_embedded(device, desc_va,
                    VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE);
        }
        else
        {
            memcpy(d.payload, &desc->RaytracingAccelerationStructure.Location, sizeof(D3D12_GPU_VIRTUAL_ADDRESS));
        }

        return;
    }

    if (desc->ViewDimension != D3D12_SRV_DIMENSION_BUFFER)
    {
        WARN("Unexpected view dimension %#x.\n", desc->ViewDimension);
        return;
    }

    if (!resource)
    {
        /* We prepare a packed NULL descriptor that contains both texel buffer and SSBO. */
        d3d12_descriptor_heap_write_null_descriptor_template_embedded(device, desc_va,
                VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER);
        return;
    }

    /* Ignore metadata for SRV. */
    vkd3d_get_metadata_buffer_view_for_resource(device, resource,
            desc->Format, desc->Buffer.FirstElement, desc->Buffer.NumElements,
            desc->Buffer.StructureByteStride, &view);

    /* Emit SSBO. */
    get_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_GET_INFO_EXT;
    get_info.pNext = NULL;
    get_info.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    get_info.data.pStorageBuffer = &addr_info;
    addr_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_ADDRESS_INFO_EXT;
    addr_info.pNext = NULL;
    addr_info.address = view.va;
    addr_info.range = view.range;
    addr_info.format = VK_FORMAT_UNDEFINED;
    VK_CALL(vkGetDescriptorEXT(device->vk_device, &get_info,
            device->device_info.descriptor_buffer_properties.robustStorageBufferDescriptorSize,
            d.payload + device->bindless_state.descriptor_buffer_packed_raw_buffer_offset));

    /* Emit texel buffer alias. */
    get_info.type = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
    get_info.data.pUniformTexelBuffer = &addr_info;
    addr_info.format = vkd3d_internal_get_vk_format(device, view.dxgi_format);
    /* If we really intended to emit raw buffers, the fallback will be inferred as R32_UINT. */
    if (addr_info.format == VK_FORMAT_UNDEFINED)
        addr_info.format = VK_FORMAT_R32_UINT;
    VK_CALL(vkGetDescriptorEXT(device->vk_device, &get_info,
            device->device_info.descriptor_buffer_properties.robustUniformTexelBufferDescriptorSize,
            d.payload));
}

static void vkd3d_create_buffer_srv(vkd3d_cpu_descriptor_va_t desc_va,
        struct d3d12_device *device, struct d3d12_resource *resource,
        const D3D12_SHADER_RESOURCE_VIEW_DESC *desc)
{
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    VKD3D_UNUSED vkd3d_descriptor_qa_flags descriptor_qa_flags = 0;
    struct vkd3d_bound_buffer_range bound_range = { 0, 0, 0, 0 };
    union vkd3d_descriptor_info descriptor_info[2];
    struct vkd3d_descriptor_binding binding;
    VkDescriptorAddressInfoEXT addr_info;
    VkDescriptorType vk_descriptor_type;
    bool mutable_uses_single_descriptor;
    VkWriteDescriptorSet vk_write[2];
    VkDescriptorGetInfoEXT get_info;
    struct vkd3d_view *view = NULL;
    uint32_t vk_write_count = 0;
    struct d3d12_desc_split d;
    unsigned int vk_flags;
    uint32_t info_index;
    bool desc_is_raw;
    bool emit_typed;
    bool emit_ssbo;
    void *payload;

    if (!desc)
    {
        FIXME("Default buffer SRV not supported.\n");
        return;
    }

    d = d3d12_desc_decode_va(desc_va);

    if (desc->ViewDimension == D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE)
    {
        if (!desc->RaytracingAccelerationStructure.Location)
        {
            /* There is no concrete descriptor to use here,
             * so just write a SAMPLED_IMAGE to clear out mutable descriptor.
             * What we really want to clear here is the raw VA. */
            d3d12_descriptor_heap_write_null_descriptor_template(desc_va, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE);
            return;
        }

        if (d3d12_device_supports_ray_tracing_tier_1_0(device))
        {
            /* We implement this as a raw VA in the aux buffer. */
            VkDeviceAddress *raw_addresses = d.heap->raw_va_aux_buffer.host_ptr;
            uint32_t descriptor_index = d.offset;
            raw_addresses[descriptor_index] = desc->RaytracingAccelerationStructure.Location;
            d.view->info.buffer.flags = VKD3D_DESCRIPTOR_FLAG_RAW_VA_AUX_BUFFER |
                    VKD3D_DESCRIPTOR_FLAG_NON_NULL;
            d.types->set_info_mask = 0;
            /* There is no resource tied to this descriptor, just a naked pointer. */
            vkd3d_descriptor_metadata_view_set_qa_cookie(d.view, 0);
        }
        else
            WARN("Using CreateSRV for RTAS without RT support?\n");

        vkd3d_descriptor_debug_write_descriptor(d.heap->descriptor_heap_info.host_ptr,
                d.heap->cookie, d.offset,
                VKD3D_DESCRIPTOR_QA_TYPE_RT_ACCELERATION_STRUCTURE_BIT | VKD3D_DESCRIPTOR_QA_TYPE_RAW_VA_BIT,
                d.view->qa_cookie);

        return;
    }

    if (desc->ViewDimension != D3D12_SRV_DIMENSION_BUFFER)
    {
        WARN("Unexpected view dimension %#x.\n", desc->ViewDimension);
        return;
    }

    mutable_uses_single_descriptor = !!(device->bindless_state.flags & VKD3D_BINDLESS_MUTABLE_TYPE_RAW_SSBO);
    desc_is_raw = (desc->Format == DXGI_FORMAT_UNKNOWN && desc->Buffer.StructureByteStride) ||
            (desc->Buffer.Flags & D3D12_BUFFER_SRV_FLAG_RAW);
    emit_ssbo = (!mutable_uses_single_descriptor || desc_is_raw) && d3d12_device_use_ssbo_raw_buffer(device);
    emit_typed = !mutable_uses_single_descriptor || !desc_is_raw || !emit_ssbo;

    if (!resource)
    {
        if (mutable_uses_single_descriptor)
        {
            d3d12_descriptor_heap_write_null_descriptor_template(desc_va,
                    desc_is_raw ? VK_DESCRIPTOR_TYPE_STORAGE_BUFFER : VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER);
        }
        else
        {
            /* In the mutable set, always write texel buffer. The STORAGE_BUFFER set is also written to. */
            d3d12_descriptor_heap_write_null_descriptor_template(desc_va,
                    VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER);
        }
        return;
    }

    d.types->set_info_mask = 0;
    vkd3d_get_metadata_buffer_view_for_resource(device, resource,
            desc->Format, desc->Buffer.FirstElement, desc->Buffer.NumElements,
            desc->Buffer.StructureByteStride, &d.view->info.buffer);

    if (emit_ssbo)
    {
        info_index = vkd3d_bindless_state_find_set_info_index_fast(device,
                VKD3D_BINDLESS_STATE_INFO_INDEX_MUTABLE_SPLIT_RAW,
                VKD3D_BINDLESS_SET_SRV | VKD3D_BINDLESS_SET_RAW_SSBO);

        binding = vkd3d_bindless_state_binding_from_info_index(&device->bindless_state, info_index);

        d.types->set_info_mask |= 1u << info_index;

        if (device->bindless_state.flags & VKD3D_SSBO_OFFSET_BUFFER)
            d.view->info.buffer.flags |= VKD3D_DESCRIPTOR_FLAG_BUFFER_OFFSET;
        d.types->single_binding = binding;

        vk_descriptor_type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        descriptor_qa_flags |= VKD3D_DESCRIPTOR_QA_TYPE_STORAGE_BUFFER_BIT;

        if (d3d12_device_uses_descriptor_buffers(device))
        {
            get_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_GET_INFO_EXT;
            get_info.pNext = NULL;
            get_info.type = vk_descriptor_type;
            get_info.data.pStorageBuffer = &addr_info;
            addr_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_ADDRESS_INFO_EXT;
            addr_info.pNext = NULL;
            addr_info.address = d.view->info.buffer.va;
            addr_info.range = d.view->info.buffer.range;
            addr_info.format = VK_FORMAT_UNDEFINED;
            payload = d3d12_descriptor_heap_get_mapped_payload(d.heap, binding.set, d.offset);
            VK_CALL(vkGetDescriptorEXT(device->vk_device, &get_info,
                    device->device_info.descriptor_buffer_properties.robustStorageBufferDescriptorSize,
                    payload));
        }
        else
        {
            VkDeviceSize stride = desc->Format == DXGI_FORMAT_UNKNOWN
                    ? desc->Buffer.StructureByteStride :
                    vkd3d_get_format(device, desc->Format, false)->byte_count;

            vkd3d_buffer_view_get_bound_range_ssbo(device, resource,
                    desc->Buffer.FirstElement * stride, desc->Buffer.NumElements * stride,
                    &descriptor_info[vk_write_count].buffer, &bound_range);

            vkd3d_init_write_descriptor_set(&vk_write[vk_write_count], &d, binding,
                    vk_descriptor_type, &descriptor_info[vk_write_count]);
            vk_write_count++;
        }
    }

    if (emit_typed)
    {
        info_index = vkd3d_bindless_state_find_set_info_index_fast(device,
                VKD3D_BINDLESS_STATE_INFO_INDEX_MUTABLE_SPLIT_TYPED,
                VKD3D_BINDLESS_SET_SRV | VKD3D_BINDLESS_SET_BUFFER);

        binding = vkd3d_bindless_state_binding_from_info_index(&device->bindless_state, info_index);

        d.types->set_info_mask |= 1u << info_index;

        if (device->bindless_state.flags & VKD3D_TYPED_OFFSET_BUFFER)
            d.view->info.buffer.flags |= VKD3D_DESCRIPTOR_FLAG_BUFFER_OFFSET;
        d.types->single_binding = binding;

        vk_descriptor_type = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
        descriptor_qa_flags |= VKD3D_DESCRIPTOR_QA_TYPE_UNIFORM_TEXEL_BUFFER_BIT;

        if (d3d12_device_uses_descriptor_buffers(device))
        {
            get_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_GET_INFO_EXT;
            get_info.pNext = NULL;
            get_info.type = vk_descriptor_type;
            get_info.data.pUniformTexelBuffer = &addr_info;
            addr_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_ADDRESS_INFO_EXT;
            addr_info.pNext = NULL;
            addr_info.address = d.view->info.buffer.va;
            addr_info.range = d.view->info.buffer.range;
            addr_info.format = vkd3d_internal_get_vk_format(device, d.view->info.buffer.dxgi_format);
            /* If we really intended to emit raw buffers, the fallback will be inferred as R32_UINT. */
            if (addr_info.format == VK_FORMAT_UNDEFINED)
                addr_info.format = VK_FORMAT_R32_UINT;
            payload = d3d12_descriptor_heap_get_mapped_payload(d.heap, binding.set, d.offset);
            VK_CALL(vkGetDescriptorEXT(device->vk_device, &get_info,
                    device->device_info.descriptor_buffer_properties.robustUniformTexelBufferDescriptorSize,
                    payload));
        }
        else
        {
            vk_flags = vkd3d_view_flags_from_d3d12_buffer_srv_flags(desc->Buffer.Flags);
            if (!vkd3d_buffer_view_get_aligned_view(device, resource, desc->Format, vk_flags,
                    desc->Buffer.FirstElement, desc->Buffer.NumElements, desc->Buffer.StructureByteStride,
                    &bound_range, &view))
                return;

            descriptor_info[vk_write_count].buffer_view = view ? view->vk_buffer_view : VK_NULL_HANDLE;
            vkd3d_init_write_descriptor_set(&vk_write[vk_write_count], &d, binding,
                    vk_descriptor_type, &descriptor_info[vk_write_count]);
            vk_write_count++;
        }
    }

    if (d.view->info.buffer.flags & VKD3D_DESCRIPTOR_FLAG_BUFFER_OFFSET)
    {
        struct vkd3d_bound_buffer_range *buffer_ranges = d.heap->buffer_ranges.host_ptr;
        buffer_ranges[d.offset] = bound_range;
    }

    if (mutable_uses_single_descriptor)
        d.view->info.buffer.flags |= VKD3D_DESCRIPTOR_FLAG_SINGLE_DESCRIPTOR;

#ifdef VKD3D_ENABLE_DESCRIPTOR_QA
    if (vkd3d_config_flags & VKD3D_CONFIG_FLAG_INSTRUCTION_QA_CHECKS)
    {
        /* We may want to peek at the buffer's raw VA when doing instrumentation. */
        VkDeviceAddress *counter_addresses = d.heap->raw_va_aux_buffer.host_ptr;
        uint32_t elem_size;
        elem_size = desc->Format ? vkd3d_get_format(device, desc->Format, false)->byte_count : sizeof(uint32_t);
        counter_addresses[d.offset] = vkd3d_descriptor_debug_encode_buffer_va(d.view->info.buffer.va, elem_size);
        d.view->info.buffer.flags |= VKD3D_DESCRIPTOR_QA_TYPE_RAW_VA_BIT;
    }
#endif

    vkd3d_descriptor_metadata_view_set_qa_cookie(d.view, resource ? resource->res.cookie : 0);
    vkd3d_descriptor_debug_write_descriptor(d.heap->descriptor_heap_info.host_ptr,
            d.heap->cookie, d.offset, descriptor_qa_flags, d.view->qa_cookie);

    if (vk_write_count)
        VK_CALL(vkUpdateDescriptorSets(device->vk_device, vk_write_count, vk_write, 0, NULL));
}

static struct vkd3d_view *vkd3d_create_texture_uav_view(struct d3d12_device *device,
        struct d3d12_resource *resource, const D3D12_UNORDERED_ACCESS_VIEW_DESC *desc)
{
    struct vkd3d_view_key key;
    key.view_type = VKD3D_VIEW_TYPE_IMAGE;

    if (!init_default_texture_view_desc(&key.u.texture, resource, desc ? desc->Format : 0))
        return NULL;

    key.u.texture.image_usage = VK_IMAGE_USAGE_STORAGE_BIT;

    if (vkd3d_format_is_compressed(key.u.texture.format))
    {
        WARN("UAVs cannot be created for compressed formats.\n");
        return NULL;
    }

    if (desc)
    {
        switch (desc->ViewDimension)
        {
            case D3D12_UAV_DIMENSION_TEXTURE1D:
                key.u.texture.view_type = VK_IMAGE_VIEW_TYPE_1D;
                key.u.texture.miplevel_idx = desc->Texture1D.MipSlice;
                key.u.texture.layer_count = 1;
                break;
            case D3D12_UAV_DIMENSION_TEXTURE1DARRAY:
                key.u.texture.view_type = VK_IMAGE_VIEW_TYPE_1D_ARRAY;
                key.u.texture.miplevel_idx = desc->Texture1DArray.MipSlice;
                key.u.texture.layer_idx = desc->Texture1DArray.FirstArraySlice;
                key.u.texture.layer_count = desc->Texture1DArray.ArraySize;
                break;
            case D3D12_UAV_DIMENSION_TEXTURE2D:
                key.u.texture.view_type = VK_IMAGE_VIEW_TYPE_2D;
                key.u.texture.miplevel_idx = desc->Texture2D.MipSlice;
                key.u.texture.layer_count = 1;
                key.u.texture.aspect_mask = vk_image_aspect_flags_from_d3d12(resource->format, desc->Texture2D.PlaneSlice);
                break;
            case D3D12_UAV_DIMENSION_TEXTURE2DMS:
                key.u.texture.view_type = VK_IMAGE_VIEW_TYPE_2D;
                key.u.texture.miplevel_idx = 0;
                key.u.texture.layer_count = 1;
                key.u.texture.aspect_mask = vk_image_aspect_flags_from_d3d12(resource->format, desc->Texture2D.PlaneSlice);
                break;
            case D3D12_UAV_DIMENSION_TEXTURE2DARRAY:
                key.u.texture.view_type = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
                key.u.texture.miplevel_idx = desc->Texture2DArray.MipSlice;
                key.u.texture.layer_idx = desc->Texture2DArray.FirstArraySlice;
                key.u.texture.layer_count = desc->Texture2DArray.ArraySize;
                key.u.texture.aspect_mask = vk_image_aspect_flags_from_d3d12(resource->format, desc->Texture2DArray.PlaneSlice);
                break;
            case D3D12_UAV_DIMENSION_TEXTURE2DMSARRAY:
                key.u.texture.view_type = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
                key.u.texture.miplevel_idx = 0;
                key.u.texture.layer_idx = desc->Texture2DMSArray.FirstArraySlice;
                key.u.texture.layer_count = desc->Texture2DMSArray.ArraySize;
                key.u.texture.aspect_mask = vk_image_aspect_flags_from_d3d12(resource->format, 0);
                break;
            case D3D12_UAV_DIMENSION_TEXTURE3D:
                key.u.texture.view_type = VK_IMAGE_VIEW_TYPE_3D;
                key.u.texture.miplevel_idx = desc->Texture3D.MipSlice;
                key.u.texture.w_offset = desc->Texture3D.FirstWSlice;
                key.u.texture.w_size = desc->Texture3D.WSize;
                if (!device->device_info.image_sliced_view_of_3d_features.imageSlicedViewOf3D)
                {
                    if (desc->Texture3D.FirstWSlice ||
                            ((desc->Texture3D.WSize != max(1u, (UINT)resource->desc.DepthOrArraySize >> desc->Texture3D.MipSlice)) &&
                                    desc->Texture3D.WSize != UINT_MAX))
                    {
                        FIXME("Unhandled depth view %u-%u.\n",
                                desc->Texture3D.FirstWSlice, desc->Texture3D.WSize);
                    }
                }
                break;
            default:
                FIXME("Unhandled view dimension %#x.\n", desc->ViewDimension);
        }
    }

    return vkd3d_view_map_create_view(&resource->view_map, device, &key);
}

static struct vkd3d_view *vkd3d_create_texture_srv_view(struct d3d12_device *device,
        struct d3d12_resource *resource, const D3D12_SHADER_RESOURCE_VIEW_DESC *desc)
{
    struct vkd3d_view_key key;

    if (!init_default_texture_view_desc(&key.u.texture, resource, desc ? desc->Format : 0))
        return NULL;

    key.view_type = VKD3D_VIEW_TYPE_IMAGE;
    key.u.texture.miplevel_count = VK_REMAINING_MIP_LEVELS;
    key.u.texture.allowed_swizzle = true;
    key.u.texture.image_usage = VK_IMAGE_USAGE_SAMPLED_BIT;

    if (desc)
    {
        if (desc->Shader4ComponentMapping != D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING)
        {
            TRACE("Component mapping %s for format %#x.\n",
                    debug_d3d12_shader_component_mapping(desc->Shader4ComponentMapping), desc->Format);

            vk_component_mapping_from_d3d12(&key.u.texture.components, desc->Shader4ComponentMapping);
        }

        switch (desc->ViewDimension)
        {
            case D3D12_SRV_DIMENSION_TEXTURE1D:
                key.u.texture.view_type = VK_IMAGE_VIEW_TYPE_1D;
                key.u.texture.miplevel_idx = desc->Texture1D.MostDetailedMip;
                key.u.texture.miplevel_count = desc->Texture1D.MipLevels;
                key.u.texture.miplevel_clamp = desc->Texture1D.ResourceMinLODClamp;
                key.u.texture.layer_count = 1;
                break;
            case D3D12_SRV_DIMENSION_TEXTURE1DARRAY:
                key.u.texture.view_type = VK_IMAGE_VIEW_TYPE_1D_ARRAY;
                key.u.texture.miplevel_idx = desc->Texture1DArray.MostDetailedMip;
                key.u.texture.miplevel_count = desc->Texture1DArray.MipLevels;
                key.u.texture.miplevel_clamp = desc->Texture1DArray.ResourceMinLODClamp;
                key.u.texture.layer_idx = desc->Texture1DArray.FirstArraySlice;
                key.u.texture.layer_count = desc->Texture1DArray.ArraySize;
                break;
            case D3D12_SRV_DIMENSION_TEXTURE2D:
                key.u.texture.view_type = VK_IMAGE_VIEW_TYPE_2D;
                key.u.texture.miplevel_idx = desc->Texture2D.MostDetailedMip;
                key.u.texture.miplevel_count = desc->Texture2D.MipLevels;
                key.u.texture.miplevel_clamp = desc->Texture2D.ResourceMinLODClamp;
                key.u.texture.layer_count = 1;
                key.u.texture.aspect_mask = vk_image_aspect_flags_from_d3d12(resource->format, desc->Texture2D.PlaneSlice);
                break;
            case D3D12_SRV_DIMENSION_TEXTURE2DARRAY:
                key.u.texture.view_type = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
                key.u.texture.miplevel_idx = desc->Texture2DArray.MostDetailedMip;
                key.u.texture.miplevel_count = desc->Texture2DArray.MipLevels;
                key.u.texture.miplevel_clamp = desc->Texture2DArray.ResourceMinLODClamp;
                key.u.texture.layer_idx = desc->Texture2DArray.FirstArraySlice;
                key.u.texture.layer_count = desc->Texture2DArray.ArraySize;
                key.u.texture.aspect_mask = vk_image_aspect_flags_from_d3d12(resource->format, desc->Texture2DArray.PlaneSlice);
                break;
            case D3D12_SRV_DIMENSION_TEXTURE2DMS:
                key.u.texture.view_type = VK_IMAGE_VIEW_TYPE_2D;
                key.u.texture.layer_count = 1;
                break;
            case D3D12_SRV_DIMENSION_TEXTURE2DMSARRAY:
                key.u.texture.view_type = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
                key.u.texture.layer_idx = desc->Texture2DMSArray.FirstArraySlice;
                key.u.texture.layer_count = desc->Texture2DMSArray.ArraySize;
                break;
            case D3D12_SRV_DIMENSION_TEXTURE3D:
                key.u.texture.view_type = VK_IMAGE_VIEW_TYPE_3D;
                key.u.texture.miplevel_idx = desc->Texture3D.MostDetailedMip;
                key.u.texture.miplevel_count = desc->Texture3D.MipLevels;
                key.u.texture.miplevel_clamp = desc->Texture3D.ResourceMinLODClamp;
                break;
            case D3D12_SRV_DIMENSION_TEXTURECUBE:
                key.u.texture.view_type = VK_IMAGE_VIEW_TYPE_CUBE;
                key.u.texture.miplevel_idx = desc->TextureCube.MostDetailedMip;
                key.u.texture.miplevel_count = desc->TextureCube.MipLevels;
                key.u.texture.miplevel_clamp = desc->TextureCube.ResourceMinLODClamp;
                key.u.texture.layer_count = 6;
                break;
            case D3D12_SRV_DIMENSION_TEXTURECUBEARRAY:
                key.u.texture.view_type = VK_IMAGE_VIEW_TYPE_CUBE_ARRAY;
                key.u.texture.miplevel_idx = desc->TextureCubeArray.MostDetailedMip;
                key.u.texture.miplevel_count = desc->TextureCubeArray.MipLevels;
                key.u.texture.miplevel_clamp = desc->TextureCubeArray.ResourceMinLODClamp;
                key.u.texture.layer_idx = desc->TextureCubeArray.First2DArrayFace;
                key.u.texture.layer_count = desc->TextureCubeArray.NumCubes;
                if (key.u.texture.layer_count != VK_REMAINING_ARRAY_LAYERS)
                    key.u.texture.layer_count *= 6;
                break;
            default:
                FIXME("Unhandled view dimension %#x.\n", desc->ViewDimension);
        }
    }

    if (key.u.texture.miplevel_count == VK_REMAINING_MIP_LEVELS)
        key.u.texture.miplevel_count = resource->desc.MipLevels - key.u.texture.miplevel_idx;

    if ((vkd3d_config_flags & VKD3D_CONFIG_FLAG_PREALLOCATE_SRV_MIP_CLAMPS) &&
            desc->ViewDimension != D3D12_SRV_DIMENSION_TEXTURE2DMS &&
            desc->ViewDimension != D3D12_SRV_DIMENSION_TEXTURE2DMSARRAY)
    {
        bool found;

        key.u.texture.miplevel_clamp = floor(key.u.texture.miplevel_clamp);

        rw_spinlock_acquire_read(&resource->view_map.spinlock);
        found = !!hash_map_find(&resource->view_map.map, &key);
        rw_spinlock_release_read(&resource->view_map.spinlock);

        if (!found)
        {
            uint32_t starting_mip = key.u.texture.miplevel_idx;
            uint32_t mip_count = key.u.texture.miplevel_count != UINT32_MAX ?
                    key.u.texture.miplevel_count :
                    resource->desc.MipLevels - starting_mip;
            uint32_t i;

            struct vkd3d_view_key preallocate_key = key;

            for (i = starting_mip; i < mip_count; i++)
            {
                preallocate_key.u.texture.miplevel_clamp = (float)(i);
                vkd3d_view_map_create_view(&resource->view_map, device, &preallocate_key);
            }
        }
    }

    return vkd3d_view_map_create_view(&resource->view_map, device, &key);
}

static void vkd3d_create_texture_srv_embedded(vkd3d_cpu_descriptor_va_t desc_va,
        struct d3d12_device *device, struct d3d12_resource *resource,
        const D3D12_SHADER_RESOURCE_VIEW_DESC *desc)
{
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    struct d3d12_desc_split_embedded d;
    VkDescriptorGetInfoEXT get_info;
    struct vkd3d_view *view = NULL;
    VkDescriptorImageInfo image;

    if (!resource)
    {
        d3d12_descriptor_heap_write_null_descriptor_template_embedded(device, desc_va, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE);
        return;
    }

    d = d3d12_desc_decode_embedded_resource_va(desc_va);

    /* Ignore metadata. */
    view = vkd3d_create_texture_srv_view(device, resource, desc);

    image.sampler = VK_NULL_HANDLE;
    image.imageView = view ? view->vk_image_view : VK_NULL_HANDLE;
    image.imageLayout = view ? resource->common_layout : VK_IMAGE_LAYOUT_UNDEFINED;

    get_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_GET_INFO_EXT;
    get_info.pNext = NULL;
    get_info.type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    get_info.data.pSampledImage = &image;
    VK_CALL(vkGetDescriptorEXT(device->vk_device, &get_info,
            device->device_info.descriptor_buffer_properties.sampledImageDescriptorSize,
            d.payload));
}

static void vkd3d_create_texture_srv(vkd3d_cpu_descriptor_va_t desc_va,
        struct d3d12_device *device, struct d3d12_resource *resource,
        const D3D12_SHADER_RESOURCE_VIEW_DESC *desc)
{
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    union vkd3d_descriptor_info null_descriptor_info;
    union vkd3d_descriptor_info descriptor_info;
    struct vkd3d_descriptor_binding binding;
    VkWriteDescriptorSet vk_writes[2];
    VkDescriptorGetInfoEXT get_info;
    struct vkd3d_view *view = NULL;
    uint32_t vk_write_count = 0;
    struct d3d12_desc_split d;
    uint32_t info_index;
    void *payload;

    if (!resource)
    {
        d3d12_descriptor_heap_write_null_descriptor_template(desc_va, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE);
        return;
    }

    d = d3d12_desc_decode_va(desc_va);

    view = vkd3d_create_texture_srv_view(device, resource, desc);

    descriptor_info.image.sampler = VK_NULL_HANDLE;
    descriptor_info.image.imageView = view ? view->vk_image_view : VK_NULL_HANDLE;
    descriptor_info.image.imageLayout = view ? resource->common_layout : VK_IMAGE_LAYOUT_UNDEFINED;

    info_index = vkd3d_bindless_state_find_set_info_index_fast(device,
            VKD3D_BINDLESS_STATE_INFO_INDEX_MUTABLE_SPLIT_TYPED,
            VKD3D_BINDLESS_SET_SRV | VKD3D_BINDLESS_SET_IMAGE);

    binding = vkd3d_bindless_state_binding_from_info_index(&device->bindless_state, info_index);

    d.view->info.image.view = view;
    d.view->info.image.flags = VKD3D_DESCRIPTOR_FLAG_IMAGE_VIEW | VKD3D_DESCRIPTOR_FLAG_NON_NULL;
    d.types->set_info_mask = 1u << info_index;
    d.types->single_binding = binding;

    if (d3d12_device_uses_descriptor_buffers(device))
    {
        get_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_GET_INFO_EXT;
        get_info.pNext = NULL;
        get_info.type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        get_info.data.pSampledImage = &descriptor_info.image;
        payload = d3d12_descriptor_heap_get_mapped_payload(d.heap, binding.set, d.offset);
        VK_CALL(vkGetDescriptorEXT(device->vk_device, &get_info,
                device->device_info.descriptor_buffer_properties.sampledImageDescriptorSize,
                payload));
    }
    else
    {
        vkd3d_init_write_descriptor_set(&vk_writes[vk_write_count++], &d, binding,
                VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, &descriptor_info);
    }

    /* Clear out sibling typed descriptor if appropriate.
     * Native drivers are robust against scenarios like these, and so we need to be too. */
    if (device->bindless_state.flags & VKD3D_BINDLESS_MUTABLE_TYPE_SPLIT_RAW_TYPED)
    {
        /* The NULL descriptor template for non-embedded is too messy to invoke here. */
        info_index = VKD3D_BINDLESS_STATE_INFO_INDEX_MUTABLE_SPLIT_RAW;
        binding = vkd3d_bindless_state_binding_from_info_index(&device->bindless_state, info_index);
        d.types->set_info_mask |= 1u << info_index;

        if (d3d12_device_uses_descriptor_buffers(device))
        {
            get_info.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            get_info.data.pStorageBuffer = NULL;
            payload = d3d12_descriptor_heap_get_mapped_payload(d.heap, binding.set, d.offset);
            VK_CALL(vkGetDescriptorEXT(device->vk_device, &get_info,
                    device->device_info.descriptor_buffer_properties.robustStorageBufferDescriptorSize,
                    payload));
        }
        else
        {
            null_descriptor_info.buffer.buffer = VK_NULL_HANDLE;
            null_descriptor_info.buffer.offset = 0;
            null_descriptor_info.buffer.range = VK_WHOLE_SIZE;
            vkd3d_init_write_descriptor_set(&vk_writes[vk_write_count++], &d, binding,
                    VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &null_descriptor_info);
        }
    }
    else
        d.view->info.image.flags |= VKD3D_DESCRIPTOR_FLAG_SINGLE_DESCRIPTOR;

    if (vk_write_count)
        VK_CALL(vkUpdateDescriptorSets(device->vk_device, vk_write_count, vk_writes, 0, NULL));

    vkd3d_descriptor_metadata_view_set_qa_cookie(d.view, view ? view->cookie : 0);
    vkd3d_descriptor_debug_write_descriptor(d.heap->descriptor_heap_info.host_ptr,
            d.heap->cookie, d.offset,
            VKD3D_DESCRIPTOR_QA_TYPE_SAMPLED_IMAGE_BIT, d.view->qa_cookie);
}

void d3d12_desc_create_srv_embedded(vkd3d_cpu_descriptor_va_t desc_va,
        struct d3d12_device *device, struct d3d12_resource *resource,
        const D3D12_SHADER_RESOURCE_VIEW_DESC *desc)
{
    bool is_buffer;

    if (resource)
    {
        is_buffer = d3d12_resource_is_buffer(resource);
    }
    else if (desc)
    {
        is_buffer = desc->ViewDimension == D3D12_SRV_DIMENSION_BUFFER ||
                desc->ViewDimension == D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE;
    }
    else
    {
        WARN("Description required for NULL SRV.\n");
        return;
    }

    if (is_buffer)
        vkd3d_create_buffer_srv_embedded(desc_va, device, resource, desc);
    else
        vkd3d_create_texture_srv_embedded(desc_va, device, resource, desc);
}

void d3d12_desc_create_srv(vkd3d_cpu_descriptor_va_t desc_va,
        struct d3d12_device *device, struct d3d12_resource *resource,
        const D3D12_SHADER_RESOURCE_VIEW_DESC *desc)
{
    bool is_buffer;

    if (resource)
    {
        is_buffer = d3d12_resource_is_buffer(resource);
    }
    else if (desc)
    {
        is_buffer = desc->ViewDimension == D3D12_SRV_DIMENSION_BUFFER ||
                desc->ViewDimension == D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE;
    }
    else
    {
        WARN("Description required for NULL SRV.\n");
        return;
    }

    if (is_buffer)
        vkd3d_create_buffer_srv(desc_va, device, resource, desc);
    else
        vkd3d_create_texture_srv(desc_va, device, resource, desc);
}

static unsigned int vkd3d_view_flags_from_d3d12_buffer_uav_flags(D3D12_BUFFER_UAV_FLAGS flags)
{
    if (flags == D3D12_BUFFER_UAV_FLAG_RAW)
        return VKD3D_VIEW_RAW_BUFFER;
    if (flags)
        FIXME("Unhandled buffer UAV flags %#x.\n", flags);
    return 0;
}

VkDeviceAddress vkd3d_get_buffer_device_address(struct d3d12_device *device, VkBuffer vk_buffer)
{
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;

    VkBufferDeviceAddressInfo address_info;
    address_info.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    address_info.pNext = NULL;
    address_info.buffer = vk_buffer;

    return VK_CALL(vkGetBufferDeviceAddress(device->vk_device, &address_info));
}

VkDeviceAddress vkd3d_get_acceleration_structure_device_address(struct d3d12_device *device,
        VkAccelerationStructureKHR vk_acceleration_structure)
{
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;

    VkAccelerationStructureDeviceAddressInfoKHR address_info;
    address_info.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
    address_info.pNext = NULL;
    address_info.accelerationStructure = vk_acceleration_structure;

    return VK_CALL(vkGetAccelerationStructureDeviceAddressKHR(device->vk_device, &address_info));
}

static void vkd3d_create_buffer_uav_embedded(vkd3d_cpu_descriptor_va_t desc_va, struct d3d12_device *device,
        struct d3d12_resource *resource, struct d3d12_resource *counter_resource,
        const D3D12_UNORDERED_ACCESS_VIEW_DESC *desc)
{
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    struct vkd3d_descriptor_metadata_buffer_view view;
    VkDescriptorAddressInfoEXT addr_info;
    struct d3d12_desc_split_embedded d;
    struct d3d12_desc_split_metadata m;
    VkDescriptorGetInfoEXT get_info;

    if (!desc)
    {
        FIXME("Default buffer UAV not supported.\n");
        return;
    }

    if (desc->ViewDimension != D3D12_UAV_DIMENSION_BUFFER)
    {
        WARN("Unexpected view dimension %#x.\n", desc->ViewDimension);
        return;
    }

    if (!resource)
    {
        /* We prepare a packed NULL descriptor that contains both texel buffer and SSBO. */
        d3d12_descriptor_heap_write_null_descriptor_template_embedded(device, desc_va,
                VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER);
        return;
    }

    d = d3d12_desc_decode_embedded_resource_va(desc_va);
    m = d3d12_desc_decode_metadata(device, desc_va);

    vkd3d_get_metadata_buffer_view_for_resource(device, resource,
            desc->Format, desc->Buffer.FirstElement, desc->Buffer.NumElements,
            desc->Buffer.StructureByteStride, &view);

    if (m.view)
        m.view->info.buffer = view;

    get_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_GET_INFO_EXT;
    get_info.pNext = NULL;
    get_info.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    get_info.data.pStorageBuffer = &addr_info;
    addr_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_ADDRESS_INFO_EXT;
    addr_info.pNext = NULL;
    addr_info.address = view.va;
    addr_info.range = view.range;
    addr_info.format = VK_FORMAT_UNDEFINED;
    VK_CALL(vkGetDescriptorEXT(device->vk_device, &get_info,
            device->device_info.descriptor_buffer_properties.robustStorageBufferDescriptorSize,
            d.payload + device->bindless_state.descriptor_buffer_packed_raw_buffer_offset));

    /* UAV counter and texel buffers alias. This is fine. We don't expect having to work around
     * scenarios where this happens.
     * Reuse the texel buffer system here directly.
     * We might as well use it when it's practical. */

    get_info.type = VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER;
    get_info.data.pStorageTexelBuffer = &addr_info;

    if (resource && counter_resource)
    {
        assert(d3d12_resource_is_buffer(counter_resource));
        assert(desc->Buffer.StructureByteStride);
        addr_info.address = counter_resource->res.va + desc->Buffer.CounterOffsetInBytes;
        addr_info.range = 4;
        addr_info.format = VK_FORMAT_R32_UINT;
    }
    else
    {
        addr_info.format = vkd3d_internal_get_vk_format(device, view.dxgi_format);
        /* If we really intended to emit raw buffers, the fallback will be inferred as R32_UINT. */
        if (addr_info.format == VK_FORMAT_UNDEFINED)
            addr_info.format = VK_FORMAT_R32_UINT;
    }

    VK_CALL(vkGetDescriptorEXT(device->vk_device, &get_info,
            device->device_info.descriptor_buffer_properties.robustStorageTexelBufferDescriptorSize,
            d.payload));
}

static void vkd3d_create_buffer_uav(vkd3d_cpu_descriptor_va_t desc_va, struct d3d12_device *device,
        struct d3d12_resource *resource, struct d3d12_resource *counter_resource,
        const D3D12_UNORDERED_ACCESS_VIEW_DESC *desc)
{
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    VKD3D_UNUSED vkd3d_descriptor_qa_flags descriptor_qa_flags = 0;
    struct vkd3d_bound_buffer_range bound_range = { 0, 0, 0, 0 };
    union vkd3d_descriptor_info descriptor_info[3];
    struct vkd3d_descriptor_binding binding;
    unsigned int flags, vk_write_count = 0;
    VkDescriptorAddressInfoEXT addr_info;
    bool mutable_uses_single_descriptor;
    VkDescriptorType vk_descriptor_type;
    VkDeviceAddress uav_counter_address;
    VkDeviceAddress *counter_addresses;
    VkWriteDescriptorSet vk_write[3];
    VkDescriptorGetInfoEXT get_info;
    struct vkd3d_view *view = NULL;
    struct d3d12_desc_split d;
    uint32_t descriptor_index;
    uint32_t info_index;
    bool desc_is_raw;
    bool emit_typed;
    bool emit_ssbo;
    void *payload;

    if (!desc)
    {
        FIXME("Default buffer UAV not supported.\n");
        return;
    }

    if (desc->ViewDimension != D3D12_UAV_DIMENSION_BUFFER)
    {
        WARN("Unexpected view dimension %#x.\n", desc->ViewDimension);
        return;
    }

    mutable_uses_single_descriptor = !!(device->bindless_state.flags & VKD3D_BINDLESS_MUTABLE_TYPE_RAW_SSBO);
    desc_is_raw = (desc->Format == DXGI_FORMAT_UNKNOWN && desc->Buffer.StructureByteStride) ||
            (desc->Buffer.Flags & D3D12_BUFFER_UAV_FLAG_RAW);
    emit_ssbo = (!mutable_uses_single_descriptor || desc_is_raw) && d3d12_device_use_ssbo_raw_buffer(device);
    emit_typed = !mutable_uses_single_descriptor || !desc_is_raw || !emit_ssbo;

    if (!resource)
    {
        if (mutable_uses_single_descriptor)
        {
            d3d12_descriptor_heap_write_null_descriptor_template(desc_va,
                    desc_is_raw ? VK_DESCRIPTOR_TYPE_STORAGE_BUFFER : VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER);
        }
        else
        {
            /* In the mutable set, always write texel buffer. The STORAGE_BUFFER set is also written to. */
            d3d12_descriptor_heap_write_null_descriptor_template(desc_va,
                    VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER);
        }
        return;
    }

    d = d3d12_desc_decode_va(desc_va);

    /* Handle UAV itself */
    d.types->set_info_mask = 0;

    vkd3d_get_metadata_buffer_view_for_resource(device, resource,
            desc->Format, desc->Buffer.FirstElement, desc->Buffer.NumElements,
            desc->Buffer.StructureByteStride, &d.view->info.buffer);
    d.view->info.buffer.flags |= VKD3D_DESCRIPTOR_FLAG_RAW_VA_AUX_BUFFER;

    if (emit_ssbo)
    {
        info_index = vkd3d_bindless_state_find_set_info_index_fast(device,
                VKD3D_BINDLESS_STATE_INFO_INDEX_MUTABLE_SPLIT_RAW,
                VKD3D_BINDLESS_SET_UAV | VKD3D_BINDLESS_SET_RAW_SSBO);

        binding = vkd3d_bindless_state_binding_from_info_index(&device->bindless_state, info_index);

        d.types->set_info_mask |= 1u << info_index;

        if (device->bindless_state.flags & VKD3D_SSBO_OFFSET_BUFFER)
            d.view->info.buffer.flags |= VKD3D_DESCRIPTOR_FLAG_BUFFER_OFFSET;
        d.types->single_binding = binding;

        vk_descriptor_type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        descriptor_qa_flags |= VKD3D_DESCRIPTOR_QA_TYPE_STORAGE_BUFFER_BIT;

        if (d3d12_device_uses_descriptor_buffers(device))
        {
            get_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_GET_INFO_EXT;
            get_info.pNext = NULL;
            get_info.type = vk_descriptor_type;
            get_info.data.pStorageBuffer = &addr_info;
            addr_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_ADDRESS_INFO_EXT;
            addr_info.pNext = NULL;
            addr_info.address = d.view->info.buffer.va;
            addr_info.range = d.view->info.buffer.range;
            addr_info.format = VK_FORMAT_UNDEFINED;
            payload = d3d12_descriptor_heap_get_mapped_payload(d.heap, binding.set, d.offset);
            VK_CALL(vkGetDescriptorEXT(device->vk_device, &get_info,
                    device->device_info.descriptor_buffer_properties.robustStorageBufferDescriptorSize,
                    payload));
        }
        else
        {
            VkDescriptorBufferInfo *buffer_info = &descriptor_info[vk_write_count].buffer;
            VkDeviceSize stride = desc->Format == DXGI_FORMAT_UNKNOWN
                    ? desc->Buffer.StructureByteStride :
                    vkd3d_get_format(device, desc->Format, false)->byte_count;

            vkd3d_buffer_view_get_bound_range_ssbo(device, resource,
                    desc->Buffer.FirstElement * stride, desc->Buffer.NumElements * stride,
                    buffer_info, &bound_range);

            vkd3d_init_write_descriptor_set(&vk_write[vk_write_count], &d, binding,
                    vk_descriptor_type, &descriptor_info[vk_write_count]);
            vk_write_count++;
        }
    }

    if (emit_typed)
    {
        info_index = vkd3d_bindless_state_find_set_info_index_fast(device,
                VKD3D_BINDLESS_STATE_INFO_INDEX_MUTABLE_SPLIT_TYPED,
                VKD3D_BINDLESS_SET_UAV | VKD3D_BINDLESS_SET_BUFFER);

        binding = vkd3d_bindless_state_binding_from_info_index(&device->bindless_state, info_index);

        d.types->set_info_mask |= 1u << info_index;

        if (device->bindless_state.flags & VKD3D_TYPED_OFFSET_BUFFER)
            d.view->info.buffer.flags |= VKD3D_DESCRIPTOR_FLAG_BUFFER_OFFSET;
        d.types->single_binding = binding;

        vk_descriptor_type = VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER;
        descriptor_qa_flags |= VKD3D_DESCRIPTOR_QA_TYPE_STORAGE_TEXEL_BUFFER_BIT;

        if (d3d12_device_uses_descriptor_buffers(device))
        {
            get_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_GET_INFO_EXT;
            get_info.pNext = NULL;
            get_info.type = vk_descriptor_type;
            get_info.data.pStorageTexelBuffer = &addr_info;
            addr_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_ADDRESS_INFO_EXT;
            addr_info.pNext = NULL;
            addr_info.address = d.view->info.buffer.va;
            addr_info.range = d.view->info.buffer.range;
            addr_info.format = vkd3d_internal_get_vk_format(device, d.view->info.buffer.dxgi_format);
            /* If we really intended to emit raw buffers, the fallback will be inferred as R32_UINT. */
            if (addr_info.format == VK_FORMAT_UNDEFINED)
                addr_info.format = VK_FORMAT_R32_UINT;
            payload = d3d12_descriptor_heap_get_mapped_payload(d.heap, binding.set, d.offset);
            VK_CALL(vkGetDescriptorEXT(device->vk_device, &get_info,
                    device->device_info.descriptor_buffer_properties.robustStorageTexelBufferDescriptorSize,
                    payload));
        }
        else
        {
            flags = vkd3d_view_flags_from_d3d12_buffer_uav_flags(desc->Buffer.Flags);

            if (!vkd3d_buffer_view_get_aligned_view(device, resource, desc->Format, flags,
                    desc->Buffer.FirstElement, desc->Buffer.NumElements,
                    desc->Buffer.StructureByteStride, &bound_range, &view))
                return;

            descriptor_info[vk_write_count].buffer_view = view ? view->vk_buffer_view : VK_NULL_HANDLE;

            vkd3d_init_write_descriptor_set(&vk_write[vk_write_count], &d, binding,
                    vk_descriptor_type, &descriptor_info[vk_write_count]);
            vk_write_count++;
        }
    }

    if (d.view->info.buffer.flags & VKD3D_DESCRIPTOR_FLAG_BUFFER_OFFSET)
    {
        struct vkd3d_bound_buffer_range *buffer_ranges = d.heap->buffer_ranges.host_ptr;
        buffer_ranges[d.offset] = bound_range;
    }

    if (mutable_uses_single_descriptor)
        d.view->info.buffer.flags |= VKD3D_DESCRIPTOR_FLAG_SINGLE_DESCRIPTOR;

    /* Handle UAV counter */
    uav_counter_address = 0;

    if (resource && counter_resource)
    {
        assert(d3d12_resource_is_buffer(counter_resource));
        assert(desc->Buffer.StructureByteStride);

        uav_counter_address = counter_resource->res.va + desc->Buffer.CounterOffsetInBytes;

        /* This is used to denote that a counter descriptor is present, irrespective of underlying descriptor type. */
        descriptor_qa_flags |= VKD3D_DESCRIPTOR_QA_TYPE_RAW_VA_BIT;
    }
#ifdef VKD3D_ENABLE_DESCRIPTOR_QA
    else if (vkd3d_config_flags & VKD3D_CONFIG_FLAG_INSTRUCTION_QA_CHECKS)
    {
        /* We may want to peek at the buffer's raw VA when doing instrumentation.
         * UAV counted resources do not get instrumentation, so the aliasing should not be a problem in practice. */
        uint32_t elem_size;
        elem_size = desc->Format ? vkd3d_get_format(device, desc->Format, false)->byte_count : sizeof(uint32_t);
        uav_counter_address = vkd3d_descriptor_debug_encode_buffer_va(d.view->info.buffer.va, elem_size);
        d.view->info.buffer.flags |= VKD3D_DESCRIPTOR_QA_TYPE_RAW_VA_BIT;
    }
#endif

    counter_addresses = d.heap->raw_va_aux_buffer.host_ptr;
    descriptor_index = d.offset;
    counter_addresses[descriptor_index] = uav_counter_address;

    vkd3d_descriptor_metadata_view_set_qa_cookie(d.view, resource ? resource->res.cookie : 0);
    vkd3d_descriptor_debug_write_descriptor(d.heap->descriptor_heap_info.host_ptr,
            d.heap->cookie, d.offset,
            descriptor_qa_flags, d.view->qa_cookie);

    VK_CALL(vkUpdateDescriptorSets(device->vk_device, vk_write_count, vk_write, 0, NULL));
}

static void vkd3d_create_texture_uav_embedded(vkd3d_cpu_descriptor_va_t desc_va,
        struct d3d12_device *device, struct d3d12_resource *resource,
        const D3D12_UNORDERED_ACCESS_VIEW_DESC *desc)
{
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    struct d3d12_desc_split_embedded d;
    struct d3d12_desc_split_metadata m;
    VkDescriptorGetInfoEXT get_info;
    struct vkd3d_view *view = NULL;
    VkDescriptorImageInfo image;

    d = d3d12_desc_decode_embedded_resource_va(desc_va);
    m = d3d12_desc_decode_metadata(device, desc_va);

    if (!resource)
    {
        d3d12_descriptor_heap_write_null_descriptor_template_embedded(device, desc_va, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
        return;
    }

    view = vkd3d_create_texture_uav_view(device, resource, desc);

    image.sampler = VK_NULL_HANDLE;
    image.imageView = view ? view->vk_image_view : VK_NULL_HANDLE;
    image.imageLayout = view ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_UNDEFINED;

    if (m.view)
    {
        m.view->info.image.view = view;
        m.view->info.image.flags = VKD3D_DESCRIPTOR_FLAG_IMAGE_VIEW;
    }

    get_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_GET_INFO_EXT;
    get_info.pNext = NULL;
    get_info.type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    get_info.data.pStorageImage = &image;
    VK_CALL(vkGetDescriptorEXT(device->vk_device, &get_info,
            device->device_info.descriptor_buffer_properties.storageImageDescriptorSize,
            d.payload));

    /* We should clear out the sibling raw resource that is packed in the higher bits.
     * If we have planar metadata there isn't much we can do since the storage image will take up the entire
     * 32 bytes, but reading an image as SSBO is far less common than reading buffers as images. */
    if (device->bindless_state.flags & VKD3D_BINDLESS_MUTABLE_EMBEDDED_PACKED_METADATA)
    {
        /* See vkd3d_bindless_state_init_null_descriptor_payloads for details.
         * Use STORAGE_IMAGE template here, since we've already prepared the desired NULL payload
         * at the raw offset. */
        d3d12_descriptor_heap_write_null_descriptor_template_embedded_partial(device, desc_va,
                VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, device->bindless_state.descriptor_buffer_packed_raw_buffer_offset,
                device->device_info.descriptor_buffer_properties.robustStorageBufferDescriptorSize);
    }
}

static void vkd3d_create_texture_uav(vkd3d_cpu_descriptor_va_t desc_va,
        struct d3d12_device *device, struct d3d12_resource *resource,
        const D3D12_UNORDERED_ACCESS_VIEW_DESC *desc)
{
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    union vkd3d_descriptor_info null_descriptor_info;
    union vkd3d_descriptor_info descriptor_info;
    struct vkd3d_descriptor_binding binding;
    VkWriteDescriptorSet vk_writes[2];
    VkDescriptorGetInfoEXT get_info;
    struct vkd3d_view *view = NULL;
    uint32_t vk_write_count = 0;
    struct d3d12_desc_split d;
    uint32_t info_index;
    void *payload;

    if (!resource)
    {
        d3d12_descriptor_heap_write_null_descriptor_template(desc_va, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
        return;
    }

    d = d3d12_desc_decode_va(desc_va);

    view = vkd3d_create_texture_uav_view(device, resource, desc);

    descriptor_info.image.sampler = VK_NULL_HANDLE;
    descriptor_info.image.imageView = view ? view->vk_image_view : VK_NULL_HANDLE;
    descriptor_info.image.imageLayout = view ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_UNDEFINED;

    info_index = vkd3d_bindless_state_find_set_info_index_fast(device,
            VKD3D_BINDLESS_STATE_INFO_INDEX_MUTABLE_SPLIT_TYPED,
            VKD3D_BINDLESS_SET_UAV | VKD3D_BINDLESS_SET_IMAGE);
    binding = vkd3d_bindless_state_binding_from_info_index(&device->bindless_state, info_index);

    d.view->info.image.view = view;
    d.view->info.image.flags = VKD3D_DESCRIPTOR_FLAG_IMAGE_VIEW | VKD3D_DESCRIPTOR_FLAG_NON_NULL;
    d.types->set_info_mask = 1u << info_index;
    d.types->single_binding = binding;

    if (d3d12_device_uses_descriptor_buffers(device))
    {
        get_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_GET_INFO_EXT;
        get_info.pNext = NULL;
        get_info.type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        get_info.data.pSampledImage = &descriptor_info.image;
        payload = d3d12_descriptor_heap_get_mapped_payload(d.heap, binding.set, d.offset);
        VK_CALL(vkGetDescriptorEXT(device->vk_device, &get_info,
                device->device_info.descriptor_buffer_properties.storageImageDescriptorSize,
                payload));
    }
    else
    {
        vkd3d_init_write_descriptor_set(&vk_writes[vk_write_count++], &d, binding,
                VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &descriptor_info);
    }

    /* Clear out sibling typed descriptor if appropriate.
     * Native drivers are robust against scenarios like these, and so we need to be too. */
    if (device->bindless_state.flags & VKD3D_BINDLESS_MUTABLE_TYPE_SPLIT_RAW_TYPED)
    {
        /* The NULL descriptor template for non-embedded is too messy to invoke here. */
        info_index = VKD3D_BINDLESS_STATE_INFO_INDEX_MUTABLE_SPLIT_RAW;
        binding = vkd3d_bindless_state_binding_from_info_index(&device->bindless_state, info_index);
        d.types->set_info_mask |= 1u << info_index;

        if (d3d12_device_uses_descriptor_buffers(device))
        {
            get_info.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            get_info.data.pStorageBuffer = NULL;
            payload = d3d12_descriptor_heap_get_mapped_payload(d.heap, binding.set, d.offset);
            VK_CALL(vkGetDescriptorEXT(device->vk_device, &get_info,
                    device->device_info.descriptor_buffer_properties.robustStorageBufferDescriptorSize,
                    payload));
        }
        else
        {
            null_descriptor_info.buffer.buffer = VK_NULL_HANDLE;
            null_descriptor_info.buffer.offset = 0;
            null_descriptor_info.buffer.range = VK_WHOLE_SIZE;
            vkd3d_init_write_descriptor_set(&vk_writes[vk_write_count++], &d, binding,
                    VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &null_descriptor_info);
        }
    }
    else
        d.view->info.image.flags |= VKD3D_DESCRIPTOR_FLAG_SINGLE_DESCRIPTOR;

    if (vk_write_count)
        VK_CALL(vkUpdateDescriptorSets(device->vk_device, vk_write_count, vk_writes, 0, NULL));

    vkd3d_descriptor_metadata_view_set_qa_cookie(d.view, view ? view->cookie : 0);
    vkd3d_descriptor_debug_write_descriptor(d.heap->descriptor_heap_info.host_ptr,
            d.heap->cookie, d.offset,
            VKD3D_DESCRIPTOR_QA_TYPE_STORAGE_IMAGE_BIT, d.view->qa_cookie);
}

void d3d12_desc_create_uav_embedded(vkd3d_cpu_descriptor_va_t desc_va, struct d3d12_device *device,
        struct d3d12_resource *resource, struct d3d12_resource *counter_resource,
        const D3D12_UNORDERED_ACCESS_VIEW_DESC *desc)
{
    bool is_buffer;

    if (resource)
    {
        is_buffer = d3d12_resource_is_buffer(resource);
    }
    else if (desc)
    {
        is_buffer = desc->ViewDimension == D3D12_UAV_DIMENSION_BUFFER;
    }
    else
    {
        WARN("Description required for NULL UAV.\n");
        return;
    }

    if (counter_resource && (!resource || !is_buffer))
        FIXME("Ignoring counter resource %p.\n", counter_resource);

    if (is_buffer)
        vkd3d_create_buffer_uav_embedded(desc_va, device, resource, counter_resource, desc);
    else
        vkd3d_create_texture_uav_embedded(desc_va, device, resource, desc);
}

void d3d12_desc_create_uav(vkd3d_cpu_descriptor_va_t desc_va, struct d3d12_device *device,
        struct d3d12_resource *resource, struct d3d12_resource *counter_resource,
        const D3D12_UNORDERED_ACCESS_VIEW_DESC *desc)
{
    bool is_buffer;

    if (resource)
    {
        is_buffer = d3d12_resource_is_buffer(resource);
    }
    else if (desc)
    {
        is_buffer = desc->ViewDimension == D3D12_UAV_DIMENSION_BUFFER;
    }
    else
    {
        WARN("Description required for NULL UAV.\n");
        return;
    }

    if (counter_resource && (!resource || !is_buffer))
        FIXME("Ignoring counter resource %p.\n", counter_resource);

    if (is_buffer)
        vkd3d_create_buffer_uav(desc_va, device, resource, counter_resource, desc);
    else
        vkd3d_create_texture_uav(desc_va, device, resource, desc);
}

bool vkd3d_create_raw_buffer_view(struct d3d12_device *device,
        D3D12_GPU_VIRTUAL_ADDRESS gpu_address, VkBufferView *vk_buffer_view)
{
    const struct vkd3d_unique_resource *resource;
    uint64_t range;
    uint64_t offset;

    resource = vkd3d_va_map_deref(&device->memory_allocator.va_map, gpu_address);
    assert(resource && resource->va && resource->size);

    offset = gpu_address - resource->va;
    range = min(resource->size - offset, device->vk_info.device_limits.maxStorageBufferRange);

    return vkd3d_create_raw_r32ui_vk_buffer_view(device, resource->vk_buffer,
            offset, range, vk_buffer_view);
}

/* samplers */
static VkFilter vk_filter_from_d3d12(D3D12_FILTER_TYPE type)
{
    switch (type)
    {
        case D3D12_FILTER_TYPE_POINT:
            return VK_FILTER_NEAREST;
        case D3D12_FILTER_TYPE_LINEAR:
            return VK_FILTER_LINEAR;
        default:
            FIXME("Unhandled filter type %#x.\n", type);
            return VK_FILTER_NEAREST;
    }
}

static VkSamplerMipmapMode vk_mipmap_mode_from_d3d12(D3D12_FILTER_TYPE type)
{
    switch (type)
    {
        case D3D12_FILTER_TYPE_POINT:
            return VK_SAMPLER_MIPMAP_MODE_NEAREST;
        case D3D12_FILTER_TYPE_LINEAR:
            return VK_SAMPLER_MIPMAP_MODE_LINEAR;
        default:
            FIXME("Unhandled filter type %#x.\n", type);
            return VK_SAMPLER_MIPMAP_MODE_NEAREST;
    }
}

static VkSamplerAddressMode vk_address_mode_from_d3d12(D3D12_TEXTURE_ADDRESS_MODE mode)
{
    switch (mode)
    {
        case D3D12_TEXTURE_ADDRESS_MODE_WRAP:
            return VK_SAMPLER_ADDRESS_MODE_REPEAT;
        case D3D12_TEXTURE_ADDRESS_MODE_MIRROR:
            return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
        case D3D12_TEXTURE_ADDRESS_MODE_CLAMP:
            return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        case D3D12_TEXTURE_ADDRESS_MODE_BORDER:
            return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        case D3D12_TEXTURE_ADDRESS_MODE_MIRROR_ONCE:
            return VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE;
        default:
            FIXME("Unhandled address mode %#x.\n", mode);
            return VK_SAMPLER_ADDRESS_MODE_REPEAT;
    }
}

static VkSamplerReductionModeEXT vk_reduction_mode_from_d3d12(D3D12_FILTER_REDUCTION_TYPE mode)
{
    switch (mode)
    {
        case D3D12_FILTER_REDUCTION_TYPE_STANDARD:
        case D3D12_FILTER_REDUCTION_TYPE_COMPARISON:
            return VK_SAMPLER_REDUCTION_MODE_WEIGHTED_AVERAGE;
        case D3D12_FILTER_REDUCTION_TYPE_MINIMUM:
            return VK_SAMPLER_REDUCTION_MODE_MIN;
        case D3D12_FILTER_REDUCTION_TYPE_MAXIMUM:
            return VK_SAMPLER_REDUCTION_MODE_MAX;
        default:
            FIXME("Unhandled reduction mode %#x.\n", mode);
            return VK_SAMPLER_REDUCTION_MODE_WEIGHTED_AVERAGE;
    }
}

static bool d3d12_sampler_needs_border_color(D3D12_TEXTURE_ADDRESS_MODE u,
        D3D12_TEXTURE_ADDRESS_MODE v, D3D12_TEXTURE_ADDRESS_MODE w)
{
    return u == D3D12_TEXTURE_ADDRESS_MODE_BORDER ||
        v == D3D12_TEXTURE_ADDRESS_MODE_BORDER ||
        w == D3D12_TEXTURE_ADDRESS_MODE_BORDER;
}

static VkBorderColor vk_static_border_color_from_d3d12(D3D12_STATIC_BORDER_COLOR border_color)
{
    switch (border_color)
    {
        case D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK:
            return VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
        case D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK:
            return VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
        case D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE:
            return VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
        case D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK_UINT:
            return VK_BORDER_COLOR_INT_OPAQUE_BLACK;
        case D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE_UINT:
            return VK_BORDER_COLOR_INT_OPAQUE_WHITE;
        default:
            WARN("Unhandled static border color %u.\n", border_color);
            return VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
    }
}

static VkBorderColor vk_border_color_from_d3d12(struct d3d12_device *device, const uint32_t *border_color,
        D3D12_SAMPLER_FLAGS flags)
{
    bool uint_border = !!(flags & D3D12_SAMPLER_FLAG_UINT_BORDER_COLOR);
    unsigned int i;

#define ONE_FP32 0x3f800000
    static const struct
    {
        uint32_t color[4];
        bool uint_border;
        VkBorderColor vk_border_color;
    }
    border_colors[] = {
      { {0, 0, 0, 0}, false, VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK },
      { {0, 0, 0, ONE_FP32}, false, VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK },
      { {ONE_FP32, ONE_FP32, ONE_FP32, ONE_FP32}, false, VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE },
      { {0, 0, 0, 0}, true, VK_BORDER_COLOR_INT_TRANSPARENT_BLACK },
      { {0, 0, 0, 1}, true, VK_BORDER_COLOR_INT_OPAQUE_BLACK },
      { {1, 1, 1, 1}, true, VK_BORDER_COLOR_INT_OPAQUE_WHITE},
    };
#undef ONE_FP32

    for (i = 0; i < ARRAY_SIZE(border_colors); i++)
    {
        if (uint_border == border_colors[i].uint_border &&
                !memcmp(border_color, border_colors[i].color, sizeof(border_colors[i].color)))
        {
            return border_colors[i].vk_border_color;
        }
    }

    if (!device->device_info.custom_border_color_features.customBorderColorWithoutFormat)
    {
        FIXME("Unsupported border color (#%x, #%x, #%x, #%x).\n",
                border_color[0], border_color[1], border_color[2], border_color[3]);
        return uint_border ? VK_BORDER_COLOR_INT_TRANSPARENT_BLACK : VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
    }

    return uint_border ? VK_BORDER_COLOR_INT_CUSTOM_EXT : VK_BORDER_COLOR_FLOAT_CUSTOM_EXT;
}

HRESULT d3d12_create_static_sampler(struct d3d12_device *device,
        const D3D12_STATIC_SAMPLER_DESC1 *desc, VkSampler *vk_sampler)
{
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    VkSamplerReductionModeCreateInfoEXT reduction_desc;
    VkSamplerCreateInfo sampler_desc;
    VkResult vr;

    reduction_desc.sType = VK_STRUCTURE_TYPE_SAMPLER_REDUCTION_MODE_CREATE_INFO_EXT;
    reduction_desc.pNext = NULL;
    reduction_desc.reductionMode = vk_reduction_mode_from_d3d12(D3D12_DECODE_FILTER_REDUCTION(desc->Filter));

    sampler_desc.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sampler_desc.pNext = NULL;
    sampler_desc.flags = 0;
    sampler_desc.magFilter = vk_filter_from_d3d12(D3D12_DECODE_MAG_FILTER(desc->Filter));
    sampler_desc.minFilter = vk_filter_from_d3d12(D3D12_DECODE_MIN_FILTER(desc->Filter));
    sampler_desc.mipmapMode = vk_mipmap_mode_from_d3d12(D3D12_DECODE_MIP_FILTER(desc->Filter));
    sampler_desc.addressModeU = vk_address_mode_from_d3d12(desc->AddressU);
    sampler_desc.addressModeV = vk_address_mode_from_d3d12(desc->AddressV);
    sampler_desc.addressModeW = vk_address_mode_from_d3d12(desc->AddressW);
    sampler_desc.mipLodBias = desc->MipLODBias;
    sampler_desc.anisotropyEnable = D3D12_DECODE_IS_ANISOTROPIC_FILTER(desc->Filter);
    sampler_desc.maxAnisotropy = desc->MaxAnisotropy;
    sampler_desc.compareEnable = D3D12_DECODE_IS_COMPARISON_FILTER(desc->Filter);
    sampler_desc.compareOp = sampler_desc.compareEnable ? vk_compare_op_from_d3d12(desc->ComparisonFunc) : 0;
    sampler_desc.minLod = desc->MinLOD;
    sampler_desc.maxLod = desc->MaxLOD;
    sampler_desc.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
    sampler_desc.unnormalizedCoordinates = !!(desc->Flags & D3D12_SAMPLER_FLAG_NON_NORMALIZED_COORDINATES);

    if (sampler_desc.maxAnisotropy < 1.0f)
        sampler_desc.anisotropyEnable = VK_FALSE;

    if (sampler_desc.anisotropyEnable)
        sampler_desc.maxAnisotropy = min(16.0f, sampler_desc.maxAnisotropy);

    if (d3d12_sampler_needs_border_color(desc->AddressU, desc->AddressV, desc->AddressW))
        sampler_desc.borderColor = vk_static_border_color_from_d3d12(desc->BorderColor);

    if (reduction_desc.reductionMode != VK_SAMPLER_REDUCTION_MODE_WEIGHTED_AVERAGE &&
            device->device_info.vulkan_1_2_features.samplerFilterMinmax)
        vk_prepend_struct(&sampler_desc, &reduction_desc);

    if ((vr = VK_CALL(vkCreateSampler(device->vk_device, &sampler_desc, NULL, vk_sampler))) < 0)
        WARN("Failed to create Vulkan sampler, vr %d.\n", vr);

    return hresult_from_vk_result(vr);
}

static HRESULT d3d12_create_sampler(struct d3d12_device *device,
        const D3D12_SAMPLER_DESC2 *desc, VkSampler *vk_sampler)
{
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    VkSamplerCustomBorderColorCreateInfoEXT border_color_info;
    VkSamplerReductionModeCreateInfoEXT reduction_desc;
    VkSamplerCreateInfo sampler_desc;
    VkResult vr;

    border_color_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CUSTOM_BORDER_COLOR_CREATE_INFO_EXT;
    border_color_info.pNext = NULL;
    memcpy(border_color_info.customBorderColor.uint32, desc->UintBorderColor,
            sizeof(border_color_info.customBorderColor.uint32));
    border_color_info.format = VK_FORMAT_UNDEFINED;

    reduction_desc.sType = VK_STRUCTURE_TYPE_SAMPLER_REDUCTION_MODE_CREATE_INFO_EXT;
    reduction_desc.pNext = NULL;
    reduction_desc.reductionMode = vk_reduction_mode_from_d3d12(D3D12_DECODE_FILTER_REDUCTION(desc->Filter));

    sampler_desc.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sampler_desc.pNext = NULL;
    sampler_desc.flags = 0;
    sampler_desc.magFilter = vk_filter_from_d3d12(D3D12_DECODE_MAG_FILTER(desc->Filter));
    sampler_desc.minFilter = vk_filter_from_d3d12(D3D12_DECODE_MIN_FILTER(desc->Filter));
    sampler_desc.mipmapMode = vk_mipmap_mode_from_d3d12(D3D12_DECODE_MIP_FILTER(desc->Filter));
    sampler_desc.addressModeU = vk_address_mode_from_d3d12(desc->AddressU);
    sampler_desc.addressModeV = vk_address_mode_from_d3d12(desc->AddressV);
    sampler_desc.addressModeW = vk_address_mode_from_d3d12(desc->AddressW);
    sampler_desc.mipLodBias = desc->MipLODBias;
    sampler_desc.anisotropyEnable = D3D12_DECODE_IS_ANISOTROPIC_FILTER(desc->Filter);
    sampler_desc.maxAnisotropy = desc->MaxAnisotropy;
    sampler_desc.compareEnable = D3D12_DECODE_IS_COMPARISON_FILTER(desc->Filter);
    sampler_desc.compareOp = sampler_desc.compareEnable ? vk_compare_op_from_d3d12(desc->ComparisonFunc) : 0;
    sampler_desc.minLod = desc->MinLOD;
    sampler_desc.maxLod = desc->MaxLOD;
    sampler_desc.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
    sampler_desc.unnormalizedCoordinates = !!(desc->Flags & D3D12_SAMPLER_FLAG_NON_NORMALIZED_COORDINATES);

    if (sampler_desc.maxAnisotropy < 1.0f)
        sampler_desc.anisotropyEnable = VK_FALSE;

    if (sampler_desc.anisotropyEnable)
        sampler_desc.maxAnisotropy = min(16.0f, sampler_desc.maxAnisotropy);

    if (d3d12_sampler_needs_border_color(desc->AddressU, desc->AddressV, desc->AddressW))
        sampler_desc.borderColor = vk_border_color_from_d3d12(device, desc->UintBorderColor, desc->Flags);

    if (sampler_desc.borderColor == VK_BORDER_COLOR_FLOAT_CUSTOM_EXT ||
            sampler_desc.borderColor == VK_BORDER_COLOR_INT_CUSTOM_EXT)
    {
        vk_prepend_struct(&sampler_desc, &border_color_info);
    }

    if (reduction_desc.reductionMode != VK_SAMPLER_REDUCTION_MODE_WEIGHTED_AVERAGE &&
            device->device_info.vulkan_1_2_features.samplerFilterMinmax)
        vk_prepend_struct(&sampler_desc, &reduction_desc);

    if ((vr = VK_CALL(vkCreateSampler(device->vk_device, &sampler_desc, NULL, vk_sampler))) < 0)
        WARN("Failed to create Vulkan sampler, vr %d.\n", vr);

    return hresult_from_vk_result(vr);
}

void d3d12_desc_create_sampler_embedded(vkd3d_cpu_descriptor_va_t desc_va,
        struct d3d12_device *device, const D3D12_SAMPLER_DESC2 *desc)
{
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    VkDescriptorGetInfoEXT get_info;
    struct vkd3d_view_key key;
    struct vkd3d_view *view;

    if (!desc)
    {
        WARN("NULL sampler desc.\n");
        return;
    }

    key.view_type = VKD3D_VIEW_TYPE_SAMPLER;
    key.u.sampler = *desc;

    if (!(view = vkd3d_view_map_create_view(&device->sampler_map, device, &key)))
        return;

    get_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_GET_INFO_EXT;
    get_info.pNext = NULL;
    get_info.type = VK_DESCRIPTOR_TYPE_SAMPLER;
    get_info.data.pSampler = &view->vk_sampler;
    VK_CALL(vkGetDescriptorEXT(device->vk_device, &get_info,
            device->device_info.descriptor_buffer_properties.samplerDescriptorSize,
            (void *)desc_va));
}

void d3d12_desc_create_sampler(vkd3d_cpu_descriptor_va_t desc_va,
        struct d3d12_device *device, const D3D12_SAMPLER_DESC2 *desc)
{
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    union vkd3d_descriptor_info descriptor_info;
    struct vkd3d_descriptor_binding binding;
    VkDescriptorGetInfoEXT get_info;
    VkWriteDescriptorSet vk_write;
    struct d3d12_desc_split d;
    struct vkd3d_view_key key;
    struct vkd3d_view *view;
    uint32_t info_index;
    void *payload;

    if (!desc)
    {
        WARN("NULL sampler desc.\n");
        return;
    }

    d = d3d12_desc_decode_va(desc_va);

    key.view_type = VKD3D_VIEW_TYPE_SAMPLER;
    key.u.sampler = *desc;

    if (!(view = vkd3d_view_map_create_view(&device->sampler_map, device, &key)))
        return;

    vkd3d_descriptor_debug_register_view_cookie(device->descriptor_qa_global_info, view->cookie, 0);

    info_index = VKD3D_BINDLESS_STATE_INFO_INDEX_SAMPLER;
    binding = vkd3d_bindless_state_binding_from_info_index(&device->bindless_state, info_index);

    d.view->info.image.view = view;
    d.view->info.image.flags = VKD3D_DESCRIPTOR_FLAG_IMAGE_VIEW |
            VKD3D_DESCRIPTOR_FLAG_NON_NULL |
            VKD3D_DESCRIPTOR_FLAG_SINGLE_DESCRIPTOR;
    d.types->set_info_mask = 1u << info_index;
    d.types->single_binding = binding;

    if (d3d12_device_uses_descriptor_buffers(device))
    {
        get_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_GET_INFO_EXT;
        get_info.pNext = NULL;
        get_info.type = VK_DESCRIPTOR_TYPE_SAMPLER;
        get_info.data.pSampler = &view->vk_sampler;
        payload = d3d12_descriptor_heap_get_mapped_payload(d.heap, binding.set, d.offset);
        VK_CALL(vkGetDescriptorEXT(device->vk_device, &get_info,
                device->device_info.descriptor_buffer_properties.samplerDescriptorSize,
                payload));
    }
    else
    {
        descriptor_info.image.sampler = view->vk_sampler;
        descriptor_info.image.imageView = VK_NULL_HANDLE;
        descriptor_info.image.imageLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        vkd3d_init_write_descriptor_set(&vk_write, &d, binding, VK_DESCRIPTOR_TYPE_SAMPLER, &descriptor_info);
        VK_CALL(vkUpdateDescriptorSets(device->vk_device, 1, &vk_write, 0, NULL));
    }

    vkd3d_descriptor_metadata_view_set_qa_cookie(d.view, view->cookie);
    vkd3d_descriptor_debug_write_descriptor(d.heap->descriptor_heap_info.host_ptr,
            d.heap->cookie, d.offset,
            VKD3D_DESCRIPTOR_QA_TYPE_SAMPLER_BIT, d.view->qa_cookie);
}

/* RTVs */
void d3d12_rtv_desc_copy(struct d3d12_rtv_desc *dst, struct d3d12_rtv_desc *src, unsigned int count)
{
    memcpy(dst, src, sizeof(*dst) * count);
}

void d3d12_rtv_desc_create_rtv(struct d3d12_rtv_desc *rtv_desc, struct d3d12_device *device,
        struct d3d12_resource *resource, const D3D12_RENDER_TARGET_VIEW_DESC *desc)
{
    struct vkd3d_view_key key;
    struct vkd3d_view *view;
    VkExtent3D view_extent;

    if (!resource)
    {
        memset(rtv_desc, 0, sizeof(*rtv_desc));
        return;
    }

    if (!(resource->desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET))
        FIXME("Resource %p does not set D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET.\n", resource);

    if (!init_default_texture_view_desc(&key.u.texture, resource, desc ? desc->Format : 0))
        return;

    if (key.u.texture.format->vk_aspect_mask != VK_IMAGE_ASPECT_COLOR_BIT)
    {
        WARN("Trying to create RTV for depth/stencil format %#x.\n", key.u.texture.format->dxgi_format);
        return;
    }

    key.view_type = VKD3D_VIEW_TYPE_IMAGE;
    key.u.texture.image_usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    if (desc)
    {
        switch (desc->ViewDimension)
        {
            case D3D12_RTV_DIMENSION_TEXTURE1D:
                key.u.texture.view_type = VK_IMAGE_VIEW_TYPE_1D;
                key.u.texture.miplevel_idx = desc->Texture1D.MipSlice;
                key.u.texture.layer_count = 1;
                break;
            case D3D12_RTV_DIMENSION_TEXTURE1DARRAY:
                key.u.texture.view_type = VK_IMAGE_VIEW_TYPE_1D_ARRAY;
                key.u.texture.miplevel_idx = desc->Texture1DArray.MipSlice;
                key.u.texture.layer_idx = desc->Texture1DArray.FirstArraySlice;
                key.u.texture.layer_count = desc->Texture1DArray.ArraySize;
                break;
            case D3D12_RTV_DIMENSION_TEXTURE2D:
                key.u.texture.view_type = VK_IMAGE_VIEW_TYPE_2D;
                key.u.texture.miplevel_idx = desc->Texture2D.MipSlice;
                key.u.texture.layer_count = 1;
                key.u.texture.aspect_mask = vk_image_aspect_flags_from_d3d12(resource->format, desc->Texture2D.PlaneSlice);
                break;
            case D3D12_RTV_DIMENSION_TEXTURE2DARRAY:
                key.u.texture.view_type = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
                key.u.texture.miplevel_idx = desc->Texture2DArray.MipSlice;
                key.u.texture.layer_idx = desc->Texture2DArray.FirstArraySlice;
                key.u.texture.layer_count = desc->Texture2DArray.ArraySize;
                key.u.texture.aspect_mask = vk_image_aspect_flags_from_d3d12(resource->format, desc->Texture2DArray.PlaneSlice);
                break;
            case D3D12_RTV_DIMENSION_TEXTURE2DMS:
                key.u.texture.view_type = VK_IMAGE_VIEW_TYPE_2D;
                key.u.texture.layer_count = 1;
                break;
            case D3D12_RTV_DIMENSION_TEXTURE2DMSARRAY:
                key.u.texture.view_type = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
                key.u.texture.layer_idx = desc->Texture2DMSArray.FirstArraySlice;
                key.u.texture.layer_count = desc->Texture2DMSArray.ArraySize;
                break;
            case D3D12_RTV_DIMENSION_TEXTURE3D:
                key.u.texture.view_type = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
                key.u.texture.miplevel_idx = desc->Texture3D.MipSlice;
                key.u.texture.layer_idx = desc->Texture3D.FirstWSlice;
                key.u.texture.layer_count = desc->Texture3D.WSize;
                break;
            default:
                FIXME("Unhandled view dimension %#x.\n", desc->ViewDimension);
        }

        /* Avoid passing down UINT32_MAX here since that makes framebuffer logic later rather awkward. */
        key.u.texture.layer_count = min(key.u.texture.layer_count, resource->desc.DepthOrArraySize - key.u.texture.layer_idx);
    }
    else if (resource->desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D)
    {
        key.u.texture.view_type = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
        key.u.texture.layer_idx = 0;
        key.u.texture.layer_count = resource->desc.DepthOrArraySize;
    }

    assert(d3d12_resource_is_texture(resource));

    if (!(view = vkd3d_view_map_create_view(&resource->view_map, device, &key)))
        return;

    vkd3d_descriptor_debug_register_view_cookie(device->descriptor_qa_global_info, view->cookie, resource->res.cookie);

    view_extent = d3d12_resource_get_view_subresource_extent(resource, view);

    rtv_desc->sample_count = vk_samples_from_dxgi_sample_desc(&resource->desc.SampleDesc);
    rtv_desc->format = key.u.texture.format;
    rtv_desc->width = view_extent.width;
    rtv_desc->height = view_extent.height;
    rtv_desc->layer_count = key.u.texture.layer_count;
    rtv_desc->view = view;
    rtv_desc->resource = resource;
    rtv_desc->plane_write_enable = 1u << 0;
}

void d3d12_rtv_desc_create_dsv(struct d3d12_rtv_desc *dsv_desc, struct d3d12_device *device,
        struct d3d12_resource *resource, const D3D12_DEPTH_STENCIL_VIEW_DESC *desc)
{
    struct vkd3d_view_key key;
    struct vkd3d_view *view;
    VkExtent3D view_extent;

    if (!resource)
    {
        memset(dsv_desc, 0, sizeof(*dsv_desc));
        return;
    }

    if (!(resource->desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL))
        FIXME("Resource %p does not set D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL.\n", resource);

    if (resource->desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D)
    {
        WARN("Cannot create DSV for 3D texture.\n");
        return;
    }

    if (!init_default_texture_view_desc(&key.u.texture, resource, desc ? desc->Format : 0))
        return;

    if (!(key.u.texture.format->vk_aspect_mask & (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT)))
    {
        WARN("Trying to create DSV for format %#x.\n", key.u.texture.format->dxgi_format);
        return;
    }

    key.view_type = VKD3D_VIEW_TYPE_IMAGE;
    key.u.texture.image_usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;

    if (desc)
    {
        switch (desc->ViewDimension)
        {
            case D3D12_DSV_DIMENSION_TEXTURE1D:
                key.u.texture.miplevel_idx = desc->Texture1D.MipSlice;
                key.u.texture.layer_count = 1;
                break;
            case D3D12_DSV_DIMENSION_TEXTURE1DARRAY:
                key.u.texture.view_type = VK_IMAGE_VIEW_TYPE_1D_ARRAY;
                key.u.texture.miplevel_idx = desc->Texture1DArray.MipSlice;
                key.u.texture.layer_idx = desc->Texture1DArray.FirstArraySlice;
                key.u.texture.layer_count = desc->Texture1DArray.ArraySize;
                break;
            case D3D12_DSV_DIMENSION_TEXTURE2D:
                key.u.texture.miplevel_idx = desc->Texture2D.MipSlice;
                key.u.texture.layer_count = 1;
                break;
            case D3D12_DSV_DIMENSION_TEXTURE2DARRAY:
                key.u.texture.view_type = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
                key.u.texture.miplevel_idx = desc->Texture2DArray.MipSlice;
                key.u.texture.layer_idx = desc->Texture2DArray.FirstArraySlice;
                key.u.texture.layer_count = desc->Texture2DArray.ArraySize;
                break;
            case D3D12_DSV_DIMENSION_TEXTURE2DMS:
                key.u.texture.view_type = VK_IMAGE_VIEW_TYPE_2D;
                key.u.texture.layer_count = 1;
                break;
            case D3D12_DSV_DIMENSION_TEXTURE2DMSARRAY:
                key.u.texture.view_type = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
                key.u.texture.layer_idx = desc->Texture2DMSArray.FirstArraySlice;
                key.u.texture.layer_count = desc->Texture2DMSArray.ArraySize;
                break;
            default:
                FIXME("Unhandled view dimension %#x.\n", desc->ViewDimension);
        }

        /* Avoid passing down UINT32_MAX here since that makes framebuffer logic later rather awkward. */
        key.u.texture.layer_count = min(key.u.texture.layer_count, resource->desc.DepthOrArraySize - key.u.texture.layer_idx);
    }

    assert(d3d12_resource_is_texture(resource));

    if (!(view = vkd3d_view_map_create_view(&resource->view_map, device, &key)))
        return;

    vkd3d_descriptor_debug_register_view_cookie(device->descriptor_qa_global_info, view->cookie, resource->res.cookie);

    view_extent = d3d12_resource_get_view_subresource_extent(resource, view);

    dsv_desc->sample_count = vk_samples_from_dxgi_sample_desc(&resource->desc.SampleDesc);
    dsv_desc->format = key.u.texture.format;
    dsv_desc->width = view_extent.width;
    dsv_desc->height = view_extent.height;
    dsv_desc->layer_count = key.u.texture.layer_count;
    dsv_desc->view = view;
    dsv_desc->resource = resource;
    dsv_desc->plane_write_enable =
            (desc && (desc->Flags & D3D12_DSV_FLAG_READ_ONLY_DEPTH) ? 0 : (1u << 0)) |
            (desc && (desc->Flags & D3D12_DSV_FLAG_READ_ONLY_STENCIL) ? 0 : (1u << 1));
}

/* ID3D12DescriptorHeap */
static HRESULT STDMETHODCALLTYPE d3d12_descriptor_heap_QueryInterface(ID3D12DescriptorHeap *iface,
        REFIID riid, void **object)
{
    struct d3d12_descriptor_heap *descriptor_heap = impl_from_ID3D12DescriptorHeap(iface);

    TRACE("iface %p, riid %s, object %p.\n", iface, debugstr_guid(riid), object);

    if (!object)
        return E_POINTER;

    if (IsEqualGUID(riid, &IID_ID3D12DescriptorHeap)
            || IsEqualGUID(riid, &IID_ID3D12Pageable)
            || IsEqualGUID(riid, &IID_ID3D12DeviceChild)
            || IsEqualGUID(riid, &IID_ID3D12Object)
            || IsEqualGUID(riid, &IID_IUnknown))
    {
        ID3D12DescriptorHeap_AddRef(iface);
        *object = iface;
        return S_OK;
    }

    if (IsEqualGUID(riid, &IID_ID3DDestructionNotifier))
    {
        ID3DDestructionNotifier_AddRef(&descriptor_heap->destruction_notifier.ID3DDestructionNotifier_iface);
        *object = &descriptor_heap->destruction_notifier.ID3DDestructionNotifier_iface;
        return S_OK;
    }

    WARN("%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid(riid));

    *object = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE d3d12_descriptor_heap_AddRef(ID3D12DescriptorHeap *iface)
{
    struct d3d12_descriptor_heap *heap = impl_from_ID3D12DescriptorHeap(iface);
    ULONG refcount = InterlockedIncrement(&heap->refcount);

    TRACE("%p increasing refcount to %u.\n", heap, refcount);

    return refcount;
}

static ULONG STDMETHODCALLTYPE d3d12_descriptor_heap_Release(ID3D12DescriptorHeap *iface)
{
    struct d3d12_descriptor_heap *heap = impl_from_ID3D12DescriptorHeap(iface);
    ULONG refcount = InterlockedDecrement(&heap->refcount);

    TRACE("%p decreasing refcount to %u.\n", heap, refcount);

    if (!refcount)
    {
        struct d3d12_device *device = heap->device;

        d3d_destruction_notifier_free(&heap->destruction_notifier);

        d3d12_descriptor_heap_cleanup(heap);
        vkd3d_private_store_destroy(&heap->private_store);
        vkd3d_free_aligned(heap);

        d3d12_device_release(device);
    }

    return refcount;
}

static HRESULT STDMETHODCALLTYPE d3d12_descriptor_heap_GetPrivateData(ID3D12DescriptorHeap *iface,
        REFGUID guid, UINT *data_size, void *data)
{
    struct d3d12_descriptor_heap *heap = impl_from_ID3D12DescriptorHeap(iface);

    TRACE("iface %p, guid %s, data_size %p, data %p.\n", iface, debugstr_guid(guid), data_size, data);

    return vkd3d_get_private_data(&heap->private_store, guid, data_size, data);
}

static HRESULT STDMETHODCALLTYPE d3d12_descriptor_heap_SetPrivateData(ID3D12DescriptorHeap *iface,
        REFGUID guid, UINT data_size, const void *data)
{
    struct d3d12_descriptor_heap *heap = impl_from_ID3D12DescriptorHeap(iface);

    TRACE("iface %p, guid %s, data_size %u, data %p.\n", iface, debugstr_guid(guid), data_size, data);

    return vkd3d_set_private_data(&heap->private_store, guid, data_size, data,
            NULL, NULL);
}

static HRESULT STDMETHODCALLTYPE d3d12_descriptor_heap_SetPrivateDataInterface(ID3D12DescriptorHeap *iface,
        REFGUID guid, const IUnknown *data)
{
    struct d3d12_descriptor_heap *heap = impl_from_ID3D12DescriptorHeap(iface);

    TRACE("iface %p, guid %s, data %p.\n", iface, debugstr_guid(guid), data);

    return vkd3d_set_private_data_interface(&heap->private_store, guid, data,
            NULL, NULL);
}

static HRESULT STDMETHODCALLTYPE d3d12_descriptor_heap_GetDevice(ID3D12DescriptorHeap *iface, REFIID iid, void **device)
{
    struct d3d12_descriptor_heap *heap = impl_from_ID3D12DescriptorHeap(iface);

    TRACE("iface %p, iid %s, device %p.\n", iface, debugstr_guid(iid), device);

    return d3d12_device_query_interface(heap->device, iid, device);
}

static D3D12_DESCRIPTOR_HEAP_DESC * STDMETHODCALLTYPE d3d12_descriptor_heap_GetDesc(ID3D12DescriptorHeap *iface,
        D3D12_DESCRIPTOR_HEAP_DESC *desc)
{
    struct d3d12_descriptor_heap *heap = impl_from_ID3D12DescriptorHeap(iface);

    TRACE("iface %p, desc %p.\n", iface, desc);

    *desc = heap->desc;
    return desc;
}

static D3D12_CPU_DESCRIPTOR_HANDLE * STDMETHODCALLTYPE d3d12_descriptor_heap_GetCPUDescriptorHandleForHeapStart(
        ID3D12DescriptorHeap *iface, D3D12_CPU_DESCRIPTOR_HANDLE *descriptor)
{
    struct d3d12_descriptor_heap *heap = impl_from_ID3D12DescriptorHeap(iface);

    TRACE("iface %p, descriptor %p.\n", iface, descriptor);

    *descriptor = heap->cpu_va;

    return descriptor;
}

static D3D12_GPU_DESCRIPTOR_HANDLE * STDMETHODCALLTYPE d3d12_descriptor_heap_GetGPUDescriptorHandleForHeapStart(
        ID3D12DescriptorHeap *iface, D3D12_GPU_DESCRIPTOR_HANDLE *descriptor)
{
    struct d3d12_descriptor_heap *heap = impl_from_ID3D12DescriptorHeap(iface);

    TRACE("iface %p, descriptor %p.\n", iface, descriptor);

    descriptor->ptr = heap->gpu_va;

    return descriptor;
}

CONST_VTBL struct ID3D12DescriptorHeapVtbl d3d12_descriptor_heap_vtbl =
{
    /* IUnknown methods */
    d3d12_descriptor_heap_QueryInterface,
    d3d12_descriptor_heap_AddRef,
    d3d12_descriptor_heap_Release,
    /* ID3D12Object methods */
    d3d12_descriptor_heap_GetPrivateData,
    d3d12_descriptor_heap_SetPrivateData,
    d3d12_descriptor_heap_SetPrivateDataInterface,
    (void *)d3d12_object_SetName,
    /* ID3D12DeviceChild methods */
    d3d12_descriptor_heap_GetDevice,
    /* ID3D12DescriptorHeap methods */
    d3d12_descriptor_heap_GetDesc,
    d3d12_descriptor_heap_GetCPUDescriptorHandleForHeapStart,
    d3d12_descriptor_heap_GetGPUDescriptorHandleForHeapStart,
};

static HRESULT d3d12_descriptor_heap_create_descriptor_buffer(struct d3d12_descriptor_heap *descriptor_heap)
{
    const struct vkd3d_vk_device_procs *vk_procs = &descriptor_heap->device->vk_procs;
    const uint8_t *src_null_payloads[VKD3D_MAX_BINDLESS_DESCRIPTOR_SETS];
    size_t src_null_payload_offsets[VKD3D_MAX_BINDLESS_DESCRIPTOR_SETS];
    size_t src_null_payload_sizes[VKD3D_MAX_BINDLESS_DESCRIPTOR_SETS];
    struct d3d12_device *device = descriptor_heap->device;
    VkMemoryPropertyFlags property_flags;
    VkDeviceSize total_alloc_size = 0;
    VkDeviceSize descriptor_count;
    unsigned int i, j, set_count;
    VkBufferUsageFlags2KHR usage;
    VkDeviceSize alloc_size;
    VkResult vr;
    HRESULT hr;

    if (descriptor_heap->desc.Type != D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV &&
            descriptor_heap->desc.Type != D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER)
        return S_OK;

    descriptor_count = descriptor_heap->desc.NumDescriptors;
    if (descriptor_heap->desc.Type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV &&
            d3d12_descriptor_heap_require_padding_descriptors())
    {
        descriptor_count += VKD3D_DESCRIPTOR_DEBUG_NUM_PAD_DESCRIPTORS;
    }

    for (i = 0, set_count = 0; i < device->bindless_state.set_count; i++)
    {
        const struct vkd3d_bindless_set_info *set_info = &device->bindless_state.set_info[i];
        bool aliased_descriptor_set;

        if (set_info->heap_type == descriptor_heap->desc.Type)
        {
            /* This set does not allocate memory on its own, it just offsets into the first buffer. */
            aliased_descriptor_set = set_count != 0 && d3d12_device_use_embedded_mutable_descriptors(device);

            if (!aliased_descriptor_set)
            {
                /* For VARIABLE_COUNT, the required size needs to be computed based on offset of binding, desired count
                 * and number of descriptors. */
                alloc_size = set_info->host_mapping_offset;
                alloc_size += set_info->host_mapping_descriptor_size * descriptor_count;

                /* If we're using embedded descriptors, and we need CPU visibility, we will need to store metadata
                 * as well. The CPU VA encodes an offset from descriptor payload to its associated metadata. */
                if (d3d12_device_use_embedded_mutable_descriptors(device) &&
                        !(device->bindless_state.flags & VKD3D_BINDLESS_MUTABLE_EMBEDDED_PACKED_METADATA) &&
                        !(descriptor_heap->desc.Flags & D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE) &&
                        set_info->heap_type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV)
                {
                    alloc_size += set_info->host_mapping_descriptor_size * (1u << vkd3d_log2i_ceil(max(1u, descriptor_count)));
                }

                /* Align to cache lines to avoid false sharing. */
                total_alloc_size = align64(total_alloc_size,
                        max(device->device_info.descriptor_buffer_properties.descriptorBufferOffsetAlignment,
                                device->device_info.properties2.properties.limits.nonCoherentAtomSize));

                descriptor_heap->descriptor_buffer.offsets[set_count] = total_alloc_size;
            }
            else
            {
                /* Deduce offset based on the first set.
                 * set_info->host_mapping_offset *really* should be 0,
                 * but if it's not we can shuffle things around so that the offsets cancel
                 * each other out ...
                 * For this to work we need device to support low alignment,
                 * but we check this when enabling embedded mutable descriptors. */
                descriptor_heap->descriptor_buffer.offsets[set_count] =
                        src_null_payload_offsets[0] +
                        device->bindless_state.descriptor_buffer_packed_raw_buffer_offset - set_info->host_mapping_offset;
                assert(descriptor_heap->descriptor_buffer.offsets[set_count] < UINT_MAX);
                assert(!(descriptor_heap->descriptor_buffer.offsets[set_count] &
                        (device->device_info.descriptor_buffer_properties.descriptorBufferOffsetAlignment - 1)));

                alloc_size = 0;
            }

            if (aliased_descriptor_set || set_info->vk_descriptor_type == VK_DESCRIPTOR_TYPE_SAMPLER)
                src_null_payloads[set_count] = NULL;
            else
            {
                /* Similar rationale as descriptor set implementation, need to pick some descriptor type,
                 * there is no generic null descriptor. */
                src_null_payloads[set_count] = vkd3d_bindless_state_get_null_descriptor_payload(
                        &device->bindless_state,
                        set_info->vk_descriptor_type == VK_DESCRIPTOR_TYPE_MUTABLE_EXT ?
                                VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE : set_info->vk_descriptor_type);
            }

            src_null_payload_sizes[set_count] = set_info->host_mapping_descriptor_size;
            src_null_payload_offsets[set_count] = set_info->host_mapping_offset + total_alloc_size;
            total_alloc_size += alloc_size;
            set_count++;
        }
    }

    if (descriptor_heap->desc.Flags & D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE)
    {
        usage = VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
        if (descriptor_heap->desc.Type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV)
        {
            usage |= VK_BUFFER_USAGE_RESOURCE_DESCRIPTOR_BUFFER_BIT_EXT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
            if (!device->device_info.descriptor_buffer_properties.bufferlessPushDescriptors)
                usage |= VK_BUFFER_USAGE_PUSH_DESCRIPTORS_DESCRIPTOR_BUFFER_BIT_EXT;
        }
        else
            usage |= VK_BUFFER_USAGE_SAMPLER_DESCRIPTOR_BUFFER_BIT_EXT;

        if (FAILED(hr = vkd3d_create_buffer_explicit_usage(device, usage, total_alloc_size,
                "descriptor-buffer", &descriptor_heap->descriptor_buffer.vk_buffer)))
            return hr;

        property_flags = device->memory_info.descriptor_heap_memory_properties;

        if (FAILED(hr = vkd3d_allocate_internal_buffer_memory(device, descriptor_heap->descriptor_buffer.vk_buffer,
                property_flags,
                &descriptor_heap->descriptor_buffer.device_allocation)))
        {
            VK_CALL(vkDestroyBuffer(device->vk_device, descriptor_heap->descriptor_buffer.vk_buffer, NULL));
            descriptor_heap->descriptor_buffer.vk_buffer = VK_NULL_HANDLE;
            return hr;
        }

        descriptor_heap->descriptor_buffer.va =
                vkd3d_get_buffer_device_address(device, descriptor_heap->descriptor_buffer.vk_buffer);

        if ((vr = VK_CALL(vkMapMemory(device->vk_device,
                descriptor_heap->descriptor_buffer.device_allocation.vk_memory,
                0, VK_WHOLE_SIZE, 0, (void**)&descriptor_heap->descriptor_buffer.host_allocation))))
        {
            ERR("Failed to map descriptor set memory.\n");
            vkd3d_free_device_memory(device, &descriptor_heap->descriptor_buffer.device_allocation);
            VK_CALL(vkDestroyBuffer(device->vk_device, descriptor_heap->descriptor_buffer.vk_buffer, NULL));
            return hresult_from_vk_result(vr);
        }
    }
    else
    {
        descriptor_heap->descriptor_buffer.host_allocation = vkd3d_malloc_aligned(total_alloc_size,
                device->device_info.properties2.properties.limits.nonCoherentAtomSize);

        if (!descriptor_heap->descriptor_buffer.host_allocation)
        {
            ERR("Failed to allocate host descriptor buffer.\n");
            return E_OUTOFMEMORY;
        }
    }

    /* Clear all descriptors with NULL descriptors. Ideally we'd just use memset(),
     * but NULL descriptors might not be all zero in memory sadly. */
    for (i = 0; i < set_count; i++)
    {
        const uint8_t *src;
        uint8_t *dst;
        size_t size;

        src = src_null_payloads[i];
        if (!src)
            continue;

        dst = descriptor_heap->descriptor_buffer.host_allocation + src_null_payload_offsets[i];
        size = src_null_payload_sizes[i];

        for (j = 0; j < descriptor_count; j++)
        {
            memcpy(dst, src, size);
            dst += size;
        }
    }

    return S_OK;
}

static HRESULT d3d12_descriptor_heap_create_descriptor_pool(struct d3d12_descriptor_heap *descriptor_heap,
        VkDescriptorPool *vk_descriptor_pool)
{
    const struct vkd3d_vk_device_procs *vk_procs = &descriptor_heap->device->vk_procs;
    VkDescriptorPoolSize vk_pool_sizes[VKD3D_MAX_BINDLESS_DESCRIPTOR_SETS];
    const struct d3d12_device *device = descriptor_heap->device;
    unsigned int i, pool_count = 0, ssbo_count = 0;
    VkDescriptorPoolCreateInfo vk_pool_info;
    VkDescriptorPoolSize *ssbo_pool = NULL;
    VkResult vr;

    for (i = 0; i < device->bindless_state.set_count; i++)
    {
        const struct vkd3d_bindless_set_info *set_info = &device->bindless_state.set_info[i];

        if (set_info->heap_type == descriptor_heap->desc.Type)
        {
            VkDescriptorPoolSize *vk_pool_size = &vk_pool_sizes[pool_count++];
            vk_pool_size->type = set_info->vk_descriptor_type;
            vk_pool_size->descriptorCount = descriptor_heap->desc.NumDescriptors;

            if (vkd3d_descriptor_debug_active_descriptor_qa_checks() &&
                    descriptor_heap->desc.Type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV)
            {
                vk_pool_size->descriptorCount += VKD3D_DESCRIPTOR_DEBUG_NUM_PAD_DESCRIPTORS;
            }

            if (set_info->vk_descriptor_type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                ssbo_pool = vk_pool_size;
        }

        ssbo_count += vkd3d_popcount(set_info->flags & VKD3D_BINDLESS_SET_EXTRA_MASK);
    }

    if (ssbo_count && !ssbo_pool)
    {
        ssbo_pool = &vk_pool_sizes[pool_count++];
        ssbo_pool->type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        ssbo_pool->descriptorCount = 0;  /* see below */
    }

    if (ssbo_pool)
        ssbo_pool->descriptorCount += ssbo_count;

    if (!pool_count)
        return S_OK;

    /* If using mutable type, we will allocate the most conservative size.
     * This is fine since we're attempting to allocate a completely generic descriptor set. */

    vk_pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    vk_pool_info.pNext = NULL;

    vk_pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
    if (!(descriptor_heap->desc.Flags & D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE) &&
            (descriptor_heap->device->bindless_state.flags & VKD3D_BINDLESS_MUTABLE_TYPE))
        vk_pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_HOST_ONLY_BIT_EXT;

    vk_pool_info.maxSets = pool_count;
    vk_pool_info.poolSizeCount = pool_count;
    vk_pool_info.pPoolSizes = vk_pool_sizes;

    if ((vr = VK_CALL(vkCreateDescriptorPool(device->vk_device,
            &vk_pool_info, NULL, vk_descriptor_pool))) < 0)
    {
        ERR("Failed to create descriptor pool, vr %d.\n", vr);
        return hresult_from_vk_result(vr);
    }

    return S_OK;
}

static void d3d12_descriptor_heap_zero_initialize(struct d3d12_descriptor_heap *descriptor_heap,
        VkDescriptorType vk_descriptor_type, VkDescriptorSet vk_descriptor_set,
        uint32_t binding_index, uint32_t descriptor_count)
{
    const struct vkd3d_vk_device_procs *vk_procs = &descriptor_heap->device->vk_procs;
    const struct d3d12_device *device = descriptor_heap->device;
    VkDescriptorBufferInfo *buffer_infos = NULL;
    VkDescriptorImageInfo *image_infos = NULL;
    VkBufferView *buffer_view_infos = NULL;
    VkWriteDescriptorSet write;
    uint32_t i;

    /* Clear out descriptor heap with the largest possible descriptor type we know of when using mutable descriptor type.
     * Purely for defensive purposes. */
    assert(vk_descriptor_type != VK_DESCRIPTOR_TYPE_MUTABLE_EXT);

    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.pNext = NULL;
    write.descriptorType = vk_descriptor_type;
    write.dstSet = vk_descriptor_set;
    write.dstBinding = binding_index;
    write.dstArrayElement = 0;
    write.descriptorCount = descriptor_count;
    write.pTexelBufferView = NULL;
    write.pImageInfo = NULL;
    write.pBufferInfo = NULL;

    switch (vk_descriptor_type)
    {
    case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
    case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
        image_infos = vkd3d_calloc(descriptor_count, sizeof(*image_infos));
        write.pImageInfo = image_infos;
        break;

    case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
    case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
        buffer_infos = vkd3d_calloc(descriptor_count, sizeof(*buffer_infos));
        write.pBufferInfo = buffer_infos;
        for (i = 0; i < descriptor_count; i++)
            buffer_infos[i].range = VK_WHOLE_SIZE;
        break;

    case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
    case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
        buffer_view_infos = vkd3d_calloc(descriptor_count, sizeof(*buffer_view_infos));
        write.pTexelBufferView = buffer_view_infos;
        break;

    default:
        break;
    }

    VK_CALL(vkUpdateDescriptorSets(device->vk_device, 1, &write, 0, NULL));
    vkd3d_free(image_infos);
    vkd3d_free(buffer_view_infos);
    vkd3d_free(buffer_infos);
}

static void d3d12_descriptor_heap_get_host_mapping(struct d3d12_descriptor_heap *descriptor_heap,
        const struct vkd3d_bindless_set_info *binding, uint32_t set_index)
{
    const struct vkd3d_vk_device_procs *vk_procs = &descriptor_heap->device->vk_procs;
    uint8_t *mapped_set = NULL;

    descriptor_heap->sets[set_index].mapped_set = NULL;
    descriptor_heap->sets[set_index].copy_template = NULL;
    descriptor_heap->sets[set_index].copy_template_single = NULL;

    if (binding->host_copy_template && binding->host_copy_template_single)
    {
        if (d3d12_device_uses_descriptor_buffers(descriptor_heap->device))
        {
            mapped_set = descriptor_heap->descriptor_buffer.host_allocation +
                    descriptor_heap->descriptor_buffer.offsets[set_index];
        }
        else
        {
            VK_CALL(vkGetDescriptorSetHostMappingVALVE(descriptor_heap->device->vk_device,
                    descriptor_heap->sets[set_index].vk_descriptor_set, (void**)&mapped_set));
        }

        if (mapped_set)
        {
            mapped_set += binding->host_mapping_offset;
            descriptor_heap->sets[set_index].stride = binding->host_mapping_descriptor_size;
            descriptor_heap->sets[set_index].mapped_set = mapped_set;
            /* Keep a local copy close so we can fetch stuff from same cache line easily. */
            descriptor_heap->sets[set_index].copy_template = binding->host_copy_template;
            descriptor_heap->sets[set_index].copy_template_single = binding->host_copy_template_single;
        }
    }
}

static HRESULT d3d12_descriptor_heap_create_descriptor_set(struct d3d12_descriptor_heap *descriptor_heap,
        const struct vkd3d_bindless_set_info *binding, VkDescriptorSet *vk_descriptor_set)
{
    const struct vkd3d_vk_device_procs *vk_procs = &descriptor_heap->device->vk_procs;
    VkDescriptorSetVariableDescriptorCountAllocateInfoEXT vk_variable_count_info;
    uint32_t descriptor_count = descriptor_heap->desc.NumDescriptors;
    const struct d3d12_device *device = descriptor_heap->device;
    VkDescriptorSetAllocateInfo vk_set_info;
    VkResult vr;

    if (vkd3d_descriptor_debug_active_descriptor_qa_checks() &&
            descriptor_heap->desc.Type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV)
        descriptor_count += VKD3D_DESCRIPTOR_DEBUG_NUM_PAD_DESCRIPTORS;

    vk_variable_count_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO_EXT;
    vk_variable_count_info.pNext = NULL;
    vk_variable_count_info.descriptorSetCount = 1;
    vk_variable_count_info.pDescriptorCounts = &descriptor_count;

    vk_set_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    vk_set_info.pNext = &vk_variable_count_info;
    vk_set_info.descriptorPool = descriptor_heap->vk_descriptor_pool;
    vk_set_info.descriptorSetCount = 1;
    vk_set_info.pSetLayouts = &binding->vk_host_set_layout;

    if (descriptor_heap->desc.Flags & D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE)
        vk_set_info.pSetLayouts = &binding->vk_set_layout;

    if ((vr = VK_CALL(vkAllocateDescriptorSets(device->vk_device, &vk_set_info, vk_descriptor_set))) < 0)
    {
        ERR("Failed to allocate descriptor set, vr %d.\n", vr);
        return hresult_from_vk_result(vr);
    }

    if (binding->vk_init_null_descriptor_type != VK_DESCRIPTOR_TYPE_SAMPLER)
    {
        d3d12_descriptor_heap_zero_initialize(descriptor_heap,
                binding->vk_init_null_descriptor_type, *vk_descriptor_set,
                binding->binding_index, descriptor_count);
    }

    return S_OK;
}

static void d3d12_descriptor_heap_get_buffer_range(struct d3d12_descriptor_heap *descriptor_heap,
        VkDeviceSize *offset, VkDeviceSize size, struct vkd3d_host_visible_buffer_range *range)
{
    if (size)
    {
        range->descriptor.buffer = descriptor_heap->vk_buffer;
        range->descriptor.offset = *offset;
        range->descriptor.range = size;
        range->host_ptr = void_ptr_offset(descriptor_heap->host_memory, *offset);

        *offset += size;
    }
    else
    {
        range->descriptor.buffer = VK_NULL_HANDLE;
        range->descriptor.offset = 0;
        range->descriptor.range = VK_WHOLE_SIZE;
        range->host_ptr = NULL;
    }
}

static HRESULT d3d12_descriptor_heap_init_data_buffer(struct d3d12_descriptor_heap *descriptor_heap,
        struct d3d12_device *device, const D3D12_DESCRIPTOR_HEAP_DESC *desc)
{
    const struct vkd3d_vk_device_procs *vk_procs = &descriptor_heap->device->vk_procs;
    VkDeviceSize alignment = max(device->device_info.properties2.properties.limits.minStorageBufferOffsetAlignment,
            device->device_info.properties2.properties.limits.nonCoherentAtomSize);
    VkDeviceSize raw_va_buffer_size = 0, offset_buffer_size = 0;
    VKD3D_UNUSED VkDeviceSize descriptor_heap_info_size = 0;
    VkMemoryPropertyFlags property_flags;
    D3D12_RESOURCE_DESC1 buffer_desc;
    VkDeviceSize buffer_size, offset;
    D3D12_HEAP_PROPERTIES heap_info;
    D3D12_HEAP_FLAGS heap_flags;
    VkResult vr;
    HRESULT hr;

    /* Embedded mutable descriptors alias the raw va buffer on top of the regular descriptor buffer. */
    if (desc->Type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV && !d3d12_device_use_embedded_mutable_descriptors(device))
    {
        raw_va_buffer_size = align(desc->NumDescriptors * sizeof(VkDeviceAddress), alignment);
        if (vkd3d_descriptor_debug_active_descriptor_qa_checks())
            raw_va_buffer_size += align(VKD3D_DESCRIPTOR_DEBUG_NUM_PAD_DESCRIPTORS * sizeof(VkDeviceAddress), alignment);

        if (device->bindless_state.flags & (VKD3D_SSBO_OFFSET_BUFFER | VKD3D_TYPED_OFFSET_BUFFER))
            offset_buffer_size = align(desc->NumDescriptors * sizeof(struct vkd3d_bound_buffer_range), alignment);

        if (vkd3d_descriptor_debug_active_descriptor_qa_checks())
            descriptor_heap_info_size = align(vkd3d_descriptor_debug_heap_info_size(desc->NumDescriptors), alignment);
    }

    buffer_size = raw_va_buffer_size + offset_buffer_size;
    buffer_size += descriptor_heap_info_size;

    if (!buffer_size)
        return S_OK;

    if (desc->Flags & D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE)
    {
        memset(&buffer_desc, 0, sizeof(buffer_desc));
        buffer_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        buffer_desc.Width = buffer_size;
        buffer_desc.Height = 1;
        buffer_desc.DepthOrArraySize = 1;
        buffer_desc.MipLevels = 1;
        buffer_desc.SampleDesc.Count = 1;
        buffer_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        buffer_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

        /* host-visible device memory */
        memset(&heap_info, 0, sizeof(heap_info));
        heap_info.Type = D3D12_HEAP_TYPE_UPLOAD;

        heap_flags = D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS;

        if (FAILED(hr = vkd3d_create_buffer(device, &heap_info, heap_flags, &buffer_desc,
                "descriptor-buffer", &descriptor_heap->vk_buffer)))
            return hr;

        property_flags = device->memory_info.descriptor_heap_memory_properties;

        if (FAILED(hr = vkd3d_allocate_internal_buffer_memory(device, descriptor_heap->vk_buffer,
                property_flags, &descriptor_heap->device_allocation)))
            return hr;

        if ((vr = VK_CALL(vkMapMemory(device->vk_device, descriptor_heap->device_allocation.vk_memory,
                0, VK_WHOLE_SIZE, 0, &descriptor_heap->host_memory))))
        {
            ERR("Failed to map buffer, vr %d.\n", vr);
            return hresult_from_vk_result(vr);
        }
    }
    else
    {
        memset(&descriptor_heap->device_allocation, 0, sizeof(descriptor_heap->device_allocation));
        descriptor_heap->vk_buffer = VK_NULL_HANDLE;
        descriptor_heap->host_memory = vkd3d_calloc(1, buffer_size);
    }

    offset = 0;

    d3d12_descriptor_heap_get_buffer_range(descriptor_heap, &offset, raw_va_buffer_size, &descriptor_heap->raw_va_aux_buffer);
    d3d12_descriptor_heap_get_buffer_range(descriptor_heap, &offset, offset_buffer_size, &descriptor_heap->buffer_ranges);
#ifdef VKD3D_ENABLE_DESCRIPTOR_QA
    d3d12_descriptor_heap_get_buffer_range(descriptor_heap, &offset,
            descriptor_heap_info_size,
            &descriptor_heap->descriptor_heap_info);
#endif
    return S_OK;
}

static void d3d12_descriptor_heap_update_extra_bindings(struct d3d12_descriptor_heap *descriptor_heap,
        struct d3d12_device *device)
{
    VkDescriptorBufferInfo vk_buffer_info[VKD3D_BINDLESS_SET_MAX_EXTRA_BINDINGS];
    VkWriteDescriptorSet vk_writes[VKD3D_BINDLESS_SET_MAX_EXTRA_BINDINGS];
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    unsigned int i, binding_index, set_index = 0, write_count = 0;
    VkDescriptorAddressInfoEXT desc_addr_info;
    VkDescriptorGetInfoEXT get_info;
    VkDeviceSize binding_offset;
    uint32_t flags;

    get_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_GET_INFO_EXT;
    get_info.pNext = NULL;
    get_info.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    get_info.data.pStorageBuffer = &desc_addr_info;

    desc_addr_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_ADDRESS_INFO_EXT;
    desc_addr_info.pNext = NULL;
    desc_addr_info.format = VK_FORMAT_UNDEFINED;

    for (i = 0; i < device->bindless_state.set_count; i++)
    {
        const struct vkd3d_bindless_set_info *set_info = &device->bindless_state.set_info[i];

        if (set_info->heap_type != descriptor_heap->desc.Type)
            continue;

        flags = set_info->flags & VKD3D_BINDLESS_SET_EXTRA_MASK;
        binding_index = 0;

        while (flags)
        {
            enum vkd3d_bindless_set_flag flag = (enum vkd3d_bindless_set_flag)(flags & -flags);
            VkDescriptorBufferInfo *vk_buffer = &vk_buffer_info[write_count];
            VkWriteDescriptorSet *vk_write = &vk_writes[write_count];
            uint8_t *host_ptr;

            if (descriptor_heap->descriptor_buffer.host_allocation)
            {
                host_ptr = descriptor_heap->descriptor_buffer.host_allocation +
                        descriptor_heap->descriptor_buffer.offsets[set_index];
                VK_CALL(vkGetDescriptorSetLayoutBindingOffsetEXT(device->vk_device,
                        set_info->vk_set_layout, binding_index, &binding_offset));
                host_ptr += binding_offset;
            }
            else
                host_ptr = NULL;

            vk_write->sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            vk_write->pNext = NULL;
            vk_write->dstSet = descriptor_heap->sets[set_index].vk_descriptor_set;
            vk_write->dstBinding = binding_index++;
            vk_write->dstArrayElement = 0;
            vk_write->descriptorCount = 1;
            vk_write->descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            vk_write->pImageInfo = NULL;
            vk_write->pBufferInfo = vk_buffer;
            vk_write->pTexelBufferView = NULL;

            switch (flag)
            {
                case VKD3D_BINDLESS_SET_EXTRA_RAW_VA_AUX_BUFFER:
                    if (d3d12_device_use_embedded_mutable_descriptors(device))
                    {
                        /* Bind the descriptor buffer itself as an SSBO. */
                        vk_buffer->buffer = descriptor_heap->descriptor_buffer.vk_buffer;
                        vk_buffer->offset = (uintptr_t)descriptor_heap->sets[0].mapped_set -
                                (uintptr_t)descriptor_heap->descriptor_buffer.host_allocation;
                        vk_buffer->range = descriptor_heap->desc.NumDescriptors *
                                d3d12_device_get_descriptor_handle_increment_size(device, descriptor_heap->desc.Type);
                    }
                    else
                    {
                        *vk_buffer = descriptor_heap->raw_va_aux_buffer.descriptor;
                    }
                    break;

                case VKD3D_BINDLESS_SET_EXTRA_OFFSET_BUFFER:
                    *vk_buffer = descriptor_heap->buffer_ranges.descriptor;
                    assert(!d3d12_device_use_embedded_mutable_descriptors(device));
                    break;

#ifdef VKD3D_ENABLE_DESCRIPTOR_QA
                case VKD3D_BINDLESS_SET_EXTRA_FEEDBACK_PAYLOAD_INFO_BUFFER:
                    *vk_buffer = *vkd3d_descriptor_debug_get_payload_info_descriptor(device->descriptor_qa_global_info);
                    assert(!d3d12_device_use_embedded_mutable_descriptors(device));
                    break;

                case VKD3D_BINDLESS_SET_EXTRA_FEEDBACK_CONTROL_INFO_BUFFER:
                    *vk_buffer = descriptor_heap->descriptor_heap_info.descriptor;
                    assert(!d3d12_device_use_embedded_mutable_descriptors(device));
                    break;
#endif

                default:
                    ERR("Unsupported extra flags %#x.\n", flag);
                    continue;
            }

            if (host_ptr)
            {
                desc_addr_info.address =
                        vkd3d_get_buffer_device_address(device, vk_buffer->buffer) + vk_buffer->offset;
                desc_addr_info.range = vk_buffer->range;
                assert(vk_buffer->range != VK_WHOLE_SIZE);
                VK_CALL(vkGetDescriptorEXT(device->vk_device, &get_info,
                        device->device_info.descriptor_buffer_properties.robustStorageBufferDescriptorSize,
                        host_ptr));
            }
            else
                write_count += 1;

            flags -= flag;
        }

        set_index += 1;
    }

    if (write_count)
        VK_CALL(vkUpdateDescriptorSets(device->vk_device, write_count, vk_writes, 0, NULL));
}

static void d3d12_descriptor_heap_add_null_descriptor_template_buffers(
        struct d3d12_descriptor_heap *descriptor_heap,
        const struct vkd3d_bindless_set_info *set_info,
        unsigned int set_info_index)
{
    struct d3d12_null_descriptor_template *null_descriptor_template;
    unsigned int index;

    null_descriptor_template = &descriptor_heap->null_descriptor_template;
    index = descriptor_heap->null_descriptor_template.num_writes;

    if (set_info->vk_descriptor_type == VK_DESCRIPTOR_TYPE_MUTABLE_EXT)
        null_descriptor_template->writes.payloads[index].src_payload = NULL;
    else
    {
        null_descriptor_template->writes.payloads[index].src_payload =
                vkd3d_bindless_state_get_null_descriptor_payload(
                        &descriptor_heap->device->bindless_state,
                        set_info->vk_descriptor_type);
    }

    null_descriptor_template->writes.payloads[index].dst_base =
            descriptor_heap->sets[set_info->set_index].mapped_set;
    null_descriptor_template->writes.payloads[index].desc_size =
            descriptor_heap->device->bindless_state.set_info[set_info_index].host_mapping_descriptor_size;

    if (index == 0)
    {
        null_descriptor_template->has_mutable_descriptors =
                descriptor_heap->device->device_info.mutable_descriptor_features.mutableDescriptorType;
        null_descriptor_template->has_descriptor_buffer = true;
    }

    descriptor_heap->null_descriptor_template.num_writes++;
    descriptor_heap->null_descriptor_template.set_info_mask |= 1u << set_info_index;
}

static void d3d12_descriptor_heap_add_null_descriptor_template_descriptors(
        struct d3d12_descriptor_heap *descriptor_heap,
        const struct vkd3d_bindless_set_info *set_info,
        unsigned int set_info_index)
{
    struct VkWriteDescriptorSet *write;
    unsigned int index;

    index = descriptor_heap->null_descriptor_template.num_writes;

    write = &descriptor_heap->null_descriptor_template.writes.descriptors.writes[index];
    write->sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write->pNext = NULL;
    write->descriptorCount = 1;
    write->dstSet = descriptor_heap->sets[set_info->set_index].vk_descriptor_set;
    write->dstBinding = set_info->binding_index;

    /* Replaced when instantiating template. */
    write->dstArrayElement = 0;

    /* For mutable, will be replaced when instantiating template. */
    write->descriptorType = set_info->vk_descriptor_type;

    write->pBufferInfo = &descriptor_heap->null_descriptor_template.writes.descriptors.buffer;
    write->pImageInfo = &descriptor_heap->null_descriptor_template.writes.descriptors.image;
    write->pTexelBufferView = &descriptor_heap->null_descriptor_template.writes.descriptors.buffer_view;

    if (index == 0)
    {
        descriptor_heap->null_descriptor_template.writes.descriptors.buffer.offset = 0;
        descriptor_heap->null_descriptor_template.writes.descriptors.buffer.range = VK_WHOLE_SIZE;
        descriptor_heap->null_descriptor_template.writes.descriptors.buffer.buffer = VK_NULL_HANDLE;
        descriptor_heap->null_descriptor_template.writes.descriptors.image.sampler = VK_NULL_HANDLE;
        descriptor_heap->null_descriptor_template.writes.descriptors.image.imageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        descriptor_heap->null_descriptor_template.writes.descriptors.image.imageView = VK_NULL_HANDLE;
        descriptor_heap->null_descriptor_template.writes.descriptors.buffer_view = VK_NULL_HANDLE;
        descriptor_heap->null_descriptor_template.has_mutable_descriptors =
                descriptor_heap->device->device_info.mutable_descriptor_features.mutableDescriptorType;
        descriptor_heap->null_descriptor_template.has_descriptor_buffer = false;
    }

    descriptor_heap->null_descriptor_template.num_writes++;
    descriptor_heap->null_descriptor_template.set_info_mask |= 1u << set_info_index;
}

static HRESULT d3d12_descriptor_heap_init(struct d3d12_descriptor_heap *descriptor_heap,
        struct d3d12_device *device, const D3D12_DESCRIPTOR_HEAP_DESC *desc)
{
    uint32_t fast_bank_pointer_index = 0;
    unsigned int i;
    HRESULT hr;

    memset(descriptor_heap, 0, sizeof(*descriptor_heap));
    descriptor_heap->ID3D12DescriptorHeap_iface.lpVtbl = &d3d12_descriptor_heap_vtbl;
    descriptor_heap->refcount = 1;
    descriptor_heap->device = device;
    descriptor_heap->desc = *desc;

    if (desc->Flags & D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE)
        descriptor_heap->gpu_va = d3d12_device_get_descriptor_heap_gpu_va(device, desc->Type);

    if (d3d12_device_uses_descriptor_buffers(device))
    {
        if (FAILED(hr = d3d12_descriptor_heap_create_descriptor_buffer(descriptor_heap)))
            goto fail;
    }
    else
    {
        if (FAILED(hr = d3d12_descriptor_heap_create_descriptor_pool(descriptor_heap,
                &descriptor_heap->vk_descriptor_pool)))
            goto fail;
    }

    if (desc->Type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV ||
            desc->Type == D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER)
    {
        for (i = 0; i < device->bindless_state.set_count; i++)
        {
            const struct vkd3d_bindless_set_info *set_info = &device->bindless_state.set_info[i];

            if (set_info->heap_type == desc->Type)
            {
                if (!d3d12_device_uses_descriptor_buffers(device))
                {
                    if (FAILED(hr = d3d12_descriptor_heap_create_descriptor_set(descriptor_heap,
                            set_info, &descriptor_heap->sets[set_info->set_index].vk_descriptor_set)))
                        goto fail;
                }

                d3d12_descriptor_heap_get_host_mapping(descriptor_heap, set_info, set_info->set_index);

                /* For special fast paths of descriptor copies
                 * (e.g. d3d12_device_CopyDescriptorsSimple_descriptor_buffer_16_16_4),
                 * we can store the mapped pointers in a convenient location. */
                if (fast_bank_pointer_index < ARRAY_SIZE(descriptor_heap->fast_pointer_bank) - 1)
                {
                    descriptor_heap->fast_pointer_bank[fast_bank_pointer_index++] =
                            descriptor_heap->sets[set_info->set_index].mapped_set;
                }

                if (descriptor_heap->desc.Type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV &&
                        !d3d12_device_use_embedded_mutable_descriptors(device))
                {
                    if (d3d12_device_uses_descriptor_buffers(device))
                        d3d12_descriptor_heap_add_null_descriptor_template_buffers(descriptor_heap, set_info, i);
                    else
                        d3d12_descriptor_heap_add_null_descriptor_template_descriptors(descriptor_heap, set_info, i);
                }
            }
        }
    }

    if (FAILED(hr = d3d12_descriptor_heap_init_data_buffer(descriptor_heap, device, desc)))
        goto fail;

    if (desc->Type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV)
        descriptor_heap->fast_pointer_bank[fast_bank_pointer_index++] = descriptor_heap->raw_va_aux_buffer.host_ptr;

    if (desc->Flags & D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE)
        d3d12_descriptor_heap_update_extra_bindings(descriptor_heap, device);

    if (FAILED(hr = vkd3d_private_store_init(&descriptor_heap->private_store)))
        goto fail;

    d3d_destruction_notifier_init(&descriptor_heap->destruction_notifier,
            (IUnknown*)&descriptor_heap->ID3D12DescriptorHeap_iface);
    d3d12_device_add_ref(descriptor_heap->device);
    return S_OK;

fail:
    d3d12_descriptor_heap_cleanup(descriptor_heap);
    return hr;
}

static void d3d12_descriptor_heap_init_descriptors(struct d3d12_descriptor_heap *descriptor_heap)
{
    struct vkd3d_descriptor_metadata_types *meta;
    unsigned int i;

    switch (descriptor_heap->desc.Type)
    {
        case D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV:
        case D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER:
            if (!d3d12_device_use_embedded_mutable_descriptors(descriptor_heap->device))
            {
                meta = (struct vkd3d_descriptor_metadata_types *) descriptor_heap->descriptors;
                for (i = 0; i < descriptor_heap->desc.NumDescriptors; i++)
                    meta[i].set_info_mask = descriptor_heap->null_descriptor_template.set_info_mask;
            }
            break;

        case D3D12_DESCRIPTOR_HEAP_TYPE_RTV:
        case D3D12_DESCRIPTOR_HEAP_TYPE_DSV:
            break;

        default:
            WARN("Unhandled descriptor heap type: %d.\n", descriptor_heap->desc.Type);
    }
}

#ifndef VKD3D_NO_TRACE_MESSAGES
static void d3d12_descriptor_heap_report_allocation(const D3D12_DESCRIPTOR_HEAP_DESC *desc)
{
    if (desc->Type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV)
    {
        if (desc->Flags & D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE)
            TRACE("  %u GPU visible CBV_SRV_UAV descriptors.\n", desc->NumDescriptors);
        else
            TRACE("  %u host visible CBV_SRV_UAV descriptors.\n", desc->NumDescriptors);
    }
    else if (desc->Type == D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER)
    {
        if (desc->Flags & D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE)
            TRACE("  %u GPU visible sampler descriptors.\n", desc->NumDescriptors);
        else
            TRACE("  %u host visible sampler descriptors.\n", desc->NumDescriptors);
    }
}
#endif

HRESULT d3d12_descriptor_heap_create(struct d3d12_device *device,
        const D3D12_DESCRIPTOR_HEAP_DESC *desc, struct d3d12_descriptor_heap **descriptor_heap)
{
    size_t max_descriptor_count, descriptor_size;
    struct d3d12_descriptor_heap *object;
    unsigned int num_descriptor_bits = 0;
    unsigned int num_descriptors_pot = 0;
    size_t required_size;
    size_t alignment;
    HRESULT hr;

#ifndef VKD3D_NO_TRACE_MESSAGES
    if (desc->Type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV ||
            desc->Type == D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER)
    {
        TRACE("Allocating descriptor heap:\n");
        d3d12_descriptor_heap_report_allocation(desc);
    }
#endif

    if (!(descriptor_size = d3d12_device_get_descriptor_handle_increment_size(device, desc->Type)))
    {
        WARN("No descriptor size for descriptor type %#x.\n", desc->Type);
        return E_INVALIDARG;
    }

    if (desc->Flags & D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE)
    {
        if (desc->Type == D3D12_DESCRIPTOR_HEAP_TYPE_RTV || desc->Type == D3D12_DESCRIPTOR_HEAP_TYPE_DSV)
        {
            WARN("RTV/DSV descriptor heaps cannot be shader visible.\n");
            return E_INVALIDARG;
        }

        /* Match current agility SDK behaviour, older D3D12 runtimes would
         * either pass here, remove the device or return E_OUTOFMEMORY. */
        max_descriptor_count = d3d12_device_get_max_descriptor_heap_size(device, desc->Type);

        if (desc->NumDescriptors > max_descriptor_count)
        {
            WARN("Unsupported descriptor count %u for heap type %u (max %zu).\n", desc->NumDescriptors, desc->Type, max_descriptor_count);
            return E_INVALIDARG;
        }
    }
    else
    {
        /* CPU descriptor heaps are supposed to support any size, but our
         * implementation has a hard limit of ~4 GiB. */
        max_descriptor_count = (UINT32_MAX - sizeof(*object)) / descriptor_size;

        if (desc->NumDescriptors > max_descriptor_count)
        {
            WARN("Unsupported descriptor count %u for heap type %u (max %zu).\n", desc->NumDescriptors, desc->Type, max_descriptor_count);
            return E_OUTOFMEMORY;
        }
    }

    if (desc->Type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV || desc->Type == D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER)
    {
        if (d3d12_device_use_embedded_mutable_descriptors(device))
        {
            /* Ultra-fast path. Here, all required metadata is embedded inside the descriptor buffer itself,
             * so the heap is never accessed when writing descriptors and copying them. */
            required_size = sizeof(struct d3d12_descriptor_heap);
            alignment = D3D12_DESC_ALIGNMENT;
        }
        else
        {
            /* Always allocate a POT number of descriptors. We want the CPU VA layout to be very specific.
             * This allows us to synthesize pointers to the heap and metadata without
             * performing any de-references, which is great, because it avoids cache misses,
             * and reduces our dependency chain of memory accesses required to perform a descriptor copy.
             * Missing caches in descriptor copies is a large chunk of our CPU overhead, so we should make sure to avoid it
             * when we can.
             * We encode descriptors as:
             * <--- MSBs ------------------------------------------------------- LSBs --->
             * | descriptor_heap VA | heap_offset | log2i_ceil(num_descriptors) (5 bits) |
             * The base VA of the heap therefore becomes descriptor_heap CPU VA + log2i_ceil(num_descriptors).
             * The increment is set to 1 << 5. */

            num_descriptor_bits = vkd3d_log2i_ceil(max(1, desc->NumDescriptors));
            num_descriptors_pot = 1u << num_descriptor_bits;

            required_size = sizeof(struct d3d12_descriptor_heap);
            /* From base descriptor heap, we can offset directly to get metadata_bindings.
             * Metadata view data is placed at an offset we can deduce from num descriptors. */
            required_size += num_descriptors_pot * sizeof(struct vkd3d_descriptor_metadata_types);
            required_size += desc->NumDescriptors * sizeof(struct vkd3d_descriptor_metadata_view);

            /* The alignment should scale roughly with size of the heap,
             * so any wasted space shouldn't really be that bad. */
            alignment = max(D3D12_DESC_ALIGNMENT, num_descriptors_pot * VKD3D_RESOURCE_DESC_INCREMENT);
        }
    }
    else
    {
        /* For RTV/DSV just store the descriptors inline after the data structure.
         * Performance isn't that critical. */
        required_size = sizeof(struct d3d12_descriptor_heap);
        required_size += desc->NumDescriptors * sizeof(struct d3d12_rtv_desc);
        alignment = D3D12_DESC_ALIGNMENT;
    }

    if (!(object = vkd3d_malloc_aligned(required_size, alignment)))
        return E_OUTOFMEMORY;
    memset(object, 0, required_size);

    if (FAILED(hr = d3d12_descriptor_heap_init(object, device, desc)))
    {
        vkd3d_free_aligned(object);
        return hr;
    }

    if (desc->Type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV || desc->Type == D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER)
    {
        if (d3d12_device_use_embedded_mutable_descriptors(device))
        {
            /* Need to guarantee that this offset is aligned to 32 byte.
             * We're guaranteed the base allocation is aligned, but to align the mutable descriptor binding itself,
             * we might need to get creative.
             * We can tweak the descriptor set layout such that we get an aligned offset, however. */
            object->cpu_va.ptr = (SIZE_T)object->sets[0].mapped_set;
            assert(!(object->cpu_va.ptr & VKD3D_RESOURCE_EMBEDDED_METADATA_OFFSET_LOG2_MASK));

            if (!(desc->Flags & D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE) &&
                    desc->Type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV)
            {
                if (!(device->bindless_state.flags & VKD3D_BINDLESS_MUTABLE_EMBEDDED_PACKED_METADATA))
                {
                    /* Need to encode offset to metadata from any given CPU VA.
                     * Samplers don't require metadata structs, only non-shader visible resource heap does. */
                    object->cpu_va.ptr |= vkd3d_log2i_ceil(descriptor_size * max(1u, desc->NumDescriptors));
                }
                else
                {
                    /* Use this bit only to mark if this is a shader visible heap or not.
                     * If we're copying to shader visible heap,
                     * we can use non-temporal copies for more perf on Deck.
                     * For the more generic functions which decode VAs, the log2 offset must be greater
                     * than this value for it to detect planar metadata.
                     * Specialized functions can make use of this bit to enter more optimal code paths. */

                    /* Ignore all of this for sampler heaps since they are irrelevant
                     * from a performance standpoint. */
                    object->cpu_va.ptr += VKD3D_RESOURCE_EMBEDDED_CACHED_MASK;
                }
            }
        }
        else
        {
            /* See comments above on how this is supposed to work */
            object->cpu_va.ptr = (SIZE_T)object + num_descriptor_bits;
        }
    }
    else
    {
        object->cpu_va.ptr = (SIZE_T)object->descriptors;
    }

    d3d12_descriptor_heap_init_descriptors(object);

    TRACE("Created descriptor heap %p.\n", object);

#ifdef VKD3D_ENABLE_DESCRIPTOR_QA
    object->cookie = vkd3d_allocate_cookie();
    vkd3d_descriptor_debug_register_heap(object->descriptor_heap_info.host_ptr, object->cookie, desc);
#endif

    *descriptor_heap = object;

    return S_OK;
}

void d3d12_descriptor_heap_cleanup(struct d3d12_descriptor_heap *descriptor_heap)
{
    const struct vkd3d_vk_device_procs *vk_procs = &descriptor_heap->device->vk_procs;
    struct d3d12_device *device = descriptor_heap->device;

#ifndef VKD3D_NO_TRACE_MESSAGES
    if (descriptor_heap->desc.Type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV ||
            descriptor_heap->desc.Type == D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER)
    {
        TRACE("Freeing descriptor heap:\n");
        d3d12_descriptor_heap_report_allocation(&descriptor_heap->desc);
    }
#endif

    if (!descriptor_heap->device_allocation.vk_memory)
        vkd3d_free(descriptor_heap->host_memory);

    if (descriptor_heap->gpu_va != 0)
        d3d12_device_return_descriptor_heap_gpu_va(device, descriptor_heap->gpu_va);

    VK_CALL(vkDestroyBuffer(device->vk_device, descriptor_heap->vk_buffer, NULL));
    vkd3d_free_device_memory(device, &descriptor_heap->device_allocation);

    VK_CALL(vkDestroyDescriptorPool(device->vk_device, descriptor_heap->vk_descriptor_pool, NULL));

    if (!descriptor_heap->descriptor_buffer.device_allocation.vk_memory)
        vkd3d_free_aligned(descriptor_heap->descriptor_buffer.host_allocation);
    vkd3d_free_device_memory(device, &descriptor_heap->descriptor_buffer.device_allocation);
    VK_CALL(vkDestroyBuffer(device->vk_device, descriptor_heap->descriptor_buffer.vk_buffer, NULL));

    vkd3d_descriptor_debug_unregister_heap(descriptor_heap->cookie);
}

bool d3d12_descriptor_heap_require_padding_descriptors(void)
{
    uint32_t quirks;
    unsigned int i;

    if (vkd3d_descriptor_debug_active_descriptor_qa_checks())
        return true;

    /* If we use descriptor heap robustness, reserve a dummy descriptor we can use
     * as fake NULL descriptor. */
    quirks = vkd3d_shader_quirk_info.default_quirks | vkd3d_shader_quirk_info.global_quirks;
    for (i = 0; i < vkd3d_shader_quirk_info.num_hashes; i++)
        quirks |= vkd3d_shader_quirk_info.hashes[i].quirks;
    return !!(quirks & VKD3D_SHADER_QUIRK_DESCRIPTOR_HEAP_ROBUSTNESS);
}

static void d3d12_query_heap_set_name(struct d3d12_query_heap *heap, const char *name)
{
    if (heap->vk_query_pool)
    {
        vkd3d_set_vk_object_name(heap->device, (uint64_t)heap->vk_query_pool,
                VK_OBJECT_TYPE_QUERY_POOL, name);
    }
    else /*if (heap->vk_buffer)*/
    {
        vkd3d_set_vk_object_name(heap->device, (uint64_t)heap->vk_buffer,
                VK_OBJECT_TYPE_BUFFER, name);
    }
}

/* ID3D12QueryHeap */
static HRESULT STDMETHODCALLTYPE d3d12_query_heap_QueryInterface(ID3D12QueryHeap *iface,
        REFIID iid, void **out)
{
    struct d3d12_query_heap *query_heap = impl_from_ID3D12QueryHeap(iface);

    TRACE("iface %p, iid %s, out %p.\n", iface, debugstr_guid(iid), out);

    if (!out)
        return E_POINTER;

    if (IsEqualGUID(iid, &IID_ID3D12QueryHeap)
            || IsEqualGUID(iid, &IID_ID3D12Pageable)
            || IsEqualGUID(iid, &IID_ID3D12DeviceChild)
            || IsEqualGUID(iid, &IID_ID3D12Object)
            || IsEqualGUID(iid, &IID_IUnknown))
    {
        ID3D12QueryHeap_AddRef(iface);
        *out = iface;
        return S_OK;
    }

    if (IsEqualGUID(iid, &IID_ID3DDestructionNotifier))
    {
        ID3DDestructionNotifier_AddRef(&query_heap->destruction_notifier.ID3DDestructionNotifier_iface);
        *out = &query_heap->destruction_notifier.ID3DDestructionNotifier_iface;
        return S_OK;
    }

    WARN("%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid(iid));

    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE d3d12_query_heap_AddRef(ID3D12QueryHeap *iface)
{
    struct d3d12_query_heap *heap = impl_from_ID3D12QueryHeap(iface);
    ULONG refcount = InterlockedIncrement(&heap->refcount);

    TRACE("%p increasing refcount to %u.\n", heap, refcount);

    return refcount;
}

static ULONG STDMETHODCALLTYPE d3d12_query_heap_Release(ID3D12QueryHeap *iface)
{
    struct d3d12_query_heap *heap = impl_from_ID3D12QueryHeap(iface);
    ULONG refcount = InterlockedDecrement(&heap->refcount);

    TRACE("%p decreasing refcount to %u.\n", heap, refcount);

    if (!refcount)
    {
        struct d3d12_device *device = heap->device;
        const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;

        d3d_destruction_notifier_free(&heap->destruction_notifier);
        vkd3d_private_store_destroy(&heap->private_store);

        VK_CALL(vkDestroyQueryPool(device->vk_device, heap->vk_query_pool, NULL));
        VK_CALL(vkDestroyBuffer(device->vk_device, heap->vk_buffer, NULL));
        vkd3d_free_device_memory(device, &heap->device_allocation);
        vkd3d_descriptor_debug_unregister_cookie(device->descriptor_qa_global_info, heap->cookie);
        vkd3d_free(heap);

        d3d12_device_release(device);
    }

    return refcount;
}

static HRESULT STDMETHODCALLTYPE d3d12_query_heap_GetPrivateData(ID3D12QueryHeap *iface,
        REFGUID guid, UINT *data_size, void *data)
{
    struct d3d12_query_heap *heap = impl_from_ID3D12QueryHeap(iface);

    TRACE("iface %p, guid %s, data_size %p, data %p.\n", iface, debugstr_guid(guid), data_size, data);

    return vkd3d_get_private_data(&heap->private_store, guid, data_size, data);
}

static HRESULT STDMETHODCALLTYPE d3d12_query_heap_SetPrivateData(ID3D12QueryHeap *iface,
        REFGUID guid, UINT data_size, const void *data)
{
    struct d3d12_query_heap *heap = impl_from_ID3D12QueryHeap(iface);

    TRACE("iface %p, guid %s, data_size %u, data %p.\n", iface, debugstr_guid(guid), data_size, data);

    return vkd3d_set_private_data(&heap->private_store, guid, data_size, data,
            (vkd3d_set_name_callback) d3d12_query_heap_set_name, heap);
}

static HRESULT STDMETHODCALLTYPE d3d12_query_heap_SetPrivateDataInterface(ID3D12QueryHeap *iface,
        REFGUID guid, const IUnknown *data)
{
    struct d3d12_query_heap *heap = impl_from_ID3D12QueryHeap(iface);

    TRACE("iface %p, guid %s, data %p.\n", iface, debugstr_guid(guid), data);

    return vkd3d_set_private_data_interface(&heap->private_store, guid, data,
            (vkd3d_set_name_callback) d3d12_query_heap_set_name, heap);
}

static HRESULT STDMETHODCALLTYPE d3d12_query_heap_GetDevice(ID3D12QueryHeap *iface, REFIID iid, void **device)
{
    struct d3d12_query_heap *heap = impl_from_ID3D12QueryHeap(iface);

    TRACE("iface %p, iid %s, device %p.\n", iface, debugstr_guid(iid), device);

    return d3d12_device_query_interface(heap->device, iid, device);
}

CONST_VTBL struct ID3D12QueryHeapVtbl d3d12_query_heap_vtbl =
{
    /* IUnknown methods */
    d3d12_query_heap_QueryInterface,
    d3d12_query_heap_AddRef,
    d3d12_query_heap_Release,
    /* ID3D12Object methods */
    d3d12_query_heap_GetPrivateData,
    d3d12_query_heap_SetPrivateData,
    d3d12_query_heap_SetPrivateDataInterface,
    (void *)d3d12_object_SetName,
    /* ID3D12DeviceChild methods */
    d3d12_query_heap_GetDevice,
};

HRESULT d3d12_query_heap_create(struct d3d12_device *device, const D3D12_QUERY_HEAP_DESC *desc,
        struct d3d12_query_heap **heap)
{
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    D3D12_HEAP_PROPERTIES heap_properties;
    D3D12_RESOURCE_DESC1 buffer_desc;
    struct d3d12_query_heap *object;
    VkQueryPoolCreateInfo pool_info;
    VkResult vr;
    HRESULT hr;

    if (!(object = vkd3d_malloc(sizeof(*object))))
        return E_OUTOFMEMORY;

    memset(object, 0, sizeof(*object));
    object->ID3D12QueryHeap_iface.lpVtbl = &d3d12_query_heap_vtbl;
    object->refcount = 1;
    object->device = device;
    object->desc = *desc;
    object->cookie = vkd3d_allocate_cookie();

    if (!d3d12_query_heap_type_is_inline(desc->Type))
    {
        pool_info.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
        pool_info.pNext = NULL;
        pool_info.flags = 0;
        pool_info.queryCount = desc->Count;

        switch (desc->Type)
        {
            case D3D12_QUERY_HEAP_TYPE_TIMESTAMP:
            case D3D12_QUERY_HEAP_TYPE_COPY_QUEUE_TIMESTAMP:
                pool_info.queryType = VK_QUERY_TYPE_TIMESTAMP;
                pool_info.pipelineStatistics = 0;
                break;

            case D3D12_QUERY_HEAP_TYPE_PIPELINE_STATISTICS:
                pool_info.queryType = VK_QUERY_TYPE_PIPELINE_STATISTICS;
                pool_info.pipelineStatistics = VK_QUERY_PIPELINE_STATISTIC_INPUT_ASSEMBLY_VERTICES_BIT
                        | VK_QUERY_PIPELINE_STATISTIC_INPUT_ASSEMBLY_PRIMITIVES_BIT
                        | VK_QUERY_PIPELINE_STATISTIC_VERTEX_SHADER_INVOCATIONS_BIT
                        | VK_QUERY_PIPELINE_STATISTIC_GEOMETRY_SHADER_INVOCATIONS_BIT
                        | VK_QUERY_PIPELINE_STATISTIC_GEOMETRY_SHADER_PRIMITIVES_BIT
                        | VK_QUERY_PIPELINE_STATISTIC_CLIPPING_INVOCATIONS_BIT
                        | VK_QUERY_PIPELINE_STATISTIC_CLIPPING_PRIMITIVES_BIT
                        | VK_QUERY_PIPELINE_STATISTIC_FRAGMENT_SHADER_INVOCATIONS_BIT
                        | VK_QUERY_PIPELINE_STATISTIC_TESSELLATION_CONTROL_SHADER_PATCHES_BIT
                        | VK_QUERY_PIPELINE_STATISTIC_TESSELLATION_EVALUATION_SHADER_INVOCATIONS_BIT
                        | VK_QUERY_PIPELINE_STATISTIC_COMPUTE_SHADER_INVOCATIONS_BIT;
                break;

            default:
                WARN("Invalid query heap type %u.\n", desc->Type);
                vkd3d_free(object);
                return E_INVALIDARG;
        }

        if ((vr = VK_CALL(vkCreateQueryPool(device->vk_device, &pool_info, NULL, &object->vk_query_pool))) < 0)
        {
            WARN("Failed to create Vulkan query pool, vr %d.\n", vr);
            vkd3d_free(object);
            return hresult_from_vk_result(vr);
        }
    }
    else
    {
        memset(&heap_properties, 0, sizeof(heap_properties));
        heap_properties.Type = D3D12_HEAP_TYPE_DEFAULT;

        memset(&buffer_desc, 0, sizeof(buffer_desc));
        buffer_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        buffer_desc.Width = d3d12_query_heap_type_get_data_size(desc->Type) * desc->Count;
        buffer_desc.Height = 1;
        buffer_desc.DepthOrArraySize = 1;
        buffer_desc.MipLevels = 1;
        buffer_desc.Format = DXGI_FORMAT_UNKNOWN;
        buffer_desc.SampleDesc.Count = 1;
        buffer_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        buffer_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

        if (FAILED(hr = vkd3d_create_buffer(device, &heap_properties,
                D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS, &buffer_desc, "query-heap", &object->vk_buffer)))
        {
            vkd3d_free(object);
            return hr;
        }

        if (FAILED(hr = vkd3d_allocate_internal_buffer_memory(device, object->vk_buffer,
                VK_MEMORY_HEAP_DEVICE_LOCAL_BIT, &object->device_allocation)))
        {
            VK_CALL(vkDestroyBuffer(device->vk_device, object->vk_buffer, NULL));
            vkd3d_free(object);
            return hr;
        }

        object->va = vkd3d_get_buffer_device_address(device, object->vk_buffer);

        /* Explicit initialization is not required for these since
         * we can expect the buffer to be zero-initialized. */
        object->initialized = 1;
    }

    if (FAILED(hr = vkd3d_private_store_init(&object->private_store)))
    {
        vkd3d_free(object);
        return hr;
    }

    vkd3d_descriptor_debug_register_query_heap_cookie(device->descriptor_qa_global_info,
            object->cookie, desc);

    d3d_destruction_notifier_init(&object->destruction_notifier, (IUnknown*)&object->ID3D12QueryHeap_iface);
    d3d12_device_add_ref(device);

    TRACE("Created query heap %p.\n", object);

    *heap = object;
    return S_OK;
}

struct vkd3d_memory_topology
{
    VkDeviceSize largest_device_local_heap_size;
    VkDeviceSize largest_host_only_heap_size;
    uint32_t largest_device_local_heap_index;
    uint32_t largest_host_only_heap_index;
    uint32_t device_local_heap_count;
    uint32_t host_only_heap_count;
    bool exists_device_only_type;
    bool exists_host_only_type;
};

static void vkd3d_memory_info_get_topology(struct vkd3d_memory_topology *topology,
        struct d3d12_device *device)
{
    VkMemoryPropertyFlags flags;
    VkDeviceSize heap_size;
    uint32_t heap_index;
    unsigned int i;

    memset(topology, 0, sizeof(*topology));

    for (i = 0; i < device->memory_properties.memoryHeapCount; i++)
    {
        heap_size = device->memory_properties.memoryHeaps[i].size;
        if (device->memory_properties.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT)
        {
            if (heap_size > topology->largest_device_local_heap_size)
            {
                topology->largest_device_local_heap_index = i;
                topology->largest_device_local_heap_size = heap_size;
            }
            topology->device_local_heap_count++;
        }
        else
        {
            if (heap_size > topology->largest_host_only_heap_size)
            {
                topology->largest_host_only_heap_index = i;
                topology->largest_host_only_heap_size = heap_size;
            }
            topology->host_only_heap_count++;
        }
    }

    for (i = 0; i < device->memory_properties.memoryTypeCount; i++)
    {
        flags = device->memory_properties.memoryTypes[i].propertyFlags;
        heap_index = device->memory_properties.memoryTypes[i].heapIndex;

        if (heap_index == topology->largest_device_local_heap_index &&
                (flags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) != 0 &&
                (flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) == 0)
        {
            topology->exists_device_only_type = true;
        }
        else if (heap_index == topology->largest_host_only_heap_index &&
                (flags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) == 0 &&
                (flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0)
        {
            topology->exists_host_only_type = true;
        }
    }
}

static bool vkd3d_memory_topology_is_uma_like(const struct vkd3d_memory_topology *topology)
{
    if (topology->device_local_heap_count == 0 || topology->host_only_heap_count == 0)
        return true;
    else if (!topology->exists_device_only_type || !topology->exists_host_only_type)
        return true;
    else
        return false;
}

static VkMemoryPropertyFlags vkd3d_memory_info_descriptor_heap_memory_properties(
        VKD3D_UNUSED const struct vkd3d_memory_topology *topology,
        VKD3D_UNUSED const struct d3d12_device *device)
{
    if (vkd3d_config_flags & VKD3D_CONFIG_FLAG_FORCE_HOST_CACHED)
    {
        return VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                VK_MEMORY_PROPERTY_HOST_CACHED_BIT |
                VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    }
    else
    {
        /* There is very little risk in using DEVICE_LOCAL for descriptor memory.
         * We control all writes to this memory,
         * and it's fairly small in size compared to random UPLOAD heaps. */
        return VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT |
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    }
}

static bool vkd3d_memory_info_decide_hvv_usage(
        const struct vkd3d_memory_topology *topology,
        const struct d3d12_device *device)
{
    if (vkd3d_memory_topology_is_uma_like(topology))
    {
        /* Verify that there exists a DEVICE_LOCAL type that is not HOST_VISIBLE on this device
         * which maps to the largest device local heap. That way, it is safe to mask out all memory types which are
         * DEVICE_LOCAL | HOST_VISIBLE.
         * Similarly, there must exist a host-only type. */
        INFO("Topology: UMA-like topology.\n");
        return true;
    }
    else if (topology->device_local_heap_count <= 1)
    {
        INFO("Topology: No more than 1 device local heap, HVV access is viable.\n");
        return true;
    }
    else
    {
        INFO("Topology: Device heaps are split. Assuming small BAR situation.\n");
        return false;
    }
}

static VkMemoryPropertyFlags vkd3d_memory_info_upload_hvv_memory_properties(
        const struct vkd3d_memory_topology *topology, const struct d3d12_device *device, bool is_hvv_use_allowed)
{
    if (vkd3d_config_flags & VKD3D_CONFIG_FLAG_FORCE_HOST_CACHED)
    {
        INFO("Topology: Forcing HOST_CACHED | HOST_COHERENT for UPLOAD heap.\n");
        return VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                VK_MEMORY_PROPERTY_HOST_CACHED_BIT |
                VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    }

    if (vkd3d_config_flags & VKD3D_CONFIG_FLAG_NO_UPLOAD_HVV)
    {
        INFO("Topology: Forcing HOST_COHERENT for UPLOAD heap.\n");
        return VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    }

    if (is_hvv_use_allowed)
    {
        if (!vkd3d_memory_topology_is_uma_like(topology))
        {
            VkDeviceSize largest_size = device->memory_properties.memoryHeaps[topology->largest_device_local_heap_index].size;
            VkDeviceSize minimum_rebar_size_gb;
            VkDeviceSize minimum_rebar_size;

            /* 8 GiB GPUs are in an awkward place where they're just large enough
             * to take reasonable advantage of ReBAR, but small enough there is serious risk
             * of VRAM spill.
             * Allow this to be tweaked for app-opt purposes.
             * 6 GiB cards are extremely tiny in today's games, so ignore those. */
            minimum_rebar_size_gb = (vkd3d_config_flags & VKD3D_CONFIG_FLAG_SMALL_VRAM_REBAR) ? 7ull : 9ull;
            minimum_rebar_size = minimum_rebar_size_gb * 1000ull * 1000ull * 1000ull;

            if (largest_size < minimum_rebar_size)
            {
                INFO("Topology: largest device local heap is too small (%"PRIu64" bytes) for effective ReBAR.\n", largest_size);
                return VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
            }
        }

        INFO("Topology: HVV usage is allowed, using DEVICE_LOCAL | HOST_COHERENT for UPLOAD.\n");
        return VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT |
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    }
    else
    {
        INFO("Topology: HVV usage is not allowed, using HOST_COHERENT for UPLOAD.\n");
        return VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    }
}


static bool vkd3d_memory_info_decide_gpu_upload_heap(bool is_hvv_use_allowed)
{
    if (!is_hvv_use_allowed)
        return false;

    if (vkd3d_config_flags & VKD3D_CONFIG_FLAG_FORCE_HOST_CACHED)
        return false;

    if (vkd3d_config_flags & VKD3D_CONFIG_FLAG_NO_GPU_UPLOAD_HEAP)
        return false;

    return true;
}

static void vkd3d_memory_info_init_budgets(struct vkd3d_memory_info *info,
        const struct vkd3d_memory_topology *topology,
        struct d3d12_device *device)
{
    VkMemoryPropertyFlags flags;
    uint32_t heap_index;
    uint32_t i;

    info->rebar_budget_mask = 0;

    /* If we don't attempt to use DEVICE_LOCAL in a ReBAR style, don't even bother. */
    if (!(info->upload_heap_memory_properties & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT))
        return;

    /* Nothing to do on UMA-style implementations. */
    if (vkd3d_memory_topology_is_uma_like(topology))
        return;

    for (i = 0; i < device->memory_properties.memoryTypeCount; i++)
    {
        const VkMemoryPropertyFlags pinned_mask = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

        flags = device->memory_properties.memoryTypes[i].propertyFlags;
        heap_index = device->memory_properties.memoryTypes[i].heapIndex;

        if ((flags & pinned_mask) == pinned_mask && heap_index == topology->largest_device_local_heap_index)
        {
            /* Limit this type. This limit is a pure heuristic, and we might need further tuning here.
             * If there's a separate heap type for PCI-e BAR,
             * don't bother limiting it since the size is already going to be tiny.
             * The driver will limit us naturally.
             * All ReBAR enabled memory types share the same budget. */
            info->rebar_budget_mask |= 1u << i;
            info->rebar_budget = device->memory_properties.memoryHeaps[heap_index].size / 16;
            info->rebar_current = 0;
        }
    }

    INFO("Applying resizable BAR budget to memory types: 0x%x.\n", info->rebar_budget_mask);
}

void vkd3d_memory_info_cleanup(struct vkd3d_memory_info *info,
        struct d3d12_device *device)
{
    pthread_mutex_destroy(&info->budget_lock);
}

HRESULT vkd3d_memory_info_init(struct vkd3d_memory_info *info,
        struct d3d12_device *device)
{
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    VkDeviceBufferMemoryRequirements buffer_requirement_info;
    VkDeviceImageMemoryRequirements image_requirement_info;
    VkMemoryRequirements2 memory_requirements;
    struct vkd3d_memory_topology topology;
    VkBufferCreateInfo buffer_info;
    VkImageCreateInfo image_info;
    uint32_t sampled_type_mask;
    uint32_t host_visible_mask;
    uint32_t buffer_type_mask;
    uint32_t rt_ds_type_mask;
    bool is_hvv_use_allowed;
    uint32_t i;

    vkd3d_memory_info_get_topology(&topology, device);
    is_hvv_use_allowed = vkd3d_memory_info_decide_hvv_usage(&topology, device);
    info->upload_heap_memory_properties =
            vkd3d_memory_info_upload_hvv_memory_properties(&topology, device, is_hvv_use_allowed);
    info->has_gpu_upload_heap = vkd3d_memory_info_decide_gpu_upload_heap(is_hvv_use_allowed);
    info->descriptor_heap_memory_properties =
            vkd3d_memory_info_descriptor_heap_memory_properties(&topology, device);
    vkd3d_memory_info_init_budgets(info, &topology, device);
    info->has_used_gpu_upload_heap = 0;

    if (pthread_mutex_init(&info->budget_lock, NULL) != 0)
        return E_OUTOFMEMORY;

    buffer_requirement_info.sType = VK_STRUCTURE_TYPE_DEVICE_BUFFER_MEMORY_REQUIREMENTS;
    buffer_requirement_info.pNext = NULL;
    buffer_requirement_info.pCreateInfo = &buffer_info;

    image_requirement_info.sType = VK_STRUCTURE_TYPE_DEVICE_IMAGE_MEMORY_REQUIREMENTS;
    image_requirement_info.pNext = NULL;
    image_requirement_info.pCreateInfo = &image_info;
    image_requirement_info.planeAspect = 0;

    memory_requirements.sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2;
    memory_requirements.pNext = NULL;

    memset(&buffer_info, 0, sizeof(buffer_info));
    buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_info.size = 65536;
    buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
            VK_BUFFER_USAGE_TRANSFER_DST_BIT |
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT |
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
            VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT |
            VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT |
            VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
            VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;

    if (device->device_info.acceleration_structure_features.accelerationStructure)
    {
        /* Caps are not necessarily overridden yet.
         * Enabling RTAS should not change acceptable memory mask, but to be safe ... */
        buffer_info.usage |=
                VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR |
                VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR |
                VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;
    }

    VK_CALL(vkGetDeviceBufferMemoryRequirements(device->vk_device, &buffer_requirement_info, &memory_requirements));
    buffer_type_mask = memory_requirements.memoryRequirements.memoryTypeBits;

    memset(&image_info, 0, sizeof(image_info));
    image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image_info.imageType = VK_IMAGE_TYPE_2D;
    image_info.format = VK_FORMAT_R8G8B8A8_UNORM;
    image_info.extent.width = 16;
    image_info.extent.height = 16;
    image_info.extent.depth = 1;
    image_info.mipLevels = 1;
    image_info.arrayLayers = 1;
    image_info.samples = VK_SAMPLE_COUNT_1_BIT;
    image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    image_info.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT |
            VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
            VK_IMAGE_USAGE_SAMPLED_BIT |
            VK_IMAGE_USAGE_STORAGE_BIT;
    image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VK_CALL(vkGetDeviceImageMemoryRequirements(device->vk_device, &image_requirement_info, &memory_requirements));
    sampled_type_mask = memory_requirements.memoryRequirements.memoryTypeBits;

    image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    image_info.format = VK_FORMAT_R8G8B8A8_UNORM;
    image_info.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT |
            VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
            VK_IMAGE_USAGE_SAMPLED_BIT |
            VK_IMAGE_USAGE_STORAGE_BIT;

    VK_CALL(vkGetDeviceImageMemoryRequirements(device->vk_device, &image_requirement_info, &memory_requirements));
    rt_ds_type_mask = memory_requirements.memoryRequirements.memoryTypeBits;

    image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    image_info.format = VK_FORMAT_D32_SFLOAT_S8_UINT;
    image_info.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT |
            VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
            VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
            VK_IMAGE_USAGE_SAMPLED_BIT;

    VK_CALL(vkGetDeviceImageMemoryRequirements(device->vk_device, &image_requirement_info, &memory_requirements));
    rt_ds_type_mask &= memory_requirements.memoryRequirements.memoryTypeBits;

    info->non_cpu_accessible_domain.buffer_type_mask = buffer_type_mask;
    info->non_cpu_accessible_domain.sampled_type_mask = sampled_type_mask;
    info->non_cpu_accessible_domain.rt_ds_type_mask = rt_ds_type_mask;

    host_visible_mask = 0;
    for (i = 0; i < device->memory_properties.memoryTypeCount; i++)
        if (device->memory_properties.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)
            host_visible_mask |= 1u << i;

    /* We don't create images in host-visible memory anymore, only buffers */
    info->cpu_accessible_domain.buffer_type_mask = buffer_type_mask & host_visible_mask;
    info->cpu_accessible_domain.sampled_type_mask = buffer_type_mask & host_visible_mask;
    info->cpu_accessible_domain.rt_ds_type_mask = buffer_type_mask & host_visible_mask;

    TRACE("Device supports buffers on memory types 0x%#x.\n", buffer_type_mask);
    TRACE("Device supports textures on memory types 0x%#x.\n", sampled_type_mask);
    TRACE("Device supports render targets on memory types 0x%#x.\n", rt_ds_type_mask);
    TRACE("Device supports CPU visible textures on memory types 0x%#x.\n",
          info->cpu_accessible_domain.sampled_type_mask);
    TRACE("Device supports CPU visible render targets on memory types 0x%#x.\n",
          info->cpu_accessible_domain.rt_ds_type_mask);
    return S_OK;
}

HRESULT vkd3d_global_descriptor_buffer_init(struct vkd3d_global_descriptor_buffer *global_descriptor_buffer,
        struct d3d12_device *device)
{
    VkBufferUsageFlags2KHR vk_usage_flags;
    HRESULT hr;

    bool requires_offset_buffer = device->device_info.properties2.properties.limits.minStorageBufferOffsetAlignment > 4;
    bool uses_ssbo = device->device_info.properties2.properties.limits.minStorageBufferOffsetAlignment <= 16;
    if (!uses_ssbo)
        requires_offset_buffer = false;

    /* Don't bother with descriptor buffers if we need to keep offset buffer around.
     * Also, ignore descriptor buffers if implementation does not support non-uniform UBO indexing.
     * We want to keep the descriptor buffer path as lean as possible. */
    if (!device->device_info.descriptor_buffer_features.descriptorBuffer ||
            !device->device_info.descriptor_buffer_features.descriptorBufferPushDescriptors ||
            !device->device_info.vulkan_1_2_features.shaderUniformBufferArrayNonUniformIndexing ||
            requires_offset_buffer)
        return S_OK;

    if (device->device_info.mutable_descriptor_features.mutableDescriptorType)
    {
        /* If we are forced to use MUTABLE_SINGLE_SET due to small address space for resources,
         * we ignore descriptor buffers as well. Similar rationale to non-uniform UBO indexing.
         * We will not add even more code paths to deal with that.
         * Non-mutable + descriptor buffer is only relevant on AMD Windows driver for the time being,
         * and eventually we will make mutable a hard requirement, so don't bother checking that case. */
        VkDeviceSize required_resource_descriptors = VKD3D_MIN_VIEW_DESCRIPTOR_COUNT + 1; /* One magic SSBO for internal VA buffer. */
        uint32_t flags = VKD3D_BINDLESS_MUTABLE_TYPE;
        VkDeviceSize mutable_desc_size;

        if (uses_ssbo)
            flags |= VKD3D_BINDLESS_RAW_SSBO;

        /* If we cannot interleave SSBO / texel buffers, we'll have to do them side by side.
         * Implementation needs to support 2M descriptors in that case. */
        if (!vkd3d_bindless_supports_embedded_mutable_type(device, flags))
            required_resource_descriptors *= 2;

        mutable_desc_size = vkd3d_bindless_get_mutable_descriptor_type_size(device);
        if (device->device_info.descriptor_buffer_properties.maxResourceDescriptorBufferRange <
                required_resource_descriptors * mutable_desc_size)
        {
            INFO("Small descriptor heap detected, falling back to MUTABLE_SINGLE_SET.\n");
            return S_OK;
        }
    }

    vk_usage_flags = VK_BUFFER_USAGE_RESOURCE_DESCRIPTOR_BUFFER_BIT_EXT |
            VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;

    /* If push descriptors require extra backing storage, we need to let the driver reserve magic push space for it. */
    if (!device->device_info.descriptor_buffer_properties.bufferlessPushDescriptors)
        vk_usage_flags |= VK_BUFFER_USAGE_PUSH_DESCRIPTORS_DESCRIPTOR_BUFFER_BIT_EXT;

    /* Creates a default descriptor buffer we can use if the application does not bind anything.
     * This might happen if a meta shader is used without any prior descriptor heap bound,
     * and we need to use push descriptors with bufferlessPushDescriptors == VK_FALSE. */
    if (FAILED(hr = vkd3d_create_buffer_explicit_usage(device, vk_usage_flags,
            4 * 1024, "descriptor-buffer", &global_descriptor_buffer->resource.vk_buffer)))
    {
        return hr;
    }

    if (FAILED(hr = vkd3d_allocate_internal_buffer_memory(device, global_descriptor_buffer->resource.vk_buffer,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            &global_descriptor_buffer->resource.device_allocation)))
    {
        vkd3d_global_descriptor_buffer_cleanup(global_descriptor_buffer, device);
        return hr;
    }

    global_descriptor_buffer->resource.va =
            vkd3d_get_buffer_device_address(device, global_descriptor_buffer->resource.vk_buffer);
    global_descriptor_buffer->resource.usage = vk_usage_flags;

    vk_usage_flags = VK_BUFFER_USAGE_SAMPLER_DESCRIPTOR_BUFFER_BIT_EXT |
            VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    if (FAILED(hr = vkd3d_create_buffer_explicit_usage(device, vk_usage_flags,
            4 * 1024, "descriptor-buffer", &global_descriptor_buffer->sampler.vk_buffer)))
    {
        return hr;
    }

    if (FAILED(hr = vkd3d_allocate_internal_buffer_memory(device, global_descriptor_buffer->sampler.vk_buffer,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            &global_descriptor_buffer->sampler.device_allocation)))
    {
        vkd3d_global_descriptor_buffer_cleanup(global_descriptor_buffer, device);
        return hr;
    }

    global_descriptor_buffer->sampler.va =
            vkd3d_get_buffer_device_address(device, global_descriptor_buffer->sampler.vk_buffer);
    global_descriptor_buffer->sampler.usage = vk_usage_flags;

    return S_OK;
}

void vkd3d_global_descriptor_buffer_cleanup(struct vkd3d_global_descriptor_buffer *global_descriptor_buffer,
        struct d3d12_device *device)
{
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    VK_CALL(vkDestroyBuffer(device->vk_device, global_descriptor_buffer->resource.vk_buffer, NULL));
    VK_CALL(vkDestroyBuffer(device->vk_device, global_descriptor_buffer->sampler.vk_buffer, NULL));
    vkd3d_free_device_memory(device, &global_descriptor_buffer->resource.device_allocation);
    vkd3d_free_device_memory(device, &global_descriptor_buffer->sampler.device_allocation);
}
