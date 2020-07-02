/*
 * Copyright 2020 Joshua Ashton for Valve Software
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
 *
 *
 * This file uses extracts from the Wine project
 * specifically dlls/wined3d/swapchain.c and dlls/dxgi/swapchain.c
 * the license is as follows:
 * 
 * Copyright 2002-2003 Jason Edmeades
 * Copyright 2002-2003 Raphael Junqueira
 * Copyright 2005 Oliver Stieber
 * Copyright 2007-2008 Stefan Dösinger for CodeWeavers
 * Copyright 2011 Henri Verbeet for CodeWeavers
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

#include "vkd3d_win32.h"
#include "vkd3d_private.h"

#define INVALID_VK_IMAGE_INDEX (~(uint32_t)0)

static BOOL dxgi_validate_flip_swap_effect_format(DXGI_FORMAT format)
{
    switch (format)
    {
        case DXGI_FORMAT_R16G16B16A16_FLOAT:
        case DXGI_FORMAT_R10G10B10A2_UNORM:
        case DXGI_FORMAT_R8G8B8A8_UNORM:
        case DXGI_FORMAT_B8G8R8A8_UNORM:
            return TRUE;
        default:
            WARN("Invalid swapchain format %#x for flip presentation model.\n", format);
            return FALSE;
    }
}

BOOL dxgi_validate_swapchain_desc(const DXGI_SWAP_CHAIN_DESC1 *desc)
{
    uint32_t min_buffer_count;

    switch (desc->SwapEffect)
    {
        case DXGI_SWAP_EFFECT_DISCARD:
        case DXGI_SWAP_EFFECT_SEQUENTIAL:
            min_buffer_count = 1;
            break;

        case DXGI_SWAP_EFFECT_FLIP_DISCARD:
        case DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL:
            min_buffer_count = 2;

            if (desc->Format && !dxgi_validate_flip_swap_effect_format(desc->Format))
                return FALSE;

            if (desc->SampleDesc.Count != 1 || desc->SampleDesc.Quality)
            {
                WARN("Invalid sample desc %u, %u for swap effect %#x.\n",
                        desc->SampleDesc.Count, desc->SampleDesc.Quality, desc->SwapEffect);
                return FALSE;
            }
            break;

        default:
            WARN("Invalid swap effect %u used.\n", desc->SwapEffect);
            return FALSE;
    }

    if (desc->BufferCount < min_buffer_count || desc->BufferCount > DXGI_MAX_SWAP_CHAIN_BUFFERS)
    {
        WARN("BufferCount is %u.\n", desc->BufferCount);
        return FALSE;
    }

    return TRUE;
}

static HRESULT d3d12_get_output_from_window(IDXGIFactory *factory, HWND window, IDXGIOutput **dxgi_output)
{
    unsigned int adapter_idx, output_idx;
    DXGI_OUTPUT_DESC desc;
    IDXGIAdapter *adapter;
    IDXGIOutput *output;
    HMONITOR monitor;
    HRESULT hr;

    if (!(monitor = MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST)))
    {
        WARN("Failed to get monitor from window.\n");
        return DXGI_ERROR_INVALID_CALL;
    }

    for (adapter_idx = 0; SUCCEEDED(hr = IDXGIFactory_EnumAdapters(factory, adapter_idx, &adapter));
            ++adapter_idx)
    {
        for (output_idx = 0; SUCCEEDED(hr = IDXGIAdapter_EnumOutputs(adapter, output_idx,
                &output)); ++output_idx)
        {
            if (FAILED(hr = IDXGIOutput_GetDesc(output, &desc)))
            {
                WARN("Adapter %u output %u: Failed to get output desc, hr %#x.\n", adapter_idx,
                        output_idx, hr);
                IDXGIOutput_Release(output);
                continue;
            }

            if (desc.Monitor == monitor)
            {
                *dxgi_output = output;
                IDXGIAdapter_Release(adapter);
                return S_OK;
            }

            IDXGIOutput_Release(output);
        }
        IDXGIAdapter_Release(adapter);
    }

    if (hr != DXGI_ERROR_NOT_FOUND)
        WARN("Failed to enumerate outputs, hr %#x.\n", hr);

    WARN("Output could not be found.\n");
    return DXGI_ERROR_NOT_FOUND;
}

struct d3d12_swapchain
{
    IDXGISwapChain3 IDXGISwapChain3_iface;
    LONG refcount;
    struct vkd3d_private_store private_store;

    VkSwapchainKHR vk_swapchain;
    VkSurfaceKHR vk_surface;
    VkFence vk_fence;
    VkCommandPool vk_cmd_pool;
    VkImage vk_images[DXGI_MAX_SWAP_CHAIN_BUFFERS];
    VkImage vk_swapchain_images[DXGI_MAX_SWAP_CHAIN_BUFFERS];
    VkCommandBuffer vk_cmd_buffers[DXGI_MAX_SWAP_CHAIN_BUFFERS];
    VkSemaphore vk_semaphores[DXGI_MAX_SWAP_CHAIN_BUFFERS];
    ID3D12Resource *buffers[DXGI_MAX_SWAP_CHAIN_BUFFERS];
    unsigned int buffer_count;
    unsigned int vk_swapchain_width;
    unsigned int vk_swapchain_height;
    VkPresentModeKHR present_mode;

    uint32_t vk_image_index;
    unsigned int current_buffer_index;

    struct d3d12_command_queue *command_queue;
    IDXGIFactory *factory;

    HWND window;
    IDXGIOutput *target;
    DXGI_SWAP_CHAIN_DESC1 desc;
    DXGI_SWAP_CHAIN_FULLSCREEN_DESC fullscreen_desc;

    ID3D12Fence *frame_latency_fence;
    HANDLE frame_latency_event;

    uint64_t frame_number;
    uint32_t frame_latency;
};

static inline const struct vkd3d_vk_device_procs* d3d12_swapchain_procs(struct d3d12_swapchain* swapchain)
{
    return &swapchain->command_queue->device->vk_procs;
}

static inline struct ID3D12Device6* d3d12_swapchain_device_iface(struct d3d12_swapchain* swapchain)
{
    return &swapchain->command_queue->device->ID3D12Device_iface;
}

static inline struct d3d12_device* d3d12_swapchain_device(struct d3d12_swapchain* swapchain)
{
    return swapchain->command_queue->device;
}

static inline struct ID3D12CommandQueue* d3d12_swapchain_queue_iface(struct d3d12_swapchain* swapchain)
{
    return &swapchain->command_queue->ID3D12CommandQueue_iface;
}

static HRESULT d3d12_swapchain_set_display_mode(struct d3d12_swapchain *swapchain,
        IDXGIOutput* output, DXGI_MODE_DESC *mode)
{
    FIXME("TODO");
    return S_OK;
}

static HRESULT d3d12_swapchain_set_fullscreen(struct d3d12_swapchain *swapchain)
{
    FIXME("TODO!");
    return S_OK;
}

static HRESULT d3d12_swapchain_resize_target(struct d3d12_swapchain *swapchain,
        const DXGI_MODE_DESC *target_mode_desc)
{
    struct DXGI_MODE_DESC actual_mode;
    struct DXGI_OUTPUT_DESC output_desc;
    RECT original_window_rect, window_rect;
    int x, y, width, height;
    HWND window;
    HRESULT hr;

    if (!target_mode_desc)
    {
        WARN("Invalid pointer.\n");
        return DXGI_ERROR_INVALID_CALL;
    }

    window = swapchain->window;

    if (swapchain->fullscreen_desc.Windowed)
    {
        SetRect(&window_rect, 0, 0, target_mode_desc->Width, target_mode_desc->Height);
        AdjustWindowRectEx(&window_rect,
                GetWindowLongW(window, GWL_STYLE), FALSE,
                GetWindowLongW(window, GWL_EXSTYLE));
        GetWindowRect(window, &original_window_rect);

        x = original_window_rect.left;
        y = original_window_rect.top;
        width = window_rect.right - window_rect.left;
        height = window_rect.bottom - window_rect.top;
    }
    else
    {
        if (swapchain->desc.Flags & DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH)
        {
            actual_mode = *target_mode_desc;
            if (FAILED(hr = d3d12_swapchain_set_display_mode(swapchain, swapchain->target,
                    &actual_mode)))
            {
                ERR("Failed to set display mode, hr %#x.\n", hr);
                return hr;
            }
        }

        if (FAILED(hr = IDXGIOutput_GetDesc(swapchain->target, &output_desc)))
        {
            ERR("Failed to get output description, hr %#x.\n", hr);
            return hr;
        }

        x = output_desc.DesktopCoordinates.left;
        y = output_desc.DesktopCoordinates.top;
        width = output_desc.DesktopCoordinates.right - output_desc.DesktopCoordinates.left;
        height = output_desc.DesktopCoordinates.bottom - output_desc.DesktopCoordinates.top;
    }

    MoveWindow(window, x, y, width, height, TRUE);

    return S_OK;
}

static DXGI_FORMAT dxgi_format_from_vk_format(VkFormat vk_format)
{
    switch (vk_format)
    {
        case VK_FORMAT_B8G8R8A8_UNORM: return DXGI_FORMAT_B8G8R8A8_UNORM;
        case VK_FORMAT_R8G8B8A8_UNORM: return DXGI_FORMAT_R8G8B8A8_UNORM;
        case VK_FORMAT_A2B10G10R10_UNORM_PACK32: return DXGI_FORMAT_R10G10B10A2_UNORM;
        case VK_FORMAT_R16G16B16A16_SFLOAT: return DXGI_FORMAT_R16G16B16A16_FLOAT;
        default:
            WARN("Unhandled format %#x.\n", vk_format);
            return DXGI_FORMAT_UNKNOWN;
    }
}

static VkFormat get_swapchain_fallback_format(VkFormat vk_format)
{
    switch (vk_format)
    {
        case VK_FORMAT_R8G8B8A8_UNORM:
        case VK_FORMAT_A2B10G10R10_UNORM_PACK32:
        case VK_FORMAT_R16G16B16A16_SFLOAT:
            return VK_FORMAT_B8G8R8A8_UNORM;
        default:
            WARN("Unhandled format %#x.\n", vk_format);
            return VK_FORMAT_UNDEFINED;
    }
}

static HRESULT select_vk_format(const struct vkd3d_vk_device_procs *vk_procs,
        VkPhysicalDevice vk_physical_device, VkSurfaceKHR vk_surface,
        const DXGI_SWAP_CHAIN_DESC1 *swapchain_desc, VkFormat *vk_format)
{
    VkSurfaceFormatKHR *formats;
    uint32_t format_count;
    VkFormat format;
    unsigned int i;
    VkResult vr;

    *vk_format = VK_FORMAT_UNDEFINED;

    format = vkd3d_get_vk_format(swapchain_desc->Format);

    vr = vk_procs->vkGetPhysicalDeviceSurfaceFormatsKHR(vk_physical_device, vk_surface, &format_count, NULL);
    if (vr < 0 || !format_count)
    {
        WARN("Failed to get supported surface formats, vr %d.\n", vr);
        return DXGI_ERROR_INVALID_CALL;
    }

    if (!(formats = vkd3d_calloc(format_count, sizeof(*formats))))
        return E_OUTOFMEMORY;

    if ((vr = vk_procs->vkGetPhysicalDeviceSurfaceFormatsKHR(vk_physical_device,
            vk_surface, &format_count, formats)) < 0)
    {
        WARN("Failed to enumerate supported surface formats, vr %d.\n", vr);
        vkd3d_free(formats);
        return hresult_from_vk_result(vr);
    }

    for (i = 0; i < format_count; ++i)
    {
        if (formats[i].format == format && formats[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
            break;
    }
    if (i == format_count)
    {
        /* Try to create a swapchain with format conversion. */
        format = get_swapchain_fallback_format(format);
        WARN("Failed to find Vulkan swapchain format for %s.\n", debug_dxgi_format(swapchain_desc->Format));
        for (i = 0; i < format_count; ++i)
        {
            if (formats[i].format == format && formats[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
            {
                format = formats[i].format;
                break;
            }
        }
    }
    vkd3d_free(formats);
    if (i == format_count)
    {
        FIXME("Failed to find Vulkan swapchain format for %s.\n", debug_dxgi_format(swapchain_desc->Format));
        return DXGI_ERROR_UNSUPPORTED;
    }

    TRACE("Using Vulkan swapchain format %#x.\n", format);

    *vk_format = format;
    return S_OK;
}

static BOOL d3d12_swapchain_is_present_mode_supported(struct d3d12_swapchain *swapchain,
        VkPresentModeKHR present_mode)
{
    VkPhysicalDevice vk_physical_device = d3d12_swapchain_device(swapchain)->vk_physical_device;
    const struct vkd3d_vk_device_procs *vk_procs = d3d12_swapchain_procs(swapchain);
    VkPresentModeKHR *modes;
    uint32_t count, i;
    BOOL supported;
    VkResult vr;

    if (present_mode == VK_PRESENT_MODE_FIFO_KHR)
        return TRUE;

    if ((vr = vk_procs->vkGetPhysicalDeviceSurfacePresentModesKHR(vk_physical_device,
            swapchain->vk_surface, &count, NULL)) < 0)
    {
        WARN("Failed to get count of available present modes, vr %d.\n", vr);
        return FALSE;
    }

    supported = FALSE;

    if (!(modes = vkd3d_calloc(count, sizeof(*modes))))
        return FALSE;
    if ((vr = vk_procs->vkGetPhysicalDeviceSurfacePresentModesKHR(vk_physical_device,
            swapchain->vk_surface, &count, modes)) >= 0)
    {
        for (i = 0; i < count; ++i)
        {
            if (modes[i] == present_mode)
            {
                supported = TRUE;
                break;
            }
        }
    }
    else
    {
        WARN("Failed to get available present modes, vr %d.\n", vr);
    }
    vkd3d_free(modes);

    return supported;
}

static BOOL d3d12_swapchain_has_user_images(struct d3d12_swapchain *swapchain)
{
    return !!swapchain->vk_images[0];
}

static HRESULT d3d12_swapchain_create_user_buffers(struct d3d12_swapchain *swapchain, VkFormat vk_format)
{
    UINT i;
    HRESULT hr;
    D3D12_HEAP_PROPERTIES heap_properties;
    D3D12_RESOURCE_DESC resource_desc;
    struct d3d12_resource* object;

    heap_properties.Type = D3D12_HEAP_TYPE_DEFAULT;
    heap_properties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heap_properties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    heap_properties.CreationNodeMask = 1;
    heap_properties.VisibleNodeMask = 1;

    resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    resource_desc.Alignment = 0;
    resource_desc.Width = swapchain->desc.Width;
    resource_desc.Height = swapchain->desc.Height;
    resource_desc.DepthOrArraySize = 1;
    resource_desc.MipLevels = 1;
    resource_desc.Format = dxgi_format_from_vk_format(vk_format);
    resource_desc.SampleDesc.Count = 1;
    resource_desc.SampleDesc.Quality = 0;
    resource_desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    resource_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

    for (i = 0; i < swapchain->desc.BufferCount; i++)
    {
        if (FAILED(hr = d3d12_committed_resource_create(d3d12_swapchain_device(swapchain),
                &heap_properties, D3D12_HEAP_FLAG_NONE, &resource_desc,
                D3D12_RESOURCE_STATE_COPY_SOURCE, NULL, &object)))
        {
            ERR("Failed to create image for swapchain buffer");
            return hr;
        }

        swapchain->vk_images[i] = object->vk_image;
        swapchain->buffers[i] = (ID3D12Resource *)&object->ID3D12Resource_iface;

        vkd3d_resource_incref(swapchain->buffers[i]);
        ID3D12Resource_Release(swapchain->buffers[i]);
    }

    return S_OK;
}

static void vk_cmd_image_barrier(const struct vkd3d_vk_device_procs *vk_procs, VkCommandBuffer cmd_buffer,
        VkPipelineStageFlags src_stage_mask, VkPipelineStageFlags dst_stage_mask,
        VkAccessFlags src_access_mask, VkAccessFlags dst_access_mask,
        VkImageLayout old_layout, VkImageLayout new_layout, VkImage image)
{
    VkImageMemoryBarrier barrier;

    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.pNext = NULL;
    barrier.srcAccessMask = src_access_mask;
    barrier.dstAccessMask = dst_access_mask;
    barrier.oldLayout = old_layout;
    barrier.newLayout = new_layout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = VK_REMAINING_MIP_LEVELS;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = VK_REMAINING_ARRAY_LAYERS;

    vk_procs->vkCmdPipelineBarrier(cmd_buffer,
            src_stage_mask, dst_stage_mask, 0, 0, NULL, 0, NULL, 1, &barrier);
}

static VkResult d3d12_swapchain_record_swapchain_blit(struct d3d12_swapchain *swapchain,
        VkCommandBuffer vk_cmd_buffer, VkImage vk_dst_image, VkImage vk_src_image)
{
    const struct vkd3d_vk_device_procs *vk_procs = d3d12_swapchain_procs(swapchain);
    VkCommandBufferBeginInfo begin_info;
    VkImageBlit blit;
    VkFilter filter;
    VkResult vr;

    if (swapchain->desc.Width != swapchain->vk_swapchain_width
            || swapchain->desc.Height != swapchain->vk_swapchain_height)
        filter = VK_FILTER_LINEAR;
    else
        filter = VK_FILTER_NEAREST;

    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.pNext = NULL;
    begin_info.flags = 0;
    begin_info.pInheritanceInfo = NULL;

    if ((vr = vk_procs->vkBeginCommandBuffer(vk_cmd_buffer, &begin_info)) < 0)
    {
        WARN("Failed to begin command buffer, vr %d.\n", vr);
        return vr;
    }

    vk_cmd_image_barrier(vk_procs, vk_cmd_buffer,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
            0, VK_ACCESS_TRANSFER_WRITE_BIT,
            VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, vk_dst_image);

    blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    blit.srcSubresource.mipLevel = 0;
    blit.srcSubresource.baseArrayLayer = 0;
    blit.srcSubresource.layerCount = 1;
    blit.srcOffsets[0].x = 0;
    blit.srcOffsets[0].y = 0;
    blit.srcOffsets[0].z = 0;
    blit.srcOffsets[1].x = swapchain->desc.Width;
    blit.srcOffsets[1].y = swapchain->desc.Height;
    blit.srcOffsets[1].z = 1;
    blit.dstSubresource = blit.srcSubresource;
    blit.dstOffsets[0].x = 0;
    blit.dstOffsets[0].y = 0;
    blit.dstOffsets[0].z = 0;
    if (swapchain->desc.Scaling == DXGI_SCALING_NONE)
    {
        blit.srcOffsets[1].x = min(swapchain->vk_swapchain_width, blit.srcOffsets[1].x);
        blit.srcOffsets[1].y = min(swapchain->vk_swapchain_height, blit.srcOffsets[1].y);
        blit.dstOffsets[1].x = blit.srcOffsets[1].x;
        blit.dstOffsets[1].y = blit.srcOffsets[1].y;
    }
    else
    {
        /* FIXME: handle DXGI_SCALING_ASPECT_RATIO_STRETCH. */
        blit.dstOffsets[1].x = swapchain->vk_swapchain_width;
        blit.dstOffsets[1].y = swapchain->vk_swapchain_height;
    }
    blit.dstOffsets[1].z = 1;

    /* FIXME: Move to a proper graphics pipeline here
     * to avoid validation errors...
     */
    vk_procs->vkCmdBlitImage(vk_cmd_buffer,
            vk_src_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            vk_dst_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            1, &blit, filter);

    vk_cmd_image_barrier(vk_procs, vk_cmd_buffer,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
            VK_ACCESS_TRANSFER_WRITE_BIT, 0,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, vk_dst_image);

    if ((vr = vk_procs->vkEndCommandBuffer(vk_cmd_buffer)) < 0)
        WARN("Failed to end command buffer, vr %d.\n", vr);

    return vr;
}

static HRESULT d3d12_swapchain_prepare_command_buffers(struct d3d12_swapchain *swapchain,
        uint32_t queue_family_index)
{
    const struct vkd3d_vk_device_procs *vk_procs = d3d12_swapchain_procs(swapchain);
    VkDevice vk_device = d3d12_swapchain_device(swapchain)->vk_device;
    VkCommandBufferAllocateInfo allocate_info;
    VkSemaphoreCreateInfo semaphore_info;
    VkCommandPoolCreateInfo pool_info;
    unsigned int i;
    VkResult vr;

    pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool_info.pNext = NULL;
    pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pool_info.queueFamilyIndex = queue_family_index;

    assert(swapchain->vk_cmd_pool == VK_NULL_HANDLE);
    if ((vr = vk_procs->vkCreateCommandPool(vk_device, &pool_info,
            NULL, &swapchain->vk_cmd_pool)) < 0)
    {
        WARN("Failed to create command pool, vr %d.\n", vr);
        swapchain->vk_cmd_pool = VK_NULL_HANDLE;
        return hresult_from_vk_result(vr);
    }

    allocate_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocate_info.pNext = NULL;
    allocate_info.commandPool = swapchain->vk_cmd_pool;
    allocate_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocate_info.commandBufferCount = swapchain->buffer_count;

    if ((vr = vk_procs->vkAllocateCommandBuffers(vk_device, &allocate_info,
            swapchain->vk_cmd_buffers)) < 0)
    {
        WARN("Failed to allocate command buffers, vr %d.\n", vr);
        return hresult_from_vk_result(vr);
    }

    for (i = 0; i < swapchain->buffer_count; ++i)
    {
        semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        semaphore_info.pNext = NULL;
        semaphore_info.flags = 0;

        assert(swapchain->vk_semaphores[i] == VK_NULL_HANDLE);
        if ((vr = vk_procs->vkCreateSemaphore(vk_device, &semaphore_info,
                NULL, &swapchain->vk_semaphores[i])) < 0)
        {
            WARN("Failed to create semaphore, vr %d.\n", vr);
            swapchain->vk_semaphores[i] = VK_NULL_HANDLE;
            return hresult_from_vk_result(vr);
        }
    }

    return S_OK;
}

static HRESULT d3d12_swapchain_create_buffers(struct d3d12_swapchain *swapchain,
        VkFormat vk_swapchain_format, VkFormat vk_format)
{
    const struct vkd3d_vk_device_procs *vk_procs = d3d12_swapchain_procs(swapchain);
    struct vkd3d_image_resource_create_info resource_info;
    VkSwapchainKHR vk_swapchain = swapchain->vk_swapchain;
    ID3D12CommandQueue *queue = &swapchain->command_queue->ID3D12CommandQueue_iface;
    VkDevice vk_device = d3d12_swapchain_device(swapchain)->vk_device;
    ID3D12Device6* device = d3d12_swapchain_device_iface(swapchain);
    uint32_t image_count, queue_family_index;
    D3D12_COMMAND_QUEUE_DESC queue_desc;
    unsigned int i;
    VkResult vr;
    HRESULT hr;

    if ((vr = vk_procs->vkGetSwapchainImagesKHR(vk_device, vk_swapchain, &image_count, NULL)) < 0)
    {
        WARN("Failed to get Vulkan swapchain images, vr %d.\n", vr);
        return hresult_from_vk_result(vr);
    }
    if (image_count > ARRAY_SIZE(swapchain->vk_swapchain_images))
    {
        FIXME("Unsupported Vulkan swapchain image count %u.\n", image_count);
        return E_FAIL;
    }
    swapchain->buffer_count = image_count;
    if ((vr = vk_procs->vkGetSwapchainImagesKHR(vk_device, vk_swapchain,
            &image_count, swapchain->vk_swapchain_images)) < 0)
    {
        WARN("Failed to get Vulkan swapchain images, vr %d.\n", vr);
        return hresult_from_vk_result(vr);
    }

    resource_info.type = VKD3D_STRUCTURE_TYPE_IMAGE_RESOURCE_CREATE_INFO;
    resource_info.next = NULL;
    resource_info.desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    resource_info.desc.Alignment = 0;
    resource_info.desc.Width = swapchain->desc.Width;
    resource_info.desc.Height = swapchain->desc.Height;
    resource_info.desc.DepthOrArraySize = 1;
    resource_info.desc.MipLevels = 1;
    resource_info.desc.Format = dxgi_format_from_vk_format(vk_format);
    resource_info.desc.SampleDesc.Count = 1;
    resource_info.desc.SampleDesc.Quality = 0;
    resource_info.desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    resource_info.desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    resource_info.flags = VKD3D_RESOURCE_INITIAL_STATE_TRANSITION | VKD3D_RESOURCE_PRESENT_STATE_TRANSITION;

    queue_desc = ID3D12CommandQueue_GetDesc(queue);
    if (queue_desc.Type != D3D12_COMMAND_LIST_TYPE_DIRECT)
    {
        /* vkCmdBlitImage() is only supported for graphics queues. */
        FIXME("Swapchain blit not implemented for command queue type %#x.\n", queue_desc.Type);
        if (vk_swapchain_format != vk_format)
            return E_NOTIMPL;
        if (image_count != swapchain->desc.BufferCount)
        {
            FIXME("Got %u swapchain images, expected %u.\n", image_count, swapchain->desc.BufferCount);
            return E_NOTIMPL;
        }
    }
    queue_family_index = vkd3d_get_vk_queue_family_index(queue);

    if (queue_desc.Type == D3D12_COMMAND_LIST_TYPE_DIRECT)
    {
        TRACE("Creating user swapchain buffers.\n");

        if (FAILED(hr = d3d12_swapchain_create_user_buffers(swapchain, vk_format)))
            return hr;

        if (FAILED(hr = d3d12_swapchain_prepare_command_buffers(swapchain, queue_family_index)))
            return hr;

        return S_OK;
    }

    for (i = 0; i < swapchain->desc.BufferCount; ++i)
    {
        resource_info.vk_image = swapchain->vk_swapchain_images[i];
        resource_info.present_state = D3D12_RESOURCE_STATE_PRESENT;

        if (FAILED(hr = vkd3d_create_image_resource((ID3D12Device *)device, &resource_info, &swapchain->buffers[i])))
        {
            WARN("Failed to create vkd3d resource for Vulkan image %u, hr %#x.\n", i, hr);
            return hr;
        }

        vkd3d_resource_incref(swapchain->buffers[i]);
        ID3D12Resource_Release(swapchain->buffers[i]);
    }

    return S_OK;
}

static VkResult d3d12_swapchain_acquire_next_vulkan_image(struct d3d12_swapchain *swapchain)
{
    const struct vkd3d_vk_device_procs *vk_procs = d3d12_swapchain_procs(swapchain);
    VkDevice vk_device = d3d12_swapchain_device(swapchain)->vk_device;
    VkFence vk_fence = swapchain->vk_fence;
    VkResult vr;

    swapchain->vk_image_index = INVALID_VK_IMAGE_INDEX;

    if ((vr = vk_procs->vkAcquireNextImageKHR(vk_device, swapchain->vk_swapchain, UINT64_MAX,
            VK_NULL_HANDLE, vk_fence, &swapchain->vk_image_index)) < 0)
    {
        WARN("Failed to acquire next Vulkan image, vr %d.\n", vr);
        return vr;
    }

    if ((vr = vk_procs->vkWaitForFences(vk_device, 1, &vk_fence, VK_TRUE, UINT64_MAX)) != VK_SUCCESS)
    {
        ERR("Failed to wait for fence, vr %d.\n", vr);
        return vr;
    }
    if ((vr = vk_procs->vkResetFences(vk_device, 1, &vk_fence)) < 0)
        ERR("Failed to reset fence, vr %d.\n", vr);

    return vr;
}

static VkResult d3d12_swapchain_acquire_next_back_buffer(struct d3d12_swapchain *swapchain)
{
    VkResult vr;

    /* If we don't have user images, we need to acquire a Vulkan image in order
     * to get the correct value for the current back buffer index. */
    if (d3d12_swapchain_has_user_images(swapchain))
        return VK_SUCCESS;

    if ((vr = d3d12_swapchain_acquire_next_vulkan_image(swapchain)) >= 0)
        swapchain->current_buffer_index = swapchain->vk_image_index;
    else
        ERR("Failed to acquire Vulkan image, vr %d.\n", vr);

    return vr;
}

static void d3d12_swapchain_destroy_buffers(struct d3d12_swapchain *swapchain, BOOL destroy_user_buffers)
{
    const struct vkd3d_vk_device_procs *vk_procs = d3d12_swapchain_procs(swapchain);
    VkQueue vk_queue;
    unsigned int i;

    if (swapchain->command_queue)
    {
        if ((vk_queue = vkd3d_acquire_vk_queue(d3d12_swapchain_queue_iface(swapchain))))
        {
            vk_procs->vkQueueWaitIdle(vk_queue);

            vkd3d_release_vk_queue(d3d12_swapchain_queue_iface(swapchain));
        }
        else
        {
            WARN("Failed to acquire Vulkan queue.\n");
        }
    }

    for (i = 0; i < swapchain->desc.BufferCount; ++i)
    {
        if (swapchain->buffers[i] && (destroy_user_buffers || !d3d12_swapchain_has_user_images(swapchain)))
        {
            vkd3d_resource_decref(swapchain->buffers[i]);
            swapchain->buffers[i] = NULL;
        }
    }

    if (swapchain->command_queue->device->vk_device)
    {
        for (i = 0; i < swapchain->buffer_count; ++i)
        {
            vk_procs->vkDestroySemaphore(swapchain->command_queue->device->vk_device, swapchain->vk_semaphores[i], NULL);
            swapchain->vk_semaphores[i] = VK_NULL_HANDLE;
        }
        vk_procs->vkDestroyCommandPool(swapchain->command_queue->device->vk_device, swapchain->vk_cmd_pool, NULL);
        swapchain->vk_cmd_pool = VK_NULL_HANDLE;
    }
}

static HRESULT d3d12_swapchain_create_vulkan_swapchain(struct d3d12_swapchain *swapchain)
{
    VkPhysicalDevice vk_physical_device = d3d12_swapchain_device(swapchain)->vk_physical_device;
    const struct vkd3d_vk_device_procs *vk_procs = d3d12_swapchain_procs(swapchain);
    VkSwapchainCreateInfoKHR vk_swapchain_desc;
    VkDevice vk_device = d3d12_swapchain_device(swapchain)->vk_device;
    VkFormat vk_format, vk_swapchain_format;
    unsigned int width, height, image_count;
    VkSurfaceCapabilitiesKHR surface_caps;
    VkSwapchainKHR vk_swapchain;
    VkImageUsageFlags usage;
    VkResult vr;
    HRESULT hr;

    if (!(vk_format = vkd3d_get_vk_format(swapchain->desc.Format)))
    {
        WARN("Invalid format %#x.\n", swapchain->desc.Format);
        return DXGI_ERROR_INVALID_CALL;
    }

    if (FAILED(hr = select_vk_format(vk_procs, vk_physical_device,
            swapchain->vk_surface, &swapchain->desc, &vk_swapchain_format)))
        return hr;

    if ((vr = vk_procs->vkGetPhysicalDeviceSurfaceCapabilitiesKHR(vk_physical_device,
            swapchain->vk_surface, &surface_caps)) < 0)
    {
        WARN("Failed to get surface capabilities, vr %d.\n", vr);
        return hresult_from_vk_result(vr);
    }

    image_count = swapchain->desc.BufferCount;
    image_count = max(image_count, surface_caps.minImageCount);
    if (surface_caps.maxImageCount)
        image_count = min(image_count, surface_caps.maxImageCount);

    if (image_count != swapchain->desc.BufferCount)
    {
        WARN("Buffer count %u is not supported (%u-%u).\n", swapchain->desc.BufferCount,
                surface_caps.minImageCount, surface_caps.maxImageCount);
    }

    width = swapchain->desc.Width;
    height = swapchain->desc.Height;
    width = max(width, surface_caps.minImageExtent.width);
    width = min(width, surface_caps.maxImageExtent.width);
    height = max(height, surface_caps.minImageExtent.height);
    height = min(height, surface_caps.maxImageExtent.height);

    if (width != swapchain->desc.Width || height != swapchain->desc.Height)
    {
        WARN("Swapchain dimensions %ux%u are not supported (%u-%u x %u-%u).\n",
                swapchain->desc.Width, swapchain->desc.Height,
                surface_caps.minImageExtent.width, surface_caps.maxImageExtent.width,
                surface_caps.minImageExtent.height, surface_caps.maxImageExtent.height);
    }

    TRACE("Vulkan swapchain extent %ux%u.\n", width, height);

    if (!(surface_caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR))
    {
        FIXME("Unsupported alpha mode.\n");
        return DXGI_ERROR_UNSUPPORTED;
    }

    usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    usage |= surface_caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    usage |= surface_caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if (!(usage & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) || !(usage & VK_IMAGE_USAGE_TRANSFER_DST_BIT))
        WARN("Transfer not supported for swapchain images.\n");

    vk_swapchain_desc.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    vk_swapchain_desc.pNext = NULL;
    vk_swapchain_desc.flags = 0;
    vk_swapchain_desc.surface = swapchain->vk_surface;
    vk_swapchain_desc.minImageCount = image_count;
    vk_swapchain_desc.imageFormat = vk_swapchain_format;
    vk_swapchain_desc.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    vk_swapchain_desc.imageExtent.width = width;
    vk_swapchain_desc.imageExtent.height = height;
    vk_swapchain_desc.imageArrayLayers = 1;
    vk_swapchain_desc.imageUsage = usage;
    vk_swapchain_desc.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    vk_swapchain_desc.queueFamilyIndexCount = 0;
    vk_swapchain_desc.pQueueFamilyIndices = NULL;
    vk_swapchain_desc.preTransform = surface_caps.currentTransform;
    vk_swapchain_desc.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    vk_swapchain_desc.presentMode = swapchain->present_mode;
    vk_swapchain_desc.clipped = VK_TRUE;
    vk_swapchain_desc.oldSwapchain = swapchain->vk_swapchain;
    if ((vr = vk_procs->vkCreateSwapchainKHR(vk_device, &vk_swapchain_desc, NULL, &vk_swapchain)) < 0)
    {
        WARN("Failed to create Vulkan swapchain, vr %d.\n", vr);
        return hresult_from_vk_result(vr);
    }

    if (swapchain->vk_swapchain)
        vk_procs->vkDestroySwapchainKHR(swapchain->command_queue->device->vk_device, swapchain->vk_swapchain, NULL);

    swapchain->vk_swapchain = vk_swapchain;
    swapchain->vk_swapchain_width = width;
    swapchain->vk_swapchain_height = height;

    swapchain->vk_image_index = INVALID_VK_IMAGE_INDEX;

    return d3d12_swapchain_create_buffers(swapchain, vk_swapchain_format, vk_format);
}

static HRESULT d3d12_swapchain_recreate_vulkan_swapchain(struct d3d12_swapchain *swapchain)
{
    VkResult vr;
    HRESULT hr;

    if (SUCCEEDED(hr = d3d12_swapchain_create_vulkan_swapchain(swapchain)))
    {
        vr = d3d12_swapchain_acquire_next_back_buffer(swapchain);
        hr = hresult_from_vk_result(vr);
    }
    else
    {
        ERR("Failed to recreate Vulkan swapchain, hr %#x.\n", hr);
    }

    return hr;
}

static inline struct d3d12_swapchain *d3d12_swapchain_from_IDXGISwapChain3(IDXGISwapChain3 *iface)
{
    return CONTAINING_RECORD(iface, struct d3d12_swapchain, IDXGISwapChain3_iface);
}

/* IUnknown methods */

static HRESULT STDMETHODCALLTYPE d3d12_swapchain_QueryInterface(IDXGISwapChain3 *iface, REFIID iid, void **object)
{
    TRACE("iface %p, iid %s, object %p.\n", iface, debugstr_guid(iid), object);

    if (IsEqualGUID(iid, &IID_IUnknown)
            || IsEqualGUID(iid, &IID_IDXGIObject)
            || IsEqualGUID(iid, &IID_IDXGIDeviceSubObject)
            || IsEqualGUID(iid, &IID_IDXGISwapChain)
            || IsEqualGUID(iid, &IID_IDXGISwapChain1)
            || IsEqualGUID(iid, &IID_IDXGISwapChain2)
            || IsEqualGUID(iid, &IID_IDXGISwapChain3))
    {
        IDXGISwapChain3_AddRef(iface);
        *object = iface;
        return S_OK;
    }

    WARN("%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid(iid));

    *object = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE d3d12_swapchain_AddRef(IDXGISwapChain3 *iface)
{
    struct d3d12_swapchain *swapchain = d3d12_swapchain_from_IDXGISwapChain3(iface);
    ULONG refcount = InterlockedIncrement(&swapchain->refcount);

    TRACE("%p increasing refcount to %u.\n", swapchain, refcount);

    return refcount;
}

static void d3d12_swapchain_destroy(struct d3d12_swapchain *swapchain)
{
    const struct vkd3d_vk_device_procs *vk_procs = d3d12_swapchain_procs(swapchain);

    d3d12_swapchain_destroy_buffers(swapchain, TRUE);

    if (swapchain->frame_latency_event)
        CloseHandle(swapchain->frame_latency_event);

    if (swapchain->frame_latency_fence)
        ID3D12Fence_Release(swapchain->frame_latency_fence);

    ID3D12CommandQueue_Release(d3d12_swapchain_queue_iface(swapchain));

    vkd3d_private_store_destroy(&swapchain->private_store);

    if (swapchain->command_queue->device->vk_device)
    {
        vk_procs->vkDestroyFence(swapchain->command_queue->device->vk_device, swapchain->vk_fence, NULL);
        vk_procs->vkDestroySwapchainKHR(swapchain->command_queue->device->vk_device, swapchain->vk_swapchain, NULL);
    }

    vk_procs->vkDestroySurfaceKHR(d3d12_swapchain_device(swapchain)->vkd3d_instance->vk_instance, swapchain->vk_surface, NULL);

    if (swapchain->target)
    {
        WARN("Destroying fullscreen swapchain.\n");
        IDXGIOutput_Release(swapchain->target);
    }

    d3d12_device_release(d3d12_swapchain_device(swapchain));

    if (swapchain->factory)
        IDXGIFactory_Release(swapchain->factory);
}

static ULONG STDMETHODCALLTYPE d3d12_swapchain_Release(IDXGISwapChain3 *iface)
{
    struct d3d12_swapchain *swapchain = d3d12_swapchain_from_IDXGISwapChain3(iface);
    ULONG refcount = InterlockedDecrement(&swapchain->refcount);

    TRACE("%p decreasing refcount to %u.\n", swapchain, refcount);

    if (!refcount)
    {
        d3d12_swapchain_destroy(swapchain);
        vkd3d_free(swapchain);
    }

    return refcount;
}

/* IDXGIObject methods */

static HRESULT STDMETHODCALLTYPE d3d12_swapchain_SetPrivateData(IDXGISwapChain3 *iface,
        REFGUID guid, UINT data_size, const void *data)
{
    struct d3d12_swapchain *swapchain = d3d12_swapchain_from_IDXGISwapChain3(iface);

    TRACE("iface %p, guid %s, data_size %u, data %p.\n", iface, debugstr_guid(guid), data_size, data);

    return vkd3d_set_private_data(&swapchain->private_store, guid, data_size, data);
}

static HRESULT STDMETHODCALLTYPE d3d12_swapchain_SetPrivateDataInterface(IDXGISwapChain3 *iface,
        REFGUID guid, const IUnknown *object)
{
    struct d3d12_swapchain *swapchain = d3d12_swapchain_from_IDXGISwapChain3(iface);

    TRACE("iface %p, guid %s, object %p.\n", iface, debugstr_guid(guid), object);

    return vkd3d_set_private_data_interface(&swapchain->private_store, guid, object);
}

static HRESULT STDMETHODCALLTYPE d3d12_swapchain_GetPrivateData(IDXGISwapChain3 *iface,
        REFGUID guid, UINT *data_size, void *data)
{
    struct d3d12_swapchain *swapchain = d3d12_swapchain_from_IDXGISwapChain3(iface);

    TRACE("iface %p, guid %s, data_size %p, data %p.\n", iface, debugstr_guid(guid), data_size, data);

    return vkd3d_get_private_data(&swapchain->private_store, guid, data_size, data);
}

static HRESULT STDMETHODCALLTYPE d3d12_swapchain_GetParent(IDXGISwapChain3 *iface, REFIID iid, void **parent)
{
    struct d3d12_swapchain *swapchain = d3d12_swapchain_from_IDXGISwapChain3(iface);

    TRACE("iface %p, iid %s, parent %p.\n", iface, debugstr_guid(iid), parent);

    return IDXGIFactory_QueryInterface(swapchain->factory, iid, parent);
}

/* IDXGIDeviceSubObject methods */

static HRESULT STDMETHODCALLTYPE d3d12_swapchain_GetDevice(IDXGISwapChain3 *iface, REFIID iid, void **device)
{
    struct d3d12_swapchain *swapchain = d3d12_swapchain_from_IDXGISwapChain3(iface);

    TRACE("iface %p, iid %s, device %p.\n", iface, debugstr_guid(iid), device);

    return ID3D12Device6_QueryInterface(d3d12_swapchain_device_iface(swapchain), iid, device);
}

/* IDXGISwapChain methods */

static HRESULT d3d12_swapchain_set_sync_interval(struct d3d12_swapchain *swapchain,
        unsigned int sync_interval)
{
    VkPresentModeKHR present_mode;

    switch (sync_interval)
    {
        case 0:
            present_mode = VK_PRESENT_MODE_IMMEDIATE_KHR;
            break;
        default:
            FIXME("Unsupported sync interval %u.\n", sync_interval);
            present_mode = VK_PRESENT_MODE_FIFO_KHR;
            break;
        case 1:
            present_mode = VK_PRESENT_MODE_FIFO_KHR;
            break;
    }

    if (swapchain->present_mode == present_mode)
        return S_OK;

    if (!d3d12_swapchain_has_user_images(swapchain))
    {
        FIXME("Cannot recreate swapchain without user images.\n");
        return S_OK;
    }

    if (!d3d12_swapchain_is_present_mode_supported(swapchain, present_mode))
    {
        FIXME("Vulkan present mode %#x is not supported.\n", present_mode);
        return S_OK;
    }

    d3d12_swapchain_destroy_buffers(swapchain, FALSE);
    swapchain->present_mode = present_mode;
    return d3d12_swapchain_recreate_vulkan_swapchain(swapchain);
}

static VkResult d3d12_swapchain_queue_present(struct d3d12_swapchain *swapchain, VkQueue vk_queue)
{
    const struct vkd3d_vk_device_procs *vk_procs = d3d12_swapchain_procs(swapchain);
    VkPresentInfoKHR present_info;
    VkSubmitInfo submit_info;
    VkResult vr;

    if (swapchain->vk_image_index == INVALID_VK_IMAGE_INDEX)
    {
        if ((vr = d3d12_swapchain_acquire_next_vulkan_image(swapchain)) < 0)
            return vr;
    }

    assert(swapchain->vk_image_index < swapchain->buffer_count);

    present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present_info.pNext = NULL;
    present_info.waitSemaphoreCount = 0;
    present_info.pWaitSemaphores = NULL;
    present_info.swapchainCount = 1;
    present_info.pSwapchains = &swapchain->vk_swapchain;
    present_info.pImageIndices = &swapchain->vk_image_index;
    present_info.pResults = NULL;

    if (d3d12_swapchain_has_user_images(swapchain))
    {
        /* blit */
        VkCommandBuffer vk_cmd_buffer = swapchain->vk_cmd_buffers[swapchain->vk_image_index];
        VkImage vk_dst_image = swapchain->vk_swapchain_images[swapchain->vk_image_index];
        VkImage vk_src_image = swapchain->vk_images[swapchain->current_buffer_index];

        if ((vr = vk_procs->vkResetCommandBuffer(vk_cmd_buffer, 0)) < 0)
        {
            ERR("Failed to reset command buffer, vr %d.\n", vr);
            return vr;
        }

        if ((vr = d3d12_swapchain_record_swapchain_blit(swapchain,
                vk_cmd_buffer, vk_dst_image, vk_src_image)) < 0 )
            return vr;

        submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit_info.pNext = NULL;
        submit_info.waitSemaphoreCount = 0;
        submit_info.pWaitSemaphores = NULL;
        submit_info.pWaitDstStageMask = NULL;
        submit_info.commandBufferCount = 1;
        submit_info.pCommandBuffers = &vk_cmd_buffer;
        submit_info.signalSemaphoreCount = 1;
        submit_info.pSignalSemaphores = &swapchain->vk_semaphores[swapchain->vk_image_index];

        if ((vr = vk_procs->vkQueueSubmit(vk_queue, 1, &submit_info, VK_NULL_HANDLE)) < 0)
        {
            ERR("Failed to blit swapchain buffer, vr %d.\n", vr);
            return vr;
        }

        present_info.waitSemaphoreCount = 1;
        present_info.pWaitSemaphores = &swapchain->vk_semaphores[swapchain->vk_image_index];
    }

    if ((vr = vk_procs->vkQueuePresentKHR(vk_queue, &present_info)) >= 0)
        swapchain->vk_image_index = INVALID_VK_IMAGE_INDEX;

    return vr;
}

static HRESULT d3d12_swapchain_present(struct d3d12_swapchain *swapchain,
        unsigned int sync_interval, unsigned int flags)
{
    HANDLE frame_latency_event;
    VkQueue vk_queue;
    VkResult vr;
    HRESULT hr;

    if (sync_interval > 4)
    {
        WARN("Invalid sync interval %u.\n", sync_interval);
        return DXGI_ERROR_INVALID_CALL;
    }

    if (flags & ~DXGI_PRESENT_TEST)
        FIXME("Unimplemented flags %#x.\n", flags);
    if (flags & DXGI_PRESENT_TEST)
    {
        WARN("Returning S_OK for DXGI_PRESENT_TEST.\n");
        return S_OK;
    }

    if (FAILED(hr = d3d12_swapchain_set_sync_interval(swapchain, sync_interval)))
        return hr;

    if (!(vk_queue = vkd3d_acquire_vk_queue(d3d12_swapchain_queue_iface(swapchain))))
    {
        ERR("Failed to acquire Vulkan queue.\n");
        return E_FAIL;
    }

    vr = d3d12_swapchain_queue_present(swapchain, vk_queue);
    if (vr == VK_ERROR_OUT_OF_DATE_KHR)
    {
        vkd3d_release_vk_queue(d3d12_swapchain_queue_iface(swapchain));

        if (!d3d12_swapchain_has_user_images(swapchain))
        {
            FIXME("Cannot recreate swapchain without user images.\n");
            return DXGI_STATUS_MODE_CHANGED;
        }

        TRACE("Recreating Vulkan swapchain.\n");

        d3d12_swapchain_destroy_buffers(swapchain, FALSE);
        if (FAILED(hr = d3d12_swapchain_recreate_vulkan_swapchain(swapchain)))
            return hr;

        if (!(vk_queue = vkd3d_acquire_vk_queue(d3d12_swapchain_queue_iface(swapchain))))
        {
            ERR("Failed to acquire Vulkan queue.\n");
            return E_FAIL;
        }

        if ((vr = d3d12_swapchain_queue_present(swapchain, vk_queue)) < 0)
            ERR("Failed to present after recreating swapchain, vr %d.\n", vr);
    }

    vkd3d_release_vk_queue(d3d12_swapchain_queue_iface(swapchain));

    if (vr < 0)
    {
        ERR("Failed to queue present, vr %d.\n", vr);
        return hresult_from_vk_result(vr);
    }

    if ((frame_latency_event = swapchain->frame_latency_event))
    {
        ++swapchain->frame_number;

        if (FAILED(hr = ID3D12CommandQueue_Signal(d3d12_swapchain_queue_iface(swapchain),
                swapchain->frame_latency_fence, swapchain->frame_number)))
        {
            ERR("Failed to signal frame latency fence, hr %#x.\n", hr);
            return hr;
        }

        if (FAILED(hr = ID3D12Fence_SetEventOnCompletion(swapchain->frame_latency_fence,
                swapchain->frame_number - swapchain->frame_latency, frame_latency_event)))
        {
            ERR("Failed to enqueue frame latency event, hr %#x.\n", hr);
            return hr;
        }
    }

    swapchain->current_buffer_index = (swapchain->current_buffer_index + 1) % swapchain->desc.BufferCount;
    vr = d3d12_swapchain_acquire_next_back_buffer(swapchain);
    if (vr == VK_ERROR_OUT_OF_DATE_KHR)
    {
        if (!d3d12_swapchain_has_user_images(swapchain))
        {
            FIXME("Cannot recreate swapchain without user images.\n");
            return DXGI_STATUS_MODE_CHANGED;
        }

        TRACE("Recreating Vulkan swapchain.\n");

        d3d12_swapchain_destroy_buffers(swapchain, FALSE);
        return d3d12_swapchain_recreate_vulkan_swapchain(swapchain);
    }
    if (vr < 0)
        ERR("Failed to acquire next Vulkan image, vr %d.\n", vr);
    return hresult_from_vk_result(vr);
}

static HRESULT STDMETHODCALLTYPE d3d12_swapchain_Present(IDXGISwapChain3 *iface, UINT sync_interval, UINT flags)
{
    struct d3d12_swapchain *swapchain = d3d12_swapchain_from_IDXGISwapChain3(iface);

    TRACE("iface %p, sync_interval %u, flags %#x.\n", iface, sync_interval, flags);

    return d3d12_swapchain_present(swapchain, sync_interval, flags);
}

static HRESULT STDMETHODCALLTYPE d3d12_swapchain_GetBuffer(IDXGISwapChain3 *iface,
        UINT buffer_idx, REFIID iid, void **surface)
{
    struct d3d12_swapchain *swapchain = d3d12_swapchain_from_IDXGISwapChain3(iface);

    TRACE("iface %p, buffer_idx %u, iid %s, surface %p.\n",
            iface, buffer_idx, debugstr_guid(iid), surface);

    if (buffer_idx >= swapchain->desc.BufferCount)
    {
        WARN("Invalid buffer index %u.\n", buffer_idx);
        return DXGI_ERROR_INVALID_CALL;
    }

    assert(swapchain->buffers[buffer_idx]);
    return ID3D12Resource_QueryInterface(swapchain->buffers[buffer_idx], iid, surface);
}

static HRESULT STDMETHODCALLTYPE d3d12_swapchain_SetFullscreenState(IDXGISwapChain3 *iface,
        BOOL fullscreen, IDXGIOutput *target)
{
    struct d3d12_swapchain *swapchain = d3d12_swapchain_from_IDXGISwapChain3(iface);
    HRESULT hr;

    TRACE("iface %p, fullscreen %#x, target %p.\n", iface, fullscreen, target);

    if (!fullscreen && target)
    {
        WARN("Invalid call.\n");
        return DXGI_ERROR_INVALID_CALL;
    }

    if (target)
    {
        IDXGIOutput_AddRef(target);
    }
    else if (FAILED(hr = IDXGISwapChain3_GetContainingOutput(iface, &target)))
    {
        WARN("Failed to get target output for swapchain, hr %#x.\n", hr);
        return hr;
    }

    hr = d3d12_swapchain_set_fullscreen(swapchain);

    if (FAILED(hr))
        goto fail;

    swapchain->fullscreen_desc.Windowed = !fullscreen;
    if (!fullscreen)
    {
        IDXGIOutput_Release(target);
        target = NULL;
    }

    if (swapchain->target)
        IDXGIOutput_Release(swapchain->target);
    swapchain->target = target;

    return S_OK;

fail:
    IDXGIOutput_Release(target);

    return DXGI_ERROR_NOT_CURRENTLY_AVAILABLE;
}

static HRESULT STDMETHODCALLTYPE d3d12_swapchain_GetFullscreenState(IDXGISwapChain3 *iface,
        BOOL *fullscreen, IDXGIOutput **target)
{
    struct d3d12_swapchain *swapchain = d3d12_swapchain_from_IDXGISwapChain3(iface);

    TRACE("iface %p, fullscreen %p, target %p.\n", iface, fullscreen, target);

    if (fullscreen)
        *fullscreen = !swapchain->fullscreen_desc.Windowed;

    if (target && (*target = swapchain->target))
        IDXGIOutput_AddRef(*target);

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d3d12_swapchain_GetDesc(IDXGISwapChain3 *iface, DXGI_SWAP_CHAIN_DESC *desc)
{
    struct d3d12_swapchain *swapchain = d3d12_swapchain_from_IDXGISwapChain3(iface);
    const DXGI_SWAP_CHAIN_FULLSCREEN_DESC *fullscreen_desc = &swapchain->fullscreen_desc;
    const DXGI_SWAP_CHAIN_DESC1 *swapchain_desc = &swapchain->desc;

    TRACE("iface %p, desc %p.\n", iface, desc);

    if (!desc)
    {
        WARN("Invalid pointer.\n");
        return E_INVALIDARG;
    }

    desc->BufferDesc.Width = swapchain_desc->Width;
    desc->BufferDesc.Height = swapchain_desc->Height;
    desc->BufferDesc.RefreshRate = fullscreen_desc->RefreshRate;
    desc->BufferDesc.Format = swapchain_desc->Format;
    desc->BufferDesc.ScanlineOrdering = fullscreen_desc->ScanlineOrdering;
    desc->BufferDesc.Scaling = fullscreen_desc->Scaling;
    desc->SampleDesc = swapchain_desc->SampleDesc;
    desc->BufferUsage = swapchain_desc->BufferUsage;
    desc->BufferCount = swapchain_desc->BufferCount;
    desc->OutputWindow = swapchain->window;
    desc->Windowed = fullscreen_desc->Windowed;
    desc->SwapEffect = swapchain_desc->SwapEffect;
    desc->Flags = swapchain_desc->Flags;

    return S_OK;
}

static HRESULT d3d12_swapchain_resize_buffers(struct d3d12_swapchain *swapchain,
        UINT buffer_count, UINT width, UINT height, DXGI_FORMAT format, UINT flags)
{
    DXGI_SWAP_CHAIN_DESC1 *desc, new_desc;
    unsigned int i;
    ULONG refcount;

    if (flags)
        FIXME("Ignoring flags %#x.\n", flags);

    for (i = 0; i < swapchain->desc.BufferCount; ++i)
    {
        ID3D12Resource_AddRef(swapchain->buffers[i]);
        if ((refcount = ID3D12Resource_Release(swapchain->buffers[i])))
        {
            WARN("Buffer %p has %u references left.\n", swapchain->buffers[i], refcount);
            return DXGI_ERROR_INVALID_CALL;
        }
    }

    desc = &swapchain->desc;
    new_desc = swapchain->desc;

    if (buffer_count)
        new_desc.BufferCount = buffer_count;
    if (!width || !height)
    {
        RECT client_rect;

        if (!GetClientRect(swapchain->window, &client_rect))
        {
            WARN("Failed to get client rect, last error %#x.\n", GetLastError());
            return DXGI_ERROR_INVALID_CALL;
        }

        if (!width)
            width = client_rect.right;
        if (!height)
            height = client_rect.bottom;
    }
    new_desc.Width = width;
    new_desc.Height = height;

    if (format)
        new_desc.Format = format;

    if (!dxgi_validate_swapchain_desc(&new_desc))
        return DXGI_ERROR_INVALID_CALL;

    if (desc->Width == new_desc.Width && desc->Height == new_desc.Height
            && desc->Format == new_desc.Format && desc->BufferCount == new_desc.BufferCount)
        return S_OK;

    d3d12_swapchain_destroy_buffers(swapchain, TRUE);
    swapchain->desc = new_desc;
    return d3d12_swapchain_recreate_vulkan_swapchain(swapchain);
}

static HRESULT STDMETHODCALLTYPE d3d12_swapchain_ResizeBuffers(IDXGISwapChain3 *iface,
        UINT buffer_count, UINT width, UINT height, DXGI_FORMAT format, UINT flags)
{
    struct d3d12_swapchain *swapchain = d3d12_swapchain_from_IDXGISwapChain3(iface);

    TRACE("iface %p, buffer_count %u, width %u, height %u, format %s, flags %#x.\n",
            iface, buffer_count, width, height, debug_dxgi_format(format), flags);

    return d3d12_swapchain_resize_buffers(swapchain, buffer_count, width, height, format, flags);
}

static HRESULT STDMETHODCALLTYPE d3d12_swapchain_ResizeTarget(IDXGISwapChain3 *iface,
        const DXGI_MODE_DESC *target_mode_desc)
{
    struct d3d12_swapchain *swapchain = d3d12_swapchain_from_IDXGISwapChain3(iface);

    TRACE("iface %p, target_mode_desc %p.\n", iface, target_mode_desc);

    return d3d12_swapchain_resize_target(swapchain, target_mode_desc);
}

static HRESULT STDMETHODCALLTYPE d3d12_swapchain_GetContainingOutput(IDXGISwapChain3 *iface,
        IDXGIOutput **output)
{
    struct d3d12_swapchain *swapchain = d3d12_swapchain_from_IDXGISwapChain3(iface);
    IUnknown *device_parent;
    IDXGIFactory *factory;
    IDXGIAdapter *adapter;
    HRESULT hr;

    TRACE("iface %p, output %p.\n", iface, output);

    if (swapchain->target)
    {
        IDXGIOutput_AddRef(*output = swapchain->target);
        return S_OK;
    }

    device_parent = vkd3d_get_device_parent((ID3D12Device *)d3d12_swapchain_device_iface(swapchain));

    if (FAILED(hr = IUnknown_QueryInterface(device_parent, &IID_IDXGIAdapter, (void **)&adapter)))
    {
        WARN("Failed to get adapter, hr %#x.\n", hr);
        return hr;
    }

    if (FAILED(hr = IDXGIAdapter_GetParent(adapter, &IID_IDXGIFactory, (void **)&factory)))
    {
        WARN("Failed to get factory, hr %#x.\n", hr);
        IDXGIAdapter_Release(adapter);
        return hr;
    }

    hr = d3d12_get_output_from_window(factory, swapchain->window, output);
    IDXGIFactory_Release(factory);
    IDXGIAdapter_Release(adapter);
    return hr;
}

static HRESULT STDMETHODCALLTYPE d3d12_swapchain_GetFrameStatistics(IDXGISwapChain3 *iface,
        DXGI_FRAME_STATISTICS *stats)
{
    FIXME("iface %p, stats %p stub!\n", iface, stats);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d12_swapchain_GetLastPresentCount(IDXGISwapChain3 *iface,
        UINT *last_present_count)
{
    FIXME("iface %p, last_present_count %p stub!\n", iface, last_present_count);

    return E_NOTIMPL;
}

/* IDXGISwapChain1 methods */

static HRESULT STDMETHODCALLTYPE d3d12_swapchain_GetDesc1(IDXGISwapChain3 *iface, DXGI_SWAP_CHAIN_DESC1 *desc)
{
    struct d3d12_swapchain *swapchain = d3d12_swapchain_from_IDXGISwapChain3(iface);

    TRACE("iface %p, desc %p.\n", iface, desc);

    if (!desc)
    {
        WARN("Invalid pointer.\n");
        return E_INVALIDARG;
    }

    *desc = swapchain->desc;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d3d12_swapchain_GetFullscreenDesc(IDXGISwapChain3 *iface,
        DXGI_SWAP_CHAIN_FULLSCREEN_DESC *desc)
{
    struct d3d12_swapchain *swapchain = d3d12_swapchain_from_IDXGISwapChain3(iface);

    TRACE("iface %p, desc %p.\n", iface, desc);

    if (!desc)
    {
        WARN("Invalid pointer.\n");
        return E_INVALIDARG;
    }

    *desc = swapchain->fullscreen_desc;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d3d12_swapchain_GetHwnd(IDXGISwapChain3 *iface, HWND *hwnd)
{
    struct d3d12_swapchain *swapchain = d3d12_swapchain_from_IDXGISwapChain3(iface);

    TRACE("iface %p, hwnd %p.\n", iface, hwnd);

    if (!hwnd)
    {
        WARN("Invalid pointer.\n");
        return DXGI_ERROR_INVALID_CALL;
    }

    *hwnd = swapchain->window;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d3d12_swapchain_GetCoreWindow(IDXGISwapChain3 *iface,
        REFIID iid, void **core_window)
{
    FIXME("iface %p, iid %s, core_window %p stub!\n", iface, debugstr_guid(iid), core_window);

    if (core_window)
        *core_window = NULL;

    return DXGI_ERROR_INVALID_CALL;
}

static HRESULT STDMETHODCALLTYPE d3d12_swapchain_Present1(IDXGISwapChain3 *iface,
        UINT sync_interval, UINT flags, const DXGI_PRESENT_PARAMETERS *present_parameters)
{
    struct d3d12_swapchain *swapchain = d3d12_swapchain_from_IDXGISwapChain3(iface);

    TRACE("iface %p, sync_interval %u, flags %#x, present_parameters %p.\n",
            iface, sync_interval, flags, present_parameters);

    if (present_parameters)
        FIXME("Ignored present parameters %p.\n", present_parameters);

    return d3d12_swapchain_present(swapchain, sync_interval, flags);
}

static BOOL STDMETHODCALLTYPE d3d12_swapchain_IsTemporaryMonoSupported(IDXGISwapChain3 *iface)
{
    FIXME("iface %p stub!\n", iface);

    return FALSE;
}

static HRESULT STDMETHODCALLTYPE d3d12_swapchain_GetRestrictToOutput(IDXGISwapChain3 *iface, IDXGIOutput **output)
{
    FIXME("iface %p, output %p stub!\n", iface, output);

    if (!output)
    {
        WARN("Invalid pointer.\n");
        return E_INVALIDARG;
    }

    *output = NULL;
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d12_swapchain_SetBackgroundColor(IDXGISwapChain3 *iface, const DXGI_RGBA *color)
{
    FIXME("iface %p, color %p stub!\n", iface, color);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d12_swapchain_GetBackgroundColor(IDXGISwapChain3 *iface, DXGI_RGBA *color)
{
    FIXME("iface %p, color %p stub!\n", iface, color);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d12_swapchain_SetRotation(IDXGISwapChain3 *iface, DXGI_MODE_ROTATION rotation)
{
    FIXME("iface %p, rotation %#x stub!\n", iface, rotation);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d12_swapchain_GetRotation(IDXGISwapChain3 *iface, DXGI_MODE_ROTATION *rotation)
{
    FIXME("iface %p, rotation %p stub!\n", iface, rotation);

    return E_NOTIMPL;
}

/* IDXGISwapChain2 methods */

static HRESULT STDMETHODCALLTYPE d3d12_swapchain_SetSourceSize(IDXGISwapChain3 *iface, UINT width, UINT height)
{
    FIXME("iface %p, width %u, height %u stub!\n", iface, width, height);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d12_swapchain_GetSourceSize(IDXGISwapChain3 *iface, UINT *width, UINT *height)
{
    FIXME("iface %p, width %p, height %p stub!\n", iface, width, height);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d12_swapchain_SetMaximumFrameLatency(IDXGISwapChain3 *iface, UINT max_latency)
{
    struct d3d12_swapchain *swapchain = d3d12_swapchain_from_IDXGISwapChain3(iface);

    TRACE("iface %p, max_latency %u.\n", iface, max_latency);

    if (!(swapchain->desc.Flags & DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT))
    {
        WARN("DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT not set for swap chain %p.\n", iface);
        return DXGI_ERROR_INVALID_CALL;
    }

    if (!max_latency)
    {
        WARN("Invalid maximum frame latency %u.\n", max_latency);
        return DXGI_ERROR_INVALID_CALL;
    }

    swapchain->frame_latency = max_latency;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d3d12_swapchain_GetMaximumFrameLatency(IDXGISwapChain3 *iface, UINT *max_latency)
{
    struct d3d12_swapchain *swapchain = d3d12_swapchain_from_IDXGISwapChain3(iface);

    TRACE("iface %p, max_latency %p.\n", iface, max_latency);

    if (!(swapchain->desc.Flags & DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT))
    {
        WARN("DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT not set for swap chain %p.\n", iface);
        return DXGI_ERROR_INVALID_CALL;
    }

    *max_latency = swapchain->frame_latency;
    return S_OK;
}

static HANDLE STDMETHODCALLTYPE d3d12_swapchain_GetFrameLatencyWaitableObject(IDXGISwapChain3 *iface)
{
    struct d3d12_swapchain *swapchain = d3d12_swapchain_from_IDXGISwapChain3(iface);

    TRACE("iface %p.\n", iface);

    return swapchain->frame_latency_event;
}

static HRESULT STDMETHODCALLTYPE d3d12_swapchain_SetMatrixTransform(IDXGISwapChain3 *iface,
        const DXGI_MATRIX_3X2_F *matrix)
{
    FIXME("iface %p, matrix %p stub!\n", iface, matrix);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d12_swapchain_GetMatrixTransform(IDXGISwapChain3 *iface,
        DXGI_MATRIX_3X2_F *matrix)
{
    FIXME("iface %p, matrix %p stub!\n", iface, matrix);

    return E_NOTIMPL;
}

/* IDXGISwapChain3 methods */

static UINT STDMETHODCALLTYPE d3d12_swapchain_GetCurrentBackBufferIndex(IDXGISwapChain3 *iface)
{
    struct d3d12_swapchain *swapchain = d3d12_swapchain_from_IDXGISwapChain3(iface);

    TRACE("iface %p.\n", iface);

    TRACE("Current back buffer index %u.\n", swapchain->current_buffer_index);
    assert(swapchain->current_buffer_index < swapchain->desc.BufferCount);
    return swapchain->current_buffer_index;
}

static HRESULT STDMETHODCALLTYPE d3d12_swapchain_CheckColorSpaceSupport(IDXGISwapChain3 *iface,
        DXGI_COLOR_SPACE_TYPE colour_space, UINT *colour_space_support)
{
    UINT support_flags = 0;

    FIXME("iface %p, colour_space %#x, colour_space_support %p semi-stub!\n",
            iface, colour_space, colour_space_support);

    if (!colour_space_support)
        return E_INVALIDARG;

    if (colour_space == DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709)
      support_flags |= DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT;

    *colour_space_support = support_flags;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d3d12_swapchain_SetColorSpace1(IDXGISwapChain3 *iface,
        DXGI_COLOR_SPACE_TYPE colour_space)
{
    FIXME("iface %p, colour_space %#x semi-stub!\n", iface, colour_space);

    if (colour_space != DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709)
    {
        WARN("Colour space %u not supported.\n", colour_space);
        return E_INVALIDARG;
    }

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d3d12_swapchain_ResizeBuffers1(IDXGISwapChain3 *iface,
        UINT buffer_count, UINT width, UINT height, DXGI_FORMAT format, UINT flags,
        const UINT *node_mask, IUnknown * const *present_queue)
{
    struct d3d12_swapchain *swapchain = d3d12_swapchain_from_IDXGISwapChain3(iface);
    size_t i, count;

    TRACE("iface %p, buffer_count %u, width %u, height %u, format %s, flags %#x, "
            "node_mask %p, present_queue %p.\n",
            iface, buffer_count, width, height, debug_dxgi_format(format), flags, node_mask, present_queue);

    if (!node_mask || !present_queue)
        return DXGI_ERROR_INVALID_CALL;

    count = buffer_count ? buffer_count : swapchain->desc.BufferCount;
    for (i = 0; i < count; ++i)
    {
        if (node_mask[i] > 1 || !present_queue[i])
            return DXGI_ERROR_INVALID_CALL;
        if ((ID3D12CommandQueue*)present_queue[i] != d3d12_swapchain_queue_iface(swapchain))
            FIXME("Ignoring present queue %p.\n", present_queue[i]);
    }

    return d3d12_swapchain_resize_buffers(swapchain, buffer_count, width, height, format, flags);
}

static CONST_VTBL struct IDXGISwapChain3Vtbl d3d12_swapchain_vtbl =
{
    /* IUnknown methods */
    d3d12_swapchain_QueryInterface,
    d3d12_swapchain_AddRef,
    d3d12_swapchain_Release,
    /* IDXGIObject methods */
    d3d12_swapchain_SetPrivateData,
    d3d12_swapchain_SetPrivateDataInterface,
    d3d12_swapchain_GetPrivateData,
    d3d12_swapchain_GetParent,
    /* IDXGIDeviceSubObject methods */
    d3d12_swapchain_GetDevice,
    /* IDXGISwapChain methods */
    d3d12_swapchain_Present,
    d3d12_swapchain_GetBuffer,
    d3d12_swapchain_SetFullscreenState,
    d3d12_swapchain_GetFullscreenState,
    d3d12_swapchain_GetDesc,
    d3d12_swapchain_ResizeBuffers,
    d3d12_swapchain_ResizeTarget,
    d3d12_swapchain_GetContainingOutput,
    d3d12_swapchain_GetFrameStatistics,
    d3d12_swapchain_GetLastPresentCount,
    /* IDXGISwapChain1 methods */
    d3d12_swapchain_GetDesc1,
    d3d12_swapchain_GetFullscreenDesc,
    d3d12_swapchain_GetHwnd,
    d3d12_swapchain_GetCoreWindow,
    d3d12_swapchain_Present1,
    d3d12_swapchain_IsTemporaryMonoSupported,
    d3d12_swapchain_GetRestrictToOutput,
    d3d12_swapchain_SetBackgroundColor,
    d3d12_swapchain_GetBackgroundColor,
    d3d12_swapchain_SetRotation,
    d3d12_swapchain_GetRotation,
    /* IDXGISwapChain2 methods */
    d3d12_swapchain_SetSourceSize,
    d3d12_swapchain_GetSourceSize,
    d3d12_swapchain_SetMaximumFrameLatency,
    d3d12_swapchain_GetMaximumFrameLatency,
    d3d12_swapchain_GetFrameLatencyWaitableObject,
    d3d12_swapchain_SetMatrixTransform,
    d3d12_swapchain_GetMatrixTransform,
    /* IDXGISwapChain3 methods */
    d3d12_swapchain_GetCurrentBackBufferIndex,
    d3d12_swapchain_CheckColorSpaceSupport,
    d3d12_swapchain_SetColorSpace1,
    d3d12_swapchain_ResizeBuffers1,
};

static HRESULT d3d12_swapchain_init(struct d3d12_swapchain *swapchain, IDXGIFactory *factory,
        struct d3d12_command_queue *queue, HWND window,
        const DXGI_SWAP_CHAIN_DESC1 *swapchain_desc, const DXGI_SWAP_CHAIN_FULLSCREEN_DESC *fullscreen_desc)
{
    const struct vkd3d_vk_device_procs *vk_procs = &queue->device->vk_procs;
    VkWin32SurfaceCreateInfoKHR surface_desc;
    VkPhysicalDevice vk_physical_device;
    VkFenceCreateInfo fence_desc;
    uint32_t queue_family_index;
    VkSurfaceKHR vk_surface;
    VkInstance vk_instance;
    IDXGIAdapter *adapter;
    IDXGIOutput *output;
    VkBool32 supported;
    VkDevice vk_device;
    VkFence vk_fence;
    VkResult vr;
    HRESULT hr;

    if (window == GetDesktopWindow())
    {
        WARN("D3D12 swapchain cannot be created on desktop window.\n");
        return E_ACCESSDENIED;
    }

    swapchain->IDXGISwapChain3_iface.lpVtbl = &d3d12_swapchain_vtbl;
    swapchain->refcount = 1;

    swapchain->window = window;
    swapchain->desc = *swapchain_desc;
    swapchain->fullscreen_desc = *fullscreen_desc;

    swapchain->present_mode = VK_PRESENT_MODE_FIFO_KHR;

    switch (swapchain_desc->SwapEffect)
    {
        case DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL:
        case DXGI_SWAP_EFFECT_FLIP_DISCARD:
            FIXME("Ignoring swap effect %#x.\n", swapchain_desc->SwapEffect);
            break;
        default:
            WARN("Invalid swap effect %#x.\n", swapchain_desc->SwapEffect);
            return DXGI_ERROR_INVALID_CALL;
    }

    if (FAILED(hr = IUnknown_QueryInterface(queue->device->parent, &IID_IDXGIAdapter, (void **)&adapter)))
        return hr;

    if (FAILED(hr = d3d12_get_output_from_window((IDXGIFactory *)factory, window, &output)))
    {
        WARN("Failed to get output from window %p, hr %#x.\n", window, hr);
    }
    IDXGIAdapter_Release(adapter);
    IDXGIOutput_Release(output);
    if (FAILED(hr))
        return hr;

    if (swapchain_desc->BufferUsage && swapchain_desc->BufferUsage != DXGI_USAGE_RENDER_TARGET_OUTPUT)
        FIXME("Ignoring buffer usage %#x.\n", swapchain_desc->BufferUsage);
    if (swapchain_desc->Scaling != DXGI_SCALING_STRETCH && swapchain_desc->Scaling != DXGI_SCALING_NONE)
        FIXME("Ignoring scaling %#x.\n", swapchain_desc->Scaling);
    if (swapchain_desc->AlphaMode && swapchain_desc->AlphaMode != DXGI_ALPHA_MODE_IGNORE)
        FIXME("Ignoring alpha mode %#x.\n", swapchain_desc->AlphaMode);
    if (swapchain_desc->Flags & ~(DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT))
        FIXME("Ignoring swapchain flags %#x.\n", swapchain_desc->Flags);

    if (fullscreen_desc->RefreshRate.Numerator || fullscreen_desc->RefreshRate.Denominator)
        FIXME("Ignoring refresh rate.\n");
    if (fullscreen_desc->ScanlineOrdering)
        FIXME("Unhandled scanline ordering %#x.\n", fullscreen_desc->ScanlineOrdering);
    if (fullscreen_desc->Scaling)
        FIXME("Unhandled mode scaling %#x.\n", fullscreen_desc->Scaling);
    if (!fullscreen_desc->Windowed)
        FIXME("Fullscreen not supported yet.\n");

    vk_instance = queue->device->vkd3d_instance->vk_instance;
    vk_physical_device = queue->device->vk_physical_device;
    vk_device = queue->device->vk_device;

    vkd3d_private_store_init(&swapchain->private_store);

    surface_desc.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
    surface_desc.pNext = NULL;
    surface_desc.flags = 0;
    surface_desc.hinstance = GetModuleHandleA("d3d12.dll");
    surface_desc.hwnd = window;
    if ((vr = vk_procs->vkCreateWin32SurfaceKHR(vk_instance, &surface_desc, NULL, &vk_surface)) < 0)
    {
        WARN("Failed to create Vulkan surface, vr %d.\n", vr);
        d3d12_swapchain_destroy(swapchain);
        return hresult_from_vk_result(vr);
    }
    swapchain->vk_surface = vk_surface;

    queue_family_index = queue->vkd3d_queue->vk_family_index;
    if ((vr = vk_procs->vkGetPhysicalDeviceSurfaceSupportKHR(vk_physical_device,
            queue_family_index, vk_surface, &supported)) < 0 || !supported)
    {
        FIXME("Queue family does not support presentation, vr %d.\n", vr);
        d3d12_swapchain_destroy(swapchain);
        return DXGI_ERROR_UNSUPPORTED;
    }

    swapchain->command_queue = queue;
    ID3D12CommandQueue_AddRef(&queue->ID3D12CommandQueue_iface);
    d3d12_device_add_ref(queue->device);

    if (FAILED(hr = d3d12_swapchain_create_vulkan_swapchain(swapchain)))
    {
        d3d12_swapchain_destroy(swapchain);
        return hr;
    }

    fence_desc.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fence_desc.pNext = NULL;
    fence_desc.flags = 0;
    if ((vr = vk_procs->vkCreateFence(vk_device, &fence_desc, NULL, &vk_fence)) < 0)
    {
        WARN("Failed to create Vulkan fence, vr %d.\n", vr);
        d3d12_swapchain_destroy(swapchain);
        return hresult_from_vk_result(vr);
    }
    swapchain->vk_fence = vk_fence;

    swapchain->current_buffer_index = 0;
    if ((vr = d3d12_swapchain_acquire_next_back_buffer(swapchain)) < 0)
    {
        d3d12_swapchain_destroy(swapchain);
        return hresult_from_vk_result(vr);
    }

    if (swapchain_desc->Flags & DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT)
    {
        swapchain->frame_number = DXGI_MAX_SWAP_CHAIN_BUFFERS;
        swapchain->frame_latency = 1;

        if (FAILED(hr = ID3D12Device6_CreateFence(d3d12_swapchain_device_iface(swapchain), DXGI_MAX_SWAP_CHAIN_BUFFERS,
                0, &IID_ID3D12Fence, (void **)&swapchain->frame_latency_fence)))
        {
            WARN("Failed to create frame latency fence, hr %#x.\n", hr);
            d3d12_swapchain_destroy(swapchain);
            return hr;
        }

        if (!(swapchain->frame_latency_event = CreateEventW(NULL, FALSE, TRUE, NULL)))
        {
            hr = HRESULT_FROM_WIN32(GetLastError());
            WARN("Failed to create frame latency event, hr %#x.\n", hr);
            d3d12_swapchain_destroy(swapchain);
            return hr;
        }
    }

    IDXGIFactory_AddRef(swapchain->factory = factory);

    return S_OK;
}

static HRESULT d3d12_swapchain_create(IDXGIFactory *factory, struct d3d12_command_queue *queue, HWND window,
        const DXGI_SWAP_CHAIN_DESC1 *swapchain_desc, const DXGI_SWAP_CHAIN_FULLSCREEN_DESC *fullscreen_desc,
        IDXGISwapChain1 **swapchain)
{
    DXGI_SWAP_CHAIN_FULLSCREEN_DESC default_fullscreen_desc;
    struct d3d12_swapchain *object;
    HRESULT hr;

    if (swapchain_desc->Format == DXGI_FORMAT_UNKNOWN)
        return DXGI_ERROR_INVALID_CALL;

    if (!fullscreen_desc)
    {
        memset(&default_fullscreen_desc, 0, sizeof(default_fullscreen_desc));
        default_fullscreen_desc.Windowed = TRUE;
        fullscreen_desc = &default_fullscreen_desc;
    }

    if (!(object = vkd3d_calloc(1, sizeof(*object))))
        return E_OUTOFMEMORY;

    hr = d3d12_swapchain_init(object, factory, queue, window, swapchain_desc, fullscreen_desc);

    if (FAILED(hr))
    {
        vkd3d_free(object);
        return hr;
    }

    TRACE("Created swapchain %p.\n", object);

    *swapchain = (IDXGISwapChain1 *)&object->IDXGISwapChain3_iface;

    return S_OK;
}

static inline struct d3d12_swapchain_factory *d3d12_swapchain_factory_from_IWineDXGISwapChainFactory(IWineDXGISwapChainFactory *iface)
{
    return CONTAINING_RECORD(iface, struct d3d12_swapchain_factory, IWineDXGISwapChainFactory_iface);
}

static ULONG STDMETHODCALLTYPE d3d12_swapchain_factory_AddRef(IWineDXGISwapChainFactory *iface)
{
    struct d3d12_swapchain_factory *swapchain_factory = d3d12_swapchain_factory_from_IWineDXGISwapChainFactory(iface);
    return ID3D12CommandQueue_AddRef(&swapchain_factory->queue->ID3D12CommandQueue_iface);
}

static ULONG STDMETHODCALLTYPE d3d12_swapchain_factory_Release(IWineDXGISwapChainFactory *iface)
{
    struct d3d12_swapchain_factory *swapchain_factory = d3d12_swapchain_factory_from_IWineDXGISwapChainFactory(iface);
    return ID3D12CommandQueue_Release(&swapchain_factory->queue->ID3D12CommandQueue_iface);
}

static HRESULT STDMETHODCALLTYPE d3d12_swapchain_factory_QueryInterface(IWineDXGISwapChainFactory *iface,
        REFIID iid, void **out)
{
    TRACE("iface %p, iid %s, out %p.\n", iface, debugstr_guid(iid), out);

    if (IsEqualGUID(iid, &IID_IWineDXGISwapChainFactory) ||
        IsEqualGUID(iid, &IID_IUnknown))
    {
        d3d12_swapchain_factory_AddRef(iface);
        *out = iface;
        return S_OK;
    }

    WARN("%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid(iid));

    *out = NULL;
    return E_NOINTERFACE;
}

static HRESULT STDMETHODCALLTYPE d3d12_swapchain_factory_CreateSwapChainForHwnd(IWineDXGISwapChainFactory *iface,
        IDXGIFactory* factory, HWND window, const DXGI_SWAP_CHAIN_DESC1* swapchain_desc,
        const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* fullscreen_desc, IDXGIOutput* output,
        IDXGISwapChain1** swapchain)
{
    struct d3d12_swapchain_factory *swapchain_factory = d3d12_swapchain_factory_from_IWineDXGISwapChainFactory(iface);
    HRESULT hr;

    TRACE("iface %p, factory %p, window %p, swapchain_desc %p, fullscreen_desc %p, output %p, swapchain %p\n",
        iface, factory, window, swapchain_desc, fullscreen_desc, output, swapchain);

    hr = d3d12_swapchain_create(factory, swapchain_factory->queue, window, swapchain_desc, fullscreen_desc, swapchain);

    return hr;
}

static CONST_VTBL struct IWineDXGISwapChainFactoryVtbl d3d12_swapchain_factory_vtbl =
{
    /* IUnknown methods */
    d3d12_swapchain_factory_QueryInterface,
    d3d12_swapchain_factory_AddRef,
    d3d12_swapchain_factory_Release,

    /* IWineDXGISwapChainFactory methods */
    d3d12_swapchain_factory_CreateSwapChainForHwnd
};

HRESULT d3d12_swapchain_factory_init(struct d3d12_command_queue *queue, struct d3d12_swapchain_factory *factory)
{
    factory->IWineDXGISwapChainFactory_iface.lpVtbl = &d3d12_swapchain_factory_vtbl;
    factory->queue = queue;

    return S_OK;
}
