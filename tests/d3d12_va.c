/*
 * Copyright 2016-2017 Józef Kucia for CodeWeavers
 * Copyright 2020-2021 Philip Rebohle for Valve Corporation
 * Copyright 2020-2021 Joshua Ashton for Valve Corporation
 * Copyright 2020-2021 Hans-Kristian Arntzen for Valve Corporation
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

void test_gpu_virtual_address(void)
{
    D3D12_GPU_VIRTUAL_ADDRESS vb_offset, ib_offset;
    ID3D12GraphicsCommandList *command_list;
    D3D12_INPUT_LAYOUT_DESC input_layout;
    struct test_context_desc desc;
    D3D12_VERTEX_BUFFER_VIEW vbv;
    struct test_context context;
    D3D12_INDEX_BUFFER_VIEW ibv;
    ID3D12CommandQueue *queue;
    ID3D12Resource *buffer;
    HRESULT hr;
    BYTE *ptr;

    static const DWORD vs_code[] =
    {
#if 0
        void main(float4 in_position : POSITION, float4 in_color : COLOR,
                out float4 out_position : SV_POSITION, out float4 out_color : COLOR)
        {
            out_position = in_position;
            out_color = in_color;
        }
#endif
        0x43425844, 0xa58fc911, 0x280038e9, 0x14cfff54, 0xe43fc328, 0x00000001, 0x00000144, 0x00000003,
        0x0000002c, 0x0000007c, 0x000000d0, 0x4e475349, 0x00000048, 0x00000002, 0x00000008, 0x00000038,
        0x00000000, 0x00000000, 0x00000003, 0x00000000, 0x00000f0f, 0x00000041, 0x00000000, 0x00000000,
        0x00000003, 0x00000001, 0x00000f0f, 0x49534f50, 0x4e4f4954, 0x4c4f4300, 0xab00524f, 0x4e47534f,
        0x0000004c, 0x00000002, 0x00000008, 0x00000038, 0x00000000, 0x00000001, 0x00000003, 0x00000000,
        0x0000000f, 0x00000044, 0x00000000, 0x00000000, 0x00000003, 0x00000001, 0x0000000f, 0x505f5653,
        0x5449534f, 0x004e4f49, 0x4f4c4f43, 0xabab0052, 0x58454853, 0x0000006c, 0x00010050, 0x0000001b,
        0x0100086a, 0x0300005f, 0x001010f2, 0x00000000, 0x0300005f, 0x001010f2, 0x00000001, 0x04000067,
        0x001020f2, 0x00000000, 0x00000001, 0x03000065, 0x001020f2, 0x00000001, 0x05000036, 0x001020f2,
        0x00000000, 0x00101e46, 0x00000000, 0x05000036, 0x001020f2, 0x00000001, 0x00101e46, 0x00000001,
        0x0100003e,
    };
    static const D3D12_SHADER_BYTECODE vs = {vs_code, sizeof(vs_code)};
    static const DWORD ps_code[] =
    {
#if 0
        void main(float4 in_position : SV_POSITION, float4 in_color : COLOR,
                out float4 out_color : SV_TARGET)
        {
            out_color = in_color;
        }
#endif
        0x43425844, 0x1a6def50, 0x9c069300, 0x7cce68f0, 0x621239b9, 0x00000001, 0x000000f8, 0x00000003,
        0x0000002c, 0x00000080, 0x000000b4, 0x4e475349, 0x0000004c, 0x00000002, 0x00000008, 0x00000038,
        0x00000000, 0x00000001, 0x00000003, 0x00000000, 0x0000000f, 0x00000044, 0x00000000, 0x00000000,
        0x00000003, 0x00000001, 0x00000f0f, 0x505f5653, 0x5449534f, 0x004e4f49, 0x4f4c4f43, 0xabab0052,
        0x4e47534f, 0x0000002c, 0x00000001, 0x00000008, 0x00000020, 0x00000000, 0x00000000, 0x00000003,
        0x00000000, 0x0000000f, 0x545f5653, 0x45475241, 0xabab0054, 0x58454853, 0x0000003c, 0x00000050,
        0x0000000f, 0x0100086a, 0x03001062, 0x001010f2, 0x00000001, 0x03000065, 0x001020f2, 0x00000000,
        0x05000036, 0x001020f2, 0x00000000, 0x00101e46, 0x00000001, 0x0100003e,
    };
    static const D3D12_SHADER_BYTECODE ps = {ps_code, sizeof(ps_code)};
    static const float white[] = {1.0f, 1.0f, 1.0f, 1.0f};
    static const uint32_t indices[] = {0, 1, 2, 3};
    static const D3D12_INPUT_ELEMENT_DESC layout_desc[] =
    {
        {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT,       0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 8, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    };
    static const struct
    {
        struct vec2 position;
        struct vec4 color;
    }
    quad[] =
    {
        {{-1.0f, -1.0f}, {0.0f, 1.0f, 0.0f, 1.0f}},
        {{-1.0f,  1.0f}, {0.0f, 1.0f, 0.0f, 1.0f}},
        {{ 1.0f, -1.0f}, {0.0f, 1.0f, 0.0f, 1.0f}},
        {{ 1.0f,  1.0f}, {0.0f, 1.0f, 0.0f, 1.0f}},
    };

    memset(&desc, 0, sizeof(desc));
    desc.no_root_signature = true;
    if (!init_test_context(&context, &desc))
        return;
    command_list = context.list;
    queue = context.queue;

    context.root_signature = create_empty_root_signature(context.device,
            D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);
    input_layout.pInputElementDescs = layout_desc;
    input_layout.NumElements = ARRAY_SIZE(layout_desc);
    context.pipeline_state = create_pipeline_state(context.device,
            context.root_signature, context.render_target_desc.Format, &vs, &ps, &input_layout);

    vb_offset = 0x200;
    ib_offset = 0x500;
    buffer = create_upload_buffer(context.device, ib_offset + sizeof(indices), NULL);

    hr = ID3D12Resource_Map(buffer, 0, NULL, (void **)&ptr);
    ok(SUCCEEDED(hr), "Failed to map upload buffer, hr %#x.\n", hr);
    memcpy(ptr + vb_offset, quad, sizeof(quad));
    memcpy(ptr + ib_offset, indices, sizeof(indices));
    ID3D12Resource_Unmap(buffer, 0, NULL);

    vbv.BufferLocation = ID3D12Resource_GetGPUVirtualAddress(buffer) + vb_offset;
    vbv.StrideInBytes = sizeof(*quad);
    vbv.SizeInBytes = sizeof(quad);
    ibv.BufferLocation = ID3D12Resource_GetGPUVirtualAddress(buffer) + ib_offset;
    ibv.SizeInBytes = sizeof(indices);
    ibv.Format = DXGI_FORMAT_R32_UINT;

    ID3D12GraphicsCommandList_ClearRenderTargetView(command_list, context.rtv, white, 0, NULL);

    ID3D12GraphicsCommandList_OMSetRenderTargets(command_list, 1, &context.rtv, false, NULL);
    ID3D12GraphicsCommandList_SetGraphicsRootSignature(command_list, context.root_signature);
    ID3D12GraphicsCommandList_SetPipelineState(command_list, context.pipeline_state);
    ID3D12GraphicsCommandList_IASetPrimitiveTopology(command_list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    ID3D12GraphicsCommandList_IASetVertexBuffers(command_list, 0, 1, &vbv);
    ID3D12GraphicsCommandList_IASetIndexBuffer(command_list, &ibv);
    ID3D12GraphicsCommandList_RSSetViewports(command_list, 1, &context.viewport);
    ID3D12GraphicsCommandList_RSSetScissorRects(command_list, 1, &context.scissor_rect);
    ID3D12GraphicsCommandList_DrawIndexedInstanced(command_list, 4, 1, 0, 0, 0);

    transition_resource_state(command_list, context.render_target,
            D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
    check_sub_resource_uint(context.render_target, 0, queue, command_list, 0xff00ff00, 0);

    ID3D12Resource_Release(buffer);
    destroy_test_context(&context);
}

