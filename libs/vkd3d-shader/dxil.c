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

#include "vkd3d_shader_private.h"
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
        default:
            return false;
    }
}

static unsigned dxil_resource_flags_from_kind(dxil_spv_resource_kind kind)
{
    switch (kind)
    {
        case DXIL_SPV_RESOURCE_KIND_RAW_BUFFER:
        case DXIL_SPV_RESOURCE_KIND_TYPED_BUFFER:
        case DXIL_SPV_RESOURCE_KIND_STRUCTURED_BUFFER:
            return VKD3D_SHADER_BINDING_FLAG_BUFFER;

        default:
            return VKD3D_SHADER_BINDING_FLAG_IMAGE;
    }
}

static dxil_spv_bool dxil_srv_remap(void *userdata, const dxil_spv_d3d_binding *d3d_binding,
                                    dxil_spv_vulkan_binding *vk_binding)
{
    const struct vkd3d_shader_interface_info *shader_interface_info = userdata;
    unsigned int binding_count = shader_interface_info->binding_count;
    unsigned int i, resource_flags;
    resource_flags = dxil_resource_flags_from_kind(d3d_binding->kind);

    for (i = 0; i < binding_count; i++)
    {
        const struct vkd3d_shader_resource_binding *binding = &shader_interface_info->bindings[i];
        if (binding->type == VKD3D_SHADER_DESCRIPTOR_TYPE_SRV &&
            binding->register_space == d3d_binding->register_space &&
            binding->register_index == d3d_binding->register_index &&
            (binding->flags & resource_flags) != 0 &&
            dxil_match_shader_visibility(binding->shader_visibility, d3d_binding->stage))
        {
            memset(vk_binding, 0, sizeof(*vk_binding));
            vk_binding->set = binding->binding.set;
            vk_binding->binding = binding->binding.binding;
            return DXIL_SPV_TRUE;
        }
    }

    return DXIL_SPV_FALSE;
}

static dxil_spv_bool dxil_sampler_remap(void *userdata, const dxil_spv_d3d_binding *d3d_binding,
                                        dxil_spv_vulkan_binding *vk_binding)
{
    const struct vkd3d_shader_interface_info *shader_interface_info = userdata;
    unsigned int binding_count = shader_interface_info->binding_count;
    unsigned int i;

    for (i = 0; i < binding_count; i++)
    {
        const struct vkd3d_shader_resource_binding *binding = &shader_interface_info->bindings[i];
        if (binding->type == VKD3D_SHADER_DESCRIPTOR_TYPE_SAMPLER &&
            binding->register_space == d3d_binding->register_space &&
            binding->register_index == d3d_binding->register_index &&
            dxil_match_shader_visibility(binding->shader_visibility, d3d_binding->stage))
        {
            memset(vk_binding, 0, sizeof(*vk_binding));
            vk_binding->set = binding->binding.set;
            vk_binding->binding = binding->binding.binding;
            return DXIL_SPV_TRUE;
        }
    }

    return DXIL_SPV_FALSE;
}

static dxil_spv_bool dxil_input_remap(void *userdata, const dxil_spv_d3d_vertex_input *d3d_input,
                                      dxil_spv_vulkan_vertex_input *vk_input)
{
    vk_input->location = d3d_input->start_row;
    return DXIL_SPV_TRUE;
}

static dxil_spv_bool dxil_output_remap(void *userdata, const dxil_spv_d3d_stream_output *d3d_output,
                                       dxil_spv_vulkan_stream_output *vk_output)
{
    const struct vkd3d_shader_transform_feedback_info *xfb_info = userdata;
    const struct vkd3d_shader_transform_feedback_element *xfb_element;
    unsigned int i, offset, stride;

    offset = 0;
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

        offset += 4 * e->component_count;
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
    vk_output->offset = offset;
    vk_output->stride = stride;
    vk_output->buffer_index = xfb_element->output_slot;
    return DXIL_SPV_TRUE;
}

static dxil_spv_bool dxil_uav_remap(void *userdata, const dxil_spv_uav_d3d_binding *d3d_binding,
                                    dxil_spv_uav_vulkan_binding *vk_binding)
{
    const struct vkd3d_shader_interface_info *shader_interface_info = userdata;
    const struct vkd3d_shader_effective_uav_counter_binding_info *counter_info;
    unsigned int binding_count = shader_interface_info->binding_count;
    unsigned int i, j, resource_flags;

    resource_flags = dxil_resource_flags_from_kind(d3d_binding->d3d_binding.kind);

    for (i = 0; i < binding_count; i++)
    {
        const struct vkd3d_shader_resource_binding *binding = &shader_interface_info->bindings[i];
        if (binding->type == VKD3D_SHADER_DESCRIPTOR_TYPE_UAV &&
            binding->register_space == d3d_binding->d3d_binding.register_space &&
            binding->register_index == d3d_binding->d3d_binding.register_index &&
            (binding->flags & resource_flags) != 0 &&
            dxil_match_shader_visibility(binding->shader_visibility, d3d_binding->d3d_binding.stage))
        {
            memset(vk_binding, 0, sizeof(*vk_binding));
            vk_binding->buffer_binding.set = binding->binding.set;
            vk_binding->buffer_binding.binding = binding->binding.binding;

            if (d3d_binding->has_counter)
            {
                unsigned int resource_index = d3d_binding->d3d_binding.resource_index;
                for (j = 0; j < shader_interface_info->uav_counter_count; j++)
                {
                    /* Match UAV counters by resource index, and not binding. */
                    if (shader_interface_info->uav_counters[j].counter_index == resource_index)
                    {
                        counter_info = vkd3d_find_struct(shader_interface_info->next, EFFECTIVE_UAV_COUNTER_BINDING_INFO);

                        if (counter_info && resource_index < counter_info->uav_counter_count)
                        {
                            /* Let pipeline know what the actual space/bindings for the counter are. */
                            counter_info->uav_register_bindings[resource_index] = d3d_binding->d3d_binding.register_index;
                            counter_info->uav_register_spaces[resource_index] = d3d_binding->d3d_binding.register_space;
                        }

                        vk_binding->counter_binding.set = shader_interface_info->uav_counters[j].binding.set;
                        vk_binding->counter_binding.binding = shader_interface_info->uav_counters[j].binding.binding;
                        break;
                    }
                }

                if (j == shader_interface_info->uav_counter_count)
                    return DXIL_SPV_FALSE;
            }

            return DXIL_SPV_TRUE;
        }
    }

    return DXIL_SPV_FALSE;
}

static dxil_spv_bool dxil_cbv_remap(void *userdata, const dxil_spv_d3d_binding *d3d_binding,
                                    dxil_spv_cbv_vulkan_binding *vk_binding)
{
    const struct vkd3d_shader_interface_info *shader_interface_info = userdata;
    unsigned int binding_count = shader_interface_info->binding_count;
    unsigned int i;

    /* Try to map to root constant -> push constant.
     * Do not consider shader visibility here, as DXBC path does not appear to do it either. */
    for (i = 0; i < shader_interface_info->push_constant_buffer_count; i++)
    {
        const struct vkd3d_shader_push_constant_buffer *push = &shader_interface_info->push_constant_buffers[i];
        if (push->register_space == d3d_binding->register_space &&
            push->register_index == d3d_binding->register_index)
        {
            memset(vk_binding, 0, sizeof(*vk_binding));
            vk_binding->push_constant = DXIL_SPV_TRUE;
            vk_binding->vulkan.push_constant.offset_in_words = push->offset / sizeof(uint32_t);
            return DXIL_SPV_TRUE;
        }
    }

    /* Fall back to regular CBV -> UBO. */
    for (i = 0; i < binding_count; i++)
    {
        const struct vkd3d_shader_resource_binding *binding = &shader_interface_info->bindings[i];
        if (binding->type == VKD3D_SHADER_DESCRIPTOR_TYPE_CBV &&
            binding->register_space == d3d_binding->register_space &&
            binding->register_index == d3d_binding->register_index &&
            dxil_match_shader_visibility(binding->shader_visibility, d3d_binding->stage))
        {
            memset(vk_binding, 0, sizeof(*vk_binding));
            vk_binding->vulkan.uniform_binding.set = binding->binding.set;
            vk_binding->vulkan.uniform_binding.binding = binding->binding.binding;
            return DXIL_SPV_TRUE;
        }
    }

    return DXIL_SPV_FALSE;
}

static dxil_spv_bool dxil_uav_scan(
        void *userdata,
        const dxil_spv_uav_d3d_binding *binding,
        dxil_spv_uav_vulkan_binding *vk_binding)
{
    struct vkd3d_shader_scan_info *scan_info = userdata;
    if (binding->has_counter)
    {
        if (binding->d3d_binding.resource_index >= 8)
            FIXME("DXIL shader attempts to use UAV counter for resource index >= 8.\n");
        else
            scan_info->uav_counter_mask |= 1u << binding->d3d_binding.resource_index;
    }
    return DXIL_SPV_TRUE;
}

int vkd3d_shader_scan_dxil(const struct vkd3d_shader_code *dxbc,
        struct vkd3d_shader_scan_info *scan_info)
{
    dxil_spv_parsed_blob blob = NULL;
    int ret = VKD3D_OK;

    memset(scan_info, 0, sizeof(*scan_info));
    if (dxil_spv_parse_dxil_blob(dxbc->code, dxbc->size, &blob) != DXIL_SPV_SUCCESS)
    {
        ret = VKD3D_ERROR_INVALID_SHADER;
        goto end;
    }

    dxil_spv_parsed_blob_scan_resources(blob, NULL, NULL, NULL, dxil_uav_scan, scan_info);

end:
    dxil_spv_parsed_blob_free(blob);
    return ret;
}

int vkd3d_shader_compile_dxil(const struct vkd3d_shader_code *dxbc,
        struct vkd3d_shader_code *spirv,
        const struct vkd3d_shader_interface_info *shader_interface_info)
{
    dxil_spv_parsed_blob blob = NULL;
    dxil_spv_converter converter = NULL;
    dxil_spv_compiled_spirv compiled;
    void *code;
    int ret = VKD3D_OK;
    unsigned int i;
    unsigned int root_constant_words = 0;
    enum vkd3d_shader_type shader_type;
    const struct vkd3d_shader_transform_feedback_info *xfb_info;

    if (dxil_spv_parse_dxil_blob(dxbc->code, dxbc->size, &blob) != DXIL_SPV_SUCCESS)
    {
        ret = VKD3D_ERROR_INVALID_SHADER;
        goto end;
    }

    switch (dxil_spv_parsed_blob_get_shader_stage(blob))
    {
        case DXIL_SPV_STAGE_VERTEX:
            shader_type = VKD3D_SHADER_TYPE_VERTEX;
            break;
        case DXIL_SPV_STAGE_HULL:
            shader_type = VKD3D_SHADER_TYPE_HULL;
            break;
        case DXIL_SPV_STAGE_DOMAIN:
            shader_type = VKD3D_SHADER_TYPE_DOMAIN;
            break;
        case DXIL_SPV_STAGE_GEOMETRY:
            shader_type = VKD3D_SHADER_TYPE_GEOMETRY;
            break;
        case DXIL_SPV_STAGE_PIXEL:
            shader_type = VKD3D_SHADER_TYPE_PIXEL;
            break;
        case DXIL_SPV_STAGE_COMPUTE:
            shader_type = VKD3D_SHADER_TYPE_COMPUTE;
            break;
        default:
            ret = VKD3D_ERROR_INVALID_SHADER;
            goto end;
    }

    vkd3d_shader_dump_shader(shader_type, dxbc);

    if (dxil_spv_create_converter(blob, &converter) != DXIL_SPV_SUCCESS)
    {
        ret = VKD3D_ERROR_INVALID_SHADER;
        goto end;
    }

    /* Figure out how many words we need for push constants. */
    for (i = 0; i < shader_interface_info->push_constant_buffer_count; i++)
    {
        unsigned int max_size = shader_interface_info->push_constant_buffers[i].offset +
                                shader_interface_info->push_constant_buffers[i].size;
        max_size = (max_size + 3) / 4;
        if (max_size > root_constant_words)
            root_constant_words = max_size;
    }

    dxil_spv_converter_set_root_constant_word_count(converter, root_constant_words);
    dxil_spv_converter_set_srv_remapper(converter, dxil_srv_remap, (void *)shader_interface_info);
    dxil_spv_converter_set_sampler_remapper(converter, dxil_sampler_remap, (void *)shader_interface_info);
    dxil_spv_converter_set_uav_remapper(converter, dxil_uav_remap, (void *)shader_interface_info);
    dxil_spv_converter_set_cbv_remapper(converter, dxil_cbv_remap, (void *)shader_interface_info);
    dxil_spv_converter_set_vertex_input_remapper(converter, dxil_input_remap, (void *)shader_interface_info);

    xfb_info = vkd3d_find_struct(shader_interface_info->next, TRANSFORM_FEEDBACK_INFO);
    if (xfb_info)
        dxil_spv_converter_set_stream_output_remapper(converter, dxil_output_remap, (void *)xfb_info);

    if (dxil_spv_converter_run(converter) != DXIL_SPV_SUCCESS)
    {
        ret = VKD3D_ERROR_INVALID_SHADER;
        goto end;
    }

    if (dxil_spv_converter_get_compiled_spirv(converter, &compiled) != DXIL_SPV_SUCCESS)
    {
        ret = VKD3D_ERROR_INVALID_SHADER;
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

    vkd3d_shader_dump_spirv_shader(shader_type, spirv);

end:
    dxil_spv_converter_free(converter);
    dxil_spv_parsed_blob_free(blob);
    return ret;
}

