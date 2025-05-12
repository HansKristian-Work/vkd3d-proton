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

#define VKD3D_MIN_API_VERSION VK_API_VERSION_1_3
#define VKD3D_MAX_API_VERSION VK_API_VERSION_1_3

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
#define VKD3D_CONFIG_FLAG_NO_DXR (1ull << 7)
#define VKD3D_CONFIG_FLAG_FAULT (1ull << 8)
#define VKD3D_CONFIG_FLAG_FORCE_MINIMUM_SUBGROUP_SIZE (1ull << 9)
#define VKD3D_CONFIG_FLAG_NO_UPLOAD_HVV (1ull << 10)
#define VKD3D_CONFIG_FLAG_LOG_MEMORY_BUDGET (1ull << 11)
#define VKD3D_CONFIG_FLAG_BREADCRUMBS_SYNC (1ull << 12)
#define VKD3D_CONFIG_FLAG_FORCE_HOST_CACHED (1ull << 13)
#define VKD3D_CONFIG_FLAG_APP_DEBUG_MARKER_ONLY (1ull << 14)
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
#define VKD3D_CONFIG_FLAG_PLACED_TEXTURE_ALIASING (1ull << 31)
#define VKD3D_CONFIG_FLAG_USE_HOST_IMPORT_FALLBACK (1ull << 32)
#define VKD3D_CONFIG_FLAG_PREALLOCATE_SRV_MIP_CLAMPS (1ull << 33)
#define VKD3D_CONFIG_FLAG_FORCE_INITIAL_TRANSITION (1ull << 34)
#define VKD3D_CONFIG_FLAG_FORCE_DEDICATED_IMAGE_ALLOCATION (1ull << 35)
#define VKD3D_CONFIG_FLAG_BREADCRUMBS_TRACE (1ull << 36)
#define VKD3D_CONFIG_FLAG_DISABLE_SIMULTANEOUS_UAV_COMPRESSION (1ull << 37)
#define VKD3D_CONFIG_FLAG_REQUIRES_COMPUTE_INDIRECT_TEMPLATES (1ull << 38)
#define VKD3D_CONFIG_FLAG_SKIP_DRIVER_WORKAROUNDS (1ull << 39)
#define VKD3D_CONFIG_FLAG_RETAIN_PSOS (1ull << 40)
#define VKD3D_CONFIG_FLAG_ENABLE_EXPERIMENTAL_FEATURES (1ull << 41)
#define VKD3D_CONFIG_FLAG_REJECT_PADDED_SMALL_RESOURCE_ALIGNMENT (1ull << 42)
#define VKD3D_CONFIG_FLAG_DISABLE_UAV_COMPRESSION (1ull << 43)
#define VKD3D_CONFIG_FLAG_DISABLE_DEPTH_COMPRESSION (1ull << 44)
#define VKD3D_CONFIG_FLAG_DISABLE_COLOR_COMPRESSION (1ull << 45)
#define VKD3D_CONFIG_FLAG_DISABLE_DGCC (1ull << 46)
#define VKD3D_CONFIG_FLAG_MEMORY_ALLOCATOR_SKIP_IMAGE_HEAP_CLEAR (1ull << 47)
#define VKD3D_CONFIG_FLAG_DRIVER_VERSION_SENSITIVE_SHADERS (1ull << 48)
#define VKD3D_CONFIG_FLAG_SMALL_VRAM_REBAR (1ull << 49)
#define VKD3D_CONFIG_FLAG_NO_STAGGERED_SUBMIT (1ull << 50)
#define VKD3D_CONFIG_FLAG_CLEAR_UAV_SYNC (1ull << 51)
#define VKD3D_CONFIG_FLAG_FORCE_DYNAMIC_MSAA (1ull << 52)
#define VKD3D_CONFIG_FLAG_INSTRUCTION_QA_CHECKS (1ull << 53)
#define VKD3D_CONFIG_FLAG_TRANSFER_QUEUE (1ull << 54)
#define VKD3D_CONFIG_FLAG_NO_GPU_UPLOAD_HEAP (1ull << 55)
#define VKD3D_CONFIG_FLAG_ONE_TIME_SUBMIT (1ull << 56)
#define VKD3D_CONFIG_FLAG_SKIP_NULL_SPARSE_TILES (1ull << 57)
#define VKD3D_CONFIG_FLAG_QUEUE_PROFILE_EXTRA (1ull << 58)
#define VKD3D_CONFIG_FLAG_DAMAGE_NOT_ZEROED_ALLOCATIONS (1ull << 59)

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
void vkd3d_enqueue_initial_transition(ID3D12CommandQueue *queue, ID3D12Resource *resource);

HRESULT vkd3d_create_image_resource(ID3D12Device *device,
        const struct vkd3d_image_resource_create_info *create_info, ID3D12Resource **resource);
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

#ifdef __cplusplus
}
#endif  /* __cplusplus */

#endif  /* __VKD3D_H */
