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

struct triangle
{
    struct vec4 v[3];
};

#define check_triangles(a, b, c, d, e) check_triangles_(__LINE__, a, b, c, d, e)
static void check_triangles_(unsigned int line, ID3D12Resource *buffer,
        ID3D12CommandQueue *queue, ID3D12GraphicsCommandList *command_list,
        const struct triangle *triangles, unsigned int triangle_count)
{
    const struct triangle *current, *expected;
    struct resource_readback rb;
    unsigned int i, j, offset;
    bool all_match = true;

    get_buffer_readback_with_command_list(buffer, DXGI_FORMAT_UNKNOWN, &rb, queue, command_list);

    for (i = 0; i < triangle_count; ++i)
    {
        current = get_readback_data(&rb, i, 0, 0, sizeof(*current));
        expected = &triangles[i];

        offset = ~0u;
        for (j = 0; j < ARRAY_SIZE(expected->v); ++j)
        {
            if (compare_vec4(&current->v[0], &expected->v[j], 0))
            {
                offset = j;
                break;
            }
        }

        if (offset == ~0u)
        {
            all_match = false;
            break;
        }

        for (j = 0; j < ARRAY_SIZE(expected->v); ++j)
        {
            if (!compare_vec4(&current->v[j], &expected->v[(j + offset) % 3], 0))
            {
                all_match = false;
                break;
            }
        }
        if (!all_match)
            break;
    }

    ok_(line)(all_match, "Triangle %u vertices {%.8e, %.8e, %.8e, %.8e}, "
            "{%.8e, %.8e, %.8e, %.8e}, {%.8e, %.8e, %.8e, %.8e} "
            "do not match {%.8e, %.8e, %.8e, %.8e}, {%.8e, %.8e, %.8e, %.8e}, "
            "{%.8e, %.8e, %.8e, %.8e}.\n", i,
            current->v[0].x, current->v[0].y, current->v[0].z, current->v[0].w,
            current->v[1].x, current->v[1].y, current->v[1].z, current->v[1].w,
            current->v[2].x, current->v[2].y, current->v[2].z, current->v[2].w,
            expected->v[0].x, expected->v[0].y, expected->v[0].z, expected->v[0].w,
            expected->v[1].x, expected->v[1].y, expected->v[1].z, expected->v[1].w,
            expected->v[2].x, expected->v[2].y, expected->v[2].z, expected->v[2].w);

    release_resource_readback(&rb);
}

void test_nop_tessellation_shaders(void)
{
    static const float white[] = {1.0f, 1.0f, 1.0f, 1.0f};
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc;
    ID3D12GraphicsCommandList *command_list;
    struct test_context_desc desc;
    struct test_context context;
    ID3D12CommandQueue *queue;
    struct vec4 tess_factors;
    unsigned int i;
    HRESULT hr;

#include "shaders/tessellation/headers/nop_cb.h"
#include "shaders/tessellation/headers/nop_ds.h"
#include "shaders/tessellation/headers/nop_hs.h"
#include "shaders/tessellation/headers/nop_hs_clip_distance.h"
#include "shaders/tessellation/headers/nop_ds_clip_distance.h"
#include "shaders/tessellation/headers/nop_hs_index_range.h"

    static const D3D12_SHADER_BYTECODE *hull_shaders[] = { &nop_cb_dxbc, &nop_hs_dxbc, &nop_hs_index_range_dxbc, &nop_hs_clip_distance_dxbc };
    static const D3D12_SHADER_BYTECODE *domain_shaders[] = { &nop_ds_dxbc, &nop_ds_dxbc, &nop_ds_dxbc, &nop_ds_clip_distance_dxbc };

    memset(&desc, 0, sizeof(desc));
    desc.no_root_signature = true;
    if (!init_test_context(&context, &desc))
        return;
    command_list = context.list;
    queue = context.queue;

    context.root_signature = create_32bit_constants_root_signature(context.device,
            0, 4, D3D12_SHADER_VISIBILITY_HULL);

    init_pipeline_state_desc(&pso_desc, context.root_signature,
            context.render_target_desc.Format, NULL, NULL, NULL);
    pso_desc.HS = nop_cb_dxbc;
    pso_desc.DS = nop_ds_dxbc;
    hr = ID3D12Device_CreateGraphicsPipelineState(context.device, &pso_desc,
            &IID_ID3D12PipelineState, (void **)&context.pipeline_state);
    /* Runtime used to fail this, but it "just works" now :|. */
    ok(hr == S_OK, "Got unexpected hr %#x.\n", hr);
    ID3D12PipelineState_Release(context.pipeline_state);
    pso_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH;
    hr = ID3D12Device_CreateGraphicsPipelineState(context.device, &pso_desc,
            &IID_ID3D12PipelineState, (void **)&context.pipeline_state);
    ok(hr == S_OK, "Failed to create state, hr %#x.\n", hr);
    ID3D12PipelineState_Release(context.pipeline_state);

    for (i = 0; i < ARRAY_SIZE(hull_shaders); ++i)
    {
        vkd3d_test_set_context("Test %u", i);

        pso_desc.HS = *hull_shaders[i];
        pso_desc.DS = *domain_shaders[i];
        hr = ID3D12Device_CreateGraphicsPipelineState(context.device, &pso_desc,
                &IID_ID3D12PipelineState, (void **)&context.pipeline_state);
        ok(hr == S_OK, "Failed to create state, hr %#x.\n", hr);

        ID3D12GraphicsCommandList_ClearRenderTargetView(command_list, context.rtv, white, 0, NULL);

        ID3D12GraphicsCommandList_OMSetRenderTargets(command_list, 1, &context.rtv, false, NULL);
        ID3D12GraphicsCommandList_SetGraphicsRootSignature(command_list, context.root_signature);
        ID3D12GraphicsCommandList_SetPipelineState(command_list, context.pipeline_state);
        ID3D12GraphicsCommandList_IASetPrimitiveTopology(command_list, D3D_PRIMITIVE_TOPOLOGY_3_CONTROL_POINT_PATCHLIST);
        ID3D12GraphicsCommandList_RSSetViewports(command_list, 1, &context.viewport);
        ID3D12GraphicsCommandList_RSSetScissorRects(command_list, 1, &context.scissor_rect);
        tess_factors.x = tess_factors.y = tess_factors.z = tess_factors.w = 1.0f;
        ID3D12GraphicsCommandList_SetGraphicsRoot32BitConstants(command_list, 0, 4, &tess_factors.x, 0);
        ID3D12GraphicsCommandList_DrawInstanced(command_list, 3, 1, 0, 0);

        transition_resource_state(command_list, context.render_target,
                D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);

        check_sub_resource_uint(context.render_target, 0, queue, command_list, 0xff00ff00, 0);

        reset_command_list(command_list, context.allocator);
        transition_resource_state(command_list, context.render_target,
                D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);

        ID3D12PipelineState_Release(context.pipeline_state);
        context.pipeline_state = NULL;
    }
    vkd3d_test_set_context(NULL);

    destroy_test_context(&context);
}

static void test_quad_tessellation(bool use_dxil, bool wrong_pso_topology, bool wrong_input_count)
{
#include "shaders/tessellation/headers/quad_tess_vs.h"
#include "shaders/tessellation/headers/quad_tess_hs_ccw.h"
#include "shaders/tessellation/headers/quad_tess_hs_ccw_overcount_input.h"
#include "shaders/tessellation/headers/quad_tess_hs_cw_undercount_input.h"
#include "shaders/tessellation/headers/quad_tess_hs_cw.h"
#include "shaders/tessellation/headers/quad_tess_ds.h"

    static const struct vec4 quad[] =
    {
        {-1.0f, -1.0f, 0.0f, 1.0f},
        {-1.0f,  1.0f, 0.0f, 1.0f},
        { 1.0f, -1.0f, 0.0f, 1.0f},
        { 1.0f,  1.0f, 0.0f, 1.0f},
    };
    static const D3D12_INPUT_ELEMENT_DESC layout_desc[] =
    {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    };
    static const D3D12_SO_DECLARATION_ENTRY so_declaration[] =
    {
        {0, "SV_POSITION", 0, 0, 4, 0},
    };
    unsigned int strides[] = {16};
    static const float white[] = {1.0f, 1.0f, 1.0f, 1.0f};
    static const BYTE zero_data[2048];
    static const struct triangle expected_quad_ccw[] =
    {
        {{{-1.0f, -1.0f, 0.0f, 1.0f},
          { 1.0f, -1.0f, 0.0f, 1.0f},
          {-1.0f,  1.0f, 0.0f, 1.0f}}},
        {{{-1.0f,  1.0f, 0.0f, 1.0f},
          { 1.0f, -1.0f, 0.0f, 1.0f},
          { 1.0f,  1.0f, 0.0f, 1.0f}}},
        {{{ 0.0f,  0.0f, 0.0f, 0.0f},
          { 0.0f,  0.0f, 0.0f, 0.0f},
          { 0.0f,  0.0f, 0.0f, 0.0f}}},
    };
    static const struct triangle expected_quad_cw[] =
    {
        {{{-1.0f, -1.0f, 0.0f, 1.0f},
          {-1.0f,  1.0f, 0.0f, 1.0f},
          { 1.0f, -1.0f, 0.0f, 1.0f}}},
        {{{-1.0f,  1.0f, 0.0f, 1.0f},
          { 1.0f,  1.0f, 0.0f, 1.0f},
          { 1.0f, -1.0f, 0.0f, 1.0f}}},
        {{{ 0.0f,  0.0f, 0.0f, 0.0f},
          { 0.0f,  0.0f, 0.0f, 0.0f},
          { 0.0f,  0.0f, 0.0f, 0.0f}}},
    };
    struct
    {
        float tess_factors[4];
        float inside_tess_factors[2];
        uint32_t padding[2];
    } constant;

    ID3D12Resource *vb, *so_buffer, *upload_buffer, *readback_buffer;
    D3D12_QUERY_DATA_SO_STATISTICS *so_statistics;
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc;
    ID3D12GraphicsCommandList *command_list;
    D3D12_QUERY_HEAP_DESC query_heap_desc;
    D3D12_STREAM_OUTPUT_BUFFER_VIEW sobv;
    D3D12_INPUT_LAYOUT_DESC input_layout;
    struct test_context_desc desc;
    D3D12_VERTEX_BUFFER_VIEW vbv;
    struct resource_readback rb;
    ID3D12QueryHeap *query_heap;
    struct test_context context;
    ID3D12CommandQueue *queue;
    ID3D12Device *device;
    unsigned int i;
    HRESULT hr;

    memset(&desc, 0, sizeof(desc));
    desc.no_root_signature = true;
    if (!init_test_context(&context, &desc))
        return;

    if (use_dxil && !context_supports_dxil(&context))
    {
        destroy_test_context(&context);
        return;
    }

    device = context.device;
    command_list = context.list;
    queue = context.queue;

    query_heap_desc.Type = D3D12_QUERY_HEAP_TYPE_SO_STATISTICS;
    query_heap_desc.Count = 2;
    query_heap_desc.NodeMask = 0;
    hr = ID3D12Device_CreateQueryHeap(device, &query_heap_desc, &IID_ID3D12QueryHeap, (void **)&query_heap);
    if (hr == E_NOTIMPL)
    {
        skip("Stream output is not supported.\n");
        destroy_test_context(&context);
        return;
    }
    ok(hr == S_OK, "Failed to create query heap, hr %#x.\n", hr);

    context.root_signature = create_32bit_constants_root_signature_(__LINE__,
            device, 0, 6, D3D12_SHADER_VISIBILITY_HULL,
            D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT
            | D3D12_ROOT_SIGNATURE_FLAG_ALLOW_STREAM_OUTPUT);

    input_layout.pInputElementDescs = layout_desc;
    input_layout.NumElements = ARRAY_SIZE(layout_desc);

    if (use_dxil)
    {
        init_pipeline_state_desc_dxil(&pso_desc, context.root_signature,
                context.render_target_desc.Format, NULL, NULL, &input_layout);
    }
    else
    {
        init_pipeline_state_desc(&pso_desc, context.root_signature,
                context.render_target_desc.Format, NULL, NULL, &input_layout);
    }
    pso_desc.VS = use_dxil ? quad_tess_vs_dxil : quad_tess_vs_dxbc;
    pso_desc.HS = use_dxil ? quad_tess_hs_cw_dxil : quad_tess_hs_cw_dxbc;
    pso_desc.DS = use_dxil ? quad_tess_ds_dxil : quad_tess_ds_dxbc;

    if (wrong_input_count)
        pso_desc.HS = use_dxil ? quad_tess_hs_cw_undercount_input_dxil : quad_tess_hs_cw_undercount_input_dxbc;

    pso_desc.StreamOutput.NumEntries = ARRAY_SIZE(so_declaration);
    pso_desc.StreamOutput.pSODeclaration = so_declaration;
    pso_desc.StreamOutput.pBufferStrides = strides;
    pso_desc.StreamOutput.NumStrides = ARRAY_SIZE(strides);
    pso_desc.StreamOutput.RasterizedStream = 0;

    if (!wrong_pso_topology)
        pso_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH;
    else
    {
        /* This seems to "just work" on native drivers, despite runtime validating it. */
        pso_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    }

    vb = create_upload_buffer(device, sizeof(quad), quad);

    vbv.BufferLocation = ID3D12Resource_GetGPUVirtualAddress(vb);
    vbv.StrideInBytes = sizeof(*quad);
    vbv.SizeInBytes = sizeof(quad);

    upload_buffer = create_upload_buffer(device, sizeof(zero_data), &zero_data);

    so_buffer = create_default_buffer(device, sizeof(zero_data),
            D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);

    ID3D12GraphicsCommandList_CopyBufferRegion(command_list, so_buffer, 0,
            upload_buffer, 0, sizeof(zero_data));
    transition_resource_state(command_list, so_buffer,
            D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_STREAM_OUT);

    sobv.BufferLocation = ID3D12Resource_GetGPUVirtualAddress(so_buffer);
    sobv.SizeInBytes = 1024;
    sobv.BufferFilledSizeLocation = sobv.BufferLocation + sobv.SizeInBytes;

    for (i = 0; i < ARRAY_SIZE(constant.tess_factors); ++i)
        constant.tess_factors[i] = 1.0f;
    for (i = 0; i < ARRAY_SIZE(constant.inside_tess_factors); ++i)
        constant.inside_tess_factors[i] = 1.0f;

    if (wrong_input_count)
        pso_desc.HS = use_dxil ? quad_tess_hs_ccw_overcount_input_dxil : quad_tess_hs_ccw_overcount_input_dxbc;
    else
        pso_desc.HS = use_dxil ? quad_tess_hs_ccw_dxil : quad_tess_hs_ccw_dxbc;
    hr = ID3D12Device_CreateGraphicsPipelineState(device, &pso_desc,
            &IID_ID3D12PipelineState, (void **)&context.pipeline_state);
    ok(hr == S_OK, "Failed to create state, hr %#x.\n", hr);

    ID3D12GraphicsCommandList_ClearRenderTargetView(command_list, context.rtv, white, 0, NULL);

    ID3D12GraphicsCommandList_OMSetRenderTargets(command_list, 1, &context.rtv, false, NULL);
    ID3D12GraphicsCommandList_SetGraphicsRootSignature(command_list, context.root_signature);
    ID3D12GraphicsCommandList_SetPipelineState(command_list, context.pipeline_state);
    ID3D12GraphicsCommandList_IASetPrimitiveTopology(command_list, D3D_PRIMITIVE_TOPOLOGY_4_CONTROL_POINT_PATCHLIST);
    ID3D12GraphicsCommandList_RSSetViewports(command_list, 1, &context.viewport);
    ID3D12GraphicsCommandList_RSSetScissorRects(command_list, 1, &context.scissor_rect);
    ID3D12GraphicsCommandList_SetGraphicsRoot32BitConstants(command_list, 0, 6, &constant, 0);
    ID3D12GraphicsCommandList_IASetVertexBuffers(command_list, 0, 1, &vbv);
    ID3D12GraphicsCommandList_SOSetTargets(command_list, 0, 1, &sobv);
    ID3D12GraphicsCommandList_DrawInstanced(command_list, 4, 1, 0, 0);

    transition_resource_state(command_list, context.render_target,
            D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
    check_sub_resource_uint(context.render_target, 0, queue, command_list, 0xffffffff, 0);

    reset_command_list(command_list, context.allocator);
    transition_resource_state(command_list, context.render_target,
            D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);

    transition_resource_state(command_list, so_buffer,
            D3D12_RESOURCE_STATE_STREAM_OUT, D3D12_RESOURCE_STATE_COPY_SOURCE);
    check_triangles(so_buffer, queue, command_list, expected_quad_ccw, ARRAY_SIZE(expected_quad_ccw));

    reset_command_list(command_list, context.allocator);
    transition_resource_state(command_list, so_buffer,
            D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
    ID3D12GraphicsCommandList_CopyBufferRegion(command_list, so_buffer, 0,
            upload_buffer, 0, sizeof(zero_data));
    transition_resource_state(command_list, so_buffer,
            D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_STREAM_OUT);

    ID3D12PipelineState_Release(context.pipeline_state);
    pso_desc.HS = use_dxil ? quad_tess_hs_cw_dxil : quad_tess_hs_cw_dxbc;
    hr = ID3D12Device_CreateGraphicsPipelineState(device, &pso_desc,
            &IID_ID3D12PipelineState, (void **)&context.pipeline_state);
    ok(hr == S_OK, "Failed to create state, hr %#x.\n", hr);

    ID3D12GraphicsCommandList_ClearRenderTargetView(command_list, context.rtv, white, 0, NULL);

    ID3D12GraphicsCommandList_OMSetRenderTargets(command_list, 1, &context.rtv, false, NULL);
    ID3D12GraphicsCommandList_SetGraphicsRootSignature(command_list, context.root_signature);
    ID3D12GraphicsCommandList_SetPipelineState(command_list, context.pipeline_state);
    ID3D12GraphicsCommandList_IASetPrimitiveTopology(command_list, D3D_PRIMITIVE_TOPOLOGY_4_CONTROL_POINT_PATCHLIST);
    ID3D12GraphicsCommandList_RSSetViewports(command_list, 1, &context.viewport);
    ID3D12GraphicsCommandList_RSSetScissorRects(command_list, 1, &context.scissor_rect);
    ID3D12GraphicsCommandList_SetGraphicsRoot32BitConstants(command_list, 0, 6, &constant, 0);
    ID3D12GraphicsCommandList_IASetVertexBuffers(command_list, 0, 1, &vbv);
    ID3D12GraphicsCommandList_SOSetTargets(command_list, 0, 1, &sobv);
    ID3D12GraphicsCommandList_DrawInstanced(command_list, 4, 1, 0, 0);

    transition_resource_state(command_list, context.render_target,
            D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
    check_sub_resource_uint(context.render_target, 0, queue, command_list, 0xff00ff00, 0);

    reset_command_list(command_list, context.allocator);
    transition_resource_state(command_list, context.render_target,
            D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);

    transition_resource_state(command_list, so_buffer,
            D3D12_RESOURCE_STATE_STREAM_OUT, D3D12_RESOURCE_STATE_COPY_SOURCE);
    check_triangles(so_buffer, queue, command_list, expected_quad_cw, ARRAY_SIZE(expected_quad_cw));

    reset_command_list(command_list, context.allocator);
    transition_resource_state(command_list, so_buffer,
            D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
    ID3D12GraphicsCommandList_CopyBufferRegion(command_list, so_buffer, 0,
            upload_buffer, 0, sizeof(zero_data));
    transition_resource_state(command_list, so_buffer,
            D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_STREAM_OUT);

    ID3D12GraphicsCommandList_OMSetRenderTargets(command_list, 1, &context.rtv, false, NULL);
    ID3D12GraphicsCommandList_SetGraphicsRootSignature(command_list, context.root_signature);
    ID3D12GraphicsCommandList_SetPipelineState(command_list, context.pipeline_state);
    ID3D12GraphicsCommandList_IASetPrimitiveTopology(command_list, D3D_PRIMITIVE_TOPOLOGY_4_CONTROL_POINT_PATCHLIST);
    ID3D12GraphicsCommandList_RSSetViewports(command_list, 1, &context.viewport);
    ID3D12GraphicsCommandList_RSSetScissorRects(command_list, 1, &context.scissor_rect);
    ID3D12GraphicsCommandList_IASetVertexBuffers(command_list, 0, 1, &vbv);
    ID3D12GraphicsCommandList_SOSetTargets(command_list, 0, 1, &sobv);

    ID3D12GraphicsCommandList_BeginQuery(command_list, query_heap, D3D12_QUERY_TYPE_SO_STATISTICS_STREAM0, 0);

    for (i = 0; i < ARRAY_SIZE(constant.tess_factors); ++i)
        constant.tess_factors[i] = 2.0f;
    ID3D12GraphicsCommandList_SetGraphicsRoot32BitConstants(command_list, 0, 6, &constant, 0);
    ID3D12GraphicsCommandList_DrawInstanced(command_list, 4, 1, 0, 0);

    constant.tess_factors[0] = 0.0f; /* A patch is discarded. */
    ID3D12GraphicsCommandList_SetGraphicsRoot32BitConstants(command_list, 0, 6, &constant, 0);
    ID3D12GraphicsCommandList_DrawInstanced(command_list, 4, 1, 0, 0);

    ID3D12GraphicsCommandList_EndQuery(command_list, query_heap, D3D12_QUERY_TYPE_SO_STATISTICS_STREAM0, 0);

    ID3D12GraphicsCommandList_BeginQuery(command_list, query_heap, D3D12_QUERY_TYPE_SO_STATISTICS_STREAM0, 1);

    constant.tess_factors[0] = 5.0f;
    ID3D12GraphicsCommandList_SetGraphicsRoot32BitConstants(command_list, 0, 6, &constant, 0);
    ID3D12GraphicsCommandList_DrawInstanced(command_list, 4, 1, 0, 0);

    ID3D12GraphicsCommandList_EndQuery(command_list, query_heap, D3D12_QUERY_TYPE_SO_STATISTICS_STREAM0, 1);

    readback_buffer = create_readback_buffer(device, 2 * sizeof(*so_statistics));
    ID3D12GraphicsCommandList_ResolveQueryData(command_list,
            query_heap, D3D12_QUERY_TYPE_SO_STATISTICS_STREAM0, 0, 2, readback_buffer, 0);

    get_buffer_readback_with_command_list(readback_buffer, DXGI_FORMAT_UNKNOWN, &rb, queue, command_list);
    so_statistics = get_readback_data(&rb, 0, 0, 0, sizeof(*so_statistics));
    ok(so_statistics[0].NumPrimitivesWritten == 8, "Got unexpected primitives written %u.\n",
            (unsigned int)so_statistics[0].NumPrimitivesWritten);
    ok(so_statistics[0].PrimitivesStorageNeeded == 8, "Got unexpected primitives storage needed %u.\n",
            (unsigned int)so_statistics[0].PrimitivesStorageNeeded);
    ok(so_statistics[1].NumPrimitivesWritten == 11, "Got unexpected primitives written %u.\n",
            (unsigned int)so_statistics[1].NumPrimitivesWritten);
    ok(so_statistics[1].PrimitivesStorageNeeded == 11, "Got unexpected primitives storage needed %u.\n",
            (unsigned int)so_statistics[1].PrimitivesStorageNeeded);
    release_resource_readback(&rb);

    ID3D12Resource_Release(readback_buffer);
    ID3D12Resource_Release(so_buffer);
    ID3D12Resource_Release(upload_buffer);
    ID3D12Resource_Release(vb);
    ID3D12QueryHeap_Release(query_heap);
    destroy_test_context(&context);
}

void test_quad_tessellation_dxbc(void)
{
    test_quad_tessellation(false, false, false);
}

void test_quad_tessellation_dxil(void)
{
    test_quad_tessellation(true, false, false);
}

void test_quad_tessellation_wrong_pso_topology_dxbc(void)
{
    test_quad_tessellation(false, true, false);
}

void test_quad_tessellation_wrong_pso_topology_dxil(void)
{
    test_quad_tessellation(true, true, false);
}

void test_quad_tessellation_wrong_input_count_dxbc(void)
{
    test_quad_tessellation(false, false, true);
}

void test_quad_tessellation_wrong_input_count_dxil(void)
{
    test_quad_tessellation(true, false, true);
}

static void test_tessellation_dcl_index_range(bool use_dxil)
{
#include "shaders/tessellation/headers/dcl_index_range_vs.h"
#include "shaders/tessellation/headers/dcl_index_range_hs.h"
#include "shaders/tessellation/headers/dcl_index_range_ds.h"

    static const struct vec4 quad[] =
    {
        {-1.0f, -1.0f, 0.0f, 1.0f},
        {-1.0f,  1.0f, 0.0f, 1.0f},
        { 1.0f, -1.0f, 0.0f, 1.0f},
        { 1.0f,  1.0f, 0.0f, 1.0f},
    };
    static const D3D12_INPUT_ELEMENT_DESC layout_desc[] =
    {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    };
    static const float white[] = {1.0f, 1.0f, 1.0f, 1.0f};

    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc;
    ID3D12GraphicsCommandList *command_list;
    D3D12_INPUT_LAYOUT_DESC input_layout;
    struct test_context_desc desc;
    D3D12_VERTEX_BUFFER_VIEW vbv;
    struct test_context context;
    ID3D12CommandQueue *queue;
    ID3D12Device *device;
    ID3D12Resource *vb;
    HRESULT hr;

    memset(&desc, 0, sizeof(desc));
    desc.root_signature_flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    desc.no_pipeline = true;
    if (!init_test_context(&context, &desc))
        return;
    device = context.device;
    command_list = context.list;
    queue = context.queue;

    input_layout.pInputElementDescs = layout_desc;
    input_layout.NumElements = ARRAY_SIZE(layout_desc);

    if (use_dxil)
    {
        init_pipeline_state_desc_dxil(&pso_desc, context.root_signature,
            context.render_target_desc.Format, NULL, NULL, &input_layout);
    }
    else
    {
        init_pipeline_state_desc(&pso_desc, context.root_signature,
            context.render_target_desc.Format, NULL, NULL, &input_layout);
    }

    pso_desc.VS = use_dxil ? dcl_index_range_vs_dxil : dcl_index_range_vs_dxbc;
    pso_desc.HS = use_dxil ? dcl_index_range_hs_dxil : dcl_index_range_hs_dxbc;
    pso_desc.DS = use_dxil ? dcl_index_range_ds_dxil : dcl_index_range_ds_dxbc;
    pso_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH;
    hr = ID3D12Device_CreateGraphicsPipelineState(device, &pso_desc,
            &IID_ID3D12PipelineState, (void **)&context.pipeline_state);
    ok(hr == S_OK, "Failed to create state, hr %#x.\n", hr);

    vb = create_upload_buffer(device, sizeof(quad), quad);

    vbv.BufferLocation = ID3D12Resource_GetGPUVirtualAddress(vb);
    vbv.StrideInBytes = sizeof(*quad);
    vbv.SizeInBytes = sizeof(quad);

    ID3D12GraphicsCommandList_ClearRenderTargetView(command_list, context.rtv, white, 0, NULL);

    ID3D12GraphicsCommandList_OMSetRenderTargets(command_list, 1, &context.rtv, false, NULL);
    ID3D12GraphicsCommandList_SetGraphicsRootSignature(command_list, context.root_signature);
    ID3D12GraphicsCommandList_SetPipelineState(command_list, context.pipeline_state);
    ID3D12GraphicsCommandList_IASetPrimitiveTopology(command_list, D3D_PRIMITIVE_TOPOLOGY_4_CONTROL_POINT_PATCHLIST);
    ID3D12GraphicsCommandList_RSSetViewports(command_list, 1, &context.viewport);
    ID3D12GraphicsCommandList_RSSetScissorRects(command_list, 1, &context.scissor_rect);
    ID3D12GraphicsCommandList_IASetVertexBuffers(command_list, 0, 1, &vbv);
    ID3D12GraphicsCommandList_DrawInstanced(command_list, 4, 1, 0, 0);

    transition_resource_state(command_list, context.render_target,
            D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
    check_sub_resource_uint(context.render_target, 0, queue, command_list, 0xff00ff00, 0);

    ID3D12Resource_Release(vb);
    destroy_test_context(&context);
}

void test_tessellation_dcl_index_range_dxbc(void)
{
    test_tessellation_dcl_index_range(false);
}

void test_tessellation_dcl_index_range_dxil(void)
{
    test_tessellation_dcl_index_range(true);
}

static void test_tessellation_dcl_index_range_complex(bool use_dxil)
{
#include "shaders/tessellation/headers/dcl_index_range_vs_complex.h"
#include "shaders/tessellation/headers/dcl_index_range_hs_complex.h"
#include "shaders/tessellation/headers/dcl_index_range_ds.h"

    static const struct vec4 quad[] =
    {
        {-1.0f, -1.0f, 0.0f, 1.0f},
        {-1.0f,  1.0f, 0.0f, 1.0f},
        { 1.0f, -1.0f, 0.0f, 1.0f},
        { 1.0f,  1.0f, 0.0f, 1.0f},
    };
    static const D3D12_INPUT_ELEMENT_DESC layout_desc[] =
    {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    };
    static const float white[] = {1.0f, 1.0f, 1.0f, 1.0f};

    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc;
    ID3D12GraphicsCommandList *command_list;
    D3D12_INPUT_LAYOUT_DESC input_layout;
    struct test_context_desc desc;
    D3D12_VERTEX_BUFFER_VIEW vbv;
    struct test_context context;
    ID3D12CommandQueue *queue;
    ID3D12Device *device;
    ID3D12Resource *vb;
    HRESULT hr;

    memset(&desc, 0, sizeof(desc));
    desc.root_signature_flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    desc.no_pipeline = true;
    desc.rt_width = 64;
    desc.rt_height = 64;
    if (!init_test_context(&context, &desc))
        return;
    device = context.device;
    command_list = context.list;
    queue = context.queue;

    input_layout.pInputElementDescs = layout_desc;
    input_layout.NumElements = ARRAY_SIZE(layout_desc);

    if (use_dxil)
    {
        init_pipeline_state_desc_dxil(&pso_desc, context.root_signature,
            context.render_target_desc.Format, NULL, NULL, &input_layout);
    }
    else
    {
        init_pipeline_state_desc(&pso_desc, context.root_signature,
            context.render_target_desc.Format, NULL, NULL, &input_layout);
    }

    pso_desc.VS = use_dxil ? dcl_index_range_vs_complex_dxil : dcl_index_range_vs_complex_dxbc;
    pso_desc.HS = use_dxil ? dcl_index_range_hs_complex_dxil : dcl_index_range_hs_complex_dxbc;
    pso_desc.DS = use_dxil ? dcl_index_range_ds_dxil : dcl_index_range_ds_dxbc;
    pso_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH;
    hr = ID3D12Device_CreateGraphicsPipelineState(device, &pso_desc,
            &IID_ID3D12PipelineState, (void **)&context.pipeline_state);
    ok(hr == S_OK, "Failed to create state, hr %#x.\n", hr);

    vb = create_upload_buffer(device, sizeof(quad), quad);

    vbv.BufferLocation = ID3D12Resource_GetGPUVirtualAddress(vb);
    vbv.StrideInBytes = sizeof(*quad);
    vbv.SizeInBytes = sizeof(quad);

    ID3D12GraphicsCommandList_ClearRenderTargetView(command_list, context.rtv, white, 0, NULL);

    ID3D12GraphicsCommandList_OMSetRenderTargets(command_list, 1, &context.rtv, false, NULL);
    ID3D12GraphicsCommandList_SetGraphicsRootSignature(command_list, context.root_signature);
    ID3D12GraphicsCommandList_SetPipelineState(command_list, context.pipeline_state);
    ID3D12GraphicsCommandList_IASetPrimitiveTopology(command_list, D3D_PRIMITIVE_TOPOLOGY_4_CONTROL_POINT_PATCHLIST);
    ID3D12GraphicsCommandList_RSSetViewports(command_list, 1, &context.viewport);
    ID3D12GraphicsCommandList_RSSetScissorRects(command_list, 1, &context.scissor_rect);
    ID3D12GraphicsCommandList_IASetVertexBuffers(command_list, 0, 1, &vbv);
    ID3D12GraphicsCommandList_DrawInstanced(command_list, 4, 4, 0, 0);

    transition_resource_state(command_list, context.render_target,
            D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
    todo_if(!use_dxil) check_sub_resource_uint(context.render_target, 0, queue, command_list, 0xff00ff00, 0);

    ID3D12Resource_Release(vb);
    destroy_test_context(&context);
}

void test_tessellation_dcl_index_range_complex_dxbc(void)
{
    test_tessellation_dcl_index_range_complex(false);
}

void test_tessellation_dcl_index_range_complex_dxil(void)
{
    test_tessellation_dcl_index_range_complex(true);
}

static void test_hull_shader_control_point_phase(bool use_dxil)
{
    static const float white[] = {1.0f, 1.0f, 1.0f, 1.0f};
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc;
    ID3D12GraphicsCommandList *command_list;
    struct test_context_desc desc;
    struct test_context context;
    ID3D12CommandQueue *queue;
    HRESULT hr;

#include "shaders/tessellation/headers/control_point_phase_vs.h"
#include "shaders/tessellation/headers/control_point_phase_hs.h"
#include "shaders/tessellation/headers/control_point_phase_ds.h"

    memset(&desc, 0, sizeof(desc));
    desc.no_pipeline = true;
    if (!init_test_context(&context, &desc))
        return;

    if (use_dxil && !context_supports_dxil(&context))
    {
        destroy_test_context(&context);
        return;
    }

    command_list = context.list;
    queue = context.queue;

    if (use_dxil)
    {
        init_pipeline_state_desc_dxil(&pso_desc, context.root_signature,
                context.render_target_desc.Format, NULL, NULL, NULL);
    }
    else
    {
        init_pipeline_state_desc(&pso_desc, context.root_signature,
            context.render_target_desc.Format, NULL, NULL, NULL);
    }

    pso_desc.VS = use_dxil ? control_point_phase_vs_dxil : control_point_phase_vs_dxbc;
    pso_desc.HS = use_dxil ? control_point_phase_hs_dxil : control_point_phase_hs_dxbc;
    pso_desc.DS = use_dxil ? control_point_phase_ds_dxil : control_point_phase_ds_dxbc;
    pso_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH;
    hr = ID3D12Device_CreateGraphicsPipelineState(context.device, &pso_desc,
            &IID_ID3D12PipelineState, (void **)&context.pipeline_state);
    ok(hr == S_OK, "Failed to create state, hr %#x.\n", hr);

    ID3D12GraphicsCommandList_ClearRenderTargetView(command_list, context.rtv, white, 0, NULL);

    ID3D12GraphicsCommandList_OMSetRenderTargets(command_list, 1, &context.rtv, false, NULL);
    ID3D12GraphicsCommandList_SetGraphicsRootSignature(command_list, context.root_signature);
    ID3D12GraphicsCommandList_SetPipelineState(command_list, context.pipeline_state);
    ID3D12GraphicsCommandList_IASetPrimitiveTopology(command_list, D3D_PRIMITIVE_TOPOLOGY_3_CONTROL_POINT_PATCHLIST);
    ID3D12GraphicsCommandList_RSSetViewports(command_list, 1, &context.viewport);
    ID3D12GraphicsCommandList_RSSetScissorRects(command_list, 1, &context.scissor_rect);
    ID3D12GraphicsCommandList_DrawInstanced(command_list, 3, 1, 0, 0);

    transition_resource_state(command_list, context.render_target,
            D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);

    check_sub_resource_uint(context.render_target, 0, queue, command_list, 0xff00ff00, 0);

    destroy_test_context(&context);
}

void test_hull_shader_control_point_phase_dxbc(void)
{
    test_hull_shader_control_point_phase(false);
}

void test_hull_shader_control_point_phase_dxil(void)
{
    test_hull_shader_control_point_phase(true);
}

void test_hull_shader_vertex_input_patch_constant_phase(void)
{
    static const float white[] = {1.0f, 1.0f, 1.0f, 1.0f};
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc;
    ID3D12GraphicsCommandList *command_list;
    struct test_context_desc desc;
    struct test_context context;
    ID3D12CommandQueue *queue;
    HRESULT hr;

#include "shaders/tessellation/headers/vertex_input_patch_constant_phase_vs.h"
#include "shaders/tessellation/headers/vertex_input_patch_constant_phase_hs.h"
#include "shaders/tessellation/headers/vertex_input_patch_constant_phase_ds.h"
#include "shaders/tessellation/headers/vertex_input_patch_constant_phase_ps.h"

    memset(&desc, 0, sizeof(desc));
    desc.no_pipeline = true;
    if (!init_test_context(&context, &desc))
        return;
    command_list = context.list;
    queue = context.queue;

    init_pipeline_state_desc(&pso_desc, context.root_signature,
            context.render_target_desc.Format, NULL, NULL, NULL);
    pso_desc.VS = vertex_input_patch_constant_phase_vs_dxbc;
    pso_desc.HS = vertex_input_patch_constant_phase_hs_dxbc;
    pso_desc.DS = vertex_input_patch_constant_phase_ds_dxbc;
    pso_desc.PS = vertex_input_patch_constant_phase_ps_dxbc;
    pso_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH;
    hr = ID3D12Device_CreateGraphicsPipelineState(context.device, &pso_desc,
            &IID_ID3D12PipelineState, (void **)&context.pipeline_state);
    ok(hr == S_OK, "Failed to create state, hr %#x.\n", hr);

    ID3D12GraphicsCommandList_ClearRenderTargetView(command_list, context.rtv, white, 0, NULL);

    ID3D12GraphicsCommandList_OMSetRenderTargets(command_list, 1, &context.rtv, false, NULL);
    ID3D12GraphicsCommandList_SetGraphicsRootSignature(command_list, context.root_signature);
    ID3D12GraphicsCommandList_SetPipelineState(command_list, context.pipeline_state);
    ID3D12GraphicsCommandList_IASetPrimitiveTopology(command_list, D3D_PRIMITIVE_TOPOLOGY_3_CONTROL_POINT_PATCHLIST);
    ID3D12GraphicsCommandList_RSSetViewports(command_list, 1, &context.viewport);
    ID3D12GraphicsCommandList_RSSetScissorRects(command_list, 1, &context.scissor_rect);
    ID3D12GraphicsCommandList_DrawInstanced(command_list, 3, 1, 0, 0);

    transition_resource_state(command_list, context.render_target,
            D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
    check_sub_resource_uint(context.render_target, 0, queue, command_list, 0xff9bc864, 0);

    destroy_test_context(&context);
}

static void test_hull_shader_fork_phase(bool use_dxil)
{
    static const float white[] = {1.0f, 1.0f, 1.0f, 1.0f};
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc;
    ID3D12GraphicsCommandList *command_list;
    struct test_context_desc desc;
    struct test_context context;
    ID3D12CommandQueue *queue;
    HRESULT hr;

#include "shaders/tessellation/headers/fork_phase_vs.h"
#include "shaders/tessellation/headers/fork_phase_hs.h"
#include "shaders/tessellation/headers/fork_phase_ds.h"
#include "shaders/tessellation/headers/fork_phase_ps.h"

    memset(&desc, 0, sizeof(desc));
    desc.no_pipeline = true;
    if (!init_test_context(&context, &desc))
        return;

    if (use_dxil && !context_supports_dxil(&context))
    {
        destroy_test_context(&context);
        return;
    }

    command_list = context.list;
    queue = context.queue;

    init_pipeline_state_desc(&pso_desc, context.root_signature,
            context.render_target_desc.Format, NULL, NULL, NULL);
    pso_desc.VS = use_dxil ? fork_phase_vs_dxil : fork_phase_vs_dxbc;
    pso_desc.HS = use_dxil ? fork_phase_hs_dxil : fork_phase_hs_dxbc;
    pso_desc.DS = use_dxil ? fork_phase_ds_dxil : fork_phase_ds_dxbc;
    pso_desc.PS = use_dxil ? fork_phase_ps_dxil : fork_phase_ps_dxbc;
    pso_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH;
    hr = ID3D12Device_CreateGraphicsPipelineState(context.device, &pso_desc,
            &IID_ID3D12PipelineState, (void **)&context.pipeline_state);
    ok(hr == S_OK, "Failed to create state, hr %#x.\n", hr);

    ID3D12GraphicsCommandList_ClearRenderTargetView(command_list, context.rtv, white, 0, NULL);

    ID3D12GraphicsCommandList_OMSetRenderTargets(command_list, 1, &context.rtv, false, NULL);
    ID3D12GraphicsCommandList_SetGraphicsRootSignature(command_list, context.root_signature);
    ID3D12GraphicsCommandList_SetPipelineState(command_list, context.pipeline_state);
    ID3D12GraphicsCommandList_IASetPrimitiveTopology(command_list, D3D_PRIMITIVE_TOPOLOGY_1_CONTROL_POINT_PATCHLIST);
    ID3D12GraphicsCommandList_RSSetViewports(command_list, 1, &context.viewport);
    ID3D12GraphicsCommandList_RSSetScissorRects(command_list, 1, &context.scissor_rect);
    ID3D12GraphicsCommandList_DrawInstanced(command_list, 1, 1, 0, 0);

    transition_resource_state(command_list, context.render_target,
            D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
    check_sub_resource_uint(context.render_target, 0, queue, command_list, 0xff000000, 0);

    destroy_test_context(&context);
}

void test_hull_shader_fork_phase_dxbc(void)
{
    test_hull_shader_fork_phase(false);
}

void test_hull_shader_fork_phase_dxil(void)
{
    test_hull_shader_fork_phase(true);
}

void test_tessellation_read_tesslevel(void)
{
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc;
    ID3D12GraphicsCommandList *command_list;
    D3D12_STREAM_OUTPUT_BUFFER_VIEW sobv;
    D3D12_INPUT_LAYOUT_DESC input_layout;
    struct test_context_desc desc;
    struct resource_readback rb;
    struct test_context context;
    ID3D12Resource *so_buffer;
    ID3D12CommandQueue *queue;
    unsigned int i, j;
    HRESULT hr;

#include "shaders/tessellation/headers/read_tesslevel_vs.h"
#include "shaders/tessellation/headers/read_tesslevel_hs.h"
#include "shaders/tessellation/headers/read_tesslevel_ds.h"

    static const D3D12_SO_DECLARATION_ENTRY so_declaration[] =
    {
        {0, "SV_POSITION",  0, 0, 4, 0},
        {0, "COLOR",        0, 0, 4, 0},
    };
    unsigned int stride = 32;
    static const float reference[4][8] = {
        { 1.0f, 2.0f, 0.0f, 0.0f, 2.5f, 8.0f, 2.0f, 1.0f },
        { 1.0f, 2.0f, 1.0f, 0.0f, 2.5f, 8.0f, 2.0f, 1.0f },
        { 1.0f, 2.0f, 0.0f, 0.5f, 2.5f, 8.0f, 2.0f, 1.0f },
        { 1.0f, 2.0f, 1.0f, 0.5f, 2.5f, 8.0f, 2.0f, 1.0f },
    };
    memset(&desc, 0, sizeof(desc));
    desc.root_signature_flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT
            | D3D12_ROOT_SIGNATURE_FLAG_ALLOW_STREAM_OUTPUT;
    desc.no_pipeline = true;
    if (!init_test_context(&context, &desc))
        return;
    command_list = context.list;
    queue = context.queue;

    memset(&input_layout, 0, sizeof(input_layout));

    init_pipeline_state_desc(&pso_desc, context.root_signature,
            DXGI_FORMAT_UNKNOWN, NULL, NULL, &input_layout);
    pso_desc.VS = read_tesslevel_vs_dxbc;
    pso_desc.HS = read_tesslevel_hs_dxbc;
    pso_desc.DS = read_tesslevel_ds_dxbc;
    memset(&pso_desc.PS, 0, sizeof(pso_desc.PS));
    pso_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH;
    pso_desc.StreamOutput.NumEntries = ARRAY_SIZE(so_declaration);
    pso_desc.StreamOutput.pSODeclaration = so_declaration;
    pso_desc.StreamOutput.pBufferStrides = &stride;
    pso_desc.StreamOutput.NumStrides = 1;
    pso_desc.StreamOutput.RasterizedStream = D3D12_SO_NO_RASTERIZED_STREAM;
    vkd3d_mute_validation_message("09658", "See vkd3d-proton issue 2378");
    hr = ID3D12Device_CreateGraphicsPipelineState(context.device, &pso_desc,
            &IID_ID3D12PipelineState, (void **)&context.pipeline_state);
    vkd3d_unmute_validation_message("09658");
    ok(hr == S_OK, "Failed to create state, hr %#x.\n", hr);

    so_buffer = create_default_buffer(context.device, 4096,
            D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_STREAM_OUT);
    sobv.BufferLocation = ID3D12Resource_GetGPUVirtualAddress(so_buffer);
    sobv.SizeInBytes = 1024;
    sobv.BufferFilledSizeLocation = sobv.BufferLocation + sobv.SizeInBytes;

    ID3D12GraphicsCommandList_SOSetTargets(command_list, 0, 1, &sobv);
    ID3D12GraphicsCommandList_SetGraphicsRootSignature(command_list, context.root_signature);
    ID3D12GraphicsCommandList_SetPipelineState(command_list, context.pipeline_state);
    ID3D12GraphicsCommandList_IASetPrimitiveTopology(command_list, D3D_PRIMITIVE_TOPOLOGY_1_CONTROL_POINT_PATCHLIST);
    ID3D12GraphicsCommandList_RSSetViewports(command_list, 1, &context.viewport);
    ID3D12GraphicsCommandList_RSSetScissorRects(command_list, 1, &context.scissor_rect);
    ID3D12GraphicsCommandList_DrawInstanced(command_list, 4, 1, 0, 0);

    transition_resource_state(command_list, so_buffer,
            D3D12_RESOURCE_STATE_STREAM_OUT, D3D12_RESOURCE_STATE_COPY_SOURCE);
    get_buffer_readback_with_command_list(so_buffer, DXGI_FORMAT_UNKNOWN, &rb, queue, command_list);

    for (i = 0; i < 4; i++)
    {
        float *elems = get_readback_data(&rb, i, 0, 0, stride);
        for (j = 0; j < 8; j++)
        {
            ok(compare_float(reference[i][j], elems[j], 0),
                    "Got unexpected value %f for [%u][%u], expected %f.\n",
                    elems[j], i, j, reference[i][j]);
        }
    }

    release_resource_readback(&rb);
    ID3D12Resource_Release(so_buffer);
    destroy_test_context(&context);
}

static void test_line_tessellation(bool use_dxil)
{
    ID3D12Resource *vb, *so_buffer, *readback_buffer;
    D3D12_QUERY_DATA_SO_STATISTICS *so_statistics;
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc;
    ID3D12GraphicsCommandList *command_list;
    D3D12_QUERY_HEAP_DESC query_heap_desc;
    D3D12_STREAM_OUTPUT_BUFFER_VIEW sobv;
    D3D12_INPUT_LAYOUT_DESC input_layout;
    struct test_context_desc desc;
    D3D12_VERTEX_BUFFER_VIEW vbv;
    struct resource_readback rb;
    ID3D12QueryHeap *query_heap;
    struct test_context context;
    const struct vec4 *expected;
    ID3D12CommandQueue *queue;
    struct vec4 *data;
    bool broken_warp;
    unsigned int i;
    HRESULT hr;

#include "shaders/tessellation/headers/line_tessellation_vs.h"
#include "shaders/tessellation/headers/line_tessellation_hs.h"
#include "shaders/tessellation/headers/line_tessellation_ds.h"

    static const D3D12_INPUT_ELEMENT_DESC layout_desc[] =
    {
        {"SV_POSITION",  0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0,  0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"COLOR",        0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 16, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"LINE_DENSITY", 0, DXGI_FORMAT_R32_FLOAT,          0, 32, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"LINE_DETAIL",  0, DXGI_FORMAT_R32_FLOAT,          0, 36, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    };
    static const D3D12_SO_DECLARATION_ENTRY so_declaration[] =
    {
        {0, "SV_POSITION",  0, 0, 4, 0},
        {0, "COLOR",        0, 0, 4, 0},
        {0, "PRIMITIVE_ID", 0, 0, 4, 0},
    };
    unsigned int strides[] = {48};
    static const struct
    {
        struct vec4 position;
        struct vec4 color;
        float line_density;
        float line_detail;
    }
    vertices[] =
    {
        {{0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 1.0f, 0.0f}, 1.0f, 1.0f},
        {{1.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.5f, 0.0f}, 2.0f, 1.0f},
        {{2.0f, 0.0f, 0.0f, 1.0f}, {1.0f, 1.0f, 1.0f}, 1.0f, 2.0f},
        {{3.0f, 0.0f, 0.0f, 1.0f}, {0.5f, 0.5f, 0.5f}, 2.0f, 2.0f},
    };
    static const struct vec4 expected_data[] =
    {
        {0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 1.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.0f, 0.0f},
        {0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 1.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.0f, 0.0f},

        {1.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.5f, 0.0f, 1.0f}, {1.0f, 1.0f, 1.0f, 1.0f},
        {1.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.5f, 0.0f, 1.0f}, {1.0f, 1.0f, 1.0f, 1.0f},
        {1.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.5f, 0.0f, 1.0f}, {1.0f, 1.0f, 1.0f, 1.0f},
        {1.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.5f, 0.0f, 1.0f}, {1.0f, 1.0f, 1.0f, 1.0f},

        {2.0f, 0.0f, 0.0f, 1.0f}, {1.0f, 1.0f, 1.0f, 1.0f}, {2.0f, 2.0f, 2.0f, 2.0f},
        {2.0f, 0.0f, 0.0f, 1.0f}, {1.0f, 1.0f, 1.0f, 1.0f}, {2.0f, 2.0f, 2.0f, 2.0f},
        {2.0f, 0.0f, 0.0f, 1.0f}, {1.0f, 1.0f, 1.0f, 1.0f}, {2.0f, 2.0f, 2.0f, 2.0f},
        {2.0f, 0.0f, 0.0f, 1.0f}, {1.0f, 1.0f, 1.0f, 1.0f}, {2.0f, 2.0f, 2.0f, 2.0f},

        {3.0f, 0.0f, 0.0f, 1.0f}, {0.5f, 0.5f, 0.5f, 1.0f}, {3.0f, 3.0f, 3.0f, 3.0f},
        {3.0f, 0.0f, 0.0f, 1.0f}, {0.5f, 0.5f, 0.5f, 1.0f}, {3.0f, 3.0f, 3.0f, 3.0f},
        {3.0f, 0.0f, 0.0f, 1.0f}, {0.5f, 0.5f, 0.5f, 1.0f}, {3.0f, 3.0f, 3.0f, 3.0f},
        {3.0f, 0.0f, 0.0f, 1.0f}, {0.5f, 0.5f, 0.5f, 1.0f}, {3.0f, 3.0f, 3.0f, 3.0f},
        {3.0f, 0.0f, 0.0f, 1.0f}, {0.5f, 0.5f, 0.5f, 1.0f}, {3.0f, 3.0f, 3.0f, 3.0f},
        {3.0f, 0.0f, 0.0f, 1.0f}, {0.5f, 0.5f, 0.5f, 1.0f}, {3.0f, 3.0f, 3.0f, 3.0f},
        {3.0f, 0.0f, 0.0f, 1.0f}, {0.5f, 0.5f, 0.5f, 1.0f}, {3.0f, 3.0f, 3.0f, 3.0f},
        {3.0f, 0.0f, 0.0f, 1.0f}, {0.5f, 0.5f, 0.5f, 1.0f}, {3.0f, 3.0f, 3.0f, 3.0f},
    };

    memset(&desc, 0, sizeof(desc));
    desc.root_signature_flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT
            | D3D12_ROOT_SIGNATURE_FLAG_ALLOW_STREAM_OUTPUT;
    desc.no_pipeline = true;
    if (!init_test_context(&context, &desc))
        return;

    if (use_dxil && !context_supports_dxil(&context))
    {
        destroy_test_context(&context);
        return;
    }

    command_list = context.list;
    queue = context.queue;

    query_heap_desc.Type = D3D12_QUERY_HEAP_TYPE_SO_STATISTICS;
    query_heap_desc.Count = 2;
    query_heap_desc.NodeMask = 0;
    hr = ID3D12Device_CreateQueryHeap(context.device, &query_heap_desc, &IID_ID3D12QueryHeap, (void **)&query_heap);
    if (hr == E_NOTIMPL)
    {
        skip("Stream output is not supported.\n");
        destroy_test_context(&context);
        return;
    }
    ok(hr == S_OK, "Failed to create query heap, hr %#x.\n", hr);

    input_layout.pInputElementDescs = layout_desc;
    input_layout.NumElements = ARRAY_SIZE(layout_desc);

    if (use_dxil)
    {
        init_pipeline_state_desc_dxil(&pso_desc, context.root_signature,
                DXGI_FORMAT_UNKNOWN, NULL, NULL, &input_layout);
    }
    else
    {
        init_pipeline_state_desc(&pso_desc, context.root_signature,
                DXGI_FORMAT_UNKNOWN, NULL, NULL, &input_layout);
    }
    pso_desc.VS = use_dxil ? line_tessellation_vs_dxil : line_tessellation_vs_dxbc;
    pso_desc.HS = use_dxil ? line_tessellation_hs_dxil : line_tessellation_hs_dxbc;
    pso_desc.DS = use_dxil ? line_tessellation_ds_dxil : line_tessellation_ds_dxbc;
    memset(&pso_desc.PS, 0, sizeof(pso_desc.PS));
    pso_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH;
    pso_desc.StreamOutput.NumEntries = ARRAY_SIZE(so_declaration);
    pso_desc.StreamOutput.pSODeclaration = so_declaration;
    pso_desc.StreamOutput.pBufferStrides = strides;
    pso_desc.StreamOutput.NumStrides = ARRAY_SIZE(strides);
    pso_desc.StreamOutput.RasterizedStream = D3D12_SO_NO_RASTERIZED_STREAM;
    hr = ID3D12Device_CreateGraphicsPipelineState(context.device, &pso_desc,
            &IID_ID3D12PipelineState, (void **)&context.pipeline_state);
    ok(hr == S_OK, "Failed to create state, hr %#x.\n", hr);

    vb = create_upload_buffer(context.device, sizeof(vertices), vertices);
    vbv.BufferLocation = ID3D12Resource_GetGPUVirtualAddress(vb);
    vbv.StrideInBytes = sizeof(*vertices);
    vbv.SizeInBytes = sizeof(vertices);

    so_buffer = create_default_buffer(context.device, 4096,
            D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_STREAM_OUT);
    sobv.BufferLocation = ID3D12Resource_GetGPUVirtualAddress(so_buffer);
    sobv.SizeInBytes = 1024;
    sobv.BufferFilledSizeLocation = sobv.BufferLocation + sobv.SizeInBytes;

    ID3D12GraphicsCommandList_BeginQuery(command_list, query_heap, D3D12_QUERY_TYPE_SO_STATISTICS_STREAM0, 0);

    ID3D12GraphicsCommandList_SOSetTargets(command_list, 0, 1, &sobv);

    ID3D12GraphicsCommandList_SetGraphicsRootSignature(command_list, context.root_signature);
    ID3D12GraphicsCommandList_SetPipelineState(command_list, context.pipeline_state);
    ID3D12GraphicsCommandList_IASetPrimitiveTopology(command_list, D3D_PRIMITIVE_TOPOLOGY_1_CONTROL_POINT_PATCHLIST);
    ID3D12GraphicsCommandList_RSSetViewports(command_list, 1, &context.viewport);
    ID3D12GraphicsCommandList_RSSetScissorRects(command_list, 1, &context.scissor_rect);
    ID3D12GraphicsCommandList_IASetVertexBuffers(command_list, 0, 1, &vbv);
    ID3D12GraphicsCommandList_DrawInstanced(command_list, 4, 1, 0, 0);

    ID3D12GraphicsCommandList_EndQuery(command_list, query_heap, D3D12_QUERY_TYPE_SO_STATISTICS_STREAM0, 0);

    readback_buffer = create_readback_buffer(context.device, sizeof(*so_statistics));
    ID3D12GraphicsCommandList_ResolveQueryData(command_list,
            query_heap, D3D12_QUERY_TYPE_SO_STATISTICS_STREAM0, 0, 1, readback_buffer, 0);

    get_buffer_readback_with_command_list(readback_buffer, DXGI_FORMAT_UNKNOWN, &rb, queue, command_list);
    so_statistics = get_readback_data(&rb, 0, 0, 0, sizeof(*so_statistics));
    broken_warp = broken_on_warp(so_statistics[0].NumPrimitivesWritten != 9);
    ok(so_statistics[0].NumPrimitivesWritten == 9 || broken_warp, "Got unexpected primitives written %u.\n",
            (unsigned int)so_statistics[0].NumPrimitivesWritten);
    ok(so_statistics[0].PrimitivesStorageNeeded == 9 || broken_warp, "Got unexpected primitives storage needed %u.\n",
            (unsigned int)so_statistics[0].PrimitivesStorageNeeded);
    release_resource_readback(&rb);

    if (broken_warp)
    {
        skip("Broken on WARP.\n");
        goto done;
    }

    reset_command_list(command_list, context.allocator);
    transition_resource_state(command_list, so_buffer,
            D3D12_RESOURCE_STATE_STREAM_OUT, D3D12_RESOURCE_STATE_COPY_SOURCE);
    get_buffer_readback_with_command_list(so_buffer, DXGI_FORMAT_UNKNOWN, &rb, queue, command_list);
    for (i = 0; i < ARRAY_SIZE(expected_data) / 3 ; ++i)
    {
        data = get_readback_data(&rb, i, 0, 0, 3 * sizeof(*data));
        expected = &expected_data[3 * i + 0];
        ok(compare_vec4(data, expected, 1),
                "Got position {%.8e, %.8e, %.8e, %.8e}, expected {%.8e, %.8e, %.8e, %.8e} at %u.\n",
                data->x, data->y, data->z, data->w, expected->x, expected->y, expected->z, expected->w, i);
        ++data;
        expected = &expected_data[3 * i + 1];
        ok(compare_vec4(data, expected, 1),
                "Got color {%.8e, %.8e, %.8e, %.8e}, expected {%.8e, %.8e, %.8e, %.8e} at %u.\n",
                data->x, data->y, data->z, data->w, expected->x, expected->y, expected->z, expected->w, i);
        ++data;
        expected = &expected_data[3 * i + 2];
        ok(compare_vec4(data, expected, 1),
                "Got primitive ID {%.8e, %.8e, %.8e, %.8e}, expected {%.8e, %.8e, %.8e, %.8e} at %u.\n",
                data->x, data->y, data->z, data->w, expected->x, expected->y, expected->z, expected->w, i);
    }
    release_resource_readback(&rb);

done:
    ID3D12QueryHeap_Release(query_heap);
    ID3D12Resource_Release(readback_buffer);
    ID3D12Resource_Release(so_buffer);
    ID3D12Resource_Release(vb);
    destroy_test_context(&context);
}

void test_line_tessellation_dxbc(void)
{
    test_line_tessellation(false);
}

void test_line_tessellation_dxil(void)
{
    test_line_tessellation(true);
}

void test_tessellation_primitive_id(void)
{
    static const float white[] = {1.0f, 1.0f, 1.0f, 1.0f};
    D3D12_ROOT_SIGNATURE_DESC root_signature_desc;
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc;
    D3D12_ROOT_PARAMETER root_parameters[1];
    ID3D12GraphicsCommandList *command_list;
    D3D12_INPUT_LAYOUT_DESC input_layout;
    struct test_context_desc desc;
    D3D12_VERTEX_BUFFER_VIEW vbv;
    struct test_context context;
    ID3D12Resource *raw_buffer;
    ID3D12CommandQueue *queue;
    ID3D12Resource *vb;
    HRESULT hr;

#include "shaders/tessellation/headers/primitive_id_vs.h"
#include "shaders/tessellation/headers/primitive_id_hs.h"
#include "shaders/tessellation/headers/primitive_id_ds.h"
#include "shaders/tessellation/headers/primitive_id_ps.h"

    static const D3D12_INPUT_ELEMENT_DESC layout_desc[] =
    {
        {"POSITION",        0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0,  0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"REF_BUFFER_DATA", 0, DXGI_FORMAT_R32_FLOAT,          0, 16, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    };
    static const struct
    {
        struct vec4 position;
        float ref_buffer_data;
    }
    vertices[] =
    {
        {{-1.0f, -1.0f, 0.0f, 1.0f}, 1.0f},
        {{-1.0f,  1.0f, 0.0f, 1.0f}, 1.0f},
        {{ 1.0f, -1.0f, 0.0f, 1.0f}, 1.0f},

        {{-1.0f,  1.0f, 0.0f, 1.0f}, 2.0f},
        {{ 1.0f,  1.0f, 0.0f, 1.0f}, 2.0f},
        {{ 1.0f, -1.0f, 0.0f, 1.0f}, 2.0f},
    };
    static const uint32_t buffer_data[] = {1, 2};

    memset(&desc, 0, sizeof(desc));
    desc.no_root_signature = true;
    if (!init_test_context(&context, &desc))
        return;
    command_list = context.list;
    queue = context.queue;

    root_parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    root_parameters[0].Descriptor.ShaderRegister = 0;
    root_parameters[0].Descriptor.RegisterSpace = 0;
    root_parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_HULL;
    memset(&root_signature_desc, 0, sizeof(root_signature_desc));
    root_signature_desc.NumParameters = ARRAY_SIZE(root_parameters);
    root_signature_desc.pParameters = root_parameters;
    root_signature_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    hr = create_root_signature(context.device, &root_signature_desc, &context.root_signature);
    ok(hr == S_OK, "Failed to create root signature, hr %#x.\n", hr);

    input_layout.pInputElementDescs = layout_desc;
    input_layout.NumElements = ARRAY_SIZE(layout_desc);
    init_pipeline_state_desc(&pso_desc, context.root_signature,
            context.render_target_desc.Format, NULL, NULL, &input_layout);
    pso_desc.VS = primitive_id_vs_dxbc;
    pso_desc.HS = primitive_id_hs_dxbc;
    pso_desc.DS = primitive_id_ds_dxbc;
    pso_desc.PS = primitive_id_ps_dxbc;
    pso_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH;
    hr = ID3D12Device_CreateGraphicsPipelineState(context.device, &pso_desc,
            &IID_ID3D12PipelineState, (void **)&context.pipeline_state);
    ok(hr == S_OK, "Failed to create state, hr %#x.\n", hr);

    vb = create_upload_buffer(context.device, sizeof(vertices), vertices);
    vbv.BufferLocation = ID3D12Resource_GetGPUVirtualAddress(vb);
    vbv.StrideInBytes = sizeof(*vertices);
    vbv.SizeInBytes = sizeof(vertices);

    raw_buffer = create_upload_buffer(context.device, sizeof(buffer_data), buffer_data);

    ID3D12GraphicsCommandList_ClearRenderTargetView(command_list, context.rtv, white, 0, NULL);

    ID3D12GraphicsCommandList_OMSetRenderTargets(command_list, 1, &context.rtv, false, NULL);
    ID3D12GraphicsCommandList_SetGraphicsRootSignature(command_list, context.root_signature);
    ID3D12GraphicsCommandList_SetPipelineState(command_list, context.pipeline_state);
    ID3D12GraphicsCommandList_IASetPrimitiveTopology(command_list, D3D_PRIMITIVE_TOPOLOGY_3_CONTROL_POINT_PATCHLIST);
    ID3D12GraphicsCommandList_RSSetViewports(command_list, 1, &context.viewport);
    ID3D12GraphicsCommandList_RSSetScissorRects(command_list, 1, &context.scissor_rect);
    ID3D12GraphicsCommandList_IASetVertexBuffers(command_list, 0, 1, &vbv);
    ID3D12GraphicsCommandList_SetGraphicsRootShaderResourceView(command_list,
            0, ID3D12Resource_GetGPUVirtualAddress(raw_buffer));
    ID3D12GraphicsCommandList_DrawInstanced(command_list, 6, 1, 0, 0);

    transition_resource_state(command_list, context.render_target,
            D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
    check_sub_resource_uint(context.render_target, 0, queue, command_list, 0xff00ff00, 0);

    ID3D12Resource_Release(vb);
    ID3D12Resource_Release(raw_buffer);
    destroy_test_context(&context);
}

