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

#ifndef __VKD3D_H
#define __VKD3D_H

#include "vkd3d_windows.h"
#include "d3d12.h"
#include <vulkan/vulkan.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

typedef bool (*vkd3d_signal_event_pfn)(HANDLE event);

struct vkd3d_device_create_info
{
    D3D_FEATURE_LEVEL minimum_feature_level;
    vkd3d_signal_event_pfn signal_event_pfn;
};

HRESULT vkd3d_create_device(const struct vkd3d_device_create_info *create_info,
        REFIID riid, void **device);
HRESULT vkd3d_create_image_resource(ID3D12Device *device, const D3D12_RESOURCE_DESC *desc,
        VkImage vk_image, ID3D12Resource **resource);
VkDevice vkd3d_get_vk_device(ID3D12Device *device);
VkInstance vkd3d_get_vk_instance(ID3D12Device *device);

#ifdef __cplusplus
}
#endif  /* __cplusplus */

#endif  /* __VKD3D_H */
