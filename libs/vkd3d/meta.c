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

#include "vkd3d_private.h"
#include "vkd3d_shaders.h"

#define SPIRV_CODE(name) name, sizeof(name)

static VkResult vkd3d_create_shader_module(struct d3d12_device *device,
        size_t code_size, const uint32_t *code, VkShaderModule *module)
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

static VkResult vkd3d_create_descriptor_set_layout(struct d3d12_device *device,
        uint32_t binding_count, const VkDescriptorSetLayoutBinding *bindings, VkDescriptorSetLayout *set_layout)
{
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    VkDescriptorSetLayoutCreateInfo set_layout_info;

    set_layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    set_layout_info.pNext = NULL;
    set_layout_info.flags = 0;
    set_layout_info.bindingCount = binding_count;
    set_layout_info.pBindings = bindings;

    return VK_CALL(vkCreateDescriptorSetLayout(device->vk_device, &set_layout_info, NULL, set_layout));
}

static VkResult vkd3d_create_pipeline_layout(struct d3d12_device *device,
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

static VkResult vkd3d_create_compute_pipeline(struct d3d12_device *device,
        size_t code_size, const uint32_t *code, VkPipelineLayout layout,
        const VkSpecializationInfo *specialization_info, VkPipeline *pipeline)
{
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    VkComputePipelineCreateInfo pipeline_info;
    VkResult vr;

    pipeline_info.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipeline_info.pNext = NULL;
    pipeline_info.flags = 0;
    pipeline_info.layout = layout;
    pipeline_info.basePipelineHandle = VK_NULL_HANDLE;
    pipeline_info.basePipelineIndex = -1;

    pipeline_info.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    pipeline_info.stage.pNext = NULL;
    pipeline_info.stage.flags = 0;
    pipeline_info.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    pipeline_info.stage.pName = "main";
    pipeline_info.stage.pSpecializationInfo = specialization_info;

    if ((vr = vkd3d_create_shader_module(device, code_size, code, &pipeline_info.stage.module)) < 0)
    {
        ERR("Failed to create shader module, vr %d.", vr);
        return vr;
    }

    vr = VK_CALL(vkCreateComputePipelines(device->vk_device, VK_NULL_HANDLE, 1, &pipeline_info, NULL, pipeline));
    VK_CALL(vkDestroyShaderModule(device->vk_device, pipeline_info.stage.module, NULL));

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
      { &meta_clear_uav_ops->vk_set_layout_buffer, &meta_clear_uav_ops->vk_pipeline_layout_buffer, VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER },
      { &meta_clear_uav_ops->vk_set_layout_image,  &meta_clear_uav_ops->vk_pipeline_layout_image,  VK_DESCRIPTOR_TYPE_STORAGE_IMAGE },
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
        SPIRV_CODE(cs_clear_uav_buffer_float_spv) },
      { &meta_clear_uav_ops->clear_float.image_1d,
        &meta_clear_uav_ops->vk_pipeline_layout_image,
        SPIRV_CODE(cs_clear_uav_image_1d_float_spv) },
      { &meta_clear_uav_ops->clear_float.image_1d_array,
        &meta_clear_uav_ops->vk_pipeline_layout_image,
        SPIRV_CODE(cs_clear_uav_image_1d_array_float_spv) },
      { &meta_clear_uav_ops->clear_float.image_2d,
        &meta_clear_uav_ops->vk_pipeline_layout_image,
        SPIRV_CODE(cs_clear_uav_image_2d_float_spv) },
      { &meta_clear_uav_ops->clear_float.image_2d_array,
        &meta_clear_uav_ops->vk_pipeline_layout_image,
        SPIRV_CODE(cs_clear_uav_image_2d_array_float_spv) },
      { &meta_clear_uav_ops->clear_float.image_3d,
        &meta_clear_uav_ops->vk_pipeline_layout_image,
        SPIRV_CODE(cs_clear_uav_image_3d_float_spv) },
      { &meta_clear_uav_ops->clear_uint.buffer,
        &meta_clear_uav_ops->vk_pipeline_layout_buffer,
        SPIRV_CODE(cs_clear_uav_buffer_uint_spv) },
      { &meta_clear_uav_ops->clear_uint.image_1d,
        &meta_clear_uav_ops->vk_pipeline_layout_image,
        SPIRV_CODE(cs_clear_uav_image_1d_uint_spv) },
      { &meta_clear_uav_ops->clear_uint.image_1d_array,
        &meta_clear_uav_ops->vk_pipeline_layout_image,
        SPIRV_CODE(cs_clear_uav_image_1d_array_uint_spv) },
      { &meta_clear_uav_ops->clear_uint.image_2d,
        &meta_clear_uav_ops->vk_pipeline_layout_image,
        SPIRV_CODE(cs_clear_uav_image_2d_uint_spv) },
      { &meta_clear_uav_ops->clear_uint.image_2d_array,
        &meta_clear_uav_ops->vk_pipeline_layout_image,
        SPIRV_CODE(cs_clear_uav_image_2d_array_uint_spv) },
      { &meta_clear_uav_ops->clear_uint.image_3d,
        &meta_clear_uav_ops->vk_pipeline_layout_image,
        SPIRV_CODE(cs_clear_uav_image_3d_uint_spv) },
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

        vr = vkd3d_create_descriptor_set_layout(device, 1, &set_binding, set_layouts[i].set_layout);

        if (vr < 0)
        {
            ERR("Failed to create descriptor set layout %u, vr %d.", i, vr);
            goto fail;
        }

        vr = vkd3d_create_pipeline_layout(device, 1, set_layouts[i].set_layout,
                1, &push_constant_range, set_layouts[i].pipeline_layout);

        if (vr < 0)
        {
            ERR("Failed to create pipeline layout %u, vr %d.", i, vr);
            goto fail;
        }
    }

    for (i = 0; i < ARRAY_SIZE(pipelines); i++)
    {
        if ((vr = vkd3d_create_compute_pipeline(device, pipelines[i].code_size, pipelines[i].code,
                *pipelines[i].pipeline_layout, NULL, pipelines[i].pipeline)) < 0)
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

    VK_CALL(vkDestroyDescriptorSetLayout(device->vk_device, meta_clear_uav_ops->vk_set_layout_buffer, NULL));
    VK_CALL(vkDestroyDescriptorSetLayout(device->vk_device, meta_clear_uav_ops->vk_set_layout_image, NULL));

    VK_CALL(vkDestroyPipelineLayout(device->vk_device, meta_clear_uav_ops->vk_pipeline_layout_buffer, NULL));
    VK_CALL(vkDestroyPipelineLayout(device->vk_device, meta_clear_uav_ops->vk_pipeline_layout_image, NULL));

    for (i = 0; i < ARRAY_SIZE(pipeline_sets); i++)
    {
        VK_CALL(vkDestroyPipeline(device->vk_device, pipeline_sets[i]->buffer, NULL));
        VK_CALL(vkDestroyPipeline(device->vk_device, pipeline_sets[i]->image_1d, NULL));
        VK_CALL(vkDestroyPipeline(device->vk_device, pipeline_sets[i]->image_2d, NULL));
        VK_CALL(vkDestroyPipeline(device->vk_device, pipeline_sets[i]->image_3d, NULL));
        VK_CALL(vkDestroyPipeline(device->vk_device, pipeline_sets[i]->image_1d_array, NULL));
        VK_CALL(vkDestroyPipeline(device->vk_device, pipeline_sets[i]->image_2d_array, NULL));
    }
}

struct vkd3d_clear_uav_pipeline vkd3d_clear_uav_ops_get_clear_buffer_pipeline(const struct vkd3d_clear_uav_ops *meta_clear_uav_ops,
        bool as_uint)
{
    struct vkd3d_clear_uav_pipeline info;

    const struct vkd3d_clear_uav_pipelines *pipelines = as_uint
            ? &meta_clear_uav_ops->clear_uint
            : &meta_clear_uav_ops->clear_float;

    info.vk_set_layout = meta_clear_uav_ops->vk_set_layout_buffer;
    info.vk_pipeline_layout = meta_clear_uav_ops->vk_pipeline_layout_buffer;
    info.vk_pipeline = pipelines->buffer;
    return info;
}

struct vkd3d_clear_uav_pipeline vkd3d_clear_uav_ops_get_clear_image_pipeline(const struct vkd3d_clear_uav_ops *meta_clear_uav_ops,
        VkImageViewType image_view_type, bool as_uint)
{
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

VkExtent3D vkd3d_get_clear_image_uav_workgroup_size(VkImageViewType view_type)
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
        case VK_IMAGE_VIEW_TYPE_3D:
        {
            VkExtent3D result = { 8, 8, 1 };
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

HRESULT vkd3d_meta_ops_init(struct vkd3d_meta_ops *meta_ops, struct d3d12_device *device)
{
    HRESULT hr;

    memset(meta_ops, 0, sizeof(*meta_ops));

    if (FAILED(hr = vkd3d_clear_uav_ops_init(&meta_ops->clear_uav, device)))
        return hr;

    return S_OK;
}

HRESULT vkd3d_meta_ops_cleanup(struct vkd3d_meta_ops *meta_ops, struct d3d12_device *device)
{
    vkd3d_clear_uav_ops_cleanup(&meta_ops->clear_uav, device);
    return S_OK;
}
