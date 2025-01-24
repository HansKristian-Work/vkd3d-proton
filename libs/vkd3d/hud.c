/*
 * Copyright 2025 Philip Rebohle for Valve Corporation
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

#include "vkd3d_private.h"

#include "hud_font.h"

#include <cs_hud_update_general.h>
#include <fs_hud_render_text.h>
#include <vs_hud_render_text.h>
#include <fs_hud_render_frametimes.h>
#include <vs_hud_render_frametimes.h>

#define VKD3D_HUD_MAX_TIMESTAMP_QUERIES         (256u << 10)

#define VKD3D_HUD_UPLOAD_BUFFER_SIZE_PER_FRAME  (128u << 10)
#define VKD3D_HUD_MAX_BUFFERED_FRAMES           (8u)

#define VKD3D_HUD_DEFAULT_REFRESH_INTERVAL      (500u * 1000000u)
#define VKD3D_HUD_DEFAULT_DISPLAY_FRAME_COUNT   (1u)

#define VKD3D_HUD_TEXT_ALLOCATION_GRANULARITY   (16u)

#define VKD3D_HUD_MAX_PER_QUEUE_SUBMISSIONS     (8u << 10u)
#define VKD3D_HUD_MAX_PER_QUEUE_CMD_BUFFERS     (32u << 10u)
#define VKD3D_HUD_MAX_PER_QUEUE_CMD_REGIONS     (256u << 10u)

enum vkd3d_hud_item
{
    VKD3D_HUD_ITEM_VERSION      = (1u << 0),
    VKD3D_HUD_ITEM_DRIVER       = (1u << 1),
    VKD3D_HUD_ITEM_FPS          = (1u << 2),
    VKD3D_HUD_ITEM_FRAMETIMES   = (1u << 3),
    VKD3D_HUD_ITEM_SUBMISSIONS  = (1u << 4),

    VKD3D_HUD_ITEM_MASK_DEFAULT = VKD3D_HUD_ITEM_VERSION |
                                  VKD3D_HUD_ITEM_DRIVER |
                                  VKD3D_HUD_ITEM_FPS,
    VKD3D_HUD_ITEM_MASK_ALL     = (~0u)
};

struct vkd3d_hud_item_mapping
{
    const char *name;
    enum vkd3d_hud_item item;
}
vkd3d_hud_item_list[] =
{
    { "fps",                    VKD3D_HUD_ITEM_FPS            },
    { "version",                VKD3D_HUD_ITEM_VERSION        },
    { "driver",                 VKD3D_HUD_ITEM_DRIVER         },
    { "frametimes",             VKD3D_HUD_ITEM_FRAMETIMES     },
    { "submissions",            VKD3D_HUD_ITEM_SUBMISSIONS    },

    /* Pre-defined sets, similar to DXVK_HUD */
    { "1",                      VKD3D_HUD_ITEM_MASK_DEFAULT   },
    { "full",                   VKD3D_HUD_ITEM_MASK_ALL       },
};

struct vkd3d_hud_buffer
{
    VkBuffer vk_buffer;
    uint64_t va;
    void *host_ptr;
    struct vkd3d_device_memory_allocation memory;
};

enum vkd3d_hud_submission_type
{
    VKD3D_HUD_SUBMISSION_TYPE_UNKNOWN           = 0u,
    VKD3D_HUD_SUBMISSION_TYPE_EXECUTE_COMMANDS  = 1u,
    VKD3D_HUD_SUBMISSION_TYPE_SPARSE_BIND       = 2u,
    VKD3D_HUD_SUBMISSION_TYPE_SIGNAL            = 3u,
    VKD3D_HUD_SUBMISSION_TYPE_WAIT              = 4u,
    VKD3D_HUD_SUBMISSION_TYPE_PRESENT           = 5u,
};

enum vkd3d_hud_cmd_buffer_region
{
    VKD3D_CMD_BUFFER_REGION_OTHER               =   0u,
    VKD3D_CMD_BUFFER_REGION_PRESENT             =   1u,
    VKD3D_CMD_BUFFER_REGION_GRAPHICS            =   2u,
    VKD3D_CMD_BUFFER_REGION_COMPUTE             =   3u,
    VKD3D_CMD_BUFFER_REGION_RAY_TRACING         =   4u,
    VKD3D_CMD_BUFFER_REGION_RTAS_UPDATE         =   5u,
    VKD3D_CMD_BUFFER_REGION_WORK_GRAPHS         =   6u,
};

struct vkd3d_hud_queue_submission_info
{
    uint16_t type;
    uint16_t cmd_buffer_count;
    uint32_t cmd_buffer_index;
    uint64_t start_time;
    uint32_t start_frame;
    uint32_t submit_begin_delta;
    uint32_t submit_end_delta;
    uint32_t execute_end_delta;
};

struct vkd3d_hud_gpu_cmd_buffer_region
{
    uint32_t type;
    uint32_t timestamp_delta;
};

struct vkd3d_hud_gpu_cmd_buffer_info
{
    /* Start and end time of the command buffer */
    uint64_t gpu_start_time;
    uint64_t gpu_end_time;
    uint32_t region_index;
    uint32_t region_count;
};

struct vkd3d_hud_queue_gpu_buffer_layout
{
    struct vkd3d_hud_queue_submission_info submissions[VKD3D_HUD_MAX_PER_QUEUE_SUBMISSIONS];
    struct vkd3d_hud_gpu_cmd_buffer_info cmd_buffers[VKD3D_HUD_MAX_PER_QUEUE_CMD_BUFFERS];
    /* This will not be present for pure transfer queues, must therefore be last */
    struct vkd3d_hud_gpu_cmd_buffer_region cmd_regions[VKD3D_HUD_MAX_PER_QUEUE_CMD_REGIONS];
};

struct vkd3d_hud_queue_submission_stats
{
    uint32_t submission_count;
    uint32_t cmd_buffer_count;
};

struct vkd3d_hud_queue_info
{
    struct d3d12_command_queue *queue;

    spinlock_t mutex;

    struct vkd3d_hud_buffer gpu_buffer;

    /* Timestamp and ID of last completed submission */
    uint64_t last_submission_time_ns;
    uint32_t last_submission_id;

    /* Last frame when GPU buffers were accessed */
    uint32_t last_latch_update_count;
    uint32_t last_latch_submission_id;

    /* Linear allocators */
    uint32_t next_submission;
    uint32_t next_cmd_buffer;

    uint32_t last_update_submission_count;
    uint32_t last_update_cmd_buffer_count;

    struct vkd3d_hud_queue_submission_stats stats;
    struct vkd3d_hud_queue_submission_stats display_stats;

    /* Queue submission info to upload */
    struct vkd3d_hud_queue_submission_info submissions[VKD3D_HUD_MAX_PER_QUEUE_SUBMISSIONS];
};

struct vkd3d_hud_queue_tracked_submission
{
    struct vkd3d_hud_queue_info *queue;
    uint32_t submission_id;
    uint32_t cmd_buffer_id;
};

struct vkd3d_hud_glyph_info
{
    uint16_t x;
    uint16_t y;
    uint8_t  w;
    uint8_t  h;
    uint8_t  origin_x;
    uint8_t  origin_y;
};

#define VKD3D_HUD_FONT_MAX_GLYPHS (127u)

struct vkd3d_hud_font_buffer
{
    struct vkd3d_hud_font_info font_metadata;
    struct vkd3d_hud_glyph_info glyph_lut[VKD3D_HUD_FONT_MAX_GLYPHS];
};

#define VKD3D_HUD_MAX_FRAMETIME_RECORDS           (420u)

struct vkd3d_hud_stat_buffer
{
    /* Ring buffer of current and previous frame update timestamps */
    uint64_t update_timestamps[VKD3D_HUD_MAX_BUFFERED_FRAMES];
    /* Last recorded refresh timestamp. */
    uint64_t refresh_timestamp;
    /* Frames per second, updated once per refresh */
    float display_fps;
    /* Past recorded frametimes, in millisecond */
    float avg_frametime_ms;
    float max_frametime_ms;
    float frame_intervals_ms[VKD3D_HUD_MAX_FRAMETIME_RECORDS];
};

#define VKD3D_HUD_MAX_TEXT_DRAWS (512u)
#define VKD3D_HUD_MAX_TEXT_CHARS (1023u * VKD3D_HUD_TEXT_ALLOCATION_GRANULARITY)

struct vkd3d_hud_text_draw_info
{
    int16_t x;
    int16_t y;
    uint16_t size;
    uint16_t text_length;
    uint32_t text_offset;
    uint32_t color;
};

struct vkd3d_hud_text_buffer
{
    unsigned char text[VKD3D_HUD_MAX_TEXT_CHARS];

    uint32_t char_count;
    uint32_t draw_count;
    uint32_t reserved[2];

    VkDrawIndirectCommand draw_commands[VKD3D_HUD_MAX_TEXT_DRAWS];
    struct vkd3d_hud_text_draw_info draw_infos[VKD3D_HUD_MAX_TEXT_DRAWS];
};

struct vkd3d_hud_data_buffer_layout
{
    struct vkd3d_hud_font_buffer font_metadata;
    struct vkd3d_hud_text_buffer text;
    struct vkd3d_hud_stat_buffer stats;
};

struct vkd3d_hud_cs_update_args
{
    uint64_t data_buffer_va;
    uint32_t update_count;
    uint32_t refresh_count;
    uint32_t enabled_items;
    float gpu_ms_per_tick;
    uint32_t fps_draw_index;
    uint32_t frametime_draw_index;
};

struct vkd3d_hud_render_text_args
{
    uint64_t data_buffer_va;
    VkExtent2D swapchain_extent;
};

struct vkd3d_hud_render_frametime_graph_args
{
    uint64_t data_buffer_va;
    VkExtent2D swapchain_extent;
    VkRect2D graph_rect;
    uint32_t frame_index;
    uint32_t reserved;
};

struct vkd3d_hud_graphics_pipelines_key
{
    VkFormat rt_format;
    VkColorSpaceKHR rt_color_space;
};

struct vkd3d_hud_graphics_pipelines
{
    struct vkd3d_hud_graphics_pipelines_key key;

    VkPipeline render_text_pso;
    VkPipeline render_frametime_graph_pso;
};

struct vkd3d_hud_context
{
    VkOffset2D top_left;
    VkOffset2D bottom_left;
    VkOffset2D bottom_right;
};

struct vkd3d_hud
{
    struct d3d12_device *device;

    uint32_t item_mask;

    float gpu_ms_per_tick;

    struct vkd3d_hud_buffer cpu_buffer;
    struct vkd3d_hud_buffer gpu_buffer;

    VkDeviceSize cpu_buffer_offset;

    struct
    {
        VkImage image;
        VkImageView image_view;
        VkSampler sampler;

        struct vkd3d_device_memory_allocation memory;
    } font;

    struct
    {
        VkQueryPool query_pool;

        UINT64 allocator[VKD3D_HUD_MAX_TIMESTAMP_QUERIES / 64u];
        UINT32 allocator_next_hint;
    } timestamp_queries;

    struct
    {
        struct vkd3d_hud_graphics_pipelines *pipelines;
        size_t capacity;
        size_t count;

        VkDescriptorSetLayout render_text_set_layout;
        VkPipelineLayout render_text_pso_layout;
        VkPipelineLayout render_frametime_graph_pso_layout;
    } graphics_psos;

    struct
    {
        VkPipelineLayout update_pso_layout;
        VkPipeline update_pso;
    } compute_psos;

    struct
    {
        uint32_t next_cookie;
        uint32_t active_cookie;

        struct vkd3d_timeline_semaphore vk_semaphores[VKD3D_HUD_MAX_BUFFERED_FRAMES];
    } swapchain;

    struct
    {
        pthread_mutex_t mutex;

        struct vkd3d_hud_queue_info **queue_infos;
        size_t queue_infos_size;
        size_t queue_info_count;

        struct vkd3d_hud_queue_info **destroyed_queues;
        size_t destroyed_queues_size;
        size_t destroyed_queue_count;

        struct vkd3d_hud_queue_tracked_submission submission_infos[VKD3D_TIMELINE_TRACE_NUM_ENTRIES];
    } queues;

    uint32_t update_count;
    uint32_t update_timestamp_index;

    uint32_t refresh_count;
    uint64_t refresh_time;

    VkRect2D frametime_graph;
};

static int32_t vkd3d_hud_allocate_timestamp_query(struct vkd3d_hud *hud)
{
    uint32_t i, mask_index, bit_index;
    uint64_t mask, new_mask;

    mask_index = vkd3d_atomic_uint32_load_explicit(&hud->timestamp_queries.allocator_next_hint, vkd3d_memory_order_relaxed);

    for (i = 0; i < ARRAY_SIZE(hud->timestamp_queries.allocator); i++)
    {
        mask = vkd3d_atomic_uint64_load_explicit(&hud->timestamp_queries.allocator[mask_index], vkd3d_memory_order_relaxed);

        while (true)
        {
            do
            {
                bit_index = vkd3d_bitmask_tzcnt64(~mask);
            }
            while (bit_index < 64u && (mask & (1ull << bit_index)));

            /* Current mask is full */
            if (bit_index >= 64)
              break;

            new_mask = vkd3d_atomic_uint64_compare_exchange(&hud->timestamp_queries.allocator[mask_index],
                    mask, mask | (1ull << bit_index), vkd3d_memory_order_relaxed, vkd3d_memory_order_relaxed);

            if (new_mask == mask)
            {
                /* This is just a perf hint and not essential for correct operation */
                vkd3d_atomic_uint32_store_explicit(&hud->timestamp_queries.allocator_next_hint,
                        mask_index, vkd3d_memory_order_relaxed);
                return 64u * mask_index + bit_index;
            }

            mask = new_mask;
        }

        mask_index += 1;
        mask_index %= ARRAY_SIZE(hud->timestamp_queries.allocator);
    }

    ERR("Failed to allocate timestamp query.\n");
    return -1;
}

static bool vkd3d_hud_init_font(struct vkd3d_hud *hud)
{
    const struct vkd3d_vk_device_procs *vk_procs = &hud->device->vk_procs;
    VkImageViewUsageCreateInfo usage_info;
    VkSamplerCreateInfo sampler_info;
    VkImageViewCreateInfo view_info;
    VkImageCreateInfo image_info;
    VkResult vr;
    HRESULT hr;

    memset(&image_info, 0, sizeof(image_info));
    image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image_info.imageType = VK_IMAGE_TYPE_2D;
    image_info.format = VK_FORMAT_R8_UNORM;
    image_info.extent.width = vkd3d_hud_font_metadata.width;
    image_info.extent.height = vkd3d_hud_font_metadata.height;
    image_info.extent.depth = 1;
    image_info.mipLevels = 1;
    image_info.arrayLayers = 1;
    image_info.samples = VK_SAMPLE_COUNT_1_BIT;
    image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    image_info.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    if ((vr = VK_CALL(vkCreateImage(hud->device->vk_device, &image_info, NULL, &hud->font.image))))
    {
        ERR("Failed to create font texture, vr %d.\n", vr);
        return false;
    }

    if (FAILED(hr = vkd3d_allocate_internal_image_memory(hud->device, hud->font.image,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &hud->font.memory)))
    {
        ERR("Failed to allocate memory for font texture, vr %d.\n", vr);
        return false;
    }

    memset(&usage_info, 0, sizeof(usage_info));
    usage_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_USAGE_CREATE_INFO;
    usage_info.usage = VK_IMAGE_USAGE_SAMPLED_BIT;

    memset(&view_info, 0, sizeof(view_info));
    view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_info.pNext = &usage_info;
    view_info.image = hud->font.image;
    view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_info.format = image_info.format;
    view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    view_info.subresourceRange.layerCount = 1;
    view_info.subresourceRange.levelCount = 1;

    if ((vr = VK_CALL(vkCreateImageView(hud->device->vk_device, &view_info, NULL, &hud->font.image_view))))
    {
        ERR("Failed to create font image view, vr %d.\n", vr);
        return false;
    }

    memset(&sampler_info, 0, sizeof(sampler_info));
    sampler_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sampler_info.minFilter = VK_FILTER_LINEAR;
    sampler_info.magFilter = VK_FILTER_LINEAR;
    sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    sampler_info.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
    sampler_info.unnormalizedCoordinates = VK_TRUE;

    if ((vr = VK_CALL(vkCreateSampler(hud->device->vk_device, &sampler_info, NULL, &hud->font.sampler))))
    {
        ERR("Failed to create font sampler, vr %d.\n", vr);
        return false;
    }

    return true;
}

static bool vkd3d_hud_init_timestamp_queries(struct vkd3d_hud *hud)
{
    const struct vkd3d_vk_device_procs *vk_procs = &hud->device->vk_procs;
    VkQueryPoolCreateInfo pool_info;
    VkResult vr;

    memset(&pool_info, 0, sizeof(pool_info));
    pool_info.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
    pool_info.queryCount = VKD3D_HUD_MAX_TIMESTAMP_QUERIES;
    pool_info.queryType = VK_QUERY_TYPE_TIMESTAMP;

    if ((vr = VK_CALL(vkCreateQueryPool(hud->device->vk_device, &pool_info,
            NULL, &hud->timestamp_queries.query_pool))))
        ERR("Failed to create timestamp query pool, vr %d.\n", vr);

    return vr == VK_SUCCESS;
}

static bool vkd3d_hud_init_buffer(struct vkd3d_hud *hud, struct vkd3d_hud_buffer *buffer,
        VkDeviceSize size, bool host_visible, const char *debug_name)
{
    const struct vkd3d_vk_device_procs *vk_procs = &hud->device->vk_procs;
    VkMemoryPropertyFlags memory_properties;
    D3D12_HEAP_PROPERTIES heap_properties;
    D3D12_RESOURCE_DESC1 resource_desc;
    VkResult vr;

    memset(&heap_properties, 0, sizeof(heap_properties));
    heap_properties.Type = D3D12_HEAP_TYPE_DEFAULT;

    if (host_visible)
    {
        heap_properties.Type = hud->device->memory_info.has_gpu_upload_heap ?
                D3D12_HEAP_TYPE_GPU_UPLOAD : D3D12_HEAP_TYPE_UPLOAD;
    }

    memset(&resource_desc, 0, sizeof(resource_desc));
    resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    resource_desc.Width = size;
    resource_desc.Height = 1;
    resource_desc.DepthOrArraySize = 1;
    resource_desc.MipLevels = 1;
    resource_desc.Format = DXGI_FORMAT_UNKNOWN;
    resource_desc.SampleDesc.Count = 1;
    resource_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    resource_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    if (FAILED(vkd3d_create_buffer(hud->device, &heap_properties,
            D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS, &resource_desc, debug_name, &buffer->vk_buffer)))
        return false;

    memory_properties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

    if (host_visible)
        memory_properties |= VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

    if (FAILED(vkd3d_allocate_internal_buffer_memory(hud->device, buffer->vk_buffer,
            memory_properties, &buffer->memory)))
        return false;

    buffer->va = vkd3d_get_buffer_device_address(hud->device, buffer->vk_buffer);

    if (host_visible)
    {
        if ((vr = VK_CALL(vkMapMemory(hud->device->vk_device,
                buffer->memory.vk_memory, 0, VK_WHOLE_SIZE, 0, (void**)&buffer->host_ptr))))
        {
            ERR("Failed to map memory, vr %d.\n", vr);
            return false;
        }
    }

    return true;
}

static bool vkd3d_hud_create_compute_pipeline(struct vkd3d_hud *hud, const void *code,
        size_t code_size, size_t push_constant_size, VkPipeline *pso, VkPipelineLayout *layout)
{
    const struct vkd3d_vk_device_procs *vk_procs = &hud->device->vk_procs;
    VkPipelineLayoutCreateInfo layout_info;
    VkComputePipelineCreateInfo pso_info;
    VkPushConstantRange push_info;
    VkResult vr;
    HRESULT hr;

    if (!(*layout))
    {
        memset(&push_info, 0, sizeof(push_info));
        push_info.size = push_constant_size;
        push_info.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        memset(&layout_info, 0, sizeof(layout_info));
        layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layout_info.pushConstantRangeCount = 1;
        layout_info.pPushConstantRanges = &push_info;

        if ((vr = VK_CALL(vkCreatePipelineLayout(hud->device->vk_device, &layout_info, NULL, layout))))
        {
            ERR("Failed to create compute pipeline layout, vr %d.\n", vr);
            return false;
        }
    }

    memset(&pso_info, 0, sizeof(pso_info));
    pso_info.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pso_info.layout = *layout;
    pso_info.basePipelineIndex = -1;

    pso_info.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    pso_info.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    pso_info.stage.pName = "main";

    if (FAILED(hr = vkd3d_meta_create_shader_module(hud->device, code, code_size, &pso_info.stage.module)))
    {
        ERR("Failed to create compute shader module, hr %#x.\n", hr);
        return false;
    }

    vr = VK_CALL(vkCreateComputePipelines(hud->device->vk_device,
            VK_NULL_HANDLE, 1, &pso_info, NULL, pso));

    VK_CALL(vkDestroyShaderModule(hud->device->vk_device, pso_info.stage.module, NULL));

    if (vr)
    {
        ERR("Failed to create compute pipeline, vr %d.\n", vr);
        return false;
    }

    return true;
}

static bool vkd3d_hud_init_compute_pipelines(struct vkd3d_hud *hud)
{
    return vkd3d_hud_create_compute_pipeline(hud,
            cs_hud_update_general, sizeof(cs_hud_update_general),
            sizeof(struct vkd3d_hud_cs_update_args),
            &hud->compute_psos.update_pso,
            &hud->compute_psos.update_pso_layout);
}

static bool vkd3d_hud_init_graphics_pipeline_layouts(struct vkd3d_hud *hud)
{
    const struct vkd3d_vk_device_procs *vk_procs = &hud->device->vk_procs;
    VkDescriptorSetLayoutBinding font_texture_binding;
    VkDescriptorSetLayoutCreateInfo set_layout_info;
    VkPipelineLayoutCreateInfo pipeline_layout_info;
    VkPushConstantRange push_info;
    VkResult vr;

    memset(&push_info, 0, sizeof(push_info));
    push_info.size = sizeof(struct vkd3d_hud_render_text_args);
    push_info.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    memset(&font_texture_binding, 0, sizeof(font_texture_binding));
    font_texture_binding.descriptorCount = 1;
    font_texture_binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    font_texture_binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    memset(&set_layout_info, 0, sizeof(set_layout_info));
    set_layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    set_layout_info.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR;
    set_layout_info.bindingCount = 1;
    set_layout_info.pBindings = &font_texture_binding;

    if ((vr = VK_CALL(vkCreateDescriptorSetLayout(hud->device->vk_device,
            &set_layout_info, NULL, &hud->graphics_psos.render_text_set_layout))))
    {
        ERR("Failed to create descriptor set layout, vr %d.\n");
        return false;
    }

    memset(&pipeline_layout_info, 0, sizeof(pipeline_layout_info));
    pipeline_layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipeline_layout_info.setLayoutCount = 1;
    pipeline_layout_info.pSetLayouts = &hud->graphics_psos.render_text_set_layout;
    pipeline_layout_info.pushConstantRangeCount = 1;
    pipeline_layout_info.pPushConstantRanges = &push_info;

    if ((vr = VK_CALL(vkCreatePipelineLayout(hud->device->vk_device,
            &pipeline_layout_info, NULL, &hud->graphics_psos.render_text_pso_layout))))
    {
        ERR("Failed to create descriptor set layout, vr %d.\n");
        return false;
    }

    memset(&push_info, 0, sizeof(push_info));
    push_info.size = sizeof(struct vkd3d_hud_render_frametime_graph_args);
    push_info.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    memset(&pipeline_layout_info, 0, sizeof(pipeline_layout_info));
    pipeline_layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipeline_layout_info.pushConstantRangeCount = 1;
    pipeline_layout_info.pPushConstantRanges = &push_info;

    if ((vr = VK_CALL(vkCreatePipelineLayout(hud->device->vk_device,
            &pipeline_layout_info, NULL, &hud->graphics_psos.render_frametime_graph_pso_layout))))
    {
        ERR("Failed to create descriptor set layout, vr %d.\n");
        return false;
    }

    return true;
}

static bool vkd3d_hud_init(struct vkd3d_hud *hud, struct d3d12_device *device, uint32_t item_mask)
{
    hud->device = device;
    hud->item_mask = item_mask;

    hud->gpu_ms_per_tick = (float)(hud->device->vk_info.device_limits.timestampPeriod) / 1000000.0f;

    pthread_mutex_init(&hud->queues.mutex, NULL);

    if (!vkd3d_hud_init_font(hud))
        return false;

    if (!vkd3d_hud_init_timestamp_queries(hud))
        return false;

    if (!vkd3d_hud_init_buffer(hud, &hud->gpu_buffer, sizeof(struct vkd3d_hud_data_buffer_layout),
            false, "hud-data-buffer"))
        return false;

    if (!vkd3d_hud_init_buffer(hud, &hud->cpu_buffer, VKD3D_HUD_UPLOAD_BUFFER_SIZE_PER_FRAME * VKD3D_HUD_MAX_BUFFERED_FRAMES,
            true, "hud-upload-buffer"))
        return false;

    if (!vkd3d_hud_init_compute_pipelines(hud))
        return false;

    if (!vkd3d_hud_init_graphics_pipeline_layouts(hud))
        return false;

    hud->update_timestamp_index = vkd3d_hud_allocate_timestamp_query(hud);
    return true;
}

static void vkd3d_hud_free_graphics_pipelines(struct vkd3d_hud *hud, struct vkd3d_hud_graphics_pipelines *pipelines)
{
    const struct vkd3d_vk_device_procs *vk_procs = &hud->device->vk_procs;

    VK_CALL(vkDestroyPipeline(hud->device->vk_device, pipelines->render_text_pso, NULL));
    VK_CALL(vkDestroyPipeline(hud->device->vk_device, pipelines->render_frametime_graph_pso, NULL));
}

static bool vkd3d_hud_create_graphics_pipeline(struct vkd3d_hud *hud, const struct vkd3d_hud_graphics_pipelines_key *key,
        const void *vs, size_t vs_code_size, const void *fs, size_t fs_code_size, VkPrimitiveTopology primitive_topology,
        VkPipelineLayout layout, VkPipeline *pso)
{
    const struct vkd3d_vk_device_procs *vk_procs = &hud->device->vk_procs;
    VkPipelineColorBlendAttachmentState blend_attachment;
    VkPipelineInputAssemblyStateCreateInfo ia_state;
    VkPipelineRasterizationStateCreateInfo rs_state;
    VkPipelineVertexInputStateCreateInfo vi_state;
    VkPipelineMultisampleStateCreateInfo ms_state;
    VkPipelineColorBlendStateCreateInfo cb_state;
    VkPipelineViewportStateCreateInfo vp_state;
    VkPipelineDynamicStateCreateInfo dyn_state;
    VkGraphicsPipelineCreateInfo pipeline_info;
    VkPipelineShaderStageCreateInfo stages[2];
    VkPipelineRenderingCreateInfo rt_info;
    VkSpecializationInfo spec_info;
    unsigned int i;
    VkResult vr;
    HRESULT hr;

    static const VkSpecializationMapEntry spec_map[] =
    {
        { 0, offsetof(struct vkd3d_hud_graphics_pipelines_key, rt_format),      sizeof(VkFormat)        },
        { 1, offsetof(struct vkd3d_hud_graphics_pipelines_key, rt_color_space), sizeof(VkColorSpaceKHR) },
    };

    static const VkDynamicState dynamic_states[] =
    {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR,
    };

    static const uint32_t sample_mask = 0x1;

    memset(&spec_info, 0, sizeof(spec_info));
    spec_info.mapEntryCount = ARRAY_SIZE(spec_map);
    spec_info.pMapEntries = spec_map;
    spec_info.dataSize = sizeof(*key);
    spec_info.pData = key;

    memset(stages, 0, sizeof(stages));

    for (i = 0; i < ARRAY_SIZE(stages); i++)
    {
        stages[i].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[i].pName = "main";
        stages[i].pSpecializationInfo = &spec_info;
    }

    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;

    if (FAILED(hr = vkd3d_meta_create_shader_module(hud->device, vs, vs_code_size, &stages[0].module)) ||
            FAILED(hr = vkd3d_meta_create_shader_module(hud->device, fs, fs_code_size, &stages[1].module)))
    {
        ERR("Failed to create shader modules, hr %#x.\n", hr);
        goto fail_module;
    }

    memset(&ia_state, 0, sizeof(ia_state));
    ia_state.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia_state.topology = primitive_topology;

    memset(&rs_state, 0, sizeof(rs_state));
    rs_state.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rs_state.polygonMode = VK_POLYGON_MODE_FILL;
    rs_state.cullMode = VK_CULL_MODE_NONE;
    rs_state.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs_state.lineWidth = 1.0f;

    memset(&vi_state, 0, sizeof(vi_state));
    vi_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    memset(&ms_state, 0, sizeof(ms_state));
    ms_state.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms_state.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    ms_state.pSampleMask = &sample_mask;

    memset(&blend_attachment, 0, sizeof(blend_attachment));
    blend_attachment.blendEnable = VK_TRUE;
    blend_attachment.colorBlendOp = VK_BLEND_OP_ADD;
    blend_attachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    blend_attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blend_attachment.alphaBlendOp = VK_BLEND_OP_ADD;
    blend_attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blend_attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blend_attachment.colorWriteMask =
            VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
            VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    memset(&cb_state, 0, sizeof(cb_state));
    cb_state.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cb_state.attachmentCount = 1;
    cb_state.pAttachments = &blend_attachment;

    memset(&vp_state, 0, sizeof(vp_state));
    vp_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vp_state.viewportCount = 1;
    vp_state.scissorCount = 1;

    memset(&dyn_state, 0, sizeof(dyn_state));
    dyn_state.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dyn_state.dynamicStateCount = ARRAY_SIZE(dynamic_states);
    dyn_state.pDynamicStates = dynamic_states;

    memset(&rt_info, 0, sizeof(rt_info));
    rt_info.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR;
    rt_info.colorAttachmentCount = 1;
    rt_info.pColorAttachmentFormats = &key->rt_format;

    memset(&pipeline_info, 0, sizeof(pipeline_info));
    pipeline_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipeline_info.pNext = &rt_info;
    pipeline_info.stageCount = ARRAY_SIZE(stages);
    pipeline_info.pStages = stages;
    pipeline_info.pInputAssemblyState = &ia_state;
    pipeline_info.pRasterizationState = &rs_state;
    pipeline_info.pVertexInputState = &vi_state;
    pipeline_info.pMultisampleState = &ms_state;
    pipeline_info.pColorBlendState = &cb_state;
    pipeline_info.pViewportState = &vp_state;
    pipeline_info.pDynamicState = &dyn_state;
    pipeline_info.layout = layout;
    pipeline_info.basePipelineIndex = -1;

    vr = VK_CALL(vkCreateGraphicsPipelines(hud->device->vk_device,
            VK_NULL_HANDLE, 1, &pipeline_info, NULL, pso));

    for (i = 0; i < ARRAY_SIZE(stages); i++)
        VK_CALL(vkDestroyShaderModule(hud->device->vk_device, stages[i].module, NULL));

    if (vr)
    {
        ERR("Failed to create graphics pipeline, vr %d.\n", vr);
        return false;
    }

    return true;

fail_module:
    for (i = 0; i < ARRAY_SIZE(stages); i++)
        VK_CALL(vkDestroyShaderModule(hud->device->vk_device, stages[i].module, NULL));
    return false;
}

static bool vkd3d_hud_init_graphics_pipelines(struct vkd3d_hud *hud, struct vkd3d_hud_graphics_pipelines *pipelines,
        const struct vkd3d_hud_graphics_pipelines_key *key)
{
    memset(pipelines, 0, sizeof(*pipelines));
    pipelines->key = *key;

    if (!vkd3d_hud_create_graphics_pipeline(hud, key,
            vs_hud_render_text, sizeof(vs_hud_render_text),
            fs_hud_render_text, sizeof(fs_hud_render_text),
            VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, hud->graphics_psos.render_text_pso_layout,
            &pipelines->render_text_pso))
        goto fail;

    if (!vkd3d_hud_create_graphics_pipeline(hud, key,
            vs_hud_render_frametimes, sizeof(vs_hud_render_frametimes),
            fs_hud_render_frametimes, sizeof(fs_hud_render_frametimes),
            VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP, hud->graphics_psos.render_frametime_graph_pso_layout,
            &pipelines->render_frametime_graph_pso))
        goto fail;

    return true;

fail:
    vkd3d_hud_free_graphics_pipelines(hud, pipelines);
    return false;
}

static void vkd3d_hud_destroy_queue_info(struct vkd3d_hud *hud, struct vkd3d_hud_queue_info *queue_info)
{
    const struct vkd3d_vk_device_procs *vk_procs = &hud->device->vk_procs;

    VK_CALL(vkDestroyBuffer(hud->device->vk_device, queue_info->gpu_buffer.vk_buffer, NULL));

    vkd3d_free_device_memory(hud->device, &queue_info->gpu_buffer.memory);
    vkd3d_free(queue_info);
}

void vkd3d_hud_destroy(struct vkd3d_hud *hud)
{
    const struct vkd3d_vk_device_procs *vk_procs = &hud->device->vk_procs;
    size_t i;

    TRACE("hud %p.\n", hud);

    /* All virtual queues and swapchains are destroyed at this point,
     * so there cannot be any pending GPU work accessing resources */
    for (i = 0; i < hud->queues.destroyed_queue_count; i++)
        vkd3d_hud_destroy_queue_info(hud, hud->queues.destroyed_queues[i]);

    pthread_mutex_destroy(&hud->queues.mutex);

    VK_CALL(vkDestroyImage(hud->device->vk_device, hud->font.image, NULL));
    VK_CALL(vkDestroyImageView(hud->device->vk_device, hud->font.image_view, NULL));
    VK_CALL(vkDestroySampler(hud->device->vk_device, hud->font.sampler, NULL));

    VK_CALL(vkDestroyBuffer(hud->device->vk_device, hud->cpu_buffer.vk_buffer, NULL));
    VK_CALL(vkDestroyBuffer(hud->device->vk_device, hud->gpu_buffer.vk_buffer, NULL));

    VK_CALL(vkDestroyQueryPool(hud->device->vk_device, hud->timestamp_queries.query_pool, NULL));

    VK_CALL(vkDestroyDescriptorSetLayout(hud->device->vk_device, hud->graphics_psos.render_text_set_layout, NULL));

    VK_CALL(vkDestroyPipelineLayout(hud->device->vk_device, hud->graphics_psos.render_text_pso_layout, NULL));
    VK_CALL(vkDestroyPipelineLayout(hud->device->vk_device, hud->graphics_psos.render_frametime_graph_pso_layout, NULL));
    VK_CALL(vkDestroyPipelineLayout(hud->device->vk_device, hud->compute_psos.update_pso_layout, NULL));

    VK_CALL(vkDestroyPipeline(hud->device->vk_device, hud->compute_psos.update_pso, NULL));

    vkd3d_free_device_memory(hud->device, &hud->font.memory);

    vkd3d_free_device_memory(hud->device, &hud->gpu_buffer.memory);
    vkd3d_free_device_memory(hud->device, &hud->cpu_buffer.memory);

    for (i = 0; i < hud->graphics_psos.count; i++)
        vkd3d_hud_free_graphics_pipelines(hud, &hud->graphics_psos.pipelines[i]);

    vkd3d_free(hud->graphics_psos.pipelines);
    vkd3d_free(hud->queues.queue_infos);
    vkd3d_free(hud->queues.destroyed_queues);
    vkd3d_free(hud);
}

struct vkd3d_hud *vkd3d_hud_create(struct d3d12_device *device)
{
    char env[VKD3D_PATH_MAX];
    uint32_t item_mask = 0u;
    struct vkd3d_hud *hud;
    unsigned int i;

    TRACE("device %p.\n", device);

    if (!vkd3d_get_env_var("VKD3D_HUD", env, sizeof(env)))
        return NULL;

    for (i = 0; i < ARRAY_SIZE(vkd3d_hud_item_list); i++)
    {
        if (vkd3d_debug_list_has_member(env, vkd3d_hud_item_list[i].name))
            item_mask |= vkd3d_hud_item_list[i].item;
    }

    if (!item_mask)
        return NULL;

    hud = vkd3d_calloc(1, sizeof(*hud));

    /* Errors here should not be fatal */
    if (!vkd3d_hud_init(hud, device, item_mask))
    {
        vkd3d_hud_destroy(hud);
        vkd3d_free(hud);
        return NULL;
    }

    return hud;
}

static const struct vkd3d_hud_graphics_pipelines *vkd3d_hud_get_graphics_pipelines(
        struct vkd3d_hud *hud, const struct vkd3d_hud_graphics_pipelines_key *key)
{
    for (size_t i = 0; i < hud->graphics_psos.count; i++)
    {
        if (!memcmp(key, &hud->graphics_psos.pipelines[i].key, sizeof(*key)))
            return &hud->graphics_psos.pipelines[i];
    }

    if (!vkd3d_array_reserve((void**)&hud->graphics_psos.pipelines, &hud->graphics_psos.capacity,
            hud->graphics_psos.count + 1u, sizeof(*hud->graphics_psos.pipelines)))
    {
        ERR("Failed to reserve storage for graphics PSOs.\n");
        return NULL;
    }

    if (!vkd3d_hud_init_graphics_pipelines(hud, &hud->graphics_psos.pipelines[hud->graphics_psos.count], key))
    {
          ERR("Failed to create graphics PSO for format %u, color space %u.\n",
                  key->rt_format, key->rt_color_space);
          return NULL;
    }

    return &hud->graphics_psos.pipelines[hud->graphics_psos.count++];
}

static bool vkd3d_hud_alloc_upload_buffer(struct vkd3d_hud *hud, size_t size, struct vkd3d_scratch_allocation *scratch)
{
    uint32_t frame_index = hud->update_count % VKD3D_HUD_MAX_BUFFERED_FRAMES;
    size_t max_offset = VKD3D_HUD_UPLOAD_BUFFER_SIZE_PER_FRAME;

    memset(scratch, 0, sizeof(*scratch));

    max_offset = hud->update_count
        ? VKD3D_HUD_UPLOAD_BUFFER_SIZE_PER_FRAME * (frame_index + 1u)
        : VKD3D_HUD_UPLOAD_BUFFER_SIZE_PER_FRAME * VKD3D_HUD_MAX_BUFFERED_FRAMES;

    size = align(size, 64u);

    if (hud->cpu_buffer_offset + size > max_offset)
        return false;

    scratch->buffer = hud->cpu_buffer.vk_buffer;
    scratch->offset = hud->cpu_buffer_offset;
    scratch->va = hud->cpu_buffer.va + scratch->offset;
    scratch->host_ptr = void_ptr_offset(hud->cpu_buffer.host_ptr, scratch->offset);

    hud->cpu_buffer_offset += size;
    return true;
}

static void vkd3d_hud_upload_font(struct vkd3d_hud *hud, VkCommandBuffer cmd_buffer)
{
    const struct vkd3d_vk_device_procs *vk_procs = &hud->device->vk_procs;
    VkCopyBufferToImageInfo2 upload_image_info;
    struct vkd3d_scratch_allocation scratch;
    const struct vkd3d_hud_glyph *src_glyph;
    VkBufferImageCopy2 upload_image_region;
    struct vkd3d_hud_font_buffer font_data;
    struct vkd3d_hud_glyph_info *dst_glyph;
    VkImageMemoryBarrier2 image_barrier;
    VkMemoryBarrier2 memory_barrier;
    VkDependencyInfo dep_info;
    unsigned int i;

    if (!vkd3d_hud_alloc_upload_buffer(hud, sizeof(vkd3d_hud_font_texture), &scratch))
    {
        ERR("Failed to allocate storage to upload font texture.\n");
        return;
    }

    memcpy(scratch.host_ptr, vkd3d_hud_font_texture, sizeof(vkd3d_hud_font_texture));

    memset(&font_data, 0, sizeof(font_data));
    memcpy(&font_data.font_metadata, &vkd3d_hud_font_metadata, sizeof(font_data.font_metadata));

    for (i = 0; i < ARRAY_SIZE(vkd3d_hud_glyph_metadata); i++)
    {
        src_glyph = &vkd3d_hud_glyph_metadata[i];
        dst_glyph = &font_data.glyph_lut[src_glyph->code_point];

        dst_glyph->x = src_glyph->x;
        dst_glyph->y = src_glyph->y;
        dst_glyph->w = src_glyph->w;
        dst_glyph->h = src_glyph->h;
        dst_glyph->origin_x = src_glyph->origin_x;
        dst_glyph->origin_y = src_glyph->origin_y;
    }

    memset(&image_barrier, 0, sizeof(image_barrier));
    image_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    image_barrier.dstStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
    image_barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    image_barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    image_barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    image_barrier.image = hud->font.image;
    image_barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    image_barrier.subresourceRange.layerCount = 1u;
    image_barrier.subresourceRange.levelCount = 1u;

    memset(&dep_info, 0, sizeof(dep_info));
    dep_info.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dep_info.imageMemoryBarrierCount = 1u;
    dep_info.pImageMemoryBarriers = &image_barrier;

    VK_CALL(vkCmdPipelineBarrier2(cmd_buffer, &dep_info));

    memset(&upload_image_region, 0, sizeof(upload_image_region));
    upload_image_region.sType = VK_STRUCTURE_TYPE_BUFFER_IMAGE_COPY_2;
    upload_image_region.bufferOffset = scratch.offset;
    upload_image_region.imageExtent.width = vkd3d_hud_font_metadata.width;
    upload_image_region.imageExtent.height = vkd3d_hud_font_metadata.height;
    upload_image_region.imageExtent.depth = 1u;
    upload_image_region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    upload_image_region.imageSubresource.layerCount = 1u;

    memset(&upload_image_info, 0, sizeof(upload_image_info));
    upload_image_info.sType = VK_STRUCTURE_TYPE_COPY_BUFFER_TO_IMAGE_INFO_2;
    upload_image_info.srcBuffer = scratch.buffer;
    upload_image_info.dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    upload_image_info.dstImage = hud->font.image;
    upload_image_info.regionCount = 1;
    upload_image_info.pRegions = &upload_image_region;

    VK_CALL(vkCmdCopyBufferToImage2(cmd_buffer, &upload_image_info));

    VK_CALL(vkCmdUpdateBuffer(cmd_buffer, hud->gpu_buffer.vk_buffer,
            offsetof(struct vkd3d_hud_data_buffer_layout, font_metadata),
            sizeof(font_data), &font_data));

    image_barrier.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
    image_barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    image_barrier.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    image_barrier.dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
    image_barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    image_barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    memset(&memory_barrier, 0, sizeof(memory_barrier));
    memory_barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
    memory_barrier.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
    memory_barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    memory_barrier.dstStageMask = VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT;
    memory_barrier.dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT;

    dep_info.memoryBarrierCount = 1u;
    dep_info.pMemoryBarriers = &memory_barrier;

    VK_CALL(vkCmdPipelineBarrier2(cmd_buffer, &dep_info));
}

uint32_t vkd3d_hud_allocate_swapchain_cookie(struct vkd3d_hud *hud)
{
    TRACE("hud %p.\n", hud);

    return vkd3d_atomic_uint32_increment(&hud->swapchain.next_cookie, vkd3d_memory_order_acquire);
}

void vkd3d_hud_unregister_swapchain(struct vkd3d_hud *hud, uint32_t cookie)
{
    TRACE("hud %p, cookie %u, semaphore %"PRIx64"\n", hud, cookie);

    /* If the swapchain is not currently active, it means it has never been
     * made active, and thus we have never used its semaphore either. Skip. */
    if (vkd3d_atomic_uint32_load_explicit(&hud->swapchain.active_cookie, vkd3d_memory_order_relaxed) != cookie)
        return;

    /* We can assume that the semaphore that belongs to the swapchain has no pending
     * signals, so just clear out the semaphore array so we don't access an already
     * destroyed semaphore on the next update. */
    memset(hud->swapchain.vk_semaphores, 0, sizeof(hud->swapchain.vk_semaphores));

    /* Mark swapchain as inactive so that another swapchain can grab the HUD */
    vkd3d_atomic_uint32_store_explicit(&hud->swapchain.active_cookie, 0u, vkd3d_memory_order_release);
}

static bool vkd3d_hud_wait_semaphore(struct vkd3d_hud *hud, uint32_t frame_index)
{
    const struct vkd3d_timeline_semaphore *sem = &hud->swapchain.vk_semaphores[frame_index];
    const struct vkd3d_vk_device_procs *vk_procs = &hud->device->vk_procs;
    VkSemaphoreWaitInfo wait_info;
    VkResult vr;

    if (!sem->vk_semaphore)
        return true;

    memset(&wait_info, 0, sizeof(wait_info));
    wait_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
    wait_info.semaphoreCount = 1u;
    wait_info.pSemaphores = &sem->vk_semaphore;
    wait_info.pValues = &sem->last_signaled;

    vr = VK_CALL(vkWaitSemaphores(hud->device->vk_device, &wait_info, UINT64_MAX));

    if (vr)
        ERR("Failed to wait for timeline semaphore, vr %d.\n", vr);

    return vr == VK_SUCCESS;
}

static void vkd3d_hud_cleanup_destroyed_queues(struct vkd3d_hud *hud)
{
    const struct vkd3d_vk_device_procs *vk_procs = &hud->device->vk_procs;
    struct vkd3d_timeline_semaphore *semaphore;
    struct vkd3d_hud_queue_info *queue;
    uint64_t timeline_value;
    size_t count;
    VkResult vr;

    pthread_mutex_lock(&hud->queues.mutex);

    for (count = 0; count < hud->queues.destroyed_queue_count; count++)
    {
        queue = hud->queues.destroyed_queues[count];

        if (hud->update_count - queue->last_latch_update_count < VKD3D_HUD_MAX_BUFFERED_FRAMES)
        {
            semaphore = &hud->swapchain.vk_semaphores[queue->last_latch_update_count % VKD3D_HUD_MAX_BUFFERED_FRAMES];

            if ((vr = VK_CALL(vkGetSemaphoreCounterValue(hud->device->vk_device,
                semaphore->vk_semaphore, &timeline_value))))
                ERR("Failed to get semaphore value, vr %d.\n");

            if (vr || timeline_value < semaphore->last_signaled)
                break;
        }

        vkd3d_hud_destroy_queue_info(hud, queue);
    }

    hud->queues.destroyed_queue_count -= count;

    if (count && hud->queues.destroyed_queue_count)
    {
        memmove(&hud->queues.destroyed_queues[0], &hud->queues.destroyed_queues[count],
                sizeof(*hud->queues.destroyed_queues) * hud->queues.destroyed_queue_count);
    }

    pthread_mutex_unlock(&hud->queues.mutex);
}

static uint32_t vkd3d_hud_text_buffer_reserve(struct vkd3d_hud_text_buffer *text_buffer,
        int32_t x, int32_t y, uint32_t size, uint32_t color, size_t max_length)
{
    struct vkd3d_hud_text_draw_info *draw_info;
    VkDrawIndirectCommand *draw_command;

    max_length = align(max_length, VKD3D_HUD_TEXT_ALLOCATION_GRANULARITY);

    if (text_buffer->draw_count >= VKD3D_HUD_MAX_TEXT_DRAWS ||
            text_buffer->char_count + max_length > VKD3D_HUD_MAX_TEXT_CHARS ||
            !max_length)
        return VKD3D_HUD_MAX_TEXT_DRAWS;

    draw_info = &text_buffer->draw_infos[text_buffer->draw_count];
    draw_info->text_offset = text_buffer->char_count;
    draw_info->text_length = max_length;
    draw_info->x = x;
    draw_info->y = y;
    draw_info->color = color;
    draw_info->size = size;

    draw_command = &text_buffer->draw_commands[text_buffer->draw_count];
    draw_command->vertexCount = 0u;
    draw_command->firstVertex = 0u;
    draw_command->instanceCount = 1u;
    draw_command->firstInstance = text_buffer->draw_count;

    text_buffer->char_count += max_length;
    return text_buffer->draw_count++;
}

static void vkd3d_hud_text_buffer_add(struct vkd3d_hud_text_buffer *text_buffer,
        int32_t x, int32_t y, uint32_t size, uint32_t color, const char *fmt, ...)
{
    const struct vkd3d_hud_text_draw_info *draw_info;
    VkDrawIndirectCommand *draw_command;
    char str[VKD3D_HUD_MAX_TEXT_CHARS];
    uint32_t draw_index;
    va_list args;
    size_t n;

    va_start(args, fmt);
    n = vsnprintf(str, sizeof(str), fmt, args);
    va_end(args);

    n = min(n, sizeof(str));

    draw_index = vkd3d_hud_text_buffer_reserve(text_buffer, x, y, size, color, n);

    if (draw_index >= VKD3D_HUD_MAX_TEXT_DRAWS)
        return;

    draw_info = &text_buffer->draw_infos[draw_index];
    memcpy(&text_buffer->text[draw_info->text_offset], str, draw_info->text_length);

    /* Vertex shader draws two triangles per character */
    draw_command = &text_buffer->draw_commands[draw_index];
    draw_command->vertexCount = 6u * n;
}

static void vkd3d_hud_update_general(struct vkd3d_hud *hud, VkCommandBuffer cmd_buffer,
        struct vkd3d_hud_cs_update_args *args, struct vkd3d_hud_text_buffer *text_buffer)
{
    const struct vkd3d_vk_device_procs *vk_procs = &hud->device->vk_procs;

    VK_CALL(vkCmdBindPipeline(cmd_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, hud->compute_psos.update_pso));

    VK_CALL(vkCmdPushConstants(cmd_buffer, hud->compute_psos.update_pso_layout,
            VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(*args), args));

    VK_CALL(vkCmdDispatch(cmd_buffer, 1u, 1u, 1u));
}

static void vkd3d_hud_print_version(struct vkd3d_hud *hud, struct vkd3d_hud_context *context, struct vkd3d_hud_text_buffer *text_buffer)
{
    context->top_left.y += 16;

    vkd3d_hud_text_buffer_add(text_buffer,
            context->top_left.x, context->top_left.y,
            16, 0xffffffu, "vkd3d_proton %s", vkd3d_version);

    context->top_left.y += 8;
}

static void vkd3d_hud_print_driver(struct vkd3d_hud *hud, struct vkd3d_hud_context *context, struct vkd3d_hud_text_buffer *text_buffer)
{
    context->top_left.y += 16;

    vkd3d_hud_text_buffer_add(text_buffer,
            context->top_left.x, context->top_left.y,
            16, 0xffffffu, "%s", hud->device->device_info.properties2.properties.deviceName);

    context->top_left.y += 24;

    vkd3d_hud_text_buffer_add(text_buffer,
            context->top_left.x, context->top_left.y,
            16, 0xffffffu, "Driver:  %s", hud->device->device_info.vulkan_1_2_properties.driverName);

    context->top_left.y += 20;

    vkd3d_hud_text_buffer_add(text_buffer,
            context->top_left.x, context->top_left.y,
            16, 0xffffffu, "Version: %s", hud->device->device_info.vulkan_1_2_properties.driverInfo);

    context->top_left.y += 8;
}

static uint32_t vkd3d_hud_print_fps(struct vkd3d_hud *hud, struct vkd3d_hud_context *context, struct vkd3d_hud_text_buffer *text_buffer)
{
    uint32_t draw;

    context->top_left.y += 16;

    vkd3d_hud_text_buffer_add(text_buffer,
            context->top_left.x, context->top_left.y,
            16, 0xff4040u, "FPS:");

    draw = vkd3d_hud_text_buffer_reserve(text_buffer,
            context->top_left.x + 60, context->top_left.y,
            16, 0xffffffu, VKD3D_HUD_TEXT_ALLOCATION_GRANULARITY);

    context->top_left.y += 8;
    return draw;
}

static uint32_t vkd3d_hud_print_frametimes(struct vkd3d_hud *hud, struct vkd3d_hud_context *context, struct vkd3d_hud_text_buffer *text_buffer)
{
    uint32_t draw;

    context->bottom_left.y -= 104;

    hud->frametime_graph.offset = context->bottom_left;
    hud->frametime_graph.extent.width = VKD3D_HUD_MAX_FRAMETIME_RECORDS;
    hud->frametime_graph.extent.height = 96;

    context->bottom_left.y -= 8;

    vkd3d_hud_text_buffer_add(text_buffer,
            context->bottom_left.x + 4u, context->bottom_left.y,
            12, 0xff4040u, "min:");

    vkd3d_hud_text_buffer_add(text_buffer,
            context->bottom_left.x + 162u, context->bottom_left.y,
            12, 0xff4040u, "max:");

    /* We're guaranteed consecutive draw IDs, so this is safe */
    draw = vkd3d_hud_text_buffer_reserve(text_buffer,
            context->bottom_left.x + 52, context->bottom_left.y,
            12, 0xffffffu, VKD3D_HUD_TEXT_ALLOCATION_GRANULARITY);

    vkd3d_hud_text_buffer_reserve(text_buffer,
            context->bottom_left.x + 210, context->bottom_left.y,
            12, 0xffffffu, VKD3D_HUD_TEXT_ALLOCATION_GRANULARITY);

    context->bottom_left.y -= 8;
    return draw;
}

static bool vkd3d_hud_make_swapchain_active(struct vkd3d_hud *hud, uint32_t cookie)
{
    /* Make sure that only one swapchain can update the HUD. If no swapchain
     * is active, make the current one active until it gets destroyed. */
    uint32_t active_cookie = vkd3d_atomic_uint32_load_explicit(&hud->swapchain.active_cookie, vkd3d_memory_order_acquire);

    while (!active_cookie)
    {
        active_cookie = vkd3d_atomic_uint32_compare_exchange(&hud->swapchain.active_cookie,
                active_cookie, cookie, vkd3d_memory_order_acquire, vkd3d_memory_order_relaxed);
    }

    return active_cookie == cookie;
}

static uint32_t vkd3d_hud_update_submissions(struct vkd3d_hud *hud, VkCommandBuffer cmd_buffer)
{
    const struct vkd3d_vk_device_procs *vk_procs = &hud->device->vk_procs;
    uint32_t submission_count, lo_index, hi_index;
    struct vkd3d_scratch_allocation scratch;
    struct vkd3d_hud_queue_info *queue;
    VkBufferCopy2 buffer_regions[2];
    VkCopyBufferInfo2 buffer_copy;
    uint32_t last_complete_frame;
    VkDeviceSize scratch_size;
    unsigned int i;

    last_complete_frame = hud->update_count;

    for (i = 0; i < hud->queues.queue_info_count; i++)
    {
        queue = hud->queues.queue_infos[i];

        spinlock_acquire(&queue->mutex);

        /* Fetch indices of completed submissions to upload */
        submission_count = min(VKD3D_HUD_MAX_PER_QUEUE_SUBMISSIONS,
                queue->last_submission_id - queue->last_latch_submission_id);

        hi_index = (queue->last_submission_id) % VKD3D_HUD_MAX_PER_QUEUE_SUBMISSIONS;
        lo_index = (hi_index - submission_count + 1u) % VKD3D_HUD_MAX_PER_QUEUE_SUBMISSIONS;

        queue->last_latch_update_count = hud->update_count;
        queue->last_latch_submission_id = queue->last_submission_id;

        /* If there are any incomplete submissions, the frame prior to that submission
         * can be considered completed as far as this queue is concerned */
        if (queue->last_submission_id < queue->next_submission)
        {
            last_complete_frame = min(last_complete_frame,
                    queue->submissions[(queue->last_submission_id + 1u) % VKD3D_HUD_MAX_PER_QUEUE_SUBMISSIONS].start_frame - 1u);
        }

        /* Update stat counters. Since the concept of a frame is somewhat ambiguous w.r.t.
         * asynchronous submissions, display the number of submissions performed between
         * presents on the CPU timeline. */
        queue->stats.submission_count = max(queue->stats.submission_count,
                queue->next_submission - queue->last_update_submission_count);
        queue->stats.cmd_buffer_count = max(queue->stats.cmd_buffer_count,
                queue->next_cmd_buffer - queue->last_update_cmd_buffer_count);

        if (hud->refresh_count == hud->update_count)
        {
            queue->display_stats = queue->stats;
            memset(&queue->stats, 0, sizeof(queue->stats));
        }

        queue->last_update_submission_count = queue->next_submission;
        queue->last_update_cmd_buffer_count = queue->next_cmd_buffer;

        if (!submission_count)
        {
            spinlock_release(&queue->mutex);
            continue;
        }

        scratch_size = submission_count * sizeof(struct vkd3d_hud_queue_submission_info);

        if (!vkd3d_hud_alloc_upload_buffer(hud, scratch_size, &scratch))
        {
            ERR("Failed to allocate scratch buffer for queue submissions.\n");
            spinlock_release(&queue->mutex);
            break;
        }

        memset(buffer_regions, 0, sizeof(buffer_regions));
        buffer_regions[0].sType = VK_STRUCTURE_TYPE_BUFFER_COPY_2;
        buffer_regions[1].sType = VK_STRUCTURE_TYPE_BUFFER_COPY_2;

        memset(&buffer_copy, 0, sizeof(buffer_copy));
        buffer_copy.sType = VK_STRUCTURE_TYPE_COPY_BUFFER_INFO_2;
        buffer_copy.srcBuffer = scratch.buffer;
        buffer_copy.dstBuffer = queue->gpu_buffer.vk_buffer;
        buffer_copy.pRegions = buffer_regions;

        if (lo_index <= hi_index)
        {
            buffer_copy.regionCount = 1;

            buffer_regions[0].srcOffset = scratch.offset;
            buffer_regions[0].dstOffset = offsetof(struct vkd3d_hud_queue_gpu_buffer_layout, submissions) +
                    lo_index * sizeof(struct vkd3d_hud_queue_submission_info);
            buffer_regions[0].size = scratch_size;

            memcpy(scratch.host_ptr, &queue->submissions[lo_index], scratch_size);
        }
        else
        {
            /* Need two separate copy regions here as the ring buffer wraps around */
            buffer_copy.regionCount = 2;

            buffer_regions[0].srcOffset = scratch.offset;
            buffer_regions[0].dstOffset = offsetof(struct vkd3d_hud_queue_gpu_buffer_layout, submissions) +
                    lo_index * sizeof(struct vkd3d_hud_queue_submission_info);
            buffer_regions[0].size = (submission_count - lo_index) * sizeof(struct vkd3d_hud_queue_submission_info);

            memcpy(scratch.host_ptr, &queue->submissions[lo_index], buffer_regions[0].size);

            buffer_regions[1].srcOffset = scratch.offset + buffer_regions[0].size;
            buffer_regions[1].dstOffset = offsetof(struct vkd3d_hud_queue_gpu_buffer_layout, submissions);
            buffer_regions[1].size = scratch_size - buffer_regions[0].size;

            memcpy(void_ptr_offset(scratch.host_ptr, buffer_regions[0].size),
                    &queue->submissions[0], buffer_regions[1].size);
        }

        spinlock_release(&queue->mutex);

        VK_CALL(vkCmdCopyBuffer2(cmd_buffer, &buffer_copy));
    }

    /* Last frame known to have completed on all queues. Used as a
     * hint for visualizing queue submissions. */
    return last_complete_frame;
}

static void vkd3d_hud_print_submission_info(struct vkd3d_hud *hud, struct vkd3d_hud_context *context,
        struct vkd3d_hud_text_buffer *text_buffer, VkCommandBuffer cmd_buffer)
{
    struct vkd3d_hud_queue_info *queue_info;
    struct vkd3d_queue *vkd3d_queue;
    unsigned int i, queue_bit;
    uint64_t current_time_ns;
    const char *queue_name;
    bool hw_queue_active;

    static const char *vk_queue_names[] =
    {
        "gfx",
        "compute",
        "sdma",
        "sparse",
    };

    struct queue_text
    {
        const char *name;
        uint32_t color;
    } d3d_queue_text;

    static const struct queue_text d3d_queue_texts[] =
    {
        { "Direct",     0x80c0ff },
        { NULL,         0x000000 },
        { "Compute",    0xffff80 },
        { "Copy",       0x80ff80 },
        { "V.decode",   0xc0c0c0 },
        { "V.process",  0xc0c0c0 },
        { "V.encode",   0xc0c0c0 },
    };

    current_time_ns = vkd3d_get_current_time_ns();

    /* Iterate in reverse order to simplify layout calculations */
    hw_queue_active = false;

    for (i = hud->queues.queue_info_count; i; i--)
    {
        queue_info = hud->queues.queue_infos[i - 1];
        vkd3d_queue = queue_info->queue->vkd3d_queue;

        /* Don't show queues that aren't actively being used */
        if (queue_info->last_submission_time_ns + 15ull * VKD3D_QUEUE_INACTIVE_THRESHOLD_NS > current_time_ns)
        {
            memset(&d3d_queue_text, 0, sizeof(d3d_queue_text));
            d3d_queue_text.name = "Unknown";
            d3d_queue_text.color = 0xffffff;

            if (queue_info->queue->desc.Type < ARRAY_SIZE(d3d_queue_texts))
                d3d_queue_text = d3d_queue_texts[queue_info->queue->desc.Type];

            context->bottom_right.y -= 84;

            vkd3d_hud_text_buffer_add(text_buffer, context->bottom_right.x - 616,
                    context->bottom_right.y + 24, 14, d3d_queue_text.color,
                    "%s (%u)", d3d_queue_text.name, queue_info->queue->submission_thread_tid);

            vkd3d_hud_text_buffer_add(text_buffer, context->bottom_right.x - 615,
                    context->bottom_right.y + 40, 12, d3d_queue_text.color,
                    "sub:", d3d_queue_text.name);

            vkd3d_hud_text_buffer_add(text_buffer, context->bottom_right.x - 567,
                    context->bottom_right.y + 40, 12, 0xffffffu,
                    "%u", queue_info->display_stats.submission_count);

            vkd3d_hud_text_buffer_add(text_buffer, context->bottom_right.x - 615,
                    context->bottom_right.y + 56, 12, d3d_queue_text.color,
                    "cmd:", d3d_queue_text.name);

            vkd3d_hud_text_buffer_add(text_buffer, context->bottom_right.x - 567,
                    context->bottom_right.y + 56, 12, 0xffffffu,
                    "%u", queue_info->display_stats.cmd_buffer_count);

            hw_queue_active = true;
        }

        if (hw_queue_active && (i == 1 || hud->queues.queue_infos[i - 2]->queue->vkd3d_queue != vkd3d_queue))
        {
            hw_queue_active = false;

            queue_name = "unknown";
            queue_bit = vkd3d_bitmask_tzcnt32(vkd3d_queue->vk_queue_flags);

            if (queue_bit < ARRAY_SIZE(vk_queue_names))
                queue_name = vk_queue_names[queue_bit];

            vkd3d_hud_text_buffer_add(text_buffer, context->bottom_right.x - 624,
                    context->bottom_right.y, 16, 0xffffff, "hw %s queue (%u,%u)",
                    queue_name, vkd3d_queue->vk_family_index, vkd3d_queue->vk_queue_index);

            context->bottom_right.y -= 20;
        }
    }
}


void vkd3d_hud_update(struct vkd3d_hud *hud, uint32_t swapchain_cookie,
        VkCommandBuffer cmd_buffer, const struct vkd3d_timeline_semaphore *signal_semaphore)
{
    uint32_t frame_index = hud->update_count % VKD3D_HUD_MAX_BUFFERED_FRAMES;
    const struct vkd3d_vk_device_procs *vk_procs = &hud->device->vk_procs;
    struct vkd3d_hud_text_buffer cpu_text_buffer;
    struct vkd3d_hud_cs_update_args cs_args;
    struct vkd3d_scratch_allocation scratch;
    struct vkd3d_hud_context hud_context;
    VkMemoryBarrier2 memory_barrier;
    VkCopyBufferInfo2 buffer_copy;
    VkBufferCopy2 buffer_region;
    VkDependencyInfo dep_info;
    uint64_t current_time_ns;
    unsigned int i;

    TRACE("hud %p, cookie %u, cmd_buffer %p, signal_semaphore %p.\n",
            hud, swapchain_cookie, cmd_buffer, signal_semaphore);

    if (!vkd3d_hud_make_swapchain_active(hud, swapchain_cookie))
        return;

    /* Ensure we can safely write to the next upload buffer section */
    if (!vkd3d_hud_wait_semaphore(hud, frame_index))
        return;

    vkd3d_hud_cleanup_destroyed_queues(hud);

    /* Determine whether to refresh certain HUD elements */
    current_time_ns = vkd3d_get_current_time_ns();

    if (current_time_ns - hud->refresh_time >= VKD3D_HUD_DEFAULT_REFRESH_INTERVAL)
    {
        hud->refresh_count = hud->update_count;
        hud->refresh_time = current_time_ns;
    }

    hud->swapchain.vk_semaphores[frame_index] = *signal_semaphore;
    hud->cpu_buffer_offset = frame_index * VKD3D_HUD_UPLOAD_BUFFER_SIZE_PER_FRAME;

    if (!hud->update_count)
    {
        /* Upload font on the first frame. We need the entire upload buffer
         * for this, make sure to synchronize on the next update. */
        for (i = 0; i < VKD3D_HUD_MAX_BUFFERED_FRAMES; i++)
            hud->swapchain.vk_semaphores[i] = *signal_semaphore;

        hud->cpu_buffer_offset = 0u;

        vkd3d_hud_upload_font(hud, cmd_buffer);
    }

    /* Write end-of-frame timestamp to the alternating timestamp set */
    VK_CALL(vkCmdResetQueryPool(cmd_buffer, hud->timestamp_queries.query_pool, hud->update_timestamp_index, 1));

    VK_CALL(vkCmdWriteTimestamp2(cmd_buffer, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
            hud->timestamp_queries.query_pool, hud->update_timestamp_index));

    VK_CALL(vkCmdCopyQueryPoolResults(cmd_buffer, hud->timestamp_queries.query_pool,
            hud->update_timestamp_index, 1, hud->gpu_buffer.vk_buffer,
            offsetof(struct vkd3d_hud_data_buffer_layout, stats) +
            offsetof(struct vkd3d_hud_stat_buffer, update_timestamps) +
            sizeof(uint64_t) * (hud->update_count & frame_index),
            sizeof(uint64_t), VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT));

    /* Record command to upload initial text buffer early so that shaders can
     * generate text or emit additional draws. The upload buffer itself will
     * be written later. */
    memset(&cpu_text_buffer, 0, sizeof(cpu_text_buffer));

    if (!vkd3d_hud_alloc_upload_buffer(hud, sizeof(cpu_text_buffer), &scratch))
    {
        ERR("Failed to allocate text upload buffer.\n");
        return;
    }

    memset(&buffer_region, 0, sizeof(buffer_region));
    buffer_region.sType = VK_STRUCTURE_TYPE_BUFFER_COPY_2;
    buffer_region.dstOffset = offsetof(struct vkd3d_hud_data_buffer_layout, text);
    buffer_region.srcOffset = scratch.offset;
    buffer_region.size = sizeof(cpu_text_buffer);

    memset(&buffer_copy, 0, sizeof(buffer_copy));
    buffer_copy.sType = VK_STRUCTURE_TYPE_COPY_BUFFER_INFO_2;
    buffer_copy.srcBuffer = scratch.buffer;
    buffer_copy.dstBuffer = hud->gpu_buffer.vk_buffer;
    buffer_copy.regionCount = 1;
    buffer_copy.pRegions = &buffer_region;

    VK_CALL(vkCmdCopyBuffer2(cmd_buffer, &buffer_copy));

    /* Also upload submission data right away, keep queue
     * allocation locked until we're fully done though */
    pthread_mutex_lock(&hud->queues.mutex);

    if (hud->item_mask & VKD3D_HUD_ITEM_SUBMISSIONS)
        vkd3d_hud_update_submissions(hud, cmd_buffer);

    memset(&memory_barrier, 0, sizeof(memory_barrier));
    memory_barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
    memory_barrier.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
    memory_barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    memory_barrier.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    memory_barrier.dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT;

    memset(&dep_info, 0, sizeof(dep_info));
    dep_info.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dep_info.memoryBarrierCount = 1;
    dep_info.pMemoryBarriers = &memory_barrier;

    VK_CALL(vkCmdPipelineBarrier2(cmd_buffer, &dep_info));;

    memset(&hud_context, 0, sizeof(hud_context));
    hud_context.top_left.x = 8;
    hud_context.top_left.y = 8;
    hud_context.bottom_left.x = 8;
    hud_context.bottom_left.y = -8;
    hud_context.bottom_right.x = -8;
    hud_context.bottom_right.y = -8;

    memset(&cs_args, 0, sizeof(cs_args));
    cs_args.data_buffer_va = hud->gpu_buffer.va;
    cs_args.update_count = hud->update_count;
    cs_args.refresh_count = hud->refresh_count;
    cs_args.enabled_items = hud->item_mask;
    cs_args.gpu_ms_per_tick = hud->gpu_ms_per_tick;

    if (hud->item_mask & VKD3D_HUD_ITEM_VERSION)
        vkd3d_hud_print_version(hud, &hud_context, &cpu_text_buffer);

    if (hud->item_mask & VKD3D_HUD_ITEM_DRIVER)
        vkd3d_hud_print_driver(hud, &hud_context, &cpu_text_buffer);

    if (hud->item_mask & VKD3D_HUD_ITEM_FPS)
        cs_args.fps_draw_index = vkd3d_hud_print_fps(hud, &hud_context, &cpu_text_buffer);

    if (hud->item_mask & VKD3D_HUD_ITEM_FRAMETIMES)
        cs_args.frametime_draw_index = vkd3d_hud_print_frametimes(hud, &hud_context, &cpu_text_buffer);

    if (hud->item_mask & VKD3D_HUD_ITEM_SUBMISSIONS)
        vkd3d_hud_print_submission_info(hud, &hud_context, &cpu_text_buffer, cmd_buffer);

    vkd3d_hud_update_general(hud, cmd_buffer, &cs_args, &cpu_text_buffer);

    /* Copy local text buffer to upload buffer */
    memcpy(scratch.host_ptr, &cpu_text_buffer, sizeof(cpu_text_buffer));

    memory_barrier.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    memory_barrier.srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
    memory_barrier.dstStageMask = VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT |
            VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT |
            VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    memory_barrier.dstAccessMask = VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT |
            VK_ACCESS_2_SHADER_STORAGE_READ_BIT;

    VK_CALL(vkCmdPipelineBarrier2(cmd_buffer, &dep_info));

    hud->update_count++;

    pthread_mutex_unlock(&hud->queues.mutex);
}

static void vkd3d_hud_render_text(struct vkd3d_hud *hud, struct vkd3d_hud_render_parameters *params,
        const struct vkd3d_hud_graphics_pipelines *pipelines)
{
    const struct vkd3d_vk_device_procs *vk_procs = &hud->device->vk_procs;
    struct vkd3d_hud_render_text_args shader_args;
    VkDescriptorImageInfo font_texture_info;
    VkWriteDescriptorSet font_descriptor;

    memset(&shader_args, 0, sizeof(shader_args));
    shader_args.data_buffer_va = hud->gpu_buffer.va;
    shader_args.swapchain_extent = params->swapchain_extent;

    VK_CALL(vkCmdBindPipeline(params->cmd_buffer,
            VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines->render_text_pso));

    memset(&font_texture_info, 0, sizeof(font_texture_info));
    font_texture_info.imageView = hud->font.image_view;
    font_texture_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    font_texture_info.sampler = hud->font.sampler;

    memset(&font_descriptor, 0, sizeof(font_descriptor));
    font_descriptor.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    font_descriptor.descriptorCount = 1;
    font_descriptor.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    font_descriptor.pImageInfo = &font_texture_info;

    VK_CALL(vkCmdPushDescriptorSetKHR(params->cmd_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
            hud->graphics_psos.render_text_pso_layout, 0, 1, &font_descriptor));

    VK_CALL(vkCmdPushConstants(params->cmd_buffer, hud->graphics_psos.render_text_pso_layout,
            VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(shader_args), &shader_args));

    VK_CALL(vkCmdDrawIndirectCount(params->cmd_buffer,
            hud->gpu_buffer.vk_buffer,
            offsetof(struct vkd3d_hud_data_buffer_layout, text) +
            offsetof(struct vkd3d_hud_text_buffer, draw_commands),
            hud->gpu_buffer.vk_buffer,
            offsetof(struct vkd3d_hud_data_buffer_layout, text) +
            offsetof(struct vkd3d_hud_text_buffer, draw_count),
            VKD3D_HUD_MAX_TEXT_DRAWS, sizeof(VkDrawIndirectCommand)));
}

static void vkd3d_hud_render_frametime_graph(struct vkd3d_hud *hud, struct vkd3d_hud_render_parameters *params,
        const struct vkd3d_hud_graphics_pipelines *pipelines)
{
    const struct vkd3d_vk_device_procs *vk_procs = &hud->device->vk_procs;
    struct vkd3d_hud_render_frametime_graph_args shader_args;

    memset(&shader_args, 0, sizeof(shader_args));
    shader_args.data_buffer_va = hud->gpu_buffer.va;
    shader_args.swapchain_extent = params->swapchain_extent;
    shader_args.graph_rect = hud->frametime_graph;
    /* Update count gets bumped *after* the update, need to subtract 1 here */
    shader_args.frame_index = (hud->update_count - 1u) % VKD3D_HUD_MAX_FRAMETIME_RECORDS;

    VK_CALL(vkCmdBindPipeline(params->cmd_buffer,
            VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines->render_frametime_graph_pso));

    VK_CALL(vkCmdPushConstants(params->cmd_buffer, hud->graphics_psos.render_frametime_graph_pso_layout,
            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(shader_args), &shader_args));

    VK_CALL(vkCmdDraw(params->cmd_buffer, 4, 1, 0, 0));
}

void vkd3d_hud_render(struct vkd3d_hud *hud, uint32_t swapchain_cookie, struct vkd3d_hud_render_parameters *params)
{
    const struct vkd3d_hud_graphics_pipelines *pipelines;
    struct vkd3d_hud_graphics_pipelines_key key;

    TRACE("hud %p, swapchain_cookie %u, params %p.\n", hud, swapchain_cookie, params);

    if (vkd3d_atomic_uint32_load_explicit(&hud->swapchain.active_cookie, vkd3d_memory_order_relaxed) != swapchain_cookie)
        return;

    key.rt_format = params->swapchain_format.format;
    key.rt_color_space = params->swapchain_format.colorSpace;

    if (!(pipelines = vkd3d_hud_get_graphics_pipelines(hud, &key)))
        return;

    if (hud->item_mask & VKD3D_HUD_ITEM_FRAMETIMES)
        vkd3d_hud_render_frametime_graph(hud, params, pipelines);

    /* Draw text last so it's rendered on top of other elements */
    vkd3d_hud_render_text(hud, params, pipelines);
}

struct vkd3d_hud_queue_info *vkd3d_hud_register_queue(struct vkd3d_hud *hud,
        struct d3d12_command_queue *queue)
{
    struct vkd3d_queue *new_queue, *old_queue;
    struct vkd3d_hud_queue_info *object;
    size_t buffer_size, insert_index, i;

    if (!hud || !(hud->item_mask & VKD3D_HUD_ITEM_SUBMISSIONS))
        return NULL;

    TRACE("hud %p, queue %p.\n", hud, queue);

    if (!(object = vkd3d_calloc(1, sizeof(*object))))
    {
        ERR("Failed to allocate queue info structure.\n");
        return NULL;
    }

    spinlock_init(&object->mutex);

    /* Don't waste memory if we don't support command buffer infos on the queue */
    buffer_size = sizeof(struct vkd3d_hud_queue_gpu_buffer_layout);

    if (!(queue->vkd3d_queue->vk_queue_flags & VK_QUEUE_COMPUTE_BIT))
        buffer_size = offsetof(struct vkd3d_hud_queue_gpu_buffer_layout, cmd_regions);

    if (!vkd3d_hud_init_buffer(hud, &object->gpu_buffer, buffer_size, false, "hud queue info"))
        goto fail;

    pthread_mutex_lock(&hud->queues.mutex);

    if (!vkd3d_array_reserve((void**)&hud->queues.queue_infos, &hud->queues.queue_infos_size,
            hud->queues.queue_info_count + 1u, sizeof(*hud->queues.queue_infos)))
    {
        ERR("Failed to resize queue info array.\n");
        pthread_mutex_unlock(&hud->queues.mutex);
        goto fail;
    }

    /* Keep queues ordered by queue family first, then
     * by queue index, for displaying purposes */
    new_queue = queue->vkd3d_queue;

    for (insert_index = 0; insert_index < hud->queues.queue_info_count; insert_index++)
    {
        old_queue = hud->queues.queue_infos[insert_index]->queue->vkd3d_queue;

        if (new_queue->vk_family_index > old_queue->vk_family_index)
            continue;

        if (new_queue->vk_family_index < old_queue->vk_family_index ||
                new_queue->vk_queue_index < old_queue->vk_queue_index)
            break;
    }

    for (i = hud->queues.queue_info_count; i > insert_index; i--)
        hud->queues.queue_infos[i] = hud->queues.queue_infos[i - 1];

    hud->queues.queue_infos[insert_index] = object;
    hud->queues.queue_info_count++;
    pthread_mutex_unlock(&hud->queues.mutex);

    object->queue = queue;
    return object;

fail:
    vkd3d_free(object);
    return NULL;
}

void vkd3d_hud_unregister_queue(struct vkd3d_hud *hud, struct vkd3d_hud_queue_info *queue_info)
{
    size_t i;

    if (!hud || !queue_info)
        return;

    TRACE("hud %p, queue_info %p.\n", hud, queue_info);

    pthread_mutex_lock(&hud->queues.mutex);

    for (i = 0; i < hud->queues.queue_info_count; i++)
    {
        if (hud->queues.queue_infos[i] == queue_info)
        {
            hud->queues.queue_infos[i] = hud->queues.queue_infos[--hud->queues.queue_info_count];
            break;
        }
    }

    /* We can't destroy the structure immediately since the buffer may still be in use
     * by the GPU from a previous HUD update. Defer destruction until it's safe. */
    if (!vkd3d_array_reserve((void**)&hud->queues.destroyed_queues, &hud->queues.destroyed_queues_size,
            hud->queues.destroyed_queue_count + 1, sizeof(*hud->queues.destroyed_queues)))
    {
        ERR("Failed to resize destroyed queue list.\n");
        pthread_mutex_unlock(&hud->queues.mutex);
        /* Leak GPU buffer rather than rising a hang, this should never happen anyway */
        vkd3d_free(queue_info);
        return;
    }

    hud->queues.destroyed_queues[hud->queues.destroyed_queue_count++] = queue_info;
    pthread_mutex_unlock(&hud->queues.mutex);
}

void vkd3d_hud_queue_register_submission(struct vkd3d_hud *hud, struct vkd3d_hud_queue_info *queue_info,
        struct vkd3d_queue_timeline_trace_cookie cookie, const struct vkd3d_queue_timeline_trace_state *sub_info,
        uint32_t command_list_count, ID3D12CommandList * const *command_lists)
{
    struct vkd3d_hud_queue_tracked_submission *e = &hud->queues.submission_infos[cookie.index];
    struct vkd3d_hud_queue_submission_info *s;

    spinlock_acquire(&queue_info->mutex);

    if (queue_info->next_submission - queue_info->last_latch_submission_id >= VKD3D_HUD_MAX_PER_QUEUE_SUBMISSIONS)
    {
        ERR("Failed to allocate submission info.\n");
        spinlock_release(&queue_info->mutex);
        return;
    }

    e->queue = queue_info;
    e->submission_id = queue_info->next_submission;
    e->cmd_buffer_id = queue_info->next_cmd_buffer;

    s = &queue_info->submissions[e->submission_id];

    memset(s, 0, sizeof(*s));
    s->type = VKD3D_HUD_SUBMISSION_TYPE_EXECUTE_COMMANDS;
    s->cmd_buffer_count = command_list_count;
    s->cmd_buffer_index = e->cmd_buffer_id;
    s->start_time = sub_info->start_ts;
    s->start_frame = e->queue->last_latch_update_count;

    queue_info->next_submission += 1u;
    queue_info->next_cmd_buffer += command_list_count;

    spinlock_release(&queue_info->mutex);
}

void vkd3d_hud_queue_begin_submission(struct vkd3d_hud *hud,
        struct vkd3d_queue_timeline_trace_cookie cookie,
        const struct vkd3d_queue_timeline_trace_state *sub_info)
{
    struct vkd3d_hud_queue_tracked_submission *e = &hud->queues.submission_infos[cookie.index];
    struct vkd3d_hud_queue_submission_info *s;

    if (!e->queue)
        return;

    spinlock_acquire(&e->queue->mutex);

    s = &e->queue->submissions[e->submission_id];
    s->submit_begin_delta = sub_info->overhead_start_offset;
    s->submit_end_delta = sub_info->overhead_end_offset;

    spinlock_release(&e->queue->mutex);
}

void vkd3d_hud_queue_complete_submission(struct vkd3d_hud *hud,
        struct vkd3d_queue_timeline_trace_cookie cookie,
        const struct vkd3d_queue_timeline_trace_state *sub_info,
        uint64_t current_time_ns)
{
    struct vkd3d_hud_queue_tracked_submission *e = &hud->queues.submission_infos[cookie.index];
    struct vkd3d_hud_queue_submission_info *s;

    if (!e->queue)
        return;

    spinlock_acquire(&e->queue->mutex);

    s = &e->queue->submissions[e->submission_id];
    s->execute_end_delta = current_time_ns - s->start_time;

    e->queue->last_submission_time_ns = current_time_ns;
    e->queue->last_submission_id = e->submission_id;

    spinlock_release(&e->queue->mutex);

    memset(e, 0, sizeof(*e));
}
