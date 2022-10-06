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
# include <vkd3d_windows.h>
# include <vkd3d_d3d12.h>
#endif  /* VKD3D_NO_WIN32_TYPES */

#ifndef VKD3D_NO_VULKAN_H
# ifdef _WIN32
#  define VK_USE_PLATFORM_WIN32_KHR
# endif
# include <vulkan/vulkan.h>
# include "private/vulkan_private_extensions.h"
#endif  /* VKD3D_NO_VULKAN_H */

#define VKD3D_MIN_API_VERSION VK_API_VERSION_1_1
#define VKD3D_MAX_API_VERSION VK_API_VERSION_1_1

#if defined(__GNUC__)
# define DECLSPEC_VISIBLE __attribute__((visibility("default")))
#else
# define DECLSPEC_VISIBLE
#endif

#if defined(_WIN32) && !defined(VKD3D_BUILD_STANDALONE_D3D12)
# ifdef VKD3D_EXPORTS
#  define VKD3D_EXPORT __declspec(dllexport)
# else
#  define VKD3D_EXPORT __declspec(dllimport)
# endif
#elif defined(__GNUC__)
# define VKD3D_EXPORT DECLSPEC_VISIBLE
#else
# define VKD3D_EXPORT
#endif

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

#define VKD3D_CONFIG_FLAG_VULKAN_DEBUG (1ull << 0)
#define VKD3D_CONFIG_FLAG_SKIP_APPLICATION_WORKAROUNDS (1ull << 1)
#define VKD3D_CONFIG_FLAG_DEBUG_UTILS (1ull << 2)
#define VKD3D_CONFIG_FLAG_FORCE_STATIC_CBV (1ull << 3)
#define VKD3D_CONFIG_FLAG_DXR (1ull << 4)
#define VKD3D_CONFIG_FLAG_SINGLE_QUEUE (1ull << 5)
#define VKD3D_CONFIG_FLAG_DESCRIPTOR_QA_CHECKS (1ull << 6)
#define VKD3D_CONFIG_FLAG_FORCE_RTV_EXCLUSIVE_QUEUE (1ull << 7)
#define VKD3D_CONFIG_FLAG_FORCE_DSV_EXCLUSIVE_QUEUE (1ull << 8)
#define VKD3D_CONFIG_FLAG_FORCE_MINIMUM_SUBGROUP_SIZE (1ull << 9)
#define VKD3D_CONFIG_FLAG_NO_UPLOAD_HVV (1ull << 10)
#define VKD3D_CONFIG_FLAG_LOG_MEMORY_BUDGET (1ull << 11)
#define VKD3D_CONFIG_FLAG_IGNORE_RTV_HOST_VISIBLE (1ull << 12)
#define VKD3D_CONFIG_FLAG_FORCE_HOST_CACHED (1ull << 13)
#define VKD3D_CONFIG_FLAG_DXR11 (1ull << 14)
#define VKD3D_CONFIG_FLAG_FORCE_NO_INVARIANT_POSITION (1ull << 15)
#define VKD3D_CONFIG_FLAG_GLOBAL_PIPELINE_CACHE (1ull << 16)
#define VKD3D_CONFIG_FLAG_PIPELINE_LIBRARY_NO_SERIALIZE_SPIRV (1ull << 17)
#define VKD3D_CONFIG_FLAG_PIPELINE_LIBRARY_SANITIZE_SPIRV (1ull << 18)
#define VKD3D_CONFIG_FLAG_PIPELINE_LIBRARY_LOG (1ull << 19)
#define VKD3D_CONFIG_FLAG_PIPELINE_LIBRARY_IGNORE_SPIRV (1ull << 20)
#define VKD3D_CONFIG_FLAG_MUTABLE_SINGLE_SET (1ull << 21)
#define VKD3D_CONFIG_FLAG_MEMORY_ALLOCATOR_SKIP_CLEAR (1ull << 22)
#define VKD3D_CONFIG_FLAG_RECYCLE_COMMAND_POOLS (1ull << 23)
#define VKD3D_CONFIG_FLAG_PIPELINE_LIBRARY_IGNORE_MISMATCH_DRIVER (1ull << 24)
#define VKD3D_CONFIG_FLAG_BREADCRUMBS (1ull << 25)
#define VKD3D_CONFIG_FLAG_PIPELINE_LIBRARY_APP_CACHE_ONLY (1ull << 26)
#define VKD3D_CONFIG_FLAG_SHADER_CACHE_SYNC (1ull << 27)
#define VKD3D_CONFIG_FLAG_FORCE_RAW_VA_CBV (1ull << 28)
#define VKD3D_CONFIG_FLAG_ZERO_MEMORY_WORKAROUNDS_COMMITTED_BUFFER_UAV (1ull << 29)
#define VKD3D_CONFIG_FLAG_ALLOW_SBT_COLLECTION (1ull << 30)
#define VKD3D_CONFIG_FLAG_FORCE_NATIVE_FP16 (1ull << 31)
#define VKD3D_CONFIG_FLAG_USE_HOST_IMPORT_FALLBACK (1ull << 32)
#define VKD3D_CONFIG_FLAG_BREADCRUMBS_TRACE (1ull << 33)

typedef HRESULT (*PFN_vkd3d_signal_event)(HANDLE event);

typedef void * (*PFN_vkd3d_thread)(void *data);

typedef void * (*PFN_vkd3d_create_thread)(PFN_vkd3d_thread thread_main, void *data);
typedef HRESULT (*PFN_vkd3d_join_thread)(void *thread);

struct vkd3d_instance;

struct vkd3d_instance_create_info
{
    PFN_vkd3d_signal_event pfn_signal_event;
    PFN_vkd3d_create_thread pfn_create_thread;
    PFN_vkd3d_join_thread pfn_join_thread;

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
};

struct vkd3d_image_resource_create_info
{
    VkImage vk_image;
    D3D12_RESOURCE_DESC desc;
    unsigned int flags;
    D3D12_RESOURCE_STATES present_state;
};

#ifndef VKD3D_NO_PROTOTYPES

VKD3D_EXPORT HRESULT vkd3d_create_instance(const struct vkd3d_instance_create_info *create_info,
        struct vkd3d_instance **instance);
VKD3D_EXPORT ULONG vkd3d_instance_decref(struct vkd3d_instance *instance);
VKD3D_EXPORT VkInstance vkd3d_instance_get_vk_instance(struct vkd3d_instance *instance);
VKD3D_EXPORT ULONG vkd3d_instance_incref(struct vkd3d_instance *instance);

VKD3D_EXPORT HRESULT vkd3d_create_device(const struct vkd3d_device_create_info *create_info,
        REFIID iid, void **device);
VKD3D_EXPORT IUnknown *vkd3d_get_device_parent(ID3D12Device *device);
VKD3D_EXPORT VkDevice vkd3d_get_vk_device(ID3D12Device *device);
VKD3D_EXPORT VkPhysicalDevice vkd3d_get_vk_physical_device(ID3D12Device *device);
VKD3D_EXPORT struct vkd3d_instance *vkd3d_instance_from_device(ID3D12Device *device);

VKD3D_EXPORT uint32_t vkd3d_get_vk_queue_family_index(ID3D12CommandQueue *queue);
VKD3D_EXPORT VkQueue vkd3d_acquire_vk_queue(ID3D12CommandQueue *queue);
VKD3D_EXPORT void vkd3d_release_vk_queue(ID3D12CommandQueue *queue);
VKD3D_EXPORT void vkd3d_enqueue_initial_transition(ID3D12CommandQueue *queue, ID3D12Resource *resource);

VKD3D_EXPORT HRESULT vkd3d_create_image_resource(ID3D12Device *device,
        const struct vkd3d_image_resource_create_info *create_info, ID3D12Resource **resource);
VKD3D_EXPORT ULONG vkd3d_resource_decref(ID3D12Resource *resource);
VKD3D_EXPORT ULONG vkd3d_resource_incref(ID3D12Resource *resource);

VKD3D_EXPORT HRESULT vkd3d_serialize_root_signature(const D3D12_ROOT_SIGNATURE_DESC *desc,
        D3D_ROOT_SIGNATURE_VERSION version, ID3DBlob **blob, ID3DBlob **error_blob);
VKD3D_EXPORT HRESULT vkd3d_create_root_signature_deserializer(const void *data, SIZE_T data_size,
        REFIID iid, void **deserializer);

VKD3D_EXPORT VkFormat vkd3d_get_vk_format(DXGI_FORMAT format);

/* 1.1 */
VKD3D_EXPORT DXGI_FORMAT vkd3d_get_dxgi_format(VkFormat format);

/* 1.2 */
VKD3D_EXPORT HRESULT vkd3d_serialize_versioned_root_signature(const D3D12_VERSIONED_ROOT_SIGNATURE_DESC *desc,
        ID3DBlob **blob, ID3DBlob **error_blob);
VKD3D_EXPORT HRESULT vkd3d_create_versioned_root_signature_deserializer(const void *data, SIZE_T data_size,
        REFIID iid, void **deserializer);

#endif  /* VKD3D_NO_PROTOTYPES */

/*
 * Function pointer typedefs for vkd3d functions.
 */
typedef HRESULT (*PFN_vkd3d_create_instance)(const struct vkd3d_instance_create_info *create_info,
        struct vkd3d_instance **instance);
typedef ULONG (*PFN_vkd3d_instance_decref)(struct vkd3d_instance *instance);
typedef VkInstance (*PFN_vkd3d_instance_get_vk_instance)(struct vkd3d_instance *instance);
typedef ULONG (*PFN_vkd3d_instance_incref)(struct vkd3d_instance *instance);

typedef HRESULT (*PFN_vkd3d_create_device)(const struct vkd3d_device_create_info *create_info,
        REFIID iid, void **device);
typedef IUnknown * (*PFN_vkd3d_get_device_parent)(ID3D12Device *device);
typedef VkDevice (*PFN_vkd3d_get_vk_device)(ID3D12Device *device);
typedef VkPhysicalDevice (*PFN_vkd3d_get_vk_physical_device)(ID3D12Device *device);
typedef struct vkd3d_instance * (*PFN_vkd3d_instance_from_device)(ID3D12Device *device);

typedef uint32_t (*PFN_vkd3d_get_vk_queue_family_index)(ID3D12CommandQueue *queue);
typedef VkQueue (*PFN_vkd3d_acquire_vk_queue)(ID3D12CommandQueue *queue);
typedef void (*PFN_vkd3d_release_vk_queue)(ID3D12CommandQueue *queue);

typedef HRESULT (*PFN_vkd3d_create_image_resource)(ID3D12Device *device,
        const struct vkd3d_image_resource_create_info *create_info, ID3D12Resource **resource);
typedef ULONG (*PFN_vkd3d_resource_decref)(ID3D12Resource *resource);
typedef ULONG (*PFN_vkd3d_resource_incref)(ID3D12Resource *resource);

typedef HRESULT (*PFN_vkd3d_serialize_root_signature)(const D3D12_ROOT_SIGNATURE_DESC *desc,
        D3D_ROOT_SIGNATURE_VERSION version, ID3DBlob **blob, ID3DBlob **error_blob);
typedef HRESULT (*PFN_vkd3d_create_root_signature_deserializer)(const void *data, SIZE_T data_size,
        REFIID iid, void **deserializer);

typedef VkFormat (*PFN_vkd3d_get_vk_format)(DXGI_FORMAT format);

/* 1.1 */
typedef DXGI_FORMAT (*PFN_vkd3d_get_dxgi_format)(VkFormat format);

/* 1.2 */
typedef HRESULT (*PFN_vkd3d_serialize_versioned_root_signature)(const D3D12_VERSIONED_ROOT_SIGNATURE_DESC *desc,
        ID3DBlob **blob, ID3DBlob **error_blob);
typedef HRESULT (*PFN_vkd3d_create_versioned_root_signature_deserializer)(const void *data, SIZE_T data_size,
        REFIID iid, void **deserializer);

#ifdef __cplusplus
}
#endif  /* __cplusplus */

#endif  /* __VKD3D_H */
