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

void test_clear_depth_stencil_view(void)
{
    static const float expected_values[] = {0.5f, 0.1f, 0.1f, 0.6, 1.0f, 0.5f};
    ID3D12GraphicsCommandList *command_list;
    D3D12_DEPTH_STENCIL_VIEW_DESC dsv_desc;
    ID3D12Resource *tmp_float, *tmp_uint;
    struct depth_stencil_resource ds;
    unsigned int dsv_increment_size;
    D3D12_CLEAR_VALUE clear_value;
    struct test_context_desc desc;
    struct test_context context;
    ID3D12CommandQueue *queue;
    ID3D12Device *device;
    unsigned int i;

    memset(&desc, 0, sizeof(desc));
    desc.no_render_target = true;
    if (!init_test_context(&context, &desc))
        return;
    device = context.device;
    command_list = context.list;
    queue = context.queue;

    dsv_increment_size = ID3D12Device_GetDescriptorHandleIncrementSize(device,
            D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
    trace("DSV descriptor handle increment size: %u.\n", dsv_increment_size);
    ok(dsv_increment_size, "Got unexpected increment size %#x.\n", dsv_increment_size);

    clear_value.Format = DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
    clear_value.DepthStencil.Depth = 0.5f;
    clear_value.DepthStencil.Stencil = 0x3;
    init_depth_stencil(&ds, device, 32, 32, 1, 1, DXGI_FORMAT_D32_FLOAT_S8X24_UINT, 0, &clear_value);

    /* Tests that separate layout clear works correctly. */
    ID3D12GraphicsCommandList_ClearDepthStencilView(command_list, ds.dsv_handle,
            D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 0.5f, 0x3, 0, NULL);
    ID3D12GraphicsCommandList_ClearDepthStencilView(command_list, ds.dsv_handle,
            D3D12_CLEAR_FLAG_DEPTH, 0.75f, 0x7, 0, NULL);
    ID3D12GraphicsCommandList_ClearDepthStencilView(command_list, ds.dsv_handle,
            D3D12_CLEAR_FLAG_STENCIL, 0.75f, 0x7, 0, NULL);
    transition_resource_state(command_list, ds.texture,
            D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_COPY_SOURCE);
    tmp_float = create_default_texture2d(context.device, 32, 32, 1, 1, DXGI_FORMAT_R32_FLOAT,
            D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
    tmp_uint = create_default_texture2d(context.device, 32, 32, 1, 1, DXGI_FORMAT_R8_UINT,
            D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
    {
        D3D12_TEXTURE_COPY_LOCATION dst_location, src_location;
        D3D12_BOX src_box;

        dst_location.SubresourceIndex = 0;
        dst_location.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;

        src_location.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        src_location.pResource = ds.texture;

        src_box.left = 0;
        src_box.right = 32;
        src_box.top = 0;
        src_box.bottom = 32;
        src_box.front = 0;
        src_box.back = 1;
        dst_location.pResource = tmp_float;
        src_location.SubresourceIndex = 0;
        ID3D12GraphicsCommandList_CopyTextureRegion(context.list, &dst_location, 0, 0, 0, &src_location, &src_box);
        dst_location.pResource = tmp_uint;
        src_location.SubresourceIndex = 1;
        ID3D12GraphicsCommandList_CopyTextureRegion(context.list, &dst_location, 0, 0, 0, &src_location, &src_box);
    }
    transition_resource_state(command_list, tmp_float,
            D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COPY_SOURCE);
    transition_resource_state(command_list, tmp_uint,
            D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COPY_SOURCE);
    check_sub_resource_float(tmp_float, 0, queue, command_list, 0.75f, 1);
    reset_command_list(command_list, context.allocator);
    check_sub_resource_uint8(tmp_uint, 0, queue, command_list, 0x7, 0);
    ID3D12Resource_Release(tmp_float);
    ID3D12Resource_Release(tmp_uint);

    destroy_depth_stencil(&ds);
    reset_command_list(command_list, context.allocator);
    clear_value.Format = DXGI_FORMAT_D32_FLOAT;
    init_depth_stencil(&ds, device, 32, 32, 6, 1, DXGI_FORMAT_D32_FLOAT, 0, &clear_value);

    ID3D12GraphicsCommandList_ClearDepthStencilView(command_list, ds.dsv_handle,
            D3D12_CLEAR_FLAG_DEPTH, expected_values[0], 0, 0, NULL);
    memset(&dsv_desc, 0, sizeof(dsv_desc));
    dsv_desc.Format = DXGI_FORMAT_D32_FLOAT;
    dsv_desc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
    dsv_desc.Texture2DArray.FirstArraySlice = 1;
    dsv_desc.Texture2DArray.ArraySize = 2;
    ID3D12Device_CreateDepthStencilView(device, ds.texture, &dsv_desc, ds.dsv_handle);
    ID3D12GraphicsCommandList_ClearDepthStencilView(command_list, ds.dsv_handle,
            D3D12_CLEAR_FLAG_DEPTH, expected_values[1], 0, 0, NULL);
    dsv_desc.Texture2DArray.FirstArraySlice = 3;
    dsv_desc.Texture2DArray.ArraySize = 1;
    ID3D12Device_CreateDepthStencilView(device, ds.texture, &dsv_desc, ds.dsv_handle);
    ID3D12GraphicsCommandList_ClearDepthStencilView(command_list, ds.dsv_handle,
            D3D12_CLEAR_FLAG_DEPTH, expected_values[3], 0, 0, NULL);
    dsv_desc.Texture2DArray.FirstArraySlice = 4;
    ID3D12Device_CreateDepthStencilView(device, ds.texture, &dsv_desc, ds.dsv_handle);
    ID3D12GraphicsCommandList_ClearDepthStencilView(command_list, ds.dsv_handle,
            D3D12_CLEAR_FLAG_DEPTH, expected_values[4], 0, 0, NULL);

    transition_resource_state(command_list, ds.texture,
            D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_COPY_SOURCE);
    for (i = 0; i < ARRAY_SIZE(expected_values); ++i)
    {
        check_sub_resource_float(ds.texture, i, queue, command_list, expected_values[i], 1);
        reset_command_list(command_list, context.allocator);
    }

    destroy_depth_stencil(&ds);
    destroy_test_context(&context);
}

void test_clear_render_target_view(void)
{
    static const unsigned int array_expected_colors[] = {0xff00ff00, 0xff0000ff, 0xffff0000};
    static const float array_colors[][4] =
    {
        {0.0f, 1.0f, 0.0f, 1.0f},
        {1.0f, 0.0f, 0.0f, 1.0f},
        {0.0f, 0.0f, 1.0f, 1.0f},
    };
    static const float negative_value[] = {1.0f, -1.0f, -0.5f, -2.0f};
    static const float color[] = {0.1f, 0.5f, 0.3f, 0.75f};
    static const float green[] = {0.0f, 1.0f, 0.0f, 1.0f};
    ID3D12GraphicsCommandList *command_list;
    D3D12_CPU_DESCRIPTOR_HANDLE rtv_handle;
    D3D12_RENDER_TARGET_VIEW_DESC rtv_desc;
    D3D12_HEAP_PROPERTIES heap_properties;
    D3D12_RESOURCE_DESC resource_desc;
    unsigned int rtv_increment_size;
    ID3D12DescriptorHeap *rtv_heap;
    D3D12_CLEAR_VALUE clear_value;
    struct test_context_desc desc;
    struct resource_readback rb;
    struct test_context context;
    ID3D12CommandQueue *queue;
    ID3D12Resource *resource;
    ID3D12Device *device;
    unsigned int i;
    D3D12_BOX box;
    HRESULT hr;

    static const struct
    {
        const float *color;
        DXGI_FORMAT format;
        uint32_t result;
    }
    r8g8b8a8[] =
    {
        {color,          DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, 0xbf95bc59},

        {green,          DXGI_FORMAT_R8G8B8A8_UNORM,      0xff00ff00},
        {color,          DXGI_FORMAT_R8G8B8A8_UNORM,      0xbf4c7f19},

        {green,          DXGI_FORMAT_R8G8B8A8_UINT,       0x01000100},
        {color,          DXGI_FORMAT_R8G8B8A8_UINT,       0x00000000},
        {negative_value, DXGI_FORMAT_R8G8B8A8_UINT,       0x00000001},

        {green,          DXGI_FORMAT_R8G8B8A8_SINT,       0x01000100},
        {color,          DXGI_FORMAT_R8G8B8A8_SINT,       0x00000000},
        {negative_value, DXGI_FORMAT_R8G8B8A8_SINT,       0xfe00ff01},
    };
    static const struct
    {
        const float *color;
        DXGI_FORMAT format;
        uint64_t result;
    }
    r16g16b16a16[] =
    {
        {green,          DXGI_FORMAT_R16G16B16A16_UNORM, 0xffff0000ffff0000},

        {green,          DXGI_FORMAT_R16G16B16A16_UINT,  0x0001000000010000},
        {color,          DXGI_FORMAT_R16G16B16A16_UINT,  0x0000000000000000},
        {negative_value, DXGI_FORMAT_R16G16B16A16_UINT,  0x0000000000000001},

        {green,          DXGI_FORMAT_R16G16B16A16_SINT,  0x0001000000010000},
        {color,          DXGI_FORMAT_R16G16B16A16_SINT,  0x0000000000000000},
        {negative_value, DXGI_FORMAT_R16G16B16A16_SINT,  0xfffe0000ffff0001},
    };

    STATIC_ASSERT(ARRAY_SIZE(array_colors) == ARRAY_SIZE(array_expected_colors));

    memset(&desc, 0, sizeof(desc));
    desc.no_render_target = true;
    if (!init_test_context(&context, &desc))
        return;
    device = context.device;
    command_list = context.list;
    queue = context.queue;

    rtv_heap = create_cpu_descriptor_heap(device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1);

    rtv_increment_size = ID3D12Device_GetDescriptorHandleIncrementSize(device,
            D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    trace("RTV descriptor handle increment size: %u.\n", rtv_increment_size);

    rtv_handle = ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(rtv_heap);

    memset(&heap_properties, 0, sizeof(heap_properties));
    heap_properties.Type = D3D12_HEAP_TYPE_DEFAULT;
    resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    resource_desc.Alignment = 0;
    resource_desc.Width = 32;
    resource_desc.Height = 32;
    resource_desc.DepthOrArraySize = 1;
    resource_desc.MipLevels = 1;
    resource_desc.Format = DXGI_FORMAT_R8G8B8A8_TYPELESS;
    resource_desc.SampleDesc.Count = 1;
    resource_desc.SampleDesc.Quality = 0;
    resource_desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    resource_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    clear_value.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    clear_value.Color[0] = 1.0f;
    clear_value.Color[1] = 0.0f;
    clear_value.Color[2] = 0.0f;
    clear_value.Color[3] = 1.0f;
    hr = ID3D12Device_CreateCommittedResource(device,
            &heap_properties, D3D12_HEAP_FLAG_NONE, &resource_desc,
            D3D12_RESOURCE_STATE_RENDER_TARGET, &clear_value,
            &IID_ID3D12Resource, (void **)&resource);
    ok(hr == S_OK, "Failed to create texture, hr %#x.\n", hr);

    memset(&rtv_desc, 0, sizeof(rtv_desc));
    rtv_desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;

    /* R8G8B8A8 */
    for (i = 0; i < ARRAY_SIZE(r8g8b8a8); ++i)
    {
        vkd3d_test_set_context("Test %u", i);

        rtv_desc.Format = r8g8b8a8[i].format;
        ID3D12Device_CreateRenderTargetView(device, resource, &rtv_desc, rtv_handle);

        ID3D12GraphicsCommandList_ClearRenderTargetView(command_list, rtv_handle, r8g8b8a8[i].color, 0, NULL);
        transition_resource_state(command_list, resource,
                D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
        check_sub_resource_uint(resource, 0, queue, command_list, r8g8b8a8[i].result, 2);

        reset_command_list(command_list, context.allocator);
        transition_resource_state(command_list, resource,
                D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
    }
    vkd3d_test_set_context(NULL);

    /* R16G16B16A16 */
    hr = ID3D12GraphicsCommandList_Close(command_list);
    ok(hr == S_OK, "Failed to close command list, hr %#x.\n", hr);
    reset_command_list(command_list, context.allocator);
    ID3D12Resource_Release(resource);
    resource_desc.Format = DXGI_FORMAT_R16G16B16A16_TYPELESS;
    hr = ID3D12Device_CreateCommittedResource(device,
            &heap_properties, D3D12_HEAP_FLAG_NONE, &resource_desc,
            D3D12_RESOURCE_STATE_RENDER_TARGET, NULL,
            &IID_ID3D12Resource, (void **)&resource);
    ok(hr == S_OK, "Failed to create texture, hr %#x.\n", hr);

    for (i = 0; i < ARRAY_SIZE(r16g16b16a16); ++i)
    {
        vkd3d_test_set_context("Test %u", i);

        rtv_desc.Format = r16g16b16a16[i].format;
        ID3D12Device_CreateRenderTargetView(device, resource, &rtv_desc, rtv_handle);

        ID3D12GraphicsCommandList_ClearRenderTargetView(command_list, rtv_handle, r16g16b16a16[i].color, 0, NULL);
        transition_resource_state(command_list, resource,
                D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
        check_sub_resource_uint64(resource, 0, queue, command_list, r16g16b16a16[i].result, 0);

        reset_command_list(command_list, context.allocator);
        transition_resource_state(command_list, resource,
                D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
    }
    vkd3d_test_set_context(NULL);

    /* 2D array texture */
    hr = ID3D12GraphicsCommandList_Close(command_list);
    ok(hr == S_OK, "Failed to close command list, hr %#x.\n", hr);
    reset_command_list(command_list, context.allocator);
    ID3D12Resource_Release(resource);
    resource_desc.Format = DXGI_FORMAT_R8G8B8A8_TYPELESS;
    resource_desc.DepthOrArraySize = ARRAY_SIZE(array_colors);
    hr = ID3D12Device_CreateCommittedResource(device,
            &heap_properties, D3D12_HEAP_FLAG_NONE, &resource_desc,
            D3D12_RESOURCE_STATE_RENDER_TARGET, &clear_value,
            &IID_ID3D12Resource, (void **)&resource);
    ok(hr == S_OK, "Failed to create texture, hr %#x.\n", hr);

    for (i = 0; i < ARRAY_SIZE(array_colors); ++i)
    {
        memset(&rtv_desc, 0, sizeof(rtv_desc));
        rtv_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        rtv_desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DARRAY;
        rtv_desc.Texture2DArray.FirstArraySlice = i;
        rtv_desc.Texture2DArray.ArraySize = 1;

        ID3D12Device_CreateRenderTargetView(device, resource, &rtv_desc, rtv_handle);
        ID3D12GraphicsCommandList_ClearRenderTargetView(command_list, rtv_handle, array_colors[i], 0, NULL);
    }

    transition_resource_state(command_list, resource,
            D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
    for (i = 0; i < ARRAY_SIZE(array_expected_colors); ++i)
    {
        check_sub_resource_uint(resource, i, queue, command_list, array_expected_colors[i], 2);
        reset_command_list(command_list, context.allocator);
    }

    /* 2D multisample array texture */
    ID3D12Resource_Release(resource);
    resource_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    resource_desc.SampleDesc.Count = 4;
    hr = ID3D12Device_CreateCommittedResource(device,
            &heap_properties, D3D12_HEAP_FLAG_NONE, &resource_desc,
            D3D12_RESOURCE_STATE_RENDER_TARGET, &clear_value,
            &IID_ID3D12Resource, (void **)&resource);
    ok(hr == S_OK, "Failed to create texture, hr %#x.\n", hr);

    for (i = 0; i < ARRAY_SIZE(array_colors); ++i)
    {
        memset(&rtv_desc, 0, sizeof(rtv_desc));
        rtv_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        rtv_desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DMSARRAY;
        rtv_desc.Texture2DMSArray.FirstArraySlice = i;
        rtv_desc.Texture2DMSArray.ArraySize = 1;

        ID3D12Device_CreateRenderTargetView(device, resource, &rtv_desc, rtv_handle);
        ID3D12GraphicsCommandList_ClearRenderTargetView(command_list, rtv_handle, array_colors[i], 0, NULL);
    }

    transition_resource_state(command_list, resource,
            D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
    for (i = 0; i < ARRAY_SIZE(array_expected_colors); ++i)
    {
        check_sub_resource_uint(resource, i, queue, command_list, array_expected_colors[i], 2);
        reset_command_list(command_list, context.allocator);
    }

    /* 3D texture */
    ID3D12Resource_Release(resource);
    resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE3D;
    resource_desc.DepthOrArraySize = 32;
    resource_desc.MipLevels = 1;
    resource_desc.SampleDesc.Count = 1;
    hr = ID3D12Device_CreateCommittedResource(device,
            &heap_properties, D3D12_HEAP_FLAG_NONE, &resource_desc,
            D3D12_RESOURCE_STATE_RENDER_TARGET, &clear_value,
            &IID_ID3D12Resource, (void **)&resource);
    ok(hr == S_OK, "Failed to create texture, hr %#x.\n", hr);

    ID3D12Device_CreateRenderTargetView(device, resource, NULL, rtv_handle);

    ID3D12GraphicsCommandList_ClearRenderTargetView(command_list, rtv_handle, color, 0, NULL);
    transition_resource_state(command_list, resource,
            D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
    check_sub_resource_uint(resource, 0, queue, command_list, 0xbf4c7f19, 2);

    memset(&rtv_desc, 0, sizeof(rtv_desc));
    rtv_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    rtv_desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE3D;
    rtv_desc.Texture3D.FirstWSlice = 2;
    rtv_desc.Texture3D.WSize = 2;
    ID3D12Device_CreateRenderTargetView(device, resource, &rtv_desc, rtv_handle);

    reset_command_list(command_list, context.allocator);
    transition_resource_state(command_list, resource,
            D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
    ID3D12GraphicsCommandList_ClearRenderTargetView(command_list, rtv_handle, green, 0, NULL);
    transition_resource_state(command_list, resource,
            D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
    get_texture_readback_with_command_list(resource, 0, &rb, queue, command_list);
    set_box(&box, 0, 0, 0, 32, 32, 2);
    check_readback_data_uint(&rb, &box, 0xbf4c7f19, 1);
    set_box(&box, 0, 0, 2, 32, 32, 4);
    check_readback_data_uint(&rb, &box, 0xff00ff00, 1);
    set_box(&box, 0, 0, 4, 32, 32, 32);
    check_readback_data_uint(&rb, &box, 0xbf4c7f19, 1);
    release_resource_readback(&rb);

    ID3D12Resource_Release(resource);
    ID3D12DescriptorHeap_Release(rtv_heap);
    destroy_test_context(&context);
}

void test_clear_unordered_access_view_buffer(void)
{
    D3D12_UNORDERED_ACCESS_VIEW_DESC uav_desc;
    ID3D12DescriptorHeap *cpu_heap, *gpu_heap;
    ID3D12GraphicsCommandList *command_list;
    struct test_context_desc desc;
    struct test_context context;
    struct resource_readback rb;
    ID3D12CommandQueue *queue;
    D3D12_HEAP_DESC heap_desc;
    ID3D12Resource *buffer;
    ID3D12Device *device;
    UINT clear_value[4];
    unsigned int i, j;
    ID3D12Heap *heap;
    D3D12_BOX box;
    HRESULT hr;

#define BUFFER_SIZE (1024 * 1024)
    static const struct
    {
        DXGI_FORMAT format;
        D3D12_BUFFER_UAV buffer_uav;
        unsigned int values[4];
        unsigned int expected;
        bool is_float;
    }
    tests[] =
    {
        {DXGI_FORMAT_R32_UINT, { 0, BUFFER_SIZE / sizeof(uint32_t),      0, 0, D3D12_BUFFER_UAV_FLAG_NONE},
                {0, 0, 0, 0}, 0},
        {DXGI_FORMAT_R32_UINT, {64, BUFFER_SIZE / sizeof(uint32_t) - 64, 0, 0, D3D12_BUFFER_UAV_FLAG_NONE},
                {0, 0, 0, 0}, 0},
        {DXGI_FORMAT_R32_UINT, { 0, BUFFER_SIZE / sizeof(uint32_t),      0, 0, D3D12_BUFFER_UAV_FLAG_NONE},
                {1, 0, 0, 0}, 1},
        {DXGI_FORMAT_R32_UINT, {64, BUFFER_SIZE / sizeof(uint32_t) - 64, 0, 0, D3D12_BUFFER_UAV_FLAG_NONE},
                {2, 0, 0, 0}, 2},
        {DXGI_FORMAT_R32_UINT, {64, BUFFER_SIZE / sizeof(uint32_t) - 64, 0, 0, D3D12_BUFFER_UAV_FLAG_NONE},
                {3, 0, 0, 0}, 3},
        {DXGI_FORMAT_R32_UINT, {64, BUFFER_SIZE / sizeof(uint32_t) - 64, 0, 0, D3D12_BUFFER_UAV_FLAG_NONE},
                {4, 2, 3, 4}, 4},
        {DXGI_FORMAT_R32_UINT, { 0, BUFFER_SIZE / sizeof(uint32_t) - 10, 0, 0, D3D12_BUFFER_UAV_FLAG_NONE},
                {5, 0, 0, 0}, 5},

        {DXGI_FORMAT_R32_TYPELESS, { 0, BUFFER_SIZE / sizeof(uint32_t),      0, 0, D3D12_BUFFER_UAV_FLAG_RAW},
                {0, 0, 0, 0}, 0},
        {DXGI_FORMAT_R32_TYPELESS, {64, BUFFER_SIZE / sizeof(uint32_t) - 64, 0, 0, D3D12_BUFFER_UAV_FLAG_RAW},
                {0, 0, 0, 0}, 0},
        {DXGI_FORMAT_R32_TYPELESS, { 0, BUFFER_SIZE / sizeof(uint32_t),      0, 0, D3D12_BUFFER_UAV_FLAG_RAW},
                {6, 0, 0, 0}, 6},
        {DXGI_FORMAT_R32_TYPELESS, {64, BUFFER_SIZE / sizeof(uint32_t) - 64, 0, 0, D3D12_BUFFER_UAV_FLAG_RAW},
                {7, 0, 0, 0}, 7},
        {DXGI_FORMAT_R32_TYPELESS, {64, BUFFER_SIZE / sizeof(uint32_t) - 64, 0, 0, D3D12_BUFFER_UAV_FLAG_RAW},
                {8, 0, 0, 0}, 8},
        {DXGI_FORMAT_R32_TYPELESS, {64, BUFFER_SIZE / sizeof(uint32_t) - 64, 0, 0, D3D12_BUFFER_UAV_FLAG_RAW},
                {9, 1, 1, 1}, 9},
        {DXGI_FORMAT_R32_TYPELESS, {64, BUFFER_SIZE / sizeof(uint32_t) - 64, 0, 0, D3D12_BUFFER_UAV_FLAG_RAW},
                {~0u, 0, 0, 0}, ~0u},
        {DXGI_FORMAT_R32_TYPELESS, { 0, BUFFER_SIZE / sizeof(uint32_t) - 10, 0, 0, D3D12_BUFFER_UAV_FLAG_RAW},
                {10, 0, 0, 0}, 10},
        {DXGI_FORMAT_R32_TYPELESS, { 0, BUFFER_SIZE / sizeof(uint32_t) - 9,  0, 0, D3D12_BUFFER_UAV_FLAG_RAW},
                {11, 0, 0, 0}, 11},

        {DXGI_FORMAT_R32_FLOAT, { 0, BUFFER_SIZE / sizeof(uint32_t), 0, 0, D3D12_BUFFER_UAV_FLAG_NONE},
                {0, 0, 0, 0}, 0},
        {DXGI_FORMAT_R32_FLOAT, { 0, BUFFER_SIZE / sizeof(uint32_t), 0, 0, D3D12_BUFFER_UAV_FLAG_NONE},
                {1, 0, 0, 0}, 1},
        {DXGI_FORMAT_R32_FLOAT, { 0, BUFFER_SIZE / sizeof(uint32_t), 0, 0, D3D12_BUFFER_UAV_FLAG_NONE},
                {0x3f800000 /* 1.0f */, 0, 0, 0}, 0x3f800000 /* 1.0f */, true},

        {DXGI_FORMAT_R16G16_UINT, { 0, BUFFER_SIZE / sizeof(uint32_t), 0, 0, D3D12_BUFFER_UAV_FLAG_NONE},
                {0x1234, 0xabcd, 0, 0}, 0xabcd1234},
        {DXGI_FORMAT_R16G16_UINT, { 0, BUFFER_SIZE / sizeof(uint32_t), 0, 0, D3D12_BUFFER_UAV_FLAG_NONE},
                {0x10000, 0, 0, 0}, 0},

        {DXGI_FORMAT_R16G16_UNORM, { 0, BUFFER_SIZE / sizeof(uint32_t), 0, 0, D3D12_BUFFER_UAV_FLAG_NONE},
                {0x1234, 0xabcd, 0, 0}, 0xabcd1234},
        {DXGI_FORMAT_R16G16_UNORM, { 0, BUFFER_SIZE / sizeof(uint32_t), 0, 0, D3D12_BUFFER_UAV_FLAG_NONE},
                {0x3f000080 /* 0.50000762951f */, 0x3f800000 /* 1.0f */, 0, 0}, 0xffff8000, true},
        {DXGI_FORMAT_R16G16_UNORM, { 0, BUFFER_SIZE / sizeof(uint32_t), 0, 0, D3D12_BUFFER_UAV_FLAG_NONE},
                {0x40000000 /* 2.0f */, 0 /* 0.0f */, 0, 0}, 0x0000ffff, true},
        {DXGI_FORMAT_R16G16_UNORM, { 0, BUFFER_SIZE / sizeof(uint32_t), 0, 0, D3D12_BUFFER_UAV_FLAG_NONE},
                {0xbf800000 /* -1.0f */, 0 /* 0.0f */, 0x3f000000 /* 1.0f */, 0x3f000000 /* 1.0f */}, 0, true},

        {DXGI_FORMAT_R16G16_FLOAT, { 0, BUFFER_SIZE / sizeof(uint32_t), 0, 0, D3D12_BUFFER_UAV_FLAG_NONE},
                {0x1234, 0xabcd, 0, 0}, 0xabcd1234},
        {DXGI_FORMAT_R16G16_FLOAT, { 0, BUFFER_SIZE / sizeof(uint32_t), 0, 0, D3D12_BUFFER_UAV_FLAG_NONE},
                {0x3f000000 /* 0.5f */, 0x3f800000 /* 1.0f */, 0, 0}, 0x3c003800, true},

        {DXGI_FORMAT_R8G8B8A8_UINT, { 0, BUFFER_SIZE / sizeof(uint32_t), 0, 0, D3D12_BUFFER_UAV_FLAG_NONE},
                {0x11, 0x22, 0x33, 0x44}, 0x44332211},
        {DXGI_FORMAT_R8G8B8A8_UINT, { 0, BUFFER_SIZE / sizeof(uint32_t), 0, 0, D3D12_BUFFER_UAV_FLAG_NONE},
                {0x100, 0, 0, 0}, 0},

        {DXGI_FORMAT_R11G11B10_FLOAT, { 0, BUFFER_SIZE / sizeof(uint32_t), 0, 0, D3D12_BUFFER_UAV_FLAG_NONE},
                {0, 0, 0, 0}, 0},
        {DXGI_FORMAT_R11G11B10_FLOAT, { 0, BUFFER_SIZE / sizeof(uint32_t), 0, 0, D3D12_BUFFER_UAV_FLAG_NONE},
                {0x7ff, 0x7ff, 0x3ff, 0}, 0xffffffff},
        {DXGI_FORMAT_R11G11B10_FLOAT, { 0, BUFFER_SIZE / sizeof(uint32_t), 0, 0, D3D12_BUFFER_UAV_FLAG_NONE},
                {0x7ff, 0, 0x3ff, 0}, 0xffc007ff},
        {DXGI_FORMAT_R11G11B10_FLOAT, { 0, BUFFER_SIZE / sizeof(uint32_t), 0, 0, D3D12_BUFFER_UAV_FLAG_NONE},
                {0x3f000000 /* 0.5f */, 0x3f800000 /* 1.0f */, 0x40000000 /* 2.0f */, 0}, 0x801e0380, true},
        {DXGI_FORMAT_R11G11B10_FLOAT, { 0, BUFFER_SIZE / sizeof(uint32_t), 0, 0, D3D12_BUFFER_UAV_FLAG_NONE},
                {0x3f000000 /* 1.0f */, 0 /* 0.0f */, 0xbf800000 /* -1.0f */, 0x3f000000 /* 1.0f */},
                0x00000380, true},
        {DXGI_FORMAT_R10G10B10A2_UINT, { 0, BUFFER_SIZE / sizeof(uint32_t), 0, 0, D3D12_BUFFER_UAV_FLAG_NONE},
                {0x1010, 0x1020, 0x1030, 0x41}, (0x30 << 20) | (0x20 << 10) | (0x10 << 0) | (0x1 << 30)},
        {DXGI_FORMAT_R10G10B10A2_UNORM, { 0, BUFFER_SIZE / sizeof(uint32_t), 0, 0, D3D12_BUFFER_UAV_FLAG_NONE},
                {0x1010, 0x1020, 0x1030, 0x41}, (0x30u << 20) | (0x20u << 10) | (0x10u << 0) | (0x1u << 30)},
        {DXGI_FORMAT_R10G10B10A2_UNORM, { 0, BUFFER_SIZE / sizeof(uint32_t), 0, 0, D3D12_BUFFER_UAV_FLAG_NONE},
                {0x3f002008 /* 0.5004887585532747f */, 0x3f800000 /* 1.0f */, 0, 0x3f800000 /* 1.0f */},
                (0x3ffu << 10) | (0x200u << 0) | (0x3u << 30), true},
    };

    memset(&desc, 0, sizeof(desc));
    desc.no_render_target = true;
    if (!init_test_context(&context, &desc))
        return;
    device = context.device;
    command_list = context.list;
    queue = context.queue;

    cpu_heap = create_cpu_descriptor_heap(device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 2);
    gpu_heap = create_gpu_descriptor_heap(device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 2);

    heap_desc.SizeInBytes = 2 * BUFFER_SIZE;
    memset(&heap_desc.Properties, 0, sizeof(heap_desc.Properties));
    heap_desc.Properties.Type = D3D12_HEAP_TYPE_DEFAULT;
    heap_desc.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
    heap_desc.Flags = D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS;
    hr = ID3D12Device_CreateHeap(device, &heap_desc, &IID_ID3D12Heap, (void **)&heap);
    ok(hr == S_OK, "Failed to create heap, hr %#x.\n", hr);

    for (i = 0; i < ARRAY_SIZE(tests); ++i)
    {
        vkd3d_test_set_context("Test %u", i);

        buffer = create_placed_buffer(device, heap, BUFFER_SIZE, BUFFER_SIZE,
                D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

        for (j = 0; j < ARRAY_SIZE(clear_value); ++j)
            clear_value[j] = tests[i].expected ? 0 : ~0u;

        memset(&uav_desc, 0, sizeof(uav_desc));
        uav_desc.Format = DXGI_FORMAT_R32_UINT;
        uav_desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        uav_desc.Buffer.NumElements = BUFFER_SIZE / sizeof(uint32_t);
        ID3D12Device_CreateUnorderedAccessView(device, buffer, NULL, &uav_desc,
                get_cpu_descriptor_handle(&context, cpu_heap, 1));
        ID3D12Device_CreateUnorderedAccessView(device, buffer, NULL, &uav_desc,
                get_cpu_descriptor_handle(&context, gpu_heap, 1));

        uav_desc.Format = tests[i].format;
        uav_desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        uav_desc.Buffer = tests[i].buffer_uav;
        ID3D12Device_CreateUnorderedAccessView(device, buffer, NULL, &uav_desc,
                get_cpu_descriptor_handle(&context, cpu_heap, 0));
        ID3D12Device_CreateUnorderedAccessView(device, buffer, NULL, &uav_desc,
                get_cpu_descriptor_handle(&context, gpu_heap, 0));

        ID3D12GraphicsCommandList_ClearUnorderedAccessViewUint(command_list,
                get_gpu_descriptor_handle(&context, gpu_heap, 1),
                get_cpu_descriptor_handle(&context, cpu_heap, 1),
                buffer, clear_value, 0, NULL);

        uav_barrier(command_list, buffer);

        if (tests[i].is_float)
            ID3D12GraphicsCommandList_ClearUnorderedAccessViewFloat(command_list,
                    get_gpu_descriptor_handle(&context, gpu_heap, 0),
                    get_cpu_descriptor_handle(&context, cpu_heap, 0),
                    buffer, (const float *)tests[i].values, 0, NULL);
        else
            ID3D12GraphicsCommandList_ClearUnorderedAccessViewUint(command_list,
                    get_gpu_descriptor_handle(&context, gpu_heap, 0),
                    get_cpu_descriptor_handle(&context, cpu_heap, 0),
                    buffer, tests[i].values, 0, NULL);

        set_box(&box, 0, 0, 0, 1, 1, 1);
        transition_resource_state(command_list, buffer,
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
        get_buffer_readback_with_command_list(buffer, DXGI_FORMAT_R32_TYPELESS, &rb, queue, command_list);
        box.left = 0;
        box.right = uav_desc.Buffer.FirstElement;
        check_readback_data_uint(&rb, &box, clear_value[0], 0);
        box.left = uav_desc.Buffer.FirstElement;
        box.right = uav_desc.Buffer.FirstElement + uav_desc.Buffer.NumElements;
        check_readback_data_uint(&rb, &box, tests[i].expected, tests[i].is_float ? 1 : 0);
        box.left = uav_desc.Buffer.FirstElement + uav_desc.Buffer.NumElements;
        box.right = BUFFER_SIZE / format_size(uav_desc.Format);
        check_readback_data_uint(&rb, &box, clear_value[0], 0);
        release_resource_readback(&rb);

        reset_command_list(command_list, context.allocator);
        ID3D12Resource_Release(buffer);
    }
    vkd3d_test_set_context(NULL);

    ID3D12DescriptorHeap_Release(cpu_heap);
    ID3D12DescriptorHeap_Release(gpu_heap);
    ID3D12Heap_Release(heap);
    destroy_test_context(&context);
#undef BUFFER_SIZE
}

void test_clear_unordered_access_view_image(void)
{
    D3D12_FEATURE_DATA_FORMAT_SUPPORT format_support;
    unsigned int expected_colour, actual_colour;
    D3D12_UNORDERED_ACCESS_VIEW_DESC uav_desc;
    ID3D12DescriptorHeap *cpu_heap, *gpu_heap;
    ID3D12GraphicsCommandList *command_list;
    unsigned int i, j, d, p, z, layer;
    D3D12_HEAP_PROPERTIES heap_properties;
    unsigned int image_size, image_depth;
    D3D12_RESOURCE_DESC resource_desc;
    struct test_context_desc desc;
    struct test_context context;
    struct resource_readback rb;
    ID3D12CommandQueue *queue;
    bool is_inside, success;
    ID3D12Resource *texture;
    ID3D12Device *device;
    UINT clear_value[4];
    HRESULT hr;
    int x, y;

#define IMAGE_SIZE 16u
    static const struct
    {
        DXGI_FORMAT format;
        unsigned int image_mips;
        unsigned int image_layers;
        unsigned int mip_level;
        unsigned int first_layer;
        unsigned int layer_count;
        unsigned int rect_count;
        RECT clear_rects[2];
        unsigned int values[4];
        unsigned int expected;
        bool is_float;
    }
    tests[] =
    {
        /* Test clearing a specific mip level. */
        {DXGI_FORMAT_R32_FLOAT,       2, 1, 0, 0, 1, 0, {{0}}, {1,          0, 0, 0}, 1},
        {DXGI_FORMAT_R32_FLOAT,       2, 1, 1, 0, 1, 0, {{0}}, {1,          0, 0, 0}, 1},
        {DXGI_FORMAT_R32_FLOAT,       2, 1, 0, 0, 1, 0, {{0}}, {0x3f000000, 0, 0, 0}, 0x3f000000, true},
        {DXGI_FORMAT_R32_FLOAT,       2, 1, 1, 0, 1, 0, {{0}}, {0x3f000000, 0, 0, 0}, 0x3f000000, true},
        /* Test clearing specific array layers. */
        {DXGI_FORMAT_R32_FLOAT,       1, IMAGE_SIZE, 0, 0, IMAGE_SIZE, 0, {{0}}, {1, 0, 0, 0}, 1},
        {DXGI_FORMAT_R32_FLOAT,       1, IMAGE_SIZE, 0, 3, 2,          0, {{0}}, {1, 0, 0, 0}, 1},
        {DXGI_FORMAT_R32_FLOAT,       1, IMAGE_SIZE, 0, 0, IMAGE_SIZE, 0, {{0}},
                {0x3f000000, 0, 0, 0}, 0x3f000000, true},
        {DXGI_FORMAT_R32_FLOAT,       1, IMAGE_SIZE, 0, 3, 2,          0, {{0}},
                {0x3f000000, 0, 0, 0}, 0x3f000000, true},
        /* Test a single clear rect. */
        {DXGI_FORMAT_R32_FLOAT,       1, 1, 0, 0, 1, 1, {{1, 2, IMAGE_SIZE - 4, IMAGE_SIZE - 2}},
                {1,          0, 0, 0}, 1},
        {DXGI_FORMAT_R32_FLOAT,       1, 1, 0, 0, 1, 1, {{1, 2, IMAGE_SIZE - 4, IMAGE_SIZE - 2}},
                {0x3f000000, 0, 0, 0}, 0x3f000000, true},
        /* Test multiple clear rects. */
        {DXGI_FORMAT_R32_FLOAT,       1, 1, 0, 0, 1, 2, {{1, 2, 3, 4}, {5, 6, 7, 8}},
                {1,          0, 0, 0}, 1},
        {DXGI_FORMAT_R32_FLOAT,       1, 1, 0, 0, 1, 2, {{1, 2, 3, 4}, {5, 6, 7, 8}},
                {0x3f000000, 0, 0, 0}, 0x3f000000, true},

        /* Test uint clears with formats. */
        {DXGI_FORMAT_R16G16_UINT,     1, 1, 0, 0, 1, 0, {{0}}, {1,       2, 3, 4}, 0x00020001},
        {DXGI_FORMAT_R16G16_UINT,     1, 1, 0, 0, 1, 0, {{0}}, {0x12345, 0, 0, 0}, 0x00002345},
        {DXGI_FORMAT_R16G16_UNORM,    1, 1, 0, 0, 1, 0, {{0}}, {1,       2, 3, 4}, 0x00020001},
        {DXGI_FORMAT_R16G16_FLOAT,    1, 1, 0, 0, 1, 0, {{0}}, {1,       2, 3, 4}, 0x00020001},
        {DXGI_FORMAT_R8G8B8A8_UINT,   1, 1, 0, 0, 1, 0, {{0}}, {1,       2, 3, 4}, 0x04030201},
        {DXGI_FORMAT_R8G8B8A8_UINT,   1, 1, 0, 0, 1, 0, {{0}}, {0x123,   0, 0, 0}, 0x00000023},
        {DXGI_FORMAT_R8G8B8A8_UNORM,  1, 1, 0, 0, 1, 0, {{0}}, {1,       2, 3, 4}, 0x04030201},
        {DXGI_FORMAT_R11G11B10_FLOAT, 1, 1, 0, 0, 1, 0, {{0}}, {0,       0, 0, 0}, 0x00000000},
        {DXGI_FORMAT_R11G11B10_FLOAT, 1, 1, 0, 0, 1, 0, {{0}}, {1,       2, 3, 4}, 0x00c01001},
        {DXGI_FORMAT_R9G9B9E5_SHAREDEXP, 1, 1, 0, 0, 1, 0, {{0}}, {1,    2, 3, 4}, 0x200c0401},
        /* Test float clears with formats. */
        {DXGI_FORMAT_R16G16_UNORM,    1, 1, 0, 0, 1, 0, {{0}},
                {0x3f000080 /* 0.5f + unorm16 epsilon */, 0x3f800000 /* 1.0f */, 0, 0}, 0xffff8000, true},
        {DXGI_FORMAT_R16G16_FLOAT,    1, 1, 0, 0, 1, 0, {{0}},
                {0x3f000080 /* 0.5f */, 0x3f800000 /* 1.0f */, 0, 0}, 0x3c003800, true},
        {DXGI_FORMAT_R8G8B8A8_UNORM,  1, 1, 0, 0, 1, 0, {{0}},
                {0x3f000080 /* 0.5f + epsilon */, 0x3f800000 /* 1.0f */, 0, 0}, 0x0000ff80, true},
        {DXGI_FORMAT_R8G8B8A8_UNORM,  1, 1, 0, 0, 1, 0, {{0}},
                {0, 0, 0x3f000080 /* 0.5f + epsilon */, 0x3f800000 /* 1.0f */}, 0xff800000, true},
        {DXGI_FORMAT_R11G11B10_FLOAT, 1, 1, 0, 0, 1, 0, {{0}},
                {0x3f000000 /* 1.0f */, 0 /* 0.0f */, 0xbf800000 /* -1.0f */, 0x3f000000 /* 1.0f */},
                0x00000380, true},
        {DXGI_FORMAT_B8G8R8A8_UNORM,  1, 1, 0, 0, 1, 0, {{0}},
                {0, 0, 0x3f000080 /* 0.5f + epsilon */, 0x3f800000 /* 1.0f */}, 0xff000080, true},
        {DXGI_FORMAT_B8G8R8A8_UNORM,  1, 1, 0, 0, 1, 0, {{0}}, {1, 2, 3, 4}, 0x04010203},
        {DXGI_FORMAT_A8_UNORM,        1, 1, 0, 0, 1, 0, {{0}}, {0, 0, 0, 0x3f000080 /* 0.5f + epsilon */}, 0x80, true},
        {DXGI_FORMAT_A8_UNORM,        1, 1, 0, 0, 1, 0, {{0}}, {1, 2, 3, 4}, 4},
    };

    static const struct
    {
        D3D12_RESOURCE_DIMENSION resource_dim;
        D3D12_UAV_DIMENSION view_dim;
        bool is_layered;
    }
    uav_dimensions[] =
    {
        {D3D12_RESOURCE_DIMENSION_TEXTURE2D, D3D12_UAV_DIMENSION_TEXTURE2D,      false},
        {D3D12_RESOURCE_DIMENSION_TEXTURE2D, D3D12_UAV_DIMENSION_TEXTURE2DARRAY, true },
        /* Expected behaviour with partial layer coverage is unclear. */
        {D3D12_RESOURCE_DIMENSION_TEXTURE3D, D3D12_UAV_DIMENSION_TEXTURE3D,      false},
    };

    memset(&desc, 0, sizeof(desc));
    desc.no_render_target = true;
    if (!init_test_context(&context, &desc))
        return;
    device = context.device;
    command_list = context.list;
    queue = context.queue;

    cpu_heap = create_cpu_descriptor_heap(device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 2);
    gpu_heap = create_gpu_descriptor_heap(device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 2);

    memset(&heap_properties, 0, sizeof(heap_properties));
    heap_properties.Type = D3D12_HEAP_TYPE_DEFAULT;

    for (d = 0; d < ARRAY_SIZE(uav_dimensions); ++d)
    {
        for (i = 0; i < ARRAY_SIZE(tests); ++i)
        {
            vkd3d_test_set_context("Dim %u, Test %u", d, i);

            if (tests[i].image_layers > 1 && !uav_dimensions[d].is_layered)
                continue;

            memset(&format_support, 0, sizeof(format_support));
            format_support.Format = tests[i].format;

            if (FAILED(hr = ID3D12Device_CheckFeatureSupport(device,
                    D3D12_FEATURE_FORMAT_SUPPORT, &format_support, sizeof(format_support))) ||
                    !(format_support.Support1 & D3D12_FORMAT_SUPPORT1_TYPED_UNORDERED_ACCESS_VIEW))
            {
                skip("Format %u not supported.\n", tests[i].format);
                continue;
            }

            resource_desc.Dimension = uav_dimensions[d].resource_dim;
            resource_desc.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
            resource_desc.Width = IMAGE_SIZE;
            resource_desc.Height = IMAGE_SIZE;
            if (uav_dimensions[d].resource_dim == D3D12_RESOURCE_DIMENSION_TEXTURE1D)
                resource_desc.Height = 1;
            resource_desc.DepthOrArraySize = tests[i].image_layers;
            resource_desc.MipLevels = tests[i].image_mips;
            resource_desc.Format = tests[i].format;
            resource_desc.SampleDesc.Count = 1;
            resource_desc.SampleDesc.Quality = 0;
            resource_desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
            resource_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

            if (FAILED(hr = ID3D12Device_CreateCommittedResource(device, &heap_properties,
                    D3D12_HEAP_FLAG_NONE, &resource_desc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                    NULL, &IID_ID3D12Resource, (void **)&texture)))
            {
                skip("Failed to create texture, hr %#x.\n", hr);
                continue;
            }

            uav_desc.Format = tests[i].format;
            uav_desc.ViewDimension = uav_dimensions[d].view_dim;

            for (j = 0; j < 2; ++j)
            {
                unsigned int first_layer = j ? 0 : tests[i].first_layer;
                unsigned int layer_count = j ? tests[i].image_layers : tests[i].layer_count;

                switch (uav_desc.ViewDimension)
                {
                    case D3D12_UAV_DIMENSION_TEXTURE1D:
                        uav_desc.Texture1D.MipSlice = tests[i].mip_level;
                        break;

                    case D3D12_UAV_DIMENSION_TEXTURE1DARRAY:
                        uav_desc.Texture1DArray.MipSlice = tests[i].mip_level;
                        uav_desc.Texture1DArray.FirstArraySlice = first_layer;
                        uav_desc.Texture1DArray.ArraySize = layer_count;
                        break;

                    case D3D12_UAV_DIMENSION_TEXTURE2D:
                        uav_desc.Texture2D.MipSlice = tests[i].mip_level;
                        uav_desc.Texture2D.PlaneSlice = 0;
                        break;

                    case D3D12_UAV_DIMENSION_TEXTURE2DARRAY:
                        uav_desc.Texture2DArray.MipSlice = tests[i].mip_level;
                        uav_desc.Texture2DArray.FirstArraySlice = first_layer;
                        uav_desc.Texture2DArray.ArraySize = layer_count;
                        uav_desc.Texture2DArray.PlaneSlice = 0;
                        break;

                    case D3D12_UAV_DIMENSION_TEXTURE3D:
                        uav_desc.Texture3D.MipSlice = tests[i].mip_level;
                        uav_desc.Texture3D.FirstWSlice = first_layer;
                        uav_desc.Texture3D.WSize = layer_count;
                        break;

                    default:
                        continue;
                }

                ID3D12Device_CreateUnorderedAccessView(device, texture, NULL,
                        &uav_desc, get_cpu_descriptor_handle(&context, cpu_heap, j));
                ID3D12Device_CreateUnorderedAccessView(device, texture, NULL,
                        &uav_desc, get_cpu_descriptor_handle(&context, gpu_heap, j));
            }

            for (j = 0; j < 4; ++j)
                clear_value[j] = tests[i].expected ? 0u : ~0u;

            ID3D12GraphicsCommandList_ClearUnorderedAccessViewUint(command_list,
                    get_gpu_descriptor_handle(&context, gpu_heap, 1),
                    get_cpu_descriptor_handle(&context, cpu_heap, 1),
                    texture, clear_value, 0, NULL);

            uav_barrier(command_list, texture);

            if (tests[i].is_float)
                ID3D12GraphicsCommandList_ClearUnorderedAccessViewFloat(command_list,
                        get_gpu_descriptor_handle(&context, gpu_heap, 0),
                        get_cpu_descriptor_handle(&context, cpu_heap, 0),
                        texture, (const float *)tests[i].values, tests[i].rect_count, tests[i].clear_rects);
            else
                ID3D12GraphicsCommandList_ClearUnorderedAccessViewUint(command_list,
                        get_gpu_descriptor_handle(&context, gpu_heap, 0),
                        get_cpu_descriptor_handle(&context, cpu_heap, 0),
                        texture, tests[i].values, tests[i].rect_count, tests[i].clear_rects);

            transition_resource_state(command_list, texture,
                    D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);

            image_depth = uav_dimensions[d].resource_dim == D3D12_RESOURCE_DIMENSION_TEXTURE3D
                    ? max(tests[i].image_layers >> tests[i].mip_level, 1u) : 1;
            image_size = max(IMAGE_SIZE >> tests[i].mip_level, 1u);

            for (layer = 0; layer < tests[i].image_layers / image_depth; ++layer)
            {
                get_texture_readback_with_command_list(texture,
                        tests[i].mip_level + (layer * tests[i].image_mips),
                        &rb, queue, command_list);

                for (p = 0; p < image_depth * image_size * image_size; ++p)
                {
                    x = p % image_size;
                    y = (p / image_size) % image_size;
                    z = p / (image_size * image_size);

                    is_inside = tests[i].rect_count == 0;

                    for (j = 0; j < tests[i].rect_count; ++j)
                    {
                        if (y >= tests[i].clear_rects[j].top && y < tests[i].clear_rects[j].bottom
                                && x >= tests[i].clear_rects[j].left && x < tests[i].clear_rects[j].right)
                        {
                            is_inside = true;
                            break;
                        }
                    }

                    if (uav_dimensions[d].resource_dim == D3D12_RESOURCE_DIMENSION_TEXTURE3D)
                        is_inside = is_inside && z >= tests[i].first_layer
                                && z < tests[i].first_layer + tests[i].layer_count;
                    else
                        is_inside = is_inside && layer >= tests[i].first_layer
                                && layer < tests[i].first_layer + tests[i].layer_count;

                    expected_colour = is_inside ? tests[i].expected : clear_value[0];
                    actual_colour = 0xdeadbeef;

                    switch (format_size(tests[i].format))
                    {
                        case 4: actual_colour = get_readback_uint(&rb, x, y, z); break;
                        case 2: actual_colour = get_readback_uint16(&rb, x, y); break;
                        case 1: actual_colour = get_readback_uint8(&rb, x, y); break;
                    }
                    success = compare_color(actual_colour, expected_colour, tests[i].is_float ? 1 : 0);

                    ok(success, "At layer %u, (%u,%u,%u), expected %#x, got %#x.\n",
                            layer, x, y, z, expected_colour, actual_colour);

                    if (!success)
                        break;
                }

                release_resource_readback(&rb);
                reset_command_list(command_list, context.allocator);
            }

            ID3D12Resource_Release(texture);
        }
    }

    ID3D12DescriptorHeap_Release(cpu_heap);
    ID3D12DescriptorHeap_Release(gpu_heap);
    destroy_test_context(&context);
#undef IMAGE_SIZE
}

