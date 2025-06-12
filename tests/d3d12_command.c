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

#include "shaders/command/headers/multiple_render_targets.h"

    memset(&desc, 0, sizeof(desc));
    desc.rt_format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    desc.rt_descriptor_count = ARRAY_SIZE(rtvs);
    desc.no_pipeline = true;
    if (!init_test_context(&context, &desc))
        return;
    command_list = context.list;
    queue = context.queue;

    init_pipeline_state_desc(&pso_desc, context.root_signature, 0, NULL, &multiple_render_targets_dxbc, NULL);
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

#include "shaders/command/headers/fractional_viewports_vs.h"
#include "shaders/command/headers/fractional_viewports_ps.h"

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
            context.root_signature, desc.rt_format,
            &fractional_viewports_vs_dxbc, &fractional_viewports_ps_dxbc, &input_layout);

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

void test_negative_viewports(void)
{
    static const float white[] = {1.0f, 1.0f, 1.0f, 1.0f};
    D3D12_FEATURE_DATA_D3D12_OPTIONS13 options13;
    ID3D12GraphicsCommandList *command_list;
    D3D12_INPUT_LAYOUT_DESC input_layout;
    struct test_context_desc desc;
    D3D12_VERTEX_BUFFER_VIEW vbv;
    struct test_context context;
    struct resource_readback rb;
    ID3D12CommandQueue *queue;
    D3D12_VIEWPORT viewport;
    ID3D12Device *device;
    ID3D12Resource *vb;
    unsigned int x, y;
    HRESULT hr;

#include "shaders/command/headers/negative_viewports_vs.h"
#include "shaders/command/headers/negative_viewports_ps.h"

    static const D3D12_INPUT_ELEMENT_DESC layout_desc[] =
    {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 16, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    };
    static const struct
    {
        struct vec4 position;
        struct vec2 texcoord;
    }
    quad[] =
    {
        {{-1.0f,  1.0f, 0.0f, 1.0f}, {0.0f, 1.0f}},
        {{-1.0f, -1.0f, 1.0f, 1.0f}, {0.0f, 0.0f}},
        {{ 1.0f,  1.0f, 0.0f, 1.0f}, {1.0f, 1.0f}},
        {{ 1.0f, -1.0f, 1.0f, 1.0f}, {1.0f, 0.0f}},
    };

    memset(&desc, 0, sizeof(desc));
    desc.rt_format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    desc.no_root_signature = true;
    if (!init_test_context(&context, &desc))
        return;
    command_list = context.list;
    queue = context.queue;
    device = context.device;

    memset(&options13, 0, sizeof(options13));
    hr = ID3D12Device_CheckFeatureSupport(device, D3D12_FEATURE_D3D12_OPTIONS13, &options13, sizeof(options13));
    ok(SUCCEEDED(hr), "OPTIONS13 is not supported by runtime.\n");

    if (!options13.InvertedViewportHeightFlipsYSupported)
    {
        skip("Device does not support negative viewport height.\n");
        destroy_test_context(&context);
        return;
    }

    context.root_signature = create_empty_root_signature(context.device,
            D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

    input_layout.pInputElementDescs = layout_desc;
    input_layout.NumElements = ARRAY_SIZE(layout_desc);

    context.pipeline_state = create_pipeline_state(context.device,
            context.root_signature, desc.rt_format,
            &negative_viewports_vs_dxbc, &negative_viewports_ps_dxbc, &input_layout);

    vb = create_upload_buffer(context.device, sizeof(quad), quad);

    vbv.BufferLocation = ID3D12Resource_GetGPUVirtualAddress(vb);
    vbv.StrideInBytes = sizeof(quad[0]);
    vbv.SizeInBytes = sizeof(quad);

    ID3D12GraphicsCommandList_ClearRenderTargetView(command_list, context.rtv, white, 0, NULL);

    ID3D12GraphicsCommandList_OMSetRenderTargets(command_list, 1, &context.rtv, false, NULL);
    ID3D12GraphicsCommandList_SetGraphicsRootSignature(command_list, context.root_signature);
    ID3D12GraphicsCommandList_SetPipelineState(command_list, context.pipeline_state);
    ID3D12GraphicsCommandList_IASetPrimitiveTopology(command_list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    ID3D12GraphicsCommandList_RSSetScissorRects(command_list, 1, &context.scissor_rect);

    for (y = 0; y < context.render_target_desc.Height; y += context.render_target_desc.Height / 2)
    {
        for (x = 0; x < context.render_target_desc.Width; x += context.render_target_desc.Width / 2)
        {
            set_viewport(&viewport, x, context.render_target_desc.Height - y,
                context.render_target_desc.Width / 2, -(float)(context.render_target_desc.Height / 2), 1.0f, 0.0f);
            ID3D12GraphicsCommandList_RSSetViewports(command_list, 1, &viewport);
            ID3D12GraphicsCommandList_IASetVertexBuffers(command_list, 0, 1, &vbv);
            ID3D12GraphicsCommandList_DrawInstanced(command_list, 4, 1, 0, 0);
        }
    }

    transition_resource_state(command_list, context.render_target,
            D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);

    get_texture_readback_with_command_list(context.render_target, 0, &rb, queue, command_list);
    for (y = 0; y < rb.height; ++y)
    {
        for (x = 0; x < rb.width; ++x)
        {
            const struct vec4 *actual = get_readback_vec4(&rb, x, y);
            struct vec2 expected_fragcoord = {x + 0.5f, y + 0.5f};
            float expected_texcoord_y = (((y % (rb.height / 2)) * 2.0f + 1.0f) / rb.height);
            float expected_depth = expected_texcoord_y;
            ok(compare_float(actual->x, expected_fragcoord.x, 0) && compare_float(actual->y, expected_fragcoord.y, 0),
                    "Got fragcoord {%.8e, %.8e}, expected {%.8e, %.8e} at (%u, %u).\n",
                    actual->x, actual->y, expected_fragcoord.x, expected_fragcoord.y, x, y);
            ok(compare_float(actual->z, expected_depth, 2),
                    "Got depth {%.2f}, expected {%.2f} at (%u, %u).\n",
                    actual->z, expected_depth, x, y);
            ok(compare_float(actual->w, expected_texcoord_y, 2),
                    "Got texcoord {%.2f}, expected {%.2f} at (%u, %u).\n",
                    actual->w, expected_texcoord_y, x, y);
        }
    }
    release_resource_readback(&rb);

    reset_command_list(command_list, context.allocator);

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

#include "shaders/command/headers/scissor.h"

    static const float red[] = {1.0f, 0.0f, 0.0f, 1.0f};

    memset(&desc, 0, sizeof(desc));
    desc.rt_width = 640;
    desc.rt_height = 480;
    desc.ps = &scissor_dxbc;
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

#include "shaders/command/headers/draw_depth_no_ps.h"

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
    init_pipeline_state_desc(&pso_desc, context.root_signature, 0, &draw_depth_no_ps_dxbc, NULL, &input_layout);
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

#include "shaders/command/headers/draw_depth_only.h"

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
    init_pipeline_state_desc(&pso_desc, context.root_signature, 0, NULL, &draw_depth_only_dxbc, NULL);
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

#include "shaders/command/headers/draw_uav_only.h"

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

    context.pipeline_state = create_pipeline_state(context.device, context.root_signature, 0, NULL, &draw_uav_only_dxbc, NULL);

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

    ID3D12GraphicsCommandList_Release(bundle);
    ID3D12CommandAllocator_Release(bundle_allocator);
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

#include "shaders/command/headers/null_vbv_vs.h"
#include "shaders/command/headers/null_vbv_ps.h"

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
            context.root_signature, context.render_target_desc.Format,
            &null_vbv_vs_dxbc, &null_vbv_ps_dxbc, &input_layout);

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

#include "shaders/command/headers/vbv_stride_edge_cases.h"

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

    init_pipeline_state_desc(&pso_desc, context.root_signature, DXGI_FORMAT_UNKNOWN, &vbv_stride_edge_cases_dxbc, NULL, &input_layout);
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
        if (tests[i].stride && tests[i].stride < 16)
            vkd3d_mute_validation_message("06209", "Intentionally testing murky D3D12 behavior");
        ID3D12GraphicsCommandList_DrawInstanced(context.list, 2, 1, 0, 0);
        if (tests[i].stride && tests[i].stride < 16)
            vkd3d_unmute_validation_message("06209");
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

void test_execute_indirect_multi_dispatch_root_descriptors(void)
{
    const unsigned int max_indirect_count = 5;
    D3D12_INDIRECT_ARGUMENT_DESC arguments[2];
    D3D12_ROOT_PARAMETER root_params[1];
    ID3D12CommandSignature *signature;
    D3D12_COMMAND_SIGNATURE_DESC desc;
    D3D12_ROOT_SIGNATURE_DESC rs_desc;
    struct resource_readback rb;
    struct test_context context;
    ID3D12Resource *indirect;
    unsigned int test_index;
    ID3D12Resource *output;
    unsigned int iteration;
    unsigned int i;
    HRESULT hr;

    struct test_data
    {
        uint32_t dispatch_counts[4];
        struct
        {
            D3D12_GPU_VIRTUAL_ADDRESS va;
            uint32_t wg_x, wg_y, wg_z;
        } dispatches[16];
    } indirect_data = {
        { 2, 7, 4, 13 },
        {
            { 0, 2, 3, 4 },
            { 0, 5, 6, 7 },
            { 0, 8, 9, 10 },
            { 0, 11, 3, 12 },
            { 0, 6, 13, 14 },
            { 0, 13, 14, 15 },
            { 0, 16, 17, 18 },
            { 0, 19, 18, 7 },
            { 0, 2, 3, 4 },
            { 0, 5, 6, 7 },
            { 0, 8, 9, 10 },
            { 0, 11, 3, 12 },
            { 0, 6, 13, 14 },
            { 0, 13, 14, 15 },
            { 0, 16, 17, 18 },
            { 0, 19, 18, 7 },
        },
    };

    uint32_t expected_counts[ARRAY_SIZE(indirect_data.dispatches)] = { 0 };

#include "shaders/command/headers/execute_indirect_multi_dispatch_root_descriptors.h"

    if (!init_compute_test_context(&context))
        return;

    desc.ByteStride = sizeof(indirect_data.dispatches[0]);
    desc.NodeMask = 0;
    desc.NumArgumentDescs = ARRAY_SIZE(arguments);
    desc.pArgumentDescs = arguments;

    memset(&rs_desc, 0, sizeof(rs_desc));
    memset(root_params, 0, sizeof(root_params));
    rs_desc.NumParameters = ARRAY_SIZE(root_params);
    rs_desc.pParameters = root_params;
    root_params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    root_params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    create_root_signature(context.device, &rs_desc, &context.root_signature);

    context.pipeline_state = create_compute_pipeline_state(context.device, context.root_signature,
        execute_indirect_multi_dispatch_root_descriptors_dxbc);

    memset(arguments, 0, sizeof(arguments));
    arguments[0].Type = D3D12_INDIRECT_ARGUMENT_TYPE_UNORDERED_ACCESS_VIEW;
    arguments[0].UnorderedAccessView.RootParameterIndex = 0;
    arguments[1].Type = D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH;
    hr = ID3D12Device_CreateCommandSignature(context.device, &desc, context.root_signature, &IID_ID3D12CommandSignature, (void **)&signature);
    if (FAILED(hr))
        signature = NULL;
    todo ok(SUCCEEDED(hr), "Failed to create command signature, hr #%x.\n", hr);

    output = create_default_buffer(context.device, ARRAY_SIZE(indirect_data.dispatches) * 4, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    for (i = 0; i < ARRAY_SIZE(indirect_data.dispatches); i++)
        indirect_data.dispatches[i].va = ID3D12Resource_GetGPUVirtualAddress(output) + 4 * i;

    indirect = create_upload_buffer(context.device, sizeof(indirect_data), &indirect_data);

    ID3D12GraphicsCommandList_SetComputeRootSignature(context.list, context.root_signature);
    ID3D12GraphicsCommandList_SetPipelineState(context.list, context.pipeline_state);

    for (test_index = 0; test_index < 8 && signature; test_index++)
    {
        iteration = test_index & 3;

        if (test_index < 4)
        {
            /* Multi-dispatch, direct count. */
            ID3D12GraphicsCommandList_ExecuteIndirect(context.list, signature, indirect_data.dispatch_counts[iteration],
                    indirect, offsetof(struct test_data, dispatches[test_index]), NULL, 0);
        }
        else
        {
            /* Indirect count style. */
            ID3D12GraphicsCommandList_ExecuteIndirect(context.list, signature, max_indirect_count,
                    indirect, offsetof(struct test_data, dispatches[test_index]),
                    indirect, offsetof(struct test_data, dispatch_counts[iteration]));
        }

        if (test_index == 6)
        {
            /* Test case with late indirect update. Just make sure we can exercise both
             * code paths. */
            transition_resource_state(context.list, indirect, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT,
                    D3D12_RESOURCE_STATE_COPY_SOURCE);
            transition_resource_state(context.list, indirect, D3D12_RESOURCE_STATE_COPY_SOURCE,
                    D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
        }
    }

    transition_resource_state(context.list, output, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);

    get_buffer_readback_with_command_list(output, DXGI_FORMAT_UNKNOWN, &rb, context.queue, context.list);

    for (test_index = 0; test_index < 8; test_index++)
    {
        unsigned int num_dispatches;
        iteration = test_index & 3;
        num_dispatches = test_index < 4 ?
            indirect_data.dispatch_counts[iteration] :
            min(indirect_data.dispatch_counts[iteration], max_indirect_count);

        for (i = test_index; i < num_dispatches + test_index; i++)
        {
            expected_counts[i] +=
                indirect_data.dispatches[i].wg_x *
                indirect_data.dispatches[i].wg_y *
                indirect_data.dispatches[i].wg_z;
        }
    }

    for (i = 0; i < ARRAY_SIZE(indirect_data.dispatches); i++)
    {
        uint32_t value;
        value = get_readback_uint(&rb, i, 0, 0);
        todo_if(!signature) ok(value == expected_counts[i], "Value %u: Expected %u, got %u.\n", i, expected_counts[i], value);
    }

    release_resource_readback(&rb);
    ID3D12Resource_Release(indirect);
    ID3D12Resource_Release(output);
    if (signature)
        ID3D12CommandSignature_Release(signature);

    destroy_test_context(&context);
}

void test_execute_indirect_multi_dispatch_root_constants(void)
{
    const unsigned int max_indirect_count = 5;
    D3D12_INDIRECT_ARGUMENT_DESC arguments[2];
    D3D12_ROOT_PARAMETER root_params[2];
    ID3D12CommandSignature *signature;
    D3D12_COMMAND_SIGNATURE_DESC desc;
    D3D12_ROOT_SIGNATURE_DESC rs_desc;
    struct resource_readback rb;
    struct test_context context;
    ID3D12Resource *indirect;
    unsigned int test_index;
    ID3D12Resource *output;
    unsigned int iteration;
    HRESULT hr;

    static const uint32_t indirect_data[] = {
        1, 8, 3, 6, /* indirect multi dispatch counts */
        13, 10, 20, 30, /* root constant u32 + dispatch group counts */
        14, 11, 21, 31,
        15, 0, 22, 32,
        16, 13, 23, 0,
        17, 10, 20, 30,
        18, 11, 21, 31,
        19, 12, 22, 32,
        20, 13, 23, 33,
        21, 10, 20, 30,
        22, 11, 21, 31,
        23, 12, 22, 32,
        24, 13, 23, 33,
        25, 10, 20, 30,
        26, 11, 21, 31,
        27, 12, 22, 32,
        28, 13, 23, 33,
    };

#include "shaders/command/headers/execute_indirect_multi_dispatch_root_constants.h"

    if (!init_compute_test_context(&context))
        return;

    desc.ByteStride = sizeof(uint32_t) + sizeof(D3D12_DISPATCH_ARGUMENTS);
    desc.NodeMask = 0;
    desc.NumArgumentDescs = ARRAY_SIZE(arguments);
    desc.pArgumentDescs = arguments;

    memset(&rs_desc, 0, sizeof(rs_desc));
    memset(root_params, 0, sizeof(root_params));
    rs_desc.NumParameters = ARRAY_SIZE(root_params);
    rs_desc.pParameters = root_params;
    root_params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    root_params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    root_params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    root_params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    root_params[1].Constants.Num32BitValues = 4;
    create_root_signature(context.device, &rs_desc, &context.root_signature);

    context.pipeline_state = create_compute_pipeline_state(context.device, context.root_signature,
        execute_indirect_multi_dispatch_root_constants_dxbc);

    memset(arguments, 0, sizeof(arguments));
    arguments[0].Type = D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT;
    arguments[0].Constant.RootParameterIndex = 1;
    arguments[0].Constant.Num32BitValuesToSet = 1;
    arguments[0].Constant.DestOffsetIn32BitValues = 2;
    arguments[1].Type = D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH;
    hr = ID3D12Device_CreateCommandSignature(context.device, &desc, context.root_signature, &IID_ID3D12CommandSignature, (void **)&signature);
    if (FAILED(hr))
        signature = NULL;
    todo ok(SUCCEEDED(hr), "Failed to create command signature, hr #%x.\n", hr);

    output = create_default_buffer(context.device, 8 * 4, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    indirect = create_upload_buffer(context.device, sizeof(indirect_data), indirect_data);

    ID3D12GraphicsCommandList_SetComputeRootSignature(context.list, context.root_signature);
    ID3D12GraphicsCommandList_SetPipelineState(context.list, context.pipeline_state);

    for (test_index = 0; test_index < 8 && signature; test_index++)
    {
        iteration = test_index & 3;

        ID3D12GraphicsCommandList_SetComputeRootUnorderedAccessView(context.list, 0,
            ID3D12Resource_GetGPUVirtualAddress(output) + 4 * test_index);

        ID3D12GraphicsCommandList_SetComputeRoot32BitConstant(context.list, 1, 1, 0);
        ID3D12GraphicsCommandList_SetComputeRoot32BitConstant(context.list, 1, 2, 1);
        ID3D12GraphicsCommandList_SetComputeRoot32BitConstant(context.list, 1, 3, 2);
        ID3D12GraphicsCommandList_SetComputeRoot32BitConstant(context.list, 1, 4, 3);

        if (test_index < 4)
        {
            /* Multi-dispatch, direct count. */
            ID3D12GraphicsCommandList_ExecuteIndirect(context.list, signature, indirect_data[iteration], indirect, 16 + 16 * test_index, NULL, 0);
        }
        else
        {
            /* Indirect count style. */
            ID3D12GraphicsCommandList_ExecuteIndirect(context.list, signature, max_indirect_count,
                indirect, 16 + 16 * test_index,
                indirect, 4 * iteration);
        }

        /* Also test behavior for cleared root constant state. cbv.z should be cleared to 0 here. */
        ID3D12GraphicsCommandList_Dispatch(context.list, 1, 1, 1);

        if (test_index == 6)
        {
            /* Test case with late indirect update. Just make sure we can exercise both
             * code paths. */
            transition_resource_state(context.list, indirect, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT,
                    D3D12_RESOURCE_STATE_COPY_SOURCE);
            transition_resource_state(context.list, indirect, D3D12_RESOURCE_STATE_COPY_SOURCE,
                    D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
        }
    }

    transition_resource_state(context.list, output, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);

    get_buffer_readback_with_command_list(output, DXGI_FORMAT_UNKNOWN, &rb, context.queue, context.list);

    for (test_index = 0; test_index < 8; test_index++)
    {
        unsigned int num_dispatches, expected, expected_cbv, value, i;
        iteration = test_index & 3;
        num_dispatches = test_index < 4 ? indirect_data[iteration] : min(indirect_data[iteration], max_indirect_count);
        expected = 0;

        for (i = test_index; i < num_dispatches + test_index; i++)
        {
            expected_cbv = 1 | 2 | 4; /* x, y and w elements are untouched. */
            expected_cbv |= indirect_data[4 + 4 * i + 0];

            expected += expected_cbv *
                    indirect_data[4 + 4 * i + 1] *
                    indirect_data[4 + 4 * i + 2] *
                    indirect_data[4 + 4 * i + 3];
        }
        expected += 7; /* For the single 1, 1, 1 dispatch. */
        value = get_readback_uint(&rb, test_index, 0, 0);
        todo_if(!signature) ok(value == expected, "Iteration %u: Expected %u, got %u.\n", test_index, expected, value);
    }

    release_resource_readback(&rb);
    ID3D12Resource_Release(indirect);
    ID3D12Resource_Release(output);
    if (signature)
        ID3D12CommandSignature_Release(signature);

    destroy_test_context(&context);
}

void test_execute_indirect_multi_dispatch(void)
{
    const unsigned int max_indirect_count = 5;
    D3D12_INDIRECT_ARGUMENT_DESC arguments[1];
    ID3D12CommandSignature *signature;
    D3D12_COMMAND_SIGNATURE_DESC desc;
    D3D12_ROOT_SIGNATURE_DESC rs_desc;
    D3D12_ROOT_PARAMETER root_param;
    struct resource_readback rb;
    struct test_context context;
    ID3D12Resource *indirect;
    unsigned int test_index;
    ID3D12Resource *output;
    unsigned int iteration;
    HRESULT hr;

    /* Simple multi dispatch indirect without state changes. */

    static const uint32_t indirect_data[] = {
        1, 8, 3, 6, /* indirect multi dispatch counts */
        10, 20, 30, /* dispatch group counts */
        11, 21, 31,
        0, 22, 32,
        13, 23, 0,
        10, 20, 30,
        11, 21, 31,
        12, 22, 32,
        13, 23, 33,
        10, 20, 30,
        11, 21, 31,
        12, 22, 32,
        13, 23, 33,
        10, 20, 30,
        11, 21, 31,
        12, 22, 32,
        13, 23, 33,
    };

#include "shaders/command/headers/execute_indirect_multi_dispatch.h"

    if (!init_compute_test_context(&context))
        return;

    memset(&rs_desc, 0, sizeof(rs_desc));
    memset(&root_param, 0, sizeof(root_param));
    rs_desc.NumParameters = 1;
    rs_desc.pParameters = &root_param;
    root_param.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    root_param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    create_root_signature(context.device, &rs_desc, &context.root_signature);

    context.pipeline_state = create_compute_pipeline_state(context.device, context.root_signature,
        execute_indirect_multi_dispatch_dxbc);

    desc.ByteStride = sizeof(D3D12_DISPATCH_ARGUMENTS);
    desc.NodeMask = 0;
    desc.NumArgumentDescs = ARRAY_SIZE(arguments);
    desc.pArgumentDescs = arguments;

    memset(arguments, 0, sizeof(arguments));
    arguments[0].Type = D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH;
    hr = ID3D12Device_CreateCommandSignature(context.device, &desc, NULL, &IID_ID3D12CommandSignature, (void **)&signature);
    ok(SUCCEEDED(hr), "Failed to create command signature, hr #%x.\n", hr);

    output = create_default_buffer(context.device, 8 * 4, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    indirect = create_upload_buffer(context.device, sizeof(indirect_data), indirect_data);

    ID3D12GraphicsCommandList_SetComputeRootSignature(context.list, context.root_signature);
    ID3D12GraphicsCommandList_SetPipelineState(context.list, context.pipeline_state);

    for (test_index = 0; test_index < 8; test_index++)
    {
        iteration = test_index & 3;

        ID3D12GraphicsCommandList_SetComputeRootUnorderedAccessView(context.list, 0,
                ID3D12Resource_GetGPUVirtualAddress(output) + 4 * test_index);

        if (test_index < 4)
        {
            /* Multi-dispatch, direct count. */
            ID3D12GraphicsCommandList_ExecuteIndirect(context.list, signature, indirect_data[iteration], indirect, 16 + 12 * test_index, NULL, 0);
        }
        else
        {
            /* Indirect count style. */
            ID3D12GraphicsCommandList_ExecuteIndirect(context.list, signature, max_indirect_count,
                    indirect, 16 + 12 * test_index,
                    indirect, 4 * iteration);
        }

        if (test_index == 6)
        {
            /* Test case with late indirect update. Just make sure we can exercise both
             * code paths. */
            transition_resource_state(context.list, indirect, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT,
                    D3D12_RESOURCE_STATE_COPY_SOURCE);
            transition_resource_state(context.list, indirect, D3D12_RESOURCE_STATE_COPY_SOURCE,
                    D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
        }
    }

    transition_resource_state(context.list, output, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
    get_buffer_readback_with_command_list(output, DXGI_FORMAT_UNKNOWN, &rb, context.queue, context.list);

    for (test_index = 0; test_index < 8; test_index++)
    {
        unsigned int num_dispatches, expected, value, i;
        iteration = test_index & 3;
        num_dispatches = test_index < 4 ? indirect_data[iteration] : min(indirect_data[iteration], max_indirect_count);
        expected = 0;
        for (i = test_index; i < num_dispatches + test_index; i++)
        {
            expected += indirect_data[4 + 3 * i + 0] *
                    indirect_data[4 + 3 * i + 1] *
                    indirect_data[4 + 3 * i + 2];
        }
        value = get_readback_uint(&rb, test_index, 0, 0);
        ok(value == expected, "Iteration %u: Expected %u, got %u.\n", test_index, expected, value);
    }

    release_resource_readback(&rb);
    ID3D12Resource_Release(indirect);
    ID3D12Resource_Release(output);
    ID3D12CommandSignature_Release(signature);

    destroy_test_context(&context);
}

static void report_predication_timestamps(ID3D12CommandQueue *queue, ID3D12Resource *resource,
        unsigned int num_timestamps, const char *tag)
{
    uint64_t min_delta = UINT64_MAX;
    uint64_t max_delta = 0;
    const uint64_t *ts;
    unsigned int i;
    uint64_t delta;
    UINT64 ts_freq;

    ID3D12Resource_Map(resource, 0, NULL, (void **)&ts);
    ID3D12CommandQueue_GetTimestampFrequency(queue, &ts_freq);
    for (i = 1; i < num_timestamps; i++)
    {
        delta = ts[i] - ts[i - 1];
        max_delta = max(max_delta, delta);
        min_delta = min(min_delta, delta);
    }
    printf("Maximum %s time: %.3f us.\n", tag,
            1e6 * (double)max_delta / (double)ts_freq);
    printf("Minimum %s time: %.3f us.\n", tag,
            1e6 * (double)min_delta / (double)ts_freq);
    delta = ts[num_timestamps - 1] - ts[0];
    printf("Average %s time: %.3f us.\n", tag,
            1e6 * (double)delta / (double)(ts_freq * (num_timestamps - 1)));
    ID3D12Resource_Unmap(resource, 0, NULL);
}

void test_execute_indirect_state_predication(void)
{
    D3D12_INDIRECT_ARGUMENT_DESC argument_descs[2];
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc;
    D3D12_COMMAND_SIGNATURE_DESC sig_desc;
    ID3D12CommandSignature *sig_compute;
    D3D12_ROOT_SIGNATURE_DESC rs_desc;
    D3D12_ROOT_PARAMETER rs_params[3];
    ID3D12Resource *indirect_graphics;
    D3D12_QUERY_HEAP_DESC query_desc;
    ID3D12Resource *indirect_compute;
    ID3D12Resource *indirect_counts;
    ID3D12Resource *query_readback;
    ID3D12Resource *indirect_copy;
    struct test_context_desc desc;
    ID3D12PipelineState *pso_comp;
    ID3D12PipelineState *pso_gfx;
    unsigned int timestamp_index;
    ID3D12CommandSignature *sig;
    struct test_context context;
    struct resource_readback rb;
    ID3D12QueryHeap *query_heap;

    ID3D12Resource *output;
    D3D12_VIEWPORT vp;
    unsigned int i, j;
    D3D12_RECT sci;
    HRESULT hr;

    /* Tests predication feature alongside ExecuteIndirect templates,
     * but also considers our internal implementation details w.r.t.
     * adding extra predication for performance. */

    struct draw_arguments
    {
        uint32_t value;
        D3D12_DRAW_ARGUMENTS draw;
    };

    struct dispatch_arguments
    {
        uint32_t value;
        D3D12_DISPATCH_ARGUMENTS dispatch;
    };

#include "shaders/command/headers/execute_indirect_state_predication_vs.h"
#include "shaders/command/headers/execute_indirect_state_predication_ps.h"
#include "shaders/command/headers/execute_indirect_state_predication_cs.h"

    memset(&desc, 0, sizeof(desc));
    desc.no_pipeline = true;
    desc.no_render_target = true;
    desc.no_root_signature = true;
    if (!init_test_context(&context, &desc))
        return;

    memset(&rs_desc, 0, sizeof(rs_desc));
    memset(rs_params, 0, sizeof(rs_params));
    rs_desc.NumParameters = ARRAY_SIZE(rs_params);
    rs_desc.pParameters = rs_params;

    rs_params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    rs_params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    rs_params[0].Constants.Num32BitValues = 1;
    rs_params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    rs_params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rs_params[1].Descriptor.ShaderRegister = 1;
    rs_params[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    rs_params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    create_root_signature(context.device, &rs_desc, &context.root_signature);

    memset(argument_descs, 0, sizeof(argument_descs));
    argument_descs[0].Type = D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT;
    argument_descs[0].Constant.RootParameterIndex = 0;
    argument_descs[0].Constant.Num32BitValuesToSet = 1;
    argument_descs[1].Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW;

    memset(&sig_desc, 0, sizeof(sig_desc));
    sig_desc.pArgumentDescs = argument_descs;
    sig_desc.NumArgumentDescs = ARRAY_SIZE(argument_descs);
    sig_desc.ByteStride = sizeof(struct draw_arguments);

    if (FAILED(hr = ID3D12Device_CreateCommandSignature(context.device, &sig_desc, context.root_signature,
            &IID_ID3D12CommandSignature, (void **)&sig)))
    {
        skip("Implementation does not support DGC, skipping test.\n");
        destroy_test_context(&context);
        return;
    }

    argument_descs[1].Type = D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH;
    sig_desc.ByteStride = sizeof(struct dispatch_arguments);

    if (FAILED(hr = ID3D12Device_CreateCommandSignature(context.device, &sig_desc, context.root_signature,
            &IID_ID3D12CommandSignature, (void **)&sig_compute)))
    {
        skip("Implementation does not support DGCC, skipping test.\n");
        ID3D12CommandSignature_Release(sig);
        destroy_test_context(&context);
        return;
    }

    pso_comp = create_compute_pipeline_state(context.device, context.root_signature,
        execute_indirect_state_predication_cs_dxbc);

    init_pipeline_state_desc(&pso_desc, context.root_signature, DXGI_FORMAT_UNKNOWN,
        &execute_indirect_state_predication_vs_dxbc, &execute_indirect_state_predication_ps_dxbc, NULL);
    pso_desc.DepthStencilState.DepthEnable = FALSE;
    pso_desc.DepthStencilState.StencilEnable = FALSE;
    pso_desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    ID3D12Device_CreateGraphicsPipelineState(context.device, &pso_desc, &IID_ID3D12PipelineState, (void **)&pso_gfx);

    {
        uint32_t counts[256] = { 0 };
        for (i = 0; i < ARRAY_SIZE(counts) / 2; i++)
            counts[i] = i;
        indirect_counts = create_upload_buffer(context.device, sizeof(counts), counts);
        indirect_copy = create_default_buffer(context.device, sizeof(counts), D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COMMON);
    }

    {
        /* Test various ways to observe empty draws. Go beyond 32 so we can test behavior for larger dispatches as well. */
        struct draw_arguments draws[128] = {{0}};
        for (i = 0; i < ARRAY_SIZE(draws); i++)
        {
            draws[i].value = i * 4;
            if (i & 1)
                draws[i].draw.VertexCountPerInstance = 3;
            else
                draws[i].draw.InstanceCount = 40;
        }

        /* Let only the last one be a real draw. */
        draws[ARRAY_SIZE(draws) - 1].draw.VertexCountPerInstance = 3;
        draws[ARRAY_SIZE(draws) - 1].draw.InstanceCount = 40;
        indirect_graphics = create_upload_buffer(context.device, sizeof(draws), draws);
    }

    {
        /* Test various ways to observe empty dispatches. */
        struct dispatch_arguments dispatches[128] = {{0}};
        for (i = 0; i < ARRAY_SIZE(dispatches); i++)
        {
            dispatches[i].value = i * 4;
            switch (i & 3)
            {
                case 0:
                    dispatches[i].dispatch.ThreadGroupCountX = 1;
                    dispatches[i].dispatch.ThreadGroupCountY = 1;
                    break;

                case 1:
                    dispatches[i].dispatch.ThreadGroupCountX = 1;
                    dispatches[i].dispatch.ThreadGroupCountZ = 1;
                    break;

                default:
                    dispatches[i].dispatch.ThreadGroupCountY = 1;
                    dispatches[i].dispatch.ThreadGroupCountZ = 1;
                    break;
            }
        }

        dispatches[ARRAY_SIZE(dispatches) - 1].dispatch.ThreadGroupCountX = 10;
        dispatches[ARRAY_SIZE(dispatches) - 1].dispatch.ThreadGroupCountY = 10;
        dispatches[ARRAY_SIZE(dispatches) - 1].dispatch.ThreadGroupCountZ = 10;
        indirect_compute = create_upload_buffer(context.device, sizeof(dispatches), dispatches);
    }

    memset(&query_desc, 0, sizeof(query_desc));
    query_desc.Count = 64 * 1024;
    query_desc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
    ID3D12Device_CreateQueryHeap(context.device, &query_desc, &IID_ID3D12QueryHeap, (void **)&query_heap);
    query_readback = create_readback_buffer(context.device, 64 * 1024 * sizeof(uint64_t));

    /* Graphics */
    {
        timestamp_index = 0;
        ID3D12GraphicsCommandList_EndQuery(context.list, query_heap, D3D12_QUERY_TYPE_TIMESTAMP, timestamp_index++);

        /* Make sure the first timestamp is ordered before any reordered preprocess work. */
        ID3D12GraphicsCommandList_Close(context.list);
        exec_command_list(context.queue, context.list);
        ID3D12GraphicsCommandList_Reset(context.list, context.allocator, NULL);

        output = create_default_buffer(context.device, 64 * 1024, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON);

        set_viewport(&vp, 0, 0, 1, 1, 0, 1);
        set_rect(&sci, 0, 0, 1, 1);
        ID3D12GraphicsCommandList_RSSetViewports(context.list, 1, &vp);
        ID3D12GraphicsCommandList_RSSetScissorRects(context.list, 1, &sci);
        ID3D12GraphicsCommandList_SetPipelineState(context.list, pso_gfx);
        ID3D12GraphicsCommandList_SetGraphicsRootSignature(context.list, context.root_signature);
        ID3D12GraphicsCommandList_IASetPrimitiveTopology(context.list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ID3D12GraphicsCommandList_SetGraphicsRootConstantBufferView(context.list, 1, ID3D12Resource_GetGPUVirtualAddress(indirect_counts) + 256);

        for (i = 0; i < 6; i++)
        {
            if (i == 0)
                ID3D12GraphicsCommandList_SetPredication(context.list, NULL, 0, D3D12_PREDICATION_OP_EQUAL_ZERO);
            else if (i == 1)
                ID3D12GraphicsCommandList_SetPredication(context.list, indirect_counts, 0, D3D12_PREDICATION_OP_EQUAL_ZERO);
            else if (i == 2)
                ID3D12GraphicsCommandList_SetPredication(context.list, indirect_counts, 0, D3D12_PREDICATION_OP_NOT_EQUAL_ZERO);
            else if (i == 5)
                ID3D12GraphicsCommandList_SetPredication(context.list, indirect_copy, 0, D3D12_PREDICATION_OP_EQUAL_ZERO);
            /* inherit predication in i == 3 */

            /* Try to trigger sync issues. */
            ID3D12GraphicsCommandList_SetGraphicsRootUnorderedAccessView(context.list, 2, ID3D12Resource_GetGPUVirtualAddress(output) + (i * 512 + 1) * sizeof(uint32_t));
            for (j = 0; j < 16; j++)
            {
                ID3D12GraphicsCommandList_ExecuteIndirect(context.list, sig, 128, indirect_graphics, 0, NULL, 0); /* last draw will do something, verify we actually check all draws when we cull */
                ID3D12GraphicsCommandList_EndQuery(context.list, query_heap, D3D12_QUERY_TYPE_TIMESTAMP, timestamp_index++);
            }

            ID3D12GraphicsCommandList_SetGraphicsRootUnorderedAccessView(context.list, 2, ID3D12Resource_GetGPUVirtualAddress(output) + (i * 512 + 0) * sizeof(uint32_t));
            /* Hammer hard to study profiler. */
            for (j = 0; j < 1024; j++)
            {
                ID3D12GraphicsCommandList_ExecuteIndirect(context.list, sig, 127, indirect_graphics, 0, NULL, 0); /* should do nothing, and should be culled out */
                ID3D12GraphicsCommandList_EndQuery(context.list, query_heap, D3D12_QUERY_TYPE_TIMESTAMP, timestamp_index++);
            }
            ID3D12GraphicsCommandList_SetGraphicsRootUnorderedAccessView(context.list, 2, ID3D12Resource_GetGPUVirtualAddress(output) + (i * 512 + 2) * sizeof(uint32_t));
            ID3D12GraphicsCommandList_ExecuteIndirect(context.list, sig, 1, indirect_graphics, 127 * sizeof(struct draw_arguments), NULL, 0); /* same, but only 1 draw */
            ID3D12GraphicsCommandList_SetGraphicsRootUnorderedAccessView(context.list, 2, ID3D12Resource_GetGPUVirtualAddress(output) + (i * 512 + 3) * sizeof(uint32_t));

            if (i > 2)
                ID3D12GraphicsCommandList_ExecuteIndirect(context.list, sig, 1, indirect_graphics, 127 * sizeof(struct draw_arguments), indirect_copy, 0); /* same, but indirect count */
            else
                ID3D12GraphicsCommandList_ExecuteIndirect(context.list, sig, 1, indirect_graphics, 127 * sizeof(struct draw_arguments), indirect_counts, 4); /* same, but indirect count */

            ID3D12GraphicsCommandList_EndQuery(context.list, query_heap, D3D12_QUERY_TYPE_TIMESTAMP, timestamp_index++);

            if (i == 2)
            {
                /* Check if predication persists across an INDIRECT_ARGUMENT barrier */
                ID3D12GraphicsCommandList_SetPredication(context.list, NULL, 0, D3D12_PREDICATION_OP_EQUAL_ZERO);
                ID3D12GraphicsCommandList_CopyBufferRegion(context.list, indirect_copy, 0, indirect_counts, 0, 8);
                ID3D12GraphicsCommandList_SetPredication(context.list, indirect_counts, 0, D3D12_PREDICATION_OP_NOT_EQUAL_ZERO);
                transition_resource_state(context.list, indirect_copy, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT); /* --- split --- */
            }
            else if (i == 3)
            {
                ID3D12GraphicsCommandList_SetPredication(context.list, NULL, 0, D3D12_PREDICATION_OP_EQUAL_ZERO);
                transition_resource_state(context.list, indirect_copy, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT, D3D12_RESOURCE_STATE_COPY_DEST);
                ID3D12GraphicsCommandList_CopyBufferRegion(context.list, indirect_copy, 0, indirect_counts, 512, 8); /* copy a 0 count here. Copy to same location to make sure the indirect count caches get flushed. */
                transition_resource_state(context.list, indirect_copy, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT); /* --- split --- */
            }
            else if (i == 4)
            {
                transition_resource_state(context.list, indirect_copy, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT, D3D12_RESOURCE_STATE_COPY_DEST);
                ID3D12GraphicsCommandList_CopyBufferRegion(context.list, indirect_copy, 0, indirect_counts, 32, 8); /* copy a non-zero count here. Copy to same location to make sure the indirect count caches get flushed. */
                transition_resource_state(context.list, indirect_copy, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT); /* --- split --- */
            }
            else if (i == 5)
            {
                /* Stress test to make sure we stop splitting at some point. */
                transition_resource_state(context.list, indirect_copy, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT, D3D12_RESOURCE_STATE_COPY_DEST);
                transition_resource_state(context.list, indirect_copy, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT); /* --- split --- */
                transition_resource_state(context.list, indirect_copy, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT, D3D12_RESOURCE_STATE_COPY_DEST);
                transition_resource_state(context.list, indirect_copy, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT); /* --- split --- */
                transition_resource_state(context.list, indirect_copy, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT, D3D12_RESOURCE_STATE_COPY_DEST);
                transition_resource_state(context.list, indirect_copy, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT); /* --- split --- */
                transition_resource_state(context.list, indirect_copy, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT, D3D12_RESOURCE_STATE_COPY_DEST);
                transition_resource_state(context.list, indirect_copy, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT); /* --- split --- */
            }
        }

        assert(timestamp_index <= 64 * 1024);
        ID3D12GraphicsCommandList_ResolveQueryData(context.list, query_heap, D3D12_QUERY_TYPE_TIMESTAMP, 0, timestamp_index,
                query_readback, 0);

        ID3D12GraphicsCommandList_SetPredication(context.list, NULL, 0, D3D12_PREDICATION_OP_EQUAL_ZERO);
        transition_resource_state(context.list, output, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
        get_buffer_readback_with_command_list(output, DXGI_FORMAT_UNKNOWN, &rb, context.queue, context.list);
        reset_command_list(context.list, context.allocator);

        for (i = 0; i < 128; i++)
        {
            uint32_t expected_y;
            uint32_t expected_w;
            struct uvec4 value;
            uint32_t expected;

            for (j = 0; j < 6; j++)
            {
                value = *get_readback_uvec4(&rb, i + 128 * j, 0);

                expected = i == 127 ? 40 : 0; /* Number of instances (i.e. pixels) drawn */
                expected *= 64; /* multiplier from root CBV */

                if (j == 2 || j == 3)
                    expected = 0; /* predicated away */

                /* In iteration 5, we copied a zero indirect count. */
                expected_w = j == 4 ? 0 : expected;
                expected_y = expected * 16;

                /* of the gang of 4 draws, the first one should not do anything */
                ok(value.x == 0 && value.y == expected_y && value.z == expected && value.w == expected_w,
                        "Iteration %u, draw output %u: expected {%u, %u, %u, %u}, got {%u, %u, %u, %u}.\n", j, i, 0,
                        expected_y, expected, expected,
                        value.x, value.y, value.z, value.w);
            }
        }

        release_resource_readback(&rb);
        ID3D12Resource_Release(output);

        report_predication_timestamps(context.queue, query_readback, timestamp_index, "DGC");
    }

    /* Compute */
    {
        timestamp_index = 0;

        ID3D12GraphicsCommandList_EndQuery(context.list, query_heap, D3D12_QUERY_TYPE_TIMESTAMP, timestamp_index++);

        /* Make sure the first timestamp is ordered before any reordered preprocess work. */
        ID3D12GraphicsCommandList_Close(context.list);
        exec_command_list(context.queue, context.list);
        ID3D12GraphicsCommandList_Reset(context.list, context.allocator, NULL);

        output = create_default_buffer(context.device, 64 * 1024, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON);
        ID3D12GraphicsCommandList_SetComputeRootSignature(context.list, context.root_signature);
        ID3D12GraphicsCommandList_SetComputeRootConstantBufferView(context.list, 1, ID3D12Resource_GetGPUVirtualAddress(indirect_counts) + 256);
        ID3D12GraphicsCommandList_SetPipelineState(context.list, pso_comp);

        for (i = 0; i < 6; i++)
        {
            if (i == 0)
                ID3D12GraphicsCommandList_SetPredication(context.list, NULL, 0, D3D12_PREDICATION_OP_EQUAL_ZERO);
            else if (i == 1)
                ID3D12GraphicsCommandList_SetPredication(context.list, indirect_counts, 0, D3D12_PREDICATION_OP_EQUAL_ZERO);
            else if (i == 2)
                ID3D12GraphicsCommandList_SetPredication(context.list, indirect_counts, 0, D3D12_PREDICATION_OP_NOT_EQUAL_ZERO);
            else if (i == 5)
                ID3D12GraphicsCommandList_SetPredication(context.list, indirect_copy, 0, D3D12_PREDICATION_OP_EQUAL_ZERO);
            /* inherit predication in i == 3 */

            /* Try to trigger any sync issues. */
            ID3D12GraphicsCommandList_SetComputeRootUnorderedAccessView(context.list, 2, ID3D12Resource_GetGPUVirtualAddress(output) + (i * 512 + 1) * sizeof(uint32_t));
            for (j = 0; j < 16; j++)
            {
                ID3D12GraphicsCommandList_ExecuteIndirect(context.list, sig_compute, 128, indirect_compute, 0, NULL, 0); /* last draw will do something, verify we actually check all draws when we cull */
                ID3D12GraphicsCommandList_EndQuery(context.list, query_heap, D3D12_QUERY_TYPE_TIMESTAMP, timestamp_index++);
            }

            ID3D12GraphicsCommandList_SetComputeRootUnorderedAccessView(context.list, 2, ID3D12Resource_GetGPUVirtualAddress(output) + (i * 512 + 0) * sizeof(uint32_t));
            /* Hammer this really hard, so we can stare at profiler. */
            for (j = 0; j < 1024; j++)
            {
                ID3D12GraphicsCommandList_ExecuteIndirect(context.list, sig_compute, 127, indirect_compute, 0, NULL, 0); /* should do nothing, and should be culled out */
                ID3D12GraphicsCommandList_EndQuery(context.list, query_heap, D3D12_QUERY_TYPE_TIMESTAMP, timestamp_index++);
            }
            ID3D12GraphicsCommandList_SetComputeRootUnorderedAccessView(context.list, 2, ID3D12Resource_GetGPUVirtualAddress(output) + (i * 512 + 2) * sizeof(uint32_t));
            ID3D12GraphicsCommandList_ExecuteIndirect(context.list, sig_compute, 1, indirect_compute, 127 * sizeof(struct dispatch_arguments), NULL, 0); /* same, but only 1 draw */
            ID3D12GraphicsCommandList_EndQuery(context.list, query_heap, D3D12_QUERY_TYPE_TIMESTAMP, timestamp_index++);
            ID3D12GraphicsCommandList_SetComputeRootUnorderedAccessView(context.list, 2, ID3D12Resource_GetGPUVirtualAddress(output) + (i * 512 + 3) * sizeof(uint32_t));

            if (i > 2)
                ID3D12GraphicsCommandList_ExecuteIndirect(context.list, sig_compute, 1, indirect_compute, 127 * sizeof(struct dispatch_arguments), indirect_copy, 0); /* same, but indirect count */
            else
                ID3D12GraphicsCommandList_ExecuteIndirect(context.list, sig_compute, 1, indirect_compute, 127 * sizeof(struct dispatch_arguments), indirect_counts, 4); /* same, but indirect count */

            ID3D12GraphicsCommandList_EndQuery(context.list, query_heap, D3D12_QUERY_TYPE_TIMESTAMP, timestamp_index++);

            if (i == 2)
            {
                /* Check if predication persists across an INDIRECT_ARGUMENT barrier */
                ID3D12GraphicsCommandList_SetPredication(context.list, NULL, 0, D3D12_PREDICATION_OP_EQUAL_ZERO);
                ID3D12GraphicsCommandList_CopyBufferRegion(context.list, indirect_copy, 0, indirect_counts, 0, 8);
                ID3D12GraphicsCommandList_SetPredication(context.list, indirect_counts, 0, D3D12_PREDICATION_OP_NOT_EQUAL_ZERO);
                transition_resource_state(context.list, indirect_copy, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT); /* --- split --- */
            }
            else if (i == 3)
            {
                ID3D12GraphicsCommandList_SetPredication(context.list, NULL, 0, D3D12_PREDICATION_OP_EQUAL_ZERO);
                transition_resource_state(context.list, indirect_copy, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT, D3D12_RESOURCE_STATE_COPY_DEST);
                ID3D12GraphicsCommandList_CopyBufferRegion(context.list, indirect_copy, 0, indirect_counts, 512, 8); /* copy a 0 count here. Copy to same location to make sure the indirect count caches get flushed. */
                transition_resource_state(context.list, indirect_copy, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT); /* --- split --- */
            }
            else if (i == 4)
            {
                transition_resource_state(context.list, indirect_copy, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT, D3D12_RESOURCE_STATE_COPY_DEST);
                ID3D12GraphicsCommandList_CopyBufferRegion(context.list, indirect_copy, 0, indirect_counts, 32, 8); /* copy a non-zero count here. Copy to same location to make sure the indirect count caches get flushed. */
                transition_resource_state(context.list, indirect_copy, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT); /* --- split --- */
            }
            else if (i == 5)
            {
                /* Stress test to make sure we stop splitting at some point. */
                transition_resource_state(context.list, indirect_copy, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT, D3D12_RESOURCE_STATE_COPY_DEST);
                transition_resource_state(context.list, indirect_copy, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT); /* --- split --- */
                transition_resource_state(context.list, indirect_copy, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT, D3D12_RESOURCE_STATE_COPY_DEST);
                transition_resource_state(context.list, indirect_copy, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT); /* --- split --- */
                transition_resource_state(context.list, indirect_copy, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT, D3D12_RESOURCE_STATE_COPY_DEST);
                transition_resource_state(context.list, indirect_copy, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT); /* --- split --- */
                transition_resource_state(context.list, indirect_copy, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT, D3D12_RESOURCE_STATE_COPY_DEST);
                transition_resource_state(context.list, indirect_copy, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT); /* --- split --- */
            }
        }

        assert(timestamp_index <= 64 * 1024);
        ID3D12GraphicsCommandList_ResolveQueryData(context.list, query_heap, D3D12_QUERY_TYPE_TIMESTAMP, 0, timestamp_index,
                query_readback, 0);

        ID3D12GraphicsCommandList_SetPredication(context.list, NULL, 0, D3D12_PREDICATION_OP_EQUAL_ZERO);
        transition_resource_state(context.list, output, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
        get_buffer_readback_with_command_list(output, DXGI_FORMAT_UNKNOWN, &rb, context.queue, context.list);
        reset_command_list(context.list, context.allocator);

        for (i = 0; i < 128; i++)
        {
            uint32_t expected_y;
            uint32_t expected_w;
            struct uvec4 value;
            uint32_t expected;

            for (j = 0; j < 6; j++)
            {
                value = *get_readback_uvec4(&rb, i + 128 * j, 0);

                expected = i == 127 ? 1000 : 0; /* Number of workgroups drawn */
                expected *= 64; /* multiplier from root CBV */

                if (j == 2 || j == 3)
                    expected = 0; /* predicated away */

                /* In iteration 5, we copied a zero indirect count. */
                expected_w = j == 4 ? 0 : expected;
                expected_y = expected * 16;

                /* of the gang of 4 dispatches, the first one should not do anything */
                ok(value.x == 0 && value.y == expected_y && value.z == expected && value.w == expected_w,
                    "Iteration %u, draw output %u: expected {%u, %u, %u, %u}, got {%u, %u, %u, %u}.\n", j, i, 0,
                    expected_y, expected, expected,
                    value.x, value.y, value.z, value.w);
            }
        }

        release_resource_readback(&rb);
        ID3D12Resource_Release(output);
        report_predication_timestamps(context.queue, query_readback, timestamp_index, "DGCC");
    }

    ID3D12Resource_Release(query_readback);
    ID3D12QueryHeap_Release(query_heap);

    ID3D12CommandSignature_Release(sig);
    ID3D12CommandSignature_Release(sig_compute);
    ID3D12Resource_Release(indirect_counts);
    ID3D12Resource_Release(indirect_graphics);
    ID3D12Resource_Release(indirect_compute);
    ID3D12Resource_Release(indirect_copy);
    ID3D12PipelineState_Release(pso_comp);
    ID3D12PipelineState_Release(pso_gfx);
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
    ID3D12RootSignature *root_signatures[2];
    ID3D12Resource *argument_buffer_late;
    D3D12_STREAM_OUTPUT_BUFFER_VIEW sov;
    ID3D12Resource *streamout_buffer;
    D3D12_VERTEX_BUFFER_VIEW vbvs[2];
    ID3D12Resource *argument_buffer;
    struct test_context_desc desc;
    ID3D12PipelineState *psos[2];
    struct test_context context;
    struct resource_readback rb;
    D3D12_INDEX_BUFFER_VIEW ibv;
    ID3D12CommandQueue *queue;
    const UINT so_stride = 16;
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
        uint32_t pso_index;
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

    /* Modify root parameters, but very large root signature to test boundary conditions. */
    static const D3D12_INDIRECT_ARGUMENT_DESC root_constant_spill_sig[2] =
    {
        { .Type = D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT, .Constant = {
                .RootParameterIndex = 0, .DestOffsetIn32BitValues = 44 + 1, .Num32BitValuesToSet = 2 }},
        { .Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED }
    };

    static const struct root_constant_data root_constant_spill_data[] =
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

    static const struct vec4 root_constant_spill_expected[] =
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

#define DECL_TEST(t, pso_index, needs_root_sig) { t##_sig, ARRAY_SIZE(t##_sig), t##_data, sizeof(t##_data), ARRAY_SIZE(t##_data), \
        t##_expected, ARRAY_SIZE(t##_expected), sizeof(*(t##_data)), pso_index, needs_root_sig }
    const struct test tests[] =
    {
        DECL_TEST(root_constant, 0, true),
        DECL_TEST(indirect_vbo, 0, false),
        DECL_TEST(indirect_vbo_one, 0, false),
        DECL_TEST(indirect_ibo, 0, false),
        DECL_TEST(indirect_root_descriptor, 0, true),
        DECL_TEST(indirect_alignment, 0, true),
        DECL_TEST(root_constant_spill, 1, true),
        DECL_TEST(indirect_root_descriptor, 1, true),
    };
#undef DECL_TEST

    uint32_t ibo_data[ARRAY_SIZE(ibo)][64];
    float vbo_data[ARRAY_SIZE(vbo)][64];
    float generic_data[4096];

#include "shaders/command/headers/execute_indirect_state_vs_code_small_cbv.h"
#include "shaders/command/headers/execute_indirect_state_vs_code_large_cbv.h"

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
    hr = create_root_signature(context.device, &root_signature_desc, &root_signatures[0]);
    ok(SUCCEEDED(hr), "Failed to create root signature, hr #%x.\n", hr);
    root_parameters[0].Constants.Num32BitValues = 48;
    hr = create_root_signature(context.device, &root_signature_desc, &root_signatures[1]);
    ok(SUCCEEDED(hr), "Failed to create root signature, hr #%x.\n", hr);

    memset(so_entries, 0, sizeof(so_entries));
    so_entries[0].ComponentCount = 4;
    so_entries[0].SemanticName = "SV_Position";

    memset(&pso_desc, 0, sizeof(pso_desc));
    pso_desc.VS = execute_indirect_state_vs_code_small_cbv_dxbc;
    pso_desc.StreamOutput.NumStrides = 1;
    pso_desc.StreamOutput.pBufferStrides = &so_stride;
    pso_desc.StreamOutput.pSODeclaration = so_entries;
    pso_desc.StreamOutput.NumEntries = ARRAY_SIZE(so_entries);
    pso_desc.StreamOutput.RasterizedStream = D3D12_SO_NO_RASTERIZED_STREAM;
    pso_desc.pRootSignature = root_signatures[0];
    pso_desc.SampleDesc.Count = 1;
    pso_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
    pso_desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    pso_desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    pso_desc.InputLayout.NumElements = ARRAY_SIZE(layout_desc);
    pso_desc.InputLayout.pInputElementDescs = layout_desc;
    hr = ID3D12Device_CreateGraphicsPipelineState(context.device, &pso_desc, &IID_ID3D12PipelineState, (void**)&psos[0]);
    ok(SUCCEEDED(hr), "Failed to create PSO, hr #%x.\n", hr);
    pso_desc.VS = execute_indirect_state_vs_code_large_cbv_dxbc;
    pso_desc.pRootSignature = root_signatures[1];
    hr = ID3D12Device_CreateGraphicsPipelineState(context.device, &pso_desc, &IID_ID3D12PipelineState, (void**)&psos[1]);
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
        bool root_cbv;
        uint32_t size;

        vkd3d_test_set_context("Test %u", i);

        command_signature_desc.ByteStride = tests[i].stride;
        command_signature_desc.pArgumentDescs = tests[i].indirect_arguments;
        command_signature_desc.NumArgumentDescs = tests[i].indirect_argument_count;
        command_signature_desc.NodeMask = 0;
        hr = ID3D12Device_CreateCommandSignature(context.device, &command_signature_desc,
                tests[i].needs_root_sig ? root_signatures[tests[i].pso_index] : NULL,
                &IID_ID3D12CommandSignature, (void**)&command_signature);

        /* Updating root CBV requires push BDA path, which we don't enable on NV by default yet. */
        root_cbv = false;
        for (j = 0; j < tests[i].indirect_argument_count; j++)
        {
            if (tests[i].indirect_arguments[j].Type == D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT_BUFFER_VIEW)
            {
                root_cbv = true;
                break;
            }
        }

        if (FAILED(hr))
        {
            if (root_cbv && is_nvidia_device(context.device))
                skip("Creating indirect root CBV update failed. If the GPU is NVIDIA, try VKD3D_CONFIG=force_raw_va_cbv.\n");
            else
                skip("Failed creating command signature, skipping test.\n");
            continue;
        }

        argument_buffer = create_upload_buffer(context.device, 256 * 1024, NULL);
        argument_buffer_late = create_default_buffer(context.device, 256 * 1024,
                D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);

#define UNALIGNED_ARGUMENT_BUFFER_OFFSET (64 * 1024 + 4)
#define UNALIGNED_COUNT_BUFFER_OFFSET (128 * 1024 + 4)
#define OVERSIZED_COUNT_BUFFER_OFFSET (128 * 1024 + 8)
#define ALIGNED_COUNT_BUFFER_OFFSET (128 * 1024 + 4 * 1024)
        {
            const uint32_t large_value = 1024;
            uint8_t *ptr;

            ID3D12Resource_Map(argument_buffer, 0, NULL, (void**)&ptr);
            memcpy(ptr, tests[i].argument_buffer_data, tests[i].argument_buffer_size);
            memcpy(ptr + UNALIGNED_ARGUMENT_BUFFER_OFFSET, tests[i].argument_buffer_data, tests[i].argument_buffer_size);
            memcpy(ptr + UNALIGNED_COUNT_BUFFER_OFFSET, &tests[i].api_max_count, sizeof(tests[i].api_max_count));
            memcpy(ptr + OVERSIZED_COUNT_BUFFER_OFFSET, &large_value, sizeof(large_value));
            memcpy(ptr + ALIGNED_COUNT_BUFFER_OFFSET, &tests[i].api_max_count, sizeof(tests[i].api_max_count));
            ID3D12Resource_Unmap(argument_buffer, 0, NULL);
        }

        streamout_buffer = create_default_buffer(context.device, 64 * 1024,
                D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_STREAM_OUT);

        ID3D12GraphicsCommandList_SetGraphicsRootSignature(command_list, root_signatures[tests[i].pso_index]);
        ID3D12GraphicsCommandList_SetPipelineState(command_list, psos[tests[i].pso_index]);
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

        for (j = 0; j < (tests[i].pso_index ? 12 : 1); j++)
            ID3D12GraphicsCommandList_SetGraphicsRoot32BitConstants(command_list, 0, 4, &values, 4 * j);

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
                argument_buffer, UNALIGNED_ARGUMENT_BUFFER_OFFSET,
                argument_buffer, UNALIGNED_COUNT_BUFFER_OFFSET);
        /* Test equivalent call with indirect count, but indirect count which needs to be clamped. */
        ID3D12GraphicsCommandList_ExecuteIndirect(command_list, command_signature, tests[i].api_max_count,
                argument_buffer, UNALIGNED_ARGUMENT_BUFFER_OFFSET,
                argument_buffer, OVERSIZED_COUNT_BUFFER_OFFSET);
        /* Test equivalent, but now with late transition to INDIRECT. */
        ID3D12GraphicsCommandList_CopyResource(command_list, argument_buffer_late, argument_buffer);
        transition_resource_state(command_list, argument_buffer_late, D3D12_RESOURCE_STATE_COPY_DEST,
                D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
        ID3D12GraphicsCommandList_ExecuteIndirect(command_list, command_signature, 1024,
                argument_buffer_late, 0, argument_buffer_late, ALIGNED_COUNT_BUFFER_OFFSET);

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

        expected_output_size = (tests[i].expected_output_count * 4 + 2) * sizeof(struct vec4);
        size = get_readback_uint(&rb, 0, 0, 0);
        ok(size == expected_output_size, "Expected size %u, got %u.\n", expected_output_size, size);

        for (j = 0; j < tests[i].expected_output_count; j++)
        {
            expect = &tests[i].expected_output[j];
            v = get_readback_vec4(&rb, j + 1, 0);
            ok(compare_vec4(v, expect, 0), "Element (direct count) %u failed: (%f, %f, %f, %f) != (%f, %f, %f, %f)\n",
                    j, v->x, v->y, v->z, v->w, expect->x, expect->y, expect->z, expect->w);

            v = get_readback_vec4(&rb, j + tests[i].expected_output_count + 1, 0);
            ok(compare_vec4(v, expect, 0), "Element (clamped count) %u failed: (%f, %f, %f, %f) != (%f, %f, %f, %f)\n",
                    j, v->x, v->y, v->z, v->w, expect->x, expect->y, expect->z, expect->w);

            v = get_readback_vec4(&rb, j + 2 * tests[i].expected_output_count + 1, 0);
            ok(compare_vec4(v, expect, 0), "Element (indirect count) %u failed: (%f, %f, %f, %f) != (%f, %f, %f, %f)\n",
                    j, v->x, v->y, v->z, v->w, expect->x, expect->y, expect->z, expect->w);

            v = get_readback_vec4(&rb, j + 3 * tests[i].expected_output_count + 1, 0);
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
                    (&expect_reset_state[0].x)[(tests[i].indirect_arguments[j].Constant.DestOffsetIn32BitValues + k) % 4] = 0.0f;
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
            v = get_readback_vec4(&rb, j + 1 + 4 * tests[i].expected_output_count, 0);
            expect = &expect_reset_state[j];
            ok(compare_vec4(v, expect, 0), "Post-reset element %u failed: (%f, %f, %f, %f) != (%f, %f, %f, %f)\n",
                    j, v->x, v->y, v->z, v->w, expect->x, expect->y, expect->z, expect->w);
        }

        ID3D12CommandSignature_Release(command_signature);
        ID3D12Resource_Release(argument_buffer);
        ID3D12Resource_Release(argument_buffer_late);
        ID3D12Resource_Release(streamout_buffer);
        release_resource_readback(&rb);
    }
    vkd3d_test_set_context(NULL);

    for (i = 0; i < ARRAY_SIZE(psos); i++)
        ID3D12PipelineState_Release(psos[i]);
    for (i = 0; i < ARRAY_SIZE(root_signatures); i++)
        ID3D12RootSignature_Release(root_signatures[i]);
    for (i = 0; i < ARRAY_SIZE(vbo); i++)
        ID3D12Resource_Release(vbo[i]);
    for (i = 0; i < ARRAY_SIZE(ibo); i++)
        ID3D12Resource_Release(ibo[i]);
    ID3D12Resource_Release(cbv);
    ID3D12Resource_Release(srv);
    ID3D12Resource_Release(uav);

    destroy_test_context(&context);
}

void test_execute_indirect_state_tier_11(void)
{
    enum { DRAW = 0, DRAW_INDEXED, DISPATCH, MESH, COUNT };

    ID3D12CommandSignature *command_signature[COUNT] = { NULL };
    D3D12_FEATURE_DATA_D3D12_OPTIONS21 options21;
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc;
    ID3D12PipelineState *psos[COUNT] = { NULL };
    D3D12_FEATURE_DATA_D3D12_OPTIONS7 options7;
    D3D12_INDIRECT_ARGUMENT_DESC arguments[2];
    struct test_context_desc context_desc;
    D3D12_COMMAND_SIGNATURE_DESC cs_desc;
    ID3D12Resource *indirect_args[COUNT];
    D3D12_ROOT_PARAMETER root_param[2];
    D3D12_ROOT_SIGNATURE_DESC rs_desc;
    struct resource_readback rb;
    struct test_context context;
    ID3D12Resource *output;
    ID3D12Resource *ibo;
    unsigned int i, j;
    HRESULT hr;

#include "shaders/command/headers/execute_indirect_tier11_draw.h"
#include "shaders/command/headers/execute_indirect_tier11_dispatch.h"
#include "shaders/command/headers/execute_indirect_tier11_mesh.h"

    static const D3D12_DRAW_ARGUMENTS draw_arguments[] = {
        { 14 * 3, 2 },
        { 19 * 3, 1 },
        { 15 * 3, 0 },
        { 251 * 3, 2 },
    };

    static const D3D12_DRAW_INDEXED_ARGUMENTS indexed_arguments[] = {
        { 100 * 3, 2 },
        { 4 * 3, 1 },
        { 4 * 3, 0 },
        { 251 * 3, 2 },
    };

    static const D3D12_DISPATCH_ARGUMENTS dispatch_arguments[] = {
        { 8, 5, 2 },
        { 1, 3, 10 },
        { 8, 5, 0 },
        { 4, 6, 20 },
    };

    uint32_t expected_counts[COUNT][ARRAY_SIZE(dispatch_arguments)];

    static const union d3d12_shader_bytecode_subobject ms_subobject = { { D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_MS,
        { execute_indirect_tier11_mesh_code_dxil, sizeof(execute_indirect_tier11_mesh_code_dxil) } } };

    static const union d3d12_root_signature_subobject root_signature_subobject =
    { {
        D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_ROOT_SIGNATURE,
        NULL, /* fill in dynamically */
    } };

    struct
    {
        union d3d12_root_signature_subobject root_signature;
        union d3d12_shader_bytecode_subobject ms;
    } ms_only_pipeline_desc = {
        root_signature_subobject, ms_subobject,
    };

    static const struct
    {
        const void *args;
        size_t size;
    } indirect_buffers[] = {
        { draw_arguments, sizeof(draw_arguments) },
        { indexed_arguments, sizeof(indexed_arguments) },
        { dispatch_arguments, sizeof(dispatch_arguments) },
        { dispatch_arguments, sizeof(dispatch_arguments) },
    };

    for (i = 0; i < ARRAY_SIZE(draw_arguments); i++)
        expected_counts[DRAW][i] = draw_arguments[i].VertexCountPerInstance * draw_arguments[i].InstanceCount;
    for (i = 0; i < ARRAY_SIZE(indexed_arguments); i++)
        expected_counts[DRAW_INDEXED][i] = indexed_arguments[i].IndexCountPerInstance * indexed_arguments[i].InstanceCount;
    for (i = 0; i < ARRAY_SIZE(dispatch_arguments); i++)
    {
        expected_counts[DISPATCH][i] = dispatch_arguments[i].ThreadGroupCountX * dispatch_arguments[i].ThreadGroupCountY * dispatch_arguments[i].ThreadGroupCountZ;
        expected_counts[MESH][i] = expected_counts[DISPATCH][i];
    }

    memset(&context_desc, 0, sizeof(context_desc));
    context_desc.no_pipeline = true;
    context_desc.no_root_signature = true;
    context_desc.no_render_target = true;
    if (!init_test_context(&context, &context_desc))
        return;

    if (FAILED(ID3D12Device_CheckFeatureSupport(context.device, D3D12_FEATURE_D3D12_OPTIONS21, &options21, sizeof(options21))) ||
        options21.ExecuteIndirectTier < D3D12_EXECUTE_INDIRECT_TIER_1_1)
    {
        skip("ExecuteIndirect tier 1.1 not supported.\n");
        destroy_test_context(&context);
        return;
    }

    if (FAILED(ID3D12Device_CheckFeatureSupport(context.device, D3D12_FEATURE_D3D12_OPTIONS7, &options7, sizeof(options7))) ||
        options7.MeshShaderTier < D3D12_MESH_SHADER_TIER_1)
    {
        options7.MeshShaderTier = D3D12_MESH_SHADER_TIER_NOT_SUPPORTED;
        skip("Mesh shader not supported, skipping mesh indirect test.\n");
    }

    output = create_default_buffer(context.device, 4096, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON);

    {
        uint32_t ibo_data[1024];
        for (i = 0; i < ARRAY_SIZE(ibo_data); i++)
            ibo_data[i] = i;
        ibo = create_upload_buffer(context.device, sizeof(ibo_data), ibo_data);
    }

    for (i = 0; i < COUNT; i++)
        indirect_args[i] = create_upload_buffer(context.device, indirect_buffers[i].size, indirect_buffers[i].args);

    memset(&cs_desc, 0, sizeof(cs_desc));
    cs_desc.NumArgumentDescs = ARRAY_SIZE(arguments);
    cs_desc.pArgumentDescs = arguments;

    arguments[0].Type = D3D12_INDIRECT_ARGUMENT_TYPE_INCREMENTING_CONSTANT;
    arguments[0].Constant.DestOffsetIn32BitValues = 0;
    arguments[0].Constant.Num32BitValuesToSet = 1;
    arguments[0].Constant.RootParameterIndex = 0;

    memset(&rs_desc, 0, sizeof(rs_desc));
    rs_desc.NumParameters = ARRAY_SIZE(root_param);
    rs_desc.pParameters = root_param;

    memset(root_param, 0, sizeof(root_param));
    root_param[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    root_param[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    root_param[0].Constants.Num32BitValues = 1;

    root_param[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    root_param[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    create_root_signature(context.device, &rs_desc, &context.root_signature);

    arguments[1].Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW;
    cs_desc.ByteStride = sizeof(D3D12_DRAW_ARGUMENTS);
    hr = ID3D12Device_CreateCommandSignature(context.device, &cs_desc, context.root_signature,
        &IID_ID3D12CommandSignature, (void **)&command_signature[DRAW]);
    ok(SUCCEEDED(hr), "Failed to create command signature, hr %x.\n", hr);
    init_pipeline_state_desc(&pso_desc, context.root_signature, DXGI_FORMAT_UNKNOWN, &execute_indirect_tier11_draw_dxbc, NULL, NULL);
    pso_desc.PS.BytecodeLength = 0;
    pso_desc.PS.pShaderBytecode = NULL;
    hr = ID3D12Device_CreateGraphicsPipelineState(context.device, &pso_desc, &IID_ID3D12PipelineState, (void **)&psos[DRAW]);
    ok(SUCCEEDED(hr), "Failed to create PSO, hr #%x.\n", hr);

    arguments[1].Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED;
    cs_desc.ByteStride = sizeof(D3D12_DRAW_INDEXED_ARGUMENTS);
    hr = ID3D12Device_CreateCommandSignature(context.device, &cs_desc, context.root_signature,
        &IID_ID3D12CommandSignature, (void **)&command_signature[DRAW_INDEXED]);
    ok(SUCCEEDED(hr), "Failed to create command signature, hr %x.\n", hr);
    psos[DRAW_INDEXED] = psos[DRAW];
    ID3D12PipelineState_AddRef(psos[DRAW_INDEXED]);

    arguments[1].Type = D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH;
    cs_desc.ByteStride = sizeof(D3D12_DISPATCH_ARGUMENTS);
    hr = ID3D12Device_CreateCommandSignature(context.device, &cs_desc, context.root_signature,
        &IID_ID3D12CommandSignature, (void **)&command_signature[DISPATCH]);
    ok(SUCCEEDED(hr), "Failed to create command signature, hr %x.\n", hr);
    psos[DISPATCH] = create_compute_pipeline_state(context.device, context.root_signature, execute_indirect_tier11_dispatch_dxbc);

    if (options7.MeshShaderTier >= D3D12_MESH_SHADER_TIER_1)
    {
        ID3D12Device2 *device2;

        ms_only_pipeline_desc.root_signature.root_signature = context.root_signature;
        ID3D12Device_QueryInterface(context.device, &IID_ID3D12Device2, (void **)&device2);
        arguments[1].Type = D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH_MESH;
        cs_desc.ByteStride = sizeof(D3D12_DISPATCH_MESH_ARGUMENTS);
        hr = ID3D12Device_CreateCommandSignature(context.device, &cs_desc, context.root_signature,
            &IID_ID3D12CommandSignature, (void **)&command_signature[MESH]);
        ok(SUCCEEDED(hr), "Failed to create command signature, hr %x.\n", hr);
        hr = create_pipeline_state_from_stream(device2, &ms_only_pipeline_desc, &psos[MESH]);
        ok(SUCCEEDED(hr), "Failed to create mesh PSO, hr #%x.\n", hr);
        ID3D12Device2_Release(device2);
    }

    ID3D12GraphicsCommandList_SetGraphicsRootSignature(context.list, context.root_signature);
    ID3D12GraphicsCommandList_SetComputeRootSignature(context.list, context.root_signature);
    ID3D12GraphicsCommandList_IASetPrimitiveTopology(context.list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    for (i = 0; i < COUNT; i++)
    {
        D3D12_INDEX_BUFFER_VIEW ibv;
        ID3D12GraphicsCommandList_SetGraphicsRootUnorderedAccessView(context.list, 1, ID3D12Resource_GetGPUVirtualAddress(output) + i * 1024);
        ID3D12GraphicsCommandList_SetComputeRootUnorderedAccessView(context.list, 1, ID3D12Resource_GetGPUVirtualAddress(output) + i * 1024);

        ibv.BufferLocation = ID3D12Resource_GetGPUVirtualAddress(ibo);
        ibv.Format = DXGI_FORMAT_R32_UINT;
        ibv.SizeInBytes = 1024 * sizeof(uint32_t);
        ID3D12GraphicsCommandList_IASetIndexBuffer(context.list, &ibv);

        if (psos[i])
        {
            ID3D12GraphicsCommandList_SetPipelineState(context.list, psos[i]);
            ID3D12GraphicsCommandList_ExecuteIndirect(context.list, command_signature[i], ARRAY_SIZE(draw_arguments), indirect_args[i], 0, NULL, 0);
        }
    }

    transition_resource_state(context.list, output, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);

    get_buffer_readback_with_command_list(output, DXGI_FORMAT_R32_UINT, &rb, context.queue, context.list);

    for (i = 0; i < COUNT; i++)
    {
        if (!psos[i])
            continue;

        for (j = 0; j < ARRAY_SIZE(draw_arguments); j++)
        {
            uint32_t expected = expected_counts[i][j];
            uint32_t v;

            v = get_readback_uint(&rb, 256 * i + j, 0, 0);
            ok(expected == v, "PSO %u, ID %u: Expected %u, got %u.\n", i, j, expected, v);
        }
    }

    for (i = 0; i < COUNT; i++)
    {
        ID3D12Resource_Release(indirect_args[i]);
        if (command_signature[i])
            ID3D12CommandSignature_Release(command_signature[i]);
        if (psos[i])
            ID3D12PipelineState_Release(psos[i]);
    }
    ID3D12Resource_Release(output);
    ID3D12Resource_Release(ibo);
    release_resource_readback(&rb);
    destroy_test_context(&context);
}

void test_execute_indirect_state_vbo_offsets(void)
{
    D3D12_INDIRECT_ARGUMENT_DESC indirect_args[2];
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc;
    D3D12_COMMAND_SIGNATURE_DESC cs_desc;
    D3D12_INPUT_LAYOUT_DESC input_layout;
    ID3D12CommandSignature *command_sig;
    D3D12_ROOT_SIGNATURE_DESC rs_desc;
    ID3D12Resource *indirect_buffer;
    ID3D12Resource *instance_buffer;
    D3D12_ROOT_PARAMETER rs_param;
    struct test_context_desc desc;
    D3D12_VERTEX_BUFFER_VIEW vbv;
    ID3D12Resource *index_buffer;
    struct resource_readback rb;
    struct test_context context;
    D3D12_INDEX_BUFFER_VIEW ibv;
    D3D12_VIEWPORT vp;
    D3D12_RECT sci;
    HRESULT hr;

    static const D3D12_INPUT_ELEMENT_DESC layout_desc[] =
    {
        {"I", 0, DXGI_FORMAT_R32_UINT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 0},
    };

    static const struct indirect_args
    {
        float values[4];
        D3D12_DRAW_INDEXED_ARGUMENTS draw_indexed;
    } draw = {
        { 100, 200, 300, 400 }, { 3, 1, 0, 0, 4 },
    };

    static const uint32_t instance_data[] = { 0, 1, 2, 3, 4, 5 };
    static const uint32_t index_buffer_data[] = { 0, 1, 2 };

#include "shaders/command/headers/indirect_state_vbo_offsets_vs.h"
#include "shaders/command/headers/indirect_state_vbo_offsets_ps.h"

    input_layout.NumElements = ARRAY_SIZE(layout_desc);
    input_layout.pInputElementDescs = layout_desc;

    memset(&desc, 0, sizeof(desc));
    desc.no_pipeline = true;
    desc.no_root_signature = true;
    desc.rt_format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    desc.rt_width = 1;
    desc.rt_height = 1;
    if (!init_test_context(&context, &desc))
        return;

    indirect_buffer = create_upload_buffer(context.device, sizeof(draw), &draw);
    instance_buffer = create_upload_buffer(context.device, sizeof(instance_data), instance_data);
    index_buffer = create_upload_buffer(context.device, sizeof(index_buffer_data), index_buffer_data);

    memset(&rs_desc, 0, sizeof(rs_desc));
    memset(&rs_param, 0, sizeof(rs_param));
    rs_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    rs_desc.NumParameters = 1;
    rs_desc.pParameters = &rs_param;
    rs_param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    rs_param.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    rs_param.Constants.Num32BitValues = 4;
    create_root_signature(context.device, &rs_desc, &context.root_signature);

    init_pipeline_state_desc(&pso_desc, context.root_signature, DXGI_FORMAT_R32G32B32A32_FLOAT,
        &indirect_state_vbo_offsets_vs_dxbc, &indirect_state_vbo_offsets_ps_dxbc, &input_layout);
    pso_desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    pso_desc.DepthStencilState.DepthEnable = FALSE;
    pso_desc.DepthStencilState.StencilEnable = FALSE;

    ID3D12Device_CreateGraphicsPipelineState(context.device, &pso_desc, &IID_ID3D12PipelineState, (void **)&context.pipeline_state);

    memset(&cs_desc, 0, sizeof(cs_desc));
    memset(indirect_args, 0, sizeof(indirect_args));
    cs_desc.NumArgumentDescs = ARRAY_SIZE(indirect_args);
    cs_desc.pArgumentDescs = indirect_args;
    cs_desc.ByteStride = sizeof(struct indirect_args);

    indirect_args[0].Type = D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT;
    indirect_args[0].Constant.Num32BitValuesToSet = 4;
    indirect_args[1].Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED;

    hr = ID3D12Device_CreateCommandSignature(context.device, &cs_desc, context.root_signature, &IID_ID3D12CommandSignature, (void **)&command_sig);
    ok(SUCCEEDED(hr) || hr == E_NOTIMPL, "Failed to create command signature.\n");
    if (FAILED(hr))
        command_sig = NULL;
    if (hr == E_NOTIMPL)
        skip("Failed to create command signature, DGC is likely not supported.\n");

    ID3D12GraphicsCommandList_OMSetRenderTargets(context.list, 1, &context.rtv, TRUE, NULL);
    ID3D12GraphicsCommandList_SetGraphicsRootSignature(context.list, context.root_signature);
    ID3D12GraphicsCommandList_SetPipelineState(context.list, context.pipeline_state);
    set_viewport(&vp, 0, 0, 1, 1, 0, 1);
    set_rect(&sci, 0, 0, 1, 1);
    ID3D12GraphicsCommandList_RSSetViewports(context.list, 1, &vp);
    ID3D12GraphicsCommandList_RSSetScissorRects(context.list, 1, &sci);
    ID3D12GraphicsCommandList_IASetPrimitiveTopology(context.list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ibv.BufferLocation = ID3D12Resource_GetGPUVirtualAddress(index_buffer);
    ibv.Format = DXGI_FORMAT_R32_UINT;
    ibv.SizeInBytes = sizeof(index_buffer_data);
    vbv.BufferLocation = ID3D12Resource_GetGPUVirtualAddress(instance_buffer);
    vbv.SizeInBytes = sizeof(instance_data);
    vbv.StrideInBytes = sizeof(uint32_t);
    ID3D12GraphicsCommandList_IASetIndexBuffer(context.list, &ibv);
    ID3D12GraphicsCommandList_IASetVertexBuffers(context.list, 0, 1, &vbv);

    if (command_sig)
        ID3D12GraphicsCommandList_ExecuteIndirect(context.list, command_sig, 1, indirect_buffer, 0, NULL, 0);

    transition_resource_state(context.list, context.render_target, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
    get_texture_readback_with_command_list(context.render_target, 0, &rb, context.queue, context.list);

    if (command_sig)
    {
        float r, g, b, a;
        r = get_readback_float(&rb, 0, 0);
        g = get_readback_float(&rb, 1, 0);
        b = get_readback_float(&rb, 2, 0);
        a = get_readback_float(&rb, 3, 0);
        ok(r == draw.values[0] + draw.draw_indexed.StartInstanceLocation, "r: Expected %f, got %f\n", draw.values[0] + draw.draw_indexed.StartInstanceLocation, r);
        ok(g == draw.values[1], "r: Expected %f, got %f\n", draw.values[1], g);
        ok(b == draw.values[2], "r: Expected %f, got %f\n", draw.values[2], b);
        ok(a == draw.values[3], "r: Expected %f, got %f\n", draw.values[3], a);
    }

    ID3D12Resource_Release(indirect_buffer);
    ID3D12Resource_Release(instance_buffer);
    ID3D12Resource_Release(index_buffer);
    if (command_sig)
        ID3D12CommandSignature_Release(command_sig);
    release_resource_readback(&rb);
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

#include "shaders/command/headers/execute_indirect_vs.h"
#include "shaders/command/headers/execute_indirect_ps.h"
#include "shaders/command/headers/execute_indirect_cs.h"

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
            context.root_signature, context.render_target_desc.Format,
            &execute_indirect_vs_dxbc, &execute_indirect_ps_dxbc, &input_layout);

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
    pipeline_state = create_compute_pipeline_state(context.device, root_signature, execute_indirect_cs_dxbc);
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

#include "shaders/command/headers/dispatch_zero_thread_groups.h"

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

    context.pipeline_state = create_compute_pipeline_state(context.device, context.root_signature, dispatch_zero_thread_groups_dxbc);

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

#include "shaders/command/headers/unaligned_vertex_stride_vs.h"
#include "shaders/command/headers/unaligned_vertex_stride_ps.h"

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
        context.root_signature, context.render_target_desc.Format,
        &unaligned_vertex_stride_vs_dxbc, &unaligned_vertex_stride_ps_dxbc, &input_layout);

    input_layout.pInputElementDescs = instance_layout_desc;
    input_layout.NumElements = ARRAY_SIZE(instance_layout_desc);
    instance_pipeline_state = create_pipeline_state(context.device,
        context.root_signature, context.render_target_desc.Format,
        &unaligned_vertex_stride_vs_dxbc, &unaligned_vertex_stride_ps_dxbc, &input_layout);

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

#include "shaders/command/headers/unaligned_vertex_stride_vs.h"
#include "shaders/command/headers/unaligned_vertex_stride_ps.h"

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
            context.root_signature, context.render_target_desc.Format,
            &unaligned_vertex_stride_vs_dxbc, &unaligned_vertex_stride_ps_dxbc, &input_layout);

    input_layout.pInputElementDescs = instance_layout_desc;
    input_layout.NumElements = ARRAY_SIZE(instance_layout_desc);
    instance_pipeline_state = create_pipeline_state(context.device,
            context.root_signature, context.render_target_desc.Format,
            &unaligned_vertex_stride_vs_dxbc, &unaligned_vertex_stride_ps_dxbc, &input_layout);

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

#include "shaders/command/headers/command_list_initial_pipeline_state.h"

    if (!init_test_context(&context, NULL))
        return;
    queue = context.queue;

    pipeline_state = create_pipeline_state(context.device,
            context.root_signature, context.render_target_desc.Format,
            NULL, &command_list_initial_pipeline_state_dxbc, NULL);

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

#include "shaders/command/headers/conditional_rendering.h"

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

    pipeline_state = create_compute_pipeline_state(context.device, root_signature, conditional_rendering_dxbc);

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
    D3D12_WRITEBUFFERIMMEDIATE_PARAMETER parameters[3];
    ID3D12GraphicsCommandList2 *command_list2;
    D3D12_WRITEBUFFERIMMEDIATE_MODE modes[3];
    ID3D12GraphicsCommandList *command_list;
    struct resource_readback rb;
    struct test_context context;
    ID3D12CommandQueue *queue;
    ID3D12Resource *buffer;
    ID3D12Device *device;
    unsigned int value;
    HRESULT hr;

    static const unsigned int data_values[] = {0xdeadbeef, 0xf00baa, 0xdeadbeef, 0xf00baa};

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
    parameters[2].Dest = parameters[0].Dest + sizeof(data_values[0]) * 3;
    parameters[2].Value = 0x5060708;
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
    value = get_readback_uint(&rb, 2, 0, 0);
    ok(value == data_values[2], "Got unexpected value %#x, expected %#x.\n", value, data_values[2]);
    value = get_readback_uint(&rb, 3, 0, 0);
    ok(value == parameters[2].Value, "Got unexpected value %#x, expected %#x.\n", value, parameters[2].Value);
    release_resource_readback(&rb);
    reset_command_list(command_list, context.allocator);

    parameters[0].Value = 0x2030405;
    parameters[1].Value = 0xb0c0d0e;
    parameters[2].Value = 0x708090a;
    modes[0] = D3D12_WRITEBUFFERIMMEDIATE_MODE_DEFAULT;
    modes[1] = D3D12_WRITEBUFFERIMMEDIATE_MODE_MARKER_IN;
    modes[2] = D3D12_WRITEBUFFERIMMEDIATE_MODE_MARKER_OUT;
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
    value = get_readback_uint(&rb, 3, 0, 0);
    ok(value == parameters[2].Value, "Got unexpected value %#x, expected %#x.\n", value, parameters[2].Value);
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

#include "shaders/command/headers/root_parameter_preservation_ps.h"
#include "shaders/command/headers/root_parameter_preservation_cs.h"

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

    init_pipeline_state_desc(&pso_desc, root_signature, desc.rt_format, NULL, &root_parameter_preservation_ps_dxbc, NULL);
    hr = ID3D12Device_CreateGraphicsPipelineState(context.device, &pso_desc, &IID_ID3D12PipelineState, (void **)&graphics_pso);
    ok(SUCCEEDED(hr), "Failed to create PSO, hr = #%x.\n", hr);
    compute_pso = create_compute_pipeline_state(context.device, root_signature, root_parameter_preservation_cs_dxbc);

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

#include "shaders/command/headers/cbv_hoisting.h"

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

    pso = create_compute_pipeline_state(context.device, root_signature, use_dxil ? cbv_hoisting_dxil : cbv_hoisting_dxbc);

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

#include "shaders/command/headers/conservative_rasterization_vs.h"
#include "shaders/command/headers/conservative_rasterization_ps.h"
#include "shaders/command/headers/conservative_rasterization_ps_underestimate.h"

    const D3D12_SHADER_BYTECODE ps_underestimate =
        use_dxil ? conservative_rasterization_ps_underestimate_dxil : conservative_rasterization_ps_underestimate_dxbc;

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
    init_pipeline_state_desc(&pso_desc, root_signature, DXGI_FORMAT_UNKNOWN,
        use_dxil ? &conservative_rasterization_vs_dxil : &conservative_rasterization_vs_dxbc,
        use_dxil ? &conservative_rasterization_ps_dxil : &conservative_rasterization_ps_dxbc, &input_layout);
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
    ID3D12GraphicsCommandList_SetGraphicsRootSignature(command_list, root_signature);
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

void test_uninit_root_parameters(void)
{
    D3D12_DESCRIPTOR_RANGE table_range[1];
    D3D12_ROOT_SIGNATURE_DESC rs_desc;
    D3D12_ROOT_PARAMETER rs_params[3];
    ID3D12DescriptorHeap *desc_heap;
    struct test_context context;
    ID3D12RootSignature *alt_rs;
    struct resource_readback rb;
    ID3D12Resource *buf[4];
    ID3D12Resource *output;
    unsigned int i, j;

#include "shaders/command/headers/uninit_root_parameters.h"

    if (!init_compute_test_context(&context))
        return;

    memset(&rs_desc, 0, sizeof(rs_desc));
    memset(rs_params, 0, sizeof(rs_params));
    memset(table_range, 0, sizeof(table_range));

    rs_desc.NumParameters = ARRAY_SIZE(rs_params);
    rs_desc.pParameters = rs_params;
    rs_params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    rs_params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rs_params[0].DescriptorTable.NumDescriptorRanges = ARRAY_SIZE(table_range);
    rs_params[0].DescriptorTable.pDescriptorRanges = table_range;
    table_range[0].NumDescriptors = 4;
    table_range[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    rs_params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    rs_params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    rs_params[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    rs_params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    rs_params[2].Constants.Num32BitValues = ARRAY_SIZE(buf);
    create_root_signature(context.device, &rs_desc, &context.root_signature);
    rs_params[2].Constants.Num32BitValues += 2;
    create_root_signature(context.device, &rs_desc, &alt_rs);
    context.pipeline_state = create_compute_pipeline_state(context.device, context.root_signature, uninit_root_parameters_dxbc);

    desc_heap = create_gpu_descriptor_heap(context.device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, ARRAY_SIZE(buf));

    for (i = 0; i < ARRAY_SIZE(buf); i++)
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc;
        D3D12_CPU_DESCRIPTOR_HANDLE cpu_h;
        float data[4];

        for (j = 0; j < 4; j++)
            data[j] = (float)(4 * i + 1 + j);
        buf[i] = create_upload_buffer(context.device, sizeof(data), data);

        srv_desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
        srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv_desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        srv_desc.Buffer.FirstElement = 0;
        srv_desc.Buffer.Flags = 0;
        srv_desc.Buffer.NumElements = 1;
        srv_desc.Buffer.StructureByteStride = 0;
        cpu_h = ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(desc_heap);
        cpu_h.ptr += ID3D12Device_GetDescriptorHandleIncrementSize(context.device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV) * i;
        ID3D12Device_CreateShaderResourceView(context.device, buf[i], &srv_desc, cpu_h);
    }

    output = create_default_buffer(context.device, 4096, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    /* First, check to see what happens when we let stale 32-bit constants and descriptor heap tables pass through a SetRootSignature call. */
    {
        D3D12_GPU_DESCRIPTOR_HANDLE gpu_h = ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(desc_heap);
        const struct vec4 expected = { 15.0f, 26.0f, 37.0f, 48.0f };
        const float data[4] = { 10, 20, 30, 40 };
        const struct vec4 *value;

        gpu_h.ptr += ID3D12Device_GetDescriptorHandleIncrementSize(context.device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        ID3D12GraphicsCommandList_SetDescriptorHeaps(context.list, 1, &desc_heap);
        ID3D12GraphicsCommandList_SetComputeRootSignature(context.list, alt_rs);
        ID3D12GraphicsCommandList_SetComputeRootDescriptorTable(context.list, 0, gpu_h);
        ID3D12GraphicsCommandList_SetComputeRootUnorderedAccessView(context.list, 1, ID3D12Resource_GetGPUVirtualAddress(output));
        ID3D12GraphicsCommandList_SetComputeRoot32BitConstants(context.list, 2, 4, data, 0);

        ID3D12GraphicsCommandList_SetComputeRootSignature(context.list, context.root_signature);
        ID3D12GraphicsCommandList_SetPipelineState(context.list, context.pipeline_state);
        ID3D12GraphicsCommandList_Dispatch(context.list, 1, 1, 1);

        transition_resource_state(context.list, output, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
        get_buffer_readback_with_command_list(output, DXGI_FORMAT_UNKNOWN, &rb, context.queue, context.list);
        reset_command_list(context.list, context.allocator);

        /* Setting a new root signature does not clear out any state. This does not even trigger a warning, but it can hang AMD GPUs ... */
        value = get_readback_vec4(&rb, 0, 0);
        ok(compare_vec4(value, &expected, 0), "Expected (%f, %f, %f, %f), got (%f, %f, %f, %f).\n",
                expected.x, expected.y, expected.z, expected.w,
                value->x, value->y, value->z, value->w);
        release_resource_readback(&rb);
    }

    /* Try not setting root constants. Expect zeroed state. */
    {
        D3D12_GPU_DESCRIPTOR_HANDLE gpu_h = ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(desc_heap);
        const struct vec4 expected = { 5.0f, 6.0f, 7.0f, 8.0f };
        const struct vec4 *value;

        gpu_h.ptr += ID3D12Device_GetDescriptorHandleIncrementSize(context.device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        ID3D12GraphicsCommandList_SetDescriptorHeaps(context.list, 1, &desc_heap);
        ID3D12GraphicsCommandList_SetComputeRootSignature(context.list, context.root_signature);
        ID3D12GraphicsCommandList_SetComputeRootDescriptorTable(context.list, 0, gpu_h);
        ID3D12GraphicsCommandList_SetComputeRootUnorderedAccessView(context.list, 1, ID3D12Resource_GetGPUVirtualAddress(output) + 16);
        ID3D12GraphicsCommandList_SetPipelineState(context.list, context.pipeline_state);
        ID3D12GraphicsCommandList_Dispatch(context.list, 1, 1, 1);

        transition_resource_state(context.list, output, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
        get_buffer_readback_with_command_list(output, DXGI_FORMAT_UNKNOWN, &rb, context.queue, context.list);
        reset_command_list(context.list, context.allocator);

        /* Setting a new root signature does not clear out any state. This does not even trigger a warning, but it can hang AMD GPUs ... */
        value = get_readback_vec4(&rb, 1, 0);
        ok(compare_vec4(value, &expected, 0), "Expected (%f, %f, %f, %f), got (%f, %f, %f, %f).\n",
                expected.x, expected.y, expected.z, expected.w,
                value->x, value->y, value->z, value->w);
        release_resource_readback(&rb);
    }

    /* See what happens on ClearState(). It should be equivalent to a Reset(), i.e. cleared state. */
    {
        D3D12_GPU_DESCRIPTOR_HANDLE gpu_h = ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(desc_heap);
        const struct vec4 expected = { 5.0f, 6.0f, 7.0f, 8.0f };
        const float data[4] = { 10, 20, 30, 40 };
        const struct vec4 *value;

        gpu_h.ptr += ID3D12Device_GetDescriptorHandleIncrementSize(context.device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        ID3D12GraphicsCommandList_SetDescriptorHeaps(context.list, 1, &desc_heap);
        ID3D12GraphicsCommandList_SetComputeRootSignature(context.list, alt_rs);
        ID3D12GraphicsCommandList_SetComputeRootDescriptorTable(context.list, 0, gpu_h);
        ID3D12GraphicsCommandList_SetComputeRootUnorderedAccessView(context.list, 1, ID3D12Resource_GetGPUVirtualAddress(output));
        ID3D12GraphicsCommandList_SetComputeRoot32BitConstants(context.list, 2, 4, data, 0);

        /* Root parameter state is cleared to zero in ClearState. Have to set table + root descriptor or weird things happen. */
        ID3D12GraphicsCommandList_ClearState(context.list, NULL);
        ID3D12GraphicsCommandList_SetDescriptorHeaps(context.list, 1, &desc_heap);
        ID3D12GraphicsCommandList_SetComputeRootSignature(context.list, context.root_signature);
        ID3D12GraphicsCommandList_SetComputeRootDescriptorTable(context.list, 0, gpu_h);
        ID3D12GraphicsCommandList_SetComputeRootUnorderedAccessView(context.list, 1, ID3D12Resource_GetGPUVirtualAddress(output) + 32);

        ID3D12GraphicsCommandList_SetPipelineState(context.list, context.pipeline_state);
        ID3D12GraphicsCommandList_Dispatch(context.list, 1, 1, 1);

        transition_resource_state(context.list, output, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
        get_buffer_readback_with_command_list(output, DXGI_FORMAT_UNKNOWN, &rb, context.queue, context.list);
        reset_command_list(context.list, context.allocator);

        value = get_readback_vec4(&rb, 2, 0);
        ok(compare_vec4(value, &expected, 0), "Expected (%f, %f, %f, %f), got (%f, %f, %f, %f).\n",
                expected.x, expected.y, expected.z, expected.w,
                value->x, value->y, value->z, value->w);
        release_resource_readback(&rb);
    }

    ID3D12RootSignature_Release(alt_rs);
    ID3D12Resource_Release(output);
    for (i = 0; i < ARRAY_SIZE(buf); i++)
        ID3D12Resource_Release(buf[i]);
    ID3D12DescriptorHeap_Release(desc_heap);
    destroy_test_context(&context);
}
