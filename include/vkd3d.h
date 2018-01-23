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

#ifndef __VKD3D_H
#define __VKD3D_H

#ifndef VKD3D_NO_WIN32_TYPES
# include "vkd3d_windows.h"
# include "vkd3d_d3d12.h"
#endif  /* VKD3D_NO_WIN32_TYPES */

#ifndef VKD3D_NO_VULKAN_H
# include <vulkan/vulkan.h>
#endif  /* VKD3D_NO_VULKAN_H */

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

typedef bool (*vkd3d_signal_event_pfn)(HANDLE event);

typedef void * (*vkd3d_thread_pfn)(void *data);

typedef void * (*vkd3d_create_thread_pfn)(vkd3d_thread_pfn thread_main, void *data);
typedef bool (*vkd3d_join_thread_pfn)(void *thread);

struct vkd3d_instance;

struct vkd3d_instance_create_info
{
    vkd3d_signal_event_pfn signal_event_pfn;
    vkd3d_create_thread_pfn create_thread_pfn;
    vkd3d_join_thread_pfn join_thread_pfn;
    size_t wchar_size;

    /* If set to NULL, libvkd3d loads libvulkan. */
    PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr_pfn;

    const char * const *instance_extensions;
    uint32_t instance_extension_count;
};

struct vkd3d_device_create_info
{
    D3D_FEATURE_LEVEL minimum_feature_level;

    struct vkd3d_instance *instance;
    const struct vkd3d_instance_create_info *instance_create_info;

    VkPhysicalDevice vk_physical_device;
};

/* resource flags */
#define VKD3D_RESOURCE_INITIAL_STATE_TRANSITION 0x00000001
#define VKD3D_RESOURCE_SWAPCHAIN_IMAGE          0x00000002

HRESULT vkd3d_create_instance(const struct vkd3d_instance_create_info *create_info,
        struct vkd3d_instance **instance);
ULONG vkd3d_instance_decref(struct vkd3d_instance *instance);
VkInstance vkd3d_instance_get_vk_instance(struct vkd3d_instance *instance);
ULONG vkd3d_instance_incref(struct vkd3d_instance *instance);

HRESULT vkd3d_create_device(const struct vkd3d_device_create_info *create_info,
        REFIID riid, void **device);
HRESULT vkd3d_create_image_resource(ID3D12Device *device, const D3D12_RESOURCE_DESC *desc,
        VkImage vk_image, unsigned int resource_flags, ID3D12Resource **resource);
VkDevice vkd3d_get_vk_device(ID3D12Device *device);
VkPhysicalDevice vkd3d_get_vk_physical_device(ID3D12Device *device);
struct vkd3d_instance *vkd3d_instance_from_device(ID3D12Device *device);

uint32_t vkd3d_get_vk_queue_family_index(ID3D12CommandQueue *queue);
VkQueue vkd3d_acquire_vk_queue(ID3D12CommandQueue *queue);
void vkd3d_release_vk_queue(ID3D12CommandQueue *queue);

HRESULT vkd3d_serialize_root_signature(const D3D12_ROOT_SIGNATURE_DESC *root_signature_desc,
        D3D_ROOT_SIGNATURE_VERSION version, ID3DBlob **blob, ID3DBlob **error_blob);

HRESULT vkd3d_create_root_signature_deserializer(const void *data, SIZE_T data_size,
        REFIID iid, void **deserializer);

VkFormat vkd3d_get_vk_format(DXGI_FORMAT format);

#ifdef __cplusplus
}
#endif  /* __cplusplus */

#endif  /* __VKD3D_H */
