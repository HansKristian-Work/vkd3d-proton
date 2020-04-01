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

#ifndef __VKD3D_PRIVATE_H
#define __VKD3D_PRIVATE_H

#define COBJMACROS
#define NONAMELESSUNION
#define VK_NO_PROTOTYPES

#include "vkd3d_common.h"
#include "vkd3d_memory.h"
#include "vkd3d_utf8.h"
#include "list.h"
#include "rbtree.h"

#include "vkd3d.h"
#include "vkd3d_shader.h"
#include "vkd3d_threads.h"

#include <assert.h>
#include <inttypes.h>
#include <limits.h>
#include <stdbool.h>

#define VK_CALL(f) (vk_procs->f)

#define MAKE_MAGIC(a,b,c,d) (((uint32_t)a) | (((uint32_t)b) << 8) | (((uint32_t)c) << 16) | d)

#define VKD3D_DESCRIPTOR_MAGIC_HAS_VIEW 0x01000000u

#define VKD3D_DESCRIPTOR_MAGIC_FREE    0x00000000u
#define VKD3D_DESCRIPTOR_MAGIC_CBV     MAKE_MAGIC('C', 'B', 'V', 0)
#define VKD3D_DESCRIPTOR_MAGIC_SRV     MAKE_MAGIC('S', 'R', 'V', VKD3D_DESCRIPTOR_MAGIC_HAS_VIEW)
#define VKD3D_DESCRIPTOR_MAGIC_UAV     MAKE_MAGIC('U', 'A', 'V', VKD3D_DESCRIPTOR_MAGIC_HAS_VIEW)
#define VKD3D_DESCRIPTOR_MAGIC_SAMPLER MAKE_MAGIC('S', 'M', 'P', VKD3D_DESCRIPTOR_MAGIC_HAS_VIEW)
#define VKD3D_DESCRIPTOR_MAGIC_DSV     MAKE_MAGIC('D', 'S', 'V', 0)
#define VKD3D_DESCRIPTOR_MAGIC_RTV     MAKE_MAGIC('R', 'T', 'V', 0)

#define VKD3D_MAX_COMPATIBLE_FORMAT_COUNT 6u
#define VKD3D_MAX_QUEUE_FAMILY_COUNT      3u
#define VKD3D_MAX_SHADER_EXTENSIONS       1u
#define VKD3D_MAX_SHADER_STAGES           5u
#define VKD3D_MAX_VK_SYNC_OBJECTS         4u

#define VKD3D_MAX_DESCRIPTOR_SETS 8u
#define VKD3D_MAX_BINDLESS_DESCRIPTOR_SETS 6u

struct d3d12_command_list;
struct d3d12_device;
struct d3d12_resource;

struct vkd3d_bindless_set_info;

struct vkd3d_vk_global_procs
{
    PFN_vkCreateInstance vkCreateInstance;
    PFN_vkEnumerateInstanceVersion vkEnumerateInstanceVersion;
    PFN_vkEnumerateInstanceExtensionProperties vkEnumerateInstanceExtensionProperties;
    PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr;
};

#define DECLARE_VK_PFN(name) PFN_##name name;
struct vkd3d_vk_instance_procs
{
#define VK_INSTANCE_PFN     DECLARE_VK_PFN
#define VK_INSTANCE_EXT_PFN DECLARE_VK_PFN
#include "vulkan_procs.h"
};

struct vkd3d_vk_device_procs
{
#define VK_INSTANCE_PFN   DECLARE_VK_PFN
#define VK_DEVICE_PFN     DECLARE_VK_PFN
#define VK_DEVICE_EXT_PFN DECLARE_VK_PFN
#include "vulkan_procs.h"
};
#undef DECLARE_VK_PFN

HRESULT hresult_from_errno(int rc) DECLSPEC_HIDDEN;
HRESULT hresult_from_vk_result(VkResult vr) DECLSPEC_HIDDEN;
HRESULT hresult_from_vkd3d_result(int vkd3d_result) DECLSPEC_HIDDEN;

struct vkd3d_vulkan_info
{
    /* KHR instance extensions */
    bool KHR_get_physical_device_properties2;
    /* EXT instance extensions */
    bool EXT_debug_report;

    /* KHR device extensions */
    bool KHR_buffer_device_address;
    bool KHR_dedicated_allocation;
    bool KHR_draw_indirect_count;
    bool KHR_get_memory_requirements2;
    bool KHR_image_format_list;
    bool KHR_maintenance3;
    bool KHR_push_descriptor;
    bool KHR_timeline_semaphore;
    /* EXT device extensions */
    bool EXT_conditional_rendering;
    bool EXT_debug_marker;
    bool EXT_depth_clip_enable;
    bool EXT_descriptor_indexing;
    bool EXT_inline_uniform_block;
    bool EXT_shader_demote_to_helper_invocation;
    bool EXT_subgroup_size_control;
    bool EXT_texel_buffer_alignment;
    bool EXT_transform_feedback;
    bool EXT_vertex_attribute_divisor;
    /* AMD device extensions */
    bool AMD_shader_core_properties;
    bool AMD_shader_core_properties2;
    /* NV device extensions */
    bool NV_shader_sm_builtins;

    bool rasterization_stream;
    bool transform_feedback_queries;

    bool vertex_attrib_zero_divisor;
    unsigned int max_vertex_attrib_divisor;

    VkPhysicalDeviceLimits device_limits;
    VkPhysicalDeviceSparseProperties sparse_properties;

    VkPhysicalDeviceTexelBufferAlignmentPropertiesEXT texel_buffer_alignment_properties;

    unsigned int shader_extension_count;
    enum vkd3d_shader_target_extension shader_extensions[VKD3D_MAX_SHADER_EXTENSIONS];

    bool supports_volatile_packed_descriptors;
};

enum vkd3d_config_flags
{
    VKD3D_CONFIG_FLAG_VULKAN_DEBUG = 0x00000001,
};

struct vkd3d_instance
{
    VkInstance vk_instance;
    uint32_t instance_version;
    struct vkd3d_vk_instance_procs vk_procs;

    PFN_vkd3d_signal_event signal_event;
    PFN_vkd3d_create_thread create_thread;
    PFN_vkd3d_join_thread join_thread;
    size_t wchar_size;

    struct vkd3d_vulkan_info vk_info;
    struct vkd3d_vk_global_procs vk_global_procs;
    void *libvulkan;

    uint64_t config_flags;

    VkDebugReportCallbackEXT vk_debug_callback;

    LONG refcount;
};

union vkd3d_thread_handle
{
    pthread_t pthread;
    void *handle;
};

HRESULT vkd3d_create_thread(struct vkd3d_instance *instance,
        PFN_vkd3d_thread thread_main, void *data, union vkd3d_thread_handle *thread) DECLSPEC_HIDDEN;
HRESULT vkd3d_join_thread(struct vkd3d_instance *instance, union vkd3d_thread_handle *thread) DECLSPEC_HIDDEN;

struct vkd3d_waiting_fence
{
    struct d3d12_fence *fence;
    uint64_t value;
    struct vkd3d_queue *queue;
    uint64_t queue_sequence_number;
};

struct vkd3d_fence_worker
{
    union vkd3d_thread_handle thread;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    pthread_cond_t fence_destruction_cond;
    bool should_exit;
    bool pending_fence_destruction;

    size_t enqueued_fence_count;
    struct vkd3d_enqueued_fence
    {
        VkFence vk_fence;
        VkSemaphore vk_semaphore;
        struct vkd3d_waiting_fence waiting_fence;
    } *enqueued_fences;
    size_t enqueued_fences_size;

    size_t fence_count;
    VkFence *vk_fences;
    size_t vk_fences_size;
    struct vkd3d_waiting_fence *fences;
    size_t fences_size;

    uint64_t *semaphore_wait_values;
    VkSemaphore *vk_semaphores;
    size_t vk_semaphores_size;
    size_t semaphore_wait_values_size;

    struct d3d12_device *device;
};

HRESULT vkd3d_fence_worker_start(struct vkd3d_fence_worker *worker,
        struct d3d12_device *device) DECLSPEC_HIDDEN;
HRESULT vkd3d_fence_worker_stop(struct vkd3d_fence_worker *worker,
        struct d3d12_device *device) DECLSPEC_HIDDEN;

struct vkd3d_gpu_va_allocation
{
    D3D12_GPU_VIRTUAL_ADDRESS base;
    size_t size;
    void *ptr;
};

struct vkd3d_gpu_va_slab
{
    size_t size;
    void *ptr;
};

struct vkd3d_gpu_va_allocator
{
    pthread_mutex_t mutex;

    D3D12_GPU_VIRTUAL_ADDRESS fallback_floor;
    struct vkd3d_gpu_va_allocation *fallback_allocations;
    size_t fallback_allocations_size;
    size_t fallback_allocation_count;

    struct vkd3d_gpu_va_slab *slabs;
    struct vkd3d_gpu_va_slab *free_slab;
};

D3D12_GPU_VIRTUAL_ADDRESS vkd3d_gpu_va_allocator_allocate(struct vkd3d_gpu_va_allocator *allocator,
        size_t alignment, size_t size, void *ptr) DECLSPEC_HIDDEN;
void *vkd3d_gpu_va_allocator_dereference(struct vkd3d_gpu_va_allocator *allocator,
        D3D12_GPU_VIRTUAL_ADDRESS address) DECLSPEC_HIDDEN;
void vkd3d_gpu_va_allocator_free(struct vkd3d_gpu_va_allocator *allocator,
        D3D12_GPU_VIRTUAL_ADDRESS address) DECLSPEC_HIDDEN;

struct vkd3d_render_pass_key
{
    unsigned int attachment_count;
    bool depth_enable;
    bool stencil_enable;
    bool depth_stencil_write;
    bool padding;
    unsigned int sample_count;
    VkFormat vk_formats[D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT + 1];
};

struct vkd3d_render_pass_entry;

struct vkd3d_render_pass_cache
{
    struct vkd3d_render_pass_entry *render_passes;
    size_t render_pass_count;
    size_t render_passes_size;
};

void vkd3d_render_pass_cache_cleanup(struct vkd3d_render_pass_cache *cache,
        struct d3d12_device *device) DECLSPEC_HIDDEN;
HRESULT vkd3d_render_pass_cache_find(struct vkd3d_render_pass_cache *cache,
        struct d3d12_device *device, const struct vkd3d_render_pass_key *key,
        VkRenderPass *vk_render_pass) DECLSPEC_HIDDEN;
void vkd3d_render_pass_cache_init(struct vkd3d_render_pass_cache *cache) DECLSPEC_HIDDEN;

struct vkd3d_private_store
{
    pthread_mutex_t mutex;

    struct list content;
};

struct vkd3d_private_data
{
    struct list entry;

    GUID tag;
    unsigned int size;
    bool is_object;
    union
    {
        BYTE data[1];
        IUnknown *object;
    } u;
};

static inline void vkd3d_private_data_destroy(struct vkd3d_private_data *data)
{
    if (data->is_object)
        IUnknown_Release(data->u.object);
    list_remove(&data->entry);
    vkd3d_free(data);
}

static inline HRESULT vkd3d_private_store_init(struct vkd3d_private_store *store)
{
    int rc;

    list_init(&store->content);

    if ((rc = pthread_mutex_init(&store->mutex, NULL)))
        ERR("Failed to initialize mutex, error %d.\n", rc);

    return hresult_from_errno(rc);
}

static inline void vkd3d_private_store_destroy(struct vkd3d_private_store *store)
{
    struct vkd3d_private_data *data, *cursor;

    LIST_FOR_EACH_ENTRY_SAFE(data, cursor, &store->content, struct vkd3d_private_data, entry)
    {
        vkd3d_private_data_destroy(data);
    }

    pthread_mutex_destroy(&store->mutex);
}

HRESULT vkd3d_get_private_data(struct vkd3d_private_store *store,
        const GUID *tag, unsigned int *out_size, void *out) DECLSPEC_HIDDEN;
HRESULT vkd3d_set_private_data(struct vkd3d_private_store *store,
        const GUID *tag, unsigned int data_size, const void *data) DECLSPEC_HIDDEN;
HRESULT vkd3d_set_private_data_interface(struct vkd3d_private_store *store,
        const GUID *tag, const IUnknown *object) DECLSPEC_HIDDEN;

struct vkd3d_signaled_semaphore
{
    struct list entry;
    uint64_t value;
    VkSemaphore vk_semaphore;
    VkFence vk_fence;
    bool is_acquired;
};

/* ID3D12Fence */
struct d3d12_fence
{
    ID3D12Fence ID3D12Fence_iface;
    LONG refcount;

    VkSemaphore timeline_semaphore;
    uint64_t pending_timeline_value;

    uint64_t value;
    pthread_mutex_t mutex;

    struct vkd3d_waiting_event
    {
        uint64_t value;
        HANDLE event;
    } *events;
    size_t events_size;
    size_t event_count;

    struct list semaphores;
    unsigned int semaphore_count;

    LONG pending_worker_operation_count;

    VkFence old_vk_fences[VKD3D_MAX_VK_SYNC_OBJECTS];

    struct d3d12_device *device;

    struct vkd3d_private_store private_store;
};

HRESULT d3d12_fence_create(struct d3d12_device *device,
        uint64_t initial_value, D3D12_FENCE_FLAGS flags, struct d3d12_fence **fence) DECLSPEC_HIDDEN;

/* ID3D12Heap */
struct d3d12_heap
{
    ID3D12Heap ID3D12Heap_iface;
    LONG refcount;

    bool is_private;
    D3D12_HEAP_DESC desc;

    pthread_mutex_t mutex;

    VkDeviceMemory vk_memory;
    void *map_ptr;
    unsigned int map_count;
    uint32_t vk_memory_type;

    struct d3d12_resource *buffer_resource;
    struct d3d12_device *device;

    struct vkd3d_private_store private_store;
};

HRESULT d3d12_heap_create(struct d3d12_device *device, const D3D12_HEAP_DESC *desc,
        const struct d3d12_resource *resource, struct d3d12_heap **heap) DECLSPEC_HIDDEN;
struct d3d12_heap *unsafe_impl_from_ID3D12Heap(ID3D12Heap *iface) DECLSPEC_HIDDEN;

#define VKD3D_RESOURCE_PUBLIC_FLAGS \
        (VKD3D_RESOURCE_INITIAL_STATE_TRANSITION | VKD3D_RESOURCE_PRESENT_STATE_TRANSITION)
#define VKD3D_RESOURCE_EXTERNAL       0x00000004
#define VKD3D_RESOURCE_DEDICATED_HEAP 0x00000008
#define VKD3D_RESOURCE_LINEAR_TILING  0x00000010
#define VKD3D_RESOURCE_PLACED_BUFFER  0x00000020

/* ID3D12Resource */
struct d3d12_resource
{
    ID3D12Resource ID3D12Resource_iface;
    LONG refcount;
    LONG internal_refcount;

    D3D12_RESOURCE_DESC desc;

    D3D12_GPU_VIRTUAL_ADDRESS gpu_address;
    union
    {
        VkBuffer vk_buffer;
        VkImage vk_image;
    } u;
    unsigned int flags;

    unsigned int map_count;

    struct d3d12_heap *heap;
    uint64_t heap_offset;

    D3D12_RESOURCE_STATES initial_state;
    D3D12_RESOURCE_STATES present_state;

    struct d3d12_device *device;

    struct vkd3d_private_store private_store;
};

static inline bool d3d12_resource_is_buffer(const struct d3d12_resource *resource)
{
    return resource->desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER;
}

static inline bool d3d12_resource_is_texture(const struct d3d12_resource *resource)
{
    return resource->desc.Dimension != D3D12_RESOURCE_DIMENSION_BUFFER;
}

bool d3d12_resource_is_cpu_accessible(const struct d3d12_resource *resource) DECLSPEC_HIDDEN;
HRESULT d3d12_resource_validate_desc(const D3D12_RESOURCE_DESC *desc, struct d3d12_device *device) DECLSPEC_HIDDEN;

HRESULT d3d12_committed_resource_create(struct d3d12_device *device,
        const D3D12_HEAP_PROPERTIES *heap_properties, D3D12_HEAP_FLAGS heap_flags,
        const D3D12_RESOURCE_DESC *desc, D3D12_RESOURCE_STATES initial_state,
        const D3D12_CLEAR_VALUE *optimized_clear_value, struct d3d12_resource **resource) DECLSPEC_HIDDEN;
HRESULT d3d12_placed_resource_create(struct d3d12_device *device, struct d3d12_heap *heap, uint64_t heap_offset,
        const D3D12_RESOURCE_DESC *desc, D3D12_RESOURCE_STATES initial_state,
        const D3D12_CLEAR_VALUE *optimized_clear_value, struct d3d12_resource **resource) DECLSPEC_HIDDEN;
HRESULT d3d12_reserved_resource_create(struct d3d12_device *device,
        const D3D12_RESOURCE_DESC *desc, D3D12_RESOURCE_STATES initial_state,
        const D3D12_CLEAR_VALUE *optimized_clear_value, struct d3d12_resource **resource) DECLSPEC_HIDDEN;
struct d3d12_resource *unsafe_impl_from_ID3D12Resource(ID3D12Resource *iface) DECLSPEC_HIDDEN;

HRESULT vkd3d_allocate_buffer_memory(struct d3d12_device *device, VkBuffer vk_buffer,
        const D3D12_HEAP_PROPERTIES *heap_properties, D3D12_HEAP_FLAGS heap_flags,
        VkDeviceMemory *vk_memory, uint32_t *vk_memory_type, VkDeviceSize *vk_memory_size) DECLSPEC_HIDDEN;
HRESULT vkd3d_create_buffer(struct d3d12_device *device,
        const D3D12_HEAP_PROPERTIES *heap_properties, D3D12_HEAP_FLAGS heap_flags,
        const D3D12_RESOURCE_DESC *desc, VkBuffer *vk_buffer) DECLSPEC_HIDDEN;
HRESULT vkd3d_get_image_allocation_info(struct d3d12_device *device,
        const D3D12_RESOURCE_DESC *desc, D3D12_RESOURCE_ALLOCATION_INFO *allocation_info) DECLSPEC_HIDDEN;

enum vkd3d_view_type
{
    VKD3D_VIEW_TYPE_BUFFER,
    VKD3D_VIEW_TYPE_IMAGE,
    VKD3D_VIEW_TYPE_SAMPLER,
};

struct vkd3d_view
{
    LONG refcount;
    enum vkd3d_view_type type;
    union
    {
        VkBufferView vk_buffer_view;
        VkImageView vk_image_view;
        VkSampler vk_sampler;
    } u;
    VkBufferView vk_counter_view;
    VkDeviceAddress vk_counter_address;
    const struct vkd3d_format *format;
    union
    {
        struct
        {
            VkDeviceSize offset;
            VkDeviceSize size;
        } buffer;
        struct
        {
            VkImageViewType vk_view_type;
            unsigned int miplevel_idx;
            unsigned int layer_idx;
            unsigned int layer_count;
        } texture;
    } info;
};

void vkd3d_view_decref(struct vkd3d_view *view, struct d3d12_device *device) DECLSPEC_HIDDEN;
void vkd3d_view_destroy(struct vkd3d_view *view, struct d3d12_device *device) DECLSPEC_HIDDEN;
void vkd3d_view_incref(struct vkd3d_view *view) DECLSPEC_HIDDEN;

struct vkd3d_texture_view_desc
{
    VkImageViewType view_type;
    const struct vkd3d_format *format;
    unsigned int miplevel_idx;
    unsigned int miplevel_count;
    unsigned int layer_idx;
    unsigned int layer_count;
    VkComponentMapping components;
    bool allowed_swizzle;
};

bool vkd3d_create_buffer_view(struct d3d12_device *device, VkBuffer vk_buffer, const struct vkd3d_format *format,
        VkDeviceSize offset, VkDeviceSize size, struct vkd3d_view *view) DECLSPEC_HIDDEN;
bool vkd3d_create_texture_view(struct d3d12_device *device, VkImage vk_image,
        const struct vkd3d_texture_view_desc *desc, struct vkd3d_view *view) DECLSPEC_HIDDEN;

union vkd3d_descriptor_info
{
    VkBufferView buffer_view;
    VkDescriptorBufferInfo buffer;
    VkDescriptorImageInfo image;
};

struct d3d12_desc
{
    struct d3d12_descriptor_heap *heap;
    VkDeviceAddress uav_counter_address;
    struct vkd3d_view view;
    uint32_t heap_offset;
};

static inline struct d3d12_desc *d3d12_desc_from_cpu_handle(D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle)
{
    return (struct d3d12_desc *)cpu_handle.ptr;
}

static inline struct d3d12_desc *d3d12_desc_from_gpu_handle(D3D12_GPU_DESCRIPTOR_HANDLE gpu_handle)
{
    return (struct d3d12_desc *)(intptr_t)gpu_handle.ptr;
}

void d3d12_desc_copy(struct d3d12_desc *dst, struct d3d12_desc *src,
        struct d3d12_device *device) DECLSPEC_HIDDEN;
void d3d12_desc_create_cbv(struct d3d12_desc *descriptor,
        struct d3d12_device *device, const D3D12_CONSTANT_BUFFER_VIEW_DESC *desc) DECLSPEC_HIDDEN;
void d3d12_desc_create_srv(struct d3d12_desc *descriptor,
        struct d3d12_device *device, struct d3d12_resource *resource,
        const D3D12_SHADER_RESOURCE_VIEW_DESC *desc) DECLSPEC_HIDDEN;
void d3d12_desc_create_uav(struct d3d12_desc *descriptor, struct d3d12_device *device,
        struct d3d12_resource *resource, struct d3d12_resource *counter_resource,
        const D3D12_UNORDERED_ACCESS_VIEW_DESC *desc) DECLSPEC_HIDDEN;
void d3d12_desc_create_sampler(struct d3d12_desc *sampler,
        struct d3d12_device *device, const D3D12_SAMPLER_DESC *desc) DECLSPEC_HIDDEN;
void d3d12_desc_write_atomic(struct d3d12_desc *dst, const struct d3d12_desc *src,
        struct d3d12_device *device) DECLSPEC_HIDDEN;

bool vkd3d_create_raw_buffer_view(struct d3d12_device *device,
        D3D12_GPU_VIRTUAL_ADDRESS gpu_address, VkBufferView *vk_buffer_view) DECLSPEC_HIDDEN;
HRESULT vkd3d_create_static_sampler(struct d3d12_device *device,
        const D3D12_STATIC_SAMPLER_DESC *desc, VkSampler *vk_sampler) DECLSPEC_HIDDEN;

struct d3d12_rtv_desc
{
    uint32_t magic;
    VkSampleCountFlagBits sample_count;
    const struct vkd3d_format *format;
    uint64_t width;
    unsigned int height;
    unsigned int layer_count;
    struct vkd3d_view *view;
    struct d3d12_resource *resource;
};

static inline struct d3d12_rtv_desc *d3d12_rtv_desc_from_cpu_handle(D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle)
{
    return (struct d3d12_rtv_desc *)cpu_handle.ptr;
}

void d3d12_rtv_desc_create_rtv(struct d3d12_rtv_desc *rtv_desc, struct d3d12_device *device,
        struct d3d12_resource *resource, const D3D12_RENDER_TARGET_VIEW_DESC *desc) DECLSPEC_HIDDEN;

struct d3d12_dsv_desc
{
    uint32_t magic;
    VkSampleCountFlagBits sample_count;
    const struct vkd3d_format *format;
    uint64_t width;
    unsigned int height;
    unsigned int layer_count;
    struct vkd3d_view *view;
    struct d3d12_resource *resource;
};

static inline struct d3d12_dsv_desc *d3d12_dsv_desc_from_cpu_handle(D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle)
{
    return (struct d3d12_dsv_desc *)cpu_handle.ptr;
}

void d3d12_dsv_desc_create_dsv(struct d3d12_dsv_desc *dsv_desc, struct d3d12_device *device,
        struct d3d12_resource *resource, const D3D12_DEPTH_STENCIL_VIEW_DESC *desc) DECLSPEC_HIDDEN;

struct d3d12_descriptor_heap_uav_counters
{
    VkDeviceAddress *data;
    VkDeviceMemory vk_memory;
    VkBuffer vk_buffer;
};

/* ID3D12DescriptorHeap */
struct d3d12_descriptor_heap
{
    ID3D12DescriptorHeap ID3D12DescriptorHeap_iface;
    LONG refcount;

    D3D12_DESCRIPTOR_HEAP_DESC desc;

    VkDescriptorPool vk_descriptor_pool;
    VkDescriptorSet vk_descriptor_sets[VKD3D_MAX_BINDLESS_DESCRIPTOR_SETS];

    struct d3d12_descriptor_heap_uav_counters uav_counters;
    struct d3d12_device *device;

    struct vkd3d_private_store private_store;

    BYTE descriptors[];
};

HRESULT d3d12_descriptor_heap_create(struct d3d12_device *device,
        const D3D12_DESCRIPTOR_HEAP_DESC *desc, struct d3d12_descriptor_heap **descriptor_heap) DECLSPEC_HIDDEN;
void d3d12_descriptor_heap_cleanup(struct d3d12_descriptor_heap *descriptor_heap) DECLSPEC_HIDDEN;
struct d3d12_descriptor_heap *unsafe_impl_from_ID3D12DescriptorHeap(ID3D12DescriptorHeap *iface) DECLSPEC_HIDDEN;

static inline unsigned int d3d12_descriptor_heap_sampler_set_index()
{
    return 0;
}

static inline unsigned int d3d12_descriptor_heap_cbv_set_index()
{
    return 0;
}

static inline unsigned int d3d12_descriptor_heap_srv_set_index(bool is_buffer)
{
    return 0;
}

static inline unsigned int d3d12_descriptor_heap_uav_set_index(bool is_buffer)
{
    return 0;
}

static inline uint32_t d3d12_desc_heap_offset(const struct d3d12_desc *dst)
{
    return dst->heap_offset;
}

unsigned int d3d12_descriptor_heap_set_index_from_binding(const struct vkd3d_bindless_set_info *set) DECLSPEC_HIDDEN;
unsigned int d3d12_descriptor_heap_set_index_from_magic(uint32_t magic) DECLSPEC_HIDDEN;
unsigned int d3d12_descriptor_heap_set_index_from_vk_descriptor_type(VkDescriptorType type) DECLSPEC_HIDDEN;

/* ID3D12QueryHeap */
struct d3d12_query_heap
{
    ID3D12QueryHeap ID3D12QueryHeap_iface;
    LONG refcount;

    VkQueryPool vk_query_pool;

    struct d3d12_device *device;

    struct vkd3d_private_store private_store;

    uint64_t availability_mask[];
};

HRESULT d3d12_query_heap_create(struct d3d12_device *device, const D3D12_QUERY_HEAP_DESC *desc,
        struct d3d12_query_heap **heap) DECLSPEC_HIDDEN;
struct d3d12_query_heap *unsafe_impl_from_ID3D12QueryHeap(ID3D12QueryHeap *iface) DECLSPEC_HIDDEN;

/* A Vulkan query has to be issued at least one time before the result is
 * available. In D3D12 it is legal to get query reults for not issued queries.
 */
static inline bool d3d12_query_heap_is_result_available(const struct d3d12_query_heap *heap,
        unsigned int query_index)
{
    unsigned int index = query_index / (sizeof(*heap->availability_mask) * CHAR_BIT);
    unsigned int shift = query_index % (sizeof(*heap->availability_mask) * CHAR_BIT);
    return heap->availability_mask[index] & ((uint64_t)1 << shift);
}

static inline void d3d12_query_heap_mark_result_as_available(struct d3d12_query_heap *heap,
        unsigned int query_index)
{
    unsigned int index = query_index / (sizeof(*heap->availability_mask) * CHAR_BIT);
    unsigned int shift = query_index % (sizeof(*heap->availability_mask) * CHAR_BIT);
    heap->availability_mask[index] |= (uint64_t)1 << shift;
}

enum vkd3d_root_descriptor_table_flag
{
    VKD3D_ROOT_DESCRIPTOR_TABLE_HAS_PACKED_DESCRIPTORS = 0x00000001u,
};

enum vkd3d_root_signature_flag
{
    VKD3D_ROOT_SIGNATURE_USE_PUSH_DESCRIPTORS       = 0x00000001u,
    VKD3D_ROOT_SIGNATURE_USE_INLINE_UNIFORM_BLOCK   = 0x00000002u,
    VKD3D_ROOT_SIGNATURE_USE_BINDLESS_UAV_COUNTERS  = 0x00000004u,
};

struct d3d12_root_descriptor_table
{
    uint32_t table_index;
    uint32_t binding_count;
    uint32_t first_packed_descriptor;
    uint32_t flags; /* vkd3d_root_descriptor_table_flag */
    struct vkd3d_shader_resource_binding *first_binding;
};

struct d3d12_root_constant
{
    uint32_t constant_index;
    uint32_t constant_count;
};

struct d3d12_root_descriptor
{
    struct vkd3d_shader_resource_binding *binding;
    uint32_t packed_descriptor;
};

struct d3d12_root_parameter
{
    D3D12_ROOT_PARAMETER_TYPE parameter_type;
    union
    {
        struct d3d12_root_constant constant;
        struct d3d12_root_descriptor descriptor;
        struct d3d12_root_descriptor_table descriptor_table;
    } u;
};

/* ID3D12RootSignature */
struct d3d12_root_signature
{
    ID3D12RootSignature ID3D12RootSignature_iface;
    LONG refcount;

    VkPipelineLayout vk_pipeline_layout;
    VkDescriptorSetLayout vk_sampler_descriptor_layout;
    VkDescriptorSetLayout vk_packed_descriptor_layout;
    VkDescriptorSetLayout vk_root_descriptor_layout;

    struct d3d12_root_parameter *parameters;
    unsigned int parameter_count;

    uint32_t sampler_descriptor_set;
    uint32_t packed_descriptor_set;
    uint32_t root_descriptor_set;

    uint64_t descriptor_table_mask;
    uint64_t root_constant_mask;
    uint64_t root_descriptor_mask;

    D3D12_ROOT_SIGNATURE_FLAGS d3d12_flags;
    unsigned int flags; /* vkd3d_root_signature_flag */

    unsigned int binding_count;
    struct vkd3d_shader_resource_binding *bindings;

    unsigned int root_constant_count;
    struct vkd3d_shader_push_constant_buffer *root_constants;

    unsigned int packed_descriptor_count;

    /* Use one global push constant range */
    VkPushConstantRange push_constant_range;
    struct vkd3d_shader_descriptor_binding push_constant_ubo_binding;
    struct vkd3d_shader_descriptor_binding uav_counter_binding;

    uint32_t descriptor_table_offset;
    uint32_t descriptor_table_count;

    unsigned int static_sampler_count;
    VkSampler *static_samplers;

    struct d3d12_device *device;

    struct vkd3d_private_store private_store;
};

HRESULT d3d12_root_signature_create(struct d3d12_device *device, const void *bytecode,
        size_t bytecode_length, struct d3d12_root_signature **root_signature) DECLSPEC_HIDDEN;
struct d3d12_root_signature *unsafe_impl_from_ID3D12RootSignature(ID3D12RootSignature *iface) DECLSPEC_HIDDEN;

int vkd3d_parse_root_signature_v_1_0(const struct vkd3d_shader_code *dxbc,
        struct vkd3d_versioned_root_signature_desc *desc) DECLSPEC_HIDDEN;

#define VKD3D_MAX_DYNAMIC_STATE_COUNT (4)

enum vkd3d_dynamic_state_flag
{
    VKD3D_DYNAMIC_STATE_VIEWPORT          = (1 << 0),
    VKD3D_DYNAMIC_STATE_SCISSOR           = (1 << 1),
    VKD3D_DYNAMIC_STATE_BLEND_CONSTANTS   = (1 << 2),
    VKD3D_DYNAMIC_STATE_STENCIL_REFERENCE = (1 << 3),
};

struct d3d12_graphics_pipeline_state
{
    VkPipelineShaderStageCreateInfo stages[VKD3D_MAX_SHADER_STAGES];
    size_t stage_count;

    VkVertexInputAttributeDescription attributes[D3D12_VS_INPUT_REGISTER_COUNT];
    VkVertexInputRate input_rates[D3D12_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT];
    VkVertexInputBindingDivisorDescriptionEXT instance_divisors[D3D12_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT];
    size_t instance_divisor_count;
    size_t attribute_count;

    VkPipelineColorBlendAttachmentState blend_attachments[D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT];
    unsigned int rt_count;
    unsigned int null_attachment_mask;
    VkFormat dsv_format;
    VkFormat rtv_formats[D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT];
    VkRenderPass render_pass;

    D3D12_INDEX_BUFFER_STRIP_CUT_VALUE index_buffer_strip_cut_value;
    VkPipelineRasterizationStateCreateInfo rs_desc;
    VkPipelineMultisampleStateCreateInfo ms_desc;
    VkPipelineDepthStencilStateCreateInfo ds_desc;
    VkPipelineColorBlendStateCreateInfo blend_desc;

    VkSampleMask sample_mask[2];
    VkPipelineRasterizationDepthClipStateCreateInfoEXT rs_depth_clip_info;
    VkPipelineRasterizationStateStreamCreateInfoEXT rs_stream_info;

    VkDynamicState dynamic_states[VKD3D_MAX_DYNAMIC_STATE_COUNT];
    VkPipelineDynamicStateCreateInfo dynamic_desc;

    uint32_t dynamic_state_flags; /* vkd3d_dynamic_state_flag */

    const struct d3d12_root_signature *root_signature;

    struct list compiled_pipelines;

    bool xfb_enabled;
};

static inline unsigned int dsv_attachment_mask(const struct d3d12_graphics_pipeline_state *graphics)
{
    return 1u << graphics->rt_count;
}

struct d3d12_compute_pipeline_state
{
    VkPipeline vk_pipeline;
};

/* ID3D12PipelineState */
struct d3d12_pipeline_state
{
    ID3D12PipelineState ID3D12PipelineState_iface;
    LONG refcount;

    union
    {
        struct d3d12_graphics_pipeline_state graphics;
        struct d3d12_compute_pipeline_state compute;
    } u;
    VkPipelineBindPoint vk_bind_point;

    struct d3d12_device *device;

    struct vkd3d_private_store private_store;
};

static inline bool d3d12_pipeline_state_is_compute(const struct d3d12_pipeline_state *state)
{
    return state && state->vk_bind_point == VK_PIPELINE_BIND_POINT_COMPUTE;
}

static inline bool d3d12_pipeline_state_is_graphics(const struct d3d12_pipeline_state *state)
{
    return state && state->vk_bind_point == VK_PIPELINE_BIND_POINT_GRAPHICS;
}

static inline bool d3d12_pipeline_state_has_unknown_dsv_format(struct d3d12_pipeline_state *state)
{
    if (d3d12_pipeline_state_is_graphics(state))
    {
        struct d3d12_graphics_pipeline_state *graphics = &state->u.graphics;

        return graphics->null_attachment_mask & dsv_attachment_mask(graphics);
    }

    return false;
}

struct d3d12_pipeline_state_desc
{
    ID3D12RootSignature *root_signature;
    D3D12_SHADER_BYTECODE vs;
    D3D12_SHADER_BYTECODE ps;
    D3D12_SHADER_BYTECODE ds;
    D3D12_SHADER_BYTECODE hs;
    D3D12_SHADER_BYTECODE gs;
    D3D12_SHADER_BYTECODE cs;
    D3D12_STREAM_OUTPUT_DESC stream_output;
    D3D12_BLEND_DESC blend_state;
    UINT sample_mask;
    D3D12_RASTERIZER_DESC rasterizer_state;
    D3D12_DEPTH_STENCIL_DESC1 depth_stencil_state;
    D3D12_INPUT_LAYOUT_DESC input_layout;
    D3D12_INDEX_BUFFER_STRIP_CUT_VALUE strip_cut_value;
    D3D12_PRIMITIVE_TOPOLOGY_TYPE primitive_topology_type;
    D3D12_RT_FORMAT_ARRAY rtv_formats;
    DXGI_FORMAT dsv_format;
    DXGI_SAMPLE_DESC sample_desc;
    D3D12_VIEW_INSTANCING_DESC view_instancing_desc;
    UINT node_mask;
    D3D12_CACHED_PIPELINE_STATE cached_pso;
    D3D12_PIPELINE_STATE_FLAGS flags;
};

HRESULT d3d12_pipeline_state_create(struct d3d12_device *device, VkPipelineBindPoint bind_point,
        const struct d3d12_pipeline_state_desc *desc, struct d3d12_pipeline_state **state) DECLSPEC_HIDDEN;
VkPipeline d3d12_pipeline_state_get_or_create_pipeline(struct d3d12_pipeline_state *state,
        D3D12_PRIMITIVE_TOPOLOGY topology, const uint32_t *strides, VkFormat dsv_format,
        VkRenderPass *vk_render_pass) DECLSPEC_HIDDEN;
struct d3d12_pipeline_state *unsafe_impl_from_ID3D12PipelineState(ID3D12PipelineState *iface) DECLSPEC_HIDDEN;

struct vkd3d_buffer
{
    VkBuffer vk_buffer;
    VkDeviceMemory vk_memory;
};

struct d3d12_descriptor_pool_cache
{
    VkDescriptorPool vk_descriptor_pool;
    VkDescriptorPool *free_descriptor_pools;
    size_t free_descriptor_pools_size;
    size_t free_descriptor_pool_count;

    VkDescriptorPool *descriptor_pools;
    size_t descriptor_pools_size;
    size_t descriptor_pool_count;
};

enum vkd3d_descriptor_pool_types
{
    VKD3D_DESCRIPTOR_POOL_TYPE_STATIC = 0,
    VKD3D_DESCRIPTOR_POOL_TYPE_VOLATILE,
    VKD3D_DESCRIPTOR_POOL_TYPE_IMMUTABLE_SAMPLER,
    VKD3D_DESCRIPTOR_POOL_TYPE_COUNT
};

/* ID3D12CommandAllocator */
struct d3d12_command_allocator
{
    ID3D12CommandAllocator ID3D12CommandAllocator_iface;
    LONG refcount;

    D3D12_COMMAND_LIST_TYPE type;
    VkQueueFlags vk_queue_flags;

    VkCommandPool vk_command_pool;

    struct d3d12_descriptor_pool_cache descriptor_pool_caches[VKD3D_DESCRIPTOR_POOL_TYPE_COUNT];

    VkRenderPass *passes;
    size_t passes_size;
    size_t pass_count;

    VkFramebuffer *framebuffers;
    size_t framebuffers_size;
    size_t framebuffer_count;

    struct vkd3d_view **views;
    size_t views_size;
    size_t view_count;

    VkBufferView *buffer_views;
    size_t buffer_views_size;
    size_t buffer_view_count;

    struct vkd3d_buffer *transfer_buffers;
    size_t transfer_buffers_size;
    size_t transfer_buffer_count;

    VkCommandBuffer *command_buffers;
    size_t command_buffers_size;
    size_t command_buffer_count;

    struct d3d12_command_list *current_command_list;
    struct d3d12_device *device;

    struct vkd3d_private_store private_store;
};

HRESULT d3d12_command_allocator_create(struct d3d12_device *device,
        D3D12_COMMAND_LIST_TYPE type, struct d3d12_command_allocator **allocator) DECLSPEC_HIDDEN;

enum vkd3d_pipeline_dirty_flag
{
    VKD3D_PIPELINE_DIRTY_STATIC_SAMPLER_SET       = 0x00000001u,
    VKD3D_PIPELINE_DIRTY_PACKED_DESCRIPTOR_SET    = 0x00000002u,
    VKD3D_PIPELINE_DIRTY_DESCRIPTOR_TABLE_OFFSETS = 0x00000004u,
    VKD3D_PIPELINE_DIRTY_UAV_COUNTER_BINDING      = 0x00000008u,
};

struct vkd3d_descriptor_updates
{
    VkWriteDescriptorSet *descriptor_writes;
    size_t descriptor_writes_size;

    union vkd3d_descriptor_info *descriptors;
    size_t descriptors_size;
};

struct vkd3d_pipeline_bindings
{
    const struct d3d12_root_signature *root_signature;

    VkDescriptorSet static_sampler_set;
    uint32_t dirty_flags; /* vkd3d_pipeline_dirty_flags */

    D3D12_GPU_DESCRIPTOR_HANDLE descriptor_tables[D3D12_MAX_ROOT_COST];
    uint64_t descriptor_table_active_mask;
    uint64_t descriptor_heap_dirty_mask;

    /* Needed when VK_KHR_push_descriptor is not available. */
    union vkd3d_descriptor_info root_descriptors[D3D12_MAX_ROOT_COST / 2];
    uint64_t root_descriptor_dirty_mask;
    uint64_t root_descriptor_active_mask;

    uint32_t root_constants[D3D12_MAX_ROOT_COST];
    uint64_t root_constant_dirty_mask;
};

struct d3d12_deferred_descriptor_set_update
{
    VkDescriptorSet descriptor_set;
    const struct d3d12_root_signature *root_signature;
    const struct d3d12_desc *base_descriptor;
    struct vkd3d_descriptor_updates *updates;
    unsigned int root_parameter_index;
};

struct vkd3d_dynamic_state
{
    uint32_t dirty_flags; /* vkd3d_dynamic_state_flags */

    uint32_t viewport_count;
    VkViewport viewports[D3D12_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
    VkRect2D scissors[D3D12_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];

    float blend_constants[4];
    uint32_t stencil_reference;
};

/* ID3D12CommandList */
typedef ID3D12GraphicsCommandList2 d3d12_command_list_iface;

struct d3d12_command_list
{
    d3d12_command_list_iface ID3D12GraphicsCommandList_iface;
    LONG refcount;

    D3D12_COMMAND_LIST_TYPE type;
    VkQueueFlags vk_queue_flags;

    bool is_recording;
    bool is_valid;
    VkCommandBuffer vk_command_buffer;

    uint32_t strides[D3D12_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT];
    D3D12_PRIMITIVE_TOPOLOGY primitive_topology;

    DXGI_FORMAT index_buffer_format;

    VkImageView rtvs[D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT];
    VkImageView dsv;
    unsigned int fb_width;
    unsigned int fb_height;
    unsigned int fb_layer_count;
    VkFormat dsv_format;

    bool xfb_enabled;

    bool is_predicated;

    VkFramebuffer current_framebuffer;
    VkPipeline current_pipeline;
    VkRenderPass pso_render_pass;
    VkRenderPass current_render_pass;
    VkBuffer uav_counter_address_buffer;
    struct vkd3d_dynamic_state dynamic_state;
    struct vkd3d_pipeline_bindings pipeline_bindings[VK_PIPELINE_BIND_POINT_RANGE_SIZE];
    struct vkd3d_descriptor_updates packed_descriptors[VK_PIPELINE_BIND_POINT_RANGE_SIZE];

    VkDescriptorSet descriptor_heaps[VKD3D_MAX_BINDLESS_DESCRIPTOR_SETS];

    struct d3d12_pipeline_state *state;

    struct d3d12_command_allocator *allocator;
    struct d3d12_device *device;

    VkBuffer so_counter_buffers[D3D12_SO_BUFFER_SLOT_COUNT];
    VkDeviceSize so_counter_buffer_offsets[D3D12_SO_BUFFER_SLOT_COUNT];

    struct d3d12_deferred_descriptor_set_update *descriptor_updates;
    size_t descriptor_updates_size;
    size_t descriptor_updates_count;

    struct vkd3d_private_store private_store;
};

HRESULT d3d12_command_list_create(struct d3d12_device *device,
        UINT node_mask, D3D12_COMMAND_LIST_TYPE type, ID3D12CommandAllocator *allocator_iface,
        ID3D12PipelineState *initial_pipeline_state, struct d3d12_command_list **list) DECLSPEC_HIDDEN;

struct vkd3d_queue
{
    /* Access to VkQueue must be externally synchronized. */
    pthread_mutex_t mutex;

    VkQueue vk_queue;

    uint64_t completed_sequence_number;
    uint64_t submitted_sequence_number;

    uint32_t vk_family_index;
    VkQueueFlags vk_queue_flags;
    uint32_t timestamp_bits;

    struct
    {
        VkSemaphore vk_semaphore;
        uint64_t sequence_number;
    } *semaphores;
    size_t semaphores_size;
    size_t semaphore_count;

    VkSemaphore old_vk_semaphores[VKD3D_MAX_VK_SYNC_OBJECTS];
};

VkQueue vkd3d_queue_acquire(struct vkd3d_queue *queue) DECLSPEC_HIDDEN;
HRESULT vkd3d_queue_create(struct d3d12_device *device,
        uint32_t family_index, const VkQueueFamilyProperties *properties,
        struct vkd3d_queue **queue) DECLSPEC_HIDDEN;
void vkd3d_queue_destroy(struct vkd3d_queue *queue, struct d3d12_device *device) DECLSPEC_HIDDEN;
void vkd3d_queue_release(struct vkd3d_queue *queue) DECLSPEC_HIDDEN;

/* ID3D12CommandQueue */
struct d3d12_command_queue
{
    ID3D12CommandQueue ID3D12CommandQueue_iface;
    LONG refcount;

    D3D12_COMMAND_QUEUE_DESC desc;

    struct vkd3d_queue *vkd3d_queue;

    const struct d3d12_fence *last_waited_fence;
    uint64_t last_waited_fence_value;

    struct d3d12_device *device;

    struct vkd3d_private_store private_store;
};

HRESULT d3d12_command_queue_create(struct d3d12_device *device,
        const D3D12_COMMAND_QUEUE_DESC *desc, struct d3d12_command_queue **queue) DECLSPEC_HIDDEN;

/* ID3D12CommandSignature */
struct d3d12_command_signature
{
    ID3D12CommandSignature ID3D12CommandSignature_iface;
    LONG refcount;

    D3D12_COMMAND_SIGNATURE_DESC desc;

    struct d3d12_device *device;

    struct vkd3d_private_store private_store;
};

HRESULT d3d12_command_signature_create(struct d3d12_device *device, const D3D12_COMMAND_SIGNATURE_DESC *desc,
        struct d3d12_command_signature **signature) DECLSPEC_HIDDEN;
struct d3d12_command_signature *unsafe_impl_from_ID3D12CommandSignature(ID3D12CommandSignature *iface) DECLSPEC_HIDDEN;

/* NULL resources */
struct vkd3d_null_resources
{
    VkBuffer vk_buffer;
    VkBufferView vk_buffer_view;
    VkDeviceMemory vk_buffer_memory;

    VkBuffer vk_storage_buffer;
    VkBufferView vk_storage_buffer_view;
    VkDeviceMemory vk_storage_buffer_memory;

    VkImage vk_2d_image;
    VkImageView vk_2d_image_view;
    VkDeviceMemory vk_2d_image_memory;

    VkImage vk_2d_storage_image;
    VkImageView vk_2d_storage_image_view;
    VkDeviceMemory vk_2d_storage_image_memory;

    VkSampler vk_sampler;
};

HRESULT vkd3d_init_null_resources(struct vkd3d_null_resources *null_resources,
        struct d3d12_device *device) DECLSPEC_HIDDEN;
void vkd3d_destroy_null_resources(struct vkd3d_null_resources *null_resources,
        struct d3d12_device *device) DECLSPEC_HIDDEN;

/* Bindless */
enum vkd3d_bindless_flags
{
    VKD3D_BINDLESS_SAMPLER      = (1u << 0),
    VKD3D_BINDLESS_CBV          = (1u << 1),
    VKD3D_BINDLESS_SRV          = (1u << 2),
    VKD3D_BINDLESS_UAV          = (1u << 3),
    VKD3D_BINDLESS_UAV_COUNTER  = (1u << 4),
    VKD3D_BINDLESS_CBV_AS_SSBO  = (1u << 5),
};

struct vkd3d_bindless_set_info
{
    VkDescriptorType vk_descriptor_type;
    D3D12_DESCRIPTOR_HEAP_TYPE heap_type;
    D3D12_DESCRIPTOR_RANGE_TYPE range_type;
    enum vkd3d_shader_binding_flag binding_flag;

    VkDescriptorSetLayout vk_set_layout;
};

struct vkd3d_bindless_state
{
    uint32_t flags; /* vkd3d_bindless_flags */

    struct vkd3d_bindless_set_info set_info[VKD3D_MAX_BINDLESS_DESCRIPTOR_SETS];
    unsigned int set_count;
};

HRESULT vkd3d_bindless_state_init(struct vkd3d_bindless_state *bindless_state,
        struct d3d12_device *device) DECLSPEC_HIDDEN;
void vkd3d_bindless_state_cleanup(struct vkd3d_bindless_state *bindless_state,
        struct d3d12_device *device) DECLSPEC_HIDDEN;
bool vkd3d_bindless_state_find_binding(const struct vkd3d_bindless_state *bindless_state,
        D3D12_DESCRIPTOR_RANGE_TYPE range_type, enum vkd3d_shader_binding_flag binding_flag,
        struct vkd3d_shader_descriptor_binding *binding) DECLSPEC_HIDDEN;

static inline VkDescriptorType vkd3d_bindless_state_get_cbv_descriptor_type(const struct vkd3d_bindless_state *bindless_state)
{
    return bindless_state->flags & VKD3D_BINDLESS_CBV_AS_SSBO
            ? VK_DESCRIPTOR_TYPE_STORAGE_BUFFER
            : VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
}

struct vkd3d_format_compatibility_list
{
    DXGI_FORMAT typeless_format;
    unsigned int format_count;
    VkFormat vk_formats[VKD3D_MAX_COMPATIBLE_FORMAT_COUNT];
};

struct vkd3d_uav_clear_args
{
    VkClearColorValue colour;
    VkOffset2D offset;
    VkExtent2D extent;
};

struct vkd3d_uav_clear_pipelines
{
    VkPipeline buffer;
    VkPipeline image_1d;
    VkPipeline image_1d_array;
    VkPipeline image_2d;
    VkPipeline image_2d_array;
    VkPipeline image_3d;
};

struct vkd3d_uav_clear_state
{
    VkDescriptorSetLayout vk_set_layout_buffer;
    VkDescriptorSetLayout vk_set_layout_image;

    VkPipelineLayout vk_pipeline_layout_buffer;
    VkPipelineLayout vk_pipeline_layout_image;

    struct vkd3d_uav_clear_pipelines pipelines_float;
    struct vkd3d_uav_clear_pipelines pipelines_uint;
};

HRESULT vkd3d_uav_clear_state_init(struct vkd3d_uav_clear_state *state, struct d3d12_device *device) DECLSPEC_HIDDEN;
void vkd3d_uav_clear_state_cleanup(struct vkd3d_uav_clear_state *state, struct d3d12_device *device) DECLSPEC_HIDDEN;

struct vkd3d_physical_device_info
{
    /* properties */
    VkPhysicalDeviceDescriptorIndexingPropertiesEXT descriptor_indexing_properties;
    VkPhysicalDeviceInlineUniformBlockPropertiesEXT inline_uniform_block_properties;
    VkPhysicalDevicePushDescriptorPropertiesKHR push_descriptor_properties;
    VkPhysicalDeviceMaintenance3Properties maintenance3_properties;
    VkPhysicalDeviceTexelBufferAlignmentPropertiesEXT texel_buffer_alignment_properties;
    VkPhysicalDeviceTransformFeedbackPropertiesEXT xfb_properties;
    VkPhysicalDeviceVertexAttributeDivisorPropertiesEXT vertex_divisor_properties;
    VkPhysicalDeviceSubgroupProperties subgroup_properties;
    VkPhysicalDeviceTimelineSemaphorePropertiesKHR timeline_semaphore_properties;
    VkPhysicalDeviceSubgroupSizeControlPropertiesEXT subgroup_size_control_properties;
    VkPhysicalDeviceShaderCorePropertiesAMD shader_core_properties;
    VkPhysicalDeviceShaderCoreProperties2AMD shader_core_properties2;
    VkPhysicalDeviceShaderSMBuiltinsPropertiesNV shader_sm_builtins_properties;

    VkPhysicalDeviceProperties2KHR properties2;

    /* features */
    VkPhysicalDeviceBufferDeviceAddressFeaturesKHR buffer_device_address_features;
    VkPhysicalDeviceConditionalRenderingFeaturesEXT conditional_rendering_features;
    VkPhysicalDeviceDepthClipEnableFeaturesEXT depth_clip_features;
    VkPhysicalDeviceDescriptorIndexingFeaturesEXT descriptor_indexing_features;
    VkPhysicalDeviceShaderDemoteToHelperInvocationFeaturesEXT demote_features;
    VkPhysicalDeviceInlineUniformBlockFeaturesEXT inline_uniform_block_features;
    VkPhysicalDeviceTexelBufferAlignmentFeaturesEXT texel_buffer_alignment_features;
    VkPhysicalDeviceTransformFeedbackFeaturesEXT xfb_features;
    VkPhysicalDeviceVertexAttributeDivisorFeaturesEXT vertex_divisor_features;
    VkPhysicalDeviceTimelineSemaphoreFeaturesKHR timeline_semaphore_features;

    VkPhysicalDeviceFeatures2 features2;
};

struct d3d12_caps
{
    D3D12_FEATURE_DATA_D3D12_OPTIONS options;
    D3D12_FEATURE_DATA_D3D12_OPTIONS1 options1;
    D3D12_FEATURE_DATA_D3D12_OPTIONS2 options2;
    D3D12_FEATURE_DATA_D3D12_OPTIONS3 options3;

    D3D_FEATURE_LEVEL max_feature_level;
    D3D_SHADER_MODEL max_shader_model;
};

/* ID3D12Device */
typedef ID3D12Device3 d3d12_device_iface;

struct d3d12_device
{
    d3d12_device_iface ID3D12Device_iface;
    LONG refcount;

    VkDevice vk_device;
    uint32_t api_version;
    VkPhysicalDevice vk_physical_device;
    struct vkd3d_vk_device_procs vk_procs;
    PFN_vkd3d_signal_event signal_event;
    size_t wchar_size;

    struct vkd3d_gpu_va_allocator gpu_va_allocator;
    struct vkd3d_fence_worker fence_worker;

    pthread_mutex_t mutex;
    struct vkd3d_render_pass_cache render_pass_cache;
    VkPipelineCache vk_pipeline_cache;

    VkPhysicalDeviceMemoryProperties memory_properties;

    struct vkd3d_vulkan_info vk_info;
    struct vkd3d_physical_device_info device_info;

    struct vkd3d_queue *direct_queue;
    struct vkd3d_queue *compute_queue;
    struct vkd3d_queue *copy_queue;
    uint32_t queue_family_indices[VKD3D_MAX_QUEUE_FAMILY_COUNT];
    unsigned int queue_family_count;

    struct vkd3d_instance *vkd3d_instance;

    IUnknown *parent;
    LUID adapter_luid;

    struct vkd3d_private_store private_store;
    struct d3d12_caps d3d12_caps;

    HRESULT removed_reason;

    const struct vkd3d_format *depth_stencil_formats;
    unsigned int format_compatibility_list_count;
    const struct vkd3d_format_compatibility_list *format_compatibility_lists;
    struct vkd3d_null_resources null_resources;
    struct vkd3d_bindless_state bindless_state;
    struct vkd3d_uav_clear_state uav_clear_state;
};

HRESULT d3d12_device_create(struct vkd3d_instance *instance,
        const struct vkd3d_device_create_info *create_info, struct d3d12_device **device) DECLSPEC_HIDDEN;
struct vkd3d_queue *d3d12_device_get_vkd3d_queue(struct d3d12_device *device,
        D3D12_COMMAND_LIST_TYPE type) DECLSPEC_HIDDEN;
bool d3d12_device_is_uma(struct d3d12_device *device, bool *coherent) DECLSPEC_HIDDEN;
void d3d12_device_mark_as_removed(struct d3d12_device *device, HRESULT reason,
        const char *message, ...) VKD3D_PRINTF_FUNC(3, 4) DECLSPEC_HIDDEN;
struct d3d12_device *unsafe_impl_from_ID3D12Device(d3d12_device_iface *iface) DECLSPEC_HIDDEN;

static inline HRESULT d3d12_device_query_interface(struct d3d12_device *device, REFIID iid, void **object)
{
    return ID3D12Device_QueryInterface(&device->ID3D12Device_iface, iid, object);
}

static inline ULONG d3d12_device_add_ref(struct d3d12_device *device)
{
    return ID3D12Device_AddRef(&device->ID3D12Device_iface);
}

static inline ULONG d3d12_device_release(struct d3d12_device *device)
{
    return ID3D12Device_Release(&device->ID3D12Device_iface);
}

static inline unsigned int d3d12_device_get_descriptor_handle_increment_size(struct d3d12_device *device,
        D3D12_DESCRIPTOR_HEAP_TYPE descriptor_type)
{
    return ID3D12Device_GetDescriptorHandleIncrementSize(&device->ID3D12Device_iface, descriptor_type);
}

/* utils */
enum vkd3d_format_type
{
    VKD3D_FORMAT_TYPE_OTHER,
    VKD3D_FORMAT_TYPE_TYPELESS,
    VKD3D_FORMAT_TYPE_SINT,
    VKD3D_FORMAT_TYPE_UINT,
};

struct vkd3d_format
{
    DXGI_FORMAT dxgi_format;
    VkFormat vk_format;
    size_t byte_count;
    size_t block_width;
    size_t block_height;
    size_t block_byte_count;
    VkImageAspectFlags vk_aspect_mask;
    unsigned int plane_count;
    enum vkd3d_format_type type;
    bool is_emulated;
};

static inline size_t vkd3d_format_get_data_offset(const struct vkd3d_format *format,
        unsigned int row_pitch, unsigned int slice_pitch,
        unsigned int x, unsigned int y, unsigned int z)
{
    return z * slice_pitch
            + (y / format->block_height) * row_pitch
            + (x / format->block_width) * format->byte_count * format->block_byte_count;
}

static inline bool vkd3d_format_is_compressed(const struct vkd3d_format *format)
{
    return format->block_byte_count != 1;
}

void vkd3d_format_copy_data(const struct vkd3d_format *format, const uint8_t *src,
        unsigned int src_row_pitch, unsigned int src_slice_pitch, uint8_t *dst, unsigned int dst_row_pitch,
        unsigned int dst_slice_pitch, unsigned int w, unsigned int h, unsigned int d) DECLSPEC_HIDDEN;

const struct vkd3d_format *vkd3d_get_format(const struct d3d12_device *device,
        DXGI_FORMAT dxgi_format, bool depth_stencil) DECLSPEC_HIDDEN;
const struct vkd3d_format *vkd3d_find_uint_format(const struct d3d12_device *device,
        DXGI_FORMAT dxgi_format) DECLSPEC_HIDDEN;

HRESULT vkd3d_init_format_info(struct d3d12_device *device) DECLSPEC_HIDDEN;
void vkd3d_cleanup_format_info(struct d3d12_device *device) DECLSPEC_HIDDEN;

static inline const struct vkd3d_format *vkd3d_format_from_d3d12_resource_desc(
        const struct d3d12_device *device, const D3D12_RESOURCE_DESC *desc, DXGI_FORMAT view_format)
{
    return vkd3d_get_format(device, view_format ? view_format : desc->Format,
            desc->Flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);
}

static inline bool d3d12_box_is_empty(const D3D12_BOX *box)
{
    return box->right <= box->left || box->bottom <= box->top || box->back <= box->front;
}

static inline unsigned int d3d12_resource_desc_get_width(const D3D12_RESOURCE_DESC *desc,
        unsigned int miplevel_idx)
{
    return max(1, desc->Width >> miplevel_idx);
}

static inline unsigned int d3d12_resource_desc_get_height(const D3D12_RESOURCE_DESC *desc,
        unsigned int miplevel_idx)
{
    return max(1, desc->Height >> miplevel_idx);
}

static inline unsigned int d3d12_resource_desc_get_depth(const D3D12_RESOURCE_DESC *desc,
        unsigned int miplevel_idx)
{
    unsigned int d = desc->Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE3D ? 1 : desc->DepthOrArraySize;
    return max(1, d >> miplevel_idx);
}

static inline unsigned int d3d12_resource_desc_get_layer_count(const D3D12_RESOURCE_DESC *desc)
{
    return desc->Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE3D ? desc->DepthOrArraySize : 1;
}

static inline unsigned int d3d12_resource_desc_get_sub_resource_count(const D3D12_RESOURCE_DESC *desc)
{
    return d3d12_resource_desc_get_layer_count(desc) * desc->MipLevels;
}

static inline unsigned int vkd3d_compute_workgroup_count(unsigned int thread_count, unsigned int workgroup_size)
{
    return (thread_count + workgroup_size - 1) / workgroup_size;
}

VkCompareOp vk_compare_op_from_d3d12(D3D12_COMPARISON_FUNC op) DECLSPEC_HIDDEN;
VkSampleCountFlagBits vk_samples_from_dxgi_sample_desc(const DXGI_SAMPLE_DESC *desc) DECLSPEC_HIDDEN;
VkSampleCountFlagBits vk_samples_from_sample_count(unsigned int sample_count) DECLSPEC_HIDDEN;

bool is_valid_feature_level(D3D_FEATURE_LEVEL feature_level) DECLSPEC_HIDDEN;

bool is_valid_resource_state(D3D12_RESOURCE_STATES state) DECLSPEC_HIDDEN;
bool is_write_resource_state(D3D12_RESOURCE_STATES state) DECLSPEC_HIDDEN;

HRESULT return_interface(void *iface, REFIID iface_iid,
        REFIID requested_iid, void **object) DECLSPEC_HIDDEN;

const char *debug_d3d12_box(const D3D12_BOX *box) DECLSPEC_HIDDEN;
const char *debug_d3d12_shader_component_mapping(unsigned int mapping) DECLSPEC_HIDDEN;
const char *debug_vk_extent_3d(VkExtent3D extent) DECLSPEC_HIDDEN;
const char *debug_vk_memory_heap_flags(VkMemoryHeapFlags flags) DECLSPEC_HIDDEN;
const char *debug_vk_memory_property_flags(VkMemoryPropertyFlags flags) DECLSPEC_HIDDEN;
const char *debug_vk_queue_flags(VkQueueFlags flags) DECLSPEC_HIDDEN;

static inline void debug_ignored_node_mask(unsigned int mask)
{
    if (mask && mask != 1)
        FIXME("Ignoring node mask 0x%08x.\n", mask);
}

HRESULT vkd3d_load_vk_global_procs(struct vkd3d_vk_global_procs *procs,
        PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr) DECLSPEC_HIDDEN;
HRESULT vkd3d_load_vk_instance_procs(struct vkd3d_vk_instance_procs *procs,
        const struct vkd3d_vk_global_procs *global_procs, VkInstance instance) DECLSPEC_HIDDEN;
HRESULT vkd3d_load_vk_device_procs(struct vkd3d_vk_device_procs *procs,
        const struct vkd3d_vk_instance_procs *parent_procs, VkDevice device) DECLSPEC_HIDDEN;

extern const char vkd3d_build[];

#ifdef _WIN32
/* This value isn't really used for anything useful on Windows, just need some kind of value. */
#define VKD3D_PATH_MAX _MAX_PATH
#else
#define VKD3D_PATH_MAX PATH_MAX
#endif
bool vkd3d_get_program_name(char program_name[VKD3D_PATH_MAX]) DECLSPEC_HIDDEN;

VkResult vkd3d_set_vk_object_name_utf8(struct d3d12_device *device, uint64_t vk_object,
        VkDebugReportObjectTypeEXT vk_object_type, const char *name) DECLSPEC_HIDDEN;
HRESULT vkd3d_set_vk_object_name(struct d3d12_device *device, uint64_t vk_object,
        VkDebugReportObjectTypeEXT vk_object_type, const WCHAR *name) DECLSPEC_HIDDEN;

static inline void vk_prepend_struct(void *header, void *structure)
{
    VkBaseOutStructure *vk_header = header, *vk_structure = structure;

    assert(!vk_structure->pNext);
    vk_structure->pNext = vk_header->pNext;
    vk_header->pNext = vk_structure;
}

#endif  /* __VKD3D_PRIVATE_H */
