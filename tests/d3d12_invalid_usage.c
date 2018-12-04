/*
 * Copyright 2016-2018 Józef Kucia for CodeWeavers
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

#include "d3d12_crosstest.h"

static void test_invalid_texture_resource_barriers(void)
{
    ID3D12Resource *texture, *readback_buffer, *upload_buffer;
    D3D12_COMMAND_QUEUE_DESC command_queue_desc;
    ID3D12CommandAllocator *command_allocator;
    ID3D12GraphicsCommandList *command_list;
    ID3D12CommandQueue *queue;
    ID3D12Device *device;
    ULONG refcount;
    HRESULT hr;

    if (!(device = create_device()))
    {
        skip("Failed to create device.\n");
        return;
    }

    command_queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    command_queue_desc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
    command_queue_desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    command_queue_desc.NodeMask = 0;
    hr = ID3D12Device_CreateCommandQueue(device, &command_queue_desc,
            &IID_ID3D12CommandQueue, (void **)&queue);
    ok(SUCCEEDED(hr), "Failed to create command queue, hr %#x.\n", hr);

    hr = ID3D12Device_CreateCommandAllocator(device, D3D12_COMMAND_LIST_TYPE_DIRECT,
            &IID_ID3D12CommandAllocator, (void **)&command_allocator);
    ok(SUCCEEDED(hr), "Failed to create command allocator, hr %#x.\n", hr);

    hr = ID3D12Device_CreateCommandList(device, 0, D3D12_COMMAND_LIST_TYPE_DIRECT,
            command_allocator, NULL, &IID_ID3D12GraphicsCommandList, (void **)&command_list);
    ok(SUCCEEDED(hr), "Failed to create command list, hr %#x.\n", hr);

    texture = create_default_texture(device, 32, 32, DXGI_FORMAT_R8G8B8A8_UNORM,
            D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    upload_buffer = create_upload_buffer(device, 32, NULL);
    readback_buffer = create_readback_buffer(device, 32);

    /* The following invalid barrier is not detected by the runtime. */
    transition_resource_state(command_list, texture,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
    hr = ID3D12GraphicsCommandList_Close(command_list);
    ok(SUCCEEDED(hr), "Failed to close command list, hr %#x.\n", hr);

    reset_command_list(command_list, command_allocator);

    /* The before state does not match with the previous state. */
    transition_resource_state(command_list, texture,
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_SOURCE);
    transition_resource_state(command_list, texture,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    hr = ID3D12GraphicsCommandList_Close(command_list);
    /* The returned error code has changed after a Windows update. */
    ok(hr == S_OK || hr == E_INVALIDARG, "Got unexpected hr %#x.\n", hr);
    if (hr == S_OK)
    {
        exec_command_list(queue, command_list);
        wait_queue_idle(device, queue);
    }

    ID3D12GraphicsCommandList_Release(command_list);
    hr = ID3D12CommandAllocator_Reset(command_allocator);
    ok(SUCCEEDED(hr), "Failed to reset command allocator, hr %#x.\n", hr);
    hr = ID3D12Device_CreateCommandList(device, 0, D3D12_COMMAND_LIST_TYPE_DIRECT,
            command_allocator, NULL, &IID_ID3D12GraphicsCommandList, (void **)&command_list);
    ok(SUCCEEDED(hr), "Failed to create command list, hr %#x.\n", hr);

    /* The before state does not match with the previous state. */
    transition_resource_state(command_list, texture,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    transition_resource_state(command_list, texture,
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    hr = ID3D12GraphicsCommandList_Close(command_list);
    /* The returned error code has changed after a Windows update. */
    ok(hr == S_OK || hr == E_INVALIDARG, "Got unexpected hr %#x.\n", hr);
    if (hr == S_OK)
    {
        exec_command_list(queue, command_list);
        wait_queue_idle(device, queue);
    }

    hr = ID3D12CommandAllocator_Reset(command_allocator);
    ok(SUCCEEDED(hr), "Failed to reset command allocator, hr %#x.\n", hr);
    ID3D12GraphicsCommandList_Release(command_list);
    hr = ID3D12Device_CreateCommandList(device, 0, D3D12_COMMAND_LIST_TYPE_DIRECT,
            command_allocator, NULL, &IID_ID3D12GraphicsCommandList, (void **)&command_list);
    ok(SUCCEEDED(hr), "Failed to create command list, hr %#x.\n", hr);

    /* Exactly one write state or a combination of read-only states are allowed. */
    transition_resource_state(command_list, texture,
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    hr = ID3D12GraphicsCommandList_Close(command_list);
    ok(hr == E_INVALIDARG, "Got unexpected hr %#x.\n", hr);

    hr = ID3D12CommandAllocator_Reset(command_allocator);
    ok(SUCCEEDED(hr), "Failed to reset command allocator, hr %#x.\n", hr);
    ID3D12GraphicsCommandList_Release(command_list);
    hr = ID3D12Device_CreateCommandList(device, 0, D3D12_COMMAND_LIST_TYPE_DIRECT,
            command_allocator, NULL, &IID_ID3D12GraphicsCommandList, (void **)&command_list);
    ok(SUCCEEDED(hr), "Failed to create command list, hr %#x.\n", hr);

    /* Readback resources cannot transition from D3D12_RESOURCE_STATE_COPY_DEST. */
    transition_resource_state(command_list, readback_buffer,
            D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON);
    hr = ID3D12GraphicsCommandList_Close(command_list);
    todo ok(hr == E_INVALIDARG, "Got unexpected hr %#x.\n", hr);

    hr = ID3D12CommandAllocator_Reset(command_allocator);
    ok(SUCCEEDED(hr), "Failed to reset command allocator, hr %#x.\n", hr);
    ID3D12GraphicsCommandList_Release(command_list);
    hr = ID3D12Device_CreateCommandList(device, 0, D3D12_COMMAND_LIST_TYPE_DIRECT,
            command_allocator, NULL, &IID_ID3D12GraphicsCommandList, (void **)&command_list);
    ok(SUCCEEDED(hr), "Failed to create command list, hr %#x.\n", hr);

    /* Upload resources cannot transition from D3D12_RESOURCE_STATE_GENERIC_READ. */
    transition_resource_state(command_list, upload_buffer,
            D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_STATE_COMMON);
    hr = ID3D12GraphicsCommandList_Close(command_list);
    todo ok(hr == E_INVALIDARG, "Got unexpected hr %#x.\n", hr);

    ID3D12CommandAllocator_Release(command_allocator);
    ID3D12CommandQueue_Release(queue);
    ID3D12GraphicsCommandList_Release(command_list);
    ID3D12Resource_Release(readback_buffer);
    ID3D12Resource_Release(texture);
    ID3D12Resource_Release(upload_buffer);
    refcount = ID3D12Device_Release(device);
    ok(!refcount, "ID3D12Device has %u references left.\n", (unsigned int)refcount);
}

START_TEST(d3d12_invalid_usage)
{
    bool enable_debug_layer = false;
    ID3D12Debug *debug;
    unsigned int i;

    for (i = 1; i < argc; ++i)
    {
        if (!strcmp(argv[i], "--validate"))
            enable_debug_layer = true;
        else if (!strcmp(argv[i], "--warp"))
            use_warp_device = true;
        else if (!strcmp(argv[i], "--adapter") && i + 1 < argc)
            use_adapter_idx = atoi(argv[++i]);
    }

    if (enable_debug_layer && SUCCEEDED(D3D12GetDebugInterface(&IID_ID3D12Debug, (void **)&debug)))
    {
        ID3D12Debug_EnableDebugLayer(debug);
        ID3D12Debug_Release(debug);
    }

    print_adapter_info();

    run_test(test_invalid_texture_resource_barriers);
}
