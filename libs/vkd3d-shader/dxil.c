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

static bool vkd3d_shader_binding_is_root_descriptor(const struct vkd3d_shader_resource_binding *binding)
{
    const uint32_t relevant_flags = VKD3D_SHADER_BINDING_FLAG_RAW_VA |
                                    VKD3D_SHADER_BINDING_FLAG_COUNTER;
    const uint32_t expected_flags = VKD3D_SHADER_BINDING_FLAG_RAW_VA;
    return (binding->flags & relevant_flags) == expected_flags;
}

struct vkd3d_dxil_remap_userdata
{
    const struct vkd3d_shader_interface_info *shader_interface_info;
    unsigned int num_root_descriptors;
};

static dxil_spv_bool dxil_remap(const struct vkd3d_dxil_remap_userdata *remap,
        enum vkd3d_shader_descriptor_type descriptor_type,
        const dxil_spv_d3d_binding *d3d_binding,
        dxil_spv_vulkan_binding *vk_binding,
        uint32_t resource_flags)
{
    const struct vkd3d_shader_interface_info *shader_interface_info = remap->shader_interface_info;
    unsigned int binding_count = shader_interface_info->binding_count;
    unsigned int root_descriptor_index = 0;
    unsigned int i;

    for (i = 0; i < binding_count; i++)
    {
        const struct vkd3d_shader_resource_binding *binding = &shader_interface_info->bindings[i];
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
                vk_binding->bindless.heap_root_offset = binding->descriptor_offset +
                        d3d_binding->register_index - binding->register_index;
                vk_binding->root_constant_index = binding->descriptor_table +
                        (shader_interface_info->descriptor_tables.offset / sizeof(uint32_t));
                vk_binding->set = binding->binding.set;
                vk_binding->binding = binding->binding.binding;

                if (vk_binding->root_constant_index < 2 * remap->num_root_descriptors)
                {
                    ERR("Bindless push constant table offset is impossible. %u < 2 * %u\n",
                        vk_binding->root_constant_index, remap->num_root_descriptors);
                    return DXIL_SPV_FALSE;
                }
                vk_binding->root_constant_index -= 2 * remap->num_root_descriptors;
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
                &vk_binding->counter_binding, VKD3D_SHADER_BINDING_FLAG_COUNTER))
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

int vkd3d_shader_compile_dxil(const struct vkd3d_shader_code *dxbc,
        struct vkd3d_shader_code *spirv,
        const struct vkd3d_shader_interface_info *shader_interface_info,
        const struct vkd3d_shader_compile_arguments *compiler_args)
{
    const struct vkd3d_shader_transform_feedback_info *xfb_info;
    struct vkd3d_dxil_remap_userdata remap_userdata;
    unsigned int non_raw_va_binding_count = 0;
    unsigned int raw_va_binding_count = 0;
    unsigned int num_root_descriptors = 0;
    unsigned int root_constant_words = 0;
    dxil_spv_converter converter = NULL;
    dxil_spv_parsed_blob blob = NULL;
    dxil_spv_compiled_spirv compiled;
    unsigned int i, max_size;
    vkd3d_shader_hash_t hash;
    int ret = VKD3D_OK;
    void *code;

    dxil_spv_set_thread_log_callback(vkd3d_dxil_log_callback, NULL);

    hash = vkd3d_shader_hash(dxbc);
    spirv->meta.replaced = false;
    spirv->meta.hash = hash;
    if (vkd3d_shader_replace(hash, &spirv->code, &spirv->size))
    {
        spirv->meta.replaced = true;
        return ret;
    }

    dxil_spv_begin_thread_allocator_context();

    vkd3d_shader_dump_shader(hash, dxbc, "dxil");

    if (dxil_spv_parse_dxil_blob(dxbc->code, dxbc->size, &blob) != DXIL_SPV_SUCCESS)
    {
        ret = VKD3D_ERROR_INVALID_SHADER;
        goto end;
    }

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

    for (i = 0; i < shader_interface_info->binding_count; i++)
    {
        /* Bindless UAV counters are implemented as physical storage buffer pointers.
         * For simplicity, dxil-spirv only accepts either fully RAW VA, or all non-raw VA. */
        if ((shader_interface_info->bindings[i].flags &
             (VKD3D_SHADER_BINDING_FLAG_COUNTER | VKD3D_SHADER_BINDING_FLAG_BINDLESS)) ==
            (VKD3D_SHADER_BINDING_FLAG_COUNTER | VKD3D_SHADER_BINDING_FLAG_BINDLESS))
        {
            if (shader_interface_info->bindings[i].flags & VKD3D_SHADER_BINDING_FLAG_RAW_VA)
                raw_va_binding_count++;
            else
                non_raw_va_binding_count++;
        }

        if (vkd3d_shader_binding_is_root_descriptor(&shader_interface_info->bindings[i]))
            num_root_descriptors++;
    }

    if (raw_va_binding_count && non_raw_va_binding_count)
    {
        ERR("dxil-spirv currently cannot mix and match bindless UAV counters with RAW VA and texel buffer.\n");
        ret = VKD3D_ERROR_NOT_IMPLEMENTED;
        goto end;
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

    {
        char buffer[16 + 5 + 1];
        const struct dxil_spv_option_shader_source_file helper =
                { { DXIL_SPV_OPTION_SHADER_SOURCE_FILE }, buffer };

        sprintf(buffer, "%016"PRIx64".dxil", spirv->meta.hash);
        if (dxil_spv_converter_add_option(converter, &helper.base) != DXIL_SPV_SUCCESS)
            WARN("dxil-spirv does not support SHADER_SOURCE_FILE.\n");
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

    remap_userdata.shader_interface_info = shader_interface_info;
    remap_userdata.num_root_descriptors = num_root_descriptors;

    dxil_spv_converter_set_root_constant_word_count(converter, root_constant_words);
    dxil_spv_converter_set_root_descriptor_count(converter, num_root_descriptors);
    dxil_spv_converter_set_srv_remapper(converter, dxil_srv_remap, &remap_userdata);
    dxil_spv_converter_set_sampler_remapper(converter, dxil_sampler_remap, &remap_userdata);
    dxil_spv_converter_set_uav_remapper(converter, dxil_uav_remap, &remap_userdata);
    dxil_spv_converter_set_cbv_remapper(converter, dxil_cbv_remap, &remap_userdata);

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

    vkd3d_shader_dump_spirv_shader(hash, spirv);

end:
    dxil_spv_converter_free(converter);
    dxil_spv_parsed_blob_free(blob);
    dxil_spv_end_thread_allocator_context();
    return ret;
}

