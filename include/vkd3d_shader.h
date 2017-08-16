/*
 * Copyright 2017 Józef Kucia for CodeWeavers
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

#ifndef __VKD3D_SHADER_H
#define __VKD3D_SHADER_H

#include "vkd3d.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

enum vkd3d_shader_compiler_option
{
    VKD3D_SHADER_STRIP_DEBUG = 0x00000001,

    VKD3D_SHADER_COMPILER_OPTIONS_FORCE_32_BIT = 0x7fffffff,
};

enum vkd3d_shader_visibility
{
    VKD3D_SHADER_VISIBILITY_ALL,
    VKD3D_SHADER_VISIBILITY_VERTEX,
    VKD3D_SHADER_VISIBILITY_HULL,
    VKD3D_SHADER_VISIBILITY_DOMAIN,
    VKD3D_SHADER_VISIBILITY_GEOMETRY,
    VKD3D_SHADER_VISIBILITY_PIXEL,
};

struct vkd3d_shader_code
{
    const void *code;
    size_t size;
};

enum vkd3d_descriptor_type
{
    VKD3D_DESCRIPTOR_TYPE_UNKNOWN,
    VKD3D_DESCRIPTOR_TYPE_CBV,     /* cb# */
    VKD3D_DESCRIPTOR_TYPE_SRV,     /* t#  */
    VKD3D_DESCRIPTOR_TYPE_UAV,     /* u#  */
    VKD3D_DESCRIPTOR_TYPE_SAMPLER, /* s#  */
};

struct vkd3d_shader_descriptor_binding
{
    uint32_t set;
    uint32_t binding;
};

struct vkd3d_shader_resource_binding
{
    enum vkd3d_descriptor_type type;
    unsigned int register_index;
    bool is_buffer;

    struct vkd3d_shader_descriptor_binding binding;
};

struct vkd3d_shader_push_constant
{
    unsigned int register_index;
    enum vkd3d_shader_visibility shader_visibility;

    unsigned int offset; /* in bytes */
    unsigned int size;   /* in bytes */
};

struct vkd3d_shader_interface
{
    const struct vkd3d_shader_resource_binding *bindings;
    unsigned int binding_count;

    const struct vkd3d_shader_push_constant *push_constants;
    unsigned int push_constant_count;
};

HRESULT vkd3d_shader_compile_dxbc(const struct vkd3d_shader_code *dxbc,
        struct vkd3d_shader_code *spirv, uint32_t compiler_options,
        const struct vkd3d_shader_interface *shader_interface);
void vkd3d_shader_free_shader_code(struct vkd3d_shader_code *code);

HRESULT vkd3d_shader_parse_root_signature(const struct vkd3d_shader_code *dxbc,
        D3D12_ROOT_SIGNATURE_DESC *root_signature);
void vkd3d_shader_free_root_signature(D3D12_ROOT_SIGNATURE_DESC *root_signature);

#ifdef __cplusplus
}
#endif  /* __cplusplus */

#endif  /* __VKD3D_SHADER_H */
