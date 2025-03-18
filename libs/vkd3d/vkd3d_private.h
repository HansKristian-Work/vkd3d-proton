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
#define VK_NO_PROTOTYPES

#include "vkd3d_common.h"
#include "vkd3d_memory.h"
#include "vkd3d_utf8.h"
#include "hashmap.h"
#include "list.h"
#include "rbtree.h"

#include "vkd3d.h"
#include "vkd3d_build.h"
#include "vkd3d_version.h"
#include "vkd3d_shader.h"
#include "vkd3d_threads.h"
#include "vkd3d_platform.h"
#include "vkd3d_swapchain_factory.h"
#include "vkd3d_command_list_vkd3d_ext.h"
#include "vkd3d_command_queue_vkd3d_ext.h"
#include "vkd3d_device_vkd3d_ext.h"
#include "vkd3d_string.h"
#include "vkd3d_file_utils.h"
#include "vkd3d_native_sync_handle.h"
#include "copy_utils.h"
#include <assert.h>
#include <inttypes.h>
#include <limits.h>
#include <stdbool.h>

#define VK_CALL(f) (vk_procs->f)

#define MAKE_MAGIC(a,b,c,d) (((uint32_t)a) | (((uint32_t)b) << 8) | (((uint32_t)c) << 16) | (((uint32_t)d) << 24))

#define VKD3D_MAX_COMPATIBLE_FORMAT_COUNT 10u
#define VKD3D_MAX_SHADER_STAGES           5u
#define VKD3D_MAX_VK_SYNC_OBJECTS         4u

/* 6 types for CBV_SRV_UAV and 1 for sampler. */
#define VKD3D_MAX_BINDLESS_DESCRIPTOR_SETS 7u
/* The above plus one push descriptor set + static sampler set + static sampler set for local root signatures. */
#define VKD3D_MAX_DESCRIPTOR_SETS (VKD3D_MAX_BINDLESS_DESCRIPTOR_SETS + 3u)
#define VKD3D_MAX_MUTABLE_DESCRIPTOR_TYPES 6u
#define VKD3D_MAX_DESCRIPTOR_SIZE 256u /* Maximum allowed value in VK_EXT_descriptor_buffer. */

#define VKD3D_MIN_VIEW_DESCRIPTOR_COUNT (1000000u)
#define VKD3D_MIN_SAMPLER_DESCRIPTOR_COUNT (2048u)

#define VKD3D_TILE_SIZE 65536

typedef ID3D12Fence1 d3d12_fence_iface;

struct d3d12_command_list;
struct d3d12_command_allocator;
struct d3d12_device;
struct d3d12_resource;

struct vkd3d_bindless_set_info;
struct vkd3d_dynamic_state;

struct vkd3d_vk_global_procs
{
    PFN_vkCreateInstance vkCreateInstance;
    PFN_vkEnumerateInstanceVersion vkEnumerateInstanceVersion;
    PFN_vkEnumerateInstanceExtensionProperties vkEnumerateInstanceExtensionProperties;
    PFN_vkEnumerateInstanceLayerProperties vkEnumerateInstanceLayerProperties;
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
#define VK_INSTANCE_PFN     DECLARE_VK_PFN
#define VK_INSTANCE_EXT_PFN DECLARE_VK_PFN
#define VK_DEVICE_PFN       DECLARE_VK_PFN
#define VK_DEVICE_EXT_PFN   DECLARE_VK_PFN
#include "vulkan_procs.h"
};
#undef DECLARE_VK_PFN

HRESULT hresult_from_errno(int rc);
HRESULT hresult_from_vk_result(VkResult vr);
HRESULT hresult_from_vkd3d_result(int vkd3d_result);

struct vkd3d_vulkan_info
{
    /* EXT instance extensions */
    bool EXT_debug_utils;

    /* KHR device extensions */
    bool KHR_push_descriptor;
    bool KHR_ray_tracing_pipeline;
    bool KHR_acceleration_structure;
    bool KHR_deferred_host_operations;
    bool KHR_pipeline_library;
    bool KHR_ray_query;
    bool KHR_fragment_shading_rate;
    bool KHR_ray_tracing_maintenance1;
    bool KHR_fragment_shader_barycentric;
    bool KHR_external_memory_win32;
    bool KHR_external_semaphore_win32;
    bool KHR_present_wait;
    bool KHR_present_id;
    bool KHR_maintenance5;
    bool KHR_maintenance6;
    bool KHR_maintenance7;
    bool KHR_maintenance8;
    bool KHR_shader_maximal_reconvergence;
    bool KHR_shader_quad_control;
    bool KHR_compute_shader_derivatives;
    bool KHR_calibrated_timestamps;
    /* EXT device extensions */
    bool EXT_conditional_rendering;
    bool EXT_conservative_rasterization;
    bool EXT_custom_border_color;
    bool EXT_depth_clip_enable;
    bool EXT_device_generated_commands;
    bool EXT_image_view_min_lod;
    bool EXT_robustness2;
    bool EXT_shader_stencil_export;
    bool EXT_transform_feedback;
    bool EXT_vertex_attribute_divisor;
    bool EXT_extended_dynamic_state2;
    bool EXT_extended_dynamic_state3;
    bool EXT_external_memory_host;
    bool EXT_shader_image_atomic_int64;
    bool EXT_mesh_shader;
    bool EXT_mutable_descriptor_type; /* EXT promotion of VALVE one. */
    bool EXT_hdr_metadata;
    bool EXT_shader_module_identifier;
    bool EXT_descriptor_buffer;
    bool EXT_pipeline_library_group_handles;
    bool EXT_image_sliced_view_of_3d;
    bool EXT_graphics_pipeline_library;
    bool EXT_fragment_shader_interlock;
    bool EXT_pageable_device_local_memory;
    bool EXT_memory_priority;
    bool EXT_dynamic_rendering_unused_attachments;
    bool EXT_line_rasterization;
    bool EXT_image_compression_control;
    bool EXT_device_fault;
    bool EXT_memory_budget;
    bool EXT_device_address_binding_report;
    bool EXT_depth_bias_control;
    /* AMD device extensions */
    bool AMD_buffer_marker;
    bool AMD_device_coherent_memory;
    bool AMD_shader_core_properties;
    bool AMD_shader_core_properties2;
    /* NV device extensions */
    bool NV_optical_flow;
    bool NV_shader_sm_builtins;
    bool NVX_binary_import;
    bool NVX_image_view_handle;
    bool NV_fragment_shader_barycentric;
    bool NV_compute_shader_derivatives;
    bool NV_device_diagnostic_checkpoints;
    bool NV_device_generated_commands;
    bool NV_shader_subgroup_partitioned;
    bool NV_memory_decompression;
    bool NV_device_generated_commands_compute;
    bool NV_low_latency2;
    bool NV_raw_access_chains;
    /* VALVE extensions */
    bool VALVE_mutable_descriptor_type;
    /* MESA extensions */
    bool MESA_image_alignment_control;

    /* Optional extensions which are enabled externally as optional extensions
     * if swapchain/surface extensions are enabled. */
    bool EXT_surface_maintenance1;
    bool EXT_swapchain_maintenance1;

    unsigned int extension_count;
    const char* const* extension_names;

    bool rasterization_stream;
    unsigned int max_vertex_attrib_divisor;

    VkPhysicalDeviceLimits device_limits;
    VkPhysicalDeviceSparseProperties sparse_properties;

    unsigned int shader_extension_count;
    enum vkd3d_shader_target_extension shader_extensions[VKD3D_SHADER_TARGET_EXTENSION_COUNT];
};

struct vkd3d_instance
{
    VkInstance vk_instance;
    uint32_t instance_version;
    struct vkd3d_vk_instance_procs vk_procs;

    struct vkd3d_vulkan_info vk_info;
    struct vkd3d_vk_global_procs vk_global_procs;

    VkDebugUtilsMessengerEXT vk_debug_callback;

    LONG refcount;
};

extern uint64_t vkd3d_config_flags;
extern struct vkd3d_shader_quirk_info vkd3d_shader_quirk_info;

struct vkd3d_queue_timeline_trace_cookie
{
    unsigned int index;
};

struct vkd3d_fence_worker;

typedef void (*vkd3d_waiting_fence_callback)(struct vkd3d_fence_worker *, void *, bool);

struct vkd3d_fence_wait_info
{
    VkSemaphore vk_semaphore;
    uint64_t vk_semaphore_value;
    vkd3d_waiting_fence_callback release_callback;
    unsigned char userdata[32];
};

struct vkd3d_waiting_fence
{
    struct vkd3d_fence_wait_info fence_info;
    struct vkd3d_queue_timeline_trace_cookie timeline_cookie;
};

struct vkd3d_fence_worker
{
    pthread_t thread;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    bool should_exit;

    uint32_t enqueued_fence_count;
    struct vkd3d_waiting_fence *enqueued_fences;
    size_t enqueued_fences_size;

    struct d3d12_device *device;
    struct d3d12_command_queue *queue;

    /* To aid timeline profiles. A single fence worker processes work monotonically. */
    struct
    {
        char tid[64];
        /* The lock timestamps is to ensure that the timeline trace becomes readable in chrome://tracing.
         * For us, start and end ranges can overlap. This ends up as an unreadable trace
         * since the tracer expects a stack-like nesting for overlapping events.
         * To work around this, we ensure that start TS of a following event is moved to end TS of previous event. */
        double lock_end_gpu_ts;
        double lock_end_cpu_ts;
        double lock_end_event_ts;
        double lock_end_present_wait_ts;

        /* A thread local buffer used to avoid holding locks for too long.
         * Only submission threads flush out JSON IO and this serves as thread-local
         * scratch space. */
        unsigned int *list_buffer;
        size_t list_buffer_size;
    } timeline;
};

/* 2 MiB is a good threshold, because it's huge page size. */
#define VKD3D_VA_BLOCK_SIZE_BITS (21)
#define VKD3D_VA_BLOCK_SIZE (1ull << VKD3D_VA_BLOCK_SIZE_BITS)
#define VKD3D_VA_LO_MASK (VKD3D_VA_BLOCK_SIZE - 1)

#define VKD3D_VA_BLOCK_BITS (20)
#define VKD3D_VA_BLOCK_COUNT (1ull << VKD3D_VA_BLOCK_BITS)
#define VKD3D_VA_BLOCK_MASK (VKD3D_VA_BLOCK_COUNT - 1)

#define VKD3D_VA_NEXT_BITS (12)
#define VKD3D_VA_NEXT_COUNT (1ull << VKD3D_VA_NEXT_BITS)
#define VKD3D_VA_NEXT_MASK (VKD3D_VA_NEXT_COUNT - 1)

void vkd3d_add_wait_to_all_queues(struct d3d12_device *device,
        VkSemaphore vk_semaphore, uint64_t value);
HRESULT vkd3d_create_timeline_semaphore(struct d3d12_device *device,
        uint64_t initial_value, bool shared, VkSemaphore *vk_semaphore);
HRESULT vkd3d_enqueue_timeline_semaphore(struct vkd3d_fence_worker *worker,
        const struct vkd3d_fence_wait_info *fence_info,
        const struct vkd3d_queue_timeline_trace_cookie *timeline_cookie);

struct vkd3d_unique_resource;

struct vkd3d_va_entry
{
    DECLSPEC_ALIGN(8) VkDeviceAddress va;
    const struct vkd3d_unique_resource *resource;
};

struct vkd3d_va_block
{
    struct vkd3d_va_entry l;
    struct vkd3d_va_entry r;
};

struct vkd3d_va_tree
{
    struct vkd3d_va_block blocks[VKD3D_VA_BLOCK_COUNT];
    struct vkd3d_va_tree *next[VKD3D_VA_NEXT_COUNT];
};

struct vkd3d_va_range
{
    VkDeviceAddress base;
    VkDeviceSize size;
};

struct vkd3d_va_map
{
    struct vkd3d_va_tree va_tree;

    pthread_mutex_t mutex;

    struct vkd3d_unique_resource **small_entries;
    size_t small_entries_size;
    size_t small_entries_count;
};

void vkd3d_va_map_insert(struct vkd3d_va_map *va_map, struct vkd3d_unique_resource *resource);
void vkd3d_va_map_remove(struct vkd3d_va_map *va_map, const struct vkd3d_unique_resource *resource);
const struct vkd3d_unique_resource *vkd3d_va_map_deref(struct vkd3d_va_map *va_map, VkDeviceAddress va);
VkAccelerationStructureKHR vkd3d_va_map_place_acceleration_structure(struct vkd3d_va_map *va_map,
        struct d3d12_device *device,
        VkDeviceAddress va);
void vkd3d_va_map_init(struct vkd3d_va_map *va_map);
void vkd3d_va_map_cleanup(struct vkd3d_va_map *va_map);

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
    };
};

static inline void vkd3d_private_data_destroy(struct vkd3d_private_data *data)
{
    if (data->is_object)
        IUnknown_Release(data->object);
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

static inline HRESULT vkd3d_private_data_lock(struct vkd3d_private_store *store)
{
    int rc;
    if ((rc = pthread_mutex_lock(&store->mutex)))
    {
        ERR("Failed to lock mutex, error %d.\n", rc);
        return hresult_from_errno(rc);
    }

    return S_OK;
}

static inline void vkd3d_private_data_unlock(struct vkd3d_private_store *store)
{
    pthread_mutex_unlock(&store->mutex);
}

HRESULT vkd3d_get_private_data(struct vkd3d_private_store *store,
        const GUID *tag, unsigned int *out_size, void *out);

HRESULT vkd3d_private_store_set_private_data(struct vkd3d_private_store *store,
        const GUID *tag, const void *data, unsigned int data_size, bool is_object);

typedef void(*vkd3d_set_name_callback)(void *, const char *);

static inline bool vkd3d_private_data_object_name_ptr(REFGUID guid,
    UINT data_size, const void *data, const char **out_name)
{
    if (out_name)
        *out_name = NULL;

    /* This is also handled in the object_name implementation
     * but this avoids an additional, needless allocation
     * and some games may spam SetName.
     */
    if (!(vkd3d_config_flags & VKD3D_CONFIG_FLAG_DEBUG_UTILS))
        return false;

    if (IsEqualGUID(guid, &WKPDID_D3DDebugObjectName))
    {
        const char *name = (const char *)data;

        if (!data || !data_size)
            return true;

        if (out_name)
            *out_name = name[data_size - 1] != '\0'
                ? vkd3d_strdup_n(name, data_size)
                : name;

        return true;
    }
    else if (IsEqualGUID(guid, &WKPDID_D3DDebugObjectNameW))
    {
        const WCHAR *name = (const WCHAR *)data;

        if (!data || data_size < sizeof(WCHAR))
            return true;

        if (out_name)
            *out_name = vkd3d_strdup_w_utf8(name, data_size / sizeof(WCHAR));
        return true;
    }

    return false;
}

static inline HRESULT vkd3d_set_private_data(struct vkd3d_private_store *store,
        const GUID *tag, unsigned int data_size, const void *data,
        vkd3d_set_name_callback set_name_callback, void *calling_object)
{
    const char *name;
    HRESULT hr;

    if (FAILED(hr = vkd3d_private_data_lock(store)))
        return hr;

    if (FAILED(hr = vkd3d_private_store_set_private_data(store, tag, data, data_size, false)))
    {
        vkd3d_private_data_unlock(store);
        return hr;
    }

    if (set_name_callback && vkd3d_private_data_object_name_ptr(tag, data_size, data, &name))
    {
        set_name_callback(calling_object, name);
        if (name && name != data)
            vkd3d_free((void *)name);
    }

    vkd3d_private_data_unlock(store);
    return hr;
}

static inline HRESULT vkd3d_set_private_data_interface(struct vkd3d_private_store *store,
        const GUID *tag, const IUnknown *object,
        vkd3d_set_name_callback set_name_callback, void *calling_object)
{
    const void *data = object ? object : (void *)&object;
    HRESULT hr;

    if (FAILED(hr = vkd3d_private_data_lock(store)))
        return hr;

    if (FAILED(hr = vkd3d_private_store_set_private_data(store, tag, data, sizeof(object), !!object)))
    {
        vkd3d_private_data_unlock(store);
        return hr;
    }

    if (set_name_callback && vkd3d_private_data_object_name_ptr(tag, 0, NULL, NULL))
        set_name_callback(calling_object, NULL);

    vkd3d_private_data_unlock(store);
    return hr;
}

HRESULT STDMETHODCALLTYPE d3d12_object_SetName(ID3D12Object *iface, const WCHAR *name);

struct d3d_destruction_callback_entry
{
    PFN_DESTRUCTION_CALLBACK callback;
    void *userdata;
    UINT callback_id;
};

struct d3d_destruction_notifier
{
    ID3DDestructionNotifier ID3DDestructionNotifier_iface;

    IUnknown *parent;

    pthread_mutex_t mutex;

    struct d3d_destruction_callback_entry *callbacks;
    size_t callback_size;
    size_t callback_count;

    UINT next_callback_id;
};

void d3d_destruction_notifier_init(struct d3d_destruction_notifier *notifier, IUnknown *parent);
void d3d_destruction_notifier_free(struct d3d_destruction_notifier *notifier);
void d3d_destruction_notifier_notify(struct d3d_destruction_notifier *notifier);

/* ID3D12Fence */
struct d3d12_fence_value
{
    uint64_t virtual_value;
    uint64_t update_count;
    VkSemaphore vk_semaphore;
    uint64_t vk_semaphore_value;
};

#define VKD3D_WAITING_EVENT_SIGNAL_BIT (1u << 31)

enum vkd3d_waiting_event_type
{
    VKD3D_WAITING_EVENT_SINGLE,
    VKD3D_WAITING_EVENT_MULTI_ALL,
    VKD3D_WAITING_EVENT_MULTI_ANY,
};

struct vkd3d_waiting_event
{
    enum vkd3d_waiting_event_type wait_type;
    uint64_t value;
    vkd3d_native_sync_handle handle;
    bool *latch;
    uint32_t *payload;
    struct vkd3d_queue_timeline_trace_cookie timeline_cookie;
};

struct d3d12_fence
{
    d3d12_fence_iface ID3D12Fence_iface;
    LONG refcount_internal;
    LONG refcount;

    D3D12_FENCE_FLAGS d3d12_flags;

    /* only used for shared semaphores */
    VkSemaphore timeline_semaphore;

    uint64_t max_pending_virtual_timeline_value;
    uint64_t virtual_value;
    uint64_t signal_count;
    uint64_t update_count;
    struct d3d12_fence_value *pending_updates;
    size_t pending_updates_count;
    size_t pending_updates_size;

    pthread_mutex_t mutex;
    pthread_cond_t cond;
    pthread_cond_t null_event_cond;

    struct vkd3d_waiting_event *events;
    size_t events_size;
    size_t event_count;

    struct d3d12_device *device;

    struct vkd3d_private_store private_store;
    struct d3d_destruction_notifier destruction_notifier;
};

static inline struct d3d12_fence *impl_from_ID3D12Fence1(ID3D12Fence1 *iface)
{
    extern CONST_VTBL struct ID3D12Fence1Vtbl d3d12_fence_vtbl;
    if (!iface)
        return NULL;
    assert(iface->lpVtbl == &d3d12_fence_vtbl);
    return CONTAINING_RECORD(iface, struct d3d12_fence, ID3D12Fence_iface);
}

static inline struct d3d12_fence *impl_from_ID3D12Fence(ID3D12Fence *iface)
{
    return impl_from_ID3D12Fence1((ID3D12Fence1 *)iface);
}

HRESULT d3d12_fence_create(struct d3d12_device *device,
        uint64_t initial_value, D3D12_FENCE_FLAGS flags, struct d3d12_fence **fence);
HRESULT d3d12_fence_set_event_on_completion(struct d3d12_fence *fence,
        UINT64 value, HANDLE event);
HRESULT d3d12_fence_set_native_sync_handle_on_completion(struct d3d12_fence *fence,
        UINT64 value, vkd3d_native_sync_handle handle);

struct vkd3d_shared_fence_waiting_event
{
    struct list entry;
    struct vkd3d_waiting_event wait;
};

struct d3d12_shared_fence
{
    d3d12_fence_iface ID3D12Fence_iface;
    LONG refcount_internal;
    LONG refcount;

    D3D12_FENCE_FLAGS d3d12_flags;

    VkSemaphore timeline_semaphore;

    pthread_t thread;
    pthread_mutex_t mutex;
    pthread_cond_t cond_var;
    uint32_t is_running;

    struct list events;

    struct d3d12_device *device;

    struct vkd3d_private_store private_store;
    struct d3d_destruction_notifier destruction_notifier;
};

static inline struct d3d12_shared_fence *shared_impl_from_ID3D12Fence1(ID3D12Fence1 *iface)
{
    extern CONST_VTBL struct ID3D12Fence1Vtbl d3d12_shared_fence_vtbl;
    if (!iface)
        return NULL;
    assert(iface->lpVtbl == &d3d12_shared_fence_vtbl);
    return CONTAINING_RECORD(iface, struct d3d12_shared_fence, ID3D12Fence_iface);
}

static inline struct d3d12_shared_fence *shared_impl_from_ID3D12Fence(ID3D12Fence *iface)
{
    return shared_impl_from_ID3D12Fence1((ID3D12Fence1 *)iface);
}

HRESULT d3d12_shared_fence_create(struct d3d12_device *device,
        uint64_t initial_value, D3D12_FENCE_FLAGS flags, struct d3d12_shared_fence **fence);

static inline bool is_shared_ID3D12Fence1(ID3D12Fence1 *iface)
{
    extern CONST_VTBL struct ID3D12Fence1Vtbl d3d12_shared_fence_vtbl;
    extern CONST_VTBL struct ID3D12Fence1Vtbl d3d12_fence_vtbl;
    assert(iface->lpVtbl ==  &d3d12_shared_fence_vtbl || iface->lpVtbl == &d3d12_fence_vtbl);

    return iface->lpVtbl ==  &d3d12_shared_fence_vtbl;
}

static inline bool is_shared_ID3D12Fence(ID3D12Fence *iface)
{
    return is_shared_ID3D12Fence1((ID3D12Fence1 *)iface);
}

HRESULT d3d12_fence_iface_set_native_sync_handle_on_completion_explicit(ID3D12Fence *iface,
        enum vkd3d_waiting_event_type wait_type, UINT64 value, vkd3d_native_sync_handle handle, uint32_t *payload);

enum vkd3d_allocation_flag
{
    VKD3D_ALLOCATION_FLAG_GLOBAL_BUFFER     = (1u << 0),
    VKD3D_ALLOCATION_FLAG_GPU_ADDRESS       = (1u << 1),
    VKD3D_ALLOCATION_FLAG_CPU_ACCESS        = (1u << 2),
    VKD3D_ALLOCATION_FLAG_ALLOW_WRITE_WATCH = (1u << 3),
    VKD3D_ALLOCATION_FLAG_NO_FALLBACK       = (1u << 4),
    VKD3D_ALLOCATION_FLAG_DEDICATED         = (1u << 5),
    /* Intended for internal allocation of scratch buffers.
     * They are never suballocated since we do that ourselves,
     * and we do not consume space in the VA map. */
    VKD3D_ALLOCATION_FLAG_INTERNAL_SCRATCH  = (1u << 6),
    VKD3D_ALLOCATION_FLAG_ALLOW_IMAGE_SUBALLOCATION  = (1u << 7),
};

#define VKD3D_MEMORY_CHUNK_SIZE (VKD3D_VA_BLOCK_SIZE * 8)
#define VKD3D_MEMORY_IMAGE_HEAP_SUBALLOCATE_THRESHOLD (8 * 1024 * 1024)
#define VKD3D_MEMORY_LARGE_CHUNK_SIZE (VKD3D_MEMORY_IMAGE_HEAP_SUBALLOCATE_THRESHOLD * 4)

struct vkd3d_memory_chunk;

struct vkd3d_allocate_memory_info
{
    VkMemoryRequirements memory_requirements;
    D3D12_HEAP_PROPERTIES heap_properties;
    D3D12_HEAP_FLAGS heap_flags;
    void *host_ptr;
    const void *pNext;
    uint32_t flags;
    VkBufferUsageFlags2KHR explicit_global_buffer_usage;
    VkMemoryPropertyFlags optional_memory_properties;
    float vk_memory_priority;
};

struct vkd3d_allocate_heap_memory_info
{
    D3D12_HEAP_DESC heap_desc;
    void *host_ptr;
    uint32_t extra_allocation_flags;
    float vk_memory_priority;
    VkBufferUsageFlags2KHR explicit_global_buffer_usage;
};

struct vkd3d_allocate_resource_memory_info
{
    D3D12_HEAP_PROPERTIES heap_properties;
    D3D12_HEAP_FLAGS heap_flags;
    VkBuffer vk_buffer;
    VkImage vk_image;
    void *host_ptr;
};

uint32_t vkd3d_get_priority_adjust(VkDeviceSize size);
float vkd3d_convert_to_vk_prio(D3D12_RESIDENCY_PRIORITY d3d12prio);

struct vkd3d_view_map;

struct vkd3d_unique_resource
{
    union
    {
        VkBuffer vk_buffer;
        VkImage vk_image;
    };
    uint64_t cookie;
    VkDeviceAddress va;
    VkDeviceSize size;

    /* This is used to handle views when we cannot bind it to a
     * specific ID3D12Resource, i.e. RTAS. Only allocated as needed. */
    struct vkd3d_view_map *view_map;
};

struct vkd3d_device_memory_allocation
{
    VkDeviceMemory vk_memory;
    uint32_t vk_memory_type;
    VkDeviceSize size;
};

struct vkd3d_memory_allocation
{
    struct vkd3d_unique_resource resource;
    struct vkd3d_device_memory_allocation device_allocation;
    VkDeviceSize offset;
    void *cpu_address;

    D3D12_HEAP_TYPE heap_type;
    uint32_t flags;
    VkBufferUsageFlags2KHR explicit_global_buffer_usage;

    uint64_t clear_semaphore_value;

    struct vkd3d_memory_chunk *chunk;
};

static inline void vkd3d_memory_allocation_slice(struct vkd3d_memory_allocation *dst,
        const struct vkd3d_memory_allocation *src, VkDeviceSize offset, VkDeviceSize size)
{
    *dst = *src;
    dst->offset += offset;
    dst->resource.size = size;
    dst->resource.va += offset;

    if (dst->cpu_address)
        dst->cpu_address = void_ptr_offset(dst->cpu_address, offset);
}

struct vkd3d_memory_free_range
{
    VkDeviceSize offset;
    VkDeviceSize length;
};

struct vkd3d_memory_chunk
{
    struct vkd3d_memory_allocation allocation;
    struct vkd3d_memory_free_range *free_ranges;
    size_t free_ranges_size;
    size_t free_ranges_count;
};

#define VKD3D_MEMORY_TRANSFER_COMMAND_BUFFER_COUNT (16u)

enum vkd3d_memory_transfer_op
{
    VKD3D_MEMORY_TRANSFER_OP_CLEAR_ALLOCATION,
    VKD3D_MEMORY_TRANSFER_OP_WRITE_SUBRESOURCE,
};

struct vkd3d_memory_transfer_info
{
    enum vkd3d_memory_transfer_op op;
    struct vkd3d_memory_allocation *allocation;

    struct d3d12_resource *resource;
    uint32_t subresource_idx;
    VkOffset3D offset;
    VkExtent3D extent;
};

struct vkd3d_memory_transfer_tracked_resource
{
    struct d3d12_resource *resource;
    UINT64 semaphore_value;
};

struct vkd3d_memory_transfer_queue
{
    struct d3d12_device *device;
    struct vkd3d_queue *vkd3d_queue;

    pthread_mutex_t mutex;
    pthread_cond_t cond;
    pthread_t thread;

    VkCommandBuffer vk_command_buffers[VKD3D_MEMORY_TRANSFER_COMMAND_BUFFER_COUNT];
    VkCommandPool vk_command_pool;
    VkSemaphore vk_semaphore;

    UINT64 last_known_value;
    UINT64 next_signal_value;

    VkDeviceSize num_bytes_pending;
    uint32_t command_buffer_index;

    struct vkd3d_memory_transfer_info *transfers;
    size_t transfer_size;
    size_t transfer_count;

    struct vkd3d_memory_transfer_tracked_resource *tracked_resources;
    size_t tracked_resource_size;
    size_t tracked_resource_count;
};

void vkd3d_memory_transfer_queue_cleanup(struct vkd3d_memory_transfer_queue *queue);
HRESULT vkd3d_memory_transfer_queue_init(struct vkd3d_memory_transfer_queue *queue, struct d3d12_device *device);
HRESULT vkd3d_memory_transfer_queue_flush(struct vkd3d_memory_transfer_queue *queue);
HRESULT vkd3d_memory_transfer_queue_write_subresource(struct vkd3d_memory_transfer_queue *queue,
        struct d3d12_resource *resource, uint32_t subresource_idx, VkOffset3D offset, VkExtent3D extent);

struct vkd3d_memory_allocator
{
    pthread_mutex_t mutex;

    struct vkd3d_memory_chunk **chunks;
    size_t chunks_size;
    size_t chunks_count;

    struct vkd3d_va_map va_map;
};

void vkd3d_free_memory(struct d3d12_device *device, struct vkd3d_memory_allocator *allocator,
        const struct vkd3d_memory_allocation *allocation);
HRESULT vkd3d_allocate_memory(struct d3d12_device *device, struct vkd3d_memory_allocator *allocator,
        const struct vkd3d_allocate_memory_info *info, struct vkd3d_memory_allocation *allocation);
bool vkd3d_allocate_image_memory_prefers_dedicated(struct d3d12_device *device,
        D3D12_HEAP_FLAGS heap_flags, const VkMemoryRequirements *requirements);
HRESULT vkd3d_allocate_heap_memory(struct d3d12_device *device, struct vkd3d_memory_allocator *allocator,
        const struct vkd3d_allocate_heap_memory_info *info, struct vkd3d_memory_allocation *allocation);

HRESULT vkd3d_memory_allocator_init(struct vkd3d_memory_allocator *allocator, struct d3d12_device *device);
void vkd3d_memory_allocator_cleanup(struct vkd3d_memory_allocator *allocator, struct d3d12_device *device);

/* ID3D12Heap */
typedef ID3D12Heap1 d3d12_heap_iface;

typedef struct
{
    bool allows_dynamic_residency;

    spinlock_t spinlock; /* covers access to any of the following fields after creation */

    D3D12_RESIDENCY_PRIORITY d3d12priority;
    LONG residency_count;
} priority_info;

struct d3d12_heap
{
    d3d12_heap_iface ID3D12Heap_iface;
    LONG internal_refcount;
    LONG refcount;

    D3D12_HEAP_DESC desc;
    struct vkd3d_memory_allocation allocation;

    priority_info priority;

    struct d3d12_device *device;
    struct vkd3d_private_store private_store;
    struct d3d_destruction_notifier destruction_notifier;
};

HRESULT d3d12_heap_create(struct d3d12_device *device, const D3D12_HEAP_DESC *desc,
        void *host_address, struct d3d12_heap **heap);
HRESULT d3d12_device_validate_custom_heap_type(struct d3d12_device *device,
        const D3D12_HEAP_PROPERTIES *heap_properties);

ULONG d3d12_heap_incref(struct d3d12_heap *heap);
ULONG d3d12_heap_decref(struct d3d12_heap *heap);

static inline struct d3d12_heap *impl_from_ID3D12Heap1(ID3D12Heap1 *iface)
{
    extern CONST_VTBL struct ID3D12Heap1Vtbl d3d12_heap_vtbl;
    if (!iface)
        return NULL;
    assert(iface->lpVtbl == &d3d12_heap_vtbl);
    return CONTAINING_RECORD(iface, struct d3d12_heap, ID3D12Heap_iface);
}

static inline struct d3d12_heap *impl_from_ID3D12Heap(ID3D12Heap *iface)
{
    return impl_from_ID3D12Heap1((ID3D12Heap1 *)iface);
}

enum vkd3d_resource_flag
{
    VKD3D_RESOURCE_COMMITTED              = (1u << 0),
    VKD3D_RESOURCE_PLACED                 = (1u << 1),
    VKD3D_RESOURCE_RESERVED               = (1u << 2),
    VKD3D_RESOURCE_ALLOCATION             = (1u << 3),
    VKD3D_RESOURCE_LINEAR_STAGING_COPY    = (1u << 4),
    VKD3D_RESOURCE_EXTERNAL               = (1u << 5),
    VKD3D_RESOURCE_ACCELERATION_STRUCTURE = (1u << 6),
    VKD3D_RESOURCE_GENERAL_LAYOUT         = (1u << 7),
};

#define VKD3D_INVALID_TILE_INDEX (~0u)

struct d3d12_sparse_image_region
{
    VkImageSubresource subresource;
    uint32_t subresource_index;
    VkOffset3D offset;
    VkExtent3D extent;
};

struct d3d12_sparse_buffer_region
{
    VkDeviceSize offset;
    VkDeviceSize length;
};

struct d3d12_sparse_tile
{
    union
    {
        struct d3d12_sparse_image_region image;
        struct d3d12_sparse_buffer_region buffer;
    };
    VkDeviceMemory vk_memory;
    VkDeviceSize vk_offset;
};

struct d3d12_sparse_info
{
    uint32_t tile_count;
    uint32_t tiling_count;
    struct d3d12_sparse_tile *tiles;
    D3D12_TILE_SHAPE tile_shape;
    D3D12_PACKED_MIP_INFO packed_mips;
    D3D12_SUBRESOURCE_TILING *tilings;
    uint64_t init_timeline_value;
    struct vkd3d_device_memory_allocation vk_metadata_memory;
};

struct vkd3d_view_map
{
    spinlock_t spinlock;
    struct hash_map map;
#ifdef VKD3D_ENABLE_DESCRIPTOR_QA
    uint64_t resource_cookie;
#endif
};

HRESULT vkd3d_view_map_init(struct vkd3d_view_map *view_map);
void vkd3d_view_map_destroy(struct vkd3d_view_map *view_map, struct d3d12_device *device);

struct vkd3d_subresource_layout
{
    size_t offset;
    size_t row_pitch;
    size_t depth_pitch;
};

struct vkd3d_format_compatibility_list
{
    unsigned int format_count;
    VkFormat vk_formats[VKD3D_MAX_COMPATIBLE_FORMAT_COUNT];
    DXGI_FORMAT uint_format;
};

/* ID3D12Resource */
typedef ID3D12Resource2 d3d12_resource_iface;

struct d3d12_resource
{
    d3d12_resource_iface ID3D12Resource_iface;
    LONG refcount;
    LONG internal_refcount;

    D3D12_RESOURCE_DESC1 desc;
    D3D12_HEAP_PROPERTIES heap_properties;
    D3D12_HEAP_FLAGS heap_flags;
    struct vkd3d_memory_allocation mem;
    struct vkd3d_memory_allocation private_mem;
    struct vkd3d_unique_resource res;

    struct d3d12_heap *heap;

    uint32_t flags;

    /* To keep track of initial layout. */
    VkImageLayout common_layout;
    D3D12_RESOURCE_STATES initial_state;
    uint32_t initial_layout_transition;
#ifdef VKD3D_ENABLE_BREADCRUMBS
    bool initial_layout_transition_validate_only;
#endif

    struct d3d12_sparse_info sparse;
    struct vkd3d_view_map view_map;
    struct vkd3d_subresource_layout *subresource_layouts;
    struct vkd3d_format_compatibility_list format_compatibility_list;

    priority_info priority;

    struct d3d12_device *device;

    const struct vkd3d_format *format;

    VkImageView vrs_view;

    struct vkd3d_private_store private_store;
    struct d3d_destruction_notifier destruction_notifier;
};

static inline bool d3d12_resource_is_buffer(const struct d3d12_resource *resource)
{
    return resource->desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER;
}

static inline bool d3d12_resource_is_acceleration_structure(const struct d3d12_resource *resource)
{
    return !!(resource->flags & VKD3D_RESOURCE_ACCELERATION_STRUCTURE);
}

static inline bool d3d12_resource_is_texture(const struct d3d12_resource *resource)
{
    return resource->desc.Dimension != D3D12_RESOURCE_DIMENSION_BUFFER;
}

static inline VkImageLayout d3d12_resource_pick_layout(const struct d3d12_resource *resource, VkImageLayout layout)
{
    return resource->flags & VKD3D_RESOURCE_GENERAL_LAYOUT ?
            VK_IMAGE_LAYOUT_GENERAL : layout;
}

ULONG d3d12_resource_incref(struct d3d12_resource *resource);
ULONG d3d12_resource_decref(struct d3d12_resource *resource);

LONG64 vkd3d_allocate_cookie();

bool d3d12_resource_is_cpu_accessible(const struct d3d12_resource *resource);
void d3d12_resource_promote_desc(const D3D12_RESOURCE_DESC *desc, D3D12_RESOURCE_DESC1 *desc1);
HRESULT d3d12_resource_validate_desc(const D3D12_RESOURCE_DESC1 *desc,
        UINT num_castable_formats, const DXGI_FORMAT *castable_formats,
        struct d3d12_device *device);
VkImageSubresource d3d12_resource_get_vk_subresource(const struct d3d12_resource *resource,
        uint32_t subresource_idx, bool all_aspects);
VkImageAspectFlags vk_image_aspect_flags_from_d3d12(
        const struct vkd3d_format *format, uint32_t plane_idx);
VkImageSubresource vk_image_subresource_from_d3d12(
        const struct vkd3d_format *format, uint32_t subresource_idx,
        unsigned int miplevel_count, unsigned int layer_count,
        bool all_aspects);
VkImageSubresourceLayers vk_image_subresource_layers_from_d3d12(
        const struct vkd3d_format *format, unsigned int sub_resource_idx,
        unsigned int miplevel_count, unsigned int layer_count);
VkImageLayout vk_image_layout_from_d3d12_resource_state(
        struct d3d12_command_list *list, const struct d3d12_resource *resource, D3D12_RESOURCE_STATES state);
UINT d3d12_plane_index_from_vk_aspect(VkImageAspectFlagBits aspect);

HRESULT d3d12_resource_create_borrowed(struct d3d12_device *device, const D3D12_RESOURCE_DESC1 *desc,
        UINT64 vk_handle, struct d3d12_resource **resource);
HRESULT d3d12_resource_create_committed(struct d3d12_device *device, const D3D12_RESOURCE_DESC1 *desc,
        const D3D12_HEAP_PROPERTIES *heap_properties, D3D12_HEAP_FLAGS heap_flags, D3D12_RESOURCE_STATES initial_state,
        const D3D12_CLEAR_VALUE *optimized_clear_value,
        UINT num_castable_formats, const DXGI_FORMAT *castable_formats,
        HANDLE shared_handle, struct d3d12_resource **resource);
HRESULT d3d12_resource_create_placed(struct d3d12_device *device, const D3D12_RESOURCE_DESC1 *desc,
        struct d3d12_heap *heap, uint64_t heap_offset, D3D12_RESOURCE_STATES initial_state,
        const D3D12_CLEAR_VALUE *optimized_clear_value,
        UINT num_castable_formats, const DXGI_FORMAT *castable_formats,
        struct d3d12_resource **resource);
HRESULT d3d12_resource_create_reserved(struct d3d12_device *device,
        const D3D12_RESOURCE_DESC1 *desc, D3D12_RESOURCE_STATES initial_state,
        const D3D12_CLEAR_VALUE *optimized_clear_value,
        UINT num_castable_formats, const DXGI_FORMAT *castable_formats,
        struct d3d12_resource **resource);

static inline struct d3d12_resource *impl_from_ID3D12Resource2(ID3D12Resource2 *iface)
{
    extern CONST_VTBL struct ID3D12Resource2Vtbl d3d12_resource_vtbl;
    if (!iface)
        return NULL;
    assert(iface->lpVtbl == &d3d12_resource_vtbl);
    return CONTAINING_RECORD(iface, struct d3d12_resource, ID3D12Resource_iface);
}

static inline struct d3d12_resource *impl_from_ID3D12Resource(ID3D12Resource *iface)
{
    return impl_from_ID3D12Resource2((ID3D12Resource2 *)iface);
}

HRESULT vkd3d_allocate_device_memory(struct d3d12_device *device,
        VkDeviceSize size, VkMemoryPropertyFlags type_flags, uint32_t type_mask,
        void *pNext, bool respect_budget, struct vkd3d_device_memory_allocation *allocation);
void vkd3d_free_device_memory(struct d3d12_device *device,
        const struct vkd3d_device_memory_allocation *allocation);
HRESULT vkd3d_allocate_internal_buffer_memory(struct d3d12_device *device, VkBuffer vk_buffer,
        VkMemoryPropertyFlags type_flags,
        struct vkd3d_device_memory_allocation *allocation);
HRESULT vkd3d_create_buffer(struct d3d12_device *device,
        const D3D12_HEAP_PROPERTIES *heap_properties, D3D12_HEAP_FLAGS heap_flags,
        const D3D12_RESOURCE_DESC1 *desc, const char *tag, VkBuffer *vk_buffer);
HRESULT vkd3d_create_buffer_explicit_usage(struct d3d12_device *device,
        VkBufferUsageFlags2KHR vk_usage, VkDeviceSize vk_size, const char *tag, VkBuffer *vk_buffer);
HRESULT vkd3d_get_image_allocation_info(struct d3d12_device *device,
        const D3D12_RESOURCE_DESC1 *desc,
        UINT num_castable_formats, const DXGI_FORMAT *castable_formats,
        D3D12_RESOURCE_ALLOCATION_INFO *allocation_info);

enum vkd3d_view_type
{
    VKD3D_VIEW_TYPE_BUFFER,
    VKD3D_VIEW_TYPE_IMAGE,
    VKD3D_VIEW_TYPE_SAMPLER,
    VKD3D_VIEW_TYPE_ACCELERATION_STRUCTURE
};

struct vkd3d_view
{
    LONG refcount;
    enum vkd3d_view_type type;
    uint64_t cookie;

    union
    {
        VkBufferView vk_buffer_view;
        VkImageView vk_image_view;
        VkSampler vk_sampler;
        VkAccelerationStructureKHR vk_acceleration_structure;
    };
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
            VkImageAspectFlags aspect_mask;
            unsigned int miplevel_idx;
            unsigned int layer_idx;
            unsigned int layer_count;
            unsigned int w_offset;
            unsigned int w_size;
        } texture;
    } info;
};

void vkd3d_view_decref(struct vkd3d_view *view, struct d3d12_device *device);
void vkd3d_view_incref(struct vkd3d_view *view);

struct vkd3d_buffer_view_desc
{
    VkBuffer buffer;
    const struct vkd3d_format *format;
    VkDeviceSize offset;
    VkDeviceSize size;
};

struct vkd3d_texture_view_desc
{
    VkImage image;
    VkImageViewType view_type;
    VkImageAspectFlags aspect_mask;
    VkImageUsageFlags image_usage;
    const struct vkd3d_format *format;
    unsigned int miplevel_idx;
    unsigned int miplevel_count;
    unsigned int layer_idx;
    unsigned int layer_count;
    unsigned int w_offset;
    unsigned int w_size;
    float miplevel_clamp;
    VkComponentMapping components;
    bool allowed_swizzle;
};

bool vkd3d_create_buffer_view(struct d3d12_device *device,
        const struct vkd3d_buffer_view_desc *desc, struct vkd3d_view **view);
bool vkd3d_create_raw_r32ui_vk_buffer_view(struct d3d12_device *device,
        VkBuffer vk_buffer, VkDeviceSize offset, VkDeviceSize range, VkBufferView *vk_view);
bool vkd3d_create_acceleration_structure_view(struct d3d12_device *device,
        const struct vkd3d_buffer_view_desc *desc, struct vkd3d_view **view);
bool vkd3d_create_texture_view(struct d3d12_device *device,
        const struct vkd3d_texture_view_desc *desc, struct vkd3d_view **view);

enum vkd3d_descriptor_flag
{
    VKD3D_DESCRIPTOR_FLAG_IMAGE_VIEW        = (1 << 0),
    VKD3D_DESCRIPTOR_FLAG_RAW_VA_AUX_BUFFER = (1 << 1),
    VKD3D_DESCRIPTOR_FLAG_BUFFER_OFFSET     = (1 << 2),
    VKD3D_DESCRIPTOR_FLAG_BUFFER_VA_RANGE   = (1 << 3),
    VKD3D_DESCRIPTOR_FLAG_NON_NULL          = (1 << 4),
    VKD3D_DESCRIPTOR_FLAG_SINGLE_DESCRIPTOR = (1 << 5),
};

struct vkd3d_descriptor_binding
{
    uint8_t set;
    uint8_t binding;
};

#define VKD3D_RESOURCE_DESC_INCREMENT_LOG2 5
#define VKD3D_RESOURCE_DESC_INCREMENT (1u << VKD3D_RESOURCE_DESC_INCREMENT_LOG2)
#define VKD3D_RESOURCE_EMBEDDED_METADATA_OFFSET_LOG2_MASK 31
#define VKD3D_RESOURCE_EMBEDDED_RESOURCE_HEAP_MASK (1ull << 32)
#define VKD3D_RESOURCE_EMBEDDED_CACHED_MASK ((vkd3d_cpu_descriptor_va_t)1)

/* Arrange data so that it can pack as tightly as possible.
 * When we copy descriptors, we must copy both structures.
 * In copy_desc_range we scan through the entire metadata_binding, so
 * this data structure should be small. */
struct vkd3d_descriptor_metadata_types
{
    VkDescriptorType current_null_type;
    uint8_t set_info_mask;
    /* If SINGLE_DESCRIPTOR is set, use the embedded write info instead
     * to avoid missing caches. */
    struct vkd3d_descriptor_binding single_binding;
};
STATIC_ASSERT(sizeof(struct vkd3d_descriptor_metadata_types) == 8);
/* Our use of 8-bit mask relies on MAX_BINDLESS_DESCRIPTOR_SETS fitting. */
STATIC_ASSERT(VKD3D_MAX_BINDLESS_DESCRIPTOR_SETS <= 8);

struct vkd3d_descriptor_metadata_buffer_view
{
    uint8_t flags;
    /* Format used if used in a formatted context such as UAV clears.
     * If UNKNOWN, denotes a raw buffer. R32_UINT can be used in place of it. */
    uint8_t dxgi_format; /* All valid formats fit in 8-bit. */
    uint16_t padding;
    /* Allows tighter packing. 64-bit view range is not supported in D3D12. */
    uint32_t range;
    VkDeviceAddress va;
};

struct vkd3d_descriptor_metadata_image_view
{
    uint8_t flags;
    struct vkd3d_view *view;
};

struct vkd3d_descriptor_metadata_view
{
#ifdef VKD3D_ENABLE_DESCRIPTOR_QA
    uint64_t qa_cookie;
#endif
    union
    {
        struct vkd3d_descriptor_metadata_buffer_view buffer;
        struct vkd3d_descriptor_metadata_image_view image;
        uint8_t flags;
    } info;
};

#ifdef VKD3D_ENABLE_DESCRIPTOR_QA
STATIC_ASSERT(sizeof(struct vkd3d_descriptor_metadata_view) == 24);
static inline void vkd3d_descriptor_metadata_view_set_qa_cookie(
        struct vkd3d_descriptor_metadata_view *view, uint64_t cookie)
{
    view->qa_cookie = cookie;
}
#else
STATIC_ASSERT(sizeof(struct vkd3d_descriptor_metadata_view) == 16);
#define vkd3d_descriptor_metadata_view_set_qa_cookie(view, cookie) ((void)0)
#endif

typedef uintptr_t vkd3d_cpu_descriptor_va_t;

void d3d12_desc_copy(vkd3d_cpu_descriptor_va_t dst, vkd3d_cpu_descriptor_va_t src,
        unsigned int count, D3D12_DESCRIPTOR_HEAP_TYPE heap_type, struct d3d12_device *device);
void d3d12_desc_copy_single(vkd3d_cpu_descriptor_va_t dst,
        vkd3d_cpu_descriptor_va_t src, struct d3d12_device *device);

void d3d12_desc_create_cbv(vkd3d_cpu_descriptor_va_t descriptor,
        struct d3d12_device *device, const D3D12_CONSTANT_BUFFER_VIEW_DESC *desc);
void d3d12_desc_create_srv(vkd3d_cpu_descriptor_va_t descriptor,
        struct d3d12_device *device, struct d3d12_resource *resource,
        const D3D12_SHADER_RESOURCE_VIEW_DESC *desc);
void d3d12_desc_create_uav(vkd3d_cpu_descriptor_va_t descriptor, struct d3d12_device *device,
        struct d3d12_resource *resource, struct d3d12_resource *counter_resource,
        const D3D12_UNORDERED_ACCESS_VIEW_DESC *desc);
void d3d12_desc_create_sampler(vkd3d_cpu_descriptor_va_t sampler,
        struct d3d12_device *device, const D3D12_SAMPLER_DESC2 *desc);

void d3d12_desc_create_cbv_embedded(vkd3d_cpu_descriptor_va_t descriptor,
        struct d3d12_device *device, const D3D12_CONSTANT_BUFFER_VIEW_DESC *desc);
void d3d12_desc_create_srv_embedded(vkd3d_cpu_descriptor_va_t descriptor,
        struct d3d12_device *device, struct d3d12_resource *resource,
        const D3D12_SHADER_RESOURCE_VIEW_DESC *desc);
void d3d12_desc_create_uav_embedded(vkd3d_cpu_descriptor_va_t descriptor, struct d3d12_device *device,
        struct d3d12_resource *resource, struct d3d12_resource *counter_resource,
        const D3D12_UNORDERED_ACCESS_VIEW_DESC *desc);
void d3d12_desc_create_sampler_embedded(vkd3d_cpu_descriptor_va_t sampler,
        struct d3d12_device *device, const D3D12_SAMPLER_DESC2 *desc);

bool vkd3d_create_vk_buffer_view(struct d3d12_device *device,
        VkBuffer vk_buffer, const struct vkd3d_format *format,
        VkDeviceSize offset, VkDeviceSize range, VkBufferView *vk_view);
bool vkd3d_create_raw_buffer_view(struct d3d12_device *device,
        D3D12_GPU_VIRTUAL_ADDRESS gpu_address, VkBufferView *vk_buffer_view);
HRESULT d3d12_create_static_sampler(struct d3d12_device *device,
        const D3D12_STATIC_SAMPLER_DESC1 *desc, VkSampler *vk_sampler);

#define D3D12_DESC_ALIGNMENT 64
struct d3d12_rtv_desc
{
    DECLSPEC_ALIGN(D3D12_DESC_ALIGNMENT) VkSampleCountFlagBits sample_count;
    const struct vkd3d_format *format;
    unsigned int width;
    unsigned int height;
    unsigned int layer_count;
    unsigned int plane_write_enable;
    struct vkd3d_view *view;
    struct d3d12_resource *resource;
};
STATIC_ASSERT(sizeof(struct d3d12_rtv_desc) == D3D12_DESC_ALIGNMENT);

void d3d12_rtv_desc_copy(struct d3d12_rtv_desc *dst, struct d3d12_rtv_desc *src, unsigned int count);

static inline struct d3d12_rtv_desc *d3d12_rtv_desc_from_cpu_handle(D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle)
{
    return (struct d3d12_rtv_desc *)cpu_handle.ptr;
}

void d3d12_rtv_desc_create_rtv(struct d3d12_rtv_desc *rtv_desc, struct d3d12_device *device,
        struct d3d12_resource *resource, const D3D12_RENDER_TARGET_VIEW_DESC *desc);

void d3d12_rtv_desc_create_dsv(struct d3d12_rtv_desc *dsv_desc, struct d3d12_device *device,
        struct d3d12_resource *resource, const D3D12_DEPTH_STENCIL_VIEW_DESC *desc);

struct vkd3d_bound_buffer_range
{
    uint32_t byte_offset;
    uint32_t byte_count;
    uint32_t element_offset;
    uint32_t element_count;
};

struct vkd3d_host_visible_buffer_range
{
    VkDescriptorBufferInfo descriptor;
    void *host_ptr;
};

union vkd3d_descriptor_info
{
    VkBufferView buffer_view;
    VkDescriptorBufferInfo buffer;
    VkDescriptorImageInfo image;
    VkDeviceAddress va;
};

/* ID3D12DescriptorHeap */
struct d3d12_null_descriptor_template
{
    union
    {
        struct
        {
            uint8_t *dst_base;
            size_t desc_size;
            /* If NULL, mutable descriptor type, pull appropriate payload from bindless_state. */
            const uint8_t *src_payload;
        } payloads[VKD3D_MAX_BINDLESS_DESCRIPTOR_SETS];

        struct
        {
            struct VkWriteDescriptorSet writes[VKD3D_MAX_BINDLESS_DESCRIPTOR_SETS];
            VkDescriptorBufferInfo buffer;
            VkDescriptorImageInfo image;
            VkBufferView buffer_view;
        } descriptors;
    } writes;

    unsigned int num_writes;
    unsigned int set_info_mask;
    bool has_mutable_descriptors;
    bool has_descriptor_buffer;
};

typedef void (*pfn_vkd3d_host_mapping_copy_template)(void * restrict dst, const void * restrict src,
        size_t dst_index, size_t src_index, size_t count);
typedef void (*pfn_vkd3d_host_mapping_copy_template_single)(void * restrict dst, const void * restrict src,
        size_t dst_index, size_t src_index);

struct d3d12_descriptor_heap_set
{
    VkDescriptorSet vk_descriptor_set;
    size_t stride;
    void *mapped_set;
    pfn_vkd3d_host_mapping_copy_template copy_template;
    pfn_vkd3d_host_mapping_copy_template_single copy_template_single;
};

struct d3d12_descriptor_heap
{
    /* Used by special optimizations where we can take advantage of knowledge of the binding model
     * without awkward lookups. Optimized vtable overrides define what these pointers mean. */
    void *fast_pointer_bank[3];

    ID3D12DescriptorHeap ID3D12DescriptorHeap_iface;
    LONG refcount;

    uint64_t gpu_va;
    D3D12_DESCRIPTOR_HEAP_DESC desc;
    D3D12_CPU_DESCRIPTOR_HANDLE cpu_va;

    struct
    {
        VkBuffer vk_buffer;
        VkDeviceAddress va;
        struct vkd3d_device_memory_allocation device_allocation;
        uint8_t *host_allocation;
        VkDeviceSize offsets[VKD3D_MAX_BINDLESS_DESCRIPTOR_SETS];
    } descriptor_buffer;

    VkDescriptorPool vk_descriptor_pool;
    struct d3d12_descriptor_heap_set sets[VKD3D_MAX_BINDLESS_DESCRIPTOR_SETS];

    struct vkd3d_device_memory_allocation device_allocation;
    VkBuffer vk_buffer;
    void *host_memory;

    struct vkd3d_host_visible_buffer_range raw_va_aux_buffer;
    struct vkd3d_host_visible_buffer_range buffer_ranges;
#ifdef VKD3D_ENABLE_DESCRIPTOR_QA
    struct vkd3d_host_visible_buffer_range descriptor_heap_info;
    uint64_t cookie;
#endif

    struct d3d12_null_descriptor_template null_descriptor_template;

    struct d3d12_device *device;

    struct vkd3d_private_store private_store;
    struct d3d_destruction_notifier destruction_notifier;

    /* Here we pack metadata data structures for CBV_SRV_UAV and SAMPLER.
     * For RTV/DSV heaps, we just encode rtv_desc structs inline. */
    DECLSPEC_ALIGN(D3D12_DESC_ALIGNMENT) BYTE descriptors[];
};

HRESULT d3d12_descriptor_heap_create(struct d3d12_device *device,
        const D3D12_DESCRIPTOR_HEAP_DESC *desc, struct d3d12_descriptor_heap **descriptor_heap);
void d3d12_descriptor_heap_cleanup(struct d3d12_descriptor_heap *descriptor_heap);
bool d3d12_descriptor_heap_require_padding_descriptors(void);

static inline struct d3d12_descriptor_heap *impl_from_ID3D12DescriptorHeap(ID3D12DescriptorHeap *iface)
{
    extern CONST_VTBL struct ID3D12DescriptorHeapVtbl d3d12_descriptor_heap_vtbl;
    if (!iface)
        return NULL;
    assert(iface->lpVtbl == &d3d12_descriptor_heap_vtbl);
    return CONTAINING_RECORD(iface, struct d3d12_descriptor_heap, ID3D12DescriptorHeap_iface);
}

/* Decodes descriptor heap VA (for resources only) and its offset.
 * Somewhat cursed, but avoids any de-referencing to achieve this result.
 * See d3d12_descriptor_heap_create for comments on how this works. */

struct d3d12_desc_split
{
    struct d3d12_descriptor_heap *heap;
    struct vkd3d_descriptor_metadata_types *types;
    struct vkd3d_descriptor_metadata_view *view;
    uint32_t offset;
};

struct d3d12_desc_split_embedded
{
    uint8_t *payload;
    struct vkd3d_descriptor_metadata_view *metadata;
};

static inline struct d3d12_desc_split_embedded d3d12_desc_decode_embedded_resource_va(vkd3d_cpu_descriptor_va_t va)
{
    struct d3d12_desc_split_embedded split;

    uint32_t log2_offset = va & VKD3D_RESOURCE_EMBEDDED_METADATA_OFFSET_LOG2_MASK;

    if (log2_offset > VKD3D_RESOURCE_EMBEDDED_CACHED_MASK)
    {
        va -= log2_offset;
        split.payload = (uint8_t *)va;
        va += 1u << log2_offset;
        split.metadata = (struct vkd3d_descriptor_metadata_view *)va;
    }
    else
    {
        va &= ~VKD3D_RESOURCE_EMBEDDED_CACHED_MASK;
        /* Shader visible VA. We don't care about metadata at this point. */
        split.metadata = NULL;
        split.payload = (uint8_t *)va;
    }

    return split;
}

static inline void d3d12_desc_copy_embedded_resource(vkd3d_cpu_descriptor_va_t dst_va,
        vkd3d_cpu_descriptor_va_t src_va, size_t size)
{
    struct d3d12_desc_split_embedded dst, src;

    dst = d3d12_desc_decode_embedded_resource_va(dst_va);
    src = d3d12_desc_decode_embedded_resource_va(src_va);

    /* Copy metadata if we're doing CPU -> CPU descriptor copy.
     * Copying from GPU descriptor heap is not allowed. */
    if (VKD3D_EXPECT_TRUE(dst.metadata == NULL))
    {
        /* If we don't have metadata it means we're writing to a descriptor buffer,
         * prefer NT writes. */
        vkd3d_memcpy_aligned_non_temporal(dst.payload, src.payload, size);
    }
    else
    {
        vkd3d_memcpy_aligned_cached(dst.payload, src.payload, size);
        vkd3d_memcpy_aligned_cached(dst.metadata, src.metadata, size);
    }
}

static inline void d3d12_desc_copy_embedded_resource_single_32(vkd3d_cpu_descriptor_va_t dst_va,
        vkd3d_cpu_descriptor_va_t src_va)
{
    struct d3d12_desc_split_embedded dst, src;

    dst = d3d12_desc_decode_embedded_resource_va(dst_va);
    src = d3d12_desc_decode_embedded_resource_va(src_va);

    /* Copy metadata if we're doing CPU -> CPU descriptor copy.
     * Copying from GPU descriptor heap is not allowed. */
    if (VKD3D_EXPECT_TRUE(dst.metadata == NULL))
    {
        /* If we don't have metadata it means we're writing to a descriptor buffer,
         * prefer NT writes. */
        vkd3d_memcpy_aligned_32_non_temporal(dst.payload, src.payload);
    }
    else
    {
        vkd3d_memcpy_aligned_32_cached(dst.payload, src.payload);
        vkd3d_memcpy_aligned_32_cached(dst.metadata, src.metadata);
    }
}

static inline struct d3d12_desc_split d3d12_desc_decode_va(vkd3d_cpu_descriptor_va_t va)
{
    uintptr_t num_bits_descriptors;
    struct d3d12_desc_split split;
    uintptr_t heap_offset;
    uintptr_t heap_va;

    /* 5 LSBs encode number of bits for descriptors.
     * Over that, we have the heap offset (increment size is 32).
     * Above that, we have the d3d12_descriptor_heap, which is allocated with enough alignment
     * to contain these twiddle bits. */

    num_bits_descriptors = va & (VKD3D_RESOURCE_DESC_INCREMENT - 1);
    heap_offset = (va >> VKD3D_RESOURCE_DESC_INCREMENT_LOG2) & (((size_t)1 << num_bits_descriptors) - 1);
    split.offset = (uint32_t)heap_offset;

    heap_va = va & ~(((size_t)1 << (num_bits_descriptors + VKD3D_RESOURCE_DESC_INCREMENT_LOG2)) - 1);
    split.heap = (struct d3d12_descriptor_heap *)heap_va;
    heap_va += offsetof(struct d3d12_descriptor_heap, descriptors);
    split.types = (struct vkd3d_descriptor_metadata_types *)heap_va;
    split.types += heap_offset;
    heap_va += sizeof(struct vkd3d_descriptor_metadata_types) << num_bits_descriptors;
    split.view = (struct vkd3d_descriptor_metadata_view *)heap_va;
    split.view += heap_offset;

    return split;
}

static inline uint32_t d3d12_desc_heap_offset_from_embedded_gpu_handle(D3D12_GPU_DESCRIPTOR_HANDLE handle,
        unsigned int cbv_srv_uav_size_log2, unsigned int sampler_size_log2)
{
    uint32_t low_word = (uint32_t)handle.ptr;
    if (handle.ptr & VKD3D_RESOURCE_EMBEDDED_RESOURCE_HEAP_MASK)
        return low_word >> cbv_srv_uav_size_log2;
    else
        return low_word >> sampler_size_log2;
}

static inline uint32_t d3d12_desc_heap_offset_from_gpu_handle(D3D12_GPU_DESCRIPTOR_HANDLE handle)
{
    return (uint32_t)handle.ptr / VKD3D_RESOURCE_DESC_INCREMENT;
}

static inline void *d3d12_descriptor_heap_get_mapped_payload(struct d3d12_descriptor_heap *heap,
        unsigned int set_index, unsigned int desc_index)
{
    uint8_t *payload = heap->sets[set_index].mapped_set;
    payload += desc_index * heap->sets[set_index].stride;
    return payload;
}

/* ID3D12QueryHeap */
struct d3d12_query_heap
{
    ID3D12QueryHeap ID3D12QueryHeap_iface;
    LONG refcount;

    D3D12_QUERY_HEAP_DESC desc;
    VkQueryPool vk_query_pool;
    struct vkd3d_device_memory_allocation device_allocation;
    VkBuffer vk_buffer;
    VkDeviceAddress va;
    uint64_t cookie;
    uint32_t initialized;

    struct d3d12_device *device;

    struct vkd3d_private_store private_store;
    struct d3d_destruction_notifier destruction_notifier;
};

HRESULT d3d12_query_heap_create(struct d3d12_device *device, const D3D12_QUERY_HEAP_DESC *desc,
        struct d3d12_query_heap **heap);

static inline struct d3d12_query_heap *impl_from_ID3D12QueryHeap(ID3D12QueryHeap *iface)
{
    extern CONST_VTBL struct ID3D12QueryHeapVtbl d3d12_query_heap_vtbl;
    if (!iface)
        return NULL;
    assert(iface->lpVtbl == &d3d12_query_heap_vtbl);
    return CONTAINING_RECORD(iface, struct d3d12_query_heap, ID3D12QueryHeap_iface);
}

static inline size_t d3d12_query_heap_type_get_data_size(D3D12_QUERY_HEAP_TYPE heap_type)
{
    switch (heap_type)
    {
        case D3D12_QUERY_HEAP_TYPE_OCCLUSION:
        case D3D12_QUERY_HEAP_TYPE_TIMESTAMP:
        case D3D12_QUERY_HEAP_TYPE_COPY_QUEUE_TIMESTAMP:
            return sizeof(uint64_t);
        case D3D12_QUERY_HEAP_TYPE_PIPELINE_STATISTICS:
            return sizeof(D3D12_QUERY_DATA_PIPELINE_STATISTICS);
        case D3D12_QUERY_HEAP_TYPE_SO_STATISTICS:
            return sizeof(D3D12_QUERY_DATA_SO_STATISTICS);
        default:
            ERR("Unhandled query pool type %u.\n", heap_type);
            return 0;
    }
}

static inline bool d3d12_query_heap_type_is_inline(D3D12_QUERY_HEAP_TYPE heap_type)
{
    return heap_type == D3D12_QUERY_HEAP_TYPE_OCCLUSION ||
            heap_type == D3D12_QUERY_HEAP_TYPE_SO_STATISTICS;
}

enum vkd3d_root_signature_flag
{
    VKD3D_ROOT_SIGNATURE_USE_PUSH_CONSTANT_UNIFORM_BLOCK = 0x00000001u,
    VKD3D_ROOT_SIGNATURE_USE_SSBO_OFFSET_BUFFER          = 0x00000002u,
    VKD3D_ROOT_SIGNATURE_USE_TYPED_OFFSET_BUFFER         = 0x00000004u,
};

enum vkd3d_pipeline_type
{
    VKD3D_PIPELINE_TYPE_NONE,
    VKD3D_PIPELINE_TYPE_GRAPHICS,
    VKD3D_PIPELINE_TYPE_MESH_GRAPHICS,
    VKD3D_PIPELINE_TYPE_COMPUTE,
    VKD3D_PIPELINE_TYPE_RAY_TRACING,
};

static inline VkPipelineBindPoint vk_bind_point_from_pipeline_type(enum vkd3d_pipeline_type pipeline_type)
{
    switch (pipeline_type)
    {
        case VKD3D_PIPELINE_TYPE_NONE:
          break;
        case VKD3D_PIPELINE_TYPE_GRAPHICS:
        case VKD3D_PIPELINE_TYPE_MESH_GRAPHICS:
            return VK_PIPELINE_BIND_POINT_GRAPHICS;
        case VKD3D_PIPELINE_TYPE_COMPUTE:
            return VK_PIPELINE_BIND_POINT_COMPUTE;
        case VKD3D_PIPELINE_TYPE_RAY_TRACING:
            return VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR;
    }

    return VK_PIPELINE_BIND_POINT_MAX_ENUM;
}

/* ID3D12RootSignature */
struct d3d12_bind_point_layout
{
    VkPipelineLayout vk_pipeline_layout;
    VkShaderStageFlags vk_push_stages;
    unsigned int flags; /* vkd3d_root_signature_flag */
    uint32_t num_set_layouts;
    VkPushConstantRange push_constant_range;
};

#define VKD3D_MAX_HOISTED_DESCRIPTORS 16
struct vkd3d_descriptor_hoist_desc
{
    uint32_t table_index;
    uint32_t table_offset;
    uint32_t parameter_index;
};

struct vkd3d_descriptor_hoist_info
{
    struct vkd3d_descriptor_hoist_desc desc[VKD3D_MAX_HOISTED_DESCRIPTORS];
    unsigned int num_desc;
};

struct d3d12_root_signature
{
    ID3D12RootSignature ID3D12RootSignature_iface;
    LONG refcount;
    LONG internal_refcount;

    /* Compatibility for exact match. For PSO blob validation. */
    vkd3d_shader_hash_t pso_compatibility_hash;
    /* Compatiblity for ABI in RTPSOs. Match if the VkPipelineLayouts are equivalent. */
    vkd3d_shader_hash_t layout_compatibility_hash;

    struct d3d12_bind_point_layout graphics, mesh, compute, raygen;
    VkDescriptorSetLayout vk_sampler_descriptor_layout;
    VkDescriptorSetLayout vk_root_descriptor_layout;

    VkDescriptorPool vk_sampler_pool;
    VkDescriptorSet vk_sampler_set;

    struct vkd3d_shader_root_parameter *parameters;
    unsigned int parameter_count;

    uint32_t sampler_descriptor_set;
    uint32_t root_descriptor_set;

    uint64_t descriptor_table_mask;
    uint64_t root_constant_mask;
    uint64_t root_descriptor_raw_va_mask;
    uint64_t root_descriptor_push_mask;

    D3D12_ROOT_SIGNATURE_FLAGS d3d12_flags;

    unsigned int binding_count;
    struct vkd3d_shader_resource_binding *bindings;

    unsigned int root_constant_count;
    struct vkd3d_shader_push_constant_buffer *root_constants;

    struct vkd3d_shader_descriptor_binding push_constant_ubo_binding;
    struct vkd3d_shader_descriptor_binding raw_va_aux_buffer_binding;
    struct vkd3d_shader_descriptor_binding offset_buffer_binding;
#ifdef VKD3D_ENABLE_DESCRIPTOR_QA
    struct vkd3d_shader_descriptor_binding descriptor_qa_payload_binding;
    struct vkd3d_shader_descriptor_binding descriptor_qa_control_binding;
#endif

    VkDescriptorSetLayout set_layouts[VKD3D_MAX_DESCRIPTOR_SETS];

    uint32_t descriptor_table_offset;
    uint32_t descriptor_table_count;

    unsigned int static_sampler_count;
    D3D12_STATIC_SAMPLER_DESC1 *static_samplers_desc;
    VkSampler *static_samplers;

    struct vkd3d_descriptor_hoist_info hoist_info;

    struct d3d12_device *device;

    struct vkd3d_private_store private_store;
    struct d3d_destruction_notifier destruction_notifier;
};

HRESULT d3d12_root_signature_create(struct d3d12_device *device, const void *bytecode,
        size_t bytecode_length, struct d3d12_root_signature **root_signature);
HRESULT d3d12_root_signature_create_raw(struct d3d12_device *device, const void *payload,
        size_t payload_size, struct d3d12_root_signature **root_signature);
HRESULT d3d12_root_signature_create_empty(struct d3d12_device *device,
        struct d3d12_root_signature **root_signature);
/* Private ref counts, for pipeline library. */
void d3d12_root_signature_inc_ref(struct d3d12_root_signature *state);
void d3d12_root_signature_dec_ref(struct d3d12_root_signature *state);

static inline struct d3d12_root_signature *impl_from_ID3D12RootSignature(ID3D12RootSignature *iface)
{
    extern CONST_VTBL struct ID3D12RootSignatureVtbl d3d12_root_signature_vtbl;
    if (!iface)
        return NULL;
    assert(iface->lpVtbl == &d3d12_root_signature_vtbl);
    return CONTAINING_RECORD(iface, struct d3d12_root_signature, ID3D12RootSignature_iface);
}

unsigned int d3d12_root_signature_get_shader_interface_flags(const struct d3d12_root_signature *root_signature,
        enum vkd3d_pipeline_type pipeline_type);
HRESULT d3d12_root_signature_create_local_static_samplers_layout(struct d3d12_root_signature *root_signature,
        VkDescriptorSetLayout vk_set_layout, VkPipelineLayout *vk_pipeline_layout);
HRESULT d3d12_root_signature_create_work_graph_layout(struct d3d12_root_signature *root_signature,
        VkDescriptorSetLayout *vk_push_set_layout, VkPipelineLayout *vk_pipeline_layout);
HRESULT vkd3d_create_pipeline_layout(struct d3d12_device *device,
        unsigned int set_layout_count, const VkDescriptorSetLayout *set_layouts,
        unsigned int push_constant_count, const VkPushConstantRange *push_constants,
        VkPipelineLayout *pipeline_layout);

VkShaderStageFlags vkd3d_vk_stage_flags_from_visibility(D3D12_SHADER_VISIBILITY visibility);
enum vkd3d_shader_visibility vkd3d_shader_visibility_from_d3d12(D3D12_SHADER_VISIBILITY visibility);
HRESULT vkd3d_create_descriptor_set_layout(struct d3d12_device *device,
        VkDescriptorSetLayoutCreateFlags flags, unsigned int binding_count,
        const VkDescriptorSetLayoutBinding *bindings,
        VkDescriptorSetLayoutCreateFlags descriptor_buffer_flags,
        VkDescriptorSetLayout *set_layout);

static inline const struct d3d12_bind_point_layout *d3d12_root_signature_get_layout(
        const struct d3d12_root_signature *root_signature, enum vkd3d_pipeline_type pipeline_type)
{
    switch (pipeline_type)
    {
        case VKD3D_PIPELINE_TYPE_NONE:
            return NULL;

        case VKD3D_PIPELINE_TYPE_GRAPHICS:
            return &root_signature->graphics;

        case VKD3D_PIPELINE_TYPE_MESH_GRAPHICS:
            return &root_signature->mesh;

        case VKD3D_PIPELINE_TYPE_COMPUTE:
            return &root_signature->compute;

        case VKD3D_PIPELINE_TYPE_RAY_TRACING:
            return &root_signature->raygen;
    }

    return NULL;
}

static inline bool d3d12_root_signature_is_pipeline_compatible(
        const struct d3d12_root_signature *a, const struct d3d12_root_signature *b)
{
    if (a && a->pso_compatibility_hash == 0)
        a = NULL;
    if (b && b->pso_compatibility_hash == 0)
        b = NULL;

    if (!a && !b)
        return true;
    else if ((!!a) != (!!b))
        return false;
    else
        return a->pso_compatibility_hash == b->pso_compatibility_hash;
}

static inline bool d3d12_root_signature_is_layout_compatible(
        const struct d3d12_root_signature *a, const struct d3d12_root_signature *b)
{
    if (a && a->layout_compatibility_hash == 0)
        a = NULL;
    if (b && b->layout_compatibility_hash == 0)
        b = NULL;

    if (!a && !b)
        return true;
    else if ((!!a) != (!!b))
        return false;
    else
        return a->layout_compatibility_hash == b->layout_compatibility_hash;
}

enum vkd3d_dynamic_state_flag
{
    VKD3D_DYNAMIC_STATE_VIEWPORT              = (1 << 0),
    VKD3D_DYNAMIC_STATE_SCISSOR               = (1 << 1),
    VKD3D_DYNAMIC_STATE_BLEND_CONSTANTS       = (1 << 2),
    VKD3D_DYNAMIC_STATE_STENCIL_REFERENCE     = (1 << 3),
    VKD3D_DYNAMIC_STATE_DEPTH_BOUNDS          = (1 << 4),
    VKD3D_DYNAMIC_STATE_TOPOLOGY              = (1 << 5),
    VKD3D_DYNAMIC_STATE_VERTEX_BUFFER_STRIDE  = (1 << 6),
    VKD3D_DYNAMIC_STATE_FRAGMENT_SHADING_RATE = (1 << 7),
    VKD3D_DYNAMIC_STATE_PRIMITIVE_RESTART     = (1 << 8),
    VKD3D_DYNAMIC_STATE_PATCH_CONTROL_POINTS  = (1 << 9),
    VKD3D_DYNAMIC_STATE_DEPTH_WRITE_ENABLE    = (1 << 10),
    VKD3D_DYNAMIC_STATE_STENCIL_WRITE_MASK    = (1 << 11),
    VKD3D_DYNAMIC_STATE_DEPTH_BIAS            = (1 << 12),
    VKD3D_DYNAMIC_STATE_RASTERIZATION_SAMPLES = (1 << 13),
};

struct vkd3d_shader_debug_ring_spec_constants
{
    uint64_t hash;
    uint64_t atomic_bda;
    uint64_t host_bda;
    uint32_t ring_words;
};

#define VKD3D_MAX_VERTEX_INPUT_DYNAMIC_STATES (3u)
#define VKD3D_VERTEX_INPUT_DYNAMIC_STATE_MASK (VKD3D_DYNAMIC_STATE_TOPOLOGY |\
                VKD3D_DYNAMIC_STATE_VERTEX_BUFFER_STRIDE |\
                VKD3D_DYNAMIC_STATE_PRIMITIVE_RESTART)

struct vkd3d_vertex_input_pipeline_desc
{
    VkVertexInputBindingDivisorDescriptionEXT vi_divisors[D3D12_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT];
    VkPipelineVertexInputDivisorStateCreateInfoEXT vi_divisor_info;

    VkVertexInputAttributeDescription vi_attributes[D3D12_VS_INPUT_REGISTER_COUNT];
    VkVertexInputBindingDescription vi_bindings[D3D12_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT];
    VkPipelineVertexInputStateCreateInfo vi_info;

    VkPipelineInputAssemblyStateCreateInfo ia_info;

    VkDynamicState dy_states[VKD3D_MAX_VERTEX_INPUT_DYNAMIC_STATES];
    VkPipelineDynamicStateCreateInfo dy_info;
};

struct vkd3d_vertex_input_pipeline
{
    struct hash_map_entry entry;
    struct vkd3d_vertex_input_pipeline_desc desc;
    VkPipeline vk_pipeline;
};

uint32_t vkd3d_vertex_input_pipeline_desc_hash(const void *key);
bool vkd3d_vertex_input_pipeline_desc_compare(const void *key, const struct hash_map_entry *entry);
VkPipeline vkd3d_vertex_input_pipeline_create(struct d3d12_device *device,
        const struct vkd3d_vertex_input_pipeline_desc *desc);
void vkd3d_vertex_input_pipeline_free(struct hash_map_entry *entry, void *userdata);

#define VKD3D_MAX_FRAGMENT_OUTPUT_DYNAMIC_STATES (1u)
#define VKD3D_FRAGMENT_OUTPUT_DYNAMIC_STATE_MASK (VKD3D_DYNAMIC_STATE_BLEND_CONSTANTS)

struct vkd3d_fragment_output_pipeline_desc
{
    VkPipelineColorBlendAttachmentState cb_attachments[D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT];
    VkPipelineColorBlendStateCreateInfo cb_info;

    VkSampleMask ms_sample_mask;
    VkPipelineMultisampleStateCreateInfo ms_info;

    VkFormat rt_formats[D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT];
    VkPipelineRenderingCreateInfo rt_info;

    VkDynamicState dy_states[VKD3D_MAX_FRAGMENT_OUTPUT_DYNAMIC_STATES];
    VkPipelineDynamicStateCreateInfo dy_info;
};

struct vkd3d_fragment_output_pipeline
{
    struct hash_map_entry entry;
    struct vkd3d_fragment_output_pipeline_desc desc;
    VkPipeline vk_pipeline;
};

uint32_t vkd3d_fragment_output_pipeline_desc_hash(const void *key);
bool vkd3d_fragment_output_pipeline_desc_compare(const void *key, const struct hash_map_entry *entry);
VkPipeline vkd3d_fragment_output_pipeline_create(struct d3d12_device *device,
        const struct vkd3d_fragment_output_pipeline_desc *desc);
void vkd3d_fragment_output_pipeline_free(struct hash_map_entry *entry, void *userdata);

#define VKD3D_SHADER_DEBUG_RING_SPEC_INFO_MAP_ENTRIES 4
struct vkd3d_shader_debug_ring_spec_info
{
    struct vkd3d_shader_debug_ring_spec_constants constants;
    VkSpecializationMapEntry map_entries[VKD3D_SHADER_DEBUG_RING_SPEC_INFO_MAP_ENTRIES];
    VkSpecializationInfo spec_info;
};

enum vkd3d_plane_optimal_flag
{
    VKD3D_DEPTH_PLANE_OPTIMAL = (1 << 0),
    VKD3D_STENCIL_PLANE_OPTIMAL = (1 << 1),
    VKD3D_DEPTH_STENCIL_PLANE_GENERAL = (1 << 2),
};

struct d3d12_graphics_pipeline_state_cached_desc
{
    /* Information needed to compile to SPIR-V. */
    unsigned int ps_output_swizzle[D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT];
    struct vkd3d_shader_parameter ps_shader_parameters[1];
    bool is_dual_source_blending;
    VkShaderStageFlagBits xfb_stage;
    struct vkd3d_shader_transform_feedback_info *xfb_info;
    struct vkd3d_shader_stage_io_map stage_io_map_ms_ps;

    D3D12_SHADER_BYTECODE bytecode[VKD3D_MAX_SHADER_STAGES];
    VkShaderStageFlagBits bytecode_stages[VKD3D_MAX_SHADER_STAGES];
    uint32_t bytecode_duped_mask;
};

struct d3d12_graphics_pipeline_state
{
    struct vkd3d_shader_debug_ring_spec_info spec_info[VKD3D_MAX_SHADER_STAGES];
    VkPipelineShaderStageCreateInfo stages[VKD3D_MAX_SHADER_STAGES];
    struct vkd3d_shader_code code[VKD3D_MAX_SHADER_STAGES];
    struct vkd3d_shader_code_debug code_debug[VKD3D_MAX_SHADER_STAGES];
    VkShaderStageFlags stage_flags;
    VkShaderModuleIdentifierEXT identifiers[VKD3D_MAX_SHADER_STAGES];
    VkPipelineShaderStageModuleIdentifierCreateInfoEXT identifier_create_infos[VKD3D_MAX_SHADER_STAGES];
    size_t stage_count;

    struct d3d12_graphics_pipeline_state_cached_desc cached_desc;

    VkVertexInputAttributeDescription attributes[D3D12_VS_INPUT_REGISTER_COUNT];
    VkVertexInputRate input_rates[D3D12_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT];
    VkVertexInputBindingDivisorDescriptionEXT instance_divisors[D3D12_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT];
    VkVertexInputBindingDescription attribute_bindings[D3D12_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT];
    uint32_t minimum_vertex_buffer_dynamic_stride[D3D12_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT];
    uint32_t vertex_buffer_stride_align_mask[D3D12_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT];
    size_t instance_divisor_count;
    size_t attribute_binding_count;
    size_t attribute_count;
    D3D12_PRIMITIVE_TOPOLOGY_TYPE primitive_topology_type;
    uint32_t vertex_buffer_mask;

    VkPipelineColorBlendAttachmentState blend_attachments[D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT];
    unsigned int rt_count;
    unsigned int null_attachment_mask;
    unsigned int rtv_active_mask;
    unsigned int patch_vertex_count;
    const struct vkd3d_format *dsv_format;
    VkFormat rtv_formats[D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT];
    uint32_t dsv_plane_optimal_mask;

    D3D12_INDEX_BUFFER_STRIP_CUT_VALUE index_buffer_strip_cut_value;
    VkPipelineRasterizationStateCreateInfo rs_desc;
    VkPipelineMultisampleStateCreateInfo ms_desc;
    VkPipelineDepthStencilStateCreateInfo ds_desc;
    VkPipelineColorBlendStateCreateInfo blend_desc;

    VkSampleMask sample_mask;
    VkPipelineRasterizationConservativeStateCreateInfoEXT rs_conservative_info;
    VkPipelineRasterizationDepthClipStateCreateInfoEXT rs_depth_clip_info;
    VkPipelineRasterizationStateStreamCreateInfoEXT rs_stream_info;
    VkPipelineRasterizationLineStateCreateInfoEXT rs_line_info;
    VkDepthBiasRepresentationInfoEXT rs_depth_bias_info;

    /* vkd3d_dynamic_state_flag */
    uint32_t explicit_dynamic_states;
    uint32_t pipeline_dynamic_states;

    VkPipelineLayout pipeline_layout;
    VkPipeline pipeline;
    VkPipeline library;
    VkGraphicsPipelineLibraryFlagsEXT library_flags;
    VkPipelineCreateFlags library_create_flags;
    struct list compiled_fallback_pipelines;

    unsigned int xfb_buffer_count;

    bool disable_optimization;
};

static inline unsigned int dsv_attachment_mask(const struct d3d12_graphics_pipeline_state *graphics)
{
    return 1u << graphics->rt_count;
}

struct d3d12_compute_pipeline_state
{
    VkPipeline vk_pipeline;
    struct vkd3d_shader_code code;
    struct vkd3d_shader_code_debug code_debug;
    VkShaderModuleIdentifierEXT identifier;
    VkPipelineShaderStageModuleIdentifierCreateInfoEXT identifier_create_info;
};

/* To be able to load a pipeline from cache, this information must match exactly,
 * otherwise, we must regard the PSO as incompatible (which is invalid usage and must be validated). */
struct vkd3d_pipeline_cache_compatibility
{
    uint64_t state_desc_compat_hash;
    uint64_t root_signature_compat_hash;
    uint64_t dxbc_blob_hashes[VKD3D_MAX_SHADER_STAGES];
};

/* ID3D12PipelineState */
struct d3d12_pipeline_state
{
    ID3D12PipelineState ID3D12PipelineState_iface;
    LONG refcount;
    LONG internal_refcount;

    union
    {
        struct d3d12_graphics_pipeline_state graphics;
        struct d3d12_compute_pipeline_state compute;
    };

    enum vkd3d_pipeline_type pipeline_type;
    VkPipelineCache vk_pso_cache;
    rwlock_t lock;

    struct vkd3d_pipeline_cache_compatibility pipeline_cache_compat;
    struct d3d12_root_signature *root_signature;
    struct d3d12_device *device;
    bool root_signature_compat_hash_is_dxbc_derived;
    bool pso_is_loaded_from_cached_blob;
    bool pso_is_fully_dynamic;

    struct vkd3d_private_store private_store;
    struct d3d_destruction_notifier destruction_notifier;
};

HRESULT d3d12_pipeline_state_create_shader_module(struct d3d12_device *device,
        VkShaderModule *vk_module, const struct vkd3d_shader_code *code);

static inline bool d3d12_pipeline_state_is_compute(const struct d3d12_pipeline_state *state)
{
    return state && state->pipeline_type == VKD3D_PIPELINE_TYPE_COMPUTE;
}

static inline bool d3d12_pipeline_state_is_graphics(const struct d3d12_pipeline_state *state)
{
    return state && state->pipeline_type != VKD3D_PIPELINE_TYPE_COMPUTE;
}

/* This returns true for invalid D3D12 API usage. Game intends to use depth-stencil tests,
 * but we don't know the format until bind time. Some games like SottR rely on this to work ... somehow. */
static inline bool d3d12_graphics_pipeline_state_has_unknown_dsv_format_with_test(
        const struct d3d12_graphics_pipeline_state *graphics)
{
    return graphics->null_attachment_mask & dsv_attachment_mask(graphics);
}

/* Private ref counts, for pipeline library. */
ULONG d3d12_pipeline_state_inc_public_ref(struct d3d12_pipeline_state *state);
void d3d12_pipeline_state_inc_ref(struct d3d12_pipeline_state *state);
void d3d12_pipeline_state_dec_ref(struct d3d12_pipeline_state *state);

struct d3d12_cached_pipeline_state
{
    D3D12_CACHED_PIPELINE_STATE blob;
    /* For cached PSO if that blob comes from a library.
     * Might need it to resolve references. */
    struct d3d12_pipeline_library *library;
};

struct d3d12_pipeline_state_desc
{
    ID3D12RootSignature *root_signature;
    D3D12_SHADER_BYTECODE vs;
    D3D12_SHADER_BYTECODE ps;
    D3D12_SHADER_BYTECODE ds;
    D3D12_SHADER_BYTECODE hs;
    D3D12_SHADER_BYTECODE gs;
    D3D12_SHADER_BYTECODE cs;
    D3D12_SHADER_BYTECODE as;
    D3D12_SHADER_BYTECODE ms;
    D3D12_STREAM_OUTPUT_DESC stream_output;
    D3D12_BLEND_DESC blend_state;
    UINT sample_mask;
    D3D12_RASTERIZER_DESC2 rasterizer_state;
    D3D12_DEPTH_STENCIL_DESC2 depth_stencil_state;
    D3D12_INPUT_LAYOUT_DESC input_layout;
    D3D12_INDEX_BUFFER_STRIP_CUT_VALUE strip_cut_value;
    D3D12_PRIMITIVE_TOPOLOGY_TYPE primitive_topology_type;
    D3D12_RT_FORMAT_ARRAY rtv_formats;
    DXGI_FORMAT dsv_format;
    DXGI_SAMPLE_DESC sample_desc;
    D3D12_VIEW_INSTANCING_DESC view_instancing_desc;
    UINT node_mask;
    struct d3d12_cached_pipeline_state cached_pso;
    D3D12_PIPELINE_STATE_FLAGS flags;
};

HRESULT vkd3d_pipeline_state_desc_from_d3d12_graphics_desc(struct d3d12_pipeline_state_desc *desc,
        const D3D12_GRAPHICS_PIPELINE_STATE_DESC *d3d12_desc);
HRESULT vkd3d_pipeline_state_desc_from_d3d12_compute_desc(struct d3d12_pipeline_state_desc *desc,
        const D3D12_COMPUTE_PIPELINE_STATE_DESC *d3d12_desc);
HRESULT vkd3d_pipeline_state_desc_from_d3d12_stream_desc(struct d3d12_pipeline_state_desc *desc,
        const D3D12_PIPELINE_STATE_STREAM_DESC *d3d12_desc, VkPipelineBindPoint *vk_bind_point);

static inline bool vk_primitive_topology_supports_restart(VkPrimitiveTopology topology)
{
    switch (topology)
    {
        case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP:
        case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP_WITH_ADJACENCY:
        case VK_PRIMITIVE_TOPOLOGY_LINE_STRIP:
        case VK_PRIMITIVE_TOPOLOGY_LINE_STRIP_WITH_ADJACENCY:
            return true;

        default:
            return false;
    }
}

struct vkd3d_pipeline_key
{
    D3D12_PRIMITIVE_TOPOLOGY topology;
    VkFormat dsv_format;
    VkSampleCountFlagBits rasterization_samples;

    bool dynamic_topology;
};

bool d3d12_pipeline_state_has_replaced_shaders(struct d3d12_pipeline_state *state);
HRESULT d3d12_pipeline_state_create(struct d3d12_device *device, VkPipelineBindPoint bind_point,
        const struct d3d12_pipeline_state_desc *desc, struct d3d12_pipeline_state **state);
VkPipeline d3d12_pipeline_state_get_or_create_pipeline(struct d3d12_pipeline_state *state,
        const struct vkd3d_dynamic_state *dyn_state, const struct vkd3d_format *dsv_format,
        uint32_t *dynamic_state_flags);
VkPipeline d3d12_pipeline_state_get_pipeline(struct d3d12_pipeline_state *state,
        const struct vkd3d_dynamic_state *dyn_state, const struct vkd3d_format *dsv_format,
        uint32_t *dynamic_state_flags);
VkPipeline d3d12_pipeline_state_create_pipeline_variant(struct d3d12_pipeline_state *state,
        const struct vkd3d_pipeline_key *key, const struct vkd3d_format *dsv_format,
        VkPipelineCache vk_cache, VkGraphicsPipelineLibraryFlagsEXT library_flags, uint32_t *dynamic_state_flags);

static inline struct d3d12_pipeline_state *impl_from_ID3D12PipelineState(ID3D12PipelineState *iface)
{
    extern CONST_VTBL struct ID3D12PipelineStateVtbl d3d12_pipeline_state_vtbl;
    if (!iface)
        return NULL;
    assert(iface->lpVtbl == &d3d12_pipeline_state_vtbl);
    return CONTAINING_RECORD(iface, struct d3d12_pipeline_state, ID3D12PipelineState_iface);
}

/* ID3D12PipelineLibrary */
typedef ID3D12PipelineLibrary1 d3d12_pipeline_library_iface;

struct vkd3d_pipeline_library_disk_cache_item
{
    struct d3d12_pipeline_state *state;
};

struct vkd3d_pipeline_library_disk_cache
{
    /* This memory is generally mapped with MapViewOfFile() or mmap(),
     * and must remain mapped for the duration of the library. */
    struct vkd3d_memory_mapped_file mapped_file;
    struct d3d12_pipeline_library *library;

    pthread_t thread;
    condvar_reltime_t cond;
    pthread_mutex_t lock;
    bool thread_active;

    struct vkd3d_pipeline_library_disk_cache_item *items;
    size_t items_count;
    size_t items_size;

    char read_path[VKD3D_PATH_MAX];
    char write_path[VKD3D_PATH_MAX];

    /* The stream archive is designed to be safe against concurrent readers and writers, ala Fossilize.
     * There is a read-only portion, and a write-only portion which can be merged back to the read-only archive
     * on demand. */
    FILE *stream_archive_write_file;
    bool stream_archive_attempted_write;
};

struct d3d12_pipeline_library
{
    d3d12_pipeline_library_iface ID3D12PipelineLibrary_iface;
    LONG refcount;
    LONG internal_refcount;
    uint32_t flags;

    struct d3d12_device *device;

    rwlock_t mutex;
    /* driver_cache_map and spirv_cache_map can be touched in serialize_pipeline_state.
     * Use the internal mutex when touching the internal caches
     * so we don't need a big lock on the outside when serializing. */
    rwlock_t internal_hashmap_mutex;
    struct hash_map pso_map;
    struct hash_map driver_cache_map;
    struct hash_map spirv_cache_map;

    size_t total_name_table_size;
    size_t total_blob_size;

    /* Non-owned pointer. Calls back into the disk cache when blobs are added. */
    struct vkd3d_pipeline_library_disk_cache *disk_cache_listener;
    /* Useful if parsing a huge archive in the disk thread from a cold cache.
     * If we want to tear down device immediately after device creation (not too uncommon),
     * we can end up blocking for a long time. */
    uint32_t stream_archive_cancellation_point;

    struct vkd3d_private_store private_store;
    struct d3d_destruction_notifier destruction_notifier;

    const void *input_blob;
    size_t input_blob_length;
};

enum vkd3d_pipeline_library_flags
{
    VKD3D_PIPELINE_LIBRARY_FLAG_SAVE_FULL_SPIRV = 1 << 0,
    VKD3D_PIPELINE_LIBRARY_FLAG_SAVE_PSO_BLOB = 1 << 1,
    VKD3D_PIPELINE_LIBRARY_FLAG_INTERNAL_KEYS = 1 << 2,
    VKD3D_PIPELINE_LIBRARY_FLAG_USE_PIPELINE_CACHE_UUID = 1 << 3,
    VKD3D_PIPELINE_LIBRARY_FLAG_STREAM_ARCHIVE = 1 << 4,
    /* We expect to parse archive from thread, so consider thread safety and cancellation points. */
    VKD3D_PIPELINE_LIBRARY_FLAG_STREAM_ARCHIVE_PARSE_ASYNC = 1 << 5,
    VKD3D_PIPELINE_LIBRARY_FLAG_SHADER_IDENTIFIER = 1 << 6,
};

HRESULT d3d12_pipeline_library_create(struct d3d12_device *device, const void *blob,
        size_t blob_length, uint32_t flags, /* vkd3d_pipeline_library_flags */
        struct d3d12_pipeline_library **pipeline_library);

VkResult vkd3d_create_pipeline_cache(struct d3d12_device *device,
        size_t size, const void *data, VkPipelineCache *cache);
HRESULT vkd3d_create_pipeline_cache_from_d3d12_desc(struct d3d12_device *device,
        const struct d3d12_cached_pipeline_state *state, VkPipelineCache *cache);
HRESULT vkd3d_get_cached_spirv_code_from_d3d12_desc(
        const struct d3d12_cached_pipeline_state *state,
        VkShaderStageFlagBits stage,
        struct vkd3d_shader_code *spirv_code,
        VkPipelineShaderStageModuleIdentifierCreateInfoEXT *identifier);
VkResult vkd3d_serialize_pipeline_state(struct d3d12_pipeline_library *pipeline_library,
        const struct d3d12_pipeline_state *state, size_t *size, void *data);
HRESULT d3d12_cached_pipeline_state_validate(struct d3d12_device *device,
        const struct d3d12_cached_pipeline_state *state,
        const struct vkd3d_pipeline_cache_compatibility *compat);
bool d3d12_cached_pipeline_state_is_dummy(const struct d3d12_cached_pipeline_state *state);
void vkd3d_pipeline_cache_compat_from_state_desc(struct vkd3d_pipeline_cache_compatibility *compat,
        const struct d3d12_pipeline_state_desc *desc);
uint64_t vkd3d_pipeline_cache_compatibility_condense(const struct vkd3d_pipeline_cache_compatibility *compat);

ULONG d3d12_pipeline_library_inc_public_ref(struct d3d12_pipeline_library *state);
ULONG d3d12_pipeline_library_dec_public_ref(struct d3d12_pipeline_library *state);
void d3d12_pipeline_library_inc_ref(struct d3d12_pipeline_library *state);
void d3d12_pipeline_library_dec_ref(struct d3d12_pipeline_library *state);

/* For internal on-disk pipeline cache fallback. The key to Load/StorePipeline is implied by the PSO cache compatibility. */
HRESULT vkd3d_pipeline_library_store_pipeline_to_disk_cache(struct vkd3d_pipeline_library_disk_cache *pipeline_library,
        struct d3d12_pipeline_state *state);
HRESULT vkd3d_pipeline_library_find_cached_blob_from_disk_cache(struct vkd3d_pipeline_library_disk_cache *pipeline_library,
        const struct vkd3d_pipeline_cache_compatibility *compat,
        struct d3d12_cached_pipeline_state *cached_state);
void vkd3d_pipeline_library_disk_cache_notify_blob_insert(struct vkd3d_pipeline_library_disk_cache *disk_cache,
        uint64_t hash, uint32_t type /* vkd3d_serialized_pipeline_stream_entry_type */,
        const void *data, size_t size);

/* Called on device init. */
HRESULT vkd3d_pipeline_library_init_disk_cache(struct vkd3d_pipeline_library_disk_cache *cache,
        struct d3d12_device *device);
/* Called on device destroy. */
void vkd3d_pipeline_library_flush_disk_cache(struct vkd3d_pipeline_library_disk_cache *cache);

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

#define VKD3D_SCRATCH_BUFFER_SIZE_DEFAULT (1ull << 20)
#define VKD3D_SCRATCH_BUFFER_SIZE_DGCC_PREPROCESS_NV (32ull << 20)
#define VKD3D_SCRATCH_BUFFER_COUNT_DEFAULT (32u)
#define VKD3D_SCRATCH_BUFFER_COUNT_INDIRECT_PREPROCESS (128u)
#define VKD3D_MAX_SCRATCH_BUFFER_COUNT (128u)

struct vkd3d_scratch_buffer
{
    struct vkd3d_memory_allocation allocation;
    VkDeviceSize offset;
};

#define VKD3D_QUERY_TYPE_INDEX_OCCLUSION (0u)
#define VKD3D_QUERY_TYPE_INDEX_PIPELINE_STATISTICS (1u)
#define VKD3D_QUERY_TYPE_INDEX_TRANSFORM_FEEDBACK (2u)
#define VKD3D_QUERY_TYPE_INDEX_RT_COMPACTED_SIZE (3u)
#define VKD3D_QUERY_TYPE_INDEX_RT_SERIALIZE_SIZE (4u)
#define VKD3D_QUERY_TYPE_INDEX_RT_CURRENT_SIZE (5u)
#define VKD3D_QUERY_TYPE_INDEX_RT_SERIALIZE_SIZE_BOTTOM_LEVEL_POINTERS (6u)
#define VKD3D_VIRTUAL_QUERY_TYPE_COUNT (7u)
#define VKD3D_VIRTUAL_QUERY_POOL_COUNT (128u)

struct vkd3d_query_pool
{
    VkQueryPool vk_query_pool;
    uint32_t type_index;
    uint32_t query_count;
    uint32_t next_index;
};

struct d3d12_command_allocator_scratch_pool
{
    struct vkd3d_scratch_buffer *scratch_buffers;
    size_t scratch_buffers_size;
    size_t scratch_buffer_count;
};

enum vkd3d_scratch_pool_kind
{
    VKD3D_SCRATCH_POOL_KIND_DEVICE_STORAGE = 0,
    VKD3D_SCRATCH_POOL_KIND_INDIRECT_PREPROCESS,
    VKD3D_SCRATCH_POOL_KIND_UNIFORM_UPLOAD,
    VKD3D_SCRATCH_POOL_KIND_COUNT
};

/* ID3D12CommandAllocator */
struct d3d12_command_allocator
{
    ID3D12CommandAllocator ID3D12CommandAllocator_iface;
    LONG refcount;
    LONG internal_refcount;

    D3D12_COMMAND_LIST_TYPE type;
    VkQueueFlags vk_queue_flags;
    uint32_t vk_family_index;

    VkCommandPool vk_command_pool;

    struct vkd3d_view **views;
    size_t views_size;
    size_t view_count;

    VkBufferView *buffer_views;
    size_t buffer_views_size;
    size_t buffer_view_count;

    struct d3d12_pipeline_state **pipelines;
    size_t pipelines_size;
    size_t pipelines_count;

    VkCommandBuffer *command_buffers;
    size_t command_buffers_size;
    size_t command_buffer_count;

    struct d3d12_command_allocator_scratch_pool scratch_pools[VKD3D_SCRATCH_POOL_KIND_COUNT];

    struct vkd3d_query_pool *query_pools;
    size_t query_pools_size;
    size_t query_pool_count;

    struct vkd3d_query_pool active_query_pools[VKD3D_VIRTUAL_QUERY_TYPE_COUNT];

    struct d3d12_command_list *current_command_list;
    struct d3d12_device *device;

    struct vkd3d_private_store private_store;
    struct d3d_destruction_notifier destruction_notifier;

#ifdef VKD3D_ENABLE_BREADCRUMBS
    unsigned int *breadcrumb_context_indices;
    size_t breadcrumb_context_index_size;
    size_t breadcrumb_context_index_count;
#endif
};

HRESULT d3d12_command_allocator_create(struct d3d12_device *device,
        D3D12_COMMAND_LIST_TYPE type,
        uint32_t vk_family_index,
        struct d3d12_command_allocator **allocator);
bool d3d12_command_allocator_allocate_query_from_type_index(
        struct d3d12_command_allocator *allocator,
        uint32_t type_index, VkQueryPool *query_pool, uint32_t *query_index);

struct d3d12_command_list *d3d12_command_list_from_iface(ID3D12CommandList *iface);
void d3d12_command_list_decay_tracked_state(struct d3d12_command_list *list);

struct vkd3d_scratch_allocation
{
    VkBuffer buffer;
    VkDeviceSize offset;
    VkDeviceAddress va;
    void *host_ptr;
};

bool d3d12_command_allocator_allocate_scratch_memory(struct d3d12_command_allocator *allocator,
        enum vkd3d_scratch_pool_kind kind,
        VkDeviceSize size, VkDeviceSize alignment, uint32_t memory_types,
        struct vkd3d_scratch_allocation *allocation);

enum vkd3d_pipeline_dirty_flag
{
    VKD3D_PIPELINE_DIRTY_STATIC_SAMPLER_SET       = 0x00000001u,
    VKD3D_PIPELINE_DIRTY_DESCRIPTOR_TABLE_OFFSETS = 0x00000002u,
    VKD3D_PIPELINE_DIRTY_HOISTED_DESCRIPTORS      = 0x00000004u,
};

struct vkd3d_root_descriptor_info
{
    VkDescriptorType vk_descriptor_type;
    union vkd3d_descriptor_info info;
};

struct vkd3d_pipeline_bindings
{
    const struct d3d12_root_signature *root_signature;

    VkDescriptorSet static_sampler_set;
    uint32_t dirty_flags; /* vkd3d_pipeline_dirty_flags */

    uint32_t descriptor_tables[D3D12_MAX_ROOT_COST];
    uint64_t descriptor_heap_dirty_mask;

    /* Needed when VK_KHR_push_descriptor is not available. */
    struct vkd3d_root_descriptor_info root_descriptors[D3D12_MAX_ROOT_COST];
    uint64_t root_descriptor_dirty_mask;
    uint64_t root_descriptor_active_mask;

    uint32_t root_constants[D3D12_MAX_ROOT_COST];
    uint64_t root_constant_dirty_mask;
};

struct vkd3d_dynamic_state
{
    uint32_t active_flags; /* vkd3d_dynamic_state_flags */
    uint32_t dirty_flags; /* vkd3d_dynamic_state_flags */
    uint32_t dirty_vbos;

    uint32_t viewport_count;
    VkViewport viewports[D3D12_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
    VkRect2D scissors[D3D12_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
    VkSampleCountFlagBits rasterization_samples;

    float blend_constants[4];

    struct
    {
        uint8_t reference;
        uint8_t write_mask;
    } stencil_front, stencil_back;

    uint32_t dsv_plane_write_enable;

    struct
    {
        float constant_factor;
        float clamp;
        float slope_factor;
    } depth_bias;

    D3D12_INDEX_BUFFER_STRIP_CUT_VALUE index_buffer_strip_cut_value;

    float min_depth_bounds;
    float max_depth_bounds;

    VkBuffer vertex_buffers[D3D12_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT];
    VkDeviceSize vertex_offsets[D3D12_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT];
    VkDeviceSize vertex_sizes[D3D12_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT];
    VkDeviceSize vertex_strides[D3D12_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT];

    D3D12_PRIMITIVE_TOPOLOGY primitive_topology;
    VkPrimitiveTopology vk_primitive_topology;

    struct
    {
        VkExtent2D fragment_size;
        VkFragmentShadingRateCombinerOpKHR combiner_ops[D3D12_RS_SET_SHADING_RATE_COMBINER_COUNT];
    } fragment_shading_rate;

    uint32_t pipeline_stack_size;
};

/* ID3D12CommandList */
typedef ID3D12GraphicsCommandList10 d3d12_command_list_iface;

enum vkd3d_initial_transition_type
{
    VKD3D_INITIAL_TRANSITION_TYPE_RESOURCE,
    VKD3D_INITIAL_TRANSITION_TYPE_QUERY_HEAP,
};

struct vkd3d_initial_transition
{
    enum vkd3d_initial_transition_type type;
    union
    {
        struct
        {
            struct d3d12_resource *resource;
            bool perform_initial_transition;
        } resource;
        struct d3d12_query_heap *query_heap;
    };
};

bool vk_image_memory_barrier_for_initial_transition(const struct d3d12_resource *resource,
        VkImageMemoryBarrier2 *barrier);

enum vkd3d_active_query_state
{
    VKD3D_ACTIVE_QUERY_RESET,
    VKD3D_ACTIVE_QUERY_BEGUN,
    VKD3D_ACTIVE_QUERY_ENDED,
};

struct vkd3d_active_query
{
    struct d3d12_query_heap *heap;
    uint32_t index;
    D3D12_QUERY_TYPE type;
    VkQueryPool vk_pool;
    uint32_t vk_index;
    enum vkd3d_active_query_state state;
    uint32_t resolve_index;
};

enum vkd3d_query_range_flag
{
    VKD3D_QUERY_RANGE_RESET = 0x1,
};

struct vkd3d_query_range
{
    VkQueryPool vk_pool;
    uint32_t index;
    uint32_t count;
    uint32_t flags;
};

enum vkd3d_rendering_flags
{
    VKD3D_RENDERING_ACTIVE    = (1u << 0),
    VKD3D_RENDERING_SUSPENDED = (1u << 1),
    VKD3D_RENDERING_CURRENT   = (1u << 2),
};

struct vkd3d_rendering_info
{
    VkRenderingInfo info;
    VkRenderingAttachmentInfo rtv[D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT];
    VkRenderingAttachmentInfo dsv;
    VkRenderingFragmentShadingRateAttachmentInfoKHR vrs;
    uint32_t state_flags;
    uint32_t rtv_mask;
};

/* ID3D12CommandListExt */
typedef ID3D12GraphicsCommandListExt1 d3d12_command_list_vkd3d_ext_iface;

struct d3d12_rt_state_object;

struct d3d12_resource_tracking
{
    const struct d3d12_resource *resource;
    uint32_t plane_optimal_mask;
};

struct vkd3d_subresource_tracking
{
    struct d3d12_resource *resource;
    VkImageSubresourceLayers subresource;
};

#define VKD3D_BUFFER_COPY_TRACKING_BUFFER_COUNT 4
struct d3d12_buffer_copy_tracked_buffer
{
    /* Need to track on VkBuffer level to handle aliasing. For ID3D12Heap, all resources share one VkBuffer. */
    VkBuffer vk_buffer;
    VkDeviceSize hazard_begin;
    VkDeviceSize hazard_end;
};

enum vkd3d_batch_type
{
    VKD3D_BATCH_TYPE_NONE,
    VKD3D_BATCH_TYPE_COPY_BUFFER_TO_IMAGE,
    VKD3D_BATCH_TYPE_COPY_IMAGE_TO_BUFFER,
    VKD3D_BATCH_TYPE_COPY_IMAGE,
};

struct vkd3d_image_copy_info
{
    D3D12_TEXTURE_COPY_LOCATION src, dst;
    const struct vkd3d_format *src_format, *dst_format;
    enum vkd3d_batch_type batch_type;
    union
    {
        VkBufferImageCopy2 buffer_image;
        VkImageCopy2 image;
    } copy;
    /* TODO: split d3d12_command_list_copy_image too, so this can be a local variable of before_copy_texture_region. */
    bool writes_full_subresource;
    bool writes_full_resource;
    bool overlapping_subresource;
    VkImageLayout src_layout;
    VkImageLayout dst_layout;
};

struct vkd3d_query_resolve_entry
{
    D3D12_QUERY_TYPE query_type;
    struct d3d12_query_heap *query_heap;
    uint32_t query_index;
    uint32_t query_count;
    struct d3d12_resource *dst_buffer;
    VkDeviceSize dst_offset;
};

#define VKD3D_QUERY_LOOKUP_GRANULARITY_BITS (6u)
#define VKD3D_QUERY_LOOKUP_GRANULARITY (1u << VKD3D_QUERY_LOOKUP_GRANULARITY_BITS)
#define VKD3D_QUERY_LOOKUP_INDEX_MASK (VKD3D_QUERY_LOOKUP_GRANULARITY - 1u)

struct vkd3d_query_lookup_key
{
    struct d3d12_query_heap *query_heap;
    uint32_t bucket;
};

struct vkd3d_query_lookup_entry
{
    struct hash_map_entry hash_entry;
    struct vkd3d_query_lookup_key key;
    uint64_t query_mask;
};

/* This is arbitrary, but tuned towards real-world use cases.
 * One such use case is copying mipmapped cubemaps.
 * 6 faces * up to 5 LODs for environment probes eats the budget.
 * Observed in at least one UE5 title, so probably worth tuning for that. */
#define VKD3D_COPY_TEXTURE_REGION_MAX_BATCH_SIZE 32

struct d3d12_transfer_batch_state
{
    enum vkd3d_batch_type batch_type;
    struct vkd3d_image_copy_info batch[VKD3D_COPY_TEXTURE_REGION_MAX_BATCH_SIZE];
    size_t batch_len;
};

#define VKD3D_MAX_WBI_BATCH_SIZE 128

struct d3d12_wbi_batch_state
{
    VkBuffer buffers[VKD3D_MAX_WBI_BATCH_SIZE];
    VkDeviceSize offsets[VKD3D_MAX_WBI_BATCH_SIZE];
    VkPipelineStageFlags stages[VKD3D_MAX_WBI_BATCH_SIZE];
    uint32_t values[VKD3D_MAX_WBI_BATCH_SIZE];
    size_t batch_len;
};

struct d3d12_rtas_batch_state
{
    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE build_type;

    VkAccelerationStructureBuildGeometryInfoKHR *build_infos;
    size_t build_info_count;
    size_t build_info_size;

    VkAccelerationStructureGeometryKHR *geometry_infos;
    size_t geometry_info_count;
    size_t geometry_info_size;

    VkAccelerationStructureBuildRangeInfoKHR *range_infos;
    size_t range_info_size;

    const VkAccelerationStructureBuildRangeInfoKHR **range_ptrs;
    size_t range_ptr_size;
};

union vkd3d_descriptor_heap_state
{
    struct
    {
        VkDeviceAddress heap_va_resource;
        VkDeviceAddress heap_va_sampler;
        VkBuffer vk_buffer_resource;
        bool heap_dirty;

        VkDeviceSize vk_offsets[VKD3D_MAX_BINDLESS_DESCRIPTOR_SETS];
    } buffers;

    struct
    {
        VkDescriptorSet vk_sets[VKD3D_MAX_BINDLESS_DESCRIPTOR_SETS];
    } sets;
};

struct d3d12_rtv_resolve
{
    struct d3d12_resource *src_resource;
    struct d3d12_resource *dst_resource;
    uint32_t region_index;
    uint32_t region_count;
    DXGI_FORMAT format;
    D3D12_RESOLVE_MODE mode;
};

struct d3d12_command_list_iteration_indirect_meta
{
    bool need_compute_to_indirect_barrier;
    bool need_compute_to_cbv_barrier;
    bool need_preprocess_barrier;
};

struct d3d12_command_list_iteration
{
    VkCommandBuffer vk_command_buffer;
    VkCommandBuffer vk_init_commands;
    uint32_t estimated_cost;
    struct d3d12_command_list_iteration_indirect_meta indirect_meta;
};

#define VKD3D_MAX_COMMAND_LIST_SEQUENCES 2

#define VKD3D_COMMAND_COST_LOW              (1u)
#define VKD3D_COMMAND_COST_HIGH             (16u)

#define VKD3D_COMMAND_COST_MERGE_THRESHOLD  (VKD3D_COMMAND_COST_HIGH)

struct d3d12_command_list_sequence
{
    /* A command list can be split into multiple sequences of
     * init -> command -> init -> command.
     * This facilitates batching.
     * Command stream can be split on e.g. INDIRECT_ARGUMENT resource state.
     * This allows us to hoist predication CS streams nicely even in cases
     * where INDIRECT_ARGUMENT barriers appear in the stream. */
    struct d3d12_command_list_iteration iterations[VKD3D_MAX_COMMAND_LIST_SEQUENCES];
    unsigned int iteration_count;
    unsigned int active_non_inline_running_queries;
    bool uses_dgc_compute_in_async_compute;
    bool clear_uav_pending;

    /* Number of draws, dispatches, copies etc. Used to fuse barrier-only
     * command buffers for staggered submissions. */
    uint32_t estimated_cost;

    /* Emit normal commands here. */
    VkCommandBuffer vk_command_buffer;
    /* For various commands which should be thrown to the start of ID3D12CommandList. */
    VkCommandBuffer vk_init_commands;
    /* For any command which is sensitive to INDIRECT_ARGUMENT barriers.
     * If equal to vk_command_buffer, it means it is not possible to split command buffers, and
     * we must use vk_command_buffer with appropriate barriers. */
    VkCommandBuffer vk_init_commands_post_indirect_barrier;

    struct d3d12_command_list_iteration_indirect_meta *indirect_meta;
};

struct d3d12_command_list
{
    d3d12_command_list_iface ID3D12GraphicsCommandList_iface;
    d3d12_command_list_vkd3d_ext_iface ID3D12GraphicsCommandListExt_iface;
    LONG refcount;

    D3D12_COMMAND_LIST_TYPE type;
    VkQueueFlags vk_queue_flags;

    bool is_recording;
    bool is_valid;
    bool debug_capture;
    bool has_replaced_shaders;

    struct
    {
        VkBuffer buffer;
        VkDeviceSize offset;
        VkDeviceSize size;
        DXGI_FORMAT dxgi_format;
        VkIndexType vk_type;
        bool is_dirty;
    } index_buffer;

    struct d3d12_command_list_sequence cmd;

    bool is_inside_render_pass;
    D3D12_RENDER_PASS_FLAGS render_pass_flags;
    struct d3d12_rtv_desc rtvs[D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT];
    struct d3d12_rtv_desc dsv;

    struct d3d12_rtv_resolve *rtv_resolves;
    size_t rtv_resolve_size;
    size_t rtv_resolve_count;

    VkImageResolve2 *rtv_resolve_regions;
    size_t rtv_resolve_region_size;
    size_t rtv_resolve_region_count;

    uint32_t dsv_plane_optimal_mask;
    VkImageLayout dsv_layout;
    unsigned int fb_width;
    unsigned int fb_height;
    unsigned int fb_layer_count;

    unsigned int xfb_buffer_count;

    struct
    {
        VkDeviceAddress va;
        VkBuffer vk_buffer;
        VkDeviceSize vk_buffer_offset;
        bool enabled_on_command_buffer;
        bool fallback_enabled;
    } predication;

    /* This is VK_NULL_HANDLE when we are no longer sure which pipeline to bind,
     * if this is NULL, we might need to lookup a pipeline key in order to bind the correct pipeline. */
    VkPipeline current_pipeline;

    /* This is the actual pipeline which is bound to the pipeline. This lets us elide
     * possible calls to vkCmdBindPipeline and avoids invalidating dynamic state. */
    VkPipeline command_buffer_pipeline;

    struct vkd3d_rendering_info rendering_info;
    struct vkd3d_dynamic_state dynamic_state;
    struct vkd3d_pipeline_bindings graphics_bindings;
    struct vkd3d_pipeline_bindings compute_bindings;
    enum vkd3d_pipeline_type active_pipeline_type;

    union vkd3d_descriptor_heap_state descriptor_heap;

    struct d3d12_pipeline_state *state;
    struct d3d12_rt_state_object *rt_state;
    const struct d3d12_rt_state_object_variant *rt_state_variant;
    uint32_t current_compute_meta_flags;

    struct d3d12_command_allocator *allocator;
    struct d3d12_command_allocator *submit_allocator;
    struct d3d12_device *device;

    VkBuffer so_buffers[D3D12_SO_BUFFER_SLOT_COUNT];
    VkDeviceSize so_buffer_offsets[D3D12_SO_BUFFER_SLOT_COUNT];
    VkDeviceSize so_buffer_sizes[D3D12_SO_BUFFER_SLOT_COUNT];
    VkBuffer so_counter_buffers[D3D12_SO_BUFFER_SLOT_COUNT];
    VkDeviceSize so_counter_buffer_offsets[D3D12_SO_BUFFER_SLOT_COUNT];

    struct vkd3d_initial_transition *init_transitions;
    size_t init_transitions_size;
    size_t init_transitions_count;

    struct vkd3d_query_range *query_ranges;
    size_t query_ranges_size;
    size_t query_ranges_count;

    struct vkd3d_active_query *active_queries;
    size_t active_queries_size;
    size_t active_queries_count;

    struct vkd3d_active_query *pending_queries;
    size_t pending_queries_size;
    size_t pending_queries_count;

    const struct vkd3d_descriptor_metadata_view *cbv_srv_uav_descriptors_view;

    struct d3d12_resource *vrs_image;

    struct d3d12_resource_tracking *dsv_resource_tracking;
    size_t dsv_resource_tracking_count;
    size_t dsv_resource_tracking_size;

    struct vkd3d_subresource_tracking *subresource_tracking;
    size_t subresource_tracking_count;
    size_t subresource_tracking_size;

    struct vkd3d_query_resolve_entry *query_resolves;
    size_t query_resolve_count;
    size_t query_resolve_size;

    struct hash_map query_resolve_lut;

    struct d3d12_buffer_copy_tracked_buffer tracked_copy_buffers[VKD3D_BUFFER_COPY_TRACKING_BUFFER_COUNT];
    unsigned int tracked_copy_buffer_count;

    struct d3d12_transfer_batch_state transfer_batch;
    struct d3d12_wbi_batch_state wbi_batch;
    struct d3d12_rtas_batch_state rtas_batch;
    struct vkd3d_queue_timeline_trace_cookie timeline_cookie;

    struct vkd3d_private_store private_store;
    struct d3d_destruction_notifier destruction_notifier;

#ifdef VKD3D_ENABLE_BREADCRUMBS
    unsigned int breadcrumb_context_index;
#endif
};

HRESULT d3d12_command_list_create(struct d3d12_device *device,
        UINT node_mask, D3D12_COMMAND_LIST_TYPE type, struct d3d12_command_list **list);
bool d3d12_command_list_reset_query(struct d3d12_command_list *list,
        VkQueryPool vk_pool, uint32_t index);
void d3d12_command_list_end_current_render_pass(struct d3d12_command_list *list, bool suspend);
void d3d12_command_list_invalidate_all_state(struct d3d12_command_list *list);

void d3d12_command_list_debug_mark_label(struct d3d12_command_list *list, const char *tag,
        float r, float g, float b, float a);
void d3d12_command_list_debug_mark_begin_region(
        struct d3d12_command_list *list, const char *tag);
void d3d12_command_list_debug_mark_end_region(struct d3d12_command_list *list);

void d3d12_command_list_invalidate_current_pipeline(struct d3d12_command_list *list, bool meta_shader);
void d3d12_command_list_invalidate_root_parameters(struct d3d12_command_list *list,
        struct vkd3d_pipeline_bindings *bindings, bool invalidate_descriptor_heaps,
        struct vkd3d_pipeline_bindings *sibling_push_domain);
void d3d12_command_list_update_descriptor_buffers(struct d3d12_command_list *list);

union vkd3d_root_parameter_data
{
    uint32_t root_constants[D3D12_MAX_ROOT_COST];
    VkDeviceAddress root_descriptor_vas[D3D12_MAX_ROOT_COST / 2];
};

void d3d12_command_list_fetch_root_parameter_data(struct d3d12_command_list *list,
        struct vkd3d_pipeline_bindings *bindings, union vkd3d_root_parameter_data *dst_data);

static inline struct vkd3d_pipeline_bindings *d3d12_command_list_get_bindings(
        struct d3d12_command_list *list, enum vkd3d_pipeline_type pipeline_type)
{
    switch (pipeline_type)
    {
        case VKD3D_PIPELINE_TYPE_NONE:
            break;

        case VKD3D_PIPELINE_TYPE_GRAPHICS:
        case VKD3D_PIPELINE_TYPE_MESH_GRAPHICS:
            return &list->graphics_bindings;

        case VKD3D_PIPELINE_TYPE_COMPUTE:
        case VKD3D_PIPELINE_TYPE_RAY_TRACING:
            return &list->compute_bindings;
    }

    return NULL;
}

#define VKD3D_BUNDLE_CHUNK_SIZE (256 << 10)
#define VKD3D_BUNDLE_COMMAND_ALIGNMENT (sizeof(UINT64))

struct d3d12_bundle_allocator
{
    ID3D12CommandAllocator ID3D12CommandAllocator_iface;
    LONG refcount;

    void **chunks;
    size_t chunks_size;
    size_t chunks_count;
    size_t chunk_offset;

    struct d3d12_bundle *current_bundle;
    struct d3d12_device *device;

    struct vkd3d_private_store private_store;
    struct d3d_destruction_notifier destruction_notifier;
};

HRESULT d3d12_bundle_allocator_create(struct d3d12_device *device,
        struct d3d12_bundle_allocator **allocator);

typedef void (*pfn_d3d12_bundle_command)(d3d12_command_list_iface *command_list, const void *args);

struct d3d12_bundle_command
{
    pfn_d3d12_bundle_command proc;
    struct d3d12_bundle_command *next;
};

struct d3d12_bundle
{
    d3d12_command_list_iface ID3D12GraphicsCommandList_iface;
    LONG refcount;

    bool is_recording;

    struct d3d12_device *device;
    struct d3d12_bundle_allocator *allocator;
    struct d3d12_bundle_command *head;
    struct d3d12_bundle_command *tail;

    struct vkd3d_private_store private_store;
    struct d3d_destruction_notifier destruction_notifier;
};

HRESULT d3d12_bundle_create(struct d3d12_device *device,
        UINT node_mask, D3D12_COMMAND_LIST_TYPE type, struct d3d12_bundle **bundle);
void d3d12_bundle_execute(struct d3d12_bundle *bundle, d3d12_command_list_iface *list);
struct d3d12_bundle *d3d12_bundle_from_iface(ID3D12GraphicsCommandList *iface);

#define VKD3D_QUEUE_INACTIVE_THRESHOLD_NS (1000000000ull) /* 1s */

struct vkd3d_queue
{
    /* Access to VkQueue must be externally synchronized. */
    pthread_mutex_t mutex;

    /* If not NULL, lock a shared mutex as well. */
    pthread_mutex_t *global_mutex;

    VkQueue vk_queue;

    VkCommandPool barrier_pool;
    VkCommandBuffer barrier_command_buffer;
    VkSemaphore submission_timeline;
    uint64_t submission_timeline_count;

    uint32_t vk_family_index;
    uint32_t vk_queue_index;
    VkQueueFlags vk_queue_flags;
    uint32_t timestamp_bits;
    uint32_t virtual_queue_count;

    struct d3d12_command_queue **command_queues;
    size_t command_queue_size;
    size_t command_queue_count;

    VkSemaphoreSubmitInfo *wait_semaphores;
    size_t wait_semaphores_size;
    uint32_t wait_count;
};

VkQueue vkd3d_queue_acquire(struct vkd3d_queue *queue);
HRESULT vkd3d_queue_create(struct d3d12_device *device, uint32_t family_index, uint32_t queue_index,
        const VkQueueFamilyProperties *properties, struct vkd3d_queue **queue);
void vkd3d_set_queue_out_of_band(struct d3d12_device *device, struct vkd3d_queue *queue, VkOutOfBandQueueTypeNV type);
void vkd3d_queue_drain(struct vkd3d_queue *queue, struct d3d12_device *device);
void vkd3d_queue_destroy(struct vkd3d_queue *queue, struct d3d12_device *device);
void vkd3d_queue_release(struct vkd3d_queue *queue);
void vkd3d_queue_add_wait(struct vkd3d_queue *queue, VkSemaphore semaphore, uint64_t value);

enum vkd3d_submission_type
{
    VKD3D_SUBMISSION_WAIT,
    VKD3D_SUBMISSION_SIGNAL,
    VKD3D_SUBMISSION_EXECUTE,
    VKD3D_SUBMISSION_BIND_SPARSE,
    VKD3D_SUBMISSION_STOP,
    VKD3D_SUBMISSION_CALLBACK,
    VKD3D_SUBMISSION_DRAIN
};

enum vkd3d_sparse_memory_bind_mode
{
    VKD3D_SPARSE_MEMORY_BIND_MODE_UPDATE,
    VKD3D_SPARSE_MEMORY_BIND_MODE_COPY,
};

struct vkd3d_sparse_memory_bind
{
    uint32_t dst_tile;
    uint32_t src_tile;
    VkDeviceMemory vk_memory;
    VkDeviceSize vk_offset;
};

struct vkd3d_sparse_memory_bind_range
{
    uint32_t tile_index;
    uint32_t tile_count;
    VkDeviceMemory vk_memory;
    VkDeviceSize vk_offset;
};

struct d3d12_command_queue_submission_wait
{
    d3d12_fence_iface *fence;
    UINT64 value;
};

struct d3d12_command_queue_submission_signal
{
    d3d12_fence_iface *fence;
    UINT64 value;
};

struct d3d12_command_queue_submission_execute
{
    VkCommandBufferSubmitInfo *cmd;
    uint32_t *cmd_cost;
    struct d3d12_command_allocator **command_allocators;
    UINT cmd_count;
    UINT num_command_allocators;
    uint64_t low_latency_frame_id;

    struct vkd3d_initial_transition *transitions;
    size_t transition_count;

#ifdef VKD3D_ENABLE_BREADCRUMBS
    /* Replays commands in submission order for heavy debug. */
    unsigned int *breadcrumb_indices;
    size_t breadcrumb_indices_count;
#endif

    struct vkd3d_queue_timeline_trace_cookie timeline_cookie;

    bool debug_capture;
    bool split_submission;
};

struct d3d12_command_queue_submission_bind_sparse
{
    enum vkd3d_sparse_memory_bind_mode mode;
    uint32_t bind_count;
    struct vkd3d_sparse_memory_bind *bind_infos;
    struct d3d12_resource *dst_resource;
    struct d3d12_resource *src_resource;
};

struct d3d12_command_queue_submission_callback
{
    void (*callback)(void *);
    void *userdata;
};

struct d3d12_command_queue_submission
{
    enum vkd3d_submission_type type;
    union
    {
        struct d3d12_command_queue_submission_wait wait;
        struct d3d12_command_queue_submission_signal signal;
        struct d3d12_command_queue_submission_execute execute;
        struct d3d12_command_queue_submission_bind_sparse bind_sparse;
        struct d3d12_command_queue_submission_callback callback;
    };
};

struct vkd3d_timeline_semaphore
{
    VkSemaphore vk_semaphore;
    uint64_t last_signaled;
};

/* IDXGIVkSwapChainFactory */
struct dxgi_vk_swap_chain_factory
{
    IDXGIVkSwapChainFactory IDXGIVkSwapChainFactory_iface;
    struct d3d12_command_queue *queue;
};

struct dxgi_vk_swap_chain;

bool dxgi_vk_swap_chain_low_latency_enabled(struct dxgi_vk_swap_chain *chain);
void dxgi_vk_swap_chain_latency_sleep(struct dxgi_vk_swap_chain *chain);
void dxgi_vk_swap_chain_set_latency_sleep_mode(struct dxgi_vk_swap_chain *chain,
	bool low_latency_mode, bool low_latency_boost, uint32_t minimum_interval_us);
void dxgi_vk_swap_chain_set_latency_marker(struct dxgi_vk_swap_chain *chain,
	uint64_t frameID, VkLatencyMarkerNV marker);
void dxgi_vk_swap_chain_get_latency_info(struct dxgi_vk_swap_chain *chain,
	D3D12_LATENCY_RESULTS *latency_results);

ULONG dxgi_vk_swap_chain_incref(struct dxgi_vk_swap_chain *chain);
ULONG dxgi_vk_swap_chain_decref(struct dxgi_vk_swap_chain *chain);

HRESULT dxgi_vk_swap_chain_factory_init(struct d3d12_command_queue *queue, struct dxgi_vk_swap_chain_factory *chain);

enum vkd3d_wait_semaphore_flags
{
    VKD3D_WAIT_SEMAPHORES_EXTERNAL        = (1u << 0),
    VKD3D_WAIT_SEMAPHORES_SERIALIZING     = (1u << 1),
};

struct vkd3d_fence_virtual_wait
{
    struct d3d12_fence *fence;
    uint64_t virtual_value;
    VkSemaphore vk_semaphore;
    uint64_t vk_semaphore_value;
};

/* ID3D12CommandQueueExt */
typedef ID3D12CommandQueueExt d3d12_command_queue_vkd3d_ext_iface;

/* ID3D12CommandQueue */
typedef ID3D12CommandQueue d3d12_command_queue_iface;

struct d3d12_command_queue
{
    d3d12_command_queue_iface ID3D12CommandQueue_iface;
    d3d12_command_queue_vkd3d_ext_iface ID3D12CommandQueueExt_iface;

    LONG refcount;

    D3D12_COMMAND_QUEUE_DESC desc;

    struct vkd3d_queue *vkd3d_queue;

    struct d3d12_device *device;

    pthread_mutex_t queue_lock;
    pthread_cond_t queue_cond;
    pthread_t submission_thread;

    struct d3d12_command_queue_submission *submissions;
    size_t submissions_count;
    size_t submissions_size;
    uint64_t drain_count;
    uint64_t queue_drain_count;

    UINT64 last_submission_timeline_value;
    UINT64 last_submission_time_ns;
    bool stagger_submissions;

    struct vkd3d_fence_worker fence_worker;
    struct vkd3d_private_store private_store;
    struct d3d_destruction_notifier destruction_notifier;
    struct dxgi_vk_swap_chain_factory vk_swap_chain_factory;
    unsigned int submission_thread_tid;

    struct vkd3d_fence_virtual_wait *wait_fences;
    size_t wait_fences_size;
    size_t wait_fence_count;

    VkSemaphoreSubmitInfo *wait_semaphores;
    size_t wait_semaphores_size;
    size_t wait_semaphore_count;

    VkSemaphore serializing_semaphore;
    bool serializing_semaphore_signaled;

    struct
    {
        uint32_t buffer_binds_count;
        uint32_t image_binds_count;
        uint32_t image_opaque_binds_count;
        size_t buffer_binds_size;
        size_t image_binds_size;
        size_t image_opaque_binds_size;
        VkSparseBufferMemoryBindInfo *buffer_binds;
        VkSparseImageMemoryBindInfo *image_binds;
        VkSparseImageOpaqueMemoryBindInfo *image_opaque_binds;
        uint32_t total_tiles;

        struct
        {
            struct d3d12_resource *resource;
            uint32_t *tile_mask;
        } *tracked;
        size_t tracked_size;
        size_t tracked_count;
    } sparse;
};

HRESULT d3d12_command_queue_create(struct d3d12_device *device,
        const D3D12_COMMAND_QUEUE_DESC *desc, uint32_t vk_family_index, struct d3d12_command_queue **queue);
void d3d12_command_queue_submit_stop(struct d3d12_command_queue *queue);
void d3d12_command_queue_signal_inline(struct d3d12_command_queue *queue, d3d12_fence_iface *fence, uint64_t value);
void d3d12_command_queue_enqueue_callback(struct d3d12_command_queue *queue, void (*callback)(void *), void *userdata);

struct vkd3d_execute_indirect_info
{
    VkPipelineLayout vk_pipeline_layout;
    VkPipeline vk_pipeline;
};

enum vkd3d_patch_command_token
{
    VKD3D_PATCH_COMMAND_TOKEN_COPY_CONST_U32 = 0,
    VKD3D_PATCH_COMMAND_TOKEN_COPY_IBO_VA_LO = 1,
    VKD3D_PATCH_COMMAND_TOKEN_COPY_IBO_VA_HI = 2,
    VKD3D_PATCH_COMMAND_TOKEN_COPY_IBO_SIZE = 3,
    VKD3D_PATCH_COMMAND_TOKEN_COPY_INDEX_FORMAT = 4,
    VKD3D_PATCH_COMMAND_TOKEN_COPY_VBO_VA_LO = 5,
    VKD3D_PATCH_COMMAND_TOKEN_COPY_VBO_VA_HI = 6,
    VKD3D_PATCH_COMMAND_TOKEN_COPY_VBO_SIZE = 7,
    VKD3D_PATCH_COMMAND_TOKEN_COPY_VBO_STRIDE = 8,
    VKD3D_PATCH_COMMAND_TOKEN_COPY_ROOT_VA_LO = 9,
    VKD3D_PATCH_COMMAND_TOKEN_COPY_ROOT_VA_HI = 10,
    VKD3D_PATCH_COMMAND_TOKEN_COPY_VERTEX_COUNT = 11,
    VKD3D_PATCH_COMMAND_TOKEN_COPY_INDEX_COUNT = 12,
    VKD3D_PATCH_COMMAND_TOKEN_COPY_INSTANCE_COUNT = 13,
    VKD3D_PATCH_COMMAND_TOKEN_COPY_FIRST_INDEX = 14,
    VKD3D_PATCH_COMMAND_TOKEN_COPY_FIRST_VERTEX = 15,
    VKD3D_PATCH_COMMAND_TOKEN_COPY_FIRST_INSTANCE = 16,
    VKD3D_PATCH_COMMAND_TOKEN_COPY_VERTEX_OFFSET = 17,
    VKD3D_PATCH_COMMAND_TOKEN_COPY_MESH_TASKS_X = 18,
    VKD3D_PATCH_COMMAND_TOKEN_COPY_MESH_TASKS_Y = 19,
    VKD3D_PATCH_COMMAND_TOKEN_COPY_MESH_TASKS_Z = 20,
    VKD3D_PATCH_COMMAND_INT_MAX = 0x7fffffff
};

/* ID3D12CommandSignature */
struct d3d12_command_signature
{
    ID3D12CommandSignature ID3D12CommandSignature_iface;
    LONG refcount;

    D3D12_COMMAND_SIGNATURE_DESC desc;
    uint32_t argument_buffer_offset_for_command;

    /* Complex command signatures require some work to stamp out device generated commands. */
    union
    {
        struct
        {
            VkBuffer buffer;
            VkDeviceAddress buffer_va;
            struct vkd3d_device_memory_allocation memory;
            VkIndirectCommandsLayoutNV layout_implicit_nv;
            VkIndirectCommandsLayoutNV layout_preprocess_nv;
            VkIndirectCommandsLayoutEXT layout_implicit_ext;
            VkIndirectCommandsLayoutEXT layout_preprocess_ext;
            uint32_t stride;
            struct vkd3d_execute_indirect_info pipeline;
        } dgc;
        struct
        {
            int32_t source_offsets[D3D12_MAX_ROOT_COST];
            uint32_t dispatch_offset_words;
        } compute;
    } state_template;
    bool requires_state_template_dgc;
    bool requires_state_template;
    enum vkd3d_pipeline_type pipeline_type;

    struct d3d12_device *device;

    struct vkd3d_private_store private_store;
    struct d3d_destruction_notifier destruction_notifier;
};

HRESULT d3d12_command_signature_create(struct d3d12_device *device, struct d3d12_root_signature *root_signature,
        const D3D12_COMMAND_SIGNATURE_DESC *desc,
        struct d3d12_command_signature **signature);

static inline struct d3d12_command_signature *impl_from_ID3D12CommandSignature(ID3D12CommandSignature *iface)
{
    extern CONST_VTBL struct ID3D12CommandSignatureVtbl d3d12_command_signature_vtbl;
    if (!iface)
        return NULL;
    assert(iface->lpVtbl == &d3d12_command_signature_vtbl);
    return CONTAINING_RECORD(iface, struct d3d12_command_signature, ID3D12CommandSignature_iface);
}

/* Static samplers */
struct vkd3d_sampler_state
{
    pthread_mutex_t mutex;
    struct hash_map map;

    VkDescriptorPool *vk_descriptor_pools;
    size_t vk_descriptor_pools_size;
    size_t vk_descriptor_pool_count;
};

struct vkd3d_shader_debug_ring
{
    VkBuffer host_buffer;
    VkBuffer device_atomic_buffer;

    struct vkd3d_device_memory_allocation host_buffer_memory;
    struct vkd3d_device_memory_allocation device_atomic_buffer_memory;

    uint32_t *mapped_control_block;
    uint32_t *mapped_ring;
    VkDeviceAddress ring_device_address;
    VkDeviceAddress atomic_device_address;
    size_t ring_size;
    size_t control_block_size;

    pthread_t ring_thread;
    pthread_mutex_t ring_lock;
    pthread_cond_t ring_cond;
    bool device_lost;
    bool active;
};

HRESULT vkd3d_sampler_state_init(struct vkd3d_sampler_state *state,
        struct d3d12_device *device);
void vkd3d_sampler_state_cleanup(struct vkd3d_sampler_state *state,
        struct d3d12_device *device);
HRESULT vkd3d_sampler_state_create_static_sampler(struct vkd3d_sampler_state *state,
        struct d3d12_device *device, const D3D12_STATIC_SAMPLER_DESC1 *desc, VkSampler *vk_sampler);
HRESULT vkd3d_sampler_state_allocate_descriptor_set(struct vkd3d_sampler_state *state,
        struct d3d12_device *device, VkDescriptorSetLayout vk_layout, VkDescriptorSet *vk_set,
        VkDescriptorPool *vk_pool);
void vkd3d_sampler_state_free_descriptor_set(struct vkd3d_sampler_state *state,
        struct d3d12_device *device, VkDescriptorSet vk_set, VkDescriptorPool vk_pool);

struct vkd3d_global_descriptor_buffer
{
    struct
    {
        VkBuffer vk_buffer;
        VkDeviceAddress va;
        struct vkd3d_device_memory_allocation device_allocation;
        VkBufferUsageFlags2KHR usage;
    } resource, sampler;
};

HRESULT vkd3d_global_descriptor_buffer_init(struct vkd3d_global_descriptor_buffer *global_descriptor_buffer,
        struct d3d12_device *device);
void vkd3d_global_descriptor_buffer_cleanup(struct vkd3d_global_descriptor_buffer *global_descriptor_buffer,
        struct d3d12_device *device);

HRESULT vkd3d_shader_debug_ring_init(struct vkd3d_shader_debug_ring *state,
        struct d3d12_device *device);
void vkd3d_shader_debug_ring_cleanup(struct vkd3d_shader_debug_ring *state,
        struct d3d12_device *device);
void *vkd3d_shader_debug_ring_thread_main(void *arg);
void vkd3d_shader_debug_ring_init_spec_constant(struct d3d12_device *device,
        struct vkd3d_shader_debug_ring_spec_info *info, vkd3d_shader_hash_t hash);
/* If we assume device lost, try really hard to fish for messages. */
void vkd3d_shader_debug_ring_kick(struct vkd3d_shader_debug_ring *state,
        struct d3d12_device *device, bool device_lost);

enum vkd3d_breadcrumb_command_type
{
    VKD3D_BREADCRUMB_COMMAND_SET_TOP_MARKER,
    VKD3D_BREADCRUMB_COMMAND_SET_BOTTOM_MARKER,
    VKD3D_BREADCRUMB_COMMAND_SET_SHADER_HASH,
    VKD3D_BREADCRUMB_COMMAND_DRAW,
    VKD3D_BREADCRUMB_COMMAND_DRAW_INDEXED,
    VKD3D_BREADCRUMB_COMMAND_DISPATCH,
    VKD3D_BREADCRUMB_COMMAND_EXECUTE_INDIRECT,
    VKD3D_BREADCRUMB_COMMAND_EXECUTE_INDIRECT_TEMPLATE,
    VKD3D_BREADCRUMB_COMMAND_COPY,
    VKD3D_BREADCRUMB_COMMAND_COPY_TILES,
    VKD3D_BREADCRUMB_COMMAND_RESOLVE,
    VKD3D_BREADCRUMB_COMMAND_WBI,
    VKD3D_BREADCRUMB_COMMAND_RESOLVE_QUERY,
    VKD3D_BREADCRUMB_COMMAND_GATHER_VIRTUAL_QUERY,
    VKD3D_BREADCRUMB_COMMAND_BUILD_RTAS,
    VKD3D_BREADCRUMB_COMMAND_COPY_RTAS,
    VKD3D_BREADCRUMB_COMMAND_EMIT_RTAS_POSTBUILD,
    VKD3D_BREADCRUMB_COMMAND_TRACE_RAYS,
    VKD3D_BREADCRUMB_COMMAND_BARRIER,
    VKD3D_BREADCRUMB_COMMAND_AUX32, /* Used to report arbitrary 32-bit words as arguments to other commands. */
    VKD3D_BREADCRUMB_COMMAND_AUX64, /* Used to report arbitrary 64-bit words as arguments to other commands. */
    VKD3D_BREADCRUMB_COMMAND_COOKIE, /* 64-bit value representing a resource. */
    VKD3D_BREADCRUMB_COMMAND_VBO,
    VKD3D_BREADCRUMB_COMMAND_IBO,
    VKD3D_BREADCRUMB_COMMAND_ROOT_TABLE,
    VKD3D_BREADCRUMB_COMMAND_ROOT_DESC,
    VKD3D_BREADCRUMB_COMMAND_ROOT_CONST,
    VKD3D_BREADCRUMB_COMMAND_TAG,
    VKD3D_BREADCRUMB_COMMAND_DISCARD,
    VKD3D_BREADCRUMB_COMMAND_CLEAR_INLINE,
    VKD3D_BREADCRUMB_COMMAND_CLEAR_PASS,
    VKD3D_BREADCRUMB_COMMAND_EXECUTE_INDIRECT_PATCH_COMPUTE,
    VKD3D_BREADCRUMB_COMMAND_EXECUTE_INDIRECT_PATCH_STATE_COMPUTE,
    VKD3D_BREADCRUMB_COMMAND_EXECUTE_INDIRECT_UNROLL_COMPUTE,
    VKD3D_BREADCRUMB_COMMAND_DSTORAGE,
    VKD3D_BREADCRUMB_COMMAND_WORKGRAPH_META,
    VKD3D_BREADCRUMB_COMMAND_WORKGRAPH_NODE,
    VKD3D_BREADCRUMB_COMMAND_CLEAR_UAV,
    VKD3D_BREADCRUMB_COMMAND_CLEAR_UAV_COPY,
    VKD3D_BREADCRUMB_COMMAND_SYNC_VAL_CLEAR
};

#if defined(VKD3D_ENABLE_BREADCRUMBS) || defined(VKD3D_ENABLE_DESCRIPTOR_QA)

enum vkd3d_shader_hash_range_qa_flags
{
    VKD3D_SHADER_HASH_RANGE_QA_FLAG_ALLOW = 1 << 0,
    VKD3D_SHADER_HASH_RANGE_QA_FLAG_DISALLOW = 1 << 1,
    VKD3D_SHADER_HASH_RANGE_QA_FLAG_FULL_QA = 1 << 2,
    VKD3D_SHADER_HASH_RANGE_QA_FLAG_FLUSH_NAN = 1 << 3,
    VKD3D_SHADER_HASH_RANGE_QA_FLAG_EXPECT_ASSUME = 1 << 4,
    VKD3D_SHADER_HASH_RANGE_QA_FLAG_SYNC = 1 << 5,
    VKD3D_SHADER_HASH_RANGE_QA_FLAG_SYNC_COMPUTE = 1 << 6
};

struct vkd3d_shader_hash_range
{
    vkd3d_shader_hash_t lo;
    vkd3d_shader_hash_t hi;
    uint32_t flags; /* interpretation of this depends on the kind. */
};

enum vkd3d_shader_hash_range_kind
{
    VKD3D_SHADER_HASH_RANGE_KIND_BARRIERS = 0,
    VKD3D_SHADER_HASH_RANGE_KIND_QA,
};

void vkd3d_shader_hash_range_parse(FILE *file, struct vkd3d_shader_hash_range **ranges,
        size_t *range_size, size_t *range_count, enum vkd3d_shader_hash_range_kind kind);

VkMemoryPropertyFlags vkd3d_debug_buffer_memory_properties(struct d3d12_device *device,
        VkMemoryPropertyFlags flags, bool high_throughput);
#endif

#ifdef VKD3D_ENABLE_BREADCRUMBS
struct vkd3d_breadcrumb_counter
{
    uint32_t begin_marker;
    uint32_t end_marker;
};

struct vkd3d_breadcrumb_command
{
    enum vkd3d_breadcrumb_command_type type;
    union
    {
        struct
        {
            vkd3d_shader_hash_t hash;
            VkShaderStageFlagBits stage;
        } shader;

        uint32_t word_32bit;
        uint64_t word_64bit;
        uint32_t count;
        /* Pointer must remain alive. */
        const char *tag;
    };
};

struct vkd3d_breadcrumb_command_list_trace_context
{
    struct vkd3d_breadcrumb_command *commands;
    size_t command_size;
    size_t command_count;
    uint32_t counter;
    uint32_t locked;
    uint32_t prev;
    uint32_t next;
};

struct vkd3d_breadcrumb_tracer
{
    /* There is room for N live command lists in this system.
     * We can allocate an index for each command list.
     * For AMD buffer markers, the index refers to the u32 counter in mapped.
     * 0 is inactive (has never been executed),
     * 1 is a command set on command buffer begin,
     * UINT_MAX is set on completion of the command buffer.
     * Concurrent submits is not legal. The counter will go back to 1 again from UINT_MAX
     * for multiple submits. */
    VkBuffer host_buffer;
    struct vkd3d_device_memory_allocation host_buffer_memory;
    struct vkd3d_breadcrumb_counter *mapped;

    struct vkd3d_breadcrumb_command_list_trace_context *trace_contexts;
    size_t trace_context_index;

    pthread_mutex_t lock;

    pthread_mutex_t barrier_hash_lock;
    struct vkd3d_shader_hash_range *barrier_hashes;
    size_t barrier_hashes_size;
    uint32_t barrier_hashes_count;

    bool reported_fault;
};

HRESULT vkd3d_breadcrumb_tracer_init(struct vkd3d_breadcrumb_tracer *tracer, struct d3d12_device *device);
void vkd3d_breadcrumb_tracer_init_barrier_hashes(struct vkd3d_breadcrumb_tracer *tracer);
void vkd3d_breadcrumb_tracer_cleanup(struct vkd3d_breadcrumb_tracer *tracer, struct d3d12_device *device);
void vkd3d_breadcrumb_tracer_cleanup_barrier_hashes(struct vkd3d_breadcrumb_tracer *tracer);
unsigned int vkd3d_breadcrumb_tracer_allocate_command_list(struct vkd3d_breadcrumb_tracer *tracer,
        struct d3d12_command_list *list, struct d3d12_command_allocator *allocator);
/* Command allocator keeps a list of allocated breadcrumb command lists. */
void vkd3d_breadcrumb_tracer_release_command_lists(struct vkd3d_breadcrumb_tracer *tracer,
        const unsigned int *indices, size_t indices_count);
void vkd3d_breadcrumb_tracer_report_device_lost(struct vkd3d_breadcrumb_tracer *tracer,
        struct d3d12_device *device);
void vkd3d_breadcrumb_tracer_begin_command_list(struct d3d12_command_list *list);
void vkd3d_breadcrumb_tracer_add_command(struct d3d12_command_list *list,
        const struct vkd3d_breadcrumb_command *command);
void vkd3d_breadcrumb_tracer_signal(struct d3d12_command_list *list);
void vkd3d_breadcrumb_tracer_end_command_list(struct d3d12_command_list *list);
void vkd3d_breadcrumb_tracer_link_submission(struct d3d12_command_list *list,
        struct d3d12_command_list *prev, struct d3d12_command_list *next);

void vkd3d_breadcrumb_tracer_update_barrier_hashes(struct vkd3d_breadcrumb_tracer *tracer);
uint32_t vkd3d_breadcrumb_tracer_shader_hash_forces_barrier(
        struct vkd3d_breadcrumb_tracer *device, vkd3d_shader_hash_t hash);

#define VKD3D_BREADCRUMB_FLUSH_BATCHES(list) do { \
    if (vkd3d_config_flags & VKD3D_CONFIG_FLAG_BREADCRUMBS) { \
        d3d12_command_list_end_transfer_batch(list);          \
    } \
} while(0)

/* For heavy debug, replays the trace stream in submission order. */
void vkd3d_breadcrumb_tracer_dump_command_list(struct vkd3d_breadcrumb_tracer *tracer,
        unsigned int index);

#define VKD3D_BREADCRUMB_COMMAND(cmd_type) do { \
    if (vkd3d_config_flags & VKD3D_CONFIG_FLAG_BREADCRUMBS) { \
        struct vkd3d_breadcrumb_command breadcrumb_cmd; \
        breadcrumb_cmd.type = VKD3D_BREADCRUMB_COMMAND_##cmd_type; \
        vkd3d_breadcrumb_tracer_add_command(list, &breadcrumb_cmd); \
        vkd3d_breadcrumb_tracer_signal(list); \
    } \
} while(0)

/* State commands do no work on their own, should not signal. */
#define VKD3D_BREADCRUMB_COMMAND_STATE(cmd_type) do { \
    if (vkd3d_config_flags & VKD3D_CONFIG_FLAG_BREADCRUMBS) { \
        struct vkd3d_breadcrumb_command breadcrumb_cmd; \
        breadcrumb_cmd.type = VKD3D_BREADCRUMB_COMMAND_##cmd_type; \
        vkd3d_breadcrumb_tracer_add_command(list, &breadcrumb_cmd); \
    } \
} while(0)

#define VKD3D_BREADCRUMB_AUX32(v) do { \
    if (vkd3d_config_flags & VKD3D_CONFIG_FLAG_BREADCRUMBS) { \
        struct vkd3d_breadcrumb_command breadcrumb_cmd; \
        breadcrumb_cmd.type = VKD3D_BREADCRUMB_COMMAND_AUX32; \
        breadcrumb_cmd.word_32bit = v; \
        vkd3d_breadcrumb_tracer_add_command(list, &breadcrumb_cmd); \
    } \
} while(0)

#define VKD3D_BREADCRUMB_AUX64(v) do { \
    if (vkd3d_config_flags & VKD3D_CONFIG_FLAG_BREADCRUMBS) { \
        struct vkd3d_breadcrumb_command breadcrumb_cmd; \
        breadcrumb_cmd.type = VKD3D_BREADCRUMB_COMMAND_AUX64; \
        breadcrumb_cmd.word_64bit = v; \
        vkd3d_breadcrumb_tracer_add_command(list, &breadcrumb_cmd); \
    } \
} while(0)

#define VKD3D_BREADCRUMB_COOKIE(v) do { \
    if (vkd3d_config_flags & VKD3D_CONFIG_FLAG_BREADCRUMBS) { \
        struct vkd3d_breadcrumb_command breadcrumb_cmd; \
        breadcrumb_cmd.type = VKD3D_BREADCRUMB_COMMAND_COOKIE; \
        breadcrumb_cmd.word_64bit = v; \
        vkd3d_breadcrumb_tracer_add_command(list, &breadcrumb_cmd); \
    } \
} while(0)

#define VKD3D_BREADCRUMB_TAG(tag_static_str) do { \
    if (vkd3d_config_flags & VKD3D_CONFIG_FLAG_BREADCRUMBS) { \
        struct vkd3d_breadcrumb_command breadcrumb_cmd; \
        breadcrumb_cmd.type = VKD3D_BREADCRUMB_COMMAND_TAG; \
        breadcrumb_cmd.tag = tag_static_str; \
        vkd3d_breadcrumb_tracer_add_command(list, &breadcrumb_cmd); \
    } \
} while(0)

/* Remember to kick debug ring as well. */
#define VKD3D_DEVICE_REPORT_FAULT_AND_BREADCRUMB_IF(device, cond) do { \
    if (cond) \
        d3d12_device_report_fault(device); \
    if ((vkd3d_config_flags & VKD3D_CONFIG_FLAG_BREADCRUMBS) && (cond)) { \
        vkd3d_breadcrumb_tracer_report_device_lost(&(device)->breadcrumb_tracer, device); \
        vkd3d_shader_debug_ring_kick(&(device)->debug_ring, device, true); \
    } \
} while(0)

static inline void vkd3d_breadcrumb_image(
        struct d3d12_command_list *list, const struct d3d12_resource *resource)
{
    const D3D12_RESOURCE_DESC1 *desc = &resource->desc;
    VKD3D_BREADCRUMB_TAG("ImageDesc [Cookie, DXGI_FORMAT, D3D12_RESOURCE_DIMENSION, width, height, DepthOrArraySize, MipLevels, Flags]");
    VKD3D_BREADCRUMB_COOKIE(resource->res.cookie);
    VKD3D_BREADCRUMB_AUX32(desc->Format);
    VKD3D_BREADCRUMB_AUX32(desc->Dimension);
    VKD3D_BREADCRUMB_AUX64(desc->Width);
    VKD3D_BREADCRUMB_AUX32(desc->Height);
    VKD3D_BREADCRUMB_AUX32(desc->DepthOrArraySize);
    VKD3D_BREADCRUMB_AUX32(desc->MipLevels);
    VKD3D_BREADCRUMB_AUX32(desc->Flags);
}

static inline void vkd3d_breadcrumb_buffer(
        struct d3d12_command_list *list, const struct d3d12_resource *resource)
{
    VKD3D_BREADCRUMB_TAG("BufferDesc [VkBuffer VA, SuballocatedOffset, Cookie, GlobalCookie, Size, Flags]");
    VKD3D_BREADCRUMB_AUX64(resource->mem.resource.va);
    VKD3D_BREADCRUMB_AUX64(resource->mem.offset);
    VKD3D_BREADCRUMB_COOKIE(resource->res.cookie);
    VKD3D_BREADCRUMB_COOKIE(resource->mem.resource.cookie);
    VKD3D_BREADCRUMB_AUX64(resource->desc.Width);
    VKD3D_BREADCRUMB_AUX32(resource->desc.Flags);
}

static inline void vkd3d_breadcrumb_resource(
        struct d3d12_command_list *list, const struct d3d12_resource *resource)
{
    if (d3d12_resource_is_buffer(resource))
        vkd3d_breadcrumb_buffer(list, resource);
    else if (d3d12_resource_is_texture(resource))
        vkd3d_breadcrumb_image(list, resource);
}

static inline void vkd3d_breadcrumb_subresource(
        struct d3d12_command_list *list, const VkImageSubresourceLayers *subresource)
{
    VKD3D_BREADCRUMB_TAG("SubresourceLayers [mipLevel, baseArrayLayer, layerCount, aspectMask]");
    VKD3D_BREADCRUMB_AUX32(subresource->mipLevel);
    VKD3D_BREADCRUMB_AUX32(subresource->baseArrayLayer);
    VKD3D_BREADCRUMB_AUX32(subresource->layerCount);
    VKD3D_BREADCRUMB_AUX32(subresource->aspectMask);
}

static inline void vkd3d_breadcrumb_buffer_image_copy(
        struct d3d12_command_list *list, const VkBufferImageCopy2 *buffer_image)
{
    vkd3d_breadcrumb_subresource(list, &buffer_image->imageSubresource);
    VKD3D_BREADCRUMB_TAG("ImageOffsetExtent [offset, extent, bufferOffset, bufferRowLength, bufferImageHeight]");
    VKD3D_BREADCRUMB_AUX32(buffer_image->imageOffset.x);
    VKD3D_BREADCRUMB_AUX32(buffer_image->imageOffset.y);
    VKD3D_BREADCRUMB_AUX32(buffer_image->imageOffset.z);
    VKD3D_BREADCRUMB_AUX32(buffer_image->imageExtent.width);
    VKD3D_BREADCRUMB_AUX32(buffer_image->imageExtent.height);
    VKD3D_BREADCRUMB_AUX32(buffer_image->imageExtent.depth);
    VKD3D_BREADCRUMB_AUX32(buffer_image->bufferOffset);
    VKD3D_BREADCRUMB_AUX32(buffer_image->bufferRowLength);
    VKD3D_BREADCRUMB_AUX32(buffer_image->bufferImageHeight);
}

static inline void vkd3d_breadcrumb_image_copy(
        struct d3d12_command_list *list, const VkImageCopy2 *image)
{
    vkd3d_breadcrumb_subresource(list, &image->srcSubresource);
    vkd3d_breadcrumb_subresource(list, &image->dstSubresource);
    VKD3D_BREADCRUMB_TAG("ImageOffsetExtent [srcOffset, dstOffset, extent]");
    VKD3D_BREADCRUMB_AUX32(image->srcOffset.x);
    VKD3D_BREADCRUMB_AUX32(image->srcOffset.y);
    VKD3D_BREADCRUMB_AUX32(image->srcOffset.z);
    VKD3D_BREADCRUMB_AUX32(image->dstOffset.x);
    VKD3D_BREADCRUMB_AUX32(image->dstOffset.y);
    VKD3D_BREADCRUMB_AUX32(image->dstOffset.z);
    VKD3D_BREADCRUMB_AUX32(image->extent.width);
    VKD3D_BREADCRUMB_AUX32(image->extent.height);
    VKD3D_BREADCRUMB_AUX32(image->extent.depth);
}

static inline void vkd3d_breadcrumb_buffer_copy(
        struct d3d12_command_list *list, const VkBufferCopy2 *buffer)
{
    VKD3D_BREADCRUMB_TAG("BufferCopy [srcOffset, dstOffset, size]");
    VKD3D_BREADCRUMB_AUX64(buffer->srcOffset);
    VKD3D_BREADCRUMB_AUX64(buffer->dstOffset);
    VKD3D_BREADCRUMB_AUX64(buffer->size);
}

#define VKD3D_BREADCRUMB_RESOURCE(res) vkd3d_breadcrumb_resource(list, res)
#define VKD3D_BREADCRUMB_BUFFER_IMAGE_COPY(buffer_image) vkd3d_breadcrumb_buffer_image_copy(list, buffer_image)
#define VKD3D_BREADCRUMB_IMAGE_COPY(image) vkd3d_breadcrumb_image_copy(list, image)
#define VKD3D_BREADCRUMB_BUFFER_COPY(buffer) vkd3d_breadcrumb_buffer_copy(list, buffer)
#else
#define VKD3D_BREADCRUMB_COMMAND(type) ((void)(VKD3D_BREADCRUMB_COMMAND_##type))
#define VKD3D_BREADCRUMB_COMMAND_STATE(type) ((void)(VKD3D_BREADCRUMB_COMMAND_##type))
#define VKD3D_BREADCRUMB_AUX32(v) ((void)(v))
#define VKD3D_BREADCRUMB_AUX64(v) ((void)(v))
#define VKD3D_BREADCRUMB_COOKIE(v) ((void)(v))
#define VKD3D_DEVICE_REPORT_FAULT_AND_BREADCRUMB_IF(device, cond) do { \
    if (cond) \
        d3d12_device_report_fault(device); \
} while(0)
#define VKD3D_BREADCRUMB_FLUSH_BATCHES(list) ((void)(list))
#define VKD3D_BREADCRUMB_TAG(tag) ((void)(tag))
#define VKD3D_BREADCRUMB_RESOURCE(res) ((void)(res))
#define VKD3D_BREADCRUMB_BUFFER_IMAGE_COPY(buffer_image) ((void)(buffer_image))
#define VKD3D_BREADCRUMB_IMAGE_COPY(image) ((void)(image))
#define VKD3D_BREADCRUMB_BUFFER_COPY(buffer) ((void)(buffer))
#endif /* VKD3D_ENABLE_BREADCRUMBS */

/* Bindless */
enum vkd3d_bindless_flags
{
    VKD3D_BINDLESS_CBV_AS_SSBO                      = (1u << 0),
    VKD3D_BINDLESS_RAW_SSBO                         = (1u << 1),
    VKD3D_SSBO_OFFSET_BUFFER                        = (1u << 2),
    VKD3D_TYPED_OFFSET_BUFFER                       = (1u << 3),
    VKD3D_RAW_VA_ROOT_DESCRIPTOR_CBV                = (1u << 4),
    VKD3D_RAW_VA_ROOT_DESCRIPTOR_SRV_UAV            = (1u << 5),
    VKD3D_BINDLESS_MUTABLE_TYPE                     = (1u << 6),
    VKD3D_HOIST_STATIC_TABLE_CBV                    = (1u << 7),
    VKD3D_BINDLESS_MUTABLE_TYPE_RAW_SSBO            = (1u << 8),
    VKD3D_BINDLESS_MUTABLE_EMBEDDED                 = (1u << 9),
    VKD3D_BINDLESS_MUTABLE_EMBEDDED_PACKED_METADATA = (1u << 10),
    VKD3D_FORCE_COMPUTE_ROOT_PARAMETERS_PUSH_UBO    = (1u << 11),
    VKD3D_BINDLESS_MUTABLE_TYPE_SPLIT_RAW_TYPED     = (1u << 12),
};

#define VKD3D_BINDLESS_SET_MAX_EXTRA_BINDINGS 8

enum vkd3d_bindless_set_flag
{
    VKD3D_BINDLESS_SET_SAMPLER       = (1u << 0),
    VKD3D_BINDLESS_SET_CBV           = (1u << 1),
    VKD3D_BINDLESS_SET_SRV           = (1u << 2),
    VKD3D_BINDLESS_SET_UAV           = (1u << 3),
    VKD3D_BINDLESS_SET_IMAGE         = (1u << 4),
    VKD3D_BINDLESS_SET_BUFFER        = (1u << 5),
    VKD3D_BINDLESS_SET_RAW_SSBO      = (1u << 6),
    VKD3D_BINDLESS_SET_MUTABLE       = (1u << 7),
    VKD3D_BINDLESS_SET_MUTABLE_RAW   = (1u << 8),
    VKD3D_BINDLESS_SET_MUTABLE_TYPED = (1u << 9),

    VKD3D_BINDLESS_SET_EXTRA_RAW_VA_AUX_BUFFER            = (1u << 24),
    VKD3D_BINDLESS_SET_EXTRA_OFFSET_BUFFER                = (1u << 25),
    VKD3D_BINDLESS_SET_EXTRA_FEEDBACK_PAYLOAD_INFO_BUFFER = (1u << 26),
    VKD3D_BINDLESS_SET_EXTRA_FEEDBACK_CONTROL_INFO_BUFFER = (1u << 27),
    VKD3D_BINDLESS_SET_EXTRA_MASK = 0xff000000u
};

/* No need to scan through for common cases. */
enum vkd3d_bindless_state_info_indices
{
    VKD3D_BINDLESS_STATE_INFO_INDEX_SAMPLER = 0,
    VKD3D_BINDLESS_STATE_INFO_INDEX_MUTABLE_SPLIT_TYPED = 1,
    VKD3D_BINDLESS_STATE_INFO_INDEX_MUTABLE_SPLIT_RAW = 2,
    VKD3D_BINDLESS_STATE_INFO_INDEX_MUTABLE_SINGLE = 1,
};

struct vkd3d_bindless_set_info
{
    VkDescriptorType vk_descriptor_type;
    VkDescriptorType vk_init_null_descriptor_type;
    D3D12_DESCRIPTOR_HEAP_TYPE heap_type;
    uint32_t flags; /* vkd3d_bindless_set_flag */
    uint32_t set_index;
    uint32_t binding_index;

    /* For VK_EXT_descriptor_buffer (or VK_VALVE_descriptor_set_host_mapping). */
    size_t host_mapping_offset;
    size_t host_mapping_descriptor_size;
    pfn_vkd3d_host_mapping_copy_template host_copy_template;
    pfn_vkd3d_host_mapping_copy_template_single host_copy_template_single;

    VkDescriptorSetLayout vk_set_layout;
    /* Unused for descriptor buffers. */
    VkDescriptorSetLayout vk_host_set_layout;
};

struct vkd3d_bindless_state
{
    uint32_t flags; /* vkd3d_bindless_flags */

    /* For descriptor buffers, pre-baked array passed directly to vkCmdBindDescriptorBuffersEXT. */
    uint32_t vk_descriptor_buffer_indices[VKD3D_MAX_BINDLESS_DESCRIPTOR_SETS];
    struct vkd3d_bindless_set_info set_info[VKD3D_MAX_BINDLESS_DESCRIPTOR_SETS];
    unsigned int set_count;
    unsigned int cbv_srv_uav_count;

    /* NULL descriptor payloads are not necessarily all zero.
     * Access the array with vkd3d_bindless_state_get_null_descriptor_payload(). */
    DECLSPEC_ALIGN(16) uint8_t null_descriptor_payloads[6][VKD3D_MAX_DESCRIPTOR_SIZE];
    size_t descriptor_buffer_cbv_srv_uav_size;
    size_t descriptor_buffer_sampler_size;
    unsigned int descriptor_buffer_cbv_srv_uav_size_log2;
    unsigned int descriptor_buffer_sampler_size_log2;
    unsigned int descriptor_buffer_packed_raw_buffer_offset;
    unsigned int descriptor_buffer_packed_metadata_offset;
};

HRESULT vkd3d_bindless_state_init(struct vkd3d_bindless_state *bindless_state,
        struct d3d12_device *device);
void vkd3d_bindless_state_cleanup(struct vkd3d_bindless_state *bindless_state,
        struct d3d12_device *device);
bool vkd3d_bindless_state_find_binding(const struct vkd3d_bindless_state *bindless_state,
        uint32_t flags, struct vkd3d_shader_descriptor_binding *binding);
struct vkd3d_descriptor_binding vkd3d_bindless_state_find_set(const struct vkd3d_bindless_state *bindless_state, uint32_t flags);
uint32_t vkd3d_bindless_state_find_set_info_index(const struct vkd3d_bindless_state *bindless_state,
        uint32_t flags);

static inline struct vkd3d_descriptor_binding vkd3d_bindless_state_binding_from_info_index(
        const struct vkd3d_bindless_state *bindless_state, uint32_t index)
{
    struct vkd3d_descriptor_binding binding;
    binding.binding = bindless_state->set_info[index].binding_index;
    binding.set = bindless_state->set_info[index].set_index;
    return binding;
}

static inline VkDescriptorType vkd3d_bindless_state_get_cbv_descriptor_type(const struct vkd3d_bindless_state *bindless_state)
{
    return bindless_state->flags & VKD3D_BINDLESS_CBV_AS_SSBO
            ? VK_DESCRIPTOR_TYPE_STORAGE_BUFFER
            : VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
}

static inline uint8_t *vkd3d_bindless_state_get_null_descriptor_payload(struct vkd3d_bindless_state *bindless_state,
        VkDescriptorType type)
{
    /* The descriptor types we care about are laid out nicely in enum-space. */
    int index = type;
    assert(index >= 2 && index < 8);
    return bindless_state->null_descriptor_payloads[index - 2];
}

enum vkd3d_format_type
{
    VKD3D_FORMAT_TYPE_OTHER,
    VKD3D_FORMAT_TYPE_TYPELESS,
    VKD3D_FORMAT_TYPE_SINT,
    VKD3D_FORMAT_TYPE_UINT,
};

void vkd3d_format_compatibility_list_add_format(struct vkd3d_format_compatibility_list *list, VkFormat vk_format);

struct vkd3d_memory_info_domain
{
    uint32_t buffer_type_mask;
    uint32_t sampled_type_mask;
    uint32_t rt_ds_type_mask;
};

struct vkd3d_memory_info
{
    /* Includes normal system memory, but also resizable BAR memory.
     * Only types which have HOST_VISIBLE_BIT can be in this domain.
     * For images, we only include memory types which are LINEAR tiled. */
    struct vkd3d_memory_info_domain cpu_accessible_domain;
    /* Also includes fallback memory types when DEVICE_LOCAL is exhausted.
     * It can include HOST_VISIBLE_BIT as well, but when choosing this domain,
     * that's not something we care about.
     * Used when we want to allocate DEFAULT heaps or non-visible CUSTOM heaps.
     * For images, we only include memory types which are OPTIMAL tiled. */
    struct vkd3d_memory_info_domain non_cpu_accessible_domain;

    VkMemoryPropertyFlags upload_heap_memory_properties;
    VkMemoryPropertyFlags descriptor_heap_memory_properties;

    uint32_t rebar_budget_mask;
    VkDeviceSize rebar_budget;
    VkDeviceSize rebar_current;

    bool has_gpu_upload_heap;

    /* Only used for debug logging. */
    VkDeviceSize type_current[VK_MAX_MEMORY_TYPES];

    pthread_mutex_t budget_lock;
    uint32_t has_used_gpu_upload_heap;
};

HRESULT vkd3d_memory_info_init(struct vkd3d_memory_info *info,
        struct d3d12_device *device);
void vkd3d_memory_info_cleanup(struct vkd3d_memory_info *info,
        struct d3d12_device *device);

/* meta operations */
struct vkd3d_clear_uav_args
{
    VkClearColorValue clear_color;
    VkOffset2D offset;
    VkExtent2D extent;
};

struct vkd3d_clear_uav_pipelines
{
    VkPipeline buffer;
    VkPipeline buffer_raw;
    VkPipeline image_1d;
    VkPipeline image_2d;
    VkPipeline image_3d;
    VkPipeline image_1d_array;
    VkPipeline image_2d_array;
};

struct vkd3d_clear_uav_ops
{
    VkDescriptorSetLayout vk_set_layout_buffer_raw;
    VkDescriptorSetLayout vk_set_layout_buffer;
    VkDescriptorSetLayout vk_set_layout_image;

    VkPipelineLayout vk_pipeline_layout_buffer_raw;
    VkPipelineLayout vk_pipeline_layout_buffer;
    VkPipelineLayout vk_pipeline_layout_image;

    struct vkd3d_clear_uav_pipelines clear_float;
    struct vkd3d_clear_uav_pipelines clear_uint;
};

struct vkd3d_clear_uav_pipeline
{
    VkDescriptorSetLayout vk_set_layout;
    VkPipelineLayout vk_pipeline_layout;
    VkPipeline vk_pipeline;
};

struct vkd3d_copy_image_args
{
    VkOffset2D offset;
    uint32_t bit_mask;
};

struct vkd3d_copy_image_info
{
    VkDescriptorSetLayout vk_set_layout;
    VkPipelineLayout vk_pipeline_layout;
    VkPipeline vk_pipeline;
    bool needs_stencil_mask;
};

struct vkd3d_copy_image_pipeline_key
{
    const struct vkd3d_format *format;
    VkImageViewType view_type;
    VkSampleCountFlagBits sample_count;
    VkImageAspectFlags dst_aspect_mask;
};

struct vkd3d_copy_image_pipeline
{
    struct vkd3d_copy_image_pipeline_key key;

    VkPipeline vk_pipeline;
};

struct vkd3d_copy_image_ops
{
    VkDescriptorSetLayout vk_set_layout;
    VkPipelineLayout vk_pipeline_layout;
    VkShaderModule vk_fs_float_module;
    VkShaderModule vk_fs_uint_module;
    VkShaderModule vk_fs_stencil_module;

    pthread_mutex_t mutex;

    struct vkd3d_copy_image_pipeline *pipelines;
    size_t pipelines_size;
    size_t pipeline_count;
};

enum vkd3d_resolve_image_path
{
    VKD3D_RESOLVE_IMAGE_PATH_UNSUPPORTED,
    VKD3D_RESOLVE_IMAGE_PATH_DIRECT,
    VKD3D_RESOLVE_IMAGE_PATH_RENDER_PASS_ATTACHMENT,
    VKD3D_RESOLVE_IMAGE_PATH_RENDER_PASS_PIPELINE,
    VKD3D_RESOLVE_IMAGE_PATH_COMPUTE_PIPELINE,
};

struct vkd3d_resolve_image_args
{
    VkOffset2D offset;
    uint32_t bit_mask;
};

struct vkd3d_resolve_image_compute_args
{
    VkOffset2D dst_offset;
    VkOffset2D src_offset;
    VkExtent2D extent;
};

struct vkd3d_resolve_image_info
{
    VkDescriptorSetLayout vk_set_layout;
    VkPipelineLayout vk_pipeline_layout;
    VkPipeline vk_pipeline;
    bool needs_stencil_mask;
};

struct vkd3d_resolve_image_graphics_pipeline_key
{
    const struct vkd3d_format *format;
    VkImageAspectFlagBits dst_aspect;
    D3D12_RESOLVE_MODE mode;
};

struct vkd3d_resolve_image_compute_pipeline_key
{
    enum vkd3d_format_type format_type;
    D3D12_RESOLVE_MODE mode;
    VkBool32 srgb;
};

struct vkd3d_resolve_image_pipeline_key
{
    enum vkd3d_resolve_image_path path;
    union
    {
        struct vkd3d_resolve_image_graphics_pipeline_key graphics;
        struct vkd3d_resolve_image_compute_pipeline_key compute;
    };
};

struct vkd3d_resolve_image_pipeline
{
    struct vkd3d_resolve_image_pipeline_key key;

    VkPipeline vk_pipeline;
};

struct vkd3d_resolve_image_ops
{
    VkDescriptorSetLayout vk_graphics_set_layout;
    VkDescriptorSetLayout vk_compute_set_layout;
    VkPipelineLayout vk_graphics_pipeline_layout;
    VkPipelineLayout vk_compute_pipeline_layout;
    VkShaderModule vk_fs_float_module;
    VkShaderModule vk_fs_uint_module;
    VkShaderModule vk_fs_sint_module;
    VkShaderModule vk_fs_depth_module;
    VkShaderModule vk_fs_stencil_module;

    pthread_mutex_t mutex;

    struct vkd3d_resolve_image_pipeline *pipelines;
    size_t pipelines_size;
    size_t pipeline_count;
};

struct vkd3d_swapchain_pipeline_key
{
    VkPipelineBindPoint bind_point;
    VkFormat format;
    VkFilter filter;
};

struct vkd3d_swapchain_info
{
    VkDescriptorSetLayout vk_set_layout;
    VkPipelineLayout vk_pipeline_layout;
    VkPipeline vk_pipeline;
};

struct vkd3d_swapchain_pipeline
{
    VkPipeline vk_pipeline;
    struct vkd3d_swapchain_pipeline_key key;
};

struct vkd3d_swapchain_ops
{
    VkDescriptorSetLayout vk_set_layouts[2];
    VkPipelineLayout vk_pipeline_layouts[2];
    VkShaderModule vk_vs_module;
    VkShaderModule vk_fs_module;
    VkSampler vk_samplers[2];

    pthread_mutex_t mutex;

    struct vkd3d_swapchain_pipeline *pipelines;
    size_t pipelines_size;
    size_t pipeline_count;
};

#define VKD3D_QUERY_OP_WORKGROUP_SIZE (64)

struct vkd3d_query_resolve_args
{
    VkDeviceAddress dst_va;
    VkDeviceAddress src_va;
    uint32_t query_count;
};

struct vkd3d_query_gather_args
{
    VkDeviceAddress dst_va;
    VkDeviceAddress src_va;
    VkDeviceAddress map_va;
    uint32_t query_count;
};

struct vkd3d_query_gather_info
{
    VkPipelineLayout vk_pipeline_layout;
    VkPipeline vk_pipeline;
};

struct vkd3d_query_ops
{
    VkPipelineLayout vk_gather_pipeline_layout;
    VkPipeline vk_gather_occlusion_pipeline;
    VkPipeline vk_gather_so_statistics_pipeline;
    VkPipelineLayout vk_resolve_pipeline_layout;
    VkPipeline vk_resolve_binary_pipeline;
};

struct vkd3d_predicate_command_direct_args_execute_indirect
{
    uint32_t max_commands;
    uint32_t stride_words;
};

union vkd3d_predicate_command_direct_args
{
    VkDispatchIndirectCommand dispatch;
    VkDrawIndirectCommand draw;
    VkDrawIndexedIndirectCommand draw_indexed;
    struct vkd3d_predicate_command_direct_args_execute_indirect execute_indirect;
    uint32_t draw_count;
};

struct vkd3d_predicate_command_args
{
    VkDeviceAddress predicate_va;
    VkDeviceAddress src_arg_va;
    VkDeviceAddress dst_arg_va;
    union vkd3d_predicate_command_direct_args args;
};

enum vkd3d_predicate_command_type
{
    VKD3D_PREDICATE_COMMAND_DRAW,
    VKD3D_PREDICATE_COMMAND_DRAW_INDEXED,
    VKD3D_PREDICATE_COMMAND_DRAW_INDIRECT,
    VKD3D_PREDICATE_COMMAND_DRAW_INDIRECT_COUNT,
    VKD3D_PREDICATE_COMMAND_DISPATCH,
    VKD3D_PREDICATE_COMMAND_DISPATCH_INDIRECT,
    VKD3D_PREDICATE_COMMAND_EXECUTE_INDIRECT_GRAPHICS,
    VKD3D_PREDICATE_COMMAND_EXECUTE_INDIRECT_COMPUTE,
    VKD3D_PREDICATE_COMMAND_COUNT
};

enum vkd3d_sampler_feedback_resolve_type
{
    VKD3D_SAMPLER_FEEDBACK_RESOLVE_MIN_MIP_TO_BUFFER,
    VKD3D_SAMPLER_FEEDBACK_RESOLVE_BUFFER_TO_MIN_MIP,
    VKD3D_SAMPLER_FEEDBACK_RESOLVE_MIN_MIP_TO_IMAGE,
    VKD3D_SAMPLER_FEEDBACK_RESOLVE_IMAGE_TO_MIN_MIP,
    VKD3D_SAMPLER_FEEDBACK_RESOLVE_MIP_USED_TO_IMAGE,
    VKD3D_SAMPLER_FEEDBACK_RESOLVE_IMAGE_TO_MIP_USED,
    VKD3D_SAMPLER_FEEDBACK_RESOLVE_COUNT
};

struct vkd3d_predicate_command_info
{
    VkPipelineLayout vk_pipeline_layout;
    VkPipeline vk_pipeline;
    uint32_t data_size;
};

struct vkd3d_predicate_resolve_args
{
    VkDeviceAddress src_va;
    VkDeviceAddress dst_va;
    VkBool32 invert;
};

struct vkd3d_predicate_ops
{
    VkPipelineLayout vk_command_pipeline_layout;
    VkPipelineLayout vk_resolve_pipeline_layout;
    VkPipeline vk_command_pipelines[VKD3D_PREDICATE_COMMAND_COUNT];
    VkPipeline vk_resolve_pipeline;
    uint32_t data_sizes[VKD3D_PREDICATE_COMMAND_COUNT];
};

struct vkd3d_multi_dispatch_indirect_info
{
    VkPipelineLayout vk_pipeline_layout;
    VkPipeline vk_pipeline;
};

struct vkd3d_multi_dispatch_indirect_args
{
    VkDeviceAddress indirect_va;
    VkDeviceAddress count_va;
    VkDeviceAddress output_va;
    uint32_t stride_words;
    uint32_t max_commands;
};

struct vkd3d_multi_dispatch_indirect_state_args
{
    VkDeviceAddress indirect_va;
    VkDeviceAddress count_va;
    VkDeviceAddress dispatch_va;
    VkDeviceAddress root_parameters_va;
    VkDeviceAddress root_parameter_template_va;
    uint32_t stride_words;
    uint32_t dispatch_offset_words;
};

struct vkd3d_multi_dispatch_indirect_ops
{
    VkPipelineLayout vk_multi_dispatch_indirect_layout;
    VkPipelineLayout vk_multi_dispatch_indirect_state_layout;
    VkPipeline vk_multi_dispatch_indirect_pipeline;
    VkPipeline vk_multi_dispatch_indirect_state_pipeline;
};

struct vkd3d_execute_indirect_args
{
    VkDeviceAddress template_va;
    VkDeviceAddress api_buffer_va;
    VkDeviceAddress device_generated_commands_va;
    VkDeviceAddress indirect_count_va;
    VkDeviceAddress dst_indirect_count_va;
    uint32_t api_buffer_word_stride;
    uint32_t device_generated_commands_word_stride;

    /* Arbitrary tag used for debug version of state patcher. Debug messages from tag 0 are ignored. */
    uint32_t debug_tag;
    uint32_t implicit_instance;
};

struct vkd3d_execute_indirect_pipeline
{
    VkPipeline vk_pipeline;
    uint32_t workgroup_size_x;
};

struct vkd3d_execute_indirect_ops
{
    VkPipelineLayout vk_pipeline_layout;
    struct vkd3d_execute_indirect_pipeline *pipelines;
    size_t pipelines_count;
    size_t pipelines_size;
    pthread_mutex_t mutex;
};

struct vkd3d_dstorage_emit_nv_memory_decompression_regions_args
{
    VkDeviceAddress control_va;
    VkDeviceAddress src_buffer_va;
    VkDeviceAddress dst_buffer_va;
    VkDeviceAddress scratch_va;
    uint32_t stream_count;
    uint32_t stream_index;
};

struct vkd3d_dstorage_ops
{
    VkPipelineLayout vk_emit_nv_memory_decompression_regions_layout;
    VkPipeline vk_emit_nv_memory_decompression_regions_pipeline;
    VkPipeline vk_emit_nv_memory_decompression_workgroups_pipeline;
};

struct vkd3d_meta_ops_common
{
    VkShaderModule vk_module_fullscreen_vs;
    VkShaderModule vk_module_fullscreen_gs;
};

struct vkd3d_sampler_feedback_resolve_info
{
    VkPipelineLayout vk_layout;
    VkPipeline vk_pipeline;
};

struct vkd3d_sampler_feedback_resolve_decode_args
{
    uint32_t src_x, src_y;
    uint32_t dst_x, dst_y;
    uint32_t resolve_width, resolve_height;
    uint32_t paired_width, paired_height;
    float inv_paired_width, inv_paired_height;
    float inv_feedback_width, inv_feedback_height;
    uint32_t num_mip_levels;
    uint32_t mip_level;
};

struct vkd3d_sampler_feedback_resolve_encode_args
{
    uint32_t src_x, src_y;
    uint32_t dst_x, dst_y;
    uint32_t resolve_width, resolve_height;
    uint32_t src_mip;
    uint32_t dst_mip;
};

struct vkd3d_sampler_feedback_resolve_ops
{
    VkPipelineLayout vk_compute_encode_layout;
    VkPipelineLayout vk_compute_decode_layout;
    VkPipelineLayout vk_graphics_decode_layout;
    VkDescriptorSetLayout vk_decode_set_layout;
    VkDescriptorSetLayout vk_encode_set_layout;
    VkPipeline vk_pipelines[VKD3D_SAMPLER_FEEDBACK_RESOLVE_COUNT];
};

struct vkd3d_workgraph_payload_offsets_args
{
    VkDeviceAddress packed_offset_counts;
    VkDeviceAddress unrolled_offsets;
    VkDeviceAddress commands;
    VkDeviceAddress payload;
    VkDeviceAddress meta;
};

struct vkd3d_workgraph_complete_compaction_args
{
    VkDeviceAddress commands;
    VkDeviceAddress meta;
    uint32_t node_count;
};

struct vkd3d_workgraph_workgroups_args
{
    VkDeviceAddress node_atomics_va;
    VkDeviceAddress commands_va;
    VkDeviceAddress dividers_va;
    VkDeviceAddress node_share_mapping_va;
    uint32_t num_nodes;
};

/* If the implementation supports 16M workgroups (arbitrarily chosen large number),
 * we don't have to split execution into primary and secondary.
 * Reduces number of indirect node dispatches by a factor of 2 since the primary will always be empty. */
#define VKD3D_WORKGRAPH_MAX_WGX_NO_PRIMARY_EXECUTION_THRESHOLD 0xffffffu

struct vkd3d_workgraph_setup_gpu_input_args
{
    VkDeviceAddress gpu_input_va;
    VkDeviceAddress indirect_commands_va;
    VkDeviceAddress coalesce_divider_va;
    VkDeviceAddress entry_point_mapping_va;
    uint32_t num_entry_points;
};

struct vkd3d_workgraph_indirect_pipeline
{
    uint32_t component_count;
    uint32_t component_bits;
    bool group_tracking;
    bool group_compact;
    VkPipeline vk_pipeline;
};

struct vkd3d_workgraph_indirect_ops
{
    VkPipelineLayout vk_setup_gpu_input_layout;
    VkPipelineLayout vk_complete_compaction_layout;
    VkPipelineLayout vk_workgroup_layout;
    VkPipelineLayout vk_payload_offset_layout;
    VkPipeline vk_payload_workgroup_pipeline[2];
    VkPipeline vk_setup_gpu_input_pipeline;
    VkPipeline vk_payload_offset_pipeline;
    VkPipeline vk_complete_compaction_pipeline;
};

struct vkd3d_meta_ops
{
    struct d3d12_device *device;
    struct vkd3d_meta_ops_common common;
    struct vkd3d_clear_uav_ops clear_uav;
    struct vkd3d_copy_image_ops copy_image;
    struct vkd3d_resolve_image_ops resolve_image;
    struct vkd3d_swapchain_ops swapchain;
    struct vkd3d_query_ops query;
    struct vkd3d_predicate_ops predicate;
    struct vkd3d_execute_indirect_ops execute_indirect;
    struct vkd3d_multi_dispatch_indirect_ops multi_dispatch_indirect;
    struct vkd3d_dstorage_ops dstorage;
    struct vkd3d_sampler_feedback_resolve_ops sampler_feedback;
    struct vkd3d_workgraph_indirect_ops workgraph;
};

HRESULT vkd3d_meta_ops_init(struct vkd3d_meta_ops *meta_ops, struct d3d12_device *device);
HRESULT vkd3d_meta_ops_cleanup(struct vkd3d_meta_ops *meta_ops, struct d3d12_device *device);

struct vkd3d_clear_uav_pipeline vkd3d_meta_get_clear_buffer_uav_pipeline(struct vkd3d_meta_ops *meta_ops,
        bool as_uint, bool raw);
struct vkd3d_clear_uav_pipeline vkd3d_meta_get_clear_image_uav_pipeline(struct vkd3d_meta_ops *meta_ops,
        VkImageViewType image_view_type, bool as_uint);
VkExtent3D vkd3d_meta_get_clear_image_uav_workgroup_size(VkImageViewType view_type);

static inline VkExtent3D vkd3d_meta_get_clear_buffer_uav_workgroup_size()
{
    VkExtent3D result = { 128, 1, 1 };
    return result;
}

HRESULT vkd3d_meta_get_copy_image_pipeline(struct vkd3d_meta_ops *meta_ops,
        const struct vkd3d_copy_image_pipeline_key *key, struct vkd3d_copy_image_info *info);
VkImageViewType vkd3d_meta_get_copy_image_view_type(D3D12_RESOURCE_DIMENSION dim);
const struct vkd3d_format *vkd3d_meta_get_copy_image_attachment_format(struct vkd3d_meta_ops *meta_ops,
        const struct vkd3d_format *dst_format, const struct vkd3d_format *src_format,
        VkImageAspectFlags dst_aspect, VkImageAspectFlags src_aspect);
HRESULT vkd3d_meta_get_resolve_image_pipeline(struct vkd3d_meta_ops *meta_ops,
        const struct vkd3d_resolve_image_pipeline_key *key, struct vkd3d_resolve_image_info *info);
HRESULT vkd3d_meta_get_swapchain_pipeline(struct vkd3d_meta_ops *meta_ops,
        const struct vkd3d_swapchain_pipeline_key *key, struct vkd3d_swapchain_info *info);

bool vkd3d_meta_get_query_gather_pipeline(struct vkd3d_meta_ops *meta_ops,
        D3D12_QUERY_HEAP_TYPE heap_type, struct vkd3d_query_gather_info *info);

void vkd3d_meta_get_predicate_pipeline(struct vkd3d_meta_ops *meta_ops,
        enum vkd3d_predicate_command_type command_type, struct vkd3d_predicate_command_info *info);

void vkd3d_meta_get_multi_dispatch_indirect_pipeline(struct vkd3d_meta_ops *meta_ops,
        struct vkd3d_multi_dispatch_indirect_info *info);
void vkd3d_meta_get_multi_dispatch_indirect_state_pipeline(struct vkd3d_meta_ops *meta_ops,
        struct vkd3d_multi_dispatch_indirect_info *info);

static inline uint32_t vkd3d_meta_get_multi_dispatch_indirect_workgroup_size(void)
{
    return 32;
}

HRESULT vkd3d_meta_get_execute_indirect_pipeline(struct vkd3d_meta_ops *meta_ops,
        uint32_t patch_command_count, struct vkd3d_execute_indirect_info *info);

void vkd3d_meta_get_sampler_feedback_resolve_pipeline(struct vkd3d_meta_ops *meta_ops,
        enum vkd3d_sampler_feedback_resolve_type type, struct vkd3d_sampler_feedback_resolve_info *info);

static inline VkExtent3D vkd3d_meta_get_sampler_feedback_workgroup_size(void)
{
    VkExtent3D result = { 8, 8, 1 };
    return result;
}

struct vkd3d_workgraph_meta_pipeline_info
{
    VkPipeline vk_pipeline;
    VkPipelineLayout vk_pipeline_layout;
};

void vkd3d_meta_get_workgraph_workgroup_pipeline(struct vkd3d_meta_ops *meta_ops,
        struct vkd3d_workgraph_meta_pipeline_info *info, bool broadcast_compacting);
void vkd3d_meta_get_workgraph_setup_gpu_input_pipeline(struct vkd3d_meta_ops *meta_ops,
        struct vkd3d_workgraph_meta_pipeline_info *info);
void vkd3d_meta_get_workgraph_payload_offset_pipeline(struct vkd3d_meta_ops *meta_ops,
        struct vkd3d_workgraph_meta_pipeline_info *info);
void vkd3d_meta_get_workgraph_complete_compaction_pipeline(struct vkd3d_meta_ops *meta_ops,
        struct vkd3d_workgraph_meta_pipeline_info *info);

static inline uint32_t vkd3d_meta_get_workgraph_setup_gpu_input_workgroup_size(void)
{
    return 32;
}

static inline uint32_t vkd3d_meta_get_workgraph_complete_compaction_workgroup_size(void)
{
    return 32;
}

enum vkd3d_time_domain_flag
{
    VKD3D_TIME_DOMAIN_DEVICE = 0x00000001u,
    VKD3D_TIME_DOMAIN_QPC    = 0x00000002u,
};

struct vkd3d_physical_device_info
{
    /* properties */
    VkPhysicalDeviceVulkan11Properties vulkan_1_1_properties;
    VkPhysicalDeviceVulkan12Properties vulkan_1_2_properties;
    VkPhysicalDeviceVulkan13Properties vulkan_1_3_properties;
    VkPhysicalDevicePushDescriptorPropertiesKHR push_descriptor_properties;
    VkPhysicalDeviceTransformFeedbackPropertiesEXT xfb_properties;
    VkPhysicalDeviceVertexAttributeDivisorPropertiesEXT vertex_divisor_properties;
    VkPhysicalDeviceCustomBorderColorPropertiesEXT custom_border_color_properties;
    VkPhysicalDeviceShaderCorePropertiesAMD shader_core_properties;
    VkPhysicalDeviceShaderCoreProperties2AMD shader_core_properties2;
    VkPhysicalDeviceShaderSMBuiltinsPropertiesNV shader_sm_builtins_properties;
    VkPhysicalDeviceRobustness2PropertiesEXT robustness2_properties;
    VkPhysicalDeviceExternalMemoryHostPropertiesEXT external_memory_host_properties;
    VkPhysicalDeviceRayTracingPipelinePropertiesKHR ray_tracing_pipeline_properties;
    VkPhysicalDeviceAccelerationStructurePropertiesKHR acceleration_structure_properties;
    VkPhysicalDeviceFragmentShadingRatePropertiesKHR fragment_shading_rate_properties;
    VkPhysicalDeviceConservativeRasterizationPropertiesEXT conservative_rasterization_properties;
    VkPhysicalDeviceDeviceGeneratedCommandsPropertiesNV device_generated_commands_properties_nv;
    VkPhysicalDeviceDeviceGeneratedCommandsPropertiesEXT device_generated_commands_properties_ext;
    VkPhysicalDeviceMeshShaderPropertiesEXT mesh_shader_properties;
    VkPhysicalDeviceShaderModuleIdentifierPropertiesEXT shader_module_identifier_properties;
    VkPhysicalDeviceDescriptorBufferPropertiesEXT descriptor_buffer_properties;
    VkPhysicalDeviceGraphicsPipelineLibraryPropertiesEXT graphics_pipeline_library_properties;
    VkPhysicalDeviceMemoryDecompressionPropertiesNV memory_decompression_properties;
    VkPhysicalDeviceMaintenance5PropertiesKHR maintenance_5_properties;
    VkPhysicalDeviceMaintenance6PropertiesKHR maintenance_6_properties;
    VkPhysicalDeviceMaintenance7PropertiesKHR maintenance_7_properties;
    /* the ID of the layer implementation if running layered */
    VkDriverId layer_driver_id;
    VkPhysicalDeviceLineRasterizationPropertiesEXT line_rasterization_properties;
    VkPhysicalDeviceComputeShaderDerivativesPropertiesKHR compute_shader_derivatives_properties_khr;

    VkPhysicalDeviceProperties2KHR properties2;

    /* features */
    VkPhysicalDeviceVulkan11Features vulkan_1_1_features;
    VkPhysicalDeviceVulkan12Features vulkan_1_2_features;
    VkPhysicalDeviceVulkan13Features vulkan_1_3_features;
    VkPhysicalDeviceConditionalRenderingFeaturesEXT conditional_rendering_features;
    VkPhysicalDeviceDepthClipEnableFeaturesEXT depth_clip_features;
    VkPhysicalDeviceTransformFeedbackFeaturesEXT xfb_features;
    VkPhysicalDeviceVertexAttributeDivisorFeaturesEXT vertex_divisor_features;
    VkPhysicalDeviceCustomBorderColorFeaturesEXT custom_border_color_features;
    VkPhysicalDeviceRobustness2FeaturesEXT robustness2_features;
    VkPhysicalDeviceExtendedDynamicState2FeaturesEXT extended_dynamic_state2_features;
    VkPhysicalDeviceExtendedDynamicState3FeaturesEXT extended_dynamic_state3_features;
    VkPhysicalDeviceMutableDescriptorTypeFeaturesEXT mutable_descriptor_features;
    VkPhysicalDeviceRayTracingPipelineFeaturesKHR ray_tracing_pipeline_features;
    VkPhysicalDeviceAccelerationStructureFeaturesKHR acceleration_structure_features;
    VkPhysicalDeviceFragmentShadingRateFeaturesKHR fragment_shading_rate_features;
    VkPhysicalDeviceFragmentShaderBarycentricFeaturesNV barycentric_features_nv;
    VkPhysicalDeviceFragmentShaderBarycentricFeaturesKHR barycentric_features_khr;
    VkPhysicalDeviceRayQueryFeaturesKHR ray_query_features;
    VkPhysicalDeviceComputeShaderDerivativesFeaturesKHR compute_shader_derivatives_features_khr;
    VkPhysicalDeviceShaderImageAtomicInt64FeaturesEXT shader_image_atomic_int64_features;
    VkPhysicalDeviceImageViewMinLodFeaturesEXT image_view_min_lod_features;
    VkPhysicalDeviceCoherentMemoryFeaturesAMD device_coherent_memory_features_amd;
    VkPhysicalDeviceRayTracingMaintenance1FeaturesKHR ray_tracing_maintenance1_features;
    VkPhysicalDeviceDeviceGeneratedCommandsFeaturesNV device_generated_commands_features_nv;
    VkPhysicalDeviceDeviceGeneratedCommandsFeaturesEXT device_generated_commands_features_ext;
    VkPhysicalDeviceMeshShaderFeaturesEXT mesh_shader_features;
    VkPhysicalDeviceShaderModuleIdentifierFeaturesEXT shader_module_identifier_features;
    VkPhysicalDevicePresentIdFeaturesKHR present_id_features;
    VkPhysicalDevicePresentWaitFeaturesKHR present_wait_features;
    VkPhysicalDeviceDescriptorBufferFeaturesEXT descriptor_buffer_features;
    VkPhysicalDevicePipelineLibraryGroupHandlesFeaturesEXT pipeline_library_group_handles_features;
    VkPhysicalDeviceImageSlicedViewOf3DFeaturesEXT image_sliced_view_of_3d_features;
    VkPhysicalDeviceGraphicsPipelineLibraryFeaturesEXT graphics_pipeline_library_features;
    VkPhysicalDeviceFragmentShaderInterlockFeaturesEXT fragment_shader_interlock_features;
    VkPhysicalDeviceMemoryPriorityFeaturesEXT memory_priority_features;
    VkPhysicalDevicePageableDeviceLocalMemoryFeaturesEXT pageable_device_memory_features;
    VkPhysicalDeviceDynamicRenderingUnusedAttachmentsFeaturesEXT dynamic_rendering_unused_attachments_features;
    VkPhysicalDeviceMemoryDecompressionFeaturesNV memory_decompression_features;
    VkPhysicalDeviceDeviceGeneratedCommandsComputeFeaturesNV device_generated_commands_compute_features_nv;
    VkPhysicalDeviceMaintenance5FeaturesKHR maintenance_5_features;
    VkPhysicalDeviceMaintenance6FeaturesKHR maintenance_6_features;
    VkPhysicalDeviceMaintenance7FeaturesKHR maintenance_7_features;
    VkPhysicalDeviceMaintenance8FeaturesKHR maintenance_8_features;
    VkPhysicalDeviceLineRasterizationFeaturesEXT line_rasterization_features;
    VkPhysicalDeviceImageCompressionControlFeaturesEXT image_compression_control_features;
    VkPhysicalDeviceFaultFeaturesEXT fault_features;
    VkPhysicalDeviceSwapchainMaintenance1FeaturesEXT swapchain_maintenance1_features;
    VkPhysicalDeviceShaderMaximalReconvergenceFeaturesKHR shader_maximal_reconvergence_features;
    VkPhysicalDeviceShaderQuadControlFeaturesKHR shader_quad_control_features;
    VkPhysicalDeviceRawAccessChainsFeaturesNV raw_access_chains_nv;
    VkPhysicalDeviceAddressBindingReportFeaturesEXT address_binding_report_features;
    VkPhysicalDeviceImageAlignmentControlFeaturesMESA image_alignment_control_features;
    VkPhysicalDeviceImageAlignmentControlPropertiesMESA image_alignment_control_properties;
    VkPhysicalDeviceDepthBiasControlFeaturesEXT depth_bias_control_features;
    VkPhysicalDeviceOpticalFlowFeaturesNV optical_flow_nv_features;

    VkPhysicalDeviceFeatures2 features2;

    /* others, for extensions that have no feature bits */
    uint32_t time_domains;  /* vkd3d_time_domain_flag */

    bool additional_shading_rates_supported; /* d3d12 additional fragment shading rates cap */
};

struct d3d12_caps
{
    D3D12_FEATURE_DATA_D3D12_OPTIONS options;
    D3D12_FEATURE_DATA_D3D12_OPTIONS1 options1;
    D3D12_FEATURE_DATA_D3D12_OPTIONS2 options2;
    D3D12_FEATURE_DATA_D3D12_OPTIONS3 options3;
    D3D12_FEATURE_DATA_D3D12_OPTIONS4 options4;
    D3D12_FEATURE_DATA_D3D12_OPTIONS5 options5;
    D3D12_FEATURE_DATA_D3D12_OPTIONS6 options6;
    D3D12_FEATURE_DATA_D3D12_OPTIONS7 options7;
    D3D12_FEATURE_DATA_D3D12_OPTIONS8 options8;
    D3D12_FEATURE_DATA_D3D12_OPTIONS9 options9;
    D3D12_FEATURE_DATA_D3D12_OPTIONS10 options10;
    D3D12_FEATURE_DATA_D3D12_OPTIONS11 options11;
    D3D12_FEATURE_DATA_D3D12_OPTIONS12 options12;
    D3D12_FEATURE_DATA_D3D12_OPTIONS13 options13;
    D3D12_FEATURE_DATA_D3D12_OPTIONS14 options14;
    D3D12_FEATURE_DATA_D3D12_OPTIONS15 options15;
    D3D12_FEATURE_DATA_D3D12_OPTIONS16 options16;
    D3D12_FEATURE_DATA_D3D12_OPTIONS17 options17;
    D3D12_FEATURE_DATA_D3D12_OPTIONS18 options18;
    D3D12_FEATURE_DATA_D3D12_OPTIONS19 options19;
    D3D12_FEATURE_DATA_D3D12_OPTIONS20 options20;
    D3D12_FEATURE_DATA_D3D12_OPTIONS21 options21;

    D3D_FEATURE_LEVEL max_feature_level;
    D3D_SHADER_MODEL max_shader_model;
};

enum vkd3d_queue_family
{
    VKD3D_QUEUE_FAMILY_GRAPHICS,
    VKD3D_QUEUE_FAMILY_COMPUTE,
    VKD3D_QUEUE_FAMILY_TRANSFER,
    VKD3D_QUEUE_FAMILY_OPTICAL_FLOW,
    /* Keep internal queues at the end */
    VKD3D_QUEUE_FAMILY_INTERNAL_COMPUTE,
    /* Keep sparse after all other queues since it does not append concurrent sharing list.
     * It's considered an internal queue type since we only use it as a fallback or for init binds. */
    VKD3D_QUEUE_FAMILY_SPARSE_BINDING,

    VKD3D_QUEUE_FAMILY_COUNT
};

struct vkd3d_queue_family_info
{
    struct vkd3d_queue *out_of_band_queue;
    struct vkd3d_queue **queues;
    uint32_t queue_count;
    uint32_t vk_family_index;
    uint32_t timestamp_bits;
    VkQueueFlags vk_queue_flags;
};

#define VKD3D_CACHED_COMMAND_ALLOCATOR_COUNT 8
struct vkd3d_cached_command_allocator
{
    VkCommandPool vk_command_pool;
    uint32_t vk_family_index;
};

struct vkd3d_device_swapchain_info
{
    struct dxgi_vk_swap_chain *low_latency_swapchain;
    uint32_t swapchain_count;
    bool mode;
    bool boost;
    uint32_t minimum_us;
};

#define VKD3D_LOW_LATENCY_FRAME_ID_STRIDE 10000
struct vkd3d_device_frame_markers
{
    uint64_t simulation;
    uint64_t render;
    uint64_t present;
    uint64_t consumed_present_id;
};

/* ID3D12Device */
typedef ID3D12Device12 d3d12_device_iface;

struct vkd3d_descriptor_qa_global_info;
struct vkd3d_descriptor_qa_heap_buffer_data;

/* ID3D12DeviceExt */
typedef ID3D12DeviceExt1 d3d12_device_vkd3d_ext_iface;

/* ID3D12DXVKInteropDevice */
typedef ID3D12DXVKInteropDevice1 d3d12_dxvk_interop_device_iface;

/* ID3DLowLatencyDevice */
typedef ID3DLowLatencyDevice d3d_low_latency_device_iface;

struct d3d12_device_scratch_pool
{
    struct vkd3d_scratch_buffer scratch_buffers[VKD3D_MAX_SCRATCH_BUFFER_COUNT];
    size_t scratch_buffer_count;
    size_t scratch_buffer_size;
    VkDeviceSize block_size;
    unsigned int high_water_mark;
};

enum vkd3d_queue_timeline_trace_state_type
{
    VKD3D_QUEUE_TIMELINE_TRACE_STATE_TYPE_NONE = 0,
    VKD3D_QUEUE_TIMELINE_TRACE_STATE_TYPE_EVENT,
    VKD3D_QUEUE_TIMELINE_TRACE_STATE_TYPE_SUBMISSION,
    VKD3D_QUEUE_TIMELINE_TRACE_STATE_TYPE_WAIT,
    VKD3D_QUEUE_TIMELINE_TRACE_STATE_TYPE_SIGNAL,

    /* Blit task */
    VKD3D_QUEUE_TIMELINE_TRACE_STATE_TYPE_PRESENT_BLIT,

    /* Waiting for present wait to complete. */
    VKD3D_QUEUE_TIMELINE_TRACE_STATE_TYPE_PRESENT_WAIT,

    /* Generic region markers. */
    VKD3D_QUEUE_TIMELINE_TRACE_STATE_TYPE_GENERIC_REGION,

    /* Time spent blocking in ::Present() in user thread. */
    VKD3D_QUEUE_TIMELINE_TRACE_STATE_TYPE_PRESENT_BLOCK,

    /* Time spent blocking in LowLatencySleep in user thread. */
    VKD3D_QUEUE_TIMELINE_TRACE_STATE_TYPE_LOW_LATENCY_SLEEP,

    /* PSO compilation */
    VKD3D_QUEUE_TIMELINE_TRACE_STATE_TYPE_PSO_COMPILATION,

    /* Reset() and Close() are useful instant events to see when command recording is happening and
     * which threads do so. */
    VKD3D_QUEUE_TIMELINE_TRACE_STATE_TYPE_COMMAND_LIST,

    /* Misc instantaneous events that are expected to be heavy. */
    VKD3D_QUEUE_TIMELINE_TRACE_STATE_TYPE_QUEUE_PRESENT,
    VKD3D_QUEUE_TIMELINE_TRACE_STATE_TYPE_COMMITTED_RESOURCE_ALLOCATION,
    VKD3D_QUEUE_TIMELINE_TRACE_STATE_TYPE_HEAP_ALLOCATION,
    VKD3D_QUEUE_TIMELINE_TRACE_STATE_TYPE_VK_ALLOCATE_MEMORY,
    VKD3D_QUEUE_TIMELINE_TRACE_STATE_TYPE_CLEAR_ALLOCATION,
    VKD3D_QUEUE_TIMELINE_TRACE_STATE_TYPE_COMMAND_ALLOCATOR_RESET,
};

struct vkd3d_queue_timeline_trace_state
{
    enum vkd3d_queue_timeline_trace_state_type type;
    unsigned int tid;
    uint64_t start_ts;
    uint64_t start_submit_ts;
    uint64_t record_end_ts;
    uint64_t record_cookie;
    uint32_t overhead_start_offset;
    uint32_t overhead_end_offset;
    char desc[128 - 6 * sizeof(uint64_t)];
};

struct vkd3d_queue_timeline_trace
{
    pthread_mutex_t lock;
    pthread_mutex_t ready_lock;
    FILE *file;
    bool active;

    unsigned int *vacant_indices;
    size_t vacant_indices_count;
    size_t vacant_indices_size;

    unsigned int *ready_command_lists;
    size_t ready_command_lists_count;
    size_t ready_command_lists_size;

    struct vkd3d_queue_timeline_trace_state *state;
    uint64_t base_ts;
    uint64_t submit_count;
};

static inline bool vkd3d_queue_timeline_trace_cookie_is_valid(struct vkd3d_queue_timeline_trace_cookie cookie)
{
    return cookie.index != 0;
}

HRESULT vkd3d_queue_timeline_trace_init(struct vkd3d_queue_timeline_trace *trace,
        struct d3d12_device *device);
void vkd3d_queue_timeline_trace_cleanup(struct vkd3d_queue_timeline_trace *trace);
struct vkd3d_queue_timeline_trace_cookie
vkd3d_queue_timeline_trace_register_event_signal(struct vkd3d_queue_timeline_trace *trace,
        vkd3d_native_sync_handle handle, d3d12_fence_iface *fence, uint64_t value);
struct vkd3d_queue_timeline_trace_cookie
vkd3d_queue_timeline_trace_register_signal(struct vkd3d_queue_timeline_trace *trace,
        d3d12_fence_iface *fence, uint64_t value);
struct vkd3d_queue_timeline_trace_cookie
vkd3d_queue_timeline_trace_register_wait(struct vkd3d_queue_timeline_trace *trace,
        d3d12_fence_iface *fence, uint64_t value);
struct vkd3d_queue_timeline_trace_cookie
vkd3d_queue_timeline_trace_register_swapchain_blit(struct vkd3d_queue_timeline_trace *trace,
        uint64_t present_id);
struct vkd3d_queue_timeline_trace_cookie
vkd3d_queue_timeline_trace_register_present_wait(struct vkd3d_queue_timeline_trace *trace,
        uint64_t present_id);
struct vkd3d_queue_timeline_trace_cookie
vkd3d_queue_timeline_trace_register_present_block(struct vkd3d_queue_timeline_trace *trace,
        uint64_t present_id);
struct vkd3d_queue_timeline_trace_cookie
vkd3d_queue_timeline_trace_register_low_latency_sleep(struct vkd3d_queue_timeline_trace *trace,
        uint64_t present_id);
struct vkd3d_queue_timeline_trace_cookie
vkd3d_queue_timeline_trace_register_pso_compile(struct vkd3d_queue_timeline_trace *trace);
struct vkd3d_queue_timeline_trace_cookie
vkd3d_queue_timeline_trace_register_sparse(struct vkd3d_queue_timeline_trace *trace, uint32_t num_tiles);
struct vkd3d_queue_timeline_trace_cookie
vkd3d_queue_timeline_trace_register_execute(struct vkd3d_queue_timeline_trace *trace,
        ID3D12CommandList * const *command_lists, unsigned int count);
struct vkd3d_queue_timeline_trace_cookie
vkd3d_queue_timeline_trace_register_command_list(struct vkd3d_queue_timeline_trace *trace);

void vkd3d_queue_timeline_trace_register_instantaneous(struct vkd3d_queue_timeline_trace *trace,
        enum vkd3d_queue_timeline_trace_state_type type, uint64_t value);

struct vkd3d_queue_timeline_trace_cookie
vkd3d_queue_timeline_trace_register_generic_region(struct vkd3d_queue_timeline_trace *trace, const char *tag);

void vkd3d_queue_timeline_trace_complete_event_signal(struct vkd3d_queue_timeline_trace *trace,
        struct vkd3d_fence_worker *worker,
        struct vkd3d_queue_timeline_trace_cookie cookie);
void vkd3d_queue_timeline_trace_complete_execute(struct vkd3d_queue_timeline_trace *trace,
        struct vkd3d_fence_worker *worker,
        struct vkd3d_queue_timeline_trace_cookie cookie);
void vkd3d_queue_timeline_trace_complete_present_wait(struct vkd3d_queue_timeline_trace *trace,
        struct vkd3d_queue_timeline_trace_cookie cookie);
void vkd3d_queue_timeline_trace_complete_present_block(struct vkd3d_queue_timeline_trace *trace,
        struct vkd3d_queue_timeline_trace_cookie cookie);
void vkd3d_queue_timeline_trace_complete_low_latency_sleep(struct vkd3d_queue_timeline_trace *trace,
        struct vkd3d_queue_timeline_trace_cookie cookie);
void vkd3d_queue_timeline_trace_close_command_list(struct vkd3d_queue_timeline_trace *trace,
        struct vkd3d_queue_timeline_trace_cookie cookie);
void vkd3d_queue_timeline_trace_begin_execute(struct vkd3d_queue_timeline_trace *trace,
        struct vkd3d_queue_timeline_trace_cookie cookie);
void vkd3d_queue_timeline_trace_begin_execute_overhead(struct vkd3d_queue_timeline_trace *trace,
        struct vkd3d_queue_timeline_trace_cookie cookie);
void vkd3d_queue_timeline_trace_end_execute_overhead(struct vkd3d_queue_timeline_trace *trace,
        struct vkd3d_queue_timeline_trace_cookie cookie);
void vkd3d_queue_timeline_trace_complete_pso_compile(struct vkd3d_queue_timeline_trace *trace,
        struct vkd3d_queue_timeline_trace_cookie cookie, uint64_t pso_hash, const char *completion_kind);

struct vkd3d_address_binding_report_buffer_info
{
    const char *tag;
};

struct vkd3d_address_binding_report_image_info
{
    VkFormat format;
    VkExtent3D extent;
    VkImageType type;
    VkImageUsageFlags usage;
    uint32_t levels;
    uint32_t layers;
};

struct vkd3d_address_binding_report_memory_info
{
    uint32_t memory_type_index;
};

union vkd3d_address_binding_report_resource_info
{
    struct vkd3d_address_binding_report_buffer_info buffer;
    struct vkd3d_address_binding_report_image_info image;
    struct vkd3d_address_binding_report_memory_info memory;
};

struct vkd3d_address_binding_report
{
    uint64_t handle;
    VkDeviceAddress addr;
    VkDeviceSize size;
    VkObjectType type;
    VkDeviceAddressBindingFlagsEXT flags;
    VkDeviceAddressBindingTypeEXT binding_type;
    bool sparse;
    uint64_t timestamp_ns;

    union vkd3d_address_binding_report_resource_info info;
    uint64_t cookie;
};

struct vkd3d_address_binding_mapping
{
    VkObjectType type;
    uint64_t handle;

    union vkd3d_address_binding_report_resource_info info;
    uint64_t cookie;
};

struct vkd3d_address_binding_tracker
{
    VkDebugUtilsMessengerEXT messenger;
    pthread_mutex_t lock;

    struct vkd3d_address_binding_report *reports;
    size_t reports_size;
    size_t reports_count;

    size_t *recent_memory_indices;
    size_t recent_memory_indices_size;
    size_t recent_memory_indices_count;

    struct vkd3d_address_binding_mapping *mappings;
    size_t mappings_size;
    size_t mappings_count;
};

HRESULT vkd3d_address_binding_tracker_init(struct vkd3d_address_binding_tracker *tracker, struct d3d12_device *device);
void vkd3d_address_binding_tracker_mark_user_thread();
void vkd3d_address_binding_tracker_cleanup(struct vkd3d_address_binding_tracker *tracker, struct d3d12_device *device);
void vkd3d_address_binding_tracker_assign_info(struct vkd3d_address_binding_tracker *tracker,
        VkObjectType type, uint64_t handle, const union vkd3d_address_binding_report_resource_info *info);
void vkd3d_address_binding_tracker_assign_cookie(struct vkd3d_address_binding_tracker *tracker,
        VkObjectType type, uint64_t handle, uint64_t cookie);
void vkd3d_address_binding_tracker_check_va(struct vkd3d_address_binding_tracker *tracker,
        VkDeviceAddress address);

static inline bool vkd3d_address_binding_tracker_active(struct vkd3d_address_binding_tracker *tracker)
{
    return tracker->messenger != VK_NULL_HANDLE;
}

struct d3d12_device
{
    d3d12_device_iface ID3D12Device_iface;
    d3d12_device_vkd3d_ext_iface ID3D12DeviceExt_iface;
    d3d12_dxvk_interop_device_iface ID3D12DXVKInteropDevice_iface;
    d3d_low_latency_device_iface ID3DLowLatencyDevice_iface;
    LONG refcount;

    VkDevice vk_device;
    uint32_t api_version;
    VkPhysicalDevice vk_physical_device;
    struct vkd3d_vk_device_procs vk_procs;

    pthread_mutex_t mutex;
    pthread_mutex_t global_submission_mutex;
    spinlock_t low_latency_swapchain_spinlock;

    VkPhysicalDeviceMemoryProperties memory_properties;

    struct vkd3d_vulkan_info vk_info;
    struct vkd3d_physical_device_info device_info;

    struct vkd3d_queue_family_info *queue_families[VKD3D_QUEUE_FAMILY_COUNT];
    uint32_t concurrent_queue_family_indices[VKD3D_QUEUE_FAMILY_COUNT];
    uint32_t concurrent_queue_family_count;
    uint32_t unique_queue_mask;

    struct vkd3d_instance *vkd3d_instance;

    IUnknown *parent;
    LUID adapter_luid;

    struct vkd3d_private_store private_store;
    struct d3d_destruction_notifier destruction_notifier;
    struct d3d12_caps d3d12_caps;

    struct vkd3d_memory_transfer_queue memory_transfers;
    struct vkd3d_memory_allocator memory_allocator;

    struct vkd3d_queue *internal_sparse_queue;
    VkSemaphore sparse_init_timeline;
    uint64_t sparse_init_timeline_value;

    struct d3d12_device_scratch_pool scratch_pools[VKD3D_SCRATCH_POOL_KIND_COUNT];

    struct vkd3d_query_pool query_pools[VKD3D_VIRTUAL_QUERY_POOL_COUNT];
    size_t query_pool_count;

    struct vkd3d_cached_command_allocator cached_command_allocators[VKD3D_CACHED_COMMAND_ALLOCATOR_COUNT];
    size_t cached_command_allocator_count;

    uint32_t *descriptor_heap_gpu_vas;
    size_t descriptor_heap_gpu_va_count;
    size_t descriptor_heap_gpu_va_size;
    uint32_t descriptor_heap_gpu_next;

    HRESULT removed_reason;

    const struct vkd3d_format *formats;
    const struct vkd3d_format *depth_stencil_formats;
    unsigned int format_compatibility_list_count;
    const struct vkd3d_format_compatibility_list *format_compatibility_lists;
    struct vkd3d_bindless_state bindless_state;
    struct vkd3d_queue_timeline_trace queue_timeline_trace;
    struct vkd3d_memory_info memory_info;
    struct vkd3d_meta_ops meta_ops;
    struct vkd3d_view_map sampler_map;
    struct vkd3d_sampler_state sampler_state;
    struct vkd3d_shader_debug_ring debug_ring;
    struct vkd3d_pipeline_library_disk_cache disk_cache;
    struct vkd3d_global_descriptor_buffer global_descriptor_buffer;
    struct vkd3d_address_binding_tracker address_binding_tracker;
    rwlock_t vertex_input_lock;
    struct hash_map vertex_input_pipelines;
    rwlock_t fragment_output_lock;
    struct hash_map fragment_output_pipelines;
#ifdef VKD3D_ENABLE_BREADCRUMBS
    struct vkd3d_breadcrumb_tracer breadcrumb_tracer;
#endif
#ifdef VKD3D_ENABLE_DESCRIPTOR_QA
    struct vkd3d_descriptor_qa_global_info *descriptor_qa_global_info;
#endif
    uint64_t shader_interface_key;
    uint32_t device_has_dgc_templates;

    struct vkd3d_device_swapchain_info swapchain_info;
    struct vkd3d_device_frame_markers frame_markers;

    struct
    {
        bool amdgpu_broken_clearvram;
        bool amdgpu_broken_null_tile_mapping;
        bool tiler_renderpass_barriers;
    } workarounds;
};

HRESULT d3d12_device_create(struct vkd3d_instance *instance,
        const struct vkd3d_device_create_info *create_info, struct d3d12_device **device);
struct vkd3d_queue_family_info *d3d12_device_get_vkd3d_queue_family(struct d3d12_device *device,
        D3D12_COMMAND_LIST_TYPE type,
        uint32_t vk_family_index);
struct vkd3d_queue *d3d12_device_allocate_vkd3d_queue(struct vkd3d_queue_family_info *queue_family,
        struct d3d12_command_queue *command_queue);
void d3d12_device_unmap_vkd3d_queue(struct vkd3d_queue *queue, struct d3d12_command_queue *command_queue);
bool d3d12_device_is_uma(struct d3d12_device *device, bool *coherent);
void d3d12_device_mark_as_removed(struct d3d12_device *device, HRESULT reason,
        const char *message, ...) VKD3D_PRINTF_FUNC(3, 4);
void d3d12_device_report_fault(struct d3d12_device *device);

VkPipeline d3d12_device_get_or_create_vertex_input_pipeline(struct d3d12_device *device,
        const struct vkd3d_vertex_input_pipeline_desc *desc);
VkPipeline d3d12_device_get_or_create_fragment_output_pipeline(struct d3d12_device *device,
        const struct vkd3d_fragment_output_pipeline_desc *desc);

uint32_t d3d12_device_get_max_descriptor_heap_size(struct d3d12_device *device, D3D12_DESCRIPTOR_HEAP_TYPE heap_type);

static inline struct d3d12_device *unsafe_impl_from_ID3D12Device(d3d12_device_iface *iface)
{
    return CONTAINING_RECORD(iface, struct d3d12_device, ID3D12Device_iface);
}

static inline struct d3d12_device *impl_from_ID3D12Device(d3d12_device_iface *iface)
{
    if (!iface)
        return NULL;

    /* Not practical to check the vtables every call when we have so many variants. */

    return CONTAINING_RECORD(iface, struct d3d12_device, ID3D12Device_iface);
}

bool d3d12_device_validate_shader_meta(struct d3d12_device *device, const struct vkd3d_shader_meta *meta);

HRESULT d3d12_device_get_scratch_buffer(struct d3d12_device *device, enum vkd3d_scratch_pool_kind kind,
        VkDeviceSize min_size, uint32_t memory_types, struct vkd3d_scratch_buffer *scratch);
void d3d12_device_return_scratch_buffer(struct d3d12_device *device, enum vkd3d_scratch_pool_kind kind,
        const struct vkd3d_scratch_buffer *scratch);

HRESULT d3d12_device_get_query_pool(struct d3d12_device *device, uint32_t type_index, struct vkd3d_query_pool *pool);
void d3d12_device_return_query_pool(struct d3d12_device *device, const struct vkd3d_query_pool *pool);

uint64_t d3d12_device_get_descriptor_heap_gpu_va(struct d3d12_device *device, D3D12_DESCRIPTOR_HEAP_TYPE type);
void d3d12_device_return_descriptor_heap_gpu_va(struct d3d12_device *device, uint64_t va);

static inline bool d3d12_device_uses_descriptor_buffers(const struct d3d12_device *device)
{
    return device->global_descriptor_buffer.resource.va != 0;
}

static inline bool is_cpu_accessible_heap(const D3D12_HEAP_PROPERTIES *properties)
{
    if (properties->Type == D3D12_HEAP_TYPE_DEFAULT)
        return false;
    if (properties->Type == D3D12_HEAP_TYPE_CUSTOM)
    {
        return properties->CPUPageProperty == D3D12_CPU_PAGE_PROPERTY_WRITE_COMBINE
                || properties->CPUPageProperty == D3D12_CPU_PAGE_PROPERTY_WRITE_BACK;
    }
    return true;
}

static inline bool is_cpu_accessible_system_memory_heap(const D3D12_HEAP_PROPERTIES *properties)
{
    if (properties->Type == D3D12_HEAP_TYPE_DEFAULT)
        return false;
    if (properties->Type == D3D12_HEAP_TYPE_GPU_UPLOAD)
        return false;
    if (properties->Type == D3D12_HEAP_TYPE_CUSTOM)
    {
        return (properties->CPUPageProperty == D3D12_CPU_PAGE_PROPERTY_WRITE_COMBINE
                || properties->CPUPageProperty == D3D12_CPU_PAGE_PROPERTY_WRITE_BACK)
                && properties->MemoryPoolPreference == D3D12_MEMORY_POOL_L0;
    }
    return true;
}

static inline uint32_t vkd3d_bindless_state_find_set_info_index_fast(struct d3d12_device *device,
        enum vkd3d_bindless_state_info_indices split_type, uint32_t fallback_lookup_types)
{
    if (device->bindless_state.flags & VKD3D_BINDLESS_MUTABLE_TYPE_SPLIT_RAW_TYPED)
        return split_type;
    else if (device->bindless_state.flags & VKD3D_BINDLESS_MUTABLE_TYPE)
        return VKD3D_BINDLESS_STATE_INFO_INDEX_MUTABLE_SINGLE;
    else
        return vkd3d_bindless_state_find_set_info_index(&device->bindless_state, fallback_lookup_types);
}

static inline const struct vkd3d_memory_info_domain *d3d12_device_get_memory_info_domain(
        struct d3d12_device *device,
        const D3D12_HEAP_PROPERTIES *heap_properties)
{
    /* Host visible and non-host visible memory types do not necessarily
     * overlap. Need to select memory types appropriately. */
    if (is_cpu_accessible_heap(heap_properties))
        return &device->memory_info.cpu_accessible_domain;
    else
        return &device->memory_info.non_cpu_accessible_domain;
}

static inline HRESULT d3d12_device_query_interface(struct d3d12_device *device, REFIID iid, void **object)
{
    return ID3D12Device12_QueryInterface(&device->ID3D12Device_iface, iid, object);
}

ULONG d3d12_device_add_ref_common(struct d3d12_device *device);
ULONG d3d12_device_release_common(struct d3d12_device *device);

static inline ULONG d3d12_device_add_ref(struct d3d12_device *device)
{
    ULONG refcount = d3d12_device_add_ref_common(device);
    TRACE("Increasing refcount to %u.\n", refcount);
    return refcount;
}

static inline ULONG d3d12_device_release(struct d3d12_device *device)
{
    ULONG refcount = d3d12_device_release_common(device);
    TRACE("Decreasing refcount to %u.\n", refcount);
    return refcount;
}

static inline bool d3d12_device_use_embedded_mutable_descriptors(struct d3d12_device *device)
{
    return (device->bindless_state.flags & VKD3D_BINDLESS_MUTABLE_EMBEDDED) != 0;
}

struct d3d12_desc_split_metadata
{
    struct vkd3d_descriptor_metadata_view *view;
    struct vkd3d_descriptor_metadata_types *types;
};

static inline struct d3d12_desc_split_metadata d3d12_desc_decode_metadata(
        struct d3d12_device *device, vkd3d_cpu_descriptor_va_t va)
{
    struct d3d12_desc_split_metadata meta;

    if (d3d12_device_use_embedded_mutable_descriptors(device))
    {
        /* If the descriptor is large enough we can just inline the metadata side by side with the actual descriptor.
         * If the descriptor is smaller, we can use the planar method where we encode log2 offset. */
        if (device->bindless_state.flags & VKD3D_BINDLESS_MUTABLE_EMBEDDED_PACKED_METADATA)
        {
            struct vkd3d_descriptor_metadata_view *m;
            va &= ~VKD3D_RESOURCE_EMBEDDED_CACHED_MASK;
            m = (void *)(uintptr_t)(va + device->bindless_state.descriptor_buffer_packed_metadata_offset);
            meta.view = m;
            meta.types = NULL;
        }
        else
        {
            struct d3d12_desc_split_embedded d = d3d12_desc_decode_embedded_resource_va(va);
            if (d.metadata)
            {
                meta.view = d.metadata;
                meta.types = NULL;
            }
            else
            {
                meta.view = NULL;
                meta.types = NULL;
            }
        }
    }
    else
    {
        struct d3d12_desc_split d = d3d12_desc_decode_va(va);
        meta.view = d.view;
        meta.types = d.types;
    }

    return meta;
}

static inline unsigned int d3d12_device_get_descriptor_handle_increment_size(
        struct d3d12_device *device,
        D3D12_DESCRIPTOR_HEAP_TYPE descriptor_heap_type)
{
    switch (descriptor_heap_type)
    {
        case D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV:
            return d3d12_device_use_embedded_mutable_descriptors(device) ?
                    device->bindless_state.descriptor_buffer_cbv_srv_uav_size : VKD3D_RESOURCE_DESC_INCREMENT;
        case D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER:
            return d3d12_device_use_embedded_mutable_descriptors(device) ?
                    device->bindless_state.descriptor_buffer_sampler_size : VKD3D_RESOURCE_DESC_INCREMENT;

        case D3D12_DESCRIPTOR_HEAP_TYPE_RTV:
        case D3D12_DESCRIPTOR_HEAP_TYPE_DSV:
            return sizeof(struct d3d12_rtv_desc);

        default:
            FIXME("Unhandled type %#x.\n", descriptor_heap_type);
            return 0;
    }
}

uint32_t vkd3d_bindless_get_mutable_descriptor_type_size(struct d3d12_device *device);
bool vkd3d_bindless_supports_embedded_mutable_type(struct d3d12_device *device, uint32_t flags);

static inline uint32_t vkd3d_bindless_embedded_mutable_raw_buffer_offset(struct d3d12_device *device)
{
    const VkPhysicalDeviceDescriptorBufferPropertiesEXT *props = &device->device_info.descriptor_buffer_properties;
    uint32_t texel_buffer_size, raw_buffer_descriptor_offset;

    texel_buffer_size = max(props->robustUniformTexelBufferDescriptorSize, props->robustStorageTexelBufferDescriptorSize);
    if (props->sampledImageDescriptorSize > props->storageImageDescriptorSize)
    {
        /* Somewhat RADV specific. If sampled image size is larger than storage image, we can sneak in
         * descriptors in the upper half. Try to take advantage of this. */
        texel_buffer_size = max(texel_buffer_size, props->storageImageDescriptorSize);
    }
    raw_buffer_descriptor_offset = align(texel_buffer_size, props->descriptorBufferOffsetAlignment);
    return raw_buffer_descriptor_offset;
}

static inline bool d3d12_device_use_ssbo_raw_buffer(struct d3d12_device *device)
{
    return (device->bindless_state.flags & VKD3D_BINDLESS_RAW_SSBO) != 0;
}

static inline VkDeviceSize d3d12_device_get_ssbo_alignment(struct d3d12_device *device)
{
    return device->device_info.properties2.properties.limits.minStorageBufferOffsetAlignment;
}

static inline bool d3d12_device_use_ssbo_root_descriptors(struct d3d12_device *device)
{
    /* We only know the VA of root SRV/UAVs, so we cannot
     * make any better assumptions about the alignment */
    return d3d12_device_use_ssbo_raw_buffer(device) &&
            d3d12_device_get_ssbo_alignment(device) <= 4;
}

bool d3d12_device_supports_variable_shading_rate_tier_1(struct d3d12_device *device);
bool d3d12_device_supports_variable_shading_rate_tier_2(struct d3d12_device *device);
bool d3d12_device_supports_ray_tracing_tier_1_0(const struct d3d12_device *device);
UINT d3d12_determine_shading_rate_image_tile_size(struct d3d12_device *device);
bool d3d12_device_supports_required_subgroup_size_for_stage(
        struct d3d12_device *device, VkShaderStageFlagBits stage);

static inline void d3d12_device_register_swapchain(struct d3d12_device *device, struct dxgi_vk_swap_chain *chain)
{
    spinlock_acquire(&device->low_latency_swapchain_spinlock);

    if (!device->swapchain_info.low_latency_swapchain && device->swapchain_info.swapchain_count == 0)
    {
        dxgi_vk_swap_chain_incref(chain);
        device->swapchain_info.low_latency_swapchain = chain;
        dxgi_vk_swap_chain_set_latency_sleep_mode(chain, device->swapchain_info.mode,
                device->swapchain_info.boost, device->swapchain_info.minimum_us);
    }
    else
    {
        if (device->swapchain_info.low_latency_swapchain)
            dxgi_vk_swap_chain_decref(device->swapchain_info.low_latency_swapchain);
        device->swapchain_info.low_latency_swapchain = NULL;
    }

    device->swapchain_info.swapchain_count++;

    spinlock_release(&device->low_latency_swapchain_spinlock);
}

static inline void d3d12_device_remove_swapchain(struct d3d12_device *device, struct dxgi_vk_swap_chain *chain)
{
    spinlock_acquire(&device->low_latency_swapchain_spinlock);

    if (device->swapchain_info.low_latency_swapchain == chain)
    {
        dxgi_vk_swap_chain_decref(chain);
        device->swapchain_info.low_latency_swapchain = NULL;
    }

    device->swapchain_info.swapchain_count--;

    spinlock_release(&device->low_latency_swapchain_spinlock);
}

/* ID3DBlob */
struct d3d_blob
{
    ID3D10Blob ID3DBlob_iface;
    LONG refcount;

    void *buffer;
    SIZE_T size;
};

HRESULT d3d_blob_create(void *buffer, SIZE_T size, struct d3d_blob **blob);

/* ID3D12StateObject */
typedef ID3D12StateObject d3d12_state_object_iface;
typedef ID3D12StateObjectProperties1 d3d12_state_object_properties_iface;

struct d3d12_rt_state_object_identifier
{
    WCHAR *mangled_export;
    WCHAR *plain_export;
    /* Must be a persistent pointer as long as the StateObject object is live. */
    uint8_t identifier[D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES];

    /* The index into pStages[]. */
    uint32_t general_stage_index;
    uint32_t closest_stage_index;
    uint32_t anyhit_stage_index;
    uint32_t intersection_stage_index;
    VkShaderStageFlagBits general_stage;

    VkDeviceSize stack_size_general;
    VkDeviceSize stack_size_closest;
    VkDeviceSize stack_size_any;
    VkDeviceSize stack_size_intersection;

    /* Index into object->pipelines[]. */
    uint32_t pipeline_variant_index;
    /* The index into vkGetShaderStackSize and friends for pGroups[].
     * Unique per d3d12_state_object_variant */
    uint32_t per_variant_group_index;

    /* For AddToStateObject(). We need to return the identifier pointer
     * for the parent, not the child. This makes it easy to validate that
     * we observe the same SBT handles as specified by DXR 1.1. */

    /* If -1, ignore, otherwise, redirect. */
    int inherited_collection_index;
    uint32_t inherited_collection_export_index;
};

struct d3d12_rt_state_object_stack_info
{
    uint32_t max_callable;
    uint32_t max_anyhit;
    uint32_t max_miss;
    uint32_t max_raygen;
    uint32_t max_intersect;
    uint32_t max_closest;
};

#ifdef VKD3D_ENABLE_BREADCRUMBS
struct d3d12_rt_state_object_breadcrumb_shader
{
    vkd3d_shader_hash_t hash;
    VkShaderStageFlagBits stage;
    char name[64];
};
#endif

struct d3d12_rt_state_object_variant
{
    /* Can be bound. */
    VkPipeline pipeline;
    /* Can be used as a library. */
    VkPipeline pipeline_library;
    /* The global root signature associated with this variant. */
    struct d3d12_root_signature *global_root_signature;

    /* For offseting pStages and pGroups for COLLECTIONS. */
    uint32_t stages_count;
    uint32_t groups_count;

    struct
    {
        VkDescriptorSetLayout set_layout;
        VkPipelineLayout pipeline_layout;
        VkDescriptorSet desc_set;
        VkDescriptorPool desc_pool;
        uint32_t set_index;
        uint64_t compatibility_hash;
        bool owned_handles;
    } local_static_sampler;
};

struct d3d12_rt_state_object_pipeline_data;

struct d3d12_rt_state_object
{
    d3d12_state_object_iface ID3D12StateObject_iface;
    d3d12_state_object_properties_iface ID3D12StateObjectProperties1_iface;
    LONG refcount;
    LONG internal_refcount;
    D3D12_STATE_OBJECT_TYPE type;
    D3D12_STATE_OBJECT_FLAGS flags;
    struct d3d12_device *device;

    /* Could potentially be a hashmap. */
    struct d3d12_rt_state_object_identifier *exports;
    size_t exports_size;
    size_t exports_count;

    struct vkd3d_shader_library_entry_point *entry_points;
    size_t entry_points_count;

    struct d3d12_rt_state_object_variant *pipelines;
    size_t pipelines_size;
    size_t pipelines_count;

    /* Can be inherited by AddToStateObject(). */
    D3D12_RAYTRACING_PIPELINE_CONFIG1 pipeline_config;
    D3D12_RAYTRACING_SHADER_CONFIG shader_config;

    UINT64 pipeline_stack_size;
    struct d3d12_rt_state_object_stack_info stack;

    struct d3d12_rt_state_object **collections;
    size_t collections_count;

    struct d3d12_rt_state_object_pipeline_data *deferred_data;

#ifdef VKD3D_ENABLE_BREADCRUMBS
    /* For breadcrumbs. */
    struct d3d12_rt_state_object_breadcrumb_shader *breadcrumb_shaders;
    size_t breadcrumb_shaders_size;
    size_t breadcrumb_shaders_count;
#endif

    struct vkd3d_private_store private_store;
    struct d3d_destruction_notifier destruction_notifier;
};

struct d3d12_state_object_association
{
    enum vkd3d_shader_subobject_kind kind;
    unsigned int priority; /* Different priorities can tie-break. */
    union
    {
        struct d3d12_root_signature *root_signature;
        D3D12_STATE_OBJECT_CONFIG object_config;
        D3D12_RAYTRACING_PIPELINE_CONFIG1 pipeline_config;
        D3D12_RAYTRACING_SHADER_CONFIG shader_config;
    };
    const WCHAR *export;
};

#define VKD3D_ASSOCIATION_PRIORITY_INHERITED_COLLECTION 0
#define VKD3D_ASSOCIATION_PRIORITY_DXIL_SUBOBJECT 1
#define VKD3D_ASSOCIATION_PRIORITY_DXIL_SUBOBJECT_ASSIGNMENT_DEFAULT 2
#define VKD3D_ASSOCIATION_PRIORITY_DXIL_SUBOBJECT_ASSIGNMENT_EXPLICIT 3
#define VKD3D_ASSOCIATION_PRIORITY_DECLARED_STATE_OBJECT 4
#define VKD3D_ASSOCIATION_PRIORITY_EXPLICIT_DEFAULT 5
#define VKD3D_ASSOCIATION_PRIORITY_EXPLICIT 6

const struct d3d12_state_object_association *d3d12_state_object_find_association(
        enum vkd3d_shader_subobject_kind kind,
        const struct d3d12_state_object_association *associations,
        size_t associations_count,
        const struct D3D12_HIT_GROUP_DESC **hit_groups,
        size_t hit_groups_count,
        const struct vkd3d_shader_library_entry_point *entry,
        LPCWSTR export);

bool d3d12_state_object_association_data_equal(
        const struct d3d12_state_object_association *a,
        const struct d3d12_state_object_association *b);

bool vkd3d_export_equal(LPCWSTR export, const struct vkd3d_shader_library_entry_point *entry);

HRESULT d3d12_rt_state_object_create(struct d3d12_device *device, const D3D12_STATE_OBJECT_DESC *desc,
        struct d3d12_rt_state_object *parent,
        struct d3d12_rt_state_object **object);
HRESULT d3d12_rt_state_object_add(struct d3d12_device *device, const D3D12_STATE_OBJECT_DESC *desc,
        struct d3d12_rt_state_object *parent,
        struct d3d12_rt_state_object **object);

static inline struct d3d12_rt_state_object *rt_impl_from_ID3D12StateObject(ID3D12StateObject *iface)
{
    return CONTAINING_RECORD(iface, struct d3d12_rt_state_object, ID3D12StateObject_iface);
}

/* ID3D12MetaCommand */
struct d3d12_meta_command;

extern const GUID IID_META_COMMAND_DSTORAGE;

/* These are not documented in any way, semantics and types
 * were guessed based on native DirectStorage behaviour. */
struct d3d12_meta_command_dstorage_query_in_args
{
    uint16_t unknown0;  /* always 1 */
    uint16_t stream_count;
    uint32_t unknown1;
    uint64_t unknown2;
};

struct d3d12_meta_command_dstorage_query_out_args
{
    uint16_t unknown0;  /* always 1 */
    uint16_t max_stream_count;
    uint32_t unknown1;
    uint64_t scratch_size;
    uint64_t unknown2;
};

/* Upper limit on the number of tiles we can decompress in
 * one call to the meta command. Corresponds to up to 4GB
 * of decompressed data. */
#define VKD3D_DSTORAGE_MAX_TILE_COUNT (0x10000u)

struct d3d12_meta_command_dstorage_scratch_header
{
    /* Workgroup count for fallback shader */
    VkDispatchIndirectCommand region_count;
    /* Padding to ensure 8-byte alignment of region array */
    uint32_t padding;
    /* Region array to be interpreted by vkCmdDecompressMemoryNV */
    VkDecompressMemoryRegionNV regions[VKD3D_DSTORAGE_MAX_TILE_COUNT];
};

typedef HRESULT (*d3d12_meta_command_create_proc)(struct d3d12_meta_command*, struct d3d12_device*, const void*, size_t);
typedef void (*d3d12_meta_command_exec_proc)(struct d3d12_meta_command*, struct d3d12_command_list*, const void*, size_t);

typedef ID3D12MetaCommand d3d12_meta_command_iface;

struct d3d12_meta_command
{
    d3d12_meta_command_iface ID3D12MetaCommand_iface;
    LONG refcount;

    d3d12_meta_command_exec_proc init_proc;
    d3d12_meta_command_exec_proc exec_proc;

    struct d3d12_device *device;

    struct vkd3d_private_store private_store;
    struct d3d_destruction_notifier destruction_notifier;
};

struct d3d12_meta_command *impl_from_ID3D12MetaCommand(ID3D12MetaCommand *iface);

void vkd3d_enumerate_meta_commands(struct d3d12_device *device, UINT *count, D3D12_META_COMMAND_DESC *output_descs);
bool vkd3d_enumerate_meta_command_parameters(struct d3d12_device *device, REFGUID command_id,
        D3D12_META_COMMAND_PARAMETER_STAGE stage, UINT *total_size, UINT *param_count,
        D3D12_META_COMMAND_PARAMETER_DESC *param_descs);
HRESULT d3d12_meta_command_create(struct d3d12_device *device, REFGUID guid,
        const void *parameters, size_t parameter_size, struct d3d12_meta_command **meta_command);

/* utils */
struct vkd3d_format_footprint
{
    DXGI_FORMAT dxgi_format;
    uint32_t block_width;
    uint32_t block_height;
    uint32_t block_byte_count;
    uint32_t subsample_x_log2;
    uint32_t subsample_y_log2;
};

struct vkd3d_format
{
    DXGI_FORMAT dxgi_format;
    VkFormat vk_format;
    uint32_t byte_count;
    uint32_t block_width;
    uint32_t block_height;
    uint32_t block_byte_count;
    VkImageAspectFlags vk_aspect_mask;
    unsigned int plane_count;
    enum vkd3d_format_type type;
    bool is_emulated;
    const struct vkd3d_format_footprint *plane_footprints;
    VkImageTiling vk_image_tiling;
    /* Only includes image format features explicitly for vk_format. */
    VkFormatFeatureFlags2 vk_format_features;
    /* If the format is TYPELESS or relaxed castable (e.g. sRGB to UNORM),
     * the feature list includes all potential format features.
     * This will hold either just depth features or color features depending on which format query is used. */
    VkFormatFeatureFlags2 vk_format_features_castable;
    /* Includes only buffer view features. */
    VkFormatFeatureFlags2 vk_format_features_buffer;
    /* Supported sample counts for regular and sparse images */
    VkSampleCountFlags supported_sample_counts;
    VkSampleCountFlags supported_sparse_sample_counts;
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
        unsigned int dst_slice_pitch, unsigned int w, unsigned int h, unsigned int d);

const struct vkd3d_format *vkd3d_get_format(const struct d3d12_device *device,
        DXGI_FORMAT dxgi_format, bool depth_stencil);
VkFormat vkd3d_internal_get_vk_format(const struct d3d12_device *device, DXGI_FORMAT dxgi_format);
struct vkd3d_format_footprint vkd3d_format_footprint_for_plane(const struct vkd3d_format *format, unsigned int plane_idx);

HRESULT vkd3d_init_format_info(struct d3d12_device *device);
void vkd3d_cleanup_format_info(struct d3d12_device *device);

static inline const struct vkd3d_format *vkd3d_format_from_d3d12_resource_desc(
        const struct d3d12_device *device, const D3D12_RESOURCE_DESC1 *desc, DXGI_FORMAT view_format)
{
    return vkd3d_get_format(device, view_format ? view_format : desc->Format,
            desc->Flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);
}

static inline VkImageSubresourceRange vk_subresource_range_from_subresource(const VkImageSubresource *subresource)
{
    VkImageSubresourceRange range;
    range.aspectMask = subresource->aspectMask;
    range.baseMipLevel = subresource->mipLevel;
    range.levelCount = 1;
    range.baseArrayLayer = subresource->arrayLayer;
    range.layerCount = 1;
    return range;
}

static inline VkImageSubresourceRange vk_subresource_range_from_layers(const VkImageSubresourceLayers *layers)
{
    VkImageSubresourceRange range;
    range.aspectMask = layers->aspectMask;
    range.baseMipLevel = layers->mipLevel;
    range.levelCount = 1;
    range.baseArrayLayer = layers->baseArrayLayer;
    range.layerCount = layers->layerCount;
    return range;
}

static inline VkImageSubresourceLayers vk_subresource_layers_from_subresource(const VkImageSubresource *subresource)
{
    VkImageSubresourceLayers layers;
    layers.aspectMask = subresource->aspectMask;
    layers.mipLevel = subresource->mipLevel;
    layers.baseArrayLayer = subresource->arrayLayer;
    layers.layerCount = 1;
    return layers;
}

static inline VkImageSubresourceLayers vk_subresource_layers_from_view(const struct vkd3d_view *view)
{
    VkImageSubresourceLayers layers;
    layers.aspectMask = view->info.texture.aspect_mask;
    layers.mipLevel = view->info.texture.miplevel_idx;
    layers.baseArrayLayer = view->info.texture.layer_idx;
    layers.layerCount = view->info.texture.layer_count;
    return layers;
}

static inline VkImageSubresourceRange vk_subresource_range_from_view(const struct vkd3d_view *view)
{
    VkImageSubresourceLayers layers = vk_subresource_layers_from_view(view);
    return vk_subresource_range_from_layers(&layers);
}

static inline bool d3d12_box_is_empty(const D3D12_BOX *box)
{
    return box->right <= box->left || box->bottom <= box->top || box->back <= box->front;
}

static inline unsigned int d3d12_resource_desc_get_layer_count(const D3D12_RESOURCE_DESC1 *desc)
{
    return desc->Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE3D ? desc->DepthOrArraySize : 1;
}

static inline VkExtent3D d3d12_resource_desc_get_vk_subresource_extent(const D3D12_RESOURCE_DESC1 *desc,
        const struct vkd3d_format *format, const VkImageSubresourceLayers *subresource)
{
    const struct vkd3d_format_footprint *footprint;
    unsigned int shift_x, shift_y, plane_idx;
    VkExtent3D extent;

    shift_x = subresource->mipLevel;
    shift_y = subresource->mipLevel;

    if (format && format->plane_footprints)
    {
        plane_idx = d3d12_plane_index_from_vk_aspect(subresource->aspectMask & -subresource->aspectMask);
        footprint = &format->plane_footprints[plane_idx];

        shift_x += footprint->subsample_x_log2;
        shift_y += footprint->subsample_y_log2;
    }

    extent.width = max(1, desc->Width >> shift_x);
    extent.height = max(1, desc->Height >> shift_y);

    if (desc->Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D)
        extent.depth = max(1, desc->DepthOrArraySize >> subresource->mipLevel);
    else
        extent.depth = 1;

    return extent;
}

static inline VkExtent3D vkd3d_compute_block_count(VkExtent3D extent, const struct vkd3d_format *format)
{
    if (max(format->block_width, format->block_height) == 1u)
        return extent;

    extent.width += format->block_width - 1u;
    extent.width /= format->block_width;

    extent.height += format->block_height - 1u;
    extent.height /= format->block_height;

    return extent;
}

static inline VkOffset3D vkd3d_compute_block_offset(VkOffset3D offset, const struct vkd3d_format *format)
{
    if (max(format->block_width, format->block_height) == 1u)
        return offset;

    offset.x /= format->block_width;
    offset.y /= format->block_height;
    return offset;
}

static inline VkExtent3D vkd3d_compute_texel_count_from_blocks(VkExtent3D extent, const struct vkd3d_format *format)
{
    extent.width *= format->block_width;
    extent.height *= format->block_height;
    return extent;
}

static inline VkOffset3D vkd3d_compute_texel_offset_from_blocks(VkOffset3D offset, const struct vkd3d_format *format)
{
    offset.x *= format->block_width;
    offset.y *= format->block_height;
    return offset;
}

static inline VkExtent3D d3d12_resource_desc_get_subresource_extent(const struct D3D12_RESOURCE_DESC1 *desc,
        const struct vkd3d_format *format, unsigned int subresource_idx)
{
    VkImageSubresourceLayers vk_subresource = vk_image_subresource_layers_from_d3d12(
        format, subresource_idx, desc->MipLevels, d3d12_resource_desc_get_layer_count(desc));
    return d3d12_resource_desc_get_vk_subresource_extent(desc, format, &vk_subresource);
}

static inline VkExtent3D d3d12_resource_get_view_subresource_extent(const struct d3d12_resource *resource, const struct vkd3d_view *view)
{
    VkImageSubresourceLayers vk_subresource = vk_subresource_layers_from_view(view);
    return d3d12_resource_desc_get_vk_subresource_extent(&resource->desc, resource->format, &vk_subresource);
}

static inline bool d3d12_resource_desc_is_sampler_feedback(const D3D12_RESOURCE_DESC1 *desc)
{
    return desc->Format == DXGI_FORMAT_SAMPLER_FEEDBACK_MIN_MIP_OPAQUE ||
            desc->Format == DXGI_FORMAT_SAMPLER_FEEDBACK_MIP_REGION_USED_OPAQUE;
}

static inline unsigned int d3d12_resource_desc_get_active_level_count(const D3D12_RESOURCE_DESC1 *desc)
{
    return d3d12_resource_desc_is_sampler_feedback(desc) ? 1 : desc->MipLevels;
}

static inline VkExtent3D d3d12_resource_desc_get_active_feedback_extent(const D3D12_RESOURCE_DESC1 *desc,
        unsigned int mip_level)
{
    VkImageSubresourceLayers vk_subresource;
    VkExtent3D result;

    vk_subresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    vk_subresource.mipLevel = mip_level;
    vk_subresource.baseArrayLayer = 0;
    vk_subresource.layerCount = 1;

    result = d3d12_resource_desc_get_vk_subresource_extent(desc, NULL, &vk_subresource);
    result.width = DIV_ROUND_UP(result.width, desc->SamplerFeedbackMipRegion.Width);
    result.height = DIV_ROUND_UP(result.height, desc->SamplerFeedbackMipRegion.Height);
    return result;
}

static inline VkExtent3D d3d12_resource_desc_get_padded_feedback_extent(const D3D12_RESOURCE_DESC1 *desc)
{
    const unsigned int ENCODED_REGION_ALIGNMENT = 16;
    unsigned int lsb_width, lsb_height;
    VkExtent3D result, active_result;

    active_result = d3d12_resource_desc_get_active_feedback_extent(desc, 0);
    lsb_width = vkd3d_log2i(desc->SamplerFeedbackMipRegion.Width);
    lsb_height = vkd3d_log2i(desc->SamplerFeedbackMipRegion.Height);

    /* Cute trick: Use the lower 4 bits of image size to signal mip region width / height.
     * We don't rely on specific edge behavior anyway, so this is a neat way of
     * doing it without changing the binding model *again* ... */
    result.width = (active_result.width & ~(ENCODED_REGION_ALIGNMENT - 1)) | lsb_width;
    result.height = (active_result.height & ~(ENCODED_REGION_ALIGNMENT - 1)) | lsb_height;
    result.depth = 1;

    if (result.width < active_result.width)
        result.width += ENCODED_REGION_ALIGNMENT;
    if (result.height < active_result.height)
        result.height += ENCODED_REGION_ALIGNMENT;

    return result;
}

static inline unsigned int d3d12_resource_desc_get_sub_resource_count_per_plane(const D3D12_RESOURCE_DESC1 *desc)
{
    return d3d12_resource_desc_get_layer_count(desc) * desc->MipLevels;
}

static inline unsigned int d3d12_resource_desc_get_sub_resource_count(const struct d3d12_device *device,
        const D3D12_RESOURCE_DESC1 *desc)
{
    const struct vkd3d_format *format = vkd3d_get_format(device, desc->Format, true);
    return d3d12_resource_desc_get_sub_resource_count_per_plane(desc) * (format ? format->plane_count : 1);
}

static inline unsigned int d3d12_resource_get_sub_resource_count(const struct d3d12_resource *resource)
{
    return d3d12_resource_desc_get_sub_resource_count_per_plane(&resource->desc) *
            (resource->format ? vkd3d_popcount(resource->format->vk_aspect_mask) : 1);
}

static inline void vkd3d_get_depth_bias_representation(VkDepthBiasRepresentationInfoEXT *info,
        const struct d3d12_device *device, DXGI_FORMAT dsv_format)
{
    memset(info, 0, sizeof(*info));
    info->sType = VK_STRUCTURE_TYPE_DEPTH_BIAS_REPRESENTATION_INFO_EXT;
    info->depthBiasRepresentation = VK_DEPTH_BIAS_REPRESENTATION_LEAST_REPRESENTABLE_VALUE_FORMAT_EXT;
    info->depthBiasExact = device->device_info.depth_bias_control_features.depthBiasExact;

    /* Checking only strongly typed formats should work here since we take the
     * format from the DSV or PSO desc, where typeless formats are not allowed */
    if (device->device_info.depth_bias_control_features.leastRepresentableValueForceUnormRepresentation &&
            (dsv_format == DXGI_FORMAT_D16_UNORM || dsv_format == DXGI_FORMAT_D24_UNORM_S8_UINT))
        info->depthBiasRepresentation = VK_DEPTH_BIAS_REPRESENTATION_LEAST_REPRESENTABLE_VALUE_FORCE_UNORM_EXT;
}

VkDeviceAddress vkd3d_get_buffer_device_address(struct d3d12_device *device, VkBuffer vk_buffer);
VkDeviceAddress vkd3d_get_acceleration_structure_device_address(struct d3d12_device *device,
        VkAccelerationStructureKHR vk_acceleration_structure);

static inline unsigned int vkd3d_compute_workgroup_count(unsigned int thread_count, unsigned int workgroup_size)
{
    return (thread_count + workgroup_size - 1) / workgroup_size;
}

VkCompareOp vk_compare_op_from_d3d12(D3D12_COMPARISON_FUNC op);
VkSampleCountFlagBits vk_samples_from_dxgi_sample_desc(const DXGI_SAMPLE_DESC *desc);
VkSampleCountFlagBits vk_samples_from_sample_count(unsigned int sample_count);

bool is_valid_feature_level(D3D_FEATURE_LEVEL feature_level);

bool is_valid_resource_state(D3D12_RESOURCE_STATES state);
bool is_write_resource_state(D3D12_RESOURCE_STATES state);

bool is_valid_format(DXGI_FORMAT format);

HRESULT return_interface(void *iface, REFIID iface_iid,
        REFIID requested_iid, void **object);

const char *debug_dxgi_format(DXGI_FORMAT format);
const char *debug_d3d12_box(const D3D12_BOX *box);
const char *debug_d3d12_shader_component_mapping(unsigned int mapping);
const char *debug_vk_extent_3d(VkExtent3D extent);

#define VKD3D_DEBUG_FLAGS_BUFFER_SIZE 1024
const char *debug_vk_memory_heap_flags(VkMemoryHeapFlags flags, char buffer[VKD3D_DEBUG_FLAGS_BUFFER_SIZE]);
const char *debug_vk_memory_property_flags(VkMemoryPropertyFlags flags, char buffer[VKD3D_DEBUG_FLAGS_BUFFER_SIZE]);
const char *debug_vk_queue_flags(VkQueueFlags flags, char buffer[VKD3D_DEBUG_FLAGS_BUFFER_SIZE]);

static inline void debug_ignored_node_mask(unsigned int mask)
{
    if (mask && mask != 1)
        FIXME("Ignoring node mask 0x%08x.\n", mask);
}

HRESULT vkd3d_load_vk_global_procs(struct vkd3d_vk_global_procs *procs,
        PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr);
HRESULT vkd3d_load_vk_instance_procs(struct vkd3d_vk_instance_procs *procs,
        const struct vkd3d_vk_global_procs *global_procs, VkInstance instance);
HRESULT vkd3d_load_vk_device_procs(struct vkd3d_vk_device_procs *procs,
        const struct vkd3d_vk_instance_procs *parent_procs, VkDevice device);

HRESULT vkd3d_set_vk_object_name(struct d3d12_device *device, uint64_t vk_object,
        VkObjectType vk_object_type, const char *name);

enum VkPrimitiveTopology vk_topology_from_d3d12_topology(D3D12_PRIMITIVE_TOPOLOGY topology);

static inline void vk_prepend_struct(void *header, void *structure)
{
    VkBaseOutStructure *vk_header = header, *vk_structure = structure;

    assert(!vk_structure->pNext);
    vk_structure->pNext = vk_header->pNext;
    vk_header->pNext = vk_structure;
}

static inline void vk_remove_struct(void *header, VkStructureType type)
{
    VkBaseInStructure *chain;
    for (chain = (VkBaseInStructure *)header; chain->pNext; chain = (VkBaseInStructure *)chain->pNext)
    {
        if (chain->pNext->sType == type)
        {
            chain->pNext = chain->pNext->pNext;
            break;
        }
    }
}

#define VKD3D_NULL_BUFFER_SIZE 16

struct vkd3d_view_key
{
    enum vkd3d_view_type view_type;
    union
    {
        struct vkd3d_buffer_view_desc buffer;
        struct vkd3d_texture_view_desc texture;
        D3D12_SAMPLER_DESC2 sampler;
    } u;
};
struct vkd3d_view *vkd3d_view_map_create_view(struct vkd3d_view_map *view_map,
        struct d3d12_device *device, const struct vkd3d_view_key *key);

/* This is not a hard limit, just an arbitrary value which lets us avoid allocation for
 * the common case. */
#define VKD3D_BUILD_INFO_STACK_COUNT 16

uint32_t vkd3d_acceleration_structure_get_geometry_count(
        const D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS *desc);
bool vkd3d_acceleration_structure_convert_inputs(const struct d3d12_device *device,
        const D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS *desc,
        VkAccelerationStructureBuildGeometryInfoKHR *build_info,
        VkAccelerationStructureGeometryKHR *geometry_infos,
        VkAccelerationStructureBuildRangeInfoKHR *range_infos,
        uint32_t *primitive_counts);
void vkd3d_acceleration_structure_emit_postbuild_info(
        struct d3d12_command_list *list,
        const D3D12_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO_DESC *desc,
        uint32_t count, const D3D12_GPU_VIRTUAL_ADDRESS *addresses);
void vkd3d_acceleration_structure_emit_immediate_postbuild_info(
        struct d3d12_command_list *list, uint32_t count,
        const D3D12_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO_DESC *desc,
        VkAccelerationStructureKHR vk_acceleration_structure);
void vkd3d_acceleration_structure_copy(
        struct d3d12_command_list *list,
        D3D12_GPU_VIRTUAL_ADDRESS dst, D3D12_GPU_VIRTUAL_ADDRESS src,
        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_COPY_MODE mode);

typedef enum D3D11_USAGE
{
    D3D11_USAGE_DEFAULT,
    D3D11_USAGE_IMMUTABLE,
    D3D11_USAGE_DYNAMIC,
    D3D11_USAGE_STAGING,
} D3D11_USAGE;

typedef enum D3D11_BIND_FLAG
{
    D3D11_BIND_VERTEX_BUFFER    = 0x0001,
    D3D11_BIND_INDEX_BUFFER     = 0x0002,
    D3D11_BIND_CONSTANT_BUFFER  = 0x0004,
    D3D11_BIND_SHADER_RESOURCE  = 0x0008,
    D3D11_BIND_STREAM_OUTPUT    = 0x0010,
    D3D11_BIND_RENDER_TARGET    = 0x0020,
    D3D11_BIND_DEPTH_STENCIL    = 0x0040,
    D3D11_BIND_UNORDERED_ACCESS = 0x0080,
    D3D11_BIND_DECODER          = 0x0200,
    D3D11_BIND_VIDEO_ENCODER    = 0x0400
} D3D11_BIND_FLAG;

typedef enum D3D11_TEXTURE_LAYOUT
{
    D3D11_TEXTURE_LAYOUT_UNDEFINED = 0x0,
    D3D11_TEXTURE_LAYOUT_ROW_MAJOR = 0x1,
    D3D11_TEXTURE_LAYOUT_64K_STANDARD_SWIZZLE = 0x2,
} D3D11_TEXTURE_LAYOUT;

typedef enum D3D11_RESOURCE_MISC_FLAG
{
    D3D11_RESOURCE_MISC_GENERATE_MIPS                    = 0x1,
    D3D11_RESOURCE_MISC_SHARED                           = 0x2,
    D3D11_RESOURCE_MISC_TEXTURECUBE                      = 0x4,
    D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS                = 0x10,
    D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS           = 0x20,
    D3D11_RESOURCE_MISC_BUFFER_STRUCTURED                = 0x40,
    D3D11_RESOURCE_MISC_RESOURCE_CLAMP                   = 0x80,
    D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX                = 0x100,
    D3D11_RESOURCE_MISC_GDI_COMPATIBLE                   = 0x200,
    D3D11_RESOURCE_MISC_SHARED_NTHANDLE                  = 0x800,
    D3D11_RESOURCE_MISC_RESTRICTED_CONTENT               = 0x1000,
    D3D11_RESOURCE_MISC_RESTRICT_SHARED_RESOURCE         = 0x2000,
    D3D11_RESOURCE_MISC_RESTRICT_SHARED_RESOURCE_DRIVER  = 0x4000,
    D3D11_RESOURCE_MISC_GUARDED                          = 0x8000,
    D3D11_RESOURCE_MISC_TILE_POOL                        = 0x20000,
    D3D11_RESOURCE_MISC_TILED                            = 0x40000,
    D3D11_RESOURCE_MISC_HW_PROTECTED                     = 0x80000,
} D3D11_RESOURCE_MISC_FLAG;

struct DxvkSharedTextureMetadata {
    UINT             Width;
    UINT             Height;
    UINT             MipLevels;
    UINT             ArraySize;
    DXGI_FORMAT      Format;
    DXGI_SAMPLE_DESC SampleDesc;
    D3D11_USAGE      Usage;
    UINT             BindFlags;
    UINT             CPUAccessFlags;
    UINT             MiscFlags;
    D3D11_TEXTURE_LAYOUT TextureLayout;
};

bool vkd3d_set_shared_metadata(HANDLE handle, void *buf, uint32_t buf_size);
bool vkd3d_get_shared_metadata(HANDLE handle, void *buf, uint32_t buf_size, uint32_t *metadata_size);
HANDLE vkd3d_open_kmt_handle(HANDLE kmt_handle);

#define VKD3D_VENDOR_ID_NVIDIA 0x10DE
#define VKD3D_VENDOR_ID_AMD 0x1002
#define VKD3D_VENDOR_ID_INTEL 0x8086

#define VKD3D_DRIVER_VERSION_MAJOR_NV(v) ((v) >> 22)
#define VKD3D_DRIVER_VERSION_MINOR_NV(v) (((v) >> 14) & 0xff)
#define VKD3D_DRIVER_VERSION_PATCH_NV(v) (((v) >>  6) & 0xff)
#define VKD3D_DRIVER_VERSION_MAKE_NV(major, minor, patch) (((uint32_t)(major) << 22) | ((uint32_t)(minor) << 14) | ((uint32_t)(patch) << 6))

static inline const void *vk_find_pnext(const void *pnext, VkStructureType sType)
{
    const VkBaseInStructure *base_in = pnext;
    while (base_in && base_in->sType != sType)
        base_in = base_in->pNext;
    return base_in;
}

static inline void vkd3d_mapped_memory_range_align(const struct d3d12_device *device, VkMappedMemoryRange *range, VkDeviceSize size)
{
    size_t atom_size = device->device_info.properties2.properties.limits.nonCoherentAtomSize;
    range->size += range->offset & (atom_size - 1);
    range->offset &= ~(VkDeviceSize)(atom_size - 1);
    range->size = align64(range->size, atom_size);
    range->size = min(range->size, size - range->offset);
}

#endif  /* __VKD3D_PRIVATE_H */
