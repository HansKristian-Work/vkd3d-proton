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
    bool is_gpu_upload_heap_supported;
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
        {D3D12_HEAP_TYPE_UPLOAD,     D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET},
        {D3D12_HEAP_TYPE_READBACK,   D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET},
        {D3D12_HEAP_TYPE_UPLOAD,     D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS},
        {D3D12_HEAP_TYPE_READBACK,   D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS},
        {D3D12_HEAP_TYPE_DEFAULT,    D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS},
        {D3D12_HEAP_TYPE_UPLOAD,     D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS},
        {D3D12_HEAP_TYPE_READBACK,   D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS},
        {D3D12_HEAP_TYPE_GPU_UPLOAD, D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS},
    };

    if (!(device = create_device()))
    {
        skip("Failed to create device.\n");
        return;
    }

    is_gpu_upload_heap_supported = device_supports_gpu_upload_heap(device);

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

    resource_desc.SampleDesc.Count = 0;
    hr = ID3D12Device_CreateCommittedResource(device, &heap_properties, D3D12_HEAP_FLAG_NONE,
            &resource_desc, D3D12_RESOURCE_STATE_RENDER_TARGET, &clear_value,
            &IID_ID3D12Resource, (void **)&resource);
    ok(hr == E_INVALIDARG, "Got unexpected hr %#x.\n", hr);
    resource_desc.SampleDesc.Count = 1;

    hr = ID3D12Device_CreateCommittedResource(device, &heap_properties, D3D12_HEAP_FLAG_NONE,
            &resource_desc, D3D12_RESOURCE_STATE_RENDER_TARGET | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
            &clear_value, &IID_ID3D12Resource, (void **)&resource);
    ok(hr == E_INVALIDARG, "Got unexpected hr %#x.\n", hr);

    /* For D3D12_RESOURCE_STATE_RENDER_TARGET the D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET flag is required. */
    resource_desc.Flags = 0;
    hr = ID3D12Device_CreateCommittedResource(device, &heap_properties, D3D12_HEAP_FLAG_NONE,
            &resource_desc, D3D12_RESOURCE_STATE_RENDER_TARGET, NULL,
            &IID_ID3D12Resource, (void **)&resource);
    ok(hr == E_INVALIDARG, "Got unexpected hr %#x.\n", hr);
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

    /* A texture *can* be created on a GPU UPLOAD heap. */
    heap_properties.Type = D3D12_HEAP_TYPE_GPU_UPLOAD;
    resource_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    resource = (void*)(uintptr_t)0xdeadbeef;
    hr = ID3D12Device_CreateCommittedResource(device, &heap_properties, D3D12_HEAP_FLAG_NONE,
            &resource_desc, D3D12_RESOURCE_STATE_RENDER_TARGET, NULL,
            &IID_ID3D12Resource, (void **)&resource);
    if (is_gpu_upload_heap_supported)
        ok(hr == S_OK, "Failed to create committed resource (rendertarget) on GPU upload heap, hr %#x.\n", hr);
    else
        ok(hr == E_INVALIDARG, "Got unexpected hr %#x.\n", hr);
    if (SUCCEEDED(hr))
        ID3D12Resource_Release(resource);

    resource_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    resource_desc.Format = DXGI_FORMAT_D32_FLOAT;
    resource = (void*)(uintptr_t)0xdeadbeef;
    hr = ID3D12Device_CreateCommittedResource(device, &heap_properties, D3D12_HEAP_FLAG_NONE,
        &resource_desc, D3D12_RESOURCE_STATE_DEPTH_READ, NULL,
        &IID_ID3D12Resource, (void**)&resource);
    if (is_gpu_upload_heap_supported)
        todo ok(hr == S_OK, "Failed to create committed resource (depth texture) on GPU upload heap, hr %#x.\n", hr);
    else
        todo ok(hr == E_INVALIDARG, "Got unexpected hr %#x.\n", hr);
    if (SUCCEEDED(hr))
        ID3D12Resource_Release(resource);
    resource_desc.Flags = 0;
    resource_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;

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
    /* Unaligned mip 0 textures are allowed now on AgilitySDK 606. */
    ok(hr == S_OK, "Got unexpected hr %#x.\n", hr);
    if (SUCCEEDED(hr))
        ID3D12Resource_Release(resource);

    resource_desc.Width = 31;
    resource_desc.Height = 32;
    hr = ID3D12Device_CreateCommittedResource(device, &heap_properties, D3D12_HEAP_FLAG_NONE,
            &resource_desc, D3D12_RESOURCE_STATE_COMMON, NULL,
            &IID_ID3D12Resource, (void **)&resource);
    /* Unaligned mip 0 textures are allowed now on AgilitySDK 606. */
    ok(hr == S_OK, "Got unexpected hr %#x.\n", hr);
    if (SUCCEEDED(hr))
        ID3D12Resource_Release(resource);

    resource_desc.Width = 30;
    resource_desc.Height = 30;
    hr = ID3D12Device_CreateCommittedResource(device, &heap_properties, D3D12_HEAP_FLAG_NONE,
            &resource_desc, D3D12_RESOURCE_STATE_COMMON, NULL,
            &IID_ID3D12Resource, (void **)&resource);
    /* Unaligned mip 0 textures are allowed now on AgilitySDK 606. */
    ok(hr == S_OK, "Got unexpected hr %#x.\n", hr);
    if (SUCCEEDED(hr))
        ID3D12Resource_Release(resource);

    resource_desc.Width = 2;
    resource_desc.Height = 2;
    hr = ID3D12Device_CreateCommittedResource(device, &heap_properties, D3D12_HEAP_FLAG_NONE,
            &resource_desc, D3D12_RESOURCE_STATE_COMMON, NULL,
            &IID_ID3D12Resource, (void **)&resource);
    /* Unaligned mip 0 textures are allowed now on AgilitySDK 606. */
    ok(hr == S_OK, "Got unexpected hr %#x.\n", hr);
    if (SUCCEEDED(hr))
        ID3D12Resource_Release(resource);

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

    heap_properties.Type = D3D12_HEAP_TYPE_GPU_UPLOAD;
    hr = ID3D12Device_CreateCommittedResource(device, &heap_properties, D3D12_HEAP_FLAG_NONE,
            &resource_desc, D3D12_RESOURCE_STATE_GENERIC_READ, NULL,
            &IID_ID3D12Resource, (void **)&resource);
    if (is_gpu_upload_heap_supported)
    {
        ok(hr == S_OK, "Failed to create committed resource, hr %#x.\n", hr);

        check_interface(resource, &IID_ID3D12Object, true);
        check_interface(resource, &IID_ID3D12DeviceChild, true);
        check_interface(resource, &IID_ID3D12Pageable, true);
        check_interface(resource, &IID_ID3D12Resource, true);

        gpu_address = ID3D12Resource_GetGPUVirtualAddress(resource);
        ok(gpu_address, "Got unexpected GPU virtual address %#"PRIx64".\n", gpu_address);

        refcount = ID3D12Resource_Release(resource);
        ok(!refcount, "ID3D12Resource has %u references left.\n", (unsigned int)refcount);
    }
    else
        ok(hr == E_INVALIDARG, "Got unexpected hr %#x.\n", hr);

    heap_properties.Type = D3D12_HEAP_TYPE_UPLOAD;
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
    ok(hr == S_OK, "Got unexpected hr %#x.\n", hr);
    /* AgilitySDK 606 allows COMMON for UPLOAD heap now. */
    if (SUCCEEDED(hr))
        ID3D12Resource_Release(resource);

    hr = ID3D12Device_CreateCommittedResource(device, &heap_properties, D3D12_HEAP_FLAG_NONE,
            &resource_desc, D3D12_RESOURCE_STATE_COPY_SOURCE, NULL,
            &IID_ID3D12Resource, (void **)&resource);
    ok(hr == S_OK, "Got unexpected hr %#x.\n", hr);
    /* AgilitySDK 606 allows partial GENERIC_READ for UPLOAD heap now. */
    if (SUCCEEDED(hr))
        ID3D12Resource_Release(resource);

    heap_properties.Type = D3D12_HEAP_TYPE_GPU_UPLOAD;
    /* D3D12_HEAP_TYPE_GPU_UPLOAD does not have the same restrictions for the state. */
    hr = ID3D12Device_CreateCommittedResource(device, &heap_properties, D3D12_HEAP_FLAG_NONE,
            &resource_desc, D3D12_RESOURCE_STATE_COMMON, NULL,
            &IID_ID3D12Resource, (void **)&resource);
    ok(hr == (is_gpu_upload_heap_supported ? S_OK : E_INVALIDARG), "Got unexpected hr %#x.\n", hr);
    if (SUCCEEDED(hr))
        ID3D12Resource_Release(resource);

    hr = ID3D12Device_CreateCommittedResource(device, &heap_properties, D3D12_HEAP_FLAG_NONE,
            &resource_desc, D3D12_RESOURCE_STATE_COPY_SOURCE, NULL,
            &IID_ID3D12Resource, (void **)&resource);
    ok(hr == (is_gpu_upload_heap_supported ? S_OK : E_INVALIDARG), "Got unexpected hr %#x.\n", hr);
    if (SUCCEEDED(hr))
        ID3D12Resource_Release(resource);

    hr = ID3D12Device_CreateCommittedResource(device, &heap_properties, D3D12_HEAP_FLAG_NONE,
        &resource_desc, D3D12_RESOURCE_STATE_COPY_DEST, NULL,
        &IID_ID3D12Resource, (void**)&resource);
    ok(hr == (is_gpu_upload_heap_supported ? S_OK : E_INVALIDARG), "Got unexpected hr %#x.\n", hr);
    if (SUCCEEDED(hr))
        ID3D12Resource_Release(resource);

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
    ok(hr == S_OK, "Got unexpected hr %#x.\n", hr);
    /* AgilitySDK 606 allows COMMON for READBACK heap now. */
    if (SUCCEEDED(hr))
        ID3D12Resource_Release(resource);
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

        if (invalid_buffer_desc_tests[i].heap_type == D3D12_HEAP_TYPE_UPLOAD || invalid_buffer_desc_tests[i].heap_type == D3D12_HEAP_TYPE_GPU_UPLOAD)
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
    bool is_gpu_upload_heap_supported;
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
        {D3D12_CPU_PAGE_PROPERTY_WRITE_COMBINE, D3D12_MEMORY_POOL_L1, S_OK},
        {D3D12_CPU_PAGE_PROPERTY_WRITE_BACK, D3D12_MEMORY_POOL_L1, E_INVALIDARG},
    };

    if (!(device = create_device()))
    {
        skip("Failed to create device.\n");
        return;
    }

    is_gpu_upload_heap_supported = device_supports_gpu_upload_heap(device);

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
    for (i = D3D12_HEAP_TYPE_DEFAULT; i <= D3D12_HEAP_TYPE_GPU_UPLOAD; ++i)
    {
        if (i == D3D12_HEAP_TYPE_CUSTOM || (i == D3D12_HEAP_TYPE_GPU_UPLOAD && !is_gpu_upload_heap_supported))
            continue;

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

            case D3D12_HEAP_TYPE_GPU_UPLOAD:
                ok(desc.Properties.CPUPageProperty == (architecture.CacheCoherentUMA
                        ? D3D12_CPU_PAGE_PROPERTY_WRITE_BACK : D3D12_CPU_PAGE_PROPERTY_WRITE_COMBINE),
                        "Got unexpected CPUPageProperty %#x.\n", desc.Properties.CPUPageProperty);
                ok(desc.Properties.MemoryPoolPreference == (architecture.UMA
                        ? D3D12_MEMORY_POOL_L0 : D3D12_MEMORY_POOL_L1),
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
        expected_hr = custom_tests[i].expected_hr;
        if (custom_tests[i].pool_preference == D3D12_MEMORY_POOL_L1 && !is_pool_L1_supported)
            expected_hr = E_INVALIDARG;
        if (custom_tests[i].pool_preference == D3D12_MEMORY_POOL_L1 && custom_tests[i].page_property != D3D12_CPU_PAGE_PROPERTY_NOT_AVAILABLE && !is_gpu_upload_heap_supported)
            expected_hr = E_INVALIDARG;

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
    bool is_gpu_upload_heap_supported;
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
        {D3D12_HEAP_TYPE_UPLOAD,     D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET},
        {D3D12_HEAP_TYPE_READBACK,   D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET},
        {D3D12_HEAP_TYPE_GPU_UPLOAD, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET},
        {D3D12_HEAP_TYPE_UPLOAD,     D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS},
        {D3D12_HEAP_TYPE_READBACK,   D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS},
        {D3D12_HEAP_TYPE_GPU_UPLOAD, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS},
    };

    if (!(device = create_device()))
    {
        skip("Failed to create device.\n");
        return;
    }

    is_gpu_upload_heap_supported = device_supports_gpu_upload_heap(device);

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
        if (heap_desc.Properties.Type != D3D12_HEAP_TYPE_GPU_UPLOAD || is_gpu_upload_heap_supported)
            ok(hr == S_OK, "Test %u: Failed to create heap, hr %#x.\n", i, hr);
        else
        {
            ok(hr == E_INVALIDARG, "Got unexpected hr %#x.\n", hr);
            continue;
        }

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

        if (invalid_buffer_desc_tests[i].heap_type == D3D12_HEAP_TYPE_UPLOAD || invalid_buffer_desc_tests[i].heap_type == D3D12_HEAP_TYPE_GPU_UPLOAD)
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
    bool is_gpu_upload_heap_supported;
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

    is_gpu_upload_heap_supported = device_supports_gpu_upload_heap(device);

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

    heap_properties.Type = D3D12_HEAP_TYPE_GPU_UPLOAD;
    hr = ID3D12Device_CreateCommittedResource(device, &heap_properties, D3D12_HEAP_FLAG_NONE,
            &resource_desc, D3D12_RESOURCE_STATE_GENERIC_READ, NULL,
            &IID_ID3D12Resource, (void **)&resource);
    if (is_gpu_upload_heap_supported)
    {
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
    }
    else
        ok(hr == E_INVALIDARG, "Got unexpected hr %#x.\n", hr);


    refcount = ID3D12Device_Release(device);
    ok(!refcount, "ID3D12Device has %u references left.\n", (unsigned int)refcount);
}

void test_map_placed_resources(void)
{
    ID3D12Heap *upload_heap, *readback_heap, *gpu_upload_heap;
    D3D12_ROOT_SIGNATURE_DESC root_signature_desc;
    ID3D12GraphicsCommandList *command_list;
    D3D12_ROOT_PARAMETER root_parameters[2];
    D3D12_RESOURCE_DESC resource_desc;
    bool is_gpu_upload_heap_supported;
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

#include "shaders/resource/headers/ps_store_buffer.h"

    static const uint32_t expected_values[] = {0xdead, 0xbeef, 0xfeed, 0xc0de};

    memset(&desc, 0, sizeof(desc));
    desc.no_root_signature = true;
    if (!init_test_context(&context, &desc))
        return;
    device = context.device;
    command_list = context.list;
    queue = context.queue;

    is_gpu_upload_heap_supported = device_supports_gpu_upload_heap(device);

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

    context.pipeline_state = create_pipeline_state(device, context.root_signature, 0, NULL, &ps_store_buffer_dxbc, NULL);

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

    heap_desc.Properties.Type = D3D12_HEAP_TYPE_GPU_UPLOAD;
    hr = ID3D12Device_CreateHeap(device, &heap_desc, &IID_ID3D12Heap, (void **)&gpu_upload_heap);
    if (is_gpu_upload_heap_supported)
        ok(hr == S_OK, "Failed to create heap, hr %#x.\n", hr);
    else
        ok(hr == E_INVALIDARG, "Got unexpected hr %#x.\n", hr);

    if (is_gpu_upload_heap_supported)
    {

        hr = ID3D12Device_CreatePlacedResource(device, gpu_upload_heap, 0,
            &resource_desc, D3D12_RESOURCE_STATE_GENERIC_READ, NULL,
            &IID_ID3D12Resource, (void**)&cb[0]);
        ok(hr == S_OK, "Failed to create placed resource, hr %#x.\n", hr);

        cb_data[0] = NULL;
        hr = ID3D12Resource_Map(cb[0], 0, NULL, (void **)&cb_data[0]);
        ok(hr == S_OK, "Got unexpected hr %#x.\n", hr);
        ok(cb_data[0], "Got NULL pointer.\n");
        ID3D12Resource_Unmap(cb[0], 0, NULL);

        cb_data[0] = (void *)(uintptr_t)0xdeadbeef;
        hr = ID3D12Resource_Map(cb[0], 1, NULL, (void **)&cb_data[0]);
        ok(hr == E_INVALIDARG, "Got unexpected hr %#x.\n", hr);
        ok(cb_data[0] == (void *)(uintptr_t)0xdeadbeef, "Pointer was modified %p.\n", cb_data[0]);

        cb_data[0] = NULL;
        hr = ID3D12Resource_Map(cb[0], 0, NULL, (void **)&cb_data[0]);
        ok(hr == S_OK, "Got unexpected hr %#x.\n", hr);
        ok(cb_data[0], "Got NULL pointer.\n");
        ID3D12Resource_Unmap(cb[0], 1, NULL);
        ID3D12Resource_Unmap(cb[0], 0, NULL);

        /* Passing NULL to Map should map, but not disclose the CPU VA to caller. */
        hr = ID3D12Resource_Map(cb[0], 0, NULL, NULL);
        ok(hr == S_OK, "Got unexpected hr %#x.\n", hr);
        ID3D12Resource_Unmap(cb[0], 0, NULL);

        ID3D12Resource_Release(cb[0]);
        ID3D12Heap_Release(gpu_upload_heap);
    }
    else
        ok(hr == E_INVALIDARG, "Got unexpected hr %#x.\n", hr);

    destroy_test_context(&context);
}

#define check_copyable_footprints(a, b, c, d, e, f, g, h) \
        check_copyable_footprints_(__LINE__, a, b, c, d, e, f, g, h)
static void check_copyable_footprints_(unsigned int line, const D3D12_RESOURCE_DESC *desc,
        unsigned int sub_resource_idx, unsigned int sub_resource_count, uint64_t base_offset,
        const D3D12_PLACED_SUBRESOURCE_FOOTPRINT *layouts, const UINT *row_counts,
        const uint64_t *row_sizes, uint64_t *total_size)
{
    unsigned int miplevel, width, height, depth, row_count, row_size, row_pitch, row_alignment, layers, plane, num_planes;
    unsigned int subsample_x_log2, subsample_y_log2;
    uint64_t offset, size, total;
    unsigned int i;

    layers = desc->Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D ? 1 : desc->DepthOrArraySize;
    num_planes = format_num_planes(desc->Format);
    offset = total = 0;

    for (i = 0; i < sub_resource_count; ++i)
    {
        miplevel = (sub_resource_idx + i) % desc->MipLevels;
        plane = (sub_resource_idx + i) / (desc->MipLevels * layers);
        format_subsample_log2(desc->Format, plane, &subsample_x_log2, &subsample_y_log2);
        width = align(max(1, desc->Width >> (miplevel + subsample_x_log2)), format_block_width(desc->Format));
        height = align(max(1, desc->Height >> (miplevel + subsample_y_log2)), format_block_height(desc->Format));
        depth = desc->Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D ? desc->DepthOrArraySize : 1;
        depth = max(1, depth >> miplevel);
        row_count = height / format_block_height(desc->Format);
        row_size = (width / format_block_width(desc->Format)) * format_size_planar(desc->Format, plane);

        /* For whatever reason, depth-stencil images and some video formats actually have 512 byte row alignment,
         * not 256. Both WARP and NV driver have this behavior, so it might be an undocumented requirement.
         * This function is likely part of the core runtime though ... */
        row_alignment = D3D12_TEXTURE_DATA_PITCH_ALIGNMENT;
        if (num_planes == 2 || desc->Format == DXGI_FORMAT_420_OPAQUE)
            row_alignment = D3D12_TEXTURE_DATA_PITCH_ALIGNMENT * 2;
        row_pitch = align(row_size, row_alignment);

        if (layouts)
        {
            const D3D12_PLACED_SUBRESOURCE_FOOTPRINT *l = &layouts[i];
            const D3D12_SUBRESOURCE_FOOTPRINT *f = &l->Footprint;
            DXGI_FORMAT footprint_format;

            footprint_format = format_to_footprint_format(desc->Format, plane);
            ok_(line)(l->Offset == base_offset + offset,
                    "Got offset %"PRIu64", expected %"PRIu64".\n", l->Offset, base_offset + offset);
            ok_(line)(f->Format == footprint_format, "Got format %#x, expected %#x.\n", f->Format, footprint_format);
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
        size = max(0, depth - 1) * align(size, D3D12_TEXTURE_DATA_PITCH_ALIGNMENT * num_planes) + size;

        total = offset + size;
        offset = align(total, D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT);
    }

    if (total_size)
        ok_(line)(*total_size == total, "Got total size %"PRIu64", expected %"PRIu64".\n", *total_size, total);
}

void test_get_copyable_footprints_planar(void)
{
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprints[2 * 2 * 3];
    UINT64 row_sizes[ARRAY_SIZE(footprints)];
    UINT row_counts[ARRAY_SIZE(footprints)];
    D3D12_RESOURCE_DESC desc;
    ID3D12Device *device;
    UINT64 total_bytes;
    unsigned int i;

    /* All of these formats will have R32_TYPELESS + R8_TYPELESS placements. */
    static const DXGI_FORMAT planar_formats[] =
    {
        DXGI_FORMAT_D32_FLOAT_S8X24_UINT,
        DXGI_FORMAT_D24_UNORM_S8_UINT,
        DXGI_FORMAT_R24G8_TYPELESS,
        DXGI_FORMAT_R32G8X24_TYPELESS,
    };

    if (!(device = create_device()))
    {
        skip("Failed to create device.\n");
        return;
    }

    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = 130;
    desc.Height = 119;
    desc.DepthOrArraySize = 2;
    desc.MipLevels = 3;
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    desc.Alignment = 0;

    for (i = 0; i < ARRAY_SIZE(planar_formats); i++)
    {
        vkd3d_test_set_context("Test %u", i);
        desc.Format = planar_formats[i];
        ID3D12Device_GetCopyableFootprints(device, &desc, 0, ARRAY_SIZE(footprints), 0,
                footprints, row_counts, row_sizes, &total_bytes);
        check_copyable_footprints(&desc, 0, ARRAY_SIZE(footprints), 0, footprints, row_counts, row_sizes, &total_bytes);
    }
    vkd3d_test_set_context(NULL);

    ID3D12Device_Release(device);
}

void test_get_copyable_footprints(void)
{
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT layouts[10];
    D3D12_RESOURCE_DESC resource_desc;
    UINT64 row_sizes[10], total_size;
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

void test_suballocate_small_textures_size(void)
{
    /* A strict test. Should expose any case where a driver is pessimizing our allocation patterns. */
    D3D12_RESOURCE_ALLOCATION_INFO info_normal;
    D3D12_RESOURCE_ALLOCATION_INFO info_small;
    D3D12_RESOURCE_DESC resource_desc;
    ID3D12Device *device;
    bool is_old_radv_gpu;
    unsigned int i;
    bool is_radv;

    static const struct test
    {
        unsigned int bpp;
        DXGI_FORMAT format;
    } tests[] =
    {
        /* 4-bpp compressed */
        { 4, DXGI_FORMAT_BC1_UNORM },
        { 4, DXGI_FORMAT_BC4_UNORM },

        /* 8-bpp compressed */
        { 8, DXGI_FORMAT_BC2_UNORM },
        { 8, DXGI_FORMAT_BC3_UNORM },
        { 8, DXGI_FORMAT_BC5_UNORM },
        { 8, DXGI_FORMAT_BC6H_SF16 },
        { 8, DXGI_FORMAT_BC6H_UF16 },
        { 8, DXGI_FORMAT_BC7_UNORM },

        /* Avoid formats where we trigger "shader needs RT copy" fallbacks.
         * RT usage triggers 64 KiB. */
#if 0
        /* 8-bpp */
        { 8, DXGI_FORMAT_R8_UNORM },
        { 8, DXGI_FORMAT_R8_UINT },
        { 8, DXGI_FORMAT_R8_TYPELESS },
#endif

        /* 16-bpp */
        { 16, DXGI_FORMAT_R8G8_UNORM },
        { 16, DXGI_FORMAT_R8G8_UINT },
        { 16, DXGI_FORMAT_R8G8_TYPELESS },
#if 0
        { 16, DXGI_FORMAT_R16_UNORM },
        { 16, DXGI_FORMAT_R16_UINT },
        { 16, DXGI_FORMAT_R16_TYPELESS },
#endif

        /* 32-bpp */
        { 32, DXGI_FORMAT_R8G8B8A8_UNORM },
        { 32, DXGI_FORMAT_R16G16_FLOAT },
        { 32, DXGI_FORMAT_R16G16_TYPELESS },
#if 0
        { 32, DXGI_FORMAT_R32_TYPELESS },
        { 32, DXGI_FORMAT_R32_UINT },
        { 32, DXGI_FORMAT_R32_FLOAT },
#endif

        /* 64-bpp */
        { 64, DXGI_FORMAT_R16G16B16A16_FLOAT },
        { 64, DXGI_FORMAT_R32G32_UINT },
        { 64, DXGI_FORMAT_R32G32_FLOAT },
        { 64, DXGI_FORMAT_R32G32_TYPELESS },

        /* 128-bpp */
        { 128, DXGI_FORMAT_R32G32B32A32_UINT },
        { 128, DXGI_FORMAT_R32G32B32A32_FLOAT },
        { 128, DXGI_FORMAT_R32G32B32A32_SINT },
        { 128, DXGI_FORMAT_R32G32B32A32_TYPELESS },
    };

    if (!(device = create_device()))
    {
        skip("Failed to create device.\n");
        return;
    }

    /* Pre GFX9 does not expose VK_MESA_image_alignment_control. */
    is_radv = is_radv_device(device);
    is_old_radv_gpu = is_radv && !is_vk_device_extension_supported(device, "VK_MESA_image_alignment_control");

    for (i = 0; i < ARRAY_SIZE(tests); i++)
    {
        unsigned int max_levels, max_layers;
        unsigned int levels, layers;
        unsigned int size_config;

        static const struct size_config
        {
            unsigned int bpp;
            unsigned int width;
            unsigned int height;
            unsigned int max_levels;
        } size_configs[] = {
            { 4, 512, 256, 9 },
            { 4, 256, 512, 9 },
            { 4, 1024, 128, 8 },
            { 4, 128, 1024, 8 },
            { 4, 256, 256, 9 },

            { 8, 256, 256, 9 },
            { 8, 512, 128, 8 },
            { 8, 128, 512, 8 },
            { 8, 128, 256, 8 },
            { 8, 256, 128, 8 },

            { 16, 256, 128, 8 },
            { 16, 128, 256, 8 },
            { 16, 512, 64, 7 },
            { 16, 64, 512, 7 },
            { 16, 128, 128, 8 },

            { 32, 128, 128, 8 },
            { 32, 256, 64, 7 },
            { 32, 64, 256, 7 },
            { 32, 64, 128, 7 },
            { 32, 128, 64, 7 },

            { 64, 128, 64, 7 },
            { 64, 64, 128, 7 },
            { 64, 256, 32, 6 },
            { 64, 32, 256, 6 },
            { 64, 64, 64, 7 },

            { 128, 64, 64, 7 },
            { 128, 128, 32, 6 },
            { 128, 32, 128, 6 },
            { 128, 64, 32, 6 },
            { 128, 32, 64, 6 },
        };

        memset(&resource_desc, 0, sizeof(resource_desc));
        resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        resource_desc.Format = tests[i].format;
        resource_desc.SampleDesc.Count = 1;

        for (size_config = 0; size_config < ARRAY_SIZE(size_configs); size_config++)
        {
            const struct size_config *config = &size_configs[size_config];
            bool is_bugged_thin;
            bool is_thin;
            if (config->bpp != tests[i].bpp)
                continue;

            max_levels = config->max_levels;
            max_layers = 16;
            resource_desc.Width = config->width;
            resource_desc.Height = config->height;

            is_thin = config->width * 8 <= config->height;

            for (layers = 1; layers <= max_layers; layers++)
            {
                for (levels = 1; levels <= max_levels; levels++)
                {
                    unsigned int expected_size;

                    /* RADV fails on Vega for levels >= 4 on very thin textures.
                     * Likely a hardware limitation with very thin textures. This isn't too surprising.
                     * It is unknown what native does here. */
                    is_bugged_thin = is_radv && is_thin && levels >= 4;

                    if (is_adreno_device(device))
                    {
                        /* Adreno has layout rules because of which the required memory size
                         * can't necessarily be captured in some conservative estimate. The
                         * calculation below should provide the exact required size, but doesn't
                         * address specific things we don't hit here (e.g. 3D textures and UBWC). */
                        unsigned int block_size, pitchalign, layer_size, l;

                        block_size = format_size(tests[i].format);
                        pitchalign = block_size * 64;
                        if (block_size == 2)
                            pitchalign *= 2;

                        /* Stride for each level is calculated based on the number of horizontal blocks
                         * and aligned to the required pitch alignment. Number of vertical blocks is
                         * aligned to 16 for levels that use tiled mode, and aligned to 4 for the last
                         * level due to hardware access. */
                        layer_size = 0;
                        for (l = 0; l < levels; ++l)
                        {
                            unsigned int nblocksx, nblocksy;

                            nblocksx = max((config->width / format_block_width(tests[i].format)) >> l, 1);
                            nblocksy = max((config->height / format_block_height(tests[i].format)) >> l, 1);
                            if ((config->width >> l) >= 16)
                                nblocksy = align(nblocksy, 16);
                            if ((l + 1) == levels)
                                nblocksy = align(nblocksy, 4);

                            layer_size += align(nblocksx * block_size, pitchalign) * nblocksy;
                        }

                        /* Specific alignment is required for layer size of any non-3D texture. */
                        expected_size = align(layer_size, 4096) * layers;
                    }
                    else
                    {
                        /* This assumes tight packing without compression metadata.
                         * Generally compression is not allowed for placed non-RTV/DSV in D3D12. */
                        expected_size = (config->width * config->height * config->bpp) / 8;

                        /* Be a bit conservative and allow 2x overflow for mipmaps. Tuned so native implementations pass. */
                        if (levels > 1)
                            expected_size = 2 * expected_size;
                        expected_size *= layers;
                    }

                    vkd3d_test_set_context("Test %u: fmt #%x, bpp %u, width %u, height %u, levels %u, layers %u",
                            i, tests[i].format, config->bpp,
                            config->width, config->height, levels, layers);

                    resource_desc.DepthOrArraySize = layers;
                    resource_desc.MipLevels = levels;

                    resource_desc.Alignment = D3D12_SMALL_RESOURCE_PLACEMENT_ALIGNMENT;
                    info_small = ID3D12Device_GetResourceAllocationInfo(device, 0, 1, &resource_desc);
                    resource_desc.Alignment = 0;
                    info_normal = ID3D12Device_GetResourceAllocationInfo(device, 0, 1, &resource_desc);

                    bug_if(is_old_radv_gpu || is_bugged_thin)
                    ok(info_small.Alignment == D3D12_SMALL_RESOURCE_PLACEMENT_ALIGNMENT, "Alignment is not 4 KiB.\n");
                    bug_if(is_old_radv_gpu || is_bugged_thin)
                    ok(info_normal.Alignment == D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT, "Alignment is not 64 KiB.\n");
                    bug_if(is_old_radv_gpu || is_bugged_thin)
                    ok(info_small.SizeInBytes <= expected_size,
                            "Resource size %u is larger than expected %u.\n",
                            (unsigned int)info_small.SizeInBytes, expected_size);
                    bug_if(is_old_radv_gpu || is_bugged_thin)
                    ok(info_normal.SizeInBytes <= align(expected_size, D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT),
                            "Resource size %u is larger than expected %zu.\n",
                            (unsigned int)info_normal.SizeInBytes,
                            align(expected_size, D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT));

                    /* It's not guaranteed that sizeof(small) <= sizeof(normal).
                     * What we want to check here is that implementation doesn't magically pad the resource out. */
                    bug_if(is_old_radv_gpu || is_bugged_thin)
                    ok(info_normal.SizeInBytes + D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT / 2 >= info_small.SizeInBytes,
                            "Small resource is oddly padded (%u vs %u).\n",
                            (unsigned int)info_small.SizeInBytes, (unsigned int)info_normal.SizeInBytes);
                }
            }
        }
    }

    vkd3d_test_set_context(NULL);
    ID3D12Device_Release(device);
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

void test_read_subresource_rt(void)
{
    const FLOAT white[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    D3D12_CPU_DESCRIPTOR_HANDLE desc_handle;
    D3D12_HEAP_PROPERTIES heap_properties;
    D3D12_RESOURCE_DESC resource_desc;
    ID3D12DescriptorHeap *desc_heap;
    struct test_context_desc desc;
    struct test_context context;
    ID3D12Resource *resource;
    uint32_t pixels[4 * 4];
    ID3D12Device *device;
    D3D12_RECT rect;
    uint32_t pixel;
    uint32_t x, y;
    D3D12_BOX box;
    HRESULT hr;

    memset(&desc, 0, sizeof(desc));
    desc.no_pipeline = true;
    desc.no_render_target = true;
    desc.no_render_target = true;
    if (!init_test_context(&context, &desc))
        return;

    device = context.device;

    resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    resource_desc.Alignment = 0;
    resource_desc.Width = 4;
    resource_desc.Height = 4;
    resource_desc.DepthOrArraySize = 1;
    resource_desc.MipLevels = 1;
    resource_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    resource_desc.SampleDesc.Count = 1;
    resource_desc.SampleDesc.Quality = 0;
    resource_desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    resource_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

    memset(&heap_properties, 0, sizeof(heap_properties));
    heap_properties.Type = D3D12_HEAP_TYPE_CUSTOM;
    heap_properties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_WRITE_BACK;
    heap_properties.MemoryPoolPreference = D3D12_MEMORY_POOL_L0;
    hr = ID3D12Device_CreateCommittedResource(device, &heap_properties, D3D12_HEAP_FLAG_NONE,
            &resource_desc, D3D12_RESOURCE_STATE_COMMON, NULL, &IID_ID3D12Resource, (void **)&resource);

    if (FAILED(hr))
    {
        skip("Cannot create CPU accessible render target. Skipping test.\n");
        destroy_test_context(&context);
        return;
    }

    pixel = 0x80808080;
    ID3D12Resource_Map(resource, 0, NULL, NULL);
    for (y = 0; y < 4; y++)
    {
        for (x = 0; x < 4; x++)
        {
            set_box(&box, x, y, 0, x + 1, y + 1, 1);
            ID3D12Resource_WriteToSubresource(resource, 0, &box, &pixel,
                    sizeof(uint32_t), sizeof(uint32_t));
        }
    }
    ID3D12Resource_Unmap(resource, 0, NULL);

    desc_heap = create_cpu_descriptor_heap(device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1);
    desc_handle = ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(desc_heap);
    ID3D12Device_CreateRenderTargetView(device, resource, NULL, desc_handle);
    transition_resource_state(context.list, resource,
            D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_RENDER_TARGET);

    for (x = 0; x < 4; x++)
    {
        set_rect(&rect, x, x, x + 1, x + 1);
        ID3D12GraphicsCommandList_ClearRenderTargetView(context.list, desc_handle, white, 1, &rect);
    }

    transition_resource_state(context.list, resource,
            D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COMMON);

    ID3D12GraphicsCommandList_Close(context.list);
    exec_command_list(context.queue, context.list);
    wait_queue_idle(device, context.queue);

    ID3D12Resource_Map(resource, 0, NULL, NULL);
    set_box(&box, 0, 0, 0, 4, 4, 1);
    ID3D12Resource_ReadFromSubresource(resource, pixels,
            4 * sizeof(uint32_t), 16 * sizeof(uint32_t), 0, &box);
    ID3D12Resource_Unmap(resource, 0, NULL);

    for (y = 0; y < 4; y++)
    {
        for (x = 0; x < 4; x++)
        {
            uint32_t expected = x == y ? UINT32_MAX : pixel;
            ok(pixels[y * 4 + x] == expected, "Pixel %u, %u: %#x != %#x\n", x, y, pixels[y * 4 + x], expected);
        }
    }

    ID3D12DescriptorHeap_Release(desc_heap);
    ID3D12Resource_Release(resource);

    destroy_test_context(&context);
}

/* Reduced test case which runs on more implementations. */
void test_read_write_subresource_2d(void)
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
    unsigned int row_pitch;
    uint32_t got, expected;
    unsigned int x, y, i;
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
    buffer_size = slice_pitch * 1;

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
    resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    resource_desc.Alignment = 0;
    resource_desc.Width = 128;
    resource_desc.Height = 100;
    resource_desc.DepthOrArraySize = 1;
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
    set_box(&box, 0, 0, 0, 128, 100, 2);
    hr = ID3D12Resource_ReadFromSubresource(src_texture, dst_buffer, row_pitch, slice_pitch, 0, &box);
    ok(hr == E_INVALIDARG, "Got unexpected hr %#x.\n", hr);

    set_box(&box, 0, 0, 2, 128, 100, 2);
    hr = ID3D12Resource_ReadFromSubresource(src_texture, dst_buffer, row_pitch, slice_pitch, 0, &box);
    ok(hr == E_INVALIDARG, "Got unexpected hr %#x.\n", hr);

    set_box(&box, 128, 0, 0, 129, 100, 1);
    hr = ID3D12Resource_ReadFromSubresource(src_texture, dst_buffer, row_pitch, slice_pitch, 0, &box);
    ok(hr == E_INVALIDARG, "Got unexpected hr %#x.\n", hr);

    /* NULL box */
    hr = ID3D12Resource_WriteToSubresource(src_texture, 0, NULL, dst_buffer, row_pitch, slice_pitch);
    ok(hr == S_OK, "Got unexpected hr %#x.\n", hr);

    hr = ID3D12Resource_ReadFromSubresource(src_texture, dst_buffer, row_pitch, slice_pitch, 0, NULL);
    ok(hr == S_OK, "Got unexpected hr %#x.\n", hr);

    /* Empty box */
    set_box(&box, 128, 100, 1, 128, 100, 1);
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

        for (y = 0; y < 100; ++y)
        {
            for (x = 0; x < 128; ++x)
            {
                ptr = &dst_buffer[y * 128 + x];
                if (x < 2 && y < 2) /* Region 1 */
                    *ptr = (y + 1) << 8 | (x + 1);
                else if (2 <= x && x < 11 && 2 <= y && y < 13) /* Region 2 */
                    *ptr = (y + 2) << 8 | (x + 2);
                else
                    *ptr = 0xdeadbeef;
            }
        }

        if (i)
        {
            hr = ID3D12Resource_WriteToSubresource(src_texture, 0, NULL, zero_buffer, row_pitch, slice_pitch);
            ok(hr == S_OK, "Got unexpected hr %#x.\n", hr);

            /* Write region 1 */
            set_box(&box, 0, 0, 0, 2, 2, 1);
            hr = ID3D12Resource_WriteToSubresource(src_texture, 0, &box, dst_buffer, row_pitch, slice_pitch);
            ok(hr == S_OK, "Got unexpected hr %#x.\n", hr);

            /* Write region 2 */
            set_box(&box, 2, 2, 0, 11, 13, 1);
            hr = ID3D12Resource_WriteToSubresource(src_texture, 0, &box, &dst_buffer[2 * 128 + 2],
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
        set_box(&box, 0, 0, 0, 2, 2, 1);
        hr = ID3D12Resource_ReadFromSubresource(src_texture, dst_buffer, row_pitch, slice_pitch, 0, &box);
        ok(hr == S_OK, "Got unexpected hr %#x.\n", hr);

        /* Read region 2 */
        set_box(&box, 2, 2, 0, 11, 13, 1);
        hr = ID3D12Resource_ReadFromSubresource(src_texture, &dst_buffer[2 * 128 + 2], row_pitch,
                slice_pitch, 0, &box);
        ok(hr == S_OK, "Got unexpected hr %#x.\n", hr);

        for (y = 0; y < 100; ++y)
        {
            for (x = 0; x < 128; ++x)
            {
                if (x < 2 && y < 2) /* Region 1 */
                    expected = (y + 1) << 8 | (x + 1);
                else if (2 <= x && x < 11 && 2 <= y && y < 13) /* Region 2 */
                    expected = (y + 2) << 8 | (x + 2);
                else /* Untouched */
                    expected = 0;

                got = dst_buffer[y * 128 + x];
                if (got != expected)
                    break;
            }
            if (got != expected)
                break;
        }
        ok(got == expected, "Got unexpected value 0x%08x at (%u, %u), expected 0x%08x.\n", got, x, y, expected);
    }
    vkd3d_test_set_context(NULL);

    /* Test layout is the same */
    dst_texture = create_default_texture2d(device, 128, 100, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, 0,
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
    set_box(&box, 0, 0, 0, 128, 100, 1);
    ID3D12GraphicsCommandList_CopyTextureRegion(command_list, &dst_location, 0, 0, 0, &src_location, &box);

    transition_resource_state(command_list, dst_texture,
            D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COPY_SOURCE);
    get_texture_readback_with_command_list(dst_texture, 0, &rb, queue, command_list);
    for (y = 0; y < 100; ++y)
    {
        for (x = 0; x < 128; ++x)
        {
            if (x < 2 && y < 2) /* Region 1 */
                expected = (y + 1) << 8 | (x + 1);
            else if (2 <= x && x < 11 && 2 <= y && y < 13) /* Region 2 */
                expected = (y + 2) << 8 | (x + 2);
            else /* Untouched */
                expected = 0;

            got = get_readback_uint(&rb, x, y, 0);
            if (got != expected)
                break;
        }
        if (got != expected)
            break;
    }
    ok(got == expected, "Got unexpected value 0x%08x at (%u, %u), expected 0x%08x.\n", got, x, y, expected);
    release_resource_readback(&rb);

    ID3D12Resource_Release(src_texture);
    ID3D12Resource_Release(dst_texture);

done:
    free(dst_buffer);
    free(zero_buffer);
    destroy_test_context(&context);
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
        ok(hr == S_OK, "Got unexpected hr %#x.\n", hr);

        /* Read region 2 */
        set_box(&box, 2, 2, 2, 11, 13, 17);
        hr = ID3D12Resource_ReadFromSubresource(src_texture, &dst_buffer[2 * 128 * 100 + 2 * 128 + 2], row_pitch,
                slice_pitch, 0, &box);
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

#include "shaders/resource/headers/cs_clear_buffer.h"

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

    pipeline_state = create_compute_pipeline_state(context->device, root_signature, cs_clear_buffer_dxbc);

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
            /* Ensures we sometimes hit dedicated allocation paths. (2 MiB limit). */
            alloc_size = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT * (1 + rand_r(&seed) % 40);
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

void test_stress_fallback_render_target_allocation_device(void)
{
    D3D12_RESOURCE_ALLOCATION_INFO alloc_info;
    struct test_context context;
    D3D12_HEAP_DESC heap_desc;
    D3D12_RESOURCE_DESC desc;
    ID3D12Resource *resource;
    ID3D12Heap *heaps[1024];
    unsigned int i;
    HRESULT hr;

    if (!init_compute_test_context(&context))
        return;

    /* Spam allocate enough that we should exhaust VRAM and require fallbacks to system memory.
     * Verify that we don't collapse in such a situation.
     * Render targets hit some particular edge cases on NV that we should focus on testing. */

    memset(&desc, 0, sizeof(desc));
    desc.Width = 2048;
    desc.Height = 2048;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.SampleDesc.Count = 1;

    alloc_info = ID3D12Device_GetResourceAllocationInfo(context.device, 0, 1, &desc);

    memset(&heap_desc, 0, sizeof(heap_desc));
    heap_desc.SizeInBytes = alloc_info.SizeInBytes;
    heap_desc.Flags = D3D12_HEAP_FLAG_ALLOW_ONLY_RT_DS_TEXTURES;
    heap_desc.Properties.Type = D3D12_HEAP_TYPE_DEFAULT;
    memset(heaps, 0, sizeof(heaps));

    for (i = 0; i < ARRAY_SIZE(heaps); i++)
    {
        hr = ID3D12Device_CreateHeap(context.device, &heap_desc, &IID_ID3D12Heap, (void**)&heaps[i]);
        ok(SUCCEEDED(hr), "Failed to create heap, hr #%x.\n", hr);

        if (SUCCEEDED(hr))
        {
            hr = ID3D12Device_CreatePlacedResource(context.device, heaps[i], 0, &desc,
                    D3D12_RESOURCE_STATE_RENDER_TARGET, NULL, &IID_ID3D12Resource, (void**)&resource);
            ok(SUCCEEDED(hr), "Failed to place resource, hr #%x.\n", hr);
            if (SUCCEEDED(hr))
                ID3D12Resource_Release(resource);
        }
    }

    for (i = 0; i < ARRAY_SIZE(heaps); i++)
        if (heaps[i])
            ID3D12Heap_Release(heaps[i]);

    destroy_test_context(&context);
}

void test_stress_suballocation_rebar(void)
{
    ID3D12Resource *resources_suballocate[4096];
    ID3D12Resource *resources_direct[1024];
    struct test_context context;
    unsigned int i;

    if (!init_compute_test_context(&context))
        return;

    /* Spam allocate enough that we should either exhaust small BAR, or our budget.
     * Verify that we don't collapse in such a situation. */

    for (i = 0; i < ARRAY_SIZE(resources_suballocate); i++)
    {
        resources_suballocate[i] = create_upload_buffer(context.device, 256 * 1024, NULL);
        ok(!!resources_suballocate[i], "Failed to create buffer.\n");
    }

    for (i = 0; i < ARRAY_SIZE(resources_suballocate); i++)
        if (resources_suballocate[i])
            ID3D12Resource_Release(resources_suballocate[i]);

    for (i = 0; i < ARRAY_SIZE(resources_direct); i++)
    {
        resources_direct[i] = create_upload_buffer(context.device, 2 * 1024 * 1024, NULL);
        ok(!!resources_direct[i], "Failed to create buffer.\n");
    }

    for (i = 0; i < ARRAY_SIZE(resources_direct); i++)
        if (resources_direct[i])
            ID3D12Resource_Release(resources_direct[i]);

    destroy_test_context(&context);
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
    ok(alloc_info.Alignment <= D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT, "Requirement alignment %"PRIu64" is > 64KiB.\n", alloc_info.Alignment);

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

void test_map_texture_validation(void)
{
    D3D12_RESOURCE_ALLOCATION_INFO alloc_info;
    bool is_gpu_upload_heap_supported;
    D3D12_HEAP_PROPERTIES heap_props;
    struct test_context context;
    D3D12_HEAP_DESC heap_desc;
    D3D12_RESOURCE_DESC desc;
    ID3D12Resource *resource;
    ID3D12Device *device;
    ID3D12Heap *heap;
    void *mapped_ptr;
    unsigned int i;
    HRESULT hr;

    struct test
    {
        D3D12_HEAP_FLAGS heap_flags;
        D3D12_TEXTURE_LAYOUT layout;
        D3D12_RESOURCE_DIMENSION dimension;
        UINT mip_levels;
        UINT depth_or_array_size;
        D3D12_RESOURCE_FLAGS flags;
        D3D12_CPU_PAGE_PROPERTY page_property;
        HRESULT heap_creation_hr;
        HRESULT creation_hr;
        HRESULT map_hr_with_ppdata;
        HRESULT map_hr_without_ppdata;
        D3D12_HEAP_TYPE heap_type;
        bool is_todo;
    };

    /* Various weird cases all come together to make mapping ROW_MAJOR textures impossible in D3D12. */
    static const struct test tests[] =
    {
        /* MipLevel 2 not allowed. */
        { D3D12_HEAP_FLAG_SHARED_CROSS_ADAPTER | D3D12_HEAP_FLAG_SHARED,
                D3D12_TEXTURE_LAYOUT_ROW_MAJOR,
                D3D12_RESOURCE_DIMENSION_TEXTURE2D,
                2, 1, D3D12_RESOURCE_FLAG_ALLOW_CROSS_ADAPTER,
                D3D12_CPU_PAGE_PROPERTY_WRITE_BACK,
                E_INVALIDARG, E_INVALIDARG, E_INVALIDARG, E_INVALIDARG, D3D12_HEAP_TYPE_CUSTOM },

        /* LayerCount 2 not allowed. */
        { D3D12_HEAP_FLAG_SHARED_CROSS_ADAPTER | D3D12_HEAP_FLAG_SHARED,
                D3D12_TEXTURE_LAYOUT_ROW_MAJOR,
                D3D12_RESOURCE_DIMENSION_TEXTURE2D,
                1, 2, D3D12_RESOURCE_FLAG_ALLOW_CROSS_ADAPTER,
                D3D12_CPU_PAGE_PROPERTY_WRITE_BACK,
                E_INVALIDARG, E_INVALIDARG, E_INVALIDARG, E_INVALIDARG, D3D12_HEAP_TYPE_CUSTOM },

        /* Need SHARED resource flag. */
        { D3D12_HEAP_FLAG_NONE,
                D3D12_TEXTURE_LAYOUT_ROW_MAJOR,
                D3D12_RESOURCE_DIMENSION_TEXTURE2D,
                1, 1, D3D12_RESOURCE_FLAG_NONE,
                D3D12_CPU_PAGE_PROPERTY_WRITE_BACK,
                S_OK, E_INVALIDARG, E_INVALIDARG, E_INVALIDARG, D3D12_HEAP_TYPE_CUSTOM },

        /* WRITE_BACK not allowed. */
        { D3D12_HEAP_FLAG_SHARED_CROSS_ADAPTER | D3D12_HEAP_FLAG_SHARED,
                D3D12_TEXTURE_LAYOUT_ROW_MAJOR,
                D3D12_RESOURCE_DIMENSION_TEXTURE2D,
                1, 1, D3D12_RESOURCE_FLAG_ALLOW_CROSS_ADAPTER,
                D3D12_CPU_PAGE_PROPERTY_WRITE_BACK,
                E_INVALIDARG, E_INVALIDARG, E_INVALIDARG, E_INVALIDARG, D3D12_HEAP_TYPE_CUSTOM },

        /* OK, but cannot map. */
        { D3D12_HEAP_FLAG_SHARED_CROSS_ADAPTER | D3D12_HEAP_FLAG_SHARED,
                D3D12_TEXTURE_LAYOUT_ROW_MAJOR,
                D3D12_RESOURCE_DIMENSION_TEXTURE2D,
                1, 1, D3D12_RESOURCE_FLAG_ALLOW_CROSS_ADAPTER,
                D3D12_CPU_PAGE_PROPERTY_NOT_AVAILABLE,
                S_OK, S_OK, E_INVALIDARG, E_INVALIDARG, D3D12_HEAP_TYPE_CUSTOM, true },

        /* 1D texture not allowed. */
        { D3D12_HEAP_FLAG_SHARED_CROSS_ADAPTER | D3D12_HEAP_FLAG_SHARED,
                D3D12_TEXTURE_LAYOUT_ROW_MAJOR,
                D3D12_RESOURCE_DIMENSION_TEXTURE1D,
                1, 1, D3D12_RESOURCE_FLAG_ALLOW_CROSS_ADAPTER,
                D3D12_CPU_PAGE_PROPERTY_NOT_AVAILABLE,
                S_OK, E_INVALIDARG, E_INVALIDARG, E_INVALIDARG, D3D12_HEAP_TYPE_CUSTOM },

        /* 3D texture not allowed. */
        { D3D12_HEAP_FLAG_SHARED_CROSS_ADAPTER | D3D12_HEAP_FLAG_SHARED,
                D3D12_TEXTURE_LAYOUT_ROW_MAJOR,
                D3D12_RESOURCE_DIMENSION_TEXTURE3D,
                1, 1, D3D12_RESOURCE_FLAG_ALLOW_CROSS_ADAPTER,
                D3D12_CPU_PAGE_PROPERTY_NOT_AVAILABLE,
                S_OK, E_INVALIDARG, E_INVALIDARG, E_INVALIDARG, D3D12_HEAP_TYPE_CUSTOM },

        /* UPLOAD heap not allowed. */
        { D3D12_HEAP_FLAG_SHARED_CROSS_ADAPTER | D3D12_HEAP_FLAG_SHARED,
                D3D12_TEXTURE_LAYOUT_ROW_MAJOR,
                D3D12_RESOURCE_DIMENSION_TEXTURE2D,
                1, 1, D3D12_RESOURCE_FLAG_ALLOW_CROSS_ADAPTER,
                D3D12_CPU_PAGE_PROPERTY_UNKNOWN,
                E_INVALIDARG, E_INVALIDARG, E_INVALIDARG, E_INVALIDARG, D3D12_HEAP_TYPE_UPLOAD },

        /* UPLOAD heap not allowed. */
        { D3D12_HEAP_FLAG_NONE,
                D3D12_TEXTURE_LAYOUT_UNKNOWN,
                D3D12_RESOURCE_DIMENSION_TEXTURE2D,
                2, 2, D3D12_RESOURCE_FLAG_NONE,
                D3D12_CPU_PAGE_PROPERTY_UNKNOWN,
                S_OK, E_INVALIDARG, E_INVALIDARG, E_INVALIDARG, D3D12_HEAP_TYPE_UPLOAD },

        /* GPU_UPLOAD heap not allowed. */
        { D3D12_HEAP_FLAG_SHARED_CROSS_ADAPTER | D3D12_HEAP_FLAG_SHARED,
                D3D12_TEXTURE_LAYOUT_ROW_MAJOR,
                D3D12_RESOURCE_DIMENSION_TEXTURE2D,
                1, 1, D3D12_RESOURCE_FLAG_ALLOW_CROSS_ADAPTER,
                D3D12_CPU_PAGE_PROPERTY_UNKNOWN,
                E_INVALIDARG, E_INVALIDARG, E_INVALIDARG, E_INVALIDARG, D3D12_HEAP_TYPE_GPU_UPLOAD, true },

        /* GPU_UPLOAD heap mostly allowed. */
        { D3D12_HEAP_FLAG_NONE,
                D3D12_TEXTURE_LAYOUT_UNKNOWN,
                D3D12_RESOURCE_DIMENSION_TEXTURE2D,
                2, 2, D3D12_RESOURCE_FLAG_NONE,
                D3D12_CPU_PAGE_PROPERTY_UNKNOWN,
                S_OK, S_OK, E_INVALIDARG, S_OK, D3D12_HEAP_TYPE_GPU_UPLOAD },

        /* Allowed, but cannot get concrete pointer.
         * TODO: 1D linear not supported in general. */
        { D3D12_HEAP_FLAG_NONE,
                D3D12_TEXTURE_LAYOUT_UNKNOWN,
                D3D12_RESOURCE_DIMENSION_TEXTURE1D,
                1, 1, D3D12_RESOURCE_FLAG_NONE,
                D3D12_CPU_PAGE_PROPERTY_WRITE_COMBINE,
                S_OK, S_OK, E_INVALIDARG, S_OK, D3D12_HEAP_TYPE_CUSTOM, true },

        /* Allowed, but cannot get concrete pointer. */
        { D3D12_HEAP_FLAG_NONE,
                D3D12_TEXTURE_LAYOUT_UNKNOWN,
                D3D12_RESOURCE_DIMENSION_TEXTURE2D,
                1, 1, D3D12_RESOURCE_FLAG_NONE,
                D3D12_CPU_PAGE_PROPERTY_WRITE_COMBINE,
                S_OK, S_OK, E_INVALIDARG, S_OK, D3D12_HEAP_TYPE_CUSTOM, true },

        /* Allowed, but cannot get concrete pointer.
         * TODO: Mipmapped linear not supported in general. */
        { D3D12_HEAP_FLAG_NONE,
                D3D12_TEXTURE_LAYOUT_UNKNOWN,
                D3D12_RESOURCE_DIMENSION_TEXTURE2D,
                2, 2, D3D12_RESOURCE_FLAG_NONE,
                D3D12_CPU_PAGE_PROPERTY_WRITE_COMBINE,
                S_OK, S_OK, E_INVALIDARG, S_OK, D3D12_HEAP_TYPE_CUSTOM, true },

        /* Allowed, but cannot map 3D with mip levels > 1.
         * TODO: 3D linear not supported in general. */
        { D3D12_HEAP_FLAG_NONE,
                D3D12_TEXTURE_LAYOUT_UNKNOWN,
                D3D12_RESOURCE_DIMENSION_TEXTURE3D,
                2, 2, D3D12_RESOURCE_FLAG_NONE,
                D3D12_CPU_PAGE_PROPERTY_WRITE_COMBINE,
                S_OK, S_OK, E_INVALIDARG, E_INVALIDARG, D3D12_HEAP_TYPE_CUSTOM, true },

        /* Allowed.
         * TODO: 3D linear not supported in general. */
        { D3D12_HEAP_FLAG_NONE,
                D3D12_TEXTURE_LAYOUT_UNKNOWN,
                D3D12_RESOURCE_DIMENSION_TEXTURE3D,
                1, 2, D3D12_RESOURCE_FLAG_NONE,
                D3D12_CPU_PAGE_PROPERTY_WRITE_COMBINE,
                S_OK, S_OK, E_INVALIDARG, S_OK, D3D12_HEAP_TYPE_CUSTOM, true },
    };

    if (!init_compute_test_context(&context))
        return;

    device = context.device;

    is_gpu_upload_heap_supported = device_supports_gpu_upload_heap(device);

    memset(&desc, 0, sizeof(desc));
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.Width = 64;
    desc.Height = 1;
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.SampleDesc.Count = 1;

    memset(&heap_props, 0, sizeof(heap_props));
    heap_props.Type = D3D12_HEAP_TYPE_CUSTOM;
    heap_props.MemoryPoolPreference = D3D12_MEMORY_POOL_L0;

    for (i = 0; i < ARRAY_SIZE(tests); i++)
    {
        vkd3d_test_set_context("Test %u", i);
        heap_props.CPUPageProperty = tests[i].page_property;
        desc.MipLevels = tests[i].mip_levels;
        desc.DepthOrArraySize = tests[i].depth_or_array_size;
        desc.Flags = tests[i].flags;
        desc.Layout = tests[i].layout;
        desc.Dimension = tests[i].dimension;

        heap_props.Type = tests[i].heap_type;
        switch (heap_props.Type)
        {
            case D3D12_HEAP_TYPE_UPLOAD:
                heap_props.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
                break;

            case D3D12_HEAP_TYPE_GPU_UPLOAD:
                heap_props.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
                break;

            default:
            case D3D12_HEAP_TYPE_CUSTOM:
                heap_props.MemoryPoolPreference = D3D12_MEMORY_POOL_L0;
                break;
        }

        alloc_info = ID3D12Device_GetResourceAllocationInfo(device, 0, 1, &desc);

        if (alloc_info.SizeInBytes != UINT64_MAX)
        {
            memset(&heap_desc, 0, sizeof(heap_desc));
            heap_desc.Properties = heap_props;
            heap_desc.Flags = tests[i].heap_flags;

            /* According to docs (https://docs.microsoft.com/en-us/windows/win32/direct3d12/shared-heaps),
             * with SHARED_CROSS_ADAPTER, a heap must be created with ALLOW_ALL_BUFFERS_AND_TEXTURES.
             * Unsure if this particular case requires HEAP_TIER_2? */
            if (!(heap_desc.Flags & D3D12_HEAP_FLAG_SHARED_CROSS_ADAPTER))
                heap_desc.Flags |= D3D12_HEAP_FLAG_DENY_BUFFERS | D3D12_HEAP_FLAG_DENY_RT_DS_TEXTURES;
            heap_desc.SizeInBytes = alloc_info.SizeInBytes;
            hr = ID3D12Device_CreateHeap(device, &heap_desc, &IID_ID3D12Heap, (void**)&heap);

            /* We cannot successfully create host visible linear RT on all implementations. */
            todo_if(tests[i].is_todo)
            ok(hr == (heap_props.Type != D3D12_HEAP_TYPE_GPU_UPLOAD || is_gpu_upload_heap_supported ? tests[i].heap_creation_hr : E_INVALIDARG), "Unexpected hr %#x.\n", hr);

            if (SUCCEEDED(hr))
            {
                hr = ID3D12Device_CreatePlacedResource(device, heap, 0, &desc, D3D12_RESOURCE_STATE_GENERIC_READ,
                        NULL, &IID_ID3D12Resource, (void**)&resource);
                todo_if(tests[i].is_todo) ok(hr == (heap_props.Type != D3D12_HEAP_TYPE_GPU_UPLOAD || is_gpu_upload_heap_supported ? tests[i].creation_hr : E_INVALIDARG), "Unexpected hr %#x.\n", hr);
                if (SUCCEEDED(hr))
                    ID3D12Resource_Release(resource);
                ID3D12Heap_Release(heap);
            }
        }

        hr = ID3D12Device_CreateCommittedResource(device, &heap_props, tests[i].heap_flags,
                &desc, D3D12_RESOURCE_STATE_GENERIC_READ, NULL, &IID_ID3D12Resource,
                (void**)&resource);
        todo_if(tests[i].is_todo) ok(hr == (heap_props.Type != D3D12_HEAP_TYPE_GPU_UPLOAD || is_gpu_upload_heap_supported ? tests[i].creation_hr : E_INVALIDARG), "Unexpected hr %#x.\n", hr);

        if (SUCCEEDED(hr))
        {
            hr = ID3D12Resource_Map(resource, 0, NULL, &mapped_ptr);
            ok(hr == (heap_props.Type != D3D12_HEAP_TYPE_GPU_UPLOAD || is_gpu_upload_heap_supported ? tests[i].map_hr_with_ppdata : E_INVALIDARG), "Unexpected hr %#x.\n", hr);
            if (SUCCEEDED(hr))
                ID3D12Resource_Unmap(resource, 0, NULL);

            hr = ID3D12Resource_Map(resource, 0, NULL, NULL);
            ok(hr == (heap_props.Type != D3D12_HEAP_TYPE_GPU_UPLOAD || is_gpu_upload_heap_supported ? tests[i].map_hr_without_ppdata : E_INVALIDARG), "Unexpected hr %#x.\n", hr);
            if (SUCCEEDED(hr))
                ID3D12Resource_Unmap(resource, 0, NULL);
            ID3D12Resource_Release(resource);
        }
    }
    vkd3d_test_set_context(NULL);
    destroy_test_context(&context);
}

void test_aliasing_barrier_edge_cases(void)
{
    const FLOAT color[4] = { 0.0f, 0.0f, 1.0f, 0.0f };
    D3D12_RESOURCE_ALLOCATION_INFO alloc_info;
    D3D12_CPU_DESCRIPTOR_HANDLE rtv[3];
    D3D12_RESOURCE_DESC resource_desc;
    D3D12_RESOURCE_BARRIER barrier;
    struct test_context_desc desc;
    ID3D12Resource *resources[3];
    struct test_context context;
    ID3D12DescriptorHeap *rtvs;
    D3D12_HEAP_DESC heap_desc;
    ID3D12Heap *heap;
    unsigned int i;
    HRESULT hr;

    memset(&desc, 0, sizeof(desc));
    desc.no_render_target = true;
    desc.no_pipeline = true;
    desc.no_root_signature = true;
    if (!init_test_context(&context, &desc))
        return;

    memset(&resource_desc, 0, sizeof(resource_desc));
    resource_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    resource_desc.Width = 2048;
    resource_desc.Height = 2048;
    resource_desc.DepthOrArraySize = 1;
    resource_desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    resource_desc.SampleDesc.Count = 1;
    resource_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    resource_desc.MipLevels = 1;
    resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;

    alloc_info = ID3D12Device_GetResourceAllocationInfo(context.device, 0, 1, &resource_desc);

    memset(&heap_desc, 0, sizeof(heap_desc));
    heap_desc.SizeInBytes = alloc_info.SizeInBytes;
    heap_desc.Properties.Type = D3D12_HEAP_TYPE_DEFAULT;
    heap_desc.Flags = D3D12_HEAP_FLAG_ALLOW_ONLY_RT_DS_TEXTURES;
    hr = ID3D12Device_CreateHeap(context.device, &heap_desc, &IID_ID3D12Heap, (void**)&heap);
    ok(SUCCEEDED(hr), "Failed to create heap, hr #%x.\n", hr);

    rtvs = create_cpu_descriptor_heap(context.device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, ARRAY_SIZE(resources));
    for (i = 0; i < ARRAY_SIZE(resources); i++)
    {
        D3D12_CPU_DESCRIPTOR_HANDLE h = ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(rtvs);
        h.ptr += i * ID3D12Device_GetDescriptorHandleIncrementSize(context.device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        hr = ID3D12Device_CreatePlacedResource(context.device, heap, 0, &resource_desc, D3D12_RESOURCE_STATE_RENDER_TARGET,
                NULL, &IID_ID3D12Resource, (void**)&resources[i]);
        ok(SUCCEEDED(hr), "Failed to create resource, hr #%x.\n", hr);
        ID3D12Device_CreateRenderTargetView(context.device, resources[i], NULL, h);
        rtv[i] = h;
    }

    /* D3D12 validation does not complain about any of this, and it works on native drivers, somehow ...
     * It's somewhat clear from this that aliasing barrier on its own should not modify any image layout,
     * we should only consider global memory barriers here. */
    ID3D12GraphicsCommandList_ClearRenderTargetView(context.list, rtv[0], color, 0, NULL);
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_ALIASING;
    barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier.Aliasing.pResourceBefore = resources[2];
    barrier.Aliasing.pResourceAfter = resources[1];
    ID3D12GraphicsCommandList_ResourceBarrier(context.list, 1, &barrier);
    transition_resource_state(context.list, resources[0], D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
    check_sub_resource_uint(resources[0], 0, context.queue, context.list, 0x00ff0000, 0);

    ID3D12DescriptorHeap_Release(rtvs);
    for (i = 0; i < ARRAY_SIZE(resources); i++)
        ID3D12Resource_Release(resources[i]);
    ID3D12Heap_Release(heap);

    destroy_test_context(&context);
}

#define check_video_format_subresource(a, b, c, d, e) \
    check_video_format_subresource_(__LINE__, a, b, c, d, e)
static void check_video_format_subresource_(unsigned int line, struct resource_readback *rb,
        const D3D12_RESOURCE_DESC *desc, unsigned int plane_idx, const void *data, uint32_t constant_data)
{
    unsigned int size, subsample_x_log2, subsample_y_log2;
    uint32_t got, expected;
    unsigned int x, y;

    size = format_size_planar(desc->Format, plane_idx);
    format_subsample_log2(desc->Format, plane_idx, &subsample_x_log2, &subsample_y_log2);

    expected = constant_data;
    got = 0;

    for (y = 0; y < desc->Height >> subsample_y_log2; y++)
    {
        for (x = 0; x < desc->Width >> subsample_x_log2; x++)
        {
            memcpy(&got, get_readback_data(rb, x, y, 0, size), size);

            if (data)
                memcpy(&expected, (const char*)data + size * (desc->Width * y + x), size);

            ok_(line)(got == expected, "Got %#x, expected %#x at (%u,%u), plane %u.\n", got, expected, x, y, plane_idx);
        }
    }

}

void test_planar_video_formats(void)
{
#define MAX_PLANES 2
    ID3D12DescriptorHeap *rtv_heap, *srv_uav_heap, *srv_uav_cpu_heap;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprints[MAX_PLANES];
    D3D12_TEXTURE_COPY_LOCATION src_location, dst_location;
    UINT64 row_sizes[MAX_PLANES], total_sizes[MAX_PLANES];
    D3D12_GRAPHICS_PIPELINE_STATE_DESC graphics_pso_desc;
    D3D12_COMPUTE_PIPELINE_STATE_DESC compute_pso_desc;
    D3D12_FEATURE_DATA_FORMAT_SUPPORT format_support;
    unsigned int subsample_x_log2, subsample_y_log2;
    ID3D12PipelineState *graphics_psos[MAX_PLANES];
    D3D12_DESCRIPTOR_RANGE descriptor_ranges[2];
    D3D12_FEATURE_DATA_FORMAT_INFO format_info;
    D3D12_UNORDERED_ACCESS_VIEW_DESC uav_desc;
    D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc;
    D3D12_SUBRESOURCE_DATA subresource_data;
    D3D12_RENDER_TARGET_VIEW_DESC rtv_desc;
    D3D12_HEAP_PROPERTIES heap_properties;
    unsigned int row_pitch, element_size;
    D3D12_ROOT_SIGNATURE_DESC rs_desc;
    D3D12_RESOURCE_DESC resource_desc;
    ID3D12PipelineState *compute_pso;
    D3D12_CPU_DESCRIPTOR_HANDLE rtv;
    D3D12_ROOT_PARAMETER rs_args[1];
    struct test_context_desc desc;
    ID3D12Resource *resources[2];
    struct resource_readback rb;
    struct test_context context;
    UINT row_counts[MAX_PLANES];
    unsigned char dst_data[64];
    D3D12_VIEWPORT viewport;
    unsigned int i, j, k, l;
    D3D12_RECT scissor;
    D3D12_BOX box;
    HRESULT hr;

#include "shaders/resource/headers/ps_copy_simple.h"
#include "shaders/resource/headers/cs_copy_simple.h"

    static const FLOAT clear_color[] = { 128.0f / 255.0f, 130.0f / 255.0f, 132.0f / 255.0f, 134.0f / 255.0f };
    static const UINT clear_color_uint[] = { 0xdead, 0xbeef, 0xfeed, 0xc0de };

    static const uint8_t r8_data[4][4] =
    {
        { 0x0, 0x1, 0x2, 0x3 },
        { 0x4, 0x5, 0x6, 0x7 },
        { 0x8, 0x9, 0xa, 0xb },
        { 0xc, 0xd, 0xe, 0xf },
    };

    static const uint8_t rg8_data[4][4][2] =
    {
        { {0x10,0x20}, {0x11,0x21}, {0x12,0x22}, {0x13,0x23} },
        { {0x14,0x24}, {0x15,0x25}, {0x16,0x26}, {0x17,0x27} },
        { {0x18,0x28}, {0x19,0x29}, {0x1a,0x2a}, {0x1b,0x2b} },
        { {0x1c,0x2c}, {0x1d,0x2d}, {0x1e,0x2e}, {0x1f,0x2f} },
    };

    static const uint16_t r10x6_data[4][4] =
    {
        { 0x1000, 0x2040, 0x3080, 0x40c0 },
        { 0x2100, 0x3140, 0x4180, 0x51c0 },
        { 0x3200, 0x4240, 0x5280, 0x62c0 },
        { 0x4300, 0x5340, 0x6380, 0x73c0 },
    };

    static const uint16_t rg10x6_data[4][4][2] =
    {
        { {0x4000,0x8000}, {0x5040,0x9040}, {0x6080,0xa080}, {0x70c0,0xb0c0} },
        { {0x5100,0x9100}, {0x6140,0xa140}, {0x7180,0xb180}, {0x81c0,0xc1c0} },
        { {0x6200,0xa200}, {0x7240,0xb240}, {0x8280,0xc280}, {0x92c0,0xd2c0} },
        { {0x7300,0xb300}, {0x8340,0xc340}, {0x9380,0xd380}, {0xa3c0,0xe3c0} },
    };

    static const uint16_t r16_data[4][4] =
    {
        { 0x0000, 0x1011, 0x2022, 0x3033 },
        { 0x4044, 0x5055, 0x6066, 0x7077 },
        { 0x8088, 0x9099, 0xa0aa, 0xb0bb },
        { 0xc0cc, 0xd0dd, 0xe0ee, 0xf0ff },
    };

    static const uint16_t rg16_data[4][4][2] =
    {
        { {0x0400,0x0800}, {0x1411,0x1811}, {0x24ee,0x2822}, {0x34ff,0x3833} },
        { {0x4444,0x4844}, {0x5455,0x5855}, {0x64ee,0x6866}, {0x74ff,0x7877} },
        { {0x8488,0x8888}, {0x9499,0x9899}, {0xa4ee,0xa8aa}, {0xb4ff,0xb8bb} },
        { {0xc4cc,0xc8cc}, {0xd4dd,0xd8dd}, {0xe4ee,0xe8ee}, {0xf4ff,0xf8ff} },
    };

    struct plane_info
    {
        const void *data;
        DXGI_FORMAT view_format;
        uint32_t expected_clear_value;
        uint32_t expected_clear_value_uint;
    };

    static const struct
    {
        DXGI_FORMAT format;
        UINT plane_count;
        struct plane_info planes[MAX_PLANES];
    }
    test_formats[] =
    {
        { DXGI_FORMAT_NV12, 2, { { r8_data,     DXGI_FORMAT_R8_UNORM,  0x80,   0xad   }, { rg8_data,    DXGI_FORMAT_R8G8_UNORM,   0x8280,     0xefad     } } },
        { DXGI_FORMAT_P010, 2, { { r10x6_data,  DXGI_FORMAT_R16_UNORM, 0x8080, 0xdead }, { rg10x6_data, DXGI_FORMAT_R16G16_UNORM, 0x82828080, 0xbeefdead } } },
        { DXGI_FORMAT_P016, 2, { { r16_data,    DXGI_FORMAT_R16_UNORM, 0x8080, 0xdead }, { rg16_data,   DXGI_FORMAT_R16G16_UNORM, 0x82828080, 0xbeefdead } } },
    };

    static const struct
    {
        DXGI_FORMAT format;
        unsigned int width;
        unsigned int height;
        unsigned int mips;
        unsigned int layers;
        D3D12_TEXTURE_LAYOUT layout;
        HRESULT expected_hr;
    }
    test_resource_descs[] =
    {
        { DXGI_FORMAT_NV12, 8, 6, 1, 1, D3D12_TEXTURE_LAYOUT_UNKNOWN, S_OK         },
        { DXGI_FORMAT_NV12, 8, 6, 1, 2, D3D12_TEXTURE_LAYOUT_UNKNOWN, S_OK         },
        { DXGI_FORMAT_NV12, 8, 6, 0, 1, D3D12_TEXTURE_LAYOUT_UNKNOWN, E_INVALIDARG },
        { DXGI_FORMAT_NV12, 8, 6, 2, 1, D3D12_TEXTURE_LAYOUT_UNKNOWN, E_INVALIDARG },
        { DXGI_FORMAT_NV12, 7, 6, 1, 1, D3D12_TEXTURE_LAYOUT_UNKNOWN, E_INVALIDARG },
        { DXGI_FORMAT_NV12, 8, 5, 1, 1, D3D12_TEXTURE_LAYOUT_UNKNOWN, E_INVALIDARG },
        { DXGI_FORMAT_NV12, 8, 6, 1, 1, D3D12_TEXTURE_LAYOUT_ROW_MAJOR, E_INVALIDARG },
    };

    static const UINT required_features1 = D3D12_FORMAT_SUPPORT1_TEXTURE2D | D3D12_FORMAT_SUPPORT1_SHADER_LOAD |
            D3D12_FORMAT_SUPPORT1_SHADER_SAMPLE | D3D12_FORMAT_SUPPORT1_SHADER_GATHER | D3D12_FORMAT_SUPPORT1_RENDER_TARGET |
            D3D12_FORMAT_SUPPORT1_BLENDABLE | D3D12_FORMAT_SUPPORT1_TYPED_UNORDERED_ACCESS_VIEW;
    static const UINT required_features2 = D3D12_FORMAT_SUPPORT2_UAV_TYPED_STORE;

    memset(&desc, 0, sizeof(desc));
    desc.no_render_target = true;
    desc.no_pipeline = true;
    desc.no_root_signature = true;
    if (!init_test_context(&context, &desc))
        return;

    memset(descriptor_ranges, 0, sizeof(descriptor_ranges));
    descriptor_ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    descriptor_ranges[0].NumDescriptors = 1;

    descriptor_ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    descriptor_ranges[1].OffsetInDescriptorsFromTableStart = 1;
    descriptor_ranges[1].NumDescriptors = 1;

    memset(rs_args, 0, sizeof(rs_args));
    rs_args[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rs_args[0].DescriptorTable.NumDescriptorRanges = ARRAY_SIZE(descriptor_ranges);
    rs_args[0].DescriptorTable.pDescriptorRanges = descriptor_ranges;
    rs_args[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    memset(&rs_desc, 0, sizeof(rs_desc));
    rs_desc.NumParameters = ARRAY_SIZE(rs_args);
    rs_desc.pParameters = rs_args;
    rs_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_DENY_VERTEX_SHADER_ROOT_ACCESS;

    hr = create_root_signature(context.device, &rs_desc, &context.root_signature);
    ok(hr == S_OK, "Failed to create root signature, hr %#x.\n", hr);

    memset(&compute_pso_desc, 0, sizeof(compute_pso_desc));
    compute_pso_desc.CS = cs_copy_simple_dxbc;
    compute_pso_desc.pRootSignature = context.root_signature;

    hr = ID3D12Device_CreateComputePipelineState(context.device, &compute_pso_desc, &IID_ID3D12PipelineState, (void**)&compute_pso);
    ok(hr == S_OK, "Failed to create compute pipeline, hr %#x.\n", hr);

    rtv_heap = create_cpu_descriptor_heap(context.device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1);
    rtv = get_cpu_descriptor_handle(&context, rtv_heap, 0);

    srv_uav_heap = create_gpu_descriptor_heap(context.device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, MAX_PLANES * 2);
    srv_uav_cpu_heap = create_cpu_descriptor_heap(context.device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, MAX_PLANES * 2);

    memset(&scissor, 0, sizeof(scissor));
    memset(&viewport, 0, sizeof(viewport));
    viewport.MaxDepth = 1.0f;

    for (i = 0; i < ARRAY_SIZE(test_formats); i++)
    {
        vkd3d_test_set_context("Format %#x", test_formats[i].format);

        memset(&format_support, 0, sizeof(format_support));
        format_support.Format = test_formats[i].format;

        hr = ID3D12Device_CheckFeatureSupport(context.device, D3D12_FEATURE_FORMAT_SUPPORT, &format_support, sizeof(format_support));
        ok(hr == S_OK || hr == E_FAIL, "Got invalid hr %#x.\n", hr);

        if (FAILED(hr))
        {
            skip("Format %#x unsupported.\n", test_formats[i].format);
            continue;
        }

        ok((format_support.Support1 & required_features1) == required_features1,
                "Got format features 1 = %#x, expected %#x.\n", format_support.Support1, required_features1);
        ok((format_support.Support2 & required_features2) == required_features2,
                "Got format features 1 = %#x, expected %#x.\n", format_support.Support2, required_features2);

        memset(&format_info, 0, sizeof(format_info));
        format_info.Format = test_formats[i].format;

        hr = ID3D12Device_CheckFeatureSupport(context.device, D3D12_FEATURE_FORMAT_INFO, &format_info, sizeof(format_info));
        ok(hr == S_OK, "Checking format info failed, hr %#x.\n", hr);
        ok(format_info.PlaneCount == test_formats[i].plane_count, "Got plane count %u, expected %u.\n", format_info.PlaneCount, test_formats[i].plane_count);

        /* Validate copyable footprints */
        memset(&resource_desc, 0, sizeof(resource_desc));
        resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        resource_desc.Format = test_formats[i].format;
        resource_desc.Width = 4;
        resource_desc.Height = 4;
        resource_desc.DepthOrArraySize = 1;
        resource_desc.SampleDesc.Count = 1;
        resource_desc.MipLevels = 1;
        resource_desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;

        ID3D12Device_GetCopyableFootprints(context.device, &resource_desc, 0, test_formats[i].plane_count, 0, footprints, row_counts, row_sizes, total_sizes);
        check_copyable_footprints(&resource_desc, 0, test_formats[i].plane_count, 0, footprints, row_counts, row_sizes, total_sizes);

        /* Omit opaque formats that are only supported with video interfaces */
        if (!test_formats[i].planes[0].data)
            continue;

        /* Test WriteToSubresource and ReadFromSubresource */
        memset(&heap_properties, 0, sizeof(heap_properties));
        heap_properties.Type = D3D12_HEAP_TYPE_CUSTOM;
        heap_properties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_WRITE_BACK;
        heap_properties.MemoryPoolPreference = D3D12_MEMORY_POOL_L0;

        hr = ID3D12Device_CreateCommittedResource(context.device, &heap_properties, D3D12_HEAP_FLAG_NONE,
                &resource_desc, D3D12_RESOURCE_STATE_COMMON, NULL, &IID_ID3D12Resource, (void**)&resources[0]);
        ok(hr == S_OK, "Failed to create resource, hr %#x.\n", hr);

        for (j = 0; j < test_formats[i].plane_count; j++)
        {
            const struct plane_info *plane = &test_formats[i].planes[j];

            format_subsample_log2(test_formats[i].format, j, &subsample_x_log2, &subsample_y_log2);

            element_size = format_size_planar(test_formats[i].format, j);
            row_pitch = resource_desc.Width * element_size;

            hr = ID3D12Resource_Map(resources[0], j, NULL, NULL);
            ok(hr == S_OK, "Failed to map subresource %u, hr %#x.\n", j, hr);

            hr = ID3D12Resource_WriteToSubresource(resources[0], j, NULL, plane->data, row_pitch, 0);
            ok(hr == S_OK, "Failed to write subresource %u, hr %#x.\n", j, hr);

            ID3D12Resource_Unmap(resources[0], j, NULL);

            transition_sub_resource_state(context.list, resources[0], j,
                    D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_SOURCE);

            get_texture_readback_with_command_list(resources[0], j, &rb, context.queue, context.list);
            check_video_format_subresource(&rb, &resource_desc, j, plane->data, 0);
            release_resource_readback(&rb);

            reset_command_list(context.list, context.allocator);

            hr = ID3D12Resource_Map(resources[0], j, NULL, NULL);
            ok(hr == S_OK, "Failed to map subresource %u, hr %#x.\n", j, hr);

            memset(dst_data, 0xff, sizeof(dst_data));

            hr = ID3D12Resource_ReadFromSubresource(resources[0], dst_data,
                    row_pitch, 0, j, NULL);
            ok(hr == S_OK, "Failed to read subresource %u, hr %#x.\n", j, hr);

            for (k = 0; k < resource_desc.Height; k++)
            {
                for (l = 0; l < resource_desc.Width; l++)
                {
                    uint32_t expected = 0, got = 0;

                    if (k < resource_desc.Width >> subsample_x_log2 && l < resource_desc.Height >> subsample_y_log2)
                        memcpy(&expected, (const char*)plane->data + k * row_pitch + l * element_size, element_size);
                    else
                        expected = ~0u >> (8 * (4 - element_size));

                    memcpy(&got, dst_data + k * row_pitch + l * element_size, element_size);

                    ok(got == expected, "Got %#x, expected %#x at %u,%u, subresource %u.\n",
                            got, expected, k, l, j);
                }
            }

            ID3D12Resource_Unmap(resources[0], j, NULL);
        }

        ID3D12Resource_Release(resources[0]);

        /* Test copying individual planes from color images via CopyTextureRegion */
        memset(&heap_properties, 0, sizeof(heap_properties));
        heap_properties.Type = D3D12_HEAP_TYPE_DEFAULT;

        hr = ID3D12Device_CreateCommittedResource(context.device, &heap_properties, D3D12_HEAP_FLAG_NONE,
                &resource_desc, D3D12_RESOURCE_STATE_COPY_DEST, NULL, &IID_ID3D12Resource, (void**)&resources[0]);
        ok(hr == S_OK, "Failed to create resource, hr %#x.\n", hr);

        memset(&heap_properties, 0, sizeof(heap_properties));
        heap_properties.Type = D3D12_HEAP_TYPE_CUSTOM;
        heap_properties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_WRITE_COMBINE;
        heap_properties.MemoryPoolPreference = D3D12_MEMORY_POOL_L0;

        for (j = 0; j < test_formats[i].plane_count; j++)
        {
            const struct plane_info *plane = &test_formats[i].planes[j];

            format_subsample_log2(test_formats[i].format, j, &subsample_x_log2, &subsample_y_log2);

            resource_desc.Format = plane->view_format;

            hr = ID3D12Device_CreateCommittedResource(context.device, &heap_properties, D3D12_HEAP_FLAG_NONE,
                    &resource_desc, D3D12_RESOURCE_STATE_COMMON, NULL, &IID_ID3D12Resource, (void**)&resources[1]);
            ok(hr == S_OK, "Failed to create resource, hr %#x.\n", hr);

            hr = ID3D12Resource_Map(resources[1], 0, NULL, NULL);
            ok(hr == S_OK, "Failed to map resource, hr %#x.\n", hr);

            hr = ID3D12Resource_WriteToSubresource(resources[1], 0, NULL, plane->data,
                    resource_desc.Width * format_size_planar(test_formats[i].format, j), 0);
            ok(hr == S_OK, "Failed to write resource, hr %#x.\n", hr);

            memset(&dst_location, 0, sizeof(dst_location));
            dst_location.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            dst_location.pResource = resources[0];
            dst_location.SubresourceIndex = j;

            memset(&src_location, 0, sizeof(src_location));
            src_location.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            src_location.pResource = resources[1];
            src_location.SubresourceIndex = 0;

            memset(&box, 0, sizeof(box));
            box.right = resource_desc.Width >> subsample_x_log2;
            box.bottom = resource_desc.Height >> subsample_y_log2;
            box.back = 1;

            transition_sub_resource_state(context.list, resources[1], 0,
                    D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_SOURCE);

            ID3D12GraphicsCommandList_CopyTextureRegion(context.list, &dst_location, 0, 0, 0, &src_location, &box);

            transition_sub_resource_state(context.list, resources[0], j,
                    D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COPY_SOURCE);

            get_texture_readback_with_command_list(resources[0], j, &rb, context.queue, context.list);

            resource_desc.Format = test_formats[i].format;
            check_video_format_subresource(&rb, &resource_desc, j, plane->data, 0);

            release_resource_readback(&rb);
            ID3D12Resource_Release(resources[1]);

            reset_command_list(context.list, context.allocator);
        }

        ID3D12Resource_Release(resources[0]);

        /* Test upload via CopyTextureRegion */
        memset(&heap_properties, 0, sizeof(heap_properties));
        heap_properties.Type = D3D12_HEAP_TYPE_DEFAULT;

        hr = ID3D12Device_CreateCommittedResource(context.device, &heap_properties, D3D12_HEAP_FLAG_NONE,
                &resource_desc, D3D12_RESOURCE_STATE_COPY_DEST, NULL, &IID_ID3D12Resource, (void**)&resources[0]);
        ok(hr == S_OK, "Failed to create resource, hr %#x.\n", hr);

        for (j = 0; j < test_formats[i].plane_count; j++)
        {
            const struct plane_info *plane = &test_formats[i].planes[j];

            memset(&subresource_data, 0, sizeof(subresource_data));
            subresource_data.pData = plane->data;
            subresource_data.RowPitch = resource_desc.Width * format_size_planar(test_formats[i].format, j);
            subresource_data.SlicePitch = resource_desc.Height * subresource_data.RowPitch;

            upload_texture_data_base(resources[0], &subresource_data, j, 1, context.queue, context.list);
            reset_command_list(context.list, context.allocator);

            transition_sub_resource_state(context.list, resources[0], j,
                    D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_GENERIC_READ);

            get_texture_readback_with_command_list(resources[0], j, &rb, context.queue, context.list);
            check_video_format_subresource(&rb, &resource_desc, j, plane->data, 0);
            release_resource_readback(&rb);

            reset_command_list(context.list, context.allocator);
        }

        /* Test full copy via CopyResource */
        hr = ID3D12Device_CreateCommittedResource(context.device, &heap_properties, D3D12_HEAP_FLAG_NONE,
                &resource_desc, D3D12_RESOURCE_STATE_COPY_DEST, NULL, &IID_ID3D12Resource, (void**)&resources[1]);
        ok(hr == S_OK, "Failed to create resource, hr %#x.\n", hr);

        ID3D12GraphicsCommandList_CopyResource(context.list, resources[1], resources[0]);
        transition_resource_state(context.list, resources[1], D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COPY_SOURCE);

        for (j = 0; j < test_formats[i].plane_count; j++)
        {
            const struct plane_info *plane = &test_formats[i].planes[j];

            get_texture_readback_with_command_list(resources[1], j, &rb, context.queue, context.list);

            /* AMD only copies the first plane */
            bug_if(is_amd_windows_device(context.device) && j)
            check_video_format_subresource(&rb, &resource_desc, j, plane->data, 0);

            release_resource_readback(&rb);

            reset_command_list(context.list, context.allocator);
        }

        ID3D12Resource_Release(resources[1]);

        /* Test copy via CopyTextureRegion */
        hr = ID3D12Device_CreateCommittedResource(context.device, &heap_properties, D3D12_HEAP_FLAG_NONE,
                &resource_desc, D3D12_RESOURCE_STATE_COPY_DEST, NULL, &IID_ID3D12Resource, (void**)&resources[1]);
        ok(hr == S_OK, "Failed to create resource, hr %#x.\n", hr);

        for (j = 0; j < test_formats[i].plane_count; j++)
        {
            const struct plane_info *plane = &test_formats[i].planes[j];

            memset(&dst_location, 0, sizeof(dst_location));
            dst_location.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            dst_location.pResource = resources[1];
            dst_location.SubresourceIndex = j;

            memset(&src_location, 0, sizeof(src_location));
            src_location.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            src_location.pResource = resources[0];
            src_location.SubresourceIndex = j;

            ID3D12GraphicsCommandList_CopyTextureRegion(context.list, &dst_location, 0, 0, 0, &src_location, NULL);
            transition_sub_resource_state(context.list, resources[1], j,
                    D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COPY_SOURCE);

            get_texture_readback_with_command_list(resources[1], j, &rb, context.queue, context.list);
            check_video_format_subresource(&rb, &resource_desc, j, plane->data, 0);
            release_resource_readback(&rb);

            reset_command_list(context.list, context.allocator);
        }

        ID3D12Resource_Release(resources[1]);

        /* Test copying individual planes to color image via CopyTextureRegion */
        for (j = 0; j < test_formats[i].plane_count; j++)
        {
            const struct plane_info *plane = &test_formats[i].planes[j];

            format_subsample_log2(test_formats[i].format, j, &subsample_x_log2, &subsample_y_log2);

            resource_desc.Format = plane->view_format;

            hr = ID3D12Device_CreateCommittedResource(context.device, &heap_properties, D3D12_HEAP_FLAG_NONE,
                    &resource_desc, D3D12_RESOURCE_STATE_COPY_DEST, NULL, &IID_ID3D12Resource, (void**)&resources[1]);
            ok(hr == S_OK, "Failed to create resource, hr %#x.\n", hr);

            memset(&dst_location, 0, sizeof(dst_location));
            dst_location.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            dst_location.pResource = resources[1];
            dst_location.SubresourceIndex = 0;

            memset(&src_location, 0, sizeof(src_location));
            src_location.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            src_location.pResource = resources[0];
            src_location.SubresourceIndex = j;

            memset(&box, 0, sizeof(box));
            box.right = resource_desc.Width >> subsample_x_log2;
            box.bottom = resource_desc.Height >> subsample_y_log2;
            box.back = 1;

            ID3D12GraphicsCommandList_CopyTextureRegion(context.list, &dst_location, 0, 0, 0, &src_location, &box);
            transition_sub_resource_state(context.list, resources[1], 0,
                    D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COPY_SOURCE);

            get_texture_readback_with_command_list(resources[1], 0, &rb, context.queue, context.list);

            resource_desc.Format = test_formats[i].format;
            check_video_format_subresource(&rb, &resource_desc, j, plane->data, 0);

            release_resource_readback(&rb);

            ID3D12Resource_Release(resources[1]);

            reset_command_list(context.list, context.allocator);
        }

        if (format_support.Support1 & D3D12_FORMAT_SUPPORT1_SHADER_LOAD)
        {
            for (j = 0; j < test_formats[i].plane_count; j++)
            {
                const struct plane_info *plane = &test_formats[i].planes[j];

                init_pipeline_state_desc(&graphics_pso_desc, context.root_signature, plane->view_format, NULL, &ps_copy_simple_dxbc, NULL);
                hr = ID3D12Device_CreateGraphicsPipelineState(context.device, &graphics_pso_desc, &IID_ID3D12PipelineState, (void**)&graphics_psos[j]);
                ok(hr == S_OK, "Failed to create graphics pipeline, hr %#x.\n", hr);
            }

            /* Test reading image data via SRV */
            for (j = 0; j < test_formats[i].plane_count; j++)
            {
                const struct plane_info *plane = &test_formats[i].planes[j];

                format_subsample_log2(test_formats[i].format, j, &subsample_x_log2, &subsample_y_log2);

                memset(&srv_desc, 0, sizeof(srv_desc));
                srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
                srv_desc.Format = plane->view_format;
                srv_desc.Texture2D.MostDetailedMip = 0;
                srv_desc.Texture2D.MipLevels = 1;
                srv_desc.Texture2D.PlaneSlice = j;
                srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

                ID3D12Device_CreateShaderResourceView(context.device, resources[0], &srv_desc,
                        get_cpu_descriptor_handle(&context, srv_uav_heap, 2 * j));

                resource_desc.Format = plane->view_format;
                resource_desc.Width = 4 >> subsample_x_log2;
                resource_desc.Height = 4 >> subsample_y_log2;
                resource_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

                hr = ID3D12Device_CreateCommittedResource(context.device, &heap_properties, D3D12_HEAP_FLAG_NONE,
                        &resource_desc, D3D12_RESOURCE_STATE_RENDER_TARGET, NULL, &IID_ID3D12Resource, (void**)&resources[1]);
                ok(hr == S_OK, "Failed to create resource, hr %#x.\n", hr);

                memset(&rtv_desc, 0, sizeof(rtv_desc));
                rtv_desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
                rtv_desc.Format = plane->view_format;

                ID3D12Device_CreateRenderTargetView(context.device, resources[1], &rtv_desc, rtv);

                scissor.right = resource_desc.Width;
                scissor.bottom = resource_desc.Height;

                viewport.Width = (float)resource_desc.Width;
                viewport.Height = (float)resource_desc.Height;

                ID3D12GraphicsCommandList_OMSetRenderTargets(context.list, 1, &rtv, FALSE, NULL);
                ID3D12GraphicsCommandList_SetDescriptorHeaps(context.list, 1, &srv_uav_heap);
                ID3D12GraphicsCommandList_SetGraphicsRootSignature(context.list, context.root_signature);
                ID3D12GraphicsCommandList_SetGraphicsRootDescriptorTable(context.list, 0,
                        get_gpu_descriptor_handle(&context, srv_uav_heap, 2 * j));
                ID3D12GraphicsCommandList_SetPipelineState(context.list, graphics_psos[j]);
                ID3D12GraphicsCommandList_IASetPrimitiveTopology(context.list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                ID3D12GraphicsCommandList_RSSetViewports(context.list, 1, &viewport);
                ID3D12GraphicsCommandList_RSSetScissorRects(context.list, 1, &scissor);
                ID3D12GraphicsCommandList_DrawInstanced(context.list, 3, 1, 0, 0);

                transition_resource_state(context.list, resources[1], D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);

                get_texture_readback_with_command_list(resources[1], 0, &rb, context.queue, context.list);

                resource_desc.Format = test_formats[i].format;
                resource_desc.Width = 4;
                resource_desc.Height = 4;
                check_video_format_subresource(&rb, &resource_desc, j, plane->data, 0);

                release_resource_readback(&rb);
                reset_command_list(context.list, context.allocator);

                ID3D12Resource_Release(resources[1]);
            }

            /* Test RTV */
            if (!use_warp_device && (format_support.Support1 & D3D12_FORMAT_SUPPORT1_RENDER_TARGET))
            {
                resource_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

                hr = ID3D12Device_CreateCommittedResource(context.device, &heap_properties, D3D12_HEAP_FLAG_NONE,
                        &resource_desc, D3D12_RESOURCE_STATE_RENDER_TARGET, NULL, &IID_ID3D12Resource, (void**)&resources[1]);
                ok(hr == S_OK, "Failed to create resource, hr %#x.\n", hr);

                for (j = 0; j < test_formats[i].plane_count; j++)
                {
                    const struct plane_info *plane = &test_formats[i].planes[j];

                    format_subsample_log2(test_formats[i].format, j, &subsample_x_log2, &subsample_y_log2);

                    memset(&rtv_desc, 0, sizeof(rtv_desc));
                    rtv_desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
                    rtv_desc.Format = plane->view_format;
                    rtv_desc.Texture2D.MipSlice = 0;
                    rtv_desc.Texture2D.PlaneSlice = j;

                    ID3D12Device_CreateRenderTargetView(context.device, resources[1], &rtv_desc, rtv);

                    scissor.right = resource_desc.Width >> subsample_x_log2;
                    scissor.bottom = resource_desc.Height >> subsample_y_log2;

                    viewport.Width = (float)scissor.right;
                    viewport.Height = (float)scissor.bottom;

                    ID3D12GraphicsCommandList_OMSetRenderTargets(context.list, 1, &rtv, FALSE, NULL);
                    ID3D12GraphicsCommandList_SetDescriptorHeaps(context.list, 1, &srv_uav_heap);
                    ID3D12GraphicsCommandList_SetGraphicsRootSignature(context.list, context.root_signature);
                    ID3D12GraphicsCommandList_SetGraphicsRootDescriptorTable(context.list, 0,
                            get_gpu_descriptor_handle(&context, srv_uav_heap, 2 * j));
                    ID3D12GraphicsCommandList_SetPipelineState(context.list, graphics_psos[j]);
                    ID3D12GraphicsCommandList_IASetPrimitiveTopology(context.list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                    ID3D12GraphicsCommandList_RSSetViewports(context.list, 1, &viewport);
                    ID3D12GraphicsCommandList_RSSetScissorRects(context.list, 1, &scissor);
                    ID3D12GraphicsCommandList_DrawInstanced(context.list, 3, 1, 0, 0);

                    transition_sub_resource_state(context.list, resources[1], j,
                            D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);

                    get_texture_readback_with_command_list(resources[1], j, &rb, context.queue, context.list);
                    check_video_format_subresource(&rb, &resource_desc, j, plane->data, 0);
                    release_resource_readback(&rb);

                    reset_command_list(context.list, context.allocator);

                    transition_sub_resource_state(context.list, resources[1], j,
                            D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);

                    ID3D12GraphicsCommandList_ClearRenderTargetView(context.list, rtv, clear_color, 0, NULL);

                    transition_sub_resource_state(context.list, resources[1], j,
                            D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);

                    get_texture_readback_with_command_list(resources[1], j, &rb, context.queue, context.list);

                    /* For some reason Nvidia just doesn't clear */
                    bug_if(is_nvidia_device(context.device) && !j)
                    check_video_format_subresource(&rb, &resource_desc, j, NULL, plane->expected_clear_value);

                    release_resource_readback(&rb);

                    reset_command_list(context.list, context.allocator);
                }

                ID3D12Resource_Release(resources[1]);
            }
            else
            {
                skip("RTV usage not supported for format %#x, skipping.\n", test_formats[i].format);
            }

            /* Test UAV */
            if (!use_warp_device && (format_support.Support1 & D3D12_FORMAT_SUPPORT1_TYPED_UNORDERED_ACCESS_VIEW))
            {
                resource_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

                hr = ID3D12Device_CreateCommittedResource(context.device, &heap_properties, D3D12_HEAP_FLAG_NONE,
                        &resource_desc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, NULL, &IID_ID3D12Resource, (void**)&resources[1]);
                ok(hr == S_OK, "Failed to create resource, hr %#x.\n", hr);

                for (j = 0; j < test_formats[i].plane_count; j++)
                {
                    const struct plane_info *plane = &test_formats[i].planes[j];

                    memset(&uav_desc, 0, sizeof(uav_desc));
                    uav_desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
                    uav_desc.Format = plane->view_format;
                    uav_desc.Texture2D.MipSlice = 0;
                    uav_desc.Texture2D.PlaneSlice = j;

                    ID3D12Device_CreateUnorderedAccessView(context.device, resources[1], NULL, &uav_desc,
                            get_cpu_descriptor_handle(&context, srv_uav_heap, 2 * j + 1));
                    ID3D12Device_CreateUnorderedAccessView(context.device, resources[1], NULL, &uav_desc,
                            get_cpu_descriptor_handle(&context, srv_uav_cpu_heap, 2 * j + 1));

                    ID3D12GraphicsCommandList_SetDescriptorHeaps(context.list, 1, &srv_uav_heap);
                    ID3D12GraphicsCommandList_SetComputeRootSignature(context.list, context.root_signature);
                    ID3D12GraphicsCommandList_SetComputeRootDescriptorTable(context.list, 0,
                            get_gpu_descriptor_handle(&context, srv_uav_heap, 2 * j));
                    ID3D12GraphicsCommandList_SetPipelineState(context.list, compute_pso);
                    ID3D12GraphicsCommandList_Dispatch(context.list, 1, 1, 1);

                    transition_sub_resource_state(context.list, resources[1], j,
                            D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);

                    get_texture_readback_with_command_list(resources[1], j, &rb, context.queue, context.list);
                    check_video_format_subresource(&rb, &resource_desc, j, plane->data, 0);
                    release_resource_readback(&rb);

                    reset_command_list(context.list, context.allocator);

                    transition_sub_resource_state(context.list, resources[1], j,
                            D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

                    ID3D12GraphicsCommandList_ClearUnorderedAccessViewFloat(context.list,
                            get_gpu_descriptor_handle(&context, srv_uav_heap, 2 * j + 1),
                            get_cpu_descriptor_handle(&context, srv_uav_cpu_heap, 2 * j + 1),
                            resources[1], clear_color, 0, NULL);

                    transition_sub_resource_state(context.list, resources[1], j,
                            D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);

                    get_texture_readback_with_command_list(resources[1], j, &rb, context.queue, context.list);
                    check_video_format_subresource(&rb, &resource_desc, j, NULL, plane->expected_clear_value);
                    release_resource_readback(&rb);

                    reset_command_list(context.list, context.allocator);

                    transition_sub_resource_state(context.list, resources[1], j,
                            D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

                    ID3D12GraphicsCommandList_ClearUnorderedAccessViewUint(context.list,
                            get_gpu_descriptor_handle(&context, srv_uav_heap, 2 * j + 1),
                            get_cpu_descriptor_handle(&context, srv_uav_cpu_heap, 2 * j + 1),
                            resources[1], clear_color_uint, 0, NULL);

                    transition_sub_resource_state(context.list, resources[1], j,
                            D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);

                    get_texture_readback_with_command_list(resources[1], j, &rb, context.queue, context.list);
                    check_video_format_subresource(&rb, &resource_desc, j, NULL, plane->expected_clear_value_uint);
                    release_resource_readback(&rb);

                    reset_command_list(context.list, context.allocator);
                }

                ID3D12Resource_Release(resources[1]);
            }
            else
            {
                skip("UAV usage not supported for format %#x, skipping.\n", test_formats[i].format);
            }

            for (j = 0; j < test_formats[i].plane_count; j++)
                ID3D12PipelineState_Release(graphics_psos[j]);
        }

        ID3D12Resource_Release(resources[0]);
    }

    for (i = 0; i < ARRAY_SIZE(test_resource_descs); i++)
    {
        vkd3d_test_set_context("Test %u", i);

        resources[0] = NULL;

        memset(&format_support, 0, sizeof(format_support));
        format_support.Format = test_resource_descs[i].format;

        if (FAILED(ID3D12Device_CheckFeatureSupport(context.device, D3D12_FEATURE_FORMAT_SUPPORT, &format_support, sizeof(format_support))))
            continue;

        memset(&heap_properties, 0, sizeof(heap_properties));
        heap_properties.Type = D3D12_HEAP_TYPE_DEFAULT;

        memset(&resource_desc, 0, sizeof(resource_desc));
        resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        resource_desc.Format = test_resource_descs[i].format;
        resource_desc.Width = test_resource_descs[i].width;
        resource_desc.Height = test_resource_descs[i].height;
        resource_desc.DepthOrArraySize = test_resource_descs[i].layers;
        resource_desc.SampleDesc.Count = 1;
        resource_desc.MipLevels = test_resource_descs[i].mips;
        resource_desc.Layout = test_resource_descs[i].layout;

        hr = ID3D12Device_CreateCommittedResource(context.device, &heap_properties, D3D12_HEAP_FLAG_NONE,
                &resource_desc, D3D12_RESOURCE_STATE_COPY_DEST, NULL, &IID_ID3D12Resource, (void**)&resources[0]);
        ok(hr == test_resource_descs[i].expected_hr, "Got hr %#x, expected %#x.\n", hr, test_resource_descs[i].expected_hr);

        if (resources[0])
            ID3D12Resource_Release(resources[0]);
    }

    ID3D12DescriptorHeap_Release(rtv_heap);
    ID3D12DescriptorHeap_Release(srv_uav_heap);
    ID3D12DescriptorHeap_Release(srv_uav_cpu_heap);

    ID3D12PipelineState_Release(compute_pso);

    destroy_test_context(&context);
#undef MAX_PLANES
}

void test_large_texel_buffer_view(void)
{
    ID3D12DescriptorHeap *descriptor_heap, *descriptor_cpu_heap;
    ID3D12Resource *data_buffer, *feedback_buffer;
    D3D12_UNORDERED_ACCESS_VIEW_DESC uav_desc;
    ID3D12GraphicsCommandList2 *command_list2;
    D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc;
    D3D12_DESCRIPTOR_RANGE rs_desc_ranges[2];
    ID3D12PipelineState *uav_pso, *srv_pso;
    D3D12_HEAP_PROPERTIES heap_properties;
    D3D12_ROOT_SIGNATURE_DESC rs_desc;
    D3D12_RESOURCE_DESC resource_desc;
    D3D12_ROOT_PARAMETER rs_args[2];
    struct resource_readback rb;
    struct test_context context;
    UINT clear_color[4];
    unsigned int i, j;
    HRESULT hr;

#include "shaders/resource/headers/cs_large_tbo_load.h"
#include "shaders/resource/headers/cs_large_tbo_store.h"

    static const struct
    {
        DXGI_FORMAT format;
        uint32_t element_count;
        uint32_t element_data;
    }
    tests[] =
    {
        /* Minimum required texel count for any format */
        { DXGI_FORMAT_R32G32B32A32_UINT, 1u << 27, 0xf000ba22 },

        /* Above minimum required, but supported on AMD/Nvidia */
        { DXGI_FORMAT_R32G32_UINT, 1u << 28, 0xdeadbeef },
        { DXGI_FORMAT_R32_UINT, 1u << 29, 0x01234567 },

        { DXGI_FORMAT_R16G16B16A16_UINT, 1u << 28, 0x8888 },
        { DXGI_FORMAT_R16G16_UINT, 1u << 29, 0x9999 },
        { DXGI_FORMAT_R16_UINT, 1u << 29, 0xaaaa },

        { DXGI_FORMAT_R10G10B10A2_UINT, 1u << 29, 0x2ff },

        { DXGI_FORMAT_R8G8B8A8_UINT, 1u << 29, 0xbb },
        { DXGI_FORMAT_R8G8_UINT, 1u << 29, 0xcc },
        { DXGI_FORMAT_R8_UINT, 1u << 29, 0xdd },
#if 0
        /* These fail on Nvidia native and will be clamped to 2^29 texels */
        { DXGI_FORMAT_R16_UINT, 1u << 30, 0xa5df },
        { DXGI_FORMAT_R8_UINT, 1u << 31, 0x31 },
#endif
    };

    struct
    {
        uint32_t offset;
        uint32_t data;
        uint32_t feedback;
    } shader_args;

    const struct
    {
        uint32_t element_count;
        uint32_t last_value;
    } *feedback;

    if (!init_compute_test_context(&context))
        return;

    if (FAILED(ID3D12GraphicsCommandList_QueryInterface(context.list, &IID_ID3D12GraphicsCommandList2, (void**)&command_list2)))
    {
        skip("ID3D12GraphicsCommandList2 not supported by implementation.\n");
        destroy_test_context(&context);
        return;
    }

    memset(&heap_properties, 0, sizeof(heap_properties));
    heap_properties.Type = D3D12_HEAP_TYPE_DEFAULT;

    memset(&resource_desc, 0, sizeof(resource_desc));
    resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    resource_desc.Width = 2048u << 20u;
    resource_desc.Height = 1;
    resource_desc.DepthOrArraySize = 1;
    resource_desc.MipLevels = 1;
    resource_desc.SampleDesc.Count = 1;
    resource_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    resource_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    hr = ID3D12Device_CreateCommittedResource(context.device, &heap_properties, D3D12_HEAP_FLAG_NONE,
            &resource_desc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, NULL, &IID_ID3D12Resource, (void**)&data_buffer);

    if (FAILED(hr))
    {
        /* Be robust if the implementation does not support huge allocations */
        skip("Failed to create data buffer, hr %#x.\n", hr);
        ID3D12GraphicsCommandList2_Release(command_list2);
        destroy_test_context(&context);
        return;
    }

    resource_desc.Width = sizeof(*feedback) * 3;

    hr = ID3D12Device_CreateCommittedResource(context.device, &heap_properties, D3D12_HEAP_FLAG_NONE,
            &resource_desc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, NULL, &IID_ID3D12Resource, (void**)&feedback_buffer);
    ok(hr == S_OK, "Failed to create feedback buffer, hr %#x.\n", hr);

    descriptor_heap = create_gpu_descriptor_heap(context.device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 4);
    descriptor_cpu_heap = create_cpu_descriptor_heap(context.device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1);

    memset(&uav_desc, 0, sizeof(uav_desc));
    uav_desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    uav_desc.Buffer.StructureByteStride = sizeof(*feedback);
    uav_desc.Buffer.NumElements = 3;

    ID3D12Device_CreateUnorderedAccessView(context.device, feedback_buffer, NULL,
            &uav_desc, get_cpu_descriptor_handle(&context, descriptor_heap, 2));

    memset(&rs_desc, 0, sizeof(rs_desc));
    rs_desc.NumParameters = ARRAY_SIZE(rs_args);
    rs_desc.pParameters = rs_args;

    memset(rs_args, 0, sizeof(rs_args));
    rs_args[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rs_args[0].DescriptorTable.NumDescriptorRanges = ARRAY_SIZE(rs_desc_ranges);
    rs_args[0].DescriptorTable.pDescriptorRanges = rs_desc_ranges;
    rs_args[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    rs_args[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    rs_args[1].Constants.Num32BitValues = sizeof(shader_args) / sizeof(uint32_t);
    rs_args[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    memset(rs_desc_ranges, 0, sizeof(rs_desc_ranges));
    rs_desc_ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    rs_desc_ranges[0].OffsetInDescriptorsFromTableStart = 0;
    rs_desc_ranges[0].NumDescriptors = 1;

    rs_desc_ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    rs_desc_ranges[1].OffsetInDescriptorsFromTableStart = 1;
    rs_desc_ranges[1].NumDescriptors = 2;

    hr = create_root_signature(context.device, &rs_desc, &context.root_signature);
    ok(hr == S_OK, "Failed to create root signature, hr %#x.\n", hr);

    srv_pso = create_compute_pipeline_state(context.device, context.root_signature, cs_large_tbo_load_dxbc);
    uav_pso = create_compute_pipeline_state(context.device, context.root_signature, cs_large_tbo_store_dxbc);

    for (i = 0; i < ARRAY_SIZE(tests); i++)
    {
        vkd3d_test_set_context("Test %u", i);

        if (tests[i].element_count > (1 << D3D12_REQ_BUFFER_RESOURCE_TEXEL_COUNT_2_TO_EXP))
        {
            vkd3d_mute_validation_message("09427", "Intentionally testing out of spec behavior");
            vkd3d_mute_validation_message("09428", "Intentionally testing out of spec behavior");
        }

        memset(&srv_desc, 0, sizeof(srv_desc));
        srv_desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        srv_desc.Format = tests[i].format;
        srv_desc.Buffer.FirstElement = 0;
        srv_desc.Buffer.NumElements = tests[i].element_count;
        srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

        ID3D12Device_CreateShaderResourceView(context.device, data_buffer,
                &srv_desc, get_cpu_descriptor_handle(&context, descriptor_heap, 0));

        memset(&uav_desc, 0, sizeof(uav_desc));
        uav_desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        uav_desc.Format = tests[i].format;
        uav_desc.Buffer.FirstElement = 0;
        uav_desc.Buffer.NumElements = tests[i].element_count;

        ID3D12Device_CreateUnorderedAccessView(context.device, data_buffer, NULL,
                &uav_desc, get_cpu_descriptor_handle(&context, descriptor_heap, 1));

        uav_desc.Buffer.FirstElement = tests[i].element_count - 1;
        uav_desc.Buffer.NumElements = 1;

        ID3D12Device_CreateUnorderedAccessView(context.device, data_buffer, NULL,
                &uav_desc, get_cpu_descriptor_handle(&context, descriptor_heap, 3));
        ID3D12Device_CreateUnorderedAccessView(context.device, data_buffer, NULL,
                &uav_desc, get_cpu_descriptor_handle(&context, descriptor_cpu_heap, 0));

        shader_args.offset = tests[i].element_count - 1u;
        shader_args.data = tests[i].element_data;
        shader_args.feedback = 0;

        memset(clear_color, 0, sizeof(clear_color));
        clear_color[0] = tests[i].element_data;

        ID3D12GraphicsCommandList2_ClearUnorderedAccessViewUint(command_list2,
                get_gpu_descriptor_handle(&context, descriptor_heap, 3),
                get_cpu_descriptor_handle(&context, descriptor_cpu_heap, 0),
                data_buffer, clear_color, 0, NULL);

        transition_resource_state(context.list, data_buffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

        ID3D12GraphicsCommandList2_SetDescriptorHeaps(command_list2, 1, &descriptor_heap);
        ID3D12GraphicsCommandList2_SetComputeRootSignature(command_list2, context.root_signature);
        ID3D12GraphicsCommandList2_SetPipelineState(command_list2, srv_pso);
        ID3D12GraphicsCommandList2_SetComputeRootDescriptorTable(command_list2, 0, get_gpu_descriptor_handle(&context, descriptor_heap, 0));
        ID3D12GraphicsCommandList2_SetComputeRoot32BitConstants(command_list2, 1, sizeof(shader_args) / sizeof(uint32_t), &shader_args, 0);
        ID3D12GraphicsCommandList2_Dispatch(command_list2, 1, 1, 1);

        transition_resource_state(context.list, data_buffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

        shader_args.feedback = 1;

        ID3D12GraphicsCommandList2_SetPipelineState(command_list2, uav_pso);
        ID3D12GraphicsCommandList2_SetComputeRoot32BitConstants(command_list2, 1, sizeof(shader_args) / sizeof(uint32_t), &shader_args, 0);
        ID3D12GraphicsCommandList2_Dispatch(command_list2, 1, 1, 1);

        transition_resource_state(context.list, data_buffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

        shader_args.feedback = 2;

        ID3D12GraphicsCommandList2_SetPipelineState(command_list2, srv_pso);
        ID3D12GraphicsCommandList2_SetComputeRoot32BitConstants(command_list2, 1, sizeof(shader_args) / sizeof(uint32_t), &shader_args, 0);
        ID3D12GraphicsCommandList2_Dispatch(command_list2, 1, 1, 1);

        transition_resource_state(context.list, data_buffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        transition_resource_state(context.list, feedback_buffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);

        get_buffer_readback_with_command_list(feedback_buffer, DXGI_FORMAT_UNKNOWN, &rb, context.queue, context.list);

        feedback = rb.data;

        for (j = 0; j < 3; j++)
        {
            uint32_t expected = tests[i].element_data;

            if (j == 2)
                expected += 1u;

            ok(feedback[j].element_count == tests[i].element_count, "Got element count %#x, expected %#x at %u.\n",
                    feedback[j].element_count, tests[i].element_count, j);
            ok(feedback[j].last_value == expected, "Got data %#x, expected %#x at %u.\n",
                    feedback[j].last_value, expected, j);
        }

        release_resource_readback(&rb);
        reset_command_list(context.list, context.allocator);

        transition_resource_state(context.list, feedback_buffer, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

        if (tests[i].element_count > (1 << D3D12_REQ_BUFFER_RESOURCE_TEXEL_COUNT_2_TO_EXP))
        {
            vkd3d_unmute_validation_message("09427");
            vkd3d_unmute_validation_message("09428");
        }
    }

    ID3D12PipelineState_Release(srv_pso);
    ID3D12PipelineState_Release(uav_pso);

    ID3D12Resource_Release(feedback_buffer);
    ID3D12Resource_Release(data_buffer);

    ID3D12DescriptorHeap_Release(descriptor_heap);
    ID3D12DescriptorHeap_Release(descriptor_cpu_heap);

    ID3D12GraphicsCommandList2_Release(command_list2);

    destroy_test_context(&context);
}

void test_large_heap(void)
{
#define SECTION_SIZE    (1ull << 30)
#define SECTION_COUNT   (6ull)
#define TILE_SIZE       (65536ull)

    UINT heap_tile_offsets[SECTION_COUNT], heap_tile_counts[SECTION_COUNT];
    ID3D12Resource *readback_buffer, *tiled_buffer, *large_buffer;
    D3D12_DESCRIPTOR_HEAP_DESC descriptor_heap_desc;
    ID3D12Resource *placed_buffers[SECTION_COUNT];
    D3D12_TILED_RESOURCE_COORDINATE tile_coord;
    ID3D12DescriptorHeap *cpu_heap, *gpu_heap;
    D3D12_UNORDERED_ACCESS_VIEW_DESC uav_desc;
    D3D12_RESOURCE_BARRIER aliasing_barrier;
    D3D12_RESOURCE_DESC resource_desc;
    D3D12_TILE_REGION_SIZE tile_size;
    struct test_context_desc desc;
    struct resource_readback rb;
    struct test_context context;
    D3D12_HEAP_DESC heap_desc;
    UINT clear_value[4];
    ID3D12Heap *heap;
    unsigned int i;
    HRESULT hr;

    memset(&desc, 0, sizeof(desc));
    desc.no_render_target = true;
    desc.no_pipeline = true;
    desc.no_root_signature = true;
    if (!init_test_context(&context, &desc))
        return;

    memset(&aliasing_barrier, 0, sizeof(aliasing_barrier));
    aliasing_barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_ALIASING;

    memset(&heap_desc, 0, sizeof(heap_desc));
    heap_desc.Properties.Type = D3D12_HEAP_TYPE_DEFAULT;
    heap_desc.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
    heap_desc.SizeInBytes = SECTION_SIZE * SECTION_COUNT;
    heap_desc.Flags = D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS;

    vkd3d_mute_validation_message("06409", "Global buffer may exceed max buffer size");
    vkd3d_mute_validation_message("-maxMem", "Non VU related error reported which should be warning");

    hr = ID3D12Device_CreateHeap(context.device, &heap_desc, &IID_ID3D12Heap, (void**)&heap);

    vkd3d_unmute_validation_message("06409");
    vkd3d_unmute_validation_message("-maxMem");

    if (FAILED(hr))
    {
        skip("Failed to create large heap, hr %#x.\n", hr);
        destroy_test_context(&context);
        return;
    }

    memset(&resource_desc, 0, sizeof(resource_desc));
    resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    resource_desc.Width = SECTION_COUNT * sizeof(uint32_t);
    resource_desc.Height = 1u;
    resource_desc.DepthOrArraySize = 1u;
    resource_desc.MipLevels = 1u;
    resource_desc.SampleDesc.Count = 1u;
    resource_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    hr = ID3D12Device_CreateCommittedResource(context.device, &heap_desc.Properties, D3D12_HEAP_FLAG_NONE, &resource_desc, D3D12_RESOURCE_STATE_COPY_DEST, NULL, &IID_ID3D12Resource, (void**)&readback_buffer);
    ok(hr == S_OK, "Failed to create committed buffer, hr %#x.\n", hr);

    /* Create tiled buffer to test for aliasing issues */
    memset(&resource_desc, 0, sizeof(resource_desc));
    resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    resource_desc.Width = SECTION_COUNT * TILE_SIZE;
    resource_desc.Height = 1u;
    resource_desc.DepthOrArraySize = 1u;
    resource_desc.MipLevels = 1u;
    resource_desc.SampleDesc.Count = 1u;
    resource_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    resource_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    hr = ID3D12Device_CreateReservedResource(context.device, &resource_desc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, NULL, &IID_ID3D12Resource, (void**)&tiled_buffer);
    ok(hr == S_OK, "Failed to create tiled buffer, hr %#x.\n", hr);

    memset(&tile_coord, 0, sizeof(tile_coord));
    memset(&tile_size, 0, sizeof(tile_size));
    tile_size.NumTiles = SECTION_COUNT;

    for (i = 0; i < SECTION_COUNT; i++)
    {
        heap_tile_offsets[i] = (SECTION_SIZE / TILE_SIZE) * i;
        heap_tile_counts[i] = 1u;
    }

    ID3D12CommandQueue_UpdateTileMappings(context.queue, tiled_buffer, 1, &tile_coord, &tile_size,
            heap, SECTION_COUNT, NULL, heap_tile_offsets, heap_tile_counts, D3D12_TILE_MAPPING_FLAG_NONE);

    /* Create placed buffers at large offsets */
    memset(&resource_desc, 0, sizeof(resource_desc));
    resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    resource_desc.Width = SECTION_SIZE;
    resource_desc.Height = 1u;
    resource_desc.DepthOrArraySize = 1u;
    resource_desc.MipLevels = 1u;
    resource_desc.SampleDesc.Count = 1u;
    resource_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    resource_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    for (i = 0; i < SECTION_COUNT; i++)
    {
        hr = ID3D12Device_CreatePlacedResource(context.device, heap, SECTION_SIZE * i, &resource_desc, D3D12_RESOURCE_STATE_COPY_SOURCE, NULL, &IID_ID3D12Resource, (void**)&placed_buffers[i]);
        ok(hr == S_OK, "Failed to create placed buffer, hr %#x.\n", hr);
    }

    /* Create large placed buffer covering the entire heap */
    resource_desc.Width = heap_desc.SizeInBytes;

    hr = ID3D12Device_CreatePlacedResource(context.device, heap, 0, &resource_desc, D3D12_RESOURCE_STATE_COPY_SOURCE, NULL, &IID_ID3D12Resource, (void**)&large_buffer);

    if (FAILED(hr))
    {
        skip("Failed to create large buffer, hr %#x.\n", hr);
        large_buffer = NULL;
    }

    memset(&descriptor_heap_desc, 0, sizeof(descriptor_heap_desc));
    descriptor_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    descriptor_heap_desc.NumDescriptors = SECTION_COUNT;

    hr = ID3D12Device_CreateDescriptorHeap(context.device, &descriptor_heap_desc, &IID_ID3D12DescriptorHeap, (void**)&cpu_heap);
    ok(hr == S_OK, "Failed to create descriptor heap, hr %#x.\n", hr);

    descriptor_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

    hr = ID3D12Device_CreateDescriptorHeap(context.device, &descriptor_heap_desc, &IID_ID3D12DescriptorHeap, (void**)&gpu_heap);
    ok(hr == S_OK, "Failed to create descriptor heap, hr %#x.\n", hr);

    /* Test writing data to the sparse buffer. Offsets will all be smmall, so if this
     * goes wrong, this proves an issue with large offsets in UpdateTileMappings. */
    ID3D12GraphicsCommandList_SetDescriptorHeaps(context.list, 1u, &gpu_heap);

    for (i = 0; i < SECTION_COUNT; i++)
    {
        memset(&uav_desc, 0, sizeof(uav_desc));
        uav_desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        uav_desc.Format = DXGI_FORMAT_R32_UINT;
        uav_desc.Buffer.FirstElement = (TILE_SIZE / sizeof(uint32_t)) * i;
        uav_desc.Buffer.NumElements = TILE_SIZE / sizeof(uint32_t);

        ID3D12Device_CreateUnorderedAccessView(context.device, tiled_buffer, NULL, &uav_desc, get_cpu_descriptor_handle(&context, cpu_heap, i));
        ID3D12Device_CreateUnorderedAccessView(context.device, tiled_buffer, NULL, &uav_desc, get_cpu_descriptor_handle(&context, gpu_heap, i));

        memset(clear_value, 0, sizeof(clear_value));
        clear_value[0] = i + 10u;

        ID3D12GraphicsCommandList_ClearUnorderedAccessViewUint(context.list,
                get_gpu_descriptor_handle(&context, gpu_heap, i),
                get_cpu_descriptor_handle(&context, cpu_heap, i),
                tiled_buffer, clear_value, 0, NULL);
    }

    transition_resource_state(context.list, tiled_buffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);

    for (i = 0; i < SECTION_COUNT; i++)
        ID3D12GraphicsCommandList_CopyBufferRegion(context.list, readback_buffer, i * sizeof(uint32_t), tiled_buffer, i * TILE_SIZE, sizeof(uint32_t));

    transition_resource_state(context.list, readback_buffer, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COPY_SOURCE);
    get_buffer_readback_with_command_list(readback_buffer, DXGI_FORMAT_R32_UINT, &rb, context.queue, context.list);

    for (i = 0; i < SECTION_COUNT; i++)
    {
        uint32_t got = get_readback_uint(&rb, i, 0, 0);
        uint32_t expected = i + 10u;

        ok(got == expected, "Got %u, expected %u at %u.\n", got, expected, i);
    }

    release_resource_readback(&rb);

    /* Test copying data from the small placed buffers. */
    reset_command_list(context.list, context.allocator);

    transition_resource_state(context.list, readback_buffer, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
    ID3D12GraphicsCommandList_CopyBufferRegion(context.list, readback_buffer, 0, tiled_buffer, 0, sizeof(uint32_t) * SECTION_COUNT);

    ID3D12GraphicsCommandList_ResourceBarrier(context.list, 1, &aliasing_barrier);

    for (i = 0; i < SECTION_COUNT; i++)
        ID3D12GraphicsCommandList_CopyBufferRegion(context.list, readback_buffer, i * sizeof(uint32_t), placed_buffers[i], 0, sizeof(uint32_t));

    ID3D12GraphicsCommandList_ResourceBarrier(context.list, 1, &aliasing_barrier);

    transition_resource_state(context.list, readback_buffer, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COPY_SOURCE);
    get_buffer_readback_with_command_list(readback_buffer, DXGI_FORMAT_R32_UINT, &rb, context.queue, context.list);

    for (i = 0; i < SECTION_COUNT; i++)
    {
        uint32_t got = get_readback_uint(&rb, i, 0, 0);
        uint32_t expected = i + 10u;

        ok(got == expected, "Got %u, expected %u at %u.\n", got, expected, i);
    }

    release_resource_readback(&rb);

    /* Test copying data from the large buffer. */
    if (large_buffer)
    {
        reset_command_list(context.list, context.allocator);

        transition_resource_state(context.list, readback_buffer, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
        ID3D12GraphicsCommandList_CopyBufferRegion(context.list, readback_buffer, 0, tiled_buffer, 0, sizeof(uint32_t)* SECTION_COUNT);

        ID3D12GraphicsCommandList_ResourceBarrier(context.list, 1, &aliasing_barrier);

        for (i = 0; i < SECTION_COUNT; i++)
            ID3D12GraphicsCommandList_CopyBufferRegion(context.list, readback_buffer, i * sizeof(uint32_t), large_buffer, (UINT64)i * SECTION_SIZE, sizeof(uint32_t));

        ID3D12GraphicsCommandList_ResourceBarrier(context.list, 1, &aliasing_barrier);

        transition_resource_state(context.list, readback_buffer, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COPY_SOURCE);
        get_buffer_readback_with_command_list(readback_buffer, DXGI_FORMAT_R32_UINT, &rb, context.queue, context.list);

        for (i = 0; i < SECTION_COUNT; i++)
        {
            uint32_t got = get_readback_uint(&rb, i, 0, 0);
            uint32_t expected = i + 10u;

            ok(got == expected, "Got %u, expected %u at %u.\n", got, expected, i);
        }

        release_resource_readback(&rb);
    }

    /* Test creating UAVs for small placed buffers */
    reset_command_list(context.list, context.allocator);

    transition_resource_state(context.list, readback_buffer, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
    ID3D12GraphicsCommandList_CopyBufferRegion(context.list, readback_buffer, 0, tiled_buffer, 0, sizeof(uint32_t) * SECTION_COUNT);
    ID3D12GraphicsCommandList_ResourceBarrier(context.list, 1, &aliasing_barrier);

    for (i = 0; i < SECTION_COUNT; i++)
    {
        transition_resource_state(context.list, placed_buffers[i], D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

        memset(&uav_desc, 0, sizeof(uav_desc));
        uav_desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        uav_desc.Format = DXGI_FORMAT_R32_UINT;
        uav_desc.Buffer.FirstElement = 0u;
        uav_desc.Buffer.NumElements = TILE_SIZE / sizeof(uint32_t);

        ID3D12Device_CreateUnorderedAccessView(context.device, placed_buffers[i], NULL, &uav_desc, get_cpu_descriptor_handle(&context, cpu_heap, i));
        ID3D12Device_CreateUnorderedAccessView(context.device, placed_buffers[i], NULL, &uav_desc, get_cpu_descriptor_handle(&context, gpu_heap, i));

        memset(clear_value, 0, sizeof(clear_value));
        clear_value[0] = i + 20u;

        ID3D12GraphicsCommandList_ClearUnorderedAccessViewUint(context.list,
            get_gpu_descriptor_handle(&context, gpu_heap, i),
            get_cpu_descriptor_handle(&context, cpu_heap, i),
            placed_buffers[i], clear_value, 0, NULL);

        transition_resource_state(context.list, placed_buffers[i], D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
    }

    for (i = 0; i < SECTION_COUNT; i++)
        ID3D12GraphicsCommandList_CopyBufferRegion(context.list, readback_buffer, i * sizeof(uint32_t), placed_buffers[i], 0, sizeof(uint32_t));

    transition_resource_state(context.list, readback_buffer, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COPY_SOURCE);
    get_buffer_readback_with_command_list(readback_buffer, DXGI_FORMAT_R32_UINT, &rb, context.queue, context.list);

    for (i = 0; i < SECTION_COUNT; i++)
    {
        uint32_t got = get_readback_uint(&rb, i, 0, 0);
        uint32_t expected = i + 20u;

        ok(got == expected, "Got %u, expected %u at %u.\n", got, expected, i);
    }

    release_resource_readback(&rb);

    /* Test creating UAVs at large offsets. AMD native reports DEVICE_LOST after UAV creation. */
    if (large_buffer && !is_amd_windows_device(context.device))
    {
        reset_command_list(context.list, context.allocator);

        transition_resource_state(context.list, readback_buffer, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
        ID3D12GraphicsCommandList_CopyBufferRegion(context.list, readback_buffer, 0, tiled_buffer, 0, sizeof(uint32_t) * SECTION_COUNT);
        ID3D12GraphicsCommandList_ResourceBarrier(context.list, 1, &aliasing_barrier);

        transition_resource_state(context.list, large_buffer, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

        for (i = 0; i < SECTION_COUNT; i++)
        {
            memset(&uav_desc, 0, sizeof(uav_desc));
            uav_desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
            uav_desc.Format = DXGI_FORMAT_R32_UINT;
            uav_desc.Buffer.FirstElement = i * (SECTION_SIZE / sizeof(uint32_t));
            uav_desc.Buffer.NumElements = TILE_SIZE / sizeof(uint32_t);

            ID3D12Device_CreateUnorderedAccessView(context.device, large_buffer, NULL, &uav_desc, get_cpu_descriptor_handle(&context, cpu_heap, i));
            ID3D12Device_CreateUnorderedAccessView(context.device, large_buffer, NULL, &uav_desc, get_cpu_descriptor_handle(&context, gpu_heap, i));

            memset(clear_value, 0, sizeof(clear_value));
            clear_value[0] = i + 30u;

            ID3D12GraphicsCommandList_ClearUnorderedAccessViewUint(context.list,
                get_gpu_descriptor_handle(&context, gpu_heap, i),
                get_cpu_descriptor_handle(&context, cpu_heap, i),
                large_buffer, clear_value, 0, NULL);
        }

        transition_resource_state(context.list, large_buffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);

        for (i = 0; i < SECTION_COUNT; i++)
            ID3D12GraphicsCommandList_CopyBufferRegion(context.list, readback_buffer, i * sizeof(uint32_t), large_buffer, (UINT64)i * SECTION_SIZE, sizeof(uint32_t));

        transition_resource_state(context.list, readback_buffer, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COPY_SOURCE);
        get_buffer_readback_with_command_list(readback_buffer, DXGI_FORMAT_R32_UINT, &rb, context.queue, context.list);

        for (i = 0; i < SECTION_COUNT; i++)
        {
            uint32_t got = get_readback_uint(&rb, i, 0, 0);
            uint32_t expected = i + 30u;

            ok(got == expected, "Got %u, expected %u at %u.\n", got, expected, i);
        }

        release_resource_readback(&rb);
    }

    ID3D12DescriptorHeap_Release(cpu_heap);
    ID3D12DescriptorHeap_Release(gpu_heap);

    for (i = 0; i < SECTION_COUNT; i++)
        ID3D12Resource_Release(placed_buffers[i]);

    ID3D12Resource_Release(tiled_buffer);
    ID3D12Resource_Release(readback_buffer);

    if (large_buffer)
        ID3D12Resource_Release(large_buffer);

    ID3D12Heap_Release(heap);

    destroy_test_context(&context);

#undef SECTION_SIZE
#undef SECTION_COUNT
#undef TILE_SIZE
}
