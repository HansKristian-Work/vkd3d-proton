/*
 * Copyright 2024 Hans-Kristian Arntzen for Valve Corporation
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
#include "d3d12_crosstest.h"

void test_gs_multiview_transform(void)
{
    const float yellow[4] = { 1.0f, 1.0f, 0.0f, 1.0f };
    const float white[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc;
    struct test_context_desc context_desc;
    D3D12_ROOT_SIGNATURE_DESC rs_desc;
    D3D12_ROOT_PARAMETER rs_param[2];
    struct test_context context;
    struct resource_readback rb;
    unsigned int i, x, y;
    ID3D12Resource *cbv;
    D3D12_VIEWPORT vp;
    D3D12_RECT sci;
    HRESULT hr;

    struct matrix
    {
        float values[4][4];
    };

    struct uniform_data
    {
        float dummy[4];
        struct matrix matrices[6];
    } cbv_data;

#include "shaders/multiview/headers/multiview_vs.h"
#include "shaders/multiview/headers/multiview_gs.h"
#include "shaders/multiview/headers/multiview_ps.h"

    memset(&context_desc, 0, sizeof(context_desc));
    context_desc.no_root_signature = true;
    context_desc.no_pipeline = true;
    context_desc.rt_width = 4;
    context_desc.rt_height = 2;
    context_desc.rt_array_size = 6;
    context_desc.rt_format = DXGI_FORMAT_R8G8B8A8_UNORM;

    if (!init_test_context(&context, &context_desc))
        return;

    memset(&rs_desc, 0, sizeof(rs_desc));
    memset(rs_param, 0, sizeof(rs_param));
    rs_desc.NumParameters = ARRAY_SIZE(rs_param);
    rs_desc.pParameters = rs_param;

    /* Alias the CBV binding to test that we're picking the correct shader stage. */
    rs_param[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
    rs_param[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    rs_param[0].Constants.Num32BitValues = 4;
    rs_param[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_GEOMETRY;
    rs_param[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    create_root_signature(context.device, &rs_desc, &context.root_signature);

    init_pipeline_state_desc_dxil(&pso_desc, context.root_signature, DXGI_FORMAT_R8G8B8A8_UNORM,
            &multiview_vs_dxil, &multiview_ps_dxil, NULL);
    pso_desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    pso_desc.GS = multiview_gs_dxil;

    hr = ID3D12Device_CreateGraphicsPipelineState(context.device, &pso_desc, &IID_ID3D12PipelineState, (void **)&context.pipeline_state);
    ok(SUCCEEDED(hr), "Failed to create pipeline, hr #%x.\n", hr);

    /* Make it so that each vertex quad lands on one of the pixels in raster order. Row-major transform. */
    memset(&cbv_data, 0, sizeof(cbv_data));
    for (i = 0; i < 6; i++)
    {
        cbv_data.matrices[i].values[0][0] = 1.0f / 4.0f;
        cbv_data.matrices[i].values[1][1] = -1.0f / 2.0f;
        cbv_data.matrices[i].values[2][2] = 1.0f;
        cbv_data.matrices[i].values[3][3] = 1.0f;
        cbv_data.matrices[i].values[0][3] = (((i % 4) + 0.5f) / 4.0f) * 2.0f - 1.0f;
        cbv_data.matrices[i].values[1][3] = (((i / 4) + 0.5f) / 2.0f) * -2.0f + 1.0f;
    }

    cbv = create_upload_buffer(context.device, sizeof(cbv_data), &cbv_data);

    ID3D12GraphicsCommandList_ClearRenderTargetView(context.list, context.rtv, white, 0, NULL);
    ID3D12GraphicsCommandList_OMSetRenderTargets(context.list, 1, &context.rtv, TRUE, NULL);
    ID3D12GraphicsCommandList_SetGraphicsRootSignature(context.list, context.root_signature);
    ID3D12GraphicsCommandList_SetPipelineState(context.list, context.pipeline_state);
    ID3D12GraphicsCommandList_SetGraphicsRoot32BitConstants(context.list, 0, ARRAY_SIZE(yellow), yellow, 0);
    ID3D12GraphicsCommandList_SetGraphicsRootConstantBufferView(context.list, 1, ID3D12Resource_GetGPUVirtualAddress(cbv));
    set_viewport(&vp, 0, 0, 4, 2, 0, 1);
    set_rect(&sci, 0, 0, 4, 2);
    ID3D12GraphicsCommandList_RSSetViewports(context.list, 1, &vp);
    ID3D12GraphicsCommandList_RSSetScissorRects(context.list, 1, &sci);
    ID3D12GraphicsCommandList_IASetPrimitiveTopology(context.list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    ID3D12GraphicsCommandList_DrawInstanced(context.list, 4, 1, 0, 0);

    transition_resource_state(context.list, context.render_target, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
    for (i = 0; i < 6; i++)
    {
        get_texture_readback_with_command_list(context.render_target, i, &rb, context.queue, context.list);
        reset_command_list(context.list, context.allocator);

        for (y = 0; y < 2; y++)
        {
            for (x = 0; x < 4; x++)
            {
                uint32_t value, expected;
                value = get_readback_uint(&rb, x, y, 0);
                expected = y * 4 + x == i ? 0xff00ffffu : 0xffffffffu;
                ok(value == expected, "Layer %u, coord %u, %u: Expected #%x, got #%x.\n", i, x, y, expected, value);
            }
        }

        release_resource_readback(&rb);
    }

    ID3D12Resource_Release(cbv);
    destroy_test_context(&context);
}