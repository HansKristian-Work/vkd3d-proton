/*
 * Copyright 2019 Philip Rebohle
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
#include "vkd3d_shaders.h"

#define SPIRV_CODE(name) name, sizeof(name)

static VkResult vkd3d_meta_create_shader_module(struct d3d12_device *device,
        const uint32_t *code, size_t code_size, VkShaderModule *module)
{
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    VkShaderModuleCreateInfo shader_module_info;

    shader_module_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    shader_module_info.pNext = NULL;
    shader_module_info.flags = 0;
    shader_module_info.codeSize = code_size;
    shader_module_info.pCode = code;

    return VK_CALL(vkCreateShaderModule(device->vk_device, &shader_module_info, NULL, module));
}

static VkResult vkd3d_meta_create_descriptor_set_layout(struct d3d12_device *device,
        uint32_t binding_count, const VkDescriptorSetLayoutBinding *bindings,
        bool descriptor_buffer_compatible, VkDescriptorSetLayout *set_layout)
{
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    VkDescriptorSetLayoutCreateInfo set_layout_info;

    set_layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    set_layout_info.pNext = NULL;
    set_layout_info.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR;
    set_layout_info.bindingCount = binding_count;
    set_layout_info.pBindings = bindings;

    if (d3d12_device_uses_descriptor_buffers(device) && descriptor_buffer_compatible)
        set_layout_info.flags |= VK_DESCRIPTOR_SET_LAYOUT_CREATE_DESCRIPTOR_BUFFER_BIT_EXT;

    return VK_CALL(vkCreateDescriptorSetLayout(device->vk_device, &set_layout_info, NULL, set_layout));
}

static VkResult vkd3d_meta_create_sampler(struct d3d12_device *device, VkFilter filter, VkSampler *vk_sampler)
{
    struct vkd3d_view_key view_key;
    D3D12_SAMPLER_DESC2 desc;
    struct vkd3d_view *view;

    memset(&desc, 0, sizeof(desc));
    desc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    desc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    desc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    desc.Filter = filter == VK_FILTER_LINEAR ? D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT : D3D12_FILTER_MIN_MAG_MIP_POINT;

    view_key.view_type = VKD3D_VIEW_TYPE_SAMPLER;
    view_key.u.sampler = desc;
    view = vkd3d_view_map_create_view(&device->sampler_map, device, &view_key);
    if (!view)
        return VK_ERROR_OUT_OF_HOST_MEMORY;

    *vk_sampler = view->vk_sampler;
    return VK_SUCCESS;
}

static VkResult vkd3d_meta_create_pipeline_layout(struct d3d12_device *device,
        uint32_t set_layout_count, const VkDescriptorSetLayout *set_layouts,
        uint32_t push_constant_range_count, const VkPushConstantRange *push_constant_ranges,
        VkPipelineLayout *pipeline_layout)
{
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    VkPipelineLayoutCreateInfo pipeline_layout_info;

    pipeline_layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipeline_layout_info.pNext = NULL;
    pipeline_layout_info.flags = 0;
    pipeline_layout_info.setLayoutCount = set_layout_count;
    pipeline_layout_info.pSetLayouts = set_layouts;
    pipeline_layout_info.pushConstantRangeCount = push_constant_range_count;
    pipeline_layout_info.pPushConstantRanges = push_constant_ranges;

    return VK_CALL(vkCreatePipelineLayout(device->vk_device, &pipeline_layout_info, NULL, pipeline_layout));
}

static void vkd3d_meta_make_shader_stage(VkPipelineShaderStageCreateInfo *info, VkShaderStageFlagBits stage,
        VkShaderModule module, const char* entry_point, const VkSpecializationInfo *spec_info)
{
    info->sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    info->pNext = NULL;
    info->flags = 0;
    info->stage = stage;
    info->module = module;
    info->pName = entry_point;
    info->pSpecializationInfo = spec_info;
}

static VkResult vkd3d_meta_create_compute_pipeline(struct d3d12_device *device,
        size_t code_size, const uint32_t *code, VkPipelineLayout layout,
        const VkSpecializationInfo *specialization_info,
        bool descriptor_buffer_compatible, VkPipeline *pipeline)
{
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    VkComputePipelineCreateInfo pipeline_info;
    VkShaderModule module;
    VkResult vr;

    if ((vr = vkd3d_meta_create_shader_module(device, code, code_size, &module)) < 0)
    {
        ERR("Failed to create shader module, vr %d.", vr);
        return vr;
    }

    pipeline_info.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipeline_info.pNext = NULL;
    pipeline_info.flags = 0;
    pipeline_info.layout = layout;
    pipeline_info.basePipelineHandle = VK_NULL_HANDLE;
    pipeline_info.basePipelineIndex = -1;

    if (d3d12_device_uses_descriptor_buffers(device) && descriptor_buffer_compatible)
        pipeline_info.flags |= VK_PIPELINE_CREATE_DESCRIPTOR_BUFFER_BIT_EXT;

    vkd3d_meta_make_shader_stage(&pipeline_info.stage,
            VK_SHADER_STAGE_COMPUTE_BIT, module, "main", specialization_info);

    vr = VK_CALL(vkCreateComputePipelines(device->vk_device, VK_NULL_HANDLE, 1, &pipeline_info, NULL, pipeline));
    VK_CALL(vkDestroyShaderModule(device->vk_device, module, NULL));

    return vr;
}

static VkResult vkd3d_meta_create_graphics_pipeline(struct vkd3d_meta_ops *meta_ops,
        VkPipelineLayout layout, VkFormat color_format, VkFormat ds_format, VkImageAspectFlags vk_aspect_mask,
        VkShaderModule vs_module, VkShaderModule fs_module,
        VkSampleCountFlagBits samples, const VkPipelineDepthStencilStateCreateInfo *ds_state,
        const VkPipelineColorBlendStateCreateInfo *cb_state, const VkSpecializationInfo *spec_info,
        bool descriptor_buffer_compatible, VkPipeline *vk_pipeline)
{
    const struct vkd3d_vk_device_procs *vk_procs = &meta_ops->device->vk_procs;
    VkPipelineShaderStageCreateInfo shader_stages[3];
    VkPipelineInputAssemblyStateCreateInfo ia_state;
    VkPipelineRasterizationStateCreateInfo rs_state;
    VkPipelineRenderingCreateInfoKHR rendering_info;
    VkPipelineVertexInputStateCreateInfo vi_state;
    VkPipelineMultisampleStateCreateInfo ms_state;
    VkPipelineViewportStateCreateInfo vp_state;
    VkPipelineDynamicStateCreateInfo dyn_state;
    VkGraphicsPipelineCreateInfo pipeline_info;
    const uint32_t sample_mask = 0xFFFFFFFF;
    VkResult vr;

    static const VkDynamicState dynamic_states[] =
    {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR,
    };

    vi_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vi_state.pNext = NULL;
    vi_state.flags = 0;
    vi_state.vertexBindingDescriptionCount = 0;
    vi_state.pVertexBindingDescriptions = NULL;
    vi_state.vertexAttributeDescriptionCount = 0;
    vi_state.pVertexAttributeDescriptions = NULL;

    ia_state.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia_state.pNext = NULL;
    ia_state.flags = 0;
    ia_state.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    ia_state.primitiveRestartEnable = false;

    vp_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vp_state.pNext = NULL;
    vp_state.flags = 0;
    vp_state.viewportCount = 1;
    vp_state.pViewports = NULL;
    vp_state.scissorCount = 1;
    vp_state.pScissors = NULL;

    rs_state.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rs_state.pNext = NULL;
    rs_state.flags = 0;
    rs_state.depthClampEnable = VK_TRUE;
    rs_state.rasterizerDiscardEnable = VK_FALSE;
    rs_state.polygonMode = VK_POLYGON_MODE_FILL;
    rs_state.cullMode = VK_CULL_MODE_NONE;
    rs_state.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs_state.depthBiasEnable = VK_FALSE;
    rs_state.depthBiasConstantFactor = 0.0f;
    rs_state.depthBiasClamp = 0.0f;
    rs_state.depthBiasSlopeFactor = 0.0f;
    rs_state.lineWidth = 1.0f;

    ms_state.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms_state.pNext = NULL;
    ms_state.flags = 0;
    ms_state.rasterizationSamples = samples;
    ms_state.sampleShadingEnable = samples != VK_SAMPLE_COUNT_1_BIT;
    ms_state.minSampleShading = 1.0f;
    ms_state.pSampleMask = &sample_mask;
    ms_state.alphaToCoverageEnable = VK_FALSE;
    ms_state.alphaToOneEnable = VK_FALSE;

    dyn_state.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dyn_state.pNext = NULL;
    dyn_state.flags = 0;
    dyn_state.dynamicStateCount = ARRAY_SIZE(dynamic_states);
    dyn_state.pDynamicStates = dynamic_states;

    rendering_info.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR;
    rendering_info.pNext = NULL;
    rendering_info.viewMask = 0;
    rendering_info.colorAttachmentCount = color_format && (vk_aspect_mask & VK_IMAGE_ASPECT_COLOR_BIT) ? 1 : 0;
    rendering_info.pColorAttachmentFormats = color_format ? &color_format : NULL;
    rendering_info.depthAttachmentFormat = (vk_aspect_mask & VK_IMAGE_ASPECT_DEPTH_BIT) ? ds_format : VK_FORMAT_UNDEFINED;
    rendering_info.stencilAttachmentFormat = (vk_aspect_mask & VK_IMAGE_ASPECT_STENCIL_BIT) ? ds_format : VK_FORMAT_UNDEFINED;

    pipeline_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipeline_info.pNext = &rendering_info;
    pipeline_info.flags = 0;
    pipeline_info.stageCount = 0;
    pipeline_info.pStages = shader_stages;
    pipeline_info.pVertexInputState = &vi_state;
    pipeline_info.pInputAssemblyState = &ia_state;
    pipeline_info.pTessellationState = NULL;
    pipeline_info.pViewportState = &vp_state;
    pipeline_info.pRasterizationState = &rs_state;
    pipeline_info.pMultisampleState = &ms_state;
    pipeline_info.pDepthStencilState = ds_state;
    pipeline_info.pColorBlendState = cb_state;
    pipeline_info.pDynamicState = &dyn_state;
    pipeline_info.layout = layout;
    pipeline_info.renderPass = VK_NULL_HANDLE;
    pipeline_info.subpass = 0;
    pipeline_info.basePipelineHandle = VK_NULL_HANDLE;
    pipeline_info.basePipelineIndex = -1;

    if (d3d12_device_uses_descriptor_buffers(meta_ops->device) && descriptor_buffer_compatible)
        pipeline_info.flags |= VK_PIPELINE_CREATE_DESCRIPTOR_BUFFER_BIT_EXT;

    vkd3d_meta_make_shader_stage(&shader_stages[pipeline_info.stageCount++],
            VK_SHADER_STAGE_VERTEX_BIT,
            vs_module ? vs_module : meta_ops->common.vk_module_fullscreen_vs,
            "main", NULL);

    if (meta_ops->common.vk_module_fullscreen_gs)
    {
        vkd3d_meta_make_shader_stage(&shader_stages[pipeline_info.stageCount++],
                VK_SHADER_STAGE_GEOMETRY_BIT, meta_ops->common.vk_module_fullscreen_gs, "main", NULL);
    }

    if (fs_module)
    {
        vkd3d_meta_make_shader_stage(&shader_stages[pipeline_info.stageCount++],
                VK_SHADER_STAGE_FRAGMENT_BIT, fs_module, "main", spec_info);
    }

    if ((vr = VK_CALL(vkCreateGraphicsPipelines(meta_ops->device->vk_device,
            VK_NULL_HANDLE, 1, &pipeline_info, NULL, vk_pipeline))))
        ERR("Failed to create graphics pipeline, vr %d.\n", vr);

    return vr;
}


HRESULT vkd3d_clear_uav_ops_init(struct vkd3d_clear_uav_ops *meta_clear_uav_ops,
        struct d3d12_device *device)
{
    VkDescriptorSetLayoutBinding set_binding;
    VkPushConstantRange push_constant_range;
    unsigned int i;
    VkResult vr;

    struct {
      VkDescriptorSetLayout *set_layout;
      VkPipelineLayout *pipeline_layout;
      VkDescriptorType descriptor_type;
    }
    set_layouts[] =
    {
      { &meta_clear_uav_ops->vk_set_layout_buffer_raw, &meta_clear_uav_ops->vk_pipeline_layout_buffer_raw, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER },
      { &meta_clear_uav_ops->vk_set_layout_buffer,     &meta_clear_uav_ops->vk_pipeline_layout_buffer,     VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER },
      { &meta_clear_uav_ops->vk_set_layout_image,      &meta_clear_uav_ops->vk_pipeline_layout_image,      VK_DESCRIPTOR_TYPE_STORAGE_IMAGE },
    };

    struct {
      VkPipeline *pipeline;
      VkPipelineLayout *pipeline_layout;
      const uint32_t *code;
      size_t code_size;
    }
    pipelines[] =
    {
      { &meta_clear_uav_ops->clear_float.buffer,
        &meta_clear_uav_ops->vk_pipeline_layout_buffer,
        SPIRV_CODE(cs_clear_uav_buffer_float) },
      { &meta_clear_uav_ops->clear_float.image_1d,
        &meta_clear_uav_ops->vk_pipeline_layout_image,
        SPIRV_CODE(cs_clear_uav_image_1d_float) },
      { &meta_clear_uav_ops->clear_float.image_1d_array,
        &meta_clear_uav_ops->vk_pipeline_layout_image,
        SPIRV_CODE(cs_clear_uav_image_1d_array_float) },
      { &meta_clear_uav_ops->clear_float.image_2d,
        &meta_clear_uav_ops->vk_pipeline_layout_image,
        SPIRV_CODE(cs_clear_uav_image_2d_float) },
      { &meta_clear_uav_ops->clear_float.image_2d_array,
        &meta_clear_uav_ops->vk_pipeline_layout_image,
        SPIRV_CODE(cs_clear_uav_image_2d_array_float) },
      { &meta_clear_uav_ops->clear_float.image_3d,
        &meta_clear_uav_ops->vk_pipeline_layout_image,
        SPIRV_CODE(cs_clear_uav_image_3d_float) },
      { &meta_clear_uav_ops->clear_uint.buffer,
        &meta_clear_uav_ops->vk_pipeline_layout_buffer,
        SPIRV_CODE(cs_clear_uav_buffer_uint) },
      { &meta_clear_uav_ops->clear_uint.buffer_raw,
        &meta_clear_uav_ops->vk_pipeline_layout_buffer_raw,
        SPIRV_CODE(cs_clear_uav_buffer_raw) },
      { &meta_clear_uav_ops->clear_uint.image_1d,
        &meta_clear_uav_ops->vk_pipeline_layout_image,
        SPIRV_CODE(cs_clear_uav_image_1d_uint) },
      { &meta_clear_uav_ops->clear_uint.image_1d_array,
        &meta_clear_uav_ops->vk_pipeline_layout_image,
        SPIRV_CODE(cs_clear_uav_image_1d_array_uint) },
      { &meta_clear_uav_ops->clear_uint.image_2d,
        &meta_clear_uav_ops->vk_pipeline_layout_image,
        SPIRV_CODE(cs_clear_uav_image_2d_uint) },
      { &meta_clear_uav_ops->clear_uint.image_2d_array,
        &meta_clear_uav_ops->vk_pipeline_layout_image,
        SPIRV_CODE(cs_clear_uav_image_2d_array_uint) },
      { &meta_clear_uav_ops->clear_uint.image_3d,
        &meta_clear_uav_ops->vk_pipeline_layout_image,
        SPIRV_CODE(cs_clear_uav_image_3d_uint) },
    };

    memset(meta_clear_uav_ops, 0, sizeof(*meta_clear_uav_ops));

    set_binding.binding = 0;
    set_binding.descriptorCount = 1;
    set_binding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    set_binding.pImmutableSamplers = NULL;

    push_constant_range.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    push_constant_range.offset = 0;
    push_constant_range.size = sizeof(struct vkd3d_clear_uav_args);

    for (i = 0; i < ARRAY_SIZE(set_layouts); i++)
    {
        set_binding.descriptorType = set_layouts[i].descriptor_type;

        vr = vkd3d_meta_create_descriptor_set_layout(device, 1, &set_binding, true, set_layouts[i].set_layout);

        if (vr < 0)
        {
            ERR("Failed to create descriptor set layout %u, vr %d.", i, vr);
            goto fail;
        }

        vr = vkd3d_meta_create_pipeline_layout(device, 1, set_layouts[i].set_layout,
                1, &push_constant_range, set_layouts[i].pipeline_layout);

        if (vr < 0)
        {
            ERR("Failed to create pipeline layout %u, vr %d.", i, vr);
            goto fail;
        }
    }

    for (i = 0; i < ARRAY_SIZE(pipelines); i++)
    {
        if ((vr = vkd3d_meta_create_compute_pipeline(device, pipelines[i].code_size, pipelines[i].code,
                *pipelines[i].pipeline_layout, NULL, true, pipelines[i].pipeline)) < 0)
        {
            ERR("Failed to create compute pipeline %u, vr %d.", i, vr);
            goto fail;
        }
    }

    return S_OK;
fail:
    vkd3d_clear_uav_ops_cleanup(meta_clear_uav_ops, device);
    return hresult_from_vk_result(vr);
}

void vkd3d_clear_uav_ops_cleanup(struct vkd3d_clear_uav_ops *meta_clear_uav_ops,
        struct d3d12_device *device) {
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    unsigned int i;

    struct vkd3d_clear_uav_pipelines* pipeline_sets[] =
    {
        &meta_clear_uav_ops->clear_float,
        &meta_clear_uav_ops->clear_uint,
    };

    VK_CALL(vkDestroyDescriptorSetLayout(device->vk_device, meta_clear_uav_ops->vk_set_layout_buffer_raw, NULL));
    VK_CALL(vkDestroyDescriptorSetLayout(device->vk_device, meta_clear_uav_ops->vk_set_layout_buffer, NULL));
    VK_CALL(vkDestroyDescriptorSetLayout(device->vk_device, meta_clear_uav_ops->vk_set_layout_image, NULL));

    VK_CALL(vkDestroyPipelineLayout(device->vk_device, meta_clear_uav_ops->vk_pipeline_layout_buffer_raw, NULL));
    VK_CALL(vkDestroyPipelineLayout(device->vk_device, meta_clear_uav_ops->vk_pipeline_layout_buffer, NULL));
    VK_CALL(vkDestroyPipelineLayout(device->vk_device, meta_clear_uav_ops->vk_pipeline_layout_image, NULL));

    for (i = 0; i < ARRAY_SIZE(pipeline_sets); i++)
    {
        VK_CALL(vkDestroyPipeline(device->vk_device, pipeline_sets[i]->buffer, NULL));
        VK_CALL(vkDestroyPipeline(device->vk_device, pipeline_sets[i]->buffer_raw, NULL));
        VK_CALL(vkDestroyPipeline(device->vk_device, pipeline_sets[i]->image_1d, NULL));
        VK_CALL(vkDestroyPipeline(device->vk_device, pipeline_sets[i]->image_2d, NULL));
        VK_CALL(vkDestroyPipeline(device->vk_device, pipeline_sets[i]->image_3d, NULL));
        VK_CALL(vkDestroyPipeline(device->vk_device, pipeline_sets[i]->image_1d_array, NULL));
        VK_CALL(vkDestroyPipeline(device->vk_device, pipeline_sets[i]->image_2d_array, NULL));
    }
}

struct vkd3d_clear_uav_pipeline vkd3d_meta_get_clear_buffer_uav_pipeline(struct vkd3d_meta_ops *meta_ops,
        bool as_uint, bool raw)
{
    struct vkd3d_clear_uav_ops *meta_clear_uav_ops = &meta_ops->clear_uav;
    struct vkd3d_clear_uav_pipeline info;

    const struct vkd3d_clear_uav_pipelines *pipelines = (as_uint || raw)
            ? &meta_clear_uav_ops->clear_uint
            : &meta_clear_uav_ops->clear_float;

    info.vk_set_layout = raw ? meta_clear_uav_ops->vk_set_layout_buffer_raw : meta_clear_uav_ops->vk_set_layout_buffer;
    info.vk_pipeline_layout = raw ? meta_clear_uav_ops->vk_pipeline_layout_buffer_raw : meta_clear_uav_ops->vk_pipeline_layout_buffer;
    info.vk_pipeline = raw ? pipelines->buffer_raw : pipelines->buffer;
    return info;
}

struct vkd3d_clear_uav_pipeline vkd3d_meta_get_clear_image_uav_pipeline(struct vkd3d_meta_ops *meta_ops,
        VkImageViewType image_view_type, bool as_uint)
{
    struct vkd3d_clear_uav_ops *meta_clear_uav_ops = &meta_ops->clear_uav;
    struct vkd3d_clear_uav_pipeline info;

    const struct vkd3d_clear_uav_pipelines *pipelines = as_uint
            ? &meta_clear_uav_ops->clear_uint
            : &meta_clear_uav_ops->clear_float;

    info.vk_set_layout = meta_clear_uav_ops->vk_set_layout_image;
    info.vk_pipeline_layout = meta_clear_uav_ops->vk_pipeline_layout_image;

    switch (image_view_type)
    {
        case VK_IMAGE_VIEW_TYPE_1D:
            info.vk_pipeline = pipelines->image_1d;
            break;
        case VK_IMAGE_VIEW_TYPE_2D:
            info.vk_pipeline = pipelines->image_2d;
            break;
        case VK_IMAGE_VIEW_TYPE_3D:
            info.vk_pipeline = pipelines->image_3d;
            break;
        case VK_IMAGE_VIEW_TYPE_1D_ARRAY:
            info.vk_pipeline = pipelines->image_1d_array;
            break;
        case VK_IMAGE_VIEW_TYPE_2D_ARRAY:
            info.vk_pipeline = pipelines->image_2d_array;
            break;
        default:
            ERR("Unhandled view type %d.\n", image_view_type);
            info.vk_pipeline = VK_NULL_HANDLE;
    }

    return info;
}

VkExtent3D vkd3d_meta_get_clear_image_uav_workgroup_size(VkImageViewType view_type)
{
    switch (view_type)
    {
        case VK_IMAGE_VIEW_TYPE_1D:
        case VK_IMAGE_VIEW_TYPE_1D_ARRAY:
        {
            VkExtent3D result = { 64, 1, 1 };
            return result;
        }
        case VK_IMAGE_VIEW_TYPE_2D:
        case VK_IMAGE_VIEW_TYPE_2D_ARRAY:
        {
            VkExtent3D result = { 8, 8, 1 };
            return result;
        }
        case VK_IMAGE_VIEW_TYPE_3D:
        {
            VkExtent3D result = { 4, 4, 4 };
            return result;
        }
        default:
        {
            VkExtent3D result = { 0, 0, 0 };
            ERR("Unhandled view type %d.\n", view_type);
            return result;
        }
    }
}

HRESULT vkd3d_copy_image_ops_init(struct vkd3d_copy_image_ops *meta_copy_image_ops,
        struct d3d12_device *device)
{
    VkDescriptorSetLayoutBinding set_binding;
    VkPushConstantRange push_constant_range;
    VkResult vr;
    int rc;

    memset(meta_copy_image_ops, 0, sizeof(*meta_copy_image_ops));

    if ((rc = pthread_mutex_init(&meta_copy_image_ops->mutex, NULL)))
    {
        ERR("Failed to initialize mutex, error %d.\n", rc);
        return hresult_from_errno(rc);
    }

    set_binding.binding = 0;
    set_binding.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    set_binding.descriptorCount = 1;
    set_binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    set_binding.pImmutableSamplers = NULL;

    if ((vr = vkd3d_meta_create_descriptor_set_layout(device, 1, &set_binding, true, &meta_copy_image_ops->vk_set_layout)) < 0)
    {
        ERR("Failed to create descriptor set layout, vr %d.\n", vr);
        goto fail;
    }

    push_constant_range.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    push_constant_range.offset = 0;
    push_constant_range.size = sizeof(struct vkd3d_copy_image_args);

    if ((vr = vkd3d_meta_create_pipeline_layout(device, 1, &meta_copy_image_ops->vk_set_layout,
            1, &push_constant_range, &meta_copy_image_ops->vk_pipeline_layout)))
    {
        ERR("Failed to create pipeline layout, vr %d.\n", vr);
        goto fail;
    }

    if ((vr = vkd3d_meta_create_shader_module(device, SPIRV_CODE(fs_copy_image_float),
            &meta_copy_image_ops->vk_fs_float_module)) < 0)
    {
        ERR("Failed to create shader modules, vr %d.\n", vr);
        goto fail;
    }

    if ((vr = vkd3d_meta_create_shader_module(device, SPIRV_CODE(fs_copy_image_uint),
            &meta_copy_image_ops->vk_fs_uint_module)) < 0)
    {
        ERR("Failed to create shader modules, vr %d.\n", vr);
        goto fail;
    }

    if (device->vk_info.EXT_shader_stencil_export)
    {
        if ((vr = vkd3d_meta_create_shader_module(device, SPIRV_CODE(fs_copy_image_stencil),
                &meta_copy_image_ops->vk_fs_stencil_module)) < 0)
        {
            ERR("Failed to create shader modules, vr %d.\n", vr);
            goto fail;
        }
    }

    return S_OK;

fail:
    vkd3d_copy_image_ops_cleanup(meta_copy_image_ops, device);
    return hresult_from_vk_result(vr);
}

void vkd3d_copy_image_ops_cleanup(struct vkd3d_copy_image_ops *meta_copy_image_ops,
        struct d3d12_device *device)
{
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    size_t i;

    for (i = 0; i < meta_copy_image_ops->pipeline_count; i++)
    {
        struct vkd3d_copy_image_pipeline *pipeline = &meta_copy_image_ops->pipelines[i];

        VK_CALL(vkDestroyPipeline(device->vk_device, pipeline->vk_pipeline, NULL));
    }

    VK_CALL(vkDestroyDescriptorSetLayout(device->vk_device, meta_copy_image_ops->vk_set_layout, NULL));
    VK_CALL(vkDestroyPipelineLayout(device->vk_device, meta_copy_image_ops->vk_pipeline_layout, NULL));
    VK_CALL(vkDestroyShaderModule(device->vk_device, meta_copy_image_ops->vk_fs_float_module, NULL));
    VK_CALL(vkDestroyShaderModule(device->vk_device, meta_copy_image_ops->vk_fs_uint_module, NULL));
    VK_CALL(vkDestroyShaderModule(device->vk_device, meta_copy_image_ops->vk_fs_stencil_module, NULL));

    pthread_mutex_destroy(&meta_copy_image_ops->mutex);

    vkd3d_free(meta_copy_image_ops->pipelines);
}

static HRESULT vkd3d_meta_create_swapchain_pipeline(struct vkd3d_meta_ops *meta_ops,
        const struct vkd3d_swapchain_pipeline_key *key, struct vkd3d_swapchain_pipeline *pipeline)
{
    struct vkd3d_swapchain_ops *meta_swapchain_ops = &meta_ops->swapchain;
    VkPipelineColorBlendAttachmentState blend_att;
    VkPipelineColorBlendStateCreateInfo cb_state;
    VkResult vr;

    memset(&cb_state, 0, sizeof(cb_state));
    memset(&blend_att, 0, sizeof(blend_att));
    cb_state.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cb_state.attachmentCount = 1;
    cb_state.pAttachments = &blend_att;
    blend_att.colorWriteMask =
            VK_COLOR_COMPONENT_R_BIT |
            VK_COLOR_COMPONENT_G_BIT |
            VK_COLOR_COMPONENT_B_BIT |
            VK_COLOR_COMPONENT_A_BIT;

    if ((vr = vkd3d_meta_create_graphics_pipeline(meta_ops,
            meta_swapchain_ops->vk_pipeline_layouts[key->filter], key->format, VK_FORMAT_UNDEFINED, VK_IMAGE_ASPECT_COLOR_BIT,
            meta_swapchain_ops->vk_vs_module, meta_swapchain_ops->vk_fs_module, 1,
            NULL, &cb_state,
            NULL, false, &pipeline->vk_pipeline)) < 0)
        return hresult_from_vk_result(vr);

    pipeline->key = *key;
    return S_OK;
}

static HRESULT vkd3d_meta_create_copy_image_pipeline(struct vkd3d_meta_ops *meta_ops,
        const struct vkd3d_copy_image_pipeline_key *key, struct vkd3d_copy_image_pipeline *pipeline)
{
    struct vkd3d_copy_image_ops *meta_copy_image_ops = &meta_ops->copy_image;
    VkPipelineColorBlendAttachmentState blend_attachment;
    VkPipelineDepthStencilStateCreateInfo ds_state;
    VkPipelineColorBlendStateCreateInfo cb_state;
    VkSpecializationInfo spec_info;
    VkShaderModule vk_module;
    bool has_depth_target;
    VkResult vr;

    struct spec_data
    {
        enum vkd3d_meta_copy_mode mode;
    } spec_data;

    static const VkSpecializationMapEntry map_entries[] =
    {
        { 0, offsetof(struct spec_data, mode), sizeof(spec_data.mode) },
    };

    if (key->view_type == VK_IMAGE_VIEW_TYPE_1D_ARRAY)
    {
        spec_data.mode = VKD3D_META_COPY_MODE_1D;
    }
    else if (key->view_type == VK_IMAGE_VIEW_TYPE_2D_ARRAY)
    {
        spec_data.mode = key->sample_count == VK_SAMPLE_COUNT_1_BIT
                ? VKD3D_META_COPY_MODE_2D : VKD3D_META_COPY_MODE_MS;
    }
    else
    {
        ERR("Unhandled view type %u.\n", key->view_type);
        return E_INVALIDARG;
    }

    spec_info.mapEntryCount = ARRAY_SIZE(map_entries);
    spec_info.pMapEntries = map_entries;
    spec_info.dataSize = sizeof(spec_data);
    spec_info.pData = &spec_data;

    has_depth_target = (key->format->vk_aspect_mask & (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT)) != 0;

    ds_state.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    ds_state.pNext = NULL;
    ds_state.flags = 0;
    ds_state.depthTestEnable = (key->dst_aspect_mask & VK_IMAGE_ASPECT_DEPTH_BIT) ? VK_TRUE : VK_FALSE;
    ds_state.depthWriteEnable = ds_state.depthTestEnable;
    ds_state.depthCompareOp = VK_COMPARE_OP_ALWAYS;
    ds_state.depthBoundsTestEnable = VK_FALSE;

    if (key->dst_aspect_mask & VK_IMAGE_ASPECT_STENCIL_BIT)
    {
        ds_state.stencilTestEnable = VK_TRUE;
        ds_state.front.reference = 0;
        ds_state.front.writeMask = 0xff;
        ds_state.front.compareMask = 0xff;
        ds_state.front.passOp = VK_STENCIL_OP_REPLACE;
        ds_state.front.failOp = VK_STENCIL_OP_KEEP;
        ds_state.front.depthFailOp = VK_STENCIL_OP_KEEP;
        ds_state.front.compareOp = VK_COMPARE_OP_ALWAYS;
        ds_state.back = ds_state.front;
    }
    else
    {
        ds_state.stencilTestEnable = VK_FALSE;
        memset(&ds_state.front, 0, sizeof(ds_state.front));
        memset(&ds_state.back, 0, sizeof(ds_state.back));
    }

    ds_state.minDepthBounds = 0.0f;
    ds_state.maxDepthBounds = 1.0f;

    memset(&blend_attachment, 0, sizeof(blend_attachment));
    blend_attachment.blendEnable = VK_FALSE;
    blend_attachment.colorWriteMask =
            VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
            VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    cb_state.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cb_state.pNext = NULL;
    cb_state.flags = 0;
    cb_state.logicOpEnable = VK_FALSE;
    cb_state.logicOp = VK_LOGIC_OP_NO_OP;
    cb_state.attachmentCount = 1;
    cb_state.pAttachments = &blend_attachment;
    memset(&cb_state.blendConstants, 0, sizeof(cb_state.blendConstants));

    /* Special path when copying stencil -> color. */
    if (key->format->vk_format == VK_FORMAT_R8_UINT)
    {
        /* Special path when copying stencil -> color. */
        vk_module = meta_copy_image_ops->vk_fs_uint_module;
    }
    else if (key->dst_aspect_mask == VK_IMAGE_ASPECT_STENCIL_BIT)
    {
        /* FragStencilRef path. */
        vk_module = meta_copy_image_ops->vk_fs_stencil_module;
    }
    else
    {
        /* Depth or float color path. */
        vk_module = meta_copy_image_ops->vk_fs_float_module;
    }

    if ((vr = vkd3d_meta_create_graphics_pipeline(meta_ops,
            meta_copy_image_ops->vk_pipeline_layout,
            has_depth_target ? VK_FORMAT_UNDEFINED : key->format->vk_format,
            has_depth_target ? key->format->vk_format : VK_FORMAT_UNDEFINED,
            key->format->vk_aspect_mask,
            VK_NULL_HANDLE, vk_module, key->sample_count,
            has_depth_target ? &ds_state : NULL, has_depth_target ? NULL : &cb_state,
            &spec_info, true, &pipeline->vk_pipeline)) < 0)
        return hresult_from_vk_result(vr);

    pipeline->key = *key;
    return S_OK;
}

HRESULT vkd3d_meta_get_copy_image_pipeline(struct vkd3d_meta_ops *meta_ops,
        const struct vkd3d_copy_image_pipeline_key *key, struct vkd3d_copy_image_info *info)
{
    struct vkd3d_copy_image_ops *meta_copy_image_ops = &meta_ops->copy_image;
    struct vkd3d_copy_image_pipeline *pipeline;
    HRESULT hr;
    size_t i;
    int rc;

    if ((rc = pthread_mutex_lock(&meta_copy_image_ops->mutex)))
    {
        ERR("Failed to lock mutex, error %d.\n", rc);
        return hresult_from_errno(rc);
    }

    info->vk_set_layout = meta_copy_image_ops->vk_set_layout;
    info->vk_pipeline_layout = meta_copy_image_ops->vk_pipeline_layout;

    for (i = 0; i < meta_copy_image_ops->pipeline_count; i++)
    {
        pipeline = &meta_copy_image_ops->pipelines[i];

        if (!memcmp(key, &pipeline->key, sizeof(*key)))
        {
            info->vk_pipeline = pipeline->vk_pipeline;
            pthread_mutex_unlock(&meta_copy_image_ops->mutex);
            return S_OK;
        }
    }

    if (!vkd3d_array_reserve((void **)&meta_copy_image_ops->pipelines, &meta_copy_image_ops->pipelines_size,
            meta_copy_image_ops->pipeline_count + 1, sizeof(*meta_copy_image_ops->pipelines)))
    {
        ERR("Failed to reserve space for pipeline.\n");
        return E_OUTOFMEMORY;
    }

    pipeline = &meta_copy_image_ops->pipelines[meta_copy_image_ops->pipeline_count++];

    if (FAILED(hr = vkd3d_meta_create_copy_image_pipeline(meta_ops, key, pipeline)))
    {
        pthread_mutex_unlock(&meta_copy_image_ops->mutex);
        return hr;
    }

    info->vk_pipeline = pipeline->vk_pipeline;

    pthread_mutex_unlock(&meta_copy_image_ops->mutex);
    return S_OK;
}

VkImageViewType vkd3d_meta_get_copy_image_view_type(D3D12_RESOURCE_DIMENSION dim)
{
    switch (dim)
    {
        case D3D12_RESOURCE_DIMENSION_TEXTURE1D:
            return VK_IMAGE_VIEW_TYPE_1D_ARRAY;
        case D3D12_RESOURCE_DIMENSION_TEXTURE2D:
            return VK_IMAGE_VIEW_TYPE_2D_ARRAY;
        default:
            ERR("Unhandled resource dimension %u.\n", dim);
            return VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    }
}

const struct vkd3d_format *vkd3d_meta_get_copy_image_attachment_format(struct vkd3d_meta_ops *meta_ops,
        const struct vkd3d_format *dst_format, const struct vkd3d_format *src_format,
        VkImageAspectFlags dst_aspect, VkImageAspectFlags src_aspect)
{
    DXGI_FORMAT dxgi_format = DXGI_FORMAT_UNKNOWN;

    if (dst_aspect & (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT))
        return dst_format;

    assert(src_aspect & (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT));

    switch (src_format->vk_format)
    {
        case VK_FORMAT_D16_UNORM:
            dxgi_format = DXGI_FORMAT_R16_UNORM;
            break;
        case VK_FORMAT_D16_UNORM_S8_UINT:
            dxgi_format = (src_aspect & VK_IMAGE_ASPECT_DEPTH_BIT) ?
                    DXGI_FORMAT_R16_UNORM : DXGI_FORMAT_R8_UINT;
            break;
        case VK_FORMAT_D32_SFLOAT:
            dxgi_format = DXGI_FORMAT_R32_FLOAT;
            break;
        case VK_FORMAT_D32_SFLOAT_S8_UINT:
            dxgi_format = (src_aspect & VK_IMAGE_ASPECT_DEPTH_BIT) ?
                    DXGI_FORMAT_R32_FLOAT : DXGI_FORMAT_R8_UINT;
            break;
        default:
            ERR("Unhandled format %u.\n", src_format->vk_format);
            return NULL;
    }

    return vkd3d_get_format(meta_ops->device, dxgi_format, false);
}

static HRESULT vkd3d_meta_ops_common_init(struct vkd3d_meta_ops_common *meta_ops_common, struct d3d12_device *device)
{
    VkResult vr;

    if (device->device_info.vulkan_1_2_features.shaderOutputViewportIndex &&
            device->device_info.vulkan_1_2_features.shaderOutputLayer)
    {
        if ((vr = vkd3d_meta_create_shader_module(device, SPIRV_CODE(vs_fullscreen_layer), &meta_ops_common->vk_module_fullscreen_vs)) < 0)
        {
            ERR("Failed to create shader modules, vr %d.\n", vr);
            return hresult_from_vk_result(vr);
        }
    }
    else
    {
        if ((vr = vkd3d_meta_create_shader_module(device, SPIRV_CODE(vs_fullscreen), &meta_ops_common->vk_module_fullscreen_vs)) < 0 ||
            (vr = vkd3d_meta_create_shader_module(device, SPIRV_CODE(gs_fullscreen), &meta_ops_common->vk_module_fullscreen_gs)) < 0)
        {
            ERR("Failed to create shader modules, vr %d.\n", vr);
            return hresult_from_vk_result(vr);
        }
    }

    return S_OK;
}

static void vkd3d_meta_ops_common_cleanup(struct vkd3d_meta_ops_common *meta_ops_common, struct d3d12_device *device)
{
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;

    VK_CALL(vkDestroyShaderModule(device->vk_device, meta_ops_common->vk_module_fullscreen_vs, NULL));
    VK_CALL(vkDestroyShaderModule(device->vk_device, meta_ops_common->vk_module_fullscreen_gs, NULL));
}

HRESULT vkd3d_swapchain_ops_init(struct vkd3d_swapchain_ops *meta_swapchain_ops, struct d3d12_device *device)
{
    VkDescriptorSetLayoutBinding set_binding;
    unsigned int i;
    VkResult vr;
    int rc;

    memset(meta_swapchain_ops, 0, sizeof(*meta_swapchain_ops));

    if ((rc = pthread_mutex_init(&meta_swapchain_ops->mutex, NULL)))
    {
        ERR("Failed to initialize mutex, error %d.\n", rc);
        return hresult_from_errno(rc);
    }

    set_binding.binding = 0;
    set_binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    set_binding.descriptorCount = 1;
    set_binding.stageFlags = VK_SHADER_STAGE_ALL; /* Could be compute or graphics, so just use ALL. */

    for (i = 0; i < 2; i++)
    {
        if ((vr = vkd3d_meta_create_sampler(device, (VkFilter)i, &meta_swapchain_ops->vk_samplers[i])))
        {
            ERR("Failed to create sampler, vr, %d.\n", vr);
            goto fail;
        }

        set_binding.pImmutableSamplers = &meta_swapchain_ops->vk_samplers[i];
        if ((vr = vkd3d_meta_create_descriptor_set_layout(device, 1, &set_binding,
                false, &meta_swapchain_ops->vk_set_layouts[i])) < 0)
        {
            ERR("Failed to create descriptor set layout, vr %d.\n", vr);
            goto fail;
        }

        if ((vr = vkd3d_meta_create_pipeline_layout(device, 1, &meta_swapchain_ops->vk_set_layouts[i],
                0, NULL, &meta_swapchain_ops->vk_pipeline_layouts[i])))
        {
            ERR("Failed to create pipeline layout, vr %d.\n", vr);
            goto fail;
        }
    }

    if ((vr = vkd3d_meta_create_shader_module(device, SPIRV_CODE(vs_swapchain_fullscreen), &meta_swapchain_ops->vk_vs_module)) < 0)
    {
        ERR("Failed to create shader modules, vr %d.\n", vr);
        goto fail;
    }

    if ((vr = vkd3d_meta_create_shader_module(device, SPIRV_CODE(fs_swapchain_fullscreen), &meta_swapchain_ops->vk_fs_module)) < 0)
    {
        ERR("Failed to create shader modules, vr %d.\n", vr);
        goto fail;
    }

    return S_OK;

fail:
    vkd3d_swapchain_ops_cleanup(meta_swapchain_ops, device);
    return hresult_from_vk_result(vr);
}

void vkd3d_swapchain_ops_cleanup(struct vkd3d_swapchain_ops *meta_swapchain_ops, struct d3d12_device *device)
{
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    size_t i;

    for (i = 0; i < meta_swapchain_ops->pipeline_count; i++)
    {
        struct vkd3d_swapchain_pipeline *pipeline = &meta_swapchain_ops->pipelines[i];

        VK_CALL(vkDestroyPipeline(device->vk_device, pipeline->vk_pipeline, NULL));
    }

    for (i = 0; i < 2; i++)
    {
        VK_CALL(vkDestroyDescriptorSetLayout(device->vk_device, meta_swapchain_ops->vk_set_layouts[i], NULL));
        VK_CALL(vkDestroyPipelineLayout(device->vk_device, meta_swapchain_ops->vk_pipeline_layouts[i], NULL));
    }

    VK_CALL(vkDestroyShaderModule(device->vk_device, meta_swapchain_ops->vk_vs_module, NULL));
    VK_CALL(vkDestroyShaderModule(device->vk_device, meta_swapchain_ops->vk_fs_module, NULL));

    pthread_mutex_destroy(&meta_swapchain_ops->mutex);
    vkd3d_free(meta_swapchain_ops->pipelines);
}

HRESULT vkd3d_meta_get_swapchain_pipeline(struct vkd3d_meta_ops *meta_ops,
        const struct vkd3d_swapchain_pipeline_key *key, struct vkd3d_swapchain_info *info)
{
    struct vkd3d_swapchain_ops *meta_swapchain_ops = &meta_ops->swapchain;
    struct vkd3d_swapchain_pipeline *pipeline;
    HRESULT hr;
    size_t i;
    int rc;

    if ((rc = pthread_mutex_lock(&meta_swapchain_ops->mutex)))
    {
        ERR("Failed to lock mutex, error %d.\n", rc);
        return hresult_from_errno(rc);
    }

    info->vk_set_layout = meta_swapchain_ops->vk_set_layouts[key->filter];
    info->vk_pipeline_layout = meta_swapchain_ops->vk_pipeline_layouts[key->filter];

    for (i = 0; i < meta_swapchain_ops->pipeline_count; i++)
    {
        pipeline = &meta_swapchain_ops->pipelines[i];

        if (!memcmp(key, &pipeline->key, sizeof(*key)))
        {
            info->vk_pipeline = pipeline->vk_pipeline;
            pthread_mutex_unlock(&meta_swapchain_ops->mutex);
            return S_OK;
        }
    }

    if (!vkd3d_array_reserve((void **)&meta_swapchain_ops->pipelines, &meta_swapchain_ops->pipelines_size,
            meta_swapchain_ops->pipeline_count + 1, sizeof(*meta_swapchain_ops->pipelines)))
    {
        ERR("Failed to reserve space for pipeline.\n");
        return E_OUTOFMEMORY;
    }

    pipeline = &meta_swapchain_ops->pipelines[meta_swapchain_ops->pipeline_count++];

    if (FAILED(hr = vkd3d_meta_create_swapchain_pipeline(meta_ops, key, pipeline)))
    {
        pthread_mutex_unlock(&meta_swapchain_ops->mutex);
        return hr;
    }

    info->vk_pipeline = pipeline->vk_pipeline;

    pthread_mutex_unlock(&meta_swapchain_ops->mutex);
    return S_OK;
}

HRESULT vkd3d_query_ops_init(struct vkd3d_query_ops *meta_query_ops,
        struct d3d12_device *device)
{
    VkPushConstantRange push_constant_range;
    VkSpecializationInfo spec_info;
    uint32_t field_count;
    VkResult vr;

    static const VkSpecializationMapEntry spec_map = { 0, 0, sizeof(uint32_t) };

    push_constant_range.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    push_constant_range.offset = 0;
    push_constant_range.size = sizeof(struct vkd3d_query_gather_args);

    if ((vr = vkd3d_meta_create_pipeline_layout(device, 0, NULL,
            1, &push_constant_range, &meta_query_ops->vk_gather_pipeline_layout)) < 0)
        goto fail;

    spec_info.mapEntryCount = 1;
    spec_info.pMapEntries = &spec_map;
    spec_info.dataSize = sizeof(field_count);
    spec_info.pData = &field_count;

    field_count = 1;
    if ((vr = vkd3d_meta_create_compute_pipeline(device, sizeof(cs_resolve_query), cs_resolve_query,
            meta_query_ops->vk_gather_pipeline_layout, &spec_info, true, &meta_query_ops->vk_gather_occlusion_pipeline)) < 0)
        goto fail;

    field_count = 2;
    if ((vr = vkd3d_meta_create_compute_pipeline(device, sizeof(cs_resolve_query), cs_resolve_query,
            meta_query_ops->vk_gather_pipeline_layout, &spec_info, true, &meta_query_ops->vk_gather_so_statistics_pipeline)) < 0)
        goto fail;

    push_constant_range.size = sizeof(struct vkd3d_query_resolve_args);

    if ((vr = vkd3d_meta_create_pipeline_layout(device, 0, NULL,
            1, &push_constant_range, &meta_query_ops->vk_resolve_pipeline_layout)) < 0)
        goto fail;

    if ((vr = vkd3d_meta_create_compute_pipeline(device, sizeof(cs_resolve_binary_queries), cs_resolve_binary_queries,
            meta_query_ops->vk_resolve_pipeline_layout, NULL, true, &meta_query_ops->vk_resolve_binary_pipeline)) < 0)
        goto fail;

    return S_OK;

fail:
    vkd3d_query_ops_cleanup(meta_query_ops, device);
    return hresult_from_vk_result(vr);
}

void vkd3d_query_ops_cleanup(struct vkd3d_query_ops *meta_query_ops,
        struct d3d12_device *device)
{
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;

    VK_CALL(vkDestroyPipeline(device->vk_device, meta_query_ops->vk_gather_occlusion_pipeline, NULL));
    VK_CALL(vkDestroyPipeline(device->vk_device, meta_query_ops->vk_gather_so_statistics_pipeline, NULL));

    VK_CALL(vkDestroyPipelineLayout(device->vk_device, meta_query_ops->vk_gather_pipeline_layout, NULL));
    VK_CALL(vkDestroyPipelineLayout(device->vk_device, meta_query_ops->vk_resolve_pipeline_layout, NULL));
    VK_CALL(vkDestroyPipeline(device->vk_device, meta_query_ops->vk_resolve_binary_pipeline, NULL));
}

bool vkd3d_meta_get_query_gather_pipeline(struct vkd3d_meta_ops *meta_ops,
        D3D12_QUERY_HEAP_TYPE heap_type, struct vkd3d_query_gather_info *info)
{
    const struct vkd3d_query_ops *query_ops = &meta_ops->query;
    info->vk_pipeline_layout = query_ops->vk_gather_pipeline_layout;

    switch (heap_type)
    {
        case D3D12_QUERY_HEAP_TYPE_OCCLUSION:
            info->vk_pipeline = query_ops->vk_gather_occlusion_pipeline;
            return true;
        case D3D12_QUERY_HEAP_TYPE_SO_STATISTICS:
            info->vk_pipeline = query_ops->vk_gather_so_statistics_pipeline;
            return true;
        default:
            ERR("No pipeline for query heap type %u.\n", heap_type);
            return false;
    }
}

HRESULT vkd3d_multi_dispatch_indirect_ops_init(
        struct vkd3d_multi_dispatch_indirect_ops *meta_multi_dispatch_indirect_ops,
        struct d3d12_device *device)
{
    VkPushConstantRange push_constant_range;
    VkResult vr;

    memset(meta_multi_dispatch_indirect_ops, 0, sizeof(*meta_multi_dispatch_indirect_ops));
    push_constant_range.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    push_constant_range.offset = 0;
    push_constant_range.size = sizeof(struct vkd3d_multi_dispatch_indirect_args);

    if ((vr = vkd3d_meta_create_pipeline_layout(device, 0, NULL, 1,
            &push_constant_range, &meta_multi_dispatch_indirect_ops->vk_multi_dispatch_indirect_layout)) < 0)
        goto fail;

    push_constant_range.size = sizeof(struct vkd3d_multi_dispatch_indirect_state_args);

    if ((vr = vkd3d_meta_create_pipeline_layout(device, 0, NULL, 1,
            &push_constant_range, &meta_multi_dispatch_indirect_ops->vk_multi_dispatch_indirect_state_layout)) < 0)
        goto fail;

    if ((vr = vkd3d_meta_create_compute_pipeline(device,
            sizeof(cs_execute_indirect_multi_dispatch), cs_execute_indirect_multi_dispatch,
            meta_multi_dispatch_indirect_ops->vk_multi_dispatch_indirect_layout, NULL, true,
            &meta_multi_dispatch_indirect_ops->vk_multi_dispatch_indirect_pipeline)) < 0)
        goto fail;

    if ((vr = vkd3d_meta_create_compute_pipeline(device,
            sizeof(cs_execute_indirect_multi_dispatch_state), cs_execute_indirect_multi_dispatch_state,
            meta_multi_dispatch_indirect_ops->vk_multi_dispatch_indirect_state_layout, NULL, true,
            &meta_multi_dispatch_indirect_ops->vk_multi_dispatch_indirect_state_pipeline)) < 0)
        goto fail;

    return S_OK;

fail:
    vkd3d_multi_dispatch_indirect_ops_cleanup(meta_multi_dispatch_indirect_ops, device);
    return hresult_from_vk_result(vr);
}

void vkd3d_multi_dispatch_indirect_ops_cleanup(
        struct vkd3d_multi_dispatch_indirect_ops *meta_multi_dispatch_indirect_ops,
        struct d3d12_device *device)
{
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    VK_CALL(vkDestroyPipeline(device->vk_device,
            meta_multi_dispatch_indirect_ops->vk_multi_dispatch_indirect_pipeline, NULL));
    VK_CALL(vkDestroyPipeline(device->vk_device,
            meta_multi_dispatch_indirect_ops->vk_multi_dispatch_indirect_state_pipeline, NULL));
    VK_CALL(vkDestroyPipelineLayout(device->vk_device,
            meta_multi_dispatch_indirect_ops->vk_multi_dispatch_indirect_layout, NULL));
    VK_CALL(vkDestroyPipelineLayout(device->vk_device,
            meta_multi_dispatch_indirect_ops->vk_multi_dispatch_indirect_state_layout, NULL));
}

HRESULT vkd3d_predicate_ops_init(struct vkd3d_predicate_ops *meta_predicate_ops,
        struct d3d12_device *device)
{
    VkPushConstantRange push_constant_range;
    VkSpecializationInfo spec_info;
    VkResult vr;
    size_t i;

    static const struct spec_data
    {
        uint32_t arg_count;
        VkBool32 arg_indirect;
    }
    spec_data[] =
    {
        { 4, VK_FALSE }, /* VKD3D_PREDICATE_OP_DRAW */
        { 5, VK_FALSE }, /* VKD3D_PREDICATE_OP_DRAW_INDEXED */
        { 1, VK_FALSE }, /* VKD3D_PREDICATE_OP_DRAW_INDIRECT */
        { 1, VK_TRUE  }, /* VKD3D_PREDICATE_OP_DRAW_INDIRECT_COUNT */
        { 3, VK_FALSE }, /* VKD3D_PREDICATE_OP_DISPATCH */
        { 3, VK_TRUE  }, /* VKD3D_PREDICATE_OP_DISPATCH_INDIRECT */
    };

    static const VkSpecializationMapEntry spec_map[] =
    {
        { 0, offsetof(struct spec_data, arg_count), sizeof(uint32_t) },
        { 1, offsetof(struct spec_data, arg_indirect), sizeof(VkBool32) },
    };

    memset(meta_predicate_ops, 0, sizeof(*meta_predicate_ops));
    push_constant_range.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    push_constant_range.offset = 0;
    push_constant_range.size = sizeof(struct vkd3d_predicate_command_args);

    if ((vr = vkd3d_meta_create_pipeline_layout(device, 0, NULL, 1,
            &push_constant_range, &meta_predicate_ops->vk_command_pipeline_layout)) < 0)
        return hresult_from_vk_result(vr);

    push_constant_range.size = sizeof(struct vkd3d_predicate_resolve_args);
    if ((vr = vkd3d_meta_create_pipeline_layout(device, 0, NULL, 1,
            &push_constant_range, &meta_predicate_ops->vk_resolve_pipeline_layout)) < 0)
        return hresult_from_vk_result(vr);

    spec_info.mapEntryCount = ARRAY_SIZE(spec_map);
    spec_info.pMapEntries = spec_map;
    spec_info.dataSize = sizeof(struct spec_data);

    for (i = 0; i < ARRAY_SIZE(spec_data); i++)
    {
        spec_info.pData = &spec_data[i];

        if ((vr = vkd3d_meta_create_compute_pipeline(device, sizeof(cs_predicate_command), cs_predicate_command,
                meta_predicate_ops->vk_command_pipeline_layout, &spec_info, true, &meta_predicate_ops->vk_command_pipelines[i])) < 0)
            goto fail;

        meta_predicate_ops->data_sizes[i] = spec_data[i].arg_count * sizeof(uint32_t);
    }

    if (device->device_info.device_generated_commands_features_nv.deviceGeneratedCommands)
    {
        uint32_t num_active_words;
        spec_info.mapEntryCount = 1;
        spec_info.dataSize = sizeof(uint32_t);
        spec_info.pData = &num_active_words;

        num_active_words = 2; /* vertex/indexCount and instanceCount */
        if ((vr = vkd3d_meta_create_compute_pipeline(device, sizeof(cs_predicate_command_execute_indirect),
                cs_predicate_command_execute_indirect,
                meta_predicate_ops->vk_command_pipeline_layout, &spec_info, true,
                &meta_predicate_ops->vk_command_pipelines[VKD3D_PREDICATE_COMMAND_EXECUTE_INDIRECT_GRAPHICS])) < 0)
            goto fail;

        num_active_words = 3; /* X, Y and Z */
        if ((vr = vkd3d_meta_create_compute_pipeline(device, sizeof(cs_predicate_command_execute_indirect),
                cs_predicate_command_execute_indirect,
                meta_predicate_ops->vk_command_pipeline_layout, &spec_info, true,
                &meta_predicate_ops->vk_command_pipelines[VKD3D_PREDICATE_COMMAND_EXECUTE_INDIRECT_COMPUTE])) < 0)
            goto fail;

        meta_predicate_ops->data_sizes[VKD3D_PREDICATE_COMMAND_EXECUTE_INDIRECT_GRAPHICS] = sizeof(uint32_t);
        meta_predicate_ops->data_sizes[VKD3D_PREDICATE_COMMAND_EXECUTE_INDIRECT_COMPUTE] = sizeof(uint32_t);
    }

    if ((vr = vkd3d_meta_create_compute_pipeline(device, sizeof(cs_resolve_predicate), cs_resolve_predicate,
            meta_predicate_ops->vk_resolve_pipeline_layout, NULL, true, &meta_predicate_ops->vk_resolve_pipeline)) < 0)
        goto fail;

    return S_OK;

fail:
    vkd3d_predicate_ops_cleanup(meta_predicate_ops, device);
    return hresult_from_vk_result(vr);
}

void vkd3d_predicate_ops_cleanup(struct vkd3d_predicate_ops *meta_predicate_ops,
        struct d3d12_device *device)
{
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    size_t i;

    for (i = 0; i < VKD3D_PREDICATE_COMMAND_COUNT; i++)
        VK_CALL(vkDestroyPipeline(device->vk_device, meta_predicate_ops->vk_command_pipelines[i], NULL));
    VK_CALL(vkDestroyPipeline(device->vk_device, meta_predicate_ops->vk_resolve_pipeline, NULL));

    VK_CALL(vkDestroyPipelineLayout(device->vk_device, meta_predicate_ops->vk_command_pipeline_layout, NULL));
    VK_CALL(vkDestroyPipelineLayout(device->vk_device, meta_predicate_ops->vk_resolve_pipeline_layout, NULL));
}

void vkd3d_meta_get_predicate_pipeline(struct vkd3d_meta_ops *meta_ops,
        enum vkd3d_predicate_command_type command_type, struct vkd3d_predicate_command_info *info)
{
    const struct vkd3d_predicate_ops *predicate_ops = &meta_ops->predicate;

    info->vk_pipeline_layout = predicate_ops->vk_command_pipeline_layout;
    info->vk_pipeline = predicate_ops->vk_command_pipelines[command_type];
    info->data_size = predicate_ops->data_sizes[command_type];
}

void vkd3d_meta_get_multi_dispatch_indirect_pipeline(struct vkd3d_meta_ops *meta_ops,
        struct vkd3d_multi_dispatch_indirect_info *info)
{
    info->vk_pipeline = meta_ops->multi_dispatch_indirect.vk_multi_dispatch_indirect_pipeline;
    info->vk_pipeline_layout = meta_ops->multi_dispatch_indirect.vk_multi_dispatch_indirect_layout;
}

void vkd3d_meta_get_multi_dispatch_indirect_state_pipeline(struct vkd3d_meta_ops *meta_ops,
        struct vkd3d_multi_dispatch_indirect_info *info)
{
    info->vk_pipeline = meta_ops->multi_dispatch_indirect.vk_multi_dispatch_indirect_state_pipeline;
    info->vk_pipeline_layout = meta_ops->multi_dispatch_indirect.vk_multi_dispatch_indirect_state_layout;
}

HRESULT vkd3d_execute_indirect_ops_init(struct vkd3d_execute_indirect_ops *meta_indirect_ops,
        struct d3d12_device *device)
{
    VkPushConstantRange push_constant_range;
    VkResult vr;
    int rc;

    if ((rc = pthread_mutex_init(&meta_indirect_ops->mutex, NULL)))
        return hresult_from_errno(rc);

    push_constant_range.offset = 0;
    push_constant_range.size = sizeof(struct vkd3d_execute_indirect_args);
    push_constant_range.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    if ((vr = vkd3d_meta_create_pipeline_layout(device, 0, NULL, 1,
            &push_constant_range, &meta_indirect_ops->vk_pipeline_layout)) < 0)
    {
        pthread_mutex_destroy(&meta_indirect_ops->mutex);
        return hresult_from_vk_result(vr);
    }

    meta_indirect_ops->pipelines_count = 0;
    meta_indirect_ops->pipelines_size = 0;
    meta_indirect_ops->pipelines = NULL;
    return S_OK;
}

struct vkd3d_meta_execute_indirect_spec_constant_data
{
    struct vkd3d_shader_debug_ring_spec_constants constants;
    uint32_t workgroup_size_x;
};

HRESULT vkd3d_meta_get_execute_indirect_pipeline(struct vkd3d_meta_ops *meta_ops,
        uint32_t patch_command_count, struct vkd3d_execute_indirect_info *info)
{
    struct vkd3d_meta_execute_indirect_spec_constant_data execute_indirect_spec_constants;
    VkSpecializationMapEntry map_entry[VKD3D_SHADER_DEBUG_RING_SPEC_INFO_MAP_ENTRIES + 1];
    struct vkd3d_execute_indirect_ops *meta_indirect_ops = &meta_ops->execute_indirect;
    struct vkd3d_shader_debug_ring_spec_info debug_ring_info;

    VkSpecializationInfo spec;
    HRESULT hr = S_OK;
    VkResult vr;
    bool debug;
    size_t i;
    int rc;

    if ((rc = pthread_mutex_lock(&meta_indirect_ops->mutex)))
    {
        ERR("Failed to lock mutex, error %d.\n", rc);
        return hresult_from_errno(rc);
    }

    for (i = 0; i < meta_indirect_ops->pipelines_count; i++)
    {
        if (meta_indirect_ops->pipelines[i].workgroup_size_x == patch_command_count)
        {
            info->vk_pipeline_layout = meta_indirect_ops->vk_pipeline_layout;
            info->vk_pipeline = meta_indirect_ops->pipelines[i].vk_pipeline;
            goto out;
        }
    }

    debug = meta_ops->device->debug_ring.active;

    /* If we have debug ring, we can dump indirect command buffer data to the ring as well.
     * Vital for debugging broken execute indirect data with templates. */
    if (debug)
    {
        vkd3d_shader_debug_ring_init_spec_constant(meta_ops->device, &debug_ring_info,
                0 /* Reserve this hash for internal debug streams. */);

        memset(&execute_indirect_spec_constants, 0, sizeof(execute_indirect_spec_constants));
        execute_indirect_spec_constants.constants = debug_ring_info.constants;
        execute_indirect_spec_constants.workgroup_size_x = patch_command_count;

        memcpy(map_entry, debug_ring_info.map_entries, sizeof(debug_ring_info.map_entries));
        map_entry[VKD3D_SHADER_DEBUG_RING_SPEC_INFO_MAP_ENTRIES].constantID = 4;
        map_entry[VKD3D_SHADER_DEBUG_RING_SPEC_INFO_MAP_ENTRIES].offset =
                offsetof(struct vkd3d_meta_execute_indirect_spec_constant_data, workgroup_size_x);
        map_entry[VKD3D_SHADER_DEBUG_RING_SPEC_INFO_MAP_ENTRIES].size = sizeof(patch_command_count);

        spec.pMapEntries = map_entry;
        spec.pData = &execute_indirect_spec_constants;
        spec.mapEntryCount = ARRAY_SIZE(map_entry);
        spec.dataSize = sizeof(execute_indirect_spec_constants);
    }
    else
    {
        map_entry[0].constantID = 0;
        map_entry[0].offset = 0;
        map_entry[0].size = sizeof(patch_command_count);

        spec.pMapEntries = map_entry;
        spec.pData = &patch_command_count;
        spec.mapEntryCount = 1;
        spec.dataSize = sizeof(patch_command_count);
    }

    vkd3d_array_reserve((void**)&meta_indirect_ops->pipelines, &meta_indirect_ops->pipelines_size,
            meta_indirect_ops->pipelines_count + 1, sizeof(*meta_indirect_ops->pipelines));

    meta_indirect_ops->pipelines[meta_indirect_ops->pipelines_count].workgroup_size_x = patch_command_count;

    vr = vkd3d_meta_create_compute_pipeline(meta_ops->device,
            debug ? sizeof(cs_execute_indirect_patch_debug_ring) : sizeof(cs_execute_indirect_patch),
            debug ? cs_execute_indirect_patch_debug_ring : cs_execute_indirect_patch,
            meta_indirect_ops->vk_pipeline_layout, &spec,
            true, &meta_indirect_ops->pipelines[meta_indirect_ops->pipelines_count].vk_pipeline);

    if (vr)
    {
        hr = hresult_from_vk_result(vr);
        goto out;
    }

    info->vk_pipeline_layout = meta_indirect_ops->vk_pipeline_layout;
    info->vk_pipeline = meta_indirect_ops->pipelines[meta_indirect_ops->pipelines_count].vk_pipeline;
    meta_indirect_ops->pipelines_count++;

out:
    pthread_mutex_unlock(&meta_indirect_ops->mutex);
    return hr;
}

void vkd3d_execute_indirect_ops_cleanup(struct vkd3d_execute_indirect_ops *meta_indirect_ops,
        struct d3d12_device *device)
{
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    size_t i;

    for (i = 0; i < meta_indirect_ops->pipelines_count; i++)
        VK_CALL(vkDestroyPipeline(device->vk_device, meta_indirect_ops->pipelines[i].vk_pipeline, NULL));
    VK_CALL(vkDestroyPipelineLayout(device->vk_device, meta_indirect_ops->vk_pipeline_layout, NULL));
    pthread_mutex_destroy(&meta_indirect_ops->mutex);
}

static HRESULT vkd3d_dstorage_ops_init(struct vkd3d_dstorage_ops *dstorage_ops, struct d3d12_device *device)
{
    VkPushConstantRange push_range;
    VkResult vr;

    if (!device->device_info.features2.features.shaderInt64 ||
            !(device->device_info.vulkan_1_1_properties.subgroupSupportedOperations & VK_SUBGROUP_FEATURE_BALLOT_BIT))
        return S_OK;

    push_range.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    push_range.offset = 0;
    push_range.size = sizeof(struct vkd3d_dstorage_emit_nv_memory_decompression_regions_args);

    if ((vr = vkd3d_meta_create_pipeline_layout(device, 0, NULL, 1, &push_range,
            &dstorage_ops->vk_emit_nv_memory_decompression_regions_layout)))
        return hresult_from_vk_result(vr);

    if ((vr = vkd3d_meta_create_compute_pipeline(device,
            sizeof(cs_emit_nv_memory_decompression_regions),
            cs_emit_nv_memory_decompression_regions,
            dstorage_ops->vk_emit_nv_memory_decompression_regions_layout,
            NULL, false, &dstorage_ops->vk_emit_nv_memory_decompression_regions_pipeline)))
        return hresult_from_vk_result(vr);

    if ((vr = vkd3d_meta_create_compute_pipeline(device,
            sizeof(cs_emit_nv_memory_decompression_workgroups),
            cs_emit_nv_memory_decompression_workgroups,
            dstorage_ops->vk_emit_nv_memory_decompression_regions_layout,
            NULL, false, &dstorage_ops->vk_emit_nv_memory_decompression_workgroups_pipeline)))
        return hresult_from_vk_result(vr);

    return S_OK;
}

static void vkd3d_dstorage_ops_cleanup(struct vkd3d_dstorage_ops *dstorage_ops, struct d3d12_device *device)
{
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;

    VK_CALL(vkDestroyPipeline(device->vk_device, dstorage_ops->vk_emit_nv_memory_decompression_regions_pipeline, NULL));
    VK_CALL(vkDestroyPipeline(device->vk_device, dstorage_ops->vk_emit_nv_memory_decompression_workgroups_pipeline, NULL));

    VK_CALL(vkDestroyPipelineLayout(device->vk_device, dstorage_ops->vk_emit_nv_memory_decompression_regions_layout, NULL));
}

HRESULT vkd3d_meta_ops_init(struct vkd3d_meta_ops *meta_ops, struct d3d12_device *device)
{
    HRESULT hr;

    memset(meta_ops, 0, sizeof(*meta_ops));
    meta_ops->device = device;

    if (FAILED(hr = vkd3d_meta_ops_common_init(&meta_ops->common, device)))
        goto fail_common;

    if (FAILED(hr = vkd3d_clear_uav_ops_init(&meta_ops->clear_uav, device)))
        goto fail_clear_uav_ops;

    if (FAILED(hr = vkd3d_copy_image_ops_init(&meta_ops->copy_image, device)))
        goto fail_copy_image_ops;

    if (FAILED(hr = vkd3d_swapchain_ops_init(&meta_ops->swapchain, device)))
        goto fail_swapchain_ops;

    if (FAILED(hr = vkd3d_query_ops_init(&meta_ops->query, device)))
        goto fail_query_ops;

    if (FAILED(hr = vkd3d_predicate_ops_init(&meta_ops->predicate, device)))
        goto fail_predicate_ops;

    if (FAILED(hr = vkd3d_execute_indirect_ops_init(&meta_ops->execute_indirect, device)))
        goto fail_execute_indirect_ops;

    if (FAILED(hr = vkd3d_multi_dispatch_indirect_ops_init(&meta_ops->multi_dispatch_indirect, device)))
        goto fail_multi_dispatch_indirect_ops;

    if (FAILED(hr = vkd3d_dstorage_ops_init(&meta_ops->dstorage, device)))
        goto fail_dstorage_ops;

    return S_OK;

fail_dstorage_ops:
    vkd3d_multi_dispatch_indirect_ops_cleanup(&meta_ops->multi_dispatch_indirect, device);
fail_multi_dispatch_indirect_ops:
    vkd3d_execute_indirect_ops_cleanup(&meta_ops->execute_indirect, device);
fail_execute_indirect_ops:
    vkd3d_predicate_ops_cleanup(&meta_ops->predicate, device);
fail_predicate_ops:
    vkd3d_query_ops_cleanup(&meta_ops->query, device);
fail_query_ops:
    vkd3d_swapchain_ops_cleanup(&meta_ops->swapchain, device);
fail_swapchain_ops:
    vkd3d_copy_image_ops_cleanup(&meta_ops->copy_image, device);
fail_copy_image_ops:
    vkd3d_clear_uav_ops_cleanup(&meta_ops->clear_uav, device);
fail_clear_uav_ops:
    vkd3d_meta_ops_common_cleanup(&meta_ops->common, device);
fail_common:
    return hr;
}

HRESULT vkd3d_meta_ops_cleanup(struct vkd3d_meta_ops *meta_ops, struct d3d12_device *device)
{
    vkd3d_dstorage_ops_cleanup(&meta_ops->dstorage, device);
    vkd3d_multi_dispatch_indirect_ops_cleanup(&meta_ops->multi_dispatch_indirect, device);
    vkd3d_execute_indirect_ops_cleanup(&meta_ops->execute_indirect, device);
    vkd3d_predicate_ops_cleanup(&meta_ops->predicate, device);
    vkd3d_query_ops_cleanup(&meta_ops->query, device);
    vkd3d_swapchain_ops_cleanup(&meta_ops->swapchain, device);
    vkd3d_copy_image_ops_cleanup(&meta_ops->copy_image, device);
    vkd3d_clear_uav_ops_cleanup(&meta_ops->clear_uav, device);
    vkd3d_meta_ops_common_cleanup(&meta_ops->common, device);
    return S_OK;
}
