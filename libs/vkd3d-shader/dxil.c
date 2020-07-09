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

static bool dxil_resource_is_in_range(const struct vkd3d_shader_resource_binding *binding,
                                      const dxil_spv_d3d_binding *d3d_binding)
{
    if (binding->register_space != d3d_binding->register_space)
        return false;
    if (d3d_binding->register_index < binding->register_index)
        return false;

    return binding->register_count == UINT_MAX ||
           ((d3d_binding->register_index - binding->register_index) < binding->register_count);
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
            dxil_resource_is_in_range(binding, d3d_binding) &&
            (binding->flags & resource_flags) != 0 &&
            dxil_match_shader_visibility(binding->shader_visibility, d3d_binding->stage))
        {
            memset(vk_binding, 0, sizeof(*vk_binding));

            if (binding->flags & VKD3D_SHADER_BINDING_FLAG_BINDLESS)
            {
                vk_binding->bindless.use_heap = DXIL_SPV_TRUE;
                vk_binding->bindless.heap_root_offset = binding->descriptor_offset +
                        d3d_binding->register_index - binding->register_index;
                vk_binding->bindless.root_constant_word = binding->descriptor_table +
                        (shader_interface_info->descriptor_tables.offset / sizeof(uint32_t));
                vk_binding->set = binding->binding.set;
                vk_binding->binding = binding->binding.binding;
            }
            else
            {
                vk_binding->set = binding->binding.set;
                vk_binding->binding = binding->binding.binding + d3d_binding->register_index - binding->register_index;
            }

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
            dxil_resource_is_in_range(binding, d3d_binding) &&
            dxil_match_shader_visibility(binding->shader_visibility, d3d_binding->stage))
        {
            memset(vk_binding, 0, sizeof(*vk_binding));

            if (binding->flags & VKD3D_SHADER_BINDING_FLAG_BINDLESS)
            {
                vk_binding->bindless.use_heap = DXIL_SPV_TRUE;
                vk_binding->bindless.heap_root_offset = binding->descriptor_offset +
                        d3d_binding->register_index - binding->register_index;
                vk_binding->bindless.root_constant_word = binding->descriptor_table +
                        (shader_interface_info->descriptor_tables.offset / sizeof(uint32_t));
                vk_binding->set = binding->binding.set;
                vk_binding->binding = binding->binding.binding;
            }
            else
            {
                vk_binding->set = binding->binding.set;
                vk_binding->binding = binding->binding.binding + d3d_binding->register_index - binding->register_index;
            }

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
    unsigned int binding_count = shader_interface_info->binding_count;
    const struct vkd3d_shader_resource_binding *binding;
    unsigned int i, resource_flags;
    bool found_binding = false;

    resource_flags = dxil_resource_flags_from_kind(d3d_binding->d3d_binding.kind);
    if (d3d_binding->has_counter)
        resource_flags |= VKD3D_SHADER_BINDING_FLAG_COUNTER;

    memset(vk_binding, 0, sizeof(*vk_binding));

    for (i = 0; i < binding_count; i++)
    {
        binding = &shader_interface_info->bindings[i];
        if (binding->type == VKD3D_SHADER_DESCRIPTOR_TYPE_UAV &&
            dxil_resource_is_in_range(binding, &d3d_binding->d3d_binding) &&
            (binding->flags & resource_flags) != 0 &&
            dxil_match_shader_visibility(binding->shader_visibility, d3d_binding->d3d_binding.stage))
        {
            if (d3d_binding->has_counter && (binding->flags & VKD3D_SHADER_BINDING_FLAG_COUNTER))
            {
                if (binding->flags & VKD3D_SHADER_BINDING_FLAG_BINDLESS)
                {
                    vk_binding->counter_binding.bindless.use_heap = DXIL_SPV_TRUE;
                    vk_binding->counter_binding.bindless.heap_root_offset = binding->descriptor_offset +
                            d3d_binding->d3d_binding.register_index - binding->register_index;
                    vk_binding->counter_binding.bindless.root_constant_word =
                            binding->descriptor_table +
                            (shader_interface_info->descriptor_tables.offset / sizeof(uint32_t));
                    vk_binding->counter_binding.set = binding->binding.set;
                    vk_binding->counter_binding.binding = binding->binding.binding;
                }
                else
                {
                    vk_binding->counter_binding.set = binding->binding.set;
                    vk_binding->counter_binding.binding =
                            binding->binding.binding + d3d_binding->d3d_binding.register_index - binding->register_index;
                }
                found_binding = true;
            }
            else if (!(binding->flags & VKD3D_SHADER_BINDING_FLAG_COUNTER))
            {
                if (binding->flags & VKD3D_SHADER_BINDING_FLAG_BINDLESS)
                {
                    vk_binding->buffer_binding.bindless.use_heap = DXIL_SPV_TRUE;
                    vk_binding->buffer_binding.bindless.heap_root_offset = binding->descriptor_offset +
                            d3d_binding->d3d_binding.register_index - binding->register_index;
                    vk_binding->buffer_binding.bindless.root_constant_word =
                            binding->descriptor_table +
                            (shader_interface_info->descriptor_tables.offset / sizeof(uint32_t));
                    vk_binding->buffer_binding.set = binding->binding.set;
                    vk_binding->buffer_binding.binding = binding->binding.binding;
                }
                else
                {
                    vk_binding->buffer_binding.set = binding->binding.set;
                    vk_binding->buffer_binding.binding =
                            binding->binding.binding + d3d_binding->d3d_binding.register_index - binding->register_index;
                }
                found_binding = true;
            }
        }
    }

    return found_binding ? DXIL_SPV_TRUE : DXIL_SPV_FALSE;
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
            dxil_resource_is_in_range(binding, d3d_binding) &&
            dxil_match_shader_visibility(binding->shader_visibility, d3d_binding->stage))
        {
            memset(vk_binding, 0, sizeof(*vk_binding));

            if (binding->flags & VKD3D_SHADER_BINDING_FLAG_BINDLESS)
            {
                vk_binding->vulkan.uniform_binding.bindless.use_heap = DXIL_SPV_TRUE;
                vk_binding->vulkan.uniform_binding.bindless.heap_root_offset =
                        binding->descriptor_offset + d3d_binding->register_index - binding->register_index;
                vk_binding->vulkan.uniform_binding.bindless.root_constant_word =
                        binding->descriptor_table +
                        (shader_interface_info->descriptor_tables.offset / sizeof(uint32_t));
                vk_binding->vulkan.uniform_binding.set = binding->binding.set;
                vk_binding->vulkan.uniform_binding.binding = binding->binding.binding;
            }
            else
            {
                vk_binding->vulkan.uniform_binding.set = binding->binding.set;
                vk_binding->vulkan.uniform_binding.binding =
                        binding->binding.binding + d3d_binding->register_index - binding->register_index;
            }

            return DXIL_SPV_TRUE;
        }
    }

    return DXIL_SPV_FALSE;
}

int vkd3d_shader_compile_dxil(const struct vkd3d_shader_code *dxbc,
        struct vkd3d_shader_code *spirv,
        const struct vkd3d_shader_interface_info *shader_interface_info,
        const struct vkd3d_shader_compile_arguments *compiler_args)
{
    const struct vkd3d_shader_transform_feedback_info *xfb_info;
    unsigned int root_constant_words = 0;
    dxil_spv_converter converter = NULL;
    enum vkd3d_shader_type shader_type;
    dxil_spv_parsed_blob blob = NULL;
    dxil_spv_compiled_spirv compiled;
    unsigned int i, max_size;
    int ret = VKD3D_OK;
    void *code;

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

    for (i = 0; i < shader_interface_info->binding_count; i++)
    {
        /* Bindless UAV counters are implemented as physical storage buffer pointers. */
        if ((shader_interface_info->bindings[i].flags & (VKD3D_SHADER_BINDING_FLAG_COUNTER | VKD3D_SHADER_BINDING_FLAG_BINDLESS)) ==
            (VKD3D_SHADER_BINDING_FLAG_COUNTER | VKD3D_SHADER_BINDING_FLAG_BINDLESS))
        {
            static const struct dxil_spv_option_physical_storage_buffer helper =
                    { { DXIL_SPV_OPTION_PHYSICAL_STORAGE_BUFFER },
                      DXIL_SPV_TRUE };
            if (dxil_spv_converter_add_option(converter, &helper.base) != DXIL_SPV_SUCCESS)
            {
                ERR("dxil-spirv does not support PHYSICAL_STORAGE_BUFFER.\n");
                ret = VKD3D_ERROR_NOT_IMPLEMENTED;
                goto end;
            }
            break;
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

