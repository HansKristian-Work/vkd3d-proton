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

void test_set_render_targets(void)
{
    ID3D12DescriptorHeap *dsv_heap, *rtv_heap;
    ID3D12GraphicsCommandList *command_list;
    D3D12_CPU_DESCRIPTOR_HANDLE dsv, rtv;
    struct test_context context;
    ID3D12Device *device;
    HRESULT hr;

    if (!init_test_context(&context, NULL))
        return;
    device = context.device;
    command_list = context.list;

    rtv_heap = create_cpu_descriptor_heap(device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 4);
    dsv_heap = create_cpu_descriptor_heap(device, D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 4);

    dsv = ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(dsv_heap);
    rtv = ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(rtv_heap);

    ID3D12GraphicsCommandList_OMSetRenderTargets(command_list, 1, &rtv, false, NULL);
    ID3D12GraphicsCommandList_OMSetRenderTargets(command_list, 1, &rtv, true, NULL);
    ID3D12GraphicsCommandList_OMSetRenderTargets(command_list, 1, &rtv, true, &dsv);
    ID3D12GraphicsCommandList_OMSetRenderTargets(command_list, 0, &rtv, true, &dsv);
    ID3D12GraphicsCommandList_OMSetRenderTargets(command_list, 0, &rtv, false, &dsv);
    ID3D12GraphicsCommandList_OMSetRenderTargets(command_list, 0, NULL, true, &dsv);
    ID3D12GraphicsCommandList_OMSetRenderTargets(command_list, 0, NULL, false, &dsv);

    hr = ID3D12GraphicsCommandList_Close(command_list);
    ok(hr == S_OK, "Failed to close command list, hr %#x.\n", hr);

    ID3D12DescriptorHeap_Release(rtv_heap);
    ID3D12DescriptorHeap_Release(dsv_heap);
    destroy_test_context(&context);
}

void test_draw_instanced(void)
{
    static const float white[] = {1.0f, 1.0f, 1.0f, 1.0f};
    ID3D12GraphicsCommandList *command_list;
    struct test_context context;
    ID3D12CommandQueue *queue;

    if (!init_test_context(&context, NULL))
        return;
    command_list = context.list;
    queue = context.queue;

    ID3D12GraphicsCommandList_ClearRenderTargetView(command_list, context.rtv, white, 0, NULL);

    if (!use_warp_device)
    {
        /* This draw call is ignored. */
        ID3D12GraphicsCommandList_DrawInstanced(command_list, 3, 1, 0, 0);
    }

    ID3D12GraphicsCommandList_OMSetRenderTargets(command_list, 1, &context.rtv, false, NULL);
    ID3D12GraphicsCommandList_SetGraphicsRootSignature(command_list, context.root_signature);
    ID3D12GraphicsCommandList_SetPipelineState(command_list, context.pipeline_state);
    ID3D12GraphicsCommandList_IASetPrimitiveTopology(command_list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D12GraphicsCommandList_RSSetViewports(command_list, 1, &context.viewport);
    ID3D12GraphicsCommandList_RSSetScissorRects(command_list, 1, &context.scissor_rect);
    ID3D12GraphicsCommandList_DrawInstanced(command_list, 3, 1, 0, 0);

    transition_resource_state(command_list, context.render_target,
            D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);

    check_sub_resource_uint(context.render_target, 0, queue, command_list, 0xff00ff00, 0);

    destroy_test_context(&context);
}

void test_draw_indexed_instanced(void)
{
    static const float white[] = {1.0f, 1.0f, 1.0f, 1.0f};
    static const uint16_t indices[] = {0, 1, 2};
    ID3D12GraphicsCommandList *command_list;
    struct test_context context;
    D3D12_INDEX_BUFFER_VIEW ibv;
    ID3D12CommandQueue *queue;
    ID3D12Resource *ib;

    if (!init_test_context(&context, NULL))
        return;
    command_list = context.list;
    queue = context.queue;

    ib = create_upload_buffer(context.device, sizeof(indices), indices);

    ibv.BufferLocation = ID3D12Resource_GetGPUVirtualAddress(ib);
    ibv.SizeInBytes = sizeof(indices);
    ibv.Format = DXGI_FORMAT_R16_UINT;

    ID3D12GraphicsCommandList_ClearRenderTargetView(command_list, context.rtv, white, 0, NULL);

    if (!use_warp_device)
    {
        /* This draw call is ignored. */
        ID3D12GraphicsCommandList_DrawIndexedInstanced(command_list, 3, 1, 0, 0, 0);
    }

    ID3D12GraphicsCommandList_OMSetRenderTargets(command_list, 1, &context.rtv, false, NULL);
    ID3D12GraphicsCommandList_SetGraphicsRootSignature(command_list, context.root_signature);
    ID3D12GraphicsCommandList_SetPipelineState(command_list, context.pipeline_state);
    ID3D12GraphicsCommandList_IASetPrimitiveTopology(command_list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D12GraphicsCommandList_IASetIndexBuffer(command_list, NULL);
    ID3D12GraphicsCommandList_IASetIndexBuffer(command_list, &ibv);
    ID3D12GraphicsCommandList_RSSetViewports(command_list, 1, &context.viewport);
    ID3D12GraphicsCommandList_RSSetScissorRects(command_list, 1, &context.scissor_rect);
    ID3D12GraphicsCommandList_DrawIndexedInstanced(command_list, 3, 1, 0, 0, 0);

    transition_resource_state(command_list, context.render_target,
            D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);

    check_sub_resource_uint(context.render_target, 0, queue, command_list, 0xff00ff00, 0);

    ID3D12Resource_Release(ib);
    destroy_test_context(&context);
}

void test_draw_no_descriptor_bindings(void)
{
    static const float white[] = {1.0f, 1.0f, 1.0f, 1.0f};
    D3D12_ROOT_SIGNATURE_DESC root_signature_desc;
    D3D12_DESCRIPTOR_RANGE descriptor_range[2];
    ID3D12GraphicsCommandList *command_list;
    D3D12_ROOT_PARAMETER root_parameters[2];
    struct test_context_desc desc;
    struct test_context context;
    ID3D12CommandQueue *queue;
    HRESULT hr;

    memset(&desc, 0, sizeof(desc));
    desc.no_root_signature = true;
    if (!init_test_context(&context, &desc))
        return;
    command_list = context.list;
    queue = context.queue;

    descriptor_range[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    descriptor_range[0].NumDescriptors = 2;
    descriptor_range[0].BaseShaderRegister = 0;
    descriptor_range[0].RegisterSpace = 0;
    descriptor_range[0].OffsetInDescriptorsFromTableStart = 1;
    root_parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    root_parameters[0].DescriptorTable.NumDescriptorRanges = 1;
    root_parameters[0].DescriptorTable.pDescriptorRanges = &descriptor_range[0];
    root_parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    descriptor_range[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    descriptor_range[1].NumDescriptors = 1;
    descriptor_range[1].BaseShaderRegister = 0;
    descriptor_range[1].RegisterSpace = 0;
    descriptor_range[1].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
    root_parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    root_parameters[1].DescriptorTable.NumDescriptorRanges = 1;
    root_parameters[1].DescriptorTable.pDescriptorRanges = &descriptor_range[1];
    root_parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    root_signature_desc.NumParameters = ARRAY_SIZE(root_parameters);
    root_signature_desc.pParameters = root_parameters;
    root_signature_desc.NumStaticSamplers = 0;
    root_signature_desc.pStaticSamplers = NULL;
    root_signature_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
    hr = create_root_signature(context.device, &root_signature_desc, &context.root_signature);
    ok(hr == S_OK, "Failed to create root signature, hr %#x.\n", hr);

    context.pipeline_state = create_pipeline_state(context.device,
            context.root_signature, context.render_target_desc.Format, NULL, NULL, NULL);

    ID3D12GraphicsCommandList_ClearRenderTargetView(command_list, context.rtv, white, 0, NULL);

    ID3D12GraphicsCommandList_OMSetRenderTargets(command_list, 1, &context.rtv, false, NULL);
    ID3D12GraphicsCommandList_SetGraphicsRootSignature(command_list, context.root_signature);
    ID3D12GraphicsCommandList_SetPipelineState(command_list, context.pipeline_state);
    ID3D12GraphicsCommandList_IASetPrimitiveTopology(command_list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D12GraphicsCommandList_RSSetViewports(command_list, 1, &context.viewport);
    ID3D12GraphicsCommandList_RSSetScissorRects(command_list, 1, &context.scissor_rect);
    ID3D12GraphicsCommandList_DrawInstanced(command_list, 3, 1, 0, 0);

    transition_resource_state(command_list, context.render_target,
            D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);

    check_sub_resource_uint(context.render_target, 0, queue, command_list, 0xff00ff00, 0);

    destroy_test_context(&context);
}

void test_multiple_render_targets(void)
{
    static const float white[] = {1.0f, 1.0f, 1.0f, 1.0f};
    struct vec4 expected_vec4 = {0.0f, 0.0f, 0.0f, 1.0f};
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc;
    ID3D12GraphicsCommandList *command_list;
    D3D12_CPU_DESCRIPTOR_HANDLE rtvs[3];
    ID3D12Resource *render_targets[2];
    struct test_context_desc desc;
    struct test_context context;
    ID3D12CommandQueue *queue;
    unsigned int i;
    HRESULT hr;

    static const DWORD ps_code[] =
    {
#if 0
        void main(out float4 target0 : SV_Target0, out float4 target1 : SV_Target1,
                out float4 target2 : SV_Target2)
        {
            target0 = float4(1.0f, 0.0f, 0.0f, 1.0f);
            target1 = float4(2.0f, 0.0f, 0.0f, 1.0f);
            target2 = float4(3.0f, 0.0f, 0.0f, 1.0f);
        }
#endif
        0x43425844, 0xc4325131, 0x8ba4a693, 0x08d15431, 0xcb990885, 0x00000001, 0x0000013c, 0x00000003,
        0x0000002c, 0x0000003c, 0x000000a0, 0x4e475349, 0x00000008, 0x00000000, 0x00000008, 0x4e47534f,
        0x0000005c, 0x00000003, 0x00000008, 0x00000050, 0x00000000, 0x00000000, 0x00000003, 0x00000000,
        0x0000000f, 0x00000050, 0x00000001, 0x00000000, 0x00000003, 0x00000001, 0x0000000f, 0x00000050,
        0x00000002, 0x00000000, 0x00000003, 0x00000002, 0x0000000f, 0x545f5653, 0x65677261, 0xabab0074,
        0x58454853, 0x00000094, 0x00000050, 0x00000025, 0x0100086a, 0x03000065, 0x001020f2, 0x00000000,
        0x03000065, 0x001020f2, 0x00000001, 0x03000065, 0x001020f2, 0x00000002, 0x08000036, 0x001020f2,
        0x00000000, 0x00004002, 0x3f800000, 0x00000000, 0x00000000, 0x3f800000, 0x08000036, 0x001020f2,
        0x00000001, 0x00004002, 0x40000000, 0x00000000, 0x00000000, 0x3f800000, 0x08000036, 0x001020f2,
        0x00000002, 0x00004002, 0x40400000, 0x00000000, 0x00000000, 0x3f800000, 0x0100003e,
    };
    static const D3D12_SHADER_BYTECODE ps = {ps_code, sizeof(ps_code)};

    memset(&desc, 0, sizeof(desc));
    desc.rt_format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    desc.rt_descriptor_count = ARRAY_SIZE(rtvs);
    desc.no_pipeline = true;
    if (!init_test_context(&context, &desc))
        return;
    command_list = context.list;
    queue = context.queue;

    init_pipeline_state_desc(&pso_desc, context.root_signature, 0, NULL, &ps, NULL);
    pso_desc.NumRenderTargets = ARRAY_SIZE(rtvs);
    for (i = 0; i < ARRAY_SIZE(rtvs); ++i)
        pso_desc.RTVFormats[i] = desc.rt_format;
    hr = ID3D12Device_CreateGraphicsPipelineState(context.device, &pso_desc,
            &IID_ID3D12PipelineState, (void **)&context.pipeline_state);
    ok(hr == S_OK, "Failed to create state, hr %#x.\n", hr);

    rtvs[0] = get_cpu_rtv_handle(&context, context.rtv_heap, 2);
    rtvs[1] = get_cpu_rtv_handle(&context, context.rtv_heap, 0);
    rtvs[2] = get_cpu_rtv_handle(&context, context.rtv_heap, 1);

    create_render_target(&context, &desc, &render_targets[0], &rtvs[0]);
    create_render_target(&context, &desc, &render_targets[1], &rtvs[2]);

    for (i = 0; i < ARRAY_SIZE(rtvs); ++i)
        ID3D12GraphicsCommandList_ClearRenderTargetView(command_list, rtvs[i], white, 0, NULL);

    ID3D12GraphicsCommandList_OMSetRenderTargets(command_list, ARRAY_SIZE(rtvs), rtvs, false, NULL);
    ID3D12GraphicsCommandList_SetGraphicsRootSignature(command_list, context.root_signature);
    ID3D12GraphicsCommandList_SetPipelineState(command_list, context.pipeline_state);
    ID3D12GraphicsCommandList_IASetPrimitiveTopology(command_list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D12GraphicsCommandList_RSSetViewports(command_list, 1, &context.viewport);
    ID3D12GraphicsCommandList_RSSetScissorRects(command_list, 1, &context.scissor_rect);
    ID3D12GraphicsCommandList_DrawInstanced(command_list, 3, 1, 0, 0);

    transition_resource_state(command_list, context.render_target,
            D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
    transition_resource_state(command_list, render_targets[0],
            D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
    transition_resource_state(command_list, render_targets[1],
            D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);

    expected_vec4.x = 2.0f;
    check_sub_resource_vec4(context.render_target, 0, queue, command_list, &expected_vec4, 0);
    reset_command_list(command_list, context.allocator);
    expected_vec4.x = 1.0f;
    check_sub_resource_vec4(render_targets[0], 0, queue, command_list, &expected_vec4, 0);
    reset_command_list(command_list, context.allocator);
    expected_vec4.x = 3.0f;
    check_sub_resource_vec4(render_targets[1], 0, queue, command_list, &expected_vec4, 0);
    reset_command_list(command_list, context.allocator);

    transition_resource_state(command_list, context.render_target,
            D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
    transition_resource_state(command_list, render_targets[0],
            D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
    transition_resource_state(command_list, render_targets[1],
            D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);

    ID3D12GraphicsCommandList_OMSetRenderTargets(command_list, ARRAY_SIZE(rtvs), &context.rtv, true, NULL);
    ID3D12GraphicsCommandList_SetGraphicsRootSignature(command_list, context.root_signature);
    ID3D12GraphicsCommandList_SetPipelineState(command_list, context.pipeline_state);
    ID3D12GraphicsCommandList_IASetPrimitiveTopology(command_list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D12GraphicsCommandList_RSSetViewports(command_list, 1, &context.viewport);
    ID3D12GraphicsCommandList_RSSetScissorRects(command_list, 1, &context.scissor_rect);
    ID3D12GraphicsCommandList_DrawInstanced(command_list, 3, 1, 0, 0);

    transition_resource_state(command_list, context.render_target,
            D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
    transition_resource_state(command_list, render_targets[0],
            D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
    transition_resource_state(command_list, render_targets[1],
            D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);

    expected_vec4.x = 1.0f;
    check_sub_resource_vec4(context.render_target, 0, queue, command_list, &expected_vec4, 0);
    reset_command_list(command_list, context.allocator);
    expected_vec4.x = 3.0f;
    check_sub_resource_vec4(render_targets[0], 0, queue, command_list, &expected_vec4, 0);
    reset_command_list(command_list, context.allocator);
    expected_vec4.x = 2.0f;
    check_sub_resource_vec4(render_targets[1], 0, queue, command_list, &expected_vec4, 0);
    reset_command_list(command_list, context.allocator);

    for (i = 0; i < ARRAY_SIZE(render_targets); ++i)
        ID3D12Resource_Release(render_targets[i]);
    destroy_test_context(&context);
}

void test_fractional_viewports(void)
{
    static const float white[] = {1.0f, 1.0f, 1.0f, 1.0f};
    ID3D12GraphicsCommandList *command_list;
    D3D12_INPUT_LAYOUT_DESC input_layout;
    struct test_context_desc desc;
    D3D12_VERTEX_BUFFER_VIEW vbv;
    struct test_context context;
    struct resource_readback rb;
    ID3D12CommandQueue *queue;
    D3D12_VIEWPORT viewport;
    unsigned int i, x, y;
    ID3D12Resource *vb;

    static const DWORD vs_code[] =
    {
#if 0
        void main(in float4 in_position : POSITION,
                in float2 in_texcoord : TEXCOORD,
                out float4 position : SV_Position,
                out float2 texcoord : TEXCOORD)
        {
            position = in_position;
            texcoord = in_texcoord;
        }
#endif
        0x43425844, 0x4df282ca, 0x85c8bbfc, 0xd44ad19f, 0x1158be97, 0x00000001, 0x00000148, 0x00000003,
        0x0000002c, 0x00000080, 0x000000d8, 0x4e475349, 0x0000004c, 0x00000002, 0x00000008, 0x00000038,
        0x00000000, 0x00000000, 0x00000003, 0x00000000, 0x00000f0f, 0x00000041, 0x00000000, 0x00000000,
        0x00000003, 0x00000001, 0x00000303, 0x49534f50, 0x4e4f4954, 0x58455400, 0x524f4f43, 0xabab0044,
        0x4e47534f, 0x00000050, 0x00000002, 0x00000008, 0x00000038, 0x00000000, 0x00000001, 0x00000003,
        0x00000000, 0x0000000f, 0x00000044, 0x00000000, 0x00000000, 0x00000003, 0x00000001, 0x00000c03,
        0x505f5653, 0x7469736f, 0x006e6f69, 0x43584554, 0x44524f4f, 0xababab00, 0x52444853, 0x00000068,
        0x00010040, 0x0000001a, 0x0300005f, 0x001010f2, 0x00000000, 0x0300005f, 0x00101032, 0x00000001,
        0x04000067, 0x001020f2, 0x00000000, 0x00000001, 0x03000065, 0x00102032, 0x00000001, 0x05000036,
        0x001020f2, 0x00000000, 0x00101e46, 0x00000000, 0x05000036, 0x00102032, 0x00000001, 0x00101046,
        0x00000001, 0x0100003e,
    };
    static const D3D12_SHADER_BYTECODE vs = {vs_code, sizeof(vs_code)};
    static const DWORD ps_code[] =
    {
#if 0
        float4 main(float4 position : SV_Position,
                float2 texcoord : TEXCOORD) : SV_Target
        {
            return float4(position.xy, texcoord);
        }
#endif
        0x43425844, 0xa15616bc, 0x6862ab1c, 0x28b915c0, 0xdb0df67c, 0x00000001, 0x0000011c, 0x00000003,
        0x0000002c, 0x00000084, 0x000000b8, 0x4e475349, 0x00000050, 0x00000002, 0x00000008, 0x00000038,
        0x00000000, 0x00000001, 0x00000003, 0x00000000, 0x0000030f, 0x00000044, 0x00000000, 0x00000000,
        0x00000003, 0x00000001, 0x00000303, 0x505f5653, 0x7469736f, 0x006e6f69, 0x43584554, 0x44524f4f,
        0xababab00, 0x4e47534f, 0x0000002c, 0x00000001, 0x00000008, 0x00000020, 0x00000000, 0x00000000,
        0x00000003, 0x00000000, 0x0000000f, 0x545f5653, 0x65677261, 0xabab0074, 0x52444853, 0x0000005c,
        0x00000040, 0x00000017, 0x04002064, 0x00101032, 0x00000000, 0x00000001, 0x03001062, 0x00101032,
        0x00000001, 0x03000065, 0x001020f2, 0x00000000, 0x05000036, 0x00102032, 0x00000000, 0x00101046,
        0x00000000, 0x05000036, 0x001020c2, 0x00000000, 0x00101406, 0x00000001, 0x0100003e,
    };
    static const D3D12_SHADER_BYTECODE ps = {ps_code, sizeof(ps_code)};
    static const D3D12_INPUT_ELEMENT_DESC layout_desc[] =
    {
        {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 8, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    };
    static const struct
    {
        struct vec2 position;
        struct vec2 texcoord;
    }
    quad[] =
    {
        {{-1.0f, -1.0f}, {0.0f, 0.0f}},
        {{-1.0f,  1.0f}, {0.0f, 1.0f}},
        {{ 1.0f, -1.0f}, {1.0f, 0.0f}},
        {{ 1.0f,  1.0f}, {1.0f, 1.0f}},
    };
    static const float viewport_offsets[] =
    {
        0.0f, 1.0f / 2.0f, 1.0f / 4.0f, 1.0f / 8.0f, 1.0f / 16.0f, 1.0f / 32.0f,
        1.0f / 64.0f, 1.0f / 128.0f, 1.0f / 256.0f, 63.0f / 128.0f,
    };

    memset(&desc, 0, sizeof(desc));
    desc.rt_format = DXGI_FORMAT_R32G32B32A32_FLOAT;
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
            context.root_signature, desc.rt_format, &vs, &ps, &input_layout);

    vb = create_upload_buffer(context.device, sizeof(quad), quad);

    vbv.BufferLocation = ID3D12Resource_GetGPUVirtualAddress(vb);
    vbv.StrideInBytes = sizeof(*quad);
    vbv.SizeInBytes = sizeof(quad);

    for (i = 0; i < ARRAY_SIZE(viewport_offsets); ++i)
    {
        set_viewport(&viewport, viewport_offsets[i], viewport_offsets[i],
                context.render_target_desc.Width, context.render_target_desc.Height, 0.0f, 1.0f);

        if (i)
            transition_resource_state(command_list, context.render_target,
                    D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);

        ID3D12GraphicsCommandList_ClearRenderTargetView(command_list, context.rtv, white, 0, NULL);

        ID3D12GraphicsCommandList_OMSetRenderTargets(command_list, 1, &context.rtv, false, NULL);
        ID3D12GraphicsCommandList_SetGraphicsRootSignature(command_list, context.root_signature);
        ID3D12GraphicsCommandList_SetPipelineState(command_list, context.pipeline_state);
        ID3D12GraphicsCommandList_IASetPrimitiveTopology(command_list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
        ID3D12GraphicsCommandList_IASetVertexBuffers(command_list, 0, 1, &vbv);
        ID3D12GraphicsCommandList_RSSetViewports(command_list, 1, &viewport);
        ID3D12GraphicsCommandList_RSSetScissorRects(command_list, 1, &context.scissor_rect);
        ID3D12GraphicsCommandList_DrawInstanced(command_list, 4, 1, 0, 0);

        transition_resource_state(command_list, context.render_target,
                D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);

        get_texture_readback_with_command_list(context.render_target, 0, &rb, queue, command_list);
        for (y = 0; y < rb.height; ++y)
        {
            for (x = 0; x < rb.width; ++x)
            {
                const struct vec4 *v = get_readback_vec4(&rb, x, y);
                struct vec4 expected = {x + 0.5f, y + 0.5f,
                        (x + 0.5f - viewport_offsets[i]) / context.render_target_desc.Width,
                        1.0f - (y + 0.5f - viewport_offsets[i]) / context.render_target_desc.Height};
                ok(compare_float(v->x, expected.x, 0) && compare_float(v->y, expected.y, 0),
                        "Got fragcoord {%.8e, %.8e}, expected {%.8e, %.8e} at (%u, %u), offset %.8e.\n",
                        v->x, v->y, expected.x, expected.y, x, y, viewport_offsets[i]);
                ok(compare_float(v->z, expected.z, 2) && compare_float(v->w, expected.w, 2),
                        "Got texcoord {%.8e, %.8e}, expected {%.8e, %.8e} at (%u, %u), offset %.8e.\n",
                        v->z, v->w, expected.z, expected.w, x, y, viewport_offsets[i]);
            }
        }
        release_resource_readback(&rb);

        reset_command_list(command_list, context.allocator);
    }

    ID3D12Resource_Release(vb);
    destroy_test_context(&context);
}

void test_scissor(void)
{
    ID3D12GraphicsCommandList *command_list;
    struct test_context_desc desc;
    struct test_context context;
    struct resource_readback rb;
    ID3D12CommandQueue *queue;
    unsigned int color;
    RECT scissor_rect;

    static const DWORD ps_code[] =
    {
#if 0
        float4 main(float4 position : SV_POSITION) : SV_Target
        {
            return float4(0.0, 1.0, 0.0, 1.0);
        }
#endif
        0x43425844, 0x30240e72, 0x012f250c, 0x8673c6ea, 0x392e4cec, 0x00000001, 0x000000d4, 0x00000003,
        0x0000002c, 0x00000060, 0x00000094, 0x4e475349, 0x0000002c, 0x00000001, 0x00000008, 0x00000020,
        0x00000000, 0x00000001, 0x00000003, 0x00000000, 0x0000000f, 0x505f5653, 0x5449534f, 0x004e4f49,
        0x4e47534f, 0x0000002c, 0x00000001, 0x00000008, 0x00000020, 0x00000000, 0x00000000, 0x00000003,
        0x00000000, 0x0000000f, 0x545f5653, 0x65677261, 0xabab0074, 0x52444853, 0x00000038, 0x00000040,
        0x0000000e, 0x03000065, 0x001020f2, 0x00000000, 0x08000036, 0x001020f2, 0x00000000, 0x00004002,
        0x00000000, 0x3f800000, 0x00000000, 0x3f800000, 0x0100003e,
    };
    static const D3D12_SHADER_BYTECODE ps = {ps_code, sizeof(ps_code)};
    static const float red[] = {1.0f, 0.0f, 0.0f, 1.0f};

    memset(&desc, 0, sizeof(desc));
    desc.rt_width = 640;
    desc.rt_height = 480;
    desc.ps = &ps;
    if (!init_test_context(&context, &desc))
        return;
    command_list = context.list;
    queue = context.queue;

    set_rect(&scissor_rect, 160, 120, 480, 360);

    ID3D12GraphicsCommandList_ClearRenderTargetView(command_list, context.rtv, red, 0, NULL);

    ID3D12GraphicsCommandList_OMSetRenderTargets(command_list, 1, &context.rtv, false, NULL);
    ID3D12GraphicsCommandList_SetGraphicsRootSignature(command_list, context.root_signature);
    ID3D12GraphicsCommandList_SetPipelineState(command_list, context.pipeline_state);
    ID3D12GraphicsCommandList_IASetPrimitiveTopology(command_list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D12GraphicsCommandList_RSSetViewports(command_list, 1, &context.viewport);
    ID3D12GraphicsCommandList_RSSetScissorRects(command_list, 1, &scissor_rect);
    ID3D12GraphicsCommandList_DrawInstanced(command_list, 3, 1, 0, 0);

    transition_resource_state(command_list, context.render_target,
            D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);

    get_texture_readback_with_command_list(context.render_target, 0, &rb, queue, command_list);
    color = get_readback_uint(&rb, 320, 60, 0);
    ok(compare_color(color, 0xff0000ff, 1), "Got unexpected color 0x%08x.\n", color);
    color = get_readback_uint(&rb, 80, 240, 0);
    ok(compare_color(color, 0xff0000ff, 1), "Got unexpected color 0x%08x.\n", color);
    color = get_readback_uint(&rb, 320, 240, 0);
    ok(compare_color(color, 0xff00ff00, 1), "Got unexpected color 0x%08x.\n", color);
    color = get_readback_uint(&rb, 560, 240, 0);
    ok(compare_color(color, 0xff0000ff, 1), "Got unexpected color 0x%08x.\n", color);
    color = get_readback_uint(&rb, 320, 420, 0);
    ok(compare_color(color, 0xff0000ff, 1), "Got unexpected color 0x%08x.\n", color);
    release_resource_readback(&rb);

    destroy_test_context(&context);
}

void test_draw_depth_no_ps(void)
{
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc;
    ID3D12GraphicsCommandList *command_list;
    D3D12_INPUT_LAYOUT_DESC input_layout;
    struct depth_stencil_resource ds;
    struct test_context_desc desc;
    D3D12_VERTEX_BUFFER_VIEW vbv;
    struct test_context context;
    ID3D12CommandQueue *queue;
    ID3D12Resource *vb;
    HRESULT hr;

    static const D3D12_INPUT_ELEMENT_DESC layout_desc[] =
    {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    };
    static const struct
    {
        struct vec4 position;
    }
    quad[] =
    {
        {{-1.0f, -1.0f, 0.5f, 1.0f}},
        {{-1.0f,  1.0f, 0.5f, 1.0f}},
        {{ 1.0f, -1.0f, 0.5f, 1.0f}},
        {{ 1.0f,  1.0f, 0.5f, 1.0f}},
    };
    static const DWORD vs_code[] =
    {
#if 0
        void main(float4 in_position : POSITION, out float4 out_position : SV_POSITION)
        {
            out_position = in_position;
        }
#endif
        0x43425844, 0xa7a2f22d, 0x83ff2560, 0xe61638bd, 0x87e3ce90, 0x00000001, 0x000000d8, 0x00000003,
        0x0000002c, 0x00000060, 0x00000094, 0x4e475349, 0x0000002c, 0x00000001, 0x00000008, 0x00000020,
        0x00000000, 0x00000000, 0x00000003, 0x00000000, 0x00000f0f, 0x49534f50, 0x4e4f4954, 0xababab00,
        0x4e47534f, 0x0000002c, 0x00000001, 0x00000008, 0x00000020, 0x00000000, 0x00000001, 0x00000003,
        0x00000000, 0x0000000f, 0x505f5653, 0x5449534f, 0x004e4f49, 0x52444853, 0x0000003c, 0x00010040,
        0x0000000f, 0x0300005f, 0x001010f2, 0x00000000, 0x04000067, 0x001020f2, 0x00000000, 0x00000001,
        0x05000036, 0x001020f2, 0x00000000, 0x00101e46, 0x00000000, 0x0100003e,
    };
    static const D3D12_SHADER_BYTECODE vs = {vs_code, sizeof(vs_code)};

    memset(&desc, 0, sizeof(desc));
    desc.no_render_target = true;
    if (!init_test_context(&context, &desc))
        return;
    command_list = context.list;
    queue = context.queue;

    vb = create_upload_buffer(context.device, sizeof(quad), quad);

    vbv.BufferLocation = ID3D12Resource_GetGPUVirtualAddress(vb);
    vbv.StrideInBytes = sizeof(*quad);
    vbv.SizeInBytes = sizeof(quad);

    init_depth_stencil(&ds, context.device, 640, 480, 1, 1, DXGI_FORMAT_D32_FLOAT, 0, NULL);
    set_viewport(&context.viewport, 0.0f, 0.0f, 640.0f, 480.0f, 0.0f, 1.0f);
    set_rect(&context.scissor_rect, 0, 0, 640, 480);

    context.root_signature = create_empty_root_signature(context.device,
            D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);
    input_layout.pInputElementDescs = layout_desc;
    input_layout.NumElements = ARRAY_SIZE(layout_desc);
    init_pipeline_state_desc(&pso_desc, context.root_signature, 0,  &vs, NULL, &input_layout);
    memset(&pso_desc.PS, 0, sizeof(pso_desc.PS));
    pso_desc.NumRenderTargets = 0;
    pso_desc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    pso_desc.DepthStencilState.DepthEnable = true;
    pso_desc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    pso_desc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    hr = ID3D12Device_CreateGraphicsPipelineState(context.device, &pso_desc,
            &IID_ID3D12PipelineState, (void **)&context.pipeline_state);
    ok(hr == S_OK, "Failed to create graphics pipeline state, hr %#x.\n", hr);

    ID3D12GraphicsCommandList_ClearDepthStencilView(command_list, ds.dsv_handle,
            D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, NULL);

    ID3D12GraphicsCommandList_OMSetRenderTargets(command_list, 0, NULL, false, &ds.dsv_handle);
    ID3D12GraphicsCommandList_SetGraphicsRootSignature(command_list, context.root_signature);
    ID3D12GraphicsCommandList_SetPipelineState(command_list, context.pipeline_state);
    ID3D12GraphicsCommandList_IASetPrimitiveTopology(command_list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    ID3D12GraphicsCommandList_RSSetViewports(command_list, 1, &context.viewport);
    ID3D12GraphicsCommandList_RSSetScissorRects(command_list, 1, &context.scissor_rect);
    ID3D12GraphicsCommandList_IASetVertexBuffers(command_list, 0, 1, &vbv);
    ID3D12GraphicsCommandList_DrawInstanced(command_list, 4, 1, 0, 0);

    transition_resource_state(command_list, ds.texture,
            D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_COPY_SOURCE);
    check_sub_resource_float(ds.texture, 0, queue, command_list, 0.5f, 1);

    destroy_depth_stencil(&ds);
    ID3D12Resource_Release(vb);
    destroy_test_context(&context);
}

void test_draw_depth_only(void)
{
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc;
    ID3D12GraphicsCommandList *command_list;
    struct depth_stencil_resource ds;
    struct test_context_desc desc;
    struct resource_readback rb;
    struct test_context context;
    ID3D12CommandQueue *queue;
    unsigned int i, j;
    HRESULT hr;

    static const DWORD ps_code[] =
    {
#if 0
        float depth;

        float main() : SV_Depth
        {
            return depth;
        }
#endif
        0x43425844, 0x91af6cd0, 0x7e884502, 0xcede4f54, 0x6f2c9326, 0x00000001, 0x000000b0, 0x00000003,
        0x0000002c, 0x0000003c, 0x00000070, 0x4e475349, 0x00000008, 0x00000000, 0x00000008, 0x4e47534f,
        0x0000002c, 0x00000001, 0x00000008, 0x00000020, 0x00000000, 0x00000000, 0x00000003, 0xffffffff,
        0x00000e01, 0x445f5653, 0x68747065, 0xababab00, 0x52444853, 0x00000038, 0x00000040, 0x0000000e,
        0x04000059, 0x00208e46, 0x00000000, 0x00000001, 0x02000065, 0x0000c001, 0x05000036, 0x0000c001,
        0x0020800a, 0x00000000, 0x00000000, 0x0100003e,
    };
    static const D3D12_SHADER_BYTECODE ps = {ps_code, sizeof(ps_code)};
    static const struct
    {
        float clear_depth;
        float depth;
        float expected_depth;
    }
    tests[] =
    {
        {0.0f, 0.0f, 0.0f},
        {0.0f, 0.7f, 0.0f},
        {0.0f, 0.8f, 0.0f},
        {0.0f, 0.5f, 0.0f},

        {1.0f, 0.0f, 0.0f},
        {1.0f, 0.7f, 0.7f},
        {1.0f, 0.8f, 0.8f},
        {1.0f, 0.5f, 0.5f},
    };

    memset(&desc, 0, sizeof(desc));
    desc.no_render_target = true;
    if (!init_test_context(&context, &desc))
        return;
    command_list = context.list;
    queue = context.queue;

    init_depth_stencil(&ds, context.device, 640, 480, 1, 1, DXGI_FORMAT_D32_FLOAT, 0, NULL);
    set_viewport(&context.viewport, 0.0f, 0.0f, 640.0f, 480.0f, 0.0f, 1.0f);
    set_rect(&context.scissor_rect, 0, 0, 640, 480);

    context.root_signature = create_32bit_constants_root_signature(context.device,
            0, 1, D3D12_SHADER_VISIBILITY_PIXEL);
    init_pipeline_state_desc(&pso_desc, context.root_signature, 0, NULL, &ps, NULL);
    pso_desc.NumRenderTargets = 0;
    pso_desc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    pso_desc.DepthStencilState.DepthEnable = true;
    pso_desc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    pso_desc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
    hr = ID3D12Device_CreateGraphicsPipelineState(context.device, &pso_desc,
            &IID_ID3D12PipelineState, (void **)&context.pipeline_state);
    ok(SUCCEEDED(hr), "Failed to create graphics pipeline state, hr %#x.\n", hr);

    for (i = 0; i < ARRAY_SIZE(tests); ++i)
    {
        vkd3d_test_set_context("Test %u", i);

        ID3D12GraphicsCommandList_ClearDepthStencilView(command_list, ds.dsv_handle,
                D3D12_CLEAR_FLAG_DEPTH, tests[i].clear_depth, 0, 0, NULL);

        ID3D12GraphicsCommandList_OMSetRenderTargets(command_list, 0, NULL, false, &ds.dsv_handle);
        ID3D12GraphicsCommandList_SetGraphicsRootSignature(command_list, context.root_signature);
        ID3D12GraphicsCommandList_SetPipelineState(command_list, context.pipeline_state);
        ID3D12GraphicsCommandList_IASetPrimitiveTopology(command_list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ID3D12GraphicsCommandList_RSSetViewports(command_list, 1, &context.viewport);
        ID3D12GraphicsCommandList_RSSetScissorRects(command_list, 1, &context.scissor_rect);

        ID3D12GraphicsCommandList_SetGraphicsRoot32BitConstants(command_list, 0, 1, &tests[i].depth, 0);
        ID3D12GraphicsCommandList_DrawInstanced(command_list, 3, 1, 0, 0);

        transition_resource_state(command_list, ds.texture,
                D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_COPY_SOURCE);
        check_sub_resource_float(ds.texture, 0, queue, command_list, tests[i].expected_depth, 1);

        reset_command_list(command_list, context.allocator);
        transition_resource_state(command_list, ds.texture,
                D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE);
    }
    vkd3d_test_set_context(NULL);

    ID3D12GraphicsCommandList_ClearDepthStencilView(command_list, ds.dsv_handle,
            D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, NULL);
    ID3D12GraphicsCommandList_OMSetRenderTargets(command_list, 0, NULL, false, &ds.dsv_handle);
    ID3D12GraphicsCommandList_SetGraphicsRootSignature(command_list, context.root_signature);
    ID3D12GraphicsCommandList_SetPipelineState(command_list, context.pipeline_state);
    ID3D12GraphicsCommandList_IASetPrimitiveTopology(command_list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D12GraphicsCommandList_RSSetScissorRects(command_list, 1, &context.scissor_rect);
    for (i = 0; i < 4; ++i)
    {
        for (j = 0; j < 4; ++j)
        {
            float depth = 1.0f / 16.0f * (j + 4 * i);
            ID3D12GraphicsCommandList_SetGraphicsRoot32BitConstants(command_list, 0, 1, &depth, 0);

            set_viewport(&context.viewport, 160.0f * j, 120.0f * i, 160.0f, 120.0f, 0.0f, 1.0f);
            ID3D12GraphicsCommandList_RSSetViewports(command_list, 1, &context.viewport);

            ID3D12GraphicsCommandList_DrawInstanced(command_list, 3, 1, 0, 0);
        }
    }
    transition_resource_state(command_list, ds.texture,
            D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_COPY_SOURCE);
    get_texture_readback_with_command_list(ds.texture, 0, &rb, queue, command_list);
    for (i = 0; i < 4; ++i)
    {
        for (j = 0; j < 4; ++j)
        {
            float obtained_depth, expected_depth;

            obtained_depth = get_readback_float(&rb, 80 + j * 160, 60 + i * 120);
            expected_depth = 1.0f / 16.0f * (j + 4 * i);
            ok(compare_float(obtained_depth, expected_depth, 1),
                    "Got unexpected depth %.8e at (%u, %u), expected %.8e.\n",
                    obtained_depth, j, i, expected_depth);
        }
    }
    release_resource_readback(&rb);

    destroy_depth_stencil(&ds);
    destroy_test_context(&context);
}

void test_draw_uav_only(void)
{
    ID3D12DescriptorHeap *cpu_descriptor_heap, *descriptor_heap;
    D3D12_ROOT_SIGNATURE_DESC root_signature_desc;
    D3D12_DESCRIPTOR_RANGE descriptor_range;
    ID3D12GraphicsCommandList *command_list;
    D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle;
    D3D12_GPU_DESCRIPTOR_HANDLE gpu_handle;
    D3D12_ROOT_PARAMETER root_parameter;
    D3D12_RESOURCE_BARRIER barrier;
    struct test_context_desc desc;
    struct test_context context;
    ID3D12CommandQueue *queue;
    ID3D12Resource *resource;
    unsigned int i;
    HRESULT hr;

    static const DWORD ps_code[] =
    {
#if 0
        RWTexture2D<int> u;

        void main()
        {
            InterlockedAdd(u[uint2(0, 0)], 1);
        }
#endif
        0x43425844, 0x237a8398, 0xe7b34c17, 0xa28c91a4, 0xb3614d73, 0x00000001, 0x0000009c, 0x00000003,
        0x0000002c, 0x0000003c, 0x0000004c, 0x4e475349, 0x00000008, 0x00000000, 0x00000008, 0x4e47534f,
        0x00000008, 0x00000000, 0x00000008, 0x58454853, 0x00000048, 0x00000050, 0x00000012, 0x0100086a,
        0x0400189c, 0x0011e000, 0x00000000, 0x00003333, 0x0a0000ad, 0x0011e000, 0x00000000, 0x00004002,
        0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00004001, 0x00000001, 0x0100003e,
    };
    static const D3D12_SHADER_BYTECODE ps = {ps_code, sizeof(ps_code)};
    static const UINT zero[4] = {0};

    memset(&desc, 0, sizeof(desc));
    desc.no_render_target = true;
    if (!init_test_context(&context, &desc))
        return;
    command_list = context.list;
    queue = context.queue;

    descriptor_range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    descriptor_range.NumDescriptors = 1;
    descriptor_range.BaseShaderRegister = 0;
    descriptor_range.RegisterSpace = 0;
    descriptor_range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
    root_parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    root_parameter.DescriptorTable.NumDescriptorRanges = 1;
    root_parameter.DescriptorTable.pDescriptorRanges = &descriptor_range;
    root_parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    root_signature_desc.NumParameters = 1;
    root_signature_desc.pParameters = &root_parameter;
    root_signature_desc.NumStaticSamplers = 0;
    root_signature_desc.pStaticSamplers = NULL;
    root_signature_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
    hr = create_root_signature(context.device, &root_signature_desc, &context.root_signature);
    ok(SUCCEEDED(hr), "Failed to create root signature, hr %#x.\n", hr);

    context.pipeline_state = create_pipeline_state(context.device, context.root_signature, 0, NULL, &ps, NULL);

    resource = create_default_texture(context.device, 1, 1, DXGI_FORMAT_R32_SINT,
            D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    descriptor_heap = create_gpu_descriptor_heap(context.device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1);
    cpu_descriptor_heap = create_cpu_descriptor_heap(context.device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1);
    cpu_handle = ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(descriptor_heap);
    gpu_handle = ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(descriptor_heap);
    ID3D12Device_CreateUnorderedAccessView(context.device, resource, NULL, NULL, cpu_handle);
    cpu_handle = ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(cpu_descriptor_heap);
    ID3D12Device_CreateUnorderedAccessView(context.device, resource, NULL, NULL, cpu_handle);

    ID3D12GraphicsCommandList_ClearUnorderedAccessViewUint(command_list,
            gpu_handle, cpu_handle, resource, zero, 0, NULL);

    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    barrier.UAV.pResource = resource;
    barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    ID3D12GraphicsCommandList_ResourceBarrier(command_list, 1, &barrier);

    set_rect(&context.scissor_rect, 0, 0, 1000, 1000);
    set_viewport(&context.viewport, 0.0f, 0.0f, 1.0f, 100.0f, 0.0f, 0.0f);

    ID3D12GraphicsCommandList_IASetPrimitiveTopology(command_list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D12GraphicsCommandList_RSSetScissorRects(command_list, 1, &context.scissor_rect);
    ID3D12GraphicsCommandList_RSSetViewports(command_list, 1, &context.viewport);
    ID3D12GraphicsCommandList_SetGraphicsRootSignature(command_list, context.root_signature);
    ID3D12GraphicsCommandList_SetDescriptorHeaps(command_list, 1, &descriptor_heap);
    ID3D12GraphicsCommandList_SetGraphicsRootDescriptorTable(command_list, 0, gpu_handle);
    ID3D12GraphicsCommandList_SetPipelineState(command_list, context.pipeline_state);

    for (i = 0; i < 5; ++i)
        ID3D12GraphicsCommandList_DrawInstanced(command_list, 3, 1, 0, 0);

    transition_resource_state(command_list, resource,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
    check_sub_resource_uint(resource, 0, queue, command_list, 500, 0);

    ID3D12DescriptorHeap_Release(cpu_descriptor_heap);
    ID3D12DescriptorHeap_Release(descriptor_heap);
    ID3D12Resource_Release(resource);
    destroy_test_context(&context);
}

void test_texture_resource_barriers(void)
{
    ID3D12CommandAllocator *command_allocator;
    ID3D12GraphicsCommandList *command_list;
    D3D12_RESOURCE_BARRIER barriers[8];
    ID3D12CommandQueue *queue;
    ID3D12Resource *resource;
    ID3D12Device *device;
    ULONG refcount;
    HRESULT hr;

    if (!(device = create_device()))
    {
        skip("Failed to create device.\n");
        return;
    }

    queue = create_command_queue(device, D3D12_COMMAND_LIST_TYPE_DIRECT, D3D12_COMMAND_QUEUE_PRIORITY_NORMAL);

    hr = ID3D12Device_CreateCommandAllocator(device, D3D12_COMMAND_LIST_TYPE_DIRECT,
            &IID_ID3D12CommandAllocator, (void **)&command_allocator);
    ok(SUCCEEDED(hr), "Failed to create command allocator, hr %#x.\n", hr);

    hr = ID3D12Device_CreateCommandList(device, 0, D3D12_COMMAND_LIST_TYPE_DIRECT,
            command_allocator, NULL, &IID_ID3D12GraphicsCommandList, (void **)&command_list);
    ok(SUCCEEDED(hr), "Failed to create command list, hr %#x.\n", hr);

    resource = create_default_texture(device, 32, 32, DXGI_FORMAT_R8G8B8A8_UNORM,
            D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON);

    barriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barriers[0].Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barriers[0].Transition.pResource = resource;
    barriers[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
    barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    ID3D12GraphicsCommandList_ResourceBarrier(command_list, 1, &barriers[0]);

    barriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    barriers[1].Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barriers[1].UAV.pResource = resource;
    ID3D12GraphicsCommandList_ResourceBarrier(command_list, 1, &barriers[1]);

    barriers[2].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barriers[2].Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barriers[2].Transition.pResource = resource;
    barriers[2].Transition.Subresource = 0;
    barriers[2].Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    barriers[2].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    ID3D12GraphicsCommandList_ResourceBarrier(command_list, 1, &barriers[2]);

    barriers[3].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barriers[3].Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barriers[3].Transition.pResource = resource;
    barriers[3].Transition.Subresource = 0;
    barriers[3].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
    barriers[3].Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE
            | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    ID3D12GraphicsCommandList_ResourceBarrier(command_list, 1, &barriers[3]);

    barriers[4].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barriers[4].Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barriers[4].Transition.pResource = resource;
    barriers[4].Transition.Subresource = 0;
    barriers[4].Transition.StateBefore = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE
            | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    barriers[4].Transition.StateAfter  = D3D12_RESOURCE_STATE_COPY_SOURCE;
    ID3D12GraphicsCommandList_ResourceBarrier(command_list, 1, &barriers[4]);

    barriers[5].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barriers[5].Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barriers[5].Transition.pResource = resource;
    barriers[5].Transition.Subresource = 0;
    barriers[5].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
    barriers[5].Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    ID3D12GraphicsCommandList_ResourceBarrier(command_list, 1, &barriers[5]);

    barriers[6].Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    barriers[6].Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barriers[6].UAV.pResource = resource;
    ID3D12GraphicsCommandList_ResourceBarrier(command_list, 1, &barriers[6]);
    ID3D12GraphicsCommandList_ResourceBarrier(command_list, 1, &barriers[6]);

    barriers[7].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barriers[7].Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barriers[7].Transition.pResource = resource;
    barriers[7].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barriers[7].Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    barriers[7].Transition.StateAfter = D3D12_RESOURCE_STATE_COMMON;
    ID3D12GraphicsCommandList_ResourceBarrier(command_list, 1, &barriers[7]);

    ID3D12GraphicsCommandList_ResourceBarrier(command_list, 8, barriers);

    hr = ID3D12GraphicsCommandList_Close(command_list);
    ok(SUCCEEDED(hr), "Failed to close command list, hr %#x.\n", hr);
    exec_command_list(queue, command_list);
    wait_queue_idle(device, queue);

    ID3D12GraphicsCommandList_Release(command_list);
    ID3D12CommandAllocator_Release(command_allocator);
    ID3D12Resource_Release(resource);
    ID3D12CommandQueue_Release(queue);
    refcount = ID3D12Device_Release(device);
    ok(!refcount, "ID3D12Device has %u references left.\n", (unsigned int)refcount);
}

void test_bundle_state_inheritance(void)
{
    static const float white[] = {1.0f, 1.0f, 1.0f, 1.0f};
    ID3D12GraphicsCommandList *command_list, *bundle;
    ID3D12CommandAllocator *bundle_allocator;
    struct test_context context;
    struct resource_readback rb;
    ID3D12CommandQueue *queue;
    ID3D12Device *device;
    unsigned int x, y;
    HRESULT hr;

    if (use_warp_device)
    {
        skip("Bundle state inheritance test crashes on WARP.\n");
        return;
    }

    if (!init_test_context(&context, NULL))
        return;
    device = context.device;
    command_list = context.list;
    queue = context.queue;

    hr = ID3D12Device_CreateCommandAllocator(device, D3D12_COMMAND_LIST_TYPE_BUNDLE,
            &IID_ID3D12CommandAllocator, (void **)&bundle_allocator);
    ok(SUCCEEDED(hr), "Failed to create command allocator, hr %#x.\n", hr);
    hr = ID3D12Device_CreateCommandList(device, 0, D3D12_COMMAND_LIST_TYPE_BUNDLE,
            bundle_allocator, NULL, &IID_ID3D12GraphicsCommandList, (void **)&bundle);
    ok(SUCCEEDED(hr), "Failed to create command list, hr %#x.\n", hr);

    /* A bundle does not inherit the current pipeline state. */
    ID3D12GraphicsCommandList_ClearRenderTargetView(command_list, context.rtv, white, 0, NULL);

    ID3D12GraphicsCommandList_OMSetRenderTargets(command_list, 1, &context.rtv, false, NULL);
    ID3D12GraphicsCommandList_SetGraphicsRootSignature(command_list, context.root_signature);
    ID3D12GraphicsCommandList_SetPipelineState(command_list, context.pipeline_state);
    ID3D12GraphicsCommandList_IASetPrimitiveTopology(command_list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D12GraphicsCommandList_RSSetViewports(command_list, 1, &context.viewport);
    ID3D12GraphicsCommandList_RSSetScissorRects(command_list, 1, &context.scissor_rect);

    ID3D12GraphicsCommandList_DrawInstanced(bundle, 3, 1, 0, 0);
    hr = ID3D12GraphicsCommandList_Close(bundle);
    ok(SUCCEEDED(hr), "Failed to close bundle, hr %#x.\n", hr);

    ID3D12GraphicsCommandList_ExecuteBundle(command_list, bundle);

    transition_resource_state(command_list, context.render_target,
            D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);

    get_texture_readback_with_command_list(context.render_target, 0, &rb, queue, command_list);
    for (y = 0; y < rb.height; ++y)
    {
        for (x = 0; x < rb.width; ++x)
        {
           unsigned int v = get_readback_uint(&rb, x, y, 0);
           /* This works on AMD. */
           ok(v == 0xffffffff || v == 0xff00ff00, "Got unexpected value 0x%08x at (%u, %u).\n", v, x, y);
        }
    }
    release_resource_readback(&rb);

    reset_command_list(command_list, context.allocator);
    reset_command_list(bundle, bundle_allocator);

    /* A bundle does not inherit the current primitive topology. */
    transition_resource_state(command_list, context.render_target,
            D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
    ID3D12GraphicsCommandList_ClearRenderTargetView(command_list, context.rtv, white, 0, NULL);

    ID3D12GraphicsCommandList_OMSetRenderTargets(command_list, 1, &context.rtv, false, NULL);
    ID3D12GraphicsCommandList_SetGraphicsRootSignature(command_list, context.root_signature);
    ID3D12GraphicsCommandList_SetPipelineState(command_list, context.pipeline_state);
    ID3D12GraphicsCommandList_IASetPrimitiveTopology(command_list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D12GraphicsCommandList_RSSetViewports(command_list, 1, &context.viewport);
    ID3D12GraphicsCommandList_RSSetScissorRects(command_list, 1, &context.scissor_rect);

    ID3D12GraphicsCommandList_SetPipelineState(bundle, context.pipeline_state);
    ID3D12GraphicsCommandList_DrawInstanced(bundle, 3, 1, 0, 0);
    hr = ID3D12GraphicsCommandList_Close(bundle);
    ok(SUCCEEDED(hr), "Failed to close bundle, hr %#x.\n", hr);

    ID3D12GraphicsCommandList_ExecuteBundle(command_list, bundle);

    transition_resource_state(command_list, context.render_target,
            D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);

    get_texture_readback_with_command_list(context.render_target, 0, &rb, queue, command_list);
    for (y = 0; y < rb.height; ++y)
    {
        for (x = 0; x < rb.width; ++x)
        {
           unsigned int v = get_readback_uint(&rb, x, y, 0);
           /* This works on AMD, even though the debug layer says that the primitive topology is undefined. */
           ok(v == 0xffffffff || v == 0xff00ff00, "Got unexpected value 0x%08x at (%u, %u).\n", v, x, y);
        }
    }
    release_resource_readback(&rb);

    reset_command_list(command_list, context.allocator);
    reset_command_list(bundle, bundle_allocator);

    /* A bundle inherit all other states. */
    transition_resource_state(command_list, context.render_target,
            D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
    ID3D12GraphicsCommandList_ClearRenderTargetView(command_list, context.rtv, white, 0, NULL);

    ID3D12GraphicsCommandList_OMSetRenderTargets(command_list, 1, &context.rtv, false, NULL);
    ID3D12GraphicsCommandList_SetGraphicsRootSignature(command_list, context.root_signature);
    ID3D12GraphicsCommandList_RSSetViewports(command_list, 1, &context.viewport);
    ID3D12GraphicsCommandList_RSSetScissorRects(command_list, 1, &context.scissor_rect);

    ID3D12GraphicsCommandList_SetPipelineState(bundle, context.pipeline_state);
    ID3D12GraphicsCommandList_IASetPrimitiveTopology(bundle, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D12GraphicsCommandList_DrawInstanced(bundle, 3, 1, 0, 0);
    hr = ID3D12GraphicsCommandList_Close(bundle);
    ok(SUCCEEDED(hr), "Failed to close bundle, hr %#x.\n", hr);

    ID3D12GraphicsCommandList_ExecuteBundle(command_list, bundle);

    transition_resource_state(command_list, context.render_target,
            D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
    check_sub_resource_uint(context.render_target, 0, queue, command_list, 0xff00ff00, 0);

    reset_command_list(command_list, context.allocator);
    reset_command_list(bundle, bundle_allocator);

    /* All state that is set in a bundle affects a command list. */
    transition_resource_state(command_list, context.render_target,
            D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
    ID3D12GraphicsCommandList_ClearRenderTargetView(command_list, context.rtv, white, 0, NULL);
    ID3D12GraphicsCommandList_OMSetRenderTargets(command_list, 1, &context.rtv, false, NULL);
    ID3D12GraphicsCommandList_RSSetViewports(command_list, 1, &context.viewport);
    ID3D12GraphicsCommandList_RSSetScissorRects(command_list, 1, &context.scissor_rect);

    ID3D12GraphicsCommandList_SetGraphicsRootSignature(bundle, context.root_signature);
    ID3D12GraphicsCommandList_SetPipelineState(bundle, context.pipeline_state);
    ID3D12GraphicsCommandList_IASetPrimitiveTopology(bundle, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    hr = ID3D12GraphicsCommandList_Close(bundle);
    ok(SUCCEEDED(hr), "Failed to close bundle, hr %#x.\n", hr);

    ID3D12GraphicsCommandList_ExecuteBundle(command_list, bundle);

    ID3D12GraphicsCommandList_DrawInstanced(command_list, 3, 1, 0, 0);

    transition_resource_state(command_list, context.render_target,
            D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
    check_sub_resource_uint(context.render_target, 0, queue, command_list, 0xff00ff00, 0);

    ID3D12CommandAllocator_Release(bundle_allocator);
    ID3D12GraphicsCommandList_Release(bundle);
    destroy_test_context(&context);
}

void test_null_vbv(void)
{
    ID3D12GraphicsCommandList *command_list;
    D3D12_INPUT_LAYOUT_DESC input_layout;
    D3D12_VERTEX_BUFFER_VIEW vbv[2];
    struct test_context_desc desc;
    struct test_context context;
    ID3D12CommandQueue *queue;
    ID3D12Resource *vb;

    static const D3D12_INPUT_ELEMENT_DESC layout_desc[] =
    {
        {"SV_POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT,
                D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"COLOR",       0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, D3D12_APPEND_ALIGNED_ELEMENT,
                D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    };
    static const DWORD vs_code[] =
    {
#if 0
        struct vs_data
        {
            float4 pos : SV_POSITION;
            float4 color : COLOR;
        };

        void main(in struct vs_data vs_input, out struct vs_data vs_output)
        {
            vs_output.pos = vs_input.pos;
            vs_output.color = vs_input.color;
        }
#endif
        0x43425844, 0xd5b32785, 0x35332906, 0x4d05e031, 0xf66a58af, 0x00000001, 0x00000144, 0x00000003,
        0x0000002c, 0x00000080, 0x000000d4, 0x4e475349, 0x0000004c, 0x00000002, 0x00000008, 0x00000038,
        0x00000000, 0x00000000, 0x00000003, 0x00000000, 0x00000f0f, 0x00000044, 0x00000000, 0x00000000,
        0x00000003, 0x00000001, 0x00000f0f, 0x505f5653, 0x5449534f, 0x004e4f49, 0x4f4c4f43, 0xabab0052,
        0x4e47534f, 0x0000004c, 0x00000002, 0x00000008, 0x00000038, 0x00000000, 0x00000001, 0x00000003,
        0x00000000, 0x0000000f, 0x00000044, 0x00000000, 0x00000000, 0x00000003, 0x00000001, 0x0000000f,
        0x505f5653, 0x5449534f, 0x004e4f49, 0x4f4c4f43, 0xabab0052, 0x52444853, 0x00000068, 0x00010040,
        0x0000001a, 0x0300005f, 0x001010f2, 0x00000000, 0x0300005f, 0x001010f2, 0x00000001, 0x04000067,
        0x001020f2, 0x00000000, 0x00000001, 0x03000065, 0x001020f2, 0x00000001, 0x05000036, 0x001020f2,
        0x00000000, 0x00101e46, 0x00000000, 0x05000036, 0x001020f2, 0x00000001, 0x00101e46, 0x00000001,
        0x0100003e,
    };
    static const D3D12_SHADER_BYTECODE vs = {vs_code, sizeof(vs_code)};
    static const DWORD ps_code[] =
    {
#if 0
        struct ps_data
        {
            float4 pos : SV_POSITION;
            float4 color : COLOR;
        };

        float4 main(struct ps_data ps_input) : SV_Target
        {
            return ps_input.color;
        }
#endif
        0x43425844, 0x89803e59, 0x3f798934, 0xf99181df, 0xf5556512, 0x00000001, 0x000000f4, 0x00000003,
        0x0000002c, 0x00000080, 0x000000b4, 0x4e475349, 0x0000004c, 0x00000002, 0x00000008, 0x00000038,
        0x00000000, 0x00000001, 0x00000003, 0x00000000, 0x0000000f, 0x00000044, 0x00000000, 0x00000000,
        0x00000003, 0x00000001, 0x00000f0f, 0x505f5653, 0x5449534f, 0x004e4f49, 0x4f4c4f43, 0xabab0052,
        0x4e47534f, 0x0000002c, 0x00000001, 0x00000008, 0x00000020, 0x00000000, 0x00000000, 0x00000003,
        0x00000000, 0x0000000f, 0x545f5653, 0x65677261, 0xabab0074, 0x52444853, 0x00000038, 0x00000040,
        0x0000000e, 0x03001062, 0x001010f2, 0x00000001, 0x03000065, 0x001020f2, 0x00000000, 0x05000036,
        0x001020f2, 0x00000000, 0x00101e46, 0x00000001, 0x0100003e,
    };
    static const D3D12_SHADER_BYTECODE ps = {ps_code, sizeof(ps_code)};
    static const struct vec4 positions[] =
    {
        {-1.0f, -1.0f, 0.0f, 1.0f},
        {-1.0f,  1.0f, 0.0f, 1.0f},
        { 1.0f, -1.0f, 0.0f, 1.0f},
        { 1.0f,  1.0f, 0.0f, 1.0f},
    };
    static const float white[] = {1.0f, 1.0f, 1.0f, 1.0f};

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

    vb = create_upload_buffer(context.device, sizeof(positions), positions);

    ID3D12GraphicsCommandList_ClearRenderTargetView(command_list, context.rtv, white, 0, NULL);

    ID3D12GraphicsCommandList_OMSetRenderTargets(command_list, 1, &context.rtv, false, NULL);
    ID3D12GraphicsCommandList_SetGraphicsRootSignature(command_list, context.root_signature);
    ID3D12GraphicsCommandList_SetPipelineState(command_list, context.pipeline_state);
    ID3D12GraphicsCommandList_IASetPrimitiveTopology(command_list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    ID3D12GraphicsCommandList_RSSetScissorRects(command_list, 1, &context.scissor_rect);
    ID3D12GraphicsCommandList_RSSetViewports(command_list, 1, &context.viewport);

    vbv[0].BufferLocation = ID3D12Resource_GetGPUVirtualAddress(vb);
    vbv[0].StrideInBytes = sizeof(*positions);
    vbv[0].SizeInBytes = sizeof(positions);
    vbv[1] = vbv[0];
    ID3D12GraphicsCommandList_IASetVertexBuffers(command_list, 0, ARRAY_SIZE(vbv), vbv);
    vbv[1].BufferLocation = 0;
    vbv[1].StrideInBytes = 0;
    vbv[1].SizeInBytes = 0;
    ID3D12GraphicsCommandList_IASetVertexBuffers(command_list, 0, ARRAY_SIZE(vbv), vbv);

    /* Call should be ignored. */
    ID3D12GraphicsCommandList_IASetVertexBuffers(command_list, 0, 1, NULL);
    ID3D12GraphicsCommandList_IASetVertexBuffers(command_list, 1, 1, NULL);

    ID3D12GraphicsCommandList_DrawInstanced(command_list, 4, 4, 0, 0);

    transition_resource_state(command_list, context.render_target,
            D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
    check_sub_resource_uint(context.render_target, 0, queue, command_list, 0x00000000, 0);

    ID3D12Resource_Release(vb);
    destroy_test_context(&context);
}

void test_vbv_stride_edge_cases(void)
{
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc;
    D3D12_STREAM_OUTPUT_BUFFER_VIEW so_view;
    D3D12_INPUT_LAYOUT_DESC input_layout;
    D3D12_SO_DECLARATION_ENTRY so_entry;
    struct test_context_desc desc;
    D3D12_VERTEX_BUFFER_VIEW vbv;
    struct resource_readback rb;
    struct test_context context;
    ID3D12Resource *vb, *xfb;
    ID3D12PipelineState *pso;
    unsigned int i;

    static const D3D12_INPUT_ELEMENT_DESC layout_desc[] =
    {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 16, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    };

    static const DWORD vs_code[] =
    {
#if 0
    float4 main(float4 pos : POSITION) : SV_Position
    {
        return pos;
    }
#endif
        0x43425844, 0x1808c035, 0xc030df61, 0x84df42ec, 0xfc8e362e, 0x00000001, 0x000000dc, 0x00000003,
        0x0000002c, 0x00000060, 0x00000094, 0x4e475349, 0x0000002c, 0x00000001, 0x00000008, 0x00000020,
        0x00000000, 0x00000000, 0x00000003, 0x00000000, 0x00000f0f, 0x49534f50, 0x4e4f4954, 0xababab00,
        0x4e47534f, 0x0000002c, 0x00000001, 0x00000008, 0x00000020, 0x00000000, 0x00000001, 0x00000003,
        0x00000000, 0x0000000f, 0x505f5653, 0x7469736f, 0x006e6f69, 0x58454853, 0x00000040, 0x00010050,
        0x00000010, 0x0100086a, 0x0300005f, 0x001010f2, 0x00000000, 0x04000067, 0x001020f2, 0x00000000,
        0x00000001, 0x05000036, 0x001020f2, 0x00000000, 0x00101e46, 0x00000000, 0x0100003e,
    };
    static const D3D12_SHADER_BYTECODE vs = { vs_code, sizeof(vs_code) };
    const UINT so_stride = 16;
    float vb_data[1024];

    /* Various edge case behavior when stride < offset.
     * This is actually broken on native AMD drivers where bounds checking
     * happens based on vertex index being less than VBV size / stride. */
    struct test_case
    {
        UINT stride;
        UINT size;
        float reference[8];
    };

    /* Negative value marks case which should be 0.0f due to robustness.
     * The positive value denotes the value we should read if robustness does not work as expected. */
    static const struct test_case tests[] = {
        /* Stride 0 should always work as expected on AMD. */
        { 0, 4, { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f } },
        { 0, 8, { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f } },
        { 0, 12, { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f } },
        { 0, 16, { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f } }, /* Fully OOB */
        { 0, 32, { 4.0f, 5.0f, 6.0f, 7.0f, 4.0f, 5.0f, 6.0f, 7.0f } }, /* Fine */

        { 4, 16, { -4.0f, -5.0f, -6.0f, -7.0f, -5.0f, -6.0f, -7.0f, -8.0f } }, /* Fully OOB, but native D3D12 AMD driver thinks there are valid elements here. */
        { 4, 36, { 4.0f, 5.0f, 6.0f, 7.0f, 5.0f, 6.0f, 7.0f, 8.0f } }, /* Fine. There should be room for 2 vertices here. */

        { 8, 16, { -4.0f, -5.0f, -6.0f, -7.0f, -6.0f, -7.0f, -8.0f, -9.0f } }, /* Fully OOB, but native D3D12 AMD driver thinks there are valid elements here. */
        { 8, 40, { 4.0f, 5.0f, 6.0f, 7.0f, 6.0f, 7.0f, 8.0f, 9.0f } }, /* Fine. There should be room for 2. */

        { 12, 16, { -4.0f, -5.0f, -6.0f, -7.0f, 0.0f, 0.0f, 0.0f, 0.0f } }, /* Fully OOB, but native D3D12 AMD driver thinks there is one valid element. */
        { 12, 44, { 4.0f, 5.0f, 6.0f, 7.0f, 7.0f, 8.0f, 9.0f, 10.0f } }, /* Fine. There should be room for 2. */
    };

    memset(&desc, 0, sizeof(desc));
    desc.no_root_signature = true;
    if (!init_test_context(&context, &desc))
        return;

    context.root_signature = create_empty_root_signature(context.device,
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_STREAM_OUTPUT);
    input_layout.pInputElementDescs = layout_desc;
    input_layout.NumElements = ARRAY_SIZE(layout_desc);

    init_pipeline_state_desc(&pso_desc, context.root_signature, DXGI_FORMAT_UNKNOWN, &vs, NULL, &input_layout);
    pso_desc.PS.BytecodeLength = 0;
    pso_desc.PS.pShaderBytecode = NULL;
    pso_desc.StreamOutput.NumEntries = 1;
    pso_desc.StreamOutput.RasterizedStream = 0;
    pso_desc.StreamOutput.pBufferStrides = &so_stride;
    pso_desc.StreamOutput.NumStrides = 1;
    pso_desc.StreamOutput.pSODeclaration = &so_entry;
    pso_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
    so_entry.ComponentCount = 4;
    so_entry.OutputSlot = 0;
    so_entry.SemanticIndex = 0;
    so_entry.SemanticName = "SV_Position";
    so_entry.StartComponent = 0;
    so_entry.Stream = 0;

    xfb = create_default_buffer(context.device, 4096, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_STREAM_OUT);
    for (i = 0; i < ARRAY_SIZE(vb_data); i++)
        vb_data[i] = (float)i;
    vb = create_upload_buffer(context.device, sizeof(vb_data), vb_data);

    ID3D12Device_CreateGraphicsPipelineState(context.device, &pso_desc, &IID_ID3D12PipelineState, (void **)&pso);

    ID3D12GraphicsCommandList_SetGraphicsRootSignature(context.list, context.root_signature);
    ID3D12GraphicsCommandList_SetPipelineState(context.list, pso);

    so_view.BufferFilledSizeLocation = ID3D12Resource_GetGPUVirtualAddress(xfb);
    so_view.BufferLocation = ID3D12Resource_GetGPUVirtualAddress(xfb) + 16;
    so_view.SizeInBytes = 4096 - 16;
    ID3D12GraphicsCommandList_SOSetTargets(context.list, 0, 1, &so_view);
    ID3D12GraphicsCommandList_IASetPrimitiveTopology(context.list, D3D_PRIMITIVE_TOPOLOGY_POINTLIST);

    vbv.BufferLocation = ID3D12Resource_GetGPUVirtualAddress(vb);

    for (i = 0; i < ARRAY_SIZE(tests); i++)
    {
        const D3D12_VIEWPORT vp = { 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f };
        const D3D12_RECT rect = { 0, 0, 1, 1 };
        vbv.SizeInBytes = tests[i].size;
        vbv.StrideInBytes = tests[i].stride;
        ID3D12GraphicsCommandList_RSSetViewports(context.list, 1, &vp);
        ID3D12GraphicsCommandList_RSSetScissorRects(context.list, 1, &rect);
        ID3D12GraphicsCommandList_IASetVertexBuffers(context.list, 0, 1, &vbv);
        ID3D12GraphicsCommandList_DrawInstanced(context.list, 2, 1, 0, 0);
    }
    transition_resource_state(context.list, xfb, D3D12_RESOURCE_STATE_STREAM_OUT, D3D12_RESOURCE_STATE_COPY_SOURCE);

    get_buffer_readback_with_command_list(xfb, DXGI_FORMAT_R32G32B32A32_FLOAT, &rb, context.queue, context.list);

    for (i = 0; i < ARRAY_SIZE(tests); i++)
    {
        const struct vec4 *v0, *v1;
        v0 = get_readback_vec4(&rb, 1 + 2 * i, 0);
        v1 = get_readback_vec4(&rb, 2 + 2 * i, 0);

#define check(dat, ref_index) do { \
    float ref = tests[i].reference[ref_index]; \
    bool robust_is_zero = ref < 0.0f; \
    ref = fabsf(ref); \
    if (robust_is_zero && dat == ref) \
        skip("Test %u, index %u expected 0 output, but robustness failed. Got expected output as if robustness did not happen.\n", i, ref_index); \
    else \
        ok(dat == ref || (robust_is_zero && dat == 0.0f), "Test %u, index %u, %f != %f\n", i, ref_index, dat, ref); \
} while(0)

        check(v0->x, 0); check(v0->y, 1); check(v0->z, 2); check(v0->w, 3);
        check(v1->x, 4); check(v1->y, 5); check(v1->z, 6); check(v1->w, 7);
#undef check
    }

    release_resource_readback(&rb);

    ID3D12PipelineState_Release(pso);
    ID3D12Resource_Release(xfb);
    ID3D12Resource_Release(vb);
    destroy_test_context(&context);
}

void test_execute_indirect_state(void)
{
    static const struct vec4 values = { 1000.0f, 2000.0f, 3000.0f, 4000.0f };
    D3D12_INDIRECT_ARGUMENT_DESC indirect_argument_descs[2];
    D3D12_COMMAND_SIGNATURE_DESC command_signature_desc;
    D3D12_ROOT_SIGNATURE_DESC root_signature_desc;
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc;
    ID3D12CommandSignature *command_signature;
    D3D12_SO_DECLARATION_ENTRY so_entries[1];
    ID3D12GraphicsCommandList *command_list;
    D3D12_ROOT_PARAMETER root_parameters[4];
    ID3D12Resource *argument_buffer_late;
    D3D12_STREAM_OUTPUT_BUFFER_VIEW sov;
    ID3D12Resource *streamout_buffer;
    D3D12_VERTEX_BUFFER_VIEW vbvs[2];
    ID3D12Resource *argument_buffer;
    struct test_context_desc desc;
    ID3D12Resource *count_buffer;
    struct test_context context;
    struct resource_readback rb;
    D3D12_INDEX_BUFFER_VIEW ibv;
    ID3D12CommandQueue *queue;
    const UINT so_stride = 16;
    ID3D12PipelineState *pso;
    ID3D12Resource *vbo[3];
    ID3D12Resource *ibo[2];
    unsigned int i, j, k;
    ID3D12Resource *cbv;
    ID3D12Resource *srv;
    ID3D12Resource *uav;
    HRESULT hr;

    static const D3D12_INPUT_ELEMENT_DESC layout_desc[] =
    {
        {"COLOR", 0, DXGI_FORMAT_R32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"COLOR", 1, DXGI_FORMAT_R32_FLOAT, 1, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    };

    struct test
    {
        const D3D12_INDIRECT_ARGUMENT_DESC *indirect_arguments;
        uint32_t indirect_argument_count;
        const void *argument_buffer_data;
        size_t argument_buffer_size;
        uint32_t api_max_count;
        const struct vec4 *expected_output;
        uint32_t expected_output_count;
        uint32_t stride;
        bool needs_root_sig;
    };

    /* Modify root parameters. */
    struct root_constant_data
    {
        float constants[2];
        D3D12_DRAW_INDEXED_ARGUMENTS indexed;
    };

    static const D3D12_INDIRECT_ARGUMENT_DESC root_constant_sig[2] =
    {
        { .Type = D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT, .Constant = {
            .RootParameterIndex = 0, .DestOffsetIn32BitValues = 1, .Num32BitValuesToSet = 2 }},
        { .Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED }
    };

    static const struct root_constant_data root_constant_data[] =
    {
        {
            .constants = { 100.0f, 500.0f },
            .indexed = { .IndexCountPerInstance = 2, .InstanceCount = 1 }
        },
        {
            .constants = { 200.0f, 800.0f },
            .indexed = { .IndexCountPerInstance = 1, .InstanceCount = 2,
                         .StartIndexLocation = 1, .StartInstanceLocation = 100, }
        },
    };

    static const struct vec4 root_constant_expected[] =
    {
        { 1000.0f, 64.0f + 100.0f, 500.0f, 4000.0f },
        { 1001.0f, 65.0f + 100.0f, 500.0f, 4000.0f },
        { 1001.0f, 65.0f + 200.0f, 800.0f, 4000.0f },
        { 1001.0f, 65.0f + 200.0f, 800.0f, 4001.0f },
    };

    /* Modify VBOs. */
    struct indirect_vbo_data
    {
        D3D12_VERTEX_BUFFER_VIEW view[2];
        D3D12_DRAW_INDEXED_ARGUMENTS indexed;
    };

    static const D3D12_INDIRECT_ARGUMENT_DESC indirect_vbo_sig[3] =
    {
        { .Type = D3D12_INDIRECT_ARGUMENT_TYPE_VERTEX_BUFFER_VIEW, .VertexBuffer = { .Slot = 0 }},
        { .Type = D3D12_INDIRECT_ARGUMENT_TYPE_VERTEX_BUFFER_VIEW, .VertexBuffer = { .Slot = 1 }},
        { .Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED },
    };

    /* Fill buffer locations later. */
    struct indirect_vbo_data indirect_vbo_data[] =
    {
        {
            .view = { { 0, 64, 8 }, { 0, 64, 16 } },
            .indexed = { .IndexCountPerInstance = 2, .InstanceCount = 2 }
        },
        {
            /* Test indirectly binding NULL descriptor and 0 stride. */
            .view = { { 0, 0, 0 }, { 0, 64, 0 } },
            .indexed = { .IndexCountPerInstance = 2, .InstanceCount = 1 }
        }
    };

    static const struct vec4 indirect_vbo_expected[] =
    {
        { 1064.0f, 2128.0f, 3000.0f, 4000.0f },
        { 1066.0f, 2132.0f, 3000.0f, 4000.0f },
        { 1064.0f, 2128.0f, 3000.0f, 4001.0f },
        { 1066.0f, 2132.0f, 3000.0f, 4001.0f },
        { 1000.0f, 2016.0f, 3000.0f, 4000.0f }, /* This is buggy on WARP and AMD. We seem to get null descriptor instead. */
        { 1000.0f, 2016.0f, 3000.0f, 4000.0f }, /* This is buggy on WARP and AMD. */
    };

    /* Modify just one VBO. */
    struct indirect_vbo_one_data
    {
        D3D12_VERTEX_BUFFER_VIEW view;
        D3D12_DRAW_INDEXED_ARGUMENTS indexed;
    };

    static const D3D12_INDIRECT_ARGUMENT_DESC indirect_vbo_one_sig[2] =
    {
        { .Type = D3D12_INDIRECT_ARGUMENT_TYPE_VERTEX_BUFFER_VIEW, .VertexBuffer = { .Slot = 0 }},
        { .Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED },
    };

    /* Fill buffer locations later. */
    struct indirect_vbo_one_data indirect_vbo_one_data[] =
    {
        {
            .view = { 0, 64, 8 },
            .indexed = { .IndexCountPerInstance = 2, .InstanceCount = 1 }
        },
        {
            .indexed = { .IndexCountPerInstance = 1, .InstanceCount = 1 }
        }
    };

    static const struct vec4 indirect_vbo_one_expected[] =
    {
        { 1128.0f, 2064.0f, 3000.0f, 4000.0f },
        { 1130.0f, 2065.0f, 3000.0f, 4000.0f },
        { 1000.0f, 2064.0f, 3000.0f, 4000.0f },
    };

    /* Indirect IBO */
    struct indirect_ibo_data
    {
        D3D12_INDEX_BUFFER_VIEW view;
        D3D12_DRAW_INDEXED_ARGUMENTS indexed;
    };

    static const D3D12_INDIRECT_ARGUMENT_DESC indirect_ibo_sig[2] =
    {
        { .Type = D3D12_INDIRECT_ARGUMENT_TYPE_INDEX_BUFFER_VIEW },
        { .Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED },
    };

    struct indirect_ibo_data indirect_ibo_data[] =
    {
        {
            .view = { 0, 0, DXGI_FORMAT_R32_UINT },
            .indexed = { .IndexCountPerInstance = 2, .InstanceCount = 1 }
        },
        {
            .view = { 0, 64, DXGI_FORMAT_R16_UINT },
            .indexed = { .IndexCountPerInstance = 4, .InstanceCount = 1 }
        },
    };

    static const struct vec4 indirect_ibo_expected[] =
    {
        { 1000.0f, 2064.0f, 3000.0f, 4000.0f },
        { 1000.0f, 2064.0f, 3000.0f, 4000.0f },
        { 1016.0f, 2080.0f, 3000.0f, 4000.0f },
        { 1000.0f, 2064.0f, 3000.0f, 4000.0f },
        { 1017.0f, 2081.0f, 3000.0f, 4000.0f },
        { 1000.0f, 2064.0f, 3000.0f, 4000.0f },
    };

    /* Indirect root arguments */
    struct indirect_root_descriptor_data
    {
        D3D12_GPU_VIRTUAL_ADDRESS cbv;
        D3D12_GPU_VIRTUAL_ADDRESS srv;
        D3D12_GPU_VIRTUAL_ADDRESS uav;
        D3D12_DRAW_ARGUMENTS array;
    };

    static const D3D12_INDIRECT_ARGUMENT_DESC indirect_root_descriptor_sig[4] =
    {
        { .Type = D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT_BUFFER_VIEW, .ConstantBufferView = { .RootParameterIndex = 1 } },
        { .Type = D3D12_INDIRECT_ARGUMENT_TYPE_SHADER_RESOURCE_VIEW, .ShaderResourceView = { .RootParameterIndex = 2 } },
        { .Type = D3D12_INDIRECT_ARGUMENT_TYPE_UNORDERED_ACCESS_VIEW, .UnorderedAccessView = { .RootParameterIndex = 3 } },
        { .Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW },
    };

    struct indirect_root_descriptor_data indirect_root_descriptor_data[] =
    {
        { .array = { .VertexCountPerInstance = 1, .InstanceCount = 1 } },
        { .array = { .VertexCountPerInstance = 1, .InstanceCount = 1 } },
    };

    static const struct vec4 indirect_root_descriptor_expected[] =
    {
        { 1000.0f, 2064.0f, 3000.0f + 64.0f, 4000.0f + 2.0f },
        { 1000.0f, 2064.0f, 3000.0f + 128.0f, 4000.0f + 3.0f },
    };

    /* Test packing rules.
     * 64-bit aligned values are tightly packed with 32-bit alignment when they are in indirect command buffers. */
    struct indirect_alignment_data
    {
        float value;
        uint32_t cbv_va[2];
        D3D12_DRAW_ARGUMENTS arrays;
    };
    static const D3D12_INDIRECT_ARGUMENT_DESC indirect_alignment_sig[3] =
    {
        { .Type = D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT, .Constant = {
                .RootParameterIndex = 0, .DestOffsetIn32BitValues = 1, .Num32BitValuesToSet = 1 }},
        { .Type = D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT_BUFFER_VIEW, .ConstantBufferView = { .RootParameterIndex = 1 }},
        { .Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW },
    };

    struct indirect_alignment_data indirect_alignment_data[] =
    {
        {
            .value = 5.0f,
            .arrays = { .VertexCountPerInstance = 1, .InstanceCount = 1 }
        },
        {
            .value = 6.0f,
            .arrays = { .VertexCountPerInstance = 1, .InstanceCount = 1 }
        },
    };

    static const struct vec4 indirect_alignment_expected[] =
    {
        { 1000.0f, 69.0f, 3064.0f, 4000.0f },
        { 1000.0f, 70.0f, 3128.0f, 4000.0f },
    };

#define DECL_TEST(t, needs_root_sig) { t##_sig, ARRAY_SIZE(t##_sig), t##_data, sizeof(t##_data), ARRAY_SIZE(t##_data), \
        t##_expected, ARRAY_SIZE(t##_expected), sizeof(*(t##_data)), needs_root_sig }
    const struct test tests[] =
    {
        DECL_TEST(root_constant, true),
        DECL_TEST(indirect_vbo, false),
        DECL_TEST(indirect_vbo_one, false),
        DECL_TEST(indirect_ibo, false),
        DECL_TEST(indirect_root_descriptor, true),
        DECL_TEST(indirect_alignment, true),
    };
#undef DECL_TEST

    uint32_t ibo_data[ARRAY_SIZE(ibo)][64];
    float vbo_data[ARRAY_SIZE(vbo)][64];
    float generic_data[4096];

    static const DWORD vs_code[] =
    {
#if 0
    cbuffer RootCBV : register(b0)
    {
            float a;
    };

    StructuredBuffer<float> RootSRV : register(t0);

    cbuffer RootConstants : register(b0, space1)
    {
            float4 root;
    };

    float4 main(float c0 : COLOR0, float c1 : COLOR1, uint iid : SV_InstanceID) : SV_Position
    {
            return float4(c0, c1, a, RootSRV[0] + float(iid)) + root;
    }
#endif
        0x43425844, 0x33b7b302, 0x34259b9b, 0x3e8568d9, 0x5a5e0c3e, 0x00000001, 0x00000268, 0x00000003,
        0x0000002c, 0x00000098, 0x000000cc, 0x4e475349, 0x00000064, 0x00000003, 0x00000008, 0x00000050,
        0x00000000, 0x00000000, 0x00000003, 0x00000000, 0x00000101, 0x00000050, 0x00000001, 0x00000000,
        0x00000003, 0x00000001, 0x00000101, 0x00000056, 0x00000000, 0x00000008, 0x00000001, 0x00000002,
        0x00000101, 0x4f4c4f43, 0x56530052, 0x736e495f, 0x636e6174, 0x00444965, 0x4e47534f, 0x0000002c,
        0x00000001, 0x00000008, 0x00000020, 0x00000000, 0x00000001, 0x00000003, 0x00000000, 0x0000000f,
        0x505f5653, 0x7469736f, 0x006e6f69, 0x58454853, 0x00000194, 0x00010051, 0x00000065, 0x0100086a,
        0x07000059, 0x00308e46, 0x00000000, 0x00000000, 0x00000000, 0x00000001, 0x00000000, 0x07000059,
        0x00308e46, 0x00000001, 0x00000000, 0x00000000, 0x00000001, 0x00000001, 0x070000a2, 0x00307e46,
        0x00000000, 0x00000000, 0x00000000, 0x00000004, 0x00000000, 0x0300005f, 0x00101012, 0x00000000,
        0x0300005f, 0x00101012, 0x00000001, 0x04000060, 0x00101012, 0x00000002, 0x00000008, 0x04000067,
        0x001020f2, 0x00000000, 0x00000001, 0x02000068, 0x00000001, 0x0a0000a7, 0x00100012, 0x00000000,
        0x00004001, 0x00000000, 0x00004001, 0x00000000, 0x00207006, 0x00000000, 0x00000000, 0x05000056,
        0x00100022, 0x00000000, 0x0010100a, 0x00000002, 0x07000000, 0x00100012, 0x00000000, 0x0010001a,
        0x00000000, 0x0010000a, 0x00000000, 0x09000000, 0x00102012, 0x00000000, 0x0010100a, 0x00000000,
        0x0030800a, 0x00000001, 0x00000000, 0x00000000, 0x09000000, 0x00102022, 0x00000000, 0x0010100a,
        0x00000001, 0x0030801a, 0x00000001, 0x00000000, 0x00000000, 0x0b000000, 0x00102042, 0x00000000,
        0x0030800a, 0x00000000, 0x00000000, 0x00000000, 0x0030802a, 0x00000001, 0x00000000, 0x00000000,
        0x09000000, 0x00102082, 0x00000000, 0x0010000a, 0x00000000, 0x0030803a, 0x00000001, 0x00000000,
        0x00000000, 0x0100003e,
    };

    memset(&desc, 0, sizeof(desc));
    desc.no_root_signature = true;
    desc.no_pipeline = true;
    if (!init_test_context(&context, &desc))
        return;
    command_list = context.list;
    queue = context.queue;

    for (j = 0; j < ARRAY_SIZE(ibo); j++)
        for (i = 0; i < ARRAY_SIZE(ibo_data[j]); i++)
            ibo_data[j][i] = j * 16 + i;

    for (j = 0; j < ARRAY_SIZE(vbo); j++)
        for (i = 0; i < ARRAY_SIZE(vbo_data[j]); i++)
            vbo_data[j][i] = (float)(j * ARRAY_SIZE(vbo_data[j]) + i);

    for (i = 0; i < ARRAY_SIZE(generic_data); i++)
        generic_data[i] = (float)i;

    for (i = 0; i < ARRAY_SIZE(ibo); i++)
        ibo[i] = create_upload_buffer(context.device, sizeof(ibo_data[i]), ibo_data[i]);
    for (i = 0; i < ARRAY_SIZE(vbo); i++)
        vbo[i] = create_upload_buffer(context.device, sizeof(vbo_data[i]), vbo_data[i]);
    cbv = create_upload_buffer(context.device, sizeof(generic_data), generic_data);
    srv = create_upload_buffer(context.device, sizeof(generic_data), generic_data);
    uav = create_default_buffer(context.device, sizeof(generic_data),
            D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    indirect_vbo_data[0].view[0].BufferLocation = ID3D12Resource_GetGPUVirtualAddress(vbo[1]);
    indirect_vbo_data[0].view[1].BufferLocation = ID3D12Resource_GetGPUVirtualAddress(vbo[2]);
    indirect_vbo_data[1].view[0].BufferLocation = 0;
    indirect_vbo_data[1].view[1].BufferLocation = ID3D12Resource_GetGPUVirtualAddress(vbo[0]) + 64;

    indirect_vbo_one_data[0].view.BufferLocation = ID3D12Resource_GetGPUVirtualAddress(vbo[2]);
    indirect_vbo_one_data[1].view.BufferLocation = 0;

    indirect_ibo_data[1].view.BufferLocation = ID3D12Resource_GetGPUVirtualAddress(ibo[1]);

    indirect_root_descriptor_data[0].cbv = ID3D12Resource_GetGPUVirtualAddress(cbv) + 256;
    indirect_root_descriptor_data[0].srv = ID3D12Resource_GetGPUVirtualAddress(srv) + 8;
    indirect_root_descriptor_data[0].uav = ID3D12Resource_GetGPUVirtualAddress(uav) + 4;
    indirect_root_descriptor_data[1].cbv = ID3D12Resource_GetGPUVirtualAddress(cbv) + 512;
    indirect_root_descriptor_data[1].srv = ID3D12Resource_GetGPUVirtualAddress(srv) + 12;
    indirect_root_descriptor_data[1].uav = ID3D12Resource_GetGPUVirtualAddress(uav) + 8;

    memcpy(indirect_alignment_data[0].cbv_va, &indirect_root_descriptor_data[0].cbv, sizeof(D3D12_GPU_VIRTUAL_ADDRESS));
    memcpy(indirect_alignment_data[1].cbv_va, &indirect_root_descriptor_data[1].cbv, sizeof(D3D12_GPU_VIRTUAL_ADDRESS));

    memset(&root_signature_desc, 0, sizeof(root_signature_desc));
    root_signature_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
            D3D12_ROOT_SIGNATURE_FLAG_ALLOW_STREAM_OUTPUT;

    memset(root_parameters, 0, sizeof(root_parameters));
    root_signature_desc.pParameters = root_parameters;
    root_signature_desc.NumParameters = ARRAY_SIZE(root_parameters);
    root_parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    root_parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    root_parameters[0].Constants.RegisterSpace = 1;
    root_parameters[0].Constants.Num32BitValues = 4;
    root_parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    root_parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    root_parameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    root_parameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    root_parameters[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    root_parameters[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    hr = create_root_signature(context.device, &root_signature_desc, &context.root_signature);
    ok(SUCCEEDED(hr), "Failed to create root signature, hr #%x.\n", hr);

    memset(so_entries, 0, sizeof(so_entries));
    so_entries[0].ComponentCount = 4;
    so_entries[0].SemanticName = "SV_Position";

    memset(&pso_desc, 0, sizeof(pso_desc));
    pso_desc.VS.pShaderBytecode = vs_code;
    pso_desc.VS.BytecodeLength = sizeof(vs_code);
    pso_desc.StreamOutput.NumStrides = 1;
    pso_desc.StreamOutput.pBufferStrides = &so_stride;
    pso_desc.StreamOutput.pSODeclaration = so_entries;
    pso_desc.StreamOutput.NumEntries = ARRAY_SIZE(so_entries);
    pso_desc.StreamOutput.RasterizedStream = D3D12_SO_NO_RASTERIZED_STREAM;
    pso_desc.pRootSignature = context.root_signature;
    pso_desc.SampleDesc.Count = 1;
    pso_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
    pso_desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    pso_desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    pso_desc.InputLayout.NumElements = ARRAY_SIZE(layout_desc);
    pso_desc.InputLayout.pInputElementDescs = layout_desc;
    hr = ID3D12Device_CreateGraphicsPipelineState(context.device, &pso_desc, &IID_ID3D12PipelineState, (void**)&pso);
    ok(SUCCEEDED(hr), "Failed to create PSO, hr #%x.\n", hr);

    /* Verify sanity checks.
     * As per validation layers, there must be exactly one command in the signature.
     * It must come last. Verify that we check for this. */
    memset(&command_signature_desc, 0, sizeof(command_signature_desc));
    command_signature_desc.NumArgumentDescs = 1;
    command_signature_desc.pArgumentDescs = indirect_argument_descs;
    command_signature_desc.ByteStride = sizeof(D3D12_VERTEX_BUFFER_VIEW);
    indirect_argument_descs[0].Type = D3D12_INDIRECT_ARGUMENT_TYPE_VERTEX_BUFFER_VIEW;
    hr = ID3D12Device_CreateCommandSignature(context.device, &command_signature_desc, NULL,
            &IID_ID3D12CommandSignature, (void**)&command_signature);
    ok(hr == E_INVALIDARG, "Unexpected hr #%x.\n", hr);

    command_signature_desc.NumArgumentDescs = 2;
    command_signature_desc.pArgumentDescs = indirect_argument_descs;
    command_signature_desc.ByteStride = sizeof(D3D12_DRAW_INDEXED_ARGUMENTS) + sizeof(D3D12_VERTEX_BUFFER_VIEW);
    indirect_argument_descs[0].Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED;
    indirect_argument_descs[1].Type = D3D12_INDIRECT_ARGUMENT_TYPE_VERTEX_BUFFER_VIEW;
    hr = ID3D12Device_CreateCommandSignature(context.device, &command_signature_desc, NULL,
            &IID_ID3D12CommandSignature, (void**)&command_signature);
    ok(hr == E_INVALIDARG, "Unexpected hr #%x.\n", hr);

    command_signature_desc.ByteStride = sizeof(D3D12_DRAW_INDEXED_ARGUMENTS) + sizeof(D3D12_DRAW_INDEXED_ARGUMENTS);
    indirect_argument_descs[0].Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED;
    indirect_argument_descs[1].Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED;
    hr = ID3D12Device_CreateCommandSignature(context.device, &command_signature_desc, NULL,
            &IID_ID3D12CommandSignature, (void**)&command_signature);
    ok(hr == E_INVALIDARG, "Unexpected hr #%x.\n", hr);

    for (i = 0; i < ARRAY_SIZE(tests); i++)
    {
        struct vec4 expect_reset_state[2];
        const struct vec4 *expect, *v;
        uint32_t expected_output_size;
        uint32_t clear_vbo_mask;
        bool clear_ibo;
        uint32_t size;

        vkd3d_test_set_context("Test %u", i);

        command_signature_desc.ByteStride = tests[i].stride;
        command_signature_desc.pArgumentDescs = tests[i].indirect_arguments;
        command_signature_desc.NumArgumentDescs = tests[i].indirect_argument_count;
        command_signature_desc.NodeMask = 0;
        hr = ID3D12Device_CreateCommandSignature(context.device, &command_signature_desc,
                tests[i].needs_root_sig ? context.root_signature : NULL,
                &IID_ID3D12CommandSignature, (void**)&command_signature);
        ok(SUCCEEDED(hr), "Failed to create command signature, hr #%x.\n", hr);

        argument_buffer = create_upload_buffer(context.device, 256 * 1024, NULL);
        argument_buffer_late = create_default_buffer(context.device, 256 * 1024,
                D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
        {
            void *ptr;
            ID3D12Resource_Map(argument_buffer, 0, NULL, &ptr);
            memcpy(ptr, tests[i].argument_buffer_data, tests[i].argument_buffer_size);
            ID3D12Resource_Unmap(argument_buffer, 0, NULL);
        }

        count_buffer = create_upload_buffer(context.device, sizeof(tests[i].api_max_count), &tests[i].api_max_count);
        streamout_buffer = create_default_buffer(context.device, 64 * 1024,
                D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_STREAM_OUT);

        ID3D12GraphicsCommandList_SetGraphicsRootSignature(command_list, context.root_signature);
        ID3D12GraphicsCommandList_SetPipelineState(command_list, pso);
        sov.SizeInBytes = 64 * 1024 - sizeof(struct vec4);
        sov.BufferLocation = ID3D12Resource_GetGPUVirtualAddress(streamout_buffer) + sizeof(struct vec4);
        sov.BufferFilledSizeLocation = ID3D12Resource_GetGPUVirtualAddress(streamout_buffer);
        ID3D12GraphicsCommandList_SOSetTargets(command_list, 0, 1, &sov);

        /* Set up default rendering state. */
        ibv.BufferLocation = ID3D12Resource_GetGPUVirtualAddress(ibo[0]);
        ibv.SizeInBytes = sizeof(ibo_data[0]);
        ibv.Format = DXGI_FORMAT_R32_UINT;
        vbvs[0].BufferLocation = ID3D12Resource_GetGPUVirtualAddress(vbo[0]);
        vbvs[0].SizeInBytes = sizeof(vbo_data[0]);
        vbvs[0].StrideInBytes = 4;
        vbvs[1].BufferLocation = ID3D12Resource_GetGPUVirtualAddress(vbo[1]);
        vbvs[1].SizeInBytes = sizeof(vbo_data[1]);
        vbvs[1].StrideInBytes = 4;

        ID3D12GraphicsCommandList_IASetIndexBuffer(command_list, &ibv);
        ID3D12GraphicsCommandList_IASetPrimitiveTopology(command_list, D3D_PRIMITIVE_TOPOLOGY_POINTLIST);
        ID3D12GraphicsCommandList_IASetVertexBuffers(command_list, 0, 2, vbvs);
        ID3D12GraphicsCommandList_SetGraphicsRoot32BitConstants(command_list, 0, 4, &values, 0);
        ID3D12GraphicsCommandList_SetGraphicsRootConstantBufferView(command_list, 1,
                ID3D12Resource_GetGPUVirtualAddress(cbv));
        ID3D12GraphicsCommandList_SetGraphicsRootShaderResourceView(command_list, 2,
                ID3D12Resource_GetGPUVirtualAddress(srv));
        ID3D12GraphicsCommandList_SetGraphicsRootUnorderedAccessView(command_list, 3,
                ID3D12Resource_GetGPUVirtualAddress(uav));
        ID3D12GraphicsCommandList_ExecuteIndirect(command_list, command_signature, tests[i].api_max_count,
                argument_buffer, 0, NULL, 0);
        /* Test equivalent call with indirect count. */
        ID3D12GraphicsCommandList_ExecuteIndirect(command_list, command_signature, 1024,
                argument_buffer, 0, count_buffer, 0);
        /* Test equivalent, but now with late transition to INDIRECT. */
        ID3D12GraphicsCommandList_CopyResource(command_list, argument_buffer_late, argument_buffer);
        transition_resource_state(command_list, argument_buffer_late, D3D12_RESOURCE_STATE_COPY_DEST,
                D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
        ID3D12GraphicsCommandList_ExecuteIndirect(command_list, command_signature, 1024,
                argument_buffer_late, 0, count_buffer, 0);

        /* Root descriptors which are part of the state block are cleared to NULL. Recover them here
         * since attempting to draw next test will crash GPU. */
        ID3D12GraphicsCommandList_SetGraphicsRootConstantBufferView(command_list, 1,
                ID3D12Resource_GetGPUVirtualAddress(cbv));
        ID3D12GraphicsCommandList_SetGraphicsRootShaderResourceView(command_list, 2,
                ID3D12Resource_GetGPUVirtualAddress(srv));
        ID3D12GraphicsCommandList_SetGraphicsRootUnorderedAccessView(command_list, 3,
                ID3D12Resource_GetGPUVirtualAddress(uav));

        /* Other state is cleared to 0. */

        ID3D12GraphicsCommandList_DrawInstanced(command_list, 2, 1, 0, 0);
        transition_resource_state(command_list, streamout_buffer, D3D12_RESOURCE_STATE_STREAM_OUT, D3D12_RESOURCE_STATE_COPY_SOURCE);

        get_buffer_readback_with_command_list(streamout_buffer, DXGI_FORMAT_R32G32B32A32_FLOAT, &rb, queue, command_list);
        reset_command_list(command_list, context.allocator);

        expected_output_size = (tests[i].expected_output_count * 3 + 2) * sizeof(struct vec4);
        size = get_readback_uint(&rb, 0, 0, 0);
        ok(size == expected_output_size, "Expected size %u, got %u.\n", expected_output_size, size);

        for (j = 0; j < tests[i].expected_output_count; j++)
        {
            expect = &tests[i].expected_output[j];
            v = get_readback_vec4(&rb, j + 1, 0);
            ok(compare_vec4(v, expect, 0), "Element (direct count) %u failed: (%f, %f, %f, %f) != (%f, %f, %f, %f)\n",
                    j, v->x, v->y, v->z, v->w, expect->x, expect->y, expect->z, expect->w);

            v = get_readback_vec4(&rb, j + tests[i].expected_output_count + 1, 0);
            ok(compare_vec4(v, expect, 0), "Element (indirect count) %u failed: (%f, %f, %f, %f) != (%f, %f, %f, %f)\n",
                    j, v->x, v->y, v->z, v->w, expect->x, expect->y, expect->z, expect->w);

            v = get_readback_vec4(&rb, j + 2 * tests[i].expected_output_count + 1, 0);
            ok(compare_vec4(v, expect, 0), "Element (late latch) %u failed: (%f, %f, %f, %f) != (%f, %f, %f, %f)\n",
                    j, v->x, v->y, v->z, v->w, expect->x, expect->y, expect->z, expect->w);
        }

        clear_vbo_mask = 0;
        expect_reset_state[0] = values;

        /* Root constant state is cleared to zero if it's part of the signature. */
        for (j = 0; j < tests[i].indirect_argument_count; j++)
        {
            if (tests[i].indirect_arguments[j].Type == D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT)
            {
                for (k = 0; k < tests[i].indirect_arguments[j].Constant.Num32BitValuesToSet; k++)
                    (&expect_reset_state[0].x)[tests[i].indirect_arguments[j].Constant.DestOffsetIn32BitValues + k] = 0.0f;
            }
            else if (tests[i].indirect_arguments[j].Type == D3D12_INDIRECT_ARGUMENT_TYPE_VERTEX_BUFFER_VIEW)
                clear_vbo_mask |= 1u << tests[i].indirect_arguments[j].VertexBuffer.Slot;
        }

        expect_reset_state[1] = expect_reset_state[0];

        /* VBO/IBO state is cleared to zero if it's part of the signature.
         * A NULL IBO should be seen as a IBO which only reads 0 index. */
        if (!(clear_vbo_mask & (1u << 0)))
            expect_reset_state[1].x += 1.0f;

        if (!(clear_vbo_mask & (1u << 1)))
        {
            expect_reset_state[0].y += 64.0f;
            expect_reset_state[1].y += 65.0f;
        }

        for (j = 0; j < 2; j++)
        {
            v = get_readback_vec4(&rb, j + 1 + 3 * tests[i].expected_output_count, 0);
            expect = &expect_reset_state[j];
            ok(compare_vec4(v, expect, 0), "Post-reset element %u failed: (%f, %f, %f, %f) != (%f, %f, %f, %f)\n",
                    j, v->x, v->y, v->z, v->w, expect->x, expect->y, expect->z, expect->w);
        }

        ID3D12CommandSignature_Release(command_signature);
        ID3D12Resource_Release(argument_buffer);
        ID3D12Resource_Release(argument_buffer_late);
        ID3D12Resource_Release(count_buffer);
        ID3D12Resource_Release(streamout_buffer);
        release_resource_readback(&rb);
    }
    vkd3d_test_set_context(NULL);

    ID3D12PipelineState_Release(pso);
    for (i = 0; i < ARRAY_SIZE(vbo); i++)
        ID3D12Resource_Release(vbo[i]);
    for (i = 0; i < ARRAY_SIZE(ibo); i++)
        ID3D12Resource_Release(ibo[i]);
    ID3D12Resource_Release(cbv);
    ID3D12Resource_Release(srv);
    ID3D12Resource_Release(uav);

    destroy_test_context(&context);
}

void test_execute_indirect(void)
{
    ID3D12Resource *argument_buffer, *count_buffer, *uav;
    D3D12_ROOT_SIGNATURE_DESC root_signature_desc;
    ID3D12CommandSignature *command_signature;
    ID3D12GraphicsCommandList *command_list;
    D3D12_INPUT_LAYOUT_DESC input_layout;
    D3D12_ROOT_PARAMETER root_parameter;
    ID3D12PipelineState *pipeline_state;
    ID3D12RootSignature *root_signature;
    struct test_context_desc desc;
    D3D12_VERTEX_BUFFER_VIEW vbv;
    D3D12_INDEX_BUFFER_VIEW ibv;
    struct resource_readback rb;
    struct test_context context;
    ID3D12CommandQueue *queue;
    ID3D12Resource *vb, *ib;
    unsigned int i;
    D3D12_BOX box;
    HRESULT hr;

    static const struct
    {
        struct vec4 position;
        uint32_t color;
    }
    vertices[] =
    {
        {{-1.0f, -1.0f, 0.0f, 1.0f}, 0xffffff00},
        {{-1.0f,  1.0f, 0.0f, 1.0f}, 0xffffff00},
        {{ 1.0f, -1.0f, 0.0f, 1.0f}, 0xffffff00},
        {{ 1.0f,  1.0f, 0.0f, 1.0f}, 0xffffff00},

        {{-1.0f, -1.0f, 0.0f, 1.0f}, 0xff00ff00},
        {{-1.0f,  0.5f, 0.0f, 1.0f}, 0xff00ff00},
        {{ 0.5f, -1.0f, 0.0f, 1.0f}, 0xff00ff00},
        {{ 0.5f,  0.5f, 0.0f, 1.0f}, 0xff00ff00},

        {{-1.0f, -1.0f, 0.0f, 1.0f}, 0xff00ff00},
        {{-1.0f,  1.0f, 0.0f, 1.0f}, 0xff00ff00},
        {{ 1.0f, -1.0f, 0.0f, 1.0f}, 0xff00ff00},
        {{ 1.0f,  1.0f, 0.0f, 1.0f}, 0xff00ff00},
    };
    static const uint32_t indices[] = {0, 1, 2, 3, 2, 1};
    static const D3D12_INPUT_ELEMENT_DESC layout_desc[] =
    {
        {"SV_POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0,  0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"COLOR",       0, DXGI_FORMAT_R8G8B8A8_UNORM,     0, 16, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    };
    static const DWORD vs_code[] =
    {
#if 0
        struct vs_data
        {
            float4 pos : SV_POSITION;
            float4 color : COLOR;
        };

        void main(in struct vs_data vs_input, out struct vs_data vs_output)
        {
            vs_output.pos = vs_input.pos;
            vs_output.color = vs_input.color;
        }
#endif
        0x43425844, 0xd5b32785, 0x35332906, 0x4d05e031, 0xf66a58af, 0x00000001, 0x00000144, 0x00000003,
        0x0000002c, 0x00000080, 0x000000d4, 0x4e475349, 0x0000004c, 0x00000002, 0x00000008, 0x00000038,
        0x00000000, 0x00000000, 0x00000003, 0x00000000, 0x00000f0f, 0x00000044, 0x00000000, 0x00000000,
        0x00000003, 0x00000001, 0x00000f0f, 0x505f5653, 0x5449534f, 0x004e4f49, 0x4f4c4f43, 0xabab0052,
        0x4e47534f, 0x0000004c, 0x00000002, 0x00000008, 0x00000038, 0x00000000, 0x00000001, 0x00000003,
        0x00000000, 0x0000000f, 0x00000044, 0x00000000, 0x00000000, 0x00000003, 0x00000001, 0x0000000f,
        0x505f5653, 0x5449534f, 0x004e4f49, 0x4f4c4f43, 0xabab0052, 0x52444853, 0x00000068, 0x00010040,
        0x0000001a, 0x0300005f, 0x001010f2, 0x00000000, 0x0300005f, 0x001010f2, 0x00000001, 0x04000067,
        0x001020f2, 0x00000000, 0x00000001, 0x03000065, 0x001020f2, 0x00000001, 0x05000036, 0x001020f2,
        0x00000000, 0x00101e46, 0x00000000, 0x05000036, 0x001020f2, 0x00000001, 0x00101e46, 0x00000001,
        0x0100003e,
    };
    static const D3D12_SHADER_BYTECODE vs = {vs_code, sizeof(vs_code)};
    static const DWORD ps_code[] =
    {
#if 0
        struct ps_data
        {
            float4 pos : SV_POSITION;
            float4 color : COLOR;
        };

        float4 main(struct ps_data ps_input) : SV_Target
        {
            return ps_input.color;
        }
#endif
        0x43425844, 0x89803e59, 0x3f798934, 0xf99181df, 0xf5556512, 0x00000001, 0x000000f4, 0x00000003,
        0x0000002c, 0x00000080, 0x000000b4, 0x4e475349, 0x0000004c, 0x00000002, 0x00000008, 0x00000038,
        0x00000000, 0x00000001, 0x00000003, 0x00000000, 0x0000000f, 0x00000044, 0x00000000, 0x00000000,
        0x00000003, 0x00000001, 0x00000f0f, 0x505f5653, 0x5449534f, 0x004e4f49, 0x4f4c4f43, 0xabab0052,
        0x4e47534f, 0x0000002c, 0x00000001, 0x00000008, 0x00000020, 0x00000000, 0x00000000, 0x00000003,
        0x00000000, 0x0000000f, 0x545f5653, 0x65677261, 0xabab0074, 0x52444853, 0x00000038, 0x00000040,
        0x0000000e, 0x03001062, 0x001010f2, 0x00000001, 0x03000065, 0x001020f2, 0x00000000, 0x05000036,
        0x001020f2, 0x00000000, 0x00101e46, 0x00000001, 0x0100003e,
    };
    static const D3D12_SHADER_BYTECODE ps = {ps_code, sizeof(ps_code)};
    static const DWORD cs_code[] =
    {
#if 0
        RWByteAddressBuffer o;

        [numthreads(1, 1, 1)]
        void main(uint3 group_id : SV_groupID)
        {
            uint idx = group_id.x + group_id.y * 2 + group_id.z * 6;
            o.Store(idx * 4, idx);
        }
#endif
        0x43425844, 0xfdd6a339, 0xf3b8096e, 0xb5977014, 0xcdb26cfd, 0x00000001, 0x00000118, 0x00000003,
        0x0000002c, 0x0000003c, 0x0000004c, 0x4e475349, 0x00000008, 0x00000000, 0x00000008, 0x4e47534f,
        0x00000008, 0x00000000, 0x00000008, 0x58454853, 0x000000c4, 0x00050050, 0x00000031, 0x0100086a,
        0x0300009d, 0x0011e000, 0x00000000, 0x0200005f, 0x00021072, 0x02000068, 0x00000001, 0x0400009b,
        0x00000001, 0x00000001, 0x00000001, 0x06000029, 0x00100012, 0x00000000, 0x0002101a, 0x00004001,
        0x00000001, 0x0600001e, 0x00100012, 0x00000000, 0x0010000a, 0x00000000, 0x0002100a, 0x08000023,
        0x00100012, 0x00000000, 0x0002102a, 0x00004001, 0x00000006, 0x0010000a, 0x00000000, 0x07000029,
        0x00100022, 0x00000000, 0x0010000a, 0x00000000, 0x00004001, 0x00000002, 0x070000a6, 0x0011e012,
        0x00000000, 0x0010001a, 0x00000000, 0x0010000a, 0x00000000, 0x0100003e,
    };
    static const struct argument_data
    {
        D3D12_DRAW_ARGUMENTS draws[4];
        D3D12_DISPATCH_ARGUMENTS dispatch;
        D3D12_DRAW_INDEXED_ARGUMENTS indexed_draws[2];
    }
    argument_data =
    {
        {{6, 1, 4, 0}, {6, 1, 8, 0}, {6, 1, 0, 0}},
        {2, 3, 4},
        {{6, 1, 0, 0, 0}, {6, 1, 0, 4, 0}},
    };
    static const uint32_t count_data[] = {2, 1};
    static const float white[] = {1.0f, 1.0f, 1.0f, 1.0f};

    memset(&desc, 0, sizeof(desc));
    desc.root_signature_flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    desc.no_pipeline = true;
    if (!init_test_context(&context, &desc))
        return;
    command_list = context.list;
    queue = context.queue;

    input_layout.pInputElementDescs = layout_desc;
    input_layout.NumElements = ARRAY_SIZE(layout_desc);
    context.pipeline_state = create_pipeline_state(context.device,
            context.root_signature, context.render_target_desc.Format, &vs, &ps, &input_layout);

    vb = create_upload_buffer(context.device, sizeof(vertices), vertices);
    vbv.BufferLocation = ID3D12Resource_GetGPUVirtualAddress(vb);
    vbv.StrideInBytes = sizeof(*vertices);
    vbv.SizeInBytes = sizeof(vertices);

    ib = create_upload_buffer(context.device, sizeof(indices), indices);
    ibv.BufferLocation = ID3D12Resource_GetGPUVirtualAddress(ib);
    ibv.SizeInBytes = sizeof(indices);
    ibv.Format = DXGI_FORMAT_R32_UINT;

    argument_buffer = create_upload_buffer(context.device, sizeof(argument_data), &argument_data);
    count_buffer = create_upload_buffer(context.device, sizeof(count_data), count_data);

    command_signature = create_command_signature(context.device, D3D12_INDIRECT_ARGUMENT_TYPE_DRAW);

    ID3D12GraphicsCommandList_ClearRenderTargetView(command_list, context.rtv, white, 0, NULL);

    ID3D12GraphicsCommandList_OMSetRenderTargets(command_list, 1, &context.rtv, false, NULL);
    ID3D12GraphicsCommandList_SetGraphicsRootSignature(command_list, context.root_signature);
    ID3D12GraphicsCommandList_SetPipelineState(command_list, context.pipeline_state);
    ID3D12GraphicsCommandList_IASetPrimitiveTopology(command_list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    ID3D12GraphicsCommandList_IASetVertexBuffers(command_list, 0, 1, &vbv);
    ID3D12GraphicsCommandList_RSSetViewports(command_list, 1, &context.viewport);
    ID3D12GraphicsCommandList_RSSetScissorRects(command_list, 1, &context.scissor_rect);
    ID3D12GraphicsCommandList_ExecuteIndirect(command_list, command_signature, 2, argument_buffer, 0, NULL, 0);

    transition_resource_state(command_list, context.render_target,
            D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
    check_sub_resource_uint(context.render_target, 0, queue, command_list, 0xff00ff00, 0);

    reset_command_list(command_list, context.allocator);
    transition_resource_state(command_list, context.render_target,
            D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);

    ID3D12GraphicsCommandList_ClearRenderTargetView(command_list, context.rtv, white, 0, NULL);

    ID3D12GraphicsCommandList_OMSetRenderTargets(command_list, 1, &context.rtv, false, NULL);
    ID3D12GraphicsCommandList_SetGraphicsRootSignature(command_list, context.root_signature);
    ID3D12GraphicsCommandList_SetPipelineState(command_list, context.pipeline_state);
    ID3D12GraphicsCommandList_IASetPrimitiveTopology(command_list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    ID3D12GraphicsCommandList_IASetVertexBuffers(command_list, 0, 1, &vbv);
    ID3D12GraphicsCommandList_RSSetViewports(command_list, 1, &context.viewport);
    ID3D12GraphicsCommandList_RSSetScissorRects(command_list, 1, &context.scissor_rect);
    ID3D12GraphicsCommandList_ExecuteIndirect(command_list, command_signature, 4, argument_buffer, 0,
            count_buffer, 0);

    transition_resource_state(command_list, context.render_target,
            D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
    check_sub_resource_uint(context.render_target, 0, queue, command_list, 0xff00ff00, 0);

    reset_command_list(command_list, context.allocator);

    ID3D12CommandSignature_Release(command_signature);
    command_signature = create_command_signature(context.device, D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH);

    uav = create_default_buffer(context.device, 2 * 3 * 4 * sizeof(UINT),
            D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    root_parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    root_parameter.Descriptor.ShaderRegister = 0;
    root_parameter.Descriptor.RegisterSpace = 0;
    root_parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    root_signature_desc.NumParameters = 1;
    root_signature_desc.pParameters = &root_parameter;
    root_signature_desc.NumStaticSamplers = 0;
    root_signature_desc.pStaticSamplers = NULL;
    root_signature_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
    hr = create_root_signature(context.device, &root_signature_desc, &root_signature);
    ok(hr == S_OK, "Failed to create root signature, hr %#x.\n", hr);

    ID3D12GraphicsCommandList_SetComputeRootSignature(command_list, root_signature);
    pipeline_state = create_compute_pipeline_state(context.device, root_signature,
            shader_bytecode(cs_code, sizeof(cs_code)));
    ID3D12GraphicsCommandList_SetPipelineState(command_list, pipeline_state);
    ID3D12GraphicsCommandList_SetComputeRootUnorderedAccessView(command_list,
            0, ID3D12Resource_GetGPUVirtualAddress(uav));
    ID3D12GraphicsCommandList_ExecuteIndirect(command_list, command_signature, 1, argument_buffer,
            offsetof(struct argument_data, dispatch), NULL, 0);

    transition_sub_resource_state(command_list, uav, 0,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
    get_buffer_readback_with_command_list(uav, DXGI_FORMAT_R32_UINT, &rb, queue, command_list);
    for (i = 0; i < rb.width; ++i)
    {
        unsigned int ret = get_readback_uint(&rb, i, 0, 0);
        ok(ret == i, "Got unexpected result %#x at index %u.\n", ret, i);
    }
    release_resource_readback(&rb);

    reset_command_list(command_list, context.allocator);
    transition_resource_state(command_list, context.render_target,
            D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);

    ID3D12CommandSignature_Release(command_signature);
    command_signature = create_command_signature(context.device, D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED);

    ID3D12GraphicsCommandList_ClearRenderTargetView(command_list, context.rtv, white, 0, NULL);

    ID3D12GraphicsCommandList_SetGraphicsRootSignature(command_list, context.root_signature);
    ID3D12GraphicsCommandList_SetPipelineState(command_list, context.pipeline_state);
    ID3D12GraphicsCommandList_IASetPrimitiveTopology(command_list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D12GraphicsCommandList_IASetIndexBuffer(command_list, &ibv);
    ID3D12GraphicsCommandList_IASetVertexBuffers(command_list, 0, 1, &vbv);
    ID3D12GraphicsCommandList_RSSetViewports(command_list, 1, &context.viewport);
    ID3D12GraphicsCommandList_RSSetScissorRects(command_list, 1, &context.scissor_rect);
    ID3D12GraphicsCommandList_OMSetRenderTargets(command_list, 1, &context.rtv, false, NULL);
    ID3D12GraphicsCommandList_ExecuteIndirect(command_list, command_signature,
            ARRAY_SIZE(argument_data.indexed_draws), argument_buffer,
            offsetof(struct argument_data, indexed_draws), NULL, 0);

    transition_resource_state(command_list, context.render_target,
            D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
    get_texture_readback_with_command_list(context.render_target, 0, &rb, queue, command_list);
    set_box(&box, 0, 0, 0, 32, 8, 1);
    check_readback_data_uint(&rb, &box, 0xffffff00, 0);
    set_box(&box, 24, 8, 0, 32, 32, 1);
    check_readback_data_uint(&rb, &box, 0xffffff00, 0);
    set_box(&box, 0, 8, 0, 24, 32, 1);
    check_readback_data_uint(&rb, &box, 0xff00ff00, 0);
    release_resource_readback(&rb);

    reset_command_list(command_list, context.allocator);
    transition_resource_state(command_list, context.render_target,
            D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);

    ID3D12GraphicsCommandList_ClearRenderTargetView(command_list, context.rtv, white, 0, NULL);

    ID3D12GraphicsCommandList_SetGraphicsRootSignature(command_list, context.root_signature);
    ID3D12GraphicsCommandList_SetPipelineState(command_list, context.pipeline_state);
    ID3D12GraphicsCommandList_IASetPrimitiveTopology(command_list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D12GraphicsCommandList_IASetIndexBuffer(command_list, &ibv);
    ID3D12GraphicsCommandList_IASetVertexBuffers(command_list, 0, 1, &vbv);
    ID3D12GraphicsCommandList_RSSetViewports(command_list, 1, &context.viewport);
    ID3D12GraphicsCommandList_RSSetScissorRects(command_list, 1, &context.scissor_rect);
    ID3D12GraphicsCommandList_OMSetRenderTargets(command_list, 1, &context.rtv, false, NULL);
    ID3D12GraphicsCommandList_ExecuteIndirect(command_list, command_signature,
            ARRAY_SIZE(argument_data.indexed_draws), argument_buffer,
            offsetof(struct argument_data, indexed_draws), count_buffer, sizeof(uint32_t));

    transition_resource_state(command_list, context.render_target,
            D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
    check_sub_resource_uint(context.render_target, 0, queue, command_list, 0xffffff00, 0);

    ID3D12PipelineState_Release(pipeline_state);
    ID3D12RootSignature_Release(root_signature);
    ID3D12Resource_Release(ib);
    ID3D12Resource_Release(uav);
    ID3D12Resource_Release(vb);
    ID3D12CommandSignature_Release(command_signature);
    ID3D12Resource_Release(argument_buffer);
    ID3D12Resource_Release(count_buffer);
    destroy_test_context(&context);
}

void test_dispatch_zero_thread_groups(void)
{
    D3D12_ROOT_SIGNATURE_DESC root_signature_desc;
    ID3D12CommandSignature *command_signature;
    ID3D12GraphicsCommandList *command_list;
    D3D12_ROOT_PARAMETER root_parameters[2];
    ID3D12Resource *argument_buffer, *uav;
    struct resource_readback rb;
    struct test_context context;
    ID3D12CommandQueue *queue;
    unsigned int ret, i;
    HRESULT hr;

    static const DWORD cs_code[] =
    {
#if 0
        RWByteAddressBuffer o;

        uint v;

        [numthreads(1, 1, 1)]
        void main()
        {
            o.Store(0, v);
        }
#endif
        0x43425844, 0x3ad946e3, 0x83e33b81, 0x83532aa4, 0x40831f89, 0x00000001, 0x000000b0, 0x00000003,
        0x0000002c, 0x0000003c, 0x0000004c, 0x4e475349, 0x00000008, 0x00000000, 0x00000008, 0x4e47534f,
        0x00000008, 0x00000000, 0x00000008, 0x58454853, 0x0000005c, 0x00050050, 0x00000017, 0x0100086a,
        0x04000059, 0x00208e46, 0x00000000, 0x00000001, 0x0300009d, 0x0011e000, 0x00000000, 0x0400009b,
        0x00000001, 0x00000001, 0x00000001, 0x080000a6, 0x0011e012, 0x00000000, 0x00004001, 0x00000000,
        0x0020800a, 0x00000000, 0x00000000, 0x0100003e,
    };
    static const D3D12_DISPATCH_ARGUMENTS argument_data[] =
    {
        {1, 1, 1},
        {0, 3, 4},
        {0, 0, 4},
        {0, 0, 0},
        {4, 0, 0},
        {4, 0, 3},
        {4, 2, 0},
        {0, 2, 0},
        {0, 0, 0},
    };

    if (!init_compute_test_context(&context))
        return;
    command_list = context.list;
    queue = context.queue;

    argument_buffer = create_upload_buffer(context.device, sizeof(argument_data), &argument_data);

    command_signature = create_command_signature(context.device, D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH);

    uav = create_default_buffer(context.device, 2 * 256, /* minTexelBufferOffsetAlignment */
            D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    root_parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    root_parameters[0].Descriptor.ShaderRegister = 0;
    root_parameters[0].Descriptor.RegisterSpace = 0;
    root_parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    root_parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    root_parameters[1].Constants.ShaderRegister = 0;
    root_parameters[1].Constants.RegisterSpace = 0;
    root_parameters[1].Constants.Num32BitValues = 1;
    root_parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    root_signature_desc.NumParameters = 2;
    root_signature_desc.pParameters = root_parameters;
    root_signature_desc.NumStaticSamplers = 0;
    root_signature_desc.pStaticSamplers = NULL;
    root_signature_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
    hr = create_root_signature(context.device, &root_signature_desc, &context.root_signature);
    ok(SUCCEEDED(hr), "Failed to create root signature, hr %#x.\n", hr);

    context.pipeline_state = create_compute_pipeline_state(context.device, context.root_signature,
            shader_bytecode(cs_code, sizeof(cs_code)));

    ID3D12GraphicsCommandList_SetComputeRootSignature(command_list, context.root_signature);
    ID3D12GraphicsCommandList_SetPipelineState(command_list, context.pipeline_state);

    ID3D12GraphicsCommandList_SetComputeRootUnorderedAccessView(command_list,
            0, ID3D12Resource_GetGPUVirtualAddress(uav));
    for (i = 0; i < ARRAY_SIZE(argument_data); ++i)
    {
        ID3D12GraphicsCommandList_SetComputeRoot32BitConstant(command_list,
                1, 10 + i, 0);
        ID3D12GraphicsCommandList_ExecuteIndirect(command_list, command_signature,
                1, argument_buffer, i * sizeof(*argument_data), NULL, 0);
    }

    ID3D12GraphicsCommandList_SetComputeRootUnorderedAccessView(command_list,
            0, ID3D12Resource_GetGPUVirtualAddress(uav) + 256);
    for (i = 0; i < ARRAY_SIZE(argument_data); ++i)
    {
        const D3D12_DISPATCH_ARGUMENTS *arg = &argument_data[i];
        ID3D12GraphicsCommandList_SetComputeRoot32BitConstant(command_list,
                1, 50 + i, 0);
        ID3D12GraphicsCommandList_Dispatch(command_list,
                arg->ThreadGroupCountX, arg->ThreadGroupCountY, arg->ThreadGroupCountZ);
    }

    transition_sub_resource_state(command_list, uav, 0,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
    get_buffer_readback_with_command_list(uav, DXGI_FORMAT_R32_UINT, &rb, queue, command_list);
    ret = get_readback_uint(&rb, 0, 0, 0);
    ok(ret == 10, "Got unexpected result %#x.\n", ret);
    ret = get_readback_uint(&rb, 64, 0, 0);
    ok(ret == 50, "Got unexpected result %#x.\n", ret);
    release_resource_readback(&rb);

    ID3D12Resource_Release(uav);
    ID3D12CommandSignature_Release(command_signature);
    ID3D12Resource_Release(argument_buffer);
    destroy_test_context(&context);
}

void test_unaligned_vertex_stride(void)
{
    ID3D12PipelineState *instance_pipeline_state;
    ID3D12GraphicsCommandList *command_list;
    D3D12_INPUT_LAYOUT_DESC input_layout;
    D3D12_VERTEX_BUFFER_VIEW vbv[2];
    struct test_context_desc desc;
    struct test_context context;
    ID3D12CommandQueue *queue;
    ID3D12Resource *vb[2];
    unsigned int i;

    static const D3D12_INPUT_ELEMENT_DESC layout_desc[] =
    {
        {"sv_position", 0, DXGI_FORMAT_R16G16B16A16_SNORM, 0, D3D12_APPEND_ALIGNED_ELEMENT,
                D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"color",       0, DXGI_FORMAT_R16G16B16A16_SNORM, 1, D3D12_APPEND_ALIGNED_ELEMENT,
                D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    };
    static const D3D12_INPUT_ELEMENT_DESC instance_layout_desc[] =
    {
        {"sv_position", 0, DXGI_FORMAT_R16G16B16A16_SNORM, 0, D3D12_APPEND_ALIGNED_ELEMENT,
                D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"color",       0, DXGI_FORMAT_R16G16B16A16_SNORM, 1, D3D12_APPEND_ALIGNED_ELEMENT,
                D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 0},
    };
    static const DWORD vs_code[] =
    {
#if 0
        struct vs_data
        {
            float4 pos : SV_POSITION;
            float4 color : COLOR;
        };

        void main(in struct vs_data vs_input, out struct vs_data vs_output)
        {
            vs_output.pos = vs_input.pos;
            vs_output.color = vs_input.color;
        }
#endif
        0x43425844, 0xd5b32785, 0x35332906, 0x4d05e031, 0xf66a58af, 0x00000001, 0x00000144, 0x00000003,
        0x0000002c, 0x00000080, 0x000000d4, 0x4e475349, 0x0000004c, 0x00000002, 0x00000008, 0x00000038,
        0x00000000, 0x00000000, 0x00000003, 0x00000000, 0x00000f0f, 0x00000044, 0x00000000, 0x00000000,
        0x00000003, 0x00000001, 0x00000f0f, 0x505f5653, 0x5449534f, 0x004e4f49, 0x4f4c4f43, 0xabab0052,
        0x4e47534f, 0x0000004c, 0x00000002, 0x00000008, 0x00000038, 0x00000000, 0x00000001, 0x00000003,
        0x00000000, 0x0000000f, 0x00000044, 0x00000000, 0x00000000, 0x00000003, 0x00000001, 0x0000000f,
        0x505f5653, 0x5449534f, 0x004e4f49, 0x4f4c4f43, 0xabab0052, 0x52444853, 0x00000068, 0x00010040,
        0x0000001a, 0x0300005f, 0x001010f2, 0x00000000, 0x0300005f, 0x001010f2, 0x00000001, 0x04000067,
        0x001020f2, 0x00000000, 0x00000001, 0x03000065, 0x001020f2, 0x00000001, 0x05000036, 0x001020f2,
        0x00000000, 0x00101e46, 0x00000000, 0x05000036, 0x001020f2, 0x00000001, 0x00101e46, 0x00000001,
        0x0100003e,
    };
    static const D3D12_SHADER_BYTECODE vs = { vs_code, sizeof(vs_code) };
    static const DWORD ps_code[] =
    {
#if 0
        struct ps_data
        {
            float4 pos : SV_POSITION;
            float4 color : COLOR;
        };

        float4 main(struct ps_data ps_input) : SV_Target
        {
            return ps_input.color;
        }
#endif
        0x43425844, 0x89803e59, 0x3f798934, 0xf99181df, 0xf5556512, 0x00000001, 0x000000f4, 0x00000003,
        0x0000002c, 0x00000080, 0x000000b4, 0x4e475349, 0x0000004c, 0x00000002, 0x00000008, 0x00000038,
        0x00000000, 0x00000001, 0x00000003, 0x00000000, 0x0000000f, 0x00000044, 0x00000000, 0x00000000,
        0x00000003, 0x00000001, 0x00000f0f, 0x505f5653, 0x5449534f, 0x004e4f49, 0x4f4c4f43, 0xabab0052,
        0x4e47534f, 0x0000002c, 0x00000001, 0x00000008, 0x00000020, 0x00000000, 0x00000000, 0x00000003,
        0x00000000, 0x0000000f, 0x545f5653, 0x65677261, 0xabab0074, 0x52444853, 0x00000038, 0x00000040,
        0x0000000e, 0x03001062, 0x001010f2, 0x00000001, 0x03000065, 0x001020f2, 0x00000000, 0x05000036,
        0x001020f2, 0x00000000, 0x00101e46, 0x00000001, 0x0100003e,
    };
    static const D3D12_SHADER_BYTECODE ps = { ps_code, sizeof(ps_code) };

    struct i16vec4
    {
        int16_t x, y, z, w;
    };

    struct unaligned_i16vec4
    {
        uint8_t blob[2 * 4 + 1];
    };

#define I16_MIN -0x7fff
#define I16_MAX 0x7fff
    static const struct i16vec4 positions[] =
    {
        {I16_MIN, I16_MIN, 0.0f, I16_MAX},
        {I16_MIN, I16_MAX, 0.0f, I16_MAX},
        {I16_MAX, I16_MIN, 0.0f, I16_MAX},
        {I16_MAX, I16_MAX, 0.0f, I16_MAX},
    };

    static const struct i16vec4 colors[] =
    {
        {I16_MAX, 0, 0, 0},
        {0, I16_MAX, 0, 0},
        {0, 0, I16_MAX, 0},
        {0, 0, 0, I16_MAX},
        {0, 0, 0, I16_MAX},
        {0, 0, I16_MAX, 0},
        {0, I16_MAX, 0, 0},
        {I16_MAX, 0, 0, 0},
    };

    static const float white[] = { 1.0f, 1.0f, 1.0f, 1.0f };

    struct unaligned_i16vec4 unaligned_colors[ARRAY_SIZE(colors)];
    
    for (i = 0; i < ARRAY_SIZE(colors); i++)
        memcpy(&unaligned_colors[i], &colors[i], sizeof(*colors));

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

    input_layout.pInputElementDescs = instance_layout_desc;
    input_layout.NumElements = ARRAY_SIZE(instance_layout_desc);
    instance_pipeline_state = create_pipeline_state(context.device,
        context.root_signature, context.render_target_desc.Format, &vs, &ps, &input_layout);

    memset(vbv, 0, sizeof(vbv));
    vb[0] = create_upload_buffer(context.device, sizeof(positions), positions);
    vbv[0].BufferLocation = ID3D12Resource_GetGPUVirtualAddress(vb[0]);
    vbv[0].StrideInBytes = sizeof(*positions);
    vbv[0].SizeInBytes = sizeof(positions);

    vb[1] = create_upload_buffer(context.device, sizeof(unaligned_colors), unaligned_colors);
    vbv[1].BufferLocation = ID3D12Resource_GetGPUVirtualAddress(vb[1]) + 2 * sizeof(*unaligned_colors);
    vbv[1].StrideInBytes = sizeof(*unaligned_colors);
    vbv[1].SizeInBytes = 4 * sizeof(*unaligned_colors);

    ID3D12GraphicsCommandList_ClearRenderTargetView(command_list, context.rtv, white, 0, NULL);

    ID3D12GraphicsCommandList_OMSetRenderTargets(command_list, 1, &context.rtv, false, NULL);
    ID3D12GraphicsCommandList_SetGraphicsRootSignature(command_list, context.root_signature);
    ID3D12GraphicsCommandList_SetPipelineState(command_list, context.pipeline_state);
    ID3D12GraphicsCommandList_IASetPrimitiveTopology(command_list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    ID3D12GraphicsCommandList_IASetVertexBuffers(command_list, 0, ARRAY_SIZE(vbv), vbv);
    ID3D12GraphicsCommandList_RSSetViewports(command_list, 1, &context.viewport);
    ID3D12GraphicsCommandList_RSSetScissorRects(command_list, 1, &context.scissor_rect);
    ID3D12GraphicsCommandList_DrawInstanced(command_list, 4, 4, 0, 0);

    vbv[1].BufferLocation = ID3D12Resource_GetGPUVirtualAddress(vb[1]);

    ID3D12GraphicsCommandList_ClearRenderTargetView(command_list, context.rtv, white, 0, NULL);

    ID3D12GraphicsCommandList_OMSetRenderTargets(command_list, 1, &context.rtv, false, NULL);
    ID3D12GraphicsCommandList_SetGraphicsRootSignature(command_list, context.root_signature);
    ID3D12GraphicsCommandList_SetPipelineState(command_list, instance_pipeline_state);
    ID3D12GraphicsCommandList_IASetPrimitiveTopology(command_list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    ID3D12GraphicsCommandList_IASetVertexBuffers(command_list, 0, ARRAY_SIZE(vbv), vbv);
    ID3D12GraphicsCommandList_RSSetViewports(command_list, 1, &context.viewport);
    ID3D12GraphicsCommandList_RSSetScissorRects(command_list, 1, &context.scissor_rect);
    ID3D12GraphicsCommandList_DrawInstanced(command_list, 4, 4, 0, 0);

    transition_resource_state(command_list, context.render_target,
        D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);

    /* There is no one correct result. If we don't crash the GPU, we pass the test. */
    check_sub_resource_uint(context.render_target, 0, queue, command_list, 0xff00ff00, 0xff);

    ID3D12Resource_Release(vb[1]);
    ID3D12Resource_Release(vb[0]);
    ID3D12PipelineState_Release(instance_pipeline_state);
    destroy_test_context(&context);
}

void test_zero_vertex_stride(void)
{
    ID3D12PipelineState *instance_pipeline_state;
    ID3D12GraphicsCommandList *command_list;
    D3D12_INPUT_LAYOUT_DESC input_layout;
    D3D12_VERTEX_BUFFER_VIEW vbv[2];
    struct test_context_desc desc;
    struct test_context context;
    ID3D12CommandQueue *queue;
    ID3D12Resource *vb[2];

    static const D3D12_INPUT_ELEMENT_DESC layout_desc[] =
    {
        {"sv_position", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT,
                D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"color",       0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, D3D12_APPEND_ALIGNED_ELEMENT,
                D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    };
    static const D3D12_INPUT_ELEMENT_DESC instance_layout_desc[] =
    {
        {"sv_position", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT,
                D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"color",       0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, D3D12_APPEND_ALIGNED_ELEMENT,
                D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 0},
    };
    static const DWORD vs_code[] =
    {
#if 0
        struct vs_data
        {
            float4 pos : SV_POSITION;
            float4 color : COLOR;
        };

        void main(in struct vs_data vs_input, out struct vs_data vs_output)
        {
            vs_output.pos = vs_input.pos;
            vs_output.color = vs_input.color;
        }
#endif
        0x43425844, 0xd5b32785, 0x35332906, 0x4d05e031, 0xf66a58af, 0x00000001, 0x00000144, 0x00000003,
        0x0000002c, 0x00000080, 0x000000d4, 0x4e475349, 0x0000004c, 0x00000002, 0x00000008, 0x00000038,
        0x00000000, 0x00000000, 0x00000003, 0x00000000, 0x00000f0f, 0x00000044, 0x00000000, 0x00000000,
        0x00000003, 0x00000001, 0x00000f0f, 0x505f5653, 0x5449534f, 0x004e4f49, 0x4f4c4f43, 0xabab0052,
        0x4e47534f, 0x0000004c, 0x00000002, 0x00000008, 0x00000038, 0x00000000, 0x00000001, 0x00000003,
        0x00000000, 0x0000000f, 0x00000044, 0x00000000, 0x00000000, 0x00000003, 0x00000001, 0x0000000f,
        0x505f5653, 0x5449534f, 0x004e4f49, 0x4f4c4f43, 0xabab0052, 0x52444853, 0x00000068, 0x00010040,
        0x0000001a, 0x0300005f, 0x001010f2, 0x00000000, 0x0300005f, 0x001010f2, 0x00000001, 0x04000067,
        0x001020f2, 0x00000000, 0x00000001, 0x03000065, 0x001020f2, 0x00000001, 0x05000036, 0x001020f2,
        0x00000000, 0x00101e46, 0x00000000, 0x05000036, 0x001020f2, 0x00000001, 0x00101e46, 0x00000001,
        0x0100003e,
    };
    static const D3D12_SHADER_BYTECODE vs = {vs_code, sizeof(vs_code)};
    static const DWORD ps_code[] =
    {
#if 0
        struct ps_data
        {
            float4 pos : SV_POSITION;
            float4 color : COLOR;
        };

        float4 main(struct ps_data ps_input) : SV_Target
        {
            return ps_input.color;
        }
#endif
        0x43425844, 0x89803e59, 0x3f798934, 0xf99181df, 0xf5556512, 0x00000001, 0x000000f4, 0x00000003,
        0x0000002c, 0x00000080, 0x000000b4, 0x4e475349, 0x0000004c, 0x00000002, 0x00000008, 0x00000038,
        0x00000000, 0x00000001, 0x00000003, 0x00000000, 0x0000000f, 0x00000044, 0x00000000, 0x00000000,
        0x00000003, 0x00000001, 0x00000f0f, 0x505f5653, 0x5449534f, 0x004e4f49, 0x4f4c4f43, 0xabab0052,
        0x4e47534f, 0x0000002c, 0x00000001, 0x00000008, 0x00000020, 0x00000000, 0x00000000, 0x00000003,
        0x00000000, 0x0000000f, 0x545f5653, 0x65677261, 0xabab0074, 0x52444853, 0x00000038, 0x00000040,
        0x0000000e, 0x03001062, 0x001010f2, 0x00000001, 0x03000065, 0x001020f2, 0x00000000, 0x05000036,
        0x001020f2, 0x00000000, 0x00101e46, 0x00000001, 0x0100003e,
    };
    static const D3D12_SHADER_BYTECODE ps = {ps_code, sizeof(ps_code)};
    static const struct vec4 positions[] =
    {
        {-1.0f, -1.0f, 0.0f, 1.0f},
        {-1.0f,  1.0f, 0.0f, 1.0f},
        { 1.0f, -1.0f, 0.0f, 1.0f},
        { 1.0f,  1.0f, 0.0f, 1.0f},
    };
    static const struct vec4 colors[] =
    {
        {0.0f, 1.0f, 0.0f, 1.0f},
        {1.0f, 0.0f, 0.0f, 1.0f},
        {0.5f, 0.5f, 0.5f, 1.0f},
        {1.0f, 0.0f, 1.0f, 1.0f},
        {1.0f, 0.0f, 1.0f, 1.0f},
        {1.0f, 0.0f, 1.0f, 1.0f},
        {1.0f, 0.0f, 1.0f, 1.0f},
        {1.0f, 0.0f, 1.0f, 1.0f},
        {1.0f, 0.0f, 1.0f, 1.0f},
    };
    static const float white[] = {1.0f, 1.0f, 1.0f, 1.0f};

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

    input_layout.pInputElementDescs = instance_layout_desc;
    input_layout.NumElements = ARRAY_SIZE(instance_layout_desc);
    instance_pipeline_state = create_pipeline_state(context.device,
            context.root_signature, context.render_target_desc.Format, &vs, &ps, &input_layout);

    memset(vbv, 0, sizeof(vbv));
    vb[0] = create_upload_buffer(context.device, sizeof(positions), positions);
    vbv[0].BufferLocation = ID3D12Resource_GetGPUVirtualAddress(vb[0]);
    vbv[0].StrideInBytes = sizeof(*positions);
    vbv[0].SizeInBytes = sizeof(positions);

    vb[1] = create_upload_buffer(context.device, sizeof(colors), colors);
    vbv[1].BufferLocation = ID3D12Resource_GetGPUVirtualAddress(vb[1]) + 2 * sizeof(*colors);
    vbv[1].StrideInBytes = 0;
    vbv[1].SizeInBytes = sizeof(colors);

    ID3D12GraphicsCommandList_ClearRenderTargetView(command_list, context.rtv, white, 0, NULL);

    ID3D12GraphicsCommandList_OMSetRenderTargets(command_list, 1, &context.rtv, false, NULL);
    ID3D12GraphicsCommandList_SetGraphicsRootSignature(command_list, context.root_signature);
    ID3D12GraphicsCommandList_SetPipelineState(command_list, context.pipeline_state);
    ID3D12GraphicsCommandList_IASetPrimitiveTopology(command_list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    ID3D12GraphicsCommandList_IASetVertexBuffers(command_list, 0, ARRAY_SIZE(vbv), vbv);
    ID3D12GraphicsCommandList_RSSetViewports(command_list, 1, &context.viewport);
    ID3D12GraphicsCommandList_RSSetScissorRects(command_list, 1, &context.scissor_rect);
    ID3D12GraphicsCommandList_DrawInstanced(command_list, 4, 4, 0, 0);

    transition_resource_state(command_list, context.render_target,
            D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
    check_sub_resource_uint(context.render_target, 0, queue, command_list, 0xff808080, 2);

    reset_command_list(command_list, context.allocator);
    transition_resource_state(command_list, context.render_target,
            D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);

    vbv[1].BufferLocation = ID3D12Resource_GetGPUVirtualAddress(vb[1]);

    ID3D12GraphicsCommandList_ClearRenderTargetView(command_list, context.rtv, white, 0, NULL);

    ID3D12GraphicsCommandList_OMSetRenderTargets(command_list, 1, &context.rtv, false, NULL);
    ID3D12GraphicsCommandList_SetGraphicsRootSignature(command_list, context.root_signature);
    ID3D12GraphicsCommandList_SetPipelineState(command_list, instance_pipeline_state);
    ID3D12GraphicsCommandList_IASetPrimitiveTopology(command_list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    ID3D12GraphicsCommandList_IASetVertexBuffers(command_list, 0, ARRAY_SIZE(vbv), vbv);
    ID3D12GraphicsCommandList_RSSetViewports(command_list, 1, &context.viewport);
    ID3D12GraphicsCommandList_RSSetScissorRects(command_list, 1, &context.scissor_rect);
    ID3D12GraphicsCommandList_DrawInstanced(command_list, 4, 4, 0, 0);

    transition_resource_state(command_list, context.render_target,
            D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
    check_sub_resource_uint(context.render_target, 0, queue, command_list, 0xff00ff00, 0);

    ID3D12Resource_Release(vb[1]);
    ID3D12Resource_Release(vb[0]);
    ID3D12PipelineState_Release(instance_pipeline_state);
    destroy_test_context(&context);
}

static void draw_thread_main(void *thread_data)
{
    static const float white[] = {1.0f, 1.0f, 1.0f, 1.0f};
    struct test_context *context = thread_data;
    ID3D12GraphicsCommandList *command_list;
    ID3D12CommandAllocator *allocator;
    D3D12_CPU_DESCRIPTOR_HANDLE rtv;
    ID3D12Resource *render_target;
    ID3D12DescriptorHeap *heap;
    ID3D12CommandQueue *queue;
    ID3D12Device *device;
    unsigned int i;
    HRESULT hr;

    queue = context->queue;
    device = context->device;
    heap = create_cpu_descriptor_heap(device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1);
    rtv = get_cpu_descriptor_handle(context, heap, 0);
    create_render_target(context, NULL, &render_target, &rtv);

    hr = ID3D12Device_CreateCommandAllocator(device, D3D12_COMMAND_LIST_TYPE_DIRECT,
            &IID_ID3D12CommandAllocator, (void **)&allocator);
    ok(SUCCEEDED(hr), "Failed to create command allocator, hr %#x.\n", hr);
    hr = ID3D12Device_CreateCommandList(device, 0, D3D12_COMMAND_LIST_TYPE_DIRECT,
            allocator, NULL, &IID_ID3D12GraphicsCommandList, (void **)&command_list);
    ok(SUCCEEDED(hr), "Failed to create command list, hr %#x.\n", hr);

    for (i = 0; i < 100; ++i)
    {
        ID3D12GraphicsCommandList_ClearRenderTargetView(command_list, rtv, white, 0, NULL);
        ID3D12GraphicsCommandList_OMSetRenderTargets(command_list, 1, &rtv, false, NULL);
        ID3D12GraphicsCommandList_SetGraphicsRootSignature(command_list, context->root_signature);
        ID3D12GraphicsCommandList_SetPipelineState(command_list, context->pipeline_state);
        ID3D12GraphicsCommandList_IASetPrimitiveTopology(command_list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ID3D12GraphicsCommandList_RSSetViewports(command_list, 1, &context->viewport);
        ID3D12GraphicsCommandList_RSSetScissorRects(command_list, 1, &context->scissor_rect);
        ID3D12GraphicsCommandList_DrawInstanced(command_list, 3, 1, 0, 0);

        transition_resource_state(command_list, render_target,
                D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
        check_sub_resource_uint(render_target, 0, queue, command_list, 0xff00ff00, 0);
        reset_command_list(command_list, allocator);
        transition_resource_state(command_list, render_target,
                D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
    }

    ID3D12DescriptorHeap_Release(heap);
    ID3D12Resource_Release(render_target);
    ID3D12CommandAllocator_Release(allocator);
    ID3D12GraphicsCommandList_Release(command_list);
}

void test_multithread_command_queue_exec(void)
{
    static const float white[] = {1.0f, 1.0f, 1.0f, 1.0f};
    ID3D12GraphicsCommandList *command_list;
    struct test_context context;
    ID3D12CommandQueue *queue;
    HANDLE threads[10];
    unsigned int i;

    if (!init_test_context(&context, NULL))
        return;
    command_list = context.list;
    queue = context.queue;

    for (i = 0; i < ARRAY_SIZE(threads); ++i)
    {
        threads[i] = create_thread(draw_thread_main, &context);
        ok(threads[i], "Failed to create thread %u.\n", i);
    }

    for (i = 0; i < 100; ++i)
    {
        ID3D12GraphicsCommandList_ClearRenderTargetView(command_list, context.rtv, white, 0, NULL);
        ID3D12GraphicsCommandList_OMSetRenderTargets(command_list, 1, &context.rtv, false, NULL);
        ID3D12GraphicsCommandList_SetGraphicsRootSignature(command_list, context.root_signature);
        ID3D12GraphicsCommandList_SetPipelineState(command_list, context.pipeline_state);
        ID3D12GraphicsCommandList_IASetPrimitiveTopology(command_list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ID3D12GraphicsCommandList_RSSetViewports(command_list, 1, &context.viewport);
        ID3D12GraphicsCommandList_RSSetScissorRects(command_list, 1, &context.scissor_rect);
        ID3D12GraphicsCommandList_DrawInstanced(command_list, 3, 1, 0, 0);

        transition_resource_state(command_list, context.render_target,
                D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
        check_sub_resource_uint(context.render_target, 0, queue, command_list, 0xff00ff00, 0);
        reset_command_list(command_list, context.allocator);
        transition_resource_state(command_list, context.render_target,
                D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
    }

    for (i = 0; i < ARRAY_SIZE(threads); ++i)
        ok(join_thread(threads[i]), "Failed to join thread %u.\n", i);

    destroy_test_context(&context);
}

void test_command_list_initial_pipeline_state(void)
{
    static const float white[] = {1.0f, 1.0f, 1.0f, 1.0f};
    ID3D12GraphicsCommandList *command_list;
    ID3D12PipelineState *pipeline_state;
    ID3D12CommandAllocator *allocator;
    struct test_context context;
    ID3D12CommandQueue *queue;
    HRESULT hr;

    static const DWORD ps_code[] =
    {
#if 0
        void main(out float4 target : SV_Target)
        {
            target = float4(0.0f, 0.25f, 0.5f, 1.0f);
        }
#endif
        0x43425844, 0x2f09e5ff, 0xaa135d5e, 0x7860f4b5, 0x5c7b8cbc, 0x00000001, 0x000000b4, 0x00000003,
        0x0000002c, 0x0000003c, 0x00000070, 0x4e475349, 0x00000008, 0x00000000, 0x00000008, 0x4e47534f,
        0x0000002c, 0x00000001, 0x00000008, 0x00000020, 0x00000000, 0x00000000, 0x00000003, 0x00000000,
        0x0000000f, 0x545f5653, 0x65677261, 0xabab0074, 0x58454853, 0x0000003c, 0x00000050, 0x0000000f,
        0x0100086a, 0x03000065, 0x001020f2, 0x00000000, 0x08000036, 0x001020f2, 0x00000000, 0x00004002,
        0x00000000, 0x3e800000, 0x3f000000, 0x3f800000, 0x0100003e,
    };
    static const D3D12_SHADER_BYTECODE ps = {ps_code, sizeof(ps_code)};

    if (!init_test_context(&context, NULL))
        return;
    queue = context.queue;

    pipeline_state = create_pipeline_state(context.device,
            context.root_signature, context.render_target_desc.Format, NULL, &ps, NULL);

    hr = ID3D12Device_CreateCommandAllocator(context.device, D3D12_COMMAND_LIST_TYPE_DIRECT,
            &IID_ID3D12CommandAllocator, (void **)&allocator);
    ok(hr == S_OK, "Failed to create command allocator, hr %#x.\n", hr);
    hr = ID3D12Device_CreateCommandList(context.device, 0, D3D12_COMMAND_LIST_TYPE_DIRECT,
            allocator, pipeline_state, &IID_ID3D12GraphicsCommandList, (void **)&command_list);
    ok(hr == S_OK, "Failed to create command list, hr %#x.\n", hr);

    ID3D12GraphicsCommandList_ClearRenderTargetView(command_list, context.rtv, white, 0, NULL);
    ID3D12GraphicsCommandList_OMSetRenderTargets(command_list, 1, &context.rtv, false, NULL);
    ID3D12GraphicsCommandList_SetGraphicsRootSignature(command_list, context.root_signature);
    ID3D12GraphicsCommandList_IASetPrimitiveTopology(command_list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D12GraphicsCommandList_RSSetViewports(command_list, 1, &context.viewport);
    ID3D12GraphicsCommandList_RSSetScissorRects(command_list, 1, &context.scissor_rect);
    ID3D12GraphicsCommandList_DrawInstanced(command_list, 3, 1, 0, 0);
    transition_resource_state(command_list, context.render_target,
            D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
    check_sub_resource_uint(context.render_target, 0, queue, command_list, 0xff804000, 1);

    hr = ID3D12CommandAllocator_Reset(allocator);
    ok(hr == S_OK, "Failed to reset command allocator, hr %#x.\n", hr);
    hr = ID3D12GraphicsCommandList_Reset(command_list, allocator, context.pipeline_state);
    ok(hr == S_OK, "Failed to reset command list, hr %#x.\n", hr);
    transition_resource_state(command_list, context.render_target,
            D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);

    ID3D12GraphicsCommandList_ClearRenderTargetView(command_list, context.rtv, white, 0, NULL);
    ID3D12GraphicsCommandList_OMSetRenderTargets(command_list, 1, &context.rtv, false, NULL);
    ID3D12GraphicsCommandList_SetGraphicsRootSignature(command_list, context.root_signature);
    ID3D12GraphicsCommandList_IASetPrimitiveTopology(command_list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D12GraphicsCommandList_RSSetViewports(command_list, 1, &context.viewport);
    ID3D12GraphicsCommandList_RSSetScissorRects(command_list, 1, &context.scissor_rect);
    ID3D12GraphicsCommandList_DrawInstanced(command_list, 3, 1, 0, 0);
    transition_resource_state(command_list, context.render_target,
            D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
    check_sub_resource_uint(context.render_target, 0, queue, command_list, 0xff00ff00, 0);

    ID3D12CommandAllocator_Release(allocator);
    ID3D12GraphicsCommandList_Release(command_list);
    ID3D12PipelineState_Release(pipeline_state);
    destroy_test_context(&context);
}

static void prepare_instanced_draw(struct test_context *context)
{
    ID3D12GraphicsCommandList *command_list = context->list;

    ID3D12GraphicsCommandList_OMSetRenderTargets(command_list, 1, &context->rtv, false, NULL);
    ID3D12GraphicsCommandList_SetGraphicsRootSignature(command_list, context->root_signature);
    ID3D12GraphicsCommandList_SetPipelineState(command_list, context->pipeline_state);
    ID3D12GraphicsCommandList_IASetPrimitiveTopology(command_list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D12GraphicsCommandList_RSSetViewports(command_list, 1, &context->viewport);
    ID3D12GraphicsCommandList_RSSetScissorRects(command_list, 1, &context->scissor_rect);
}

void test_conditional_rendering(void)
{
    D3D12_ROOT_SIGNATURE_DESC root_signature_desc;
    ID3D12Resource *conditions, *upload_buffer;
    ID3D12CommandSignature *command_signature;
    ID3D12GraphicsCommandList *command_list;
    D3D12_ROOT_PARAMETER root_parameters[2];
    ID3D12Resource *texture, *texture_copy;
    D3D12_RENDER_TARGET_VIEW_DESC rtv_desc;
    D3D12_HEAP_PROPERTIES heap_properties;
    ID3D12PipelineState *pipeline_state;
    ID3D12RootSignature *root_signature;
    D3D12_RESOURCE_DESC resource_desc;
    struct test_context context;
    ID3D12Resource *buffer, *cb;
    struct resource_readback rb;
    ID3D12CommandQueue *queue;
    unsigned int i;
    uint32_t value;
    HRESULT hr;

    static const uint64_t predicate_args[] = {0, 1, (uint64_t)1 << 32};
    static const uint32_t r8g8b8a8_data[] = {0x28384858, 0x39495969};
    static const D3D12_DRAW_ARGUMENTS draw_args = {3, 1, 0, 0};
    static const float white[] = {1.0f, 1.0f, 1.0f, 1.0f};
    static const float green[] = {0.0f, 1.0f, 0.0f, 1.0f};
    static const float ms_color[] = {0.345f, 0.282f, 0.219f, 0.156f};
    static const uint32_t init_value = 0xdeadbeef;
    static const D3D12_SUBRESOURCE_DATA copy_data[] =
    {
        {&r8g8b8a8_data[0], sizeof(r8g8b8a8_data[0]), sizeof(r8g8b8a8_data[0])},
        {&r8g8b8a8_data[1], sizeof(r8g8b8a8_data[1]), sizeof(r8g8b8a8_data[1])}
    };
    static const DWORD cs_code[] =
    {
#if 0
        cbuffer cb
        {
            unsigned int offset;
            unsigned int value;
        };

        RWByteAddressBuffer b;

        [numthreads(1, 1, 1)]
        void main()
        {
            b.Store(4 * offset, value);
        }
#endif
        0x43425844, 0xaadc5460, 0x88c27e90, 0x2acacf4e, 0x4e06019a, 0x00000001, 0x000000d8, 0x00000003,
        0x0000002c, 0x0000003c, 0x0000004c, 0x4e475349, 0x00000008, 0x00000000, 0x00000008, 0x4e47534f,
        0x00000008, 0x00000000, 0x00000008, 0x58454853, 0x00000084, 0x00050050, 0x00000021, 0x0100086a,
        0x04000059, 0x00208e46, 0x00000000, 0x00000001, 0x0300009d, 0x0011e000, 0x00000000, 0x02000068,
        0x00000001, 0x0400009b, 0x00000001, 0x00000001, 0x00000001, 0x08000029, 0x00100012, 0x00000000,
        0x0020800a, 0x00000000, 0x00000000, 0x00004001, 0x00000002, 0x080000a6, 0x0011e012, 0x00000000,
        0x0010000a, 0x00000000, 0x0020801a, 0x00000000, 0x00000000, 0x0100003e,
    };
    static const struct
    {
        uint32_t offset;
        uint32_t value;
        uint32_t uav_offset;
    }
    input = {0, 4, 0};

    if (!init_test_context(&context, NULL))
        return;
    command_list = context.list;
    queue = context.queue;

    if (is_intel_windows_device(context.device))
    {
        skip("Predicated rendering is broken on Intel.\n");
        destroy_test_context(&context);
        return;
    }

    conditions = create_default_buffer(context.device, sizeof(predicate_args),
            D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
    upload_buffer_data(conditions, 0, sizeof(predicate_args), &predicate_args, queue, command_list);
    reset_command_list(command_list, context.allocator);
    transition_resource_state(command_list, conditions,
            D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PREDICATION);

    ID3D12GraphicsCommandList_ClearRenderTargetView(command_list, context.rtv, white, 0, NULL);

    /* Skip draw on zero. */
    prepare_instanced_draw(&context);
    ID3D12GraphicsCommandList_SetPredication(command_list, conditions, 0, D3D12_PREDICATION_OP_EQUAL_ZERO);
    ID3D12GraphicsCommandList_DrawInstanced(command_list, 3, 1, 0, 0);
    ID3D12GraphicsCommandList_SetPredication(command_list, NULL, 0, 0);

    transition_resource_state(command_list, context.render_target,
            D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);

    check_sub_resource_uint(context.render_target, 0, queue, command_list, 0xffffffff, 0);

    reset_command_list(command_list, context.allocator);
    transition_resource_state(command_list, context.render_target,
            D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);

    /* Skip draw on non-zero. */
    prepare_instanced_draw(&context);
    ID3D12GraphicsCommandList_SetPredication(command_list, conditions,
            sizeof(uint64_t), D3D12_PREDICATION_OP_NOT_EQUAL_ZERO);
    ID3D12GraphicsCommandList_DrawInstanced(command_list, 3, 1, 0, 0);
    /* Don't reset predication to test automatic reset on next SetPredication() call. */

    transition_resource_state(command_list, context.render_target,
            D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);

    ID3D12GraphicsCommandList_SetPredication(command_list, conditions,
            sizeof(uint64_t), D3D12_PREDICATION_OP_EQUAL_ZERO);
    check_sub_resource_uint(context.render_target, 0, queue, command_list, 0xffffffff, 0);

    reset_command_list(command_list, context.allocator);
    transition_resource_state(command_list, context.render_target,
            D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);

    /* Skip clear on zero. */
    ID3D12GraphicsCommandList_ClearRenderTargetView(command_list, context.rtv, white, 0, NULL);
    ID3D12GraphicsCommandList_SetPredication(command_list, conditions, 0, D3D12_PREDICATION_OP_EQUAL_ZERO);
    ID3D12GraphicsCommandList_ClearRenderTargetView(command_list, context.rtv, green, 0, NULL);
    ID3D12GraphicsCommandList_SetPredication(command_list, NULL, 0, 0);

    transition_resource_state(command_list, context.render_target,
            D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);

    get_texture_readback_with_command_list(context.render_target, 0, &rb, queue, command_list);
    todo check_readback_data_uint(&rb, NULL, 0xffffffff, 0);
    release_resource_readback(&rb);

    reset_command_list(command_list, context.allocator);
    transition_resource_state(command_list, context.render_target,
            D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);

    /* Draw on zero. */
    ID3D12GraphicsCommandList_ClearRenderTargetView(command_list, context.rtv, white, 0, NULL);
    prepare_instanced_draw(&context);
    ID3D12GraphicsCommandList_SetPredication(command_list, conditions, 0, D3D12_PREDICATION_OP_NOT_EQUAL_ZERO);
    ID3D12GraphicsCommandList_DrawInstanced(command_list, 3, 1, 0, 0);
    ID3D12GraphicsCommandList_SetPredication(command_list, NULL, 0, 0);

    transition_resource_state(command_list, context.render_target,
            D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);

    check_sub_resource_uint(context.render_target, 0, queue, command_list, 0xff00ff00, 0);

    reset_command_list(command_list, context.allocator);
    transition_resource_state(command_list, context.render_target,
            D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);

    /* Draw on non-zero. */
    ID3D12GraphicsCommandList_ClearRenderTargetView(command_list, context.rtv, white, 0, NULL);
    prepare_instanced_draw(&context);
    ID3D12GraphicsCommandList_SetPredication(command_list, conditions,
            sizeof(uint64_t), D3D12_PREDICATION_OP_EQUAL_ZERO);
    ID3D12GraphicsCommandList_DrawInstanced(command_list, 3, 1, 0, 0);
    ID3D12GraphicsCommandList_SetPredication(command_list, NULL, 0, 0);

    transition_resource_state(command_list, context.render_target,
            D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);

    check_sub_resource_uint(context.render_target, 0, queue, command_list, 0xff00ff00, 0);

    reset_command_list(command_list, context.allocator);
    transition_resource_state(command_list, context.render_target,
            D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);

    /* 64-bit conditional 0x100000000 */
    ID3D12GraphicsCommandList_ClearRenderTargetView(command_list, context.rtv, white, 0, NULL);
    prepare_instanced_draw(&context);
    ID3D12GraphicsCommandList_SetPredication(command_list, conditions,
            2 * sizeof(uint64_t), D3D12_PREDICATION_OP_NOT_EQUAL_ZERO);
    ID3D12GraphicsCommandList_DrawInstanced(command_list, 3, 1, 0, 0);
    ID3D12GraphicsCommandList_SetPredication(command_list, NULL, 0, 0);

    transition_resource_state(command_list, context.render_target,
            D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);

    get_texture_readback_with_command_list(context.render_target, 0, &rb, queue, command_list);
    check_readback_data_uint(&rb, NULL, 0xffffffff, 0);
    release_resource_readback(&rb);
    reset_command_list(command_list, context.allocator);
    transition_resource_state(command_list, context.render_target,
            D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);

    /* Direct3D latches the value of the predicate upon beginning predicated rendering. */
    buffer = create_default_buffer(context.device, sizeof(predicate_args),
            D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
    transition_resource_state(command_list, conditions,
            D3D12_RESOURCE_STATE_PREDICATION, D3D12_RESOURCE_STATE_COPY_SOURCE);
    ID3D12GraphicsCommandList_CopyResource(command_list, buffer, conditions);
    transition_resource_state(command_list,
            buffer, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PREDICATION);

    ID3D12GraphicsCommandList_ClearRenderTargetView(command_list, context.rtv, white, 0, NULL);
    prepare_instanced_draw(&context);
    ID3D12GraphicsCommandList_SetPredication(command_list, buffer, 0, D3D12_PREDICATION_OP_NOT_EQUAL_ZERO);

    transition_resource_state(command_list, buffer,
            D3D12_RESOURCE_STATE_PREDICATION, D3D12_RESOURCE_STATE_COPY_DEST);
    ID3D12GraphicsCommandList_CopyBufferRegion(command_list, buffer, 0, conditions, sizeof(uint64_t), sizeof(uint64_t));
    transition_resource_state(command_list,
            buffer, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PREDICATION);
    transition_resource_state(command_list,
            conditions, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_PREDICATION);

    ID3D12GraphicsCommandList_DrawInstanced(command_list, 3, 1, 0, 0);
    ID3D12GraphicsCommandList_SetPredication(command_list, NULL, 0, 0);

    transition_resource_state(command_list, context.render_target,
            D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
    check_sub_resource_uint(context.render_target, 0, queue, command_list, 0xff00ff00, 0);

    ID3D12Resource_Release(buffer);
    reset_command_list(command_list, context.allocator);
    transition_resource_state(command_list, context.render_target,
            D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);

    /* SetPredication() and upload buffer. */
    upload_buffer = create_upload_buffer(context.device, sizeof(predicate_args), predicate_args);

    ID3D12GraphicsCommandList_ClearRenderTargetView(command_list, context.rtv, white, 0, NULL);
    prepare_instanced_draw(&context);
    /* Skip. */
    ID3D12GraphicsCommandList_SetPredication(command_list, upload_buffer,
            0, D3D12_PREDICATION_OP_EQUAL_ZERO);
    ID3D12GraphicsCommandList_DrawInstanced(command_list, 3, 1, 0, 0);

    ID3D12GraphicsCommandList_SetPredication(command_list, upload_buffer,
            0, D3D12_PREDICATION_OP_NOT_EQUAL_ZERO);
    transition_resource_state(command_list, context.render_target,
            D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
    check_sub_resource_uint(context.render_target, 0, queue, command_list, 0xffffffff, 0);

    reset_command_list(command_list, context.allocator);
    transition_resource_state(command_list, context.render_target,
            D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);

    /* ExecuteIndirect(). */
    buffer = create_upload_buffer(context.device, sizeof(draw_args), &draw_args);

    command_signature = create_command_signature(context.device, D3D12_INDIRECT_ARGUMENT_TYPE_DRAW);

    ID3D12GraphicsCommandList_ClearRenderTargetView(command_list, context.rtv, white, 0, NULL);
    prepare_instanced_draw(&context);
    /* Skip. */
    ID3D12GraphicsCommandList_SetPredication(command_list, conditions, 0, D3D12_PREDICATION_OP_EQUAL_ZERO);
    ID3D12GraphicsCommandList_ExecuteIndirect(command_list, command_signature, 1, buffer, 0, NULL, 0);
    ID3D12GraphicsCommandList_SetPredication(command_list, NULL, 0, 0);

    transition_resource_state(command_list, context.render_target,
            D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);

    check_sub_resource_uint(context.render_target, 0, queue, command_list, 0xffffffff, 0);

    reset_command_list(command_list, context.allocator);
    transition_resource_state(command_list, context.render_target,
            D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);

    prepare_instanced_draw(&context);
    /* Draw. */
    ID3D12GraphicsCommandList_SetPredication(command_list, conditions, 0, D3D12_PREDICATION_OP_NOT_EQUAL_ZERO);
    ID3D12GraphicsCommandList_ExecuteIndirect(command_list, command_signature, 1, buffer, 0, NULL, 0);
    ID3D12GraphicsCommandList_SetPredication(command_list, NULL, 0, 0);

    transition_resource_state(command_list, context.render_target,
            D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);

    check_sub_resource_uint(context.render_target, 0, queue, command_list, 0xff00ff00, 0);

    ID3D12Resource_Release(buffer);
    ID3D12CommandSignature_Release(command_signature);
    reset_command_list(command_list, context.allocator);

    /* CopyResource(). */
    texture = create_default_texture(context.device,
            1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, 0, D3D12_RESOURCE_STATE_COPY_DEST);
    upload_texture_data(texture, &copy_data[0], 1, queue, command_list);
    reset_command_list(command_list, context.allocator);
    transition_resource_state(command_list, texture,
            D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COPY_SOURCE);

    texture_copy = create_default_texture(context.device,
                1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, 0, D3D12_RESOURCE_STATE_COPY_DEST);
    upload_texture_data(texture_copy, &copy_data[1], 1, queue, command_list);
    reset_command_list(command_list, context.allocator);

    /* Skip. */
    ID3D12GraphicsCommandList_SetPredication(command_list, conditions, 0, D3D12_PREDICATION_OP_EQUAL_ZERO);
    ID3D12GraphicsCommandList_CopyResource(command_list, texture_copy, texture);
    ID3D12GraphicsCommandList_SetPredication(command_list, NULL, 0, 0);

    transition_resource_state(command_list, texture_copy,
            D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COPY_SOURCE);

    get_texture_readback_with_command_list(texture_copy, 0, &rb, queue, command_list);
    todo check_readback_data_uint(&rb, NULL, r8g8b8a8_data[1], 0);
    release_resource_readback(&rb);

    reset_command_list(command_list, context.allocator);
    transition_resource_state(command_list, texture_copy,
            D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COPY_DEST);

    /* Copy. */
    ID3D12GraphicsCommandList_SetPredication(command_list, conditions, 0, D3D12_PREDICATION_OP_NOT_EQUAL_ZERO);
    ID3D12GraphicsCommandList_CopyResource(command_list, texture_copy, texture);
    ID3D12GraphicsCommandList_SetPredication(command_list, NULL, 0, 0);

    transition_resource_state(command_list, texture_copy,
            D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COPY_SOURCE);

    check_sub_resource_uint(texture_copy, 0, queue, command_list, r8g8b8a8_data[0], 0);

    /* Multisample texture. */
    ID3D12Resource_Release(texture);
    memset(&heap_properties, 0, sizeof(heap_properties));
    heap_properties.Type = D3D12_HEAP_TYPE_DEFAULT;

    memset(&resource_desc, 0, sizeof(resource_desc));
    resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    resource_desc.Width = 1;
    resource_desc.Height = 1;
    resource_desc.DepthOrArraySize = 1;
    resource_desc.MipLevels = 1;
    resource_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    resource_desc.SampleDesc.Count = 4;
    resource_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    hr = ID3D12Device_CreateCommittedResource(context.device, &heap_properties, D3D12_HEAP_FLAG_NONE,
            &resource_desc, D3D12_RESOURCE_STATE_RENDER_TARGET, NULL, &IID_ID3D12Resource, (void **)&texture);
    ok(hr == S_OK, "Failed to create texture, hr %#x.\n", hr);

    memset(&rtv_desc, 0, sizeof(rtv_desc));
    rtv_desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DMS;
    rtv_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    ID3D12Device_CreateRenderTargetView(context.device, texture, &rtv_desc,
            get_cpu_rtv_handle(&context, context.rtv_heap, 0));

    reset_command_list(command_list, context.allocator);
    ID3D12GraphicsCommandList_ClearRenderTargetView(command_list, context.rtv, ms_color, 0, NULL);

    /* ResolveSubresource(). */
    transition_resource_state(command_list, texture_copy,
            D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
    upload_texture_data(texture_copy, &copy_data[1], 1, queue, command_list);

    reset_command_list(command_list, context.allocator);
    transition_resource_state(command_list, texture_copy,
            D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_RESOLVE_DEST);
    transition_resource_state(command_list, texture,
            D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_RESOLVE_SOURCE);

    /* Skip. */
    ID3D12GraphicsCommandList_SetPredication(command_list, conditions, 0, D3D12_PREDICATION_OP_EQUAL_ZERO);
    ID3D12GraphicsCommandList_ResolveSubresource(command_list,
            texture_copy, 0, texture, 0, DXGI_FORMAT_R8G8B8A8_UNORM);
    ID3D12GraphicsCommandList_SetPredication(command_list, NULL, 0, 0);

    transition_resource_state(command_list, texture_copy,
            D3D12_RESOURCE_STATE_RESOLVE_DEST, D3D12_RESOURCE_STATE_COPY_SOURCE);

    get_texture_readback_with_command_list(texture_copy, 0, &rb, queue, command_list);
    todo check_readback_data_uint(&rb, NULL, r8g8b8a8_data[1], 0);
    release_resource_readback(&rb);

    reset_command_list(command_list, context.allocator);
    transition_resource_state(command_list, texture_copy,
            D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RESOLVE_DEST);

    /* Resolve. */
    ID3D12GraphicsCommandList_SetPredication(command_list, conditions, 0, D3D12_PREDICATION_OP_NOT_EQUAL_ZERO);
    ID3D12GraphicsCommandList_ResolveSubresource(command_list,
            texture_copy, 0, texture, 0, DXGI_FORMAT_R8G8B8A8_UNORM);
    ID3D12GraphicsCommandList_SetPredication(command_list, NULL, 0, 0);

    transition_resource_state(command_list, texture_copy,
            D3D12_RESOURCE_STATE_RESOLVE_DEST, D3D12_RESOURCE_STATE_COPY_SOURCE);

    check_sub_resource_uint(texture_copy, 0, queue, command_list, r8g8b8a8_data[0], 2);

    reset_command_list(command_list, context.allocator);

    /* Dispatch(). */
    cb = create_upload_buffer(context.device, sizeof(input), &input);

    buffer = create_default_buffer(context.device, 512,
            D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_DEST);
    upload_buffer_data(buffer, 0, sizeof(init_value), &init_value, queue, command_list);
    reset_command_list(command_list, context.allocator);
    transition_sub_resource_state(command_list, buffer, 0,
            D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    root_parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    root_parameters[0].Descriptor.ShaderRegister = 0;
    root_parameters[0].Descriptor.RegisterSpace = 0;
    root_parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    root_parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    root_parameters[1].Descriptor.ShaderRegister = 0;
    root_parameters[1].Descriptor.RegisterSpace = 0;
    root_parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    memset(&root_signature_desc, 0, sizeof(root_signature_desc));
    root_signature_desc.NumParameters = 2;
    root_signature_desc.pParameters = root_parameters;
    hr = create_root_signature(context.device, &root_signature_desc, &root_signature);
    ok(hr == S_OK, "Failed to create root signature, hr %#x.\n", hr);

    pipeline_state = create_compute_pipeline_state(context.device, root_signature,
            shader_bytecode(cs_code, sizeof(cs_code)));

    for (i = 0; i < 2; ++i)
    {
        ID3D12GraphicsCommandList_SetPipelineState(command_list, pipeline_state);
        ID3D12GraphicsCommandList_SetComputeRootSignature(command_list, root_signature);
        ID3D12GraphicsCommandList_SetComputeRootConstantBufferView(command_list,
                0, ID3D12Resource_GetGPUVirtualAddress(cb));
        ID3D12GraphicsCommandList_SetComputeRootUnorderedAccessView(command_list,
                1, ID3D12Resource_GetGPUVirtualAddress(buffer));
        ID3D12GraphicsCommandList_SetPredication(command_list, conditions, i * sizeof(uint64_t),
                D3D12_PREDICATION_OP_EQUAL_ZERO);
        ID3D12GraphicsCommandList_Dispatch(command_list, 1, 1, 1);
        ID3D12GraphicsCommandList_SetPredication(command_list, NULL, 0, 0);

        transition_sub_resource_state(command_list, buffer, 0,
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);

        get_buffer_readback_with_command_list(buffer, DXGI_FORMAT_R32_UINT, &rb, queue, command_list);
        value = get_readback_uint(&rb, 0, 0, 0);
        ok(value == (!i ? init_value : input.value), "Got %#x, expected %#x.\n", value, input.value);
        release_resource_readback(&rb);
        reset_command_list(command_list, context.allocator);

        transition_sub_resource_state(command_list, buffer, 0,
                D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    }

    ID3D12Resource_Release(texture);
    ID3D12Resource_Release(texture_copy);
    ID3D12Resource_Release(conditions);
    ID3D12Resource_Release(cb);
    ID3D12Resource_Release(buffer);
    ID3D12Resource_Release(upload_buffer);
    ID3D12RootSignature_Release(root_signature);
    ID3D12PipelineState_Release(pipeline_state);
    destroy_test_context(&context);
}

void test_write_buffer_immediate(void)
{
    D3D12_WRITEBUFFERIMMEDIATE_PARAMETER parameters[2];
    ID3D12GraphicsCommandList2 *command_list2;
    D3D12_WRITEBUFFERIMMEDIATE_MODE modes[2];
    ID3D12GraphicsCommandList *command_list;
    struct resource_readback rb;
    struct test_context context;
    ID3D12CommandQueue *queue;
    ID3D12Resource *buffer;
    ID3D12Device *device;
    unsigned int value;
    HRESULT hr;

    static const unsigned int data_values[] = {0xdeadbeef, 0xf00baa};

    if (!init_test_context(&context, NULL))
        return;
    device = context.device;
    command_list = context.list;
    queue = context.queue;

    if (FAILED(hr = ID3D12GraphicsCommandList_QueryInterface(command_list,
            &IID_ID3D12GraphicsCommandList2, (void **)&command_list2)))
    {
        skip("ID3D12GraphicsCommandList2 not implemented.\n");
        destroy_test_context(&context);
        return;
    }

    buffer = create_default_buffer(device, sizeof(data_values),
            D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
    upload_buffer_data(buffer, 0, sizeof(data_values), data_values, queue, command_list);
    reset_command_list(command_list, context.allocator);

    parameters[0].Dest = ID3D12Resource_GetGPUVirtualAddress(buffer);
    parameters[0].Value = 0x1020304;
    parameters[1].Dest = parameters[0].Dest + sizeof(data_values[0]);
    parameters[1].Value = 0xc0d0e0f;
    ID3D12GraphicsCommandList2_WriteBufferImmediate(command_list2, ARRAY_SIZE(parameters), parameters, NULL);
    hr = ID3D12GraphicsCommandList_Close(command_list);
    ok(hr == S_OK, "Got unexpected hr %#x.\n", hr);
    exec_command_list(queue, command_list);
    wait_queue_idle(device, queue);
    reset_command_list(command_list, context.allocator);

    get_buffer_readback_with_command_list(buffer, DXGI_FORMAT_R32_UINT, &rb, queue, command_list);
    value = get_readback_uint(&rb, 0, 0, 0);
    ok(value == parameters[0].Value, "Got unexpected value %#x, expected %#x.\n", value, parameters[0].Value);
    value = get_readback_uint(&rb, 1, 0, 0);
    ok(value == parameters[1].Value, "Got unexpected value %#x, expected %#x.\n", value, parameters[1].Value);
    release_resource_readback(&rb);
    reset_command_list(command_list, context.allocator);

    parameters[0].Value = 0x2030405;
    parameters[1].Value = 0xb0c0d0e;
    modes[0] = D3D12_WRITEBUFFERIMMEDIATE_MODE_MARKER_IN;
    modes[1] = D3D12_WRITEBUFFERIMMEDIATE_MODE_MARKER_OUT;
    ID3D12GraphicsCommandList2_WriteBufferImmediate(command_list2, ARRAY_SIZE(parameters), parameters, modes);
    hr = ID3D12GraphicsCommandList_Close(command_list);
    ok(hr == S_OK, "Got unexpected hr %#x.\n", hr);
    exec_command_list(queue, command_list);
    wait_queue_idle(device, queue);
    reset_command_list(command_list, context.allocator);

    get_buffer_readback_with_command_list(buffer, DXGI_FORMAT_R32_UINT, &rb, queue, command_list);
    value = get_readback_uint(&rb, 0, 0, 0);
    ok(value == parameters[0].Value, "Got unexpected value %#x, expected %#x.\n", value, parameters[0].Value);
    value = get_readback_uint(&rb, 1, 0, 0);
    ok(value == parameters[1].Value, "Got unexpected value %#x, expected %#x.\n", value, parameters[1].Value);
    release_resource_readback(&rb);
    reset_command_list(command_list, context.allocator);

    modes[0] = 0x7fffffff;
    ID3D12GraphicsCommandList2_WriteBufferImmediate(command_list2, ARRAY_SIZE(parameters), parameters, modes);
    hr = ID3D12GraphicsCommandList_Close(command_list);
    ok(hr == E_INVALIDARG, "Got unexpected hr %#x.\n", hr);

    ID3D12Resource_Release(buffer);
    ID3D12GraphicsCommandList2_Release(command_list2);
    destroy_test_context(&context);
}

void test_aliasing_barrier(void)
{
    /* This test mostly serves to verify that validation is clean,
     * and that we don't crash on weird inputs. There is no particular output we expect to see. */
    ID3D12GraphicsCommandList *command_list;
    D3D12_FEATURE_DATA_D3D12_OPTIONS opts;
    D3D12_RESOURCE_BARRIER barriers[256];
    ID3D12Resource *placed_textures[3];
    ID3D12Resource *committed_texture;
    ID3D12Resource *placed_buffers[3];
    D3D12_RESOURCE_DESC texture_desc;
    ID3D12Resource *committed_buffer;
    struct test_context_desc desc;
    struct test_context context;
    D3D12_HEAP_DESC heap_desc;
    bool supports_heap_tier_2;
    ID3D12Heap *buffer_heap;
    ID3D12Heap *texture_heap;
    ID3D12Heap *common_heap;
    ID3D12Device *device;
    unsigned int i;
    HRESULT hr;

    memset(&desc, 0, sizeof(desc));
    desc.no_render_target = true;
    if (!init_test_context(&context, &desc))
        return;
    device = context.device;
    command_list = context.list;

    committed_texture = create_default_texture(context.device, 1, 1, DXGI_FORMAT_R32_SINT,
            D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COMMON);
    committed_buffer = create_default_buffer(device, 1,
            D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COMMON);

    ID3D12Device_CheckFeatureSupport(device, D3D12_FEATURE_D3D12_OPTIONS, &opts, sizeof(opts));
    supports_heap_tier_2 = opts.ResourceHeapTier >= D3D12_RESOURCE_HEAP_TIER_2;

    memset(&heap_desc, 0, sizeof(heap_desc));
    heap_desc.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
    heap_desc.Properties.Type = D3D12_HEAP_TYPE_DEFAULT;
    heap_desc.SizeInBytes = 1024 * 1024;

    heap_desc.Flags = D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS;
    hr = ID3D12Device_CreateHeap(device, &heap_desc, &IID_ID3D12Heap, (void**)&buffer_heap);
    ok(hr == S_OK, "Failed to create buffer heap hr #%u.\n", hr);
    heap_desc.Flags = D3D12_HEAP_FLAG_ALLOW_ONLY_NON_RT_DS_TEXTURES;
    hr = ID3D12Device_CreateHeap(device, &heap_desc, &IID_ID3D12Heap, (void **)&texture_heap);
    ok(hr == S_OK, "Failed to create buffer heap hr #%u.\n", hr);

    if (supports_heap_tier_2)
    {
        heap_desc.Flags = D3D12_HEAP_FLAG_ALLOW_ALL_BUFFERS_AND_TEXTURES;
        hr = ID3D12Device_CreateHeap(device, &heap_desc, &IID_ID3D12Heap, (void **)&common_heap);
        ok(hr == S_OK, "Failed to create buffer heap hr #%u.\n", hr);
    }
    else
        common_heap = NULL;

    texture_desc.Format = DXGI_FORMAT_R32_SINT;
    texture_desc.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
    texture_desc.Width = 1;
    texture_desc.Height = 1;
    texture_desc.DepthOrArraySize = 1;
    texture_desc.MipLevels = 1;
    texture_desc.SampleDesc.Count = 1;
    texture_desc.SampleDesc.Quality = 0;
    texture_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texture_desc.Flags = D3D12_RESOURCE_FLAG_NONE;
    texture_desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;

    for (i = 0; i < 2; i++)
    {
        placed_buffers[i] = create_placed_buffer(device, buffer_heap, 0, 1, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COMMON);
        hr = ID3D12Device_CreatePlacedResource(device, texture_heap, 0, &texture_desc, D3D12_RESOURCE_STATE_COMMON, NULL, &IID_ID3D12Resource, (void **)&placed_textures[i]);
        ok(hr == S_OK, "Failed to create placed resource. hr = #%u.\n", hr);
    }

    placed_buffers[2] = create_placed_buffer(device, supports_heap_tier_2 ? common_heap : buffer_heap, 0, 1, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COMMON);
    hr = ID3D12Device_CreatePlacedResource(device, supports_heap_tier_2 ? common_heap : texture_heap, 0, &texture_desc, D3D12_RESOURCE_STATE_COMMON, NULL, &IID_ID3D12Resource, (void **)&placed_textures[2]);
    ok(hr == S_OK, "Failed to create placed resource. hr = #%u.\n", hr);

    for (i = 0; i < ARRAY_SIZE(barriers); i++)
    {
        barriers[i].Type = D3D12_RESOURCE_BARRIER_TYPE_ALIASING;
        barriers[i].Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    }

    /* Full barrier */
    barriers[0].Aliasing.pResourceBefore = NULL;
    barriers[0].Aliasing.pResourceAfter = NULL;
    ID3D12GraphicsCommandList_ResourceBarrier(command_list, 1, barriers);

    /* NULL to buffer */
    barriers[0].Aliasing.pResourceBefore = NULL;
    barriers[0].Aliasing.pResourceAfter = placed_buffers[0];
    ID3D12GraphicsCommandList_ResourceBarrier(command_list, 1, barriers);

    /* NULL to image */
    barriers[0].Aliasing.pResourceBefore = NULL;
    barriers[0].Aliasing.pResourceAfter = placed_textures[0];
    ID3D12GraphicsCommandList_ResourceBarrier(command_list, 1, barriers);

    /* buffer to NULL */
    barriers[0].Aliasing.pResourceBefore = placed_buffers[0];
    barriers[0].Aliasing.pResourceAfter = NULL;
    ID3D12GraphicsCommandList_ResourceBarrier(command_list, 1, barriers);

    /* NULL to image */
    barriers[0].Aliasing.pResourceBefore = placed_textures[0];
    barriers[0].Aliasing.pResourceAfter = NULL;
    ID3D12GraphicsCommandList_ResourceBarrier(command_list, 1, barriers);

    /* buffer to buffer */
    barriers[0].Aliasing.pResourceBefore = placed_buffers[0];
    barriers[0].Aliasing.pResourceAfter = placed_buffers[1];
    ID3D12GraphicsCommandList_ResourceBarrier(command_list, 1, barriers);

    /* image to image */
    barriers[0].Aliasing.pResourceBefore = placed_textures[0];
    barriers[0].Aliasing.pResourceAfter = placed_textures[1];
    ID3D12GraphicsCommandList_ResourceBarrier(command_list, 1, barriers);

    /* buffer to image */
    if (supports_heap_tier_2)
    {
        barriers[0].Aliasing.pResourceBefore = placed_buffers[2];
        barriers[0].Aliasing.pResourceAfter = placed_textures[2];
        ID3D12GraphicsCommandList_ResourceBarrier(command_list, 1, barriers);
    }

    /* Test spamming lots of redundant image barriers. */
    for (i = 0; i < ARRAY_SIZE(barriers); i++)
    {
        barriers[i].Aliasing.pResourceBefore = NULL;
        barriers[i].Aliasing.pResourceAfter = placed_textures[i % 3];
    }
    ID3D12GraphicsCommandList_ResourceBarrier(command_list, ARRAY_SIZE(barriers), barriers);

    ID3D12Resource_Release(committed_texture);
    ID3D12Resource_Release(committed_buffer);
    for (i = 0; i < 3; i++)
    {
        ID3D12Resource_Release(placed_textures[i]);
        ID3D12Resource_Release(placed_buffers[i]);
    }
    ID3D12Heap_Release(buffer_heap);
    ID3D12Heap_Release(texture_heap);
    if (common_heap)
        ID3D12Heap_Release(common_heap);
    destroy_test_context(&context);
}

static void test_discard_resource_uav_type(bool compute_queue)
{
    static const float white[] = { 1.0f, 1.0f, 1.0f, 1.0f };
    D3D12_UNORDERED_ACCESS_VIEW_DESC uav;
    struct test_context context;
    ID3D12DescriptorHeap *gpu;
    ID3D12DescriptorHeap *cpu;
    ID3D12Resource *resource;

    if (compute_queue)
    {
        /* Creates a COMPUTE list instead of DIRECT. */
        if (!init_compute_test_context(&context))
            return;
    }
    else
    {
        if (!init_test_context(&context, NULL))
            return;
    }

    /* In compute lists, we can discard UAV enabled resources,
     * and the resource must be in UAV state. */

    resource = create_default_texture2d(context.device, 4, 4, 1, 1, DXGI_FORMAT_R32_FLOAT,
            D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    gpu = create_gpu_descriptor_heap(context.device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1);
    cpu = create_cpu_descriptor_heap(context.device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1);

    memset(&uav, 0, sizeof(uav));
    uav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    uav.Format = DXGI_FORMAT_R32_FLOAT;
    ID3D12Device_CreateUnorderedAccessView(context.device, resource, NULL, &uav,
            ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(gpu));
    ID3D12Device_CreateUnorderedAccessView(context.device, resource, NULL, &uav,
            ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(cpu));

    ID3D12GraphicsCommandList_DiscardResource(context.list, resource, NULL);
    ID3D12GraphicsCommandList_ClearUnorderedAccessViewFloat(context.list,
            ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(gpu),
            ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(cpu),
            resource, white, 0, NULL);

    transition_resource_state(context.list, resource,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
    check_sub_resource_float(resource, 0, context.queue, context.list, 1.0f, 0);

    ID3D12DescriptorHeap_Release(gpu);
    ID3D12DescriptorHeap_Release(cpu);
    ID3D12Resource_Release(resource);
    destroy_test_context(&context);
}

void test_discard_resource_uav(void)
{
    vkd3d_test_set_context("Test graphics");
    test_discard_resource_uav_type(false);
    vkd3d_test_set_context("Test compute");
    test_discard_resource_uav_type(true);
}

void test_discard_resource(void)
{
    ID3D12GraphicsCommandList *command_list;
    D3D12_HEAP_PROPERTIES heap_properties;
    D3D12_CPU_DESCRIPTOR_HANDLE rtv, dsv;
    D3D12_RESOURCE_DESC resource_desc;
    ID3D12DescriptorHeap *rtv_heap;
    ID3D12DescriptorHeap *dsv_heap;
    D3D12_DISCARD_REGION ds_region;
    D3D12_DISCARD_REGION region;
    struct test_context context;
    ID3D12Resource *tmp_depth;
    ID3D12Resource *depth_rt;
    ID3D12Device *device;
    ID3D12Resource *rt;
    HRESULT hr;

    const float clear_color[] = { 1.0f, 0.0f, 0.0f, 0.0f };

    if (!init_test_context(&context, NULL))
        return;
    device = context.device;
    command_list = context.list;

    memset(&heap_properties, 0, sizeof(heap_properties));
    heap_properties.Type = D3D12_HEAP_TYPE_DEFAULT;

    resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    resource_desc.Alignment = 0;
    resource_desc.Width = 16;
    resource_desc.Height = 16;
    resource_desc.DepthOrArraySize = 2;
    resource_desc.MipLevels = 1;
    resource_desc.SampleDesc.Count = 1;
    resource_desc.SampleDesc.Quality = 0;
    resource_desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    resource_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    resource_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    hr = ID3D12Device_CreateCommittedResource(device, &heap_properties, D3D12_HEAP_FLAG_NONE,
            &resource_desc, D3D12_RESOURCE_STATE_RENDER_TARGET, NULL, &IID_ID3D12Resource, (void **)&rt);
    ok(SUCCEEDED(hr), "Failed to create texture, hr %#x.\n", hr);

    resource_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    resource_desc.Format = DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
    hr = ID3D12Device_CreateCommittedResource(device, &heap_properties, D3D12_HEAP_FLAG_NONE,
        &resource_desc, D3D12_RESOURCE_STATE_DEPTH_WRITE, NULL, &IID_ID3D12Resource, (void **)&depth_rt);
    ok(SUCCEEDED(hr), "Failed to create texture, hr %#x.\n", hr);

    resource_desc.Flags = D3D12_RESOURCE_FLAG_NONE;
    resource_desc.Format = DXGI_FORMAT_R32_FLOAT;
    resource_desc.DepthOrArraySize = 1;
    hr = ID3D12Device_CreateCommittedResource(device, &heap_properties, D3D12_HEAP_FLAG_NONE,
        &resource_desc, D3D12_RESOURCE_STATE_COPY_DEST, NULL, &IID_ID3D12Resource, (void **)&tmp_depth);
    ok(SUCCEEDED(hr), "Failed to create texture, hr %#x.\n", hr);

    region.NumRects = 0;
    region.pRects = NULL;
    region.FirstSubresource = 0;
    region.NumSubresources = 2;

    ds_region = region;
    ds_region.NumSubresources = 4;

    ID3D12GraphicsCommandList_DiscardResource(context.list, rt, &region);
    ID3D12GraphicsCommandList_DiscardResource(context.list, depth_rt, &ds_region);

    rtv_heap = create_cpu_descriptor_heap(device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1);
    dsv_heap = create_cpu_descriptor_heap(device, D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 1);
    rtv = ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(rtv_heap);
    dsv = ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(dsv_heap);

    ID3D12Device_CreateRenderTargetView(device, rt, NULL, rtv);
    ID3D12Device_CreateDepthStencilView(device, depth_rt, NULL, dsv);

    ID3D12GraphicsCommandList_OMSetRenderTargets(command_list, 1, &rtv, false, &dsv);
    ID3D12GraphicsCommandList_DiscardResource(context.list, rt, &region);
    ID3D12GraphicsCommandList_DiscardResource(context.list, depth_rt, &ds_region);

    /* Just make sure we don't have validation errors */
    hr = ID3D12GraphicsCommandList_Close(context.list);
    ok(hr == S_OK, "Failed to close command list, hr %#x.\n", hr);
    hr = ID3D12GraphicsCommandList_Reset(context.list, context.allocator, NULL);
    ok(hr == S_OK, "Failed to reset command list, hr %#x.\n", hr);

    ID3D12GraphicsCommandList_OMSetRenderTargets(command_list, 1, &rtv, false, &dsv);
    ID3D12GraphicsCommandList_DiscardResource(context.list, rt, &region);
    ID3D12GraphicsCommandList_DiscardResource(context.list, depth_rt, &ds_region);
    ID3D12GraphicsCommandList_ClearRenderTargetView(command_list, rtv, clear_color, 0, NULL);
    ID3D12GraphicsCommandList_ClearDepthStencilView(command_list, dsv, D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 128, 0, NULL);

    region.FirstSubresource = 1;
    region.NumSubresources = 1;
    ID3D12GraphicsCommandList_DiscardResource(context.list, rt, &region);

    /* Discard stencil aspect and mip 1 of depth aspect. */
    ds_region.FirstSubresource = 1;
    ds_region.NumSubresources = 3;
    ID3D12GraphicsCommandList_DiscardResource(context.list, depth_rt, &ds_region);

    /* Ensure that the clear gets executed properly for subresource 0 */
    transition_resource_state(context.list, rt, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
    transition_resource_state(context.list, depth_rt, D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_COPY_SOURCE);
    check_sub_resource_uint(rt, 0, context.queue, context.list, 0x000000ffu, 0);

    /* Ensure that the clear gets executed properly for subresource 0 */
    hr = ID3D12GraphicsCommandList_Reset(context.list, context.allocator, NULL);
    {
        D3D12_TEXTURE_COPY_LOCATION dst_location, src_location;
        D3D12_BOX src_box;

        dst_location.SubresourceIndex = 0;
        dst_location.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        dst_location.pResource = tmp_depth;

        src_location.SubresourceIndex = 0;
        src_location.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        src_location.pResource = depth_rt;

        src_box.left = 0;
        src_box.right = 16;
        src_box.top = 0;
        src_box.bottom = 16;
        src_box.front = 0;
        src_box.back = 1;

        ID3D12GraphicsCommandList_CopyTextureRegion(context.list, &dst_location, 0, 0, 0, &src_location, &src_box);
        transition_resource_state(context.list, tmp_depth, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COPY_SOURCE);
    }
    check_sub_resource_float(tmp_depth, 0, context.queue, context.list, 1.0f, 0);

    ID3D12Resource_Release(rt);
    ID3D12Resource_Release(depth_rt);
    ID3D12Resource_Release(tmp_depth);
    ID3D12DescriptorHeap_Release(rtv_heap);
    ID3D12DescriptorHeap_Release(dsv_heap);
    destroy_test_context(&context);
}

void test_root_parameter_preservation(void)
{
    D3D12_ROOT_SIGNATURE_DESC root_signature_desc;
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc;
    ID3D12RootSignature *root_signature;
    D3D12_ROOT_PARAMETER root_parameter;
    ID3D12PipelineState *graphics_pso;
    ID3D12PipelineState *compute_pso;
    struct test_context_desc desc;
    struct test_context context;
    struct resource_readback rb;
    ID3D12Resource *buffer;
    uint32_t value;
    HRESULT hr;

    static const DWORD ps_code[] =
    {
#if 0
        RWStructuredBuffer<uint> RWBuf : register(u1);
        float4 main() : SV_Target
        {
                uint v;
                InterlockedAdd(RWBuf[0], 100, v);
                return 1.0.xxxx;
        }
#endif
        0x43425844, 0x752ce8e8, 0x84d20946, 0xf5cbf13c, 0x37b624ad, 0x00000001, 0x000000ec, 0x00000003,
        0x0000002c, 0x0000003c, 0x00000070, 0x4e475349, 0x00000008, 0x00000000, 0x00000008, 0x4e47534f,
        0x0000002c, 0x00000001, 0x00000008, 0x00000020, 0x00000000, 0x00000000, 0x00000003, 0x00000000,
        0x0000000f, 0x545f5653, 0x65677261, 0xabab0074, 0x58454853, 0x00000074, 0x00000050, 0x0000001d,
        0x0100086a, 0x0400009e, 0x0011e000, 0x00000001, 0x00000004, 0x03000065, 0x001020f2, 0x00000000,
        0x0a0000ad, 0x0011e000, 0x00000001, 0x00004002, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
        0x00004001, 0x00000064, 0x08000036, 0x001020f2, 0x00000000, 0x00004002, 0x3f800000, 0x3f800000,
        0x3f800000, 0x3f800000, 0x0100003e,
    };
    static const D3D12_SHADER_BYTECODE ps = {ps_code, sizeof(ps_code)};

    static const DWORD cs_code[] =
    {
#if 0
        RWStructuredBuffer<uint> RWBuf : register(u1);
        [numthreads(1, 1, 1)]
        void main()
        {
                uint v;
                InterlockedAdd(RWBuf[0], 1, v);
        }
#endif
        0x43425844, 0x010d2839, 0x4ca90409, 0x945bf22a, 0x52d288e5, 0x00000001, 0x000000ac, 0x00000003,
        0x0000002c, 0x0000003c, 0x0000004c, 0x4e475349, 0x00000008, 0x00000000, 0x00000008, 0x4e47534f,
        0x00000008, 0x00000000, 0x00000008, 0x58454853, 0x00000058, 0x00050050, 0x00000016, 0x0100086a,
        0x0400009e, 0x0011e000, 0x00000001, 0x00000004, 0x0400009b, 0x00000001, 0x00000001, 0x00000001,
        0x0a0000ad, 0x0011e000, 0x00000001, 0x00004002, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
        0x00004001, 0x00000001, 0x0100003e,
    };
    static const D3D12_SHADER_BYTECODE cs = {cs_code, sizeof(cs_code)};

    memset(&desc, 0, sizeof(desc));
    desc.rt_width = 1;
    desc.rt_height = 1;
    desc.rt_format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.rt_descriptor_count = 1;
    desc.sample_desc.Count = 1;
    desc.rt_array_size = 1;
    desc.no_pipeline = true;
    desc.no_root_signature = true;

    if (!init_test_context(&context, &desc))
        return;

    memset(&root_signature_desc, 0, sizeof(root_signature_desc));
    memset(&root_parameter, 0, sizeof(root_parameter));

    root_signature_desc.NumParameters = 1;
    root_signature_desc.pParameters = &root_parameter;
    root_parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    root_parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    root_parameter.Descriptor.ShaderRegister = 1;

    hr = create_root_signature(context.device, &root_signature_desc, &root_signature);
    ok(SUCCEEDED(hr), "Failed to create root signature, hr = #%x.\n", hr);

    init_pipeline_state_desc(&pso_desc, root_signature, desc.rt_format, NULL, &ps, NULL);
    hr = ID3D12Device_CreateGraphicsPipelineState(context.device, &pso_desc, &IID_ID3D12PipelineState, (void **)&graphics_pso);
    ok(SUCCEEDED(hr), "Failed to create PSO, hr = #%x.\n", hr);
    compute_pso = create_compute_pipeline_state(context.device, root_signature, cs);

    buffer = create_default_buffer(context.device, 4096, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    ID3D12GraphicsCommandList_SetComputeRootSignature(context.list, root_signature);
    ID3D12GraphicsCommandList_SetGraphicsRootSignature(context.list, root_signature);
    ID3D12GraphicsCommandList_SetComputeRootUnorderedAccessView(context.list, 0, ID3D12Resource_GetGPUVirtualAddress(buffer));
    ID3D12GraphicsCommandList_SetGraphicsRootUnorderedAccessView(context.list, 0, ID3D12Resource_GetGPUVirtualAddress(buffer) + 4);

    ID3D12GraphicsCommandList_SetPipelineState(context.list, compute_pso);
    ID3D12GraphicsCommandList_Dispatch(context.list, 4, 1, 1);
    uav_barrier(context.list, buffer);

    ID3D12GraphicsCommandList_SetPipelineState(context.list, graphics_pso);
    ID3D12GraphicsCommandList_IASetPrimitiveTopology(context.list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D12GraphicsCommandList_OMSetRenderTargets(context.list, 1, &context.rtv, TRUE, NULL);
    ID3D12GraphicsCommandList_RSSetViewports(context.list, 1, &context.viewport);
    ID3D12GraphicsCommandList_RSSetScissorRects(context.list, 1, &context.scissor_rect);
    ID3D12GraphicsCommandList_DrawInstanced(context.list, 3, 1, 0, 0);
    uav_barrier(context.list, buffer);

    /* We never touched root signature or root parameters, but verify that we correctly update push constants here. */
    ID3D12GraphicsCommandList_SetPipelineState(context.list, compute_pso);
    ID3D12GraphicsCommandList_Dispatch(context.list, 4, 1, 1);

    transition_resource_state(context.list, buffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
    get_buffer_readback_with_command_list(buffer, DXGI_FORMAT_UNKNOWN, &rb, context.queue, context.list);

    value = get_readback_uint(&rb, 0, 0, 0);
    ok(value == 8, "Value %u != 8.\n", value);
    value = get_readback_uint(&rb, 1, 0, 0);
    ok(value == 100, "Value %u != 100.\n", value);

    release_resource_readback(&rb);
    ID3D12Resource_Release(buffer);
    ID3D12RootSignature_Release(root_signature);
    ID3D12PipelineState_Release(graphics_pso);
    ID3D12PipelineState_Release(compute_pso);
    destroy_test_context(&context);
}

static void test_cbv_hoisting(bool use_dxil)
{
    D3D12_VERSIONED_ROOT_SIGNATURE_DESC root_signature_desc;
    D3D12_ROOT_PARAMETER1 root_parameters[2];
    D3D12_DESCRIPTOR_RANGE1 table_ranges[4];
    unsigned int i, base_shader_register;
    ID3D12RootSignature *root_signature;
    uint32_t cbuffer_data[64 * 4];
    struct test_context context;
    struct resource_readback rb;
    ID3D12DescriptorHeap *desc;
    ID3D12PipelineState *pso;
    ID3D12Resource *wbuffer;
    ID3D12Resource *rbuffer;
    uint32_t value;
    HRESULT hr;

#if 0
    // Can be hoisted.
    cbuffer cbuf0 : register(b0)
    {
        uint v0;
    };

    struct C { uint v; };
    // Can be hoisted.
    ConstantBuffer<C> cbuf1[1]: register(b1);
    // Cannot be hoisted.
    ConstantBuffer<C> cbufs[2] : register(b2);

    RWByteAddressBuffer RWBuf : register(u0);

    [numthreads(4, 1, 1)]
    void main(uint thr : SV_DispatchThreadID)
    {
        uint wval;
        if (thr == 0)
            wval = v0;
        else if (thr == 2)
            wval = cbufs[0].v;
        else if (thr == 3)
            wval = cbufs[1].v;
        else
        {
            // Verify that we can convert this to a plain descriptor, even with weird indexing into array size of 1.
            // Array size of 1 means we have to access one descriptor.
            wval = cbuf1[NonUniformResourceIndex(thr - 1)].v;
        }

        RWBuf.Store(4 * thr, wval);
    }
#endif
    static const DWORD cs_code_dxbc[] =
    {

        0x43425844, 0x0ff40a34, 0xe72ddcb6, 0x2821e5e5, 0x57c71636, 0x00000001, 0x00000224, 0x00000003,
        0x0000002c, 0x0000003c, 0x0000004c, 0x4e475349, 0x00000008, 0x00000000, 0x00000008, 0x4e47534f,
        0x00000008, 0x00000000, 0x00000008, 0x58454853, 0x000001d0, 0x00050051, 0x00000074, 0x0100086a,
        0x07000059, 0x00308e46, 0x00000000, 0x00000000, 0x00000000, 0x00000001, 0x00000000, 0x07000859,
        0x00308e46, 0x00000001, 0x00000001, 0x00000001, 0x00000001, 0x00000000, 0x07000059, 0x00308e46,
        0x00000002, 0x00000002, 0x00000003, 0x00000001, 0x00000000, 0x0600009d, 0x0031ee46, 0x00000000,
        0x00000000, 0x00000000, 0x00000000, 0x0200005f, 0x00020012, 0x02000068, 0x00000001, 0x0400009b,
        0x00000004, 0x00000001, 0x00000001, 0x0200001f, 0x0002000a, 0x07000036, 0x00100022, 0x00000000,
        0x0030800a, 0x00000000, 0x00000000, 0x00000000, 0x01000012, 0x06000020, 0x00100012, 0x00000000,
        0x0002000a, 0x00004001, 0x00000002, 0x0304001f, 0x0010000a, 0x00000000, 0x07000036, 0x00100022,
        0x00000000, 0x0030800a, 0x00000002, 0x00000002, 0x00000000, 0x01000012, 0x06000020, 0x00100012,
        0x00000000, 0x0002000a, 0x00004001, 0x00000003, 0x0304001f, 0x0010000a, 0x00000000, 0x07000036,
        0x00100022, 0x00000000, 0x0030800a, 0x00000002, 0x00000003, 0x00000000, 0x01000012, 0x0600001e,
        0x00100012, 0x00000000, 0x0002000a, 0x00004001, 0xffffffff, 0x0a000036, 0x00100022, 0x00000000,
        0x8630800a, 0x00020001, 0x00000001, 0x00000001, 0x0010000a, 0x00000000, 0x00000000, 0x01000015,
        0x01000015, 0x01000015, 0x06000029, 0x00100012, 0x00000000, 0x0002000a, 0x00004001, 0x00000002,
        0x080000a6, 0x0021e012, 0x00000000, 0x00000000, 0x0010000a, 0x00000000, 0x0010001a, 0x00000000,
        0x0100003e,
    };
    static const BYTE cs_code_dxil[] =
    {
        0x44, 0x58, 0x42, 0x43, 0x3c, 0x44, 0x94, 0x59, 0x48, 0xd4, 0x45, 0xa8, 0x23, 0x30, 0x16, 0x51, 0x75, 0x5c, 0x04, 0x1f, 0x01, 0x00, 0x00, 0x00, 0x08, 0x07, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00,
        0x34, 0x00, 0x00, 0x00, 0x44, 0x00, 0x00, 0x00, 0x54, 0x00, 0x00, 0x00, 0x64, 0x00, 0x00, 0x00, 0xe8, 0x00, 0x00, 0x00, 0x53, 0x46, 0x49, 0x30, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x49, 0x53, 0x47, 0x31, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x4f, 0x53, 0x47, 0x31, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x08, 0x00, 0x00, 0x00, 0x50, 0x53, 0x56, 0x30, 0x7c, 0x00, 0x00, 0x00, 0x24, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x44, 0x58, 0x49, 0x4c, 0x18, 0x06, 0x00, 0x00, 0x60, 0x00, 0x05, 0x00, 0x86, 0x01, 0x00, 0x00, 0x44, 0x58, 0x49, 0x4c, 0x00, 0x01, 0x00, 0x00,
        0x10, 0x00, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x42, 0x43, 0xc0, 0xde, 0x21, 0x0c, 0x00, 0x00, 0x7d, 0x01, 0x00, 0x00, 0x0b, 0x82, 0x20, 0x00, 0x02, 0x00, 0x00, 0x00, 0x13, 0x00, 0x00, 0x00,
        0x07, 0x81, 0x23, 0x91, 0x41, 0xc8, 0x04, 0x49, 0x06, 0x10, 0x32, 0x39, 0x92, 0x01, 0x84, 0x0c, 0x25, 0x05, 0x08, 0x19, 0x1e, 0x04, 0x8b, 0x62, 0x80, 0x14, 0x45, 0x02, 0x42, 0x92, 0x0b, 0x42,
        0xa4, 0x10, 0x32, 0x14, 0x38, 0x08, 0x18, 0x4b, 0x0a, 0x32, 0x52, 0x88, 0x48, 0x90, 0x14, 0x20, 0x43, 0x46, 0x88, 0xa5, 0x00, 0x19, 0x32, 0x42, 0xe4, 0x48, 0x0e, 0x90, 0x91, 0x22, 0xc4, 0x50,
        0x41, 0x51, 0x81, 0x8c, 0xe1, 0x83, 0xe5, 0x8a, 0x04, 0x29, 0x46, 0x06, 0x51, 0x18, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x1b, 0x8c, 0xe0, 0xff, 0xff, 0xff, 0xff, 0x07, 0x40, 0x02, 0xa8, 0x0d,
        0x86, 0xf0, 0xff, 0xff, 0xff, 0xff, 0x03, 0x20, 0x01, 0xd5, 0x06, 0x62, 0xf8, 0xff, 0xff, 0xff, 0xff, 0x01, 0x90, 0x00, 0x49, 0x18, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x13, 0x82, 0x60, 0x42,
        0x20, 0x4c, 0x08, 0x06, 0x00, 0x00, 0x00, 0x00, 0x89, 0x20, 0x00, 0x00, 0x2c, 0x00, 0x00, 0x00, 0x32, 0x22, 0x48, 0x09, 0x20, 0x64, 0x85, 0x04, 0x93, 0x22, 0xa4, 0x84, 0x04, 0x93, 0x22, 0xe3,
        0x84, 0xa1, 0x90, 0x14, 0x12, 0x4c, 0x8a, 0x8c, 0x0b, 0x84, 0xa4, 0x4c, 0x10, 0x7c, 0x23, 0x00, 0x25, 0x00, 0x14, 0xe6, 0x08, 0xc0, 0xa0, 0x0c, 0x63, 0x0c, 0x22, 0x47, 0x0d, 0x97, 0x3f, 0x61,
        0x0f, 0x21, 0xf9, 0xdc, 0x46, 0x15, 0x2b, 0x31, 0xf9, 0xc8, 0x6d, 0x23, 0x62, 0x8c, 0x31, 0xe6, 0x08, 0x10, 0x3a, 0xf7, 0x0c, 0x97, 0x3f, 0x61, 0x0f, 0x21, 0xf9, 0x21, 0xd0, 0x0c, 0x0b, 0x81,
        0x02, 0x54, 0x08, 0x33, 0xd2, 0x20, 0x35, 0x47, 0x10, 0x14, 0x23, 0x8d, 0x33, 0x06, 0xa3, 0x56, 0x14, 0x30, 0xd2, 0x18, 0x63, 0x8c, 0x71, 0xe8, 0x0d, 0x04, 0x9c, 0x26, 0x4d, 0x11, 0x25, 0x4c,
        0xfe, 0x0a, 0x6f, 0xd8, 0x44, 0x68, 0xc3, 0x10, 0x11, 0x92, 0xb4, 0x51, 0x45, 0x41, 0x44, 0x28, 0x18, 0x24, 0xaf, 0x10, 0x02, 0xaa, 0xa0, 0x51, 0x30, 0x88, 0x1e, 0x22, 0x4d, 0x11, 0x25, 0x4c,
        0x3e, 0x87, 0x82, 0x91, 0x01, 0xf6, 0x0a, 0x21, 0xa0, 0x8a, 0x1a, 0x05, 0x97, 0xf0, 0x15, 0x42, 0x40, 0x15, 0x12, 0x0a, 0x6c, 0x0a, 0x68, 0xda, 0x73, 0x04, 0xa0, 0x30, 0x05, 0x00, 0x00, 0x00,
        0x13, 0x14, 0x72, 0xc0, 0x87, 0x74, 0x60, 0x87, 0x36, 0x68, 0x87, 0x79, 0x68, 0x03, 0x72, 0xc0, 0x87, 0x0d, 0xaf, 0x50, 0x0e, 0x6d, 0xd0, 0x0e, 0x7a, 0x50, 0x0e, 0x6d, 0x00, 0x0f, 0x7a, 0x30,
        0x07, 0x72, 0xa0, 0x07, 0x73, 0x20, 0x07, 0x6d, 0x90, 0x0e, 0x71, 0xa0, 0x07, 0x73, 0x20, 0x07, 0x6d, 0x90, 0x0e, 0x78, 0xa0, 0x07, 0x73, 0x20, 0x07, 0x6d, 0x90, 0x0e, 0x71, 0x60, 0x07, 0x7a,
        0x30, 0x07, 0x72, 0xd0, 0x06, 0xe9, 0x30, 0x07, 0x72, 0xa0, 0x07, 0x73, 0x20, 0x07, 0x6d, 0x90, 0x0e, 0x76, 0x40, 0x07, 0x7a, 0x60, 0x07, 0x74, 0xd0, 0x06, 0xe6, 0x10, 0x07, 0x76, 0xa0, 0x07,
        0x73, 0x20, 0x07, 0x6d, 0x60, 0x0e, 0x73, 0x20, 0x07, 0x7a, 0x30, 0x07, 0x72, 0xd0, 0x06, 0xe6, 0x60, 0x07, 0x74, 0xa0, 0x07, 0x76, 0x40, 0x07, 0x6d, 0xe0, 0x0e, 0x78, 0xa0, 0x07, 0x71, 0x60,
        0x07, 0x7a, 0x30, 0x07, 0x72, 0xa0, 0x07, 0x76, 0x40, 0x07, 0x43, 0x9e, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x86, 0x3c, 0x04, 0x10, 0x00, 0x01, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x0c, 0x79, 0x14, 0x20, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0xf2, 0x34, 0x40, 0x00, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x30, 0xe4,
        0x79, 0x80, 0x00, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x20, 0x0b, 0x04, 0x00, 0x00, 0x0c, 0x00, 0x00, 0x00, 0x32, 0x1e, 0x98, 0x14, 0x19, 0x11, 0x4c, 0x90, 0x8c, 0x09, 0x26, 0x47,
        0xc6, 0x04, 0x43, 0x1a, 0x25, 0x30, 0x02, 0x50, 0x08, 0xc5, 0x50, 0x16, 0x45, 0x40, 0x6c, 0x04, 0x80, 0x7a, 0x81, 0x50, 0x9d, 0x01, 0xa0, 0x3c, 0x03, 0x40, 0x7c, 0x06, 0x80, 0xe6, 0x0c, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x79, 0x18, 0x00, 0x00, 0x47, 0x00, 0x00, 0x00, 0x1a, 0x03, 0x4c, 0x90, 0x46, 0x02, 0x13, 0xc4, 0x88, 0x0c, 0x6f, 0xec, 0xed, 0x4d, 0x0c, 0x44, 0x06, 0x26, 0x26, 0xc7,
        0x05, 0xa6, 0xc6, 0x05, 0x06, 0x66, 0x43, 0x10, 0x4c, 0x10, 0x86, 0x62, 0x82, 0x30, 0x18, 0x1b, 0x84, 0x81, 0x98, 0x20, 0x0c, 0xc7, 0x06, 0x61, 0x30, 0x28, 0x8c, 0xcd, 0x4d, 0x10, 0x06, 0x64,
        0xc3, 0x80, 0x24, 0xc4, 0x04, 0x61, 0x82, 0x08, 0x4c, 0x10, 0x86, 0x64, 0x82, 0xc0, 0x2c, 0x1b, 0x16, 0x62, 0x61, 0x08, 0x62, 0x68, 0x1c, 0xc7, 0x01, 0x36, 0x04, 0xcf, 0x04, 0xa1, 0x6a, 0x36,
        0x20, 0x44, 0xc4, 0x10, 0xc4, 0x60, 0x00, 0x13, 0x84, 0xcc, 0xd9, 0x80, 0x0c, 0x13, 0x43, 0x0c, 0x83, 0x01, 0x4c, 0x10, 0x06, 0x65, 0x82, 0xc0, 0x3d, 0x1b, 0x90, 0xca, 0x62, 0x88, 0xaa, 0x32,
        0x80, 0x0d, 0x83, 0x44, 0x5d, 0x1b, 0x08, 0x00, 0xc2, 0x80, 0x09, 0x82, 0x00, 0x6c, 0x00, 0x36, 0x0c, 0xc4, 0xb6, 0x6d, 0x08, 0xb8, 0x0d, 0xc3, 0xa0, 0x75, 0x24, 0xda, 0xc2, 0xd2, 0xdc, 0x26,
        0x08, 0x1d, 0xb3, 0x61, 0x30, 0x86, 0x61, 0x03, 0x41, 0x80, 0x81, 0x11, 0x06, 0x1b, 0x0a, 0xed, 0x03, 0x32, 0x31, 0xa8, 0xc2, 0xc6, 0x66, 0xd7, 0xe6, 0x92, 0x46, 0x56, 0xe6, 0x46, 0x37, 0x25,
        0x08, 0xaa, 0x90, 0xe1, 0xb9, 0xd8, 0x95, 0xc9, 0xcd, 0xa5, 0xbd, 0xb9, 0x4d, 0x09, 0x88, 0x26, 0x64, 0x78, 0x2e, 0x76, 0x61, 0x6c, 0x76, 0x65, 0x72, 0x53, 0x02, 0xa3, 0x0e, 0x19, 0x9e, 0xcb,
        0x1c, 0x5a, 0x18, 0x59, 0x99, 0x5c, 0xd3, 0x1b, 0x59, 0x19, 0xdb, 0x94, 0x20, 0x29, 0x43, 0x86, 0xe7, 0x22, 0x57, 0x36, 0xf7, 0x56, 0x27, 0x37, 0x56, 0x36, 0x37, 0x25, 0xc0, 0x2a, 0x91, 0xe1,
        0xb9, 0xd0, 0xe5, 0xc1, 0x95, 0x05, 0xb9, 0xb9, 0xbd, 0xd1, 0x85, 0xd1, 0xa5, 0xbd, 0xb9, 0xcd, 0x4d, 0x09, 0xba, 0x3a, 0x64, 0x78, 0x2e, 0x65, 0x6e, 0x74, 0x72, 0x79, 0x50, 0x6f, 0x69, 0x6e,
        0x74, 0x73, 0x53, 0x02, 0x31, 0x00, 0x00, 0x00, 0x79, 0x18, 0x00, 0x00, 0x4c, 0x00, 0x00, 0x00, 0x33, 0x08, 0x80, 0x1c, 0xc4, 0xe1, 0x1c, 0x66, 0x14, 0x01, 0x3d, 0x88, 0x43, 0x38, 0x84, 0xc3,
        0x8c, 0x42, 0x80, 0x07, 0x79, 0x78, 0x07, 0x73, 0x98, 0x71, 0x0c, 0xe6, 0x00, 0x0f, 0xed, 0x10, 0x0e, 0xf4, 0x80, 0x0e, 0x33, 0x0c, 0x42, 0x1e, 0xc2, 0xc1, 0x1d, 0xce, 0xa1, 0x1c, 0x66, 0x30,
        0x05, 0x3d, 0x88, 0x43, 0x38, 0x84, 0x83, 0x1b, 0xcc, 0x03, 0x3d, 0xc8, 0x43, 0x3d, 0x8c, 0x03, 0x3d, 0xcc, 0x78, 0x8c, 0x74, 0x70, 0x07, 0x7b, 0x08, 0x07, 0x79, 0x48, 0x87, 0x70, 0x70, 0x07,
        0x7a, 0x70, 0x03, 0x76, 0x78, 0x87, 0x70, 0x20, 0x87, 0x19, 0xcc, 0x11, 0x0e, 0xec, 0x90, 0x0e, 0xe1, 0x30, 0x0f, 0x6e, 0x30, 0x0f, 0xe3, 0xf0, 0x0e, 0xf0, 0x50, 0x0e, 0x33, 0x10, 0xc4, 0x1d,
        0xde, 0x21, 0x1c, 0xd8, 0x21, 0x1d, 0xc2, 0x61, 0x1e, 0x66, 0x30, 0x89, 0x3b, 0xbc, 0x83, 0x3b, 0xd0, 0x43, 0x39, 0xb4, 0x03, 0x3c, 0xbc, 0x83, 0x3c, 0x84, 0x03, 0x3b, 0xcc, 0xf0, 0x14, 0x76,
        0x60, 0x07, 0x7b, 0x68, 0x07, 0x37, 0x68, 0x87, 0x72, 0x68, 0x07, 0x37, 0x80, 0x87, 0x70, 0x90, 0x87, 0x70, 0x60, 0x07, 0x76, 0x28, 0x07, 0x76, 0xf8, 0x05, 0x76, 0x78, 0x87, 0x77, 0x80, 0x87,
        0x5f, 0x08, 0x87, 0x71, 0x18, 0x87, 0x72, 0x98, 0x87, 0x79, 0x98, 0x81, 0x2c, 0xee, 0xf0, 0x0e, 0xee, 0xe0, 0x0e, 0xf5, 0xc0, 0x0e, 0xec, 0x30, 0x03, 0x62, 0xc8, 0xa1, 0x1c, 0xe4, 0xa1, 0x1c,
        0xcc, 0xa1, 0x1c, 0xe4, 0xa1, 0x1c, 0xdc, 0x61, 0x1c, 0xca, 0x21, 0x1c, 0xc4, 0x81, 0x1d, 0xca, 0x61, 0x06, 0xd6, 0x90, 0x43, 0x39, 0xc8, 0x43, 0x39, 0x98, 0x43, 0x39, 0xc8, 0x43, 0x39, 0xb8,
        0xc3, 0x38, 0x94, 0x43, 0x38, 0x88, 0x03, 0x3b, 0x94, 0xc3, 0x2f, 0xbc, 0x83, 0x3c, 0xfc, 0x82, 0x3b, 0xd4, 0x03, 0x3b, 0xb0, 0xc3, 0x8c, 0xcc, 0x21, 0x07, 0x7c, 0x70, 0x03, 0x74, 0x60, 0x07,
        0x37, 0x90, 0x87, 0x72, 0x98, 0x87, 0x77, 0xa8, 0x07, 0x79, 0x18, 0x87, 0x72, 0x70, 0x83, 0x70, 0xa0, 0x07, 0x7a, 0x90, 0x87, 0x74, 0x10, 0x87, 0x7a, 0xa0, 0x87, 0x72, 0x00, 0x00, 0x00, 0x00,
        0x71, 0x20, 0x00, 0x00, 0x14, 0x00, 0x00, 0x00, 0x46, 0x50, 0x0d, 0x97, 0xef, 0x3c, 0x7e, 0x40, 0x15, 0x05, 0x11, 0xb1, 0x93, 0x13, 0x11, 0x3e, 0x72, 0xdb, 0x26, 0xb0, 0x0d, 0x97, 0xef, 0x3c,
        0xbe, 0x10, 0x50, 0x45, 0x41, 0x44, 0xa5, 0x03, 0x0c, 0x25, 0x61, 0x00, 0x02, 0xe6, 0x23, 0xb7, 0x6d, 0x03, 0xd2, 0x70, 0xf9, 0xce, 0xe3, 0x0b, 0x11, 0x01, 0x4c, 0x44, 0x08, 0x34, 0xc3, 0x42,
        0x58, 0x80, 0x34, 0x5c, 0xbe, 0xf3, 0xf8, 0xd3, 0x11, 0x11, 0xc0, 0x20, 0x0e, 0x3e, 0x72, 0xdb, 0x06, 0x40, 0x30, 0x00, 0xd2, 0x00, 0x00, 0x00, 0x61, 0x20, 0x00, 0x00, 0x39, 0x00, 0x00, 0x00,
        0x13, 0x04, 0x48, 0x2c, 0x10, 0x00, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00, 0x34, 0x4a, 0xae, 0xec, 0x66, 0x00, 0xca, 0xa0, 0x74, 0x03, 0x6a, 0xa0, 0x14, 0x03, 0xe8, 0x14, 0x41, 0x09, 0x00, 0x00,
        0x23, 0x06, 0x09, 0x00, 0x82, 0x60, 0xd0, 0x5c, 0x49, 0x40, 0x51, 0xcf, 0x88, 0x41, 0x02, 0x80, 0x20, 0x18, 0x34, 0x98, 0x32, 0x58, 0x16, 0x34, 0x62, 0x90, 0x00, 0x20, 0x08, 0x06, 0x4d, 0xb6,
        0x10, 0x96, 0x15, 0x8d, 0x18, 0x24, 0x00, 0x08, 0x82, 0x41, 0xa3, 0x31, 0xc5, 0x94, 0x48, 0x23, 0x06, 0x09, 0x00, 0x82, 0x60, 0xd0, 0x6c, 0x8d, 0x41, 0x51, 0xd3, 0x88, 0x81, 0x01, 0x80, 0x20,
        0x18, 0x10, 0x9e, 0x92, 0x0d, 0x37, 0x04, 0x1a, 0x18, 0xcc, 0x32, 0x04, 0x42, 0x30, 0x62, 0x70, 0x00, 0x20, 0x08, 0x06, 0xca, 0xf7, 0x14, 0xdb, 0x68, 0x42, 0x00, 0xcc, 0x12, 0x1c, 0xc3, 0x0d,
        0x44, 0x06, 0x06, 0xb3, 0x0c, 0x03, 0x11, 0x8c, 0x18, 0x1c, 0x00, 0x08, 0x82, 0x81, 0x22, 0x06, 0x92, 0xe1, 0x8d, 0x26, 0x04, 0xc0, 0x2c, 0xc1, 0x31, 0xdc, 0x70, 0x48, 0x60, 0x30, 0xcb, 0x50,
        0x18, 0xc1, 0x88, 0xc1, 0x01, 0x80, 0x20, 0x18, 0x28, 0x65, 0x50, 0x29, 0x61, 0x30, 0x9a, 0x10, 0x00, 0xb3, 0x04, 0x47, 0x29, 0x13, 0x8c, 0x18, 0x1c, 0x00, 0x08, 0x82, 0x81, 0x82, 0x06, 0xd8,
        0x13, 0x8c, 0x26, 0x04, 0xc0, 0x2c, 0xc1, 0x31, 0x50, 0x32, 0x50, 0x81, 0x33, 0x20, 0x85, 0x60, 0x94, 0x33, 0x06, 0x37, 0x62, 0xd0, 0x00, 0x20, 0x08, 0x06, 0x8f, 0x1a, 0x5c, 0x54, 0xb0, 0x09,
        0xdb, 0xb6, 0x55, 0x08, 0x00, 0x00, 0x00, 0x00,
    };
    const D3D12_SHADER_BYTECODE cs = {use_dxil ? (const void*)cs_code_dxil : (const void*)cs_code_dxbc, use_dxil ? sizeof(cs_code_dxil) : sizeof(cs_code_dxbc)};

    if (!init_compute_test_context(&context))
        return;

    if (use_dxil && !context_supports_dxil(&context))
    {
        destroy_test_context(&context);
        return;
    }

    memset(&root_signature_desc, 0, sizeof(root_signature_desc));
    memset(root_parameters, 0, sizeof(root_parameters));
    memset(table_ranges, 0, sizeof(table_ranges));

    root_signature_desc.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
    root_signature_desc.Desc_1_1.NumParameters = ARRAY_SIZE(root_parameters);
    root_signature_desc.Desc_1_1.pParameters = root_parameters;
    root_parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    root_parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    root_parameters[0].DescriptorTable.NumDescriptorRanges = ARRAY_SIZE(table_ranges);
    root_parameters[0].DescriptorTable.pDescriptorRanges = table_ranges;
    root_parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    root_parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    base_shader_register = 0;
    for (i = 0; i < ARRAY_SIZE(table_ranges); i++)
    {
        table_ranges[i].NumDescriptors = i >= 2 ? 2 : 1;
        table_ranges[i].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
        table_ranges[i].Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_STATIC_KEEPING_BUFFER_BOUNDS_CHECKS;
        table_ranges[i].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
        table_ranges[i].BaseShaderRegister = base_shader_register;
        base_shader_register += table_ranges[i].NumDescriptors;
    }

    hr = create_versioned_root_signature(context.device, &root_signature_desc, &root_signature);
    ok(SUCCEEDED(hr), "Failed to create root signature, hr = #%x.\n", hr);

    pso = create_compute_pipeline_state(context.device, root_signature, cs);

    memset(cbuffer_data, 0, sizeof(cbuffer_data));
    for (i = 0; i < ARRAY_SIZE(table_ranges); i++)
        cbuffer_data[i * 64] = i;

    rbuffer = create_upload_buffer(context.device, sizeof(cbuffer_data), cbuffer_data);
    wbuffer = create_default_buffer(context.device, ARRAY_SIZE(table_ranges) * sizeof(uint32_t), D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    desc = create_gpu_descriptor_heap(context.device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, ARRAY_SIZE(table_ranges));
    for (i = 0; i < ARRAY_SIZE(table_ranges); i++)
    {
        D3D12_CONSTANT_BUFFER_VIEW_DESC cbv;
        D3D12_CPU_DESCRIPTOR_HANDLE handle;

        handle = ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(desc);
        handle.ptr += i * ID3D12Device_GetDescriptorHandleIncrementSize(context.device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        cbv.BufferLocation = ID3D12Resource_GetGPUVirtualAddress(rbuffer) + 256 * i;
        cbv.SizeInBytes = 256;
        ID3D12Device_CreateConstantBufferView(context.device, &cbv, handle);
    }

    ID3D12GraphicsCommandList_SetDescriptorHeaps(context.list, 1, &desc);
    ID3D12GraphicsCommandList_SetPipelineState(context.list, pso);
    ID3D12GraphicsCommandList_SetComputeRootSignature(context.list, root_signature);
    ID3D12GraphicsCommandList_SetComputeRootDescriptorTable(context.list, 0, ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(desc));
    ID3D12GraphicsCommandList_SetComputeRootUnorderedAccessView(context.list, 1, ID3D12Resource_GetGPUVirtualAddress(wbuffer));
    ID3D12GraphicsCommandList_Dispatch(context.list, 1, 1, 1);

    transition_resource_state(context.list, wbuffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
    get_buffer_readback_with_command_list(wbuffer, DXGI_FORMAT_UNKNOWN, &rb, context.queue, context.list);

    for (i = 0; i < ARRAY_SIZE(table_ranges); i++)
    {
        value = get_readback_uint(&rb, i, 0, 0);
        ok(value == i, "Value %u != %u.\n", value, i);
    }

    release_resource_readback(&rb);
    ID3D12Resource_Release(wbuffer);
    ID3D12Resource_Release(rbuffer);
    ID3D12RootSignature_Release(root_signature);
    ID3D12PipelineState_Release(pso);
    ID3D12DescriptorHeap_Release(desc);
    destroy_test_context(&context);
}

void test_cbv_hoisting_sm51(void)
{
    test_cbv_hoisting(false);
}

void test_cbv_hoisting_dxil(void)
{
    test_cbv_hoisting(true);
}

static void test_conservative_rasterization(bool use_dxil)
{
    ID3D12PipelineState *pipeline_conservative_underestimate;
    ID3D12PipelineState *pipeline_conservative_overestimate;
    ID3D12PipelineState *pipeline_conservative_off;
    D3D12_ROOT_SIGNATURE_DESC root_signature_desc;
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc;
    ID3D12PipelineState *pipeline_stencil_test;
    D3D12_FEATURE_DATA_D3D12_OPTIONS options;
    ID3D12GraphicsCommandList *command_list;
    D3D12_INPUT_LAYOUT_DESC input_layout;
    ID3D12RootSignature *root_signature;
    struct depth_stencil_resource ds;
    D3D12_QUERY_HEAP_DESC heap_desc;
    struct test_context_desc desc;
    ID3D12Resource *vb, *readback;
    D3D12_VERTEX_BUFFER_VIEW vbv;
    struct test_context context;
    ID3D12QueryHeap *query_heap;
    struct resource_readback rb;
    ID3D12CommandQueue *queue;
    uint64_t value;
    unsigned int i;
    HRESULT hr;

#if 0
    float4 main(float4 p : POSITION) : SV_Position
    {
        return p;
    }
#endif
    static const DWORD vs_dxbc[] =
    {
        0x43425844, 0x92767590, 0x06a6dba7, 0x0ae078b2, 0x7b5eb8f6, 0x00000001, 0x000000d8, 0x00000003,
        0x0000002c, 0x00000060, 0x00000094, 0x4e475349, 0x0000002c, 0x00000001, 0x00000008, 0x00000020,
        0x00000000, 0x00000000, 0x00000003, 0x00000000, 0x00000f0f, 0x49534f50, 0x4e4f4954, 0xababab00,
        0x4e47534f, 0x0000002c, 0x00000001, 0x00000008, 0x00000020, 0x00000000, 0x00000001, 0x00000003,
        0x00000000, 0x0000000f, 0x505f5653, 0x7469736f, 0x006e6f69, 0x52444853, 0x0000003c, 0x00010040,
        0x0000000f, 0x0300005f, 0x001010f2, 0x00000000, 0x04000067, 0x001020f2, 0x00000000, 0x00000001,
        0x05000036, 0x001020f2, 0x00000000, 0x00101e46, 0x00000000, 0x0100003e,
    };
    static const BYTE vs_dxil[] =
    {
        0x44, 0x58, 0x42, 0x43, 0x48, 0xc5, 0x2e, 0x8b, 0x11, 0x71, 0xe3, 0x06, 0xc8, 0x49, 0x0f, 0x0b, 0x1a, 0x84, 0x82, 0x61, 0x01, 0x00, 0x00, 0x00, 0xe9, 0x05, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00,
        0x34, 0x00, 0x00, 0x00, 0x44, 0x00, 0x00, 0x00, 0x7d, 0x00, 0x00, 0x00, 0xb9, 0x00, 0x00, 0x00, 0x39, 0x01, 0x00, 0x00, 0x53, 0x46, 0x49, 0x30, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x49, 0x53, 0x47, 0x31, 0x31, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x28, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x50, 0x4f, 0x53, 0x49, 0x54, 0x49, 0x4f, 0x4e, 0x00, 0x4f, 0x53, 0x47,
        0x31, 0x34, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x28, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x0f, 0xf0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x53, 0x56, 0x5f, 0x50, 0x6f, 0x73, 0x69, 0x74, 0x69, 0x6f, 0x6e, 0x00, 0x50, 0x53, 0x56, 0x30, 0x78, 0x00, 0x00,
        0x00, 0x24, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0x01, 0x00, 0x00,
        0x00, 0x01, 0x01, 0x00, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0c, 0x00, 0x00, 0x00, 0x00, 0x50, 0x4f, 0x53, 0x49, 0x54, 0x49, 0x4f, 0x4e, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x44, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x01, 0x00, 0x44, 0x03, 0x03, 0x04, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x44, 0x58, 0x49, 0x4c, 0xa8, 0x04, 0x00,
        0x00, 0x60, 0x00, 0x01, 0x00, 0x2a, 0x01, 0x00, 0x00, 0x44, 0x58, 0x49, 0x4c, 0x00, 0x01, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x90, 0x04, 0x00, 0x00, 0x42, 0x43, 0xc0, 0xde, 0x21, 0x0c, 0x00,
        0x00, 0x21, 0x01, 0x00, 0x00, 0x0b, 0x82, 0x20, 0x00, 0x02, 0x00, 0x00, 0x00, 0x13, 0x00, 0x00, 0x00, 0x07, 0x81, 0x23, 0x91, 0x41, 0xc8, 0x04, 0x49, 0x06, 0x10, 0x32, 0x39, 0x92, 0x01, 0x84,
        0x0c, 0x25, 0x05, 0x08, 0x19, 0x1e, 0x04, 0x8b, 0x62, 0x80, 0x10, 0x45, 0x02, 0x42, 0x92, 0x0b, 0x42, 0x84, 0x10, 0x32, 0x14, 0x38, 0x08, 0x18, 0x4b, 0x0a, 0x32, 0x42, 0x88, 0x48, 0x90, 0x14,
        0x20, 0x43, 0x46, 0x88, 0xa5, 0x00, 0x19, 0x32, 0x42, 0xe4, 0x48, 0x0e, 0x90, 0x11, 0x22, 0xc4, 0x50, 0x41, 0x51, 0x81, 0x8c, 0xe1, 0x83, 0xe5, 0x8a, 0x04, 0x21, 0x46, 0x06, 0x51, 0x18, 0x00,
        0x00, 0x06, 0x00, 0x00, 0x00, 0x1b, 0x8c, 0xe0, 0xff, 0xff, 0xff, 0xff, 0x07, 0x40, 0x02, 0xa8, 0x0d, 0x84, 0xf0, 0xff, 0xff, 0xff, 0xff, 0x03, 0x20, 0x01, 0x00, 0x00, 0x00, 0x49, 0x18, 0x00,
        0x00, 0x02, 0x00, 0x00, 0x00, 0x13, 0x82, 0x60, 0x42, 0x20, 0x00, 0x00, 0x00, 0x89, 0x20, 0x00, 0x00, 0x0f, 0x00, 0x00, 0x00, 0x32, 0x22, 0x08, 0x09, 0x20, 0x64, 0x85, 0x04, 0x13, 0x22, 0xa4,
        0x84, 0x04, 0x13, 0x22, 0xe3, 0x84, 0xa1, 0x90, 0x14, 0x12, 0x4c, 0x88, 0x8c, 0x0b, 0x84, 0x84, 0x4c, 0x10, 0x30, 0x23, 0x00, 0x25, 0x00, 0x8a, 0x19, 0x80, 0x39, 0x02, 0x30, 0x98, 0x23, 0x40,
        0x8a, 0x31, 0x44, 0x54, 0x44, 0x56, 0x0c, 0x20, 0xa2, 0x1a, 0xc2, 0x81, 0x80, 0x34, 0x20, 0x00, 0x00, 0x13, 0x14, 0x72, 0xc0, 0x87, 0x74, 0x60, 0x87, 0x36, 0x68, 0x87, 0x79, 0x68, 0x03, 0x72,
        0xc0, 0x87, 0x0d, 0xaf, 0x50, 0x0e, 0x6d, 0xd0, 0x0e, 0x7a, 0x50, 0x0e, 0x6d, 0x00, 0x0f, 0x7a, 0x30, 0x07, 0x72, 0xa0, 0x07, 0x73, 0x20, 0x07, 0x6d, 0x90, 0x0e, 0x71, 0xa0, 0x07, 0x73, 0x20,
        0x07, 0x6d, 0x90, 0x0e, 0x78, 0xa0, 0x07, 0x73, 0x20, 0x07, 0x6d, 0x90, 0x0e, 0x71, 0x60, 0x07, 0x7a, 0x30, 0x07, 0x72, 0xd0, 0x06, 0xe9, 0x30, 0x07, 0x72, 0xa0, 0x07, 0x73, 0x20, 0x07, 0x6d,
        0x90, 0x0e, 0x76, 0x40, 0x07, 0x7a, 0x60, 0x07, 0x74, 0xd0, 0x06, 0xe6, 0x10, 0x07, 0x76, 0xa0, 0x07, 0x73, 0x20, 0x07, 0x6d, 0x60, 0x0e, 0x73, 0x20, 0x07, 0x7a, 0x30, 0x07, 0x72, 0xd0, 0x06,
        0xe6, 0x60, 0x07, 0x74, 0xa0, 0x07, 0x76, 0x40, 0x07, 0x6d, 0xe0, 0x0e, 0x78, 0xa0, 0x07, 0x71, 0x60, 0x07, 0x7a, 0x30, 0x07, 0x72, 0xa0, 0x07, 0x76, 0x40, 0x07, 0x43, 0x9e, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x86, 0x3c, 0x06, 0x10, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0c, 0x79, 0x10, 0x20, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0xc8, 0x02, 0x01, 0x0a, 0x00, 0x00, 0x00, 0x32, 0x1e, 0x98, 0x10, 0x19, 0x11, 0x4c, 0x90, 0x8c, 0x09, 0x26, 0x47, 0xc6, 0x04, 0x43, 0xa2, 0x12, 0x18, 0x01, 0x28, 0x84, 0x62, 0xa0,
        0x2a, 0x89, 0x11, 0x80, 0x42, 0x28, 0x03, 0xda, 0xb1, 0x0c, 0x82, 0x08, 0x04, 0x02, 0x01, 0x00, 0x00, 0x79, 0x18, 0x00, 0x00, 0x42, 0x00, 0x00, 0x00, 0x1a, 0x03, 0x4c, 0x90, 0x46, 0x02, 0x13,
        0xc4, 0x83, 0x0c, 0x6f, 0x0c, 0x24, 0xc6, 0x45, 0x66, 0x43, 0x10, 0x4c, 0x10, 0x88, 0x61, 0x82, 0x40, 0x10, 0x1b, 0x84, 0x81, 0x98, 0x20, 0x10, 0xc5, 0x06, 0x61, 0x30, 0x28, 0xd8, 0xcd, 0x4d,
        0x10, 0x08, 0x63, 0xc3, 0x80, 0x24, 0xc4, 0x04, 0x41, 0x00, 0x36, 0x00, 0x1b, 0x06, 0x82, 0x61, 0x36, 0x04, 0xcd, 0x86, 0x61, 0x58, 0x9c, 0x09, 0xc2, 0xb2, 0x6c, 0x08, 0x20, 0x12, 0x6d, 0x61,
        0x69, 0x6e, 0x44, 0xa0, 0x9e, 0xa6, 0x92, 0xa8, 0x92, 0x9e, 0x9c, 0x26, 0x08, 0xc5, 0x31, 0x41, 0x28, 0x90, 0x0d, 0x01, 0x31, 0x41, 0x28, 0x92, 0x0d, 0x0b, 0x31, 0x51, 0x95, 0x55, 0x0d, 0x17,
        0x51, 0x01, 0x1b, 0x02, 0x8c, 0xcb, 0x94, 0xd5, 0x17, 0xd4, 0xdb, 0x5c, 0x1a, 0x5d, 0xda, 0x9b, 0xdb, 0x04, 0xa1, 0x50, 0x36, 0x2c, 0x84, 0x46, 0x6d, 0xd6, 0x35, 0x5c, 0x44, 0x05, 0x6c, 0x08,
        0xb8, 0x0d, 0x43, 0xd6, 0x01, 0x1b, 0x8a, 0x45, 0xf2, 0x00, 0xa0, 0x0a, 0x1b, 0x9b, 0x5d, 0x9b, 0x4b, 0x1a, 0x59, 0x99, 0x1b, 0xdd, 0x94, 0x20, 0xa8, 0x42, 0x86, 0xe7, 0x62, 0x57, 0x26, 0x37,
        0x97, 0xf6, 0xe6, 0x36, 0x25, 0x20, 0x9a, 0x90, 0xe1, 0xb9, 0xd8, 0x85, 0xb1, 0xd9, 0x95, 0xc9, 0x4d, 0x09, 0x8c, 0x3a, 0x64, 0x78, 0x2e, 0x73, 0x68, 0x61, 0x64, 0x65, 0x72, 0x4d, 0x6f, 0x64,
        0x65, 0x6c, 0x53, 0x82, 0xa4, 0x12, 0x19, 0x9e, 0x0b, 0x5d, 0x1e, 0x5c, 0x59, 0x90, 0x9b, 0xdb, 0x1b, 0x5d, 0x18, 0x5d, 0xda, 0x9b, 0xdb, 0xdc, 0x94, 0xc0, 0xa9, 0x43, 0x86, 0xe7, 0x62, 0x97,
        0x56, 0x76, 0x97, 0x44, 0x36, 0x45, 0x17, 0x46, 0x57, 0x36, 0x25, 0x80, 0xea, 0x90, 0xe1, 0xb9, 0x94, 0xb9, 0xd1, 0xc9, 0xe5, 0x41, 0xbd, 0xa5, 0xb9, 0xd1, 0xcd, 0x4d, 0x09, 0x3c, 0x00, 0x00,
        0x00, 0x79, 0x18, 0x00, 0x00, 0x42, 0x00, 0x00, 0x00, 0x33, 0x08, 0x80, 0x1c, 0xc4, 0xe1, 0x1c, 0x66, 0x14, 0x01, 0x3d, 0x88, 0x43, 0x38, 0x84, 0xc3, 0x8c, 0x42, 0x80, 0x07, 0x79, 0x78, 0x07,
        0x73, 0x98, 0x71, 0x0c, 0xe6, 0x00, 0x0f, 0xed, 0x10, 0x0e, 0xf4, 0x80, 0x0e, 0x33, 0x0c, 0x42, 0x1e, 0xc2, 0xc1, 0x1d, 0xce, 0xa1, 0x1c, 0x66, 0x30, 0x05, 0x3d, 0x88, 0x43, 0x38, 0x84, 0x83,
        0x1b, 0xcc, 0x03, 0x3d, 0xc8, 0x43, 0x3d, 0x8c, 0x03, 0x3d, 0xcc, 0x78, 0x8c, 0x74, 0x70, 0x07, 0x7b, 0x08, 0x07, 0x79, 0x48, 0x87, 0x70, 0x70, 0x07, 0x7a, 0x70, 0x03, 0x76, 0x78, 0x87, 0x70,
        0x20, 0x87, 0x19, 0xcc, 0x11, 0x0e, 0xec, 0x90, 0x0e, 0xe1, 0x30, 0x0f, 0x6e, 0x30, 0x0f, 0xe3, 0xf0, 0x0e, 0xf0, 0x50, 0x0e, 0x33, 0x10, 0xc4, 0x1d, 0xde, 0x21, 0x1c, 0xd8, 0x21, 0x1d, 0xc2,
        0x61, 0x1e, 0x66, 0x30, 0x89, 0x3b, 0xbc, 0x83, 0x3b, 0xd0, 0x43, 0x39, 0xb4, 0x03, 0x3c, 0xbc, 0x83, 0x3c, 0x84, 0x03, 0x3b, 0xcc, 0xf0, 0x14, 0x76, 0x60, 0x07, 0x7b, 0x68, 0x07, 0x37, 0x68,
        0x87, 0x72, 0x68, 0x07, 0x37, 0x80, 0x87, 0x70, 0x90, 0x87, 0x70, 0x60, 0x07, 0x76, 0x28, 0x07, 0x76, 0xf8, 0x05, 0x76, 0x78, 0x87, 0x77, 0x80, 0x87, 0x5f, 0x08, 0x87, 0x71, 0x18, 0x87, 0x72,
        0x98, 0x87, 0x79, 0x98, 0x81, 0x2c, 0xee, 0xf0, 0x0e, 0xee, 0xe0, 0x0e, 0xf5, 0xc0, 0x0e, 0xec, 0x30, 0x03, 0x62, 0xc8, 0xa1, 0x1c, 0xe4, 0xa1, 0x1c, 0xcc, 0xa1, 0x1c, 0xe4, 0xa1, 0x1c, 0xdc,
        0x61, 0x1c, 0xca, 0x21, 0x1c, 0xc4, 0x81, 0x1d, 0xca, 0x61, 0x06, 0xd6, 0x90, 0x43, 0x39, 0xc8, 0x43, 0x39, 0x98, 0x43, 0x39, 0xc8, 0x43, 0x39, 0xb8, 0xc3, 0x38, 0x94, 0x43, 0x38, 0x88, 0x03,
        0x3b, 0x94, 0xc3, 0x2f, 0xbc, 0x83, 0x3c, 0xfc, 0x82, 0x3b, 0xd4, 0x03, 0x3b, 0xb0, 0x03, 0x00, 0x00, 0x71, 0x20, 0x00, 0x00, 0x0b, 0x00, 0x00, 0x00, 0x16, 0x30, 0x0d, 0x97, 0xef, 0x3c, 0xfe,
        0xe2, 0x00, 0x83, 0xd8, 0x3c, 0xd4, 0xe4, 0x17, 0xb7, 0x6d, 0x00, 0x04, 0x03, 0x20, 0x8d, 0x09, 0x54, 0xc3, 0xe5, 0x3b, 0x8f, 0x2f, 0x4d, 0x4e, 0x44, 0xa0, 0xd4, 0xf4, 0x50, 0x93, 0x5f, 0xdc,
        0x36, 0x00, 0x00, 0x00, 0x00, 0x61, 0x20, 0x00, 0x00, 0x1e, 0x00, 0x00, 0x00, 0x13, 0x04, 0x41, 0x2c, 0x10, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x44, 0x33, 0x00, 0xa5, 0x40, 0x55, 0x02,
        0x45, 0x00, 0x00, 0x00, 0x00, 0x23, 0x06, 0x09, 0x00, 0x82, 0x60, 0x60, 0x3c, 0x0b, 0xc3, 0x20, 0xc4, 0x88, 0x41, 0x02, 0x80, 0x20, 0x18, 0x18, 0x10, 0xd3, 0x34, 0x43, 0x31, 0x62, 0x90, 0x00,
        0x20, 0x08, 0x06, 0x46, 0xd4, 0x38, 0xce, 0x60, 0x8c, 0x18, 0x24, 0x00, 0x08, 0x82, 0x81, 0x21, 0x39, 0xcf, 0x93, 0x1c, 0x23, 0x06, 0x09, 0x00, 0x82, 0x60, 0x80, 0x48, 0x07, 0x04, 0x31, 0xc4,
        0x88, 0x41, 0x02, 0x80, 0x20, 0x18, 0x20, 0xd2, 0x01, 0x41, 0xc6, 0x30, 0x62, 0x90, 0x00, 0x20, 0x08, 0x06, 0x88, 0x74, 0x40, 0x50, 0x21, 0x8c, 0x18, 0x24, 0x00, 0x08, 0x82, 0x01, 0x22, 0x1d,
        0x10, 0xa4, 0x04, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00,
    };
    const D3D12_SHADER_BYTECODE vs = {
      use_dxil ? (const void*)vs_dxil : (const void*)vs_dxbc,
      use_dxil ? sizeof(vs_dxil) : sizeof(vs_dxbc),
    };
#if 0
    void main() { }
#endif
    static const DWORD ps_dxbc[] =
    {
        0x43425844, 0x499d4ed5, 0xbbe2842c, 0x179313ee, 0xde5cd5d9, 0x00000001, 0x00000064, 0x00000003,
        0x0000002c, 0x0000003c, 0x0000004c, 0x4e475349, 0x00000008, 0x00000000, 0x00000008, 0x4e47534f,
        0x00000008, 0x00000000, 0x00000008, 0x58454853, 0x00000010, 0x00000050, 0x00000004, 0x0100086a,
        0x0100003e,
    };
    static const BYTE ps_dxil[] =
    {
        0x44, 0x58, 0x42, 0x43, 0xe9, 0xaf, 0xe0, 0x0e, 0x69, 0x4d, 0x92, 0x13, 0xf2, 0x58, 0xdf, 0x54, 0xf6, 0x12, 0x3e, 0x16, 0x01, 0x00, 0x00, 0x00, 0xf8, 0x03, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00,
        0x34, 0x00, 0x00, 0x00, 0x44, 0x00, 0x00, 0x00, 0x54, 0x00, 0x00, 0x00, 0x64, 0x00, 0x00, 0x00, 0xa4, 0x00, 0x00, 0x00, 0x53, 0x46, 0x49, 0x30, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x49, 0x53, 0x47, 0x31, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x4f, 0x53, 0x47, 0x31, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x08, 0x00, 0x00, 0x00, 0x50, 0x53, 0x56, 0x30, 0x38, 0x00, 0x00, 0x00, 0x24, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x44, 0x58, 0x49, 0x4c, 0x4c, 0x03, 0x00, 0x00, 0x60, 0x00, 0x00, 0x00, 0xd3, 0x00, 0x00, 0x00, 0x44, 0x58, 0x49, 0x4c, 0x00, 0x01, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00,
        0x34, 0x03, 0x00, 0x00, 0x42, 0x43, 0xc0, 0xde, 0x21, 0x0c, 0x00, 0x00, 0xca, 0x00, 0x00, 0x00, 0x0b, 0x82, 0x20, 0x00, 0x02, 0x00, 0x00, 0x00, 0x13, 0x00, 0x00, 0x00, 0x07, 0x81, 0x23, 0x91,
        0x41, 0xc8, 0x04, 0x49, 0x06, 0x10, 0x32, 0x39, 0x92, 0x01, 0x84, 0x0c, 0x25, 0x05, 0x08, 0x19, 0x1e, 0x04, 0x8b, 0x62, 0x80, 0x0c, 0x45, 0x02, 0x42, 0x92, 0x0b, 0x42, 0x64, 0x10, 0x32, 0x14,
        0x38, 0x08, 0x18, 0x4b, 0x0a, 0x32, 0x32, 0x88, 0x48, 0x90, 0x14, 0x20, 0x43, 0x46, 0x88, 0xa5, 0x00, 0x19, 0x32, 0x42, 0xe4, 0x48, 0x0e, 0x90, 0x91, 0x21, 0xc4, 0x50, 0x41, 0x51, 0x81, 0x8c,
        0xe1, 0x83, 0xe5, 0x8a, 0x04, 0x19, 0x46, 0x06, 0x89, 0x20, 0x00, 0x00, 0x0b, 0x00, 0x00, 0x00, 0x32, 0x22, 0xc8, 0x08, 0x20, 0x64, 0x85, 0x04, 0x93, 0x21, 0xa4, 0x84, 0x04, 0x93, 0x21, 0xe3,
        0x84, 0xa1, 0x90, 0x14, 0x12, 0x4c, 0x86, 0x8c, 0x0b, 0x84, 0x64, 0x4c, 0x10, 0x14, 0x23, 0x00, 0x25, 0x00, 0x65, 0x20, 0x60, 0x8e, 0x00, 0x0c, 0x00, 0x00, 0x00, 0x00, 0x13, 0x14, 0x72, 0xc0,
        0x87, 0x74, 0x60, 0x87, 0x36, 0x68, 0x87, 0x79, 0x68, 0x03, 0x72, 0xc0, 0x87, 0x0d, 0xaf, 0x50, 0x0e, 0x6d, 0xd0, 0x0e, 0x7a, 0x50, 0x0e, 0x6d, 0x00, 0x0f, 0x7a, 0x30, 0x07, 0x72, 0xa0, 0x07,
        0x73, 0x20, 0x07, 0x6d, 0x90, 0x0e, 0x71, 0xa0, 0x07, 0x73, 0x20, 0x07, 0x6d, 0x90, 0x0e, 0x78, 0xa0, 0x07, 0x73, 0x20, 0x07, 0x6d, 0x90, 0x0e, 0x71, 0x60, 0x07, 0x7a, 0x30, 0x07, 0x72, 0xd0,
        0x06, 0xe9, 0x30, 0x07, 0x72, 0xa0, 0x07, 0x73, 0x20, 0x07, 0x6d, 0x90, 0x0e, 0x76, 0x40, 0x07, 0x7a, 0x60, 0x07, 0x74, 0xd0, 0x06, 0xe6, 0x10, 0x07, 0x76, 0xa0, 0x07, 0x73, 0x20, 0x07, 0x6d,
        0x60, 0x0e, 0x73, 0x20, 0x07, 0x7a, 0x30, 0x07, 0x72, 0xd0, 0x06, 0xe6, 0x60, 0x07, 0x74, 0xa0, 0x07, 0x76, 0x40, 0x07, 0x6d, 0xe0, 0x0e, 0x78, 0xa0, 0x07, 0x71, 0x60, 0x07, 0x7a, 0x30, 0x07,
        0x72, 0xa0, 0x07, 0x76, 0x40, 0x07, 0x43, 0x9e, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xb2, 0x40, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0x32, 0x1e, 0x98, 0x0c,
        0x19, 0x11, 0x4c, 0x90, 0x8c, 0x09, 0x26, 0x47, 0xc6, 0x04, 0x43, 0x62, 0x09, 0x8c, 0x00, 0x14, 0x42, 0x31, 0x00, 0x00, 0x79, 0x18, 0x00, 0x00, 0x2b, 0x00, 0x00, 0x00, 0x1a, 0x03, 0x4c, 0x90,
        0x46, 0x02, 0x13, 0xc4, 0x83, 0x0c, 0x6f, 0x0c, 0x24, 0xc6, 0x45, 0x66, 0x43, 0x10, 0x4c, 0x10, 0x88, 0x60, 0x82, 0x40, 0x08, 0x1b, 0x84, 0x81, 0x98, 0x20, 0x10, 0xc3, 0x06, 0x61, 0x30, 0x28,
        0xc0, 0xcd, 0x4d, 0x10, 0x08, 0x62, 0xc3, 0x80, 0x24, 0xc4, 0x04, 0x41, 0x00, 0x36, 0x00, 0x1b, 0x86, 0x81, 0x61, 0x36, 0x04, 0xcd, 0x86, 0x61, 0x58, 0x1c, 0x12, 0x6d, 0x61, 0x69, 0x6e, 0x1b,
        0x8a, 0x05, 0x02, 0x00, 0xa0, 0x0a, 0x1b, 0x9b, 0x5d, 0x9b, 0x4b, 0x1a, 0x59, 0x99, 0x1b, 0xdd, 0x94, 0x20, 0xa8, 0x42, 0x86, 0xe7, 0x62, 0x57, 0x26, 0x37, 0x97, 0xf6, 0xe6, 0x36, 0x25, 0x20,
        0x9a, 0x90, 0xe1, 0xb9, 0xd8, 0x85, 0xb1, 0xd9, 0x95, 0xc9, 0x4d, 0x09, 0x8c, 0x3a, 0x64, 0x78, 0x2e, 0x73, 0x68, 0x61, 0x64, 0x65, 0x72, 0x4d, 0x6f, 0x64, 0x65, 0x6c, 0x53, 0x82, 0xa4, 0x12,
        0x19, 0x9e, 0x0b, 0x5d, 0x1e, 0x5c, 0x59, 0x90, 0x9b, 0xdb, 0x1b, 0x5d, 0x18, 0x5d, 0xda, 0x9b, 0xdb, 0xdc, 0x94, 0xc0, 0xa9, 0x43, 0x86, 0xe7, 0x52, 0xe6, 0x46, 0x27, 0x97, 0x07, 0xf5, 0x96,
        0xe6, 0x46, 0x37, 0x37, 0x25, 0x80, 0x00, 0x00, 0x79, 0x18, 0x00, 0x00, 0x42, 0x00, 0x00, 0x00, 0x33, 0x08, 0x80, 0x1c, 0xc4, 0xe1, 0x1c, 0x66, 0x14, 0x01, 0x3d, 0x88, 0x43, 0x38, 0x84, 0xc3,
        0x8c, 0x42, 0x80, 0x07, 0x79, 0x78, 0x07, 0x73, 0x98, 0x71, 0x0c, 0xe6, 0x00, 0x0f, 0xed, 0x10, 0x0e, 0xf4, 0x80, 0x0e, 0x33, 0x0c, 0x42, 0x1e, 0xc2, 0xc1, 0x1d, 0xce, 0xa1, 0x1c, 0x66, 0x30,
        0x05, 0x3d, 0x88, 0x43, 0x38, 0x84, 0x83, 0x1b, 0xcc, 0x03, 0x3d, 0xc8, 0x43, 0x3d, 0x8c, 0x03, 0x3d, 0xcc, 0x78, 0x8c, 0x74, 0x70, 0x07, 0x7b, 0x08, 0x07, 0x79, 0x48, 0x87, 0x70, 0x70, 0x07,
        0x7a, 0x70, 0x03, 0x76, 0x78, 0x87, 0x70, 0x20, 0x87, 0x19, 0xcc, 0x11, 0x0e, 0xec, 0x90, 0x0e, 0xe1, 0x30, 0x0f, 0x6e, 0x30, 0x0f, 0xe3, 0xf0, 0x0e, 0xf0, 0x50, 0x0e, 0x33, 0x10, 0xc4, 0x1d,
        0xde, 0x21, 0x1c, 0xd8, 0x21, 0x1d, 0xc2, 0x61, 0x1e, 0x66, 0x30, 0x89, 0x3b, 0xbc, 0x83, 0x3b, 0xd0, 0x43, 0x39, 0xb4, 0x03, 0x3c, 0xbc, 0x83, 0x3c, 0x84, 0x03, 0x3b, 0xcc, 0xf0, 0x14, 0x76,
        0x60, 0x07, 0x7b, 0x68, 0x07, 0x37, 0x68, 0x87, 0x72, 0x68, 0x07, 0x37, 0x80, 0x87, 0x70, 0x90, 0x87, 0x70, 0x60, 0x07, 0x76, 0x28, 0x07, 0x76, 0xf8, 0x05, 0x76, 0x78, 0x87, 0x77, 0x80, 0x87,
        0x5f, 0x08, 0x87, 0x71, 0x18, 0x87, 0x72, 0x98, 0x87, 0x79, 0x98, 0x81, 0x2c, 0xee, 0xf0, 0x0e, 0xee, 0xe0, 0x0e, 0xf5, 0xc0, 0x0e, 0xec, 0x30, 0x03, 0x62, 0xc8, 0xa1, 0x1c, 0xe4, 0xa1, 0x1c,
        0xcc, 0xa1, 0x1c, 0xe4, 0xa1, 0x1c, 0xdc, 0x61, 0x1c, 0xca, 0x21, 0x1c, 0xc4, 0x81, 0x1d, 0xca, 0x61, 0x06, 0xd6, 0x90, 0x43, 0x39, 0xc8, 0x43, 0x39, 0x98, 0x43, 0x39, 0xc8, 0x43, 0x39, 0xb8,
        0xc3, 0x38, 0x94, 0x43, 0x38, 0x88, 0x03, 0x3b, 0x94, 0xc3, 0x2f, 0xbc, 0x83, 0x3c, 0xfc, 0x82, 0x3b, 0xd4, 0x03, 0x3b, 0xb0, 0x03, 0x00, 0x00, 0x71, 0x20, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00,
        0x06, 0x40, 0x30, 0x00, 0xd2, 0x00, 0x00, 0x00, 0x61, 0x20, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x13, 0x04, 0x01, 0x02, 0x00, 0x00, 0x00, 0x00,
    };
    const D3D12_SHADER_BYTECODE ps = {
      use_dxil ? (const void*)ps_dxil : (const void*)ps_dxbc,
      use_dxil ? sizeof(ps_dxil) : sizeof(ps_dxbc),
    };
#if 0
    void main(in uint ic : SV_InnerCoverage) {
            if (ic == 0)
                    discard;
    }
#endif
    static const DWORD ps_underestimate_dxbc[] =
    {
        0x43425844, 0x8f1e8d53, 0x05b0f2c6, 0xe8d795db, 0xc9e0ffef, 0x00000001, 0x00000088, 0x00000004,
        0x00000030, 0x00000040, 0x00000050, 0x00000078, 0x4e475349, 0x00000008, 0x00000000, 0x00000008,
        0x4e47534f, 0x00000008, 0x00000000, 0x00000008, 0x58454853, 0x00000020, 0x00000050, 0x00000008,
        0x0100086a, 0x0200005f, 0x0002a001, 0x0200000d, 0x0002a00a, 0x0100003e, 0x30494653, 0x00000008,
        0x00000400, 0x00000000,
    };
    static const BYTE ps_underestimate_dxil[] =
    {
        0x44, 0x58, 0x42, 0x43, 0x44, 0xdd, 0x61, 0xc1, 0x16, 0xde, 0xe1, 0x6b, 0x12, 0xae, 0x63, 0xf3, 0xd8, 0x99, 0x57, 0x60, 0x01, 0x00, 0x00, 0x00, 0xb0, 0x04, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00,
        0x34, 0x00, 0x00, 0x00, 0x44, 0x00, 0x00, 0x00, 0x54, 0x00, 0x00, 0x00, 0x64, 0x00, 0x00, 0x00, 0xa4, 0x00, 0x00, 0x00, 0x53, 0x46, 0x49, 0x30, 0x08, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x49, 0x53, 0x47, 0x31, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x4f, 0x53, 0x47, 0x31, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x08, 0x00, 0x00, 0x00, 0x50, 0x53, 0x56, 0x30, 0x38, 0x00, 0x00, 0x00, 0x24, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x44, 0x58, 0x49, 0x4c, 0x04, 0x04, 0x00, 0x00, 0x60, 0x00, 0x00, 0x00, 0x01, 0x01, 0x00, 0x00, 0x44, 0x58, 0x49, 0x4c, 0x00, 0x01, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00,
        0xec, 0x03, 0x00, 0x00, 0x42, 0x43, 0xc0, 0xde, 0x21, 0x0c, 0x00, 0x00, 0xf8, 0x00, 0x00, 0x00, 0x0b, 0x82, 0x20, 0x00, 0x02, 0x00, 0x00, 0x00, 0x13, 0x00, 0x00, 0x00, 0x07, 0x81, 0x23, 0x91,
        0x41, 0xc8, 0x04, 0x49, 0x06, 0x10, 0x32, 0x39, 0x92, 0x01, 0x84, 0x0c, 0x25, 0x05, 0x08, 0x19, 0x1e, 0x04, 0x8b, 0x62, 0x80, 0x10, 0x45, 0x02, 0x42, 0x92, 0x0b, 0x42, 0x84, 0x10, 0x32, 0x14,
        0x38, 0x08, 0x18, 0x4b, 0x0a, 0x32, 0x42, 0x88, 0x48, 0x90, 0x14, 0x20, 0x43, 0x46, 0x88, 0xa5, 0x00, 0x19, 0x32, 0x42, 0xe4, 0x48, 0x0e, 0x90, 0x11, 0x22, 0xc4, 0x50, 0x41, 0x51, 0x81, 0x8c,
        0xe1, 0x83, 0xe5, 0x8a, 0x04, 0x21, 0x46, 0x06, 0x51, 0x18, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0x1b, 0x8c, 0xe0, 0xff, 0xff, 0xff, 0xff, 0x07, 0x40, 0x02, 0xa8, 0x0d, 0x84, 0xf0, 0xff, 0xff,
        0xff, 0xff, 0x03, 0x20, 0x01, 0x00, 0x00, 0x00, 0x49, 0x18, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x13, 0x82, 0x60, 0x42, 0x20, 0x00, 0x00, 0x00, 0x89, 0x20, 0x00, 0x00, 0x0f, 0x00, 0x00, 0x00,
        0x32, 0x22, 0x08, 0x09, 0x20, 0x64, 0x85, 0x04, 0x13, 0x22, 0xa4, 0x84, 0x04, 0x13, 0x22, 0xe3, 0x84, 0xa1, 0x90, 0x14, 0x12, 0x4c, 0x88, 0x8c, 0x0b, 0x84, 0x84, 0x4c, 0x10, 0x30, 0x23, 0x00,
        0x25, 0x00, 0x8a, 0x39, 0x02, 0x30, 0x28, 0xc2, 0x0c, 0xd1, 0x1c, 0x41, 0x50, 0x06, 0x18, 0xa3, 0x1b, 0x08, 0x98, 0x23, 0x00, 0x85, 0x29, 0x00, 0x00, 0x00, 0x00, 0x00, 0x13, 0x14, 0x72, 0xc0,
        0x87, 0x74, 0x60, 0x87, 0x36, 0x68, 0x87, 0x79, 0x68, 0x03, 0x72, 0xc0, 0x87, 0x0d, 0xaf, 0x50, 0x0e, 0x6d, 0xd0, 0x0e, 0x7a, 0x50, 0x0e, 0x6d, 0x00, 0x0f, 0x7a, 0x30, 0x07, 0x72, 0xa0, 0x07,
        0x73, 0x20, 0x07, 0x6d, 0x90, 0x0e, 0x71, 0xa0, 0x07, 0x73, 0x20, 0x07, 0x6d, 0x90, 0x0e, 0x78, 0xa0, 0x07, 0x73, 0x20, 0x07, 0x6d, 0x90, 0x0e, 0x71, 0x60, 0x07, 0x7a, 0x30, 0x07, 0x72, 0xd0,
        0x06, 0xe9, 0x30, 0x07, 0x72, 0xa0, 0x07, 0x73, 0x20, 0x07, 0x6d, 0x90, 0x0e, 0x76, 0x40, 0x07, 0x7a, 0x60, 0x07, 0x74, 0xd0, 0x06, 0xe6, 0x10, 0x07, 0x76, 0xa0, 0x07, 0x73, 0x20, 0x07, 0x6d,
        0x60, 0x0e, 0x73, 0x20, 0x07, 0x7a, 0x30, 0x07, 0x72, 0xd0, 0x06, 0xe6, 0x60, 0x07, 0x74, 0xa0, 0x07, 0x76, 0x40, 0x07, 0x6d, 0xe0, 0x0e, 0x78, 0xa0, 0x07, 0x71, 0x60, 0x07, 0x7a, 0x30, 0x07,
        0x72, 0xa0, 0x07, 0x76, 0x40, 0x07, 0x43, 0x9e, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x86, 0x3c, 0x04, 0x10, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x0c, 0x79, 0x0e, 0x20, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xc8, 0x02, 0x01, 0x07, 0x00, 0x00, 0x00, 0x32, 0x1e, 0x98, 0x10, 0x19, 0x11, 0x4c, 0x90, 0x8c, 0x09, 0x26, 0x47,
        0xc6, 0x04, 0x43, 0x9a, 0x12, 0x18, 0x01, 0x28, 0x84, 0x62, 0x20, 0x2d, 0x40, 0x08, 0x00, 0x00, 0x79, 0x18, 0x00, 0x00, 0x2d, 0x00, 0x00, 0x00, 0x1a, 0x03, 0x4c, 0x90, 0x46, 0x02, 0x13, 0xc4,
        0x83, 0x0c, 0x6f, 0x0c, 0x24, 0xc6, 0x45, 0x66, 0x43, 0x10, 0x4c, 0x10, 0x86, 0x61, 0x82, 0x30, 0x10, 0x1b, 0x84, 0x81, 0x98, 0x20, 0x0c, 0xc5, 0x06, 0x61, 0x30, 0x28, 0xc0, 0xcd, 0x4d, 0x10,
        0x06, 0x63, 0xc3, 0x80, 0x24, 0xc4, 0x04, 0x41, 0x00, 0x36, 0x00, 0x1b, 0x06, 0x82, 0x61, 0x36, 0x04, 0xcd, 0x86, 0x61, 0x58, 0x1c, 0x12, 0x6d, 0x61, 0x69, 0x6e, 0x13, 0x04, 0xe5, 0xd8, 0x20,
        0x10, 0xd1, 0x86, 0x62, 0x81, 0x00, 0x40, 0xaa, 0xc2, 0xc6, 0x66, 0xd7, 0xe6, 0x92, 0x46, 0x56, 0xe6, 0x46, 0x37, 0x25, 0x08, 0xaa, 0x90, 0xe1, 0xb9, 0xd8, 0x95, 0xc9, 0xcd, 0xa5, 0xbd, 0xb9,
        0x4d, 0x09, 0x88, 0x26, 0x64, 0x78, 0x2e, 0x76, 0x61, 0x6c, 0x76, 0x65, 0x72, 0x53, 0x02, 0xa3, 0x0e, 0x19, 0x9e, 0xcb, 0x1c, 0x5a, 0x18, 0x59, 0x99, 0x5c, 0xd3, 0x1b, 0x59, 0x19, 0xdb, 0x94,
        0x20, 0xa9, 0x44, 0x86, 0xe7, 0x42, 0x97, 0x07, 0x57, 0x16, 0xe4, 0xe6, 0xf6, 0x46, 0x17, 0x46, 0x97, 0xf6, 0xe6, 0x36, 0x37, 0x25, 0x70, 0xea, 0x90, 0xe1, 0xb9, 0x94, 0xb9, 0xd1, 0xc9, 0xe5,
        0x41, 0xbd, 0xa5, 0xb9, 0xd1, 0xcd, 0x4d, 0x09, 0x24, 0x00, 0x00, 0x00, 0x79, 0x18, 0x00, 0x00, 0x42, 0x00, 0x00, 0x00, 0x33, 0x08, 0x80, 0x1c, 0xc4, 0xe1, 0x1c, 0x66, 0x14, 0x01, 0x3d, 0x88,
        0x43, 0x38, 0x84, 0xc3, 0x8c, 0x42, 0x80, 0x07, 0x79, 0x78, 0x07, 0x73, 0x98, 0x71, 0x0c, 0xe6, 0x00, 0x0f, 0xed, 0x10, 0x0e, 0xf4, 0x80, 0x0e, 0x33, 0x0c, 0x42, 0x1e, 0xc2, 0xc1, 0x1d, 0xce,
        0xa1, 0x1c, 0x66, 0x30, 0x05, 0x3d, 0x88, 0x43, 0x38, 0x84, 0x83, 0x1b, 0xcc, 0x03, 0x3d, 0xc8, 0x43, 0x3d, 0x8c, 0x03, 0x3d, 0xcc, 0x78, 0x8c, 0x74, 0x70, 0x07, 0x7b, 0x08, 0x07, 0x79, 0x48,
        0x87, 0x70, 0x70, 0x07, 0x7a, 0x70, 0x03, 0x76, 0x78, 0x87, 0x70, 0x20, 0x87, 0x19, 0xcc, 0x11, 0x0e, 0xec, 0x90, 0x0e, 0xe1, 0x30, 0x0f, 0x6e, 0x30, 0x0f, 0xe3, 0xf0, 0x0e, 0xf0, 0x50, 0x0e,
        0x33, 0x10, 0xc4, 0x1d, 0xde, 0x21, 0x1c, 0xd8, 0x21, 0x1d, 0xc2, 0x61, 0x1e, 0x66, 0x30, 0x89, 0x3b, 0xbc, 0x83, 0x3b, 0xd0, 0x43, 0x39, 0xb4, 0x03, 0x3c, 0xbc, 0x83, 0x3c, 0x84, 0x03, 0x3b,
        0xcc, 0xf0, 0x14, 0x76, 0x60, 0x07, 0x7b, 0x68, 0x07, 0x37, 0x68, 0x87, 0x72, 0x68, 0x07, 0x37, 0x80, 0x87, 0x70, 0x90, 0x87, 0x70, 0x60, 0x07, 0x76, 0x28, 0x07, 0x76, 0xf8, 0x05, 0x76, 0x78,
        0x87, 0x77, 0x80, 0x87, 0x5f, 0x08, 0x87, 0x71, 0x18, 0x87, 0x72, 0x98, 0x87, 0x79, 0x98, 0x81, 0x2c, 0xee, 0xf0, 0x0e, 0xee, 0xe0, 0x0e, 0xf5, 0xc0, 0x0e, 0xec, 0x30, 0x03, 0x62, 0xc8, 0xa1,
        0x1c, 0xe4, 0xa1, 0x1c, 0xcc, 0xa1, 0x1c, 0xe4, 0xa1, 0x1c, 0xdc, 0x61, 0x1c, 0xca, 0x21, 0x1c, 0xc4, 0x81, 0x1d, 0xca, 0x61, 0x06, 0xd6, 0x90, 0x43, 0x39, 0xc8, 0x43, 0x39, 0x98, 0x43, 0x39,
        0xc8, 0x43, 0x39, 0xb8, 0xc3, 0x38, 0x94, 0x43, 0x38, 0x88, 0x03, 0x3b, 0x94, 0xc3, 0x2f, 0xbc, 0x83, 0x3c, 0xfc, 0x82, 0x3b, 0xd4, 0x03, 0x3b, 0xb0, 0x03, 0x00, 0x00, 0x71, 0x20, 0x00, 0x00,
        0x0a, 0x00, 0x00, 0x00, 0x26, 0xd0, 0x0c, 0x97, 0xef, 0x3c, 0xfe, 0x80, 0x48, 0x02, 0x10, 0x0d, 0x06, 0x40, 0x30, 0x00, 0xd2, 0x58, 0xc0, 0x35, 0x5c, 0xbe, 0xf3, 0xf8, 0x48, 0xd3, 0x10, 0x11,
        0xe7, 0x54, 0x44, 0x04, 0x18, 0x84, 0x8f, 0xdc, 0x36, 0x00, 0x00, 0x00, 0x61, 0x20, 0x00, 0x00, 0x0e, 0x00, 0x00, 0x00, 0x13, 0x04, 0x43, 0x2c, 0x10, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00,
        0x34, 0x85, 0x1b, 0x50, 0xa4, 0x01, 0x64, 0x35, 0x00, 0x00, 0x00, 0x00, 0x23, 0x06, 0x05, 0x00, 0x82, 0x60, 0x40, 0x28, 0xc3, 0x70, 0x43, 0x80, 0x80, 0xc1, 0x2c, 0x43, 0x20, 0x04, 0x23, 0x06,
        0x06, 0x00, 0x82, 0x60, 0x70, 0x2c, 0xc4, 0x30, 0x4b, 0x20, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00,
    };
    const D3D12_SHADER_BYTECODE ps_underestimate = {
        use_dxil ? (const void*)ps_underestimate_dxil : (const void*)ps_underestimate_dxbc,
        use_dxil ? sizeof(ps_underestimate_dxil) : sizeof(ps_underestimate_dxbc),
    };

    static const D3D12_INPUT_ELEMENT_DESC layout_desc[] =
    {
        {"position", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    };
    static const struct vec4 vertices[] =
    {
        { 0.5f, -0.5f,  1.0f, 1.0f},
        {-0.5f, -0.5f,  1.0f, 1.0f},
        { 0.0f,  0.5f,  1.0f, 1.0f},
    };
    static const struct
    {
        unsigned int stencil_ref;
        D3D12_CONSERVATIVE_RASTERIZATION_TIER min_tier;
    }
    tests[] =
    {
        { 0x1, D3D12_CONSERVATIVE_RASTERIZATION_TIER_1 },
        { 0x3, D3D12_CONSERVATIVE_RASTERIZATION_TIER_1 },
        { 0x7, D3D12_CONSERVATIVE_RASTERIZATION_TIER_3 },
    };

    memset(&desc, 0, sizeof(desc));
    desc.no_root_signature = true;
    if (!init_test_context(&context, &desc))
        return;

    hr = ID3D12Device_CheckFeatureSupport(context.device, D3D12_FEATURE_D3D12_OPTIONS, &options, sizeof(options));
    ok(hr == S_OK, "Failed to check feature support, hr %#x.\n", hr);

    if (!options.ConservativeRasterizationTier)
    {
        skip("Conservative rasterization not supported by device.\n");
        destroy_test_context(&context);
        return;
    }

    heap_desc.Type = D3D12_QUERY_HEAP_TYPE_OCCLUSION;
    heap_desc.Count = ARRAY_SIZE(tests);
    heap_desc.NodeMask = 0;
    hr = ID3D12Device_CreateQueryHeap(context.device, &heap_desc, &IID_ID3D12QueryHeap, (void **)&query_heap);
    ok(SUCCEEDED(hr), "Failed to create query heap, hr %#x.\n", hr);

    readback = create_readback_buffer(context.device, ARRAY_SIZE(tests) * sizeof(uint64_t));

    root_signature_desc.NumParameters = 0;
    root_signature_desc.pParameters = NULL;
    root_signature_desc.NumStaticSamplers = 0;
    root_signature_desc.pStaticSamplers = NULL;
    root_signature_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    hr = create_root_signature(context.device, &root_signature_desc, &root_signature);
    ok(hr == S_OK, "Failed to create root signature, hr %#x.\n", hr);

    command_list = context.list;
    queue = context.queue;

    init_depth_stencil(&ds, context.device, 32, 32, 1, 1, DXGI_FORMAT_D32_FLOAT_S8X24_UINT, 0, NULL);

    vb = create_upload_buffer(context.device, sizeof(vertices), vertices);
    vbv.BufferLocation = ID3D12Resource_GetGPUVirtualAddress(vb);
    vbv.StrideInBytes = sizeof(*vertices);
    vbv.SizeInBytes = sizeof(vertices);

    input_layout.pInputElementDescs = layout_desc;
    input_layout.NumElements = ARRAY_SIZE(layout_desc);
    init_pipeline_state_desc(&pso_desc, root_signature, DXGI_FORMAT_UNKNOWN, &vs, &ps, &input_layout);
    pso_desc.DSVFormat = DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
    pso_desc.DepthStencilState.DepthEnable = true;
    pso_desc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    pso_desc.DepthStencilState.StencilEnable = true;
    pso_desc.DepthStencilState.StencilWriteMask = 0x01;
    pso_desc.DepthStencilState.FrontFace.StencilFailOp = D3D12_STENCIL_OP_REPLACE;
    pso_desc.DepthStencilState.FrontFace.StencilDepthFailOp = D3D12_STENCIL_OP_REPLACE;
    pso_desc.DepthStencilState.FrontFace.StencilPassOp = D3D12_STENCIL_OP_REPLACE;
    pso_desc.DepthStencilState.FrontFace.StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    pso_desc.DepthStencilState.BackFace = pso_desc.DepthStencilState.FrontFace;
    pso_desc.RasterizerState.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_ON;
    hr = ID3D12Device_CreateGraphicsPipelineState(context.device, &pso_desc, &IID_ID3D12PipelineState, (void **)&pipeline_conservative_overestimate);
    ok(hr == S_OK, "Failed to create pipeline, hr %#x.\n", hr);

    pso_desc.DepthStencilState.StencilWriteMask = 0x02;
    pso_desc.RasterizerState.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;
    hr = ID3D12Device_CreateGraphicsPipelineState(context.device, &pso_desc, &IID_ID3D12PipelineState, (void **)&pipeline_conservative_off);
    ok(hr == S_OK, "Failed to create pipeline, hr %#x.\n", hr);

    if (options.ConservativeRasterizationTier >= D3D12_CONSERVATIVE_RASTERIZATION_TIER_3)
    {
        pso_desc.PS = ps_underestimate;
        pso_desc.DepthStencilState.StencilWriteMask = 0x04;
        pso_desc.RasterizerState.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_ON;
        hr = ID3D12Device_CreateGraphicsPipelineState(context.device, &pso_desc, &IID_ID3D12PipelineState, (void **)&pipeline_conservative_underestimate);
        ok(hr == S_OK, "Failed to create pipeline, hr %#x.\n", hr);
    }
    else
        pipeline_conservative_underestimate = NULL;

    init_pipeline_state_desc(&pso_desc, root_signature, DXGI_FORMAT_UNKNOWN, NULL, NULL, NULL);
    pso_desc.DSVFormat = DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
    pso_desc.DepthStencilState.DepthEnable = true;
    pso_desc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    pso_desc.DepthStencilState.StencilEnable = true;
    pso_desc.DepthStencilState.StencilReadMask = 0xFF;
    pso_desc.DepthStencilState.StencilWriteMask = 0x00;
    pso_desc.DepthStencilState.FrontFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
    pso_desc.DepthStencilState.FrontFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
    pso_desc.DepthStencilState.FrontFace.StencilPassOp = D3D12_STENCIL_OP_KEEP;
    pso_desc.DepthStencilState.FrontFace.StencilFunc = D3D12_COMPARISON_FUNC_EQUAL;
    pso_desc.DepthStencilState.BackFace = pso_desc.DepthStencilState.FrontFace;
    pso_desc.RasterizerState.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;
    hr = ID3D12Device_CreateGraphicsPipelineState(context.device, &pso_desc, &IID_ID3D12PipelineState, (void **)&pipeline_stencil_test);
    ok(hr == S_OK, "Failed to create pipeline, hr %#x.\n", hr);

    ID3D12GraphicsCommandList_OMSetRenderTargets(command_list, 0, NULL, false, &ds.dsv_handle);
    ID3D12GraphicsCommandList_ClearDepthStencilView(command_list, ds.dsv_handle,
            D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 0.0f, 0, 0, NULL);

    ID3D12GraphicsCommandList_OMSetStencilRef(command_list, 0xFF);
    ID3D12GraphicsCommandList_SetGraphicsRootSignature(command_list, context.root_signature);
    ID3D12GraphicsCommandList_IASetPrimitiveTopology(command_list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    ID3D12GraphicsCommandList_IASetVertexBuffers(command_list, 0, 1, &vbv);
    ID3D12GraphicsCommandList_RSSetViewports(command_list, 1, &context.viewport);
    ID3D12GraphicsCommandList_RSSetScissorRects(command_list, 1, &context.scissor_rect);

    ID3D12GraphicsCommandList_SetPipelineState(command_list, pipeline_conservative_overestimate);
    ID3D12GraphicsCommandList_DrawInstanced(command_list, 3, 1, 0, 0);
    ID3D12GraphicsCommandList_SetPipelineState(command_list, pipeline_conservative_off);
    ID3D12GraphicsCommandList_DrawInstanced(command_list, 3, 1, 0, 0);

    if (pipeline_conservative_underestimate)
    {
        ID3D12GraphicsCommandList_SetPipelineState(command_list, pipeline_conservative_underestimate);
        ID3D12GraphicsCommandList_DrawInstanced(command_list, 3, 1, 0, 0);
    }

    ID3D12GraphicsCommandList_SetPipelineState(command_list, pipeline_stencil_test);

    for (i = 0; i < ARRAY_SIZE(tests); i++)
    {
        ID3D12GraphicsCommandList_OMSetStencilRef(command_list, tests[i].stencil_ref);
        ID3D12GraphicsCommandList_BeginQuery(command_list, query_heap, D3D12_QUERY_TYPE_OCCLUSION, i);
        ID3D12GraphicsCommandList_DrawInstanced(command_list, 3, 1, 0, 0);
        ID3D12GraphicsCommandList_EndQuery(command_list, query_heap, D3D12_QUERY_TYPE_OCCLUSION, i);
    }

    ID3D12GraphicsCommandList_ResolveQueryData(command_list, query_heap,
            D3D12_QUERY_TYPE_OCCLUSION, 0, ARRAY_SIZE(tests), readback, 0);

    get_buffer_readback_with_command_list(readback, DXGI_FORMAT_UNKNOWN, &rb, queue, command_list);

    for (i = 0; i < ARRAY_SIZE(tests); i++)
    {
        vkd3d_test_set_context("Test %u", i);

        if (options.ConservativeRasterizationTier >= tests[i].min_tier)
        {
            value = get_readback_uint64(&rb, i, 0);
            ok(value, "Unexpected value %"PRIu64".\n", value);
        }
        else
            skip("Conservative rasterization tier %u not supported.\n", tests[i].min_tier);
    }

    ID3D12PipelineState_Release(pipeline_conservative_off);
    ID3D12PipelineState_Release(pipeline_conservative_overestimate);
    ID3D12PipelineState_Release(pipeline_stencil_test);

    if (pipeline_conservative_underestimate)
        ID3D12PipelineState_Release(pipeline_conservative_underestimate);

    release_resource_readback(&rb);
    ID3D12RootSignature_Release(root_signature);
    ID3D12QueryHeap_Release(query_heap);
    ID3D12Resource_Release(readback);
    ID3D12Resource_Release(vb);
    destroy_depth_stencil(&ds);
    destroy_test_context(&context);
}

void test_conservative_rasterization_dxbc(void)
{
    test_conservative_rasterization(false);
}

void test_conservative_rasterization_dxil(void)
{
    test_conservative_rasterization(true);
}

