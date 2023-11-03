/*
 * Copyright 2020 Hans-Kristian Arntzen for Valve Corporation
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

#define VKD3D_DBG_CHANNEL VKD3D_DBG_CHANNEL_SHADER

#include "vkd3d_shader_private.h"
#include "vkd3d_utf8.h"
#include "vkd3d_string.h"
#include <inttypes.h>
#include <stdio.h>
#include <dxil_spirv_c.h>

static bool dxil_match_shader_visibility(enum vkd3d_shader_visibility visibility,
                                         dxil_spv_shader_stage stage)
{
    if (visibility == VKD3D_SHADER_VISIBILITY_ALL)
        return true;

    switch (stage)
    {
        case DXIL_SPV_STAGE_VERTEX:
            return visibility == VKD3D_SHADER_VISIBILITY_VERTEX;
        case DXIL_SPV_STAGE_HULL:
            return visibility == VKD3D_SHADER_VISIBILITY_HULL;
        case DXIL_SPV_STAGE_DOMAIN:
            return visibility == VKD3D_SHADER_VISIBILITY_DOMAIN;
        case DXIL_SPV_STAGE_GEOMETRY:
            return visibility == VKD3D_SHADER_VISIBILITY_GEOMETRY;
        case DXIL_SPV_STAGE_PIXEL:
            return visibility == VKD3D_SHADER_VISIBILITY_PIXEL;
        case DXIL_SPV_STAGE_COMPUTE:
            return visibility == VKD3D_SHADER_VISIBILITY_COMPUTE;
        case DXIL_SPV_STAGE_AMPLIFICATION:
            return visibility == VKD3D_SHADER_VISIBILITY_AMPLIFICATION;
        case DXIL_SPV_STAGE_MESH:
            return visibility == VKD3D_SHADER_VISIBILITY_MESH;
        default:
            return false;
    }
}

static unsigned dxil_resource_flags_from_kind(dxil_spv_resource_kind kind, bool ssbo)
{
    switch (kind)
    {
        case DXIL_SPV_RESOURCE_KIND_RAW_BUFFER:
        case DXIL_SPV_RESOURCE_KIND_STRUCTURED_BUFFER:
            if (ssbo)
                return VKD3D_SHADER_BINDING_FLAG_BUFFER | VKD3D_SHADER_BINDING_FLAG_RAW_SSBO;
            else
                return VKD3D_SHADER_BINDING_FLAG_BUFFER;

        case DXIL_SPV_RESOURCE_KIND_TYPED_BUFFER:
            return VKD3D_SHADER_BINDING_FLAG_BUFFER;

        case DXIL_SPV_RESOURCE_KIND_RT_ACCELERATION_STRUCTURE:
            /* Acceleration structures use aux buffer to store raw AS pointers.
             * As root descriptors, we should check for buffer flag instead. */
            if (ssbo)
                return VKD3D_SHADER_BINDING_FLAG_AUX_BUFFER;
            else
                return VKD3D_SHADER_BINDING_FLAG_BUFFER;

        default:
            return VKD3D_SHADER_BINDING_FLAG_IMAGE;
    }
}

static bool dxil_resource_is_global_heap(const dxil_spv_d3d_binding *d3d_binding)
{
    return d3d_binding->register_index == UINT32_MAX &&
            d3d_binding->register_space == UINT32_MAX &&
            d3d_binding->range_size == UINT32_MAX;
}

static bool vkd3d_shader_resource_binding_is_global_heap(const struct vkd3d_shader_resource_binding *binding)
{
    return binding->register_index == UINT32_MAX &&
            binding->register_space == UINT32_MAX &&
            binding->register_count == UINT32_MAX;
}

static bool dxil_resource_is_in_range(const struct vkd3d_shader_resource_binding *binding,
                                      const dxil_spv_d3d_binding *d3d_binding)
{
    if (vkd3d_shader_resource_binding_is_global_heap(binding) && dxil_resource_is_global_heap(d3d_binding))
        return true;

    if (binding->register_space != d3d_binding->register_space)
        return false;
    if (d3d_binding->register_index < binding->register_index)
        return false;

    return binding->register_count == UINT_MAX ||
           ((d3d_binding->register_index - binding->register_index) < binding->register_count);
}

static bool vkd3d_shader_binding_is_root_descriptor(const struct vkd3d_shader_resource_binding *binding)
{
    const uint32_t relevant_flags = VKD3D_SHADER_BINDING_FLAG_RAW_VA |
                                    VKD3D_SHADER_BINDING_FLAG_AUX_BUFFER;
    const uint32_t expected_flags = VKD3D_SHADER_BINDING_FLAG_RAW_VA;
    return (binding->flags & relevant_flags) == expected_flags;
}

struct vkd3d_dxil_remap_userdata
{
    const struct vkd3d_shader_interface_info *shader_interface_info;
    const struct vkd3d_shader_interface_local_info *shader_interface_local_info;
    unsigned int num_root_descriptors;
};

struct vkd3d_dxil_remap_info
{
    const struct vkd3d_shader_resource_binding *bindings;
    unsigned int binding_count;
    unsigned int num_root_descriptors;
    unsigned int descriptor_table_offset_words;
};

static dxil_spv_bool dxil_remap_inner(
        const struct vkd3d_dxil_remap_info *remap,
        enum vkd3d_shader_descriptor_type descriptor_type,
        const dxil_spv_d3d_binding *d3d_binding,
        dxil_spv_vulkan_binding *vk_binding,
        uint32_t resource_flags)
{
    unsigned int root_descriptor_index = 0;
    unsigned int i;

    for (i = 0; i < remap->binding_count; i++)
    {
        const struct vkd3d_shader_resource_binding *binding = &remap->bindings[i];
        const uint32_t mask = ~(VKD3D_SHADER_BINDING_FLAG_BINDLESS | VKD3D_SHADER_BINDING_FLAG_RAW_VA);
        uint32_t match_flags = binding->flags & mask;

        if (binding->type == descriptor_type &&
            dxil_resource_is_in_range(binding, d3d_binding) &&
            (match_flags & resource_flags) == resource_flags &&
            dxil_match_shader_visibility(binding->shader_visibility, d3d_binding->stage))
        {
            memset(vk_binding, 0, sizeof(*vk_binding));

            if (vkd3d_shader_binding_is_root_descriptor(binding))
            {
                vk_binding->descriptor_type = DXIL_SPV_VULKAN_DESCRIPTOR_TYPE_BUFFER_DEVICE_ADDRESS;
                vk_binding->root_constant_index = root_descriptor_index;
            }
            else if (binding->flags & VKD3D_SHADER_BINDING_FLAG_BINDLESS)
            {
                vk_binding->bindless.use_heap = DXIL_SPV_TRUE;
                vk_binding->set = binding->binding.set;
                vk_binding->binding = binding->binding.binding;

                if (dxil_resource_is_global_heap(d3d_binding))
                {
                    vk_binding->bindless.heap_root_offset = 0; /* No constant offset. */
                    vk_binding->root_constant_index = UINT32_MAX; /* No push offset. */
                }
                else
                {
                    vk_binding->bindless.heap_root_offset = binding->descriptor_offset +
                            d3d_binding->register_index - binding->register_index;
                    vk_binding->root_constant_index = binding->descriptor_table + remap->descriptor_table_offset_words;

                    if (vk_binding->root_constant_index < 2 * remap->num_root_descriptors)
                    {
                        ERR("Bindless push constant table offset is impossible. %u < 2 * %u\n",
                                vk_binding->root_constant_index, remap->num_root_descriptors);
                        return DXIL_SPV_FALSE;
                    }
                    vk_binding->root_constant_index -= 2 * remap->num_root_descriptors;
                }

                /* Acceleration structures are mapped to SSBO uvec2[] array instead of normal heap. */
                if (d3d_binding->kind == DXIL_SPV_RESOURCE_KIND_RT_ACCELERATION_STRUCTURE)
                {
                    vk_binding->descriptor_type = DXIL_SPV_VULKAN_DESCRIPTOR_TYPE_SSBO;
                }
                else if (descriptor_type == VKD3D_SHADER_DESCRIPTOR_TYPE_UAV &&
                        (binding->flags & VKD3D_SHADER_BINDING_FLAG_AUX_BUFFER) &&
                        !(binding->flags & VKD3D_SHADER_BINDING_FLAG_RAW_VA))
                {
                    /* Force texel buffer path for UAV counters if we need to. */
                    vk_binding->descriptor_type = DXIL_SPV_VULKAN_DESCRIPTOR_TYPE_TEXEL_BUFFER;
                }
            }
            else
            {
                vk_binding->set = binding->binding.set;
                vk_binding->binding = binding->binding.binding + d3d_binding->register_index - binding->register_index;
            }

            return DXIL_SPV_TRUE;
        }

        if (vkd3d_shader_binding_is_root_descriptor(binding))
            root_descriptor_index++;
    }

    return DXIL_SPV_FALSE;
}

static dxil_spv_bool dxil_remap(const struct vkd3d_dxil_remap_userdata *remap,
        enum vkd3d_shader_descriptor_type descriptor_type, const struct dxil_spv_d3d_binding *d3d_binding,
        struct dxil_spv_vulkan_binding *vk_binding, unsigned int resource_flags)
{
    const struct vkd3d_shader_interface_local_info *shader_interface_local_info;
    const struct vkd3d_shader_interface_info *shader_interface_info;
    struct vkd3d_dxil_remap_info remap_info;

    shader_interface_info = remap->shader_interface_info;
    shader_interface_local_info = remap->shader_interface_local_info;

    remap_info.bindings = shader_interface_info->bindings;
    remap_info.binding_count = shader_interface_info->binding_count;
    remap_info.descriptor_table_offset_words = shader_interface_info->descriptor_tables.offset / sizeof(uint32_t);
    remap_info.num_root_descriptors = remap->num_root_descriptors;

    if (!dxil_remap_inner(&remap_info, descriptor_type, d3d_binding, vk_binding, resource_flags))
    {
        if (shader_interface_local_info)
        {
            /* This fallback is relevant only so that we can report
             * set/binding for bindless record buffer TABLE descriptor.
             * Root descriptor and constants are resolved internally in dxil-spirv. */
            remap_info.bindings = shader_interface_local_info->bindings;
            remap_info.binding_count = shader_interface_local_info->binding_count;
            /* Not relevant. */
            remap_info.descriptor_table_offset_words = 0;
            remap_info.num_root_descriptors = 0;
            return dxil_remap_inner(&remap_info, descriptor_type, d3d_binding, vk_binding, resource_flags);
        }
        else
            return DXIL_SPV_FALSE;
    }
    else
        return DXIL_SPV_TRUE;
}

static dxil_spv_bool dxil_srv_remap(void *userdata, const dxil_spv_d3d_binding *d3d_binding,
                                    dxil_spv_srv_vulkan_binding *vk_binding)
{
    const struct vkd3d_shader_interface_info *shader_interface_info;
    const struct vkd3d_dxil_remap_userdata *remap = userdata;
    unsigned int resource_flags, resource_flags_ssbo;
    bool use_ssbo;

    shader_interface_info = remap->shader_interface_info;
    resource_flags_ssbo = dxil_resource_flags_from_kind(d3d_binding->kind, true);
    resource_flags = dxil_resource_flags_from_kind(d3d_binding->kind, false);
    use_ssbo = resource_flags_ssbo != resource_flags;

    if (use_ssbo && dxil_remap(remap, VKD3D_SHADER_DESCRIPTOR_TYPE_SRV,
            d3d_binding, &vk_binding->buffer_binding, resource_flags_ssbo))
    {
        vk_binding->buffer_binding.descriptor_type = DXIL_SPV_VULKAN_DESCRIPTOR_TYPE_SSBO;
        if (shader_interface_info->flags & VKD3D_SHADER_INTERFACE_SSBO_OFFSET_BUFFER)
        {
            vk_binding->offset_binding.set = shader_interface_info->offset_buffer_binding->set;
            vk_binding->offset_binding.binding = shader_interface_info->offset_buffer_binding->binding;
        }
        return DXIL_SPV_TRUE;
    }
    else
    {
        vk_binding->buffer_binding.descriptor_type = DXIL_SPV_VULKAN_DESCRIPTOR_TYPE_TEXEL_BUFFER;
        if (shader_interface_info->flags & VKD3D_SHADER_INTERFACE_TYPED_OFFSET_BUFFER)
        {
            vk_binding->offset_binding.set = shader_interface_info->offset_buffer_binding->set;
            vk_binding->offset_binding.binding = shader_interface_info->offset_buffer_binding->binding;
        }
    }

    return dxil_remap(remap, VKD3D_SHADER_DESCRIPTOR_TYPE_SRV,
            d3d_binding, &vk_binding->buffer_binding, resource_flags);
}

static dxil_spv_bool dxil_sampler_remap(void *userdata, const dxil_spv_d3d_binding *d3d_binding,
                                        dxil_spv_vulkan_binding *vk_binding)
{
    const struct vkd3d_dxil_remap_userdata *remap = userdata;
    return dxil_remap(remap, VKD3D_SHADER_DESCRIPTOR_TYPE_SAMPLER,
            d3d_binding, vk_binding, VKD3D_SHADER_BINDING_FLAG_IMAGE);
}

static dxil_spv_bool dxil_input_remap(void *userdata, const dxil_spv_d3d_vertex_input *d3d_input,
                                      dxil_spv_vulkan_vertex_input *vk_input)
{
    (void)userdata;
    vk_input->location = d3d_input->start_row;
    return DXIL_SPV_TRUE;
}

static dxil_spv_bool dxil_output_remap(void *userdata, const dxil_spv_d3d_stream_output *d3d_output,
                                       dxil_spv_vulkan_stream_output *vk_output)
{
    const struct vkd3d_shader_transform_feedback_info *xfb_info = userdata;
    const struct vkd3d_shader_transform_feedback_element *xfb_element;
    unsigned int buffer_offsets[D3D12_SO_BUFFER_SLOT_COUNT];
    unsigned int i, stride;

    memset(buffer_offsets, 0, sizeof(buffer_offsets));
    xfb_element = NULL;

    for (i = 0; i < xfb_info->element_count; ++i)
    {
        const struct vkd3d_shader_transform_feedback_element *e = &xfb_info->elements[i];

        /* TODO: Stream index matching? */
        if (!ascii_strcasecmp(e->semantic_name, d3d_output->semantic) && e->semantic_index == d3d_output->semantic_index)
        {
            xfb_element = e;
            break;
        }

        buffer_offsets[e->output_slot] += 4 * e->component_count;
    }

    if (!xfb_element)
    {
        vk_output->enable = DXIL_SPV_FALSE;
        return DXIL_SPV_TRUE;
    }

    if (xfb_element->output_slot < xfb_info->buffer_stride_count)
    {
        stride = xfb_info->buffer_strides[xfb_element->output_slot];
    }
    else
    {
        stride = 0;
        for (i = 0; i < xfb_info->element_count; ++i)
        {
            const struct vkd3d_shader_transform_feedback_element *e = &xfb_info->elements[i];

            if (e->stream_index == xfb_element->stream_index && e->output_slot == xfb_element->output_slot)
                stride += 4 * e->component_count;
        }
    }

    vk_output->enable = DXIL_SPV_TRUE;
    vk_output->offset = buffer_offsets[xfb_element->output_slot];
    vk_output->stride = stride;
    vk_output->buffer_index = xfb_element->output_slot;
    return DXIL_SPV_TRUE;
}

static dxil_spv_bool dxil_shader_stage_output_capture(void *userdata, const dxil_spv_d3d_shader_stage_io *d3d_input,
                                                      dxil_spv_vulkan_shader_stage_io *vk_input)
{
    struct vkd3d_shader_stage_io_map *io_map = userdata;
    struct vkd3d_shader_stage_io_entry *e;

    if (!(e = vkd3d_shader_stage_io_map_append(io_map, d3d_input->semantic, d3d_input->semantic_index)))
    {
        ERR("Duplicate semantic %s (%u).\n", d3d_input->semantic, d3d_input->semantic_index);
        return false;
    }

    e->vk_location = vk_input->location;
    e->vk_component = vk_input->component;
    e->vk_flags = vk_input->flags;
    return true;
}

static dxil_spv_bool dxil_shader_stage_input_remap(void *userdata, const dxil_spv_d3d_shader_stage_io *d3d_input,
                                                   dxil_spv_vulkan_shader_stage_io *vk_input)
{
    const struct vkd3d_shader_stage_io_map *io_map = userdata;
    const struct vkd3d_shader_stage_io_entry *e;

    if (!(e = vkd3d_shader_stage_io_map_find(io_map, d3d_input->semantic, d3d_input->semantic_index)))
    {
        ERR("Undefined semantic %s (%u).\n", d3d_input->semantic, d3d_input->semantic_index);
        return false;
    }

    vk_input->location = e->vk_location;
    vk_input->component = e->vk_component;
    vk_input->flags = e->vk_flags;
    return true;
}

static dxil_spv_bool dxil_uav_remap(void *userdata, const dxil_spv_uav_d3d_binding *d3d_binding,
                                    dxil_spv_uav_vulkan_binding *vk_binding)
{
    const struct vkd3d_shader_interface_info *shader_interface_info;
    const struct vkd3d_dxil_remap_userdata *remap = userdata;
    unsigned int resource_flags, resource_flags_ssbo;
    bool use_ssbo;

    shader_interface_info = remap->shader_interface_info;
    resource_flags_ssbo = dxil_resource_flags_from_kind(d3d_binding->d3d_binding.kind, true);
    resource_flags = dxil_resource_flags_from_kind(d3d_binding->d3d_binding.kind, false);
    use_ssbo = resource_flags != resource_flags_ssbo;

    if (use_ssbo)
    {
        if (dxil_remap(remap, VKD3D_SHADER_DESCRIPTOR_TYPE_UAV, &d3d_binding->d3d_binding,
                &vk_binding->buffer_binding, resource_flags_ssbo))
        {
            vk_binding->buffer_binding.descriptor_type = DXIL_SPV_VULKAN_DESCRIPTOR_TYPE_SSBO;
            if (shader_interface_info->flags & VKD3D_SHADER_INTERFACE_SSBO_OFFSET_BUFFER)
            {
                vk_binding->offset_binding.set = shader_interface_info->offset_buffer_binding->set;
                vk_binding->offset_binding.binding = shader_interface_info->offset_buffer_binding->binding;
            }
        }
        else if (!dxil_remap(remap, VKD3D_SHADER_DESCRIPTOR_TYPE_UAV, &d3d_binding->d3d_binding,
                &vk_binding->buffer_binding, resource_flags))
        {
            return DXIL_SPV_FALSE;
        }
        else if (vk_binding->buffer_binding.descriptor_type != DXIL_SPV_VULKAN_DESCRIPTOR_TYPE_BUFFER_DEVICE_ADDRESS)
        {
            /* By default, we use TEXEL_BUFFER unless dxil_remap remaps it to BDA.
             * We won't trigger SSBO path when using BDA. */
            vk_binding->buffer_binding.descriptor_type = DXIL_SPV_VULKAN_DESCRIPTOR_TYPE_TEXEL_BUFFER;
            if (shader_interface_info->flags & VKD3D_SHADER_INTERFACE_TYPED_OFFSET_BUFFER)
            {
                vk_binding->offset_binding.set = shader_interface_info->offset_buffer_binding->set;
                vk_binding->offset_binding.binding = shader_interface_info->offset_buffer_binding->binding;
            }
        }
    }
    else
    {
        vk_binding->buffer_binding.descriptor_type = DXIL_SPV_VULKAN_DESCRIPTOR_TYPE_TEXEL_BUFFER;
        if (!dxil_remap(remap, VKD3D_SHADER_DESCRIPTOR_TYPE_UAV, &d3d_binding->d3d_binding,
                &vk_binding->buffer_binding, resource_flags))
        {
            return DXIL_SPV_FALSE;
        }

        if (shader_interface_info->flags & VKD3D_SHADER_INTERFACE_TYPED_OFFSET_BUFFER)
        {
            vk_binding->offset_binding.set = shader_interface_info->offset_buffer_binding->set;
            vk_binding->offset_binding.binding = shader_interface_info->offset_buffer_binding->binding;
        }
    }

    if (d3d_binding->has_counter)
    {
        if (!dxil_remap(remap, VKD3D_SHADER_DESCRIPTOR_TYPE_UAV, &d3d_binding->d3d_binding,
                &vk_binding->counter_binding, VKD3D_SHADER_BINDING_FLAG_AUX_BUFFER))
        {
            return DXIL_SPV_FALSE;
        }
    }

    return DXIL_SPV_TRUE;
}

static dxil_spv_bool dxil_cbv_remap(void *userdata, const dxil_spv_d3d_binding *d3d_binding,
                                    dxil_spv_cbv_vulkan_binding *vk_binding)
{
    const struct vkd3d_shader_interface_info *shader_interface_info;
    const struct vkd3d_dxil_remap_userdata *remap = userdata;
    unsigned int i;

    shader_interface_info = remap->shader_interface_info;

    /* Try to map to root constant -> push constant. */
    for (i = 0; i < shader_interface_info->push_constant_buffer_count; i++)
    {
        const struct vkd3d_shader_push_constant_buffer *push = &shader_interface_info->push_constant_buffers[i];
        if (push->register_space == d3d_binding->register_space &&
            push->register_index == d3d_binding->register_index &&
            dxil_match_shader_visibility(push->shader_visibility, d3d_binding->stage))
        {
            memset(vk_binding, 0, sizeof(*vk_binding));
            vk_binding->push_constant = DXIL_SPV_TRUE;
            vk_binding->vulkan.push_constant.offset_in_words = push->offset / sizeof(uint32_t);
            if (vk_binding->vulkan.push_constant.offset_in_words < remap->num_root_descriptors * 2)
            {
                ERR("Root descriptor offset of %u is impossible with %u root descriptors.\n",
                    vk_binding->vulkan.push_constant.offset_in_words, remap->num_root_descriptors);
                return DXIL_SPV_FALSE;
            }
            vk_binding->vulkan.push_constant.offset_in_words -= remap->num_root_descriptors * 2;
            return DXIL_SPV_TRUE;
        }
    }

    vk_binding->push_constant = DXIL_SPV_FALSE;
    return dxil_remap(remap, VKD3D_SHADER_DESCRIPTOR_TYPE_CBV,
            d3d_binding, &vk_binding->vulkan.uniform_binding,
            VKD3D_SHADER_BINDING_FLAG_BUFFER);
}

static void vkd3d_dxil_log_callback(void *userdata, dxil_spv_log_level level, const char *msg)
{
    /* msg already has a newline. */
    (void)userdata;
    switch (level)
    {
    case DXIL_SPV_LOG_LEVEL_ERROR:
        ERR("dxil-spirv: %s", msg);
        break;

    case DXIL_SPV_LOG_LEVEL_WARN:
        WARN("dxil-spirv: %s", msg);
        break;

    default:
    case DXIL_SPV_LOG_LEVEL_DEBUG:
        TRACE("dxil-spirv: %s", msg);
        break;
    }
}

static bool dxil_match_shader_stage(dxil_spv_shader_stage blob_stage, VkShaderStageFlagBits expected)
{
    VkShaderStageFlagBits stage;

    switch (blob_stage)
    {
        case DXIL_SPV_STAGE_VERTEX: stage = VK_SHADER_STAGE_VERTEX_BIT; break;
        case DXIL_SPV_STAGE_HULL: stage = VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT; break;
        case DXIL_SPV_STAGE_DOMAIN: stage = VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT; break;
        case DXIL_SPV_STAGE_GEOMETRY: stage = VK_SHADER_STAGE_GEOMETRY_BIT; break;
        case DXIL_SPV_STAGE_PIXEL: stage = VK_SHADER_STAGE_FRAGMENT_BIT; break;
        case DXIL_SPV_STAGE_COMPUTE: stage = VK_SHADER_STAGE_COMPUTE_BIT; break;
        case DXIL_SPV_STAGE_AMPLIFICATION: stage = VK_SHADER_STAGE_TASK_BIT_EXT; break;
        case DXIL_SPV_STAGE_MESH: stage = VK_SHADER_STAGE_MESH_BIT_EXT; break;
        default: return false;
    }

    if (stage != expected)
    {
        ERR("Expected VkShaderStage #%x, but got VkShaderStage #%x.\n", expected, stage);
        return false;
    }

    return true;
}

int vkd3d_shader_compile_dxil(const struct vkd3d_shader_code *dxbc,
        struct vkd3d_shader_code *spirv,
        struct vkd3d_shader_code_debug *spirv_debug,
        const struct vkd3d_shader_interface_info *shader_interface_info,
        const struct vkd3d_shader_compile_arguments *compiler_args)
{
    dxil_spv_option_denorm_preserve_support denorm_preserve = {{ DXIL_SPV_OPTION_DENORM_PRESERVE_SUPPORT }};
    struct vkd3d_dxil_remap_userdata remap_userdata;
    unsigned int raw_va_binding_count = 0;
    unsigned int num_root_descriptors = 0;
    unsigned int root_constant_words = 0;
    dxil_spv_converter converter = NULL;
    dxil_spv_parsed_blob blob = NULL;
    dxil_spv_compiled_spirv compiled;
    unsigned int heuristic_wave_size;
    dxil_spv_shader_stage stage;
    unsigned int i, j, max_size;
    vkd3d_shader_hash_t hash;
    int ret = VKD3D_OK;
    uint32_t quirks;
    void *code;

    dxil_spv_set_thread_log_callback(vkd3d_dxil_log_callback, NULL);

    memset(&spirv->meta, 0, sizeof(spirv->meta));
    hash = vkd3d_shader_hash(dxbc);
    spirv->meta.hash = hash;

    /* Cannot replace mesh shaders until we have reflected the IO layout. */
    if (shader_interface_info->stage != VK_SHADER_STAGE_MESH_BIT_EXT &&
            vkd3d_shader_replace(hash, &spirv->code, &spirv->size))
    {
        spirv->meta.flags |= VKD3D_SHADER_META_FLAG_REPLACED;
        return ret;
    }
    quirks = vkd3d_shader_compile_arguments_select_quirks(compiler_args, hash);
    if (quirks & VKD3D_SHADER_QUIRK_FORCE_COMPUTE_BARRIER)
        spirv->meta.flags |= VKD3D_SHADER_META_FLAG_FORCE_COMPUTE_BARRIER_AFTER_DISPATCH;

    dxil_spv_begin_thread_allocator_context();

    vkd3d_shader_dump_shader(hash, dxbc, "dxil");

    if (dxil_spv_parse_dxil_blob(dxbc->code, dxbc->size, &blob) != DXIL_SPV_SUCCESS)
    {
        ret = VKD3D_ERROR_INVALID_SHADER;
        goto end;
    }

    stage = dxil_spv_parsed_blob_get_shader_stage(blob);
    if (!dxil_match_shader_stage(stage, shader_interface_info->stage))
    {
        ret = VKD3D_ERROR_INVALID_ARGUMENT;
        goto end;
    }

    if (dxil_spv_create_converter(blob, &converter) != DXIL_SPV_SUCCESS)
    {
        ret = VKD3D_ERROR_INVALID_ARGUMENT;
        goto end;
    }

    /* Figure out how many words we need for push constants. */
    for (i = 0; i < shader_interface_info->push_constant_buffer_count; i++)
    {
        max_size = shader_interface_info->push_constant_buffers[i].offset +
                                shader_interface_info->push_constant_buffers[i].size;
        max_size = (max_size + 3) / 4;
        if (max_size > root_constant_words)
            root_constant_words = max_size;
    }

    max_size = shader_interface_info->descriptor_tables.offset / sizeof(uint32_t) +
               shader_interface_info->descriptor_tables.count;
    if (max_size > root_constant_words)
        root_constant_words = max_size;

    for (i = 0; i < shader_interface_info->binding_count; i++)
    {
        if (shader_interface_info->bindings[i].flags & VKD3D_SHADER_BINDING_FLAG_RAW_VA)
            raw_va_binding_count++;

        if (vkd3d_shader_binding_is_root_descriptor(&shader_interface_info->bindings[i]))
            num_root_descriptors++;
    }

    /* Root constants come after root descriptors. Offset the counts. */
    if (root_constant_words < num_root_descriptors * 2)
        root_constant_words = num_root_descriptors * 2;
    root_constant_words -= num_root_descriptors * 2;

    {
        const struct dxil_spv_option_ssbo_alignment helper =
                { { DXIL_SPV_OPTION_SSBO_ALIGNMENT }, shader_interface_info->min_ssbo_alignment };
        if (dxil_spv_converter_add_option(converter, &helper.base) != DXIL_SPV_SUCCESS)
        {
            ERR("dxil-spirv does not support SSBO_ALIGNMENT.\n");
            ret = VKD3D_ERROR_NOT_IMPLEMENTED;
            goto end;
        }
    }

    if (shader_interface_info->flags & VKD3D_SHADER_INTERFACE_PUSH_CONSTANTS_AS_UNIFORM_BUFFER)
    {
        const struct dxil_spv_option_root_constant_inline_uniform_block helper =
                { { DXIL_SPV_OPTION_ROOT_CONSTANT_INLINE_UNIFORM_BLOCK },
                  shader_interface_info->push_constant_ubo_binding->set,
                  shader_interface_info->push_constant_ubo_binding->binding,
                  DXIL_SPV_TRUE };
        if (dxil_spv_converter_add_option(converter, &helper.base) != DXIL_SPV_SUCCESS)
        {
            ERR("dxil-spirv does not support PUSH_CONSTANTS_AS_UNIFORM_BUFFER.\n");
            ret = VKD3D_ERROR_NOT_IMPLEMENTED;
            goto end;
        }
    }

    if (shader_interface_info->flags & VKD3D_SHADER_INTERFACE_BINDLESS_CBV_AS_STORAGE_BUFFER)
    {
        static const struct dxil_spv_option_bindless_cbv_ssbo_emulation helper =
                { { DXIL_SPV_OPTION_BINDLESS_CBV_SSBO_EMULATION },
                  DXIL_SPV_TRUE };
        if (dxil_spv_converter_add_option(converter, &helper.base) != DXIL_SPV_SUCCESS)
        {
            ERR("dxil-spirv does not support BINDLESS_CBV_AS_STORAGE_BUFFER.\n");
            ret = VKD3D_ERROR_NOT_IMPLEMENTED;
            goto end;
        }
    }

    {
        const struct dxil_spv_option_physical_storage_buffer helper =
                { { DXIL_SPV_OPTION_PHYSICAL_STORAGE_BUFFER },
                  raw_va_binding_count || num_root_descriptors ? DXIL_SPV_TRUE : DXIL_SPV_FALSE };
        if (dxil_spv_converter_add_option(converter, &helper.base) != DXIL_SPV_SUCCESS)
        {
            ERR("dxil-spirv does not support PHYSICAL_STORAGE_BUFFER.\n");
            ret = VKD3D_ERROR_NOT_IMPLEMENTED;
            goto end;
        }
    }

    if (shader_interface_info->flags & VKD3D_SHADER_INTERFACE_TYPED_OFFSET_BUFFER)
    {
        const struct dxil_spv_option_bindless_typed_buffer_offsets helper =
                { { DXIL_SPV_OPTION_BINDLESS_TYPED_BUFFER_OFFSETS },
                  DXIL_SPV_TRUE };
        if (dxil_spv_converter_add_option(converter, &helper.base) != DXIL_SPV_SUCCESS)
        {
            ERR("dxil-spirv does not support BINDLESS_TYPED_BUFFER_OFFSETS.\n");
            ret = VKD3D_ERROR_NOT_IMPLEMENTED;
            goto end;
        }
    }

#ifdef VKD3D_ENABLE_DESCRIPTOR_QA
    if (shader_interface_info->flags & VKD3D_SHADER_INTERFACE_DESCRIPTOR_QA_BUFFER)
    {
        struct dxil_spv_option_descriptor_qa helper;
        helper.base.type = DXIL_SPV_OPTION_DESCRIPTOR_QA;
        helper.enabled = DXIL_SPV_TRUE;
        helper.version = DXIL_SPV_DESCRIPTOR_QA_INTERFACE_VERSION;
        helper.shader_hash = hash;
        helper.global_desc_set = shader_interface_info->descriptor_qa_global_binding->set;
        helper.global_binding = shader_interface_info->descriptor_qa_global_binding->binding;
        helper.heap_desc_set = shader_interface_info->descriptor_qa_heap_binding->set;
        helper.heap_binding = shader_interface_info->descriptor_qa_heap_binding->binding;

        if (dxil_spv_converter_add_option(converter, &helper.base) != DXIL_SPV_SUCCESS)
        {
            ERR("dxil-spirv does not support DESCRIPTOR_QA_BUFFER.\n");
            ret = VKD3D_ERROR_NOT_IMPLEMENTED;
            goto end;
        }
    }
#endif

    if (shader_interface_info->flags & VKD3D_SHADER_INTERFACE_RAW_VA_ALIAS_DESCRIPTOR_BUFFER)
    {
        const struct dxil_spv_option_physical_address_descriptor_indexing helper =
                { { DXIL_SPV_OPTION_PHYSICAL_ADDRESS_DESCRIPTOR_INDEXING },
                    shader_interface_info->descriptor_size_cbv_srv_uav / sizeof(VkDeviceAddress),
                    0 };

        if (dxil_spv_converter_add_option(converter, &helper.base) != DXIL_SPV_SUCCESS)
        {
            ERR("dxil-spirv does not support PHYSICAL_ADDRESS_DESCRIPTOR_INDEXING.\n");
            ret = VKD3D_ERROR_NOT_IMPLEMENTED;
            goto end;
        }
    }

    {
        const struct dxil_spv_option_bindless_offset_buffer_layout helper =
                { { DXIL_SPV_OPTION_BINDLESS_OFFSET_BUFFER_LAYOUT },
                  0, 1, 2 };
        if (dxil_spv_converter_add_option(converter, &helper.base) != DXIL_SPV_SUCCESS)
        {
            ERR("dxil-spirv does not support BINDLESS_OFFSET_BUFFER_LAYOUT.\n");
            ret = VKD3D_ERROR_NOT_IMPLEMENTED;
            goto end;
        }
    }

    {
        char buffer[16 + 5 + 1];
        const struct dxil_spv_option_shader_source_file helper =
                { { DXIL_SPV_OPTION_SHADER_SOURCE_FILE }, buffer };

        sprintf(buffer, "%016"PRIx64".dxil", spirv->meta.hash);
        if (dxil_spv_converter_add_option(converter, &helper.base) != DXIL_SPV_SUCCESS)
            WARN("dxil-spirv does not support SHADER_SOURCE_FILE.\n");
    }

    {
        const struct dxil_spv_option_precise_control helper =
                { { DXIL_SPV_OPTION_PRECISE_CONTROL },
                        (quirks & VKD3D_SHADER_QUIRK_FORCE_NOCONTRACT_MATH) ? DXIL_SPV_TRUE : DXIL_SPV_FALSE,
                        DXIL_SPV_FALSE };
        if (dxil_spv_converter_add_option(converter, &helper.base) != DXIL_SPV_SUCCESS)
        {
            WARN("dxil-spirv does not support PRECISE_CONTROL.\n");
            ret = VKD3D_ERROR_NOT_IMPLEMENTED;
            goto end;
        }
    }

    if (quirks & VKD3D_SHADER_QUIRK_FORCE_LOOP)
    {
        struct dxil_spv_option_branch_control helper = { { DXIL_SPV_OPTION_BRANCH_CONTROL } };
        helper.force_loop = DXIL_SPV_TRUE;
        if (dxil_spv_converter_add_option(converter, &helper.base) != DXIL_SPV_SUCCESS)
        {
            WARN("dxil-spirv does not support BRANCH_CONTROL.\n");
            ret = VKD3D_ERROR_NOT_IMPLEMENTED;
            goto end;
        }
    }

    if (compiler_args)
    {
        for (i = 0; i < compiler_args->target_extension_count; i++)
        {
            if (compiler_args->target_extensions[i] == VKD3D_SHADER_TARGET_EXTENSION_SPV_EXT_DEMOTE_TO_HELPER_INVOCATION)
            {
                static const dxil_spv_option_shader_demote_to_helper helper =
                        { { DXIL_SPV_OPTION_SHADER_DEMOTE_TO_HELPER }, DXIL_SPV_TRUE };
                if (dxil_spv_converter_add_option(converter, &helper.base) != DXIL_SPV_SUCCESS)
                {
                    WARN("dxil-spirv does not support DEMOTE_TO_HELPER. Slower path will be used.\n");
                }
            }
            else if (compiler_args->target_extensions[i] == VKD3D_SHADER_TARGET_EXTENSION_READ_STORAGE_IMAGE_WITHOUT_FORMAT)
            {
                static const dxil_spv_option_typed_uav_read_without_format helper =
                        { { DXIL_SPV_OPTION_TYPED_UAV_READ_WITHOUT_FORMAT }, DXIL_SPV_TRUE };
                if (dxil_spv_converter_add_option(converter, &helper.base) != DXIL_SPV_SUCCESS)
                {
                    ERR("dxil-spirv does not support TYPED_UAV_READ_WITHOUT_FORMAT.\n");
                    ret = VKD3D_ERROR_NOT_IMPLEMENTED;
                    goto end;
                }
            }
            else if (compiler_args->target_extensions[i] == VKD3D_SHADER_TARGET_EXTENSION_SPV_KHR_INTEGER_DOT_PRODUCT)
            {
                static const dxil_spv_option_shader_i8_dot helper =
                        { { DXIL_SPV_OPTION_SHADER_I8_DOT }, DXIL_SPV_TRUE };
                if (dxil_spv_converter_add_option(converter, &helper.base) != DXIL_SPV_SUCCESS)
                {
                    ERR("dxil-spirv does not support SHADER_I8_DOT.\n");
                    ret = VKD3D_ERROR_NOT_IMPLEMENTED;
                    goto end;
                }
            }
            else if (compiler_args->target_extensions[i] == VKD3D_SHADER_TARGET_EXTENSION_SCALAR_BLOCK_LAYOUT)
            {
                dxil_spv_option_scalar_block_layout helper =
                        { { DXIL_SPV_OPTION_SCALAR_BLOCK_LAYOUT }, DXIL_SPV_TRUE };

                for (j = 0; j < compiler_args->target_extension_count; j++)
                {
                    if (compiler_args->target_extensions[j] ==
                            VKD3D_SHADER_TARGET_EXTENSION_ASSUME_PER_COMPONENT_SSBO_ROBUSTNESS)
                    {
                        helper.supports_per_component_robustness = DXIL_SPV_TRUE;
                        break;
                    }
                }

                if (dxil_spv_converter_add_option(converter, &helper.base) != DXIL_SPV_SUCCESS)
                {
                    ERR("dxil-spirv does not support SCALAR_BLOCK_LAYOUT.\n");
                    ret = VKD3D_ERROR_NOT_IMPLEMENTED;
                    goto end;
                }
            }
            else if (compiler_args->target_extensions[i] == VKD3D_SHADER_TARGET_EXTENSION_BARYCENTRIC_KHR)
            {
                static const dxil_spv_option_barycentric_khr helper =
                        { { DXIL_SPV_OPTION_BARYCENTRIC_KHR }, DXIL_SPV_TRUE };

                if (dxil_spv_converter_add_option(converter, &helper.base) != DXIL_SPV_SUCCESS)
                {
                    ERR("dxil-spirv does not support BARYCENTRIC_KHR.\n");
                    ret = VKD3D_ERROR_NOT_IMPLEMENTED;
                    goto end;
                }
            }
            else if (compiler_args->target_extensions[i] == VKD3D_SHADER_TARGET_EXTENSION_MIN_PRECISION_IS_NATIVE_16BIT)
            {
                if (!(quirks & VKD3D_SHADER_QUIRK_FORCE_MIN16_AS_32BIT))
                {
                    static const dxil_spv_option_min_precision_native_16bit helper =
                            { { DXIL_SPV_OPTION_MIN_PRECISION_NATIVE_16BIT }, DXIL_SPV_TRUE };

                    if (dxil_spv_converter_add_option(converter, &helper.base) != DXIL_SPV_SUCCESS)
                    {
                        ERR("dxil-spirv does not support MIN_PRECISION_NATIVE_16BIT.\n");
                        ret = VKD3D_ERROR_NOT_IMPLEMENTED;
                        goto end;
                    }
                }
            }
            else if (compiler_args->target_extensions[i] == VKD3D_SHADER_TARGET_EXTENSION_SUPPORT_FP16_DENORM_PRESERVE)
                denorm_preserve.supports_float16_denorm_preserve = DXIL_SPV_TRUE;
            else if (compiler_args->target_extensions[i] == VKD3D_SHADER_TARGET_EXTENSION_SUPPORT_FP64_DENORM_PRESERVE)
                denorm_preserve.supports_float64_denorm_preserve = DXIL_SPV_TRUE;
            else if (compiler_args->target_extensions[i] == VKD3D_SHADER_TARGET_EXTENSION_SUPPORT_SUBGROUP_PARTITIONED_NV)
            {
                static const dxil_spv_option_subgroup_partitioned_nv helper =
                        { { DXIL_SPV_OPTION_SUBGROUP_PARTITIONED_NV }, DXIL_SPV_TRUE };
                if (dxil_spv_converter_add_option(converter, &helper.base) != DXIL_SPV_SUCCESS)
                {
                    ERR("dxil-spirv does not support SUBGROUP_PARTITIONED_NV.\n");
                    ret = VKD3D_ERROR_NOT_IMPLEMENTED;
                    goto end;
                }
            }
        }

        if (dxil_spv_converter_add_option(converter, &denorm_preserve.base) != DXIL_SPV_SUCCESS)
        {
            ERR("dxil-spirv does not support DENORM_PRESERVE_SUPPORT.\n");
            ret = VKD3D_ERROR_NOT_IMPLEMENTED;
            goto end;
        }

        if (compiler_args->dual_source_blending)
        {
            static const dxil_spv_option_dual_source_blending helper =
                    { { DXIL_SPV_OPTION_DUAL_SOURCE_BLENDING }, DXIL_SPV_TRUE };
            if (dxil_spv_converter_add_option(converter, &helper.base) != DXIL_SPV_SUCCESS)
            {
                ERR("dxil-spirv does not support DUAL_SOURCE_BLENDING.\n");
                ret = VKD3D_ERROR_NOT_IMPLEMENTED;
                goto end;
            }
        }

        if (compiler_args->output_swizzle_count != 0)
        {
            const dxil_spv_option_output_swizzle helper =
                    { { DXIL_SPV_OPTION_OUTPUT_SWIZZLE }, compiler_args->output_swizzles, compiler_args->output_swizzle_count };
            if (dxil_spv_converter_add_option(converter, &helper.base) != DXIL_SPV_SUCCESS)
            {
                ERR("dxil-spirv does not support OUTPUT_SWIZZLE.\n");
                ret = VKD3D_ERROR_NOT_IMPLEMENTED;
                goto end;
            }
        }

        for (i = 0; i < compiler_args->parameter_count; i++)
        {
            const struct vkd3d_shader_parameter *argument = &compiler_args->parameters[i];
            if (argument->name == VKD3D_SHADER_PARAMETER_NAME_RASTERIZER_SAMPLE_COUNT)
            {
                bool spec_constant = argument->type == VKD3D_SHADER_PARAMETER_TYPE_SPECIALIZATION_CONSTANT;
                const dxil_spv_option_rasterizer_sample_count helper =
                        { { DXIL_SPV_OPTION_RASTERIZER_SAMPLE_COUNT },
                          spec_constant ? argument->specialization_constant.id : argument->immediate_constant.u32,
                          spec_constant ? DXIL_SPV_TRUE : DXIL_SPV_FALSE };
                if (dxil_spv_converter_add_option(converter, &helper.base) != DXIL_SPV_SUCCESS)
                {
                    ERR("dxil-spirv does not support RASTERIZER_SAMPLE_COUNT.\n");
                    ret = VKD3D_ERROR_NOT_IMPLEMENTED;
                    goto end;
                }
            }
        }
    }

    if (quirks & VKD3D_SHADER_QUIRK_INVARIANT_POSITION)
    {
        const dxil_spv_option_invariant_position helper =
                { { DXIL_SPV_OPTION_INVARIANT_POSITION }, DXIL_SPV_TRUE };
        if (dxil_spv_converter_add_option(converter, &helper.base) != DXIL_SPV_SUCCESS)
        {
            ERR("dxil-spirv does not support INVARIANT_POSITION.\n");
            ret = VKD3D_ERROR_NOT_IMPLEMENTED;
            goto end;
        }
    }

    if (quirks & VKD3D_SHADER_QUIRK_FORCE_SUBGROUP_SIZE_1)
    {
        const dxil_spv_option_force_subgroup_size helper =
                { { DXIL_SPV_OPTION_FORCE_SUBGROUP_SIZE }, 1, DXIL_SPV_FALSE };
        if (dxil_spv_converter_add_option(converter, &helper.base) != DXIL_SPV_SUCCESS)
        {
            ERR("dxil-spirv does not support FORCE_SUBGROUP_SIZE_1.\n");
            ret = VKD3D_ERROR_NOT_IMPLEMENTED;
            goto end;
        }
    }

    if (quirks & VKD3D_SHADER_QUIRK_REWRITE_GRAD_TO_BIAS)
    {
        const dxil_spv_option_sample_grad_optimization_control helper =
                { { DXIL_SPV_OPTION_SAMPLE_GRAD_OPTIMIZATION_CONTROL }, DXIL_SPV_TRUE, DXIL_SPV_TRUE };
        if (dxil_spv_converter_add_option(converter, &helper.base) != DXIL_SPV_SUCCESS)
        {
            ERR("dxil-spirv does not support SAMPLE_GRAD_OPTIMIZATION_CONTROL.\n");
            ret = VKD3D_ERROR_NOT_IMPLEMENTED;
            goto end;
        }
    }

    remap_userdata.shader_interface_info = shader_interface_info;
    remap_userdata.shader_interface_local_info = NULL;
    remap_userdata.num_root_descriptors = num_root_descriptors;

    dxil_spv_converter_set_root_constant_word_count(converter, root_constant_words);
    dxil_spv_converter_set_root_descriptor_count(converter, num_root_descriptors);
    dxil_spv_converter_set_srv_remapper(converter, dxil_srv_remap, &remap_userdata);
    dxil_spv_converter_set_sampler_remapper(converter, dxil_sampler_remap, &remap_userdata);
    dxil_spv_converter_set_uav_remapper(converter, dxil_uav_remap, &remap_userdata);
    dxil_spv_converter_set_cbv_remapper(converter, dxil_cbv_remap, &remap_userdata);

    dxil_spv_converter_set_vertex_input_remapper(converter, dxil_input_remap, (void *)shader_interface_info);

    if (shader_interface_info->xfb_info)
        dxil_spv_converter_set_stream_output_remapper(converter, dxil_output_remap, (void *)shader_interface_info->xfb_info);

    if (shader_interface_info->stage_input_map)
        dxil_spv_converter_set_stage_input_remapper(converter, dxil_shader_stage_input_remap, (void *)shader_interface_info->stage_input_map);

    if (shader_interface_info->stage_output_map)
        dxil_spv_converter_set_stage_output_remapper(converter, dxil_shader_stage_output_capture, (void *)shader_interface_info->stage_output_map);

    if (dxil_spv_converter_run(converter) != DXIL_SPV_SUCCESS)
    {
        ret = VKD3D_ERROR_INVALID_ARGUMENT;
        goto end;
    }

    if (dxil_spv_converter_get_compiled_spirv(converter, &compiled) != DXIL_SPV_SUCCESS)
    {
        ret = VKD3D_ERROR_INVALID_ARGUMENT;
        goto end;
    }

    /* Late replacement for mesh shaders. */
    if (shader_interface_info->stage == VK_SHADER_STAGE_MESH_BIT_EXT &&
            vkd3d_shader_replace(hash, &spirv->code, &spirv->size))
    {
        spirv->meta.flags |= VKD3D_SHADER_META_FLAG_REPLACED;
    }
    else
    {
        if (!(code = vkd3d_malloc(compiled.size)))
        {
            ret = VKD3D_ERROR_OUT_OF_MEMORY;
            goto end;
        }

        memcpy(code, compiled.data, compiled.size);
        spirv->code = code;
        spirv->size = compiled.size;
    }

    if (spirv_debug)
    {
        dxil_spv_converter_get_compiled_entry_point(converter, &spirv_debug->debug_entry_point_name);
        spirv_debug->debug_entry_point_name = vkd3d_strdup(spirv_debug->debug_entry_point_name);
    }

    dxil_spv_converter_get_compute_workgroup_dimensions(converter,
            &spirv->meta.cs_workgroup_size[0],
            &spirv->meta.cs_workgroup_size[1],
            &spirv->meta.cs_workgroup_size[2]);
    dxil_spv_converter_get_patch_vertex_count(converter, &spirv->meta.patch_vertex_count);
    dxil_spv_converter_get_compute_required_wave_size(converter, &spirv->meta.cs_required_wave_size);

    if (compiler_args->promote_wave_size_heuristics)
    {
        dxil_spv_converter_get_compute_heuristic_max_wave_size(converter, &heuristic_wave_size);
        if (quirks & VKD3D_SHADER_QUIRK_FORCE_MAX_WAVE32)
            heuristic_wave_size = 32;

        if (heuristic_wave_size && !spirv->meta.cs_required_wave_size &&
                compiler_args->max_subgroup_size > heuristic_wave_size &&
                compiler_args->min_subgroup_size <= heuristic_wave_size)
            spirv->meta.cs_required_wave_size = heuristic_wave_size;
    }

    vkd3d_shader_extract_feature_meta(spirv);
    vkd3d_shader_dump_spirv_shader(hash, spirv);

end:
    dxil_spv_converter_free(converter);
    dxil_spv_parsed_blob_free(blob);
    dxil_spv_end_thread_allocator_context();
    return ret;
}

int vkd3d_shader_compile_dxil_export(const struct vkd3d_shader_code *dxil,
        const char *export, const char *demangled_export,
        struct vkd3d_shader_code *spirv, struct vkd3d_shader_code_debug *spirv_debug,
        const struct vkd3d_shader_interface_info *shader_interface_info,
        const struct vkd3d_shader_interface_local_info *shader_interface_local_info,
        const struct vkd3d_shader_compile_arguments *compiler_args)
{
    dxil_spv_option_denorm_preserve_support denorm_preserve = {{ DXIL_SPV_OPTION_DENORM_PRESERVE_SUPPORT }};
    const struct vkd3d_shader_push_constant_buffer *record_constant_buffer;
    const struct vkd3d_shader_resource_binding *resource_binding;
    const struct vkd3d_shader_root_parameter *root_parameter;
    struct vkd3d_dxil_remap_userdata remap_userdata;
    unsigned int num_root_descriptors = 0;
    unsigned int root_constant_words = 0;
    dxil_spv_converter converter = NULL;
    dxil_spv_parsed_blob blob = NULL;
    dxil_spv_compiled_spirv compiled;
    unsigned int i, j, max_size;
    vkd3d_shader_hash_t hash;
    int ret = VKD3D_OK;
    uint32_t quirks;
    void *code;

    dxil_spv_set_thread_log_callback(vkd3d_dxil_log_callback, NULL);

    memset(&spirv->meta, 0, sizeof(spirv->meta));
    hash = vkd3d_shader_hash(dxil);
    spirv->meta.hash = hash;

    quirks = vkd3d_shader_compile_arguments_select_quirks(compiler_args, hash);
    if (quirks & VKD3D_SHADER_QUIRK_FORCE_COMPUTE_BARRIER)
        spirv->meta.flags |= VKD3D_SHADER_META_FLAG_FORCE_COMPUTE_BARRIER_AFTER_DISPATCH;

    /* For user provided (not mangled) export names, just inherit that name. */
    if (!demangled_export)
        demangled_export = export;

    if (demangled_export && vkd3d_shader_replace_export(hash, &spirv->code, &spirv->size, demangled_export))
    {
        spirv->meta.flags |= VKD3D_SHADER_META_FLAG_REPLACED;
        return ret;
    }

    dxil_spv_begin_thread_allocator_context();

    vkd3d_shader_dump_shader(hash, dxil, "lib.dxil");

    if (dxil_spv_parse_dxil_blob(dxil->code, dxil->size, &blob) != DXIL_SPV_SUCCESS)
    {
        ret = VKD3D_ERROR_INVALID_SHADER;
        goto end;
    }

    if (dxil_spv_create_converter(blob, &converter) != DXIL_SPV_SUCCESS)
    {
        ret = VKD3D_ERROR_INVALID_ARGUMENT;
        goto end;
    }

    /* Figure out how many words we need for push constants. */
    for (i = 0; i < shader_interface_info->push_constant_buffer_count; i++)
    {
        max_size = shader_interface_info->push_constant_buffers[i].offset +
                   shader_interface_info->push_constant_buffers[i].size;
        max_size = (max_size + 3) / 4;
        if (max_size > root_constant_words)
            root_constant_words = max_size;
    }

    max_size = shader_interface_info->descriptor_tables.offset / sizeof(uint32_t) +
               shader_interface_info->descriptor_tables.count;
    if (max_size > root_constant_words)
        root_constant_words = max_size;

    for (i = 0; i < shader_interface_info->binding_count; i++)
        if (vkd3d_shader_binding_is_root_descriptor(&shader_interface_info->bindings[i]))
            num_root_descriptors++;

    /* Push local root parameters. We cannot rely on callbacks here
     * since the local root signature has a physical layout in ShaderRecordKHR
     * which needs to be precisely specified up front. */
    for (i = 0; i < shader_interface_local_info->local_root_parameter_count; i++)
    {
        root_parameter = &shader_interface_local_info->local_root_parameters[i];
        switch (root_parameter->parameter_type)
        {
            case D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS:
                record_constant_buffer =
                        &shader_interface_local_info->shader_record_constant_buffers[root_parameter->constant.constant_index];
                dxil_spv_converter_add_local_root_constants(converter,
                        record_constant_buffer->register_space, record_constant_buffer->register_index,
                        root_parameter->constant.constant_count);
                break;

            case D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE:
                if (dxil_spv_converter_begin_local_root_descriptor_table(converter) != DXIL_SPV_SUCCESS)
                {
                    ret = VKD3D_ERROR_INVALID_ARGUMENT;
                    goto end;
                }

                for (j = 0; j < root_parameter->descriptor_table.binding_count; j++)
                {
                    dxil_spv_resource_class resource_class;
                    resource_binding = &root_parameter->descriptor_table.first_binding[j];

                    switch (resource_binding->type)
                    {
                        case VKD3D_SHADER_DESCRIPTOR_TYPE_CBV:
                            resource_class = DXIL_SPV_RESOURCE_CLASS_CBV;
                            break;

                        case VKD3D_SHADER_DESCRIPTOR_TYPE_SRV:
                            resource_class = DXIL_SPV_RESOURCE_CLASS_SRV;
                            break;

                        case VKD3D_SHADER_DESCRIPTOR_TYPE_UAV:
                            resource_class = DXIL_SPV_RESOURCE_CLASS_UAV;
                            break;

                        case VKD3D_SHADER_DESCRIPTOR_TYPE_SAMPLER:
                            resource_class = DXIL_SPV_RESOURCE_CLASS_SAMPLER;
                            break;

                        default:
                            ret = VKD3D_ERROR_INVALID_ARGUMENT;
                            goto end;
                    }

                    dxil_spv_converter_add_local_root_descriptor_table(converter,
                            resource_class,
                            resource_binding->register_space, resource_binding->register_index,
                            resource_binding->register_count, resource_binding->descriptor_offset);
                }

                if (dxil_spv_converter_end_local_root_descriptor_table(converter) != DXIL_SPV_SUCCESS)
                {
                    ret = VKD3D_ERROR_INVALID_ARGUMENT;
                    goto end;
                }
                break;

            case D3D12_ROOT_PARAMETER_TYPE_CBV:
                dxil_spv_converter_add_local_root_descriptor(converter,
                        DXIL_SPV_RESOURCE_CLASS_CBV, root_parameter->descriptor.binding->register_space,
                        root_parameter->descriptor.binding->register_index);
                break;

            case D3D12_ROOT_PARAMETER_TYPE_SRV:
                dxil_spv_converter_add_local_root_descriptor(converter,
                        DXIL_SPV_RESOURCE_CLASS_SRV, root_parameter->descriptor.binding->register_space,
                        root_parameter->descriptor.binding->register_index);
                break;

            case D3D12_ROOT_PARAMETER_TYPE_UAV:
                dxil_spv_converter_add_local_root_descriptor(converter,
                        DXIL_SPV_RESOURCE_CLASS_UAV, root_parameter->descriptor.binding->register_space,
                        root_parameter->descriptor.binding->register_index);
                break;

            default:
                ret = VKD3D_ERROR_INVALID_ARGUMENT;
                goto end;
        }
    }

    /* Root constants come after root descriptors. Offset the counts. */
    if (root_constant_words < num_root_descriptors * 2)
        root_constant_words = num_root_descriptors * 2;
    root_constant_words -= num_root_descriptors * 2;

    {
        const struct dxil_spv_option_ssbo_alignment helper =
                { { DXIL_SPV_OPTION_SSBO_ALIGNMENT }, shader_interface_info->min_ssbo_alignment };
        if (dxil_spv_converter_add_option(converter, &helper.base) != DXIL_SPV_SUCCESS)
        {
            ERR("dxil-spirv does not support SSBO_ALIGNMENT.\n");
            ret = VKD3D_ERROR_NOT_IMPLEMENTED;
            goto end;
        }
    }

    if (shader_interface_info->flags & VKD3D_SHADER_INTERFACE_PUSH_CONSTANTS_AS_UNIFORM_BUFFER)
    {
        const struct dxil_spv_option_root_constant_inline_uniform_block helper =
                { { DXIL_SPV_OPTION_ROOT_CONSTANT_INLINE_UNIFORM_BLOCK },
                  shader_interface_info->push_constant_ubo_binding->set,
                  shader_interface_info->push_constant_ubo_binding->binding,
                  DXIL_SPV_TRUE };
        if (dxil_spv_converter_add_option(converter, &helper.base) != DXIL_SPV_SUCCESS)
        {
            ERR("dxil-spirv does not support PUSH_CONSTANTS_AS_UNIFORM_BUFFER.\n");
            ret = VKD3D_ERROR_NOT_IMPLEMENTED;
            goto end;
        }
    }

    if (shader_interface_info->flags & VKD3D_SHADER_INTERFACE_BINDLESS_CBV_AS_STORAGE_BUFFER)
    {
        static const struct dxil_spv_option_bindless_cbv_ssbo_emulation helper =
                { { DXIL_SPV_OPTION_BINDLESS_CBV_SSBO_EMULATION },
                  DXIL_SPV_TRUE };
        if (dxil_spv_converter_add_option(converter, &helper.base) != DXIL_SPV_SUCCESS)
        {
            ERR("dxil-spirv does not support BINDLESS_CBV_AS_STORAGE_BUFFER.\n");
            ret = VKD3D_ERROR_NOT_IMPLEMENTED;
            goto end;
        }
    }

    /* BDA is mandatory for RT. */
    {
        const struct dxil_spv_option_physical_storage_buffer helper =
                { { DXIL_SPV_OPTION_PHYSICAL_STORAGE_BUFFER }, DXIL_SPV_TRUE };
        if (dxil_spv_converter_add_option(converter, &helper.base) != DXIL_SPV_SUCCESS)
        {
            ERR("dxil-spirv does not support PHYSICAL_STORAGE_BUFFER.\n");
            ret = VKD3D_ERROR_NOT_IMPLEMENTED;
            goto end;
        }
    }

    if (shader_interface_info->flags & VKD3D_SHADER_INTERFACE_TYPED_OFFSET_BUFFER)
    {
        const struct dxil_spv_option_bindless_typed_buffer_offsets helper =
                { { DXIL_SPV_OPTION_BINDLESS_TYPED_BUFFER_OFFSETS },
                  DXIL_SPV_TRUE };
        if (dxil_spv_converter_add_option(converter, &helper.base) != DXIL_SPV_SUCCESS)
        {
            ERR("dxil-spirv does not support BINDLESS_TYPED_BUFFER_OFFSETS.\n");
            ret = VKD3D_ERROR_NOT_IMPLEMENTED;
            goto end;
        }
    }

#ifdef VKD3D_ENABLE_DESCRIPTOR_QA
    if (shader_interface_info->flags & VKD3D_SHADER_INTERFACE_DESCRIPTOR_QA_BUFFER)
    {
        struct dxil_spv_option_descriptor_qa helper;
        helper.base.type = DXIL_SPV_OPTION_DESCRIPTOR_QA;
        helper.enabled = DXIL_SPV_TRUE;
        helper.version = DXIL_SPV_DESCRIPTOR_QA_INTERFACE_VERSION;
        helper.shader_hash = hash;
        helper.global_desc_set = shader_interface_info->descriptor_qa_global_binding->set;
        helper.global_binding = shader_interface_info->descriptor_qa_global_binding->binding;
        helper.heap_desc_set = shader_interface_info->descriptor_qa_heap_binding->set;
        helper.heap_binding = shader_interface_info->descriptor_qa_heap_binding->binding;

        if (dxil_spv_converter_add_option(converter, &helper.base) != DXIL_SPV_SUCCESS)
        {
            ERR("dxil-spirv does not support DESCRIPTOR_QA_BUFFER.\n");
            ret = VKD3D_ERROR_NOT_IMPLEMENTED;
            goto end;
        }
    }
#endif

    {
        const struct dxil_spv_option_sbt_descriptor_size_log2 helper =
                { { DXIL_SPV_OPTION_SBT_DESCRIPTOR_SIZE_LOG2 },
                    vkd3d_bitmask_tzcnt32(shader_interface_info->descriptor_size_cbv_srv_uav),
                    vkd3d_bitmask_tzcnt32(shader_interface_info->descriptor_size_sampler) };
        if (dxil_spv_converter_add_option(converter, &helper.base) != DXIL_SPV_SUCCESS)
        {
            ERR("dxil-spirv does not support SBT_DESCRIPTOR_SIZE_LOG2.\n");
            ret = VKD3D_ERROR_NOT_IMPLEMENTED;
            goto end;
        }
    }

    if (shader_interface_info->flags & VKD3D_SHADER_INTERFACE_RAW_VA_ALIAS_DESCRIPTOR_BUFFER)
    {
        const struct dxil_spv_option_physical_address_descriptor_indexing helper =
                { { DXIL_SPV_OPTION_PHYSICAL_ADDRESS_DESCRIPTOR_INDEXING },
                    shader_interface_info->descriptor_size_cbv_srv_uav / sizeof(VkDeviceAddress),
                    0 };

        if (dxil_spv_converter_add_option(converter, &helper.base) != DXIL_SPV_SUCCESS)
        {
            ERR("dxil-spirv does not support PHYSICAL_ADDRESS_DESCRIPTOR_INDEXING.\n");
            ret = VKD3D_ERROR_NOT_IMPLEMENTED;
            goto end;
        }
    }

    {
        const struct dxil_spv_option_bindless_offset_buffer_layout helper =
                { { DXIL_SPV_OPTION_BINDLESS_OFFSET_BUFFER_LAYOUT },
                  0, 1, 2 };
        if (dxil_spv_converter_add_option(converter, &helper.base) != DXIL_SPV_SUCCESS)
        {
            ERR("dxil-spirv does not support BINDLESS_OFFSET_BUFFER_LAYOUT.\n");
            ret = VKD3D_ERROR_NOT_IMPLEMENTED;
            goto end;
        }
    }

    {
        char buffer[1024];
        const struct dxil_spv_option_shader_source_file helper =
                { { DXIL_SPV_OPTION_SHADER_SOURCE_FILE }, buffer };

        snprintf(buffer, sizeof(buffer), "%016"PRIx64".%s.dxil", spirv->meta.hash, export);
        if (dxil_spv_converter_add_option(converter, &helper.base) != DXIL_SPV_SUCCESS)
            WARN("dxil-spirv does not support SHADER_SOURCE_FILE.\n");
    }

    {
        const struct dxil_spv_option_precise_control helper =
                { { DXIL_SPV_OPTION_PRECISE_CONTROL },
                        (quirks & VKD3D_SHADER_QUIRK_FORCE_NOCONTRACT_MATH) ? DXIL_SPV_TRUE : DXIL_SPV_FALSE,
                        DXIL_SPV_FALSE };
        if (dxil_spv_converter_add_option(converter, &helper.base) != DXIL_SPV_SUCCESS)
        {
            WARN("dxil-spirv does not support PRECISE_CONTROL.\n");
            ret = VKD3D_ERROR_NOT_IMPLEMENTED;
            goto end;
        }
    }

    if (quirks & VKD3D_SHADER_QUIRK_FORCE_LOOP)
    {
        struct dxil_spv_option_branch_control helper = { { DXIL_SPV_OPTION_BRANCH_CONTROL } };
        helper.force_loop = DXIL_SPV_TRUE;
        if (dxil_spv_converter_add_option(converter, &helper.base) != DXIL_SPV_SUCCESS)
        {
            WARN("dxil-spirv does not support BRANCH_CONTROL.\n");
            ret = VKD3D_ERROR_NOT_IMPLEMENTED;
            goto end;
        }
    }

    if (compiler_args)
    {
        for (i = 0; i < compiler_args->target_extension_count; i++)
        {
            if (compiler_args->target_extensions[i] == VKD3D_SHADER_TARGET_EXTENSION_READ_STORAGE_IMAGE_WITHOUT_FORMAT)
            {
                static const dxil_spv_option_typed_uav_read_without_format helper =
                        { { DXIL_SPV_OPTION_TYPED_UAV_READ_WITHOUT_FORMAT }, DXIL_SPV_TRUE };
                if (dxil_spv_converter_add_option(converter, &helper.base) != DXIL_SPV_SUCCESS)
                {
                    ERR("dxil-spirv does not support TYPED_UAV_READ_WITHOUT_FORMAT.\n");
                    ret = VKD3D_ERROR_NOT_IMPLEMENTED;
                    goto end;
                }
            }
            else if (compiler_args->target_extensions[i] == VKD3D_SHADER_TARGET_EXTENSION_SPV_KHR_INTEGER_DOT_PRODUCT)
            {
                static const dxil_spv_option_shader_i8_dot helper =
                        { { DXIL_SPV_OPTION_SHADER_I8_DOT }, DXIL_SPV_TRUE };
                if (dxil_spv_converter_add_option(converter, &helper.base) != DXIL_SPV_SUCCESS)
                {
                    ERR("dxil-spirv does not support SHADER_I8_DOT.\n");
                    ret = VKD3D_ERROR_NOT_IMPLEMENTED;
                    goto end;
                }
            }
            else if (compiler_args->target_extensions[i] == VKD3D_SHADER_TARGET_EXTENSION_SCALAR_BLOCK_LAYOUT)
            {
                dxil_spv_option_scalar_block_layout helper =
                        { { DXIL_SPV_OPTION_SCALAR_BLOCK_LAYOUT }, DXIL_SPV_TRUE };

                for (j = 0; j < compiler_args->target_extension_count; j++)
                {
                    if (compiler_args->target_extensions[j] ==
                            VKD3D_SHADER_TARGET_EXTENSION_ASSUME_PER_COMPONENT_SSBO_ROBUSTNESS)
                    {
                        helper.supports_per_component_robustness = DXIL_SPV_TRUE;
                        break;
                    }
                }

                if (dxil_spv_converter_add_option(converter, &helper.base) != DXIL_SPV_SUCCESS)
                {
                    ERR("dxil-spirv does not support SCALAR_BLOCK_LAYOUT.\n");
                    ret = VKD3D_ERROR_NOT_IMPLEMENTED;
                    goto end;
                }
            }
            else if (compiler_args->target_extensions[i] == VKD3D_SHADER_TARGET_EXTENSION_RAY_TRACING_PRIMITIVE_CULLING)
            {
                /* Only relevant for ray tracing pipelines. Ray query requires support for PrimitiveCulling feature,
                 * and the SPIR-V capability is implicitly enabled. */
                static const dxil_spv_option_shader_ray_tracing_primitive_culling helper =
                        { { DXIL_SPV_OPTION_SHADER_RAY_TRACING_PRIMITIVE_CULLING }, DXIL_SPV_TRUE };
                if (dxil_spv_converter_add_option(converter, &helper.base) != DXIL_SPV_SUCCESS)
                {
                    ERR("dxil-spirv does not support RAY_TRACING_PRIMITIVE_CULLING.\n");
                    ret = VKD3D_ERROR_NOT_IMPLEMENTED;
                    goto end;
                }
            }
            else if (compiler_args->target_extensions[i] == VKD3D_SHADER_TARGET_EXTENSION_MIN_PRECISION_IS_NATIVE_16BIT)
            {
                if (!(quirks & VKD3D_SHADER_QUIRK_FORCE_MIN16_AS_32BIT))
                {
                    static const dxil_spv_option_min_precision_native_16bit helper =
                            { {DXIL_SPV_OPTION_MIN_PRECISION_NATIVE_16BIT }, DXIL_SPV_TRUE };

                    if (dxil_spv_converter_add_option(converter, &helper.base) != DXIL_SPV_SUCCESS)
                    {
                        ERR("dxil-spirv does not support MIN_PRECISION_NATIVE_16BIT.\n");
                        ret = VKD3D_ERROR_NOT_IMPLEMENTED;
                        goto end;
                    }
                }
            }
            else if (compiler_args->target_extensions[i] == VKD3D_SHADER_TARGET_EXTENSION_SUPPORT_FP16_DENORM_PRESERVE)
                denorm_preserve.supports_float16_denorm_preserve = DXIL_SPV_TRUE;
            else if (compiler_args->target_extensions[i] == VKD3D_SHADER_TARGET_EXTENSION_SUPPORT_FP64_DENORM_PRESERVE)
                denorm_preserve.supports_float64_denorm_preserve = DXIL_SPV_TRUE;
            else if (compiler_args->target_extensions[i] == VKD3D_SHADER_TARGET_EXTENSION_SUPPORT_SUBGROUP_PARTITIONED_NV)
            {
                static const dxil_spv_option_subgroup_partitioned_nv helper =
                        { { DXIL_SPV_OPTION_SUBGROUP_PARTITIONED_NV }, DXIL_SPV_TRUE };
                if (dxil_spv_converter_add_option(converter, &helper.base) != DXIL_SPV_SUCCESS)
                {
                    ERR("dxil-spirv does not support SUBGROUP_PARTITIONED_NV.\n");
                    ret = VKD3D_ERROR_NOT_IMPLEMENTED;
                    goto end;
                }
            }
        }
    }

    if (dxil_spv_converter_add_option(converter, &denorm_preserve.base) != DXIL_SPV_SUCCESS)
    {
        ERR("dxil-spirv does not support DENORM_PRESERVE_SUPPORT.\n");
        ret = VKD3D_ERROR_NOT_IMPLEMENTED;
        goto end;
    }

    dxil_spv_converter_set_entry_point(converter, export);

    remap_userdata.shader_interface_info = shader_interface_info;
    remap_userdata.shader_interface_local_info = shader_interface_local_info;
    remap_userdata.num_root_descriptors = num_root_descriptors;

    dxil_spv_converter_set_root_constant_word_count(converter, root_constant_words);
    dxil_spv_converter_set_root_descriptor_count(converter, num_root_descriptors);
    dxil_spv_converter_set_srv_remapper(converter, dxil_srv_remap, &remap_userdata);
    dxil_spv_converter_set_sampler_remapper(converter, dxil_sampler_remap, &remap_userdata);
    dxil_spv_converter_set_uav_remapper(converter, dxil_uav_remap, &remap_userdata);
    dxil_spv_converter_set_cbv_remapper(converter, dxil_cbv_remap, &remap_userdata);

    if (dxil_spv_converter_run(converter) != DXIL_SPV_SUCCESS)
    {
        ret = VKD3D_ERROR_INVALID_ARGUMENT;
        goto end;
    }

    if (dxil_spv_converter_get_compiled_spirv(converter, &compiled) != DXIL_SPV_SUCCESS)
    {
        ret = VKD3D_ERROR_INVALID_ARGUMENT;
        goto end;
    }

    if (!(code = vkd3d_malloc(compiled.size)))
    {
        ret = VKD3D_ERROR_OUT_OF_MEMORY;
        goto end;
    }

    memcpy(code, compiled.data, compiled.size);
    spirv->code = code;
    spirv->size = compiled.size;

    /* Nothing useful here for now. */
    if (spirv_debug)
        memset(spirv_debug, 0, sizeof(*spirv_debug));

    vkd3d_shader_extract_feature_meta(spirv);

    if (demangled_export)
        vkd3d_shader_dump_spirv_shader_export(hash, spirv, demangled_export);

end:
    dxil_spv_converter_free(converter);
    dxil_spv_parsed_blob_free(blob);
    dxil_spv_end_thread_allocator_context();
    return ret;
}

void vkd3d_shader_dxil_free_library_entry_points(struct vkd3d_shader_library_entry_point *entry_points, size_t count)
{
    size_t i;
    for (i = 0; i < count; i++)
    {
        vkd3d_free(entry_points[i].mangled_entry_point);
        vkd3d_free(entry_points[i].plain_entry_point);
        vkd3d_free(entry_points[i].real_entry_point);
        vkd3d_free(entry_points[i].debug_entry_point);
    }

    vkd3d_free(entry_points);
}

void vkd3d_shader_dxil_free_library_subobjects(struct vkd3d_shader_library_subobject *subobjects, size_t count)
{
    size_t i, j;

    for (i = 0; i < count; i++)
    {
        if (subobjects[i].kind == VKD3D_SHADER_SUBOBJECT_KIND_SUBOBJECT_TO_EXPORTS_ASSOCIATION)
        {
            for (j = 0; j < subobjects[i].data.association.NumExports; j++)
                vkd3d_free((void*)subobjects[i].data.association.pExports[j]);
            vkd3d_free((void*)subobjects[i].data.association.pExports);
            vkd3d_free((void*)subobjects[i].data.association.SubobjectToAssociate);
        }
        else if (subobjects[i].kind == VKD3D_SHADER_SUBOBJECT_KIND_HIT_GROUP)
        {
            vkd3d_free((void*)subobjects[i].data.hit_group.HitGroupExport);
            vkd3d_free((void*)subobjects[i].data.hit_group.AnyHitShaderImport);
            vkd3d_free((void*)subobjects[i].data.hit_group.ClosestHitShaderImport);
            vkd3d_free((void*)subobjects[i].data.hit_group.IntersectionShaderImport);
        }
    }

    vkd3d_free(subobjects);
}

static VkShaderStageFlagBits convert_stage(dxil_spv_shader_stage stage)
{
    /* Only interested in RT entry_points. There is no way yet to use lib_6_3+ for non-RT. */
    switch (stage)
    {
        case DXIL_SPV_STAGE_RAY_GENERATION:
            return VK_SHADER_STAGE_RAYGEN_BIT_KHR;
        case DXIL_SPV_STAGE_CLOSEST_HIT:
            return VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
        case DXIL_SPV_STAGE_MISS:
            return VK_SHADER_STAGE_MISS_BIT_KHR;
        case DXIL_SPV_STAGE_CALLABLE:
            return VK_SHADER_STAGE_CALLABLE_BIT_KHR;
        case DXIL_SPV_STAGE_INTERSECTION:
            return VK_SHADER_STAGE_INTERSECTION_BIT_KHR;
        case DXIL_SPV_STAGE_ANY_HIT:
            return VK_SHADER_STAGE_ANY_HIT_BIT_KHR;
        default:
            return VK_SHADER_STAGE_ALL;
    }
}

static bool vkd3d_dxil_build_entry(struct vkd3d_shader_library_entry_point *entry,
        unsigned int identifier,
        const char *mangled_name, const char *demangled_name,
        dxil_spv_shader_stage stage)
{
    entry->identifier = identifier;
    entry->mangled_entry_point = vkd3d_dup_entry_point(mangled_name);
    if (!entry->mangled_entry_point)
        return false;

    entry->plain_entry_point = vkd3d_dup_entry_point(demangled_name);
    if (!entry->plain_entry_point)
    {
        vkd3d_free(entry->mangled_entry_point);
        entry->mangled_entry_point = NULL;
        return false;
    }

    entry->real_entry_point = vkd3d_strdup(mangled_name);
    entry->debug_entry_point = vkd3d_strdup(demangled_name);
    entry->stage = convert_stage(stage);
    entry->pipeline_variant_index = UINT32_MAX;
    entry->stage_index = UINT32_MAX;
    return true;
}

static void vkd3d_shader_dxil_copy_subobject(unsigned int identifier,
        struct vkd3d_shader_library_subobject *subobject,
        const dxil_spv_rdat_subobject *dxil_subobject)
{
    unsigned int i;

    /* Reuse same enums as DXIL. */
    subobject->kind = (enum vkd3d_shader_subobject_kind)dxil_subobject->kind;
    subobject->name = dxil_subobject->subobject_name;
    subobject->dxil_identifier = identifier;

    switch (dxil_subobject->kind)
    {
        case DXIL_SPV_RDAT_SUBOBJECT_KIND_GLOBAL_ROOT_SIGNATURE:
        case DXIL_SPV_RDAT_SUBOBJECT_KIND_LOCAL_ROOT_SIGNATURE:
            subobject->data.payload.data = dxil_subobject->payload;
            subobject->data.payload.size = dxil_subobject->payload_size;
            break;

        case DXIL_SPV_RDAT_SUBOBJECT_KIND_RAYTRACING_PIPELINE_CONFIG:
            /* Normalize the kind. */
            subobject->kind = VKD3D_SHADER_SUBOBJECT_KIND_RAYTRACING_PIPELINE_CONFIG1;
            subobject->data.pipeline_config.MaxTraceRecursionDepth = dxil_subobject->args[0];
            subobject->data.pipeline_config.Flags = 0;
            break;

        case DXIL_SPV_RDAT_SUBOBJECT_KIND_RAYTRACING_PIPELINE_CONFIG1:
            subobject->kind = VKD3D_SHADER_SUBOBJECT_KIND_RAYTRACING_PIPELINE_CONFIG1;
            subobject->data.pipeline_config.MaxTraceRecursionDepth = dxil_subobject->args[0];
            subobject->data.pipeline_config.Flags = dxil_subobject->args[1];
            break;

        case DXIL_SPV_RDAT_SUBOBJECT_KIND_RAYTRACING_SHADER_CONFIG:
            subobject->data.shader_config.MaxPayloadSizeInBytes = dxil_subobject->args[0];
            subobject->data.shader_config.MaxAttributeSizeInBytes = dxil_subobject->args[1];
            break;

        case DXIL_SPV_RDAT_SUBOBJECT_KIND_HIT_GROUP:
            /* Enum aliases. */
            subobject->data.hit_group.Type = (D3D12_HIT_GROUP_TYPE)dxil_subobject->hit_group_type;
            assert(dxil_subobject->num_exports == 3);
            /* Implementation simplifies a lot if we can reuse the D3D12 type here. */
            subobject->data.hit_group.HitGroupExport = vkd3d_dup_entry_point(dxil_subobject->subobject_name);
            subobject->data.hit_group.AnyHitShaderImport = dxil_subobject->exports[0] && *dxil_subobject->exports[0] != '\0' ?
                    vkd3d_dup_entry_point(dxil_subobject->exports[0]) : NULL;
            subobject->data.hit_group.ClosestHitShaderImport = dxil_subobject->exports[1] && *dxil_subobject->exports[1] != '\0' ?
                    vkd3d_dup_entry_point(dxil_subobject->exports[1]) : NULL;
            subobject->data.hit_group.IntersectionShaderImport = dxil_subobject->exports[2] && *dxil_subobject->exports[2] != '\0' ?
                    vkd3d_dup_entry_point(dxil_subobject->exports[2]) : NULL;
            break;

        case DXIL_SPV_RDAT_SUBOBJECT_KIND_STATE_OBJECT_CONFIG:
            subobject->data.object_config.Flags = dxil_subobject->args[0];
            break;

        case DXIL_SPV_RDAT_SUBOBJECT_KIND_SUBOBJECT_TO_EXPORTS_ASSOCIATION:
            assert(dxil_subobject->num_exports >= 1);
            subobject->data.association.SubobjectToAssociate = vkd3d_dup_entry_point(dxil_subobject->exports[0]);
            subobject->data.association.pExports = vkd3d_malloc((dxil_subobject->num_exports - 1) * sizeof(LPCWSTR));
            subobject->data.association.NumExports = dxil_subobject->num_exports - 1;
            for (i = 1; i < dxil_subobject->num_exports; i++)
                subobject->data.association.pExports[i - 1] = vkd3d_dup_entry_point(dxil_subobject->exports[i]);
            break;

        default:
            FIXME("Unrecognized RDAT subobject type: %u.\n", dxil_subobject->kind);
            break;
    }
}

int vkd3d_shader_dxil_find_global_root_signature_subobject(const void *dxbc, size_t size,
        struct vkd3d_shader_code *code)
{
    dxil_spv_parsed_blob blob = NULL;
    dxil_spv_rdat_subobject rdat;
    unsigned int i, rdat_count;
    int found_index = -1;
    int ret = VKD3D_OK;

    dxil_spv_set_thread_log_callback(vkd3d_dxil_log_callback, NULL);
    dxil_spv_begin_thread_allocator_context();

    if (dxil_spv_parse_dxil_blob(dxbc, size, &blob) != DXIL_SPV_SUCCESS)
    {
        ret = VKD3D_ERROR_INVALID_ARGUMENT;
        goto end;
    }

    rdat_count = dxil_spv_parsed_blob_get_num_rdat_subobjects(blob);

    for (i = 0; i < rdat_count; i++)
    {
        dxil_spv_parsed_blob_get_rdat_subobject(blob, i, &rdat);
        if (rdat.kind == DXIL_SPV_RDAT_SUBOBJECT_KIND_GLOBAL_ROOT_SIGNATURE)
        {
            if (found_index >= 0)
            {
                /* Ambiguous. Must fail. */
                ret = VKD3D_ERROR_INVALID_ARGUMENT;
                goto end;
            }

            found_index = (int)i;
        }
    }

    if (found_index < 0)
    {
        ret = VKD3D_ERROR_INVALID_ARGUMENT;
        goto end;
    }

    dxil_spv_parsed_blob_get_rdat_subobject(blob, found_index, &rdat);
    memset(code, 0, sizeof(*code));

    /* These point directly to blob. */
    code->code = rdat.payload;
    code->size = rdat.payload_size;

end:
    if (blob)
        dxil_spv_parsed_blob_free(blob);
    dxil_spv_end_thread_allocator_context();
    return ret;
}

int vkd3d_shader_dxil_append_library_entry_points_and_subobjects(
        const D3D12_DXIL_LIBRARY_DESC *library_desc,
        unsigned int identifier,
        struct vkd3d_shader_library_entry_point **entry_points,
        size_t *entry_point_size, size_t *entry_point_count,
        struct vkd3d_shader_library_subobject **subobjects,
        size_t *subobjects_size, size_t *subobjects_count)
{
    struct vkd3d_shader_library_entry_point new_entry;
    struct vkd3d_shader_library_subobject *subobject;
    dxil_spv_parsed_blob blob = NULL;
    struct vkd3d_shader_code code;
    dxil_spv_rdat_subobject sub;
    dxil_spv_shader_stage stage;
    const char *demangled_entry;
    const char *mangled_entry;
    char *ascii_entry = NULL;
    vkd3d_shader_hash_t hash;
    unsigned int count, i, j;
    unsigned int rdat_count;
    int ret = VKD3D_OK;

    dxil_spv_set_thread_log_callback(vkd3d_dxil_log_callback, NULL);

    memset(&new_entry, 0, sizeof(new_entry));
    dxil_spv_begin_thread_allocator_context();

    memset(&code, 0, sizeof(code));
    code.code = library_desc->DXILLibrary.pShaderBytecode;
    code.size = library_desc->DXILLibrary.BytecodeLength;
    hash = vkd3d_shader_hash(&code);
    vkd3d_shader_dump_shader(hash, &code, "lib.dxil");

    if (dxil_spv_parse_dxil_blob(
            library_desc->DXILLibrary.pShaderBytecode,
            library_desc->DXILLibrary.BytecodeLength,
            &blob) != DXIL_SPV_SUCCESS)
    {
        ret = VKD3D_ERROR_INVALID_ARGUMENT;
        goto end;
    }

    rdat_count = dxil_spv_parsed_blob_get_num_rdat_subobjects(blob);

    if (library_desc->NumExports)
    {
        for (i = 0; i < library_desc->NumExports; i++)
        {
            if (library_desc->pExports[i].ExportToRename)
                ascii_entry = vkd3d_strdup_w_utf8(library_desc->pExports[i].ExportToRename, 0);
            else
                ascii_entry = vkd3d_strdup_w_utf8(library_desc->pExports[i].Name, 0);

            /* An export can point to a subobject or an entry point. */
            for (j = 0; j < rdat_count; j++)
            {
                dxil_spv_parsed_blob_get_rdat_subobject(blob, j, &sub);
                /* Subobject names are not mangled. */
                if (strcmp(sub.subobject_name, ascii_entry) == 0)
                    break;
            }

            if (j < rdat_count)
            {
                vkd3d_array_reserve((void**)subobjects, subobjects_size,
                        *subobjects_count + 1, sizeof(**subobjects));
                subobject = &(*subobjects)[*subobjects_count];
                vkd3d_shader_dxil_copy_subobject(identifier, subobject, &sub);
                *subobjects_count += 1;
            }
            else
            {
                stage = dxil_spv_parsed_blob_get_shader_stage_for_entry(blob, ascii_entry);
                if (stage == DXIL_SPV_STAGE_UNKNOWN)
                {
                    ret = VKD3D_ERROR_INVALID_ARGUMENT;
                    goto end;
                }

                new_entry.real_entry_point = ascii_entry;
                new_entry.debug_entry_point = NULL;
                new_entry.plain_entry_point = vkd3d_wstrdup(library_desc->pExports[i].Name);
                new_entry.mangled_entry_point = NULL;
                new_entry.identifier = identifier;
                new_entry.stage = convert_stage(stage);
                new_entry.pipeline_variant_index = UINT32_MAX;
                new_entry.stage_index = UINT32_MAX;
                ascii_entry = NULL;

                vkd3d_array_reserve((void**)entry_points, entry_point_size,
                        *entry_point_count + 1, sizeof(new_entry));
                (*entry_points)[(*entry_point_count)++] = new_entry;
                memset(&new_entry, 0, sizeof(new_entry));
            }
        }
    }
    else
    {
        if (dxil_spv_parsed_blob_get_num_entry_points(blob, &count) != DXIL_SPV_SUCCESS)
        {
            ret = VKD3D_ERROR_INVALID_ARGUMENT;
            goto end;
        }

        for (i = 0; i < count; i++)
        {
            dxil_spv_parsed_blob_get_entry_point_name(blob, i, &mangled_entry);
            dxil_spv_parsed_blob_get_entry_point_demangled_name(blob, i, &demangled_entry);
            stage = dxil_spv_parsed_blob_get_shader_stage_for_entry(blob, mangled_entry);
            if (stage == DXIL_SPV_STAGE_UNKNOWN)
            {
                ERR("Invalid shader stage for %s.\n", mangled_entry);
                ret = VKD3D_ERROR_INVALID_ARGUMENT;
                goto end;
            }

            if (!vkd3d_dxil_build_entry(&new_entry, identifier, mangled_entry, demangled_entry, stage))
            {
                ret = VKD3D_ERROR_INVALID_ARGUMENT;
                goto end;
            }

            vkd3d_array_reserve((void**)entry_points, entry_point_size,
                    *entry_point_count + 1, sizeof(new_entry));
            (*entry_points)[(*entry_point_count)++] = new_entry;
            memset(&new_entry, 0, sizeof(new_entry));
        }

        if (rdat_count)
        {
            /* All subobjects are also exported. */
            vkd3d_array_reserve((void**)subobjects, subobjects_size,
                    *subobjects_count + rdat_count, sizeof(**subobjects));

            for (i = 0; i < rdat_count; i++)
            {
                dxil_spv_parsed_blob_get_rdat_subobject(blob, i, &sub);
                subobject = &(*subobjects)[*subobjects_count];
                vkd3d_shader_dxil_copy_subobject(identifier, subobject, &sub);
                *subobjects_count += 1;
            }
        }
    }

end:
    vkd3d_free(ascii_entry);
    vkd3d_free(new_entry.mangled_entry_point);
    vkd3d_free(new_entry.plain_entry_point);
    vkd3d_free(new_entry.real_entry_point);
    vkd3d_free(new_entry.debug_entry_point);
    if (blob)
        dxil_spv_parsed_blob_free(blob);
    dxil_spv_end_thread_allocator_context();
    return ret;
}
