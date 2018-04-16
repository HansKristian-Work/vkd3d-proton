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

#define VK_NO_PROTOTYPES

#define COBJMACROS
#define NONAMELESSUNION
#include "vkd3d.h"
#include "vkd3d_memory.h"
#include "vkd3d_shader.h"

#include <assert.h>
#include <inttypes.h>
#include <limits.h>
#include <pthread.h>
#include <stdbool.h>

#define VK_CALL(f) (vk_procs->f)

#define VKD3D_DESCRIPTOR_MAGIC_FREE    0x00000000u
#define VKD3D_DESCRIPTOR_MAGIC_CBV     0x00564243u
#define VKD3D_DESCRIPTOR_MAGIC_SRV     0x00565253u
#define VKD3D_DESCRIPTOR_MAGIC_UAV     0x00564155u
#define VKD3D_DESCRIPTOR_MAGIC_SAMPLER 0x504d4153u
#define VKD3D_DESCRIPTOR_MAGIC_DSV     0x00565344u
#define VKD3D_DESCRIPTOR_MAGIC_RTV     0x00565452u

#define VKD3D_MAX_SHADER_STAGES     5u

struct d3d12_command_list;
struct d3d12_device;

struct vkd3d_vk_global_procs
{
    PFN_vkCreateInstance vkCreateInstance;
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

struct vkd3d_vulkan_info
{
    /* instance extensions */
    bool KHR_get_physical_device_properties2;
    bool EXT_debug_report;
    /* device extensions */
    bool KHR_push_descriptor;

    VkPhysicalDeviceLimits device_limits;
    VkPhysicalDeviceSparseProperties sparse_properties;
};

struct vkd3d_instance
{
    VkInstance vk_instance;
    struct vkd3d_vk_instance_procs vk_procs;

    PFN_vkd3d_signal_event signal_event;
    PFN_vkd3d_create_thread create_thread;
    PFN_vkd3d_join_thread join_thread;
    size_t wchar_size;

    struct vkd3d_vulkan_info vk_info;
    struct vkd3d_vk_global_procs vk_global_procs;
    void *libvulkan;

    VkDebugReportCallbackEXT vk_debug_callback;

    LONG refcount;
};

struct vkd3d_fence_worker
{
    union
    {
        pthread_t thread;
        void *handle;
    } u;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    bool should_exit;

    size_t fence_count;
    VkFence *vk_fences;
    size_t vk_fences_size;
    struct vkd3d_waiting_fence
    {
        ID3D12Fence *fence;
        UINT64 value;
    } *fences;
    size_t fences_size;

    struct d3d12_device *device;
};

HRESULT vkd3d_fence_worker_start(struct vkd3d_fence_worker *worker,
        struct d3d12_device *device) DECLSPEC_HIDDEN;
HRESULT vkd3d_fence_worker_stop(struct vkd3d_fence_worker *worker,
        struct d3d12_device *device) DECLSPEC_HIDDEN;

struct vkd3d_gpu_va_allocator
{
    pthread_mutex_t mutex;

    D3D12_GPU_VIRTUAL_ADDRESS floor;

    struct vkd3d_gpu_va_allocation
    {
        D3D12_GPU_VIRTUAL_ADDRESS base;
        SIZE_T size;
        void *ptr;
    } *allocations;
    size_t allocations_size;
    size_t allocation_count;
};

D3D12_GPU_VIRTUAL_ADDRESS vkd3d_gpu_va_allocator_allocate(struct vkd3d_gpu_va_allocator *allocator,
        size_t size, void *ptr) DECLSPEC_HIDDEN;
void *vkd3d_gpu_va_allocator_dereference(struct vkd3d_gpu_va_allocator *allocator,
        D3D12_GPU_VIRTUAL_ADDRESS address) DECLSPEC_HIDDEN;
void vkd3d_gpu_va_allocator_free(struct vkd3d_gpu_va_allocator *allocator,
        D3D12_GPU_VIRTUAL_ADDRESS address) DECLSPEC_HIDDEN;

/* ID3D12Fence */
struct d3d12_fence
{
    ID3D12Fence ID3D12Fence_iface;
    LONG refcount;

    UINT64 value;
    pthread_mutex_t mutex;

    struct vkd3d_waiting_event
    {
        UINT64 value;
        HANDLE event;
    } *events;
    size_t events_size;
    size_t event_count;

    struct d3d12_device *device;
};

HRESULT d3d12_fence_create(struct d3d12_device *device,
        UINT64 initial_value, D3D12_FENCE_FLAGS flags, struct d3d12_fence **fence) DECLSPEC_HIDDEN;

#define VKD3D_RESOURCE_PUBLIC_FLAGS \
        (VKD3D_RESOURCE_INITIAL_STATE_TRANSITION | VKD3D_RESOURCE_PRESENT_STATE_TRANSITION)
#define VKD3D_RESOURCE_EXTERNAL 0x00000004

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
    VkDeviceMemory vk_memory;
    unsigned int flags;

    unsigned int map_count;
    void *map_data;

    D3D12_HEAP_PROPERTIES heap_properties;
    D3D12_HEAP_FLAGS heap_flags;
    D3D12_RESOURCE_STATES initial_state;
    D3D12_RESOURCE_STATES present_state;

    struct d3d12_device *device;
};

static inline bool d3d12_resource_is_buffer(const struct d3d12_resource *resource)
{
    return resource->desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER;
}

static inline bool d3d12_resource_is_texture(const struct d3d12_resource *resource)
{
    return resource->desc.Dimension != D3D12_RESOURCE_DIMENSION_BUFFER;
}

HRESULT d3d12_committed_resource_create(struct d3d12_device *device,
        const D3D12_HEAP_PROPERTIES *heap_properties, D3D12_HEAP_FLAGS heap_flags,
        const D3D12_RESOURCE_DESC *desc, D3D12_RESOURCE_STATES initial_state,
        const D3D12_CLEAR_VALUE *optimized_clear_value, struct d3d12_resource **resource) DECLSPEC_HIDDEN;
struct d3d12_resource *unsafe_impl_from_ID3D12Resource(ID3D12Resource *iface) DECLSPEC_HIDDEN;

struct vkd3d_view
{
    LONG refcount;
    union
    {
        VkBufferView vk_buffer_view;
        VkImageView vk_image_view;
        VkSampler vk_sampler;
    } u;
    VkBufferView vk_counter_view;
};

struct d3d12_desc
{
    uint32_t magic;
    VkDescriptorType vk_descriptor_type;
    union
    {
        VkDescriptorBufferInfo vk_cbv_info;
        struct vkd3d_view *view;
    } u;

    VkDeviceSize view_offset;
    VkDeviceSize view_size;
};

static inline struct d3d12_desc *d3d12_desc_from_cpu_handle(D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle)
{
    return (struct d3d12_desc *)cpu_handle.ptr;
}

static inline struct d3d12_desc *d3d12_desc_from_gpu_handle(D3D12_GPU_DESCRIPTOR_HANDLE gpu_handle)
{
    return (struct d3d12_desc *)(intptr_t)gpu_handle.ptr;
}

void d3d12_desc_copy(struct d3d12_desc *dst, const struct d3d12_desc *src,
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

bool vkd3d_create_raw_buffer_view(struct d3d12_device *device,
        D3D12_GPU_VIRTUAL_ADDRESS gpu_address, VkBufferView *vk_buffer_view) DECLSPEC_HIDDEN;
HRESULT vkd3d_create_static_sampler(struct d3d12_device *device,
        const D3D12_STATIC_SAMPLER_DESC *desc, VkSampler *vk_sampler) DECLSPEC_HIDDEN;

struct d3d12_rtv_desc
{
    uint32_t magic;
    VkFormat format;
    uint64_t width;
    unsigned int height;
    VkImageView vk_view;
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
    VkFormat format;
    uint64_t width;
    unsigned int height;
    VkImageView vk_view;
    struct d3d12_resource *resource;
};

static inline struct d3d12_dsv_desc *d3d12_dsv_desc_from_cpu_handle(D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle)
{
    return (struct d3d12_dsv_desc *)cpu_handle.ptr;
}

void d3d12_dsv_desc_create_dsv(struct d3d12_dsv_desc *dsv_desc, struct d3d12_device *device,
        struct d3d12_resource *resource, const D3D12_DEPTH_STENCIL_VIEW_DESC *desc) DECLSPEC_HIDDEN;

/* ID3D12DescriptorHeap */
struct d3d12_descriptor_heap
{
    ID3D12DescriptorHeap ID3D12DescriptorHeap_iface;
    LONG refcount;

    D3D12_DESCRIPTOR_HEAP_DESC desc;

    struct d3d12_device *device;

    BYTE descriptors[];
};

HRESULT d3d12_descriptor_heap_create(struct d3d12_device *device,
        const D3D12_DESCRIPTOR_HEAP_DESC *desc, struct d3d12_descriptor_heap **descriptor_heap) DECLSPEC_HIDDEN;

/* ID3D12QueryHeap */
struct d3d12_query_heap
{
    ID3D12QueryHeap ID3D12QueryHeap_iface;
    LONG refcount;

    VkQueryPool vk_query_pool;

    struct d3d12_device *device;

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

struct d3d12_root_descriptor_table_range
{
    unsigned int offset;
    unsigned int descriptor_count;
    uint32_t binding;

    uint32_t descriptor_magic;
    unsigned int base_register_idx;
};

struct d3d12_root_descriptor_table
{
    unsigned int range_count;
    struct d3d12_root_descriptor_table_range *ranges;
};

struct d3d12_root_constant
{
    VkShaderStageFlags stage_flags;
    uint32_t offset;
};

struct d3d12_root_descriptor
{
    uint32_t binding;
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
    VkDescriptorSetLayout vk_push_set_layout;
    VkDescriptorSetLayout vk_set_layout;

    struct VkDescriptorPoolSize *pool_sizes;
    size_t pool_size_count;

    struct d3d12_root_parameter *parameters;
    unsigned int parameter_count;
    uint32_t main_set;

    unsigned int descriptor_count;
    struct vkd3d_shader_resource_binding *descriptor_mapping;
    struct vkd3d_shader_descriptor_binding default_sampler;

    unsigned int root_constant_count;
    struct vkd3d_shader_push_constant_buffer *root_constants;

    unsigned int root_descriptor_count;

    unsigned int push_constant_range_count;
    /* Only a single push constant range may include the same stage in Vulkan. */
    VkPushConstantRange push_constant_ranges[D3D12_SHADER_VISIBILITY_PIXEL + 1];

    unsigned int static_sampler_count;
    VkSampler *static_samplers;

    struct d3d12_device *device;
};

HRESULT d3d12_root_signature_create(struct d3d12_device *device, const void *bytecode,
        size_t bytecode_length, struct d3d12_root_signature **root_signature) DECLSPEC_HIDDEN;
struct d3d12_root_signature *unsafe_impl_from_ID3D12RootSignature(ID3D12RootSignature *iface) DECLSPEC_HIDDEN;

struct d3d12_graphics_pipeline_state
{
    struct VkPipelineShaderStageCreateInfo stages[VKD3D_MAX_SHADER_STAGES];
    size_t stage_count;

    struct VkVertexInputAttributeDescription attributes[D3D12_VS_INPUT_REGISTER_COUNT];
    enum VkVertexInputRate input_rates[D3D12_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT];
    size_t attribute_count;

    struct VkAttachmentDescription attachments[D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT + 1];
    struct VkAttachmentReference attachment_references[D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT + 1];
    struct VkPipelineColorBlendAttachmentState blend_attachments[D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT];
    size_t attachment_count, rt_idx;
    VkRenderPass render_pass;

    struct VkPipelineRasterizationStateCreateInfo rs_desc;
    struct VkPipelineMultisampleStateCreateInfo ms_desc;
    struct VkPipelineDepthStencilStateCreateInfo ds_desc;

    const struct d3d12_root_signature *root_signature;
};

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

    VkPipelineLayout vk_pipeline_layout;
    VkDescriptorSetLayout vk_set_layout;
    uint32_t set_index;

    struct vkd3d_shader_uav_counter_binding *uav_counters;
    uint8_t uav_counter_mask;

    struct d3d12_device *device;
};

HRESULT d3d12_pipeline_state_create_compute(struct d3d12_device *device,
        const D3D12_COMPUTE_PIPELINE_STATE_DESC *desc, struct d3d12_pipeline_state **state) DECLSPEC_HIDDEN;
HRESULT d3d12_pipeline_state_create_graphics(struct d3d12_device *device,
        const D3D12_GRAPHICS_PIPELINE_STATE_DESC *desc, struct d3d12_pipeline_state **state) DECLSPEC_HIDDEN;
struct d3d12_pipeline_state *unsafe_impl_from_ID3D12PipelineState(ID3D12PipelineState *iface) DECLSPEC_HIDDEN;

struct vkd3d_buffer
{
    VkBuffer vk_buffer;
    VkDeviceMemory vk_memory;
};

/* ID3D12CommandAllocator */
struct d3d12_command_allocator
{
    ID3D12CommandAllocator ID3D12CommandAllocator_iface;
    LONG refcount;

    D3D12_COMMAND_LIST_TYPE type;

    VkCommandPool vk_command_pool;

    VkRenderPass *passes;
    size_t passes_size;
    size_t pass_count;

    VkFramebuffer *framebuffers;
    size_t framebuffers_size;
    size_t framebuffer_count;

    VkPipeline *pipelines;
    size_t pipelines_size;
    size_t pipeline_count;

    VkDescriptorPool *descriptor_pools;
    size_t descriptor_pools_size;
    size_t descriptor_pool_count;

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
};

HRESULT d3d12_command_allocator_create(struct d3d12_device *device,
        D3D12_COMMAND_LIST_TYPE type, struct d3d12_command_allocator **allocator) DECLSPEC_HIDDEN;

struct vkd3d_pipeline_bindings
{
    const struct d3d12_root_signature *root_signature;

    VkDescriptorSet descriptor_set;
    bool in_use;

    D3D12_GPU_DESCRIPTOR_HANDLE descriptor_tables[D3D12_MAX_ROOT_COST];
    uint64_t descriptor_table_dirty_mask;

    VkBufferView vk_uav_counter_views[VKD3D_SHADER_MAX_UNORDERED_ACCESS_VIEWS];
    uint8_t uav_counter_dirty_mask;
};

/* ID3D12CommandList */
struct d3d12_command_list
{
    ID3D12GraphicsCommandList ID3D12GraphicsCommandList_iface;
    LONG refcount;

    D3D12_COMMAND_LIST_TYPE type;
    ID3D12PipelineState *pipeline_state;

    VkCommandBuffer vk_command_buffer;
    bool is_recording;
    bool is_valid;

    uint32_t strides[D3D12_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT];
    struct VkPipelineInputAssemblyStateCreateInfo ia_desc;

    VkImageView views[D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT + 1];
    unsigned int fb_width;
    unsigned int fb_height;

    VkFramebuffer current_framebuffer;
    VkPipeline current_pipeline;
    struct vkd3d_pipeline_bindings pipeline_bindings[VK_PIPELINE_BIND_POINT_RANGE_SIZE];

    struct d3d12_pipeline_state *state;

    struct d3d12_command_allocator *allocator;
    struct d3d12_device *device;
};

HRESULT d3d12_command_list_create(struct d3d12_device *device,
        UINT node_mask, D3D12_COMMAND_LIST_TYPE type, ID3D12CommandAllocator *allocator_iface,
        ID3D12PipelineState *initial_pipeline_state, struct d3d12_command_list **list) DECLSPEC_HIDDEN;

struct vkd3d_queue
{
    /* Access to VkQueue must be externally synchronized. */
    pthread_mutex_t mutex;
    VkQueue vk_queue;
    uint32_t vk_family_index;
    uint32_t timestamp_bits;
};

HRESULT vkd3d_queue_create(struct d3d12_device *device,
        uint32_t family_index, uint32_t timestamp_bits, struct vkd3d_queue **queue) DECLSPEC_HIDDEN;
void vkd3d_queue_destroy(struct vkd3d_queue *queue) DECLSPEC_HIDDEN;

/* ID3D12CommandQueue */
struct d3d12_command_queue
{
    ID3D12CommandQueue ID3D12CommandQueue_iface;
    LONG refcount;

    D3D12_COMMAND_QUEUE_DESC desc;

    struct vkd3d_queue *vkd3d_queue;

    struct d3d12_device *device;
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
};

HRESULT d3d12_command_signature_create(struct d3d12_device *device, const D3D12_COMMAND_SIGNATURE_DESC *desc,
        struct d3d12_command_signature **signature) DECLSPEC_HIDDEN;
struct d3d12_command_signature *unsafe_impl_from_ID3D12CommandSignature(ID3D12CommandSignature *iface) DECLSPEC_HIDDEN;

/* ID3D12Device */
struct d3d12_device
{
    ID3D12Device ID3D12Device_iface;
    LONG refcount;

    VkDevice vk_device;
    VkPhysicalDevice vk_physical_device;
    struct vkd3d_vk_device_procs vk_procs;
    PFN_vkd3d_signal_event signal_event;
    size_t wchar_size;

    struct vkd3d_gpu_va_allocator gpu_va_allocator;
    struct vkd3d_fence_worker fence_worker;

    VkPipelineCache vk_pipeline_cache;

    /* A sampler used for SpvOpImageFetch. */
    VkSampler vk_default_sampler;

    VkPhysicalDeviceMemoryProperties memory_properties;

    D3D12_FEATURE_DATA_D3D12_OPTIONS feature_options;

    struct vkd3d_vulkan_info vk_info;

    struct vkd3d_queue *direct_queue;
    struct vkd3d_queue *compute_queue;
    struct vkd3d_queue *copy_queue;

    struct vkd3d_instance *vkd3d_instance;

    PFN_vkd3d_create_thread create_thread;
    PFN_vkd3d_join_thread join_thread;

    IUnknown *parent;
    LUID adapter_luid;

    HRESULT removed_reason;
};

HRESULT d3d12_device_create(struct vkd3d_instance *instance,
        const struct vkd3d_device_create_info *create_info, struct d3d12_device **device) DECLSPEC_HIDDEN;
void d3d12_device_mark_as_removed(struct d3d12_device *device, HRESULT reason,
        const char *message, ...) VKD3D_PRINTF_FUNC(3, 4) DECLSPEC_HIDDEN;
struct d3d12_device *unsafe_impl_from_ID3D12Device(ID3D12Device *iface) DECLSPEC_HIDDEN;

HRESULT vkd3d_create_buffer(struct d3d12_device *device,
        const D3D12_HEAP_PROPERTIES *heap_properties, D3D12_HEAP_FLAGS heap_flags,
        const D3D12_RESOURCE_DESC *desc, VkBuffer *vk_buffer) DECLSPEC_HIDDEN;
HRESULT vkd3d_allocate_buffer_memory(struct d3d12_device *device, VkBuffer vk_buffer,
        const D3D12_HEAP_PROPERTIES *heap_properties, D3D12_HEAP_FLAGS heap_flags,
        VkDeviceMemory *vk_memory) DECLSPEC_HIDDEN;

/* utils */
struct vkd3d_format
{
    DXGI_FORMAT dxgi_format;
    VkFormat vk_format;
    size_t byte_count;
    size_t block_width;
    size_t block_height;
    size_t block_byte_count;
    VkImageAspectFlags vk_aspect_mask;
};

static inline bool vkd3d_format_is_compressed(const struct vkd3d_format *format)
{
    return format->block_byte_count != 1;
}

const struct vkd3d_format *vkd3d_get_format(DXGI_FORMAT dxgi_format, bool depth_stencil) DECLSPEC_HIDDEN;

bool dxgi_format_is_typeless(DXGI_FORMAT dxgi_format) DECLSPEC_HIDDEN;

static inline const struct vkd3d_format *vkd3d_format_from_d3d12_resource_desc(
        const D3D12_RESOURCE_DESC *desc, DXGI_FORMAT view_format)
{
    return vkd3d_get_format(view_format ? view_format : desc->Format,
            desc->Flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);
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

enum VkCompareOp vk_compare_op_from_d3d12(D3D12_COMPARISON_FUNC op) DECLSPEC_HIDDEN;

bool is_valid_feature_level(D3D_FEATURE_LEVEL feature_level) DECLSPEC_HIDDEN;
bool check_feature_level_support(D3D_FEATURE_LEVEL feature_level) DECLSPEC_HIDDEN;

bool is_valid_resource_state(D3D12_RESOURCE_STATES state) DECLSPEC_HIDDEN;
bool is_write_resource_state(D3D12_RESOURCE_STATES state) DECLSPEC_HIDDEN;

static inline bool is_cpu_accessible_heap(const struct D3D12_HEAP_PROPERTIES *properties)
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

HRESULT return_interface(IUnknown *iface, REFIID iface_riid,
        REFIID requested_riid, void **object) DECLSPEC_HIDDEN;

const char *debug_vk_extent_3d(VkExtent3D extent) DECLSPEC_HIDDEN;
const char *debug_vk_memory_heap_flags(VkMemoryHeapFlags flags) DECLSPEC_HIDDEN;
const char *debug_vk_memory_property_flags(VkMemoryPropertyFlags flags) DECLSPEC_HIDDEN;
const char *debug_vk_queue_flags(VkQueueFlags flags) DECLSPEC_HIDDEN;

HRESULT hresult_from_vk_result(VkResult vr) DECLSPEC_HIDDEN;
HRESULT hresult_from_vkd3d_result(int vkd3d_result) DECLSPEC_HIDDEN;

HRESULT vkd3d_load_vk_global_procs(struct vkd3d_vk_global_procs *procs,
        PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr) DECLSPEC_HIDDEN;
HRESULT vkd3d_load_vk_instance_procs(struct vkd3d_vk_instance_procs *procs,
        const struct vkd3d_vk_global_procs *global_procs, VkInstance instance) DECLSPEC_HIDDEN;
HRESULT vkd3d_load_vk_device_procs(struct vkd3d_vk_device_procs *procs,
        const struct vkd3d_vk_instance_procs *parent_procs, VkDevice device) DECLSPEC_HIDDEN;

#endif  /* __VKD3D_PRIVATE_H */
