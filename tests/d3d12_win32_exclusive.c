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

#ifndef _WIN32
#include <sys/mman.h>
#endif

void test_clock_calibration(void)
{
#ifndef _WIN32
    skip("Clock calibration tests cannot pass on native Linux. Skipping.\n");
#else
    uint64_t cpu_times[2], gpu_times[2];
    struct test_context context;
    HRESULT hr;

    if (!init_test_context(&context, NULL))
        return;

    hr = ID3D12CommandQueue_GetClockCalibration(context.queue, &gpu_times[0], &cpu_times[0]);
    ok(hr == S_OK, "Failed retrieve calibrated timestamps, hr %#x.\n", (int)hr);

    vkd3d_sleep(100);

    hr = ID3D12CommandQueue_GetClockCalibration(context.queue, &gpu_times[1], &cpu_times[1]);
    ok(hr == S_OK, "Failed retrieve calibrated timestamps, hr %#x.\n", (int)hr);

    ok(gpu_times[1] > gpu_times[0], "Inconsistent GPU timestamps.\n");
    ok(cpu_times[1] > cpu_times[0], "Inconsistent CPU timestamps.\n");

    destroy_test_context(&context);
#endif
}

void test_open_heap_from_address(void)
{
    ID3D12Resource *readback_resource;
    struct test_context context;
    struct resource_readback rb;
    D3D12_HEAP_DESC heap_desc;
    ID3D12Resource *resource;
    ID3D12Device13 *device13;
    unsigned int heap_size;
    ID3D12Device3 *device3;
    ID3D12Device *device;
#ifdef _WIN32
    HANDLE file_handle;
#endif
    ID3D12Heap *heap;
    unsigned int i;
    uint32_t *addr;
    void *map_ptr;
    HRESULT hr;
    bool freed;

    if (!init_test_context(&context, NULL))
        return;

    device = context.device;
    hr = ID3D12Device_QueryInterface(device, &IID_ID3D12Device3, (void **)&device3);
    ok(hr == S_OK, "Failed to query ID3D12Device3, hr #%x.\n", (int)hr);

    if (FAILED(ID3D12Device_QueryInterface(device, &IID_ID3D12Device13, (void **)&device13)))
        device13 = NULL;

    /* Simple case, import directly from VirtualAlloc. */
    {
        heap_size = 64 * 1024;
#ifdef _WIN32
        addr = VirtualAlloc(NULL, heap_size, MEM_COMMIT, PAGE_READWRITE);
        ok(!!addr, "Failed to VirtualAllocate.\n");
#else
        addr = mmap(NULL, heap_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        ok(addr != MAP_FAILED, "Failed to mmap.\n");
#endif

        for (i = 0; i < heap_size / sizeof(uint32_t); i++)
            addr[i] = i;

#ifdef _WIN32
        hr = ID3D12Device3_OpenExistingHeapFromAddress(device3, addr, &IID_ID3D12Heap, (void **)&heap);
#else
        hr = ID3D12Device13_OpenExistingHeapFromAddress1(device13, addr, heap_size, &IID_ID3D12Heap, (void **)&heap);
#endif
        ok(hr == S_OK, "Failed to open heap from address: hr #%x.\n", (int)hr);

        freed = false;

        if (heap)
        {
            heap_desc = ID3D12Heap_GetDesc(heap);
            ok(heap_desc.SizeInBytes == heap_size, "Expected heap size of %u, but got %u.\n", heap_size, (unsigned int)heap_desc.SizeInBytes);
            ok(!!(heap_desc.Flags & D3D12_HEAP_FLAG_SHARED_CROSS_ADAPTER), "Expected Heap desc to have SHARED_CROSS_ADAPTER flag set.\n");
            ok(!!(heap_desc.Flags & D3D12_HEAP_FLAG_SHARED), "Expected heap desc to have SHARED flag set.\n");

            resource = create_placed_buffer(device, heap, 0, heap_size, D3D12_RESOURCE_FLAG_ALLOW_CROSS_ADAPTER, D3D12_RESOURCE_STATE_COPY_SOURCE);
            ok(!!resource, "Failed to create resource.\n");
            hr = ID3D12Resource_Map(resource, 0u, NULL, &map_ptr);
            ok(hr == S_OK, "Failed to map resource, hr %#x.\n", (int)hr);
            ID3D12Resource_Unmap(resource, 0u, NULL);
            ok(map_ptr == addr, "Expected mapped pointer %p to match VirtualAlloc address %p.\n", map_ptr, addr);
            readback_resource = create_default_buffer(device, heap_size, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
            ID3D12GraphicsCommandList_CopyResource(context.list, readback_resource, resource);
            transition_resource_state(context.list, readback_resource, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COPY_SOURCE);
            get_buffer_readback_with_command_list(readback_resource, DXGI_FORMAT_UNKNOWN, &rb, context.queue, context.list);
            reset_command_list(context.list, context.allocator);
            ok(!memcmp(rb.data, addr, heap_size), "Expected exact copy.\n");
            release_resource_readback(&rb);
#ifdef _WIN32
            freed = VirtualFree(addr, 0, MEM_RELEASE);
            todo ok(!freed, "Expected VirtualFree to fail.\n");
#endif
            ID3D12Heap_Release(heap);
            ID3D12Resource_Release(readback_resource);
            ID3D12Resource_Release(resource);
        }
        if (!freed)
        {
#ifdef _WIN32
            freed = VirtualFree(addr, 0, MEM_RELEASE);
            ok(freed, "Expected VirtualFree to succeed.\n");
#else
            munmap(addr, heap_size);
#endif
        }
    }

#ifdef _WIN32
    /* Import at offset, which should fail. */
    {
        heap_size = 64 * 1024;
        addr = VirtualAlloc(NULL, heap_size, MEM_COMMIT, PAGE_READWRITE);
        ok(!!addr, "Failed to VirtualAllocate.\n");
        hr = ID3D12Device3_OpenExistingHeapFromAddress(device3, addr + 1024, &IID_ID3D12Heap, (void **)&heap);
        ok(hr == E_INVALIDARG, "Should not be able to open heap at offset from VirtualAlloc.\n");
    }

    /* HANDLE variant. Kernel drivers might not support this (yet) since the file mapping might be backed by a file descriptor. */
    {
        heap_size = 256 * 1024;
        file_handle = CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, heap_size, "foobar");
        ok(!!file_handle, "Failed to open file mapping.\n");

        addr = MapViewOfFile(file_handle, FILE_MAP_ALL_ACCESS, 0, 0, heap_size);
        ok(!!addr, "Failed to map view of file.\n");
        for (i = 0; i < heap_size / sizeof(uint32_t); i++)
            addr[i] = i;

        hr = ID3D12Device3_OpenExistingHeapFromFileMapping(device3, file_handle, &IID_ID3D12Heap, (void **)&heap);
        ok(hr == S_OK, "Failed to open heap from file mapping: hr #%x.\n", (int)hr);

        if (SUCCEEDED(hr))
        {
            heap_desc = ID3D12Heap_GetDesc(heap);
            ok(heap_desc.SizeInBytes == heap_size, "Expected heap size of %u, but got %u.\n", heap_size, (unsigned int)heap_desc.SizeInBytes);
            ok(!!(heap_desc.Flags & D3D12_HEAP_FLAG_SHARED_CROSS_ADAPTER), "Expected Heap desc to have SHARED_CROSS_ADAPTER flag set.\n");
            ok(!!(heap_desc.Flags & D3D12_HEAP_FLAG_SHARED), "Expected heap desc to have SHARED flag set.\n");

            resource = create_placed_buffer(device, heap, 0, heap_size, D3D12_RESOURCE_FLAG_ALLOW_CROSS_ADAPTER, D3D12_RESOURCE_STATE_COPY_SOURCE);
            ok(!!resource, "Failed to create resource.\n");
            readback_resource = create_default_buffer(device, heap_size, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
            ID3D12GraphicsCommandList_CopyResource(context.list, readback_resource, resource);
            transition_resource_state(context.list, readback_resource, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COPY_SOURCE);
            get_buffer_readback_with_command_list(readback_resource, DXGI_FORMAT_UNKNOWN, &rb, context.queue, context.list);
            reset_command_list(context.list, context.allocator);
            for (i = 1; i < heap_size / sizeof(uint32_t); i++)
            {
                uint32_t v = get_readback_uint(&rb, i, 0, 0);
                todo ok(v == i, "Expected %u, got %u.\n", i, v);
                if (v != i)
                    break;
            }
            release_resource_readback(&rb);
            ID3D12Heap_Release(heap);
            ID3D12Resource_Release(readback_resource);
            ID3D12Resource_Release(resource);
        }

        UnmapViewOfFile(addr);
        CloseHandle(file_handle);
    }
#endif

    if (device13)
        ID3D12Device13_Release(device13);
    ID3D12Device3_Release(device3);
    destroy_test_context(&context);
}

void test_write_watch(void)
{
#ifndef _WIN32
    skip("WRITE_WATCH tests cannot pass on native Linux. Skipping.\n");
#else
    D3D12_HEAP_PROPERTIES heap_properties;
    D3D12_RESOURCE_DESC resource_desc;
    struct test_context_desc desc;
    void **dirty_addresses = NULL;
    struct test_context context;
    ULONG_PTR address_count;
    ID3D12Resource *buffer;
    size_t mapping_size;
    DWORD page_size;
    char *map_ptr;
    UINT result;
    HRESULT hr;

    mapping_size = 64 * 1024;

    memset(&desc, 0, sizeof(desc));
    desc.no_render_target = true;
    desc.no_pipeline = true;
    desc.no_root_signature = true;
    if (!init_test_context(&context, &desc))
        return;

    memset(&heap_properties, 0, sizeof(heap_properties));

    resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    resource_desc.Alignment = 0;
    resource_desc.Width = mapping_size;
    resource_desc.Height = 1;
    resource_desc.DepthOrArraySize = 1;
    resource_desc.MipLevels = 1;
    resource_desc.Format = DXGI_FORMAT_UNKNOWN;
    resource_desc.SampleDesc.Count = 1;
    resource_desc.SampleDesc.Quality = 0;
    resource_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    buffer = NULL;
    heap_properties.Type = D3D12_HEAP_TYPE_DEFAULT;
    resource_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    hr = ID3D12Device_CreateCommittedResource(context.device, &heap_properties,
            D3D12_HEAP_FLAG_ALLOW_WRITE_WATCH, &resource_desc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
            NULL, &IID_ID3D12Resource, (void **)&buffer);
    ok(hr == E_INVALIDARG, "Got hr %#x, expected %#x.\n", (int)hr, (int)E_INVALIDARG);
    if (buffer)
        ID3D12Resource_Release(buffer);

    buffer = NULL;
    heap_properties.Type = D3D12_HEAP_TYPE_UPLOAD;
    resource_desc.Flags = D3D12_RESOURCE_FLAG_NONE;
    hr = ID3D12Device_CreateCommittedResource(context.device, &heap_properties,
            D3D12_HEAP_FLAG_ALLOW_WRITE_WATCH, &resource_desc, D3D12_RESOURCE_STATE_GENERIC_READ,
            NULL, &IID_ID3D12Resource, (void **)&buffer);
    ok(hr == S_OK, "Got hr %#x, expected %#x.\n", (int)hr, (int)S_OK);

    if (FAILED(hr))
    {
        skip("Failed to create write watch buffer.\n");
        goto done;
    }

    /* Do some basic write watch testing... */
    hr = ID3D12Resource_Map(buffer, 0, NULL, (void**) &map_ptr);
    ok(hr == S_OK, "Got hr %#x, expected %#x.\n", (int)hr, (int)S_OK);
    if (FAILED(hr))
    {
        skip("Failed to map write watch resource.\n");
        goto done;
    }

    result = ResetWriteWatch((void*) map_ptr, mapping_size);
    ok(!result, "Failed to ResetWriteWatch %#x.\n", (int)GetLastError());
    if (result)
    {
        skip("Failed to ResetWriteWatch, skipping the rest of the WRITE_WATCH tests.\n");
        goto done;
    }

    page_size = 0x1000;
    address_count = mapping_size / (DWORD_PTR)page_size;
    dirty_addresses = malloc(sizeof(void*) * address_count);

    /* Dirty it a bit, in some pages... */
    map_ptr[0 * page_size] = 'a';
    map_ptr[1 * page_size] = 'b';
    map_ptr[5 * page_size] = 'c';
    map_ptr[9 * page_size] = 'd';

    result = GetWriteWatch(WRITE_WATCH_FLAG_RESET, (void*) map_ptr, mapping_size, dirty_addresses, &address_count, &page_size);
    ok(!result, "Failed to GetWriteWatch %#x.\n", (int)GetLastError());
    if (result)
    {
        skip("Failed to GetWriteWatch, skipping the rest of the WRITE_WATCH tests.\n");
        goto done;
    }

    ok(address_count == 4, "Expected address_count of %d, got %zu\n", 4, (size_t)address_count);
    ok(page_size == 0x1000, "Expected page_size of %u, got %u\n", 0x1000, (unsigned int)page_size);
    ok(dirty_addresses[0] == (void*)&map_ptr[0 * page_size], "Expected dirty address 0 to be %p, got %p\n",
            (void*)&map_ptr[0 * page_size], dirty_addresses[0]);
    ok(dirty_addresses[1] == (void*)&map_ptr[1 * page_size], "Expected dirty address 1 to be %p, got %p\n",
            (void*)&map_ptr[1 * page_size], dirty_addresses[1]);
    ok(dirty_addresses[2] == (void*)&map_ptr[5 * page_size], "Expected dirty address 2 to be %p, got %p\n",
            (void*)&map_ptr[5 * page_size], dirty_addresses[2]);
    ok(dirty_addresses[3] == (void*)&map_ptr[9 * page_size], "Expected dirty address 3 to be %p, got %p\n",
            (void*)&map_ptr[9 * page_size], dirty_addresses[3]);

done:
    free(dirty_addresses);

    if (buffer)
        ID3D12Resource_Release(buffer);

    destroy_test_context(&context);
#endif
}

#ifdef _WIN32
static void process_window_messages(void)
{
    MSG msg;

    while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE))
    {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
}

static HRESULT wait_for_present_test_result(IDXGISwapChain2 *swapchain, HRESULT expected)
{
    unsigned int i;
    HRESULT hr;

    for (i = 0; i < 200; i++)
    {
        process_window_messages();
        if ((hr = IDXGISwapChain2_Present(swapchain, 0, DXGI_PRESENT_TEST)) == expected)
            break;
        vkd3d_sleep(10);
    }

    return hr;
}
#endif

void test_frame_latency_waitable_occluded(void)
{
#ifndef _WIN32
    skip("Occlusion requires a Win32 window. Skipping.\n");
#else
    static const unsigned int max_latency = 2;
    IDXGISwapChain2 *swapchain = NULL;
    IDXGISwapChain1 *swapchain1;
    DXGI_SWAP_CHAIN_DESC1 desc;
    struct test_context context;
    unsigned int i, timeouts;
    IDXGIFactory2 *factory;
    unsigned int count;
    HANDLE waitable;
    HWND window;
    HRESULT hr;

    if (!init_test_context(&context, NULL))
        return;

    hr = CreateDXGIFactory1(&IID_IDXGIFactory2, (void **)&factory);
    ok(hr == S_OK, "Failed to create factory, hr %#x.\n", (int)hr);

    window = CreateWindowA("static", "vkd3d_test", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
            0, 0, 320, 240, NULL, NULL, NULL, NULL);
    ok(!!window, "Failed to create window.\n");

    memset(&desc, 0, sizeof(desc));
    desc.Width = 320;
    desc.Height = 240;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 3;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    desc.Flags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;

    hr = IDXGIFactory2_CreateSwapChainForHwnd(factory, (IUnknown *)context.queue, window, &desc, NULL, NULL, &swapchain1);
    ok(hr == S_OK, "Failed to create swapchain, hr %#x.\n", (int)hr);
    if (FAILED(hr))
        goto done;

    hr = IDXGISwapChain1_QueryInterface(swapchain1, &IID_IDXGISwapChain2, (void **)&swapchain);
    ok(hr == S_OK, "Failed to query IDXGISwapChain2, hr %#x.\n", (int)hr);
    IDXGISwapChain1_Release(swapchain1);
    if (FAILED(hr))
        goto done;

    hr = IDXGISwapChain2_SetMaximumFrameLatency(swapchain, max_latency);
    ok(hr == S_OK, "Failed to set maximum frame latency, hr %#x.\n", (int)hr);
    waitable = IDXGISwapChain2_GetFrameLatencyWaitableObject(swapchain);
    ok(!!waitable, "Failed to get frame latency waitable object.\n");

    for (i = 0; i < 8; i++)
    {
        process_window_messages();
        ok(WaitForSingleObject(waitable, 5000) == WAIT_OBJECT_0, "Failed to wait for frame latency object.\n");
        hr = IDXGISwapChain2_Present(swapchain, 0, 0);
        ok(hr == S_OK, "Failed to present, hr %#x.\n", (int)hr);
    }

    ShowWindow(window, SW_MINIMIZE);
    if (wait_for_present_test_result(swapchain, DXGI_STATUS_OCCLUDED) != DXGI_STATUS_OCCLUDED)
    {
        skip("Minimized window does not report occlusion. Skipping.\n");
        goto done_waitable;
    }

    /* An occluded Present still retires a frame. If it does not hand back the frame latency
     * count the application acquired for it, the waitable eventually starves. */
    for (i = 0, timeouts = 0; i < 8; i++)
    {
        process_window_messages();
        if (WaitForSingleObject(waitable, 1000) == WAIT_TIMEOUT)
            timeouts++;
        hr = IDXGISwapChain2_Present(swapchain, 0, 0);
        ok(hr == DXGI_STATUS_OCCLUDED, "Unexpected hr %#x.\n", (int)hr);
    }
    ok(!timeouts, "Frame latency waitable timed out %u times while occluded.\n", timeouts);

    /* Presenting while occluded without acquiring first must not raise the count above the maximum latency. */
    for (i = 0; i < 8; i++)
    {
        hr = IDXGISwapChain2_Present(swapchain, 0, 0);
        ok(hr == DXGI_STATUS_OCCLUDED, "Unexpected hr %#x.\n", (int)hr);
    }
    for (count = 0; count < DXGI_MAX_SWAP_CHAIN_BUFFERS; count++)
    {
        if (WaitForSingleObject(waitable, 0) != WAIT_OBJECT_0)
            break;
    }
    ok(count <= max_latency, "Frame latency count %u exceeds maximum latency %u.\n", count, max_latency);

    ShowWindow(window, SW_RESTORE);
    if (wait_for_present_test_result(swapchain, S_OK) != S_OK)
    {
        skip("Restored window still reports occlusion. Skipping.\n");
        goto done_waitable;
    }

    /* Hand back the counts drained above, then render normally again. */
    for (i = 0; i < count; i++)
    {
        hr = IDXGISwapChain2_Present(swapchain, 0, 0);
        ok(hr == S_OK, "Failed to present, hr %#x.\n", (int)hr);
    }
    for (i = 0, timeouts = 0; i < 8; i++)
    {
        process_window_messages();
        if (WaitForSingleObject(waitable, 1000) == WAIT_TIMEOUT)
            timeouts++;
        hr = IDXGISwapChain2_Present(swapchain, 0, 0);
        ok(hr == S_OK, "Failed to present, hr %#x.\n", (int)hr);
    }
    ok(!timeouts, "Frame latency waitable timed out %u times after restoring.\n", timeouts);

done_waitable:
    CloseHandle(waitable);
done:
    wait_queue_idle(context.device, context.queue);
    if (swapchain)
        IDXGISwapChain2_Release(swapchain);
    if (window)
        DestroyWindow(window);
    IDXGIFactory2_Release(factory);
    destroy_test_context(&context);
#endif
}

