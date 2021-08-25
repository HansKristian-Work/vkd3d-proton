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
    ok(hr == S_OK, "Failed retrieve calibrated timestamps, hr %#x.\n", hr);

    vkd3d_sleep(100);

    hr = ID3D12CommandQueue_GetClockCalibration(context.queue, &gpu_times[1], &cpu_times[1]);
    ok(hr == S_OK, "Failed retrieve calibrated timestamps, hr %#x.\n", hr);

    ok(gpu_times[1] > gpu_times[0], "Inconsistent GPU timestamps.\n");
    ok(cpu_times[1] > cpu_times[0], "Inconsistent CPU timestamps.\n");

    destroy_test_context(&context);
#endif
}

void test_open_heap_from_address(void)
{
#ifdef _WIN32
    ID3D12Resource *readback_resource;
    struct test_context context;
    struct resource_readback rb;
    D3D12_HEAP_DESC heap_desc;
    ID3D12Resource *resource;
    unsigned int heap_size;
    ID3D12Device3 *device3;
    ID3D12Device *device;
    HANDLE file_handle;
    ID3D12Heap *heap;
    unsigned int i;
    uint32_t *addr;
    HRESULT hr;

    if (!init_test_context(&context, NULL))
        return;

    device = context.device;
    hr = ID3D12Device_QueryInterface(device, &IID_ID3D12Device3, (void **)&device3);
    ok(hr == S_OK, "Failed to query ID3D12Device3, hr #%x.\n", hr);

    /* Simple case, import directly from VirtualAlloc. */
    {
        heap_size = 64 * 1024;
        addr = VirtualAlloc(NULL, heap_size, MEM_COMMIT, PAGE_READWRITE);
        ok(!!addr, "Failed to VirtualAllocate.\n");

        for (i = 0; i < heap_size / sizeof(uint32_t); i++)
            addr[i] = i;

        hr = ID3D12Device3_OpenExistingHeapFromAddress(device3, addr, &IID_ID3D12Heap, (void **)&heap);
        ok(hr == S_OK, "Failed to open heap from address: hr #%x.\n", hr);

        if (heap)
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
            ok(!memcmp(rb.data, addr, heap_size), "Expected exact copy.\n");
            release_resource_readback(&rb);
            ID3D12Heap_Release(heap);
            ID3D12Resource_Release(readback_resource);
            ID3D12Resource_Release(resource);
        }
        VirtualFree(addr, 0, MEM_RELEASE);
    }

    /* Import at offset, which should fail. */
    {
        heap_size = 64 * 1024;
        addr = VirtualAlloc(NULL, heap_size, MEM_COMMIT, PAGE_READWRITE);
        ok(!!addr, "Failed to VirtualAllocate.\n");
        hr = ID3D12Device3_OpenExistingHeapFromAddress(device3, addr + 1024, &IID_ID3D12Heap, (void **)&heap);
        ok(hr == E_INVALIDARG, "Should not be able to open heap at offset from VirtualAlloc.\n");
    }

    /* HANDLE variant. */
    {
        heap_size = 256 * 1024;
        file_handle = CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, heap_size, "foobar");
        ok(!!file_handle, "Failed to open file mapping.\n");

        addr = MapViewOfFile(file_handle, FILE_MAP_ALL_ACCESS, 0, 0, heap_size);
        ok(!!addr, "Failed to map view of file.\n");
        for (i = 0; i < heap_size / sizeof(uint32_t); i++)
            addr[i] = i;

        hr = ID3D12Device3_OpenExistingHeapFromFileMapping(device3, file_handle, &IID_ID3D12Heap, (void **)&heap);
        ok(hr == S_OK, "Failed to open heap from file mapping: hr #%x.\n", hr);

        if (heap)
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
            for (i = 0; i < heap_size / sizeof(uint32_t); i++)
            {
                uint32_t v = get_readback_uint(&rb, i, 0, 0);
                ok(v == i, "Expected %u, got %u.\n", i, v);
            }
            release_resource_readback(&rb);
            ID3D12Heap_Release(heap);
            ID3D12Resource_Release(readback_resource);
            ID3D12Resource_Release(resource);
        }

        UnmapViewOfFile(addr);
        CloseHandle(file_handle);
    }

    ID3D12Device3_Release(device3);
    destroy_test_context(&context);
#else
    skip("Cannot test OpenExistingHeapFrom* on non-native Win32 platforms.\n");
#endif
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
    ok(hr == E_INVALIDARG, "Got hr %#x, expected %#x.\n", hr, E_INVALIDARG);
    if (buffer)
        ID3D12Resource_Release(buffer);

    buffer = NULL;
    heap_properties.Type = D3D12_HEAP_TYPE_UPLOAD;
    resource_desc.Flags = D3D12_RESOURCE_FLAG_NONE;
    hr = ID3D12Device_CreateCommittedResource(context.device, &heap_properties,
            D3D12_HEAP_FLAG_ALLOW_WRITE_WATCH, &resource_desc, D3D12_RESOURCE_STATE_GENERIC_READ,
            NULL, &IID_ID3D12Resource, (void **)&buffer);
    ok(hr == S_OK, "Got hr %#x, expected %#x.\n", hr, S_OK);

    if (FAILED(hr))
    {
        skip("Failed to create write watch buffer.\n");
        goto done;
    }

    /* Do some basic write watch testing... */
    hr = ID3D12Resource_Map(buffer, 0, NULL, (void**) &map_ptr);
    ok(hr == S_OK, "Got hr %#x, expected %#x.\n", hr, S_OK);
    if (FAILED(hr))
    {
        skip("Failed to map write watch resource.\n");
        goto done;
    }

    result = ResetWriteWatch((void*) map_ptr, mapping_size);
    ok(!result, "Failed to ResetWriteWatch %#x.\n", GetLastError());
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
    ok(!result, "Failed to GetWriteWatch %#x.\n", GetLastError());
    if (result)
    {
        skip("Failed to GetWriteWatch, skipping the rest of the WRITE_WATCH tests.\n");
        goto done;
    }

    ok(address_count == 4, "Expected address_count of %p, got %p\n", 4, address_count);
    ok(page_size == 0x1000, "Expected page_size of %u, got %u\n", 0x1000, page_size);
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

