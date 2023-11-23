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

void test_unbound_rtv_rendering(void)
{
    static const struct vec4 red = { 1.0f, 0.0f, 0.0f, 1.0f };
    static const float white[] = { 1.0f, 1.0f, 1.0f, 1.0f };
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc;
    ID3D12GraphicsCommandList *command_list;
    D3D12_CPU_DESCRIPTOR_HANDLE rt_handle;
    struct test_context_desc desc;
    struct test_context context;
    ID3D12CommandQueue *queue;
    ID3D12Resource *fp32_rt;
    HRESULT hr;

    static const DWORD ps_code[] =
    {
#if 0
        Outputs main()
        {
            Outputs o;
            o.col0 = float4(1.0, 0.0, 0.0, 1.0);
            o.col1 = 0.5;
            return o;
        }
#endif
        0x43425844, 0xbbb26641, 0x99a7dc17, 0xc556a4cd, 0x3aa2843e, 0x00000001, 0x000000ec, 0x00000003,
        0x0000002c, 0x0000003c, 0x00000088, 0x4e475349, 0x00000008, 0x00000000, 0x00000008, 0x4e47534f,
        0x00000044, 0x00000002, 0x00000008, 0x00000038, 0x00000000, 0x00000000, 0x00000003, 0x00000000,
        0x0000000f, 0x00000038, 0x00000001, 0x00000000, 0x00000003, 0x00000001, 0x00000e01, 0x545f5653,
        0x65677261, 0xabab0074, 0x58454853, 0x0000005c, 0x00000050, 0x00000017, 0x0100086a, 0x03000065,
        0x001020f2, 0x00000000, 0x03000065, 0x00102012, 0x00000001, 0x08000036, 0x001020f2, 0x00000000,
        0x00004002, 0x3f800000, 0x00000000, 0x00000000, 0x3f800000, 0x05000036, 0x00102012, 0x00000001,
        0x00004001, 0x3f000000, 0x0100003e,
    };
    static const D3D12_SHADER_BYTECODE ps = {ps_code, sizeof(ps_code)};

    memset(&desc, 0, sizeof(desc));
    desc.rt_format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    desc.rt_width = 32;
    desc.rt_height = 32;
    desc.rt_descriptor_count = 2;
    desc.no_pipeline = true;
    if (!init_test_context(&context, &desc))
        return;
    command_list = context.list;
    queue = context.queue;

    fp32_rt = create_default_texture2d(context.device, 32, 32,
            1, 1, DXGI_FORMAT_R32_FLOAT, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,
            D3D12_RESOURCE_STATE_RENDER_TARGET);

    rt_handle = context.rtv;
    rt_handle.ptr += ID3D12Device_GetDescriptorHandleIncrementSize(context.device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    ID3D12Device_CreateRenderTargetView(context.device, fp32_rt, NULL, rt_handle);

    /* Apparently, rendering to an NULL RTV is fine. D3D12 validation does not complain about this case at all. */
    init_pipeline_state_desc(&pso_desc, context.root_signature, 0, NULL, &ps, NULL);
    pso_desc.NumRenderTargets = 2;
    pso_desc.RTVFormats[0] = DXGI_FORMAT_R32G32B32A32_FLOAT;
    pso_desc.RTVFormats[1] = DXGI_FORMAT_R32_FLOAT;
    pso_desc.DSVFormat = DXGI_FORMAT_UNKNOWN;
    pso_desc.BlendState.RenderTarget[0].RenderTargetWriteMask = 0xf;
    pso_desc.BlendState.RenderTarget[1].RenderTargetWriteMask = 0xf;
    pso_desc.DepthStencilState.DepthEnable = false;
    pso_desc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    pso_desc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    hr = ID3D12Device_CreateGraphicsPipelineState(context.device, &pso_desc,
            &IID_ID3D12PipelineState, (void **)&context.pipeline_state);
    ok(hr == S_OK, "Failed to create state, hr %#x.\n", hr);

    ID3D12GraphicsCommandList_ClearRenderTargetView(command_list, context.rtv, white, 0, NULL);
    ID3D12GraphicsCommandList_ClearRenderTargetView(command_list, rt_handle, white, 0, NULL);
    ID3D12GraphicsCommandList_SetGraphicsRootSignature(command_list, context.root_signature);
    ID3D12GraphicsCommandList_SetPipelineState(command_list, context.pipeline_state);
    ID3D12GraphicsCommandList_IASetPrimitiveTopology(command_list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    set_viewport(&context.viewport, 0.0f, 0.0f, 32.0f, 32.0f, 0.5f, 0.5f);
    ID3D12GraphicsCommandList_RSSetViewports(command_list, 1, &context.viewport);
    ID3D12GraphicsCommandList_RSSetScissorRects(command_list, 1, &context.scissor_rect);

    /* First, render to both RTs, but then only render to 1 RT. */
    ID3D12GraphicsCommandList_OMSetRenderTargets(command_list, 2, &context.rtv, true, NULL);
    ID3D12GraphicsCommandList_DrawInstanced(command_list, 3, 1, 0, 0);
    ID3D12GraphicsCommandList_OMSetRenderTargets(command_list, 1, &context.rtv, false, NULL);
    ID3D12GraphicsCommandList_DrawInstanced(command_list, 3, 1, 0, 0);

    transition_resource_state(command_list, context.render_target,
            D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
    transition_resource_state(command_list, fp32_rt,
            D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);

    check_sub_resource_vec4(context.render_target, 0, queue, command_list, &red, 0);
    reset_command_list(command_list, context.allocator);
    check_sub_resource_float(fp32_rt, 0, queue, command_list, 0.5f, 0);
    ID3D12Resource_Release(fp32_rt);
    destroy_test_context(&context);
}

void test_unknown_rtv_format(void)
{
    static const struct vec4 vec4_white = {1.0f, 1.0f, 1.0f, 1.0f};
    static const float white[] = {1.0f, 1.0f, 1.0f, 1.0f};
    struct vec4 expected_vec4 = {0.0f, 0.0f, 0.0f, 1.0f};
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc;
    ID3D12GraphicsCommandList *command_list;
    D3D12_RENDER_TARGET_VIEW_DESC rtv_desc;
    D3D12_CPU_DESCRIPTOR_HANDLE rtvs[3];
    ID3D12Resource *render_targets[2];
    struct depth_stencil_resource ds;
    struct test_context_desc desc;
    struct test_context context;
    ID3D12CommandQueue *queue;
    unsigned int i;
    HRESULT hr;

    static const DWORD ps_code[] =
    {
#if 0
        void main(out float4 target1 : SV_Target1, out float4 target2 : SV_Target2)
        {
            target1 = float4(2.0f, 0.0f, 0.0f, 1.0f);
            target2 = float4(3.0f, 0.0f, 0.0f, 1.0f);
        }
#endif
        0x43425844, 0x980554be, 0xb8743fb0, 0xf5bb8deb, 0x639feaf8, 0x00000001, 0x000000f4, 0x00000003,
        0x0000002c, 0x0000003c, 0x00000088, 0x4e475349, 0x00000008, 0x00000000, 0x00000008, 0x4e47534f,
        0x00000044, 0x00000002, 0x00000008, 0x00000038, 0x00000001, 0x00000000, 0x00000003, 0x00000001,
        0x0000000f, 0x00000038, 0x00000002, 0x00000000, 0x00000003, 0x00000002, 0x0000000f, 0x545f5653,
        0x65677261, 0xabab0074, 0x52444853, 0x00000064, 0x00000040, 0x00000019, 0x03000065, 0x001020f2,
        0x00000001, 0x03000065, 0x001020f2, 0x00000002, 0x08000036, 0x001020f2, 0x00000001, 0x00004002,
        0x40000000, 0x00000000, 0x00000000, 0x3f800000, 0x08000036, 0x001020f2, 0x00000002, 0x00004002,
        0x40400000, 0x00000000, 0x00000000, 0x3f800000, 0x0100003e,
    };
    static const D3D12_SHADER_BYTECODE ps = {ps_code, sizeof(ps_code)};

    memset(&desc, 0, sizeof(desc));
    desc.rt_format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    desc.rt_descriptor_count = 16;
    desc.no_pipeline = true;
    if (!init_test_context(&context, &desc))
        return;
    command_list = context.list;
    queue = context.queue;

    init_depth_stencil(&ds, context.device, 32, 32, 1, 1, DXGI_FORMAT_D32_FLOAT, 0, NULL);

    init_pipeline_state_desc(&pso_desc, context.root_signature, 0, NULL, &ps, NULL);
    pso_desc.NumRenderTargets = ARRAY_SIZE(rtvs);
    for (i = 0; i < ARRAY_SIZE(rtvs); ++i)
        pso_desc.RTVFormats[i] = desc.rt_format;
    pso_desc.RTVFormats[0] = DXGI_FORMAT_UNKNOWN;
    pso_desc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    pso_desc.DepthStencilState.DepthEnable = true;
    pso_desc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    pso_desc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    hr = ID3D12Device_CreateGraphicsPipelineState(context.device, &pso_desc,
            &IID_ID3D12PipelineState, (void **)&context.pipeline_state);
    ok(hr == S_OK, "Failed to create state, hr %#x.\n", hr);

    rtvs[0] = get_cpu_rtv_handle(&context, context.rtv_heap, 0);
    rtvs[1] = get_cpu_rtv_handle(&context, context.rtv_heap, 1);
    rtvs[2] = get_cpu_rtv_handle(&context, context.rtv_heap, 2);
    create_render_target(&context, &desc, &render_targets[0], &rtvs[1]);
    create_render_target(&context, &desc, &render_targets[1], &rtvs[2]);

    for (i = 0; i < ARRAY_SIZE(rtvs); ++i)
        ID3D12GraphicsCommandList_ClearRenderTargetView(command_list, rtvs[i], white, 0, NULL);

    /* NULL RTV */
    memset(&rtv_desc, 0, sizeof(rtv_desc));
    rtv_desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
    rtv_desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    rtv_desc.Texture2D.MipSlice = 0;
    rtv_desc.Texture2D.PlaneSlice = 0;
    ID3D12Device_CreateRenderTargetView(context.device, NULL, &rtv_desc,
            get_cpu_rtv_handle(&context, context.rtv_heap, 0));

    ID3D12GraphicsCommandList_OMSetRenderTargets(command_list, ARRAY_SIZE(rtvs), rtvs, false, &ds.dsv_handle);
    ID3D12GraphicsCommandList_SetGraphicsRootSignature(command_list, context.root_signature);
    ID3D12GraphicsCommandList_SetPipelineState(command_list, context.pipeline_state);
    ID3D12GraphicsCommandList_IASetPrimitiveTopology(command_list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    set_viewport(&context.viewport, 0.0f, 0.0f, 32.0f, 32.0f, 0.5f, 0.5f);
    ID3D12GraphicsCommandList_RSSetViewports(command_list, 1, &context.viewport);
    ID3D12GraphicsCommandList_RSSetScissorRects(command_list, 1, &context.scissor_rect);
    ID3D12GraphicsCommandList_DrawInstanced(command_list, 3, 1, 0, 0);

    transition_resource_state(command_list, context.render_target,
            D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
    transition_resource_state(command_list, render_targets[0],
            D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
    transition_resource_state(command_list, render_targets[1],
            D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);

    check_sub_resource_vec4(context.render_target, 0, queue, command_list, &vec4_white, 0);
    reset_command_list(command_list, context.allocator);
    expected_vec4.x = 2.0f;
    check_sub_resource_vec4(render_targets[0], 0, queue, command_list, &expected_vec4, 0);
    reset_command_list(command_list, context.allocator);
    expected_vec4.x = 3.0f;
    check_sub_resource_vec4(render_targets[1], 0, queue, command_list, &expected_vec4, 0);

    reset_command_list(command_list, context.allocator);
    transition_resource_state(command_list, ds.texture,
            D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_COPY_SOURCE);
    check_sub_resource_float(ds.texture, 0, queue, command_list, 0.5f, 1);

    for (i = 0; i < ARRAY_SIZE(render_targets); ++i)
        ID3D12Resource_Release(render_targets[i]);
    destroy_depth_stencil(&ds);
    destroy_test_context(&context);
}

void test_unknown_dsv_format(void)
{
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc;
    ID3D12GraphicsCommandList *command_list;
    struct depth_stencil_resource ds;
    D3D12_CLEAR_VALUE clear_value;
    struct test_context_desc desc;
    struct test_context context;
    ID3D12CommandQueue *queue;
    HRESULT hr;

    static const DWORD ps_color_code[] =
    {
#if 0
        float4 color;

        float4 main(float4 position : SV_POSITION) : SV_Target
        {
            return color;
        }
#endif
        0x43425844, 0xd18ead43, 0x8b8264c1, 0x9c0a062d, 0xfc843226, 0x00000001, 0x000000e0, 0x00000003,
        0x0000002c, 0x00000060, 0x00000094, 0x4e475349, 0x0000002c, 0x00000001, 0x00000008, 0x00000020,
        0x00000000, 0x00000001, 0x00000003, 0x00000000, 0x0000000f, 0x505f5653, 0x5449534f, 0x004e4f49,
        0x4e47534f, 0x0000002c, 0x00000001, 0x00000008, 0x00000020, 0x00000000, 0x00000000, 0x00000003,
        0x00000000, 0x0000000f, 0x545f5653, 0x65677261, 0xabab0074, 0x58454853, 0x00000044, 0x00000050,
        0x00000011, 0x0100086a, 0x04000059, 0x00208e46, 0x00000000, 0x00000001, 0x03000065, 0x001020f2,
        0x00000000, 0x06000036, 0x001020f2, 0x00000000, 0x00208e46, 0x00000000, 0x00000000, 0x0100003e,
    };
    static const D3D12_SHADER_BYTECODE ps_color = {ps_color_code, sizeof(ps_color_code)};
    static const float white[] = {1.0f, 1.0f, 1.0f, 1.0f};
    static const struct vec4 green = {0.0f, 1.0f, 0.0f, 1.0f};
    static const struct vec4 red = {1.0f, 0.0f, 0.0f, 1.0f};

    memset(&desc, 0, sizeof(desc));
    desc.rt_format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    desc.no_root_signature = true;
    if (!init_test_context(&context, &desc))
        return;
    command_list = context.list;
    queue = context.queue;

    clear_value.Format = DXGI_FORMAT_D32_FLOAT;
    clear_value.DepthStencil.Depth = 0.5f;
    clear_value.DepthStencil.Stencil = 0;
    init_depth_stencil(&ds, context.device, 32, 32, 1, 1, DXGI_FORMAT_D32_FLOAT, 0, &clear_value);

    context.root_signature = create_32bit_constants_root_signature(context.device,
            0, 4, D3D12_SHADER_VISIBILITY_PIXEL);

    /* DSVFormat = DXGI_FORMAT_UNKNOWN and D3D12_DEPTH_WRITE_MASK_ZERO */
    init_pipeline_state_desc(&pso_desc, context.root_signature, desc.rt_format, NULL, &ps_color, NULL);
    pso_desc.DSVFormat = DXGI_FORMAT_UNKNOWN;
    pso_desc.DepthStencilState.DepthEnable = true;
    pso_desc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    pso_desc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_EQUAL;
    hr = ID3D12Device_CreateGraphicsPipelineState(context.device, &pso_desc,
            &IID_ID3D12PipelineState, (void **)&context.pipeline_state);
    ok(hr == S_OK, "Failed to create graphics pipeline state, hr %#x.\n", hr);

    ID3D12GraphicsCommandList_ClearRenderTargetView(command_list, context.rtv, white, 0, NULL);
    ID3D12GraphicsCommandList_ClearDepthStencilView(command_list, ds.dsv_handle,
            D3D12_CLEAR_FLAG_DEPTH, 0.5f, 0, 0, NULL);

    ID3D12GraphicsCommandList_OMSetRenderTargets(command_list, 1, &context.rtv, false, &ds.dsv_handle);
    ID3D12GraphicsCommandList_SetGraphicsRootSignature(command_list, context.root_signature);
    ID3D12GraphicsCommandList_SetPipelineState(command_list, context.pipeline_state);
    ID3D12GraphicsCommandList_IASetPrimitiveTopology(command_list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D12GraphicsCommandList_RSSetScissorRects(command_list, 1, &context.scissor_rect);

    ID3D12GraphicsCommandList_SetGraphicsRoot32BitConstants(command_list, 0, 4, &green.x, 0);
    set_viewport(&context.viewport, 0.0f, 0.0f, 32.0f, 32.0f, 0.5f, 0.5f);
    ID3D12GraphicsCommandList_RSSetViewports(command_list, 1, &context.viewport);
    ID3D12GraphicsCommandList_DrawInstanced(command_list, 3, 1, 0, 0);

    ID3D12GraphicsCommandList_SetGraphicsRoot32BitConstants(command_list, 0, 4, &red.x, 0);
    set_viewport(&context.viewport, 0.0f, 0.0f, 32.0f, 32.0f, 1.0f, 1.0f);
    ID3D12GraphicsCommandList_RSSetViewports(command_list, 1, &context.viewport);
    ID3D12GraphicsCommandList_DrawInstanced(command_list, 3, 1, 0, 0);
    set_viewport(&context.viewport, 0.0f, 0.0f, 32.0f, 32.0f, 0.0f, 0.0f);
    ID3D12GraphicsCommandList_RSSetViewports(command_list, 1, &context.viewport);
    ID3D12GraphicsCommandList_DrawInstanced(command_list, 3, 1, 0, 0);
    set_viewport(&context.viewport, 0.0f, 0.0f, 32.0f, 32.0f, 0.55f, 0.55f);
    ID3D12GraphicsCommandList_RSSetViewports(command_list, 1, &context.viewport);
    ID3D12GraphicsCommandList_DrawInstanced(command_list, 3, 1, 0, 0);

    transition_resource_state(command_list, ds.texture,
            D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_COPY_SOURCE);
    check_sub_resource_float(ds.texture, 0, queue, command_list, 0.5f, 1);

    reset_command_list(command_list, context.allocator);
    transition_resource_state(command_list, context.render_target,
            D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
    check_sub_resource_vec4(context.render_target, 0, queue, command_list, &green, 0);

    /* DSVFormat = DXGI_FORMAT_UNKNOWN and no DSV */
    reset_command_list(command_list, context.allocator);
    transition_resource_state(command_list, ds.texture,
            D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE);
    transition_resource_state(command_list, context.render_target,
            D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);

    ID3D12GraphicsCommandList_ClearRenderTargetView(command_list, context.rtv, white, 0, NULL);

    ID3D12GraphicsCommandList_OMSetRenderTargets(command_list, 1, &context.rtv, false, NULL);
    ID3D12GraphicsCommandList_SetGraphicsRootSignature(command_list, context.root_signature);
    ID3D12GraphicsCommandList_SetPipelineState(command_list, context.pipeline_state);
    ID3D12GraphicsCommandList_IASetPrimitiveTopology(command_list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D12GraphicsCommandList_RSSetScissorRects(command_list, 1, &context.scissor_rect);

    ID3D12GraphicsCommandList_SetGraphicsRoot32BitConstants(command_list, 0, 4, &red.x, 0);
    set_viewport(&context.viewport, 0.0f, 0.0f, 32.0f, 32.0f, 0.0f, 0.0f);
    ID3D12GraphicsCommandList_RSSetViewports(command_list, 1, &context.viewport);
    ID3D12GraphicsCommandList_DrawInstanced(command_list, 3, 1, 0, 0);

    ID3D12GraphicsCommandList_SetGraphicsRoot32BitConstants(command_list, 0, 4, &green.x, 0);
    set_viewport(&context.viewport, 0.0f, 0.0f, 32.0f, 32.0f, 0.5f, 0.5f);
    ID3D12GraphicsCommandList_RSSetViewports(command_list, 1, &context.viewport);
    ID3D12GraphicsCommandList_DrawInstanced(command_list, 3, 1, 0, 0);

    transition_resource_state(command_list, context.render_target,
            D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
    check_sub_resource_vec4(context.render_target, 0, queue, command_list, &green, 0);

    /* DSVFormat = DXGI_FORMAT_UNKNOWN and D3D12_COMPARISON_FUNC_ALWAYS */
    ID3D12PipelineState_Release(context.pipeline_state);
    pso_desc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    hr = ID3D12Device_CreateGraphicsPipelineState(context.device, &pso_desc,
            &IID_ID3D12PipelineState, (void **)&context.pipeline_state);
    ok(hr == S_OK, "Failed to create graphics pipeline state, hr %#x.\n", hr);

    reset_command_list(command_list, context.allocator);
    transition_resource_state(command_list, context.render_target,
            D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);

    ID3D12GraphicsCommandList_ClearRenderTargetView(command_list, context.rtv, white, 0, NULL);

    ID3D12GraphicsCommandList_OMSetRenderTargets(command_list, 1, &context.rtv, false, &ds.dsv_handle);
    ID3D12GraphicsCommandList_SetGraphicsRootSignature(command_list, context.root_signature);
    ID3D12GraphicsCommandList_SetPipelineState(command_list, context.pipeline_state);
    ID3D12GraphicsCommandList_IASetPrimitiveTopology(command_list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D12GraphicsCommandList_RSSetScissorRects(command_list, 1, &context.scissor_rect);

    ID3D12GraphicsCommandList_SetGraphicsRoot32BitConstants(command_list, 0, 4, &red.x, 0);
    set_viewport(&context.viewport, 0.0f, 0.0f, 32.0f, 32.0f, 0.0f, 0.0f);
    ID3D12GraphicsCommandList_RSSetViewports(command_list, 1, &context.viewport);
    ID3D12GraphicsCommandList_DrawInstanced(command_list, 3, 1, 0, 0);

    ID3D12GraphicsCommandList_SetGraphicsRoot32BitConstants(command_list, 0, 4, &green.x, 0);
    set_viewport(&context.viewport, 0.0f, 0.0f, 32.0f, 32.0f, 0.6f, 0.6f);
    ID3D12GraphicsCommandList_RSSetViewports(command_list, 1, &context.viewport);
    ID3D12GraphicsCommandList_DrawInstanced(command_list, 3, 1, 0, 0);

    transition_resource_state(command_list, context.render_target,
            D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
    check_sub_resource_vec4(context.render_target, 0, queue, command_list, &green, 0);

    /* DSVFormat = DXGI_FORMAT_UNKNOWN and depth write */
    ID3D12PipelineState_Release(context.pipeline_state);
    pso_desc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    pso_desc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    hr = ID3D12Device_CreateGraphicsPipelineState(context.device, &pso_desc,
            &IID_ID3D12PipelineState, (void **)&context.pipeline_state);
    ok(hr == S_OK, "Failed to create graphics pipeline state, hr %#x.\n", hr);

    reset_command_list(command_list, context.allocator);
    transition_resource_state(command_list, context.render_target,
            D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);

    ID3D12GraphicsCommandList_ClearDepthStencilView(command_list, ds.dsv_handle,
            D3D12_CLEAR_FLAG_DEPTH, 0.0f, 0, 0, NULL);

    ID3D12GraphicsCommandList_SetGraphicsRootSignature(command_list, context.root_signature);
    ID3D12GraphicsCommandList_SetPipelineState(command_list, context.pipeline_state);
    ID3D12GraphicsCommandList_IASetPrimitiveTopology(command_list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D12GraphicsCommandList_RSSetScissorRects(command_list, 1, &context.scissor_rect);

    ID3D12GraphicsCommandList_OMSetRenderTargets(command_list, 1, &context.rtv, false, &ds.dsv_handle);
    ID3D12GraphicsCommandList_SetGraphicsRoot32BitConstants(command_list, 0, 4, &red.x, 0);
    set_viewport(&context.viewport, 0.0f, 0.0f, 32.0f, 32.0f, 1.0f, 1.0f);
    ID3D12GraphicsCommandList_RSSetViewports(command_list, 1, &context.viewport);
    ID3D12GraphicsCommandList_DrawInstanced(command_list, 3, 1, 0, 0);

    ID3D12GraphicsCommandList_OMSetRenderTargets(command_list, 1, &context.rtv, false, NULL);
    ID3D12GraphicsCommandList_SetGraphicsRoot32BitConstants(command_list, 0, 4, &green.x, 0);
    set_viewport(&context.viewport, 0.0f, 0.0f, 32.0f, 32.0f, 0.6f, 0.6f);
    ID3D12GraphicsCommandList_RSSetViewports(command_list, 1, &context.viewport);
    ID3D12GraphicsCommandList_DrawInstanced(command_list, 3, 1, 0, 0);

    transition_resource_state(command_list, context.render_target,
            D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
    check_sub_resource_vec4(context.render_target, 0, queue, command_list, &green, 0);

    reset_command_list(command_list, context.allocator);
    transition_resource_state(command_list, ds.texture,
            D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_COPY_SOURCE);
    check_sub_resource_float(ds.texture, 0, queue, command_list, 1.0f, 1);

    destroy_depth_stencil(&ds);
    destroy_test_context(&context);
}

void test_depth_stencil_test_no_dsv(void)
{
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc;
    ID3D12GraphicsCommandList *command_list;
    struct depth_stencil_resource ds;
    struct test_context_desc desc;
    struct test_context context;
    ID3D12CommandQueue *queue;
    HRESULT hr;

    static const DWORD ps_color_code[] =
    {
#if 0
        float4 color;

        float4 main(float4 position : SV_POSITION) : SV_Target
        {
            return color;
        }
#endif
        0x43425844, 0xd18ead43, 0x8b8264c1, 0x9c0a062d, 0xfc843226, 0x00000001, 0x000000e0, 0x00000003,
        0x0000002c, 0x00000060, 0x00000094, 0x4e475349, 0x0000002c, 0x00000001, 0x00000008, 0x00000020,
        0x00000000, 0x00000001, 0x00000003, 0x00000000, 0x0000000f, 0x505f5653, 0x5449534f, 0x004e4f49,
        0x4e47534f, 0x0000002c, 0x00000001, 0x00000008, 0x00000020, 0x00000000, 0x00000000, 0x00000003,
        0x00000000, 0x0000000f, 0x545f5653, 0x65677261, 0xabab0074, 0x58454853, 0x00000044, 0x00000050,
        0x00000011, 0x0100086a, 0x04000059, 0x00208e46, 0x00000000, 0x00000001, 0x03000065, 0x001020f2,
        0x00000000, 0x06000036, 0x001020f2, 0x00000000, 0x00208e46, 0x00000000, 0x00000000, 0x0100003e,
    };
    static const D3D12_SHADER_BYTECODE ps_color = {ps_color_code, sizeof(ps_color_code)};
    static const float white[] = {1.0f, 1.0f, 1.0f, 1.0f};
    static const struct vec4 red = {1.0f, 0.0f, 0.0f, 1.0f};
    static const struct vec4 green = { 0.0f, 1.0f, 0.0f, 1.0f };
    static const struct vec4 blue = { 0.0f, 0.0f, 1.0f, 1.0f };

    memset(&desc, 0, sizeof(desc));
    desc.rt_format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    desc.no_root_signature = true;
    desc.rt_width = 32;
    desc.rt_height = 32;
    if (!init_test_context(&context, &desc))
        return;
    command_list = context.list;
    queue = context.queue;

    init_depth_stencil(&ds, context.device, 32, 32, 1, 1, DXGI_FORMAT_D32_FLOAT, 0, NULL);

    context.root_signature = create_32bit_constants_root_signature(context.device,
              0, 4, D3D12_SHADER_VISIBILITY_PIXEL);

    init_pipeline_state_desc(&pso_desc, context.root_signature, desc.rt_format, NULL, &ps_color, NULL);
    pso_desc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    pso_desc.DepthStencilState.DepthEnable = true;
    pso_desc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    pso_desc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    hr = ID3D12Device_CreateGraphicsPipelineState(context.device, &pso_desc,
            &IID_ID3D12PipelineState, (void **)&context.pipeline_state);
    ok(hr == S_OK, "Failed to create graphics pipeline state, hr %#x.\n", hr);

    ID3D12GraphicsCommandList_ClearRenderTargetView(command_list, context.rtv, white, 0, NULL);
    ID3D12GraphicsCommandList_ClearDepthStencilView(command_list, ds.dsv_handle, D3D12_CLEAR_FLAG_DEPTH,
            1.0f, 0, 0, NULL);

    ID3D12GraphicsCommandList_OMSetRenderTargets(command_list, 1, &context.rtv, false, &ds.dsv_handle);
    ID3D12GraphicsCommandList_SetGraphicsRootSignature(command_list, context.root_signature);
    ID3D12GraphicsCommandList_SetPipelineState(command_list, context.pipeline_state);
    ID3D12GraphicsCommandList_IASetPrimitiveTopology(command_list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D12GraphicsCommandList_RSSetScissorRects(command_list, 1, &context.scissor_rect);

    ID3D12GraphicsCommandList_SetGraphicsRoot32BitConstants(command_list, 0, 4, &green.x, 0);
    set_viewport(&context.viewport, 0.0f, 0.0f, 32.0f, 32.0f, 0.5f, 0.5f);
    ID3D12GraphicsCommandList_RSSetViewports(command_list, 1, &context.viewport);
    ID3D12GraphicsCommandList_DrawInstanced(command_list, 3, 1, 0, 0);

    ID3D12GraphicsCommandList_SetGraphicsRoot32BitConstants(command_list, 0, 4, &red.x, 0);
    set_viewport(&context.viewport, 0.0f, 0.0f, 32.0f, 32.0f, 0.9f, 0.9f);
    ID3D12GraphicsCommandList_RSSetViewports(command_list, 1, &context.viewport);
    ID3D12GraphicsCommandList_DrawInstanced(command_list, 3, 1, 0, 0);

    /* Now, dynamically disable the depth attachment. */
    ID3D12GraphicsCommandList_OMSetRenderTargets(command_list, 1, &context.rtv, false, NULL);

    /* Native behavior seems to be that depth test is just disabled entirely here.
     * This last draw is the color we should get on NV at least.
     * D3D12 validation layers report errors here of course,
     * but Metro Exodus relies on depth testing on DSV NULL apparently. */
    ID3D12GraphicsCommandList_SetGraphicsRoot32BitConstants(command_list, 0, 4, &blue.x, 0);
    set_viewport(&context.viewport, 0.0f, 0.0f, 32.0f, 32.0f, 0.55f, 0.55f);
    ID3D12GraphicsCommandList_RSSetViewports(command_list, 1, &context.viewport);
    ID3D12GraphicsCommandList_DrawInstanced(command_list, 3, 1, 0, 0);

    transition_resource_state(command_list, context.render_target,
              D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
    /* vkd3d-proton just skips the draw call in this situation.
     * At least test that we don't crash. */
    check_sub_resource_vec4(context.render_target, 0, queue, command_list, &blue, 0);

    destroy_depth_stencil(&ds);
    destroy_test_context(&context);
}

void test_render_a8_dxbc(void)
{
    static const float black[] = {0.0f, 0.0f, 0.0f, 0.0f};
    ID3D12GraphicsCommandList *command_list;
    struct test_context_desc desc;
    struct test_context context;
    ID3D12CommandQueue *queue;

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

    memset(&desc, 0, sizeof(desc));
    desc.rt_format = DXGI_FORMAT_A8_UNORM;
    desc.ps = &ps;
    if (!init_test_context(&context, &desc))
        return;
    command_list = context.list;
    queue = context.queue;

    ID3D12GraphicsCommandList_ClearRenderTargetView(command_list, context.rtv, black, 0, NULL);

    ID3D12GraphicsCommandList_OMSetRenderTargets(command_list, 1, &context.rtv, false, NULL);
    ID3D12GraphicsCommandList_SetGraphicsRootSignature(command_list, context.root_signature);
    ID3D12GraphicsCommandList_SetPipelineState(command_list, context.pipeline_state);
    ID3D12GraphicsCommandList_IASetPrimitiveTopology(command_list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D12GraphicsCommandList_RSSetViewports(command_list, 1, &context.viewport);
    ID3D12GraphicsCommandList_RSSetScissorRects(command_list, 1, &context.scissor_rect);
    ID3D12GraphicsCommandList_DrawInstanced(command_list, 3, 1, 0, 0);

    transition_resource_state(command_list, context.render_target,
            D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);

    check_sub_resource_uint8(context.render_target, 0, queue, command_list, 0xff, 0);

    destroy_test_context(&context);
}

void test_render_a8_dxil(void)
{
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc;
    static const float black[] = {0.0f, 0.0f, 0.0f, 0.0f};
    ID3D12GraphicsCommandList *command_list;
    struct test_context_desc desc;
    struct test_context context;
    ID3D12CommandQueue *queue;
    ID3D12Device *device;
    HRESULT hr;

    static const BYTE ps_code_dxil[] =
    {
#if 0
        void main(out float4 target : SV_Target)
        {
            target = float4(0.0f, 0.25f, 0.5f, 1.0f);
        }
#endif
        0x44, 0x58, 0x42, 0x43, 0x21, 0x97, 0x41, 0xc7, 0x9f, 0x1a, 0xed, 0x0b, 0xa5, 0x57, 0x8b, 0x4b, 0xd2, 0x3f, 0xe9, 0x18, 0x01, 0x00, 0x00, 0x00, 0x32, 0x05, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00,
        0x34, 0x00, 0x00, 0x00, 0x44, 0x00, 0x00, 0x00, 0x54, 0x00, 0x00, 0x00, 0x8e, 0x00, 0x00, 0x00, 0xe6, 0x00, 0x00, 0x00, 0x53, 0x46, 0x49, 0x30, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x49, 0x53, 0x47, 0x31, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x4f, 0x53, 0x47, 0x31, 0x32, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
        0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x28, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0f, 0xf0, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x53, 0x56, 0x5f, 0x54, 0x61, 0x72, 0x67, 0x65, 0x74, 0x00, 0x50, 0x53, 0x56, 0x30, 0x50, 0x00, 0x00, 0x00, 0x24, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00,
        0x44, 0x10, 0x03, 0x00, 0x00, 0x00, 0x44, 0x58, 0x49, 0x4c, 0x44, 0x04, 0x00, 0x00, 0x60, 0x00, 0x00, 0x00, 0x11, 0x01, 0x00, 0x00, 0x44, 0x58, 0x49, 0x4c, 0x00, 0x01, 0x00, 0x00, 0x10, 0x00,
        0x00, 0x00, 0x2c, 0x04, 0x00, 0x00, 0x42, 0x43, 0xc0, 0xde, 0x21, 0x0c, 0x00, 0x00, 0x08, 0x01, 0x00, 0x00, 0x0b, 0x82, 0x20, 0x00, 0x02, 0x00, 0x00, 0x00, 0x13, 0x00, 0x00, 0x00, 0x07, 0x81,
        0x23, 0x91, 0x41, 0xc8, 0x04, 0x49, 0x06, 0x10, 0x32, 0x39, 0x92, 0x01, 0x84, 0x0c, 0x25, 0x05, 0x08, 0x19, 0x1e, 0x04, 0x8b, 0x62, 0x80, 0x10, 0x45, 0x02, 0x42, 0x92, 0x0b, 0x42, 0x84, 0x10,
        0x32, 0x14, 0x38, 0x08, 0x18, 0x4b, 0x0a, 0x32, 0x42, 0x88, 0x48, 0x90, 0x14, 0x20, 0x43, 0x46, 0x88, 0xa5, 0x00, 0x19, 0x32, 0x42, 0xe4, 0x48, 0x0e, 0x90, 0x11, 0x22, 0xc4, 0x50, 0x41, 0x51,
        0x81, 0x8c, 0xe1, 0x83, 0xe5, 0x8a, 0x04, 0x21, 0x46, 0x06, 0x51, 0x18, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x1b, 0x88, 0xe0, 0xff, 0xff, 0xff, 0xff, 0x07, 0x40, 0x02, 0x00, 0x00, 0x49, 0x18,
        0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x13, 0x82, 0x00, 0x00, 0x89, 0x20, 0x00, 0x00, 0x0e, 0x00, 0x00, 0x00, 0x32, 0x22, 0x08, 0x09, 0x20, 0x64, 0x85, 0x04, 0x13, 0x22, 0xa4, 0x84, 0x04, 0x13,
        0x22, 0xe3, 0x84, 0xa1, 0x90, 0x14, 0x12, 0x4c, 0x88, 0x8c, 0x0b, 0x84, 0x84, 0x4c, 0x10, 0x28, 0x23, 0x00, 0x25, 0x00, 0x8a, 0x39, 0x02, 0x30, 0x98, 0x23, 0x40, 0x66, 0x00, 0x8a, 0x01, 0x33,
        0x43, 0x45, 0x36, 0x10, 0x90, 0x02, 0x03, 0x00, 0x00, 0x00, 0x13, 0x14, 0x72, 0xc0, 0x87, 0x74, 0x60, 0x87, 0x36, 0x68, 0x87, 0x79, 0x68, 0x03, 0x72, 0xc0, 0x87, 0x0d, 0xaf, 0x50, 0x0e, 0x6d,
        0xd0, 0x0e, 0x7a, 0x50, 0x0e, 0x6d, 0x00, 0x0f, 0x7a, 0x30, 0x07, 0x72, 0xa0, 0x07, 0x73, 0x20, 0x07, 0x6d, 0x90, 0x0e, 0x71, 0xa0, 0x07, 0x73, 0x20, 0x07, 0x6d, 0x90, 0x0e, 0x78, 0xa0, 0x07,
        0x73, 0x20, 0x07, 0x6d, 0x90, 0x0e, 0x71, 0x60, 0x07, 0x7a, 0x30, 0x07, 0x72, 0xd0, 0x06, 0xe9, 0x30, 0x07, 0x72, 0xa0, 0x07, 0x73, 0x20, 0x07, 0x6d, 0x90, 0x0e, 0x76, 0x40, 0x07, 0x7a, 0x60,
        0x07, 0x74, 0xd0, 0x06, 0xe6, 0x10, 0x07, 0x76, 0xa0, 0x07, 0x73, 0x20, 0x07, 0x6d, 0x60, 0x0e, 0x73, 0x20, 0x07, 0x7a, 0x30, 0x07, 0x72, 0xd0, 0x06, 0xe6, 0x60, 0x07, 0x74, 0xa0, 0x07, 0x76,
        0x40, 0x07, 0x6d, 0xe0, 0x0e, 0x78, 0xa0, 0x07, 0x71, 0x60, 0x07, 0x7a, 0x30, 0x07, 0x72, 0xa0, 0x07, 0x76, 0x40, 0x07, 0x43, 0x9e, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x86, 0x3c, 0x06, 0x10, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x64, 0x81, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00, 0x32, 0x1e, 0x98, 0x10, 0x19, 0x11, 0x4c, 0x90, 0x8c, 0x09,
        0x26, 0x47, 0xc6, 0x04, 0x43, 0x9a, 0x12, 0x18, 0x01, 0x28, 0x84, 0x62, 0x20, 0x2a, 0x89, 0x02, 0x19, 0x01, 0x28, 0x04, 0xca, 0xb1, 0x04, 0x80, 0x00, 0x00, 0x79, 0x18, 0x00, 0x00, 0x3e, 0x00,
        0x00, 0x00, 0x1a, 0x03, 0x4c, 0x90, 0x46, 0x02, 0x13, 0xc4, 0x88, 0x0c, 0x6f, 0xec, 0xed, 0x4d, 0x0c, 0x44, 0x06, 0x26, 0x26, 0xc7, 0x05, 0xa6, 0xc6, 0x05, 0x06, 0x66, 0x43, 0x10, 0x4c, 0x10,
        0x06, 0x61, 0x82, 0x30, 0x0c, 0x1b, 0x84, 0x81, 0x98, 0x20, 0x0c, 0xc4, 0x06, 0x61, 0x30, 0x28, 0xc0, 0xcd, 0x4d, 0x10, 0x86, 0x62, 0xc3, 0x80, 0x24, 0xc4, 0x04, 0x41, 0x00, 0x36, 0x00, 0x1b,
        0x06, 0x82, 0x61, 0x36, 0x04, 0xcd, 0x86, 0x61, 0x58, 0x9c, 0x09, 0x42, 0xa2, 0x6c, 0x08, 0x20, 0x12, 0x6d, 0x61, 0x69, 0x6e, 0x4c, 0xa6, 0xac, 0xbe, 0xa8, 0xc2, 0xe4, 0xce, 0xca, 0xe8, 0x26,
        0x08, 0x84, 0x31, 0x41, 0x20, 0x8e, 0x0d, 0x01, 0x31, 0x41, 0x20, 0x90, 0x09, 0x02, 0x91, 0x6c, 0x58, 0x88, 0x89, 0xaa, 0xac, 0x6b, 0xc0, 0x88, 0x0b, 0xd8, 0x10, 0x64, 0x1b, 0x06, 0x40, 0x03,
        0x36, 0x14, 0x8b, 0xb4, 0x01, 0x40, 0x15, 0x36, 0x36, 0xbb, 0x36, 0x97, 0x34, 0xb2, 0x32, 0x37, 0xba, 0x29, 0x41, 0x50, 0x85, 0x0c, 0xcf, 0xc5, 0xae, 0x4c, 0x6e, 0x2e, 0xed, 0xcd, 0x6d, 0x4a,
        0x40, 0x34, 0x21, 0xc3, 0x73, 0xb1, 0x0b, 0x63, 0xb3, 0x2b, 0x93, 0x9b, 0x12, 0x18, 0x75, 0xc8, 0xf0, 0x5c, 0xe6, 0xd0, 0xc2, 0xc8, 0xca, 0xe4, 0x9a, 0xde, 0xc8, 0xca, 0xd8, 0xa6, 0x04, 0x49,
        0x25, 0x32, 0x3c, 0x17, 0xba, 0x3c, 0xb8, 0xb2, 0x20, 0x37, 0xb7, 0x37, 0xba, 0x30, 0xba, 0xb4, 0x37, 0xb7, 0xb9, 0x29, 0x81, 0x53, 0x87, 0x0c, 0xcf, 0xc5, 0x2e, 0xad, 0xec, 0x2e, 0x89, 0x6c,
        0x8a, 0x2e, 0x8c, 0xae, 0x6c, 0x4a, 0x00, 0xd5, 0x21, 0xc3, 0x73, 0x29, 0x73, 0xa3, 0x93, 0xcb, 0x83, 0x7a, 0x4b, 0x73, 0xa3, 0x9b, 0x9b, 0x12, 0x6c, 0x00, 0x79, 0x18, 0x00, 0x00, 0x42, 0x00,
        0x00, 0x00, 0x33, 0x08, 0x80, 0x1c, 0xc4, 0xe1, 0x1c, 0x66, 0x14, 0x01, 0x3d, 0x88, 0x43, 0x38, 0x84, 0xc3, 0x8c, 0x42, 0x80, 0x07, 0x79, 0x78, 0x07, 0x73, 0x98, 0x71, 0x0c, 0xe6, 0x00, 0x0f,
        0xed, 0x10, 0x0e, 0xf4, 0x80, 0x0e, 0x33, 0x0c, 0x42, 0x1e, 0xc2, 0xc1, 0x1d, 0xce, 0xa1, 0x1c, 0x66, 0x30, 0x05, 0x3d, 0x88, 0x43, 0x38, 0x84, 0x83, 0x1b, 0xcc, 0x03, 0x3d, 0xc8, 0x43, 0x3d,
        0x8c, 0x03, 0x3d, 0xcc, 0x78, 0x8c, 0x74, 0x70, 0x07, 0x7b, 0x08, 0x07, 0x79, 0x48, 0x87, 0x70, 0x70, 0x07, 0x7a, 0x70, 0x03, 0x76, 0x78, 0x87, 0x70, 0x20, 0x87, 0x19, 0xcc, 0x11, 0x0e, 0xec,
        0x90, 0x0e, 0xe1, 0x30, 0x0f, 0x6e, 0x30, 0x0f, 0xe3, 0xf0, 0x0e, 0xf0, 0x50, 0x0e, 0x33, 0x10, 0xc4, 0x1d, 0xde, 0x21, 0x1c, 0xd8, 0x21, 0x1d, 0xc2, 0x61, 0x1e, 0x66, 0x30, 0x89, 0x3b, 0xbc,
        0x83, 0x3b, 0xd0, 0x43, 0x39, 0xb4, 0x03, 0x3c, 0xbc, 0x83, 0x3c, 0x84, 0x03, 0x3b, 0xcc, 0xf0, 0x14, 0x76, 0x60, 0x07, 0x7b, 0x68, 0x07, 0x37, 0x68, 0x87, 0x72, 0x68, 0x07, 0x37, 0x80, 0x87,
        0x70, 0x90, 0x87, 0x70, 0x60, 0x07, 0x76, 0x28, 0x07, 0x76, 0xf8, 0x05, 0x76, 0x78, 0x87, 0x77, 0x80, 0x87, 0x5f, 0x08, 0x87, 0x71, 0x18, 0x87, 0x72, 0x98, 0x87, 0x79, 0x98, 0x81, 0x2c, 0xee,
        0xf0, 0x0e, 0xee, 0xe0, 0x0e, 0xf5, 0xc0, 0x0e, 0xec, 0x30, 0x03, 0x62, 0xc8, 0xa1, 0x1c, 0xe4, 0xa1, 0x1c, 0xcc, 0xa1, 0x1c, 0xe4, 0xa1, 0x1c, 0xdc, 0x61, 0x1c, 0xca, 0x21, 0x1c, 0xc4, 0x81,
        0x1d, 0xca, 0x61, 0x06, 0xd6, 0x90, 0x43, 0x39, 0xc8, 0x43, 0x39, 0x98, 0x43, 0x39, 0xc8, 0x43, 0x39, 0xb8, 0xc3, 0x38, 0x94, 0x43, 0x38, 0x88, 0x03, 0x3b, 0x94, 0xc3, 0x2f, 0xbc, 0x83, 0x3c,
        0xfc, 0x82, 0x3b, 0xd4, 0x03, 0x3b, 0xb0, 0x03, 0x00, 0x00, 0x71, 0x20, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0x16, 0x50, 0x0d, 0x97, 0xef, 0x3c, 0xbe, 0x34, 0x39, 0x11, 0x81, 0x52, 0xd3, 0x43,
        0x4d, 0x7e, 0x71, 0xdb, 0x06, 0x40, 0x30, 0x00, 0xd2, 0x00, 0x61, 0x20, 0x00, 0x00, 0x17, 0x00, 0x00, 0x00, 0x13, 0x04, 0x41, 0x2c, 0x10, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x34, 0xa5,
        0x40, 0x54, 0x02, 0x45, 0x50, 0x06, 0x54, 0x23, 0x00, 0x63, 0x04, 0x20, 0x08, 0x82, 0xe8, 0x37, 0x46, 0x00, 0x82, 0x20, 0x08, 0x7f, 0x63, 0x04, 0x20, 0x08, 0x82, 0xf8, 0x07, 0x00, 0x23, 0x06,
        0x09, 0x00, 0x82, 0x60, 0x60, 0x48, 0x08, 0x04, 0x2d, 0xc4, 0x88, 0x41, 0x02, 0x80, 0x20, 0x18, 0x18, 0x12, 0x02, 0x41, 0xc7, 0x30, 0x62, 0x90, 0x00, 0x20, 0x08, 0x06, 0x86, 0x84, 0x40, 0x90,
        0x21, 0x8c, 0x18, 0x24, 0x00, 0x08, 0x82, 0x81, 0x21, 0x21, 0x10, 0x54, 0x04, 0x08, 0x00, 0x00, 0x00, 0x00,
    };

    memset(&desc, 0, sizeof(desc));
    desc.rt_format = DXGI_FORMAT_A8_UNORM;
    desc.no_pipeline = true;
    if (!init_test_context(&context, &desc))
        return;

    if (!context_supports_dxil(&context))
    {
        destroy_test_context(&context);
        return;
    }

    device = context.device;
    command_list = context.list;
    queue = context.queue;

    init_pipeline_state_desc_dxil(&pso_desc, context.root_signature, 0, NULL, NULL, NULL);
    pso_desc.RTVFormats[0] = DXGI_FORMAT_A8_UNORM;
    pso_desc.NumRenderTargets = 1;
    pso_desc.PS.pShaderBytecode = ps_code_dxil;
    pso_desc.PS.BytecodeLength = sizeof(ps_code_dxil);

    hr = ID3D12Device_CreateGraphicsPipelineState(device, &pso_desc,
            &IID_ID3D12PipelineState, (void **)&context.pipeline_state);
    ok(hr == S_OK, "Failed to create graphics pipeline state, hr %#x.\n", hr);

    ID3D12GraphicsCommandList_ClearRenderTargetView(command_list, context.rtv, black, 0, NULL);

    ID3D12GraphicsCommandList_OMSetRenderTargets(command_list, 1, &context.rtv, false, NULL);
    ID3D12GraphicsCommandList_SetGraphicsRootSignature(command_list, context.root_signature);
    ID3D12GraphicsCommandList_SetPipelineState(command_list, context.pipeline_state);
    ID3D12GraphicsCommandList_IASetPrimitiveTopology(command_list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D12GraphicsCommandList_RSSetViewports(command_list, 1, &context.viewport);
    ID3D12GraphicsCommandList_RSSetScissorRects(command_list, 1, &context.scissor_rect);
    ID3D12GraphicsCommandList_DrawInstanced(command_list, 3, 1, 0, 0);

    transition_resource_state(command_list, context.render_target,
            D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);

    check_sub_resource_uint8(context.render_target, 0, queue, command_list, 0xff, 0);

    destroy_test_context(&context);
}

void test_multisample_rendering(void)
{
    static const float white[] = {1.0f, 1.0f, 1.0f, 1.0f};
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc;
    ID3D12GraphicsCommandList *command_list;
    ID3D12PipelineState *ms_pipeline_state;
    D3D12_CPU_DESCRIPTOR_HANDLE ms_rtv;
    ID3D12Resource *ms_render_target;
    struct test_context_desc desc;
    struct test_context context;
    ID3D12DescriptorHeap *heap;
    ID3D12CommandQueue *queue;
    uint32_t sample;
    unsigned int i;
    HRESULT hr;

    static const DWORD ps_color_code[] =
    {
#if 0
        float4 main(uint id : SV_SampleIndex) : SV_Target
        {
            switch (id)
            {
                case 0:  return float4(1.0f, 0.0f, 0.0f, 1.0f);
                case 1:  return float4(0.0f, 1.0f, 0.0f, 1.0f);
                case 2:  return float4(0.0f, 0.0f, 1.0f, 1.0f);
                default: return float4(0.0f, 0.0f, 0.0f, 1.0f);
            }
        }
#endif
        0x43425844, 0x94c35f48, 0x04c6b0f7, 0x407d8214, 0xc24f01e5, 0x00000001, 0x00000194, 0x00000003,
        0x0000002c, 0x00000064, 0x00000098, 0x4e475349, 0x00000030, 0x00000001, 0x00000008, 0x00000020,
        0x00000000, 0x0000000a, 0x00000001, 0x00000000, 0x00000101, 0x535f5653, 0x6c706d61, 0x646e4965,
        0xab007865, 0x4e47534f, 0x0000002c, 0x00000001, 0x00000008, 0x00000020, 0x00000000, 0x00000000,
        0x00000003, 0x00000000, 0x0000000f, 0x545f5653, 0x65677261, 0xabab0074, 0x58454853, 0x000000f4,
        0x00000050, 0x0000003d, 0x0100086a, 0x04000863, 0x00101012, 0x00000000, 0x0000000a, 0x03000065,
        0x001020f2, 0x00000000, 0x0300004c, 0x0010100a, 0x00000000, 0x03000006, 0x00004001, 0x00000000,
        0x08000036, 0x001020f2, 0x00000000, 0x00004002, 0x3f800000, 0x00000000, 0x00000000, 0x3f800000,
        0x0100003e, 0x03000006, 0x00004001, 0x00000001, 0x08000036, 0x001020f2, 0x00000000, 0x00004002,
        0x00000000, 0x3f800000, 0x00000000, 0x3f800000, 0x0100003e, 0x03000006, 0x00004001, 0x00000002,
        0x08000036, 0x001020f2, 0x00000000, 0x00004002, 0x00000000, 0x00000000, 0x3f800000, 0x3f800000,
        0x0100003e, 0x0100000a, 0x08000036, 0x001020f2, 0x00000000, 0x00004002, 0x00000000, 0x00000000,
        0x00000000, 0x3f800000, 0x0100003e, 0x01000017, 0x0100003e,
    };
    static const D3D12_SHADER_BYTECODE ps_color = {ps_color_code, sizeof(ps_color_code)};
    static const DWORD ps_resolve_code[] =
    {
#if 0
        Texture2DMS<float4> t;

        uint sample;
        uint rt_size;

        float4 main(float4 position : SV_Position) : SV_Target
        {
            float3 p;
            t.GetDimensions(p.x, p.y, p.z);
            p *= float3(position.x / rt_size, position.y / rt_size, 0);
            return t.Load((int2)p.xy, sample);
        }
#endif
        0x43425844, 0x68a4590b, 0xc1ec3070, 0x1b957c43, 0x0c080741, 0x00000001, 0x000001c8, 0x00000003,
        0x0000002c, 0x00000060, 0x00000094, 0x4e475349, 0x0000002c, 0x00000001, 0x00000008, 0x00000020,
        0x00000000, 0x00000001, 0x00000003, 0x00000000, 0x0000030f, 0x505f5653, 0x7469736f, 0x006e6f69,
        0x4e47534f, 0x0000002c, 0x00000001, 0x00000008, 0x00000020, 0x00000000, 0x00000000, 0x00000003,
        0x00000000, 0x0000000f, 0x545f5653, 0x65677261, 0xabab0074, 0x58454853, 0x0000012c, 0x00000050,
        0x0000004b, 0x0100086a, 0x04000059, 0x00208e46, 0x00000000, 0x00000001, 0x04002058, 0x00107000,
        0x00000000, 0x00005555, 0x04002064, 0x00101032, 0x00000000, 0x00000001, 0x03000065, 0x001020f2,
        0x00000000, 0x02000068, 0x00000001, 0x06000056, 0x00100012, 0x00000000, 0x0020801a, 0x00000000,
        0x00000000, 0x0700000e, 0x00100032, 0x00000000, 0x00101046, 0x00000000, 0x00100006, 0x00000000,
        0x8900003d, 0x80000102, 0x00155543, 0x001000c2, 0x00000000, 0x00004001, 0x00000000, 0x001074e6,
        0x00000000, 0x07000038, 0x00100032, 0x00000000, 0x00100046, 0x00000000, 0x00100ae6, 0x00000000,
        0x0500001b, 0x00100032, 0x00000000, 0x00100046, 0x00000000, 0x08000036, 0x001000c2, 0x00000000,
        0x00004002, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x8c00002e, 0x80000102, 0x00155543,
        0x001020f2, 0x00000000, 0x00100e46, 0x00000000, 0x00107e46, 0x00000000, 0x0020800a, 0x00000000,
        0x00000000, 0x0100003e,
    };
    static const D3D12_SHADER_BYTECODE ps_resolve = {ps_resolve_code, sizeof(ps_resolve_code)};
    static const unsigned int expected_colors[] = {0xff0000ff, 0xff00ff00, 0xffff0000, 0xff000000};

    if (use_warp_device)
    {
        skip("Sample shading tests fail on WARP.\n");
        return;
    }

    memset(&desc, 0, sizeof(desc));
    desc.rt_width = desc.rt_height = 32;
    desc.rt_descriptor_count = 2;
    desc.no_root_signature = true;
    if (!init_test_context(&context, &desc))
        return;
    command_list = context.list;
    queue = context.queue;

    context.root_signature = create_texture_root_signature(context.device,
            D3D12_SHADER_VISIBILITY_PIXEL, 2, 0);

    init_pipeline_state_desc(&pso_desc, context.root_signature,
            context.render_target_desc.Format, NULL, &ps_resolve, NULL);
    hr = ID3D12Device_CreateGraphicsPipelineState(context.device, &pso_desc,
            &IID_ID3D12PipelineState, (void **)&context.pipeline_state);
    ok(hr == S_OK, "Failed to create pipeline, hr %#x.\n", hr);

    pso_desc.PS = ps_color;
    pso_desc.SampleDesc.Count = 4;
    hr = ID3D12Device_CreateGraphicsPipelineState(context.device, &pso_desc,
            &IID_ID3D12PipelineState, (void **)&ms_pipeline_state);
    ok(hr == S_OK, "Failed to create pipeline, hr %#x.\n", hr);

    ms_rtv = get_cpu_rtv_handle(&context, context.rtv_heap, 1);
    desc.sample_desc.Count = 4;
    create_render_target(&context, &desc, &ms_render_target, &ms_rtv);

    heap = create_gpu_descriptor_heap(context.device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1);
    ID3D12Device_CreateShaderResourceView(context.device, ms_render_target, NULL,
            get_cpu_descriptor_handle(&context, heap, 0));

    ID3D12GraphicsCommandList_ClearRenderTargetView(command_list, context.rtv, white, 0, NULL);
    ID3D12GraphicsCommandList_ClearRenderTargetView(command_list, ms_rtv, white, 0, NULL);
    ID3D12GraphicsCommandList_OMSetRenderTargets(command_list, 1, &ms_rtv, false, NULL);
    ID3D12GraphicsCommandList_SetGraphicsRootSignature(command_list, context.root_signature);
    ID3D12GraphicsCommandList_SetPipelineState(command_list, ms_pipeline_state);
    ID3D12GraphicsCommandList_IASetPrimitiveTopology(command_list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D12GraphicsCommandList_RSSetViewports(command_list, 1, &context.viewport);
    ID3D12GraphicsCommandList_RSSetScissorRects(command_list, 1, &context.scissor_rect);
    ID3D12GraphicsCommandList_SetDescriptorHeaps(command_list, 1, &heap);
    ID3D12GraphicsCommandList_SetGraphicsRootDescriptorTable(command_list, 0,
            get_gpu_descriptor_handle(&context, heap, 0));
    ID3D12GraphicsCommandList_DrawInstanced(command_list, 3, 1, 0, 0);

    transition_resource_state(command_list, ms_render_target,
            D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_RESOLVE_SOURCE);
    transition_resource_state(command_list, context.render_target,
            D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_RESOLVE_DEST);
    ID3D12GraphicsCommandList_ResolveSubresource(command_list,
            context.render_target, 0, ms_render_target, 0, context.render_target_desc.Format);
    transition_resource_state(command_list, ms_render_target,
            D3D12_RESOURCE_STATE_RESOLVE_SOURCE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    transition_resource_state(command_list, context.render_target,
            D3D12_RESOURCE_STATE_RESOLVE_DEST, D3D12_RESOURCE_STATE_COPY_SOURCE);
    check_sub_resource_uint(context.render_target, 0, queue, command_list, 0xff404040, 2);

    for (i = 0; i < ARRAY_SIZE(expected_colors); ++i)
    {
        reset_command_list(command_list, context.allocator);
        transition_resource_state(command_list, context.render_target,
                D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);

        ID3D12GraphicsCommandList_ClearRenderTargetView(command_list, context.rtv, white, 0, NULL);
        ID3D12GraphicsCommandList_OMSetRenderTargets(command_list, 1, &context.rtv, false, NULL);
        ID3D12GraphicsCommandList_SetGraphicsRootSignature(command_list, context.root_signature);
        ID3D12GraphicsCommandList_SetPipelineState(command_list, context.pipeline_state);
        ID3D12GraphicsCommandList_IASetPrimitiveTopology(command_list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ID3D12GraphicsCommandList_RSSetViewports(command_list, 1, &context.viewport);
        ID3D12GraphicsCommandList_RSSetScissorRects(command_list, 1, &context.scissor_rect);
        ID3D12GraphicsCommandList_SetDescriptorHeaps(command_list, 1, &heap);
        ID3D12GraphicsCommandList_SetGraphicsRootDescriptorTable(command_list, 0,
                get_gpu_descriptor_handle(&context, heap, 0));
        ID3D12GraphicsCommandList_SetGraphicsRoot32BitConstants(command_list, 1, 1, &desc.rt_width, 1);

        sample = i;
        ID3D12GraphicsCommandList_SetGraphicsRoot32BitConstants(command_list, 1, 1, &sample, 0);
        ID3D12GraphicsCommandList_DrawInstanced(command_list, 3, 1, 0, 0);

        transition_resource_state(command_list, context.render_target,
                D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
        check_sub_resource_uint(context.render_target, 0, queue, command_list, expected_colors[i], 0);
    }

    ID3D12DescriptorHeap_Release(heap);
    ID3D12Resource_Release(ms_render_target);
    ID3D12PipelineState_Release(ms_pipeline_state);
    destroy_test_context(&context);
}

void test_rendering_no_attachments_layers(void)
{
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc;
    D3D12_FEATURE_DATA_D3D12_OPTIONS options;
    D3D12_ROOT_SIGNATURE_DESC rs_desc;
    D3D12_ROOT_PARAMETER root_param;
    struct test_context_desc desc;
    struct test_context context;
    struct resource_readback rb;
    unsigned int layer, x, y;
    ID3D12Resource *buffer;
    const struct vec4 *val;
    D3D12_VIEWPORT vp;
    D3D12_RECT sci;
    HRESULT hr;

#if 0
    struct VOut
    {
        float4 pos : SV_Position;
        uint layer : SV_RenderTargetArrayIndex;
    };

    VOut vs_main(uint vid : SV_VertexID, uint iid : SV_InstanceID)
    {
        VOut v;
        if (vid == 0)
            v.pos = float4(-1, -1, 0, 1);
        else if (vid == 1)
            v.pos = float4(-1, +3, 0, 1);
        else
            v.pos = float4(+3, -1, 0, 1);
        v.layer = iid;
        return v;
    }

    RWStructuredBuffer<float4> RWBuf : register(u0);
    void ps_main(VOut pin)
    {
        int2 coord = int2(pin.pos.xy) - int2(10000, 12000);
        int write_coord = 16 * pin.layer + coord.y * 4 + coord.x;
        float4 write_value = float4(pin.pos.xy, pin.layer, 0.0);
        RWBuf[write_coord] = write_value;
    }
#endif
    static const DWORD vs_code[] =
    {
        0x43425844, 0xcfb38ed1, 0xef3799fa, 0xf3032807, 0xf1c98b47, 0x00000001, 0x00000200, 0x00000004,
        0x00000030, 0x0000008c, 0x000000f4, 0x000001f0, 0x4e475349, 0x00000054, 0x00000002, 0x00000008,
        0x00000038, 0x00000000, 0x00000006, 0x00000001, 0x00000000, 0x00000101, 0x00000044, 0x00000000,
        0x00000008, 0x00000001, 0x00000001, 0x00000101, 0x565f5653, 0x65747265, 0x00444978, 0x495f5653,
        0x6174736e, 0x4965636e, 0xabab0044, 0x4e47534f, 0x00000060, 0x00000002, 0x00000008, 0x00000038,
        0x00000000, 0x00000001, 0x00000003, 0x00000000, 0x0000000f, 0x00000044, 0x00000000, 0x00000004,
        0x00000001, 0x00000001, 0x00000e01, 0x505f5653, 0x7469736f, 0x006e6f69, 0x525f5653, 0x65646e65,
        0x72615472, 0x41746567, 0x79617272, 0x65646e49, 0xabab0078, 0x58454853, 0x000000f4, 0x00010050,
        0x0000003d, 0x0100086a, 0x04000060, 0x00101012, 0x00000000, 0x00000006, 0x04000060, 0x00101012,
        0x00000001, 0x00000008, 0x04000067, 0x001020f2, 0x00000000, 0x00000001, 0x04000067, 0x00102012,
        0x00000001, 0x00000004, 0x02000068, 0x00000001, 0x07000020, 0x00100012, 0x00000000, 0x0010100a,
        0x00000000, 0x00004001, 0x00000001, 0x0f000037, 0x001000f2, 0x00000000, 0x00100006, 0x00000000,
        0x00004002, 0xbf800000, 0x40400000, 0x00000000, 0x3f800000, 0x00004002, 0x40400000, 0xbf800000,
        0x00000000, 0x3f800000, 0x0c000037, 0x001020f2, 0x00000000, 0x00101006, 0x00000000, 0x00100e46,
        0x00000000, 0x00004002, 0xbf800000, 0xbf800000, 0x00000000, 0x3f800000, 0x05000036, 0x00102012,
        0x00000001, 0x0010100a, 0x00000001, 0x0100003e, 0x30494653, 0x00000008, 0x00002000, 0x00000000,
    };

    static const DWORD ps_code[] =
    {
        0x43425844, 0x8e7429f9, 0xd132da4b, 0x63385208, 0x474ae15d, 0x00000001, 0x00000200, 0x00000003,
        0x0000002c, 0x00000094, 0x000000a4, 0x4e475349, 0x00000060, 0x00000002, 0x00000008, 0x00000038,
        0x00000000, 0x00000001, 0x00000003, 0x00000000, 0x0000030f, 0x00000044, 0x00000000, 0x00000004,
        0x00000001, 0x00000001, 0x00000101, 0x505f5653, 0x7469736f, 0x006e6f69, 0x525f5653, 0x65646e65,
        0x72615472, 0x41746567, 0x79617272, 0x65646e49, 0xabab0078, 0x4e47534f, 0x00000008, 0x00000000,
        0x00000008, 0x58454853, 0x00000154, 0x00000050, 0x00000055, 0x0100086a, 0x0400009e, 0x0011e000,
        0x00000000, 0x00000010, 0x04002064, 0x00101032, 0x00000000, 0x00000001, 0x04000864, 0x00101012,
        0x00000001, 0x00000004, 0x02000068, 0x00000002, 0x07000029, 0x00100012, 0x00000000, 0x0010100a,
        0x00000001, 0x00004001, 0x00000004, 0x0500001b, 0x00100062, 0x00000000, 0x00101106, 0x00000000,
        0x0a00001e, 0x00100062, 0x00000000, 0x00100656, 0x00000000, 0x00004002, 0x00000000, 0xffffd8f0,
        0xffffd120, 0x00000000, 0x07000029, 0x00100042, 0x00000000, 0x0010002a, 0x00000000, 0x00004001,
        0x00000002, 0x0700001e, 0x00100012, 0x00000000, 0x0010002a, 0x00000000, 0x0010000a, 0x00000000,
        0x0700001e, 0x00100012, 0x00000000, 0x0010001a, 0x00000000, 0x0010000a, 0x00000000, 0x05000056,
        0x00100042, 0x00000001, 0x0010100a, 0x00000001, 0x05000036, 0x00100032, 0x00000001, 0x00101046,
        0x00000000, 0x05000036, 0x00100082, 0x00000001, 0x00004001, 0x00000000, 0x090000a8, 0x0011e0f2,
        0x00000000, 0x0010000a, 0x00000000, 0x00004001, 0x00000000, 0x00100e46, 0x00000001, 0x0100003e,
    };

    static const D3D12_SHADER_BYTECODE vs = SHADER_BYTECODE(vs_code);
    static const D3D12_SHADER_BYTECODE ps = SHADER_BYTECODE(ps_code);

    memset(&desc, 0, sizeof(desc));
    desc.no_render_target = true;
    desc.no_pipeline = true;
    desc.no_root_signature = true;
    if (!init_test_context(&context, &desc))
        return;

    if (FAILED(ID3D12Device_CheckFeatureSupport(context.device, D3D12_FEATURE_D3D12_OPTIONS, &options, sizeof(options))) ||
            !options.VPAndRTArrayIndexFromAnyShaderFeedingRasterizerSupportedWithoutGSEmulation)
    {
        skip("Cannot render layers from VS. Skipping.\n");
        destroy_test_context(&context);
        return;
    }

    memset(&root_param, 0, sizeof(root_param));
    memset(&rs_desc, 0, sizeof(rs_desc));
    rs_desc.NumParameters = 1;
    rs_desc.pParameters = &root_param;
    root_param.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    root_param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    create_root_signature(context.device, &rs_desc, &context.root_signature);

    buffer = create_default_buffer(context.device, 64 * 1024,
            D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    init_pipeline_state_desc(&pso_desc, context.root_signature, DXGI_FORMAT_UNKNOWN,
            &vs, &ps, NULL);
    pso_desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;

    hr = ID3D12Device_CreateGraphicsPipelineState(context.device, &pso_desc,
            &IID_ID3D12PipelineState, (void**)&context.pipeline_state);
    ok(SUCCEEDED(hr), "Failed to create PSO, hr #%x.\n", hr);

    ID3D12GraphicsCommandList_OMSetRenderTargets(context.list, 0, NULL, FALSE, NULL);

    set_viewport(&vp, 10000.0f, 12000.0f, 4.0f, 4.0f, 0.0f, 1.0f);
    set_rect(&sci, 10000, 12000, 10004, 12004);
    ID3D12GraphicsCommandList_RSSetViewports(context.list, 1, &vp);
    ID3D12GraphicsCommandList_RSSetScissorRects(context.list, 1, &sci);
    ID3D12GraphicsCommandList_SetGraphicsRootSignature(context.list, context.root_signature);
    ID3D12GraphicsCommandList_SetPipelineState(context.list, context.pipeline_state);
    ID3D12GraphicsCommandList_IASetPrimitiveTopology(context.list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D12GraphicsCommandList_SetGraphicsRootUnorderedAccessView(context.list, 0, ID3D12Resource_GetGPUVirtualAddress(buffer));
    ID3D12GraphicsCommandList_DrawInstanced(context.list, 3, 16, 0, 0);

    transition_resource_state(context.list, buffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
    get_buffer_readback_with_command_list(buffer, DXGI_FORMAT_R32G32B32A32_FLOAT, &rb, context.queue, context.list);

    for (layer = 0; layer < 16; layer++)
    {
        for (y = 0; y < 4; y++)
        {
            for (x = 0; x < 4; x++)
            {
                val = get_readback_vec4(&rb, layer * 16 + y * 4 + x, 0);
                ok(val->x == 10000.5f + (float)x, "%u, %u, %u: Unexpected value (x) = %f.\n", x, y, layer, val->x);
                ok(val->y == 12000.5f + (float)y, "%u, %u, %u: Unexpected value (y) = %f.\n", x, y, layer, val->y);
                ok(val->z == (float)layer, "%u, %u, %u: Unexpected value (z) = %f.\n", x, y, layer, val->z);
                ok(val->w == 0.0f, "%u, %u, %u: Unexpected value (w) = %f.\n", x, y, layer, val->w);
            }
        }
    }

    release_resource_readback(&rb);
    ID3D12Resource_Release(buffer);
    destroy_test_context(&context);
}

void test_renderpass_validation(void)
{
    D3D12_RENDER_PASS_RENDER_TARGET_DESC rtv_infos[2];
    D3D12_FEATURE_DATA_D3D12_OPTIONS18 options18;
    ID3D12GraphicsCommandList4 *command_list4;
    D3D12_HEAP_PROPERTIES heap_properties;
    ID3D12CommandAllocator *allocator;
    D3D12_RESOURCE_DESC resource_desc;
    ID3D12DescriptorHeap *rtv_heap;
    ID3D12Resource *rt[4], *ds;
    ID3D12Device* device;
    unsigned int i;
    HRESULT hr;

    static const FLOAT black[] = { 0.0f, 0.0f, 0.0f, 0.0f };

    if (!(device = create_device()))
        return;

    memset(&options18, 0, sizeof(options18));
    ID3D12Device_CheckFeatureSupport(device, D3D12_FEATURE_D3D12_OPTIONS18, &options18, sizeof(options18));

    if (!options18.RenderPassesValid)
    {
        skip("Render passes not supported.\n");
        ID3D12Device_Release(device);
        return;
    }

    hr = ID3D12Device_CreateCommandAllocator(device, D3D12_COMMAND_LIST_TYPE_DIRECT, &IID_ID3D12CommandAllocator, (void**)&allocator);
    ok(hr == S_OK, "Failed to create command allocator, hr %#x.\n");

    hr = ID3D12Device_CreateCommandList(device, 0, D3D12_COMMAND_LIST_TYPE_DIRECT,
        allocator, NULL, &IID_ID3D12GraphicsCommandList4, (void**)&command_list4);

    if (FAILED(hr))
    {
        skip("ID3D12GraphicsCommandList4 not supported.\n");
        ID3D12Device_Release(device);
        return;
    }

    rtv_heap = create_cpu_descriptor_heap(device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, ARRAY_SIZE(rt));

    /* Create dummy resources to bind as render targets */
    memset(&heap_properties, 0, sizeof(heap_properties));
    heap_properties.Type = D3D12_HEAP_TYPE_DEFAULT;

    memset(&resource_desc, 0, sizeof(resource_desc));
    resource_desc.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
    resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    resource_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    resource_desc.Width = 4;
    resource_desc.Height = 4;
    resource_desc.DepthOrArraySize = 1;
    resource_desc.MipLevels = 1;
    resource_desc.SampleDesc.Count = 1;
    resource_desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    resource_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

    hr = ID3D12Device_CreateCommittedResource(device, &heap_properties, 0, &resource_desc,
            D3D12_RESOURCE_STATE_RENDER_TARGET, NULL, &IID_ID3D12Resource, (void**)&rt[0]);
    ok(hr == S_OK, "Failed to create render target, hr %#x.\n", hr);

    resource_desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;

    hr = ID3D12Device_CreateCommittedResource(device, &heap_properties, 0, &resource_desc,
            D3D12_RESOURCE_STATE_RENDER_TARGET, NULL, &IID_ID3D12Resource, (void**)&rt[1]);
    ok(hr == S_OK, "Failed to create render target, hr %#x.\n", hr);

    resource_desc.Width = 16;
    resource_desc.Height = 16;

    hr = ID3D12Device_CreateCommittedResource(device, &heap_properties, 0, &resource_desc,
            D3D12_RESOURCE_STATE_RENDER_TARGET, NULL, &IID_ID3D12Resource, (void**)&rt[2]);
    ok(hr == S_OK, "Failed to create render target, hr %#x.\n", hr);

    resource_desc.SampleDesc.Count = 4;

    hr = ID3D12Device_CreateCommittedResource(device, &heap_properties, 0, &resource_desc,
            D3D12_RESOURCE_STATE_RENDER_TARGET, NULL, &IID_ID3D12Resource, (void**)&rt[3]);
    ok(hr == S_OK, "Failed to create render target, hr %#x.\n", hr);

    resource_desc.Format = DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
    resource_desc.SampleDesc.Count = 1;
    resource_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

    hr = ID3D12Device_CreateCommittedResource(device, &heap_properties, 0, &resource_desc, D3D12_RESOURCE_STATE_DEPTH_WRITE, NULL, &IID_ID3D12Resource, (void**)&ds);
    ok(hr == S_OK, "Failed to create render target, hr %#x.\n", hr);

    for (i = 0; i < ARRAY_SIZE(rt); i++)
        ID3D12Device_CreateRenderTargetView(device, rt[i], NULL, get_cpu_handle(device, rtv_heap, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, i));

    /* Test whether beginning a render pass with no attachments is allowed */
    ID3D12GraphicsCommandList4_BeginRenderPass(command_list4, 0, NULL, NULL, 0);
    ID3D12GraphicsCommandList4_EndRenderPass(command_list4);
    hr = ID3D12GraphicsCommandList4_Close(command_list4);
    ok(SUCCEEDED(hr), "Got hr %#x, expected S_OK.\n", hr);
    ID3D12GraphicsCommandList4_Release(command_list4);

    /* Test that binding a simple RTV works */
    ID3D12CommandAllocator_Reset(allocator);
    ID3D12Device_CreateCommandList(device, 0, D3D12_COMMAND_LIST_TYPE_DIRECT,
        allocator, NULL, &IID_ID3D12GraphicsCommandList4, (void**)&command_list4);

    memset(rtv_infos, 0, sizeof(rtv_infos));

    for (i = 0; i < ARRAY_SIZE(rtv_infos); i++)
    {
        rtv_infos[i].cpuDescriptor = get_cpu_handle(device, rtv_heap, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, i);
        rtv_infos[i].BeginningAccess.Type = D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_PRESERVE;
        rtv_infos[i].EndingAccess.Type = D3D12_RENDER_PASS_ENDING_ACCESS_TYPE_PRESERVE;
    }

    ID3D12GraphicsCommandList4_BeginRenderPass(command_list4, 1, rtv_infos, NULL, 0);
    ID3D12GraphicsCommandList4_EndRenderPass(command_list4);
    hr = ID3D12GraphicsCommandList4_Close(command_list4);
    ok(SUCCEEDED(hr), "Got hr %#x, expected S_OK.\n", hr);
    ID3D12GraphicsCommandList4_Release(command_list4);

    /* Test that not matching BeginRenderPass and EndRenderPass fails */
    ID3D12CommandAllocator_Reset(allocator);
    ID3D12Device_CreateCommandList(device, 0, D3D12_COMMAND_LIST_TYPE_DIRECT,
        allocator, NULL, &IID_ID3D12GraphicsCommandList4, (void**)&command_list4);
    ID3D12GraphicsCommandList4_BeginRenderPass(command_list4, 1, rtv_infos, NULL, 0);
    hr = ID3D12GraphicsCommandList4_Close(command_list4);
    ok(FAILED(hr), "Got hr %#x, expected E_FAIL.\n", hr);
    ID3D12GraphicsCommandList4_Release(command_list4);

    ID3D12CommandAllocator_Reset(allocator);
    ID3D12Device_CreateCommandList(device, 0, D3D12_COMMAND_LIST_TYPE_DIRECT,
        allocator, NULL, &IID_ID3D12GraphicsCommandList4, (void**)&command_list4);
    ID3D12GraphicsCommandList4_EndRenderPass(command_list4);
    hr = ID3D12GraphicsCommandList4_Close(command_list4);
    ok(FAILED(hr), "Got hr %#x, expected E_INVALIDARG.\n", hr);
    ID3D12GraphicsCommandList4_Release(command_list4);

    ID3D12CommandAllocator_Reset(allocator);
    ID3D12Device_CreateCommandList(device, 0, D3D12_COMMAND_LIST_TYPE_DIRECT,
        allocator, NULL, &IID_ID3D12GraphicsCommandList4, (void**)&command_list4);
    ID3D12GraphicsCommandList4_BeginRenderPass(command_list4, 1, rtv_infos, NULL, 0);
    ID3D12GraphicsCommandList4_BeginRenderPass(command_list4, 1, rtv_infos, NULL, 0);
    ID3D12GraphicsCommandList4_EndRenderPass(command_list4);
    hr = ID3D12GraphicsCommandList4_Close(command_list4);
    ok(FAILED(hr), "Got hr %#x, expected E_INVALIDARG.\n", hr);
    ID3D12GraphicsCommandList4_Release(command_list4);

    ID3D12CommandAllocator_Reset(allocator);
    ID3D12Device_CreateCommandList(device, 0, D3D12_COMMAND_LIST_TYPE_DIRECT,
        allocator, NULL, &IID_ID3D12GraphicsCommandList4, (void**)&command_list4);
    ID3D12GraphicsCommandList4_BeginRenderPass(command_list4, 1, rtv_infos, NULL, 0);
    ID3D12GraphicsCommandList4_EndRenderPass(command_list4);
    ID3D12GraphicsCommandList4_EndRenderPass(command_list4);
    hr = ID3D12GraphicsCommandList4_Close(command_list4);
    ok(FAILED(hr), "Got hr %#x, expected E_INVALIDARG.\n", hr);
    ID3D12GraphicsCommandList4_Release(command_list4);

    /* Test consecutive render pass validation with suspend/resume flags */
    ID3D12CommandAllocator_Reset(allocator);
    ID3D12Device_CreateCommandList(device, 0, D3D12_COMMAND_LIST_TYPE_DIRECT,
        allocator, NULL, &IID_ID3D12GraphicsCommandList4, (void**)&command_list4);
    ID3D12GraphicsCommandList4_BeginRenderPass(command_list4, 2, rtv_infos, NULL, D3D12_RENDER_PASS_FLAG_SUSPENDING_PASS);
    ID3D12GraphicsCommandList4_EndRenderPass(command_list4);
    ID3D12GraphicsCommandList4_BeginRenderPass(command_list4, 2, rtv_infos, NULL, D3D12_RENDER_PASS_FLAG_SUSPENDING_PASS | D3D12_RENDER_PASS_FLAG_RESUMING_PASS);
    ID3D12GraphicsCommandList4_EndRenderPass(command_list4);
    /* Changing the render target order is explicitly allowed */
    for (i = 0; i < 2; i++)
        rtv_infos[i].cpuDescriptor = get_cpu_handle(device, rtv_heap, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1 - i);
    ID3D12GraphicsCommandList4_BeginRenderPass(command_list4, 2, rtv_infos, NULL, D3D12_RENDER_PASS_FLAG_RESUMING_PASS);
    ID3D12GraphicsCommandList4_EndRenderPass(command_list4);
    /* This is invalid, but not checked by the runtime */
    ID3D12GraphicsCommandList4_BeginRenderPass(command_list4, 2, rtv_infos, NULL, D3D12_RENDER_PASS_FLAG_RESUMING_PASS);
    ID3D12GraphicsCommandList4_EndRenderPass(command_list4);
    hr = ID3D12GraphicsCommandList4_Close(command_list4);
    ok(SUCCEEDED(hr), "Got hr %#x, expected S_OK.\n", hr);
    ID3D12GraphicsCommandList4_Release(command_list4);

    ID3D12CommandAllocator_Reset(allocator);
    ID3D12Device_CreateCommandList(device, 0, D3D12_COMMAND_LIST_TYPE_DIRECT,
        allocator, NULL, &IID_ID3D12GraphicsCommandList4, (void**)&command_list4);
    /* Changing render targets in any way is not permited for suspend/resume within the same command list */
    ID3D12GraphicsCommandList4_BeginRenderPass(command_list4, 1, rtv_infos, NULL, D3D12_RENDER_PASS_FLAG_SUSPENDING_PASS);
    ID3D12GraphicsCommandList4_EndRenderPass(command_list4);
    ID3D12GraphicsCommandList4_BeginRenderPass(command_list4, 2, rtv_infos, NULL, D3D12_RENDER_PASS_FLAG_RESUMING_PASS);
    ID3D12GraphicsCommandList4_EndRenderPass(command_list4);
    hr = ID3D12GraphicsCommandList4_Close(command_list4);
    todo ok(FAILED(hr), "Got hr %#x, expected E_INVALIDARG.\n", hr);
    ID3D12GraphicsCommandList4_Release(command_list4);

    ID3D12CommandAllocator_Reset(allocator);
    ID3D12Device_CreateCommandList(device, 0, D3D12_COMMAND_LIST_TYPE_DIRECT,
        allocator, NULL, &IID_ID3D12GraphicsCommandList4, (void**)&command_list4);
    /* Unlike resume after resume, suspend after suspend is checked by the runtime */
    ID3D12GraphicsCommandList4_BeginRenderPass(command_list4, 2, rtv_infos, NULL, D3D12_RENDER_PASS_FLAG_SUSPENDING_PASS);
    ID3D12GraphicsCommandList4_EndRenderPass(command_list4);
    ID3D12GraphicsCommandList4_BeginRenderPass(command_list4, 2, rtv_infos, NULL, D3D12_RENDER_PASS_FLAG_SUSPENDING_PASS);
    ID3D12GraphicsCommandList4_EndRenderPass(command_list4);
    hr = ID3D12GraphicsCommandList4_Close(command_list4);
    todo ok(FAILED(hr), "Got hr %#x, expected E_INVALIDARG.\n", hr);
    ID3D12GraphicsCommandList4_Release(command_list4);

    /* Test that executing certain commands during a render pass fails */
    ID3D12CommandAllocator_Reset(allocator);
    ID3D12Device_CreateCommandList(device, 0, D3D12_COMMAND_LIST_TYPE_DIRECT,
        allocator, NULL, &IID_ID3D12GraphicsCommandList4, (void**)&command_list4);
    ID3D12GraphicsCommandList4_BeginRenderPass(command_list4, 2, rtv_infos, NULL, 0);
    ID3D12GraphicsCommandList4_ClearRenderTargetView(command_list4, get_cpu_handle(device, rtv_heap, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 0), black, 0, NULL);
    ID3D12GraphicsCommandList4_EndRenderPass(command_list4);
    hr = ID3D12GraphicsCommandList4_Close(command_list4);
    ok(FAILED(hr), "Got hr %#x, expected E_FAIL.\n", hr);
    ID3D12GraphicsCommandList4_Release(command_list4);

    /* Barriers inside render passes, GPU work occuring between render passes with
     * PRESERVE_LOCAL or suspend/resume flags are not validated by the runtime. */

    ID3D12CommandAllocator_Release(allocator);

    for (i = 0; i < ARRAY_SIZE(rt); i++)
        ID3D12Resource_Release(rt[i]);

    ID3D12Resource_Release(ds);

    ID3D12DescriptorHeap_Release(rtv_heap);

    ID3D12Device_Release(device);
}

void test_renderpass_rendering(void)
{
    D3D12_RENDER_PASS_ENDING_ACCESS_RESOLVE_SUBRESOURCE_PARAMETERS rtv_resolves[5];
    D3D12_RENDER_PASS_RENDER_TARGET_DESC rtv_infos[3];
    D3D12_RENDER_PASS_DEPTH_STENCIL_DESC dsv_info;
    D3D12_FEATURE_DATA_D3D12_OPTIONS18 options18;
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc;
    ID3D12GraphicsCommandList4 *command_list4;
    ID3D12DescriptorHeap *rtv_heap, *dsv_heap;
    D3D12_RENDER_TARGET_VIEW_DESC rtv_desc;
    D3D12_DEPTH_STENCIL_VIEW_DESC dsv_desc;
    D3D12_HEAP_PROPERTIES heap_properties;
    ID3D12RootSignature *root_signature;
    D3D12_ROOT_SIGNATURE_DESC rs_desc;
    D3D12_RESOURCE_DESC resource_desc;
    ID3D12PipelineState *pso, *pso_ms;
    ID3D12Resource *rt, *rt_ms, *ds;
    struct test_context_desc desc;
    D3D12_ROOT_PARAMETER rs_param;
    struct resource_readback rb;
    struct test_context context;
    D3D12_VIEWPORT viewport;
    unsigned int i, x, y;
    D3D12_RECT scissor;
    HRESULT hr;

    static const float black[] = { 0.0f, 0.0f, 0.0f, 0.0f };
    static const float white[] = { 1.0f, 1.0f, 1.0f, 1.0f };
    static const float red[]   = { 1.0f, 0.0f, 0.0f, 1.0f };
    static const float green[] = { 0.0f, 1.0f, 0.0f, 1.0f };
    static const float blue[]  = { 0.0f, 0.0f, 1.0f, 1.0f };

    static const DWORD ps_code[] =
    {
#if 0
    cbuffer color_t : register(b0)
    {
        float depth;
        uint3 colors;
        uint sample_mask;
    }

    struct out_t
    {
        float d : SV_Depth;
        float4 a : SV_Target0;
        float4 b : SV_Target1;
        float4 c : SV_Target2;
        uint sm : SV_Coverage;
    };

    float4 unpack_unorm(uint un)
    {
        return float4((un >>  0) & 0xff, (un >>  8) & 0xff,
                (un >> 16) & 0xff, (un >> 24) & 0xff) / 255.0f;
    }

    out_t main()
    {
        out_t result;
        result.d = depth;
        result.a = unpack_unorm(colors.x);
        result.b = unpack_unorm(colors.y);
        result.c = unpack_unorm(colors.z);
        result.sm = sample_mask;
        return result;
    }
#endif
        0x43425844, 0x19e95b1b, 0x8933e8f2, 0xf2efe245, 0xbd578427, 0x00000001, 0x00000374, 0x00000003,
        0x0000002c, 0x0000003c, 0x000000e4, 0x4e475349, 0x00000008, 0x00000000, 0x00000008, 0x4e47534f,
        0x000000a0, 0x00000005, 0x00000008, 0x00000080, 0x00000000, 0x00000000, 0x00000003, 0x00000000,
        0x0000000f, 0x00000080, 0x00000001, 0x00000000, 0x00000003, 0x00000001, 0x0000000f, 0x00000080,
        0x00000002, 0x00000000, 0x00000003, 0x00000002, 0x0000000f, 0x0000008a, 0x00000000, 0x00000000,
        0x00000001, 0xffffffff, 0x00000e01, 0x00000096, 0x00000000, 0x00000000, 0x00000003, 0xffffffff,
        0x00000e01, 0x545f5653, 0x65677261, 0x56530074, 0x766f435f, 0x67617265, 0x56530065, 0x7065445f,
        0xab006874, 0x58454853, 0x00000288, 0x00000050, 0x000000a2, 0x0100086a, 0x04000059, 0x00208e46,
        0x00000000, 0x00000002, 0x03000065, 0x001020f2, 0x00000000, 0x03000065, 0x001020f2, 0x00000001,
        0x03000065, 0x001020f2, 0x00000002, 0x02000065, 0x0000f000, 0x02000065, 0x0000c001, 0x02000068,
        0x00000004, 0x05000036, 0x0000c001, 0x0020800a, 0x00000000, 0x00000000, 0x1000008a, 0x001000f2,
        0x00000000, 0x00004002, 0x00000008, 0x00000008, 0x00000008, 0x00000008, 0x00004002, 0x00000008,
        0x00000010, 0x00000008, 0x00000010, 0x00208a56, 0x00000000, 0x00000000, 0x05000056, 0x00100062,
        0x00000001, 0x00100106, 0x00000000, 0x05000056, 0x00100062, 0x00000000, 0x00100ba6, 0x00000000,
        0x0b000001, 0x00100072, 0x00000002, 0x00208796, 0x00000000, 0x00000000, 0x00004002, 0x000000ff,
        0x000000ff, 0x000000ff, 0x00000000, 0x05000056, 0x00100012, 0x00000001, 0x0010000a, 0x00000002,
        0x0b000055, 0x00100072, 0x00000003, 0x00208796, 0x00000000, 0x00000000, 0x00004002, 0x00000018,
        0x00000018, 0x00000018, 0x00000000, 0x05000056, 0x00100082, 0x00000001, 0x0010000a, 0x00000003,
        0x0a000038, 0x001020f2, 0x00000000, 0x00100e46, 0x00000001, 0x00004002, 0x3b808081, 0x3b808081,
        0x3b808081, 0x3b808081, 0x05000056, 0x00100012, 0x00000000, 0x0010001a, 0x00000002, 0x05000056,
        0x00100012, 0x00000001, 0x0010002a, 0x00000002, 0x05000056, 0x00100082, 0x00000000, 0x0010001a,
        0x00000003, 0x05000056, 0x00100082, 0x00000001, 0x0010002a, 0x00000003, 0x0a000038, 0x001020f2,
        0x00000001, 0x00100e46, 0x00000000, 0x00004002, 0x3b808081, 0x3b808081, 0x3b808081, 0x3b808081,
        0x1000008a, 0x00100032, 0x00000000, 0x00004002, 0x00000008, 0x00000008, 0x00000000, 0x00000000,
        0x00004002, 0x00000008, 0x00000010, 0x00000000, 0x00000000, 0x00208ff6, 0x00000000, 0x00000000,
        0x05000056, 0x00100062, 0x00000001, 0x00100106, 0x00000000, 0x0a000038, 0x001020f2, 0x00000002,
        0x00100e46, 0x00000001, 0x00004002, 0x3b808081, 0x3b808081, 0x3b808081, 0x3b808081, 0x05000036,
        0x0000f001, 0x0020800a, 0x00000000, 0x00000001, 0x0100003e,
    };
    static const D3D12_SHADER_BYTECODE ps = {ps_code, sizeof(ps_code)};

    struct
    {
        float depth;
        uint32_t colors[3];
        uint32_t sample_mask;
    } shader_args;

    memset(&desc, 0, sizeof(desc));
    desc.no_render_target = true;
    desc.no_root_signature = true;
    desc.no_pipeline = true;
    if (!init_test_context(&context, &desc))
        return;

    memset(&options18, 0, sizeof(options18));
    ID3D12Device_CheckFeatureSupport(context.device, D3D12_FEATURE_D3D12_OPTIONS18, &options18, sizeof(options18));

    if (!options18.RenderPassesValid)
    {
        skip("Render passes not supported.\n");
        destroy_test_context(&context);
        return;
    }

    hr = ID3D12GraphicsCommandList_QueryInterface(context.list, &IID_ID3D12GraphicsCommandList4, (void**)&command_list4);

    if (FAILED(hr))
    {
        skip("ID3D12GraphicsCommandList4 not supported.\n");
        destroy_test_context(&context);
        return;
    }

    memset(&heap_properties, 0, sizeof(heap_properties));
    heap_properties.Type = D3D12_HEAP_TYPE_DEFAULT;

    memset(&resource_desc, 0, sizeof(resource_desc));
    resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    resource_desc.Width = 4;
    resource_desc.Height = 4;
    resource_desc.DepthOrArraySize = 3;
    resource_desc.MipLevels = 1;
    resource_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    resource_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

    resource_desc.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
    resource_desc.SampleDesc.Count = 1;

    hr = ID3D12Device_CreateCommittedResource(context.device, &heap_properties,
            D3D12_HEAP_FLAG_NONE, &resource_desc, D3D12_RESOURCE_STATE_RENDER_TARGET,
            NULL, &IID_ID3D12Resource, (void**)&rt);
    ok(hr == S_OK, "Failed to create render target, hr %#x.\n");

    resource_desc.Alignment = D3D12_DEFAULT_MSAA_RESOURCE_PLACEMENT_ALIGNMENT;
    resource_desc.SampleDesc.Count = 4;

    hr = ID3D12Device_CreateCommittedResource(context.device, &heap_properties,
            D3D12_HEAP_FLAG_NONE, &resource_desc, D3D12_RESOURCE_STATE_RENDER_TARGET,
            NULL, &IID_ID3D12Resource, (void**)&rt_ms);
    ok(hr == S_OK, "Failed to create render target, hr %#x.\n");

    resource_desc.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
    resource_desc.DepthOrArraySize = 1;
    resource_desc.SampleDesc.Count = 1;
    resource_desc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    resource_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

    hr = ID3D12Device_CreateCommittedResource(context.device, &heap_properties,
            D3D12_HEAP_FLAG_NONE, &resource_desc, D3D12_RESOURCE_STATE_DEPTH_WRITE,
            NULL, &IID_ID3D12Resource, (void**)&ds);
    ok(hr == S_OK, "Failed to create render target, hr %#x.\n");

    rtv_heap = create_cpu_descriptor_heap(context.device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 8);
    dsv_heap = create_cpu_descriptor_heap(context.device, D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 1);

    viewport.TopLeftX = 0.0f;
    viewport.TopLeftY = 0.0f;
    viewport.Width = 4.0f;
    viewport.Height = 4.0f;
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;

    scissor.left = 0;
    scissor.top = 0;
    scissor.right = 4;
    scissor.bottom = 4;

    memset(&rs_param, 0, sizeof(rs_param));
    rs_param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    rs_param.Constants.Num32BitValues = sizeof(shader_args) / sizeof(uint32_t);
    rs_param.Constants.ShaderRegister = 0;
    rs_param.Constants.RegisterSpace = 0;
    rs_param.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    memset(&rs_desc, 0, sizeof(rs_desc));
    rs_desc.NumParameters = 1;
    rs_desc.pParameters = &rs_param;
    create_root_signature(context.device, &rs_desc, &root_signature);

    init_pipeline_state_desc(&pso_desc, root_signature, 0, NULL, &ps, NULL);
    pso_desc.NumRenderTargets = 3;
    pso_desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    pso_desc.RTVFormats[1] = DXGI_FORMAT_R8G8B8A8_UNORM;
    pso_desc.RTVFormats[2] = DXGI_FORMAT_R8G8B8A8_UNORM;
    pso_desc.DSVFormat = DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
    pso_desc.BlendState.RenderTarget[0].RenderTargetWriteMask = 0xf;
    pso_desc.BlendState.RenderTarget[1].RenderTargetWriteMask = 0xf;
    pso_desc.BlendState.RenderTarget[2].RenderTargetWriteMask = 0xf;
    pso_desc.DepthStencilState.DepthEnable = true;
    pso_desc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    pso_desc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    pso_desc.DepthStencilState.StencilEnable = TRUE;
    pso_desc.DepthStencilState.StencilWriteMask = 0xff;
    pso_desc.DepthStencilState.FrontFace.StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    pso_desc.DepthStencilState.FrontFace.StencilDepthFailOp = D3D12_STENCIL_OP_REPLACE;
    pso_desc.DepthStencilState.FrontFace.StencilFailOp = D3D12_STENCIL_OP_REPLACE;
    pso_desc.DepthStencilState.FrontFace.StencilPassOp = D3D12_STENCIL_OP_REPLACE;
    pso_desc.DepthStencilState.BackFace = pso_desc.DepthStencilState.FrontFace;
    hr = ID3D12Device_CreateGraphicsPipelineState(context.device, &pso_desc,
            &IID_ID3D12PipelineState, (void **)&pso);
    ok(hr == S_OK, "Failed to create state, hr %#x.\n", hr);

    pso_desc.SampleDesc.Count = 4;
    pso_desc.SampleMask = 0xf;
    hr = ID3D12Device_CreateGraphicsPipelineState(context.device, &pso_desc,
            &IID_ID3D12PipelineState, (void **)&pso_ms);
    ok(hr == S_OK, "Failed to create state, hr %#x.\n", hr);

    memset(&rtv_desc, 0, sizeof(rtv_desc));
    rtv_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;

    for (i = 0; i < 3; i++)
    {
        rtv_desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DARRAY;
        rtv_desc.Texture2DArray.MipSlice = 0;
        rtv_desc.Texture2DArray.FirstArraySlice = i;
        rtv_desc.Texture2DArray.ArraySize = 1;
        ID3D12Device_CreateRenderTargetView(context.device, rt, &rtv_desc,
                get_cpu_rtv_handle(&context, rtv_heap, i));

        rtv_desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DMSARRAY;
        rtv_desc.Texture2DMSArray.FirstArraySlice = i;
        rtv_desc.Texture2DMSArray.ArraySize = 1;
        ID3D12Device_CreateRenderTargetView(context.device, rt_ms, &rtv_desc,
                get_cpu_rtv_handle(&context, rtv_heap, i + 4));
    }

    rtv_desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DARRAY;
    rtv_desc.Texture2DArray.MipSlice = 0;
    rtv_desc.Texture2DArray.FirstArraySlice = 0;
    rtv_desc.Texture2DArray.ArraySize = 3;
    ID3D12Device_CreateRenderTargetView(context.device, rt, &rtv_desc,
            get_cpu_rtv_handle(&context, rtv_heap, 3));

    rtv_desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DMSARRAY;
    rtv_desc.Texture2DMSArray.FirstArraySlice = 0;
    rtv_desc.Texture2DMSArray.ArraySize = 3;
    ID3D12Device_CreateRenderTargetView(context.device, rt_ms, &rtv_desc,
            get_cpu_rtv_handle(&context, rtv_heap, 7));

    memset(&dsv_desc, 0, sizeof(dsv_desc));
    dsv_desc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    dsv_desc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    dsv_desc.Texture2D.MipSlice = 0;
    ID3D12Device_CreateDepthStencilView(context.device, ds, &dsv_desc,
            get_cpu_dsv_handle(&context, dsv_heap, 0));

    viewport.TopLeftX = 0.0f;
    viewport.TopLeftY = 0.0f;
    viewport.Width = 4.0f;
    viewport.Height = 4.0f;
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;

    scissor.left = 0;
    scissor.top = 0;
    scissor.right = 4;
    scissor.bottom = 4;

    /* Initialize all render target subresources */
    ID3D12GraphicsCommandList_ClearRenderTargetView(context.list, get_cpu_rtv_handle(&context, rtv_heap, 3), white, 0, NULL);
    ID3D12GraphicsCommandList_ClearRenderTargetView(context.list, get_cpu_rtv_handle(&context, rtv_heap, 7), white, 0, NULL);
    ID3D12GraphicsCommandList_ClearDepthStencilView(context.list, get_cpu_dsv_handle(&context, dsv_heap, 0),
            D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0xff, 0, NULL);

    /* Test basic clear and preserve load ops */
    memset(rtv_infos, 0, sizeof(rtv_infos));

    for (i = 0; i < ARRAY_SIZE(rtv_infos); i++)
    {
        rtv_infos[i].cpuDescriptor = get_cpu_rtv_handle(&context, rtv_heap, i);
        rtv_infos[i].BeginningAccess.Type = D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_PRESERVE;
        rtv_infos[i].BeginningAccess.Clear.ClearValue.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        rtv_infos[i].EndingAccess.Type = D3D12_RENDER_PASS_ENDING_ACCESS_TYPE_PRESERVE;
    }

    rtv_infos[0].BeginningAccess.Type = D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_CLEAR;
    memcpy(rtv_infos[0].BeginningAccess.Clear.ClearValue.Color, green, sizeof(green));

    rtv_infos[2].BeginningAccess.Type = D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_CLEAR;
    memcpy(rtv_infos[2].BeginningAccess.Clear.ClearValue.Color, red, sizeof(red));

    memset(&dsv_info, 0, sizeof(dsv_info));
    dsv_info.cpuDescriptor = get_cpu_dsv_handle(&context, dsv_heap, 0);
    dsv_info.DepthBeginningAccess.Type = D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_CLEAR;
    dsv_info.DepthBeginningAccess.Clear.ClearValue.Format = DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
    dsv_info.DepthBeginningAccess.Clear.ClearValue.DepthStencil.Depth = 0.5f;
    dsv_info.StencilBeginningAccess.Type = D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_CLEAR;
    dsv_info.StencilBeginningAccess.Clear.ClearValue.Format = DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
    dsv_info.StencilBeginningAccess.Clear.ClearValue.DepthStencil.Stencil = 0x55;
    dsv_info.DepthEndingAccess.Type = D3D12_RENDER_PASS_ENDING_ACCESS_TYPE_PRESERVE;
    dsv_info.StencilEndingAccess.Type = D3D12_RENDER_PASS_ENDING_ACCESS_TYPE_PRESERVE;

    ID3D12GraphicsCommandList4_BeginRenderPass(command_list4, ARRAY_SIZE(rtv_infos), rtv_infos, &dsv_info, 0);
    ID3D12GraphicsCommandList4_EndRenderPass(command_list4);

    transition_resource_state(context.list, rt, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
    transition_resource_state(context.list, ds, D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_COPY_SOURCE);

    check_sub_resource_uint(rt, 0, context.queue, context.list, 0xff00ff00u, 0);
    reset_command_list(context.list, context.allocator);
    check_sub_resource_uint(rt, 1, context.queue, context.list, 0xffffffffu, 0);
    reset_command_list(context.list, context.allocator);
    check_sub_resource_uint(rt, 2, context.queue, context.list, 0xff0000ffu, 0);
    reset_command_list(context.list, context.allocator);
    check_sub_resource_float(ds, 0, context.queue, context.list, 0.5f, 0.0f);
    reset_command_list(context.list, context.allocator);
    check_sub_resource_uint8(ds, 1, context.queue, context.list, 0x55, 0);
    reset_command_list(context.list, context.allocator);

    /* Test rendering with a render pass */
    transition_resource_state(context.list, rt, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
    transition_resource_state(context.list, ds, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE);

    for (i = 0; i < ARRAY_SIZE(rtv_infos); i++)
    {
        rtv_infos[i].BeginningAccess.Type = D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_CLEAR;
        memcpy(&rtv_infos[i].BeginningAccess.Clear.ClearValue.Color, &black, sizeof(black));
    }

    dsv_info.DepthBeginningAccess.Type = D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_CLEAR;
    dsv_info.DepthBeginningAccess.Clear.ClearValue.DepthStencil.Depth = 0.0f;
    dsv_info.StencilBeginningAccess.Type = D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_CLEAR;
    dsv_info.StencilBeginningAccess.Clear.ClearValue.DepthStencil.Stencil = 0x00;

    shader_args.depth = 1.0f;
    shader_args.colors[0] = 0xff0000ff;
    shader_args.colors[1] = 0xff00ff00;
    shader_args.colors[2] = 0xffff0000;
    shader_args.sample_mask = ~0u;

    ID3D12GraphicsCommandList4_BeginRenderPass(command_list4, ARRAY_SIZE(rtv_infos), rtv_infos, &dsv_info, 0);
    ID3D12GraphicsCommandList4_IASetPrimitiveTopology(command_list4, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D12GraphicsCommandList4_RSSetViewports(command_list4, 1, &viewport);
    ID3D12GraphicsCommandList4_RSSetScissorRects(command_list4, 1, &scissor);
    ID3D12GraphicsCommandList4_OMSetStencilRef(command_list4, 0xff);
    ID3D12GraphicsCommandList4_SetGraphicsRootSignature(command_list4, root_signature);
    ID3D12GraphicsCommandList4_SetPipelineState(command_list4, pso);
    ID3D12GraphicsCommandList4_SetGraphicsRoot32BitConstants(command_list4,
            0, sizeof(shader_args) / sizeof(uint32_t), &shader_args, 0);
    ID3D12GraphicsCommandList4_DrawInstanced(command_list4, 3, 1, 0, 0);
    ID3D12GraphicsCommandList4_EndRenderPass(command_list4);

    /* Ensure that render targets are unbound after the render pass */
    shader_args.depth = 0.5f;
    shader_args.colors[0] = 0xdeadbeef;
    shader_args.colors[1] = 0xdeadbeef;
    shader_args.colors[2] = 0xdeadbeef;

    ID3D12GraphicsCommandList4_SetGraphicsRoot32BitConstants(command_list4,
            0, sizeof(shader_args) / sizeof(uint32_t), &shader_args, 0);
    ID3D12GraphicsCommandList4_DrawInstanced(command_list4, 3, 1, 0, 0);

    transition_resource_state(context.list, rt, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
    transition_resource_state(context.list, ds, D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_COPY_SOURCE);

    check_sub_resource_uint(rt, 0, context.queue, context.list, 0xff0000ff, 0);
    reset_command_list(context.list, context.allocator);
    check_sub_resource_uint(rt, 1, context.queue, context.list, 0xff00ff00, 0);
    reset_command_list(context.list, context.allocator);
    check_sub_resource_uint(rt, 2, context.queue, context.list, 0xffff0000, 0);
    reset_command_list(context.list, context.allocator);
    check_sub_resource_float(ds, 0, context.queue, context.list, 1.0f, 0.0f);
    reset_command_list(context.list, context.allocator);
    check_sub_resource_uint8(ds, 1, context.queue, context.list, 0xff, 0);
    reset_command_list(context.list, context.allocator);

    /* Test clearing depth and stencil aspects indiviually */
    dsv_info.DepthBeginningAccess.Type = D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_CLEAR;
    dsv_info.DepthBeginningAccess.Clear.ClearValue.DepthStencil.Depth = 0.5f;
    dsv_info.StencilBeginningAccess.Type = D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_PRESERVE;
    dsv_info.StencilBeginningAccess.Clear.ClearValue.DepthStencil.Stencil = 0x00;

    transition_resource_state(context.list, ds, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE);

    ID3D12GraphicsCommandList4_BeginRenderPass(command_list4, ARRAY_SIZE(rtv_infos), rtv_infos, &dsv_info, 0);
    ID3D12GraphicsCommandList4_EndRenderPass(command_list4);

    transition_resource_state(context.list, ds, D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_COPY_SOURCE);

    check_sub_resource_float(ds, 0, context.queue, context.list, 0.5f, 0.0f);
    reset_command_list(context.list, context.allocator);
    check_sub_resource_uint8(ds, 1, context.queue, context.list, 0xff, 0);
    reset_command_list(context.list, context.allocator);

    dsv_info.DepthBeginningAccess.Type = D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_PRESERVE;
    dsv_info.DepthBeginningAccess.Clear.ClearValue.DepthStencil.Depth = 1.0f;
    dsv_info.StencilBeginningAccess.Type = D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_CLEAR;
    dsv_info.StencilBeginningAccess.Clear.ClearValue.DepthStencil.Stencil = 0x40;

    transition_resource_state(context.list, ds, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE);

    ID3D12GraphicsCommandList4_BeginRenderPass(command_list4, ARRAY_SIZE(rtv_infos), rtv_infos, &dsv_info, 0);
    ID3D12GraphicsCommandList4_EndRenderPass(command_list4);

    transition_resource_state(context.list, ds, D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_COPY_SOURCE);

    check_sub_resource_float(ds, 0, context.queue, context.list, 0.5f, 0.0f);
    reset_command_list(context.list, context.allocator);
    check_sub_resource_uint8(ds, 1, context.queue, context.list, 0x40, 0);
    reset_command_list(context.list, context.allocator);

    /* Test clear behaviour with SUSPEND/RESUME flags */
    transition_resource_state(context.list, rt, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);

    rtv_infos[0].BeginningAccess.Type = D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_CLEAR;
    memcpy(&rtv_infos[0].BeginningAccess.Clear.ClearValue.Color, &blue, sizeof(green));

    shader_args.colors[0] = 0xff00ff00u;

    ID3D12GraphicsCommandList4_BeginRenderPass(command_list4, 1, rtv_infos, NULL, D3D12_RENDER_PASS_FLAG_SUSPENDING_PASS);
    ID3D12GraphicsCommandList4_IASetPrimitiveTopology(command_list4, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D12GraphicsCommandList4_RSSetViewports(command_list4, 1, &viewport);
    ID3D12GraphicsCommandList4_RSSetScissorRects(command_list4, 1, &scissor);
    ID3D12GraphicsCommandList4_OMSetStencilRef(command_list4, 0xff);
    ID3D12GraphicsCommandList4_SetGraphicsRootSignature(command_list4, root_signature);
    ID3D12GraphicsCommandList4_SetPipelineState(command_list4, pso);
    ID3D12GraphicsCommandList4_SetGraphicsRoot32BitConstants(command_list4,
            0, sizeof(shader_args) / sizeof(uint32_t), &shader_args, 0);
    ID3D12GraphicsCommandList4_DrawInstanced(command_list4, 3, 1, 0, 0);
    ID3D12GraphicsCommandList4_EndRenderPass(command_list4);

    ID3D12GraphicsCommandList4_BeginRenderPass(command_list4, 1, rtv_infos, NULL, D3D12_RENDER_PASS_FLAG_RESUMING_PASS);
    ID3D12GraphicsCommandList4_EndRenderPass(command_list4);

    transition_resource_state(context.list, rt, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);

    check_sub_resource_uint(rt, 0, context.queue, context.list, 0xff00ff00, 0);
    reset_command_list(context.list, context.allocator);

    /* Test bind-to-rasterizer behaviour of different beginning access types */
    transition_resource_state(context.list, rt, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);

    rtv_infos[0].BeginningAccess.Type = D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_CLEAR;
    rtv_infos[0].EndingAccess.Type = D3D12_RENDER_PASS_ENDING_ACCESS_TYPE_PRESERVE;
    memcpy(rtv_infos[0].BeginningAccess.Clear.ClearValue.Color, white, sizeof(white));
    rtv_infos[1].BeginningAccess.Type = D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_CLEAR;
    rtv_infos[1].EndingAccess.Type = D3D12_RENDER_PASS_ENDING_ACCESS_TYPE_PRESERVE_LOCAL_RENDER;
    memcpy(rtv_infos[1].BeginningAccess.Clear.ClearValue.Color, white, sizeof(white));
    rtv_infos[2].BeginningAccess.Type = D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_CLEAR;
    rtv_infos[2].EndingAccess.Type = D3D12_RENDER_PASS_ENDING_ACCESS_TYPE_PRESERVE_LOCAL_SRV;
    memcpy(rtv_infos[2].BeginningAccess.Clear.ClearValue.Color, white, sizeof(white));

    ID3D12GraphicsCommandList4_BeginRenderPass(command_list4, ARRAY_SIZE(rtv_infos), rtv_infos, NULL, 0);
    ID3D12GraphicsCommandList4_EndRenderPass(command_list4);

    transition_sub_resource_state(context.list, rt, 2, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

    ID3D12GraphicsCommandList4_IASetPrimitiveTopology(command_list4, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D12GraphicsCommandList4_RSSetViewports(command_list4, 1, &viewport);
    ID3D12GraphicsCommandList4_RSSetScissorRects(command_list4, 1, &scissor);
    ID3D12GraphicsCommandList4_OMSetStencilRef(command_list4, 0xff);
    ID3D12GraphicsCommandList4_SetGraphicsRootSignature(command_list4, root_signature);
    ID3D12GraphicsCommandList4_SetPipelineState(command_list4, pso);

    rtv_infos[0].BeginningAccess.Type = D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_NO_ACCESS;
    rtv_infos[0].EndingAccess.Type = D3D12_RENDER_PASS_ENDING_ACCESS_TYPE_NO_ACCESS;
    rtv_infos[1].BeginningAccess.Type = D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_PRESERVE_LOCAL_RENDER;
    rtv_infos[1].EndingAccess.Type = D3D12_RENDER_PASS_ENDING_ACCESS_TYPE_PRESERVE_LOCAL_SRV;
    rtv_infos[2].BeginningAccess.Type = D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_PRESERVE_LOCAL_SRV;
    rtv_infos[2].EndingAccess.Type = D3D12_RENDER_PASS_ENDING_ACCESS_TYPE_DISCARD;

    shader_args.colors[0] = 0xff00ff00u;
    shader_args.colors[1] = 0xdeadbeefu;
    shader_args.colors[2] = 0xdeadbeefu;

    ID3D12GraphicsCommandList4_BeginRenderPass(command_list4, ARRAY_SIZE(rtv_infos), rtv_infos, NULL, 0);
    ID3D12GraphicsCommandList4_SetGraphicsRoot32BitConstants(command_list4,
            0, sizeof(shader_args) / sizeof(uint32_t), &shader_args, 0);
    ID3D12GraphicsCommandList4_DrawInstanced(command_list4, 3, 1, 0, 0);
    ID3D12GraphicsCommandList4_EndRenderPass(command_list4);

    transition_sub_resource_state(context.list, rt, 1, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    transition_sub_resource_state(context.list, rt, 2, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);

    rtv_infos[0].BeginningAccess.Type = D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_PRESERVE;
    rtv_infos[0].EndingAccess.Type = D3D12_RENDER_PASS_ENDING_ACCESS_TYPE_PRESERVE;
    rtv_infos[1].BeginningAccess.Type = D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_PRESERVE_LOCAL_SRV;
    rtv_infos[1].EndingAccess.Type = D3D12_RENDER_PASS_ENDING_ACCESS_TYPE_PRESERVE;
    rtv_infos[2].BeginningAccess.Type = D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_DISCARD;
    rtv_infos[2].EndingAccess.Type = D3D12_RENDER_PASS_ENDING_ACCESS_TYPE_PRESERVE;

    shader_args.colors[0] = 0xffff00ffu;
    shader_args.colors[1] = 0xffff0000u;
    shader_args.colors[2] = 0xdeadbeefu;

    ID3D12GraphicsCommandList4_BeginRenderPass(command_list4, ARRAY_SIZE(rtv_infos), rtv_infos, NULL, 0);
    ID3D12GraphicsCommandList4_SetGraphicsRoot32BitConstants(command_list4,
            0, sizeof(shader_args) / sizeof(uint32_t), &shader_args, 0);
    ID3D12GraphicsCommandList4_DrawInstanced(command_list4, 3, 1, 0, 0);
    ID3D12GraphicsCommandList4_EndRenderPass(command_list4);

    transition_sub_resource_state(context.list, rt, 0, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
    transition_sub_resource_state(context.list, rt, 1, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_SOURCE);
    transition_sub_resource_state(context.list, rt, 2, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);

    check_sub_resource_uint(rt, 0, context.queue, context.list, 0xffff00ff, 0);
    reset_command_list(context.list, context.allocator);
    check_sub_resource_uint(rt, 1, context.queue, context.list, 0xff00ff00, 0);
    reset_command_list(context.list, context.allocator);
    check_sub_resource_uint(rt, 2, context.queue, context.list, 0xffff0000, 0);
    reset_command_list(context.list, context.allocator);

    /* Test render pass resolves */
    transition_resource_state(context.list, rt, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RESOLVE_DEST);

    for (i = 0; i < ARRAY_SIZE(rtv_infos); i++)
    {
        rtv_infos[i].cpuDescriptor = get_cpu_rtv_handle(&context, rtv_heap, 4 + i);
        rtv_infos[i].BeginningAccess.Type = D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_DISCARD;
        rtv_infos[i].EndingAccess.Type = D3D12_RENDER_PASS_ENDING_ACCESS_TYPE_RESOLVE;
        rtv_infos[i].EndingAccess.Resolve.pSrcResource = rt_ms;
        rtv_infos[i].EndingAccess.Resolve.pDstResource = rt;
        rtv_infos[i].EndingAccess.Resolve.SubresourceCount = 1;
        rtv_infos[i].EndingAccess.Resolve.pSubresourceParameters = &rtv_resolves[i];
        rtv_infos[i].EndingAccess.Resolve.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        rtv_infos[i].EndingAccess.Resolve.PreserveResolveSource = TRUE;

        rtv_resolves[i].SrcSubresource = i;
        rtv_resolves[i].DstSubresource = i;
        rtv_resolves[i].DstX = 0;
        rtv_resolves[i].DstY = 0;
        rtv_resolves[i].SrcRect.left = 0;
        rtv_resolves[i].SrcRect.top = 0;
        rtv_resolves[i].SrcRect.right = 4;
        rtv_resolves[i].SrcRect.bottom = 4;
    }

    rtv_infos[0].EndingAccess.Resolve.ResolveMode = D3D12_RESOLVE_MODE_MIN;
    rtv_infos[1].EndingAccess.Resolve.ResolveMode = D3D12_RESOLVE_MODE_MAX;
    rtv_infos[2].EndingAccess.Resolve.ResolveMode = D3D12_RESOLVE_MODE_AVERAGE;

    ID3D12GraphicsCommandList4_BeginRenderPass(command_list4, ARRAY_SIZE(rtv_infos), rtv_infos, NULL, 0);
    ID3D12GraphicsCommandList4_IASetPrimitiveTopology(command_list4, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D12GraphicsCommandList4_RSSetViewports(command_list4, 1, &viewport);
    ID3D12GraphicsCommandList4_RSSetScissorRects(command_list4, 1, &scissor);
    ID3D12GraphicsCommandList4_SetGraphicsRootSignature(command_list4, root_signature);
    ID3D12GraphicsCommandList4_SetPipelineState(command_list4, pso_ms);

    for (i = 0; i < 4; i++)
    {
        shader_args.depth = (float)i / 3.0f;
        shader_args.colors[0] = (85 * i) << 0;
        shader_args.colors[1] = (85 * i) << 8;
        shader_args.colors[2] = (85 * i) << 16;
        shader_args.sample_mask = 1u << (i ^ 1);

        ID3D12GraphicsCommandList4_OMSetStencilRef(command_list4, 16 * i + 8);
        ID3D12GraphicsCommandList4_SetGraphicsRoot32BitConstants(command_list4,
                0, sizeof(shader_args) / sizeof(uint32_t), &shader_args, 0);
        ID3D12GraphicsCommandList4_DrawInstanced(command_list4, 3, 1, 0, 0);
    }

    ID3D12GraphicsCommandList4_EndRenderPass(command_list4);

    transition_resource_state(context.list, rt, D3D12_RESOURCE_STATE_RESOLVE_DEST, D3D12_RESOURCE_STATE_COPY_SOURCE);

    /* For some reason the resolved image (sometimes) seems to get partially
     * discarded on RADV */
    bug_if(is_radv_device(context.device))
    check_sub_resource_uint(rt, 0, context.queue, context.list, 0x00000000, 0);
    reset_command_list(context.list, context.allocator);
    bug_if(is_radv_device(context.device))
    check_sub_resource_uint(rt, 1, context.queue, context.list, 0x0000ff00, 0);
    reset_command_list(context.list, context.allocator);
    check_sub_resource_uint(rt, 2, context.queue, context.list, 0x00800000, 0x10000);
    reset_command_list(context.list, context.allocator);

    /* Test per-subresource resolve areas */
    transition_resource_state(context.list, rt, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
    ID3D12GraphicsCommandList_ClearRenderTargetView(context.list, get_cpu_rtv_handle(&context, rtv_heap, 3), white, 0, NULL);

    transition_resource_state(context.list, rt, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_RESOLVE_DEST);

    for (i = 0; i < 3; i++)
    {
        rtv_resolves[i].SrcSubresource = i;
        rtv_resolves[i].DstSubresource = i;
        rtv_resolves[i].DstX = i == 1 ? 1 : 0;
        rtv_resolves[i].DstY = i == 1 ? 2 : 0;
        rtv_resolves[i].SrcRect.left = i == 2 ? 2 : 0;
        rtv_resolves[i].SrcRect.top = i == 2 ? 1 : 0;
        rtv_resolves[i].SrcRect.right = rtv_resolves[i].SrcRect.left + 2;
        rtv_resolves[i].SrcRect.bottom = rtv_resolves[i].SrcRect.top + 2;
    }

    rtv_infos[0].cpuDescriptor = get_cpu_rtv_handle(&context, rtv_heap, 7);
    rtv_infos[0].BeginningAccess.Type = D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_PRESERVE;
    rtv_infos[0].EndingAccess.Type = D3D12_RENDER_PASS_ENDING_ACCESS_TYPE_RESOLVE;
    rtv_infos[0].EndingAccess.Resolve.pSrcResource = rt_ms;
    rtv_infos[0].EndingAccess.Resolve.pDstResource = rt;
    rtv_infos[0].EndingAccess.Resolve.SubresourceCount = 3;
    rtv_infos[0].EndingAccess.Resolve.pSubresourceParameters = &rtv_resolves[0];
    rtv_infos[0].EndingAccess.Resolve.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    rtv_infos[0].EndingAccess.Resolve.ResolveMode = D3D12_RESOLVE_MODE_AVERAGE;
    rtv_infos[0].EndingAccess.Resolve.PreserveResolveSource = TRUE;

    ID3D12GraphicsCommandList4_BeginRenderPass(command_list4, 1, rtv_infos, NULL, 0);
    ID3D12GraphicsCommandList4_EndRenderPass(command_list4);

    transition_resource_state(context.list, rt, D3D12_RESOURCE_STATE_RESOLVE_DEST, D3D12_RESOURCE_STATE_COPY_SOURCE);

    for (i = 0; i < 3; i++)
    {
        uint32_t multiplier = 1u << (8 * i);

        get_texture_readback_with_command_list(rt, i, &rb, context.queue, context.list);

        for (x = 0; x < 4; x++)
        {
            for (y = 0; y < 4; y++)
            {
                uint32_t got = get_readback_uint(&rb, x, y, 0);
                bool inside = x >= rtv_resolves[i].DstX && y >= rtv_resolves[i].DstY &&
                        x < rtv_resolves[i].DstX + rtv_resolves[i].SrcRect.right - rtv_resolves[i].SrcRect.left &&
                        y < rtv_resolves[i].DstY + rtv_resolves[i].SrcRect.bottom - rtv_resolves[i].SrcRect.top;

                if (inside)
                {
                    ok(got >= (0x7f * multiplier) && got <= (0x81 * multiplier),
                            "Got %#x, expected %#x at (%u,%u,%u).\n", got, 0x80 * multiplier, x, y, i);
                }
                else
                    ok(got == 0xffffffffu, "Got %#x, expected %#x at (%u,%u,%u).\n", got, 0xffffffffu, x, y, i);
            }
        }

        release_resource_readback(&rb);
        reset_command_list(context.list, context.allocator);
    }

    ID3D12DescriptorHeap_Release(rtv_heap);
    ID3D12DescriptorHeap_Release(dsv_heap);

    ID3D12Resource_Release(rt);
    ID3D12Resource_Release(ds);
    ID3D12Resource_Release(rt_ms);

    ID3D12PipelineState_Release(pso);
    ID3D12PipelineState_Release(pso_ms);
    ID3D12RootSignature_Release(root_signature);

    ID3D12GraphicsCommandList4_Release(command_list4);

    destroy_test_context(&context);
}
