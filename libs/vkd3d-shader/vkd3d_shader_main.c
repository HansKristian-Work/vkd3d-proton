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

#include "vkd3d_shader_private.h"

HRESULT vkd3d_shader_compile_dxbc(const struct vkd3d_shader_code *dxbc,
        struct vkd3d_shader_code *spirv, uint32_t compiler_options)
{
    struct vkd3d_dxbc_compiler *spirv_compiler;
    struct vkd3d_shader_version shader_version;
    struct vkd3d_shader_desc shader_desc;
    struct vkd3d_shader_instruction ins;
    void *parser_data;
    const DWORD *ptr;
    HRESULT hr;
    bool ret;

    TRACE("dxbc {%p, %zu}, spirv %p, compiler_options %#x.\n",
            dxbc->code, dxbc->size, spirv, compiler_options);

    if (FAILED(hr = shader_extract_from_dxbc(dxbc->code, dxbc->size, &shader_desc)))
    {
        WARN("Failed to extract shader, hr %#x.\n", hr);
        return hr;
    }

    if (!(parser_data = shader_sm4_init(shader_desc.byte_code, shader_desc.byte_code_size,
            &shader_desc.output_signature)))
    {
        WARN("Failed to initialize shader parser, hr %#x.\n", hr);
        free_shader_desc(&shader_desc);
        return hr;
    }

    shader_sm4_read_header(parser_data, &ptr, &shader_version);

    if (!(spirv_compiler = vkd3d_dxbc_compiler_create(&shader_version,
            &shader_desc.output_signature, compiler_options)))
    {
        ERR("Failed to create DXBC compiler.\n");
        shader_sm4_free(parser_data);
        free_shader_desc(&shader_desc);
        return hr;
    }

    while (!shader_sm4_is_end(parser_data, &ptr))
    {
        shader_sm4_read_instruction(parser_data, &ptr, &ins);

        if (ins.handler_idx == VKD3DSIH_TABLE_SIZE)
        {
            WARN("Encountered unrecognized or invalid instruction.\n");
            shader_sm4_free(parser_data);
            free_shader_desc(&shader_desc);
            vkd3d_dxbc_compiler_destroy(spirv_compiler);
            return E_FAIL;
        }

        vkd3d_dxbc_compiler_handle_instruction(spirv_compiler, &ins);
    }

    ret = vkd3d_dxbc_compiler_generate_spirv(spirv_compiler, spirv);
    vkd3d_dxbc_compiler_destroy(spirv_compiler);
    shader_sm4_free(parser_data);
    free_shader_desc(&shader_desc);

    return ret ? S_OK : E_FAIL;
}

void vkd3d_shader_free_shader_code(struct vkd3d_shader_code *shader_code)
{
    if (!shader_code)
        return;

    vkd3d_free((void *)shader_code->code);
}

void vkd3d_shader_free_root_signature(D3D12_ROOT_SIGNATURE_DESC *root_signature)
{
    unsigned int i;

    for (i = 0; i < root_signature->NumParameters; ++i)
    {
        const D3D12_ROOT_PARAMETER *parameter = &root_signature->pParameters[i];

        if (parameter->ParameterType == D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE)
            vkd3d_free((void *)parameter->u.DescriptorTable.pDescriptorRanges);
    }
    vkd3d_free((void *)root_signature->pParameters);
    vkd3d_free((void *)root_signature->pStaticSamplers);

    memset(root_signature, 0, sizeof(*root_signature));
}
