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

void test_create_committed_resource(void)
{
    D3D12_GPU_VIRTUAL_ADDRESS gpu_address;
    D3D12_HEAP_PROPERTIES heap_properties;
    D3D12_RESOURCE_DESC resource_desc;
    ID3D12Device *device, *tmp_device;
    D3D12_CLEAR_VALUE clear_value;
    D3D12_RESOURCE_STATES state;
    ID3D12Resource *resource;
    unsigned int i;
    ULONG refcount;
    HRESULT hr;

    static const struct
    {
        D3D12_HEAP_TYPE heap_type;
        D3D12_RESOURCE_FLAGS flags;
    }
    invalid_buffer_desc_tests[] =
    {
        /* Render target or unordered access resources are not allowed with UPLOAD or READBACK. */
        {D3D12_HEAP_TYPE_UPLOAD,   D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET},
        {D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET},
        {D3D12_HEAP_TYPE_UPLOAD,   D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS},
        {D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS},
        {D3D12_HEAP_TYPE_DEFAULT,  D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS},
        {D3D12_HEAP_TYPE_UPLOAD,   D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS},
        {D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS},
    };

    if (!(device = create_device()))
    {
        skip("Failed to create device.\n");
        return;
    }

    memset(&heap_properties, 0, sizeof(heap_properties));
    heap_properties.Type = D3D12_HEAP_TYPE_DEFAULT;

    resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    resource_desc.Alignment = 0;
    resource_desc.Width = 32;
    resource_desc.Height = 32;
    resource_desc.DepthOrArraySize = 1;
    resource_desc.MipLevels = 1;
    resource_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    resource_desc.SampleDesc.Count = 1;
    resource_desc.SampleDesc.Quality = 0;
    resource_desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    resource_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

    clear_value.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    clear_value.Color[0] = 1.0f;
    clear_value.Color[1] = 0.0f;
    clear_value.Color[2] = 0.0f;
    clear_value.Color[3] = 1.0f;

    hr = ID3D12Device_CreateCommittedResource(device, &heap_properties, D3D12_HEAP_FLAG_NONE,
            &resource_desc, D3D12_RESOURCE_STATE_RENDER_TARGET, &clear_value,
            &IID_ID3D12Resource, (void **)&resource);
    ok(hr == S_OK, "Failed to create committed resource, hr %#x.\n", hr);

    refcount = get_refcount(device);
    ok(refcount == 2, "Got unexpected refcount %u.\n", (unsigned int)refcount);
    hr = ID3D12Resource_GetDevice(resource, &IID_ID3D12Device, (void **)&tmp_device);
    ok(hr == S_OK, "Failed to get device, hr %#x.\n", hr);
    refcount = get_refcount(device);
    ok(refcount == 3, "Got unexpected refcount %u.\n", (unsigned int)refcount);
    refcount = ID3D12Device_Release(tmp_device);
    ok(refcount == 2, "Got unexpected refcount %u.\n", (unsigned int)refcount);

    check_interface(resource, &IID_ID3D12Object, true);
    check_interface(resource, &IID_ID3D12DeviceChild, true);
    check_interface(resource, &IID_ID3D12Pageable, true);
    check_interface(resource, &IID_ID3D12Resource, true);

    gpu_address = ID3D12Resource_GetGPUVirtualAddress(resource);
    ok(!gpu_address, "Got unexpected GPU virtual address %#"PRIx64".\n", gpu_address);

    refcount = ID3D12Resource_Release(resource);
    ok(!refcount, "ID3D12Resource has %u references left.\n", (unsigned int)refcount);

    resource_desc.MipLevels = 0;
    hr = ID3D12Device_CreateCommittedResource(device, &heap_properties, D3D12_HEAP_FLAG_NONE,
            &resource_desc, D3D12_RESOURCE_STATE_RENDER_TARGET, &clear_value,
            &IID_ID3D12Resource, (void **)&resource);
    ok(hr == S_OK, "Failed to create committed resource, hr %#x.\n", hr);
    resource_desc = ID3D12Resource_GetDesc(resource);
    ok(resource_desc.MipLevels == 6, "Got unexpected miplevels %u.\n", resource_desc.MipLevels);
    ID3D12Resource_Release(resource);
    resource_desc.MipLevels = 10;
    hr = ID3D12Device_CreateCommittedResource(device, &heap_properties, D3D12_HEAP_FLAG_NONE,
            &resource_desc, D3D12_RESOURCE_STATE_RENDER_TARGET, &clear_value,
            &IID_ID3D12Resource, (void **)&resource);
    ok(hr == S_OK, "Failed to create committed resource, hr %#x.\n", hr);
    resource_desc = ID3D12Resource_GetDesc(resource);
    ok(resource_desc.MipLevels == 10, "Got unexpected miplevels %u.\n", resource_desc.MipLevels);
    ID3D12Resource_Release(resource);
    resource_desc.MipLevels = 1;

    hr = ID3D12Device_CreateCommittedResource(device, &heap_properties, D3D12_HEAP_FLAG_NONE,
            &resource_desc, D3D12_RESOURCE_STATE_RENDER_TARGET | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
            &clear_value, &IID_ID3D12Resource, (void **)&resource);
    ok(hr == E_INVALIDARG, "Got unexpected hr %#x.\n", hr);

    /* For D3D12_RESOURCE_STATE_RENDER_TARGET the D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET flag is required. */
    resource_desc.Flags = 0;
    hr = ID3D12Device_CreateCommittedResource(device, &heap_properties, D3D12_HEAP_FLAG_NONE,
            &resource_desc, D3D12_RESOURCE_STATE_RENDER_TARGET, NULL,
            &IID_ID3D12Resource, (void **)&resource);
    todo ok(hr == E_INVALIDARG, "Got unexpected hr %#x.\n", hr);
    if (SUCCEEDED(hr))
        ID3D12Resource_Release(resource);

    /* A texture cannot be created on a UPLOAD heap. */
    heap_properties.Type = D3D12_HEAP_TYPE_UPLOAD;
    resource = (void *)(uintptr_t)0xdeadbeef;
    hr = ID3D12Device_CreateCommittedResource(device, &heap_properties, D3D12_HEAP_FLAG_NONE,
            &resource_desc, D3D12_RESOURCE_STATE_GENERIC_READ, NULL,
            &IID_ID3D12Resource, (void **)&resource);
    ok(hr == E_INVALIDARG, "Got unexpected hr %#x.\n", hr);
    ok(!resource, "Got unexpected pointer %p.\n", resource);

    resource = (void *)(uintptr_t)0xdeadbeef;
    hr = ID3D12Device_CreateCommittedResource(device, &heap_properties, D3D12_HEAP_FLAG_NONE,
            &resource_desc, D3D12_RESOURCE_STATE_GENERIC_READ, NULL,
            &IID_ID3D12Device, (void **)&resource);
    ok(hr == E_INVALIDARG, "Got unexpected hr %#x.\n", hr);
    ok(!resource, "Got unexpected pointer %p.\n", resource);

    /* A texture cannot be created on a READBACK heap. */
    heap_properties.Type = D3D12_HEAP_TYPE_READBACK;
    hr = ID3D12Device_CreateCommittedResource(device, &heap_properties, D3D12_HEAP_FLAG_NONE,
            &resource_desc, D3D12_RESOURCE_STATE_COPY_DEST, NULL,
            &IID_ID3D12Resource, (void **)&resource);
    ok(hr == E_INVALIDARG, "Got unexpected hr %#x.\n", hr);

    heap_properties.Type = D3D12_HEAP_TYPE_DEFAULT;
    resource_desc.Format = DXGI_FORMAT_BC1_UNORM;
    hr = ID3D12Device_CreateCommittedResource(device, &heap_properties, D3D12_HEAP_FLAG_NONE,
            &resource_desc, D3D12_RESOURCE_STATE_COMMON, NULL,
            &IID_ID3D12Resource, (void **)&resource);
    ok(hr == S_OK, "Failed to create committed resource, hr %#x.\n", hr);
    ID3D12Resource_Release(resource);

    resource_desc.Height = 31;
    hr = ID3D12Device_CreateCommittedResource(device, &heap_properties, D3D12_HEAP_FLAG_NONE,
            &resource_desc, D3D12_RESOURCE_STATE_COMMON, NULL,
            &IID_ID3D12Resource, (void **)&resource);
    ok(hr == E_INVALIDARG, "Got unexpected hr %#x.\n", hr);

    resource_desc.Width = 31;
    resource_desc.Height = 32;
    hr = ID3D12Device_CreateCommittedResource(device, &heap_properties, D3D12_HEAP_FLAG_NONE,
            &resource_desc, D3D12_RESOURCE_STATE_COMMON, NULL,
            &IID_ID3D12Resource, (void **)&resource);
    ok(hr == E_INVALIDARG, "Got unexpected hr %#x.\n", hr);

    resource_desc.Width = 30;
    resource_desc.Height = 30;
    hr = ID3D12Device_CreateCommittedResource(device, &heap_properties, D3D12_HEAP_FLAG_NONE,
            &resource_desc, D3D12_RESOURCE_STATE_COMMON, NULL,
            &IID_ID3D12Resource, (void **)&resource);
    ok(hr == E_INVALIDARG, "Got unexpected hr %#x.\n", hr);

    resource_desc.Width = 2;
    resource_desc.Height = 2;
    hr = ID3D12Device_CreateCommittedResource(device, &heap_properties, D3D12_HEAP_FLAG_NONE,
            &resource_desc, D3D12_RESOURCE_STATE_COMMON, NULL,
            &IID_ID3D12Resource, (void **)&resource);
    ok(hr == E_INVALIDARG, "Got unexpected hr %#x.\n", hr);

    resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE1D;
    resource_desc.Width = 32;
    resource_desc.Height = 1;
    hr = ID3D12Device_CreateCommittedResource(device, &heap_properties, D3D12_HEAP_FLAG_NONE,
            &resource_desc, D3D12_RESOURCE_STATE_COMMON, NULL,
            &IID_ID3D12Resource, (void **)&resource);
    ok(hr == E_INVALIDARG, "Got unexpected hr %#x.\n", hr);

    heap_properties.Type = D3D12_HEAP_TYPE_UPLOAD;

    resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    resource_desc.Alignment = 0;
    resource_desc.Width = 32;
    resource_desc.Height = 1;
    resource_desc.DepthOrArraySize = 1;
    resource_desc.MipLevels = 1;
    resource_desc.Format = DXGI_FORMAT_UNKNOWN;
    resource_desc.SampleDesc.Count = 1;
    resource_desc.SampleDesc.Quality = 0;
    resource_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    resource_desc.Flags = D3D12_RESOURCE_FLAG_NONE;

    hr = ID3D12Device_CreateCommittedResource(device, &heap_properties, D3D12_HEAP_FLAG_NONE,
            &resource_desc, D3D12_RESOURCE_STATE_GENERIC_READ, NULL,
            &IID_ID3D12Resource, (void **)&resource);
    ok(hr == S_OK, "Failed to create committed resource, hr %#x.\n", hr);

    check_interface(resource, &IID_ID3D12Object, true);
    check_interface(resource, &IID_ID3D12DeviceChild, true);
    check_interface(resource, &IID_ID3D12Pageable, true);
    check_interface(resource, &IID_ID3D12Resource, true);

    gpu_address = ID3D12Resource_GetGPUVirtualAddress(resource);
    ok(gpu_address, "Got unexpected GPU virtual address %#"PRIx64".\n", gpu_address);

    refcount = ID3D12Resource_Release(resource);
    ok(!refcount, "ID3D12Resource has %u references left.\n", (unsigned int)refcount);

    resource_desc.MipLevels = 0;
    hr = ID3D12Device_CreateCommittedResource(device, &heap_properties, D3D12_HEAP_FLAG_NONE,
            &resource_desc, D3D12_RESOURCE_STATE_GENERIC_READ, &clear_value,
            &IID_ID3D12Resource, (void **)&resource);
    ok(hr == E_INVALIDARG, "Failed to create committed resource, hr %#x.\n", hr);
    resource_desc.MipLevels = 1;

    /* The clear value must be NULL for buffers. */
    hr = ID3D12Device_CreateCommittedResource(device, &heap_properties, D3D12_HEAP_FLAG_NONE,
            &resource_desc, D3D12_RESOURCE_STATE_GENERIC_READ, &clear_value,
            &IID_ID3D12Resource, (void **)&resource);
    ok(hr == E_INVALIDARG, "Got unexpected hr %#x.\n", hr);

    /* For D3D12_HEAP_TYPE_UPLOAD the state must be D3D12_RESOURCE_STATE_GENERIC_READ. */
    hr = ID3D12Device_CreateCommittedResource(device, &heap_properties, D3D12_HEAP_FLAG_NONE,
            &resource_desc, D3D12_RESOURCE_STATE_COMMON, NULL,
            &IID_ID3D12Resource, (void **)&resource);
    ok(hr == E_INVALIDARG, "Got unexpected hr %#x.\n", hr);
    hr = ID3D12Device_CreateCommittedResource(device, &heap_properties, D3D12_HEAP_FLAG_NONE,
            &resource_desc, D3D12_RESOURCE_STATE_COPY_SOURCE, NULL,
            &IID_ID3D12Resource, (void **)&resource);
    ok(hr == E_INVALIDARG, "Got unexpected hr %#x.\n", hr);

    heap_properties.Type = D3D12_HEAP_TYPE_READBACK;

    hr = ID3D12Device_CreateCommittedResource(device, &heap_properties, D3D12_HEAP_FLAG_NONE,
            &resource_desc, D3D12_RESOURCE_STATE_COPY_DEST, NULL,
            &IID_ID3D12Resource, (void **)&resource);
    ok(hr == S_OK, "Failed to create committed resource, hr %#x.\n", hr);

    gpu_address = ID3D12Resource_GetGPUVirtualAddress(resource);
    ok(gpu_address, "Got unexpected GPU virtual address %#"PRIx64".\n", gpu_address);

    refcount = ID3D12Resource_Release(resource);
    ok(!refcount, "ID3D12Resource has %u references left.\n", (unsigned int)refcount);

    /* For D3D12_HEAP_TYPE_READBACK the state must be D3D12_RESOURCE_STATE_COPY_DEST. */
    hr = ID3D12Device_CreateCommittedResource(device, &heap_properties, D3D12_HEAP_FLAG_NONE,
            &resource_desc, D3D12_RESOURCE_STATE_COMMON, NULL,
            &IID_ID3D12Resource, (void **)&resource);
    ok(hr == E_INVALIDARG, "Got unexpected hr %#x.\n", hr);
    hr = ID3D12Device_CreateCommittedResource(device, &heap_properties, D3D12_HEAP_FLAG_NONE,
            &resource_desc, D3D12_RESOURCE_STATE_GENERIC_READ, NULL,
            &IID_ID3D12Resource, (void **)&resource);
    ok(hr == E_INVALIDARG, "Got unexpected hr %#x.\n", hr);
    hr = ID3D12Device_CreateCommittedResource(device, &heap_properties, D3D12_HEAP_FLAG_NONE,
            &resource_desc, D3D12_RESOURCE_STATE_COPY_SOURCE, NULL,
            &IID_ID3D12Resource, (void **)&resource);
    ok(hr == E_INVALIDARG, "Got unexpected hr %#x.\n", hr);

    for (i = 0; i < ARRAY_SIZE(invalid_buffer_desc_tests); ++i)
    {
        memset(&heap_properties, 0, sizeof(heap_properties));
        heap_properties.Type = invalid_buffer_desc_tests[i].heap_type;

        resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        resource_desc.Alignment = 0;
        resource_desc.Width = 32;
        resource_desc.Height = 1;
        resource_desc.DepthOrArraySize = 1;
        resource_desc.MipLevels = 1;
        resource_desc.Format = DXGI_FORMAT_UNKNOWN;
        resource_desc.SampleDesc.Count = 1;
        resource_desc.SampleDesc.Quality = 0;
        resource_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        resource_desc.Flags = invalid_buffer_desc_tests[i].flags;

        if (invalid_buffer_desc_tests[i].heap_type == D3D12_HEAP_TYPE_UPLOAD)
            state = D3D12_RESOURCE_STATE_GENERIC_READ;
        else
            state = D3D12_RESOURCE_STATE_COPY_DEST;

        hr = ID3D12Device_CreateCommittedResource(device, &heap_properties, D3D12_HEAP_FLAG_NONE,
                &resource_desc, state, NULL, &IID_ID3D12Resource, (void **)&resource);
        ok(hr == E_INVALIDARG, "Test %u: Got unexpected hr %#x.\n", i, hr);
    }

    refcount = ID3D12Device_Release(device);
    ok(!refcount, "ID3D12Device has %u references left.\n", (unsigned int)refcount);
}

void test_create_heap(void)
{
    D3D12_FEATURE_DATA_ARCHITECTURE architecture;
    D3D12_FEATURE_DATA_D3D12_OPTIONS options;
    D3D12_HEAP_DESC desc, result_desc;
    ID3D12Device *device, *tmp_device;
    bool is_pool_L1_supported;
    HRESULT hr, expected_hr;
    unsigned int i, j;
    ID3D12Heap *heap;
    ULONG refcount;

    static const struct
    {
        uint64_t alignment;
        HRESULT expected_hr;
    }
    tests[] =
    {
        {D3D12_DEFAULT_MSAA_RESOURCE_PLACEMENT_ALIGNMENT,     S_OK},
        {D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT,          S_OK},
        {2 * D3D12_DEFAULT_MSAA_RESOURCE_PLACEMENT_ALIGNMENT, E_INVALIDARG},
        {2 * D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT,      E_INVALIDARG},
        {D3D12_SMALL_RESOURCE_PLACEMENT_ALIGNMENT,            E_INVALIDARG},
    };
    static const struct
    {
        D3D12_HEAP_FLAGS flags;
        const char *name;
    }
    heap_flags[] =
    {
        {D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS, "buffers"},
        {D3D12_HEAP_FLAG_ALLOW_ONLY_NON_RT_DS_TEXTURES, "textures"},
        {D3D12_HEAP_FLAG_ALLOW_ONLY_RT_DS_TEXTURES, "rt_ds_textures"},
    };
    static const struct
    {
        D3D12_CPU_PAGE_PROPERTY page_property;
        D3D12_MEMORY_POOL pool_preference;
        HRESULT expected_hr;
    }
    custom_tests[] =
    {
        {D3D12_CPU_PAGE_PROPERTY_UNKNOWN, D3D12_MEMORY_POOL_UNKNOWN, E_INVALIDARG},
        {D3D12_CPU_PAGE_PROPERTY_NOT_AVAILABLE, D3D12_MEMORY_POOL_UNKNOWN, E_INVALIDARG},
        {D3D12_CPU_PAGE_PROPERTY_WRITE_COMBINE, D3D12_MEMORY_POOL_UNKNOWN, E_INVALIDARG},
        {D3D12_CPU_PAGE_PROPERTY_WRITE_BACK, D3D12_MEMORY_POOL_UNKNOWN, E_INVALIDARG},
        {D3D12_CPU_PAGE_PROPERTY_UNKNOWN, D3D12_MEMORY_POOL_L0, E_INVALIDARG},
        {D3D12_CPU_PAGE_PROPERTY_NOT_AVAILABLE, D3D12_MEMORY_POOL_L0, S_OK},
        {D3D12_CPU_PAGE_PROPERTY_WRITE_COMBINE, D3D12_MEMORY_POOL_L0, S_OK},
        {D3D12_CPU_PAGE_PROPERTY_WRITE_BACK, D3D12_MEMORY_POOL_L0, S_OK},
        {D3D12_CPU_PAGE_PROPERTY_UNKNOWN, D3D12_MEMORY_POOL_L1, E_INVALIDARG},
        {D3D12_CPU_PAGE_PROPERTY_NOT_AVAILABLE, D3D12_MEMORY_POOL_L1, S_OK},
        {D3D12_CPU_PAGE_PROPERTY_WRITE_COMBINE, D3D12_MEMORY_POOL_L1, E_INVALIDARG},
        {D3D12_CPU_PAGE_PROPERTY_WRITE_BACK, D3D12_MEMORY_POOL_L1, E_INVALIDARG},
    };

    if (!(device = create_device()))
    {
        skip("Failed to create device.\n");
        return;
    }

    desc.SizeInBytes = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
    memset(&desc.Properties, 0, sizeof(desc.Properties));
    desc.Properties.Type = D3D12_HEAP_TYPE_DEFAULT;
    desc.Alignment = 0;
    desc.Flags = D3D12_HEAP_FLAG_ALLOW_ONLY_NON_RT_DS_TEXTURES;
    hr = ID3D12Device_CreateHeap(device, &desc, &IID_ID3D12Heap, (void **)&heap);
    ok(hr == S_OK, "Failed to create heap, hr %#x.\n", hr);

    refcount = get_refcount(device);
    ok(refcount == 2, "Got unexpected refcount %u.\n", (unsigned int)refcount);
    hr = ID3D12Heap_GetDevice(heap, &IID_ID3D12Device, (void **)&tmp_device);
    ok(hr == S_OK, "Failed to get device, hr %#x.\n", hr);
    refcount = get_refcount(device);
    ok(refcount == 3, "Got unexpected refcount %u.\n", (unsigned int)refcount);
    refcount = ID3D12Device_Release(tmp_device);
    ok(refcount == 2, "Got unexpected refcount %u.\n", (unsigned int)refcount);

    check_interface(heap, &IID_ID3D12Object, true);
    check_interface(heap, &IID_ID3D12DeviceChild, true);
    check_interface(heap, &IID_ID3D12Pageable, true);
    check_interface(heap, &IID_ID3D12Heap, true);

    result_desc = ID3D12Heap_GetDesc(heap);
    check_heap_desc(&result_desc, &desc);

    refcount = ID3D12Heap_Release(heap);
    ok(!refcount, "ID3D12Heap has %u references left.\n", (unsigned int)refcount);

    desc.SizeInBytes = 0;
    hr = ID3D12Device_CreateHeap(device, &desc, &IID_ID3D12Heap, (void **)&heap);
    ok(hr == E_INVALIDARG, "Got unexpected hr %#x.\n", hr);

    desc.SizeInBytes = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
    desc.Flags = D3D12_HEAP_FLAG_ALLOW_ONLY_NON_RT_DS_TEXTURES | D3D12_HEAP_FLAG_ALLOW_DISPLAY;
    hr = ID3D12Device_CreateHeap(device, &desc, &IID_ID3D12Heap, (void **)&heap);
    ok(hr == E_INVALIDARG, "Got unexpected hr %#x.\n", hr);

    heap = (void *)(uintptr_t)0xdeadbeef;
    desc.Flags = D3D12_HEAP_FLAG_ALLOW_ONLY_RT_DS_TEXTURES | D3D12_HEAP_FLAG_ALLOW_DISPLAY;
    hr = ID3D12Device_CreateHeap(device, &desc, &IID_ID3D12Heap, (void **)&heap);
    ok(hr == E_INVALIDARG, "Got unexpected hr %#x.\n", hr);
    ok(!heap, "Got unexpected pointer %p.\n", heap);

    for (i = 0; i < ARRAY_SIZE(tests); ++i)
    {
        for (j = 0; j < ARRAY_SIZE(heap_flags); ++j)
        {
            vkd3d_test_set_context("Test %u, %u", i, j);

            desc.SizeInBytes = 10 * tests[i].alignment;
            desc.Alignment = tests[i].alignment;
            desc.Flags = heap_flags[j].flags;
            hr = ID3D12Device_CreateHeap(device, &desc, &IID_ID3D12Heap, (void **)&heap);
            ok(hr == tests[i].expected_hr, "Test %u, %s: Got hr %#x, expected %#x.\n",
                    i, heap_flags[j].name, hr, tests[i].expected_hr);
            if (FAILED(hr))
                continue;

            result_desc = ID3D12Heap_GetDesc(heap);
            check_heap_desc(&result_desc, &desc);

            refcount = ID3D12Heap_Release(heap);
            ok(!refcount, "ID3D12Heap has %u references left.\n", (unsigned int)refcount);
        }
    }
    vkd3d_test_set_context(NULL);

    hr = ID3D12Device_CheckFeatureSupport(device, D3D12_FEATURE_D3D12_OPTIONS, &options, sizeof(options));
    ok(hr == S_OK, "Failed to check feature support, hr %#x.\n", hr);
    if (options.ResourceHeapTier < D3D12_RESOURCE_HEAP_TIER_2)
    {
        skip("Resource heap tier %u.\n", options.ResourceHeapTier);
        goto done;
    }

    desc.SizeInBytes = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
    desc.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
    desc.Flags = D3D12_HEAP_FLAG_ALLOW_ALL_BUFFERS_AND_TEXTURES;
    hr = ID3D12Device_CreateHeap(device, &desc, &IID_ID3D12Heap, (void **)&heap);
    ok(hr == S_OK, "Got unexpected hr %#x.\n", hr);
    result_desc = ID3D12Heap_GetDesc(heap);
    check_heap_desc(&result_desc, &desc);
    refcount = ID3D12Heap_Release(heap);
    ok(!refcount, "ID3D12Heap has %u references left.\n", (unsigned int)refcount);

    memset(&architecture, 0, sizeof(architecture));
    hr = ID3D12Device_CheckFeatureSupport(device, D3D12_FEATURE_ARCHITECTURE, &architecture, sizeof(architecture));
    ok(hr == S_OK, "Got unexpected hr %#x.\n", hr);
    for (i = D3D12_HEAP_TYPE_DEFAULT; i < D3D12_HEAP_TYPE_CUSTOM; ++i)
    {
        vkd3d_test_set_context("Test %u\n", i);
        desc.Properties = ID3D12Device_GetCustomHeapProperties(device, 1, i);
        ok(desc.Properties.Type == D3D12_HEAP_TYPE_CUSTOM, "Got unexpected heap type %#x.\n", desc.Properties.Type);

        switch (i)
        {
            case D3D12_HEAP_TYPE_DEFAULT:
                ok(desc.Properties.CPUPageProperty == D3D12_CPU_PAGE_PROPERTY_NOT_AVAILABLE,
                        "Got unexpected CPUPageProperty %#x.\n", desc.Properties.CPUPageProperty);
                ok(desc.Properties.MemoryPoolPreference == (architecture.UMA
                        ? D3D12_MEMORY_POOL_L0 : D3D12_MEMORY_POOL_L1),
                        "Got unexpected MemoryPoolPreference %#x.\n", desc.Properties.MemoryPoolPreference);
                break;

            case D3D12_HEAP_TYPE_UPLOAD:
                ok(desc.Properties.CPUPageProperty == (architecture.CacheCoherentUMA
                        ? D3D12_CPU_PAGE_PROPERTY_WRITE_BACK : D3D12_CPU_PAGE_PROPERTY_WRITE_COMBINE),
                        "Got unexpected CPUPageProperty %#x.\n", desc.Properties.CPUPageProperty);
                ok(desc.Properties.MemoryPoolPreference == D3D12_MEMORY_POOL_L0,
                        "Got unexpected MemoryPoolPreference %#x.\n", desc.Properties.MemoryPoolPreference);
                break;

            case D3D12_HEAP_TYPE_READBACK:
                ok(desc.Properties.CPUPageProperty == D3D12_CPU_PAGE_PROPERTY_WRITE_BACK,
                        "Got unexpected CPUPageProperty %#x.\n", desc.Properties.CPUPageProperty);
                ok(desc.Properties.MemoryPoolPreference == D3D12_MEMORY_POOL_L0,
                        "Got unexpected MemoryPoolPreference %#x.\n", desc.Properties.MemoryPoolPreference);
                break;

            default:
              ok(0, "Invalid heap type %#x.\n", i);
              continue;
        }

        hr = ID3D12Device_CreateHeap(device, &desc, &IID_ID3D12Heap, (void **)&heap);
        ok(hr == S_OK, "Got unexpected hr %#x.\n", hr);
        result_desc = ID3D12Heap_GetDesc(heap);
        check_heap_desc(&result_desc, &desc);
        ID3D12Heap_Release(heap);
    }
    vkd3d_test_set_context(NULL);

    is_pool_L1_supported = is_memory_pool_L1_supported(device);
    desc.Properties.Type = D3D12_HEAP_TYPE_CUSTOM;
    desc.Properties.CreationNodeMask = 1;
    desc.Properties.VisibleNodeMask = 1;
    for (i = 0; i < ARRAY_SIZE(custom_tests); ++i)
    {
        vkd3d_test_set_context("Test %u", i);

        desc.Properties.CPUPageProperty = custom_tests[i].page_property;
        desc.Properties.MemoryPoolPreference = custom_tests[i].pool_preference;
        hr = ID3D12Device_CreateHeap(device, &desc, &IID_ID3D12Heap, (void **)&heap);
        expected_hr = (custom_tests[i].pool_preference != D3D12_MEMORY_POOL_L1 || is_pool_L1_supported) ? custom_tests[i].expected_hr : E_INVALIDARG;
        ok(hr == expected_hr, "Test %u, page_property %u, pool_preference %u: Got hr %#x, expected %#x.\n",
                i, custom_tests[i].page_property, custom_tests[i].pool_preference, hr, expected_hr);
        if (FAILED(hr))
            continue;

        result_desc = ID3D12Heap_GetDesc(heap);
        check_heap_desc(&result_desc, &desc);

        refcount = ID3D12Heap_Release(heap);
        ok(!refcount, "ID3D12Heap has %u references left.\n", (unsigned int)refcount);
    }
    vkd3d_test_set_context(NULL);

done:
    refcount = ID3D12Device_Release(device);
    ok(!refcount, "ID3D12Device has %u references left.\n", (unsigned int)refcount);
}

void test_create_placed_resource_size(void)
{
    D3D12_RESOURCE_ALLOCATION_INFO info;
    unsigned int mip_sizes[11], i;
    D3D12_HEAP_DESC heap_desc;
    D3D12_RESOURCE_DESC desc;
    ID3D12Resource *resource;
    ID3D12Device *device;
    ID3D12Heap *heap;
    HRESULT hr;

    if (!(device = create_device()))
    {
        skip("Failed to create device.\n");
        return;
    }

    memset(&desc, 0, sizeof(desc));
    desc.Format = DXGI_FORMAT_R11G11B10_FLOAT;
    desc.DepthOrArraySize = 1;
    desc.Width = 540;
    desc.Height = 540;
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS | D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

    for (i = 1; i < ARRAY_SIZE(mip_sizes); i++)
    {
        desc.MipLevels = i;
        info = ID3D12Device_GetResourceAllocationInfo(device, 0, 1, &desc);
        mip_sizes[i] = info.SizeInBytes;
#if 0
        /* RADV fails this check, but native driver does not.
         * It is probably legal for a driver to have non-monotonic resource sizes here. */
        if (i > 1)
            ok(mip_sizes[i] >= mip_sizes[i - 1], "Resource size is not monotonically increasing (%u < %u).\n", mip_sizes[i], mip_sizes[i - 1]);
#endif
    }

    memset(&heap_desc, 0, sizeof(heap_desc));
    heap_desc.Alignment = 0;
    heap_desc.Properties.Type = D3D12_HEAP_TYPE_DEFAULT;
    heap_desc.SizeInBytes = mip_sizes[ARRAY_SIZE(mip_sizes) - 1];
    hr = ID3D12Device_CreateHeap(device, &heap_desc, &IID_ID3D12Heap, (void **)&heap);
    ok(SUCCEEDED(hr), "Failed to create heap, hr #%x.\n", hr);

    hr = ID3D12Device_CreatePlacedResource(device, heap, 0, &desc, D3D12_RESOURCE_STATE_RENDER_TARGET, NULL, &IID_ID3D12Resource, (void **)&resource);
    ok(SUCCEEDED(hr), "Failed to create resource, hr #%x.\n", hr);

    ID3D12Resource_Release(resource);
    ID3D12Heap_Release(heap);

    heap_desc.SizeInBytes = 64 * 1024;
    hr = ID3D12Device_CreateHeap(device, &heap_desc, &IID_ID3D12Heap, (void **)&heap);
    ok(SUCCEEDED(hr), "Failed to create heap, hr #%x.\n", hr);

    /* Runtime validates range, this must fail. */
    hr = ID3D12Device_CreatePlacedResource(device, heap, 0, &desc, D3D12_RESOURCE_STATE_RENDER_TARGET, NULL, &IID_ID3D12Resource, (void **)&resource);
    ok(hr == E_INVALIDARG, "Unexpected result, hr #%x.\n", hr);

    ID3D12Heap_Release(heap);
    ID3D12Device_Release(device);
}

void test_create_placed_resource(void)
{
    D3D12_GPU_VIRTUAL_ADDRESS gpu_address;
    D3D12_RESOURCE_DESC resource_desc;
    ID3D12Device *device, *tmp_device;
    D3D12_CLEAR_VALUE clear_value;
    D3D12_RESOURCE_STATES state;
    D3D12_HEAP_DESC heap_desc;
    ID3D12Resource *resource;
    ID3D12Heap *heap;
    unsigned int i;
    ULONG refcount;
    HRESULT hr;

    static const struct
    {
        D3D12_HEAP_TYPE heap_type;
        D3D12_RESOURCE_FLAGS flags;
    }
    invalid_buffer_desc_tests[] =
    {
        /* Render target or unordered access resources are not allowed with UPLOAD or READBACK. */
        {D3D12_HEAP_TYPE_UPLOAD,   D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET},
        {D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET},
        {D3D12_HEAP_TYPE_UPLOAD,   D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS},
        {D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS},
    };

    if (!(device = create_device()))
    {
        skip("Failed to create device.\n");
        return;
    }

    heap_desc.SizeInBytes = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
    memset(&heap_desc.Properties, 0, sizeof(heap_desc.Properties));
    heap_desc.Properties.Type = D3D12_HEAP_TYPE_DEFAULT;
    heap_desc.Alignment = 0;
    heap_desc.Flags = D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS;
    hr = ID3D12Device_CreateHeap(device, &heap_desc, &IID_ID3D12Heap, (void **)&heap);
    ok(hr == S_OK, "Failed to create heap, hr %#x.\n", hr);

    resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    resource_desc.Alignment = 0;
    resource_desc.Width = 32;
    resource_desc.Height = 1;
    resource_desc.DepthOrArraySize = 1;
    resource_desc.MipLevels = 1;
    resource_desc.Format = DXGI_FORMAT_UNKNOWN;
    resource_desc.SampleDesc.Count = 1;
    resource_desc.SampleDesc.Quality = 0;
    resource_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    resource_desc.Flags = 0;

    clear_value.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    clear_value.Color[0] = 1.0f;
    clear_value.Color[1] = 0.0f;
    clear_value.Color[2] = 0.0f;
    clear_value.Color[3] = 1.0f;

    refcount = get_refcount(heap);
    ok(refcount == 1, "Got unexpected refcount %u.\n", (unsigned int)refcount);

    hr = ID3D12Device_CreatePlacedResource(device, heap, 0,
            &resource_desc, D3D12_RESOURCE_STATE_COMMON, NULL,
            &IID_ID3D12Resource, (void **)&resource);
    ok(hr == S_OK, "Failed to create placed resource, hr %#x.\n", hr);

    refcount = get_refcount(heap);
    ok(refcount == 1, "Got unexpected refcount %u.\n", (unsigned int)refcount);

    refcount = get_refcount(device);
    ok(refcount == 3, "Got unexpected refcount %u.\n", (unsigned int)refcount);
    hr = ID3D12Resource_GetDevice(resource, &IID_ID3D12Device, (void **)&tmp_device);
    ok(hr == S_OK, "Failed to get device, hr %#x.\n", hr);
    refcount = get_refcount(device);
    ok(refcount == 4, "Got unexpected refcount %u.\n", (unsigned int)refcount);
    refcount = ID3D12Device_Release(tmp_device);
    ok(refcount == 3, "Got unexpected refcount %u.\n", (unsigned int)refcount);

    check_interface(resource, &IID_ID3D12Object, true);
    check_interface(resource, &IID_ID3D12DeviceChild, true);
    check_interface(resource, &IID_ID3D12Pageable, true);
    check_interface(resource, &IID_ID3D12Resource, true);

    gpu_address = ID3D12Resource_GetGPUVirtualAddress(resource);
    ok(gpu_address, "Got unexpected GPU virtual address %#"PRIx64".\n", gpu_address);

    refcount = ID3D12Resource_Release(resource);
    ok(!refcount, "ID3D12Resource has %u references left.\n", (unsigned int)refcount);

    /* The clear value must be NULL for buffers. */
    hr = ID3D12Device_CreatePlacedResource(device, heap, 0,
            &resource_desc, D3D12_RESOURCE_STATE_COMMON, &clear_value,
            &IID_ID3D12Resource, (void **)&resource);
    ok(hr == E_INVALIDARG, "Got unexpected hr %#x.\n", hr);

    /* Textures are disallowed on ALLOW_ONLY_HEAPS */
    resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    resource_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    resource_desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;

    hr = ID3D12Device_CreatePlacedResource(device, heap, 0,
            &resource_desc, D3D12_RESOURCE_STATE_COMMON, &clear_value,
            &IID_ID3D12Resource, (void **)&resource);
    ok(hr == E_INVALIDARG, "Got unexpected hr %#x.\n", hr);

    ID3D12Heap_Release(heap);

    for (i = 0; i < ARRAY_SIZE(invalid_buffer_desc_tests); ++i)
    {
        heap_desc.SizeInBytes = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
        memset(&heap_desc.Properties, 0, sizeof(heap_desc.Properties));
        heap_desc.Properties.Type = invalid_buffer_desc_tests[i].heap_type;
        heap_desc.Alignment = 0;
        heap_desc.Flags = D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS;
        hr = ID3D12Device_CreateHeap(device, &heap_desc, &IID_ID3D12Heap, (void **)&heap);
        ok(hr == S_OK, "Test %u: Failed to create heap, hr %#x.\n", i, hr);

        resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        resource_desc.Alignment = 0;
        resource_desc.Width = 32;
        resource_desc.Height = 1;
        resource_desc.DepthOrArraySize = 1;
        resource_desc.MipLevels = 1;
        resource_desc.Format = DXGI_FORMAT_UNKNOWN;
        resource_desc.SampleDesc.Count = 1;
        resource_desc.SampleDesc.Quality = 0;
        resource_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        resource_desc.Flags = invalid_buffer_desc_tests[i].flags;

        if (invalid_buffer_desc_tests[i].heap_type == D3D12_HEAP_TYPE_UPLOAD)
            state = D3D12_RESOURCE_STATE_GENERIC_READ;
        else
            state = D3D12_RESOURCE_STATE_COPY_DEST;

        hr = ID3D12Device_CreatePlacedResource(device, heap, 0,
                &resource_desc, state, &clear_value, &IID_ID3D12Resource, (void **)&resource);
        ok(hr == E_INVALIDARG, "Test %u: Got unexpected hr %#x.\n", i, hr);

        ID3D12Heap_Release(heap);
    }

    refcount = ID3D12Device_Release(device);
    ok(!refcount, "ID3D12Device has %u references left.\n", (unsigned int)refcount);
}

void test_create_reserved_resource(void)
{
    D3D12_GPU_VIRTUAL_ADDRESS gpu_address;
    D3D12_HEAP_PROPERTIES heap_properties;
    D3D12_RESOURCE_DESC resource_desc;
    D3D12_CLEAR_VALUE clear_value;
    D3D12_HEAP_FLAGS heap_flags;
    ID3D12Resource *resource;
    bool standard_swizzle;
    ID3D12Device *device;
    ULONG refcount;
    HRESULT hr;
    void *ptr;

    if (!(device = create_device()))
    {
        skip("Failed to create device.\n");
        return;
    }

    if (get_tiled_resources_tier(device) == D3D12_TILED_RESOURCES_TIER_NOT_SUPPORTED)
    {
        skip("Tiled resources are not supported.\n");
        goto done;
    }

    standard_swizzle = is_standard_swizzle_64kb_supported(device);
    trace("Standard swizzle 64KB: %#x.\n", standard_swizzle);

    resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    resource_desc.Alignment = 0;
    resource_desc.Width = 32;
    resource_desc.Height = 1;
    resource_desc.DepthOrArraySize = 1;
    resource_desc.MipLevels = 1;
    resource_desc.Format = DXGI_FORMAT_UNKNOWN;
    resource_desc.SampleDesc.Count = 1;
    resource_desc.SampleDesc.Quality = 0;
    resource_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    resource_desc.Flags = 0;

    hr = ID3D12Device_CreateReservedResource(device,
            &resource_desc, D3D12_RESOURCE_STATE_COMMON, NULL,
            &IID_ID3D12Resource, (void **)&resource);
    ok(hr == S_OK, "Failed to create reserved resource, hr %#x.\n", hr);

    check_interface(resource, &IID_ID3D12Object, true);
    check_interface(resource, &IID_ID3D12DeviceChild, true);
    check_interface(resource, &IID_ID3D12Pageable, true);
    check_interface(resource, &IID_ID3D12Resource, true);

    gpu_address = ID3D12Resource_GetGPUVirtualAddress(resource);
    ok(gpu_address, "Got unexpected GPU virtual address %#"PRIx64".\n", gpu_address);

    heap_flags = 0xdeadbeef;
    hr = ID3D12Resource_GetHeapProperties(resource, &heap_properties, &heap_flags);
    ok(hr == E_INVALIDARG, "Got unexpected hr %#x.\n", hr);
    ok(heap_flags == 0xdeadbeef, "Got unexpected heap flags %#x.\n", heap_flags);

    /* Map() is not allowed on reserved resources */
    hr = ID3D12Resource_Map(resource, 0, NULL, &ptr);
    ok(hr == E_INVALIDARG, "Got unexpected hr %#x.\n", hr);

    refcount = ID3D12Resource_Release(resource);
    ok(!refcount, "ID3D12Resource has %u references left.\n", (unsigned int)refcount);

    /* The clear value must be NULL for buffers. */
    clear_value.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    clear_value.Color[0] = 1.0f;
    clear_value.Color[1] = 0.0f;
    clear_value.Color[2] = 0.0f;
    clear_value.Color[3] = 1.0f;

    hr = ID3D12Device_CreateReservedResource(device,
            &resource_desc, D3D12_RESOURCE_STATE_COMMON, &clear_value,
            &IID_ID3D12Resource, (void **)&resource);
    ok(hr == E_INVALIDARG, "Got unexpected hr %#x.\n", hr);

    /* D3D12_TEXTURE_LAYOUT_ROW_MAJOR must be used for buffers. */
    resource_desc.Layout = D3D12_TEXTURE_LAYOUT_64KB_UNDEFINED_SWIZZLE;
    hr = ID3D12Device_CreateReservedResource(device,
            &resource_desc, D3D12_RESOURCE_STATE_COMMON, NULL,
            &IID_ID3D12Resource, (void **)&resource);
    ok(hr == E_INVALIDARG, "Got unexpected hr %#x.\n", hr);

    resource_desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    hr = ID3D12Device_CreateReservedResource(device,
            &resource_desc, D3D12_RESOURCE_STATE_COMMON, NULL,
            &IID_ID3D12Resource, (void **)&resource);
    ok(hr == E_INVALIDARG, "Got unexpected hr %#x.\n", hr);

    /* D3D12_TEXTURE_LAYOUT_64KB_UNDEFINED_SWIZZLE must be used for textures. */
    resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    resource_desc.Alignment = 0;
    resource_desc.Width = 64;
    resource_desc.Height = 64;
    resource_desc.DepthOrArraySize = 1;
    resource_desc.MipLevels = 4;
    resource_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    resource_desc.SampleDesc.Count = 1;
    resource_desc.SampleDesc.Quality = 0;
    resource_desc.Layout = D3D12_TEXTURE_LAYOUT_64KB_UNDEFINED_SWIZZLE;
    resource_desc.Flags = 0;

    hr = ID3D12Device_CreateReservedResource(device,
            &resource_desc, D3D12_RESOURCE_STATE_COMMON, NULL,
            &IID_ID3D12Resource, (void **)&resource);
    ok(hr == S_OK, "Failed to create reserved resource, hr %#x.\n", hr);
    refcount = ID3D12Resource_Release(resource);
    ok(!refcount, "ID3D12Resource has %u references left.\n", (unsigned int)refcount);

    resource_desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    hr = ID3D12Device_CreateReservedResource(device,
            &resource_desc, D3D12_RESOURCE_STATE_COMMON, NULL,
            &IID_ID3D12Resource, (void **)&resource);
    ok(hr == E_INVALIDARG, "Got unexpected hr %#x.\n", hr);

    resource_desc.MipLevels = 1;
    resource_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    hr = ID3D12Device_CreateReservedResource(device,
            &resource_desc, D3D12_RESOURCE_STATE_COMMON, NULL,
            &IID_ID3D12Resource, (void **)&resource);
    ok(hr == E_INVALIDARG, "Got unexpected hr %#x.\n", hr);

    resource_desc.Layout = D3D12_TEXTURE_LAYOUT_64KB_STANDARD_SWIZZLE;
    hr = ID3D12Device_CreateReservedResource(device,
            &resource_desc, D3D12_RESOURCE_STATE_COMMON, NULL,
            &IID_ID3D12Resource, (void **)&resource);
    ok(hr == (standard_swizzle ? S_OK : E_INVALIDARG) || broken(use_warp_device), "Got unexpected hr %#x.\n", hr);
    if (SUCCEEDED(hr))
        ID3D12Resource_Release(resource);

    /* Depth-Stencil formats not allowed */
    resource_desc.Format = DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
    resource_desc.Layout = D3D12_TEXTURE_LAYOUT_64KB_UNDEFINED_SWIZZLE;
    hr = ID3D12Device_CreateReservedResource(device,
        &resource_desc, D3D12_RESOURCE_STATE_COMMON, NULL,
        &IID_ID3D12Resource, (void **)&resource);
    ok(hr == E_INVALIDARG, "Got unexpected hr %#x.\n", hr);
    if (SUCCEEDED(hr))
        ID3D12Resource_Release(resource);

    /* More than one layer not allowed if some mips may be packed */
    resource_desc.Format = DXGI_FORMAT_R8G8B8A8_UINT;
    resource_desc.DepthOrArraySize = 4;
    resource_desc.MipLevels = 10;
    hr = ID3D12Device_CreateReservedResource(device,
        &resource_desc, D3D12_RESOURCE_STATE_COMMON, NULL,
        &IID_ID3D12Resource, (void **)&resource);
    ok(hr == E_INVALIDARG, "Got unexpected hr %#x.\n", hr);
    if (SUCCEEDED(hr))
        ID3D12Resource_Release(resource);

    /* 1D not allowed */
    resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE1D;
    resource_desc.Format = DXGI_FORMAT_R8G8B8A8_UINT;
    resource_desc.Height = 1;
    resource_desc.DepthOrArraySize = 1;
    resource_desc.MipLevels = 1;
    hr = ID3D12Device_CreateReservedResource(device,
        &resource_desc, D3D12_RESOURCE_STATE_COMMON, NULL,
        &IID_ID3D12Resource, (void **)&resource);
    ok(hr == E_INVALIDARG, "Got unexpected hr %#x.\n", hr);
    if (SUCCEEDED(hr))
        ID3D12Resource_Release(resource);

done:
    refcount = ID3D12Device_Release(device);
    ok(!refcount, "ID3D12Device has %u references left.\n", (unsigned int)refcount);
}

void test_map_resource(void)
{
    D3D12_HEAP_PROPERTIES heap_properties;
    D3D12_RESOURCE_DESC resource_desc;
    ID3D12Resource *resource;
    ID3D12Device *device;
    ULONG refcount;
    void *data;
    HRESULT hr;

    if (!(device = create_device()))
    {
        skip("Failed to create device.\n");
        return;
    }

    resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    resource_desc.Alignment = 0;
    resource_desc.Width = 32;
    resource_desc.Height = 32;
    resource_desc.DepthOrArraySize = 1;
    resource_desc.MipLevels = 1;
    resource_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    resource_desc.SampleDesc.Count = 1;
    resource_desc.SampleDesc.Quality = 0;
    resource_desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    resource_desc.Flags = 0;

    memset(&heap_properties, 0, sizeof(heap_properties));
    heap_properties.Type = D3D12_HEAP_TYPE_DEFAULT;
    hr = ID3D12Device_CreateCommittedResource(device, &heap_properties, D3D12_HEAP_FLAG_NONE,
            &resource_desc, D3D12_RESOURCE_STATE_COMMON, NULL,
            &IID_ID3D12Resource, (void **)&resource);
    ok(hr == S_OK, "Failed to create texture, hr %#x.\n", hr);

    /* Resources on a DEFAULT heap cannot be mapped. */
    data = (void *)(uintptr_t)0xdeadbeef;
    hr = ID3D12Resource_Map(resource, 0, NULL, &data);
    ok(hr == E_INVALIDARG, "Got unexpected hr %#x.\n", hr);
    ok(data == (void *)(uintptr_t)0xdeadbeef, "Pointer was modified %p.\n", data);

    ID3D12Resource_Release(resource);

    heap_properties.Type = D3D12_HEAP_TYPE_CUSTOM;
    heap_properties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_WRITE_COMBINE;
    heap_properties.MemoryPoolPreference = D3D12_MEMORY_POOL_L0;
    hr = ID3D12Device_CreateCommittedResource(device, &heap_properties, D3D12_HEAP_FLAG_NONE,
            &resource_desc, D3D12_RESOURCE_STATE_COMMON, NULL,
            &IID_ID3D12Resource, (void **)&resource);
    if (FAILED(hr))
    {
        skip("Failed to create texture on custom heap.\n");
    }
    else
    {
        /* The data pointer must be NULL for the UNKNOWN layout. */
        data = (void *)(uintptr_t)0xdeadbeef;
        hr = ID3D12Resource_Map(resource, 0, NULL, &data);
        ok(hr == E_INVALIDARG, "Got unexpected hr %#x.\n", hr);
        ok(data == (void *)(uintptr_t)0xdeadbeef, "Pointer was modified %p.\n", data);

        /* Texture on custom heaps can be mapped, but the address doesn't get disclosed to applications */
        hr = ID3D12Resource_Map(resource, 0, NULL, NULL);
        todo ok(hr == S_OK, "Got unexpected hr %#x.\n", hr);
        ID3D12Resource_Unmap(resource, 0, NULL);

        ID3D12Resource_Release(resource);
    }

    resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    resource_desc.Height = 1;
    resource_desc.Format = DXGI_FORMAT_UNKNOWN;
    resource_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    memset(&heap_properties, 0, sizeof(heap_properties));
    heap_properties.Type = D3D12_HEAP_TYPE_DEFAULT;
    hr = ID3D12Device_CreateCommittedResource(device, &heap_properties, D3D12_HEAP_FLAG_NONE,
            &resource_desc, D3D12_RESOURCE_STATE_COMMON, NULL,
            &IID_ID3D12Resource, (void **)&resource);
    ok(hr == S_OK, "Failed to create committed resource, hr %#x.\n", hr);

    /* Resources on a DEFAULT heap cannot be mapped. */
    data = (void *)(uintptr_t)0xdeadbeef;
    hr = ID3D12Resource_Map(resource, 0, NULL, &data);
    ok(hr == E_INVALIDARG, "Got unexpected hr %#x.\n", hr);
    ok(data == (void *)(uintptr_t)0xdeadbeef, "Pointer was modified %p.\n", data);

    ID3D12Resource_Release(resource);

    heap_properties.Type = D3D12_HEAP_TYPE_UPLOAD;
    hr = ID3D12Device_CreateCommittedResource(device, &heap_properties, D3D12_HEAP_FLAG_NONE,
            &resource_desc, D3D12_RESOURCE_STATE_GENERIC_READ, NULL,
            &IID_ID3D12Resource, (void **)&resource);
    ok(hr == S_OK, "Failed to create committed resource, hr %#x.\n", hr);

    data = NULL;
    hr = ID3D12Resource_Map(resource, 0, NULL, &data);
    ok(hr == S_OK, "Got unexpected hr %#x.\n", hr);
    ok(data, "Got NULL pointer.\n");
    ID3D12Resource_Unmap(resource, 0, NULL);

    data = (void *)(uintptr_t)0xdeadbeef;
    hr = ID3D12Resource_Map(resource, 1, NULL, &data);
    ok(hr == E_INVALIDARG, "Got unexpected hr %#x.\n", hr);
    ok(data == (void *)(uintptr_t)0xdeadbeef, "Pointer was modified %p.\n", data);

    data = NULL;
    hr = ID3D12Resource_Map(resource, 0, NULL, &data);
    ok(hr == S_OK, "Got unexpected hr %#x.\n", hr);
    ok(data, "Got NULL pointer.\n");
    ID3D12Resource_Unmap(resource, 1, NULL);
    ID3D12Resource_Unmap(resource, 0, NULL);

    /* Passing NULL to Map should map, but not disclose the CPU VA to caller. */
    hr = ID3D12Resource_Map(resource, 0, NULL, NULL);
    ok(hr == S_OK, "Got unexpected hr %#x.\n", hr);
    ID3D12Resource_Unmap(resource, 0, NULL);

    ID3D12Resource_Release(resource);

    refcount = ID3D12Device_Release(device);
    ok(!refcount, "ID3D12Device has %u references left.\n", (unsigned int)refcount);
}

void test_map_placed_resources(void)
{
    D3D12_ROOT_SIGNATURE_DESC root_signature_desc;
    ID3D12GraphicsCommandList *command_list;
    ID3D12Heap *upload_heap, *readback_heap;
    D3D12_ROOT_PARAMETER root_parameters[2];
    D3D12_RESOURCE_DESC resource_desc;
    ID3D12Resource *readback_buffer;
    struct test_context_desc desc;
    struct resource_readback rb;
    struct test_context context;
    ID3D12Resource *uav_buffer;
    D3D12_HEAP_DESC heap_desc;
    ID3D12CommandQueue *queue;
    ID3D12Resource *cb[4];
    uint32_t *cb_data[4];
    ID3D12Device *device;
    D3D12_RANGE range;
    unsigned int i;
    uint32_t *ptr;
    HRESULT hr;

    STATIC_ASSERT(ARRAY_SIZE(cb) == ARRAY_SIZE(cb_data));

    static const DWORD ps_code[] =
    {
#if 0
        uint offset;
        uint value;

        RWByteAddressBuffer u;

        void main()
        {
            u.Store(offset, value);
        }
#endif
        0x43425844, 0x0dcbdd90, 0x7dad2857, 0x4ee149ee, 0x72a13d21, 0x00000001, 0x000000a4, 0x00000003,
        0x0000002c, 0x0000003c, 0x0000004c, 0x4e475349, 0x00000008, 0x00000000, 0x00000008, 0x4e47534f,
        0x00000008, 0x00000000, 0x00000008, 0x58454853, 0x00000050, 0x00000050, 0x00000014, 0x0100086a,
        0x04000059, 0x00208e46, 0x00000000, 0x00000001, 0x0300009d, 0x0011e000, 0x00000000, 0x090000a6,
        0x0011e012, 0x00000000, 0x0020800a, 0x00000000, 0x00000000, 0x0020801a, 0x00000000, 0x00000000,
        0x0100003e,
    };
    static const D3D12_SHADER_BYTECODE ps = {ps_code, sizeof(ps_code)};
    static const uint32_t expected_values[] = {0xdead, 0xbeef, 0xfeed, 0xc0de};

    memset(&desc, 0, sizeof(desc));
    desc.no_root_signature = true;
    if (!init_test_context(&context, &desc))
        return;
    device = context.device;
    command_list = context.list;
    queue = context.queue;

    root_parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    root_parameters[0].Descriptor.ShaderRegister = 0;
    root_parameters[0].Descriptor.RegisterSpace = 0;
    root_parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    root_parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    root_parameters[1].Descriptor.ShaderRegister = 0;
    root_parameters[1].Descriptor.RegisterSpace = 0;
    root_parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    root_signature_desc.NumParameters = ARRAY_SIZE(root_parameters);
    root_signature_desc.pParameters = root_parameters;
    root_signature_desc.NumStaticSamplers = 0;
    root_signature_desc.pStaticSamplers = NULL;
    root_signature_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
    hr = create_root_signature(device, &root_signature_desc, &context.root_signature);
    ok(hr == S_OK, "Failed to create root signature, hr %#x.\n", hr);

    context.pipeline_state = create_pipeline_state(device, context.root_signature, 0, NULL, &ps, NULL);

    heap_desc.SizeInBytes = ARRAY_SIZE(cb) * D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
    memset(&heap_desc.Properties, 0, sizeof(heap_desc.Properties));
    heap_desc.Properties.Type = D3D12_HEAP_TYPE_UPLOAD;
    heap_desc.Alignment = 0;
    heap_desc.Flags = D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS;
    hr = ID3D12Device_CreateHeap(device, &heap_desc, &IID_ID3D12Heap, (void **)&upload_heap);
    ok(hr == S_OK, "Failed to create heap, hr %#x.\n", hr);

    heap_desc.SizeInBytes = 1024;
    heap_desc.Properties.Type = D3D12_HEAP_TYPE_READBACK;
    hr = ID3D12Device_CreateHeap(device, &heap_desc, &IID_ID3D12Heap, (void **)&readback_heap);
    ok(hr == S_OK, "Failed to create heap, hr %#x.\n", hr);

    resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    resource_desc.Alignment = 0;
    resource_desc.Width = D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT;
    resource_desc.Height = 1;
    resource_desc.DepthOrArraySize = 1;
    resource_desc.MipLevels = 1;
    resource_desc.Format = DXGI_FORMAT_UNKNOWN;
    resource_desc.SampleDesc.Count = 1;
    resource_desc.SampleDesc.Quality = 0;
    resource_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    resource_desc.Flags = 0;

    for (i = 0; i < ARRAY_SIZE(cb); ++i)
    {
        hr = ID3D12Device_CreatePlacedResource(device, upload_heap,
                i * D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT,
                &resource_desc, D3D12_RESOURCE_STATE_GENERIC_READ, NULL,
                &IID_ID3D12Resource, (void **)&cb[i]);
        ok(hr == S_OK, "Failed to create placed resource %u, hr %#x.\n", i, hr);
    }

    resource_desc.Width = 1024;
    hr = ID3D12Device_CreatePlacedResource(device, readback_heap, 0,
            &resource_desc, D3D12_RESOURCE_STATE_COPY_DEST, NULL,
            &IID_ID3D12Resource, (void **)&readback_buffer);
    ok(hr == S_OK, "Failed to create placed resource, hr %#x.\n", hr);

    uav_buffer = create_default_buffer(device, resource_desc.Width,
            D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    for (i = 0; i < ARRAY_SIZE(cb); ++i)
    {
        hr = ID3D12Resource_Map(cb[i], 0, NULL, (void **)&cb_data[i]);
        ok(hr == S_OK, "Failed to map buffer %u, hr %#x.\n", i, hr);
    }

    hr = ID3D12Resource_Map(cb[0], 0, NULL, (void **)&ptr);
    ok(hr == S_OK, "Failed to map buffer, hr %#x.\n", hr);
    ok(ptr == cb_data[0], "Got map ptr %p, expected %p.\n", ptr, cb_data[0]);
    cb_data[0][0] = 0;
    cb_data[0][1] = expected_values[0];
    ID3D12Resource_Unmap(cb[0], 0, NULL);
    ID3D12Resource_Unmap(cb[0], 0, NULL);
    cb_data[0] = NULL;

    ID3D12GraphicsCommandList_IASetPrimitiveTopology(command_list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D12GraphicsCommandList_RSSetViewports(command_list, 1, &context.viewport);
    ID3D12GraphicsCommandList_RSSetScissorRects(command_list, 1, &context.scissor_rect);
    ID3D12GraphicsCommandList_SetGraphicsRootSignature(command_list, context.root_signature);
    ID3D12GraphicsCommandList_SetPipelineState(command_list, context.pipeline_state);
    ID3D12GraphicsCommandList_SetGraphicsRootUnorderedAccessView(command_list, 0,
            ID3D12Resource_GetGPUVirtualAddress(uav_buffer));

    ID3D12GraphicsCommandList_SetGraphicsRootConstantBufferView(command_list, 1,
            ID3D12Resource_GetGPUVirtualAddress(cb[0]));
    ID3D12GraphicsCommandList_DrawInstanced(command_list, 3, 1, 0, 0);

    ID3D12GraphicsCommandList_SetGraphicsRootConstantBufferView(command_list, 1,
            ID3D12Resource_GetGPUVirtualAddress(cb[2]));
    ID3D12GraphicsCommandList_DrawInstanced(command_list, 3, 1, 0, 0);
    cb_data[2][0] = 4;
    cb_data[2][1] = expected_values[1];

    ID3D12GraphicsCommandList_SetGraphicsRootConstantBufferView(command_list, 1,
            ID3D12Resource_GetGPUVirtualAddress(cb[1]));
    ID3D12GraphicsCommandList_DrawInstanced(command_list, 3, 1, 0, 0);
    cb_data[1][0] = 8;
    cb_data[1][1] = expected_values[2];

    ID3D12GraphicsCommandList_SetGraphicsRootConstantBufferView(command_list, 1,
            ID3D12Resource_GetGPUVirtualAddress(cb[3]));
    ID3D12GraphicsCommandList_DrawInstanced(command_list, 3, 1, 0, 0);
    cb_data[3][0] = 12;
    cb_data[3][1] = expected_values[3];
    range.Begin = 0;
    range.End = 2 * sizeof(uint32_t);
    ID3D12Resource_Unmap(cb[3], 0, &range);
    cb_data[3] = NULL;

    transition_resource_state(command_list, uav_buffer,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
    ID3D12GraphicsCommandList_CopyResource(command_list, readback_buffer, uav_buffer);

    get_buffer_readback_with_command_list(readback_buffer, DXGI_FORMAT_R32_UINT, &rb, queue, command_list);
    for (i = 0; i < ARRAY_SIZE(expected_values); ++i)
    {
        unsigned int value = get_readback_uint(&rb, i, 0, 0);
        ok(value == expected_values[i], "Got %#x, expected %#x at %u.\n", value, expected_values[i], i);
    }
    release_resource_readback(&rb);

    ID3D12Resource_Release(uav_buffer);
    ID3D12Resource_Release(readback_buffer);
    ID3D12Heap_Release(upload_heap);
    ID3D12Heap_Release(readback_heap);
    for (i = 0; i < ARRAY_SIZE(cb); ++i)
        ID3D12Resource_Release(cb[i]);
    destroy_test_context(&context);
}

#define check_copyable_footprints(a, b, c, d, e, f, g, h) \
        check_copyable_footprints_(__LINE__, a, b, c, d, e, f, g, h)
static void check_copyable_footprints_(unsigned int line, const D3D12_RESOURCE_DESC *desc,
        unsigned int sub_resource_idx, unsigned int sub_resource_count, uint64_t base_offset,
        const D3D12_PLACED_SUBRESOURCE_FOOTPRINT *layouts, const UINT *row_counts,
        const uint64_t *row_sizes, uint64_t *total_size)
{
    unsigned int miplevel, width, height, depth, row_count, row_size, row_pitch;
    uint64_t offset, size, total;
    unsigned int i;

    offset = total = 0;
    for (i = 0; i < sub_resource_count; ++i)
    {
        miplevel = (sub_resource_idx + i) % desc->MipLevels;
        width = align(max(1, desc->Width >> miplevel), format_block_width(desc->Format));
        height = align(max(1, desc->Height >> miplevel), format_block_height(desc->Format));
        depth = desc->Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D ? desc->DepthOrArraySize : 1;
        depth = max(1, depth >> miplevel);
        row_count = height / format_block_height(desc->Format);
        row_size = (width / format_block_width(desc->Format)) * format_size(desc->Format);
        row_pitch = align(row_size, D3D12_TEXTURE_DATA_PITCH_ALIGNMENT);

        if (layouts)
        {
            const D3D12_PLACED_SUBRESOURCE_FOOTPRINT *l = &layouts[i];
            const D3D12_SUBRESOURCE_FOOTPRINT *f = &l->Footprint;

            ok_(line)(l->Offset == base_offset + offset,
                    "Got offset %"PRIu64", expected %"PRIu64".\n", l->Offset, base_offset + offset);
            ok_(line)(f->Format == desc->Format, "Got format %#x, expected %#x.\n", f->Format, desc->Format);
            ok_(line)(f->Width == width, "Got width %u, expected %u.\n", f->Width, width);
            ok_(line)(f->Height == height, "Got height %u, expected %u.\n", f->Height, height);
            ok_(line)(f->Depth == depth, "Got depth %u, expected %u.\n", f->Depth, depth);
            ok_(line)(f->RowPitch == row_pitch, "Got row pitch %u, expected %u.\n", f->RowPitch, row_pitch);
        }

        if (row_counts)
            ok_(line)(row_counts[i] == row_count, "Got row count %u, expected %u.\n", row_counts[i], row_count);

        if (row_sizes)
            ok_(line)(row_sizes[i] == row_size, "Got row size %"PRIu64", expected %u.\n", row_sizes[i], row_size);

        size = max(0, row_count - 1) * row_pitch + row_size;
        size = max(0, depth - 1) * align(size, D3D12_TEXTURE_DATA_PITCH_ALIGNMENT) + size;

        total = offset + size;
        offset = align(total, D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT);
    }

    if (total_size)
        ok_(line)(*total_size == total, "Got total size %"PRIu64", expected %"PRIu64".\n", *total_size, total);
}

void test_get_copyable_footprints(void)
{
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT layouts[10];
    uint64_t row_sizes[10], total_size;
    D3D12_RESOURCE_DESC resource_desc;
    unsigned int sub_resource_count;
    unsigned int i, j, k, l;
    ID3D12Device *device;
    UINT row_counts[10];
    ULONG refcount;

    static const struct
    {
        D3D12_RESOURCE_DIMENSION dimension;
        unsigned int width;
        unsigned int height;
        unsigned int depth_or_array_size;
        unsigned int miplevel_count;
        bool test_with_compressed;
    }
    resources[] =
    {
        {D3D12_RESOURCE_DIMENSION_BUFFER, 4, 1, 1, 1, false},
        {D3D12_RESOURCE_DIMENSION_TEXTURE1D, 4, 1, 1, 1, false},
        {D3D12_RESOURCE_DIMENSION_TEXTURE1D, 4, 1, 1, 2, false},
        {D3D12_RESOURCE_DIMENSION_TEXTURE1D, 3, 1, 1, 1, false},
        {D3D12_RESOURCE_DIMENSION_TEXTURE1D, 4, 1, 2, 1, false},
        {D3D12_RESOURCE_DIMENSION_TEXTURE2D, 4, 4, 1, 1, true},
        {D3D12_RESOURCE_DIMENSION_TEXTURE2D, 4, 4, 2, 1, true},
        {D3D12_RESOURCE_DIMENSION_TEXTURE2D, 4, 4, 1, 2, true},
        {D3D12_RESOURCE_DIMENSION_TEXTURE2D, 3, 1, 1, 2, false},
        {D3D12_RESOURCE_DIMENSION_TEXTURE2D, 3, 2, 1, 2, false},
        {D3D12_RESOURCE_DIMENSION_TEXTURE2D, 3, 1, 1, 1, false},
        {D3D12_RESOURCE_DIMENSION_TEXTURE2D, 3, 2, 1, 1, false},
        {D3D12_RESOURCE_DIMENSION_TEXTURE3D, 4, 4, 1, 1, true},
        {D3D12_RESOURCE_DIMENSION_TEXTURE3D, 4, 4, 2, 1, true},
        {D3D12_RESOURCE_DIMENSION_TEXTURE3D, 4, 4, 2, 2, true},
        {D3D12_RESOURCE_DIMENSION_TEXTURE3D, 8, 8, 8, 4, true},
        {D3D12_RESOURCE_DIMENSION_TEXTURE3D, 3, 2, 2, 2, false},
    };
    static const struct
    {
        DXGI_FORMAT format;
        bool is_compressed;
    }
    formats[] =
    {
        {DXGI_FORMAT_R32G32B32A32_FLOAT, false},
        {DXGI_FORMAT_R32G32B32A32_UINT, false},
        {DXGI_FORMAT_R32_UINT, false},
        {DXGI_FORMAT_R8G8B8A8_UNORM, false},
        {DXGI_FORMAT_BC1_UNORM, true},
        {DXGI_FORMAT_BC2_UNORM, true},
        {DXGI_FORMAT_BC3_UNORM, true},
        {DXGI_FORMAT_BC4_UNORM, true},
        {DXGI_FORMAT_BC5_UNORM, true},
        {DXGI_FORMAT_BC6H_UF16, true},
        {DXGI_FORMAT_BC6H_SF16, true},
        {DXGI_FORMAT_BC7_UNORM, true},
    };
    static const uint64_t base_offsets[] =
    {
        0, 1, 2, 30, 255, 512, 513, 600, 4096, 4194304, ~(uint64_t)0,
    };
    static const struct
    {
        D3D12_RESOURCE_DESC resource_desc;
        unsigned int sub_resource_idx;
        unsigned int sub_resource_count;
    }
    invalid_descs[] =
    {
        {
            {D3D12_RESOURCE_DIMENSION_BUFFER, 0, 3, 2, 1, 1, DXGI_FORMAT_R32_UINT,
                {1, 0}, D3D12_TEXTURE_LAYOUT_UNKNOWN, D3D12_RESOURCE_FLAG_NONE}, 0, 1,
        },
        {
            {D3D12_RESOURCE_DIMENSION_TEXTURE1D, 0, 4, 2, 1, 1, DXGI_FORMAT_R32_UINT,
                {1, 0}, D3D12_TEXTURE_LAYOUT_UNKNOWN, D3D12_RESOURCE_FLAG_NONE}, 0, 1,
        },
        {
            {D3D12_RESOURCE_DIMENSION_TEXTURE2D, 0, 4, 4, 1, 1, DXGI_FORMAT_R32_UINT,
                {1, 0}, D3D12_TEXTURE_LAYOUT_UNKNOWN, D3D12_RESOURCE_FLAG_NONE}, 0, 2,
        },
        {
            {D3D12_RESOURCE_DIMENSION_TEXTURE2D, 0, 3, 1, 1, 2, DXGI_FORMAT_BC1_UNORM,
                {1, 0}, D3D12_TEXTURE_LAYOUT_UNKNOWN, D3D12_RESOURCE_FLAG_NONE}, 0, 2,
        },
        {
            {D3D12_RESOURCE_DIMENSION_TEXTURE2D, 0, 3, 1, 1, 1, DXGI_FORMAT_BC1_UNORM,
                {1, 0}, D3D12_TEXTURE_LAYOUT_UNKNOWN, D3D12_RESOURCE_FLAG_NONE}, 0, 1,
        },
        {
            {D3D12_RESOURCE_DIMENSION_TEXTURE2D, 0, 3, 1, 1, 2, DXGI_FORMAT_BC7_UNORM,
                {1, 0}, D3D12_TEXTURE_LAYOUT_UNKNOWN, D3D12_RESOURCE_FLAG_NONE}, 0, 2,
        },
        {
            {D3D12_RESOURCE_DIMENSION_TEXTURE2D, 0, 3, 1, 1, 1, DXGI_FORMAT_BC7_UNORM,
                {1, 0}, D3D12_TEXTURE_LAYOUT_UNKNOWN, D3D12_RESOURCE_FLAG_NONE}, 0, 1,
        },
        {
            {D3D12_RESOURCE_DIMENSION_TEXTURE3D, 3, 2, 2, 2, 2, DXGI_FORMAT_BC1_UNORM,
                {1, 0}, D3D12_TEXTURE_LAYOUT_UNKNOWN, D3D12_RESOURCE_FLAG_NONE}, 0, 1,
        },
    };

    if (!(device = create_device()))
    {
        skip("Failed to create device.\n");
        return;
    }

    for (i = 0; i < ARRAY_SIZE(resources); ++i)
    {
        const bool is_buffer = resources[i].dimension == D3D12_RESOURCE_DIMENSION_BUFFER;

        resource_desc.Dimension = resources[i].dimension;
        resource_desc.Alignment = 0;
        resource_desc.Width = resources[i].width;
        resource_desc.Height = resources[i].height;
        resource_desc.DepthOrArraySize = resources[i].depth_or_array_size;
        resource_desc.MipLevels = resources[i].miplevel_count;

        for (j = 0; j < ARRAY_SIZE(formats); ++j)
        {
            if (formats[j].is_compressed && !resources[i].test_with_compressed)
                continue;
            if (is_buffer && j > 0)
                continue;

            if (is_buffer)
                resource_desc.Format = DXGI_FORMAT_UNKNOWN;
            else
                resource_desc.Format = formats[j].format;

            resource_desc.SampleDesc.Count = 1;
            resource_desc.SampleDesc.Quality = 0;
            resource_desc.Layout = is_buffer ? D3D12_TEXTURE_LAYOUT_ROW_MAJOR : D3D12_TEXTURE_LAYOUT_UNKNOWN;
            resource_desc.Flags = D3D12_RESOURCE_FLAG_NONE;

            sub_resource_count = resource_desc.MipLevels;
            if (resources[i].dimension != D3D12_RESOURCE_DIMENSION_TEXTURE3D)
                sub_resource_count *= resource_desc.DepthOrArraySize;
            assert(sub_resource_count <= ARRAY_SIZE(layouts));

            for (k = 0; k < ARRAY_SIZE(base_offsets); ++k)
            {
                vkd3d_test_set_context("resource %u, format %#x, offset %#"PRIx64,
                        i, resource_desc.Format, base_offsets[k]);

                memset(layouts, 0, sizeof(layouts));
                memset(row_counts, 0, sizeof(row_counts));
                memset(row_sizes, 0, sizeof(row_sizes));
                total_size = 0;
                ID3D12Device_GetCopyableFootprints(device, &resource_desc, 0, sub_resource_count, base_offsets[k],
                        layouts, row_counts, row_sizes, &total_size);
                check_copyable_footprints(&resource_desc, 0, sub_resource_count, base_offsets[k],
                        layouts, row_counts, row_sizes, &total_size);

                memset(layouts, 0, sizeof(layouts));
                ID3D12Device_GetCopyableFootprints(device, &resource_desc, 0, sub_resource_count, base_offsets[k],
                        layouts, NULL, NULL, NULL);
                check_copyable_footprints(&resource_desc, 0, sub_resource_count, base_offsets[k],
                        layouts, NULL, NULL, NULL);
                memset(row_counts, 0, sizeof(row_counts));
                ID3D12Device_GetCopyableFootprints(device, &resource_desc, 0, sub_resource_count, base_offsets[k],
                        NULL, row_counts, NULL, NULL);
                check_copyable_footprints(&resource_desc, 0, sub_resource_count, base_offsets[k],
                        NULL, row_counts, NULL, NULL);
                memset(row_sizes, 0, sizeof(row_sizes));
                ID3D12Device_GetCopyableFootprints(device, &resource_desc, 0, sub_resource_count, base_offsets[k],
                        NULL, NULL, row_sizes, NULL);
                check_copyable_footprints(&resource_desc, 0, sub_resource_count, base_offsets[k],
                        NULL, NULL, row_sizes, NULL);
                total_size = 0;
                ID3D12Device_GetCopyableFootprints(device, &resource_desc, 0, sub_resource_count, base_offsets[k],
                        NULL, NULL, NULL, &total_size);
                check_copyable_footprints(&resource_desc, 0, sub_resource_count, base_offsets[k],
                        NULL, NULL, NULL, &total_size);

                for (l = 0; l < sub_resource_count; ++l)
                {
                    vkd3d_test_set_context("resource %u, format %#x, offset %#"PRIx64", sub-resource %u",
                            i, resource_desc.Format, base_offsets[k], l);

                    memset(layouts, 0, sizeof(layouts));
                    memset(row_counts, 0, sizeof(row_counts));
                    memset(row_sizes, 0, sizeof(row_sizes));
                    total_size = 0;
                    ID3D12Device_GetCopyableFootprints(device, &resource_desc, l, 1, base_offsets[k],
                            layouts, row_counts, row_sizes, &total_size);
                    check_copyable_footprints(&resource_desc, l, 1, base_offsets[k],
                            layouts, row_counts, row_sizes, &total_size);
                }
            }
        }
    }
    vkd3d_test_set_context(NULL);

    resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    resource_desc.Alignment = 0;
    resource_desc.Width = 512;
    resource_desc.Height = 512;
    resource_desc.DepthOrArraySize = 1;
    resource_desc.MipLevels = 1;
    resource_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    resource_desc.SampleDesc.Count = 4;
    resource_desc.SampleDesc.Quality = 0;
    resource_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    memset(layouts, 0, sizeof(layouts));
    memset(row_counts, 0, sizeof(row_counts));
    memset(row_sizes, 0, sizeof(row_sizes));
    total_size = 0;
    ID3D12Device_GetCopyableFootprints(device, &resource_desc, 0, 1, 0,
            layouts, row_counts, row_sizes, &total_size);
    check_copyable_footprints(&resource_desc, 0, 1, 0,
            layouts, row_counts, row_sizes, &total_size);

    for (i = 0; i < ARRAY_SIZE(invalid_descs); ++i)
    {
        resource_desc = invalid_descs[i].resource_desc;

        memset(layouts, 0, sizeof(layouts));
        memset(row_counts, 0, sizeof(row_counts));
        memset(row_sizes, 0, sizeof(row_sizes));
        total_size = 0;
        ID3D12Device_GetCopyableFootprints(device, &resource_desc,
                invalid_descs[i].sub_resource_idx, invalid_descs[i].sub_resource_count, 0,
                layouts, row_counts, row_sizes, &total_size);

        for (j = 0; j < invalid_descs[i].sub_resource_count; ++j)
        {
            const D3D12_PLACED_SUBRESOURCE_FOOTPRINT *l = &layouts[j];

            ok(l->Offset == ~(uint64_t)0, "Got offset %"PRIu64".\n", l->Offset);
            ok(l->Footprint.Format == ~(DXGI_FORMAT)0, "Got format %#x.\n", l->Footprint.Format);
            ok(l->Footprint.Width == ~0u, "Got width %u.\n", l->Footprint.Width);
            ok(l->Footprint.Height == ~0u, "Got height %u.\n", l->Footprint.Height);
            ok(l->Footprint.Depth == ~0u, "Got depth %u.\n", l->Footprint.Depth);
            ok(l->Footprint.RowPitch == ~0u, "Got row pitch %u.\n", l->Footprint.RowPitch);

            ok(row_counts[j] == ~0u, "Got row count %u.\n", row_counts[j]);
            ok(row_sizes[j] == ~(uint64_t)0, "Got row size %"PRIu64".\n", row_sizes[j]);
        }

        ok(total_size == ~(uint64_t)0, "Got total size %"PRIu64".\n", total_size);
    }

    refcount = ID3D12Device_Release(device);
    ok(!refcount, "ID3D12Device has %u references left.\n", (unsigned int)refcount);
}

void test_resource_allocation_info(void)
{
    D3D12_RESOURCE_ALLOCATION_INFO1 res_info[2];
    D3D12_RESOURCE_ALLOCATION_INFO info, info1;
    D3D12_RESOURCE_DESC desc[2];
    ID3D12Device4 *device4;
    ID3D12Device *device;
    unsigned int i, j;
    ULONG refcount;

    static const unsigned int alignments[] =
    {
        0,
        D3D12_SMALL_RESOURCE_PLACEMENT_ALIGNMENT,
        D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT,
        D3D12_DEFAULT_MSAA_RESOURCE_PLACEMENT_ALIGNMENT,
    };
    static const unsigned int buffer_sizes[] =
    {
        1,
        16,
        256,
        1024,
        D3D12_SMALL_RESOURCE_PLACEMENT_ALIGNMENT,
        D3D12_SMALL_RESOURCE_PLACEMENT_ALIGNMENT + 1,
        D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT,
        D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT + 1,
        D3D12_DEFAULT_MSAA_RESOURCE_PLACEMENT_ALIGNMENT,
        D3D12_DEFAULT_MSAA_RESOURCE_PLACEMENT_ALIGNMENT + 1,
    };
    static const struct
    {
        unsigned int width;
        unsigned int height;
        unsigned int array_size;
        unsigned int miplevels;
        DXGI_FORMAT format;
    }
    texture_tests[] =
    {
        { 4,  4, 1, 1, DXGI_FORMAT_R8_UINT},
        { 8,  8, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM},
        {16, 16, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM},
        {16, 16, 1024, 1, DXGI_FORMAT_R8G8B8A8_UNORM},
        {256, 512, 1, 10, DXGI_FORMAT_BC1_UNORM},
        {256, 512, 64, 1, DXGI_FORMAT_BC1_UNORM},

        {1024, 1024, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM},
        {1024, 1024, 1, 2, DXGI_FORMAT_R8G8B8A8_UNORM},
        {1024, 1024, 1, 3, DXGI_FORMAT_R8G8B8A8_UNORM},
        {1024, 1024, 1, 0, DXGI_FORMAT_R8G8B8A8_UNORM},
        {260, 512, 1, 1, DXGI_FORMAT_BC1_UNORM},
    };

    if (!(device = create_device()))
    {
        skip("Failed to create device.\n");
        return;
    }

    if (FAILED(ID3D12Device_QueryInterface(device, &IID_ID3D12Device4, (void**)&device4)))
        skip("GetResourceAllocationInfo1 not supported by device.\n");

    desc[0].Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc[0].Alignment = 0;
    desc[0].Width = 32;
    desc[0].Height = 1;
    desc[0].DepthOrArraySize = 1;
    desc[0].MipLevels = 1;
    desc[0].Format = DXGI_FORMAT_UNKNOWN;
    desc[0].SampleDesc.Count = 1;
    desc[0].SampleDesc.Quality = 0;
    desc[0].Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    desc[0].Flags = 0;

    desc[1] = desc[0];
    desc[1].Width = 120000;

    info = ID3D12Device_GetResourceAllocationInfo(device, 0, 2, &desc[0]);
    check_alignment(info.SizeInBytes, info.Alignment);

    if (device4)
    {
        uint64_t offset = 0;

        info1 = ID3D12Device4_GetResourceAllocationInfo1(device4, 0, 2, &desc[0], NULL);
        ok(info1.SizeInBytes == info.SizeInBytes, "Got unexpected size %"PRIu64".\n", info1.SizeInBytes);
        ok(info1.Alignment == info.Alignment, "Got unexpected alignment %"PRIu64".\n", info1.Alignment);

        info1 = ID3D12Device4_GetResourceAllocationInfo1(device4, 0, 2, &desc[0], &res_info[0]);
        ok(info1.SizeInBytes == info.SizeInBytes, "Got unexpected size %"PRIu64".\n", info1.SizeInBytes);
        ok(info1.Alignment == info.Alignment, "Got unexpected alignment %"PRIu64".\n", info1.Alignment);

        for (i = 0; i < 2; i++)
        {
            info = ID3D12Device_GetResourceAllocationInfo(device, 0, 1, &desc[i]);
            offset = align(offset, info.Alignment);

            ok(res_info[i].Offset == offset, "Got unexpected resource offset %"PRIu64".\n", res_info[i].Offset);
            ok(res_info[i].SizeInBytes == info.SizeInBytes, "Got unexpected resource size %"PRIu64".\n", res_info[i].SizeInBytes);
            ok(res_info[i].Alignment == info.Alignment, "Got unexpected resource alignment %"PRIu64".\n", res_info[i].Alignment);

            offset = res_info[i].Offset + res_info[i].SizeInBytes;
        }
    }

    for (i = 0; i < ARRAY_SIZE(alignments); ++i)
    {
        for (j = 0; j < ARRAY_SIZE(buffer_sizes); ++j)
        {
            desc[0].Alignment = alignments[i];
            desc[0].Width = buffer_sizes[j];
            info = ID3D12Device_GetResourceAllocationInfo(device, 0, 1, &desc[0]);
            if (!desc[0].Alignment || desc[0].Alignment == D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT)
            {
                check_alignment(info.SizeInBytes, info.Alignment);
            }
            else
            {
                ok(info.SizeInBytes == ~(uint64_t)0,
                        "Got unexpected size %"PRIu64".\n", info.SizeInBytes);
                ok(info.Alignment == D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT,
                        "Got unexpected alignment %"PRIu64".\n", info.Alignment);
            }
        }
    }

    desc[1].Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc[1].SampleDesc.Count = 1;
    desc[1].SampleDesc.Quality = 0;
    desc[1].Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    desc[1].Flags = 0;

    for (i = 0; i < ARRAY_SIZE(texture_tests); ++i)
    {
        desc[1].Width = texture_tests[i].width;
        desc[1].Height = texture_tests[i].height;
        desc[1].DepthOrArraySize = texture_tests[i].array_size;
        desc[1].MipLevels = texture_tests[i].miplevels;
        desc[1].Format = texture_tests[i].format;

        desc[1].Alignment = 0;
        info = ID3D12Device_GetResourceAllocationInfo(device, 0, 1, &desc[1]);
        ok(info.Alignment >= D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT,
                "Got unexpected alignment %"PRIu64".\n", info.Alignment);
        check_alignment(info.SizeInBytes, info.Alignment);

        desc[1].Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
        info = ID3D12Device_GetResourceAllocationInfo(device, 0, 1, &desc[1]);
        ok(info.Alignment >= D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT,
                "Got unexpected alignment %"PRIu64".\n", info.Alignment);
        check_alignment(info.SizeInBytes, info.Alignment);

        desc[1].Alignment = D3D12_SMALL_RESOURCE_PLACEMENT_ALIGNMENT;
        info = ID3D12Device_GetResourceAllocationInfo(device, 0, 1, &desc[1]);
        ok(info.Alignment >= D3D12_SMALL_RESOURCE_PLACEMENT_ALIGNMENT,
                "Got unexpected alignment %"PRIu64".\n", info.Alignment);
        if (i < 6)
        {
            check_alignment(info.SizeInBytes, info.Alignment);
        }
        else
        {
            ok(info.SizeInBytes == ~(uint64_t)0,
                    "Got unexpected size %"PRIu64".\n", info.SizeInBytes);
            ok(info.Alignment >= D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT,
                    "Got unexpected alignment %"PRIu64".\n", info.Alignment);
        }
    }

    if (device4)
        ID3D12Device4_Release(device4);

    refcount = ID3D12Device_Release(device);
    ok(!refcount, "ID3D12Device has %u references left.\n", (unsigned int)refcount);
}

void test_suballocate_small_textures(void)
{
    D3D12_GPU_VIRTUAL_ADDRESS gpu_address;
    D3D12_RESOURCE_ALLOCATION_INFO info;
    D3D12_RESOURCE_DESC resource_desc;
    ID3D12Resource *textures[10];
    D3D12_HEAP_DESC heap_desc;
    ID3D12Device *device;
    ID3D12Heap *heap;
    unsigned int i;
    ULONG refcount;
    HRESULT hr;

    if (!(device = create_device()))
    {
        skip("Failed to create device.\n");
        return;
    }

    resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    resource_desc.Alignment = 0;
    resource_desc.Width = 32;
    resource_desc.Height = 32;
    resource_desc.DepthOrArraySize = 1;
    resource_desc.MipLevels = 1;
    resource_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    resource_desc.SampleDesc.Count = 1;
    resource_desc.SampleDesc.Quality = 0;
    resource_desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    resource_desc.Flags = 0;

    resource_desc.Alignment = D3D12_SMALL_RESOURCE_PLACEMENT_ALIGNMENT;

    info = ID3D12Device_GetResourceAllocationInfo(device, 0, 1, &resource_desc);
    trace("Size %"PRIu64", alignment %"PRIu64".\n", info.SizeInBytes, info.Alignment);
    check_alignment(info.SizeInBytes, info.Alignment);
    if (info.Alignment != D3D12_SMALL_RESOURCE_PLACEMENT_ALIGNMENT)
    {
        resource_desc.Alignment = 0;
        info = ID3D12Device_GetResourceAllocationInfo(device, 0, 1, &resource_desc);
        trace("Size %"PRIu64", alignment %"PRIu64".\n", info.SizeInBytes, info.Alignment);
        check_alignment(info.SizeInBytes, info.Alignment);
    }

    ok(info.Alignment >= D3D12_SMALL_RESOURCE_PLACEMENT_ALIGNMENT, "Got alignment %"PRIu64".\n", info.Alignment);

    heap_desc.SizeInBytes = ARRAY_SIZE(textures) * info.SizeInBytes;
    memset(&heap_desc.Properties, 0, sizeof(heap_desc.Properties));
    heap_desc.Properties.Type = D3D12_HEAP_TYPE_DEFAULT;
    heap_desc.Alignment = 0;
    heap_desc.Flags = D3D12_HEAP_FLAG_DENY_BUFFERS | D3D12_HEAP_FLAG_DENY_RT_DS_TEXTURES;
    hr = ID3D12Device_CreateHeap(device, &heap_desc, &IID_ID3D12Heap, (void **)&heap);
    ok(hr == S_OK, "Failed to create heap, hr %#x.\n", hr);

    for (i = 0; i < ARRAY_SIZE(textures); ++i)
    {
        hr = ID3D12Device_CreatePlacedResource(device, heap, i * info.SizeInBytes,
                &resource_desc, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                NULL, &IID_ID3D12Resource, (void **)&textures[i]);
        ok(hr == S_OK, "Failed to create placed resource %u, hr %#x.\n", i, hr);

        check_interface(textures[i], &IID_ID3D12Object, true);
        check_interface(textures[i], &IID_ID3D12DeviceChild, true);
        check_interface(textures[i], &IID_ID3D12Pageable, true);
        check_interface(textures[i], &IID_ID3D12Resource, true);

        gpu_address = ID3D12Resource_GetGPUVirtualAddress(textures[i]);
        ok(!gpu_address, "Got unexpected GPU virtual address %#"PRIx64".\n", gpu_address);
    }

    refcount = get_refcount(heap);
    ok(refcount == 1, "Got unexpected refcount %u.\n", (unsigned int)refcount);

    for (i = 0; i < ARRAY_SIZE(textures); ++i)
    {
        refcount = ID3D12Resource_Release(textures[i]);
        ok(!refcount, "ID3D12Resource has %u references left.\n", (unsigned int)refcount);
    }

    refcount = ID3D12Heap_Release(heap);
    ok(!refcount, "ID3D12Heap has %u references left.\n", (unsigned int)refcount);
    refcount = ID3D12Device_Release(device);
    ok(!refcount, "ID3D12Device has %u references left.\n", (unsigned int)refcount);
}

void test_read_write_subresource(void)
{
    D3D12_TEXTURE_COPY_LOCATION src_location, dst_location;
    uint32_t *dst_buffer, *zero_buffer, *ptr;
    ID3D12GraphicsCommandList *command_list;
    D3D12_HEAP_PROPERTIES heap_properties;
    D3D12_SUBRESOURCE_DATA texture_data;
    D3D12_RESOURCE_DESC resource_desc;
    struct test_context_desc desc;
    struct test_context context;
    struct resource_readback rb;
    ID3D12Resource *src_texture;
    ID3D12Resource *dst_texture;
    ID3D12CommandQueue *queue;
    ID3D12Resource *rb_buffer;
    unsigned int buffer_size;
    unsigned int slice_pitch;
    unsigned int x, y, z, i;
    unsigned int row_pitch;
    uint32_t got, expected;
    ID3D12Device *device;
    D3D12_BOX box;
    HRESULT hr;

    memset(&desc, 0, sizeof(desc));
    desc.no_render_target = true;
    if (!init_test_context(&context, &desc))
        return;
    device = context.device;
    command_list = context.list;
    queue = context.queue;

    row_pitch = 128 * sizeof(unsigned int);
    slice_pitch = row_pitch * 100;
    buffer_size = slice_pitch * 64;

    /* Buffers are not supported */
    rb_buffer = create_readback_buffer(device, buffer_size);
    dst_buffer = malloc(buffer_size);
    ok(dst_buffer, "Failed to allocate memory.\n");
    zero_buffer = malloc(buffer_size);
    ok(zero_buffer, "Failed to allocate memory.\n");
    memset(zero_buffer, 0, buffer_size);

    set_box(&box, 0, 0, 0, 1, 1, 1);
    hr = ID3D12Resource_WriteToSubresource(rb_buffer, 0, &box, dst_buffer, row_pitch, slice_pitch);
    ok(hr == E_INVALIDARG, "Got unexpected hr %#x.\n", hr);

    hr = ID3D12Resource_ReadFromSubresource(rb_buffer, dst_buffer, row_pitch, slice_pitch, 0, &box);
    ok(hr == E_INVALIDARG, "Got unexpected hr %#x.\n", hr);

    ID3D12Resource_Release(rb_buffer);

    /* Only texture on custom heaps is legal for ReadFromSubresource/WriteToSubresource */
    resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE3D;
    resource_desc.Alignment = 0;
    resource_desc.Width = 128;
    resource_desc.Height = 100;
    resource_desc.DepthOrArraySize = 64;
    resource_desc.MipLevels = 1;
    resource_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    resource_desc.SampleDesc.Count = 1;
    resource_desc.SampleDesc.Quality = 0;
    resource_desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    resource_desc.Flags = 0;

    memset(&heap_properties, 0, sizeof(heap_properties));
    heap_properties.Type = D3D12_HEAP_TYPE_CUSTOM;
    heap_properties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_WRITE_BACK;
    heap_properties.MemoryPoolPreference = D3D12_MEMORY_POOL_L0;
    hr = ID3D12Device_CreateCommittedResource(device, &heap_properties, D3D12_HEAP_FLAG_NONE,
            &resource_desc, D3D12_RESOURCE_STATE_COMMON, NULL, &IID_ID3D12Resource, (void **)&src_texture);
    if (FAILED(hr))
    {
        skip("Failed to create texture on custom heap.\n");
        goto done;
    }

    /* Invalid box */
    set_box(&box, 0, 0, 0, 128, 100, 65);
    hr = ID3D12Resource_ReadFromSubresource(src_texture, dst_buffer, row_pitch, slice_pitch, 0, &box);
    ok(hr == E_INVALIDARG, "Got unexpected hr %#x.\n", hr);

    set_box(&box, 0, 0, 65, 128, 100, 65);
    hr = ID3D12Resource_ReadFromSubresource(src_texture, dst_buffer, row_pitch, slice_pitch, 0, &box);
    ok(hr == E_INVALIDARG, "Got unexpected hr %#x.\n", hr);

    set_box(&box, 128, 0, 0, 128, 100, 65);
    hr = ID3D12Resource_ReadFromSubresource(src_texture, dst_buffer, row_pitch, slice_pitch, 0, &box);
    ok(hr == E_INVALIDARG, "Got unexpected hr %#x.\n", hr);

    /* NULL box */
    hr = ID3D12Resource_WriteToSubresource(src_texture, 0, NULL, dst_buffer, row_pitch, slice_pitch);
    ok(hr == S_OK, "Got unexpected hr %#x.\n", hr);

    hr = ID3D12Resource_ReadFromSubresource(src_texture, dst_buffer, row_pitch, slice_pitch, 0, NULL);
    todo_if(is_nvidia_device(device))
    ok(hr == S_OK, "Got unexpected hr %#x.\n", hr);

    /* Empty box */
    set_box(&box, 128, 100, 64, 128, 100, 64);
    hr = ID3D12Resource_ReadFromSubresource(src_texture, dst_buffer, row_pitch, slice_pitch, 0, &box);
    ok(hr == S_OK, "Got unexpected hr %#x.\n", hr);

    set_box(&box, 0, 0, 0, 0, 0, 0);
    hr = ID3D12Resource_WriteToSubresource(src_texture, 0, &box, dst_buffer, row_pitch, slice_pitch);
    ok(hr == S_OK, "Got unexpected hr %#x.\n", hr);

    hr = ID3D12Resource_ReadFromSubresource(src_texture, dst_buffer, row_pitch, slice_pitch, 0, &box);
    ok(hr == S_OK, "Got unexpected hr %#x.\n", hr);

    for (i = 0; i < 2; ++i)
    {
        vkd3d_test_set_context("Test %u", i);

        for (z = 0; z < 64; ++z)
        {
            for (y = 0; y < 100; ++y)
            {
                for (x = 0; x < 128; ++x)
                {
                    ptr = &dst_buffer[z * 128 * 100 + y * 128 + x];
                    if (x < 2 && y< 2 && z < 2) /* Region 1 */
                        *ptr = (z + 1) << 16 | (y + 1) << 8 | (x + 1);
                    else if (2 <= x && x < 11 && 2 <= y && y < 13 && 2 <= z && z < 17) /* Region 2 */
                        *ptr = (z + 2) << 16 | (y + 2) << 8 | (x + 2);
                    else
                        *ptr = 0xdeadbeef;
                }
            }
        }

        if (i)
        {
            hr = ID3D12Resource_WriteToSubresource(src_texture, 0, NULL, zero_buffer, row_pitch, slice_pitch);
            ok(hr == S_OK, "Got unexpected hr %#x.\n", hr);

            /* Write region 1 */
            set_box(&box, 0, 0, 0, 2, 2, 2);
            hr = ID3D12Resource_WriteToSubresource(src_texture, 0, &box, dst_buffer, row_pitch, slice_pitch);
            ok(hr == S_OK, "Got unexpected hr %#x.\n", hr);

            /* Write region 2 */
            set_box(&box, 2, 2, 2, 11, 13, 17);
            hr = ID3D12Resource_WriteToSubresource(src_texture, 0, &box, &dst_buffer[2 * 128 * 100 + 2 * 128 + 2],
                    row_pitch, slice_pitch);
            ok(hr == S_OK, "Got unexpected hr %#x.\n", hr);
        }
        else
        {
            /* Upload the test data */
            transition_resource_state(command_list, src_texture,
                    D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST);
            texture_data.pData = dst_buffer;
            texture_data.RowPitch = row_pitch;
            texture_data.SlicePitch = slice_pitch;
            upload_texture_data(src_texture, &texture_data, 1, queue, command_list);
            reset_command_list(command_list, context.allocator);
            transition_resource_state(command_list, src_texture,
                    D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON);
        }

        memset(dst_buffer, 0, buffer_size);

        /* Read region 1 */
        set_box(&box, 0, 0, 0, 2, 2, 2);
        hr = ID3D12Resource_ReadFromSubresource(src_texture, dst_buffer, row_pitch, slice_pitch, 0, &box);
        todo_if(is_nvidia_device(device))
        ok(hr == S_OK, "Got unexpected hr %#x.\n", hr);

        /* Read region 2 */
        set_box(&box, 2, 2, 2, 11, 13, 17);
        hr = ID3D12Resource_ReadFromSubresource(src_texture, &dst_buffer[2 * 128 * 100 + 2 * 128 + 2], row_pitch,
                slice_pitch, 0, &box);
        todo_if(is_nvidia_device(device))
        ok(hr == S_OK, "Got unexpected hr %#x.\n", hr);

        for (z = 0; z < 64; ++z)
        {
            for (y = 0; y < 100; ++y)
            {
                for (x = 0; x < 128; ++x)
                {
                    if (x < 2 && y < 2 && z < 2) /* Region 1 */
                        expected = (z + 1) << 16 | (y + 1) << 8 | (x + 1);
                    else if (2 <= x && x < 11 && 2 <= y && y < 13 && 2 <= z && z < 17) /* Region 2 */
                        expected = (z + 2) << 16 | (y + 2) << 8 | (x + 2);
                    else /* Untouched */
                        expected = 0;

                    got = dst_buffer[z * 128 * 100 + y * 128 + x];
                    if (got != expected)
                        break;
                }
                if (got != expected)
                    break;
            }
            if (got != expected)
                break;
        }
        todo_if(is_nvidia_device(device))
        ok(got == expected, "Got unexpected value 0x%08x at (%u, %u, %u), expected 0x%08x.\n", got, x, y, z, expected);
    }
    vkd3d_test_set_context(NULL);

    /* Test layout is the same */
    dst_texture = create_default_texture3d(device, 128, 100, 64, 1, DXGI_FORMAT_R8G8B8A8_UNORM, 0,
            D3D12_RESOURCE_STATE_COPY_DEST);
    memset(dst_buffer, 0, buffer_size);
    texture_data.pData = dst_buffer;
    texture_data.RowPitch = row_pitch;
    texture_data.SlicePitch = slice_pitch;
    upload_texture_data(dst_texture, &texture_data, 1, queue, command_list);
    reset_command_list(command_list, context.allocator);

    src_location.pResource = src_texture;
    src_location.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    src_location.SubresourceIndex = 0;
    dst_location.pResource = dst_texture;
    dst_location.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dst_location.SubresourceIndex = 0;
    set_box(&box, 0, 0, 0, 128, 100, 64);
    ID3D12GraphicsCommandList_CopyTextureRegion(command_list, &dst_location, 0, 0, 0, &src_location, &box);

    transition_resource_state(command_list, dst_texture,
            D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COPY_SOURCE);
    get_texture_readback_with_command_list(dst_texture, 0, &rb, queue, command_list);
    for (z = 0; z < 64; ++z)
    {
        for (y = 0; y < 100; ++y)
        {
            for (x = 0; x < 128; ++x)
            {
                if (x < 2 && y < 2 && z < 2) /* Region 1 */
                    expected = (z + 1) << 16 | (y + 1) << 8 | (x + 1);
                else if (2 <= x && x < 11 && 2 <= y && y < 13 && 2 <= z && z < 17) /* Region 2 */
                    expected = (z + 2) << 16 | (y + 2) << 8 | (x + 2);
                else /* Untouched */
                    expected = 0;

                got = get_readback_uint(&rb, x, y, z);
                if (got != expected)
                    break;
            }
            if (got != expected)
                break;
        }
        if (got != expected)
            break;
    }
    ok(got == expected, "Got unexpected value 0x%08x at (%u, %u, %u), expected 0x%08x.\n", got, x, y, z, expected);
    release_resource_readback(&rb);

    ID3D12Resource_Release(src_texture);
    ID3D12Resource_Release(dst_texture);

    /* Invalid box */
    resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    resource_desc.Alignment = 0;
    resource_desc.Width = 64;
    resource_desc.Height = 32;
    resource_desc.DepthOrArraySize = 1;
    resource_desc.MipLevels = 1;
    resource_desc.Format = DXGI_FORMAT_BC1_UNORM;
    resource_desc.SampleDesc.Count = 1;
    resource_desc.SampleDesc.Quality = 0;
    resource_desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    resource_desc.Flags = 0;

    memset(&heap_properties, 0, sizeof(heap_properties));
    heap_properties.Type = D3D12_HEAP_TYPE_CUSTOM;
    heap_properties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_WRITE_BACK;
    heap_properties.MemoryPoolPreference = D3D12_MEMORY_POOL_L0;
    hr = ID3D12Device_CreateCommittedResource(device, &heap_properties, D3D12_HEAP_FLAG_NONE,
            &resource_desc, D3D12_RESOURCE_STATE_COMMON, NULL, &IID_ID3D12Resource, (void **)&src_texture);
    ok(hr == S_OK, "Failed to create resource, hr %#x.\n", hr);

    /* Unaligned coordinates for BC format */
    set_box(&box, 0, 0, 0, 2, 2, 1);
    hr = ID3D12Resource_ReadFromSubresource(src_texture, dst_buffer, row_pitch, slice_pitch, 0, &box);
    ok(hr == E_INVALIDARG, "Got unexpected hr %#x.\n", hr);

    set_box(&box, 2, 2, 0, 4, 4, 1);
    hr = ID3D12Resource_ReadFromSubresource(src_texture, dst_buffer, row_pitch, slice_pitch, 0, &box);
    ok(hr == E_INVALIDARG, "Got unexpected hr %#x.\n", hr);

    set_box(&box, 2, 2, 0, 6, 6, 1);
    hr = ID3D12Resource_ReadFromSubresource(src_texture, dst_buffer, row_pitch, slice_pitch, 0, &box);
    ok(hr == E_INVALIDARG, "Got unexpected hr %#x.\n", hr);

    /* Invalid coordinates for resource dimensions */
    set_box(&box, 0, 0, 0, 64, 32, 2);
    hr = ID3D12Resource_ReadFromSubresource(src_texture, dst_buffer, row_pitch, slice_pitch, 0, &box);
    ok(hr == E_INVALIDARG, "Got unexpected hr %#x.\n", hr);

    set_box(&box, 0, 0, 0, 68, 32, 1);
    hr = ID3D12Resource_ReadFromSubresource(src_texture, dst_buffer, row_pitch, slice_pitch, 0, &box);
    ok(hr == E_INVALIDARG, "Got unexpected hr %#x.\n", hr);

    ID3D12Resource_Release(src_texture);

done:
    free(dst_buffer);
    free(zero_buffer);
    destroy_test_context(&context);
}

struct suballocation_thread_data
{
    struct test_context *context;
    unsigned int seed;
};

void test_stress_suballocation_thread(void *userdata)
{
    struct suballocation_thread_data *thread_data = userdata;
    struct test_context *context = thread_data->context;

#define SUBALLOC_TEST_NUM_BUFFERS 128
#define SUBALLOC_TEST_NUM_ITERATIONS 64
    ID3D12Resource *readback_buffers[SUBALLOC_TEST_NUM_BUFFERS] = { NULL };
    ID3D12Resource *buffers[SUBALLOC_TEST_NUM_BUFFERS] = { NULL };
    UINT reference_values[SUBALLOC_TEST_NUM_BUFFERS] = { 0 };
    ID3D12Heap *heaps[SUBALLOC_TEST_NUM_BUFFERS] = { NULL };
    D3D12_ROOT_SIGNATURE_DESC root_signature_desc;
    D3D12_ROOT_PARAMETER root_parameters[2];
    ID3D12PipelineState *pipeline_state;
    ID3D12RootSignature *root_signature;
    D3D12_RESOURCE_DESC resource_desc;
    ID3D12CommandAllocator *allocator;
    ID3D12GraphicsCommandList *list;
    ID3D12Heap *dummy_heaps[2];
    D3D12_HEAP_DESC heap_desc;
    unsigned int iter, i;
    UINT reference_value;
    ID3D12Fence *fence;
    UINT64 fence_value;
    bool clear_buffer;
    unsigned int seed;
    bool alloc_heap;
    bool keep_alive;
    UINT alloc_size;
    HRESULT hr;

    static const DWORD cs_code[] =
    {
#if 0
        RWStructuredBuffer<uint> Buf : register(u0);
        cbuffer CBuf : register(b0) { uint clear_value; };

        [numthreads(64, 1, 1)]
        void main(uint thr : SV_DispatchThreadID)
        {
            Buf[thr] = clear_value;
        }
#endif
        0x43425844, 0x687983cd, 0xe75a9b58, 0xa77e1917, 0x78d96804, 0x00000001, 0x000000c0, 0x00000003,
        0x0000002c, 0x0000003c, 0x0000004c, 0x4e475349, 0x00000008, 0x00000000, 0x00000008, 0x4e47534f,
        0x00000008, 0x00000000, 0x00000008, 0x58454853, 0x0000006c, 0x00050050, 0x0000001b, 0x0100086a,
        0x04000059, 0x00208e46, 0x00000000, 0x00000001, 0x0400009e, 0x0011e000, 0x00000000, 0x00000004,
        0x0200005f, 0x00020012, 0x0400009b, 0x00000040, 0x00000001, 0x00000001, 0x090000a8, 0x0011e012,
        0x00000000, 0x0002000a, 0x00004001, 0x00000000, 0x0020800a, 0x00000000, 0x00000000, 0x0100003e,
    };

    seed = thread_data->seed;

#ifdef _WIN32
    /* rand_r() doesn't exist, but rand() does and is MT safe on Win32. */
#define rand_r(x) rand()
    srand(seed);
#endif

    root_signature_desc.NumParameters = 2;
    root_signature_desc.Flags = 0;
    root_signature_desc.NumStaticSamplers = 0;
    root_signature_desc.pStaticSamplers = NULL;
    root_signature_desc.pParameters = root_parameters;

    root_parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    root_parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    root_parameters[0].Descriptor.RegisterSpace = 0;
    root_parameters[0].Descriptor.ShaderRegister = 0;

    root_parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    root_parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    root_parameters[1].Constants.RegisterSpace = 0;
    root_parameters[1].Constants.ShaderRegister = 0;
    root_parameters[1].Constants.Num32BitValues = 1;

    hr = create_root_signature(context->device, &root_signature_desc, &root_signature);
    ok(SUCCEEDED(hr), "Failed to create root signature.\n");

    pipeline_state = create_compute_pipeline_state(context->device, root_signature,
            shader_bytecode(cs_code, sizeof(cs_code)));

    hr = ID3D12Device_CreateCommandAllocator(context->device, D3D12_COMMAND_LIST_TYPE_DIRECT, &IID_ID3D12CommandAllocator, (void **)&allocator);
    ok(SUCCEEDED(hr), "Failed to create command allocator.\n");
    hr = ID3D12Device_CreateCommandList(context->device, 0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator, NULL, &IID_ID3D12GraphicsCommandList, (void **)&list);
    ok(SUCCEEDED(hr), "Failed to create command list.\n");
    ID3D12GraphicsCommandList_Close(list);

    ID3D12Device_CreateFence(context->device, 0, D3D12_FENCE_FLAG_NONE, &IID_ID3D12Fence, (void **)&fence);
    fence_value = 0;
    reference_value = 0;

    hr = wait_for_fence(fence, fence_value);
    ok(SUCCEEDED(hr), "Failed to wait for fence.\n");

    /* Stress test internal implementation details. Perform many smaller allocations and verify that the allocation works as expected. */

    for (iter = 0; iter < SUBALLOC_TEST_NUM_ITERATIONS; iter++)
    {
        reset_command_list(list, allocator);
        fence_value++;

        for (i = 0; i < ARRAY_SIZE(heaps); i++)
        {
            /* Randomly allocate heaps and place a buffer on top of it. */
            alloc_heap = rand_r(&seed) % 2 == 0;
            alloc_size = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT * (1 + rand_r(&seed) % 20);
            keep_alive = rand_r(&seed) % 2 == 0;

            if (buffers[i] && keep_alive)
            {
                /* To test chunk allocator, make sure we don't free *everything* every iteration.
                   Just transition back to UAV state. */
                transition_resource_state(list, buffers[i], D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                /* If we reuse the buffer, always test explicit clear since we tested zero memory once already previous iteration. */
                reference_values[i] = ++reference_value;
            }
            else
            {
                clear_buffer = rand_r(&seed) % 2 == 0;
                if (clear_buffer)
                    reference_values[i] = ++reference_value;
                else
                    reference_values[i] = 0; /* Test zero memory behavior. */

                if (heaps[i])
                    ID3D12Heap_Release(heaps[i]);
                if (buffers[i])
                    ID3D12Resource_Release(buffers[i]);
                if (readback_buffers[i])
                    ID3D12Resource_Release(readback_buffers[i]);

                heaps[i] = NULL;
                buffers[i] = NULL;
                readback_buffers[i] = NULL;

                memset(&heap_desc, 0, sizeof(heap_desc));
                heap_desc.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
                heap_desc.Flags = D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS;
                heap_desc.SizeInBytes = alloc_size;
                heap_desc.Properties.Type = D3D12_HEAP_TYPE_DEFAULT;

                /* If we're clearing ourselves, this should be moot. Check that it doesn't cause issues. */
                if (clear_buffer && rand_r(&seed) % 2 == 0)
                    heap_desc.Flags |= D3D12_HEAP_FLAG_CREATE_NOT_ZEROED;

                if (alloc_heap)
                {
                    hr = ID3D12Device_CreateHeap(context->device, &heap_desc, &IID_ID3D12Heap, (void **)&heaps[i]);
                    ok(SUCCEEDED(hr), "Failed to allocate heap.\n");
                }

                memset(&resource_desc, 0, sizeof(resource_desc));
                resource_desc.Width = alloc_size;
                resource_desc.DepthOrArraySize = 1;
                resource_desc.Height = 1;
                resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
                resource_desc.Format = DXGI_FORMAT_UNKNOWN;
                resource_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
                resource_desc.SampleDesc.Count = 1;
                resource_desc.MipLevels = 1;
                resource_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
                if (alloc_heap)
                    hr = ID3D12Device_CreatePlacedResource(context->device, heaps[i], 0, &resource_desc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, NULL, &IID_ID3D12Resource, (void **)&buffers[i]);
                else
                    hr = ID3D12Device_CreateCommittedResource(context->device, &heap_desc.Properties, D3D12_HEAP_FLAG_NONE, &resource_desc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, NULL, &IID_ID3D12Resource, (void **)&buffers[i]);
                ok(SUCCEEDED(hr), "Failed to create buffer.\n");

                resource_desc.Flags = D3D12_RESOURCE_FLAG_NONE;
                heap_desc.Properties.Type = D3D12_HEAP_TYPE_READBACK;
                hr = ID3D12Device_CreateCommittedResource(context->device, &heap_desc.Properties, D3D12_HEAP_FLAG_NONE, &resource_desc, D3D12_RESOURCE_STATE_COPY_DEST, NULL, &IID_ID3D12Resource, (void **)&readback_buffers[i]);
                ok(SUCCEEDED(hr), "Failed to create readback buffer.\n");
            }

            if (reference_values[i] != 0)
            {
                ID3D12GraphicsCommandList_SetComputeRootSignature(list, root_signature);
                ID3D12GraphicsCommandList_SetComputeRootUnorderedAccessView(list, 0, ID3D12Resource_GetGPUVirtualAddress(buffers[i]));
                ID3D12GraphicsCommandList_SetComputeRoot32BitConstants(list, 1, 1, &reference_values[i], 0);
                ID3D12GraphicsCommandList_SetPipelineState(list, pipeline_state);
                ID3D12GraphicsCommandList_Dispatch(list, ID3D12Resource_GetDesc(buffers[i]).Width / (4 * 64), 1, 1);
            }

            transition_resource_state(list, buffers[i], D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
            ID3D12GraphicsCommandList_CopyResource(list, readback_buffers[i], buffers[i]);
        }

        /* Create a heap which needs to be zeroed.
         * Test that we can safely free the heap after zero memory is flushed.
         * For the first one, we free before ExecuteCommandLists, this should never attempt clearing memory.
         * For the second one, we have flushed, so we expect a CPU stall where we wait for zeromemory to complete. */
        alloc_size = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT * (1 + rand_r(&seed) % 20);
        memset(&heap_desc, 0, sizeof(heap_desc));
        heap_desc.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
        heap_desc.Flags = D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS;
        heap_desc.SizeInBytes = alloc_size;
        heap_desc.Properties.Type = D3D12_HEAP_TYPE_DEFAULT;
        hr = ID3D12Device_CreateHeap(context->device, &heap_desc, &IID_ID3D12Heap, (void **)&dummy_heaps[0]);
        ok(SUCCEEDED(hr), "Failed to allocate heap.\n");
        hr = ID3D12Device_CreateHeap(context->device, &heap_desc, &IID_ID3D12Heap, (void **)&dummy_heaps[1]);
        ok(SUCCEEDED(hr), "Failed to allocate heap.\n");

        ID3D12GraphicsCommandList_Close(list);
        ID3D12Heap_Release(dummy_heaps[0]);
        ID3D12CommandQueue_ExecuteCommandLists(context->queue, 1, (ID3D12CommandList *const *)&list);
        ID3D12Heap_Release(dummy_heaps[1]);

        ID3D12CommandQueue_Signal(context->queue, fence, fence_value);
        wait_for_fence(fence, fence_value);

        for (i = 0; i < ARRAY_SIZE(readback_buffers); i++)
        {
            bool found_error = false;
            UINT j, words, *mapped;
            UINT last_value = 0;

            words = ID3D12Resource_GetDesc(readback_buffers[i]).Width / 4;
            last_value = 0;
            hr = ID3D12Resource_Map(readback_buffers[i], 0, NULL, (void **)&mapped);
            ok(SUCCEEDED(hr), "Failed to map readback buffer.\n");

            if (SUCCEEDED(hr))
            {
                for (j = 0; j < words && !found_error; j++)
                {
                    last_value = mapped[j];
                    found_error = mapped[j] != reference_values[i];
                }
                ok(!found_error, "Expected all words to be %u, but got %u.\n", reference_values[i], last_value);
                ID3D12Resource_Unmap(readback_buffers[i], 0, NULL);
            }
        }
    }

    for (i = 0; i < ARRAY_SIZE(heaps); i++)
        if (heaps[i])
            ID3D12Heap_Release(heaps[i]);
    for (i = 0; i < ARRAY_SIZE(buffers); i++)
        if (buffers[i])
            ID3D12Resource_Release(buffers[i]);
    for (i = 0; i < ARRAY_SIZE(readback_buffers); i++)
        if (readback_buffers[i])
            ID3D12Resource_Release(readback_buffers[i]);

    ID3D12PipelineState_Release(pipeline_state);
    ID3D12RootSignature_Release(root_signature);
    ID3D12GraphicsCommandList_Release(list);
    ID3D12CommandAllocator_Release(allocator);
    ID3D12Fence_Release(fence);
#undef rand_r
}

void test_stress_suballocation(void)
{
    struct suballocation_thread_data data;
    struct test_context_desc desc;
    struct test_context context;

    memset(&desc, 0, sizeof(desc));
    desc.no_render_target = true;
    desc.no_pipeline = true;
    desc.no_root_signature = true;
    if (!init_test_context(&context, &desc))
        return;

    data.context = &context;
    data.seed = 42;
    test_stress_suballocation_thread(&data);

    destroy_test_context(&context);
}

void test_stress_suballocation_multithread(void)
{
    struct suballocation_thread_data data[8];
    struct test_context_desc desc;
    struct test_context context;
    HANDLE threads[8];
    unsigned int i;

    memset(&desc, 0, sizeof(desc));
    desc.no_render_target = true;
    desc.no_pipeline = true;
    desc.no_root_signature = true;
    if (!init_test_context(&context, &desc))
        return;

    for (i = 0; i < 8; i++)
    {
        data[i].context = &context;
        data[i].seed = 42 + i;
        threads[i] = create_thread(test_stress_suballocation_thread, &data[i]);
    }

    for (i = 0; i < 8; i++)
        ok(join_thread(threads[i]), "Failed to join thread %u.\n", i);

    destroy_test_context(&context);
}

void test_placed_image_alignment(void)
{
    ID3D12Resource *readback_buffers[4096] = { NULL };
    D3D12_TEXTURE_COPY_LOCATION copy_dst, copy_src;
    D3D12_RESOURCE_ALLOCATION_INFO alloc_info;
    D3D12_DESCRIPTOR_HEAP_DESC desc_heap_desc;
    ID3D12Resource *images[4096] = { NULL };
    D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle;
    D3D12_RENDER_TARGET_VIEW_DESC rtv_desc;
    D3D12_RESOURCE_DESC resource_desc;
    D3D12_RESOURCE_BARRIER barrier;
    struct test_context_desc desc;
    struct test_context context;
    ID3D12DescriptorHeap *rtvs;
    D3D12_HEAP_DESC heap_desc;
    unsigned int i, j;
    D3D12_BOX src_box;
    ID3D12Heap *heap;
    HRESULT hr;

    memset(&desc, 0, sizeof(desc));
    desc.no_render_target = true;
    desc.no_pipeline = true;
    desc.no_root_signature = true;
    if (!init_test_context(&context, &desc))
        return;

    /* Verifies that we don't screw up when using GPUs which require > 64k alignment for RTs (Polaris and older).
     * We verify this by ensuring we don't get any fake internal allocations.
     * If we do, we will certainly OOM the GPU since we're placing ~64 GB worth of textures. */

    memset(&resource_desc, 0, sizeof(resource_desc));
    resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    resource_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    resource_desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    resource_desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    resource_desc.Width = 1024;
    resource_desc.Height = 1024;
    resource_desc.MipLevels = 1;
    resource_desc.DepthOrArraySize = 1;
    resource_desc.SampleDesc.Count = 1;
    resource_desc.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
    alloc_info = ID3D12Device_GetResourceAllocationInfo(context.device, 0, 1, &resource_desc);
    ok(alloc_info.Alignment <= D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT, "Requirement alignment %u is > 64KiB.\n", alloc_info.Alignment);

    memset(&heap_desc, 0, sizeof(heap_desc));
    heap_desc.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
    heap_desc.SizeInBytes = alloc_info.SizeInBytes + D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT * ARRAY_SIZE(images);
    heap_desc.Properties.Type = D3D12_HEAP_TYPE_DEFAULT;
    heap_desc.Flags = D3D12_HEAP_FLAG_ALLOW_ONLY_RT_DS_TEXTURES;

    memset(&desc_heap_desc, 0, sizeof(desc_heap_desc));
    desc_heap_desc.NumDescriptors = ARRAY_SIZE(images);
    desc_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    hr = ID3D12Device_CreateDescriptorHeap(context.device, &desc_heap_desc, &IID_ID3D12DescriptorHeap, (void **)&rtvs);
    ok(SUCCEEDED(hr), "Failed to create RTV heap.\n");

    hr = ID3D12Device_CreateHeap(context.device, &heap_desc, &IID_ID3D12Heap, (void **)&heap);
    ok(SUCCEEDED(hr), "Failed to create heap.\n");

    for (i = 0; i < ARRAY_SIZE(images); i++)
    {
        hr = ID3D12Device_CreatePlacedResource(context.device, heap, D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT * i, &resource_desc, D3D12_RESOURCE_STATE_RENDER_TARGET, NULL, &IID_ID3D12Resource, (void **)&images[i]);
        ok(SUCCEEDED(hr), "Failed to create placed resource.\n");

        cpu_handle = ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(rtvs);
        cpu_handle.ptr += i * ID3D12Device_GetDescriptorHandleIncrementSize(context.device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

        memset(&rtv_desc, 0, sizeof(rtv_desc));
        rtv_desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
        rtv_desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
        ID3D12Device_CreateRenderTargetView(context.device, images[i], &rtv_desc, cpu_handle);
    }

    for (i = 0; i < ARRAY_SIZE(images); i++)
        readback_buffers[i] = create_readback_buffer(context.device, 16);

    for (i = 0; i < ARRAY_SIZE(images); i++)
    {
        const FLOAT color[] = { i, i + 1, i + 2, i + 3 };
        const RECT rect = { 0, 0, 1, 1 };

        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_ALIASING;
        barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        barrier.Aliasing.pResourceAfter = images[i];
        barrier.Aliasing.pResourceBefore = NULL;
        ID3D12GraphicsCommandList_ResourceBarrier(context.list, 1, &barrier);
        ID3D12GraphicsCommandList_DiscardResource(context.list, images[i], NULL);
        cpu_handle = ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(rtvs);
        cpu_handle.ptr += i * ID3D12Device_GetDescriptorHandleIncrementSize(context.device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        ID3D12GraphicsCommandList_ClearRenderTargetView(context.list, cpu_handle, color, 1, &rect);
        transition_resource_state(context.list, images[i], D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);

        copy_dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        copy_dst.pResource = readback_buffers[i];
        copy_dst.PlacedFootprint.Offset = 0;
        copy_dst.PlacedFootprint.Footprint.Width = 1;
        copy_dst.PlacedFootprint.Footprint.Height = 1;
        copy_dst.PlacedFootprint.Footprint.Depth = 1;
        copy_dst.PlacedFootprint.Footprint.RowPitch = 1024; /* Needs to be large on D3D12. Doesn't matter here. */
        copy_dst.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;

        copy_src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        copy_src.SubresourceIndex = 0;
        copy_src.pResource = images[i];

        src_box.left = 0;
        src_box.right = 1;
        src_box.top = 0;
        src_box.bottom = 1;
        src_box.front = 0;
        src_box.back = 1;

        ID3D12GraphicsCommandList_CopyTextureRegion(context.list, &copy_dst, 0, 0, 0, &copy_src, &src_box);
    }

    ID3D12GraphicsCommandList_Close(context.list);
    ID3D12CommandQueue_ExecuteCommandLists(context.queue, 1, (ID3D12CommandList *const *)&context.list);
    wait_queue_idle(context.device, context.queue);

    for (i = 0; i < ARRAY_SIZE(readback_buffers); i++)
    {
        float *mapped = NULL;
        hr = ID3D12Resource_Map(readback_buffers[i], 0, NULL, (void **)&mapped);
        ok(SUCCEEDED(hr), "Failed to map buffer.\n");
        if (mapped)
        {
            for (j = 0; j < 4; j++)
                ok(mapped[j] == (float)(i + j), "Readback data for component %u is unxpected (%f != %f).\n", j, mapped[j], (float)(i + j));
            ID3D12Resource_Unmap(readback_buffers[i], 0, NULL);
        }
    }

    for (i = 0; i < ARRAY_SIZE(images); i++)
    {
        ID3D12Resource_Release(images[i]);
        ID3D12Resource_Release(readback_buffers[i]);
    }
    ID3D12DescriptorHeap_Release(rtvs);
    ID3D12Heap_Release(heap);
    destroy_test_context(&context);
}

