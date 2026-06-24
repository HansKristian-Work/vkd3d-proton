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

void test_create_query_heap(void)
{
    ID3D12Device *device;
    D3D12_QUERY_HEAP_DESC heap_desc;
    ID3D12QueryHeap *query_heap;
    unsigned int refcount;
    unsigned int i;
    HRESULT hr;

    static const D3D12_QUERY_HEAP_TYPE types[] =
    {
        D3D12_QUERY_HEAP_TYPE_OCCLUSION,
        D3D12_QUERY_HEAP_TYPE_TIMESTAMP,
        D3D12_QUERY_HEAP_TYPE_PIPELINE_STATISTICS,
    };

    if (!(device = create_device()))
    {
        skip("Failed to create device.\n");
        return;
    }

    for (i = 0; i < ARRAY_SIZE(types); ++i)
    {
        heap_desc.Type = types[i];
        heap_desc.Count = 1;
        heap_desc.NodeMask = 0;

        hr = ID3D12Device_CreateQueryHeap(device, &heap_desc, &IID_ID3D12QueryHeap, (void **)&query_heap);
        ok(hr == S_OK, "Failed to create query heap, type %u, hr %#x.\n", types[i], (int)hr);

        ID3D12QueryHeap_Release(query_heap);
    }

    heap_desc.Type = D3D12_QUERY_HEAP_TYPE_SO_STATISTICS;
    heap_desc.Count = 1;
    heap_desc.NodeMask = 0;

    hr = ID3D12Device_CreateQueryHeap(device, &heap_desc, &IID_ID3D12QueryHeap, (void **)&query_heap);
    if (hr != E_NOTIMPL)
    {
        ok(hr == S_OK, "Failed to create query heap, type %u, hr %#x.\n", heap_desc.Type, (int)hr);
        ID3D12QueryHeap_Release(query_heap);
    }
    else
    {
        skip("Stream output is not supported.\n");
    }

    refcount = ID3D12Device_Release(device);
    ok(!refcount, "ID3D12Device has %u references left.\n", refcount);
}

void test_query_timestamp_write_after_read(void)
{
    D3D12_QUERY_HEAP_DESC heap_desc;
    ID3D12QueryHeap *query_heap[4];
    struct test_context context;
    struct resource_readback rb;
    ID3D12Resource *readback;
    unsigned int i;

    if (!init_compute_test_context(&context))
        return;

    memset(&heap_desc, 0, sizeof(heap_desc));
    heap_desc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
    heap_desc.Count = 2;

    for (i = 0; i < ARRAY_SIZE(query_heap); i++)
        ID3D12Device_CreateQueryHeap(context.device, &heap_desc, &IID_ID3D12QueryHeap, (void **)&query_heap[i]);

    readback = create_readback_buffer(context.device, 4 * 2 * sizeof(uint64_t));

    for (i = 0; i < ARRAY_SIZE(query_heap); i++)
    {
        ID3D12GraphicsCommandList_EndQuery(context.list, query_heap[i], D3D12_QUERY_TYPE_TIMESTAMP, 0);
        ID3D12GraphicsCommandList_EndQuery(context.list, query_heap[i], D3D12_QUERY_TYPE_TIMESTAMP, 1);
    }
    ID3D12GraphicsCommandList_Close(context.list);
    exec_command_list(context.queue, context.list);
    wait_queue_idle(context.device, context.queue);
    reset_command_list(context.list, context.allocator);

    for (i = 0; i < ARRAY_SIZE(query_heap); i++)
    {
        /* The risk is that EndQuery ends up hoisting the reset to init_cmd_buffer.
         * ResolveQueryData will end up waiting forever on the reset query. */
        ID3D12GraphicsCommandList_ResolveQueryData(context.list, query_heap[i],
                D3D12_QUERY_TYPE_TIMESTAMP, 0, 2, readback, i * 2 * sizeof(uint64_t));
        ID3D12GraphicsCommandList_EndQuery(context.list, query_heap[i], D3D12_QUERY_TYPE_TIMESTAMP, 0);
        ID3D12GraphicsCommandList_EndQuery(context.list, query_heap[i], D3D12_QUERY_TYPE_TIMESTAMP, 1);
    }

    get_buffer_readback_with_command_list(readback, DXGI_FORMAT_UNKNOWN, &rb, context.queue, context.list);

    for (i = 0; i < ARRAY_SIZE(query_heap); i++)
    {
        uint64_t start_ts, end_ts;
        start_ts = get_readback_uint64(&rb, 2 * i + 0, 0);
        end_ts = get_readback_uint64(&rb, 2 * i + 1, 0);
        ok(start_ts != 0, "StartTS is 0.\n");
        ok(end_ts != 0, "StartTS is 0.\n");
        ok(end_ts >= start_ts, "TS is not monotonically increasing, expected %"PRIu64" > %"PRIu64".\n", end_ts, start_ts);
    }

    release_resource_readback(&rb);
    for (i = 0; i < ARRAY_SIZE(query_heap); i++)
        ID3D12QueryHeap_Release(query_heap[i]);
    ID3D12Resource_Release(readback);
    destroy_test_context(&context);
}

void test_query_timestamp(void)
{
    UINT64 timestamps[4], timestamp_frequency, timestamp_diff, time_diff;
    ID3D12GraphicsCommandList *command_list;
    D3D12_QUERY_HEAP_DESC heap_desc;
    struct test_context_desc desc;
    ID3D12QueryHeap *query_heap;
    struct resource_readback rb;
    struct test_context context;
    time_t time_start, time_end;
    ID3D12CommandQueue *queue;
    ID3D12Resource *resource;
    ID3D12Device *device;
    unsigned int i;
    HRESULT hr;

    time_start = time(NULL);

    memset(&desc, 0, sizeof(desc));
    desc.no_render_target = true;
    if (!init_test_context(&context, &desc))
        return;
    device = context.device;
    command_list = context.list;
    queue = context.queue;

    hr = ID3D12CommandQueue_GetTimestampFrequency(queue, &timestamp_frequency);
    ok(SUCCEEDED(hr), "Failed to get timestamp frequency, hr %#x.\n", (int)hr);

    heap_desc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
    heap_desc.Count = ARRAY_SIZE(timestamps);
    heap_desc.NodeMask = 0;
    hr = ID3D12Device_CreateQueryHeap(device, &heap_desc, &IID_ID3D12QueryHeap, (void **)&query_heap);
    ok(SUCCEEDED(hr), "Failed to create query heap, type %u, hr %#x.\n", heap_desc.Type, (int)hr);

    resource = create_readback_buffer(device, sizeof(timestamps));

    for (i = 0; i < ARRAY_SIZE(timestamps); ++i)
        ID3D12GraphicsCommandList_EndQuery(command_list, query_heap, D3D12_QUERY_TYPE_TIMESTAMP, i);

    ID3D12GraphicsCommandList_ResolveQueryData(command_list, query_heap,
            D3D12_QUERY_TYPE_TIMESTAMP, 0, 1, resource, 0);
    ID3D12GraphicsCommandList_ResolveQueryData(command_list, query_heap,
            D3D12_QUERY_TYPE_TIMESTAMP, 1, 3, resource, sizeof(uint64_t));

    get_buffer_readback_with_command_list(resource, DXGI_FORMAT_UNKNOWN, &rb, queue, command_list);

    time_end = time(NULL) + 1;

    for (i = 0; i < ARRAY_SIZE(timestamps); ++i)
        timestamps[i] = get_readback_uint64(&rb, i, 0);

    for (i = 0; i < ARRAY_SIZE(timestamps) - 1; ++i)
    {
        ok(timestamps[i] <= timestamps[i + 1], "Expected timestamps to monotonically increase, "
                "but got %"PRIu64" > %"PRIu64".\n", timestamps[i], timestamps[i + 1]);
    }

    time_diff = (uint64_t)difftime(time_end, time_start) * timestamp_frequency;
    timestamp_diff = timestamps[ARRAY_SIZE(timestamps) - 1] - timestamps[0];

    ok(timestamp_diff <= time_diff, "Expected timestamp difference to be bounded by CPU time difference, "
            "but got %"PRIu64" > %"PRIu64".\n", timestamp_diff, time_diff);

    release_resource_readback(&rb);
    ID3D12QueryHeap_Release(query_heap);
    ID3D12Resource_Release(resource);
    destroy_test_context(&context);
}

void test_query_pipeline_statistics(void)
{
    D3D12_QUERY_DATA_PIPELINE_STATISTICS *pipeline_statistics;
    static const float white[] = {1.0f, 1.0f, 1.0f, 1.0f};
    ID3D12GraphicsCommandList *command_list;
    struct test_context context;
    ID3D12CommandQueue *queue;
    ID3D12Device *device;
    D3D12_QUERY_HEAP_DESC heap_desc;
    ID3D12QueryHeap *query_heap;
    ID3D12Resource *resource;
    struct resource_readback rb;
    unsigned int i;
    HRESULT hr;

    if (!init_test_context(&context, NULL))
        return;
    device = context.device;
    command_list = context.list;
    queue = context.queue;

    heap_desc.Type = D3D12_QUERY_HEAP_TYPE_PIPELINE_STATISTICS;
    heap_desc.Count = 2;
    heap_desc.NodeMask = 0;
    hr = ID3D12Device_CreateQueryHeap(device, &heap_desc, &IID_ID3D12QueryHeap, (void **)&query_heap);
    ok(SUCCEEDED(hr), "Failed to create query heap, type %u, hr %#x.\n", heap_desc.Type, (int)hr);

    resource = create_readback_buffer(device, 2 * sizeof(struct D3D12_QUERY_DATA_PIPELINE_STATISTICS));

    /* First query: do nothing. */
    ID3D12GraphicsCommandList_BeginQuery(command_list, query_heap, D3D12_QUERY_TYPE_PIPELINE_STATISTICS, 0);
    ID3D12GraphicsCommandList_EndQuery(command_list, query_heap, D3D12_QUERY_TYPE_PIPELINE_STATISTICS, 0);
    ID3D12GraphicsCommandList_ResolveQueryData(command_list, query_heap, D3D12_QUERY_TYPE_PIPELINE_STATISTICS, 0, 1,
            resource, 0);

    ID3D12GraphicsCommandList_ClearRenderTargetView(command_list, context.rtv, white, 0, NULL);

    /* Second query: draw something simple. */
    ID3D12GraphicsCommandList_BeginQuery(command_list, query_heap, D3D12_QUERY_TYPE_PIPELINE_STATISTICS, 1);

    ID3D12GraphicsCommandList_OMSetRenderTargets(command_list, 1, &context.rtv, false, NULL);
    ID3D12GraphicsCommandList_SetGraphicsRootSignature(command_list, context.root_signature);
    ID3D12GraphicsCommandList_SetPipelineState(command_list, context.pipeline_state);
    ID3D12GraphicsCommandList_IASetPrimitiveTopology(command_list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D12GraphicsCommandList_RSSetViewports(command_list, 1, &context.viewport);
    ID3D12GraphicsCommandList_RSSetScissorRects(command_list, 1, &context.scissor_rect);
    ID3D12GraphicsCommandList_DrawInstanced(command_list, 3, 1, 0, 0);

    ID3D12GraphicsCommandList_EndQuery(command_list, query_heap, D3D12_QUERY_TYPE_PIPELINE_STATISTICS, 1);
    ID3D12GraphicsCommandList_ResolveQueryData(command_list, query_heap, D3D12_QUERY_TYPE_PIPELINE_STATISTICS, 1, 1,
            resource, sizeof(struct D3D12_QUERY_DATA_PIPELINE_STATISTICS));

    get_buffer_readback_with_command_list(resource, DXGI_FORMAT_UNKNOWN, &rb, queue, command_list);

    for (i = 0; i < sizeof(struct D3D12_QUERY_DATA_PIPELINE_STATISTICS) / sizeof(uint64_t); ++i)
    {
        uint64_t value = get_readback_uint64(&rb, i, 0);
        ok(!value, "Element %d: Got %"PRIu64", expected 0.\n", i, value);
    }

    pipeline_statistics = get_readback_data(&rb, 1, 0, 0, sizeof(*pipeline_statistics));

    /* We read 3 vertices that formed one primitive. */
    ok(pipeline_statistics->IAVertices == 3, "IAVertices: Got %"PRIu64", expected 3.\n",
            pipeline_statistics->IAVertices);
    ok(pipeline_statistics->IAPrimitives == 1, "IAPrimitives: Got %"PRIu64", expected 1.\n",
            pipeline_statistics->IAPrimitives);
    ok(pipeline_statistics->VSInvocations == 3, "VSInvocations: Got %"PRIu64", expected 3.\n",
            pipeline_statistics->VSInvocations);

    /* No geometry shader output primitives.
     * Depending on the graphics card, the geometry shader might still have been invoked, so
     * GSInvocations might be whatever. */
    ok(pipeline_statistics->GSPrimitives == 0, "GSPrimitives: Got %"PRIu64", expected 0.\n",
            pipeline_statistics->GSPrimitives);

    /* One primitive sent to the rasterizer, but it might have been broken up into smaller pieces then. */
    ok(pipeline_statistics->CInvocations == 1, "CInvocations: Got %"PRIu64", expected 1.\n",
            pipeline_statistics->CInvocations);
    ok(pipeline_statistics->CPrimitives > 0, "CPrimitives: Got %"PRIu64", expected > 0.\n",
            pipeline_statistics->CPrimitives);

    /* Exact number of pixel shader invocations depends on the graphics card and VRS can affect it. */
    ok(pipeline_statistics->PSInvocations > 0, "PSInvocations: Got %"PRIu64", expected >= %u.\n",
            pipeline_statistics->PSInvocations, 0);

    /* We used no tessellation or compute shaders at all. */
    ok(pipeline_statistics->HSInvocations == 0, "HSInvocations: Got %"PRIu64", expected 0.\n",
            pipeline_statistics->HSInvocations);
    ok(pipeline_statistics->DSInvocations == 0, "DSInvocations: Got %"PRIu64", expected 0.\n",
            pipeline_statistics->DSInvocations);
    ok(pipeline_statistics->CSInvocations == 0, "CSInvocations: Got %"PRIu64", expected 0.\n",
            pipeline_statistics->CSInvocations);

    release_resource_readback(&rb);
    ID3D12QueryHeap_Release(query_heap);
    ID3D12Resource_Release(resource);
    destroy_test_context(&context);
}

void test_query_occlusion(void)
{
    struct test_context_desc desc;
    ID3D12GraphicsCommandList *command_list;
    struct test_context context;
    ID3D12CommandQueue *queue;
    ID3D12Device *device;
    struct depth_stencil_resource ds;
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc;
    D3D12_QUERY_HEAP_DESC heap_desc;
    ID3D12QueryHeap *query_heap;
    ID3D12Resource *resource;
    struct resource_readback rb;
    unsigned int i;
    HRESULT hr;

#include "shaders/query/headers/occlusion.h"

    static const struct
    {
        D3D12_QUERY_TYPE type;
        bool draw;
        float clear_depth;
        float depth;
    }
    tests[] =
    {
        {D3D12_QUERY_TYPE_OCCLUSION,        false, 1.0f, 0.5f},
        {D3D12_QUERY_TYPE_OCCLUSION,        true,  1.0f, 0.5f},
        {D3D12_QUERY_TYPE_BINARY_OCCLUSION, false, 1.0f, 0.5f},
        {D3D12_QUERY_TYPE_BINARY_OCCLUSION, true,  1.0f, 0.5f},
        {D3D12_QUERY_TYPE_OCCLUSION,        false, 0.0f, 0.5f},
        {D3D12_QUERY_TYPE_OCCLUSION,        true,  0.0f, 0.5f},
        {D3D12_QUERY_TYPE_BINARY_OCCLUSION, false, 0.0f, 0.5f},
        {D3D12_QUERY_TYPE_BINARY_OCCLUSION, true,  0.0f, 0.5f},
    };

    memset(&desc, 0, sizeof(desc));
    desc.no_render_target = true;
    if (!init_test_context(&context, &desc))
        return;
    device = context.device;
    command_list = context.list;
    queue = context.queue;

    init_depth_stencil(&ds, context.device, 640, 480, 1, 1, DXGI_FORMAT_D32_FLOAT, 0, NULL);
    set_viewport(&context.viewport, 0.0f, 0.0f, 640.0f, 480.0f, 0.0f, 1.0f);
    set_rect(&context.scissor_rect, 0, 0, 640, 480);

    context.root_signature = create_32bit_constants_root_signature(context.device,
            0, 1, D3D12_SHADER_VISIBILITY_PIXEL);
    init_pipeline_state_desc(&pso_desc, context.root_signature, 0, NULL, &occlusion_dxbc, NULL);
    pso_desc.NumRenderTargets = 0;
    pso_desc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    pso_desc.DepthStencilState.DepthEnable = true;
    pso_desc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    pso_desc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
    hr = ID3D12Device_CreateGraphicsPipelineState(context.device, &pso_desc,
            &IID_ID3D12PipelineState, (void **)&context.pipeline_state);
    ok(SUCCEEDED(hr), "Failed to create graphics pipeline state, hr %#x.\n", (int)hr);

    heap_desc.Type = D3D12_QUERY_HEAP_TYPE_OCCLUSION;
    heap_desc.Count = ARRAY_SIZE(tests);
    heap_desc.NodeMask = 0;
    hr = ID3D12Device_CreateQueryHeap(device, &heap_desc, &IID_ID3D12QueryHeap, (void **)&query_heap);
    ok(SUCCEEDED(hr), "Failed to create query heap, type %u, hr %#x.\n", heap_desc.Type, (int)hr);

    resource = create_readback_buffer(device, ARRAY_SIZE(tests) * sizeof(uint64_t));

    ID3D12GraphicsCommandList_OMSetRenderTargets(command_list, 0, NULL, false, &ds.dsv_handle);
    ID3D12GraphicsCommandList_SetGraphicsRootSignature(command_list, context.root_signature);
    ID3D12GraphicsCommandList_SetPipelineState(command_list, context.pipeline_state);
    ID3D12GraphicsCommandList_IASetPrimitiveTopology(command_list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D12GraphicsCommandList_RSSetViewports(command_list, 1, &context.viewport);
    ID3D12GraphicsCommandList_RSSetScissorRects(command_list, 1, &context.scissor_rect);

    for (i = 0; i < ARRAY_SIZE(tests); ++i)
    {
        vkd3d_test_set_context("Test %u", i);

        ID3D12GraphicsCommandList_ClearDepthStencilView(command_list, ds.dsv_handle,
                D3D12_CLEAR_FLAG_DEPTH, tests[i].clear_depth, 0, 0, NULL);

        ID3D12GraphicsCommandList_BeginQuery(command_list, query_heap, tests[i].type, i);

        if (tests[i].draw)
        {
            ID3D12GraphicsCommandList_SetGraphicsRoot32BitConstants(command_list, 0, 1, &tests[i].depth, 0);
            ID3D12GraphicsCommandList_DrawInstanced(command_list, 3, 1, 0, 0);
        }

        ID3D12GraphicsCommandList_EndQuery(command_list, query_heap, tests[i].type, i);
        ID3D12GraphicsCommandList_ResolveQueryData(command_list, query_heap, tests[i].type, i, 1,
                resource, i * sizeof(uint64_t));
    }
    vkd3d_test_set_context(NULL);

    get_buffer_readback_with_command_list(resource, DXGI_FORMAT_UNKNOWN, &rb, queue, command_list);
    for (i = 0; i < ARRAY_SIZE(tests); ++i)
    {
        const bool samples_passed = tests[i].draw && tests[i].clear_depth > tests[i].depth;
        const uint64_t result = get_readback_uint64(&rb, i, 0);
        uint64_t expected_result;

        if (tests[i].type == D3D12_QUERY_TYPE_BINARY_OCCLUSION)
            expected_result = samples_passed ? 1 : 0;
        else
            expected_result = samples_passed ? 640 * 480 : 0;

        ok(result == expected_result, "Test %u: Got unexpected result %"PRIu64".\n", i, result);
    }
    release_resource_readback(&rb);

    ID3D12QueryHeap_Release(query_heap);
    ID3D12Resource_Release(resource);
    destroy_depth_stencil(&ds);
    destroy_test_context(&context);
}

void test_resolve_non_issued_query_data(void)
{
    static const uint64_t initial_data[] = {0xdeadbeef, 0xdeadbeef, 0xdeadbabe, 0xdeadbeef};
    ID3D12Resource *readback_buffer, *upload_buffer;
    ID3D12GraphicsCommandList *command_list;
    D3D12_QUERY_HEAP_DESC heap_desc;
    struct test_context_desc desc;
    ID3D12QueryHeap *query_heap;
    struct resource_readback rb;
    struct test_context context;
    ID3D12CommandQueue *queue;
    ID3D12Device *device;
    uint64_t *timestamps;
    HRESULT hr;

    memset(&desc, 0, sizeof(desc));
    desc.no_render_target = true;
    if (!init_test_context(&context, &desc))
        return;
    device = context.device;
    command_list = context.list;
    queue = context.queue;

    heap_desc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
    heap_desc.Count = ARRAY_SIZE(initial_data);
    heap_desc.NodeMask = 0;
    hr = ID3D12Device_CreateQueryHeap(device, &heap_desc, &IID_ID3D12QueryHeap, (void **)&query_heap);
    ok(SUCCEEDED(hr), "Failed to create query heap, hr %#x.\n", (int)hr);

    readback_buffer = create_readback_buffer(device, sizeof(initial_data));
    upload_buffer = create_upload_buffer(context.device, sizeof(initial_data), initial_data);

    ID3D12GraphicsCommandList_EndQuery(command_list, query_heap, D3D12_QUERY_TYPE_TIMESTAMP, 0);
    ID3D12GraphicsCommandList_CopyResource(command_list, readback_buffer, upload_buffer);
    ID3D12GraphicsCommandList_EndQuery(command_list, query_heap, D3D12_QUERY_TYPE_TIMESTAMP, 3);
    ID3D12GraphicsCommandList_ResolveQueryData(command_list, query_heap,
            D3D12_QUERY_TYPE_TIMESTAMP, 0, 4, readback_buffer, 0);

    get_buffer_readback_with_command_list(readback_buffer, DXGI_FORMAT_UNKNOWN, &rb, queue, command_list);
    timestamps = get_readback_data(&rb, 0, 0, 0, sizeof(*timestamps));
    ok(timestamps[0] != initial_data[0] && timestamps[0] > 0,
            "Got unexpected timestamp %#"PRIx64".\n", timestamps[0]);
    todo ok(!timestamps[1], "Got unexpected timestamp %#"PRIx64".\n", timestamps[1]);
    todo ok(!timestamps[2], "Got unexpected timestamp %#"PRIx64".\n", timestamps[2]);
    ok(timestamps[3] != initial_data[3] && timestamps[3] > 0,
            "Got unexpected timestamp %#"PRIx64".\n", timestamps[3]);
    release_resource_readback(&rb);

    ID3D12QueryHeap_Release(query_heap);
    ID3D12Resource_Release(readback_buffer);
    ID3D12Resource_Release(upload_buffer);
    destroy_test_context(&context);
}

void test_resolve_query_data_in_different_command_list(void)
{
    ID3D12GraphicsCommandList *command_list;
    D3D12_QUERY_HEAP_DESC heap_desc;
    ID3D12Resource *readback_buffer;
    struct resource_readback rb;
    ID3D12QueryHeap *query_heap;
    struct test_context context;
    ID3D12CommandQueue *queue;
    ID3D12Device *device;
    unsigned int i;
    HRESULT hr;

    static const float white[] = {1.0f, 1.0f, 1.0f, 1.0f};
    const unsigned int readback_buffer_capacity = 4;

    if (!init_test_context(&context, NULL))
        return;
    device = context.device;
    command_list = context.list;
    queue = context.queue;

    heap_desc.Type = D3D12_QUERY_HEAP_TYPE_OCCLUSION;
    heap_desc.Count = 1;
    heap_desc.NodeMask = 0;
    hr = ID3D12Device_CreateQueryHeap(device, &heap_desc, &IID_ID3D12QueryHeap, (void **)&query_heap);
    ok(SUCCEEDED(hr), "Failed to create query heap, hr %#x.\n", (int)hr);

    readback_buffer = create_readback_buffer(device, readback_buffer_capacity * sizeof(uint64_t));

    ID3D12GraphicsCommandList_ClearRenderTargetView(command_list, context.rtv, white, 0, NULL);

    ID3D12GraphicsCommandList_OMSetRenderTargets(command_list, 1, &context.rtv, false, NULL);
    ID3D12GraphicsCommandList_SetGraphicsRootSignature(command_list, context.root_signature);
    ID3D12GraphicsCommandList_SetPipelineState(command_list, context.pipeline_state);
    ID3D12GraphicsCommandList_IASetPrimitiveTopology(command_list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D12GraphicsCommandList_RSSetViewports(command_list, 1, &context.viewport);
    ID3D12GraphicsCommandList_RSSetScissorRects(command_list, 1, &context.scissor_rect);

    ID3D12GraphicsCommandList_BeginQuery(command_list, query_heap, D3D12_QUERY_TYPE_OCCLUSION, 0);
    ID3D12GraphicsCommandList_DrawInstanced(command_list, 3, 1, 0, 0);
    ID3D12GraphicsCommandList_EndQuery(command_list, query_heap, D3D12_QUERY_TYPE_OCCLUSION, 0);

    transition_resource_state(command_list, context.render_target,
            D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
    check_sub_resource_uint(context.render_target, 0, queue, command_list, 0xff00ff00, 0);

    reset_command_list(command_list, context.allocator);
    for (i = 0; i < readback_buffer_capacity / 2; ++i)
    {
        ID3D12GraphicsCommandList_ResolveQueryData(command_list,
                query_heap, D3D12_QUERY_TYPE_OCCLUSION, 0, 1, readback_buffer, i * sizeof(uint64_t));
    }
    hr = ID3D12GraphicsCommandList_Close(command_list);
    ok(SUCCEEDED(hr), "Failed to close command list, hr %#x.\n", (int)hr);
    exec_command_list(queue, command_list);
    wait_queue_idle(context.device, queue);

    reset_command_list(command_list, context.allocator);
    for (; i < readback_buffer_capacity; ++i)
    {
        ID3D12GraphicsCommandList_ResolveQueryData(command_list,
                query_heap, D3D12_QUERY_TYPE_OCCLUSION, 0, 1, readback_buffer, i * sizeof(uint64_t));
    }

    get_buffer_readback_with_command_list(readback_buffer, DXGI_FORMAT_UNKNOWN, &rb, queue, command_list);
    for (i = 0; i < readback_buffer_capacity; ++i)
    {
        uint64_t expected_result = context.render_target_desc.Width * context.render_target_desc.Height;
        uint64_t result = get_readback_uint64(&rb, i, 0);

        ok(result == expected_result, "Got unexpected result %"PRIu64" at %u.\n", result, i);
    }
    release_resource_readback(&rb);

    ID3D12QueryHeap_Release(query_heap);
    ID3D12Resource_Release(readback_buffer);
    destroy_test_context(&context);
}

void test_resolve_query_data_in_reordered_command_list(void)
{
    ID3D12GraphicsCommandList *command_lists[2];
    ID3D12CommandAllocator *command_allocator;
    D3D12_QUERY_HEAP_DESC heap_desc;
    ID3D12Resource *readback_buffer;
    struct resource_readback rb;
    ID3D12QueryHeap *query_heap;
    struct test_context context;
    ID3D12CommandQueue *queue;
    ID3D12Device *device;
    uint64_t result;
    HRESULT hr;

    if (!init_test_context(&context, NULL))
        return;
    device = context.device;
    command_lists[0] = context.list;
    queue = context.queue;

    hr = ID3D12Device_CreateCommandAllocator(device, D3D12_COMMAND_LIST_TYPE_DIRECT,
            &IID_ID3D12CommandAllocator, (void **)&command_allocator);
    ok(SUCCEEDED(hr), "Failed to create command allocator, hr %#x.\n", (int)hr);
    hr = ID3D12Device_CreateCommandList(device, 0, D3D12_COMMAND_LIST_TYPE_DIRECT,
            command_allocator, NULL, &IID_ID3D12GraphicsCommandList, (void **)&command_lists[1]);
    ok(SUCCEEDED(hr), "Failed to create command list, hr %#x.\n", (int)hr);

    heap_desc.Type = D3D12_QUERY_HEAP_TYPE_OCCLUSION;
    heap_desc.Count = 1;
    heap_desc.NodeMask = 0;
    hr = ID3D12Device_CreateQueryHeap(device, &heap_desc, &IID_ID3D12QueryHeap, (void **)&query_heap);
    ok(SUCCEEDED(hr), "Failed to create query heap, hr %#x.\n", (int)hr);

    readback_buffer = create_readback_buffer(device, sizeof(uint64_t));

    /* Read query results in the second command list. */
    ID3D12GraphicsCommandList_ResolveQueryData(command_lists[1],
            query_heap, D3D12_QUERY_TYPE_OCCLUSION, 0, 1, readback_buffer, 0);
    hr = ID3D12GraphicsCommandList_Close(command_lists[1]);
    ok(SUCCEEDED(hr), "Failed to close command list, hr %#x.\n", (int)hr);

    /* Produce query results in the first command list. */
    ID3D12GraphicsCommandList_OMSetRenderTargets(command_lists[0], 1, &context.rtv, false, NULL);
    ID3D12GraphicsCommandList_SetGraphicsRootSignature(command_lists[0], context.root_signature);
    ID3D12GraphicsCommandList_SetPipelineState(command_lists[0], context.pipeline_state);
    ID3D12GraphicsCommandList_IASetPrimitiveTopology(command_lists[0], D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D12GraphicsCommandList_RSSetViewports(command_lists[0], 1, &context.viewport);
    ID3D12GraphicsCommandList_RSSetScissorRects(command_lists[0], 1, &context.scissor_rect);
    ID3D12GraphicsCommandList_BeginQuery(command_lists[0], query_heap, D3D12_QUERY_TYPE_OCCLUSION, 0);
    ID3D12GraphicsCommandList_DrawInstanced(command_lists[0], 3, 1, 0, 0);
    ID3D12GraphicsCommandList_EndQuery(command_lists[0], query_heap, D3D12_QUERY_TYPE_OCCLUSION, 0);
    hr = ID3D12GraphicsCommandList_Close(command_lists[0]);
    ok(SUCCEEDED(hr), "Failed to close command list, hr %#x.\n", (int)hr);

    ID3D12CommandQueue_ExecuteCommandLists(queue,
            ARRAY_SIZE(command_lists), (ID3D12CommandList **)command_lists);
    wait_queue_idle(device, queue);

    reset_command_list(command_lists[0], context.allocator);
    get_buffer_readback_with_command_list(readback_buffer, DXGI_FORMAT_UNKNOWN, &rb, queue, command_lists[0]);
    result = get_readback_uint64(&rb, 0, 0);
    ok(result == context.render_target_desc.Width * context.render_target_desc.Height,
            "Got unexpected result %"PRIu64".\n", result);
    release_resource_readback(&rb);

    ID3D12GraphicsCommandList_Release(command_lists[1]);
    ID3D12CommandAllocator_Release(command_allocator);
    ID3D12QueryHeap_Release(query_heap);
    ID3D12Resource_Release(readback_buffer);
    destroy_test_context(&context);
}

void test_virtual_queries(void)
{
    struct test_context_desc desc;
    ID3D12GraphicsCommandList *command_list;
    struct test_context context;
    ID3D12CommandQueue *queue;
    ID3D12Device *device;
    struct depth_stencil_resource ds[2];
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc;
    D3D12_QUERY_HEAP_DESC heap_desc;
    ID3D12QueryHeap *query_heaps[2];
    ID3D12Resource *resource;
    struct resource_readback rb;
    unsigned int i;
    HRESULT hr;

#include "shaders/query/headers/occlusion.h"

    static const uint32_t expected_results[] = {1,0,1,1,614400,0,307200,307200};
    static const float depth_one = 1.0f;
    static const float depth_zero = 0.0f;

    memset(&desc, 0, sizeof(desc));
    desc.no_render_target = true;
    if (!init_test_context(&context, &desc))
        return;
    device = context.device;
    command_list = context.list;
    queue = context.queue;

    for (i = 0; i < ARRAY_SIZE(ds); i++)
      init_depth_stencil(&ds[i], context.device, 640, 480, 1, 1, DXGI_FORMAT_D32_FLOAT, 0, NULL);
    set_viewport(&context.viewport, 0.0f, 0.0f, 640.0f, 480.0f, 0.0f, 1.0f);
    set_rect(&context.scissor_rect, 0, 0, 640, 480);

    context.root_signature = create_32bit_constants_root_signature(context.device,
            0, 1, D3D12_SHADER_VISIBILITY_PIXEL);
    init_pipeline_state_desc(&pso_desc, context.root_signature, 0, NULL, &occlusion_dxbc, NULL);
    pso_desc.NumRenderTargets = 0;
    pso_desc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    pso_desc.DepthStencilState.DepthEnable = true;
    pso_desc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    pso_desc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
    hr = ID3D12Device_CreateGraphicsPipelineState(context.device, &pso_desc,
            &IID_ID3D12PipelineState, (void **)&context.pipeline_state);
    ok(SUCCEEDED(hr), "Failed to create graphics pipeline state, hr %#x.\n", (int)hr);

    heap_desc.Type = D3D12_QUERY_HEAP_TYPE_OCCLUSION;
    heap_desc.Count = ARRAY_SIZE(expected_results) / 2;
    heap_desc.NodeMask = 0;
    for (i = 0; i < ARRAY_SIZE(query_heaps); i++)
    {
        hr = ID3D12Device_CreateQueryHeap(device, &heap_desc, &IID_ID3D12QueryHeap, (void **)&query_heaps[i]);
        ok(SUCCEEDED(hr), "Failed to create query heap, type %u, hr %#x.\n", heap_desc.Type, (int)hr);
    }

    resource = create_readback_buffer(device, ARRAY_SIZE(expected_results) * sizeof(uint64_t));

    ID3D12GraphicsCommandList_SetGraphicsRootSignature(command_list, context.root_signature);
    ID3D12GraphicsCommandList_SetPipelineState(command_list, context.pipeline_state);
    ID3D12GraphicsCommandList_IASetPrimitiveTopology(command_list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D12GraphicsCommandList_RSSetViewports(command_list, 1, &context.viewport);
    ID3D12GraphicsCommandList_RSSetScissorRects(command_list, 1, &context.scissor_rect);

    ID3D12GraphicsCommandList_ClearDepthStencilView(command_list, ds[0].dsv_handle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, NULL);
    ID3D12GraphicsCommandList_ClearDepthStencilView(command_list, ds[1].dsv_handle, D3D12_CLEAR_FLAG_DEPTH, 0.5f, 0, 0, NULL);

    ID3D12GraphicsCommandList_OMSetRenderTargets(command_list, 0, NULL, false, &ds[0].dsv_handle);
    vkd3d_mute_validation_message("01922", "See vkd3d-proton issue 2381");
    ID3D12GraphicsCommandList_BeginQuery(command_list, query_heaps[0], D3D12_QUERY_TYPE_BINARY_OCCLUSION, 0);
    ID3D12GraphicsCommandList_BeginQuery(command_list, query_heaps[0], D3D12_QUERY_TYPE_BINARY_OCCLUSION, 1);
    ID3D12GraphicsCommandList_BeginQuery(command_list, query_heaps[0], D3D12_QUERY_TYPE_BINARY_OCCLUSION, 2);
    ID3D12GraphicsCommandList_BeginQuery(command_list, query_heaps[1], D3D12_QUERY_TYPE_OCCLUSION, 0);
    ID3D12GraphicsCommandList_BeginQuery(command_list, query_heaps[1], D3D12_QUERY_TYPE_OCCLUSION, 1);
    ID3D12GraphicsCommandList_BeginQuery(command_list, query_heaps[1], D3D12_QUERY_TYPE_OCCLUSION, 2);
    ID3D12GraphicsCommandList_EndQuery(command_list, query_heaps[0], D3D12_QUERY_TYPE_BINARY_OCCLUSION, 1);
    ID3D12GraphicsCommandList_EndQuery(command_list, query_heaps[1], D3D12_QUERY_TYPE_OCCLUSION, 1);
    ID3D12GraphicsCommandList_SetGraphicsRoot32BitConstants(command_list, 0, 1, &depth_zero, 0);
    ID3D12GraphicsCommandList_DrawInstanced(command_list, 3, 1, 0, 0);
    ID3D12GraphicsCommandList_EndQuery(command_list, query_heaps[0], D3D12_QUERY_TYPE_BINARY_OCCLUSION, 2);
    ID3D12GraphicsCommandList_EndQuery(command_list, query_heaps[1], D3D12_QUERY_TYPE_OCCLUSION, 2);

    ID3D12GraphicsCommandList_OMSetRenderTargets(command_list, 0, NULL, false, &ds[1].dsv_handle);
    ID3D12GraphicsCommandList_BeginQuery(command_list, query_heaps[0], D3D12_QUERY_TYPE_BINARY_OCCLUSION, 3);
    ID3D12GraphicsCommandList_BeginQuery(command_list, query_heaps[1], D3D12_QUERY_TYPE_OCCLUSION, 3);
    ID3D12GraphicsCommandList_DrawInstanced(command_list, 3, 1, 0, 0);
    ID3D12GraphicsCommandList_EndQuery(command_list, query_heaps[0], D3D12_QUERY_TYPE_BINARY_OCCLUSION, 0);
    ID3D12GraphicsCommandList_EndQuery(command_list, query_heaps[1], D3D12_QUERY_TYPE_OCCLUSION, 0);
    ID3D12GraphicsCommandList_SetGraphicsRoot32BitConstants(command_list, 0, 1, &depth_one, 0);
    ID3D12GraphicsCommandList_DrawInstanced(command_list, 3, 1, 0, 0);
    ID3D12GraphicsCommandList_EndQuery(command_list, query_heaps[0], D3D12_QUERY_TYPE_BINARY_OCCLUSION, 3);
    ID3D12GraphicsCommandList_EndQuery(command_list, query_heaps[1], D3D12_QUERY_TYPE_OCCLUSION, 3);
    vkd3d_unmute_validation_message("01922");

    ID3D12GraphicsCommandList_OMSetRenderTargets(command_list, 0, NULL, false, &ds[1].dsv_handle);

    for (i = 0; i < ARRAY_SIZE(query_heaps); i++)
    {
        ID3D12GraphicsCommandList_ResolveQueryData(command_list, query_heaps[i],
                i ? D3D12_QUERY_TYPE_OCCLUSION : D3D12_QUERY_TYPE_BINARY_OCCLUSION,
                0, 4, resource, i * 4 * sizeof(uint64_t));
    }

    get_buffer_readback_with_command_list(resource, DXGI_FORMAT_UNKNOWN, &rb, queue, command_list);
    for (i = 0; i < ARRAY_SIZE(expected_results); ++i)
    {
        const uint64_t result = get_readback_uint64(&rb, i, 0);
        ok(result == expected_results[i], "Test %u: Got unexpected result %"PRIu64".\n", i, result);
    }
    release_resource_readback(&rb);

    for (i = 0; i < ARRAY_SIZE(query_heaps); i++)
        ID3D12QueryHeap_Release(query_heaps[i]);
    ID3D12Resource_Release(resource);
    for (i = 0; i < ARRAY_SIZE(ds); i++)
        destroy_depth_stencil(&ds[i]);
    destroy_test_context(&context);
}

void test_query_heap_cpu_resolve_timestamp(void)
{
    struct test_context_desc context_desc;
    D3D12_QUERY_HEAP_DESC heap_desc;
    struct test_context context;
    UINT64 before_gpu_timestamp;
    UINT64 after_gpu_timestamp;
    ID3D12Device15 *device15;
    uint64_t query_data[16];
    ID3D12QueryHeap *heap;
    UINT64 cpu_timestamp;
    HRESULT hr;
    UINT i;

    memset(&context_desc, 0, sizeof(context_desc));
    context_desc.no_pipeline = true;
    context_desc.no_render_target = true;
    context_desc.no_root_signature = true;

    if (!init_test_context(&context, &context_desc))
        return;

    if (FAILED(hr = ID3D12Device_QueryInterface(context.device, &IID_ID3D12Device15, (void **)&device15)))
    {
        skip("ID3D12Device15 not supported.\n");
        destroy_test_context(&context);
    }

    /* There are no feature bits. Native runtime emulates this as necessary. */

    memset(&heap_desc, 0, sizeof(heap_desc));
    heap_desc.Count = 1024;
    heap_desc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
    hr = ID3D12Device15_CreateQueryHeap1(device15, &heap_desc, D3D12_QUERY_HEAP_FLAG_NONE,
            &IID_ID3D12QueryHeap, (void **)&heap);
    ok(SUCCEEDED(hr), "Failed to create query heap, hr #%x.\n", hr);
    hr = ID3D12Device15_ResolveQueryData(device15, heap, D3D12_QUERY_TYPE_TIMESTAMP, 0, 1, query_data);
    ok(hr == E_INVALIDARG, "Expected invalidarg, got hr #%x.\n", hr);
    ID3D12QueryHeap_Release(heap);

    hr = ID3D12Device15_CreateQueryHeap1(device15, &heap_desc, D3D12_QUERY_HEAP_FLAG_CPU_RESOLVE,
            &IID_ID3D12QueryHeap, (void **)&heap);
    ok(SUCCEEDED(hr), "Failed to create query heap, hr #%x.\n", hr);

    /* This should be allowed. */
    memset(query_data, 0xab, sizeof(query_data));
    hr = ID3D12Device15_ResolveQueryData(device15, heap, D3D12_QUERY_TYPE_TIMESTAMP,
            0, ARRAY_SIZE(query_data), query_data);
    ok(SUCCEEDED(hr), "Unexpected failure, got hr #%x.\n", hr);

    for (i = 0; i < ARRAY_SIZE(query_data); i++)
        ok(query_data[i] == 0, "Unexpected data %"PRIu64" for query %u.\n", query_data[i], i);

    hr = ID3D12CommandQueue_GetClockCalibration(context.queue, &before_gpu_timestamp, &cpu_timestamp);
    ok(SUCCEEDED(hr), "Failed to calibrate timestamps, hr #%x.\n", hr);

    for (i = 0; i < ARRAY_SIZE(query_data) - 1; i++)
        ID3D12GraphicsCommandList_EndQuery(context.list, heap, D3D12_QUERY_TYPE_TIMESTAMP, i);
    ID3D12GraphicsCommandList_Close(context.list);
    exec_command_list(context.queue, context.list);

    /* This should be allowed. Results will be bogus. */
    hr = ID3D12Device15_ResolveQueryData(device15, heap, D3D12_QUERY_TYPE_TIMESTAMP,
            0, ARRAY_SIZE(query_data), query_data);
    ok(SUCCEEDED(hr), "Unexpected failure, got hr #%x.\n", hr);

    wait_queue_idle_no_event(context.device, context.queue);
    hr = ID3D12CommandQueue_GetClockCalibration(context.queue, &after_gpu_timestamp, &cpu_timestamp);
    ok(SUCCEEDED(hr), "Failed to calibrate timestamps, hr #%x.\n", hr);

    ok(before_gpu_timestamp <= after_gpu_timestamp, "Unexpected monotonicity, expected %"PRIu64" <= %"PRIu64".\n",
            before_gpu_timestamp, after_gpu_timestamp);

    /* The last query was never written, but should be allowed to resolve as-is. */
    hr = ID3D12Device15_ResolveQueryData(device15, heap, D3D12_QUERY_TYPE_TIMESTAMP,
            0, ARRAY_SIZE(query_data), query_data);
    ok(SUCCEEDED(hr), "Unexpected failure, got hr #%x.\n", hr);

    for (i = 0; i < ARRAY_SIZE(query_data) - 1; i++)
    {
        ok(query_data[i] >= before_gpu_timestamp, "Unexpected monotonicity, expected %"PRIu64" >= %"PRIu64".\n",
                query_data[i], before_gpu_timestamp);
        ok(query_data[i] <= after_gpu_timestamp, "Unexpected monotonicity, expected %"PRIu64" <= %"PRIu64".\n",
                query_data[i], after_gpu_timestamp);

        if (i != 0)
        {
            ok(query_data[i] >= query_data[i - 1], "Unexpected monotonicity, expected %"PRIu64" > %"PRIu64".\n",
                    query_data[i], query_data[i - 1]);
        }
    }

    ID3D12QueryHeap_Release(heap);
    ID3D12Device15_Release(device15);
    destroy_test_context(&context);
}

void test_query_heap_cpu_resolve_occlusion(void)
{
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc;
    struct test_context_desc context_desc;
    struct depth_stencil_resource ds;
    D3D12_QUERY_HEAP_DESC heap_desc;
    struct test_context context;
    ID3D12Device15 *device15;
    uint64_t query_data[16];
    ID3D12QueryHeap *heap;
    HRESULT hr;
    UINT i;

#include "shaders/query/headers/occlusion.h"

    memset(&context_desc, 0, sizeof(context_desc));
    context_desc.no_pipeline = true;
    context_desc.no_render_target = true;
    context_desc.no_root_signature = true;

    if (!init_test_context(&context, &context_desc))
        return;

    if (FAILED(hr = ID3D12Device_QueryInterface(context.device, &IID_ID3D12Device15, (void **)&device15)))
    {
        skip("ID3D12Device15 not supported.\n");
        destroy_test_context(&context);
    }

    /* There are no feature bits. Native runtime emulates this as necessary. */
    init_depth_stencil(&ds, context.device, 16, 16, 1, 1, DXGI_FORMAT_D32_FLOAT, 0, NULL);
    set_viewport(&context.viewport, 0.0f, 0.0f, 16.0f, 16.0f, 0.0f, 1.0f);
    set_rect(&context.scissor_rect, 0, 0, 16, 16);

    context.root_signature = create_32bit_constants_root_signature(context.device, 0, 1, D3D12_SHADER_VISIBILITY_PIXEL);
    init_pipeline_state_desc(&pso_desc, context.root_signature, 0, NULL, &occlusion_dxbc, NULL);
    pso_desc.NumRenderTargets = 0;
    pso_desc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    pso_desc.DepthStencilState.DepthEnable = true;
    pso_desc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    pso_desc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    hr = ID3D12Device_CreateGraphicsPipelineState(context.device, &pso_desc,
            &IID_ID3D12PipelineState, (void **)&context.pipeline_state);
    ok(SUCCEEDED(hr), "Failed to create graphics pipeline state, hr %#x.\n", (int)hr);

    memset(&heap_desc, 0, sizeof(heap_desc));
    heap_desc.Count = 1024;
    heap_desc.Type = D3D12_QUERY_HEAP_TYPE_OCCLUSION;
    hr = ID3D12Device15_CreateQueryHeap1(device15, &heap_desc, D3D12_QUERY_HEAP_FLAG_NONE,
            &IID_ID3D12QueryHeap, (void **)&heap);
    ok(SUCCEEDED(hr), "Failed to create query heap, hr #%x.\n", hr);
    hr = ID3D12Device15_ResolveQueryData(device15, heap, D3D12_QUERY_TYPE_OCCLUSION, 0, 1, query_data);
    ok(hr == E_INVALIDARG, "Expected invalidarg, got hr #%x.\n", hr);
    ID3D12QueryHeap_Release(heap);

    hr = ID3D12Device15_CreateQueryHeap1(device15, &heap_desc, D3D12_QUERY_HEAP_FLAG_CPU_RESOLVE,
            &IID_ID3D12QueryHeap, (void **)&heap);
    ok(SUCCEEDED(hr), "Failed to create query heap, hr #%x.\n", hr);

    /* This should be allowed. Trips device lost on AMD. */
    memset(query_data, 0xab, sizeof(query_data));

    if (!is_amd_windows_device(context.device))
    {
        hr = ID3D12Device15_ResolveQueryData(device15, heap, D3D12_QUERY_TYPE_BINARY_OCCLUSION,
            0, ARRAY_SIZE(query_data), query_data);
        ok(SUCCEEDED(hr), "Unexpected failure, got hr #%x.\n", hr);

        for (i = 0; i < ARRAY_SIZE(query_data); i++)
            ok(query_data[i] == 0, "Unexpected data %"PRIu64" for query %u.\n", query_data[i], i);
    }

    ID3D12GraphicsCommandList_SetGraphicsRootSignature(context.list, context.root_signature);
    ID3D12GraphicsCommandList_SetPipelineState(context.list, context.pipeline_state);
    ID3D12GraphicsCommandList_IASetPrimitiveTopology(context.list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D12GraphicsCommandList_RSSetViewports(context.list, 1, &context.viewport);
    ID3D12GraphicsCommandList_RSSetScissorRects(context.list, 1, &context.scissor_rect);
    ID3D12GraphicsCommandList_SetGraphicsRoot32BitConstant(context.list, 0, 0, 0);
    ID3D12GraphicsCommandList_OMSetRenderTargets(context.list, 0, NULL, false, &ds.dsv_handle);

    for (i = 0; i < ARRAY_SIZE(query_data); i++)
    {
        ID3D12GraphicsCommandList_BeginQuery(context.list, heap, D3D12_QUERY_TYPE_OCCLUSION, i);
        ID3D12GraphicsCommandList_DrawInstanced(context.list, 3, i, 0, 0);
        ID3D12GraphicsCommandList_EndQuery(context.list, heap, D3D12_QUERY_TYPE_OCCLUSION, i);
    }

    ID3D12GraphicsCommandList_Close(context.list);
    exec_command_list(context.queue, context.list);

    /* This should be allowed. Results will be bogus. */
    if (!is_amd_windows_device(context.device))
    {
        hr = ID3D12Device15_ResolveQueryData(device15, heap, D3D12_QUERY_TYPE_BINARY_OCCLUSION,
            0, ARRAY_SIZE(query_data), query_data);
        ok(SUCCEEDED(hr), "Unexpected failure, got hr #%x.\n", hr);
    }

    wait_queue_idle_no_event(context.device, context.queue);

    hr = ID3D12Device15_ResolveQueryData(device15, heap, D3D12_QUERY_TYPE_OCCLUSION,
            0, ARRAY_SIZE(query_data), query_data);
    ok(SUCCEEDED(hr), "Unexpected failure, got hr #%x.\n", hr);

    for (i = 0; i < ARRAY_SIZE(query_data); i++)
    {
        ok(query_data[i] == i * 256, "Unexpected query for query %u. Expected %u, got %"PRIu64".\n",
                i, i * 256, query_data[i]);
    }

    /* It's allowed to resolve a query in different ways based on the context. */
    hr = ID3D12Device15_ResolveQueryData(device15, heap, D3D12_QUERY_TYPE_BINARY_OCCLUSION,
            0, ARRAY_SIZE(query_data), query_data);
    ok(SUCCEEDED(hr), "Unexpected failure, got hr #%x.\n", hr);

    for (i = 0; i < ARRAY_SIZE(query_data); i++)
    {
        ok(query_data[i] == !!i, "Unexpected query for query %u. Expected %u, got %"PRIu64".\n",
                i, !!i, query_data[i]);
    }

    destroy_depth_stencil(&ds);
    ID3D12QueryHeap_Release(heap);
    ID3D12Device15_Release(device15);
    destroy_test_context(&context);
}
