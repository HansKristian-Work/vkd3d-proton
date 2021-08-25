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

static void test_vertex_shader_stream_output(bool use_dxil)
{
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc;
    ID3D12Resource *counter_buffer, *so_buffer;
    ID3D12GraphicsCommandList *command_list;
    D3D12_STREAM_OUTPUT_BUFFER_VIEW sobv;
    ID3D12Resource *upload_buffer;
    struct test_context_desc desc;
    struct resource_readback rb;
    struct test_context context;
    ID3D12CommandQueue *queue;
    unsigned int counter, i;
    const struct vec4 *data;
    ID3D12Device *device;
    HRESULT hr;

    static const D3D12_SO_DECLARATION_ENTRY so_declaration[] =
    {
        {0, "SV_Position", 0, 0, 4, 0},
    };
    static const struct vec4 expected_output[] =
    {
        {-1.0f, 1.0f, 0.0f, 1.0f},
        { 3.0f, 1.0f, 0.0f, 1.0f},
        {-1.0f,-3.0f, 0.0f, 1.0f},
    };
    unsigned int strides[] = {16};

    memset(&desc, 0, sizeof(desc));
    desc.root_signature_flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_STREAM_OUTPUT;
    desc.no_pipeline = true;
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

    if (use_dxil)
        init_pipeline_state_desc_dxil(&pso_desc, context.root_signature, 0, NULL, NULL, NULL);
    else
        init_pipeline_state_desc(&pso_desc, context.root_signature, 0, NULL, NULL, NULL);
    pso_desc.StreamOutput.NumEntries = ARRAY_SIZE(so_declaration);
    pso_desc.StreamOutput.pSODeclaration = so_declaration;
    pso_desc.StreamOutput.pBufferStrides = strides;
    pso_desc.StreamOutput.NumStrides = ARRAY_SIZE(strides);
    pso_desc.StreamOutput.RasterizedStream = D3D12_SO_NO_RASTERIZED_STREAM;
    hr = ID3D12Device_CreateGraphicsPipelineState(device, &pso_desc,
            &IID_ID3D12PipelineState, (void **)&context.pipeline_state);
    if (hr == E_NOTIMPL)
    {
        skip("Stream output is not supported.\n");
        destroy_test_context(&context);
        return;
    }
    ok(hr == S_OK, "Failed to create graphics pipeline state, hr %#x.\n", hr);

    counter = 0;
    upload_buffer = create_upload_buffer(device, sizeof(counter), &counter);

    counter_buffer = create_default_buffer(device, 32,
            D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
    so_buffer = create_default_buffer(device, 1024,
            D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_STREAM_OUT);
    sobv.BufferLocation = ID3D12Resource_GetGPUVirtualAddress(so_buffer);
    sobv.SizeInBytes = 1024;
    sobv.BufferFilledSizeLocation = ID3D12Resource_GetGPUVirtualAddress(counter_buffer);

    ID3D12GraphicsCommandList_CopyBufferRegion(command_list, counter_buffer, 0,
            upload_buffer, 0, sizeof(counter));

    transition_resource_state(command_list, counter_buffer,
            D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_STREAM_OUT);

    ID3D12GraphicsCommandList_RSSetScissorRects(command_list, 1, &context.scissor_rect);
    ID3D12GraphicsCommandList_RSSetViewports(command_list, 1, &context.viewport);
    ID3D12GraphicsCommandList_SetGraphicsRootSignature(command_list, context.root_signature);
    ID3D12GraphicsCommandList_SetPipelineState(command_list, context.pipeline_state);
    ID3D12GraphicsCommandList_IASetPrimitiveTopology(command_list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D12GraphicsCommandList_SOSetTargets(command_list, 0, 1, &sobv);
    ID3D12GraphicsCommandList_DrawInstanced(command_list, 3, 1, 0, 0);

    transition_resource_state(command_list, counter_buffer,
            D3D12_RESOURCE_STATE_STREAM_OUT, D3D12_RESOURCE_STATE_COPY_SOURCE);
    transition_resource_state(command_list, so_buffer,
            D3D12_RESOURCE_STATE_STREAM_OUT, D3D12_RESOURCE_STATE_COPY_SOURCE);

    get_buffer_readback_with_command_list(counter_buffer, DXGI_FORMAT_R32_UINT, &rb, queue, command_list);
    counter = get_readback_uint(&rb, 0, 0, 0);
    ok(counter == 3 * sizeof(struct vec4), "Got unexpected counter %u.\n", counter);
    release_resource_readback(&rb);
    reset_command_list(command_list, context.allocator);
    get_buffer_readback_with_command_list(so_buffer, DXGI_FORMAT_UNKNOWN, &rb, queue, command_list);
    for (i = 0; i < ARRAY_SIZE(expected_output); ++i)
    {
        const struct vec4 *expected = &expected_output[i];
        data = get_readback_vec4(&rb, i, 0);
        ok(compare_vec4(data, expected, 1),
                "Got {%.8e, %.8e, %.8e, %.8e}, expected {%.8e, %.8e, %.8e, %.8e}.\n",
                data->x, data->y, data->z, data->w, expected->x, expected->y, expected->z, expected->w);
    }
    release_resource_readback(&rb);

    ID3D12Resource_Release(counter_buffer);
    ID3D12Resource_Release(upload_buffer);
    ID3D12Resource_Release(so_buffer);
    destroy_test_context(&context);
}

void test_index_buffer_edge_case_stream_output(void)
{
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc;
    ID3D12Resource *counter_buffer, *so_buffer;
    ID3D12GraphicsCommandList *command_list;
    D3D12_STREAM_OUTPUT_BUFFER_VIEW sobv;
    ID3D12Resource *upload_buffer;
    struct test_context_desc desc;
    ID3D12Resource *index_buffer;
    struct resource_readback rb;
    D3D12_INDEX_BUFFER_VIEW ibv;
    struct test_context context;
    ID3D12CommandQueue *queue;
    unsigned int counter, i;
    const struct vec4 *data;
    ID3D12Device *device;
    HRESULT hr;

    static const D3D12_SO_DECLARATION_ENTRY so_declaration[] =
    {
        {0, "SV_Position", 0, 0, 4, 0},
    };
    static const struct vec4 expected_output[] =
    {
        {-1.0f, 1.0f, 0.0f, 1.0f},
        { 3.0f, 1.0f, 0.0f, 1.0f},
        {-1.0f,-3.0f, 0.0f, 1.0f},
        {-1.0f, 1.0f, 0.0f, 1.0f}, /* For the case where we are rendering with NULL index. The first vertex is always picked. */
        {-1.0f, 1.0f, 0.0f, 1.0f},
        {-1.0f, 1.0f, 0.0f, 1.0f},
        {-1.0f, 1.0f, 0.0f, 1.0f}, /* For DrawInstanced with NULL index buffer, which should work just fine. */
        { 3.0f, 1.0f, 0.0f, 1.0f},
        {-1.0f,-3.0f, 0.0f, 1.0f},
        {-1.0f, 1.0f, 0.0f, 1.0f}, /* For the case where we are rendering with NULL GPU VA for index. */
        {-1.0f, 1.0f, 0.0f, 1.0f},
        {-1.0f, 1.0f, 0.0f, 1.0f},
        {-1.0f,-3.0f, 0.0f, 1.0f}, /* For the case where we are rendering with UNKNOWN index format. It is actually R16_UINT. */
        { 3.0f, 1.0f, 0.0f, 1.0f},
        {-1.0f, 1.0f, 0.0f, 1.0f},
    };
    unsigned int strides[] = {16};
    static const uint16_t index_data[3] = { 2, 1, 0 };

    memset(&desc, 0, sizeof(desc));
    desc.root_signature_flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_STREAM_OUTPUT;
    desc.no_pipeline = true;
    if (!init_test_context(&context, &desc))
        return;

    device = context.device;
    command_list = context.list;
    queue = context.queue;

    index_buffer = create_upload_buffer(device, sizeof(index_data), index_data);

    init_pipeline_state_desc(&pso_desc, context.root_signature, 0, NULL, NULL, NULL);
    pso_desc.StreamOutput.NumEntries = ARRAY_SIZE(so_declaration);
    pso_desc.StreamOutput.pSODeclaration = so_declaration;
    pso_desc.StreamOutput.pBufferStrides = strides;
    pso_desc.StreamOutput.NumStrides = ARRAY_SIZE(strides);
    pso_desc.StreamOutput.RasterizedStream = D3D12_SO_NO_RASTERIZED_STREAM;
    hr = ID3D12Device_CreateGraphicsPipelineState(device, &pso_desc,
            &IID_ID3D12PipelineState, (void **)&context.pipeline_state);
    if (hr == E_NOTIMPL)
    {
        skip("Stream output is not supported.\n");
        destroy_test_context(&context);
        return;
    }
    ok(hr == S_OK, "Failed to create graphics pipeline state, hr %#x.\n", hr);

    counter = 0;
    upload_buffer = create_upload_buffer(device, sizeof(counter), &counter);

    counter_buffer = create_default_buffer(device, 32,
            D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
    so_buffer = create_default_buffer(device, 1024,
            D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_STREAM_OUT);
    sobv.BufferLocation = ID3D12Resource_GetGPUVirtualAddress(so_buffer);
    sobv.SizeInBytes = 1024;
    sobv.BufferFilledSizeLocation = ID3D12Resource_GetGPUVirtualAddress(counter_buffer);

    ID3D12GraphicsCommandList_CopyBufferRegion(command_list, counter_buffer, 0,
            upload_buffer, 0, sizeof(counter));

    transition_resource_state(command_list, counter_buffer,
            D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_STREAM_OUT);

    ID3D12GraphicsCommandList_RSSetScissorRects(command_list, 1, &context.scissor_rect);
    ID3D12GraphicsCommandList_RSSetViewports(command_list, 1, &context.viewport);
    ID3D12GraphicsCommandList_SetGraphicsRootSignature(command_list, context.root_signature);
    ID3D12GraphicsCommandList_SetPipelineState(command_list, context.pipeline_state);
    ID3D12GraphicsCommandList_IASetPrimitiveTopology(command_list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D12GraphicsCommandList_SOSetTargets(command_list, 0, 1, &sobv);
    ID3D12GraphicsCommandList_DrawInstanced(command_list, 3, 1, 0, 0);

    /* Should render all 0 indices. */
    ID3D12GraphicsCommandList_IASetIndexBuffer(command_list, NULL);
    ID3D12GraphicsCommandList_DrawIndexedInstanced(command_list, 3, 1, 1, 1, 0);

    /* Should still render. */
    ID3D12GraphicsCommandList_DrawInstanced(command_list, 3, 1, 0, 0);

    ibv.BufferLocation = 0;
    ibv.Format = DXGI_FORMAT_R32_UINT;
    ibv.SizeInBytes = 0;

    /* Should render all 0 indices. */
    ID3D12GraphicsCommandList_IASetIndexBuffer(command_list, &ibv);
    ID3D12GraphicsCommandList_DrawIndexedInstanced(command_list, 3, 1, 2, 1, 0);

    /* This is supposed to be illegal, but works anyways. UNKNOWN is R16_UINT on AMD at least. */
    ibv.BufferLocation = ID3D12Resource_GetGPUVirtualAddress(index_buffer);
    ibv.Format = DXGI_FORMAT_UNKNOWN;
    ibv.SizeInBytes = sizeof(index_data);
    ID3D12GraphicsCommandList_IASetIndexBuffer(command_list, &ibv);
    ID3D12GraphicsCommandList_DrawIndexedInstanced(command_list, 3, 1, 0, 0, 0);

    transition_resource_state(command_list, counter_buffer,
            D3D12_RESOURCE_STATE_STREAM_OUT, D3D12_RESOURCE_STATE_COPY_SOURCE);
    transition_resource_state(command_list, so_buffer,
            D3D12_RESOURCE_STATE_STREAM_OUT, D3D12_RESOURCE_STATE_COPY_SOURCE);

    get_buffer_readback_with_command_list(counter_buffer, DXGI_FORMAT_R32_UINT, &rb, queue, command_list);
    counter = get_readback_uint(&rb, 0, 0, 0);
    todo ok(counter == 15 * sizeof(struct vec4), "Got unexpected counter %u.\n", counter);
    release_resource_readback(&rb);
    reset_command_list(command_list, context.allocator);
    get_buffer_readback_with_command_list(so_buffer, DXGI_FORMAT_UNKNOWN, &rb, queue, command_list);
    for (i = 0; i < ARRAY_SIZE(expected_output); ++i)
    {
        const struct vec4 *expected = &expected_output[i];
        data = get_readback_vec4(&rb, i, 0);
        todo_if(i >= 4 && i != 7) ok(compare_vec4(data, expected, 1),
                "Got {%.8e, %.8e, %.8e, %.8e}, expected {%.8e, %.8e, %.8e, %.8e}.\n",
                data->x, data->y, data->z, data->w, expected->x, expected->y, expected->z, expected->w);
    }
    release_resource_readback(&rb);

    ID3D12Resource_Release(counter_buffer);
    ID3D12Resource_Release(upload_buffer);
    ID3D12Resource_Release(so_buffer);
    ID3D12Resource_Release(index_buffer);
    destroy_test_context(&context);
}

void test_vertex_shader_stream_output_dxbc(void)
{
    test_vertex_shader_stream_output(false);
}

void test_vertex_shader_stream_output_dxil(void)
{
    test_vertex_shader_stream_output(true);
}

