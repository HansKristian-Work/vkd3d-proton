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

#include <vkd3d_types.h>

#ifndef VKD3D_NO_WIN32_TYPES
# define COBJMACROS
# include <vkd3d_windows.h>

# define WIDL_C_INLINE_WRAPPERS

# ifdef __MINGW32__
/* Workaround for MinGW-tools WIDL when using inline wrappers.
 * FORCEINLINE is extern which conflicts. It is okay to override it here.
 * All relevant system headers have been included. */
#  undef FORCEINLINE
#  define FORCEINLINE inline
# endif

# include <vkd3d_d3d12.h>
# include <vkd3d_core_interface.h>
# undef WIDL_C_INLINE_WRAPPERS
#endif  /* VKD3D_NO_WIN32_TYPES */

#ifndef VKD3D_NO_VULKAN_H
# ifdef _WIN32
#  define VK_USE_PLATFORM_WIN32_KHR
# endif
# include <vulkan/vulkan.h>
# include "private/vulkan_private_extensions.h"
#endif  /* VKD3D_NO_VULKAN_H */

#include <stdbool.h>

#define VKD3D_MIN_API_VERSION VK_API_VERSION_1_3
#define VKD3D_MAX_API_VERSION VK_API_VERSION_1_3

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

struct vkd3d_instance;

struct vkd3d_instance_create_info
{
    /* If set to NULL, libvkd3d loads libvulkan. */
    PFN_vkGetInstanceProcAddr pfn_vkGetInstanceProcAddr;

    const char * const *instance_extensions;
    uint32_t instance_extension_count;

    const char * const *optional_instance_extensions;
    uint32_t optional_instance_extension_count;
};

struct vkd3d_device_create_info
{
    D3D_FEATURE_LEVEL minimum_feature_level;

    struct vkd3d_instance *instance;
    const struct vkd3d_instance_create_info *instance_create_info;

    VkPhysicalDevice vk_physical_device;

    const char * const *device_extensions;
    uint32_t device_extension_count;

    const char * const *optional_device_extensions;
    uint32_t optional_device_extension_count;

    IUnknown *parent;
    LUID adapter_luid;

    D3D12_DEVICE_FACTORY_FLAGS device_factory_flags;
    bool independent;
};

struct vkd3d_image_resource_create_info
{
    VkImage vk_image;
    D3D12_RESOURCE_DESC desc;
    unsigned int flags;
    D3D12_RESOURCE_STATES present_state;
};

HRESULT vkd3d_create_instance(const struct vkd3d_instance_create_info *create_info,
        struct vkd3d_instance **instance);
ULONG vkd3d_instance_decref(struct vkd3d_instance *instance);
VkInstance vkd3d_instance_get_vk_instance(struct vkd3d_instance *instance);
ULONG vkd3d_instance_incref(struct vkd3d_instance *instance);

HRESULT vkd3d_create_device(const struct vkd3d_device_create_info *create_info,
        REFIID iid, void **device);
IUnknown *vkd3d_get_device_parent(ID3D12Device *device);
VkDevice vkd3d_get_vk_device(ID3D12Device *device);
VkPhysicalDevice vkd3d_get_vk_physical_device(ID3D12Device *device);
struct vkd3d_instance *vkd3d_instance_from_device(ID3D12Device *device);

uint32_t vkd3d_get_vk_queue_family_index(ID3D12CommandQueue *queue);
uint32_t vkd3d_get_vk_queue_index(ID3D12CommandQueue *queue);
uint32_t vkd3d_get_vk_queue_flags(ID3D12CommandQueue *queue);
VkQueue vkd3d_acquire_vk_queue(ID3D12CommandQueue *queue);
void vkd3d_release_vk_queue(ID3D12CommandQueue *queue);
VkQueue vkd3d_lock_vk_queue(ID3D12CommandQueue *queue);
void vkd3d_unlock_vk_queue(ID3D12CommandQueue *queue);
void vkd3d_enqueue_initial_transition(ID3D12CommandQueue *queue, ID3D12Resource *resource);

ULONG vkd3d_resource_decref(ID3D12Resource *resource);
ULONG vkd3d_resource_incref(ID3D12Resource *resource);

HRESULT vkd3d_serialize_root_signature(const D3D12_ROOT_SIGNATURE_DESC *desc,
        D3D_ROOT_SIGNATURE_VERSION version, ID3DBlob **blob, ID3DBlob **error_blob);
HRESULT vkd3d_create_root_signature_deserializer(const void *data, SIZE_T data_size,
        REFIID iid, void **deserializer);

VkFormat vkd3d_get_vk_format(DXGI_FORMAT format);

DXGI_FORMAT vkd3d_get_dxgi_format(VkFormat format);

HRESULT vkd3d_serialize_versioned_root_signature(const D3D12_VERSIONED_ROOT_SIGNATURE_DESC *desc,
        ID3DBlob **blob, ID3DBlob **error_blob);
HRESULT vkd3d_create_versioned_root_signature_deserializer(const void *data, SIZE_T data_size,
        REFIID iid, void **deserializer);
HRESULT vkd3d_create_versioned_root_signature_deserializer_for_subobject(const void *data, SIZE_T data_size,
        LPCWSTR subobject_name, REFIID iid, void **deserializer);

#ifdef __cplusplus
}
#endif  /* __cplusplus */

#endif  /* __VKD3D_H */
