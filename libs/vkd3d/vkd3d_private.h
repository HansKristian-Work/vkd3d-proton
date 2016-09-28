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

#ifndef __VKD3D_PRIVATE_H
#define __VKD3D_PRIVATE_H

#define COBJMACROS
#define NONAMELESSUNION
#include "vkd3d_common.h"
#include "vkd3d_debug.h"
#include "vkd3d_vulkan.h"

#include "d3d12.h"

#include <assert.h>

struct d3d12_command_list;
struct d3d12_device;

struct vkd3d_instance
{
    VkInstance vk_instance;
    struct vkd3d_vk_instance_procs vk_procs;
};

/* ID3D12Resource */
struct d3d12_resource
{
    ID3D12Resource ID3D12Resource_iface;
    ULONG refcount;

    D3D12_RESOURCE_DESC desc;

    union
    {
        VkBuffer vk_buffer;
        VkImage vk_image;
    } u;
    VkDeviceMemory vk_memory;

    unsigned int map_count;
    void *map_data;

    struct d3d12_device *device;
};

HRESULT d3d12_committed_resource_create(struct d3d12_device *device,
        const D3D12_HEAP_PROPERTIES *heap_properties, D3D12_HEAP_FLAGS heap_flags,
        const D3D12_RESOURCE_DESC *desc, D3D12_RESOURCE_STATES initial_state,
        const D3D12_CLEAR_VALUE *optimized_clear_value, struct d3d12_resource **resource) DECLSPEC_HIDDEN;

/* ID3D12DescriptorHeap */
struct d3d12_descriptor_heap
{
    ID3D12DescriptorHeap ID3D12DescriptorHeap_iface;
    ULONG refcount;

    D3D12_DESCRIPTOR_HEAP_DESC desc;

    struct d3d12_device *device;
};

HRESULT d3d12_descriptor_heap_create(struct d3d12_device *device,
        const D3D12_DESCRIPTOR_HEAP_DESC *desc, struct d3d12_descriptor_heap **descriptor_heap) DECLSPEC_HIDDEN;

/* ID3D12RootSignature */
struct d3d12_root_signature
{
    ID3D12RootSignature ID3D12RootSignature_iface;
    ULONG refcount;

    VkPipelineLayout vk_pipeline_layout;

    struct d3d12_device *device;
};

HRESULT d3d12_root_signature_create(struct d3d12_device *device,
        const D3D12_ROOT_SIGNATURE_DESC *desc, struct d3d12_root_signature **root_signature) DECLSPEC_HIDDEN;

/* ID3D12PipelineState */
struct d3d12_pipeline_state
{
    ID3D12PipelineState ID3D12PipelineState_iface;
    ULONG refcount;

    VkPipeline vk_pipeline;

    struct d3d12_device *device;
};

HRESULT d3d12_pipeline_state_create_compute(struct d3d12_device *device,
        const D3D12_COMPUTE_PIPELINE_STATE_DESC *desc, struct d3d12_pipeline_state **state) DECLSPEC_HIDDEN;

/* ID3D12CommandAllocator */
struct d3d12_command_allocator
{
    ID3D12CommandAllocator ID3D12CommandAllocator_iface;
    ULONG refcount;

    D3D12_COMMAND_LIST_TYPE type;

    VkCommandPool vk_command_pool;

    struct d3d12_command_list *current_command_list;
    struct d3d12_device *device;
};

HRESULT d3d12_command_allocator_create(struct d3d12_device *device,
        D3D12_COMMAND_LIST_TYPE type, struct d3d12_command_allocator **allocator) DECLSPEC_HIDDEN;

/* ID3D12CommandList */
struct d3d12_command_list
{
    ID3D12GraphicsCommandList ID3D12GraphicsCommandList_iface;
    ULONG refcount;

    D3D12_COMMAND_LIST_TYPE type;

    VkCommandBuffer vk_command_buffer;

    struct d3d12_command_allocator *allocator;
    struct d3d12_device *device;
};

HRESULT d3d12_command_list_create(struct d3d12_device *device,
        UINT node_mask, D3D12_COMMAND_LIST_TYPE type, ID3D12CommandAllocator *allocator_iface,
        ID3D12PipelineState *initial_pipeline_state, struct d3d12_command_list **list) DECLSPEC_HIDDEN;

/* ID3D12CommandQueue */
struct d3d12_command_queue
{
    ID3D12CommandQueue ID3D12CommandQueue_iface;
    ULONG refcount;

    D3D12_COMMAND_QUEUE_DESC desc;

    struct d3d12_device *device;
};

HRESULT d3d12_command_queue_create(struct d3d12_device *device,
        const D3D12_COMMAND_QUEUE_DESC *desc, struct d3d12_command_queue **queue) DECLSPEC_HIDDEN;

/* ID3D12Device */
struct d3d12_device
{
    ID3D12Device ID3D12Device_iface;
    ULONG refcount;

    VkDevice vk_device;
    struct vkd3d_vk_device_procs vk_procs;

    unsigned int direct_queue_family_index;
    unsigned int copy_queue_family_index;
    VkPhysicalDeviceMemoryProperties memory_properties;

    struct vkd3d_instance vkd3d_instance;
};

HRESULT d3d12_device_create(struct d3d12_device **device) DECLSPEC_HIDDEN;

/* utils */
BOOL is_valid_feature_level(D3D_FEATURE_LEVEL feature_level) DECLSPEC_HIDDEN;
BOOL check_feature_level_support(D3D_FEATURE_LEVEL feature_level) DECLSPEC_HIDDEN;

HRESULT return_interface(IUnknown *iface, REFIID iface_riid,
        REFIID requested_riid, void **object) DECLSPEC_HIDDEN;

const char *debug_vk_extent_3d(VkExtent3D extent) DECLSPEC_HIDDEN;
const char *debug_vk_memory_heap_flags(VkMemoryHeapFlags flags) DECLSPEC_HIDDEN;
const char *debug_vk_memory_property_flags(VkMemoryPropertyFlags flags) DECLSPEC_HIDDEN;
const char *debug_vk_queue_flags(VkQueueFlags flags) DECLSPEC_HIDDEN;

VkFormat vk_format_from_dxgi_format(DXGI_FORMAT format) DECLSPEC_HIDDEN;

static inline void *vkd3d_malloc(size_t size)
{
    void *ptr;
    if (!(ptr = malloc(size)))
        ERR("Out of memory.\n");
    return ptr;
}

static inline void *vkd3d_calloc(size_t count, size_t size)
{
    void *ptr;
    if (!(ptr = calloc(count, size)))
        ERR("Out of memory.\n");
    return ptr;
}

static inline void vkd3d_free(void *ptr)
{
    free(ptr);
}

HRESULT hresult_from_vk_result(VkResult vr) DECLSPEC_HIDDEN;

HRESULT vkd3d_load_vk_instance_procs(struct vkd3d_vk_instance_procs *procs,
        VkInstance instance) DECLSPEC_HIDDEN;
HRESULT vkd3d_load_vk_device_procs(struct vkd3d_vk_device_procs *procs,
        const struct vkd3d_vk_instance_procs *parent_procs, VkDevice device) DECLSPEC_HIDDEN;

#endif  /* __VKD3D_PRIVATE_H */
