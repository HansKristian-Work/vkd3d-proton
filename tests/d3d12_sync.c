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

void test_queue_wait(void)
{
    D3D12_TEXTURE_COPY_LOCATION dst_location, src_location;
    ID3D12GraphicsCommandList *command_list;
    ID3D12Resource *readback_buffer, *cb;
    ID3D12CommandQueue *queue, *queue2;
    D3D12_RESOURCE_DESC resource_desc;
    uint64_t row_pitch, buffer_size;
    struct test_context_desc desc;
    struct resource_readback rb;
    ID3D12Fence *fence, *fence2;
    struct test_context context;
    ID3D12Device *device;
    unsigned int ret;
    uint64_t value;
    float color[4];
    HANDLE event;
    HRESULT hr;

    static const float blue[] = {0.0f, 0.0f, 1.0f, 1.0f};
    static const float green[] = {0.0f, 1.0f, 0.0f, 1.0f};
    static const float white[] = {1.0f, 1.0f, 1.0f, 1.0f};
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

    memset(&desc, 0, sizeof(desc));
    desc.no_root_signature = true;
    if (!init_test_context(&context, &desc))
        return;
    device = context.device;
    command_list = context.list;
    queue = context.queue;

    queue2 = create_command_queue(device, D3D12_COMMAND_LIST_TYPE_DIRECT, D3D12_COMMAND_QUEUE_PRIORITY_NORMAL);

    event = create_event();
    ok(event, "Failed to create event.\n");

    hr = ID3D12Device_CreateFence(device, 1, D3D12_FENCE_FLAG_NONE, &IID_ID3D12Fence, (void **)&fence);
    ok(hr == S_OK, "Failed to create fence, hr %#x.\n", hr);
    hr = ID3D12Device_CreateFence(device, 0, D3D12_FENCE_FLAG_NONE, &IID_ID3D12Fence, (void **)&fence2);
    ok(hr == S_OK, "Failed to create fence, hr %#x.\n", hr);

    context.root_signature = create_cb_root_signature(context.device,
            0, D3D12_SHADER_VISIBILITY_PIXEL, D3D12_ROOT_SIGNATURE_FLAG_NONE);
    context.pipeline_state = create_pipeline_state(context.device,
            context.root_signature, context.render_target_desc.Format, NULL, &ps, NULL);

    cb = create_upload_buffer(device, sizeof(color), NULL);

    resource_desc = ID3D12Resource_GetDesc(context.render_target);

    row_pitch = align(resource_desc.Width * format_size(resource_desc.Format), D3D12_TEXTURE_DATA_PITCH_ALIGNMENT);
    buffer_size = row_pitch * resource_desc.Height;
    readback_buffer = create_readback_buffer(device, buffer_size);

    ID3D12GraphicsCommandList_ClearRenderTargetView(command_list, context.rtv, white, 0, NULL);
    ID3D12GraphicsCommandList_OMSetRenderTargets(command_list, 1, &context.rtv, false, NULL);
    ID3D12GraphicsCommandList_SetGraphicsRootSignature(command_list, context.root_signature);
    ID3D12GraphicsCommandList_SetPipelineState(command_list, context.pipeline_state);
    ID3D12GraphicsCommandList_IASetPrimitiveTopology(command_list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D12GraphicsCommandList_RSSetViewports(command_list, 1, &context.viewport);
    ID3D12GraphicsCommandList_RSSetScissorRects(command_list, 1, &context.scissor_rect);
    ID3D12GraphicsCommandList_SetGraphicsRootConstantBufferView(command_list, 0,
            ID3D12Resource_GetGPUVirtualAddress(cb));
    ID3D12GraphicsCommandList_DrawInstanced(command_list, 3, 1, 0, 0);

    transition_resource_state(command_list, context.render_target,
            D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);

    dst_location.pResource = readback_buffer;
    dst_location.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dst_location.PlacedFootprint.Offset = 0;
    dst_location.PlacedFootprint.Footprint.Format = resource_desc.Format;
    dst_location.PlacedFootprint.Footprint.Width = resource_desc.Width;
    dst_location.PlacedFootprint.Footprint.Height = resource_desc.Height;
    dst_location.PlacedFootprint.Footprint.Depth = 1;
    dst_location.PlacedFootprint.Footprint.RowPitch = row_pitch;
    src_location.pResource = context.render_target;
    src_location.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    src_location.SubresourceIndex = 0;
    ID3D12GraphicsCommandList_CopyTextureRegion(command_list, &dst_location, 0, 0, 0, &src_location, NULL);

    transition_resource_state(command_list, context.render_target,
            D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);

    hr = ID3D12GraphicsCommandList_Close(command_list);
    ok(hr == S_OK, "Failed to close command list, hr %#x.\n", hr);

    /* Wait() with signaled fence */
    update_buffer_data(cb, 0, sizeof(green), &green);
    queue_wait(queue, fence, 1);
    exec_command_list(queue, command_list);
    wait_queue_idle(device, queue);
    init_readback(&rb, readback_buffer, buffer_size, resource_desc.Width, resource_desc.Height, 1, row_pitch);
    check_readback_data_uint(&rb, NULL, 0xff00ff00, 0);
    release_resource_readback(&rb);

    /* Wait() before CPU signal */
    update_buffer_data(cb, 0, sizeof(blue), &blue);
    queue_wait(queue, fence, 2);
    exec_command_list(queue, command_list);
    queue_signal(queue, fence2, 1);
    hr = ID3D12Fence_SetEventOnCompletion(fence2, 1, event);
    ok(hr == S_OK, "Failed to set event on completion, hr %#x.\n", hr);
    ret = wait_event(event, 0);
    ok(ret == WAIT_TIMEOUT, "Got unexpected return value %#x.\n", ret);
    init_readback(&rb, readback_buffer, buffer_size, resource_desc.Width, resource_desc.Height, 1, row_pitch);
    check_readback_data_uint(&rb, NULL, 0xff00ff00, 0);
    release_resource_readback(&rb);
    value = ID3D12Fence_GetCompletedValue(fence2);
    ok(value == 0, "Got unexpected value %"PRIu64".\n", value);

    hr = ID3D12Fence_Signal(fence, 2);
    ok(hr == S_OK, "Failed to signal fence, hr %#x.\n", hr);
    ret = wait_event(event, INFINITE);
    ok(ret == WAIT_OBJECT_0, "Got unexpected return value %#x.\n", ret);
    ret = wait_event(event, 0);
    ok(ret == WAIT_TIMEOUT, "Got unexpected return value %#x.\n", ret);
    init_readback(&rb, readback_buffer, buffer_size, resource_desc.Width, resource_desc.Height, 1, row_pitch);
    check_readback_data_uint(&rb, NULL, 0xffff0000, 0);
    release_resource_readback(&rb);
    value = ID3D12Fence_GetCompletedValue(fence2);
    ok(value == 1, "Got unexpected value %"PRIu64".\n", value);

    /* Wait() before GPU signal */
    update_buffer_data(cb, 0, sizeof(green), &green);
    queue_wait(queue, fence, 3);
    exec_command_list(queue, command_list);
    queue_signal(queue, fence2, 2);
    hr = ID3D12Fence_SetEventOnCompletion(fence2, 2, event);
    ok(hr == S_OK, "Failed to set event on completion, hr %#x.\n", hr);
    ret = wait_event(event, 0);
    ok(ret == WAIT_TIMEOUT, "Got unexpected return value %#x.\n", ret);
    init_readback(&rb, readback_buffer, buffer_size, resource_desc.Width, resource_desc.Height, 1, row_pitch);
    check_readback_data_uint(&rb, NULL, 0xffff0000, 0);
    release_resource_readback(&rb);
    value = ID3D12Fence_GetCompletedValue(fence2);
    ok(value == 1, "Got unexpected value %"PRIu64".\n", value);

    queue_signal(queue2, fence, 3);
    ret = wait_event(event, INFINITE);
    ok(ret == WAIT_OBJECT_0, "Got unexpected return value %#x.\n", ret);
    ret = wait_event(event, 0);
    ok(ret == WAIT_TIMEOUT, "Got unexpected return value %#x.\n", ret);
    init_readback(&rb, readback_buffer, buffer_size, resource_desc.Width, resource_desc.Height, 1, row_pitch);
    check_readback_data_uint(&rb, NULL, 0xff00ff00, 0);
    release_resource_readback(&rb);
    value = ID3D12Fence_GetCompletedValue(fence2);
    ok(value == 2, "Got unexpected value %"PRIu64".\n", value);

    /* update constant buffer after Wait() */
    queue_wait(queue, fence, 5);
    exec_command_list(queue, command_list);
    hr = ID3D12Fence_Signal(fence, 4);
    ok(hr == S_OK, "Failed to signal fence, hr %#x.\n", hr);
    update_buffer_data(cb, 0, sizeof(blue), &blue);
    hr = ID3D12Fence_Signal(fence, 5);
    ok(hr == S_OK, "Failed to signal fence, hr %#x.\n", hr);
    wait_queue_idle(device, queue);
    init_readback(&rb, readback_buffer, buffer_size, resource_desc.Width, resource_desc.Height, 1, row_pitch);
    check_readback_data_uint(&rb, NULL, 0xffff0000, 0);
    release_resource_readback(&rb);

    hr = ID3D12Fence_Signal(fence, 6);
    ok(hr == S_OK, "Failed to signal fence, hr %#x.\n", hr);
    update_buffer_data(cb, 0, sizeof(green), &green);
    exec_command_list(queue, command_list);
    wait_queue_idle(device, queue);
    init_readback(&rb, readback_buffer, buffer_size, resource_desc.Width, resource_desc.Height, 1, row_pitch);
    check_readback_data_uint(&rb, NULL, 0xff00ff00, 0);
    release_resource_readback(&rb);

    /* Signal() and Wait() in the same command queue */
    update_buffer_data(cb, 0, sizeof(blue), &blue);
    queue_signal(queue, fence, 7);
    queue_wait(queue, fence, 7);
    exec_command_list(queue, command_list);
    wait_queue_idle(device, queue);
    init_readback(&rb, readback_buffer, buffer_size, resource_desc.Width, resource_desc.Height, 1, row_pitch);
    check_readback_data_uint(&rb, NULL, 0xffff0000, 0);
    release_resource_readback(&rb);

    value = ID3D12Fence_GetCompletedValue(fence);
    ok(value == 7, "Got unexpected value %"PRIu64".\n", value);

    destroy_event(event);
    ID3D12Fence_Release(fence);
    ID3D12Fence_Release(fence2);
    ID3D12Resource_Release(cb);
    ID3D12CommandQueue_Release(queue2);
    ID3D12Resource_Release(readback_buffer);
    destroy_test_context(&context);
}

void test_graphics_compute_queue_synchronization(void)
{
    ID3D12GraphicsCommandList *graphics_lists[2], *compute_lists[2];
    D3D12_ROOT_SIGNATURE_DESC root_signature_desc;
    ID3D12PipelineState *compute_pipeline_state;
    D3D12_UNORDERED_ACCESS_VIEW_DESC uav_desc;
    ID3D12CommandQueue *queue, *compute_queue;
    ID3D12DescriptorHeap *cpu_heap, *gpu_heap;
    ID3D12GraphicsCommandList *command_list;
    D3D12_ROOT_PARAMETER root_parameters[2];
    ID3D12CommandAllocator *allocators[3];
    struct test_context_desc desc;
    struct resource_readback rb;
    struct test_context context;
    ID3D12Fence *fence, *fence2;
    ID3D12Resource *buffer;
    ID3D12Device *device;
    uint32_t value;
    unsigned int i;
    HRESULT hr;

    unsigned int clear_value[4] = {0};
    static const DWORD cs_code[] =
    {
#if 0
        uint expected_value;
        RWByteAddressBuffer u : register(u1);

        [numthreads(64, 1, 1)]
        void main(void)
        {
            u.InterlockedCompareStore(0, expected_value, expected_value + 10);
        }
#endif
        0x43425844, 0x7909aab0, 0xf8576455, 0x58f9dd61, 0x3e7e64f0, 0x00000001, 0x000000e0, 0x00000003,
        0x0000002c, 0x0000003c, 0x0000004c, 0x4e475349, 0x00000008, 0x00000000, 0x00000008, 0x4e47534f,
        0x00000008, 0x00000000, 0x00000008, 0x58454853, 0x0000008c, 0x00050050, 0x00000023, 0x0100086a,
        0x04000059, 0x00208e46, 0x00000000, 0x00000001, 0x0300009d, 0x0011e000, 0x00000001, 0x02000068,
        0x00000001, 0x0400009b, 0x00000040, 0x00000001, 0x00000001, 0x0800001e, 0x00100012, 0x00000000,
        0x0020800a, 0x00000000, 0x00000000, 0x00004001, 0x0000000a, 0x0a0000ac, 0x0011e000, 0x00000001,
        0x00004001, 0x00000000, 0x0020800a, 0x00000000, 0x00000000, 0x0010000a, 0x00000000, 0x0100003e,
    };
    static const DWORD ps_code[] =
    {
#if 0
        uint expected_value;
        RWByteAddressBuffer u;

        float4 main(void) : SV_Target
        {
            u.InterlockedCompareStore(0, expected_value, expected_value + 2);
            return float4(0, 1, 0, 1);
        }
#endif
        0x43425844, 0x82fbce04, 0xa014204c, 0xc4d46d91, 0x1081c7a7, 0x00000001, 0x00000120, 0x00000003,
        0x0000002c, 0x0000003c, 0x00000070, 0x4e475349, 0x00000008, 0x00000000, 0x00000008, 0x4e47534f,
        0x0000002c, 0x00000001, 0x00000008, 0x00000020, 0x00000000, 0x00000000, 0x00000003, 0x00000000,
        0x0000000f, 0x545f5653, 0x65677261, 0xabab0074, 0x58454853, 0x000000a8, 0x00000050, 0x0000002a,
        0x0100086a, 0x04000059, 0x00208e46, 0x00000000, 0x00000001, 0x0300009d, 0x0011e000, 0x00000001,
        0x03000065, 0x001020f2, 0x00000000, 0x02000068, 0x00000001, 0x0800001e, 0x00100012, 0x00000000,
        0x0020800a, 0x00000000, 0x00000000, 0x00004001, 0x00000002, 0x0a0000ac, 0x0011e000, 0x00000001,
        0x00004001, 0x00000000, 0x0020800a, 0x00000000, 0x00000000, 0x0010000a, 0x00000000, 0x08000036,
        0x001020f2, 0x00000000, 0x00004002, 0x00000000, 0x3f800000, 0x00000000, 0x3f800000, 0x0100003e,
    };
    static const D3D12_SHADER_BYTECODE ps = {ps_code, sizeof(ps_code)};

    memset(&desc, 0, sizeof(desc));
    desc.no_root_signature = true;
    if (!init_test_context(&context, &desc))
        return;
    device = context.device;
    queue = context.queue;

    root_parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    root_parameters[0].Descriptor.ShaderRegister = 1;
    root_parameters[0].Descriptor.RegisterSpace = 0;
    root_parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    root_parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    root_parameters[1].Constants.ShaderRegister = 0;
    root_parameters[1].Constants.RegisterSpace = 0;
    root_parameters[1].Constants.Num32BitValues = 1;
    root_parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    root_signature_desc.NumParameters = ARRAY_SIZE(root_parameters);
    root_signature_desc.pParameters = root_parameters;
    root_signature_desc.NumStaticSamplers = 0;
    root_signature_desc.pStaticSamplers = NULL;
    root_signature_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
    hr = create_root_signature(device, &root_signature_desc, &context.root_signature);
    ok(hr == S_OK, "Failed to create root signature, hr %#x.\n", hr);

    compute_pipeline_state = create_compute_pipeline_state(device, context.root_signature,
            shader_bytecode(cs_code, sizeof(cs_code)));
    context.pipeline_state = create_pipeline_state(context.device,
            context.root_signature, context.render_target_desc.Format, NULL, &ps, NULL);

    compute_queue = create_command_queue(device, D3D12_COMMAND_LIST_TYPE_COMPUTE, D3D12_COMMAND_QUEUE_PRIORITY_NORMAL);
    hr = ID3D12Device_CreateFence(device, 0, D3D12_FENCE_FLAG_NONE, &IID_ID3D12Fence, (void **)&fence);
    ok(hr == S_OK, "Failed to create fence, hr %#x.\n", hr);
    hr = ID3D12Device_CreateFence(device, 0, D3D12_FENCE_FLAG_NONE, &IID_ID3D12Fence, (void **)&fence2);
    ok(hr == S_OK, "Failed to create fence, hr %#x.\n", hr);

    buffer = create_default_buffer(device, 1024,
            D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    hr = ID3D12Device_CreateCommandAllocator(device, D3D12_COMMAND_LIST_TYPE_DIRECT,
            &IID_ID3D12CommandAllocator, (void **)&allocators[0]);
    ok(hr == S_OK, "Failed to create command allocator, hr %#x.\n", hr);
    hr = ID3D12Device_CreateCommandList(device, 0, D3D12_COMMAND_LIST_TYPE_DIRECT,
            allocators[0], NULL, &IID_ID3D12GraphicsCommandList, (void **)&graphics_lists[0]);
    ok(hr == S_OK, "Failed to create command list, hr %#x.\n", hr);

    graphics_lists[1] = context.list;
    ID3D12GraphicsCommandList_AddRef(graphics_lists[1]);

    for (i = 0; i < ARRAY_SIZE(compute_lists); ++i)
    {
        hr = ID3D12Device_CreateCommandAllocator(device, D3D12_COMMAND_LIST_TYPE_COMPUTE,
                &IID_ID3D12CommandAllocator, (void **)&allocators[i + 1]);
        ok(hr == S_OK, "Failed to create command allocator, hr %#x.\n", hr);
        hr = ID3D12Device_CreateCommandList(device, 0, D3D12_COMMAND_LIST_TYPE_COMPUTE,
                allocators[i + 1], NULL, &IID_ID3D12GraphicsCommandList, (void **)&compute_lists[i]);
        ok(hr == S_OK, "Failed to create command list, hr %#x.\n", hr);
    }

    cpu_heap = create_cpu_descriptor_heap(device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1);
    gpu_heap = create_gpu_descriptor_heap(device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1);

    memset(&uav_desc, 0, sizeof(uav_desc));
    uav_desc.Format = DXGI_FORMAT_R32_UINT;
    uav_desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    uav_desc.Buffer.NumElements = 1024 / sizeof(uint32_t);
    ID3D12Device_CreateUnorderedAccessView(device, buffer, NULL, &uav_desc,
            get_cpu_descriptor_handle(&context, cpu_heap, 0));
    ID3D12Device_CreateUnorderedAccessView(device, buffer, NULL, &uav_desc,
            get_cpu_descriptor_handle(&context, gpu_heap, 0));

    ID3D12GraphicsCommandList_ClearUnorderedAccessViewUint(compute_lists[0],
            get_gpu_descriptor_handle(&context, gpu_heap, 0),
            get_cpu_descriptor_handle(&context, cpu_heap, 0),
            buffer, clear_value, 0, NULL);
    uav_barrier(compute_lists[0], buffer);

    value = 0;
    for (i = 0; i < ARRAY_SIZE(compute_lists); ++i)
    {
        command_list = compute_lists[i];

        ID3D12GraphicsCommandList_SetComputeRootSignature(command_list, context.root_signature);
        ID3D12GraphicsCommandList_SetPipelineState(command_list, compute_pipeline_state);
        ID3D12GraphicsCommandList_SetComputeRootUnorderedAccessView(command_list,
                0, ID3D12Resource_GetGPUVirtualAddress(buffer));
        ID3D12GraphicsCommandList_SetComputeRoot32BitConstants(command_list, 1, 1, &value, 0);
        ID3D12GraphicsCommandList_Dispatch(command_list, 1, 1, 1);
        hr = ID3D12GraphicsCommandList_Close(command_list);
        ok(hr == S_OK, "Failed to close command list, hr %#x.\n", hr);

        value += 10;

        command_list = graphics_lists[i];

        ID3D12GraphicsCommandList_SetGraphicsRootSignature(command_list, context.root_signature);
        ID3D12GraphicsCommandList_SetPipelineState(command_list, context.pipeline_state);
        ID3D12GraphicsCommandList_IASetPrimitiveTopology(command_list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ID3D12GraphicsCommandList_RSSetViewports(command_list, 1, &context.viewport);
        ID3D12GraphicsCommandList_RSSetScissorRects(command_list, 1, &context.scissor_rect);
        ID3D12GraphicsCommandList_OMSetRenderTargets(command_list, 1, &context.rtv, false, NULL);
        ID3D12GraphicsCommandList_SetGraphicsRootUnorderedAccessView(command_list,
                0, ID3D12Resource_GetGPUVirtualAddress(buffer));
        ID3D12GraphicsCommandList_SetGraphicsRoot32BitConstants(command_list, 1, 1, &value, 0);
        ID3D12GraphicsCommandList_DrawInstanced(command_list, 3, 1, 0, 0);
        hr = ID3D12GraphicsCommandList_Close(command_list);
        ok(hr == S_OK, "Failed to close command list, hr %#x.\n", hr);

        value += 2;
    }

    exec_command_list(compute_queue, compute_lists[0]);
    queue_signal(compute_queue, fence, 1);

    queue_wait(queue, fence, 1);
    exec_command_list(queue, graphics_lists[0]);
    queue_signal(queue, fence2, 1);

    queue_wait(compute_queue, fence2, 1);
    exec_command_list(compute_queue, compute_lists[1]);
    queue_signal(compute_queue, fence, 2);

    queue_wait(queue, fence, 2);
    exec_command_list(queue, graphics_lists[1]);

    hr = wait_for_fence(fence2, 1);
    ok(hr == S_OK, "Failed to wait for fence, hr %#x.\n", hr);
    reset_command_list(graphics_lists[0], allocators[0]);
    command_list = graphics_lists[0];

    transition_resource_state(command_list, buffer,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
    get_buffer_readback_with_command_list(buffer, DXGI_FORMAT_R32_UINT, &rb, queue, command_list);
    value = get_readback_uint(&rb, 0, 0, 0);
    ok(value == 24, "Got unexpected value %u.\n", value);
    release_resource_readback(&rb);

    ID3D12DescriptorHeap_Release(cpu_heap);
    ID3D12DescriptorHeap_Release(gpu_heap);
    for (i = 0; i < ARRAY_SIZE(graphics_lists); ++i)
        ID3D12GraphicsCommandList_Release(graphics_lists[i]);
    for (i = 0; i < ARRAY_SIZE(compute_lists); ++i)
        ID3D12GraphicsCommandList_Release(compute_lists[i]);
    for (i = 0; i < ARRAY_SIZE(allocators); ++i)
        ID3D12CommandAllocator_Release(allocators[i]);
    ID3D12Fence_Release(fence);
    ID3D12Fence_Release(fence2);
    ID3D12Resource_Release(buffer);
    ID3D12CommandQueue_Release(compute_queue);
    ID3D12PipelineState_Release(compute_pipeline_state);
    destroy_test_context(&context);
}

void test_fence_values(void)
{
    uint64_t value, next_value;
    ID3D12CommandQueue *queue;
    ID3D12Device *device;
    ID3D12Fence *fence;
    ULONG refcount;
    unsigned int i;
    HRESULT hr;

    if (!(device = create_device()))
    {
        skip("Failed to create device.\n");
        return;
    }

    queue = create_command_queue(device, D3D12_COMMAND_LIST_TYPE_DIRECT, D3D12_COMMAND_QUEUE_PRIORITY_NORMAL);

    next_value = (uint64_t)1 << 60;
    hr = ID3D12Device_CreateFence(device, next_value, D3D12_FENCE_FLAG_NONE, &IID_ID3D12Fence, (void **)&fence);
    ok(hr == S_OK, "Failed to create fence, hr %#x.\n", hr);

    value = ID3D12Fence_GetCompletedValue(fence);
    ok(value == next_value, "Got value %#"PRIx64", expected %#"PRIx64".\n", value, next_value);

    for (i = 0; i < 100; ++i)
    {
        ++next_value;
        queue_signal(queue, fence, next_value);
        /* Sprinke in some tests for no event path. */
        wait_queue_idle_no_event(device, queue);
        value = ID3D12Fence_GetCompletedValue(fence);
        ok(value == next_value, "Got value %#"PRIx64", expected %#"PRIx64".\n", value, next_value);
    }

    for (i = 0; i < 100; ++i)
    {
        next_value += 10000;
        hr = ID3D12Fence_Signal(fence, next_value);
        ok(hr == S_OK, "Failed to signal fence, hr %#x.\n", hr);
        value = ID3D12Fence_GetCompletedValue(fence);
        ok(value == next_value, "Got value %#"PRIx64", expected %#"PRIx64".\n", value, next_value);
    }

    ID3D12Fence_Release(fence);

    hr = ID3D12Device_CreateFence(device, 0, D3D12_FENCE_FLAG_NONE, &IID_ID3D12Fence, (void **)&fence);
    ok(hr == S_OK, "Failed to create fence, hr %#x.\n", hr);
    next_value = (uint64_t)1 << 60;
    hr = ID3D12Fence_Signal(fence, next_value);
    ok(hr == S_OK, "Failed to signal fence, hr %#x.\n", hr);
    value = ID3D12Fence_GetCompletedValue(fence);
    ok(value == next_value, "Got value %#"PRIx64", expected %#"PRIx64".\n", value, next_value);
    next_value = 0;
    hr = ID3D12Fence_Signal(fence, next_value);
    ok(hr == S_OK, "Failed to signal fence, hr %#x.\n", hr);
    value = ID3D12Fence_GetCompletedValue(fence);
    ok(value == next_value, "Got value %#"PRIx64", expected %#"PRIx64".\n", value, next_value);
    ID3D12Fence_Release(fence);

    hr = ID3D12Device_CreateFence(device, 0, D3D12_FENCE_FLAG_NONE, &IID_ID3D12Fence, (void **)&fence);
    ok(hr == S_OK, "Failed to create fence, hr %#x.\n", hr);
    next_value = (uint64_t)1 << 60;
    queue_signal(queue, fence, next_value);
    wait_queue_idle_no_event(device, queue);
    value = ID3D12Fence_GetCompletedValue(fence);
    ok(value == next_value, "Got value %#"PRIx64", expected %#"PRIx64".\n", value, next_value);
    next_value = 0;
    queue_signal(queue, fence, next_value);
    wait_queue_idle(device, queue);
    value = ID3D12Fence_GetCompletedValue(fence);
    ok(value == next_value, "Got value %#"PRIx64", expected %#"PRIx64".\n", value, next_value);
    ID3D12Fence_Release(fence);

    ID3D12CommandQueue_Release(queue);
    refcount = ID3D12Device_Release(device);
    ok(!refcount, "ID3D12Device has %u references left.\n", (unsigned int)refcount);
}

void test_cpu_signal_fence(void)
{
    HANDLE event1, event2;
    ID3D12Device *device;
    unsigned int i, ret;
    ID3D12Fence *fence;
    ULONG refcount;
    uint64_t value;
    HRESULT hr;

    if (!(device = create_device()))
    {
        skip("Failed to create device.\n");
        return;
    }

    hr = ID3D12Device_CreateFence(device, 0, D3D12_FENCE_FLAG_NONE,
            &IID_ID3D12Fence, (void **)&fence);
    ok(SUCCEEDED(hr), "Failed to create fence, hr %#x.\n", hr);

    hr = ID3D12Fence_Signal(fence, 1);
    ok(SUCCEEDED(hr), "Failed to signal fence, hr %#x.\n", hr);
    value = ID3D12Fence_GetCompletedValue(fence);
    ok(value == 1, "Got unexpected value %"PRIu64".\n", value);

    hr = ID3D12Fence_Signal(fence, 10);
    ok(SUCCEEDED(hr), "Failed to signal fence, hr %#x.\n", hr);
    value = ID3D12Fence_GetCompletedValue(fence);
    ok(value == 10, "Got unexpected value %"PRIu64".\n", value);

    hr = ID3D12Fence_Signal(fence, 5);
    ok(SUCCEEDED(hr), "Failed to signal fence, hr %#x.\n", hr);
    value = ID3D12Fence_GetCompletedValue(fence);
    ok(value == 5, "Got unexpected value %"PRIu64".\n", value);

    hr = ID3D12Fence_Signal(fence, 0);
    ok(SUCCEEDED(hr), "Failed to signal fence, hr %#x.\n", hr);
    value = ID3D12Fence_GetCompletedValue(fence);
    ok(value == 0, "Got unexpected value %"PRIu64".\n", value);

    /* Basic tests with single event. */
    event1 = create_event();
    ok(event1, "Failed to create event.\n");
    ret = wait_event(event1, 0);
    ok(ret == WAIT_TIMEOUT, "Got unexpected return value %#x.\n", ret);

    hr = ID3D12Fence_SetEventOnCompletion(fence, 5, event1);
    ok(SUCCEEDED(hr), "Failed to set event on completion, hr %#x.\n", hr);
    ret = wait_event(event1, 0);
    ok(ret == WAIT_TIMEOUT, "Got unexpected return value %#x.\n", ret);
    hr = ID3D12Fence_Signal(fence, 5);
    ok(SUCCEEDED(hr), "Failed to signal fence, hr %#x.\n", hr);
    ret = wait_event(event1, 0);
    ok(ret == WAIT_OBJECT_0, "Got unexpected return value %#x.\n", ret);
    ret = wait_event(event1, 0);
    ok(ret == WAIT_TIMEOUT, "Got unexpected return value %#x.\n", ret);

    hr = ID3D12Fence_SetEventOnCompletion(fence, 6, event1);
    ok(SUCCEEDED(hr), "Failed to set event on completion, hr %#x.\n", hr);
    ret = wait_event(event1, 0);
    ok(ret == WAIT_TIMEOUT, "Got unexpected return value %#x.\n", ret);
    hr = ID3D12Fence_Signal(fence, 7);
    ok(SUCCEEDED(hr), "Failed to signal fence, hr %#x.\n", hr);
    ret = wait_event(event1, 0);
    ok(ret == WAIT_OBJECT_0, "Got unexpected return value %#x.\n", ret);
    ret = wait_event(event1, 0);
    ok(ret == WAIT_TIMEOUT, "Got unexpected return value %#x.\n", ret);

    hr = ID3D12Fence_Signal(fence, 10);
    ok(SUCCEEDED(hr), "Failed to signal fence, hr %#x.\n", hr);
    ret = wait_event(event1, 0);
    ok(ret == WAIT_TIMEOUT, "Got unexpected return value %#x.\n", ret);

    /* Event is signaled immediately when value <= GetCompletedValue(). */
    ret = wait_event(event1, 0);
    ok(ret == WAIT_TIMEOUT, "Got unexpected return value %#x.\n", ret);
    for (i = 0; i <= ID3D12Fence_GetCompletedValue(fence); ++i)
    {
        hr = ID3D12Fence_SetEventOnCompletion(fence, i, event1);
        ok(SUCCEEDED(hr), "Failed to set event on completion, hr %#x.\n", hr);
        ret = wait_event(event1, 0);
        ok(ret == WAIT_OBJECT_0, "Got unexpected return value %#x for %u.\n", ret, i);
        ret = wait_event(event1, 0);
        ok(ret == WAIT_TIMEOUT, "Got unexpected return value %#x for %u.\n", ret, i);
    }
    hr = ID3D12Fence_SetEventOnCompletion(fence, i, event1);
    ok(SUCCEEDED(hr), "Failed to set event on completion, hr %#x.\n", hr);
    ret = wait_event(event1, 0);
    ok(ret == WAIT_TIMEOUT, "Got unexpected return value %#x.\n", ret);
    hr = ID3D12Fence_Signal(fence, i);
    ok(SUCCEEDED(hr), "Failed to signal fence, hr %#x.\n", hr);
    ret = wait_event(event1, 0);
    ok(ret == WAIT_OBJECT_0, "Got unexpected return value %#x.\n", ret);
    ret = wait_event(event1, 0);
    ok(ret == WAIT_TIMEOUT, "Got unexpected return value %#x.\n", ret);

    /* Attach event to multiple values. */
    hr = ID3D12Fence_Signal(fence, 0);
    ok(SUCCEEDED(hr), "Failed to signal fence, hr %#x.\n", hr);
    ret = wait_event(event1, 0);
    ok(ret == WAIT_TIMEOUT, "Got unexpected return value %#x.\n", ret);

    hr = ID3D12Fence_SetEventOnCompletion(fence, 3, event1);
    ok(SUCCEEDED(hr), "Failed to set event on completion, hr %#x.\n", hr);
    hr = ID3D12Fence_SetEventOnCompletion(fence, 5, event1);
    ok(SUCCEEDED(hr), "Failed to set event on completion, hr %#x.\n", hr);
    hr = ID3D12Fence_SetEventOnCompletion(fence, 9, event1);
    ok(SUCCEEDED(hr), "Failed to set event on completion, hr %#x.\n", hr);
    hr = ID3D12Fence_SetEventOnCompletion(fence, 12, event1);
    ok(SUCCEEDED(hr), "Failed to set event on completion, hr %#x.\n", hr);
    hr = ID3D12Fence_SetEventOnCompletion(fence, 12, event1);
    ok(SUCCEEDED(hr), "Failed to set event on completion, hr %#x.\n", hr);
    ret = wait_event(event1, 0);
    ok(ret == WAIT_TIMEOUT, "Got unexpected return value %#x.\n", ret);
    for (i = 1; i < 13; ++i)
    {
        hr = ID3D12Fence_Signal(fence, i);
        ok(SUCCEEDED(hr), "Failed to signal fence, hr %#x.\n", hr);
        if (i == 3 || i == 5 || i == 9 || i == 12)
        {
            ret = wait_event(event1, 0);
            ok(ret == WAIT_OBJECT_0, "Got unexpected return value %#x for %u.\n", ret, i);
        }
        ret = wait_event(event1, 0);
        ok(ret == WAIT_TIMEOUT, "Got unexpected return value %#x for %u.\n", ret, i);
    }

    /* Tests with 2 events. */
    hr = ID3D12Fence_Signal(fence, 0);
    ok(SUCCEEDED(hr), "Failed to signal fence, hr %#x.\n", hr);
    value = ID3D12Fence_GetCompletedValue(fence);
    ok(value == 0, "Got unexpected value %"PRIu64".\n", value);

    event2 = create_event();
    ok(event2, "Failed to create event.\n");

    ret = wait_event(event1, 0);
    ok(ret == WAIT_TIMEOUT, "Got unexpected return value %#x.\n", ret);
    ret = wait_event(event2, 0);
    ok(ret == WAIT_TIMEOUT, "Got unexpected return value %#x.\n", ret);
    hr = ID3D12Fence_SetEventOnCompletion(fence, 100, event1);
    ok(SUCCEEDED(hr), "Failed to set event on completion, hr %#x.\n", hr);
    hr = ID3D12Fence_SetEventOnCompletion(fence, ~(uint64_t)0, event2);
    ok(SUCCEEDED(hr), "Failed to set event on completion, hr %#x.\n", hr);

    hr = ID3D12Fence_Signal(fence, 50);
    ok(SUCCEEDED(hr), "Failed to signal fence, hr %#x.\n", hr);
    ret = wait_event(event1, 0);
    ok(ret == WAIT_TIMEOUT, "Got unexpected return value %#x.\n", ret);
    ret = wait_event(event2, 0);
    ok(ret == WAIT_TIMEOUT, "Got unexpected return value %#x.\n", ret);

    hr = ID3D12Fence_Signal(fence, 99);
    ok(SUCCEEDED(hr), "Failed to signal fence, hr %#x.\n", hr);
    ret = wait_event(event1, 0);
    ok(ret == WAIT_TIMEOUT, "Got unexpected return value %#x.\n", ret);
    ret = wait_event(event2, 0);
    ok(ret == WAIT_TIMEOUT, "Got unexpected return value %#x.\n", ret);

    hr = ID3D12Fence_Signal(fence, 100);
    ok(SUCCEEDED(hr), "Failed to signal fence, hr %#x.\n", hr);
    ret = wait_event(event1, 0);
    ok(ret == WAIT_OBJECT_0, "Got unexpected return value %#x.\n", ret);
    ret = wait_event(event1, 0);
    ok(ret == WAIT_TIMEOUT, "Got unexpected return value %#x.\n", ret);
    ret = wait_event(event2, 0);
    ok(ret == WAIT_TIMEOUT, "Got unexpected return value %#x.\n", ret);

    hr = ID3D12Fence_Signal(fence, 101);
    ok(SUCCEEDED(hr), "Failed to signal fence, hr %#x.\n", hr);
    ret = wait_event(event1, 0);
    ok(ret == WAIT_TIMEOUT, "Got unexpected return value %#x.\n", ret);
    ret = wait_event(event2, 0);
    ok(ret == WAIT_TIMEOUT, "Got unexpected return value %#x.\n", ret);

    hr = ID3D12Fence_Signal(fence, 0);
    ok(SUCCEEDED(hr), "Failed to signal fence, hr %#x.\n", hr);
    ret = wait_event(event1, 0);
    ok(ret == WAIT_TIMEOUT, "Got unexpected return value %#x.\n", ret);
    ret = wait_event(event2, 0);
    ok(ret == WAIT_TIMEOUT, "Got unexpected return value %#x.\n", ret);

    hr = ID3D12Fence_Signal(fence, 100);
    ok(SUCCEEDED(hr), "Failed to signal fence, hr %#x.\n", hr);
    ret = wait_event(event1, 0);
    ok(ret == WAIT_TIMEOUT, "Got unexpected return value %#x.\n", ret);
    ret = wait_event(event2, 0);
    ok(ret == WAIT_TIMEOUT, "Got unexpected return value %#x.\n", ret);

    hr = ID3D12Fence_Signal(fence, ~(uint64_t)0);
    ok(SUCCEEDED(hr), "Failed to signal fence, hr %#x.\n", hr);
    ret = wait_event(event1, 0);
    ok(ret == WAIT_TIMEOUT, "Got unexpected return value %#x.\n", ret);
    ret = wait_event(event2, 0);
    ok(ret == WAIT_OBJECT_0, "Got unexpected return value %#x.\n", ret);
    ret = wait_event(event2, 0);
    ok(ret == WAIT_TIMEOUT, "Got unexpected return value %#x.\n", ret);

    hr = ID3D12Fence_Signal(fence, ~(uint64_t)0);
    ok(SUCCEEDED(hr), "Failed to signal fence, hr %#x.\n", hr);
    ret = wait_event(event1, 0);
    ok(ret == WAIT_TIMEOUT, "Got unexpected return value %#x.\n", ret);
    ret = wait_event(event2, 0);
    ok(ret == WAIT_TIMEOUT, "Got unexpected return value %#x.\n", ret);
    hr = ID3D12Fence_Signal(fence, 0);
    ok(SUCCEEDED(hr), "Failed to signal fence, hr %#x.\n", hr);
    ret = wait_event(event1, 0);
    ok(ret == WAIT_TIMEOUT, "Got unexpected return value %#x.\n", ret);
    ret = wait_event(event2, 0);
    ok(ret == WAIT_TIMEOUT, "Got unexpected return value %#x.\n", ret);

    /* Attach two events to the same value. */
    hr = ID3D12Fence_Signal(fence, 0);
    ok(SUCCEEDED(hr), "Failed to signal fence, hr %#x.\n", hr);
    ret = wait_event(event1, 0);
    ok(ret == WAIT_TIMEOUT, "Got unexpected return value %#x.\n", ret);
    ret = wait_event(event2, 0);
    ok(ret == WAIT_TIMEOUT, "Got unexpected return value %#x.\n", ret);

    hr = ID3D12Fence_SetEventOnCompletion(fence, 1, event1);
    ok(SUCCEEDED(hr), "Failed to set event on completion, hr %#x.\n", hr);
    hr = ID3D12Fence_SetEventOnCompletion(fence, 1, event2);
    ok(SUCCEEDED(hr), "Failed to set event on completion, hr %#x.\n", hr);
    ret = wait_event(event1, 0);
    ok(ret == WAIT_TIMEOUT, "Got unexpected return value %#x.\n", ret);
    ret = wait_event(event2, 0);
    ok(ret == WAIT_TIMEOUT, "Got unexpected return value %#x.\n", ret);
    hr = ID3D12Fence_Signal(fence, 3);
    ok(SUCCEEDED(hr), "Failed to signal fence, hr %#x.\n", hr);
    ret = wait_event(event1, 0);
    ok(ret == WAIT_OBJECT_0, "Got unexpected return value %#x.\n", ret);
    ret = wait_event(event2, 0);
    ok(ret == WAIT_OBJECT_0, "Got unexpected return value %#x.\n", ret);
    ret = wait_event(event1, 0);
    ok(ret == WAIT_TIMEOUT, "Got unexpected return value %#x.\n", ret);
    ret = wait_event(event2, 0);
    ok(ret == WAIT_TIMEOUT, "Got unexpected return value %#x.\n", ret);

    /* Test passing signaled event. */
    hr = ID3D12Fence_Signal(fence, 20);
    ok(SUCCEEDED(hr), "Failed to signal fence, hr %#x.\n", hr);
    value = ID3D12Fence_GetCompletedValue(fence);
    ok(value == 20, "Got unexpected value %"PRIu64".\n", value);
    ret = wait_event(event1, 0);
    ok(ret == WAIT_TIMEOUT, "Got unexpected return value %#x.\n", ret);

    signal_event(event1);
    hr = ID3D12Fence_SetEventOnCompletion(fence, 30, event1);
    ok(SUCCEEDED(hr), "Failed to set event on completion, hr %#x.\n", hr);
    ret = wait_event(event1, 0);
    ok(ret == WAIT_OBJECT_0, "Got unexpected return value %#x.\n", ret);
    ret = wait_event(event1, 0);
    ok(ret == WAIT_TIMEOUT, "Got unexpected return value %#x.\n", ret);

    hr = ID3D12Fence_Signal(fence, 30);
    ok(SUCCEEDED(hr), "Failed to signal fence, hr %#x.\n", hr);
    ret = wait_event(event1, 0);
    ok(ret == WAIT_OBJECT_0, "Got unexpected return value %#x.\n", ret);
    ret = wait_event(event1, 0);
    ok(ret == WAIT_TIMEOUT, "Got unexpected return value %#x.\n", ret);

    destroy_event(event1);
    destroy_event(event2);

    ID3D12Fence_Release(fence);
    refcount = ID3D12Device_Release(device);
    ok(!refcount, "ID3D12Device has %u references left.\n", (unsigned int)refcount);
}

void test_gpu_signal_fence(void)
{
    ID3D12CommandQueue *queue;
    HANDLE event1, event2;
    ID3D12Device *device;
    unsigned int i, ret;
    ID3D12Fence *fence;
    ULONG refcount;
    uint64_t value;
    HRESULT hr;

    if (!(device = create_device()))
    {
        skip("Failed to create device.\n");
        return;
    }

    queue = create_command_queue(device, D3D12_COMMAND_LIST_TYPE_DIRECT, D3D12_COMMAND_QUEUE_PRIORITY_NORMAL);

    hr = ID3D12Device_CreateFence(device, 0, D3D12_FENCE_FLAG_NONE,
            &IID_ID3D12Fence, (void **)&fence);
    ok(hr == S_OK, "Failed to create fence, hr %#x.\n", hr);

    /* XXX: It seems that when a queue is idle a fence is signalled immediately
     * in D3D12. Vulkan implementations don't signal a fence immediately so
     * libvkd3d doesn't as well. In order to make this test reliable
     * wait_queue_idle() is inserted after every ID3D12CommandQueue_Signal(). */
    queue_signal(queue, fence, 10);
    wait_queue_idle(device, queue);
    value = ID3D12Fence_GetCompletedValue(fence);
    ok(value == 10, "Got unexpected value %"PRIu64".\n", value);

    queue_signal(queue, fence, 0);
    wait_queue_idle(device, queue);
    value = ID3D12Fence_GetCompletedValue(fence);
    ok(value == 0, "Got unexpected value %"PRIu64".\n", value);

    /* Basic tests with single event. */
    event1 = create_event();
    ok(event1, "Failed to create event.\n");
    ret = wait_event(event1, 0);
    ok(ret == WAIT_TIMEOUT, "Got unexpected return value %#x.\n", ret);

    hr = ID3D12Fence_SetEventOnCompletion(fence, 5, event1);
    ok(hr == S_OK, "Failed to set event on completion, hr %#x.\n", hr);
    ret = wait_event(event1, 0);
    ok(ret == WAIT_TIMEOUT, "Got unexpected return value %#x.\n", ret);
    queue_signal(queue, fence, 5);
    wait_queue_idle(device, queue);
    ret = wait_event(event1, 0);
    ok(ret == WAIT_OBJECT_0, "Got unexpected return value %#x.\n", ret);
    ret = wait_event(event1, 0);
    ok(ret == WAIT_TIMEOUT, "Got unexpected return value %#x.\n", ret);

    hr = ID3D12Fence_SetEventOnCompletion(fence, 6, event1);
    ok(hr == S_OK, "Failed to set event on completion, hr %#x.\n", hr);
    ret = wait_event(event1, 0);
    ok(ret == WAIT_TIMEOUT, "Got unexpected return value %#x.\n", ret);
    queue_signal(queue, fence, 7);
    wait_queue_idle(device, queue);
    ret = wait_event(event1, 0);
    ok(ret == WAIT_OBJECT_0, "Got unexpected return value %#x.\n", ret);
    ret = wait_event(event1, 0);
    ok(ret == WAIT_TIMEOUT, "Got unexpected return value %#x.\n", ret);

    queue_signal(queue, fence, 10);
    wait_queue_idle(device, queue);
    ret = wait_event(event1, 0);
    ok(ret == WAIT_TIMEOUT, "Got unexpected return value %#x.\n", ret);

    /* Attach one event to multiple values. */
    queue_signal(queue, fence, 0);
    wait_queue_idle(device, queue);
    ret = wait_event(event1, 0);
    ok(ret == WAIT_TIMEOUT, "Got unexpected return value %#x.\n", ret);

    hr = ID3D12Fence_SetEventOnCompletion(fence, 3, event1);
    ok(hr == S_OK, "Failed to set event on completion, hr %#x.\n", hr);
    hr = ID3D12Fence_SetEventOnCompletion(fence, 5, event1);
    ok(hr == S_OK, "Failed to set event on completion, hr %#x.\n", hr);
    hr = ID3D12Fence_SetEventOnCompletion(fence, 9, event1);
    ok(hr == S_OK, "Failed to set event on completion, hr %#x.\n", hr);
    hr = ID3D12Fence_SetEventOnCompletion(fence, 12, event1);
    ok(hr == S_OK, "Failed to set event on completion, hr %#x.\n", hr);
    hr = ID3D12Fence_SetEventOnCompletion(fence, 12, event1);
    ok(hr == S_OK, "Failed to set event on completion, hr %#x.\n", hr);
    ret = wait_event(event1, 0);
    ok(ret == WAIT_TIMEOUT, "Got unexpected return value %#x.\n", ret);
    for (i = 1; i < 13; ++i)
    {
        queue_signal(queue, fence, i);
        wait_queue_idle(device, queue);
        if (i == 3 || i == 5 || i == 9 || i == 12)
        {
            ret = wait_event(event1, 0);
            ok(ret == WAIT_OBJECT_0, "Got unexpected return value %#x for %u.\n", ret, i);
        }
        ret = wait_event(event1, 0);
        ok(ret == WAIT_TIMEOUT, "Got unexpected return value %#x for %u.\n", ret, i);
    }

    /* Tests with 2 events. */
    queue_signal(queue, fence, 0);
    wait_queue_idle(device, queue);
    value = ID3D12Fence_GetCompletedValue(fence);
    ok(value == 0, "Got unexpected value %"PRIu64".\n", value);

    event2 = create_event();
    ok(event2, "Failed to create event.\n");

    ret = wait_event(event1, 0);
    ok(ret == WAIT_TIMEOUT, "Got unexpected return value %#x.\n", ret);
    ret = wait_event(event2, 0);
    ok(ret == WAIT_TIMEOUT, "Got unexpected return value %#x.\n", ret);
    hr = ID3D12Fence_SetEventOnCompletion(fence, 100, event1);
    ok(hr == S_OK, "Failed to set event on completion, hr %#x.\n", hr);
    hr = ID3D12Fence_SetEventOnCompletion(fence, ~(uint64_t)0, event2);
    ok(hr == S_OK, "Failed to set event on completion, hr %#x.\n", hr);

    queue_signal(queue, fence, 50);
    wait_queue_idle(device, queue);
    ret = wait_event(event1, 0);
    ok(ret == WAIT_TIMEOUT, "Got unexpected return value %#x.\n", ret);
    ret = wait_event(event2, 0);
    ok(ret == WAIT_TIMEOUT, "Got unexpected return value %#x.\n", ret);

    queue_signal(queue, fence, 99);
    wait_queue_idle(device, queue);
    ret = wait_event(event1, 0);
    ok(ret == WAIT_TIMEOUT, "Got unexpected return value %#x.\n", ret);
    ret = wait_event(event2, 0);
    ok(ret == WAIT_TIMEOUT, "Got unexpected return value %#x.\n", ret);

    queue_signal(queue, fence, 100);
    wait_queue_idle(device, queue);
    ret = wait_event(event1, 0);
    ok(ret == WAIT_OBJECT_0, "Got unexpected return value %#x.\n", ret);
    ret = wait_event(event1, 0);
    ok(ret == WAIT_TIMEOUT, "Got unexpected return value %#x.\n", ret);
    ret = wait_event(event2, 0);
    ok(ret == WAIT_TIMEOUT, "Got unexpected return value %#x.\n", ret);

    queue_signal(queue, fence, 101);
    wait_queue_idle(device, queue);
    ret = wait_event(event1, 0);
    ok(ret == WAIT_TIMEOUT, "Got unexpected return value %#x.\n", ret);
    ret = wait_event(event2, 0);
    ok(ret == WAIT_TIMEOUT, "Got unexpected return value %#x.\n", ret);

    queue_signal(queue, fence, 0);
    wait_queue_idle(device, queue);
    ret = wait_event(event1, 0);
    ok(ret == WAIT_TIMEOUT, "Got unexpected return value %#x.\n", ret);
    ret = wait_event(event2, 0);
    ok(ret == WAIT_TIMEOUT, "Got unexpected return value %#x.\n", ret);

    queue_signal(queue, fence, 100);
    wait_queue_idle(device, queue);
    ret = wait_event(event1, 0);
    ok(ret == WAIT_TIMEOUT, "Got unexpected return value %#x.\n", ret);
    ret = wait_event(event2, 0);
    ok(ret == WAIT_TIMEOUT, "Got unexpected return value %#x.\n", ret);

    queue_signal(queue, fence, ~(uint64_t)0);
    wait_queue_idle(device, queue);
    ret = wait_event(event1, 0);
    ok(ret == WAIT_TIMEOUT, "Got unexpected return value %#x.\n", ret);
    ret = wait_event(event2, 0);
    ok(ret == WAIT_OBJECT_0, "Got unexpected return value %#x.\n", ret);
    ret = wait_event(event2, 0);
    ok(ret == WAIT_TIMEOUT, "Got unexpected return value %#x.\n", ret);

    queue_signal(queue, fence, ~(uint64_t)0);
    wait_queue_idle(device, queue);
    ret = wait_event(event1, 0);
    ok(ret == WAIT_TIMEOUT, "Got unexpected return value %#x.\n", ret);
    ret = wait_event(event2, 0);
    ok(ret == WAIT_TIMEOUT, "Got unexpected return value %#x.\n", ret);
    queue_signal(queue, fence, 0);
    wait_queue_idle(device, queue);
    ret = wait_event(event1, 0);
    ok(ret == WAIT_TIMEOUT, "Got unexpected return value %#x.\n", ret);
    ret = wait_event(event2, 0);
    ok(ret == WAIT_TIMEOUT, "Got unexpected return value %#x.\n", ret);

    /* Attach two events to the same value. */
    queue_signal(queue, fence, 0);
    wait_queue_idle(device, queue);
    ret = wait_event(event1, 0);
    ok(ret == WAIT_TIMEOUT, "Got unexpected return value %#x.\n", ret);
    ret = wait_event(event2, 0);
    ok(ret == WAIT_TIMEOUT, "Got unexpected return value %#x.\n", ret);

    hr = ID3D12Fence_SetEventOnCompletion(fence, 1, event1);
    ok(hr == S_OK, "Failed to set event on completion, hr %#x.\n", hr);
    hr = ID3D12Fence_SetEventOnCompletion(fence, 1, event2);
    ok(hr == S_OK, "Failed to set event on completion, hr %#x.\n", hr);
    ret = wait_event(event1, 0);
    ok(ret == WAIT_TIMEOUT, "Got unexpected return value %#x.\n", ret);
    ret = wait_event(event2, 0);
    ok(ret == WAIT_TIMEOUT, "Got unexpected return value %#x.\n", ret);
    queue_signal(queue, fence, 3);
    wait_queue_idle(device, queue);
    ret = wait_event(event1, 0);
    ok(ret == WAIT_OBJECT_0, "Got unexpected return value %#x.\n", ret);
    ret = wait_event(event2, 0);
    ok(ret == WAIT_OBJECT_0, "Got unexpected return value %#x.\n", ret);
    ret = wait_event(event1, 0);
    ok(ret == WAIT_TIMEOUT, "Got unexpected return value %#x.\n", ret);
    ret = wait_event(event2, 0);
    ok(ret == WAIT_TIMEOUT, "Got unexpected return value %#x.\n", ret);

    wait_queue_idle(device, queue);

    destroy_event(event1);
    destroy_event(event2);

    ID3D12Fence_Release(fence);
    ID3D12CommandQueue_Release(queue);
    refcount = ID3D12Device_Release(device);
    ok(!refcount, "ID3D12Device has %u references left.\n", (unsigned int)refcount);
}

struct multithread_fence_wait_data
{
    HANDLE event;
    ID3D12Fence *fence;
    uint64_t value;
};

static void fence_event_wait_main(void *untyped_data)
{
    struct multithread_fence_wait_data *data = untyped_data;
    unsigned int ret;
    HANDLE event;
    HRESULT hr;

    event = create_event();
    ok(event, "Failed to create event.\n");

    hr = ID3D12Fence_SetEventOnCompletion(data->fence, data->value, event);
    ok(SUCCEEDED(hr), "Failed to set event on completion, hr %#x.\n", hr);

    signal_event(data->event);

    ret = wait_event(event, INFINITE);
    ok(ret == WAIT_OBJECT_0, "Got unexpected return value %#x.\n", ret);

    destroy_event(event);
}

static void fence_busy_wait_main(void *untyped_data)
{
    struct multithread_fence_wait_data *data = untyped_data;

    signal_event(data->event);

    while (ID3D12Fence_GetCompletedValue(data->fence) < data->value)
        ;
}

void test_multithread_fence_wait(void)
{
    struct multithread_fence_wait_data thread_data;
    ID3D12CommandQueue *queue;
    ID3D12Device *device;
    unsigned int ret;
    ULONG refcount;
    HANDLE thread;
    HRESULT hr;

    if (!(device = create_device()))
    {
        skip("Failed to create device.\n");
        return;
    }

    queue = create_command_queue(device, D3D12_COMMAND_LIST_TYPE_DIRECT, D3D12_COMMAND_QUEUE_PRIORITY_NORMAL);

    thread_data.event = create_event();
    thread_data.value = 0;
    ok(thread_data.event, "Failed to create event.\n");
    hr = ID3D12Device_CreateFence(device, thread_data.value, D3D12_FENCE_FLAG_NONE,
            &IID_ID3D12Fence, (void **)&thread_data.fence);
    ok(hr == S_OK, "Failed to create fence, hr %#x.\n", hr);

    /* Signal fence on host. */
    ++thread_data.value;
    thread = create_thread(fence_event_wait_main, &thread_data);
    ok(thread, "Failed to create thread.\n");
    ret = wait_event(thread_data.event, INFINITE);
    ok(ret == WAIT_OBJECT_0, "Failed to wait for thread start, return value %#x.\n", ret);

    hr = ID3D12Fence_Signal(thread_data.fence, thread_data.value);
    ok(hr == S_OK, "Failed to signal fence, hr %#x.\n", hr);

    ok(join_thread(thread), "Failed to join thread.\n");

    ++thread_data.value;
    thread = create_thread(fence_busy_wait_main, &thread_data);
    ok(thread, "Failed to create thread.\n");
    ret = wait_event(thread_data.event, INFINITE);
    ok(ret == WAIT_OBJECT_0, "Failed to wait for thread start, return value %#x.\n", ret);

    hr = ID3D12Fence_Signal(thread_data.fence, thread_data.value);
    ok(hr == S_OK, "Failed to signal fence, hr %#x.\n", hr);

    ok(join_thread(thread), "Failed to join thread.\n");

    /* Signal fence on device. */
    ++thread_data.value;
    thread = create_thread(fence_event_wait_main, &thread_data);
    ok(thread, "Failed to create thread.\n");
    ret = wait_event(thread_data.event, INFINITE);
    ok(ret == WAIT_OBJECT_0, "Failed to wait for thread start, return value %#x.\n", ret);

    queue_signal(queue, thread_data.fence, thread_data.value);

    ok(join_thread(thread), "Failed to join thread.\n");

    ++thread_data.value;
    thread = create_thread(fence_busy_wait_main, &thread_data);
    ok(thread, "Failed to create thread.\n");
    ret = wait_event(thread_data.event, INFINITE);
    ok(ret == WAIT_OBJECT_0, "Failed to wait for thread start, return value %#x.\n", ret);

    queue_signal(queue, thread_data.fence, thread_data.value);

    ok(join_thread(thread), "Failed to join thread.\n");

    destroy_event(thread_data.event);
    ID3D12Fence_Release(thread_data.fence);
    ID3D12CommandQueue_Release(queue);
    refcount = ID3D12Device_Release(device);
    ok(!refcount, "ID3D12Device has %u references left.\n", (unsigned int)refcount);
}

void test_create_fence(void)
{
    ID3D12Device *device, *tmp_device;
    ID3D12Fence *fence;
    ULONG refcount;
    uint64_t value;
    HRESULT hr;

    if (!(device = create_device()))
    {
        skip("Failed to create device.\n");
        return;
    }

    hr = ID3D12Device_CreateFence(device, 0, D3D12_FENCE_FLAG_NONE,
            &IID_ID3D12Fence, (void **)&fence);
    ok(SUCCEEDED(hr), "Failed to create fence, hr %#x.\n", hr);

    refcount = get_refcount(device);
    ok(refcount == 2, "Got unexpected refcount %u.\n", (unsigned int)refcount);
    hr = ID3D12Fence_GetDevice(fence, &IID_ID3D12Device, (void **)&tmp_device);
    ok(SUCCEEDED(hr), "Failed to get device, hr %#x.\n", hr);
    refcount = get_refcount(device);
    ok(refcount == 3, "Got unexpected refcount %u.\n", (unsigned int)refcount);
    refcount = ID3D12Device_Release(tmp_device);
    ok(refcount == 2, "Got unexpected refcount %u.\n", (unsigned int)refcount);

    check_interface(fence, &IID_ID3D12Object, true);
    check_interface(fence, &IID_ID3D12DeviceChild, true);
    check_interface(fence, &IID_ID3D12Pageable, true);
    check_interface(fence, &IID_ID3D12Fence, true);

    value = ID3D12Fence_GetCompletedValue(fence);
    ok(value == 0, "Got unexpected value %"PRIu64".\n", value);

    refcount = ID3D12Fence_Release(fence);
    ok(!refcount, "ID3D12Fence has %u references left.\n", (unsigned int)refcount);

    hr = ID3D12Device_CreateFence(device, 99, D3D12_FENCE_FLAG_NONE,
            &IID_ID3D12Fence, (void **)&fence);
    ok(SUCCEEDED(hr), "Failed to create fence, hr %#x.\n", hr);
    value = ID3D12Fence_GetCompletedValue(fence);
    ok(value == 99, "Got unexpected value %"PRIu64".\n", value);
    refcount = ID3D12Fence_Release(fence);
    ok(!refcount, "ID3D12Fence has %u references left.\n", (unsigned int)refcount);

    refcount = ID3D12Device_Release(device);
    ok(!refcount, "ID3D12Device has %u references left.\n", (unsigned int)refcount);
}

void test_fence_wait_robustness_inner(bool shared_handles)
{
    VKD3D_UNUSED HANDLE shared_signal = NULL;
    VKD3D_UNUSED HANDLE shared_drain = NULL;
    VKD3D_UNUSED HANDLE shared_wait = NULL;
    ID3D12CommandAllocator *allocator[2];
    ID3D12Fence *signal_fence_dup = NULL;
    D3D12_COMMAND_QUEUE_DESC queue_desc;
    ID3D12Fence *drain_fence_dup = NULL;
    ID3D12Fence *wait_fence_dup = NULL;
    ID3D12GraphicsCommandList *list[2];
    ID3D12CommandQueue *compute_queue;
    struct test_context context;
    ID3D12Fence *signal_fence;
    ID3D12Fence *drain_fence;
    ID3D12Fence *wait_fence;
    ID3D12Resource *src;
    ID3D12Resource *dst;
    unsigned int i;
    HANDLE event;
    UINT value;
    HRESULT hr;

    if (!init_compute_test_context(&context))
        return;

    hr = ID3D12Device_CreateFence(context.device, 0,
            shared_handles ? D3D12_FENCE_FLAG_SHARED : D3D12_FENCE_FLAG_NONE,
            &IID_ID3D12Fence, (void**)&signal_fence);
    todo_if(shared_handles) ok(SUCCEEDED(hr), "Failed to create fence, hr #%x.\n", hr);

    if (FAILED(hr))
    {
        skip("Failed to create fence, skipping test ...\n");
        destroy_test_context(&context);
        return;
    }

    hr = ID3D12Device_CreateFence(context.device, 0,
            shared_handles ? D3D12_FENCE_FLAG_SHARED : D3D12_FENCE_FLAG_NONE,
            &IID_ID3D12Fence, (void**)&wait_fence);
    ok(SUCCEEDED(hr), "Failed to create fence, hr #%x.\n", hr);

    if (FAILED(hr))
    {
        skip("Failed to create fence, skipping test ...\n");
        ID3D12Fence_Release(signal_fence);
        destroy_test_context(&context);
        return;
    }

    hr = ID3D12Device_CreateFence(context.device, 0,
            shared_handles ? D3D12_FENCE_FLAG_SHARED : D3D12_FENCE_FLAG_NONE,
            &IID_ID3D12Fence, (void**)&drain_fence);
    ok(SUCCEEDED(hr), "Failed to create fence, hr #%x.\n", hr);

    if (FAILED(hr))
    {
        skip("Failed to create fence, skipping test ...\n");
        ID3D12Fence_Release(signal_fence);
        ID3D12Fence_Release(wait_fence);
        destroy_test_context(&context);
        return;
    }

#ifdef _WIN32
    if (shared_handles)
    {
        hr = ID3D12Device_CreateSharedHandle(context.device, (ID3D12DeviceChild*)signal_fence,
                NULL, GENERIC_ALL, NULL, &shared_signal);
        ok(SUCCEEDED(hr), "Failed to create shared handle, hr #%x.\n", hr);
        hr = ID3D12Device_CreateSharedHandle(context.device, (ID3D12DeviceChild*)wait_fence,
                NULL, GENERIC_ALL, NULL, &shared_wait);
        ok(SUCCEEDED(hr), "Failed to create shared handle, hr #%x.\n", hr);
        hr = ID3D12Device_CreateSharedHandle(context.device, (ID3D12DeviceChild*)drain_fence,
                NULL, GENERIC_ALL, NULL, &shared_drain);
        ok(SUCCEEDED(hr), "Failed to create shared handle, hr #%x.\n", hr);

        ID3D12Fence_Release(signal_fence);
        ID3D12Fence_Release(wait_fence);
        ID3D12Fence_Release(drain_fence);

        hr = ID3D12Device_OpenSharedHandle(context.device, shared_signal, &IID_ID3D12Fence, (void**)&signal_fence);
        ok(SUCCEEDED(hr), "Failed to open shared handle, hr #%x.\n", hr);
        hr = ID3D12Device_OpenSharedHandle(context.device, shared_wait, &IID_ID3D12Fence, (void**)&wait_fence);
        ok(SUCCEEDED(hr), "Failed to open shared handle, hr #%x.\n", hr);
        hr = ID3D12Device_OpenSharedHandle(context.device, shared_drain, &IID_ID3D12Fence, (void**)&drain_fence);
        ok(SUCCEEDED(hr), "Failed to open shared handle, hr #%x.\n", hr);

        /* OpenSharedHandle takes a kernel level reference on the HANDLE. */
        hr = ID3D12Device_OpenSharedHandle(context.device, shared_signal, &IID_ID3D12Fence, (void**)&signal_fence_dup);
        ok(SUCCEEDED(hr), "Failed to open shared handle, hr #%x.\n", hr);
        hr = ID3D12Device_OpenSharedHandle(context.device, shared_wait, &IID_ID3D12Fence, (void**)&wait_fence_dup);
        ok(SUCCEEDED(hr), "Failed to open shared handle, hr #%x.\n", hr);
        hr = ID3D12Device_OpenSharedHandle(context.device, shared_drain, &IID_ID3D12Fence, (void**)&drain_fence_dup);
        ok(SUCCEEDED(hr), "Failed to open shared handle, hr #%x.\n", hr);

        /* Observed behavior: Closing the last reference to the kernel HANDLE object unblocks all waiters.
         * This isn't really implementable in Wine as it stands since applications are free to share
         * the HANDLE and Dupe it arbitrarily.
         * For now, assume this is not a thing, we can report TDR-like situations if this comes up in practice. */
        if (shared_signal)
            CloseHandle(shared_signal);
        if (shared_wait)
            CloseHandle(shared_wait);
        if (shared_drain)
            CloseHandle(shared_drain);
    }
#endif

    memset(&queue_desc, 0, sizeof(queue_desc));
    queue_desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    queue_desc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
    queue_desc.Type = D3D12_COMMAND_LIST_TYPE_COMPUTE;

    src = create_default_buffer(context.device, 256 * 1024 * 1024, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_SOURCE);
    dst = create_default_buffer(context.device, 256 * 1024 * 1024, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);

    ID3D12Device_CreateCommandQueue(context.device, &queue_desc, &IID_ID3D12CommandQueue, (void**)&compute_queue);

    for (i = 0; i < 2; i++)
    {
        ID3D12Device_CreateCommandAllocator(context.device, D3D12_COMMAND_LIST_TYPE_COMPUTE,
                &IID_ID3D12CommandAllocator, (void**)&allocator[i]);
        ID3D12Device_CreateCommandList(context.device, 0, D3D12_COMMAND_LIST_TYPE_COMPUTE, allocator[i], NULL,
                &IID_ID3D12GraphicsCommandList, (void**)&list[i]);
    }

    /* Heavy copy action. */
    for (i = 0; i < 128; i++)
    {
        ID3D12GraphicsCommandList_CopyResource(list[0], dst, src);
        ID3D12GraphicsCommandList_CopyResource(list[1], src, dst);
    }

    ID3D12GraphicsCommandList_Close(list[0]);
    ID3D12GraphicsCommandList_Close(list[1]);

    /* Note on ref-count checks: The debug layers can take transient public ref-counts it seems. */

    ID3D12CommandQueue_ExecuteCommandLists(context.queue, 1, (ID3D12CommandList * const *)&list[0]);
    ID3D12CommandQueue_Signal(context.queue, signal_fence, 1);
    /* Validate that signal/wait does not take public ref-counts. */
    value = get_refcount(signal_fence);
    ok(value == 1, "Unexpected ref-count %u\n", value);

    /* The GPU copy is 32 GB worth of BW. There is literally zero chance it would have completed in this amount of time. */
    value = (UINT)ID3D12Fence_GetCompletedValue(signal_fence);
    ok(value == 0, "Unexpected signal event %u.\n", value);

    /* Try waiting for a signal that never comes. We'll be able to unblock this wait
     * when we fully release the fence. */
    ID3D12CommandQueue_Wait(compute_queue, signal_fence, UINT64_MAX);
    value = get_refcount(signal_fence);
    ok(value == 1, "Unexpected ref-count %u\n", value);

    ID3D12CommandQueue_Signal(compute_queue, wait_fence, 1);
    value = get_refcount(wait_fence);
    ok(value == 1, "Unexpected ref-count %u\n", value);

    /* The GPU copy is 32 GB worth of BW. There is literally zero chance it would have completed in this amount of time. */
    value = (UINT)ID3D12Fence_GetCompletedValue(wait_fence);
    ok(value == 0, "Unexpected signal event %u.\n", value);
    value = (UINT)ID3D12Fence_GetCompletedValue(signal_fence);
    ok(value == 0, "Unexpected signal event %u.\n", value);

    ID3D12CommandQueue_Wait(compute_queue, wait_fence, 1);
    value = get_refcount(wait_fence);
    ok(value == 1, "Unexpected ref-count %u\n", value);

    /* Check that we can queue up event completion.
     * Again, verify that releasing the fence unblocks all waiters ... */
    event = create_event();
    ID3D12Fence_SetEventOnCompletion(signal_fence, UINT64_MAX, event);

    if (signal_fence_dup)
        ID3D12Fence_Release(signal_fence_dup);
    if (wait_fence_dup)
        ID3D12Fence_Release(wait_fence_dup);

    /* The GPU copy is 32 GB worth of BW. There is literally zero chance it would have completed in this amount of time.
     * Makes sure that the fences aren't signalled when we try to free them.
     * (Sure, there is a theoretical race condition if GPU completes between this check and the release, but seriously ...). */
    value = (UINT)ID3D12Fence_GetCompletedValue(signal_fence);
    ok(value == 0, "Unexpected signal event %u.\n", value);
    value = (UINT)ID3D12Fence_GetCompletedValue(wait_fence);
    ok(value == 0, "Unexpected signal event %u.\n", value);

    /* Test that it's valid to release fence while it's in flight.
     * If we don't cause device lost and drain_fence is waited on successfully we pass the test. */
    value = ID3D12Fence_Release(signal_fence);
    ok(value == 0, "Unexpected fence ref-count %u.\n", value);
    value = ID3D12Fence_Release(wait_fence);
    ok(value == 0, "Unexpected fence ref-count %u.\n", value);

    ID3D12CommandQueue_ExecuteCommandLists(compute_queue, 1, (ID3D12CommandList * const *)&list[1]);
    ID3D12CommandQueue_Signal(compute_queue, drain_fence, 1);

    wait_event(event, INFINITE);
    destroy_event(event);
    ID3D12Fence_SetEventOnCompletion(drain_fence, 1, NULL);
    value = (UINT)ID3D12Fence_GetCompletedValue(drain_fence);
    ok(value == 1, "Expected fence wait value 1, but got %u.\n", value);

    if (drain_fence_dup)
    {
        /* Check we observe the counter in sibling fences as well. */
        value = (UINT)ID3D12Fence_GetCompletedValue(drain_fence_dup);
        ok(value == 1, "Expected fence wait value 1, but got %u.\n", value);
        ID3D12Fence_Release(drain_fence_dup);
    }

    value = ID3D12Fence_Release(drain_fence);
    ok(value == 0, "Unexpected fence ref-count %u.\n", value);

    /* Early freeing of fences might signal the drain fence too early, causing GPU hang. */
    wait_queue_idle(context.device, context.queue);
    wait_queue_idle(context.device, compute_queue);

    ID3D12CommandQueue_Release(compute_queue);
    for (i = 0; i < 2; i++)
    {
        ID3D12CommandAllocator_Release(allocator[i]);
        ID3D12GraphicsCommandList_Release(list[i]);
    }
    ID3D12Resource_Release(dst);
    ID3D12Resource_Release(src);

    destroy_test_context(&context);
}

void test_fence_wait_robustness(void)
{
    test_fence_wait_robustness_inner(false);
}

void test_fence_wait_robustness_shared(void)
{
#ifdef _WIN32
    test_fence_wait_robustness_inner(true);
#else
    skip("Shared fences not supported on native Linux build.\n");
#endif
}

void test_fence_wait_multiple_inner(bool shared_handles)
{
    ID3D12CommandQueue *queue1, *queue2;
    struct test_context context;
    ID3D12Device1 *device1;
    ID3D12Fence *fences[4];
    UINT64 values[4];
    unsigned int i;
    HANDLE event;
    HRESULT hr;
    DWORD ret;

    if (!init_compute_test_context(&context))
        return;

    if (ID3D12Device_QueryInterface(context.device, &IID_ID3D12Device1, (void**)&device1))
    {
        skip("Failed to query ID3D12Device1 interface, skipping test ...\n");
        destroy_test_context(&context);
        return;
    }

    memset(fences, 0, sizeof(fences));

    for (i = 0; i < ARRAY_SIZE(fences); i++)
    {
        hr = ID3D12Device1_CreateFence(device1, 0,
                shared_handles ? D3D12_FENCE_FLAG_SHARED : D3D12_FENCE_FLAG_NONE,
                &IID_ID3D12Fence, (void**)&fences[i]);
        todo_if(shared_handles) ok(SUCCEEDED(hr), "Failed to create fence, hr #%x.\n", hr);

        if (FAILED(hr))
            break;
    }

    if (FAILED(hr))
    {
        skip("Failed to create fence, skipping test ...\n");

        for (i = 0; i < ARRAY_SIZE(fences); i++)
        {
            if (fences[i])
                ID3D12Fence_Release(fences[i]);
        }

        ID3D12Device1_Release(device1);
        destroy_test_context(&context);
        return;
    }

    queue1 = create_command_queue(context.device, D3D12_COMMAND_LIST_TYPE_DIRECT, D3D12_COMMAND_QUEUE_PRIORITY_NORMAL);
    queue2 = create_command_queue(context.device, D3D12_COMMAND_LIST_TYPE_COMPUTE, D3D12_COMMAND_QUEUE_PRIORITY_NORMAL);

    event = create_event();
    ret = wait_event(event, 0);
    ok(ret == WAIT_TIMEOUT, "Got unexpected return value %u.\n", ret);

    /* Test various invalid parameter combinations that should not crash */
    hr = ID3D12Device1_SetEventOnMultipleFenceCompletion(device1,
            NULL, NULL, 0, D3D12_MULTIPLE_FENCE_WAIT_FLAG_ALL, NULL);
    ok(hr == E_INVALIDARG, "Got unexpected result, hr %#x.\n", hr);
    hr = ID3D12Device1_SetEventOnMultipleFenceCompletion(device1,
            fences, values, 0, D3D12_MULTIPLE_FENCE_WAIT_FLAG_ANY, event);
    ok(hr == E_INVALIDARG, "Got unexpected result, hr %#x.\n", hr);
    hr = ID3D12Device1_SetEventOnMultipleFenceCompletion(device1,
            fences, values, 4, 0xFFFFFFFF, event);
    ok(hr == E_INVALIDARG, "Got unexpected result, hr %#x.\n", hr);

    /* The D3D12 runtime will crash here
    hr = ID3D12Device1_SetEventOnMultipleFenceCompletion(device1,
            fences, NULL, 1, D3D12_MULTIPLE_FENCE_WAIT_FLAG_ALL, event);
    ok(hr == E_INVALIDARG, "Got unexpected result, hr %#x.\n", hr);
    */

    /* Test simple single-wait scenario */
    values[0] = 1;

    hr = ID3D12Device1_SetEventOnMultipleFenceCompletion(device1,
            fences, values, 1, D3D12_MULTIPLE_FENCE_WAIT_FLAG_ALL, event);
    ok(hr == S_OK, "Failed to set event on multiple fence completion, hr %#x.\n", hr);
    ret = wait_event(event, 0);
    ok(ret == WAIT_TIMEOUT, "Got unexpected return value %u.\n", ret);

    ID3D12Fence_Signal(fences[0], 1);
    ret = wait_event(event, INFINITE);
    ok(ret == WAIT_OBJECT_0, "Got unexpected return value %u.\n", ret);
    ret = wait_event(event, 0);
    ok(ret == WAIT_TIMEOUT, "Got unexpected return value %u.\n", ret);

    /* Test complex multi-wait scenario with WAIT_ALL */
    values[0] = 2;
    values[1] = 1;
    values[2] = 53;
    values[3] = 11;

    hr = ID3D12Device1_SetEventOnMultipleFenceCompletion(device1,
            fences, values, 4, D3D12_MULTIPLE_FENCE_WAIT_FLAG_ALL, event);
    ok(hr == S_OK, "Failed to set event on multiple fence completion, hr %#x.\n", hr);
    ret = wait_event(event, 0);
    ok(ret == WAIT_TIMEOUT, "Got unexpected return value %u.\n", ret);

    ID3D12Fence_Signal(fences[1], 4);

    ret = wait_event(event, 100);
    ok(ret == WAIT_TIMEOUT, "Got unexpected return value %u.\n", ret);

    queue_signal(queue1, fences[0], 2);
    queue_signal(queue2, fences[3], 14);
    ID3D12Fence_Signal(fences[2], 53);

    ret = wait_event(event, INFINITE);
    ok(ret == WAIT_OBJECT_0, "Got unexpected return value %u.\n", ret);
    ret = wait_event(event, 0);
    ok(ret == WAIT_TIMEOUT, "Got unexpected return value %u.\n", ret);

    hr = ID3D12Device1_SetEventOnMultipleFenceCompletion(device1,
            fences, values, 4, D3D12_MULTIPLE_FENCE_WAIT_FLAG_ALL, NULL);
    ok(hr == S_OK, "Failed to set event on multiple fence completion, hr %#x.\n", hr);

    /* Test complex multi-wait with WAIT_ANY */
    values[0] = 100;
    values[1] = 200;
    values[2] = 300;
    values[3] = 400;

    hr = ID3D12Device1_SetEventOnMultipleFenceCompletion(device1,
            fences, values, 4, D3D12_MULTIPLE_FENCE_WAIT_FLAG_ANY, event);
    ok(hr == S_OK, "Failed to set event on multiple fence completion, hr %#x.\n", hr);
    ret = wait_event(event, 0);
    ok(ret == WAIT_TIMEOUT, "Got unexpected return value %u.\n", ret);

    queue_signal(queue1, fences[1], 200);
    queue_signal(queue2, fences[2], 300);
    ret = wait_event(event, INFINITE);
    ok(ret == WAIT_OBJECT_0, "Got unexpected return value %u.\n", ret);

    ID3D12Fence_Signal(fences[0], 100);

    ret = wait_event(event, 100);
    ok(ret == WAIT_TIMEOUT, "Got unexpected return value %u.\n", ret);

    hr = ID3D12Device1_SetEventOnMultipleFenceCompletion(device1,
            fences, values, 4, D3D12_MULTIPLE_FENCE_WAIT_FLAG_ANY, NULL);
    ok(hr == S_OK, "Failed to set event on multiple fence completion, hr %#x.\n", hr);

    hr = ID3D12Device1_SetEventOnMultipleFenceCompletion(device1,
            fences, values, 4, D3D12_MULTIPLE_FENCE_WAIT_FLAG_ANY, event);
    ok(hr == S_OK, "Failed to set event on multiple fence completion, hr %#x.\n", hr);
    ret = wait_event(event, INFINITE);
    ok(ret == WAIT_OBJECT_0, "Got unexpected return value %u.\n", ret);

    for (i = 0; i < ARRAY_SIZE(fences); i++)
        ID3D12Fence_Release(fences[i]);

    destroy_event(event);

    ID3D12CommandQueue_Release(queue1);
    ID3D12CommandQueue_Release(queue2);
    ID3D12Device1_Release(device1);
    destroy_test_context(&context);
}

void test_fence_wait_multiple(void)
{
    test_fence_wait_multiple_inner(false);
}

void test_fence_wait_multiple_shared(void)
{
#ifdef _WIN32
    test_fence_wait_multiple_inner(true);
#else
    skip("Shared fences not supported on native Linux build.\n");
#endif
}

static void test_concurrent_signal_stress_inner(void)
{
    ID3D12CommandQueue *queue1, *queue2;
    struct test_context context;
    ID3D12Fence *fences[4];
    unsigned int iteration;
    UINT64 fence_values[4];
    ID3D12Device1 *device1;
    bool wait_success;
    unsigned int i;
    HANDLE event;
    HRESULT hr;

    event = create_event();

    if (!init_compute_test_context(&context))
        return;

    ID3D12Device_QueryInterface(context.device, &IID_ID3D12Device1, (void**)&device1);
    queue1 = create_command_queue(context.device, D3D12_COMMAND_LIST_TYPE_DIRECT, D3D12_COMMAND_QUEUE_PRIORITY_NORMAL);
    queue2 = create_command_queue(context.device, D3D12_COMMAND_LIST_TYPE_COMPUTE, D3D12_COMMAND_QUEUE_PRIORITY_NORMAL);

    for (i = 0; i < ARRAY_SIZE(fences); i++)
    {
        hr = ID3D12Device_CreateFence(context.device, 0, D3D12_FENCE_FLAG_NONE, &IID_ID3D12Fence, (void**)&fences[i]);
        ok(SUCCEEDED(hr), "Failed to create fence, hr #%x.\n", hr);
        fence_values[i] = 0;
    }

    for (iteration = 0; iteration < 10; iteration++)
    {
        for (i = 0; i < 2; i++)
        {
            fence_values[0] += 3;
            queue_signal(queue1, fences[0], fence_values[0]);
            fence_values[0] += 4;
            queue_signal(queue1, fences[0], fence_values[0]);
            fence_values[1] += 5;
            queue_signal(queue2, fences[1], fence_values[1]);
            fence_values[2] += 6;
            queue_signal(queue1, fences[2], fence_values[2]);
            fence_values[3] += 7;
            queue_signal(queue2, fences[3], fence_values[3]);
        }

        hr = ID3D12Device1_SetEventOnMultipleFenceCompletion(device1,
                fences, fence_values, 4, D3D12_MULTIPLE_FENCE_WAIT_FLAG_ALL, event);
        ok(SUCCEEDED(hr), "Failed to set event hr #%x.\n", hr);

        wait_success = wait_event(event, 1000) == WAIT_OBJECT_0;

        ok(wait_success, "Failed to wait for event. GPU likely hung.\n");
        if (!wait_success)
            goto err;
    }

    for (i = 0; i < ARRAY_SIZE(fences); i++)
        wait_for_fence(fences[i], fence_values[i]);

err:
    ID3D12CommandQueue_Release(queue1);
    ID3D12CommandQueue_Release(queue2);
    for (i = 0; i < ARRAY_SIZE(fences); i++)
        ID3D12Fence_Release(fences[i]);
    ID3D12Device1_Release(device1);
    destroy_event(event);
    destroy_test_context(&context);
}

void test_concurrent_signal_stress(void)
{
    unsigned int i;
    for (i = 0; i < 100; i++)
    {
        vkd3d_test_set_context("Test %u", i);
        test_concurrent_signal_stress_inner();
    }
    vkd3d_test_set_context(NULL);
}

static void test_fence_pending_signal_cpu_rewind(bool use_shared)
{
    D3D12_UNORDERED_ACCESS_VIEW_DESC uav_desc;
    ID3D12Resource *dummy_resources[256];
    D3D12_DESCRIPTOR_RANGE desc_range[2];
    D3D12_COMMAND_QUEUE_DESC queue_desc;
    D3D12_ROOT_SIGNATURE_DESC rs_desc;
    D3D12_ROOT_PARAMETER rs_param;
    struct test_context context;
    ID3D12DescriptorHeap *heap;
    ID3D12CommandQueue *queue;
    ID3D12Fence *fence_alt;
    ID3D12Fence *fence;
    unsigned int i, j;
    unsigned int iter;
    HANDLE event;
    UINT64 value;
    HRESULT hr;

#include "shaders/sync/headers/gpu_load.h"

#ifndef _WIN32
    if (use_shared)
    {
        skip("Skipping shared fence tests on Linux.\n");
        return;
    }
#endif

    if (!init_compute_test_context(&context))
        return;

    ID3D12Device_CreateFence(context.device, 8, use_shared ? D3D12_FENCE_FLAG_SHARED : D3D12_FENCE_FLAG_NONE,
            &IID_ID3D12Fence, (void **)&fence);
    ID3D12Device_CreateFence(context.device, 0, use_shared ? D3D12_FENCE_FLAG_SHARED : D3D12_FENCE_FLAG_NONE,
            &IID_ID3D12Fence, (void **)&fence_alt);
    ID3D12GraphicsCommandList_Close(context.list);

    memset(&queue_desc, 0, sizeof(queue_desc));
    queue_desc.Type = D3D12_COMMAND_LIST_TYPE_COMPUTE;
    hr = ID3D12Device_CreateCommandQueue(context.device, &queue_desc, &IID_ID3D12CommandQueue, (void **)&queue);
    ok(SUCCEEDED(hr), "Failed to create command queue, hr #%x\n", hr);

    memset(&rs_desc, 0, sizeof(rs_desc));
    memset(&rs_param, 0, sizeof(rs_param));
    memset(&desc_range, 0, sizeof(desc_range));
    rs_desc.NumParameters = 1;
    rs_desc.pParameters = &rs_param;

    rs_param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rs_param.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    rs_param.DescriptorTable.pDescriptorRanges = desc_range;
    rs_param.DescriptorTable.NumDescriptorRanges = ARRAY_SIZE(desc_range);

    desc_range[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    desc_range[0].NumDescriptors = 1000000;
    desc_range[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    desc_range[1].NumDescriptors = 1000000;

    create_root_signature(context.device, &rs_desc, &context.root_signature);
    context.pipeline_state = create_compute_pipeline_state(
        context.device, context.root_signature, gpu_load_dxbc);

    heap = create_gpu_descriptor_heap(context.device,
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 256 * ARRAY_SIZE(dummy_resources));

    for (i = 0; i < ARRAY_SIZE(dummy_resources); i++)
    {
        dummy_resources[i] = create_default_buffer(context.device, 2 * 1024 * 1024,
            D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        memset(&uav_desc, 0, sizeof(uav_desc));

        for (j = 0; j < 256; j++)
        {
            uav_desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
            uav_desc.Buffer.StructureByteStride = 4;
            uav_desc.Buffer.FirstElement = 64 * j;
            uav_desc.Buffer.NumElements = 64;
            ID3D12Device_CreateUnorderedAccessView(context.device, dummy_resources[i], NULL,
                &uav_desc, get_cpu_descriptor_handle(&context, heap, 256 * i + j));
        }
    }

    for (iter = 0; iter < 64; iter++)
    {
        ID3D12Fence_Signal(fence, 8);
        ID3D12Fence_Signal(fence_alt, 0);
        ID3D12CommandAllocator_Reset(context.allocator);

        for (i = 0; i < 2; i++)
        {
            ID3D12GraphicsCommandList_Reset(context.list, context.allocator, NULL);
            ID3D12GraphicsCommandList_SetDescriptorHeaps(context.list, 1, &heap);
            ID3D12GraphicsCommandList_SetComputeRootSignature(context.list, context.root_signature);
            ID3D12GraphicsCommandList_SetPipelineState(context.list, context.pipeline_state);
            ID3D12GraphicsCommandList_SetComputeRootDescriptorTable(
                    context.list, 0, ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(heap));
            /* Load up the GPU ... */

            if (i)
            {
                ID3D12GraphicsCommandList_Dispatch(context.list,
                                                   16 * 1024, 256 * ARRAY_SIZE(dummy_resources) / (16 * 1024),
                                                   use_warp_device ? 1 : 4);
            }
            else
            {
                /* On first iteration before we signal 9, just load up some trivial work,
                 * but enough that driver cannot signal fence on CPU due to inactive queue. */
                ID3D12GraphicsCommandList_Dispatch(context.list, 1, 1, 1);
            }
            ID3D12GraphicsCommandList_Close(context.list);
            exec_command_list(context.queue, context.list);
            ID3D12CommandQueue_Signal(context.queue, fence, 9 + i);
        }

        /* Sleep until we've seen the GPU get active. */
        ID3D12Fence_SetEventOnCompletion(fence, 9, NULL);

        value = ID3D12Fence_GetCompletedValue(fence);
        if (value == 10)
        {
            skip("GPU is too fast? Fence is already signalled to 10.\n");
        }
        else
        {
            unsigned int wait_result;

            /* GPU is busy now. Reset the fence to 0 while there's a pending signal to 10.
             * Technically this is a bit racy. */
            value = ID3D12Fence_GetCompletedValue(fence);
            ok(value == 9, "Expected completed fence value of 9.\n");
            hr = ID3D12Fence_Signal(fence, 0);
            ok(SUCCEEDED(hr), "Failed to signal fence, hr #%x\n", hr);
            value = ID3D12Fence_GetCompletedValue(fence);
            ok(value == 0, "Expected completed fence value of 0.\n");

            /* Make sure that GPU isn't using an older pending submit to unblock the second queue. */
            ID3D12CommandQueue_Wait(queue, fence, 9);
            ID3D12CommandQueue_Signal(queue, fence_alt, 1);

            event = create_event();
            hr = ID3D12Fence_SetEventOnCompletion(fence_alt, 1, event);
            ok(SUCCEEDED(hr), "Failed to queue event signal, hr #%x.\n", hr);

            /* Add a timeout just in case to avoid a deadlock. */
            wait_result = wait_event(event, 2000);
            ok(wait_result == WAIT_OBJECT_0, "Expected WAIT_OBJECT_0, got %u.\n", wait_result);

            value = ID3D12Fence_GetCompletedValue(fence);
            ok(value == 10, "Expected first queue to be fully complete before fence_alt get signaled.\n");
            destroy_event(event);
        }
    }

    for (i = 0; i < ARRAY_SIZE(dummy_resources); i++)
        ID3D12Resource_Release(dummy_resources[i]);

    /* Destroying fences unblock any waiters, so this should unstuck anything. */
    ID3D12Fence_Release(fence);
    ID3D12Fence_Release(fence_alt);
    ID3D12CommandQueue_Release(queue);
    ID3D12DescriptorHeap_Release(heap);
    destroy_test_context(&context);
}

static void test_fence_ping_pong_deadlock_stress(bool use_shared)
{
    D3D12_UNORDERED_ACCESS_VIEW_DESC uav_desc;
    ID3D12Resource *early_resources[256];
    D3D12_DESCRIPTOR_RANGE desc_range[2];
    D3D12_COMMAND_QUEUE_DESC queue_desc;
    D3D12_ROOT_SIGNATURE_DESC rs_desc;
    D3D12_ROOT_PARAMETER rs_param;
    struct test_context context;
    ID3D12DescriptorHeap *heap;
    ID3D12CommandQueue *queue;
    uint64_t last_observed_ts;
    ID3D12Fence *fence_alt;
    uint64_t last_observed;
    ID3D12Fence *fence;
    unsigned int i, j;
    HRESULT hr;

#include "shaders/sync/headers/gpu_load.h"

#ifndef _WIN32
    if (use_shared)
    {
        skip("Skipping shared fence tests on Linux.\n");
        return;
    }
#endif

    if (!init_compute_test_context(&context))
        return;

    ID3D12Device_CreateFence(context.device, 0, use_shared ? D3D12_FENCE_FLAG_SHARED : D3D12_FENCE_FLAG_NONE,
            &IID_ID3D12Fence, (void **)&fence);
    ID3D12Device_CreateFence(context.device, 0, use_shared ? D3D12_FENCE_FLAG_SHARED : D3D12_FENCE_FLAG_NONE,
            &IID_ID3D12Fence, (void **)&fence_alt);
    ID3D12GraphicsCommandList_Close(context.list);

    memset(&queue_desc, 0, sizeof(queue_desc));
    queue_desc.Type = D3D12_COMMAND_LIST_TYPE_COMPUTE;
    hr = ID3D12Device_CreateCommandQueue(context.device, &queue_desc, &IID_ID3D12CommandQueue, (void **)&queue);
    ok(SUCCEEDED(hr), "Failed to create command queue, hr #%x\n", hr);

    memset(&rs_desc, 0, sizeof(rs_desc));
    memset(&rs_param, 0, sizeof(rs_param));
    memset(&desc_range, 0, sizeof(desc_range));
    rs_desc.NumParameters = 1;
    rs_desc.pParameters = &rs_param;

    rs_param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rs_param.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    rs_param.DescriptorTable.pDescriptorRanges = desc_range;
    rs_param.DescriptorTable.NumDescriptorRanges = ARRAY_SIZE(desc_range);

    desc_range[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    desc_range[0].NumDescriptors = 1000000;
    desc_range[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    desc_range[1].NumDescriptors = 1000000;

    create_root_signature(context.device, &rs_desc, &context.root_signature);
    context.pipeline_state = create_compute_pipeline_state(
        context.device, context.root_signature, gpu_load_dxbc);

    heap = create_gpu_descriptor_heap(context.device,
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 256 * ARRAY_SIZE(early_resources));

    for (i = 0; i < ARRAY_SIZE(early_resources); i++)
    {
        early_resources[i] = create_default_buffer(context.device, 2 * 1024 * 1024,
            D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        memset(&uav_desc, 0, sizeof(uav_desc));

        for (j = 0; j < 256; j++)
        {
            uav_desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
            uav_desc.Buffer.StructureByteStride = 4;
            uav_desc.Buffer.FirstElement = 64 * j;
            uav_desc.Buffer.NumElements = 64;
            ID3D12Device_CreateUnorderedAccessView(context.device, early_resources[i], NULL,
                &uav_desc, get_cpu_descriptor_handle(&context, heap, 256 * i + j));
        }
    }

    for (i = 0; i < 4096; i++)
    {
        ID3D12GraphicsCommandList_Reset(context.list, context.allocator, NULL);
        ID3D12GraphicsCommandList_SetDescriptorHeaps(context.list, 1, &heap);
        ID3D12GraphicsCommandList_SetComputeRootSignature(context.list, context.root_signature);
        ID3D12GraphicsCommandList_SetPipelineState(context.list, context.pipeline_state);
        ID3D12GraphicsCommandList_SetComputeRootDescriptorTable(context.list, 0,
            ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(heap));
        /* Load up the GPU ... */

        /* Load up some non-trivial work. */
        ID3D12GraphicsCommandList_Dispatch(context.list, 100, 1, 1);

        ID3D12GraphicsCommandList_Close(context.list);

        /* Long wait-before-idle chain. */
        if (i < 2048)
        {
            exec_command_list(context.queue, context.list);
            ID3D12CommandQueue_Wait(context.queue, fence, 2 * (i % 2048) + 1);
            ID3D12CommandQueue_Signal(context.queue, fence, 2 * (i % 2048) + 2);
            /* This signal is racy, but that's okay. */
            ID3D12CommandQueue_Signal(context.queue, fence_alt, 2 * (i % 2048) + 2);
        }
        else
        {
            ID3D12CommandQueue_Wait(queue, fence, 2 * (i % 2048));
            /* This signal is racy, but that's okay.
             * In the last iteration, we ensure that signal to 4095 is complete before we signal fence_alt to 4096. */
            ID3D12CommandQueue_Signal(queue, fence_alt, 2 * (i % 2048) + 1);
            ID3D12CommandQueue_Signal(queue, fence, 2 * (i % 2048) + 1);
        }
    }

    /* While GPU is busy, try really hard to cause a deadlock. */

    last_observed = ID3D12Fence_GetCompletedValue(fence_alt);
    last_observed_ts = vkd3d_get_current_time_ns();

    while (last_observed < 4096)
    {
        uint64_t next_observed = ID3D12Fence_GetCompletedValue(fence_alt);
        uint64_t ts = vkd3d_get_current_time_ns();
        if (last_observed != next_observed)
            last_observed_ts = ts;
        last_observed = next_observed;

        if (ts - last_observed_ts > 1000000000)
        {
            /* The threads have deadlocked. No progress observed for a second. */
            ok(false, "Threads deadlocked at fence value of %"PRIu64".\n", next_observed);

            /* Unstuck the threads and force them through. */
            last_observed_ts = ts;
            while (ID3D12Fence_GetCompletedValue(fence_alt) < 4096)
            {
                ts = vkd3d_get_current_time_ns();
                ID3D12Fence_Signal(fence, UINT64_MAX);
                if (ts - last_observed_ts > 1000000000)
                {
                    ok(false, "Have been trying to unstuck threads for a second, but it's not working ...\n");
                    break;
                }
            }

            ok(ID3D12Fence_GetCompletedValue(fence_alt) == 4096, "Failed to unstuck the threads.\n");
            break;
        }

        /* FIXME: This breaks Proton Experimental in shared fence path.
         * I believe there is a race condition where we don't reach the
         * vkWaitSemaphore path in time, meaning a value has pulsed up and down
         * before we have the chance to wait for it, but there is some evidence to suggest
         * that there is a bug in winevulkan as well. */
        hr = ID3D12Fence_Signal(fence, 0);
        ok(SUCCEEDED(hr), "Failed to rewind fence, hr #%x.\n", hr);
    }

    for (i = 0; i < ARRAY_SIZE(early_resources); i++)
        ID3D12Resource_Release(early_resources[i]);

    /* Destroying fences unblock any waiters, so this should unstuck anything. */
    ID3D12Fence_Release(fence);
    ID3D12Fence_Release(fence_alt);
    ID3D12CommandQueue_Release(queue);
    ID3D12DescriptorHeap_Release(heap);
    destroy_test_context(&context);
}

static void test_fence_signal_order_deadlock_stress(bool use_shared)
{
    D3D12_COMMAND_QUEUE_DESC queue_desc;
    struct test_context context;
    ID3D12CommandQueue *queue_a;
    ID3D12CommandQueue *queue_b;
    ID3D12Fence *fence_alt;
    ID3D12Fence *fence;
    unsigned int iter;
    HRESULT hr;

#ifndef _WIN32
    if (use_shared)
    {
        skip("Skipping shared fence tests on Linux.\n");
        return;
    }
#endif

    if (!init_compute_test_context(&context))
        return;

    memset(&queue_desc, 0, sizeof(queue_desc));
    queue_desc.Type = D3D12_COMMAND_LIST_TYPE_COMPUTE;
    hr = ID3D12Device_CreateCommandQueue(context.device, &queue_desc, &IID_ID3D12CommandQueue, (void **)&queue_a);
    ok(SUCCEEDED(hr), "Failed to create command queue, hr #%x\n", hr);
    hr = ID3D12Device_CreateCommandQueue(context.device, &queue_desc, &IID_ID3D12CommandQueue, (void **)&queue_b);
    ok(SUCCEEDED(hr), "Failed to create command queue, hr #%x\n", hr);

    ID3D12Device_CreateFence(context.device, 0, use_shared ? D3D12_FENCE_FLAG_SHARED : D3D12_FENCE_FLAG_NONE,
            &IID_ID3D12Fence, (void **)&fence);
    ID3D12Device_CreateFence(context.device, 0, use_shared ? D3D12_FENCE_FLAG_SHARED : D3D12_FENCE_FLAG_NONE,
            &IID_ID3D12Fence, (void **)&fence_alt);

	/* For hardcore stress test, just run this test in a loop. */
    for (iter = 0; iter < 16 * 1024; iter++)
    {
        ID3D12Fence_Signal(fence, 0);

        /* Queue 1, set up wait-before-signal chain. */
        ID3D12CommandQueue_Wait(context.queue, fence, 1);
        ID3D12CommandQueue_Signal(context.queue, fence, 2);
        ID3D12CommandQueue_Wait(context.queue, fence, 3);
        ID3D12CommandQueue_Signal(context.queue, fence, 4);
        ID3D12CommandQueue_Wait(context.queue, fence, 5);
        ID3D12CommandQueue_Signal(context.queue, fence, 6);
        ID3D12CommandQueue_Wait(context.queue, fence, 7);
        ID3D12CommandQueue_Signal(context.queue, fence, 8);
        ID3D12CommandQueue_Wait(context.queue, fence, 9);
        ID3D12CommandQueue_Signal(context.queue, fence, 10);

        /* Set up duelling ping-pong between Queue 2 and 3. */
        ID3D12CommandQueue_Signal(queue_a, fence_alt, 1);
        ID3D12CommandQueue_Wait(queue_a, fence_alt, 2);

        ID3D12CommandQueue_Wait(queue_b, fence_alt, 1);
        ID3D12CommandQueue_Signal(queue_b, fence_alt, 2);

        /* Unblocks Queue 2, then starts ping-pong. */
        ID3D12CommandQueue_Signal(queue_a, fence, 1);
        ID3D12CommandQueue_Wait(queue_a, fence, 2);
        ID3D12CommandQueue_Signal(queue_a, fence, 3);
        ID3D12CommandQueue_Wait(queue_a, fence, 4);
        ID3D12CommandQueue_Signal(queue_a, fence, 5);
        ID3D12CommandQueue_Wait(queue_a, fence, 6);
        ID3D12CommandQueue_Signal(queue_a, fence, 7);
        ID3D12CommandQueue_Wait(queue_a, fence, 8);
        ID3D12CommandQueue_Signal(queue_a, fence, 9);
        ID3D12CommandQueue_Wait(queue_a, fence, 10);

        ID3D12CommandQueue_Signal(queue_a, fence_alt, 0);

        ID3D12Fence_SetEventOnCompletion(fence, 10, NULL);

        if (iter % 1024 == 0)
            skip("Done with iteration %u\n", iter);
    }

    ID3D12CommandQueue_Release(queue_a);
    ID3D12CommandQueue_Release(queue_b);
    ID3D12Fence_Release(fence);
    ID3D12Fence_Release(fence_alt);
    destroy_test_context(&context);
}

static void test_fence_signal_availability(bool use_shared)
{
    D3D12_COMMAND_QUEUE_DESC queue_desc;
    struct test_context context;
    ID3D12Fence *fence_progress;
    ID3D12CommandQueue *queue;
    unsigned int wait_result;
    ID3D12Fence *fence_alt;
    ID3D12Fence *fence;
    unsigned int iter;
    HANDLE event;
    HRESULT hr;

#ifndef _WIN32
    if (use_shared)
    {
        skip("Skipping shared fence tests on Linux.\n");
        return;
    }
#endif

    if (!init_compute_test_context(&context))
        return;

    memset(&queue_desc, 0, sizeof(queue_desc));
    queue_desc.Type = D3D12_COMMAND_LIST_TYPE_COMPUTE;
    hr = ID3D12Device_CreateCommandQueue(context.device, &queue_desc, &IID_ID3D12CommandQueue, (void **)&queue);
    ok(SUCCEEDED(hr), "Failed to create command queue, hr #%x\n", hr);

    for (iter = 0; iter < 2; iter++)
    {
        ID3D12Device_CreateFence(context.device, 0, use_shared ? D3D12_FENCE_FLAG_SHARED : D3D12_FENCE_FLAG_NONE,
                &IID_ID3D12Fence, (void **)&fence);
        ID3D12Device_CreateFence(context.device, 0, use_shared ? D3D12_FENCE_FLAG_SHARED : D3D12_FENCE_FLAG_NONE,
                &IID_ID3D12Fence, (void **)&fence_alt);
        ID3D12Device_CreateFence(context.device, 0, use_shared ? D3D12_FENCE_FLAG_SHARED : D3D12_FENCE_FLAG_NONE,
                &IID_ID3D12Fence, (void **)&fence_progress);

        ID3D12CommandQueue_Wait(context.queue, fence, 1);
        ID3D12CommandQueue_Wait(context.queue, fence, 2);
        ID3D12CommandQueue_Wait(context.queue, fence, 3);
        ID3D12CommandQueue_Wait(context.queue, fence, 4);
        ID3D12CommandQueue_Signal(context.queue, fence_progress, 1);
        ID3D12CommandQueue_Wait(context.queue, fence_alt, 1);
        ID3D12CommandQueue_Wait(context.queue, fence, 5);
        ID3D12CommandQueue_Wait(context.queue, fence, 6);
        ID3D12CommandQueue_Wait(context.queue, fence, 7);
        ID3D12CommandQueue_Wait(context.queue, fence, 8);
        ID3D12CommandQueue_Signal(context.queue, fence_progress, 2);

        /* Question now is which jobs do we actually unblock?
         * One interpretation is queued jobs enter a ticketing system where they are unblocked as signals come on.
         * Another interpretation is that only when jobs become submission candidates do they check if they have been satisfied.
         * I.e., waits enter one by one as they can be processed and only then being available for being signaled.
         * Test both GPU signal and CPU signal paths to see if there is a difference. */
        if (iter == 0)
            ID3D12CommandQueue_Signal(queue, fence, 8);
        else
            ID3D12Fence_Signal(fence, 8);

        /* This is guaranteed. The queue will be blocked on fence_alt. */
        ID3D12Fence_SetEventOnCompletion(fence_progress, 1, NULL);
        ID3D12Fence_Signal(fence, 0);

        /* Verify that the GPU queue has stalled. */
        {
            event = create_event();
            ID3D12Fence_SetEventOnCompletion(fence_progress, 2, event);
            wait_result = wait_event(event, 100);
            ok(wait_result == WAIT_TIMEOUT, "Expected %u, got %u\n", WAIT_TIMEOUT, wait_result);
        }

        ID3D12Fence_Signal(fence_alt, 1);

        /* Question now is the queue is supposed to remember that we signalled 8.
         * It seems like that is the case. */
        {
            event = create_event();
            ID3D12Fence_SetEventOnCompletion(fence_progress, 2, event);
            wait_result = wait_event(event, 100);

            /* In shared path we have no way to implement the ticketing system,
             * so this is unimplementable as-is. */
            todo_if(use_shared) ok(wait_result == WAIT_OBJECT_0, "Expected %u, got %u.\n", WAIT_OBJECT_0, wait_result);

            ID3D12Fence_Signal(fence, 8);
            ID3D12Fence_SetEventOnCompletion(fence_progress, 2, NULL);
            destroy_event(event);
        }

        ID3D12Fence_Release(fence);
        ID3D12Fence_Release(fence_alt);
        ID3D12Fence_Release(fence_progress);
    }

    ID3D12CommandQueue_Release(queue);
    destroy_test_context(&context);
}

void test_fence_pending_signal_cpu_rewind_plain(void)
{
    test_fence_pending_signal_cpu_rewind(false);
}

void test_fence_ping_pong_deadlock_stress_plain(void)
{
    test_fence_ping_pong_deadlock_stress(false);
}

void test_fence_signal_order_deadlock_stress_plain(void)
{
    test_fence_signal_order_deadlock_stress(false);
}

void test_fence_signal_availability_plain(void)
{
    test_fence_signal_availability(false);
}

void test_fence_pending_signal_cpu_rewind_shared(void)
{
    test_fence_pending_signal_cpu_rewind(true);
}

void test_fence_ping_pong_deadlock_stress_shared(void)
{
    test_fence_ping_pong_deadlock_stress(true);
}

void test_fence_signal_order_deadlock_stress_shared(void)
{
    test_fence_signal_order_deadlock_stress(true);
}

void test_fence_signal_availability_shared(void)
{
    test_fence_signal_availability(true);
}
