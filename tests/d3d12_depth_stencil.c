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

void test_depth_clip(void)
{
    static const float white[] = {1.0f, 1.0f, 1.0f, 1.0f};
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc;
    ID3D12GraphicsCommandList *command_list;
    D3D12_INPUT_LAYOUT_DESC input_layout;
    struct depth_stencil_resource ds;
    struct test_context_desc desc;
    D3D12_VERTEX_BUFFER_VIEW vbv;
    struct test_context context;
    ID3D12CommandQueue *queue;
    ID3D12Resource *vb;
    unsigned int i;
    float depth;
    HRESULT hr;

    static const DWORD vs_code[] =
    {
#if 0
        float4 main(float4 p : POSITION) : SV_Position
        {
            return p;
        }
#endif
        0x43425844, 0x92767590, 0x06a6dba7, 0x0ae078b2, 0x7b5eb8f6, 0x00000001, 0x000000d8, 0x00000003,
        0x0000002c, 0x00000060, 0x00000094, 0x4e475349, 0x0000002c, 0x00000001, 0x00000008, 0x00000020,
        0x00000000, 0x00000000, 0x00000003, 0x00000000, 0x00000f0f, 0x49534f50, 0x4e4f4954, 0xababab00,
        0x4e47534f, 0x0000002c, 0x00000001, 0x00000008, 0x00000020, 0x00000000, 0x00000001, 0x00000003,
        0x00000000, 0x0000000f, 0x505f5653, 0x7469736f, 0x006e6f69, 0x52444853, 0x0000003c, 0x00010040,
        0x0000000f, 0x0300005f, 0x001010f2, 0x00000000, 0x04000067, 0x001020f2, 0x00000000, 0x00000001,
        0x05000036, 0x001020f2, 0x00000000, 0x00101e46, 0x00000000, 0x0100003e,
    };
    static const D3D12_SHADER_BYTECODE vs = {vs_code, sizeof(vs_code)};
    static const DWORD ps_depth_code[] =
    {
#if 0
        float depth;

        float4 main(float4 p : SV_Position, out float out_depth : SV_Depth) : SV_Target
        {
            out_depth = depth;
            return float4(0, 1, 0, 1);
        }
#endif
        0x43425844, 0x6744db20, 0x3e266cd1, 0xc50630b3, 0xd7455b94, 0x00000001, 0x00000120, 0x00000003,
        0x0000002c, 0x00000060, 0x000000b4, 0x4e475349, 0x0000002c, 0x00000001, 0x00000008, 0x00000020,
        0x00000000, 0x00000001, 0x00000003, 0x00000000, 0x0000000f, 0x505f5653, 0x7469736f, 0x006e6f69,
        0x4e47534f, 0x0000004c, 0x00000002, 0x00000008, 0x00000038, 0x00000000, 0x00000000, 0x00000003,
        0x00000000, 0x0000000f, 0x00000042, 0x00000000, 0x00000000, 0x00000003, 0xffffffff, 0x00000e01,
        0x545f5653, 0x65677261, 0x56530074, 0x7065445f, 0xab006874, 0x52444853, 0x00000064, 0x00000040,
        0x00000019, 0x04000059, 0x00208e46, 0x00000000, 0x00000001, 0x03000065, 0x001020f2, 0x00000000,
        0x02000065, 0x0000c001, 0x08000036, 0x001020f2, 0x00000000, 0x00004002, 0x00000000, 0x3f800000,
        0x00000000, 0x3f800000, 0x05000036, 0x0000c001, 0x0020800a, 0x00000000, 0x00000000, 0x0100003e,
    };
    static const D3D12_SHADER_BYTECODE ps_depth = {ps_depth_code, sizeof(ps_depth_code)};
    static const D3D12_INPUT_ELEMENT_DESC layout_desc[] =
    {
        {"position", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    };
    static const struct vec4 vertices[] =
    {
        {-1.0f, -1.0f,  0.0f, 1.0f},
        {-1.0f,  1.0f,  0.0f, 1.0f},
        { 1.0f, -1.0f,  0.0f, 1.0f},
        { 1.0f,  1.0f,  0.0f, 1.0f},

        {-1.0f, -1.0f,  0.5f, 1.0f},
        {-1.0f,  1.0f,  0.5f, 1.0f},
        { 1.0f, -1.0f,  0.5f, 1.0f},
        { 1.0f,  1.0f,  0.5f, 1.0f},

        {-1.0f, -1.0f, -0.5f, 1.0f},
        {-1.0f,  1.0f, -0.5f, 1.0f},
        { 1.0f, -1.0f, -0.5f, 1.0f},
        { 1.0f,  1.0f, -0.5f, 1.0f},

        {-1.0f, -1.0f,  1.0f, 1.0f},
        {-1.0f,  1.0f,  1.0f, 1.0f},
        { 1.0f, -1.0f,  1.0f, 1.0f},
        { 1.0f,  1.0f,  1.0f, 1.0f},

        {-1.0f, -1.0f,  1.5f, 1.0f},
        {-1.0f,  1.0f,  1.5f, 1.0f},
        { 1.0f, -1.0f,  1.5f, 1.0f},
        { 1.0f,  1.0f,  1.5f, 1.0f},
    };
    struct result
    {
        uint32_t expected_color;
        float expected_depth;
    };
    static const struct
    {
        struct result depth_clip;
        struct result no_depth_clip;
    }
    tests[] =
    {
        {{0xff00ff00, 0.0f  }, {0xff00ff00, 0.0f}},
        {{0xff00ff00, 0.5f  }, {0xff00ff00, 0.5f}},
        {{0xffffffff, 0.125f}, {0xff00ff00, 0.0f}},
        {{0xff00ff00, 1.0f  }, {0xff00ff00, 1.0f}},
        {{0xffffffff, 0.125f}, {0xff00ff00, 1.0f}},
    };

    memset(&desc, 0, sizeof(desc));
    desc.no_root_signature = true;
    if (!init_test_context(&context, &desc))
        return;
    command_list = context.list;
    queue = context.queue;

    context.root_signature = create_32bit_constants_root_signature_(__LINE__, context.device,
            0, 4, D3D12_SHADER_VISIBILITY_PIXEL, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

    init_depth_stencil(&ds, context.device, 32, 32, 1, 1, DXGI_FORMAT_D32_FLOAT, 0, NULL);

    vb = create_upload_buffer(context.device, sizeof(vertices), vertices);
    vbv.BufferLocation = ID3D12Resource_GetGPUVirtualAddress(vb);
    vbv.StrideInBytes = sizeof(*vertices);
    vbv.SizeInBytes = sizeof(vertices);

    input_layout.pInputElementDescs = layout_desc;
    input_layout.NumElements = ARRAY_SIZE(layout_desc);
    init_pipeline_state_desc(&pso_desc, context.root_signature,
            context.render_target_desc.Format, &vs, NULL, &input_layout);
    pso_desc.RasterizerState.DepthClipEnable = true;
    pso_desc.DepthStencilState.DepthEnable = true;
    pso_desc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    pso_desc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    pso_desc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    hr = ID3D12Device_CreateGraphicsPipelineState(context.device, &pso_desc,
            &IID_ID3D12PipelineState, (void **)&context.pipeline_state);
    ok(hr == S_OK, "Failed to create pipeline, hr %#x.\n", hr);

    for (i = 0; i < ARRAY_SIZE(tests); ++i)
    {
        const struct result *result = &tests[i].depth_clip;

        ID3D12GraphicsCommandList_ClearRenderTargetView(command_list, context.rtv, white, 0, NULL);
        ID3D12GraphicsCommandList_ClearDepthStencilView(command_list,
                ds.dsv_handle, D3D12_CLEAR_FLAG_DEPTH, 0.125f, 0, 0, NULL);

        ID3D12GraphicsCommandList_OMSetRenderTargets(command_list, 1, &context.rtv, false, &ds.dsv_handle);
        ID3D12GraphicsCommandList_SetGraphicsRootSignature(command_list, context.root_signature);
        ID3D12GraphicsCommandList_SetPipelineState(command_list, context.pipeline_state);
        ID3D12GraphicsCommandList_IASetPrimitiveTopology(command_list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
        ID3D12GraphicsCommandList_IASetVertexBuffers(command_list, 0, 1, &vbv);
        ID3D12GraphicsCommandList_RSSetViewports(command_list, 1, &context.viewport);
        ID3D12GraphicsCommandList_RSSetScissorRects(command_list, 1, &context.scissor_rect);
        ID3D12GraphicsCommandList_DrawInstanced(command_list, 4, 1, 4 * i, 0);

        transition_resource_state(command_list, ds.texture,
                D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_COPY_SOURCE);
        check_sub_resource_float(ds.texture, 0, queue, command_list, result->expected_depth, 2);
        reset_command_list(command_list, context.allocator);

        transition_resource_state(command_list, context.render_target,
                D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
        check_sub_resource_uint(context.render_target, 0, queue, command_list, result->expected_color, 0);

        reset_command_list(command_list, context.allocator);
        transition_resource_state(command_list, ds.texture,
                D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE);
        transition_resource_state(command_list, context.render_target,
                D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
    }

    ID3D12PipelineState_Release(context.pipeline_state);
    pso_desc.RasterizerState.DepthClipEnable = false;
    hr = ID3D12Device_CreateGraphicsPipelineState(context.device, &pso_desc,
            &IID_ID3D12PipelineState, (void **)&context.pipeline_state);
    ok(hr == S_OK, "Failed to create pipeline, hr %#x.\n", hr);

    for (i = 0; i < ARRAY_SIZE(tests); ++i)
    {
        const struct result *result = &tests[i].no_depth_clip;

        ID3D12GraphicsCommandList_ClearRenderTargetView(command_list, context.rtv, white, 0, NULL);
        ID3D12GraphicsCommandList_ClearDepthStencilView(command_list,
                ds.dsv_handle, D3D12_CLEAR_FLAG_DEPTH, 0.125f, 0, 0, NULL);

        ID3D12GraphicsCommandList_OMSetRenderTargets(command_list, 1, &context.rtv, false, &ds.dsv_handle);
        ID3D12GraphicsCommandList_SetGraphicsRootSignature(command_list, context.root_signature);
        ID3D12GraphicsCommandList_SetPipelineState(command_list, context.pipeline_state);
        ID3D12GraphicsCommandList_IASetPrimitiveTopology(command_list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
        ID3D12GraphicsCommandList_IASetVertexBuffers(command_list, 0, 1, &vbv);
        ID3D12GraphicsCommandList_RSSetViewports(command_list, 1, &context.viewport);
        ID3D12GraphicsCommandList_RSSetScissorRects(command_list, 1, &context.scissor_rect);
        ID3D12GraphicsCommandList_DrawInstanced(command_list, 4, 1, 4 * i, 0);

        transition_resource_state(command_list, ds.texture,
                D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_COPY_SOURCE);
        check_sub_resource_float(ds.texture, 0, queue, command_list, result->expected_depth, 2);
        reset_command_list(command_list, context.allocator);

        transition_resource_state(command_list, context.render_target,
                D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
        check_sub_resource_uint(context.render_target, 0, queue, command_list, result->expected_color, 0);

        reset_command_list(command_list, context.allocator);
        transition_resource_state(command_list, ds.texture,
                D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE);
        transition_resource_state(command_list, context.render_target,
                D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
    }

    ID3D12PipelineState_Release(context.pipeline_state);
    pso_desc.PS = ps_depth;
    pso_desc.RasterizerState.DepthClipEnable = true;
    hr = ID3D12Device_CreateGraphicsPipelineState(context.device, &pso_desc,
            &IID_ID3D12PipelineState, (void **)&context.pipeline_state);
    ok(hr == S_OK, "Failed to create pipeline, hr %#x.\n", hr);

    ID3D12GraphicsCommandList_ClearRenderTargetView(command_list, context.rtv, white, 0, NULL);
    ID3D12GraphicsCommandList_ClearDepthStencilView(command_list,
            ds.dsv_handle, D3D12_CLEAR_FLAG_DEPTH, 0.125f, 0, 0, NULL);

    ID3D12GraphicsCommandList_OMSetRenderTargets(command_list, 1, &context.rtv, false, &ds.dsv_handle);
    ID3D12GraphicsCommandList_SetGraphicsRootSignature(command_list, context.root_signature);
    ID3D12GraphicsCommandList_SetPipelineState(command_list, context.pipeline_state);
    ID3D12GraphicsCommandList_IASetPrimitiveTopology(command_list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    ID3D12GraphicsCommandList_IASetVertexBuffers(command_list, 0, 1, &vbv);
    ID3D12GraphicsCommandList_RSSetViewports(command_list, 1, &context.viewport);
    ID3D12GraphicsCommandList_RSSetScissorRects(command_list, 1, &context.scissor_rect);
    depth = 2.0f;
    ID3D12GraphicsCommandList_SetGraphicsRoot32BitConstants(command_list, 0, 1, &depth, 0);
    ID3D12GraphicsCommandList_DrawInstanced(command_list, 4, 1, 0, 0);

    transition_resource_state(command_list, ds.texture,
            D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_COPY_SOURCE);
    check_sub_resource_float(ds.texture, 0, queue, command_list, 1.0f, 2);
    reset_command_list(command_list, context.allocator);

    transition_resource_state(command_list, context.render_target,
            D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
    check_sub_resource_uint(context.render_target, 0, queue, command_list, 0xff00ff00, 0);

    ID3D12Resource_Release(vb);
    destroy_depth_stencil(&ds);
    destroy_test_context(&context);
}

#define check_depth_stencil_sampling(a, b, c, d, e, f, g) \
        check_depth_stencil_sampling_(__LINE__, a, b, c, d, e, f, g)
static void check_depth_stencil_sampling_(unsigned int line, struct test_context *context,
        ID3D12PipelineState *pso, ID3D12Resource *cb, ID3D12Resource *texture,
        D3D12_CPU_DESCRIPTOR_HANDLE dsv_handle, ID3D12DescriptorHeap *srv_heap,
        float expected_value)
{
    static const float black[] = {0.0f, 0.0f, 0.0f, 0.0f};
    ID3D12GraphicsCommandList *command_list;
    ID3D12CommandQueue *queue;
    HRESULT hr;

    command_list = context->list;
    queue = context->queue;

    transition_sub_resource_state(command_list, texture, 0,
            D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

    ID3D12GraphicsCommandList_ClearRenderTargetView(command_list, context->rtv, black, 0, NULL);
    ID3D12GraphicsCommandList_OMSetRenderTargets(command_list, 1, &context->rtv, false, NULL);

    ID3D12GraphicsCommandList_SetPipelineState(command_list, pso);

    ID3D12GraphicsCommandList_SetGraphicsRootSignature(command_list, context->root_signature);
    ID3D12GraphicsCommandList_SetDescriptorHeaps(command_list, 1, &srv_heap);
    ID3D12GraphicsCommandList_SetGraphicsRootDescriptorTable(command_list, 0,
            ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(srv_heap));
    ID3D12GraphicsCommandList_SetGraphicsRootConstantBufferView(command_list, 1,
            ID3D12Resource_GetGPUVirtualAddress(cb));
    ID3D12GraphicsCommandList_IASetPrimitiveTopology(command_list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D12GraphicsCommandList_RSSetViewports(command_list, 1, &context->viewport);
    ID3D12GraphicsCommandList_RSSetScissorRects(command_list, 1, &context->scissor_rect);
    ID3D12GraphicsCommandList_DrawInstanced(command_list, 3, 1, 0, 0);

    transition_sub_resource_state(command_list, context->render_target, 0,
            D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
    check_sub_resource_float_(line, context->render_target, 0, queue, command_list, expected_value, 2);

    reset_command_list(command_list, context->allocator);
    transition_sub_resource_state(command_list, context->render_target, 0,
            D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
    transition_sub_resource_state(command_list, texture, 0,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE);
    hr = ID3D12GraphicsCommandList_Close(command_list);
    ok_(line)(SUCCEEDED(hr), "Failed to close command list, hr %#x.\n", hr);
    exec_command_list(queue, command_list);
    wait_queue_idle(context->device, queue);
}

void test_depth_stencil_sampling(void)
{
    ID3D12PipelineState *pso_compare, *pso_depth, *pso_stencil, *pso_depth_stencil;
    D3D12_CPU_DESCRIPTOR_HANDLE dsv_handle, srv_cpu_handle;
    D3D12_ROOT_SIGNATURE_DESC root_signature_desc;
    D3D12_STATIC_SAMPLER_DESC sampler_desc[2];
    D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc;
    D3D12_DESCRIPTOR_RANGE descriptor_range;
    D3D12_ROOT_PARAMETER root_parameters[2];
    ID3D12GraphicsCommandList *command_list;
    struct depth_stencil_resource ds;
    ID3D12DescriptorHeap *srv_heap;
    struct test_context_desc desc;
    ID3D12Resource *cb, *texture;
    unsigned int descriptor_size;
    struct test_context context;
    struct vec4 ps_constant;
    ID3D12Device *device;
    unsigned int i;
    HRESULT hr;

    static const DWORD ps_compare_code[] =
    {
#if 0
        Texture2D t;
        SamplerComparisonState s : register(s1);

        float ref;

        float4 main(float4 position : SV_Position) : SV_Target
        {
            return t.SampleCmp(s, float2(position.x / 640.0f, position.y / 480.0f), ref);
        }
#endif
        0x43425844, 0xbea899fb, 0xcbeaa744, 0xbad6daa0, 0xd4363d30, 0x00000001, 0x00000164, 0x00000003,
        0x0000002c, 0x00000060, 0x00000094, 0x4e475349, 0x0000002c, 0x00000001, 0x00000008, 0x00000020,
        0x00000000, 0x00000001, 0x00000003, 0x00000000, 0x0000030f, 0x505f5653, 0x7469736f, 0x006e6f69,
        0x4e47534f, 0x0000002c, 0x00000001, 0x00000008, 0x00000020, 0x00000000, 0x00000000, 0x00000003,
        0x00000000, 0x0000000f, 0x545f5653, 0x65677261, 0xabab0074, 0x52444853, 0x000000c8, 0x00000040,
        0x00000032, 0x04000059, 0x00208e46, 0x00000000, 0x00000001, 0x0300085a, 0x00106000, 0x00000001,
        0x04001858, 0x00107000, 0x00000000, 0x00005555, 0x04002064, 0x00101032, 0x00000000, 0x00000001,
        0x03000065, 0x001020f2, 0x00000000, 0x02000068, 0x00000001, 0x0a000038, 0x00100032, 0x00000000,
        0x00101046, 0x00000000, 0x00004002, 0x3acccccd, 0x3b088889, 0x00000000, 0x00000000, 0x0c000046,
        0x00100012, 0x00000000, 0x00100046, 0x00000000, 0x00107006, 0x00000000, 0x00106000, 0x00000001,
        0x0020800a, 0x00000000, 0x00000000, 0x05000036, 0x001020f2, 0x00000000, 0x00100006, 0x00000000,
        0x0100003e,
    };
    static const D3D12_SHADER_BYTECODE ps_compare = {ps_compare_code, sizeof(ps_compare_code)};
    static const DWORD ps_sample_code[] =
    {
#if 0
        Texture2D t;
        SamplerState s;

        float4 main(float4 position : SV_Position) : SV_Target
        {
            return t.Sample(s, float2(position.x / 640.0f, position.y / 480.0f));
        }
#endif
        0x43425844, 0x7472c092, 0x5548f00e, 0xf4e007f1, 0x5970429c, 0x00000001, 0x00000134, 0x00000003,
        0x0000002c, 0x00000060, 0x00000094, 0x4e475349, 0x0000002c, 0x00000001, 0x00000008, 0x00000020,
        0x00000000, 0x00000001, 0x00000003, 0x00000000, 0x0000030f, 0x505f5653, 0x7469736f, 0x006e6f69,
        0x4e47534f, 0x0000002c, 0x00000001, 0x00000008, 0x00000020, 0x00000000, 0x00000000, 0x00000003,
        0x00000000, 0x0000000f, 0x545f5653, 0x65677261, 0xabab0074, 0x52444853, 0x00000098, 0x00000040,
        0x00000026, 0x0300005a, 0x00106000, 0x00000000, 0x04001858, 0x00107000, 0x00000000, 0x00005555,
        0x04002064, 0x00101032, 0x00000000, 0x00000001, 0x03000065, 0x001020f2, 0x00000000, 0x02000068,
        0x00000001, 0x0a000038, 0x00100032, 0x00000000, 0x00101046, 0x00000000, 0x00004002, 0x3acccccd,
        0x3b088889, 0x00000000, 0x00000000, 0x09000045, 0x001020f2, 0x00000000, 0x00100046, 0x00000000,
        0x00107e46, 0x00000000, 0x00106000, 0x00000000, 0x0100003e,
    };
    static const D3D12_SHADER_BYTECODE ps_sample = {ps_sample_code, sizeof(ps_sample_code)};
    static const DWORD ps_stencil_code[] =
    {
#if 0
        Texture2D<uint4> t : register(t1);

        float4 main(float4 position : SV_Position) : SV_Target
        {
            float2 s;
            t.GetDimensions(s.x, s.y);
            return t.Load(int3(float3(s.x * position.x / 640.0f, s.y * position.y / 480.0f, 0))).y;
        }
#endif
        0x43425844, 0x78574912, 0x1b7763f5, 0x0124de83, 0x39954d6c, 0x00000001, 0x000001a0, 0x00000003,
        0x0000002c, 0x00000060, 0x00000094, 0x4e475349, 0x0000002c, 0x00000001, 0x00000008, 0x00000020,
        0x00000000, 0x00000001, 0x00000003, 0x00000000, 0x0000030f, 0x505f5653, 0x7469736f, 0x006e6f69,
        0x4e47534f, 0x0000002c, 0x00000001, 0x00000008, 0x00000020, 0x00000000, 0x00000000, 0x00000003,
        0x00000000, 0x0000000f, 0x545f5653, 0x65677261, 0xabab0074, 0x52444853, 0x00000104, 0x00000040,
        0x00000041, 0x04001858, 0x00107000, 0x00000001, 0x00004444, 0x04002064, 0x00101032, 0x00000000,
        0x00000001, 0x03000065, 0x001020f2, 0x00000000, 0x02000068, 0x00000001, 0x0700003d, 0x001000f2,
        0x00000000, 0x00004001, 0x00000000, 0x00107e46, 0x00000001, 0x07000038, 0x00100032, 0x00000000,
        0x00100046, 0x00000000, 0x00101046, 0x00000000, 0x0a000038, 0x00100032, 0x00000000, 0x00100046,
        0x00000000, 0x00004002, 0x3acccccd, 0x3b088889, 0x00000000, 0x00000000, 0x0500001b, 0x00100032,
        0x00000000, 0x00100046, 0x00000000, 0x08000036, 0x001000c2, 0x00000000, 0x00004002, 0x00000000,
        0x00000000, 0x00000000, 0x00000000, 0x0700002d, 0x001000f2, 0x00000000, 0x00100e46, 0x00000000,
        0x00107e46, 0x00000001, 0x05000056, 0x001020f2, 0x00000000, 0x00100556, 0x00000000, 0x0100003e,
    };
    static const D3D12_SHADER_BYTECODE ps_stencil = {ps_stencil_code, sizeof(ps_stencil_code)};
    static const DWORD ps_depth_stencil_code[] =
    {
#if 0
        SamplerState samp;
        Texture2D depth_tex;
        Texture2D<uint4> stencil_tex;

        float main(float4 position: SV_Position) : SV_Target
        {
            float2 s, p;
            float depth, stencil;
            depth_tex.GetDimensions(s.x, s.y);
            p = float2(s.x * position.x / 640.0f, s.y * position.y / 480.0f);
            depth = depth_tex.Sample(samp, p).r;
            stencil = stencil_tex.Load(int3(float3(p.x, p.y, 0))).y;
            return depth + stencil;
        }
#endif
        0x43425844, 0x348f8377, 0x977d1ee0, 0x8cca4f35, 0xff5c5afc, 0x00000001, 0x000001fc, 0x00000003,
        0x0000002c, 0x00000060, 0x00000094, 0x4e475349, 0x0000002c, 0x00000001, 0x00000008, 0x00000020,
        0x00000000, 0x00000001, 0x00000003, 0x00000000, 0x0000030f, 0x505f5653, 0x7469736f, 0x006e6f69,
        0x4e47534f, 0x0000002c, 0x00000001, 0x00000008, 0x00000020, 0x00000000, 0x00000000, 0x00000003,
        0x00000000, 0x00000e01, 0x545f5653, 0x65677261, 0xabab0074, 0x52444853, 0x00000160, 0x00000040,
        0x00000058, 0x0300005a, 0x00106000, 0x00000000, 0x04001858, 0x00107000, 0x00000000, 0x00005555,
        0x04001858, 0x00107000, 0x00000001, 0x00004444, 0x04002064, 0x00101032, 0x00000000, 0x00000001,
        0x03000065, 0x00102012, 0x00000000, 0x02000068, 0x00000002, 0x0700003d, 0x001000f2, 0x00000000,
        0x00004001, 0x00000000, 0x00107e46, 0x00000000, 0x07000038, 0x00100032, 0x00000000, 0x00100046,
        0x00000000, 0x00101046, 0x00000000, 0x0a000038, 0x00100032, 0x00000000, 0x00100046, 0x00000000,
        0x00004002, 0x3acccccd, 0x3b088889, 0x00000000, 0x00000000, 0x0500001b, 0x00100032, 0x00000001,
        0x00100046, 0x00000000, 0x09000045, 0x001000f2, 0x00000000, 0x00100046, 0x00000000, 0x00107e46,
        0x00000000, 0x00106000, 0x00000000, 0x08000036, 0x001000c2, 0x00000001, 0x00004002, 0x00000000,
        0x00000000, 0x00000000, 0x00000000, 0x0700002d, 0x001000f2, 0x00000001, 0x00100e46, 0x00000001,
        0x00107e46, 0x00000001, 0x05000056, 0x00100022, 0x00000000, 0x0010001a, 0x00000001, 0x07000000,
        0x00102012, 0x00000000, 0x0010001a, 0x00000000, 0x0010000a, 0x00000000, 0x0100003e,
    };
    static const D3D12_SHADER_BYTECODE ps_depth_stencil = {ps_depth_stencil_code, sizeof(ps_depth_stencil_code)};
    static const struct test
    {
        DXGI_FORMAT typeless_format;
        DXGI_FORMAT dsv_format;
        DXGI_FORMAT depth_view_format;
        DXGI_FORMAT stencil_view_format;
    }
    tests[] =
    {
        {DXGI_FORMAT_R32G8X24_TYPELESS, DXGI_FORMAT_D32_FLOAT_S8X24_UINT,
                DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS, DXGI_FORMAT_X32_TYPELESS_G8X24_UINT},
        {DXGI_FORMAT_R32_TYPELESS, DXGI_FORMAT_D32_FLOAT,
                DXGI_FORMAT_R32_FLOAT},
        {DXGI_FORMAT_R24G8_TYPELESS, DXGI_FORMAT_D24_UNORM_S8_UINT,
                DXGI_FORMAT_R24_UNORM_X8_TYPELESS, DXGI_FORMAT_X24_TYPELESS_G8_UINT},
        {DXGI_FORMAT_R16_TYPELESS, DXGI_FORMAT_D16_UNORM,
                DXGI_FORMAT_R16_UNORM},
    };

    memset(&desc, 0, sizeof(desc));
    desc.rt_width = 640;
    desc.rt_height = 480;
    desc.rt_format = DXGI_FORMAT_R32_FLOAT;
    desc.no_root_signature = true;
    if (!init_test_context(&context, &desc))
        return;
    device = context.device;
    command_list = context.list;

    sampler_desc[0].Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
    sampler_desc[0].AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler_desc[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler_desc[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler_desc[0].MipLODBias = 0.0f;
    sampler_desc[0].MaxAnisotropy = 0;
    sampler_desc[0].ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    sampler_desc[0].BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
    sampler_desc[0].MinLOD = 0.0f;
    sampler_desc[0].MaxLOD = 0.0f;
    sampler_desc[0].ShaderRegister = 0;
    sampler_desc[0].RegisterSpace = 0;
    sampler_desc[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    sampler_desc[1] = sampler_desc[0];
    sampler_desc[1].Filter = D3D12_FILTER_COMPARISON_MIN_MAG_MIP_POINT;
    sampler_desc[1].ComparisonFunc = D3D12_COMPARISON_FUNC_GREATER;
    sampler_desc[1].ShaderRegister = 1;

    descriptor_range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    descriptor_range.NumDescriptors = 2;
    descriptor_range.BaseShaderRegister = 0;
    descriptor_range.RegisterSpace = 0;
    descriptor_range.OffsetInDescriptorsFromTableStart = 0;
    root_parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    root_parameters[0].DescriptorTable.NumDescriptorRanges = 1;
    root_parameters[0].DescriptorTable.pDescriptorRanges = &descriptor_range;
    root_parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    root_parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    root_parameters[1].Descriptor.ShaderRegister = 0;
    root_parameters[1].Descriptor.RegisterSpace = 0;
    root_parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    memset(&root_signature_desc, 0, sizeof(root_signature_desc));
    root_signature_desc.NumParameters = 2;
    root_signature_desc.pParameters = root_parameters;
    root_signature_desc.NumStaticSamplers = 2;
    root_signature_desc.pStaticSamplers = sampler_desc;
    hr = create_root_signature(device, &root_signature_desc, &context.root_signature);
    ok(SUCCEEDED(hr), "Failed to create root signature, hr %#x.\n", hr);

    pso_compare = create_pipeline_state(device,
            context.root_signature, context.render_target_desc.Format, NULL, &ps_compare, NULL);
    pso_depth = create_pipeline_state(device,
            context.root_signature, context.render_target_desc.Format, NULL, &ps_sample, NULL);
    pso_stencil = create_pipeline_state(device,
            context.root_signature, context.render_target_desc.Format, NULL, &ps_stencil, NULL);
    pso_depth_stencil = create_pipeline_state(device,
            context.root_signature, context.render_target_desc.Format, NULL, &ps_depth_stencil, NULL);

    srv_heap = create_gpu_descriptor_heap(device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 2);
    descriptor_size = ID3D12Device_GetDescriptorHandleIncrementSize(device,
            D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    memset(&ps_constant, 0, sizeof(ps_constant));
    cb = create_upload_buffer(device, sizeof(ps_constant), &ps_constant);

    hr = ID3D12GraphicsCommandList_Close(command_list);
    ok(SUCCEEDED(hr), "Failed to close command list, hr %#x.\n", hr);

    for (i = 0; i < ARRAY_SIZE(tests); ++i)
    {
        vkd3d_test_set_context("Test %u", i);

        reset_command_list(command_list, context.allocator);

        init_depth_stencil(&ds, device, context.render_target_desc.Width,
                context.render_target_desc.Height, 1, 1, tests[i].typeless_format,
                tests[i].dsv_format, NULL);
        texture = ds.texture;
        dsv_handle = ds.dsv_handle;

        srv_cpu_handle = ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(srv_heap);

        memset(&srv_desc, 0, sizeof(srv_desc));
        srv_desc.Format = tests[i].depth_view_format;
        srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv_desc.Texture2D.MipLevels = 1;
        ID3D12Device_CreateShaderResourceView(device, texture, &srv_desc, srv_cpu_handle);
        srv_cpu_handle.ptr += descriptor_size;
        ID3D12Device_CreateShaderResourceView(device, NULL, &srv_desc, srv_cpu_handle);

        ps_constant.x = 0.5f;
        update_buffer_data(cb, 0, sizeof(ps_constant), &ps_constant);

        /* pso_compare */
        ID3D12GraphicsCommandList_ClearDepthStencilView(command_list, dsv_handle,
                D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, NULL);
        check_depth_stencil_sampling(&context, pso_compare, cb, texture, dsv_handle, srv_heap, 0.0f);

        reset_command_list(command_list, context.allocator);
        ID3D12GraphicsCommandList_ClearDepthStencilView(command_list, dsv_handle,
                D3D12_CLEAR_FLAG_DEPTH, 0.0f, 0, 0, NULL);
        check_depth_stencil_sampling(&context, pso_compare, cb, texture, dsv_handle, srv_heap, 1.0f);

        reset_command_list(command_list, context.allocator);
        ID3D12GraphicsCommandList_ClearDepthStencilView(command_list, dsv_handle,
                D3D12_CLEAR_FLAG_DEPTH, 0.5f, 0, 0, NULL);
        check_depth_stencil_sampling(&context, pso_compare, cb, texture, dsv_handle, srv_heap, 0.0f);

        reset_command_list(command_list, context.allocator);
        ID3D12GraphicsCommandList_ClearDepthStencilView(command_list, dsv_handle,
                D3D12_CLEAR_FLAG_DEPTH, 0.6f, 0, 0, NULL);
        check_depth_stencil_sampling(&context, pso_compare, cb, texture, dsv_handle, srv_heap, 0.0f);

        ps_constant.x = 0.7f;
        update_buffer_data(cb, 0, sizeof(ps_constant), &ps_constant);

        reset_command_list(command_list, context.allocator);
        check_depth_stencil_sampling(&context, pso_compare, cb, texture, dsv_handle, srv_heap, 1.0f);

        /* pso_depth */
        reset_command_list(command_list, context.allocator);
        ID3D12GraphicsCommandList_ClearDepthStencilView(command_list, dsv_handle,
                D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, NULL);
        check_depth_stencil_sampling(&context, pso_depth, cb, texture, dsv_handle, srv_heap, 1.0f);

        reset_command_list(command_list, context.allocator);
        ID3D12GraphicsCommandList_ClearDepthStencilView(command_list, dsv_handle,
                D3D12_CLEAR_FLAG_DEPTH, 0.2f, 0, 0, NULL);
        check_depth_stencil_sampling(&context, pso_depth, cb, texture, dsv_handle, srv_heap, 0.2f);

        if (!tests[i].stencil_view_format)
        {
            destroy_depth_stencil(&ds);
            continue;
        }
        if (is_amd_windows_device(device))
        {
            skip("Reads from depth/stencil shader resource views return stale values on some AMD drivers.\n");
            destroy_depth_stencil(&ds);
            continue;
        }

        srv_desc.Format = tests[i].stencil_view_format;
        srv_desc.Texture2D.PlaneSlice = 1;
        ID3D12Device_CreateShaderResourceView(device, texture, &srv_desc, srv_cpu_handle);

        /* pso_stencil */
        reset_command_list(command_list, context.allocator);
        ID3D12GraphicsCommandList_ClearDepthStencilView(command_list, dsv_handle,
                D3D12_CLEAR_FLAG_STENCIL, 0.0f, 0, 0, NULL);
        check_depth_stencil_sampling(&context, pso_stencil, cb, texture, dsv_handle, srv_heap, 0.0f);

        reset_command_list(command_list, context.allocator);
        ID3D12GraphicsCommandList_ClearDepthStencilView(command_list, dsv_handle,
                D3D12_CLEAR_FLAG_STENCIL, 0.0f, 100, 0, NULL);
        check_depth_stencil_sampling(&context, pso_stencil, cb, texture, dsv_handle, srv_heap, 100.0f);

        reset_command_list(command_list, context.allocator);
        ID3D12GraphicsCommandList_ClearDepthStencilView(command_list, dsv_handle,
                D3D12_CLEAR_FLAG_STENCIL, 0.0f, 255, 0, NULL);
        check_depth_stencil_sampling(&context, pso_stencil, cb, texture, dsv_handle, srv_heap, 255.0f);

        /* pso_depth_stencil */
        reset_command_list(command_list, context.allocator);
        ID3D12GraphicsCommandList_ClearDepthStencilView(command_list, dsv_handle,
                D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 0.3f, 3, 0, NULL);
        check_depth_stencil_sampling(&context, pso_depth_stencil, cb, texture, dsv_handle, srv_heap, 3.3f);

        reset_command_list(command_list, context.allocator);
        ID3D12GraphicsCommandList_ClearDepthStencilView(command_list, dsv_handle,
                D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 3, 0, NULL);
        check_depth_stencil_sampling(&context, pso_depth_stencil, cb, texture, dsv_handle, srv_heap, 4.0f);

        reset_command_list(command_list, context.allocator);
        ID3D12GraphicsCommandList_ClearDepthStencilView(command_list, dsv_handle,
                D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 0.0f, 0, 0, NULL);
        check_depth_stencil_sampling(&context, pso_depth_stencil, cb, texture, dsv_handle, srv_heap, 0.0f);

        destroy_depth_stencil(&ds);
    }
    vkd3d_test_set_context(NULL);

    ID3D12Resource_Release(cb);
    ID3D12DescriptorHeap_Release(srv_heap);
    ID3D12PipelineState_Release(pso_compare);
    ID3D12PipelineState_Release(pso_depth);
    ID3D12PipelineState_Release(pso_stencil);
    ID3D12PipelineState_Release(pso_depth_stencil);
    destroy_test_context(&context);
}

void test_depth_load(void)
{
    D3D12_ROOT_SIGNATURE_DESC root_signature_desc;
    D3D12_DESCRIPTOR_RANGE descriptor_ranges[2];
    D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc;
    D3D12_ROOT_PARAMETER root_parameters[1];
    ID3D12GraphicsCommandList *command_list;
    ID3D12PipelineState *pipeline_state;
    struct depth_stencil_resource ds;
    struct test_context_desc desc;
    struct test_context context;
    ID3D12DescriptorHeap *heap;
    ID3D12CommandQueue *queue;
    ID3D12Resource *texture;
    ID3D12Device *device;
    unsigned int i;
    HRESULT hr;

    static const DWORD cs_code[] =
    {
#if 0
        Texture2D<float> t;
        RWTexture2D<float> u;

        [numthreads(1, 1, 1)]
        void main(uint2 id : SV_GroupID)
        {
            u[id] = t[id];
        }
#endif
        0x43425844, 0x6ddce3d0, 0x24b47ad3, 0x7f6772d2, 0x6a644890, 0x00000001, 0x00000110, 0x00000003,
        0x0000002c, 0x0000003c, 0x0000004c, 0x4e475349, 0x00000008, 0x00000000, 0x00000008, 0x4e47534f,
        0x00000008, 0x00000000, 0x00000008, 0x58454853, 0x000000bc, 0x00050050, 0x0000002f, 0x0100086a,
        0x04001858, 0x00107000, 0x00000000, 0x00005555, 0x0400189c, 0x0011e000, 0x00000000, 0x00005555,
        0x0200005f, 0x00021032, 0x02000068, 0x00000001, 0x0400009b, 0x00000001, 0x00000001, 0x00000001,
        0x04000036, 0x00100032, 0x00000000, 0x00021046, 0x08000036, 0x001000c2, 0x00000000, 0x00004002,
        0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x8900002d, 0x800000c2, 0x00155543, 0x00100012,
        0x00000000, 0x00100e46, 0x00000000, 0x00107e46, 0x00000000, 0x060000a4, 0x0011e0f2, 0x00000000,
        0x00021546, 0x00100006, 0x00000000, 0x0100003e,
    };
    static const DWORD ps_code[] =
    {
#if 0
        Texture2D<float> t;

        float main(float4 position : SV_Position) : SV_Target
        {
            return t[int2(position.x, position.y)];
        }
#endif
        0x43425844, 0x0beace24, 0x5e10b05b, 0x742de364, 0xb2b65d2b, 0x00000001, 0x00000140, 0x00000003,
        0x0000002c, 0x00000060, 0x00000094, 0x4e475349, 0x0000002c, 0x00000001, 0x00000008, 0x00000020,
        0x00000000, 0x00000001, 0x00000003, 0x00000000, 0x0000030f, 0x505f5653, 0x7469736f, 0x006e6f69,
        0x4e47534f, 0x0000002c, 0x00000001, 0x00000008, 0x00000020, 0x00000000, 0x00000000, 0x00000003,
        0x00000000, 0x00000e01, 0x545f5653, 0x65677261, 0xabab0074, 0x52444853, 0x000000a4, 0x00000040,
        0x00000029, 0x04001858, 0x00107000, 0x00000000, 0x00005555, 0x04002064, 0x00101032, 0x00000000,
        0x00000001, 0x03000065, 0x00102012, 0x00000000, 0x02000068, 0x00000001, 0x0500001b, 0x00100032,
        0x00000000, 0x00101046, 0x00000000, 0x08000036, 0x001000c2, 0x00000000, 0x00004002, 0x00000000,
        0x00000000, 0x00000000, 0x00000000, 0x0700002d, 0x001000f2, 0x00000000, 0x00100e46, 0x00000000,
        0x00107e46, 0x00000000, 0x05000036, 0x00102012, 0x00000000, 0x0010000a, 0x00000000, 0x0100003e,
    };
    static const D3D12_SHADER_BYTECODE ps = {ps_code, sizeof(ps_code)};
    static const float white[] = {1.0f, 1.0f, 1.0f, 1.0f};
    static const float tests[] = {0.00f, 0.25f, 0.75f, 1.00f};

    memset(&desc, 0, sizeof(desc));
    desc.rt_format = DXGI_FORMAT_R32_FLOAT;
    desc.no_root_signature = true;
    if (!init_test_context(&context, &desc))
        return;
    device = context.device;
    command_list = context.list;
    queue = context.queue;

    descriptor_ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    descriptor_ranges[0].NumDescriptors = 1;
    descriptor_ranges[0].BaseShaderRegister = 0;
    descriptor_ranges[0].RegisterSpace = 0;
    descriptor_ranges[0].OffsetInDescriptorsFromTableStart = 0;
    descriptor_ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    descriptor_ranges[1].NumDescriptors = 1;
    descriptor_ranges[1].BaseShaderRegister = 0;
    descriptor_ranges[1].RegisterSpace = 0;
    descriptor_ranges[1].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
    root_parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    root_parameters[0].DescriptorTable.NumDescriptorRanges = 2;
    root_parameters[0].DescriptorTable.pDescriptorRanges = descriptor_ranges;
    root_parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    root_signature_desc.NumParameters = 1;
    root_signature_desc.pParameters = root_parameters;
    root_signature_desc.NumStaticSamplers = 0;
    root_signature_desc.pStaticSamplers = NULL;
    root_signature_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
    hr = create_root_signature(device, &root_signature_desc, &context.root_signature);
    ok(SUCCEEDED(hr), "Failed to create root signature, hr %#x.\n", hr);

    pipeline_state = create_compute_pipeline_state(device, context.root_signature,
            shader_bytecode(cs_code, sizeof(cs_code)));
    context.pipeline_state = create_pipeline_state(context.device,
            context.root_signature, context.render_target_desc.Format, NULL, &ps, NULL);

    heap = create_gpu_descriptor_heap(device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 2);

    init_depth_stencil(&ds, device, context.render_target_desc.Width,
            context.render_target_desc.Height, 1, 1, DXGI_FORMAT_R32_TYPELESS,
            DXGI_FORMAT_D32_FLOAT, NULL);
    memset(&srv_desc, 0, sizeof(srv_desc));
    srv_desc.Format = DXGI_FORMAT_R32_FLOAT;
    srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv_desc.Texture2D.MipLevels = 1;
    ID3D12Device_CreateShaderResourceView(device, ds.texture, &srv_desc,
            get_cpu_descriptor_handle(&context, heap, 0));

    texture = create_default_texture(device, 32, 32, DXGI_FORMAT_R16_UNORM,
            D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    ID3D12Device_CreateUnorderedAccessView(device, texture, NULL, NULL,
            get_cpu_descriptor_handle(&context, heap, 1));

    for (i = 0; i < ARRAY_SIZE(tests); ++i)
    {
        vkd3d_test_set_context("Test %u", i);

        ID3D12GraphicsCommandList_ClearDepthStencilView(command_list, ds.dsv_handle,
                D3D12_CLEAR_FLAG_DEPTH, tests[i], 0, 0, NULL);
        transition_sub_resource_state(command_list, ds.texture, 0,
                D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

        ID3D12GraphicsCommandList_ClearRenderTargetView(command_list, context.rtv, white, 0, NULL);
        ID3D12GraphicsCommandList_OMSetRenderTargets(command_list, 1, &context.rtv, false, NULL);
        ID3D12GraphicsCommandList_RSSetViewports(command_list, 1, &context.viewport);
        ID3D12GraphicsCommandList_RSSetScissorRects(command_list, 1, &context.scissor_rect);

        ID3D12GraphicsCommandList_SetDescriptorHeaps(command_list, 1, &heap);

        ID3D12GraphicsCommandList_SetPipelineState(command_list, context.pipeline_state);
        ID3D12GraphicsCommandList_SetGraphicsRootSignature(command_list, context.root_signature);
        ID3D12GraphicsCommandList_SetGraphicsRootDescriptorTable(command_list, 0,
                ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(heap));
        ID3D12GraphicsCommandList_IASetPrimitiveTopology(command_list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ID3D12GraphicsCommandList_DrawInstanced(command_list, 3, 1, 0, 0);

        ID3D12GraphicsCommandList_SetPipelineState(command_list, pipeline_state);
        ID3D12GraphicsCommandList_SetComputeRootSignature(command_list, context.root_signature);
        ID3D12GraphicsCommandList_SetComputeRootDescriptorTable(command_list, 0,
                ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(heap));
        ID3D12GraphicsCommandList_Dispatch(command_list, 32, 32, 1);

        transition_sub_resource_state(command_list, context.render_target, 0,
                D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
        check_sub_resource_float(context.render_target, 0, queue, command_list, tests[i], 2);

        reset_command_list(command_list, context.allocator);
        transition_sub_resource_state(command_list, texture, 0,
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
        check_sub_resource_uint16(texture, 0, queue, command_list, tests[i] * UINT16_MAX, 2);

        reset_command_list(command_list, context.allocator);
        transition_sub_resource_state(command_list, context.render_target, 0,
                D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
        transition_sub_resource_state(command_list, texture, 0,
                D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        transition_sub_resource_state(command_list, ds.texture, 0,
                D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE);
    }
    vkd3d_test_set_context(NULL);

    destroy_depth_stencil(&ds);
    ID3D12Resource_Release(texture);
    ID3D12DescriptorHeap_Release(heap);
    ID3D12PipelineState_Release(pipeline_state);
    destroy_test_context(&context);
}

void test_depth_read_only_view(void)
{
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc;
    ID3D12GraphicsCommandList *command_list;
    D3D12_DEPTH_STENCIL_VIEW_DESC dsv_desc;
    D3D12_CPU_DESCRIPTOR_HANDLE dsv_handle;
    struct depth_stencil_resource ds;
    struct test_context_desc desc;
    D3D12_CLEAR_VALUE clear_value;
    struct test_context context;
    ID3D12DescriptorHeap *heap;
    ID3D12CommandQueue *queue;
    ID3D12Device *device;
    HRESULT hr;

    static const DWORD ps_code[] =
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
    static const D3D12_SHADER_BYTECODE ps = {ps_code, sizeof(ps_code)};
    static const float white[] = {1.0f, 1.0f, 1.0f, 1.0f};
    static const float green[] = {0.0f, 1.0f, 0.0f, 1.0f};
    static const float red[] = {1.0f, 0.0f, 0.0f, 1.0f};

    memset(&desc, 0, sizeof(desc));
    desc.no_root_signature = true;
    if (!init_test_context(&context, &desc))
        return;
    device = context.device;
    command_list = context.list;
    queue = context.queue;

    context.root_signature = create_32bit_constants_root_signature(device,
            0, 4, D3D12_SHADER_VISIBILITY_PIXEL);

    init_pipeline_state_desc(&pso_desc, context.root_signature,
            context.render_target_desc.Format, NULL, &ps, NULL);
    pso_desc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    pso_desc.DepthStencilState.DepthEnable = true;
    pso_desc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    pso_desc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_GREATER;
    hr = ID3D12Device_CreateGraphicsPipelineState(context.device, &pso_desc,
            &IID_ID3D12PipelineState, (void **)&context.pipeline_state);
    ok(SUCCEEDED(hr), "Failed to create graphics pipeline state, hr %#x.\n", hr);

    heap = create_cpu_descriptor_heap(device, D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 1);

    clear_value.Format = DXGI_FORMAT_D32_FLOAT;
    clear_value.DepthStencil.Depth = 0.5f;
    clear_value.DepthStencil.Stencil = 0;
    init_depth_stencil(&ds, device, context.render_target_desc.Width,
            context.render_target_desc.Height, 1, 1, DXGI_FORMAT_R32_TYPELESS,
            DXGI_FORMAT_D32_FLOAT, &clear_value);
    memset(&dsv_desc, 0, sizeof(dsv_desc));
    dsv_desc.Format = DXGI_FORMAT_D32_FLOAT;
    dsv_desc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    dsv_desc.Flags = D3D12_DSV_FLAG_READ_ONLY_DEPTH;
    dsv_handle = get_cpu_descriptor_handle(&context, heap, 0);
    ID3D12Device_CreateDepthStencilView(device, ds.texture, &dsv_desc, dsv_handle);

    ID3D12GraphicsCommandList_ClearDepthStencilView(command_list, ds.dsv_handle,
            D3D12_CLEAR_FLAG_DEPTH, 0.5f, 0, 0, NULL);
    transition_sub_resource_state(command_list, ds.texture, 0,
            D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_DEPTH_READ);

    ID3D12GraphicsCommandList_ClearRenderTargetView(command_list, context.rtv, white, 0, NULL);
    ID3D12GraphicsCommandList_OMSetRenderTargets(command_list, 1, &context.rtv, false, &dsv_handle);
    ID3D12GraphicsCommandList_RSSetScissorRects(command_list, 1, &context.scissor_rect);

    ID3D12GraphicsCommandList_SetPipelineState(command_list, context.pipeline_state);
    ID3D12GraphicsCommandList_SetGraphicsRootSignature(command_list, context.root_signature);
    ID3D12GraphicsCommandList_IASetPrimitiveTopology(command_list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    context.viewport.MinDepth = 0.6f;
    context.viewport.MaxDepth = 0.6f;
    ID3D12GraphicsCommandList_RSSetViewports(command_list, 1, &context.viewport);
    ID3D12GraphicsCommandList_SetGraphicsRoot32BitConstants(command_list, 0, 4, green, 0);
    ID3D12GraphicsCommandList_DrawInstanced(command_list, 3, 1, 0, 0);

    context.viewport.MinDepth = 0.4f;
    context.viewport.MaxDepth = 0.4f;
    ID3D12GraphicsCommandList_RSSetViewports(command_list, 1, &context.viewport);
    ID3D12GraphicsCommandList_SetGraphicsRoot32BitConstants(command_list, 0, 4, red, 0);
    ID3D12GraphicsCommandList_DrawInstanced(command_list, 3, 1, 0, 0);

    transition_sub_resource_state(command_list, context.render_target, 0,
            D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
    check_sub_resource_uint(context.render_target, 0, queue, command_list, 0xff00ff00, 0);

    reset_command_list(command_list, context.allocator);
    transition_sub_resource_state(command_list, ds.texture, 0,
            D3D12_RESOURCE_STATE_DEPTH_READ, D3D12_RESOURCE_STATE_COPY_SOURCE);
    check_sub_resource_float(ds.texture, 0, queue, command_list, 0.5f, 2);

    destroy_depth_stencil(&ds);
    ID3D12DescriptorHeap_Release(heap);
    destroy_test_context(&context);
}

void test_stencil_load(void)
{
    D3D12_ROOT_SIGNATURE_DESC root_signature_desc;
    D3D12_DESCRIPTOR_RANGE descriptor_ranges[2];
    D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc;
    D3D12_ROOT_PARAMETER root_parameters[1];
    ID3D12GraphicsCommandList *command_list;
    ID3D12PipelineState *pipeline_state;
    struct depth_stencil_resource ds;
    struct test_context_desc desc;
    struct test_context context;
    ID3D12DescriptorHeap *heap;
    ID3D12CommandQueue *queue;
    struct uvec4 uvec4 = {0};
    ID3D12Resource *texture;
    ID3D12Device *device;
    unsigned int i;
    HRESULT hr;

    static const DWORD cs_code[] =
    {
#if 0
        Texture2D<uint4> t;
        RWTexture2D<uint4> u;

        [numthreads(1, 1, 1)]
        void main(uint2 id : SV_GroupID)
        {
            u[id] = t[id];
        }
#endif
        0x43425844, 0x0b41fa64, 0xd64df766, 0xc4c98283, 0xb810dc2b, 0x00000001, 0x00000110, 0x00000003,
        0x0000002c, 0x0000003c, 0x0000004c, 0x4e475349, 0x00000008, 0x00000000, 0x00000008, 0x4e47534f,
        0x00000008, 0x00000000, 0x00000008, 0x58454853, 0x000000bc, 0x00050050, 0x0000002f, 0x0100086a,
        0x04001858, 0x00107000, 0x00000000, 0x00004444, 0x0400189c, 0x0011e000, 0x00000000, 0x00004444,
        0x0200005f, 0x00021032, 0x02000068, 0x00000001, 0x0400009b, 0x00000001, 0x00000001, 0x00000001,
        0x04000036, 0x00100032, 0x00000000, 0x00021046, 0x08000036, 0x001000c2, 0x00000000, 0x00004002,
        0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x8900002d, 0x800000c2, 0x00111103, 0x001000f2,
        0x00000000, 0x00100e46, 0x00000000, 0x00107e46, 0x00000000, 0x060000a4, 0x0011e0f2, 0x00000000,
        0x00021546, 0x00100e46, 0x00000000, 0x0100003e,
    };
    static const DWORD ps_code[] =
    {
#if 0
        Texture2D<uint4> t;

        uint4 main(float4 position : SV_Position) : SV_Target
        {
            return t[int2(position.x, position.y)];
        }
#endif
        0x43425844, 0x9ad18dbc, 0x98de0e54, 0xe3c15d5b, 0xac8b580a, 0x00000001, 0x00000138, 0x00000003,
        0x0000002c, 0x00000060, 0x00000094, 0x4e475349, 0x0000002c, 0x00000001, 0x00000008, 0x00000020,
        0x00000000, 0x00000001, 0x00000003, 0x00000000, 0x0000030f, 0x505f5653, 0x7469736f, 0x006e6f69,
        0x4e47534f, 0x0000002c, 0x00000001, 0x00000008, 0x00000020, 0x00000000, 0x00000000, 0x00000001,
        0x00000000, 0x0000000f, 0x545f5653, 0x65677261, 0xabab0074, 0x58454853, 0x0000009c, 0x00000050,
        0x00000027, 0x0100086a, 0x04001858, 0x00107000, 0x00000000, 0x00004444, 0x04002064, 0x00101032,
        0x00000000, 0x00000001, 0x03000065, 0x001020f2, 0x00000000, 0x02000068, 0x00000001, 0x0500001b,
        0x00100032, 0x00000000, 0x00101046, 0x00000000, 0x08000036, 0x001000c2, 0x00000000, 0x00004002,
        0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x8900002d, 0x800000c2, 0x00111103, 0x001020f2,
        0x00000000, 0x00100e46, 0x00000000, 0x00107e46, 0x00000000, 0x0100003e,
    };
    static const D3D12_SHADER_BYTECODE ps = {ps_code, sizeof(ps_code)};
    static const float white[] = {1.0f, 1.0f, 1.0f, 1.0f};
    static unsigned int tests[] = {0, 50, 75, 100, 150, 200, 255};

    memset(&desc, 0, sizeof(desc));
    desc.rt_format = DXGI_FORMAT_R32G32B32A32_UINT;
    desc.no_root_signature = true;
    if (!init_test_context(&context, &desc))
        return;
    device = context.device;
    command_list = context.list;
    queue = context.queue;

    descriptor_ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    descriptor_ranges[0].NumDescriptors = 1;
    descriptor_ranges[0].BaseShaderRegister = 0;
    descriptor_ranges[0].RegisterSpace = 0;
    descriptor_ranges[0].OffsetInDescriptorsFromTableStart = 0;
    descriptor_ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    descriptor_ranges[1].NumDescriptors = 1;
    descriptor_ranges[1].BaseShaderRegister = 0;
    descriptor_ranges[1].RegisterSpace = 0;
    descriptor_ranges[1].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
    root_parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    root_parameters[0].DescriptorTable.NumDescriptorRanges = 2;
    root_parameters[0].DescriptorTable.pDescriptorRanges = descriptor_ranges;
    root_parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    root_signature_desc.NumParameters = 1;
    root_signature_desc.pParameters = root_parameters;
    root_signature_desc.NumStaticSamplers = 0;
    root_signature_desc.pStaticSamplers = NULL;
    root_signature_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
    hr = create_root_signature(device, &root_signature_desc, &context.root_signature);
    ok(hr == S_OK, "Failed to create root signature, hr %#x.\n", hr);

    pipeline_state = create_compute_pipeline_state(device, context.root_signature,
            shader_bytecode(cs_code, sizeof(cs_code)));
    context.pipeline_state = create_pipeline_state(context.device,
            context.root_signature, context.render_target_desc.Format, NULL, &ps, NULL);

    heap = create_gpu_descriptor_heap(device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 2);

    init_depth_stencil(&ds, device, context.render_target_desc.Width,
            context.render_target_desc.Height, 1, 1, DXGI_FORMAT_R24G8_TYPELESS, DXGI_FORMAT_D24_UNORM_S8_UINT, NULL);
    memset(&srv_desc, 0, sizeof(srv_desc));
    srv_desc.Format = DXGI_FORMAT_X24_TYPELESS_G8_UINT;
    srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv_desc.Shader4ComponentMapping = D3D12_ENCODE_SHADER_4_COMPONENT_MAPPING(
            D3D12_SHADER_COMPONENT_MAPPING_FROM_MEMORY_COMPONENT_1,
            D3D12_SHADER_COMPONENT_MAPPING_FROM_MEMORY_COMPONENT_1,
            D3D12_SHADER_COMPONENT_MAPPING_FROM_MEMORY_COMPONENT_1,
            D3D12_SHADER_COMPONENT_MAPPING_FROM_MEMORY_COMPONENT_1);
    srv_desc.Texture2D.MipLevels = 1;
    srv_desc.Texture2D.PlaneSlice = 1;
    ID3D12Device_CreateShaderResourceView(device, ds.texture, &srv_desc,
            get_cpu_descriptor_handle(&context, heap, 0));

    texture = create_default_texture(device, 32, 32, DXGI_FORMAT_R32G32B32A32_UINT,
            D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    ID3D12Device_CreateUnorderedAccessView(device, texture, NULL, NULL,
            get_cpu_descriptor_handle(&context, heap, 1));

    for (i = 0; i < ARRAY_SIZE(tests); ++i)
    {
        vkd3d_test_set_context("Test %u", i);

        uvec4.x = uvec4.y = uvec4.z = uvec4.w = tests[i];

        ID3D12GraphicsCommandList_ClearDepthStencilView(command_list, ds.dsv_handle,
                D3D12_CLEAR_FLAG_STENCIL, 0.0f, tests[i], 0, NULL);
        transition_sub_resource_state(command_list, ds.texture, 0,
                D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

        ID3D12GraphicsCommandList_ClearRenderTargetView(command_list, context.rtv, white, 0, NULL);
        ID3D12GraphicsCommandList_OMSetRenderTargets(command_list, 1, &context.rtv, false, NULL);
        ID3D12GraphicsCommandList_RSSetViewports(command_list, 1, &context.viewport);
        ID3D12GraphicsCommandList_RSSetScissorRects(command_list, 1, &context.scissor_rect);

        ID3D12GraphicsCommandList_SetDescriptorHeaps(command_list, 1, &heap);

        ID3D12GraphicsCommandList_SetPipelineState(command_list, context.pipeline_state);
        ID3D12GraphicsCommandList_SetGraphicsRootSignature(command_list, context.root_signature);
        ID3D12GraphicsCommandList_SetGraphicsRootDescriptorTable(command_list, 0,
                ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(heap));
        ID3D12GraphicsCommandList_IASetPrimitiveTopology(command_list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ID3D12GraphicsCommandList_DrawInstanced(command_list, 3, 1, 0, 0);

        ID3D12GraphicsCommandList_SetPipelineState(command_list, pipeline_state);
        ID3D12GraphicsCommandList_SetComputeRootSignature(command_list, context.root_signature);
        ID3D12GraphicsCommandList_SetComputeRootDescriptorTable(command_list, 0,
                ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(heap));
        ID3D12GraphicsCommandList_Dispatch(command_list, 32, 32, 1);

        transition_sub_resource_state(command_list, context.render_target, 0,
                D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
        check_sub_resource_uvec4(context.render_target, 0, queue, command_list, &uvec4);

        reset_command_list(command_list, context.allocator);
        transition_sub_resource_state(command_list, texture, 0,
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
        check_sub_resource_uvec4(texture, 0, queue, command_list, &uvec4);

        reset_command_list(command_list, context.allocator);
        transition_sub_resource_state(command_list, context.render_target, 0,
                D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
        transition_sub_resource_state(command_list, texture, 0,
                D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        transition_sub_resource_state(command_list, ds.texture, 0,
                D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE);
    }
    vkd3d_test_set_context(NULL);

    destroy_depth_stencil(&ds);
    ID3D12Resource_Release(texture);
    ID3D12DescriptorHeap_Release(heap);
    ID3D12PipelineState_Release(pipeline_state);
    destroy_test_context(&context);
}

void test_early_depth_stencil_tests(void)
{
    D3D12_ROOT_SIGNATURE_DESC root_signature_desc;
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc;
    ID3D12DescriptorHeap *cpu_heap, *gpu_heap;
    ID3D12GraphicsCommandList *command_list;
    D3D12_DESCRIPTOR_RANGE descriptor_range;
    D3D12_ROOT_PARAMETER root_parameter;
    struct depth_stencil_resource ds;
    struct test_context_desc desc;
    struct test_context context;
    ID3D12CommandQueue *queue;
    ID3D12Resource *texture;
    HRESULT hr;

    static const DWORD ps_code[] =
    {
#if 0
        RWTexture2D<int> u;

        [earlydepthstencil]
        void main()
        {
            InterlockedAdd(u[uint2(0, 0)], 1);
        }
#endif
        0x43425844, 0xd8c9f845, 0xadb9dbe2, 0x4e8aea86, 0x80f0b053, 0x00000001, 0x0000009c, 0x00000003,
        0x0000002c, 0x0000003c, 0x0000004c, 0x4e475349, 0x00000008, 0x00000000, 0x00000008, 0x4e47534f,
        0x00000008, 0x00000000, 0x00000008, 0x58454853, 0x00000048, 0x00000050, 0x00000012, 0x0100286a,
        0x0400189c, 0x0011e000, 0x00000000, 0x00003333, 0x0a0000ad, 0x0011e000, 0x00000000, 0x00004002,
        0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00004001, 0x00000001, 0x0100003e,
    };
    static const D3D12_SHADER_BYTECODE ps = {ps_code, sizeof(ps_code)};
    static const UINT values[4] = {0};

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
    root_parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    root_signature_desc.NumParameters = 1;
    root_signature_desc.pParameters = &root_parameter;
    root_signature_desc.NumStaticSamplers = 0;
    root_signature_desc.pStaticSamplers = NULL;
    root_signature_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
    hr = create_root_signature(context.device, &root_signature_desc, &context.root_signature);
    ok(hr == S_OK, "Failed to create root signature, hr %#x.\n", hr);

    init_pipeline_state_desc(&pso_desc, context.root_signature, 0, NULL, &ps, NULL);
    pso_desc.NumRenderTargets = 0;
    pso_desc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    pso_desc.DepthStencilState.DepthEnable = true;
    pso_desc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    pso_desc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    hr = ID3D12Device_CreateGraphicsPipelineState(context.device, &pso_desc,
            &IID_ID3D12PipelineState, (void **)&context.pipeline_state);
    ok(hr == S_OK, "Failed to create graphics pipeline state, hr %#x.\n", hr);

    init_depth_stencil(&ds, context.device, 1, 1, 1, 1, DXGI_FORMAT_D32_FLOAT, 0, NULL);
    set_rect(&context.scissor_rect, 0, 0, 1, 1);

    texture = create_default_texture(context.device, 1, 1, DXGI_FORMAT_R32_SINT,
            D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    cpu_heap = create_cpu_descriptor_heap(context.device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1);
    gpu_heap = create_gpu_descriptor_heap(context.device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1);
    ID3D12Device_CreateUnorderedAccessView(context.device, texture, NULL, NULL,
            get_cpu_descriptor_handle(&context, cpu_heap, 0));
    ID3D12Device_CreateUnorderedAccessView(context.device, texture, NULL, NULL,
            get_cpu_descriptor_handle(&context, gpu_heap, 0));

    set_viewport(&context.viewport, 0.0f, 0.0f, 1.0f, 100.0f, 0.5f, 0.5f);

    ID3D12GraphicsCommandList_OMSetRenderTargets(command_list, 0, NULL, false, &ds.dsv_handle);
    ID3D12GraphicsCommandList_SetGraphicsRootSignature(command_list, context.root_signature);
    ID3D12GraphicsCommandList_SetPipelineState(command_list, context.pipeline_state);
    ID3D12GraphicsCommandList_IASetPrimitiveTopology(command_list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D12GraphicsCommandList_RSSetViewports(command_list, 1, &context.viewport);
    ID3D12GraphicsCommandList_RSSetScissorRects(command_list, 1, &context.scissor_rect);
    ID3D12GraphicsCommandList_SetDescriptorHeaps(command_list, 1, &gpu_heap);
    ID3D12GraphicsCommandList_SetGraphicsRootDescriptorTable(command_list, 0,
            get_gpu_descriptor_handle(&context, gpu_heap, 0));

    ID3D12GraphicsCommandList_ClearUnorderedAccessViewUint(command_list,
            get_gpu_descriptor_handle(&context, gpu_heap, 0),
            get_cpu_descriptor_handle(&context, cpu_heap, 0), texture, values, 0, NULL);

    ID3D12GraphicsCommandList_ClearDepthStencilView(command_list, ds.dsv_handle,
            D3D12_CLEAR_FLAG_DEPTH, 0.6f, 0, 0, NULL);
    ID3D12GraphicsCommandList_DrawInstanced(command_list, 3, 1, 0, 0);
    ID3D12GraphicsCommandList_DrawInstanced(command_list, 3, 1, 0, 0);

    transition_resource_state(command_list, ds.texture,
            D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_COPY_SOURCE);
    transition_resource_state(command_list, texture,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
    check_sub_resource_float(ds.texture, 0, queue, command_list, 0.6f, 1);
    reset_command_list(command_list, context.allocator);
    check_sub_resource_uint(texture, 0, queue, command_list, 2, 1);

    reset_command_list(command_list, context.allocator);
    transition_resource_state(command_list, ds.texture,
            D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE);
    transition_resource_state(command_list, texture,
            D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    ID3D12GraphicsCommandList_OMSetRenderTargets(command_list, 0, NULL, false, &ds.dsv_handle);
    ID3D12GraphicsCommandList_SetGraphicsRootSignature(command_list, context.root_signature);
    ID3D12GraphicsCommandList_SetPipelineState(command_list, context.pipeline_state);
    ID3D12GraphicsCommandList_IASetPrimitiveTopology(command_list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D12GraphicsCommandList_RSSetViewports(command_list, 1, &context.viewport);
    ID3D12GraphicsCommandList_RSSetScissorRects(command_list, 1, &context.scissor_rect);
    ID3D12GraphicsCommandList_SetDescriptorHeaps(command_list, 1, &gpu_heap);
    ID3D12GraphicsCommandList_SetGraphicsRootDescriptorTable(command_list, 0,
            get_gpu_descriptor_handle(&context, gpu_heap, 0));

    ID3D12GraphicsCommandList_ClearDepthStencilView(command_list, ds.dsv_handle,
            D3D12_CLEAR_FLAG_DEPTH, 0.3f, 0, 0, NULL);
    ID3D12GraphicsCommandList_DrawInstanced(command_list, 3, 1, 0, 0);
    ID3D12GraphicsCommandList_DrawInstanced(command_list, 3, 1, 0, 0);

    ID3D12GraphicsCommandList_ClearDepthStencilView(command_list, ds.dsv_handle,
            D3D12_CLEAR_FLAG_DEPTH, 0.55f, 0, 0, NULL);
    ID3D12GraphicsCommandList_DrawInstanced(command_list, 3, 1, 0, 0);

    ID3D12GraphicsCommandList_ClearDepthStencilView(command_list, ds.dsv_handle,
            D3D12_CLEAR_FLAG_DEPTH, 0.5f, 0, 0, NULL);
    ID3D12GraphicsCommandList_DrawInstanced(command_list, 3, 1, 0, 0);

    transition_resource_state(command_list, ds.texture,
            D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_COPY_SOURCE);
    transition_resource_state(command_list, texture,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
    check_sub_resource_float(ds.texture, 0, queue, command_list, 0.5f, 1);
    reset_command_list(command_list, context.allocator);
    check_sub_resource_uint(texture, 0, queue, command_list, 4, 1);

    ID3D12Resource_Release(texture);
    ID3D12DescriptorHeap_Release(cpu_heap);
    ID3D12DescriptorHeap_Release(gpu_heap);
    destroy_depth_stencil(&ds);
    destroy_test_context(&context);
}

static void test_stencil_export(bool use_dxil)
{
    D3D12_SHADER_RESOURCE_VIEW_DESC stencil_srv_desc;
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc;
    D3D12_FEATURE_DATA_D3D12_OPTIONS options;
    ID3D12GraphicsCommandList *command_list;
    ID3D12PipelineState *pso_sample, *pso;
    D3D12_DESCRIPTOR_HEAP_DESC heap_desc;
    ID3D12RootSignature *rs_sample, *rs;
    struct depth_stencil_resource ds;
    ID3D12DescriptorHeap *srv_heap;
    struct test_context_desc desc;
    struct test_context context;
    ID3D12CommandQueue *queue;
    FLOAT clear_color[4];
    HRESULT hr;

#if 0
        uint stencil_ref;

        uint main() : SV_StencilRef
        {
            return stencil_ref;
        }
#endif
    static const DWORD ps_code[] =
    {
        0x43425844, 0x3980cb16, 0xbbe87d38, 0xb93f7c61, 0x200c41ed, 0x00000001, 0x000000cc, 0x00000004,
        0x00000030, 0x00000040, 0x00000078, 0x000000bc, 0x4e475349, 0x00000008, 0x00000000, 0x00000008,
        0x4e47534f, 0x00000030, 0x00000001, 0x00000008, 0x00000020, 0x00000000, 0x00000000, 0x00000001,
        0xffffffff, 0x00000e01, 0x535f5653, 0x636e6574, 0x65526c69, 0xabab0066, 0x58454853, 0x0000003c,
        0x00000050, 0x0000000f, 0x0100086a, 0x04000059, 0x00208e46, 0x00000000, 0x00000001, 0x02000065,
        0x00029001, 0x05000036, 0x00029001, 0x0020800a, 0x00000000, 0x00000000, 0x0100003e, 0x30494653,
        0x00000008, 0x00000200, 0x00000000,
    };
    static const BYTE ps_code_dxil[] =
    {
        0x44, 0x58, 0x42, 0x43, 0x07, 0xb6, 0x67, 0xb6, 0xca, 0xa1, 0xf2, 0x47, 0x15, 0x5c, 0xaa, 0xa2, 0x90, 0x68, 0xa7, 0xd9, 0x01, 0x00, 0x00, 0x00, 0x3a, 0x06, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00,
        0x38, 0x00, 0x00, 0x00, 0x48, 0x00, 0x00, 0x00, 0x58, 0x00, 0x00, 0x00, 0x96, 0x00, 0x00, 0x00, 0x02, 0x01, 0x00, 0x00, 0x1e, 0x01, 0x00, 0x00, 0x53, 0x46, 0x49, 0x30, 0x08, 0x00, 0x00, 0x00,
        0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x49, 0x53, 0x47, 0x31, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x4f, 0x53, 0x47, 0x31, 0x36, 0x00, 0x00, 0x00,
        0x01, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x28, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x45, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff,
        0x01, 0x0e, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x53, 0x56, 0x5f, 0x53, 0x74, 0x65, 0x6e, 0x63, 0x69, 0x6c, 0x52, 0x65, 0x66, 0x00, 0x50, 0x53, 0x56, 0x30, 0x64, 0x00, 0x00, 0x00, 0x24, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x01, 0x14, 0x01, 0x00,
        0x00, 0x00, 0x48, 0x41, 0x53, 0x48, 0x14, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xa4, 0xab, 0xe9, 0x54, 0x43, 0xff, 0x1e, 0x7d, 0xf7, 0xbe, 0xaa, 0x16, 0x6d, 0xf8, 0x26, 0x51, 0x44, 0x58,
        0x49, 0x4c, 0x14, 0x05, 0x00, 0x00, 0x60, 0x00, 0x00, 0x00, 0x45, 0x01, 0x00, 0x00, 0x44, 0x58, 0x49, 0x4c, 0x00, 0x01, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0xfc, 0x04, 0x00, 0x00, 0x42, 0x43,
        0xc0, 0xde, 0x21, 0x0c, 0x00, 0x00, 0x3c, 0x01, 0x00, 0x00, 0x0b, 0x82, 0x20, 0x00, 0x02, 0x00, 0x00, 0x00, 0x13, 0x00, 0x00, 0x00, 0x07, 0x81, 0x23, 0x91, 0x41, 0xc8, 0x04, 0x49, 0x06, 0x10,
        0x32, 0x39, 0x92, 0x01, 0x84, 0x0c, 0x25, 0x05, 0x08, 0x19, 0x1e, 0x04, 0x8b, 0x62, 0x80, 0x14, 0x45, 0x02, 0x42, 0x92, 0x0b, 0x42, 0xa4, 0x10, 0x32, 0x14, 0x38, 0x08, 0x18, 0x4b, 0x0a, 0x32,
        0x52, 0x88, 0x48, 0x90, 0x14, 0x20, 0x43, 0x46, 0x88, 0xa5, 0x00, 0x19, 0x32, 0x42, 0xe4, 0x48, 0x0e, 0x90, 0x91, 0x22, 0xc4, 0x50, 0x41, 0x51, 0x81, 0x8c, 0xe1, 0x83, 0xe5, 0x8a, 0x04, 0x29,
        0x46, 0x06, 0x51, 0x18, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0x1b, 0x88, 0xe0, 0xff, 0xff, 0xff, 0xff, 0x07, 0x40, 0xda, 0x60, 0x08, 0xff, 0xff, 0xff, 0xff, 0x3f, 0x00, 0x12, 0x50, 0x01, 0x00,
        0x00, 0x00, 0x49, 0x18, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x13, 0x82, 0x60, 0x42, 0x20, 0x00, 0x00, 0x00, 0x89, 0x20, 0x00, 0x00, 0x1f, 0x00, 0x00, 0x00, 0x32, 0x22, 0x48, 0x09, 0x20, 0x64,
        0x85, 0x04, 0x93, 0x22, 0xa4, 0x84, 0x04, 0x93, 0x22, 0xe3, 0x84, 0xa1, 0x90, 0x14, 0x12, 0x4c, 0x8a, 0x8c, 0x0b, 0x84, 0xa4, 0x4c, 0x10, 0x4c, 0x23, 0x00, 0x25, 0x00, 0x14, 0xe6, 0x08, 0xc0,
        0x60, 0x8e, 0x00, 0x29, 0x06, 0x18, 0x63, 0x90, 0x41, 0xe5, 0xa8, 0xe1, 0xf2, 0x27, 0xec, 0x21, 0x24, 0x9f, 0xdb, 0xa8, 0x62, 0x25, 0x26, 0x1f, 0xb9, 0x6d, 0x44, 0x8c, 0x31, 0x06, 0x91, 0x7b,
        0x86, 0xcb, 0x9f, 0xb0, 0x87, 0x90, 0xfc, 0x10, 0x68, 0x86, 0x85, 0x40, 0x01, 0x2a, 0xc4, 0x19, 0x69, 0x90, 0x9a, 0x23, 0x08, 0x8a, 0x91, 0x06, 0x19, 0x83, 0x51, 0x1b, 0x08, 0x98, 0x09, 0x21,
        0x83, 0x53, 0x60, 0x87, 0x77, 0x10, 0x87, 0x70, 0x60, 0x87, 0x79, 0x40, 0xc1, 0x20, 0x38, 0x47, 0x00, 0x0a, 0x00, 0x00, 0x00, 0x00, 0x13, 0x14, 0x72, 0xc0, 0x87, 0x74, 0x60, 0x87, 0x36, 0x68,
        0x87, 0x79, 0x68, 0x03, 0x72, 0xc0, 0x87, 0x0d, 0xaf, 0x50, 0x0e, 0x6d, 0xd0, 0x0e, 0x7a, 0x50, 0x0e, 0x6d, 0x00, 0x0f, 0x7a, 0x30, 0x07, 0x72, 0xa0, 0x07, 0x73, 0x20, 0x07, 0x6d, 0x90, 0x0e,
        0x71, 0xa0, 0x07, 0x73, 0x20, 0x07, 0x6d, 0x90, 0x0e, 0x78, 0xa0, 0x07, 0x73, 0x20, 0x07, 0x6d, 0x90, 0x0e, 0x71, 0x60, 0x07, 0x7a, 0x30, 0x07, 0x72, 0xd0, 0x06, 0xe9, 0x30, 0x07, 0x72, 0xa0,
        0x07, 0x73, 0x20, 0x07, 0x6d, 0x90, 0x0e, 0x76, 0x40, 0x07, 0x7a, 0x60, 0x07, 0x74, 0xd0, 0x06, 0xe6, 0x10, 0x07, 0x76, 0xa0, 0x07, 0x73, 0x20, 0x07, 0x6d, 0x60, 0x0e, 0x73, 0x20, 0x07, 0x7a,
        0x30, 0x07, 0x72, 0xd0, 0x06, 0xe6, 0x60, 0x07, 0x74, 0xa0, 0x07, 0x76, 0x40, 0x07, 0x6d, 0xe0, 0x0e, 0x78, 0xa0, 0x07, 0x71, 0x60, 0x07, 0x7a, 0x30, 0x07, 0x72, 0xa0, 0x07, 0x76, 0x40, 0x07,
        0x43, 0x9e, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x86, 0x3c, 0x05, 0x10, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0c, 0x79, 0x14, 0x20, 0x00, 0x04,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0xf2, 0x34, 0x40, 0x00, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x90, 0x05, 0x02, 0x00, 0x00, 0x00, 0x0b, 0x00, 0x00, 0x00, 0x32, 0x1e,
        0x98, 0x14, 0x19, 0x11, 0x4c, 0x90, 0x8c, 0x09, 0x26, 0x47, 0xc6, 0x04, 0x43, 0x1a, 0x25, 0x30, 0x02, 0x50, 0x0a, 0xc5, 0x50, 0x08, 0x35, 0x50, 0x06, 0x44, 0x4a, 0xa1, 0x50, 0x46, 0x00, 0x4a,
        0xa0, 0x06, 0x48, 0x16, 0x20, 0x08, 0xc5, 0x19, 0x00, 0x00, 0x79, 0x18, 0x00, 0x00, 0x47, 0x00, 0x00, 0x00, 0x1a, 0x03, 0x4c, 0x90, 0x46, 0x02, 0x13, 0x44, 0x35, 0x18, 0x63, 0x0b, 0x73, 0x3b,
        0x03, 0xb1, 0x2b, 0x93, 0x9b, 0x4b, 0x7b, 0x73, 0x03, 0x99, 0x71, 0xb9, 0x01, 0x41, 0xa1, 0x0b, 0x3b, 0x9b, 0x7b, 0x91, 0x2a, 0x62, 0x2a, 0x0a, 0x9a, 0x2a, 0xfa, 0x9a, 0xb9, 0x81, 0x79, 0x31,
        0x4b, 0x73, 0x0b, 0x63, 0x4b, 0xd9, 0x10, 0x04, 0x13, 0x84, 0x81, 0x98, 0x20, 0x0c, 0xc5, 0x06, 0x61, 0x20, 0x26, 0x08, 0x83, 0xb1, 0x41, 0x18, 0x0c, 0x0a, 0x70, 0x73, 0x13, 0x84, 0xe1, 0xd8,
        0x30, 0x20, 0x09, 0x31, 0x41, 0x88, 0x22, 0x02, 0x13, 0x84, 0x01, 0xd9, 0x80, 0x10, 0x0b, 0x43, 0x10, 0x43, 0x03, 0x6c, 0x08, 0x9c, 0x0d, 0x04, 0x00, 0x3c, 0xc0, 0x04, 0x41, 0x00, 0x48, 0xb4,
        0x85, 0xa5, 0xb9, 0xb1, 0x99, 0xb2, 0xfa, 0x9a, 0xa2, 0x2b, 0x73, 0x1b, 0x4b, 0x63, 0x93, 0x2a, 0x33, 0x9b, 0x20, 0x10, 0xcb, 0x04, 0x81, 0x60, 0x36, 0x04, 0xc4, 0x04, 0x81, 0x68, 0x26, 0x08,
        0x84, 0x33, 0x41, 0x18, 0x92, 0x09, 0x02, 0xf1, 0x4c, 0x10, 0x06, 0x65, 0x83, 0xb0, 0x0d, 0x1b, 0x16, 0x62, 0xa2, 0x2a, 0xeb, 0x1a, 0xb0, 0x4c, 0xe3, 0x36, 0x04, 0xdd, 0x86, 0x01, 0xf0, 0x80,
        0x09, 0x82, 0x04, 0x6d, 0x10, 0x08, 0x30, 0xd8, 0x50, 0x44, 0xd2, 0x07, 0x85, 0x41, 0x15, 0x36, 0x36, 0xbb, 0x36, 0x97, 0x34, 0xb2, 0x32, 0x37, 0xba, 0x29, 0x41, 0x50, 0x85, 0x0c, 0xcf, 0xc5,
        0xae, 0x4c, 0x6e, 0x2e, 0xed, 0xcd, 0x6d, 0x4a, 0x40, 0x34, 0x21, 0xc3, 0x73, 0xb1, 0x0b, 0x63, 0xb3, 0x2b, 0x93, 0x9b, 0x12, 0x18, 0x75, 0xc8, 0xf0, 0x5c, 0xe6, 0xd0, 0xc2, 0xc8, 0xca, 0xe4,
        0x9a, 0xde, 0xc8, 0xca, 0xd8, 0xa6, 0x04, 0x49, 0x19, 0x32, 0x3c, 0x17, 0xb9, 0xb2, 0xb9, 0xb7, 0x3a, 0xb9, 0xb1, 0xb2, 0xb9, 0x29, 0xc1, 0x53, 0x87, 0x0c, 0xcf, 0xa5, 0xcc, 0x8d, 0x4e, 0x2e,
        0x0f, 0xea, 0x2d, 0xcd, 0x8d, 0x6e, 0x6e, 0x4a, 0x10, 0x06, 0x00, 0x00, 0x00, 0x00, 0x79, 0x18, 0x00, 0x00, 0x4c, 0x00, 0x00, 0x00, 0x33, 0x08, 0x80, 0x1c, 0xc4, 0xe1, 0x1c, 0x66, 0x14, 0x01,
        0x3d, 0x88, 0x43, 0x38, 0x84, 0xc3, 0x8c, 0x42, 0x80, 0x07, 0x79, 0x78, 0x07, 0x73, 0x98, 0x71, 0x0c, 0xe6, 0x00, 0x0f, 0xed, 0x10, 0x0e, 0xf4, 0x80, 0x0e, 0x33, 0x0c, 0x42, 0x1e, 0xc2, 0xc1,
        0x1d, 0xce, 0xa1, 0x1c, 0x66, 0x30, 0x05, 0x3d, 0x88, 0x43, 0x38, 0x84, 0x83, 0x1b, 0xcc, 0x03, 0x3d, 0xc8, 0x43, 0x3d, 0x8c, 0x03, 0x3d, 0xcc, 0x78, 0x8c, 0x74, 0x70, 0x07, 0x7b, 0x08, 0x07,
        0x79, 0x48, 0x87, 0x70, 0x70, 0x07, 0x7a, 0x70, 0x03, 0x76, 0x78, 0x87, 0x70, 0x20, 0x87, 0x19, 0xcc, 0x11, 0x0e, 0xec, 0x90, 0x0e, 0xe1, 0x30, 0x0f, 0x6e, 0x30, 0x0f, 0xe3, 0xf0, 0x0e, 0xf0,
        0x50, 0x0e, 0x33, 0x10, 0xc4, 0x1d, 0xde, 0x21, 0x1c, 0xd8, 0x21, 0x1d, 0xc2, 0x61, 0x1e, 0x66, 0x30, 0x89, 0x3b, 0xbc, 0x83, 0x3b, 0xd0, 0x43, 0x39, 0xb4, 0x03, 0x3c, 0xbc, 0x83, 0x3c, 0x84,
        0x03, 0x3b, 0xcc, 0xf0, 0x14, 0x76, 0x60, 0x07, 0x7b, 0x68, 0x07, 0x37, 0x68, 0x87, 0x72, 0x68, 0x07, 0x37, 0x80, 0x87, 0x70, 0x90, 0x87, 0x70, 0x60, 0x07, 0x76, 0x28, 0x07, 0x76, 0xf8, 0x05,
        0x76, 0x78, 0x87, 0x77, 0x80, 0x87, 0x5f, 0x08, 0x87, 0x71, 0x18, 0x87, 0x72, 0x98, 0x87, 0x79, 0x98, 0x81, 0x2c, 0xee, 0xf0, 0x0e, 0xee, 0xe0, 0x0e, 0xf5, 0xc0, 0x0e, 0xec, 0x30, 0x03, 0x62,
        0xc8, 0xa1, 0x1c, 0xe4, 0xa1, 0x1c, 0xcc, 0xa1, 0x1c, 0xe4, 0xa1, 0x1c, 0xdc, 0x61, 0x1c, 0xca, 0x21, 0x1c, 0xc4, 0x81, 0x1d, 0xca, 0x61, 0x06, 0xd6, 0x90, 0x43, 0x39, 0xc8, 0x43, 0x39, 0x98,
        0x43, 0x39, 0xc8, 0x43, 0x39, 0xb8, 0xc3, 0x38, 0x94, 0x43, 0x38, 0x88, 0x03, 0x3b, 0x94, 0xc3, 0x2f, 0xbc, 0x83, 0x3c, 0xfc, 0x82, 0x3b, 0xd4, 0x03, 0x3b, 0xb0, 0xc3, 0x0c, 0xc4, 0x21, 0x07,
        0x7c, 0x70, 0x03, 0x7a, 0x28, 0x87, 0x76, 0x80, 0x87, 0x19, 0xd1, 0x43, 0x0e, 0xf8, 0xe0, 0x06, 0xe4, 0x20, 0x0e, 0xe7, 0xe0, 0x06, 0xf6, 0x10, 0x0e, 0xf2, 0xc0, 0x0e, 0xe1, 0x90, 0x0f, 0xef,
        0x50, 0x0f, 0xf4, 0x00, 0x00, 0x00, 0x71, 0x20, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x26, 0xb0, 0x0d, 0x97, 0xef, 0x3c, 0xbe, 0x10, 0x50, 0x45, 0x41, 0x44, 0xa5, 0x03, 0x0c, 0x25, 0x61, 0x00,
        0x02, 0xe6, 0x23, 0xb7, 0x6d, 0x03, 0xd2, 0x70, 0xf9, 0xce, 0xe3, 0x0b, 0x11, 0x01, 0x4c, 0x44, 0x08, 0x34, 0xc3, 0x42, 0x58, 0x40, 0x35, 0x5c, 0xbe, 0xf3, 0xf8, 0xd2, 0xe4, 0x44, 0x04, 0x4a,
        0x4d, 0x0f, 0x35, 0xf9, 0xc8, 0x6d, 0x1b, 0x00, 0xc1, 0x00, 0x48, 0x03, 0x00, 0x00, 0x61, 0x20, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x13, 0x04, 0x41, 0x2c, 0x10, 0x00, 0x00, 0x00, 0x03, 0x00,
        0x00, 0x00, 0x34, 0x4a, 0xae, 0xec, 0x88, 0x14, 0x01, 0xb1, 0x11, 0x00, 0x00, 0x00, 0x23, 0x06, 0x09, 0x00, 0x82, 0x60, 0xd0, 0x4c, 0x84, 0x10, 0x45, 0xc1, 0x88, 0xc1, 0x01, 0x80, 0x20, 0x18,
        0x28, 0x15, 0x11, 0x48, 0xa3, 0x09, 0x01, 0x30, 0x62, 0x90, 0x00, 0x20, 0x08, 0x06, 0x05, 0x36, 0x51, 0x14, 0x13, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    };
    const D3D12_SHADER_BYTECODE ps = {
        use_dxil ? (const void*)ps_code_dxil : (const void*)ps_code,
        use_dxil ? sizeof(ps_code_dxil) : sizeof(ps_code)};
    static const DWORD ps_sample_code[] =
    {
#if 0
        Texture2D<uint4> tex : register(t0);

        uint4 main(float4 pos : SV_Position) : SV_TARGET
        {
                return tex[uint2(pos.xy)].g;
        }
#endif
        0x43425844, 0xfc2ab030, 0xba3b4106, 0x435bce60, 0xee3a6f75, 0x00000001, 0x0000014c, 0x00000003,
        0x0000002c, 0x00000060, 0x00000094, 0x4e475349, 0x0000002c, 0x00000001, 0x00000008, 0x00000020,
        0x00000000, 0x00000001, 0x00000003, 0x00000000, 0x0000030f, 0x505f5653, 0x7469736f, 0x006e6f69,
        0x4e47534f, 0x0000002c, 0x00000001, 0x00000008, 0x00000020, 0x00000000, 0x00000000, 0x00000001,
        0x00000000, 0x00000e01, 0x545f5653, 0x45475241, 0xabab0054, 0x58454853, 0x000000b0, 0x00000050,
        0x0000002c, 0x0100086a, 0x04001858, 0x00107000, 0x00000000, 0x00004444, 0x04002064, 0x00101032,
        0x00000000, 0x00000001, 0x03000065, 0x00102012, 0x00000000, 0x02000068, 0x00000001, 0x0500001c,
        0x00100032, 0x00000000, 0x00101046, 0x00000000, 0x08000036, 0x001000c2, 0x00000000, 0x00004002,
        0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x8900002d, 0x800000c2, 0x00111103, 0x00100012,
        0x00000000, 0x00100e46, 0x00000000, 0x00107e16, 0x00000000, 0x05000036, 0x00102012, 0x00000000,
        0x0010000a, 0x00000000, 0x0100003e, 
    };
    static const D3D12_SHADER_BYTECODE ps_sample = {ps_sample_code, sizeof(ps_sample_code)};

    memset(&desc, 0, sizeof(desc));
    desc.rt_width = 640;
    desc.rt_height = 480;
    desc.rt_format = DXGI_FORMAT_R8_UINT;
    if (!init_test_context(&context, &desc))
        return;

    if (use_dxil && !context_supports_dxil(&context))
    {
        destroy_test_context(&context);
        return;
    }

    hr = ID3D12Device_CheckFeatureSupport(context.device, D3D12_FEATURE_D3D12_OPTIONS, &options, sizeof(options));
    ok(hr == S_OK, "Failed to check feature support, hr %#x.\n", hr);

    if (!options.PSSpecifiedStencilRefSupported)
    {
        skip("PSSpecifiedStencilRefSupported not supported by device.\n");
        destroy_test_context(&context);
        return;
    }

    command_list = context.list;
    queue = context.queue;

    init_depth_stencil(&ds, context.device, 640, 480, 1, 1,
            DXGI_FORMAT_R32G8X24_TYPELESS, DXGI_FORMAT_D32_FLOAT_S8X24_UINT, NULL);
    set_viewport(&context.viewport, 0.0f, 0.0f, 640.0f, 480.0f, 0.0f, 1.0f);
    set_rect(&context.scissor_rect, 0, 0, 640, 480);

    rs = create_32bit_constants_root_signature(context.device,
            0, 1, D3D12_SHADER_VISIBILITY_PIXEL);

    if (use_dxil)
        init_pipeline_state_desc_dxil(&pso_desc, rs, 0, NULL, &ps, NULL);
    else
        init_pipeline_state_desc(&pso_desc, rs, 0, NULL, &ps, NULL);

    pso_desc.NumRenderTargets = 0;
    pso_desc.DSVFormat = DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
    pso_desc.DepthStencilState.StencilEnable = true;
    pso_desc.DepthStencilState.StencilReadMask = 0xFF;
    pso_desc.DepthStencilState.StencilWriteMask = 0xFF;
    pso_desc.DepthStencilState.FrontFace.StencilFailOp = D3D12_STENCIL_OP_REPLACE;
    pso_desc.DepthStencilState.FrontFace.StencilDepthFailOp = D3D12_STENCIL_OP_REPLACE;
    pso_desc.DepthStencilState.FrontFace.StencilPassOp = D3D12_STENCIL_OP_REPLACE;
    pso_desc.DepthStencilState.FrontFace.StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    pso_desc.DepthStencilState.BackFace = pso_desc.DepthStencilState.FrontFace;
    hr = ID3D12Device_CreateGraphicsPipelineState(context.device, &pso_desc,
            &IID_ID3D12PipelineState, (void **)&pso);
    ok(SUCCEEDED(hr), "Failed to create graphics pipeline state, hr %#x.\n", hr);

    rs_sample = create_texture_root_signature(context.device,
            D3D12_SHADER_VISIBILITY_PIXEL, 0, 0);
    init_pipeline_state_desc(&pso_desc, rs_sample, DXGI_FORMAT_R8_UINT, NULL, &ps_sample, NULL);
    hr = ID3D12Device_CreateGraphicsPipelineState(context.device, &pso_desc,
            &IID_ID3D12PipelineState, (void **)&pso_sample);
    ok(SUCCEEDED(hr), "Failed to create graphics pipeline state, hr %#x.\n", hr);

    heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heap_desc.NumDescriptors = 1;
    heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    heap_desc.NodeMask = 0;

    hr = ID3D12Device_CreateDescriptorHeap(context.device, &heap_desc, &IID_ID3D12DescriptorHeap, (void **)&srv_heap);
    ok(hr == S_OK, "Failed to create descriptor heap, hr %#x.\n", hr);

    ID3D12GraphicsCommandList_ClearDepthStencilView(command_list, ds.dsv_handle,
            D3D12_CLEAR_FLAG_STENCIL, 0.0f, 0x80, 0, NULL);
    
    ID3D12GraphicsCommandList_OMSetRenderTargets(command_list, 0, NULL, false, &ds.dsv_handle);
    ID3D12GraphicsCommandList_OMSetStencilRef(command_list, 0x40);
    ID3D12GraphicsCommandList_SetGraphicsRootSignature(command_list, rs);
    ID3D12GraphicsCommandList_SetPipelineState(command_list, pso);
    ID3D12GraphicsCommandList_IASetPrimitiveTopology(command_list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D12GraphicsCommandList_RSSetViewports(command_list, 1, &context.viewport);
    ID3D12GraphicsCommandList_RSSetScissorRects(command_list, 1, &context.scissor_rect);

    ID3D12GraphicsCommandList_SetGraphicsRoot32BitConstant(command_list, 0, 0xFF, 0);
    ID3D12GraphicsCommandList_DrawInstanced(command_list, 3, 1, 0, 0);

    transition_resource_state(command_list, ds.texture,
            D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

    stencil_srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    stencil_srv_desc.Format = DXGI_FORMAT_X32_TYPELESS_G8X24_UINT;
    stencil_srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    stencil_srv_desc.Texture2D.MostDetailedMip = 0;
    stencil_srv_desc.Texture2D.MipLevels = 1;
    stencil_srv_desc.Texture2D.PlaneSlice = 1;
    stencil_srv_desc.Texture2D.ResourceMinLODClamp = 0.0f;

    ID3D12Device_CreateShaderResourceView(context.device, ds.texture, &stencil_srv_desc,
            ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(srv_heap));

    memset(clear_color, 0, sizeof(clear_color));

    ID3D12GraphicsCommandList_OMSetRenderTargets(command_list, 1, &context.rtv, false, NULL);
    ID3D12GraphicsCommandList_ClearRenderTargetView(command_list, context.rtv, clear_color, 0, NULL);
    ID3D12GraphicsCommandList_SetDescriptorHeaps(command_list, 1, &srv_heap);
    ID3D12GraphicsCommandList_SetGraphicsRootSignature(command_list, rs_sample);
    ID3D12GraphicsCommandList_SetGraphicsRootDescriptorTable(command_list, 0,
            ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(srv_heap));
    ID3D12GraphicsCommandList_SetPipelineState(command_list, pso_sample);
    ID3D12GraphicsCommandList_DrawInstanced(command_list, 3, 1, 0, 0);

    transition_resource_state(command_list, context.render_target,
            D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);

    check_sub_resource_uint8(context.render_target, 0, queue, command_list, 0x000000ff, 0);

    ID3D12DescriptorHeap_Release(srv_heap);

    ID3D12PipelineState_Release(pso_sample);
    ID3D12PipelineState_Release(pso);

    ID3D12RootSignature_Release(rs_sample);
    ID3D12RootSignature_Release(rs);

    destroy_depth_stencil(&ds);
    destroy_test_context(&context);
}

void test_stencil_export_dxbc(void)
{
    test_stencil_export(false);
}

void test_stencil_export_dxil(void)
{
    test_stencil_export(true);
}

void test_depth_stencil_layout_tracking(void)
{
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc;
    struct depth_stencil_resource ds;
    struct test_context_desc desc;
    ID3D12PipelineState *psos[4];
    struct test_context context;
    D3D12_DISCARD_REGION region;
    ID3D12RootSignature *rs;
    unsigned int i, j;
    HRESULT hr;

    static const DWORD vs_code[] =
    {
#if 0
    cbuffer C : register(b0)
    {
        float z;
    };

    float4 main(uint vid : SV_VertexID) : SV_Position
    {
        if (vid == 0)
            return float4(-1.0, -1.0, z, 1.0);
        else if (vid == 1)
            return float4(-1.0, +3.0, z, 1.0);
        else
            return float4(+3.0, -1.0, z, 1.0);
    }
#endif
        0x43425844, 0x31be9212, 0x8e44bbde, 0x8f0a87b5, 0xb8d5783b, 0x00000001, 0x000001dc, 0x00000003,
        0x0000002c, 0x00000060, 0x00000094, 0x4e475349, 0x0000002c, 0x00000001, 0x00000008, 0x00000020,
        0x00000000, 0x00000006, 0x00000001, 0x00000000, 0x00000101, 0x565f5653, 0x65747265, 0x00444978,
        0x4e47534f, 0x0000002c, 0x00000001, 0x00000008, 0x00000020, 0x00000000, 0x00000001, 0x00000003,
        0x00000000, 0x0000000f, 0x505f5653, 0x7469736f, 0x006e6f69, 0x58454853, 0x00000140, 0x00010050,
        0x00000050, 0x0100086a, 0x04000059, 0x00208e46, 0x00000000, 0x00000001, 0x04000060, 0x00101012,
        0x00000000, 0x00000006, 0x04000067, 0x001020f2, 0x00000000, 0x00000001, 0x02000068, 0x00000001,
        0x0300001f, 0x0010100a, 0x00000000, 0x08000036, 0x001020b2, 0x00000000, 0x00004002, 0xbf800000,
        0xbf800000, 0x00000000, 0x3f800000, 0x06000036, 0x00102042, 0x00000000, 0x0020800a, 0x00000000,
        0x00000000, 0x0100003e, 0x01000012, 0x07000020, 0x00100012, 0x00000000, 0x0010100a, 0x00000000,
        0x00004001, 0x00000001, 0x0304001f, 0x0010000a, 0x00000000, 0x08000036, 0x001020b2, 0x00000000,
        0x00004002, 0xbf800000, 0x40400000, 0x00000000, 0x3f800000, 0x06000036, 0x00102042, 0x00000000,
        0x0020800a, 0x00000000, 0x00000000, 0x0100003e, 0x01000012, 0x08000036, 0x001020b2, 0x00000000,
        0x00004002, 0x40400000, 0xbf800000, 0x00000000, 0x3f800000, 0x06000036, 0x00102042, 0x00000000,
        0x0020800a, 0x00000000, 0x00000000, 0x0100003e, 0x01000015, 0x01000015, 0x0100003e,
    };

    static const DWORD ps_code[] =
    {
#if 0
    void main() {}
#endif
        0x43425844, 0x499d4ed5, 0xbbe2842c, 0x179313ee, 0xde5cd5d9, 0x00000001, 0x00000064, 0x00000003,
        0x0000002c, 0x0000003c, 0x0000004c, 0x4e475349, 0x00000008, 0x00000000, 0x00000008, 0x4e47534f,
        0x00000008, 0x00000000, 0x00000008, 0x58454853, 0x00000010, 0x00000050, 0x00000004, 0x0100086a,
        0x0100003e,
    };

    static const D3D12_SHADER_BYTECODE vs = SHADER_BYTECODE(vs_code);
    static const D3D12_SHADER_BYTECODE ps = SHADER_BYTECODE(ps_code);

    enum draw_type
    {
        DRAW_TYPE_DRAW,
        DRAW_TYPE_TRANSITION,
        DRAW_TYPE_CLEAR,
        DRAW_TYPE_DISCARD,
    };

    struct draw
    {
        bool depth_write;
        bool stencil_write;
        enum draw_type type;
        D3D12_RECT rect;
        float z;
        uint8_t stencil;
    };

    static const struct draw test_full_promotion[] =
    {
        { false, false, DRAW_TYPE_DRAW, { 0, 0, 1024, 1024 }, 0.0f, 0 },
        { true, true, DRAW_TYPE_DRAW, { 0, 0, 1024, 1024 }, 0.0f, 0 },
    };

    static const struct draw test_full_promotion_no_read[] =
    {
        { true, true, DRAW_TYPE_DRAW, { 0, 0, 1024, 1024 }, 0.0f, 0 },
    };

    static const struct draw test_partial_promotion[] =
    {
        { false, false, DRAW_TYPE_DRAW, { 0, 0, 1024, 1024 }, 0.0f, 0 },
        /* Expect transition to WRITE/READ */
        { true, false, DRAW_TYPE_DRAW, { 0, 0, 1024, 1024 }, 0.0f, 0 },
        /* Expect transition to WRITE/WRITE */
        { false, true, DRAW_TYPE_DRAW, { 0, 0, 1024, 1024 }, 0.0f, 0 },
        { false, false, DRAW_TYPE_DRAW, { 0, 0, 1024, 1024 }, 0.0f, 0 },
    };

    static const struct draw test_full_implicit_transition[] =
    {
        { true, true, DRAW_TYPE_DRAW, { 0, 0, 1024, 1024 }, 0.0f, 0 },
    };

    static const struct draw test_full_explicit_transition[] =
    {
        { false, false, DRAW_TYPE_TRANSITION },
        { true, true, DRAW_TYPE_TRANSITION },
        /* We should already know the attachment is optimal. */
        { true, true, DRAW_TYPE_DRAW, { 0, 0, 1024, 1024 }, 0.0f, 0 },
    };

    static const struct draw test_partial_transition_depth[] =
    {
        { false, true, DRAW_TYPE_TRANSITION },
        /* Mark depth as optimal. */
        { true, true, DRAW_TYPE_TRANSITION },
        /* Promote stencil state here. */
        { true, true, DRAW_TYPE_DRAW, { 0, 0, 1024, 1024 }, 0.0f, 0 },
    };

    static const struct draw test_partial_transition_stencil[] =
    {
        { true, false, DRAW_TYPE_TRANSITION },
        /* Mark stencil as optimal. */
        { true, true, DRAW_TYPE_TRANSITION },
        /* Promote depth state here. */
        { true, true, DRAW_TYPE_DRAW, { 0, 0, 1024, 1024 }, 0.0f, 0 },
    };

    static const struct draw test_full_clear_transition[] =
    {
        { true, true, DRAW_TYPE_CLEAR, { 0, 0, 1024, 1024 }, 0.5f, 128 },
        /* We should already know the attachment is optimal. */
        { true, true, DRAW_TYPE_DRAW, { 0, 0, 1024, 1024 }, 0.0f, 0 },
    };

    static const struct draw test_full_discard_transition[] =
    {
        { true, true, DRAW_TYPE_DISCARD },
        /* We should already know the attachment is optimal. */
        { true, true, DRAW_TYPE_DRAW, { 0, 0, 1024, 1024 }, 0.0f, 0 },
    };

    static const struct draw test_partial_clear_depth[] =
    {
        { true, false, DRAW_TYPE_CLEAR, { 0, 0, 1024, 1024 }, 0.5f, 128 },
        /* Promote stencil here. */
        { true, true, DRAW_TYPE_DRAW, { 0, 0, 1024, 1024 }, 0.0f, 0 },
    };

    static const struct draw test_partial_clear_stencil[] =
    {
        { false, true, DRAW_TYPE_CLEAR, { 0, 0, 1024, 1024 }, 0.5f, 128 },
        /* Promote depth here. */
        { true, true, DRAW_TYPE_DRAW, { 0, 0, 1024, 1024 }, 0.0f, 0 },
    };

    static const struct draw test_partial_discard_depth[] =
    {
        { true, false, DRAW_TYPE_DISCARD },
        /* Promote stencil here. */
        { true, true, DRAW_TYPE_DRAW, { 0, 0, 1024, 1024 }, 0.0f, 0 },
    };

    static const struct draw test_partial_discard_stencil[] =
    {
        { false, true, DRAW_TYPE_DISCARD },
        /* Promote depth here. */
        { true, true, DRAW_TYPE_DRAW, { 0, 0, 1024, 1024 }, 0.0f, 0 },
    };

    static const struct draw test_decay[] =
    {
        { true, true, DRAW_TYPE_CLEAR, { 0, 0, 1024, 1024 }, 0.0f, 0 },
        /* This should decay the resource back to READ_ONLY. */
        { false, false, DRAW_TYPE_TRANSITION },
        { false, false, DRAW_TYPE_DRAW, { 0, 0, 1024, 1024 }, 0.0f, 0 },
    };

    static const struct draw test_decay_depth[] =
    {
        { true, true, DRAW_TYPE_CLEAR, { 0, 0, 1024, 1024 }, 0.0f, 128 },
        { false, true, DRAW_TYPE_TRANSITION },
        { false, true, DRAW_TYPE_DRAW, { 0, 0, 1024, 1024 }, 0.0f, 0 },
    };

    static const struct draw test_decay_stencil[] =
    {
        { true, true, DRAW_TYPE_CLEAR, { 0, 0, 1024, 1024 }, 0.5f, 0 },
        { true, false, DRAW_TYPE_TRANSITION },
        { true, false, DRAW_TYPE_DRAW, { 0, 0, 1024, 1024 }, 0.0f, 0 },
    };

    static const struct draw test_sub_clear_no_render_pass[] =
    {
        /* Both of these will be emitted as separate clear passes, but no UNDEFINED transition. */
        { true, true, DRAW_TYPE_CLEAR, { 0, 0, 1024, 512 }, 0.0f, 0 },
        { true, true, DRAW_TYPE_CLEAR, { 0, 512, 1024, 1024 }, 0.0f, 0 },
    };

    static const struct draw test_sub_clear_separate_no_render_pass[] =
    {
        /* Same as above, but separate layouts. */
        { true, false, DRAW_TYPE_CLEAR, { 0, 0, 1024, 512 }, 0.0f, 0 },
        { true, false, DRAW_TYPE_CLEAR, { 0, 512, 1024, 1024 }, 0.0f, 0 },
        { false, true, DRAW_TYPE_CLEAR, { 0, 0, 1024, 512 }, 0.0f, 0 },
        { false, true, DRAW_TYPE_CLEAR, { 0, 512, 1024, 1024 }, 0.0f, 0 },
    };

    static const struct draw test_sub_clear_after_discard[] =
    {
        /* Both of these will be emitted as separate clear passes, but no UNDEFINED transition. */
        { true, true, DRAW_TYPE_DISCARD },
        { true, true, DRAW_TYPE_CLEAR, { 0, 0, 1024, 512 }, 0.0f, 0 },
        { true, true, DRAW_TYPE_CLEAR, { 0, 512, 1024, 1024 }, 0.0f, 0 },
    };

    static const struct draw test_sub_clear_separate_after_discard[] =
    {
        /* Same as above, but separate layouts. */
        { true, false, DRAW_TYPE_DISCARD },
        { true, false, DRAW_TYPE_CLEAR, { 0, 0, 1024, 512 }, 0.0f, 0 },
        { true, false, DRAW_TYPE_CLEAR, { 0, 512, 1024, 1024 }, 0.0f, 0 },
        { false, true, DRAW_TYPE_DISCARD },
        { false, true, DRAW_TYPE_CLEAR, { 0, 0, 1024, 512 }, 0.0f, 0 },
        { false, true, DRAW_TYPE_CLEAR, { 0, 512, 1024, 1024 }, 0.0f, 0 },
    };

    static const struct draw test_clear_in_render_pass[] =
    {
        { true, true, DRAW_TYPE_DRAW, { 0, 0, 1024, 1024 }, 0.5f, 128 },
        /* No need to split render pass here and promote layout. */
        { true, true, DRAW_TYPE_CLEAR, { 0, 0, 1024, 1024 }, 0.0f, 0 },
    };

    static const struct draw test_clear_in_render_pass_promote[] =
    {
        { false, false, DRAW_TYPE_DRAW, { 0, 0, 1024, 1024 }, 0.5f, 128 },
        /* Need to split render pass here and promote layout. */
        { true, true, DRAW_TYPE_CLEAR, { 0, 0, 1024, 1024 }, 0.0f, 0 },
    };

    static const struct draw test_partial_clear_in_render_pass_promote[] =
    {
        { true, false, DRAW_TYPE_DRAW, { 0, 0, 1024, 1024 }, 0.0f, 128 },
        /* Need to split render pass here and promote layout. */
        { false, true, DRAW_TYPE_CLEAR, { 0, 0, 1024, 1024 }, 0.0f, 0 },
    };

    struct test
    {
        const struct draw *draws;
        unsigned int draw_count;
    };

    /* It's also useful to test this with validation layers on, since this is mostly a test to see if we handle
     * the layout transitions correctly. */
    static const struct test tests[] =
    {
        { test_full_promotion, ARRAY_SIZE(test_full_promotion) },
        { test_full_promotion_no_read, ARRAY_SIZE(test_full_promotion_no_read) },
        { test_partial_promotion, ARRAY_SIZE(test_partial_promotion) },
        { test_full_implicit_transition, ARRAY_SIZE(test_full_implicit_transition) },
        { test_full_explicit_transition, ARRAY_SIZE(test_full_explicit_transition) },
        { test_full_clear_transition, ARRAY_SIZE(test_full_clear_transition) },
        { test_full_discard_transition, ARRAY_SIZE(test_full_discard_transition) },
        { test_partial_transition_depth, ARRAY_SIZE(test_partial_transition_depth) },
        { test_partial_transition_stencil, ARRAY_SIZE(test_partial_transition_stencil) },
        { test_partial_clear_depth, ARRAY_SIZE(test_partial_clear_depth) },
        { test_partial_clear_stencil, ARRAY_SIZE(test_partial_clear_stencil) },
        { test_partial_discard_depth, ARRAY_SIZE(test_partial_discard_depth) },
        { test_partial_discard_stencil, ARRAY_SIZE(test_partial_discard_stencil) },
        { test_decay, ARRAY_SIZE(test_decay) },
        { test_decay_depth, ARRAY_SIZE(test_decay_depth) },
        { test_decay_stencil, ARRAY_SIZE(test_decay_stencil) },
        { test_sub_clear_no_render_pass, ARRAY_SIZE(test_sub_clear_no_render_pass) },
        { test_sub_clear_separate_no_render_pass, ARRAY_SIZE(test_sub_clear_separate_no_render_pass) },
        { test_sub_clear_after_discard, ARRAY_SIZE(test_sub_clear_after_discard) },
        { test_sub_clear_separate_after_discard, ARRAY_SIZE(test_sub_clear_separate_after_discard) },
        { test_clear_in_render_pass, ARRAY_SIZE(test_clear_in_render_pass) },
        { test_clear_in_render_pass_promote, ARRAY_SIZE(test_clear_in_render_pass_promote) },
        { test_partial_clear_in_render_pass_promote, ARRAY_SIZE(test_partial_clear_in_render_pass_promote) },
    };

    memset(&desc, 0, sizeof(desc));
    desc.no_render_target = true;
    if (!init_test_context(&context, &desc))
        return;

    init_depth_stencil(&ds, context.device, 1024, 1024, 1, 1,
            DXGI_FORMAT_R32G8X24_TYPELESS, DXGI_FORMAT_D32_FLOAT_S8X24_UINT, NULL);
    rs = create_32bit_constants_root_signature(context.device, 0, 1, D3D12_SHADER_VISIBILITY_VERTEX);

    init_pipeline_state_desc(&pso_desc, rs, 0, &vs, &ps, NULL);

    pso_desc.NumRenderTargets = 0;
    pso_desc.DSVFormat = DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
    pso_desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;

    for (i = 0; i < ARRAY_SIZE(psos); i++)
    {
        pso_desc.DepthStencilState.StencilEnable = TRUE;
        pso_desc.DepthStencilState.DepthEnable = TRUE;
        pso_desc.DepthStencilState.StencilReadMask = 0xFF;

        if (i >= 2)
        {
            pso_desc.DepthStencilState.StencilWriteMask = 0xFF;
            pso_desc.DepthStencilState.FrontFace.StencilFailOp = D3D12_STENCIL_OP_REPLACE;
            pso_desc.DepthStencilState.FrontFace.StencilDepthFailOp = D3D12_STENCIL_OP_REPLACE;
            pso_desc.DepthStencilState.FrontFace.StencilPassOp = D3D12_STENCIL_OP_REPLACE;
        }
        else
        {
            pso_desc.DepthStencilState.StencilWriteMask = 0x00;
            pso_desc.DepthStencilState.FrontFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
            pso_desc.DepthStencilState.FrontFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
            pso_desc.DepthStencilState.FrontFace.StencilPassOp = D3D12_STENCIL_OP_KEEP;
        }

        pso_desc.DepthStencilState.FrontFace.StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        pso_desc.DepthStencilState.BackFace = pso_desc.DepthStencilState.FrontFace;

        pso_desc.DepthStencilState.DepthWriteMask = (i & 1) ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
        pso_desc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;

        hr = ID3D12Device_CreateGraphicsPipelineState(context.device, &pso_desc,
                &IID_ID3D12PipelineState, (void **)&psos[i]);
        ok(SUCCEEDED(hr), "Failed to create graphics pipeline state, hr %#x.\n", hr);
    }

    /* In the tests, begin command lists from a clean slate.
     * Implementation must assume the depth-stencil image is in read-only state until proven otherwise. */
    for (i = 0; i < ARRAY_SIZE(tests); i++)
    {
        const D3D12_VIEWPORT vp = { 0, 0, 1024, 1024, 0, 1 };
        D3D12_RESOURCE_STATES stencil_state = D3D12_RESOURCE_STATE_DEPTH_WRITE;
        D3D12_RESOURCE_STATES depth_state = D3D12_RESOURCE_STATE_DEPTH_WRITE;
        D3D12_RESOURCE_STATES new_stencil_state;
        D3D12_RESOURCE_STATES new_depth_state;

        vkd3d_test_set_context("Test %u", i);

        /* Initialize the DS image to a known state. */
        ID3D12GraphicsCommandList_ClearDepthStencilView(context.list, ds.dsv_handle,
                D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL,
                1.0f, 255, 0, NULL);
        transition_resource_state(context.list, ds.texture, D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_COPY_SOURCE);
        check_sub_resource_float(ds.texture, 0, context.queue, context.list, 1.0f, 0);
        reset_command_list(context.list, context.allocator);
        check_sub_resource_uint8(ds.texture, 1, context.queue, context.list, 255, 0);
        reset_command_list(context.list, context.allocator);
        transition_resource_state(context.list, ds.texture, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE);
        ID3D12GraphicsCommandList_Close(context.list);
        exec_command_list(context.queue, context.list);
        wait_queue_idle(context.device, context.queue);
        reset_command_list(context.list, context.allocator);

        ID3D12GraphicsCommandList_OMSetRenderTargets(context.list, 0, NULL, FALSE, &ds.dsv_handle);
        ID3D12GraphicsCommandList_RSSetViewports(context.list, 1, &vp);
        ID3D12GraphicsCommandList_SetGraphicsRootSignature(context.list, rs);
        ID3D12GraphicsCommandList_IASetPrimitiveTopology(context.list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        for (j = 0; j < tests[i].draw_count; j++)
        {
            switch (tests[i].draws[j].type)
            {
                case DRAW_TYPE_DRAW:
                    ID3D12GraphicsCommandList_RSSetScissorRects(context.list, 1, &tests[i].draws[j].rect);
                    ID3D12GraphicsCommandList_SetPipelineState(context.list, psos[tests[i].draws[j].depth_write + tests[i].draws[j].stencil_write * 2]);
                    ID3D12GraphicsCommandList_SetGraphicsRoot32BitConstants(context.list, 0, 1, &tests[i].draws[j].z, 0);
                    ID3D12GraphicsCommandList_OMSetStencilRef(context.list, tests[i].draws[j].stencil);
                    ID3D12GraphicsCommandList_DrawInstanced(context.list, 3, 1, 0, 0);
                    break;

                case DRAW_TYPE_TRANSITION:
                    new_depth_state = tests[i].draws[j].depth_write ? D3D12_RESOURCE_STATE_DEPTH_WRITE :
                            (D3D12_RESOURCE_STATE_DEPTH_READ | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
                    new_stencil_state = tests[i].draws[j].stencil_write ? D3D12_RESOURCE_STATE_DEPTH_WRITE :
                            (D3D12_RESOURCE_STATE_DEPTH_READ | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

                    if (new_depth_state != depth_state)
                    {
                        transition_sub_resource_state(context.list, ds.texture, 0, depth_state, new_depth_state);
                        depth_state = new_depth_state;
                    }

                    if (new_stencil_state != stencil_state)
                    {
                        transition_sub_resource_state(context.list, ds.texture, 1, stencil_state, new_stencil_state);
                        stencil_state = new_stencil_state;
                    }
                    break;

                case DRAW_TYPE_CLEAR:
                    ID3D12GraphicsCommandList_ClearDepthStencilView(context.list, ds.dsv_handle,
                            (tests[i].draws[j].depth_write ? D3D12_CLEAR_FLAG_DEPTH : 0) |
                            (tests[i].draws[j].stencil_write ? D3D12_CLEAR_FLAG_STENCIL : 0),
                            tests[i].draws[j].z, tests[i].draws[j].stencil, 1, &tests[i].draws[j].rect);
                    break;

                case DRAW_TYPE_DISCARD:
                    region.NumRects = 0;
                    region.pRects = NULL;

                    if (tests[i].draws[j].depth_write && tests[i].draws[j].stencil_write)
                    {
                        region.FirstSubresource = 0;
                        region.NumSubresources = 2;
                    }
                    else if (tests[i].draws[j].depth_write)
                    {
                        region.FirstSubresource = 0;
                        region.NumSubresources = 1;
                    }
                    else
                    {
                        region.FirstSubresource = 1;
                        region.NumSubresources = 1;
                    }

                    ID3D12GraphicsCommandList_DiscardResource(context.list, ds.texture, &region);
                    break;
            }
        }

        /* Normalize the resource state back to DEPTH_WRITE. */
        if (depth_state != D3D12_RESOURCE_STATE_DEPTH_WRITE)
            transition_sub_resource_state(context.list, ds.texture, 0, depth_state, D3D12_RESOURCE_STATE_DEPTH_WRITE);
        if (stencil_state != D3D12_RESOURCE_STATE_DEPTH_WRITE)
            transition_sub_resource_state(context.list, ds.texture, 1, stencil_state, D3D12_RESOURCE_STATE_DEPTH_WRITE);

        transition_resource_state(context.list, ds.texture, D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_COPY_SOURCE);
        check_sub_resource_float(ds.texture, 0, context.queue, context.list, 0.0f, 0);
        reset_command_list(context.list, context.allocator);
        check_sub_resource_uint8(ds.texture, 1, context.queue, context.list, 0, 0);
        reset_command_list(context.list, context.allocator);
        transition_resource_state(context.list, ds.texture, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE);
        ID3D12GraphicsCommandList_Close(context.list);
        exec_command_list(context.queue, context.list);
        wait_queue_idle(context.device, context.queue);
        reset_command_list(context.list, context.allocator);
    }
    vkd3d_test_set_context(NULL);

    transition_resource_state(context.list, ds.texture, D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_COPY_SOURCE);
    check_sub_resource_float(ds.texture, 0, context.queue, context.list, 0.0f, 0);
    reset_command_list(context.list, context.allocator);
    check_sub_resource_uint8(ds.texture, 1, context.queue, context.list, 0, 0);
    reset_command_list(context.list, context.allocator);

    ID3D12RootSignature_Release(rs);
    for (i = 0; i < ARRAY_SIZE(psos); i++)
        ID3D12PipelineState_Release(psos[i]);
    destroy_depth_stencil(&ds);
    destroy_test_context(&context);
}

void test_dynamic_depth_stencil_write(void)
{
    enum { NUM_QUADS = 4 * 4 };
    enum { READ_ONLY = 0, DEPTH_WRITE = 1, STENCIL_WRITE = 2, WRITE = 3 };

    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc;
    D3D12_DEPTH_STENCIL_VIEW_DESC dsv_desc;
    D3D12_INPUT_LAYOUT_DESC input_layout;
    D3D12_INPUT_ELEMENT_DESC layout_elem;
    D3D12_CPU_DESCRIPTOR_HANDLE dsvs[4];
    D3D12_ROOT_SIGNATURE_DESC rs_desc;
    float vbo_data[NUM_QUADS][4][4];
    struct test_context_desc desc;
    ID3D12PipelineState *psos[4];
    D3D12_VERTEX_BUFFER_VIEW vbv;
    struct test_context context;
    struct resource_readback rb;
    ID3D12DescriptorHeap *heap;
    ID3D12Resource *vbo;
    ID3D12Resource *ds;
    D3D12_VIEWPORT vp;
    unsigned int i, j;
    D3D12_RECT rect;

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

    memset(&desc, 0, sizeof(desc));
    desc.no_pipeline = true;
    desc.no_render_target = true;
    desc.no_root_signature = true;

    if (!init_test_context(&context, &desc))
        return;

    memset(&rs_desc, 0, sizeof(rs_desc));
    rs_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    create_root_signature(context.device, &rs_desc, &context.root_signature);

    memset(&input_layout, 0, sizeof(input_layout));
    memset(&layout_elem, 0, sizeof(layout_elem));
    input_layout.NumElements = 1;
    input_layout.pInputElementDescs = &layout_elem;
    layout_elem.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    layout_elem.InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
    layout_elem.SemanticName = "POSITION";

    init_pipeline_state_desc_shaders(&pso_desc, context.root_signature, DXGI_FORMAT_UNKNOWN,
            &input_layout, vs_code, sizeof(vs_code), NULL, 0);
    pso_desc.DSVFormat = DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
    pso_desc.DepthStencilState.DepthEnable = TRUE;
    pso_desc.DepthStencilState.StencilEnable = TRUE;
    pso_desc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    pso_desc.DepthStencilState.FrontFace.StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    pso_desc.DepthStencilState.FrontFace.StencilFailOp = D3D12_STENCIL_OP_REPLACE;
    pso_desc.DepthStencilState.FrontFace.StencilPassOp = D3D12_STENCIL_OP_REPLACE;
    pso_desc.DepthStencilState.FrontFace.StencilDepthFailOp = D3D12_STENCIL_OP_REPLACE;
    pso_desc.DepthStencilState.BackFace = pso_desc.DepthStencilState.FrontFace;
    pso_desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;

    heap = create_cpu_descriptor_heap(context.device, D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 4);
    ds = create_default_texture2d(context.device, 4, 4, 1, 1, DXGI_FORMAT_D32_FLOAT_S8X24_UINT, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL, D3D12_RESOURCE_STATE_DEPTH_WRITE);

    for (i = 0; i < ARRAY_SIZE(psos); i++)
    {
        D3D12_CPU_DESCRIPTOR_HANDLE h;
        pso_desc.DepthStencilState.DepthWriteMask = (i & 1) ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
        pso_desc.DepthStencilState.StencilWriteMask = (i & 2) ? 0xff : 0;
        ID3D12Device_CreateGraphicsPipelineState(context.device, &pso_desc, &IID_ID3D12PipelineState, (void **)&psos[i]);

        memset(&dsv_desc, 0, sizeof(dsv_desc));
        dsv_desc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
        dsv_desc.Format = DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
        dsv_desc.Flags |= (i & 1) == 0 ? D3D12_DSV_FLAG_READ_ONLY_DEPTH : 0;
        dsv_desc.Flags |= (i & 2) == 0 ? D3D12_DSV_FLAG_READ_ONLY_STENCIL : 0;
        h = ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(heap);
        h.ptr += ID3D12Device_GetDescriptorHandleIncrementSize(context.device, D3D12_DESCRIPTOR_HEAP_TYPE_DSV) * i;
        ID3D12Device_CreateDepthStencilView(context.device, ds, &dsv_desc, h);
        dsvs[i] = h;
    }

    /* Verify if DSV read-only state affects clear operations. */
    ID3D12GraphicsCommandList_ClearDepthStencilView(context.list, dsvs[WRITE], D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 0.5f, 0x80, 0, NULL);
    set_rect(&rect, 0, 0, 4, 1);
    ID3D12GraphicsCommandList_ClearDepthStencilView(context.list, dsvs[READ_ONLY], D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 0.25f, 0x70, 1, &rect);
    set_rect(&rect, 0, 1, 4, 2);
    ID3D12GraphicsCommandList_ClearDepthStencilView(context.list, dsvs[DEPTH_WRITE], D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 0.125f, 0x10, 1, &rect);
    set_rect(&rect, 0, 2, 4, 3);
    ID3D12GraphicsCommandList_ClearDepthStencilView(context.list, dsvs[STENCIL_WRITE], D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 0.75f, 0x90, 1, &rect);

    transition_resource_state(context.list, ds, D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_COPY_SOURCE);

    /* Verify that read-only state does not affect ClearDSV. */
    get_texture_readback_with_command_list(ds, 0, &rb, context.queue, context.list);
    {
        static const float expected_depth[] =
        {
            0.25f, 0.25f, 0.25f, 0.25f,
            0.125f, 0.125f, 0.125f, 0.125f,
            0.75f, 0.75f, 0.75f, 0.75f,
            0.5f, 0.5f, 0.5f, 0.5f,
        };
        float value, expected;
        unsigned int x, y;

        for (y = 0; y < 4; y++)
        {
            for (x = 0; x < 4; x++)
            {
                value = get_readback_float(&rb, x, y);
                expected = expected_depth[y * 4 + x];
                ok(expected == value, "Depth pixel %u, %u mismatch, expected %f, got %f.\n", x, y, expected, value);
            }
        }
    }
    release_resource_readback(&rb);
    reset_command_list(context.list, context.allocator);
    get_texture_readback_with_command_list(ds, 1, &rb, context.queue, context.list);
    {
        static const uint8_t expected_stencil[] =
        {
            0x70, 0x70, 0x70, 0x70,
            0x10, 0x10, 0x10, 0x10,
            0x90, 0x90, 0x90, 0x90,
            0x80, 0x80, 0x80, 0x80,
        };
        uint8_t value, expected;
        unsigned int x, y;

        for (y = 0; y < 4; y++)
        {
            for (x = 0; x < 4; x++)
            {
                value = get_readback_uint8(&rb, x, y);
                expected = expected_stencil[y * 4 + x];
                ok(expected == value, "Stencil pixel %u, %u mismatch, expected %u, got %u.\n", x, y, expected, value);
            }
        }
    }
    release_resource_readback(&rb);

    reset_command_list(context.list, context.allocator);
    transition_resource_state(context.list, ds, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE);
    ID3D12GraphicsCommandList_ClearDepthStencilView(context.list, dsvs[WRITE], D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, NULL);

    for (i = 0; i < NUM_QUADS; i++)
    {
        for (j = 0; j < 4; j++)
        {
            vbo_data[i][j][0] = (j & 1) ? +1.0f : -1.0f;
            vbo_data[i][j][1] = (j & 2) ? +1.0f : -1.0f;
            vbo_data[i][j][2] = (float)i / 256.0f;
            vbo_data[i][j][3] = 1.0f;
        }
    }

    vbo = create_upload_buffer(context.device, sizeof(vbo_data), vbo_data);
    vbv.BufferLocation = ID3D12Resource_GetGPUVirtualAddress(vbo);
    vbv.SizeInBytes = sizeof(vbo_data);
    vbv.StrideInBytes = 4 * sizeof(float);

    ID3D12GraphicsCommandList_SetGraphicsRootSignature(context.list, context.root_signature);
    ID3D12GraphicsCommandList_IASetPrimitiveTopology(context.list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    ID3D12GraphicsCommandList_IASetVertexBuffers(context.list, 0, 1, &vbv);

    set_rect(&rect, 0, 0, 4, 4);
    ID3D12GraphicsCommandList_RSSetScissorRects(context.list, 1, &rect);

    for (i = 0; i < NUM_QUADS; i++)
    {
        unsigned int x, y;
        x = i % 4;
        y = i / 4;
        set_viewport(&vp, x, y, 1, 1, 0, 1);
        ID3D12GraphicsCommandList_RSSetViewports(context.list, 1, &vp);

        ID3D12GraphicsCommandList_OMSetRenderTargets(context.list, 0, NULL, FALSE, &dsvs[y]);
        ID3D12GraphicsCommandList_SetPipelineState(context.list, psos[x]);
        ID3D12GraphicsCommandList_OMSetStencilRef(context.list, i + 1);
        ID3D12GraphicsCommandList_DrawInstanced(context.list, 4, 1, 4 * i, 0);
    }

    /* Read-write state of the DSV *does* matter when rendering however! We have to dynamically disable writes. */
    transition_resource_state(context.list, ds, D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_COPY_SOURCE);
    get_texture_readback_with_command_list(ds, 0, &rb, context.queue, context.list);
    {
        float value, expected;
        unsigned int x, y;

        for (y = 0; y < 4; y++)
        {
            for (x = 0; x < 4; x++)
            {
                value = get_readback_float(&rb, x, y);
                /* Write happens if both PSO and DSV enable write. */
                expected = (x & DEPTH_WRITE) && (y & DEPTH_WRITE) ? (float)(y * 4 + x) / 256.0f : 1.0f;
                ok(expected == value, "Depth pixel %u, %u mismatch, expected %f, got %f.\n", x, y, expected, value);
            }
        }
    }

    release_resource_readback(&rb);
    reset_command_list(context.list, context.allocator);
    get_texture_readback_with_command_list(ds, 1, &rb, context.queue, context.list);
    {
        uint8_t value, expected;
        unsigned int x, y;

        for (y = 0; y < 4; y++)
        {
            for (x = 0; x < 4; x++)
            {
                value = get_readback_uint8(&rb, x, y);
                expected = (x & STENCIL_WRITE) && (y & STENCIL_WRITE) ? (y * 4 + x + 1) : 0;
                ok(expected == value, "Stencil pixel %u, %u mismatch, expected %u, got %u.\n", x, y, expected, value);
            }
        }
    }
    release_resource_readback(&rb);

    for (i = 0; i < ARRAY_SIZE(psos); i++)
        ID3D12PipelineState_Release(psos[i]);

    ID3D12Resource_Release(vbo);
    ID3D12Resource_Release(ds);
    ID3D12DescriptorHeap_Release(heap);
    destroy_test_context(&context);
}

void test_depth_stencil_front_and_back(void)
{
    D3D12_CPU_DESCRIPTOR_HANDLE dsv_handle, rtv_handle;
    D3D12_FEATURE_DATA_D3D12_OPTIONS14 options14;
    ID3D12GraphicsCommandList8 *command_list8;
    ID3D12DescriptorHeap *rtv_heap, *dsv_heap;
    D3D12_DEPTH_STENCIL_VIEW_DESC dsv_desc;
    D3D12_RENDER_TARGET_VIEW_DESC rtv_desc;
    ID3D12PipelineState *pso_stencil_write;
    ID3D12PipelineState *pso_stencil_read;
    D3D12_ROOT_SIGNATURE_DESC rs_desc;
    struct test_context_desc desc;
    D3D12_VERTEX_BUFFER_VIEW vbv;
    struct test_context context;
    float triangles[2][3][4];
    ID3D12Resource *ds, *rt;
    D3D12_VIEWPORT viewport;
    ID3D12Device2 *device2;
    ID3D12Resource *vbo;
    D3D12_RECT scissor;
    unsigned int i;
    HRESULT hr;

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

    static const DWORD ps_code[] =
    {
#if 0
    float4 main(bool is_front_face : SV_IsFrontFace) : SV_TARGET
    {
        return is_front_face ? float4(1.0f, 0.0f, 0.0f, 0.0f) : float4(0.0f, 1.0f, 0.0f, 0.0f);
    }
#endif
        0x43425844, 0xfc6882f9, 0xb7f43ec2, 0x96a536df, 0xeb15543c, 0x00000001, 0x00000108, 0x00000003,
        0x0000002c, 0x00000064, 0x00000098, 0x4e475349, 0x00000030, 0x00000001, 0x00000008, 0x00000020,
        0x00000000, 0x00000009, 0x00000001, 0x00000000, 0x00000101, 0x495f5653, 0x6f724673, 0x6146746e,
        0xab006563, 0x4e47534f, 0x0000002c, 0x00000001, 0x00000008, 0x00000020, 0x00000000, 0x00000000,
        0x00000003, 0x00000000, 0x0000000f, 0x545f5653, 0x45475241, 0xabab0054, 0x58454853, 0x00000068,
        0x00000050, 0x0000001a, 0x0100086a, 0x04000863, 0x00101012, 0x00000000, 0x00000009, 0x03000065,
        0x001020f2, 0x00000000, 0x0f000037, 0x001020f2, 0x00000000, 0x00101006, 0x00000000, 0x00004002,
        0x3f800000, 0x00000000, 0x00000000, 0x00000000, 0x00004002, 0x00000000, 0x3f800000, 0x00000000,
        0x00000000, 0x0100003e,
    };

    static const union d3d12_root_signature_subobject root_signature_subobject =
    {{
        D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_ROOT_SIGNATURE,
        NULL, /* fill in dynamically */
    }};

    static const union d3d12_shader_bytecode_subobject vs_subobject = {{ D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VS, { vs_code, sizeof(vs_code) } }};
    static const union d3d12_shader_bytecode_subobject ps_subobject = {{ D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS, { ps_code, sizeof(ps_code) } }};

    static const union d3d12_sample_mask_subobject sample_mask_subobject =
    {{
        D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_MASK,
        0xFFFFFFFFu
    }};

    static const union d3d12_blend_subobject blend_write_subobject =
    {{
        D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_BLEND,
        { FALSE, FALSE },
    }};

    static const union d3d12_blend_subobject blend_read_subobject =
    {{
        D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_BLEND,
        { FALSE, TRUE,
            {{ TRUE, FALSE,
                D3D12_BLEND_ONE, D3D12_BLEND_ONE, D3D12_BLEND_OP_ADD,
                D3D12_BLEND_ONE, D3D12_BLEND_ONE, D3D12_BLEND_OP_ADD,
                D3D12_LOGIC_OP_NOOP, 0xF }},
        }
    }};

    static const union d3d12_rasterizer_subobject rasterizer_subobject =
    {{
        D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RASTERIZER,
        { D3D12_FILL_MODE_SOLID, D3D12_CULL_MODE_NONE,
            FALSE, 0, 0.0f, 0.0f, TRUE, FALSE, FALSE, 0,
            D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF },
    }};

    static const union d3d12_depth_stencil2_subobject depth_stencil_write_subobject =
    {{
        D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL2,
        { FALSE, D3D12_DEPTH_WRITE_MASK_ZERO, D3D12_COMPARISON_FUNC_ALWAYS, TRUE,
            { D3D12_STENCIL_OP_REPLACE, D3D12_STENCIL_OP_REPLACE, D3D12_STENCIL_OP_REPLACE, D3D12_COMPARISON_FUNC_ALWAYS, 0x0F, 0x0F },
            { D3D12_STENCIL_OP_REPLACE, D3D12_STENCIL_OP_REPLACE, D3D12_STENCIL_OP_REPLACE, D3D12_COMPARISON_FUNC_ALWAYS, 0xF0, 0xF0 } },
    }};

    static const union d3d12_depth_stencil2_subobject depth_stencil_read_subobject =
    {{
        D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL2,
        { FALSE, D3D12_DEPTH_WRITE_MASK_ZERO, D3D12_COMPARISON_FUNC_ALWAYS, TRUE,
            { D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP, D3D12_COMPARISON_FUNC_EQUAL, 0x0F, 0x00 },
            { D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP, D3D12_COMPARISON_FUNC_EQUAL, 0xF0, 0x00 } },
    }};

    static const D3D12_INPUT_ELEMENT_DESC input_elements[] =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };

    static const union d3d12_input_layout_subobject input_layout_subobject =
    {{
        D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_INPUT_LAYOUT,
        { input_elements, ARRAY_SIZE(input_elements) },
    }};

    static const union d3d12_ib_strip_cut_value_subobject ib_strip_cut_value_subobject =
    {{
        D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_IB_STRIP_CUT_VALUE,
        D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_DISABLED,
    }};

    static const union d3d12_primitive_topology_subobject primitive_topology_subobject =
    {{
        D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PRIMITIVE_TOPOLOGY,
        D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE,
    }};

    static const union d3d12_render_target_formats_subobject render_target_formats_write_subobject =
    {{
        D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RENDER_TARGET_FORMATS,
        { { DXGI_FORMAT_UNKNOWN }, 0 },
    }};

    static const union d3d12_render_target_formats_subobject render_target_formats_read_subobject =
    {{
        D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RENDER_TARGET_FORMATS,
        { { DXGI_FORMAT_R8G8B8A8_UNORM }, 1 },
    }};

    static const union d3d12_depth_stencil_format_subobject depth_stencil_format_subobject =
    {{
        D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL_FORMAT,
        DXGI_FORMAT_D32_FLOAT_S8X24_UINT,
    }};

    static const union d3d12_sample_desc_subobject sample_desc_subobject =
    {{
        D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_DESC,
        { 1, 0 },
    }};

    struct
    {
        union d3d12_root_signature_subobject root_signature;
        union d3d12_shader_bytecode_subobject vertex_shader;
        union d3d12_blend_subobject blend;
        union d3d12_sample_mask_subobject sample_mask;
        union d3d12_rasterizer_subobject rasterizer;
        union d3d12_depth_stencil2_subobject depth_stencil;
        union d3d12_input_layout_subobject input_layout;
        union d3d12_ib_strip_cut_value_subobject strip_cut;
        union d3d12_primitive_topology_subobject primitive_topology;
        union d3d12_render_target_formats_subobject render_target_formats;
        union d3d12_depth_stencil_format_subobject depth_stencil_format;
        union d3d12_sample_desc_subobject sample_desc;
    }
    pso_desc_write_stencil =
    {
        root_signature_subobject,
        vs_subobject,
        blend_write_subobject,
        sample_mask_subobject,
        rasterizer_subobject,
        depth_stencil_write_subobject,
        input_layout_subobject,
        ib_strip_cut_value_subobject,
        primitive_topology_subobject,
        render_target_formats_write_subobject,
        depth_stencil_format_subobject,
        sample_desc_subobject,
    };

    struct
    {
        union d3d12_root_signature_subobject root_signature;
        union d3d12_shader_bytecode_subobject vertex_shader;
        union d3d12_shader_bytecode_subobject pixel_shader;
        union d3d12_blend_subobject blend;
        union d3d12_sample_mask_subobject sample_mask;
        union d3d12_rasterizer_subobject rasterizer;
        union d3d12_depth_stencil2_subobject depth_stencil;
        union d3d12_input_layout_subobject input_layout;
        union d3d12_ib_strip_cut_value_subobject strip_cut;
        union d3d12_primitive_topology_subobject primitive_topology;
        union d3d12_render_target_formats_subobject render_target_formats;
        union d3d12_depth_stencil_format_subobject depth_stencil_format;
        union d3d12_sample_desc_subobject sample_desc;
    }
    pso_desc_read_stencil =
    {
        root_signature_subobject,
        vs_subobject,
        ps_subobject,
        blend_read_subobject,
        sample_mask_subobject,
        rasterizer_subobject,
        depth_stencil_read_subobject,
        input_layout_subobject,
        ib_strip_cut_value_subobject,
        primitive_topology_subobject,
        render_target_formats_read_subobject,
        depth_stencil_format_subobject,
        sample_desc_subobject,
    };

    const D3D12_PIPELINE_STATE_STREAM_DESC pso_stream_write_stencil = { sizeof(pso_desc_write_stencil), &pso_desc_write_stencil };
    const D3D12_PIPELINE_STATE_STREAM_DESC pso_stream_read_stencil = { sizeof(pso_desc_read_stencil), &pso_desc_read_stencil };

    static const struct
    {
        uint8_t stencil_ref_front;
        uint8_t stencil_ref_back;
        bool use_two_sided;
        uint32_t expected_color;
    }
    tests[] =
    {
        { 0x55, 0x00, false, 0x000000ff },
        { 0xaa, 0x00, false, 0x0000ff00 },
        { 0xff, 0xff, true,  0x00000000 },
        { 0x55, 0x55, true,  0x000000ff },
        { 0xaa, 0xaa, true,  0x0000ff00 },
        { 0x55, 0xaa, true,  0x0000ffff },
    };

    memset(&desc, 0, sizeof(desc));
    desc.no_pipeline = true;
    desc.no_render_target = true;
    desc.no_root_signature = true;

    if (!init_test_context(&context, &desc))
        return;

    memset(&options14, 0, sizeof(options14));
    ID3D12Device_CheckFeatureSupport(context.device, D3D12_FEATURE_D3D12_OPTIONS14, &options14, sizeof(options14));

    if (!options14.IndependentFrontAndBackStencilRefMaskSupported)
    {
        skip("IndependentFrontAndBackStencilRefMaskSupported not supported.\n");
        destroy_test_context(&context);
        return;
    }

    if (FAILED(ID3D12Device_QueryInterface(context.device, &IID_ID3D12Device2, (void **)&device2)))
    {
        skip("ID3D12Device2 not supported.\n");
        destroy_test_context(&context);
        return;
    }

    if (FAILED(ID3D12GraphicsCommandList_QueryInterface(context.list, &IID_ID3D12GraphicsCommandList8, (void **)&command_list8)))
    {
        skip("ID3D12GraphicsCommandList8 not supported.\n");
        ID3D12Device2_Release(device2);
        destroy_test_context(&context);
        return;
    }

    memset(&rs_desc, 0, sizeof(rs_desc));
    rs_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    create_root_signature(context.device, &rs_desc, &context.root_signature);

    pso_desc_read_stencil.root_signature.root_signature = context.root_signature;
    pso_desc_write_stencil.root_signature.root_signature = context.root_signature;

    hr = ID3D12Device2_CreatePipelineState(device2, &pso_stream_write_stencil, &IID_ID3D12PipelineState, (void **)&pso_stencil_write);
    ok(hr == S_OK, "Failed to create pipeline state, hr %#x.\n", hr);
    hr = ID3D12Device2_CreatePipelineState(device2, &pso_stream_read_stencil, &IID_ID3D12PipelineState, (void **)&pso_stencil_read);
    ok(hr == S_OK, "Failed to create pipeline state, hr %#x.\n", hr);

    for (i = 0; i < 3; i++)
    {
        /* Front-facing triangle */
        triangles[0][i][0] = i == 1 ? 3.0f : -1.0f;
        triangles[0][i][1] = i == 2 ? 3.0f : -1.0f;
        triangles[0][i][2] = 0.0f;
        triangles[0][i][3] = 1.0f;

        /* Back-facing triangle */
        triangles[1][i][0] = i == 2 ? 3.0f : -1.0f;
        triangles[1][i][1] = i == 1 ? 3.0f : -1.0f;
        triangles[1][i][2] = 0.0f;
        triangles[1][i][3] = 1.0f;
    }

    vbo = create_upload_buffer(context.device, sizeof(triangles), triangles);
    vbv.BufferLocation = ID3D12Resource_GetGPUVirtualAddress(vbo);
    vbv.SizeInBytes = sizeof(triangles);
    vbv.StrideInBytes = 4 * sizeof(float);

    dsv_heap = create_cpu_descriptor_heap(context.device, D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 1);
    rtv_heap = create_cpu_descriptor_heap(context.device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1);

    ds = create_default_texture2d(context.device, 4, 4, 1, 1, DXGI_FORMAT_D32_FLOAT_S8X24_UINT, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL, D3D12_RESOURCE_STATE_DEPTH_WRITE);
    rt = create_default_texture2d(context.device, 4, 4, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);

    memset(&dsv_desc, 0, sizeof(dsv_desc));
    dsv_desc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    dsv_desc.Format = DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
    dsv_handle = ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(dsv_heap);
    ID3D12Device_CreateDepthStencilView(context.device, ds, &dsv_desc, dsv_handle);

    memset(&rtv_desc, 0, sizeof(rtv_desc));
    rtv_desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
    rtv_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    rtv_handle = ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(rtv_heap);
    ID3D12Device_CreateRenderTargetView(context.device, rt, &rtv_desc, rtv_handle);

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

    /* This should write 0xa5 to the stencil buffer due to the write masks also being different per face */
    ID3D12GraphicsCommandList8_ClearDepthStencilView(command_list8, dsv_handle, D3D12_CLEAR_FLAG_DEPTH, 0.0f, 0, 0, NULL);
    ID3D12GraphicsCommandList8_OMSetRenderTargets(command_list8, 0, NULL, false, &dsv_handle);
    ID3D12GraphicsCommandList8_OMSetFrontAndBackStencilRef(command_list8, 0x55, 0xaa);
    ID3D12GraphicsCommandList8_SetGraphicsRootSignature(command_list8, context.root_signature);
    ID3D12GraphicsCommandList8_SetPipelineState(command_list8, pso_stencil_write);
    ID3D12GraphicsCommandList8_IASetPrimitiveTopology(command_list8, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D12GraphicsCommandList8_IASetVertexBuffers(command_list8, 0, 1, &vbv);
    ID3D12GraphicsCommandList8_RSSetViewports(command_list8, 1, &viewport);
    ID3D12GraphicsCommandList8_RSSetScissorRects(command_list8, 1, &scissor);

    ID3D12GraphicsCommandList8_DrawInstanced(command_list8, 6, 1, 0, 0);

    for (i = 0; i < ARRAY_SIZE(tests); i++)
    {
        static const FLOAT black[] = { 0.0f, 0.0f, 0.0f, 0.0f };

        transition_resource_state(context.list, rt, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);

        ID3D12GraphicsCommandList8_ClearRenderTargetView(command_list8, rtv_handle, black, 0, NULL);
        ID3D12GraphicsCommandList8_OMSetRenderTargets(command_list8, 1, &rtv_handle, false, &dsv_handle);
        ID3D12GraphicsCommandList8_SetGraphicsRootSignature(command_list8, context.root_signature);
        ID3D12GraphicsCommandList8_SetPipelineState(command_list8, pso_stencil_read);
        ID3D12GraphicsCommandList8_IASetPrimitiveTopology(command_list8, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ID3D12GraphicsCommandList8_IASetVertexBuffers(command_list8, 0, 1, &vbv);
        ID3D12GraphicsCommandList8_RSSetViewports(command_list8, 1, &viewport);
        ID3D12GraphicsCommandList8_RSSetScissorRects(command_list8, 1, &scissor);

        if (tests[i].use_two_sided)
            ID3D12GraphicsCommandList8_OMSetFrontAndBackStencilRef(command_list8, tests[i].stencil_ref_front, tests[i].stencil_ref_back);
        else
            ID3D12GraphicsCommandList8_OMSetStencilRef(command_list8, tests[i].stencil_ref_front);

        ID3D12GraphicsCommandList8_DrawInstanced(command_list8, 6, 1, 0, 0);

        transition_resource_state(context.list, rt, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
        check_sub_resource_uint(rt, 0, context.queue, context.list, tests[i].expected_color, 0);

        reset_command_list(context.list, context.allocator);
    }

    ID3D12DescriptorHeap_Release(dsv_heap);
    ID3D12DescriptorHeap_Release(rtv_heap);

    ID3D12Resource_Release(ds);
    ID3D12Resource_Release(rt);
    ID3D12Resource_Release(vbo);

    ID3D12PipelineState_Release(pso_stencil_write);
    ID3D12PipelineState_Release(pso_stencil_read);

    ID3D12GraphicsCommandList8_Release(command_list8);
    ID3D12Device2_Release(device2);
    destroy_test_context(&context);
}
