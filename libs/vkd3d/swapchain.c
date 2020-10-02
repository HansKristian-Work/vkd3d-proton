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

#define VKD3D_DBG_CHANNEL VKD3D_DBG_CHANNEL_API

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

struct d3d12_swapchain_state
{
    struct DXGI_MODE_DESC original_mode, d3d_mode;
    RECT original_window_rect;

    /* Window styles to restore when switching fullscreen mode. */
    LONG style;
    LONG exstyle;
    HWND device_window;
};

typedef IDXGISwapChain4 dxgi_swapchain_iface;

struct d3d12_swapchain
{
    dxgi_swapchain_iface IDXGISwapChain_iface;
    LONG refcount;
    struct vkd3d_private_store private_store;

    /* Use critical section rather than pthread_mutex_t wrapper here since we need it
     * to be re-entrant, and this code is Win32-only. */
    CRITICAL_SECTION mutex;

    VkSwapchainKHR vk_swapchain;
    VkSurfaceKHR vk_surface;
    VkFence vk_fence;
    VkCommandPool vk_cmd_pool;
    VkImage vk_images[DXGI_MAX_SWAP_CHAIN_BUFFERS];
    VkImage vk_swapchain_images[DXGI_MAX_SWAP_CHAIN_BUFFERS];
    VkImageView vk_swapchain_image_views[DXGI_MAX_SWAP_CHAIN_BUFFERS];
    VkFramebuffer vk_framebuffers[DXGI_MAX_SWAP_CHAIN_BUFFERS];
    VkCommandBuffer vk_cmd_buffers[DXGI_MAX_SWAP_CHAIN_BUFFERS];
    VkSemaphore vk_semaphores[DXGI_MAX_SWAP_CHAIN_BUFFERS];
    ID3D12Resource *buffers[DXGI_MAX_SWAP_CHAIN_BUFFERS];
    unsigned int buffer_count;
    unsigned int vk_swapchain_width;
    unsigned int vk_swapchain_height;
    VkPresentModeKHR present_mode;

    struct
    {
        VkDescriptorPool pool;
        VkDescriptorSet sets[DXGI_MAX_SWAP_CHAIN_BUFFERS];
        VkImageView vk_image_views[DXGI_MAX_SWAP_CHAIN_BUFFERS];
    } descriptors;

    struct vkd3d_swapchain_info pipeline;

    uint32_t vk_image_index;
    unsigned int current_buffer_index;

    struct d3d12_command_queue *command_queue;
    IDXGIFactory *factory;

    HWND window;
    IDXGIOutput *target;
    DXGI_SWAP_CHAIN_DESC1 desc;
    DXGI_SWAP_CHAIN_FULLSCREEN_DESC fullscreen_desc;
    struct d3d12_swapchain_state state;

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

DXGI_FORMAT format_for_depth(DWORD depth)
{
    switch (depth)
    {
        case 8:  return DXGI_FORMAT_P8;
        case 16: return DXGI_FORMAT_B5G6R5_UNORM;
        case 24: return DXGI_FORMAT_B8G8R8X8_UNORM;
        case 32: return DXGI_FORMAT_B8G8R8X8_UNORM;
        default: return DXGI_FORMAT_UNKNOWN;
    }
}

DWORD depth_for_format(DXGI_FORMAT format)
{
    switch (format)
    {
        case DXGI_FORMAT_B4G4R4A4_UNORM:
        case DXGI_FORMAT_B5G6R5_UNORM:
            return 16;

        default:
            WARN("Unknown format depth. Returning 32.");
            return 32;

        case DXGI_FORMAT_R8G8B8A8_UNORM:
        case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
        case DXGI_FORMAT_B8G8R8A8_UNORM:
        case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
        case DXGI_FORMAT_B8G8R8X8_UNORM:
        case DXGI_FORMAT_R10G10B10A2_UNORM:
        case DXGI_FORMAT_R10G10B10_XR_BIAS_A2_UNORM:
            return 32;

        case DXGI_FORMAT_R16G16B16A16_FLOAT:
            return 64;

    }
}

static HRESULT d3d12_output_set_display_mode(IDXGIOutput *output, DXGI_MODE_DESC *mode)
{
    DXGI_OUTPUT_DESC desc;
    DEVMODEW new_mode, current_mode;
    LONG ret;

    TRACE("output %p, mode %p.\n", output, mode);

    IDXGIOutput_GetDesc(output, &desc);

    memset(&new_mode, 0, sizeof(new_mode));
    new_mode.dmSize = sizeof(new_mode);
    memset(&current_mode, 0, sizeof(current_mode));
    current_mode.dmSize = sizeof(current_mode);
    if (mode)
    {
        new_mode.dmFields = DM_BITSPERPEL | DM_PELSWIDTH | DM_PELSHEIGHT;
        new_mode.dmBitsPerPel = depth_for_format(mode->Format);
        new_mode.dmPelsWidth = mode->Width;
        new_mode.dmPelsHeight = mode->Height;

        new_mode.dmDisplayFrequency = 0;
        if (mode->RefreshRate.Numerator && mode->RefreshRate.Denominator)
        {
            new_mode.dmDisplayFrequency = mode->RefreshRate.Numerator / mode->RefreshRate.Denominator;
            new_mode.dmFields |= DM_DISPLAYFREQUENCY;
        }

        if (mode->ScanlineOrdering != DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED)
        {
            new_mode.dmFields |= DM_DISPLAYFLAGS;
            if (mode->ScanlineOrdering == DXGI_MODE_SCANLINE_ORDER_UPPER_FIELD_FIRST)
                new_mode.dmDisplayFlags |= DM_INTERLACED;
            else if (mode->ScanlineOrdering == DXGI_MODE_SCANLINE_ORDER_LOWER_FIELD_FIRST)
            {
                ERR("Interlacing mode not supported.");
                return DXGI_ERROR_NOT_CURRENTLY_AVAILABLE;
            }
                
        }
    }
    else
    {
        if (!EnumDisplaySettingsW(desc.DeviceName, ENUM_REGISTRY_SETTINGS, &new_mode))
        {
            ERR("Failed to read mode from registry.\n");
            return DXGI_ERROR_NOT_CURRENTLY_AVAILABLE;
        }
    }

    /* Only change the mode if necessary. */
    if (!EnumDisplaySettingsW(desc.DeviceName, ENUM_CURRENT_SETTINGS, &current_mode))
    {
        ERR("Failed to get current display mode.\n");
    }
    else if (current_mode.dmPelsWidth == new_mode.dmPelsWidth
            && current_mode.dmPelsHeight == new_mode.dmPelsHeight
            && current_mode.dmBitsPerPel == new_mode.dmBitsPerPel
            && (current_mode.dmDisplayFrequency == new_mode.dmDisplayFrequency
            || !(new_mode.dmFields & DM_DISPLAYFREQUENCY))
            && (current_mode.dmDisplayFlags == new_mode.dmDisplayFlags
            || !(new_mode.dmFields & DM_DISPLAYFLAGS)))
    {
        TRACE("Skipping redundant mode setting call.\n");
        return S_OK;
    }

    ret = ChangeDisplaySettingsExW(desc.DeviceName, &new_mode, NULL, CDS_FULLSCREEN, NULL);
    if (ret != DISP_CHANGE_SUCCESSFUL)
    {
        if (new_mode.dmFields & DM_DISPLAYFREQUENCY)
        {
            WARN("ChangeDisplaySettingsExW failed, trying without the refresh rate.\n");
            new_mode.dmFields &= ~DM_DISPLAYFREQUENCY;
            new_mode.dmDisplayFrequency = 0;
            ret = ChangeDisplaySettingsExW(desc.DeviceName, &new_mode, NULL, CDS_FULLSCREEN, NULL);
        }
        if (ret != DISP_CHANGE_SUCCESSFUL)
            return DXGI_ERROR_NOT_CURRENTLY_AVAILABLE;
    }

    return S_OK;
}

static HRESULT d3d12_output_get_display_mode(IDXGIOutput* output, DXGI_MODE_DESC* mode)
{
    DXGI_OUTPUT_DESC desc;
    DEVMODEW m;

    IDXGIOutput_GetDesc(output, &desc);

    memset(&m, 0, sizeof(m));
    m.dmSize = sizeof(m);

    EnumDisplaySettingsExW(desc.DeviceName, ENUM_CURRENT_SETTINGS, &m, 0);
    mode->Width = m.dmPelsWidth;
    mode->Height = m.dmPelsHeight;
    mode->RefreshRate.Numerator = 60000;
    mode->RefreshRate.Denominator = 1000;
    if (m.dmFields & DM_DISPLAYFREQUENCY)
        mode->RefreshRate.Numerator = m.dmDisplayFrequency * 1000;
    mode->Format = format_for_depth(m.dmBitsPerPel);

    if (!(m.dmFields & DM_DISPLAYFLAGS))
        mode->ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
    else if (m.dmDisplayFlags & DM_INTERLACED)
        mode->ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UPPER_FIELD_FIRST;
    else
        mode->ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_PROGRESSIVE;

    return S_OK;
}


static HRESULT d3d12_swapchain_set_display_mode(struct d3d12_swapchain *swapchain,
        IDXGIOutput* output, DXGI_MODE_DESC *mode)
{
    DXGI_MODE_DESC matching_mode;
    HRESULT hr;

    /* Workaround. FindClosestMatchingMode can take UNKNOWN format, but neither Wine nor DXVK DXGI seems to like it.
     * Just opt for existing format to workaround the issue. */
    if (mode->Format == DXGI_FORMAT_UNKNOWN)
        mode->Format = swapchain->desc.Format;

    if (FAILED(hr = IDXGIOutput_FindClosestMatchingMode(output, mode, &matching_mode, NULL)))
    {
        WARN("Failed to find closest matching mode, hr %#x.\n", hr);
        return hr;
    }

    if (output != swapchain->target && swapchain->target)
    {
        if (FAILED(hr = d3d12_output_set_display_mode(swapchain->target, &swapchain->state.original_mode)))
        {
            WARN("Failed to set display mode, hr %#x.\n", hr);
            return hr;
        }

        if (FAILED(hr = d3d12_output_get_display_mode(output, &swapchain->state.original_mode)))
        {
            WARN("Failed to get current display mode, hr %#x.\n", hr);
            return hr;
        }
    }

    if (FAILED(hr = d3d12_output_set_display_mode(output, &matching_mode)))
    {
        WARN("Failed to set display mode, hr %#x.\n", hr);
        return DXGI_ERROR_INVALID_CALL;
    }

    return S_OK;
}

static LONG fullscreen_style(LONG style)
{
    /* Make sure the window is managed, otherwise we won't get keyboard input. */
    style |= WS_POPUP | WS_SYSMENU;
    style &= ~(WS_CAPTION | WS_THICKFRAME);

    return style;
}

static LONG fullscreen_exstyle(LONG exstyle)
{
    /* Filter out window decorations. */
    exstyle &= ~(WS_EX_WINDOWEDGE | WS_EX_CLIENTEDGE);

    return exstyle;
}

void d3d12_swapchain_state_restore_from_fullscreen(struct d3d12_swapchain *swapchain,
        HWND window, const RECT *window_rect)
{
    unsigned int window_pos_flags = SWP_FRAMECHANGED | SWP_NOZORDER | SWP_NOACTIVATE;
    LONG style, exstyle;
    RECT rect = {0};

    if (!swapchain->state.style && !swapchain->state.exstyle)
        return;

    style = GetWindowLongW(window, GWL_STYLE);
    exstyle = GetWindowLongW(window, GWL_EXSTYLE);

    swapchain->state.style ^= (swapchain->state.style ^ style) & WS_VISIBLE;
    swapchain->state.exstyle ^= (swapchain->state.exstyle ^ exstyle) & WS_EX_TOPMOST;

    TRACE("Restoring window style of window %p to %08x, %08x.\n",
            window, swapchain->state.style, swapchain->state.exstyle);

    if (style == fullscreen_style(swapchain->state.style) &&
        exstyle == fullscreen_exstyle(swapchain->state.exstyle))
    {
        SetWindowLongW(window, GWL_STYLE, swapchain->state.style);
        SetWindowLongW(window, GWL_EXSTYLE, swapchain->state.exstyle);
    }

    if (window_rect)
        rect = *window_rect;
    else
        window_pos_flags |= (SWP_NOMOVE | SWP_NOSIZE);
    SetWindowPos(window, 0, rect.left, rect.top,
            rect.right - rect.left, rect.bottom - rect.top, window_pos_flags);

    /* Delete the old values. */
    swapchain->state.style = 0;
    swapchain->state.exstyle = 0;
}

HRESULT d3d12_swapchain_state_setup_fullscreen(struct d3d12_swapchain* swapchain,
    HWND window, int x, int y, int width, int height)
{
    LONG style, exstyle;

    TRACE("Setting up window %p for fullscreen mode.\n", window);

    if (!IsWindow(window))
    {
        WARN("%p is not a valid window.\n", window);
        return DXGI_ERROR_NOT_CURRENTLY_AVAILABLE;;
    }

    if (swapchain->state.style || swapchain->state.exstyle)
    {
        ERR("Changing the window style for window %p, but another style (%08x, %08x) is already stored.\n",
            window, swapchain->state.style, swapchain->state.exstyle);
    }

    swapchain->state.style = GetWindowLongW(window, GWL_STYLE);
    swapchain->state.exstyle = GetWindowLongW(window, GWL_EXSTYLE);

    style = fullscreen_style(swapchain->state.style);
    exstyle = fullscreen_exstyle(swapchain->state.exstyle);

    TRACE("Old style was %08x, %08x, setting to %08x, %08x.\n",
        swapchain->state.style, swapchain->state.exstyle, style, exstyle);

    SetWindowLongW(window, GWL_STYLE, style);
    SetWindowLongW(window, GWL_EXSTYLE, exstyle);
    SetWindowPos(window, HWND_TOPMOST, x, y, width, height,
        SWP_FRAMECHANGED | SWP_SHOWWINDOW | SWP_NOACTIVATE);

    return S_OK;
}

static HRESULT d3d12_swapchain_set_fullscreen(struct d3d12_swapchain *swapchain, IDXGIOutput *target, BOOL originally_windowed)
{
    DXGI_MODE_DESC actual_mode;
    DXGI_OUTPUT_DESC output_desc;
    HRESULT hr;

    if (swapchain->desc.Flags & DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH)
    {
        if (!swapchain->fullscreen_desc.Windowed)
        {
            actual_mode.Width = swapchain->desc.Width;
            actual_mode.Height = swapchain->desc.Height;
            actual_mode.RefreshRate = swapchain->fullscreen_desc.RefreshRate;
            actual_mode.Format = swapchain->desc.Format;
            actual_mode.ScanlineOrdering = swapchain->fullscreen_desc.ScanlineOrdering;
            actual_mode.Scaling = swapchain->fullscreen_desc.Scaling;
        }
        else
        {
            actual_mode = swapchain->state.original_mode;
        }

        if (FAILED(hr = d3d12_swapchain_set_display_mode(swapchain, target,
                &actual_mode)))
            return hr;
    }
    else
    {
        if (FAILED(hr = d3d12_output_get_display_mode(target, &actual_mode)))
        {
            ERR("Failed to get display mode, hr %#x.\n", hr);
            return DXGI_ERROR_INVALID_CALL;
        }
    }

    if (!swapchain->fullscreen_desc.Windowed)
    {
        unsigned int width = actual_mode.Width;
        unsigned int height = actual_mode.Height;

        if (FAILED(hr = IDXGIOutput_GetDesc(target, &output_desc)))
        {
            ERR("Failed to get output description, hr %#x.\n", hr);
            return hr;
        }

        if (originally_windowed)
        {
            /* Switch from windowed to fullscreen */
            if (FAILED(hr = d3d12_swapchain_state_setup_fullscreen(swapchain, swapchain->window,
                    output_desc.DesktopCoordinates.left, output_desc.DesktopCoordinates.top, width, height)))
                return hr;
        }
        else
        {
            HWND window = swapchain->window;

            /* Fullscreen -> fullscreen mode change */
            MoveWindow(window, output_desc.DesktopCoordinates.left, output_desc.DesktopCoordinates.top, width,
                    height, TRUE);
            ShowWindow(window, SW_SHOW);
        }
        swapchain->state.d3d_mode = actual_mode;
    }
    else if (!originally_windowed)
    {
        /* Fullscreen -> windowed switch */
        d3d12_swapchain_state_restore_from_fullscreen(swapchain, swapchain->window, &swapchain->state.original_window_rect);
    }

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

static HRESULT d3d12_swapchain_get_user_graphics_pipeline(struct d3d12_swapchain *swapchain, VkFormat format)
{
    struct d3d12_device *device = d3d12_swapchain_device(swapchain);
    struct vkd3d_swapchain_pipeline_key key;
    HRESULT hr;

    key.bind_point = VK_PIPELINE_BIND_POINT_GRAPHICS;
    key.load_op = swapchain->desc.Scaling == DXGI_SCALING_NONE ?
            VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    key.filter = swapchain->desc.Scaling == DXGI_SCALING_NONE ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;
    key.format = format;

    if (FAILED(hr = vkd3d_meta_get_swapchain_pipeline(&device->meta_ops, &key, &swapchain->pipeline)))
        return hr;

    return S_OK;
}

static void d3d12_swapchain_destroy_user_descriptors(struct d3d12_swapchain *swapchain)
{
    struct d3d12_device *device = d3d12_swapchain_device(swapchain);
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    UINT i;
    for (i = 0; i < swapchain->desc.BufferCount; i++)
    {
        VK_CALL(vkDestroyImageView(device->vk_device, swapchain->descriptors.vk_image_views[i], NULL));
        swapchain->descriptors.vk_image_views[i] = VK_NULL_HANDLE;
    }

    VK_CALL(vkDestroyDescriptorPool(device->vk_device, swapchain->descriptors.pool, NULL));
    swapchain->descriptors.pool = VK_NULL_HANDLE;
}

static HRESULT d3d12_swapchain_create_user_descriptors(struct d3d12_swapchain *swapchain, VkFormat vk_format)
{
    struct d3d12_device *device = d3d12_swapchain_device(swapchain);
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    VkDescriptorPoolCreateInfo pool_create_info;
    VkDescriptorSetAllocateInfo allocate_info;
    VkImageViewCreateInfo image_view_info;
    VkDescriptorImageInfo image_info;
    VkWriteDescriptorSet write_info;
    VkDescriptorPoolSize pool_sizes;
    VkResult vr;
    UINT i;

    image_view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    image_view_info.pNext = NULL;
    image_view_info.flags = 0;
    image_view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    image_view_info.format = vk_format;
    image_view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    image_view_info.subresourceRange.baseMipLevel = 0;
    image_view_info.subresourceRange.baseArrayLayer = 0;
    image_view_info.subresourceRange.levelCount = 1;
    image_view_info.subresourceRange.layerCount = 1;
    image_view_info.components.r = VK_COMPONENT_SWIZZLE_R;
    image_view_info.components.g = VK_COMPONENT_SWIZZLE_G;
    image_view_info.components.b = VK_COMPONENT_SWIZZLE_B;
    image_view_info.components.a = VK_COMPONENT_SWIZZLE_A;

    for (i = 0; i < swapchain->desc.BufferCount; i++)
    {
        image_view_info.image = swapchain->vk_images[i];
        if ((vr = VK_CALL(vkCreateImageView(device->vk_device, &image_view_info, NULL, &swapchain->descriptors.vk_image_views[i]))))
            return hresult_from_vk_result(vr);
    }

    pool_sizes.descriptorCount = swapchain->desc.BufferCount;
    pool_sizes.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;

    pool_create_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_create_info.pNext = NULL;
    pool_create_info.flags = 0;
    pool_create_info.poolSizeCount = 1;
    pool_create_info.pPoolSizes = &pool_sizes;
    pool_create_info.maxSets = swapchain->desc.BufferCount;
    if ((vr = VK_CALL(vkCreateDescriptorPool(device->vk_device, &pool_create_info, NULL, &swapchain->descriptors.pool))))
        return hresult_from_vk_result(vr);

    allocate_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocate_info.pNext = NULL;
    allocate_info.descriptorPool = swapchain->descriptors.pool;
    allocate_info.descriptorSetCount = 1;
    allocate_info.pSetLayouts = &swapchain->pipeline.vk_set_layout;

    for (i = 0; i < swapchain->desc.BufferCount; i++)
    {
        if ((vr = VK_CALL(vkAllocateDescriptorSets(device->vk_device, &allocate_info, &swapchain->descriptors.sets[i]))))
            return hresult_from_vk_result(vr);
    }

    write_info.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write_info.pNext = NULL;
    write_info.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write_info.pBufferInfo = NULL;
    write_info.pTexelBufferView = NULL;
    write_info.pImageInfo = &image_info;
    write_info.dstBinding = 0;
    write_info.dstArrayElement = 0;
    write_info.descriptorCount = 1;
    image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    image_info.sampler = VK_NULL_HANDLE;

    for (i = 0; i < swapchain->desc.BufferCount; i++)
    {
        write_info.dstSet = swapchain->descriptors.sets[i];
        image_info.imageView = swapchain->descriptors.vk_image_views[i];
        VK_CALL(vkUpdateDescriptorSets(device->vk_device, 1, &write_info, 0, NULL));
    }

    return S_OK;
}

static HRESULT d3d12_swapchain_create_user_buffers(struct d3d12_swapchain *swapchain, VkFormat vk_format)
{
    D3D12_HEAP_PROPERTIES heap_properties;
    D3D12_RESOURCE_DESC resource_desc;
    struct d3d12_resource* object;
    HRESULT hr;
    UINT i;

    if (d3d12_swapchain_has_user_images(swapchain))
        return S_OK;

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
                D3D12_RESOURCE_STATE_PRESENT, NULL, &object)))
        {
            ERR("Failed to create image for swapchain buffer");
            return hr;
        }

        swapchain->vk_images[i] = object->vk_image;
        swapchain->buffers[i] = (ID3D12Resource *)&object->ID3D12Resource_iface;

        vkd3d_resource_incref(swapchain->buffers[i]);
        ID3D12Resource_Release(swapchain->buffers[i]);

        /* It is technically possible to just start presenting images without rendering to them.
         * The initial resource state for swapchain images is PRESENT.
         * Since presentable images are dedicated allocations, we can safely queue a transition into common state
         * right away. We will also drain the queue when we release the images, so there is no risk of early delete. */
        vkd3d_enqueue_initial_transition(&swapchain->command_queue->ID3D12CommandQueue_iface, swapchain->buffers[i]);
    }

    if (FAILED(hr = d3d12_swapchain_create_user_descriptors(swapchain, vk_format)))
        return hr;

    return S_OK;
}

static VkResult d3d12_swapchain_record_swapchain_blit(struct d3d12_swapchain *swapchain,
        VkCommandBuffer vk_cmd_buffer, unsigned int dst_index, unsigned int src_index)
{
    const struct vkd3d_vk_device_procs *vk_procs = d3d12_swapchain_procs(swapchain);
    VkCommandBufferBeginInfo begin_info;
    VkRenderPassBeginInfo rp_info;
    VkClearValue clear_value;
    VkViewport viewport;
    VkResult vr;

    memset(&clear_value, 0, sizeof(clear_value));

    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.pNext = NULL;
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    begin_info.pInheritanceInfo = NULL;

    if ((vr = vk_procs->vkBeginCommandBuffer(vk_cmd_buffer, &begin_info)) < 0)
    {
        WARN("Failed to begin command buffer, vr %d.\n", vr);
        return vr;
    }

    rp_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rp_info.pNext = NULL;
    rp_info.renderPass = swapchain->pipeline.vk_render_pass;
    rp_info.framebuffer = swapchain->vk_framebuffers[dst_index];

    rp_info.renderArea.offset.x = 0;
    rp_info.renderArea.offset.y = 0;
    rp_info.renderArea.extent.width = swapchain->vk_swapchain_width;
    rp_info.renderArea.extent.height = swapchain->vk_swapchain_height;

    if (swapchain->desc.Scaling == DXGI_SCALING_NONE)
    {
        rp_info.clearValueCount = 1;
        rp_info.pClearValues = &clear_value;
    }
    else
    {
        rp_info.clearValueCount = 0;
        rp_info.pClearValues = NULL;
    }

    viewport.x = viewport.y = 0.0f;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VK_CALL(vkCmdBeginRenderPass(vk_cmd_buffer, &rp_info, VK_SUBPASS_CONTENTS_INLINE));
    if (swapchain->desc.Scaling == DXGI_SCALING_NONE)
    {
        viewport.width = (float)swapchain->desc.Width;
        viewport.height = (float)swapchain->desc.Height;
    }
    else
    {
        viewport.width = swapchain->vk_swapchain_width;
        viewport.height = swapchain->vk_swapchain_height;
    }

    VK_CALL(vkCmdSetViewport(vk_cmd_buffer, 0, 1, &viewport));
    VK_CALL(vkCmdSetScissor(vk_cmd_buffer, 0, 1, &rp_info.renderArea));

    VK_CALL(vkCmdBindPipeline(vk_cmd_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, swapchain->pipeline.vk_pipeline));
    VK_CALL(vkCmdBindDescriptorSets(vk_cmd_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
            swapchain->pipeline.vk_pipeline_layout, 0, 1, &swapchain->descriptors.sets[src_index],
            0, NULL));
    VK_CALL(vkCmdDraw(vk_cmd_buffer, 3, 1, 0, 0));
    VK_CALL(vkCmdEndRenderPass(vk_cmd_buffer));

    if ((vr = vk_procs->vkEndCommandBuffer(vk_cmd_buffer)) < 0)
        WARN("Failed to end command buffer, vr %d.\n", vr);

    return vr;
}

static void d3d12_swapchain_destroy_framebuffers(struct d3d12_swapchain *swapchain)
{
    const struct vkd3d_vk_device_procs *vk_procs = d3d12_swapchain_procs(swapchain);
    VkDevice vk_device = d3d12_swapchain_device(swapchain)->vk_device;
    unsigned int i;

    for (i = 0; i < swapchain->buffer_count; i++)
    {
        VK_CALL(vkDestroyImageView(vk_device, swapchain->vk_swapchain_image_views[i], NULL));
        VK_CALL(vkDestroyFramebuffer(vk_device, swapchain->vk_framebuffers[i], NULL));
        swapchain->vk_swapchain_image_views[i] = VK_NULL_HANDLE;
        swapchain->vk_framebuffers[i] = VK_NULL_HANDLE;
    }
}

static HRESULT d3d12_swapchain_create_framebuffers(struct d3d12_swapchain *swapchain, VkFormat format)
{
    const struct vkd3d_vk_device_procs *vk_procs = d3d12_swapchain_procs(swapchain);
    VkDevice vk_device = d3d12_swapchain_device(swapchain)->vk_device;
    VkImageViewCreateInfo image_view_info;
    VkFramebufferCreateInfo fb_info;
    unsigned int i;
    VkResult vr;

    fb_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fb_info.pNext = NULL;
    fb_info.flags = 0;
    fb_info.renderPass = swapchain->pipeline.vk_render_pass;
    fb_info.width = swapchain->vk_swapchain_width;
    fb_info.height = swapchain->vk_swapchain_height;
    fb_info.layers = 1;
    fb_info.attachmentCount = 1;

    image_view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    image_view_info.pNext = NULL;
    image_view_info.flags = 0;
    image_view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    image_view_info.format = format;
    image_view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    image_view_info.subresourceRange.baseMipLevel = 0;
    image_view_info.subresourceRange.baseArrayLayer = 0;
    image_view_info.subresourceRange.levelCount = 1;
    image_view_info.subresourceRange.layerCount = 1;
    image_view_info.components.r = VK_COMPONENT_SWIZZLE_R;
    image_view_info.components.g = VK_COMPONENT_SWIZZLE_G;
    image_view_info.components.b = VK_COMPONENT_SWIZZLE_B;
    image_view_info.components.a = VK_COMPONENT_SWIZZLE_A;

    for (i = 0; i < swapchain->buffer_count; i++)
    {
        image_view_info.image = swapchain->vk_swapchain_images[i];
        if ((vr = VK_CALL(vkCreateImageView(vk_device, &image_view_info, NULL, &swapchain->vk_swapchain_image_views[i]))))
            return hresult_from_vk_result(vr);
        fb_info.pAttachments = &swapchain->vk_swapchain_image_views[i];
        if ((vr = VK_CALL(vkCreateFramebuffer(vk_device, &fb_info, NULL, &swapchain->vk_framebuffers[i]))))
            return hresult_from_vk_result(vr);
    }

    return S_OK;
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
    VkSwapchainKHR vk_swapchain = swapchain->vk_swapchain;
    ID3D12CommandQueue *queue = &swapchain->command_queue->ID3D12CommandQueue_iface;
    VkDevice vk_device = d3d12_swapchain_device(swapchain)->vk_device;
    uint32_t image_count, queue_family_index;
    D3D12_COMMAND_QUEUE_DESC queue_desc;
    VkResult vr;
    HRESULT hr;

    d3d12_swapchain_destroy_framebuffers(swapchain);

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

    queue_desc = ID3D12CommandQueue_GetDesc(queue);
    if (queue_desc.Type != D3D12_COMMAND_LIST_TYPE_DIRECT)
    {
        FIXME("Swapchain blit not implemented for command queue type %#x.\n", queue_desc.Type);
        return E_NOTIMPL;
    }
    queue_family_index = vkd3d_get_vk_queue_family_index(queue);

    if (queue_desc.Type == D3D12_COMMAND_LIST_TYPE_DIRECT)
    {
        if (FAILED(hr = d3d12_swapchain_create_framebuffers(swapchain, vk_swapchain_format)))
            return hr;
    }

    if (FAILED(hr = d3d12_swapchain_create_user_buffers(swapchain, vk_format)))
        return hr;

    if (FAILED(hr = d3d12_swapchain_prepare_command_buffers(swapchain, queue_family_index)))
        return hr;

    return S_OK;
}

static VkResult d3d12_swapchain_wait_and_reset_swapchain_fence(struct d3d12_swapchain *swapchain)
{
    const struct vkd3d_vk_device_procs *vk_procs = d3d12_swapchain_procs(swapchain);
    VkDevice vk_device = d3d12_swapchain_device(swapchain)->vk_device;
    VkFence vk_fence = swapchain->vk_fence;
    VkResult vr;

    if ((vr = vk_procs->vkWaitForFences(vk_device, 1, &vk_fence, VK_TRUE, UINT64_MAX)) != VK_SUCCESS)
    {
        ERR("Failed to wait for fence, vr %d.\n", vr);
        return vr;
    }
    if ((vr = vk_procs->vkResetFences(vk_device, 1, &vk_fence)) < 0)
        ERR("Failed to reset fence, vr %d.\n", vr);

    return vr;
}

static VkResult d3d12_swapchain_acquire_next_vulkan_image(struct d3d12_swapchain *swapchain)
{
    const struct vkd3d_vk_device_procs *vk_procs = d3d12_swapchain_procs(swapchain);
    VkDevice vk_device = d3d12_swapchain_device(swapchain)->vk_device;
    VkFence vk_fence = swapchain->vk_fence;
    VkResult vr;

    swapchain->vk_image_index = INVALID_VK_IMAGE_INDEX;

    if ((vr = vk_procs->vkAcquireNextImageKHR(vk_device, swapchain->vk_swapchain, UINT64_MAX,
            VK_NULL_HANDLE, vk_fence, &swapchain->vk_image_index)))
    {
        if (vr == VK_SUBOPTIMAL_KHR)
        {
            /* Suboptimal is still considered success, so make sure to wait and reset fence here, but we always want
             * to recreate swapchains in this case. */
            d3d12_swapchain_wait_and_reset_swapchain_fence(swapchain);
            return VK_ERROR_OUT_OF_DATE_KHR;
        }
        WARN("Failed to acquire next Vulkan image, vr %d.\n", vr);
        return vr;
    }

    vr = d3d12_swapchain_wait_and_reset_swapchain_fence(swapchain);
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
        if (swapchain->buffers[i] && destroy_user_buffers)
        {
            vkd3d_resource_decref(swapchain->buffers[i]);
            swapchain->buffers[i] = NULL;
            swapchain->vk_images[i] = VK_NULL_HANDLE;
        }
    }

    if (destroy_user_buffers)
        d3d12_swapchain_destroy_user_descriptors(swapchain);

    if (swapchain->command_queue && swapchain->command_queue->device->vk_device)
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

static bool d3d12_swapchain_has_nonzero_surface_size(struct d3d12_swapchain *swapchain)
{
    VkPhysicalDevice vk_physical_device = d3d12_swapchain_device(swapchain)->vk_physical_device;
    const struct vkd3d_vk_device_procs *vk_procs = d3d12_swapchain_procs(swapchain);
    VkSurfaceCapabilitiesKHR surface_caps;
    VkResult vr;

    if ((vr = vk_procs->vkGetPhysicalDeviceSurfaceCapabilitiesKHR(vk_physical_device,
            swapchain->vk_surface, &surface_caps)) < 0)
    {
        WARN("Failed to get surface capabilities, vr %d.\n", vr);
        return false;
    }

    return surface_caps.maxImageExtent.width != 0 && surface_caps.maxImageExtent.height != 0;
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

    if (surface_caps.maxImageExtent.width == 0 || surface_caps.maxImageExtent.height == 0)
    {
        /* This is expected behavior on Windows when a HWND minimizes.
         * We must retry creating a proper swapchain later.
         * Since we can always fall back to user buffers,
         * we can pretend that we have an active swapchain. */
        TRACE("Window is minimized, cannot create swapchain at this time.\n");
    }
    else if (width != swapchain->desc.Width || height != swapchain->desc.Height)
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

    if (swapchain->command_queue->desc.Type == D3D12_COMMAND_LIST_TYPE_DIRECT)
        usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    else if (swapchain->command_queue->desc.Type == D3D12_COMMAND_LIST_TYPE_COMPUTE)
        usage = VK_IMAGE_USAGE_STORAGE_BIT;
    else
    {
        FIXME("Unsupported queue type.\n");
        return DXGI_ERROR_UNSUPPORTED;
    }

    if ((usage & surface_caps.supportedUsageFlags) != usage)
    {
        FIXME("Require usage flags not supported.\n");
        return DXGI_ERROR_UNSUPPORTED;
    }

    if (width && height)
    {
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
    }
    else
        vk_swapchain = VK_NULL_HANDLE;

    if (swapchain->vk_swapchain)
        vk_procs->vkDestroySwapchainKHR(swapchain->command_queue->device->vk_device, swapchain->vk_swapchain, NULL);

    swapchain->vk_swapchain = vk_swapchain;
    swapchain->vk_swapchain_width = width;
    swapchain->vk_swapchain_height = height;

    swapchain->vk_image_index = INVALID_VK_IMAGE_INDEX;

    if (swapchain->vk_swapchain != VK_NULL_HANDLE)
    {
        if (FAILED(hr = d3d12_swapchain_get_user_graphics_pipeline(swapchain, vk_swapchain_format)))
        {
            ERR("Failed to create user graphics pipeline, hr %ld.\n", hr);
            return hr;
        }

        return d3d12_swapchain_create_buffers(swapchain, vk_swapchain_format, vk_format);
    }
    else
    {
        /* Fallback path for when surface size is 0. We'll try to create a proper swapchain in a future Present call. */
        d3d12_swapchain_destroy_buffers(swapchain, FALSE);
        d3d12_swapchain_destroy_framebuffers(swapchain);
        swapchain->buffer_count = 0;
        return S_OK;
    }
}

static HRESULT d3d12_swapchain_recreate_vulkan_swapchain(struct d3d12_swapchain *swapchain)
{
    HRESULT hr;

    if (FAILED(hr = d3d12_swapchain_create_vulkan_swapchain(swapchain)))
        ERR("Failed to recreate Vulkan swapchain, hr %#x.\n", hr);

    return hr;
}

static inline struct d3d12_swapchain *d3d12_swapchain_from_IDXGISwapChain(dxgi_swapchain_iface *iface)
{
    return CONTAINING_RECORD(iface, struct d3d12_swapchain, IDXGISwapChain_iface);
}

/* IUnknown methods */

static HRESULT STDMETHODCALLTYPE d3d12_swapchain_QueryInterface(dxgi_swapchain_iface *iface, REFIID iid, void **object)
{
    TRACE("iface %p, iid %s, object %p.\n", iface, debugstr_guid(iid), object);

    if (IsEqualGUID(iid, &IID_IUnknown)
            || IsEqualGUID(iid, &IID_IDXGIObject)
            || IsEqualGUID(iid, &IID_IDXGIDeviceSubObject)
            || IsEqualGUID(iid, &IID_IDXGISwapChain)
            || IsEqualGUID(iid, &IID_IDXGISwapChain1)
            || IsEqualGUID(iid, &IID_IDXGISwapChain2)
            || IsEqualGUID(iid, &IID_IDXGISwapChain3)
            || IsEqualGUID(iid, &IID_IDXGISwapChain4))
    {
        IDXGISwapChain4_AddRef(iface);
        *object = iface;
        return S_OK;
    }

    WARN("%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid(iid));

    *object = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE d3d12_swapchain_AddRef(dxgi_swapchain_iface *iface)
{
    struct d3d12_swapchain *swapchain = d3d12_swapchain_from_IDXGISwapChain(iface);
    ULONG refcount = InterlockedIncrement(&swapchain->refcount);

    TRACE("%p increasing refcount to %u.\n", swapchain, refcount);

    return refcount;
}

static void d3d12_swapchain_destroy(struct d3d12_swapchain *swapchain)
{
    const struct vkd3d_vk_device_procs *vk_procs = d3d12_swapchain_procs(swapchain);

    d3d12_swapchain_destroy_buffers(swapchain, TRUE);
    d3d12_swapchain_destroy_framebuffers(swapchain);

    if (swapchain->frame_latency_event)
        CloseHandle(swapchain->frame_latency_event);

    if (swapchain->frame_latency_fence)
        ID3D12Fence_Release(swapchain->frame_latency_fence);


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

    ID3D12CommandQueue_Release(d3d12_swapchain_queue_iface(swapchain));

    if (swapchain->factory)
        IDXGIFactory_Release(swapchain->factory);

    DeleteCriticalSection(&swapchain->mutex);
}

static ULONG STDMETHODCALLTYPE d3d12_swapchain_Release(dxgi_swapchain_iface *iface)
{
    struct d3d12_swapchain *swapchain = d3d12_swapchain_from_IDXGISwapChain(iface);
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

static HRESULT STDMETHODCALLTYPE d3d12_swapchain_SetPrivateData(dxgi_swapchain_iface *iface,
        REFGUID guid, UINT data_size, const void *data)
{
    struct d3d12_swapchain *swapchain = d3d12_swapchain_from_IDXGISwapChain(iface);

    TRACE("iface %p, guid %s, data_size %u, data %p.\n", iface, debugstr_guid(guid), data_size, data);

    return vkd3d_set_private_data(&swapchain->private_store, guid, data_size, data);
}

static HRESULT STDMETHODCALLTYPE d3d12_swapchain_SetPrivateDataInterface(dxgi_swapchain_iface *iface,
        REFGUID guid, const IUnknown *object)
{
    struct d3d12_swapchain *swapchain = d3d12_swapchain_from_IDXGISwapChain(iface);

    TRACE("iface %p, guid %s, object %p.\n", iface, debugstr_guid(guid), object);

    return vkd3d_set_private_data_interface(&swapchain->private_store, guid, object);
}

static HRESULT STDMETHODCALLTYPE d3d12_swapchain_GetPrivateData(dxgi_swapchain_iface *iface,
        REFGUID guid, UINT *data_size, void *data)
{
    struct d3d12_swapchain *swapchain = d3d12_swapchain_from_IDXGISwapChain(iface);

    TRACE("iface %p, guid %s, data_size %p, data %p.\n", iface, debugstr_guid(guid), data_size, data);

    return vkd3d_get_private_data(&swapchain->private_store, guid, data_size, data);
}

static HRESULT STDMETHODCALLTYPE d3d12_swapchain_GetParent(dxgi_swapchain_iface *iface, REFIID iid, void **parent)
{
    struct d3d12_swapchain *swapchain = d3d12_swapchain_from_IDXGISwapChain(iface);

    TRACE("iface %p, iid %s, parent %p.\n", iface, debugstr_guid(iid), parent);

    return IDXGIFactory_QueryInterface(swapchain->factory, iid, parent);
}

/* IDXGIDeviceSubObject methods */

static HRESULT STDMETHODCALLTYPE d3d12_swapchain_GetDevice(dxgi_swapchain_iface *iface, REFIID iid, void **device)
{
    struct d3d12_swapchain *swapchain = d3d12_swapchain_from_IDXGISwapChain(iface);

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

    if (swapchain->vk_swapchain != VK_NULL_HANDLE && swapchain->present_mode == present_mode)
        return S_OK;

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
    VkCommandBuffer vk_cmd_buffer;
    VkPresentInfoKHR present_info;
    VkSubmitInfo submit_info;
    VkResult vr;

    /* Dummy present by doing nothing. */
    if (swapchain->vk_swapchain == VK_NULL_HANDLE)
        return VK_SUCCESS;

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

    vk_cmd_buffer = swapchain->vk_cmd_buffers[swapchain->vk_image_index];

    if ((vr = vk_procs->vkResetCommandBuffer(vk_cmd_buffer, 0)) < 0)
    {
        ERR("Failed to reset command buffer, vr %d.\n", vr);
        return vr;
    }

    if ((vr = d3d12_swapchain_record_swapchain_blit(swapchain,
            vk_cmd_buffer, swapchain->vk_image_index, swapchain->current_buffer_index)) < 0)
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

    if ((vr = vk_procs->vkQueuePresentKHR(vk_queue, &present_info)) >= 0)
    {
        swapchain->vk_image_index = INVALID_VK_IMAGE_INDEX;
        /* Could get SUBOPTIMAL here. Deal with it later. */
        vr = VK_SUCCESS;
    }

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

    if (swapchain->vk_swapchain == VK_NULL_HANDLE)
    {
        /* We're in a minimized state where we cannot present. However, we might be able to present now, so check that. */
        if (!d3d12_swapchain_has_nonzero_surface_size(swapchain))
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
    return hresult_from_vk_result(vr);
}

static HRESULT STDMETHODCALLTYPE d3d12_swapchain_Present(dxgi_swapchain_iface *iface, UINT sync_interval, UINT flags)
{
    struct d3d12_swapchain *swapchain = d3d12_swapchain_from_IDXGISwapChain(iface);
    HRESULT hr;

    TRACE("iface %p, sync_interval %u, flags %#x.\n", iface, sync_interval, flags);

    EnterCriticalSection(&swapchain->mutex);
    hr = d3d12_swapchain_present(swapchain, sync_interval, flags);
    LeaveCriticalSection(&swapchain->mutex);
    return hr;
}

static HRESULT STDMETHODCALLTYPE d3d12_swapchain_GetBuffer(dxgi_swapchain_iface *iface,
        UINT buffer_idx, REFIID iid, void **surface)
{
    struct d3d12_swapchain *swapchain = d3d12_swapchain_from_IDXGISwapChain(iface);

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

static HRESULT d3d12_swapchain_get_containing_output(struct d3d12_swapchain *swapchain, IDXGIOutput **output)
{
    IUnknown *device_parent;
    IDXGIFactory *factory;
    IDXGIAdapter *adapter;
    HRESULT hr;

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

static HRESULT STDMETHODCALLTYPE d3d12_swapchain_SetFullscreenState(dxgi_swapchain_iface *iface,
        BOOL fullscreen, IDXGIOutput *target)
{
    struct d3d12_swapchain *swapchain = d3d12_swapchain_from_IDXGISwapChain(iface);
    BOOL original_state;
    HRESULT hr;

    TRACE("iface %p, fullscreen %#x, target %p.\n", iface, fullscreen, target);

    EnterCriticalSection(&swapchain->mutex);

    if (!fullscreen && target)
    {
        WARN("Invalid call.\n");
        LeaveCriticalSection(&swapchain->mutex);
        return DXGI_ERROR_INVALID_CALL;
    }

    /* no-op */
    if (fullscreen != swapchain->fullscreen_desc.Windowed)
    {
        LeaveCriticalSection(&swapchain->mutex);
        return S_OK;
    }

    if (target)
    {
        IDXGIOutput_AddRef(target);
    }
    else if (FAILED(hr = d3d12_swapchain_get_containing_output(swapchain, &target)))
    {
        WARN("Failed to get target output for swapchain, hr %#x.\n", hr);
        LeaveCriticalSection(&swapchain->mutex);
        return hr;
    }

    original_state = swapchain->fullscreen_desc.Windowed;
    swapchain->fullscreen_desc.Windowed = !fullscreen;
    hr = d3d12_swapchain_set_fullscreen(swapchain, target, original_state);

    if (FAILED(hr))
    {
        swapchain->fullscreen_desc.Windowed = original_state;
        goto fail;
    }

    if (!fullscreen)
    {
        IDXGIOutput_Release(target);
        target = NULL;
    }

    if (swapchain->target)
        IDXGIOutput_Release(swapchain->target);
    swapchain->target = target;

    LeaveCriticalSection(&swapchain->mutex);
    return S_OK;

fail:
    IDXGIOutput_Release(target);
    LeaveCriticalSection(&swapchain->mutex);
    return DXGI_ERROR_NOT_CURRENTLY_AVAILABLE;
}

static HRESULT STDMETHODCALLTYPE d3d12_swapchain_GetFullscreenState(dxgi_swapchain_iface *iface,
        BOOL *fullscreen, IDXGIOutput **target)
{
    struct d3d12_swapchain *swapchain = d3d12_swapchain_from_IDXGISwapChain(iface);
    EnterCriticalSection(&swapchain->mutex);

    TRACE("iface %p, fullscreen %p, target %p.\n", iface, fullscreen, target);

    if (fullscreen)
        *fullscreen = !swapchain->fullscreen_desc.Windowed;

    if (target && (*target = swapchain->target))
        IDXGIOutput_AddRef(*target);

    LeaveCriticalSection(&swapchain->mutex);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d3d12_swapchain_GetDesc(dxgi_swapchain_iface *iface, DXGI_SWAP_CHAIN_DESC *desc)
{
    struct d3d12_swapchain *swapchain = d3d12_swapchain_from_IDXGISwapChain(iface);
    const DXGI_SWAP_CHAIN_FULLSCREEN_DESC *fullscreen_desc = &swapchain->fullscreen_desc;
    const DXGI_SWAP_CHAIN_DESC1 *swapchain_desc = &swapchain->desc;

    TRACE("iface %p, desc %p.\n", iface, desc);

    if (!desc)
    {
        WARN("Invalid pointer.\n");
        return E_INVALIDARG;
    }

    EnterCriticalSection(&swapchain->mutex);
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
    LeaveCriticalSection(&swapchain->mutex);

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

static HRESULT STDMETHODCALLTYPE d3d12_swapchain_ResizeBuffers(dxgi_swapchain_iface *iface,
        UINT buffer_count, UINT width, UINT height, DXGI_FORMAT format, UINT flags)
{
    struct d3d12_swapchain *swapchain = d3d12_swapchain_from_IDXGISwapChain(iface);
    HRESULT hr;

    TRACE("iface %p, buffer_count %u, width %u, height %u, format %s, flags %#x.\n",
            iface, buffer_count, width, height, debug_dxgi_format(format), flags);

    EnterCriticalSection(&swapchain->mutex);
    hr = d3d12_swapchain_resize_buffers(swapchain, buffer_count, width, height, format, flags);
    LeaveCriticalSection(&swapchain->mutex);
    return hr;
}

static HRESULT STDMETHODCALLTYPE d3d12_swapchain_ResizeTarget(dxgi_swapchain_iface *iface,
        const DXGI_MODE_DESC *target_mode_desc)
{
    struct d3d12_swapchain *swapchain = d3d12_swapchain_from_IDXGISwapChain(iface);
    HRESULT hr;

    TRACE("iface %p, target_mode_desc %p.\n", iface, target_mode_desc);

    EnterCriticalSection(&swapchain->mutex);
    hr = d3d12_swapchain_resize_target(swapchain, target_mode_desc);
    LeaveCriticalSection(&swapchain->mutex);
    return hr;
}

static HRESULT STDMETHODCALLTYPE d3d12_swapchain_GetContainingOutput(dxgi_swapchain_iface *iface,
        IDXGIOutput **output)
{
    struct d3d12_swapchain *swapchain = d3d12_swapchain_from_IDXGISwapChain(iface);
    HRESULT hr;
    TRACE("iface %p, output %p.\n", iface, output);

    EnterCriticalSection(&swapchain->mutex);
    hr = d3d12_swapchain_get_containing_output(swapchain, output);
    LeaveCriticalSection(&swapchain->mutex);
    return hr;
}

static HRESULT STDMETHODCALLTYPE d3d12_swapchain_GetFrameStatistics(dxgi_swapchain_iface *iface,
        DXGI_FRAME_STATISTICS *stats)
{
    FIXME("iface %p, stats %p stub!\n", iface, stats);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d12_swapchain_GetLastPresentCount(dxgi_swapchain_iface *iface,
        UINT *last_present_count)
{
    FIXME("iface %p, last_present_count %p stub!\n", iface, last_present_count);

    return E_NOTIMPL;
}

/* IDXGISwapChain1 methods */

static HRESULT STDMETHODCALLTYPE d3d12_swapchain_GetDesc1(dxgi_swapchain_iface *iface, DXGI_SWAP_CHAIN_DESC1 *desc)
{
    struct d3d12_swapchain *swapchain = d3d12_swapchain_from_IDXGISwapChain(iface);

    TRACE("iface %p, desc %p.\n", iface, desc);

    if (!desc)
    {
        WARN("Invalid pointer.\n");
        return E_INVALIDARG;
    }

    EnterCriticalSection(&swapchain->mutex);
    *desc = swapchain->desc;
    LeaveCriticalSection(&swapchain->mutex);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d3d12_swapchain_GetFullscreenDesc(dxgi_swapchain_iface *iface,
        DXGI_SWAP_CHAIN_FULLSCREEN_DESC *desc)
{
    struct d3d12_swapchain *swapchain = d3d12_swapchain_from_IDXGISwapChain(iface);

    TRACE("iface %p, desc %p.\n", iface, desc);

    if (!desc)
    {
        WARN("Invalid pointer.\n");
        return E_INVALIDARG;
    }

    EnterCriticalSection(&swapchain->mutex);
    *desc = swapchain->fullscreen_desc;
    LeaveCriticalSection(&swapchain->mutex);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d3d12_swapchain_GetHwnd(dxgi_swapchain_iface *iface, HWND *hwnd)
{
    struct d3d12_swapchain *swapchain = d3d12_swapchain_from_IDXGISwapChain(iface);

    TRACE("iface %p, hwnd %p.\n", iface, hwnd);

    if (!hwnd)
    {
        WARN("Invalid pointer.\n");
        return DXGI_ERROR_INVALID_CALL;
    }

    *hwnd = swapchain->window;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d3d12_swapchain_GetCoreWindow(dxgi_swapchain_iface *iface,
        REFIID iid, void **core_window)
{
    FIXME("iface %p, iid %s, core_window %p stub!\n", iface, debugstr_guid(iid), core_window);

    if (core_window)
        *core_window = NULL;

    return DXGI_ERROR_INVALID_CALL;
}

static HRESULT STDMETHODCALLTYPE d3d12_swapchain_Present1(dxgi_swapchain_iface *iface,
        UINT sync_interval, UINT flags, const DXGI_PRESENT_PARAMETERS *present_parameters)
{
    struct d3d12_swapchain *swapchain = d3d12_swapchain_from_IDXGISwapChain(iface);
    HRESULT hr;

    TRACE("iface %p, sync_interval %u, flags %#x, present_parameters %p.\n",
            iface, sync_interval, flags, present_parameters);

    if (present_parameters)
        FIXME("Ignored present parameters %p.\n", present_parameters);

    EnterCriticalSection(&swapchain->mutex);
    hr = d3d12_swapchain_present(swapchain, sync_interval, flags);
    LeaveCriticalSection(&swapchain->mutex);
    return hr;
}

static BOOL STDMETHODCALLTYPE d3d12_swapchain_IsTemporaryMonoSupported(dxgi_swapchain_iface *iface)
{
    FIXME("iface %p stub!\n", iface);

    return FALSE;
}

static HRESULT STDMETHODCALLTYPE d3d12_swapchain_GetRestrictToOutput(dxgi_swapchain_iface *iface, IDXGIOutput **output)
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

static HRESULT STDMETHODCALLTYPE d3d12_swapchain_SetBackgroundColor(dxgi_swapchain_iface *iface, const DXGI_RGBA *color)
{
    FIXME("iface %p, color %p stub!\n", iface, color);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d12_swapchain_GetBackgroundColor(dxgi_swapchain_iface *iface, DXGI_RGBA *color)
{
    FIXME("iface %p, color %p stub!\n", iface, color);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d12_swapchain_SetRotation(dxgi_swapchain_iface *iface, DXGI_MODE_ROTATION rotation)
{
    FIXME("iface %p, rotation %#x stub!\n", iface, rotation);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d12_swapchain_GetRotation(dxgi_swapchain_iface *iface, DXGI_MODE_ROTATION *rotation)
{
    FIXME("iface %p, rotation %p stub!\n", iface, rotation);

    return E_NOTIMPL;
}

/* IDXGISwapChain2 methods */

static HRESULT STDMETHODCALLTYPE d3d12_swapchain_SetSourceSize(dxgi_swapchain_iface *iface, UINT width, UINT height)
{
    FIXME("iface %p, width %u, height %u stub!\n", iface, width, height);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d12_swapchain_GetSourceSize(dxgi_swapchain_iface *iface, UINT *width, UINT *height)
{
    FIXME("iface %p, width %p, height %p stub!\n", iface, width, height);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d12_swapchain_SetMaximumFrameLatency(dxgi_swapchain_iface *iface, UINT max_latency)
{
    struct d3d12_swapchain *swapchain = d3d12_swapchain_from_IDXGISwapChain(iface);

    TRACE("iface %p, max_latency %u.\n", iface, max_latency);
    EnterCriticalSection(&swapchain->mutex);

    if (!(swapchain->desc.Flags & DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT))
    {
        WARN("DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT not set for swap chain %p.\n", iface);
        LeaveCriticalSection(&swapchain->mutex);
        return DXGI_ERROR_INVALID_CALL;
    }

    if (!max_latency)
    {
        WARN("Invalid maximum frame latency %u.\n", max_latency);
        LeaveCriticalSection(&swapchain->mutex);
        return DXGI_ERROR_INVALID_CALL;
    }

    swapchain->frame_latency = max_latency;
    LeaveCriticalSection(&swapchain->mutex);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d3d12_swapchain_GetMaximumFrameLatency(dxgi_swapchain_iface *iface, UINT *max_latency)
{
    struct d3d12_swapchain *swapchain = d3d12_swapchain_from_IDXGISwapChain(iface);

    TRACE("iface %p, max_latency %p.\n", iface, max_latency);
    EnterCriticalSection(&swapchain->mutex);

    if (!(swapchain->desc.Flags & DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT))
    {
        WARN("DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT not set for swap chain %p.\n", iface);
        LeaveCriticalSection(&swapchain->mutex);
        return DXGI_ERROR_INVALID_CALL;
    }

    *max_latency = swapchain->frame_latency;
    LeaveCriticalSection(&swapchain->mutex);
    return S_OK;
}

static HANDLE STDMETHODCALLTYPE d3d12_swapchain_GetFrameLatencyWaitableObject(dxgi_swapchain_iface *iface)
{
    struct d3d12_swapchain *swapchain = d3d12_swapchain_from_IDXGISwapChain(iface);

    TRACE("iface %p.\n", iface);

    return swapchain->frame_latency_event;
}

static HRESULT STDMETHODCALLTYPE d3d12_swapchain_SetMatrixTransform(dxgi_swapchain_iface *iface,
        const DXGI_MATRIX_3X2_F *matrix)
{
    FIXME("iface %p, matrix %p stub!\n", iface, matrix);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d12_swapchain_GetMatrixTransform(dxgi_swapchain_iface *iface,
        DXGI_MATRIX_3X2_F *matrix)
{
    FIXME("iface %p, matrix %p stub!\n", iface, matrix);

    return E_NOTIMPL;
}

/* IDXGISwapChain3 methods */

static UINT STDMETHODCALLTYPE d3d12_swapchain_GetCurrentBackBufferIndex(dxgi_swapchain_iface *iface)
{
    struct d3d12_swapchain *swapchain = d3d12_swapchain_from_IDXGISwapChain(iface);
    UINT index;
    EnterCriticalSection(&swapchain->mutex);

    TRACE("iface %p.\n", iface);

    TRACE("Current back buffer index %u.\n", swapchain->current_buffer_index);
    assert(swapchain->current_buffer_index < swapchain->desc.BufferCount);
    index = swapchain->current_buffer_index;
    LeaveCriticalSection(&swapchain->mutex);
    return index;
}

static HRESULT STDMETHODCALLTYPE d3d12_swapchain_CheckColorSpaceSupport(dxgi_swapchain_iface *iface,
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

static HRESULT STDMETHODCALLTYPE d3d12_swapchain_SetColorSpace1(dxgi_swapchain_iface *iface,
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

static HRESULT STDMETHODCALLTYPE d3d12_swapchain_ResizeBuffers1(dxgi_swapchain_iface *iface,
        UINT buffer_count, UINT width, UINT height, DXGI_FORMAT format, UINT flags,
        const UINT *node_mask, IUnknown * const *present_queue)
{
    struct d3d12_swapchain *swapchain = d3d12_swapchain_from_IDXGISwapChain(iface);
    size_t i, count;
    HRESULT hr;

    TRACE("iface %p, buffer_count %u, width %u, height %u, format %s, flags %#x, "
            "node_mask %p, present_queue %p.\n",
            iface, buffer_count, width, height, debug_dxgi_format(format), flags, node_mask, present_queue);

    if (!node_mask || !present_queue)
        return DXGI_ERROR_INVALID_CALL;

    EnterCriticalSection(&swapchain->mutex);

    count = buffer_count ? buffer_count : swapchain->desc.BufferCount;
    for (i = 0; i < count; ++i)
    {
        if (node_mask[i] > 1 || !present_queue[i])
        {
            LeaveCriticalSection(&swapchain->mutex);
            return DXGI_ERROR_INVALID_CALL;
        }

        if ((ID3D12CommandQueue*)present_queue[i] != d3d12_swapchain_queue_iface(swapchain))
            FIXME("Ignoring present queue %p.\n", present_queue[i]);
    }

    hr = d3d12_swapchain_resize_buffers(swapchain, buffer_count, width, height, format, flags);
    LeaveCriticalSection(&swapchain->mutex);
    return hr;
}

static HRESULT STDMETHODCALLTYPE d3d12_swapchain_SetHDRMetaData(dxgi_swapchain_iface *iface,
        DXGI_HDR_METADATA_TYPE type, UINT size, void *metadata)
{
    FIXME("iface %p, type %u, size %u, metadata %p semi-stub!", iface, type, size, metadata);

    if (size && !metadata)
      return E_INVALIDARG;

    switch (type)
    {
        case DXGI_HDR_METADATA_TYPE_NONE:
            return S_OK;

        case DXGI_HDR_METADATA_TYPE_HDR10:
            if (size != sizeof(DXGI_HDR_METADATA_HDR10))
                return E_INVALIDARG;

            /* For some reason this always seems to succeed on Windows */
            return S_OK;

        default:
            FIXME("Unsupported HDR metadata type %u.\n", type);
            return E_INVALIDARG;
    }
}

static CONST_VTBL struct IDXGISwapChain4Vtbl d3d12_swapchain_vtbl =
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
    /* IDXGISwapChain4 methods */
    d3d12_swapchain_SetHDRMetaData,
};

static HRESULT d3d12_swapchain_init(struct d3d12_swapchain *swapchain, IDXGIFactory *factory,
        struct d3d12_command_queue *queue, HWND window,
        const DXGI_SWAP_CHAIN_DESC1 *swapchain_desc, const DXGI_SWAP_CHAIN_FULLSCREEN_DESC *fullscreen_desc,
        IDXGIOutput *restrict_output_to_target)
{
    const struct vkd3d_vk_device_procs *vk_procs = &queue->device->vk_procs;
    VkWin32SurfaceCreateInfoKHR surface_desc;
    VkPhysicalDevice vk_physical_device;
    VkFenceCreateInfo fence_desc;
    uint32_t queue_family_index;
    VkSurfaceKHR vk_surface;
    VkInstance vk_instance;
    IDXGIAdapter *adapter;
    IDXGIOutput *target;
    VkBool32 supported;
    VkDevice vk_device;
    VkFence vk_fence;
    VkResult vr;
    HRESULT hr;

    InitializeCriticalSection(&swapchain->mutex);

    if (window == GetDesktopWindow())
    {
        WARN("D3D12 swapchain cannot be created on desktop window.\n");
        return E_ACCESSDENIED;
    }

    swapchain->IDXGISwapChain_iface.lpVtbl = &d3d12_swapchain_vtbl;
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

    if (FAILED(hr = d3d12_get_output_from_window((IDXGIFactory*)factory, window, &target)))
    {
        WARN("Failed to get output from window %p, hr %#x.\n", window, hr);

        if (FAILED(hr = IDXGIAdapter_EnumOutputs(adapter, 0, &target)))
        {
            IDXGIAdapter_Release(adapter);
            return hr;
        }

        FIXME("Using the primary output for the device window that is on a non-primary output.\n");
    }

    if (FAILED(hr = d3d12_output_get_display_mode(target, &swapchain->state.original_mode)))
    {
        ERR("Failed to get current display mode, hr %#x.\n", hr);
        return hr;
    }

    if (!swapchain->fullscreen_desc.Windowed)
    {
        swapchain->state.d3d_mode.Width = swapchain->desc.Width;
        swapchain->state.d3d_mode.Height = swapchain->desc.Height;
        swapchain->state.d3d_mode.Format = swapchain->desc.Format;
        swapchain->state.d3d_mode.RefreshRate = swapchain->fullscreen_desc.RefreshRate;
        swapchain->state.d3d_mode.ScanlineOrdering = swapchain->fullscreen_desc.ScanlineOrdering;
    }
    else
    {
        swapchain->state.d3d_mode = swapchain->state.original_mode;
    }

    GetWindowRect(window, &swapchain->state.original_window_rect);

    IDXGIAdapter_Release(adapter);

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

    if (FAILED(hr = d3d12_swapchain_set_fullscreen(swapchain, target, TRUE)))
    {
        ERR("Failed to enter fullscreen.");
        return hr;
    }

    if (!swapchain->fullscreen_desc.Windowed)
        swapchain->target = target;
    else
        IDXGIOutput_Release(target);

    IDXGIFactory_AddRef(swapchain->factory = factory);

    return S_OK;
}

static HRESULT d3d12_swapchain_create(IDXGIFactory *factory, struct d3d12_command_queue *queue, HWND window,
        const DXGI_SWAP_CHAIN_DESC1 *swapchain_desc, const DXGI_SWAP_CHAIN_FULLSCREEN_DESC *fullscreen_desc,
        IDXGIOutput *restrict_to_output, IDXGISwapChain1 **swapchain)
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

    hr = d3d12_swapchain_init(object, factory, queue, window, swapchain_desc, fullscreen_desc, restrict_to_output);

    if (FAILED(hr))
    {
        vkd3d_free(object);
        return hr;
    }

    TRACE("Created swapchain %p.\n", object);

    *swapchain = (IDXGISwapChain1 *)&object->IDXGISwapChain_iface;

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

    hr = d3d12_swapchain_create(factory, swapchain_factory->queue, window, swapchain_desc, fullscreen_desc, output, swapchain);

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
