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

#ifndef __VKD3D_D3D12_TEST_UTILS_H
#define __VKD3D_D3D12_TEST_UTILS_H

#ifdef _WIN32
#include "renderdoc_app.h"
#endif

#define SHADER_BYTECODE(code) {code,sizeof(code)}

#define wait_queue_idle(a, b) wait_queue_idle_(__LINE__, a, b)
static void wait_queue_idle_(unsigned int line, ID3D12Device *device, ID3D12CommandQueue *queue);
#define wait_queue_idle_no_event(a, b) wait_queue_idle_no_event_(__LINE__, a, b)
static inline void wait_queue_idle_no_event_(unsigned int line, ID3D12Device *device, ID3D12CommandQueue *queue);
static ID3D12Device *create_device(void);

static inline void set_rect(RECT *rect, int left, int top, int right, int bottom)
{
    rect->left = left;
    rect->right = right;
    rect->top = top;
    rect->bottom = bottom;
}

static inline void set_box(D3D12_BOX *box, unsigned int left, unsigned int top, unsigned int front,
        unsigned int right, unsigned int bottom, unsigned int back)
{
    box->left = left;
    box->top = top;
    box->front = front;
    box->right = right;
    box->bottom = bottom;
    box->back = back;
}

static inline void set_viewport(D3D12_VIEWPORT *vp, float x, float y,
        float width, float height, float min_depth, float max_depth)
{
    vp->TopLeftX = x;
    vp->TopLeftY = y;
    vp->Width = width;
    vp->Height = height;
    vp->MinDepth = min_depth;
    vp->MaxDepth = max_depth;
}

static inline uint8_t  delta_uint8 (uint8_t a,  uint8_t  b) { return max(a, b) - min(a, b); }
static inline uint16_t delta_uint16(uint16_t a, uint16_t b) { return max(a, b) - min(a, b); }
static inline uint32_t delta_uint32(uint32_t a, uint32_t b) { return max(a, b) - min(a, b); }
static inline uint64_t delta_uint64(uint64_t a, uint64_t b) { return max(a, b) - min(a, b); }

static inline bool compare_color(DWORD c1, DWORD c2, BYTE max_diff)
{
    if (delta_uint32(c1 & 0xff, c2 & 0xff) > max_diff)
        return false;
    c1 >>= 8; c2 >>= 8;
    if (delta_uint32(c1 & 0xff, c2 & 0xff) > max_diff)
        return false;
    c1 >>= 8; c2 >>= 8;
    if (delta_uint32(c1 & 0xff, c2 & 0xff) > max_diff)
        return false;
    c1 >>= 8; c2 >>= 8;
    if (delta_uint32(c1 & 0xff, c2 & 0xff) > max_diff)
        return false;
    return true;
}

static inline D3D12_SHADER_BYTECODE shader_bytecode(const void *code, size_t size)
{
    D3D12_SHADER_BYTECODE shader_bytecode = { code, size };
    return shader_bytecode;
}

static inline void exec_command_list(ID3D12CommandQueue *queue, ID3D12GraphicsCommandList *list)
{
    ID3D12CommandList *lists[] = {(ID3D12CommandList *)list};
    ID3D12CommandQueue_ExecuteCommandLists(queue, 1, lists);
}

#define reset_command_list(a, b) reset_command_list_(__LINE__, a, b)
static inline void reset_command_list_(unsigned int line,
        ID3D12GraphicsCommandList *list, ID3D12CommandAllocator *allocator)
{
    HRESULT hr;

    hr = ID3D12CommandAllocator_Reset(allocator);
    assert_that_(line)(hr == S_OK, "Failed to reset command allocator, hr %#x.\n", hr);
    hr = ID3D12GraphicsCommandList_Reset(list, allocator, NULL);
    assert_that_(line)(hr == S_OK, "Failed to reset command list, hr %#x.\n", hr);
}

#define queue_signal(a, b, c) queue_signal_(__LINE__, a, b, c)
static inline void queue_signal_(unsigned int line, ID3D12CommandQueue *queue, ID3D12Fence *fence, uint64_t value)
{
    HRESULT hr;

    hr = ID3D12CommandQueue_Signal(queue, fence, value);
    ok_(line)(hr == S_OK, "Failed to submit signal operation to queue, hr %#x.\n", hr);
}

#define queue_wait(a, b, c) queue_wait_(__LINE__, a, b, c)
static inline void queue_wait_(unsigned int line, ID3D12CommandQueue *queue, ID3D12Fence *fence, uint64_t value)
{
    HRESULT hr;

    hr = ID3D12CommandQueue_Wait(queue, fence, value);
    ok_(line)(hr == S_OK, "Failed to submit wait operation to queue, hr %#x.\n", hr);
}

#define create_placed_buffer(a, b, c, d, e, f) create_placed_buffer_(__LINE__, a, b, c, d, e, f)
static inline ID3D12Resource *create_placed_buffer_(unsigned int line, ID3D12Device *device,
        ID3D12Heap *heap, size_t offset, size_t size, D3D12_RESOURCE_FLAGS resource_flags,
        D3D12_RESOURCE_STATES initial_resource_state)
{
    D3D12_RESOURCE_DESC resource_desc;
    ID3D12Resource *buffer;
    HRESULT hr;

    resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    resource_desc.Alignment = 0;
    resource_desc.Width = size;
    resource_desc.Height = 1;
    resource_desc.DepthOrArraySize = 1;
    resource_desc.MipLevels = 1;
    resource_desc.Format = DXGI_FORMAT_UNKNOWN;
    resource_desc.SampleDesc.Count = 1;
    resource_desc.SampleDesc.Quality = 0;
    resource_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    resource_desc.Flags = resource_flags;

    hr = ID3D12Device_CreatePlacedResource(device, heap, offset, &resource_desc,
            initial_resource_state, NULL, &IID_ID3D12Resource, (void **)&buffer);
    assert_that_(line)(SUCCEEDED(hr), "Failed to create buffer, hr %#x.\n", hr);
    return buffer;
}

#define create_placed_buffer2(a, b, c, d, e) create_placed_buffer2_(__LINE__, a, b, c, d, e)
static inline ID3D12Resource *create_placed_buffer2_(unsigned int line, ID3D12Device *device,
        ID3D12Heap *heap, size_t offset, size_t size, D3D12_RESOURCE_FLAGS resource_flags)
{
    D3D12_RESOURCE_DESC1 resource_desc;
    ID3D12Device10 *device10;
    ID3D12Resource *buffer;
    HRESULT hr;

    memset(&resource_desc, 0, sizeof(resource_desc));
    resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    resource_desc.Alignment = 0;
    resource_desc.Width = size;
    resource_desc.Height = 1;
    resource_desc.DepthOrArraySize = 1;
    resource_desc.MipLevels = 1;
    resource_desc.Format = DXGI_FORMAT_UNKNOWN;
    resource_desc.SampleDesc.Count = 1;
    resource_desc.SampleDesc.Quality = 0;
    resource_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    resource_desc.Flags = resource_flags;

    hr = ID3D12Device_QueryInterface(device, &IID_ID3D12Device10, (void **)&device10);
    assert_that_(line)(SUCCEEDED(hr), "Failed to query device10 interface.\n");
    if (FAILED(hr))
        return NULL;

    hr = ID3D12Device10_CreatePlacedResource2(device10, heap, offset, &resource_desc,
            D3D12_BARRIER_LAYOUT_UNDEFINED, NULL, 0, NULL, &IID_ID3D12Resource, (void **)&buffer);
    assert_that_(line)(SUCCEEDED(hr), "Failed to create buffer, hr %#x.\n", hr);
    ID3D12Device10_Release(device10);
    return buffer;
}

#define create_buffer(a, b, c, d, e) create_buffer_(__LINE__, a, b, c, d, e)
static inline ID3D12Resource *create_buffer_(unsigned int line, ID3D12Device *device,
        D3D12_HEAP_TYPE heap_type, size_t size, D3D12_RESOURCE_FLAGS resource_flags,
        D3D12_RESOURCE_STATES initial_resource_state)
{
    D3D12_HEAP_PROPERTIES heap_properties;
    D3D12_RESOURCE_DESC resource_desc;
    ID3D12Resource *buffer;
    HRESULT hr;

    memset(&heap_properties, 0, sizeof(heap_properties));
    heap_properties.Type = heap_type;

    resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    resource_desc.Alignment = 0;
    resource_desc.Width = size;
    resource_desc.Height = 1;
    resource_desc.DepthOrArraySize = 1;
    resource_desc.MipLevels = 1;
    resource_desc.Format = DXGI_FORMAT_UNKNOWN;
    resource_desc.SampleDesc.Count = 1;
    resource_desc.SampleDesc.Quality = 0;
    resource_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    resource_desc.Flags = resource_flags;

    hr = ID3D12Device_CreateCommittedResource(device, &heap_properties,
            D3D12_HEAP_FLAG_NONE, &resource_desc, initial_resource_state,
            NULL, &IID_ID3D12Resource, (void **)&buffer);
    assert_that_(line)(SUCCEEDED(hr), "Failed to create buffer, hr %#x.\n", hr);
    return buffer;
}

#define create_buffer2(a, b, c, d) create_buffer2_(__LINE__, a, b, c, d)
static inline ID3D12Resource *create_buffer2_(unsigned int line, ID3D12Device *device,
    D3D12_HEAP_TYPE heap_type, size_t size, D3D12_RESOURCE_FLAGS resource_flags)
{
    D3D12_HEAP_PROPERTIES heap_properties;
    D3D12_RESOURCE_DESC1 resource_desc;
    ID3D12Device10 *device10;
    ID3D12Resource *buffer;
    HRESULT hr;

    memset(&heap_properties, 0, sizeof(heap_properties));
    heap_properties.Type = heap_type;

    memset(&resource_desc, 0, sizeof(resource_desc));
    resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    resource_desc.Alignment = 0;
    resource_desc.Width = size;
    resource_desc.Height = 1;
    resource_desc.DepthOrArraySize = 1;
    resource_desc.MipLevels = 1;
    resource_desc.Format = DXGI_FORMAT_UNKNOWN;
    resource_desc.SampleDesc.Count = 1;
    resource_desc.SampleDesc.Quality = 0;
    resource_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    resource_desc.Flags = resource_flags;

    hr = ID3D12Device_QueryInterface(device, &IID_ID3D12Device10, (void **)&device10);
    assert_that_(line)(SUCCEEDED(hr), "Failed to query device10 interface.\n");
    if (FAILED(hr))
        return NULL;

    hr = ID3D12Device10_CreateCommittedResource3(device10, &heap_properties,
            D3D12_HEAP_FLAG_NONE, &resource_desc, D3D12_BARRIER_LAYOUT_UNDEFINED,
            NULL, NULL, 0, NULL, &IID_ID3D12Resource, (void **)&buffer);
    assert_that_(line)(SUCCEEDED(hr), "Failed to create buffer, hr %#x.\n", hr);
    ID3D12Device10_Release(device10);
    return buffer;
}

#define create_readback_buffer(a, b) create_readback_buffer_(__LINE__, a, b)
static inline ID3D12Resource *create_readback_buffer_(unsigned int line, ID3D12Device *device,
        size_t size)
{
    return create_buffer_(line, device, D3D12_HEAP_TYPE_READBACK, size,
            D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
}

#define create_readback_buffer2(a, b) create_readback_buffer2_(__LINE__, a, b)
static inline ID3D12Resource *create_readback_buffer2_(unsigned int line, ID3D12Device *device,
        size_t size)
{
    return create_buffer2_(line, device, D3D12_HEAP_TYPE_READBACK, size,
            D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE);
}

#define update_buffer_data(a, b, c, d) update_buffer_data_(__LINE__, a, b, c, d)
static inline void update_buffer_data_(unsigned int line, ID3D12Resource *buffer,
        size_t offset, size_t size, const void *data)
{
    D3D12_RANGE range;
    HRESULT hr;
    void *ptr;

    range.Begin = range.End = 0;
    hr = ID3D12Resource_Map(buffer, 0, &range, &ptr);
    ok_(line)(hr == S_OK, "Failed to map buffer, hr %#x.\n", hr);
    memcpy((BYTE *)ptr + offset, data, size);
    ID3D12Resource_Unmap(buffer, 0, NULL);
}

#define create_upload_buffer(a, b, c) create_upload_buffer_(__LINE__, a, b, c)
static inline ID3D12Resource *create_upload_buffer_(unsigned int line, ID3D12Device *device,
        size_t size, const void *data)
{
    ID3D12Resource *buffer;

    buffer = create_buffer_(line, device, D3D12_HEAP_TYPE_UPLOAD, size,
            D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ);
    if (data)
        update_buffer_data_(line, buffer, 0, size, data);
    return buffer;
}

#define create_upload_buffer2(a, b, c) create_upload_buffer2_(__LINE__, a, b, c)
static inline ID3D12Resource *create_upload_buffer2_(unsigned int line, ID3D12Device *device,
        size_t size, const void *data)
{
    ID3D12Resource *buffer;

    buffer = create_buffer2_(line, device, D3D12_HEAP_TYPE_UPLOAD, size,
            D3D12_RESOURCE_FLAG_NONE);
    if (data)
        update_buffer_data_(line, buffer, 0, size, data);
    return buffer;
}

#define create_cpu_descriptor_heap(a, b, c) create_cpu_descriptor_heap_(__LINE__, a, b, c)
static inline ID3D12DescriptorHeap *create_cpu_descriptor_heap_(unsigned int line, ID3D12Device *device,
        D3D12_DESCRIPTOR_HEAP_TYPE heap_type, unsigned int descriptor_count)
{
    D3D12_DESCRIPTOR_HEAP_DESC heap_desc;
    ID3D12DescriptorHeap *descriptor_heap;
    HRESULT hr;

    heap_desc.Type = heap_type,
    heap_desc.NumDescriptors = descriptor_count;
    heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    heap_desc.NodeMask = 0;
    hr = ID3D12Device_CreateDescriptorHeap(device, &heap_desc,
            &IID_ID3D12DescriptorHeap, (void **)&descriptor_heap);
    ok_(line)(SUCCEEDED(hr), "Failed to create descriptor heap, hr %#x.\n", hr);

    return descriptor_heap;
}

#define create_gpu_descriptor_heap(a, b, c) create_gpu_descriptor_heap_(__LINE__, a, b, c)
static inline ID3D12DescriptorHeap *create_gpu_descriptor_heap_(unsigned int line, ID3D12Device *device,
        D3D12_DESCRIPTOR_HEAP_TYPE heap_type, unsigned int descriptor_count)
{
    D3D12_DESCRIPTOR_HEAP_DESC heap_desc;
    ID3D12DescriptorHeap *descriptor_heap;
    HRESULT hr;

    heap_desc.Type = heap_type,
    heap_desc.NumDescriptors = descriptor_count;
    heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    heap_desc.NodeMask = 0;
    hr = ID3D12Device_CreateDescriptorHeap(device, &heap_desc,
            &IID_ID3D12DescriptorHeap, (void **)&descriptor_heap);
    ok_(line)(SUCCEEDED(hr), "Failed to create descriptor heap, hr %#x.\n", hr);

    return descriptor_heap;
}

static inline void transition_sub_resource_state(ID3D12GraphicsCommandList *list, ID3D12Resource *resource,
        unsigned int sub_resource_idx, D3D12_RESOURCE_STATES state_before, D3D12_RESOURCE_STATES state_after)
{
    D3D12_RESOURCE_BARRIER barrier;

    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier.Transition.pResource = resource;
    barrier.Transition.Subresource = sub_resource_idx;
    barrier.Transition.StateBefore = state_before;
    barrier.Transition.StateAfter = state_after;

    ID3D12GraphicsCommandList_ResourceBarrier(list, 1, &barrier);
}

#define create_command_queue(a, b, c) create_command_queue_(__LINE__, a, b, c)
static inline ID3D12CommandQueue *create_command_queue_(unsigned int line, ID3D12Device *device,
        D3D12_COMMAND_LIST_TYPE type, int priority)
{
    D3D12_COMMAND_QUEUE_DESC queue_desc;
    ID3D12CommandQueue *queue;
    HRESULT hr;

    queue_desc.Type = type;
    queue_desc.Priority = priority;
    queue_desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    queue_desc.NodeMask = 0;
    hr = ID3D12Device_CreateCommandQueue(device, &queue_desc, &IID_ID3D12CommandQueue, (void **)&queue);
    ok_(line)(hr == S_OK, "Failed to create command queue, hr %#x.\n", hr);

    return queue;
}

static inline void transition_resource_state(ID3D12GraphicsCommandList *list, ID3D12Resource *resource,
        D3D12_RESOURCE_STATES state_before, D3D12_RESOURCE_STATES state_after)
{
    transition_sub_resource_state(list, resource, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
            state_before, state_after);
}

static inline unsigned int format_size(DXGI_FORMAT format)
{
    switch (format)
    {
        case DXGI_FORMAT_R32G32B32A32_FLOAT:
        case DXGI_FORMAT_R32G32B32A32_UINT:
        case DXGI_FORMAT_R8G8_UNORM:
            return 16;
        case DXGI_FORMAT_R16G16B16A16_TYPELESS:
        case DXGI_FORMAT_R32G32_UINT:
        case DXGI_FORMAT_R32G32_TYPELESS:
            return 8;
        case DXGI_FORMAT_R32_TYPELESS:
        case DXGI_FORMAT_D32_FLOAT:
        case DXGI_FORMAT_R32_FLOAT:
        case DXGI_FORMAT_R32_UINT:
        case DXGI_FORMAT_R32_SINT:
        case DXGI_FORMAT_R16G16_FLOAT:
        case DXGI_FORMAT_R16G16_UNORM:
        case DXGI_FORMAT_R16G16_UINT:
        case DXGI_FORMAT_R11G11B10_FLOAT:
        case DXGI_FORMAT_R10G10B10A2_UINT:
        case DXGI_FORMAT_R10G10B10A2_UNORM:
        case DXGI_FORMAT_R10G10B10A2_TYPELESS:
        case DXGI_FORMAT_R8G8B8A8_TYPELESS:
        case DXGI_FORMAT_R8G8B8A8_UNORM:
        case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
        case DXGI_FORMAT_R8G8B8A8_UINT:
        case DXGI_FORMAT_R8G8B8A8_SINT:
        case DXGI_FORMAT_R8G8B8A8_SNORM:
        case DXGI_FORMAT_B8G8R8A8_UNORM:
            return 4;
        case DXGI_FORMAT_R16_FLOAT:
        case DXGI_FORMAT_R16_UNORM:
        case DXGI_FORMAT_R16_UINT:
            return 2;
        case DXGI_FORMAT_UNKNOWN:
        case DXGI_FORMAT_A8_UNORM:
        case DXGI_FORMAT_R8_UINT:
        case DXGI_FORMAT_R8_UNORM:
            return 1;

        case DXGI_FORMAT_BC2_UNORM:
        case DXGI_FORMAT_BC2_UNORM_SRGB:
        case DXGI_FORMAT_BC3_UNORM:
        case DXGI_FORMAT_BC3_UNORM_SRGB:
        case DXGI_FORMAT_BC5_UNORM:
        case DXGI_FORMAT_BC5_SNORM:
        case DXGI_FORMAT_BC6H_UF16:
        case DXGI_FORMAT_BC6H_SF16:
        case DXGI_FORMAT_BC7_UNORM:
        case DXGI_FORMAT_BC7_UNORM_SRGB:
            return 16;
        case DXGI_FORMAT_BC1_UNORM:
        case DXGI_FORMAT_BC1_UNORM_SRGB:
        case DXGI_FORMAT_BC4_UNORM:
        case DXGI_FORMAT_BC4_SNORM:
            return 8;

        default:
            trace("Unhandled format %#x.\n", format);
            return 1;
    }
}

static inline unsigned int format_num_planes(DXGI_FORMAT format)
{
    switch (format)
    {
        case DXGI_FORMAT_D24_UNORM_S8_UINT:
        case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
        case DXGI_FORMAT_R24G8_TYPELESS:
        case DXGI_FORMAT_R32G8X24_TYPELESS:
            return 2;

        default:
            return 1;
    }
}

static inline unsigned int format_size_planar(DXGI_FORMAT format, unsigned int plane)
{
    switch (format)
    {
        case DXGI_FORMAT_D24_UNORM_S8_UINT:
        case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
        case DXGI_FORMAT_R24G8_TYPELESS:
        case DXGI_FORMAT_R32G8X24_TYPELESS:
        case DXGI_FORMAT_R24_UNORM_X8_TYPELESS:
            return plane ? 1 : 4;

        default:
            return format_size(format);
    }
}

static inline unsigned int format_to_footprint_format(DXGI_FORMAT format, unsigned int plane)
{
    switch (format)
    {
        case DXGI_FORMAT_D24_UNORM_S8_UINT:
        case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
        case DXGI_FORMAT_R24G8_TYPELESS:
        case DXGI_FORMAT_R32G8X24_TYPELESS:
        case DXGI_FORMAT_R24_UNORM_X8_TYPELESS:
            return plane ? DXGI_FORMAT_R8_TYPELESS : DXGI_FORMAT_R32_TYPELESS;

        default:
            return format;
    }
}

static inline unsigned int format_block_width(DXGI_FORMAT format)
{
    switch (format)
    {
        case DXGI_FORMAT_BC1_UNORM:
        case DXGI_FORMAT_BC1_UNORM_SRGB:
        case DXGI_FORMAT_BC4_UNORM:
        case DXGI_FORMAT_BC4_SNORM:
        case DXGI_FORMAT_BC2_UNORM:
        case DXGI_FORMAT_BC2_UNORM_SRGB:
        case DXGI_FORMAT_BC3_UNORM:
        case DXGI_FORMAT_BC3_UNORM_SRGB:
        case DXGI_FORMAT_BC5_UNORM:
        case DXGI_FORMAT_BC5_SNORM:
        case DXGI_FORMAT_BC6H_UF16:
        case DXGI_FORMAT_BC6H_SF16:
        case DXGI_FORMAT_BC7_UNORM:
        case DXGI_FORMAT_BC7_UNORM_SRGB:
            return 4;
        default:
            return 1;
    }
}

static inline unsigned int format_block_height(DXGI_FORMAT format)
{
    switch (format)
    {
        case DXGI_FORMAT_BC1_UNORM:
        case DXGI_FORMAT_BC1_UNORM_SRGB:
        case DXGI_FORMAT_BC4_UNORM:
        case DXGI_FORMAT_BC4_SNORM:
        case DXGI_FORMAT_BC2_UNORM:
        case DXGI_FORMAT_BC2_UNORM_SRGB:
        case DXGI_FORMAT_BC3_UNORM:
        case DXGI_FORMAT_BC3_UNORM_SRGB:
        case DXGI_FORMAT_BC5_UNORM:
        case DXGI_FORMAT_BC5_SNORM:
        case DXGI_FORMAT_BC6H_UF16:
        case DXGI_FORMAT_BC6H_SF16:
        case DXGI_FORMAT_BC7_UNORM:
        case DXGI_FORMAT_BC7_UNORM_SRGB:
            return 4;
        default:
            return 1;
    }
}

struct resource_readback
{
    uint64_t width;
    unsigned int height;
    unsigned int depth;
    ID3D12Resource *resource;
    uint64_t row_pitch;
    void *data;
};

static inline void get_texture_readback_with_command_list(ID3D12Resource *texture, unsigned int sub_resource,
        struct resource_readback *rb, ID3D12CommandQueue *queue, ID3D12GraphicsCommandList *command_list)
{
    D3D12_TEXTURE_COPY_LOCATION dst_location, src_location;
    D3D12_HEAP_PROPERTIES heap_properties;
    unsigned int miplevel, plane, layers;
    D3D12_RESOURCE_DESC resource_desc;
    ID3D12Resource *src_resource;
    D3D12_RANGE read_range;
    ID3D12Device *device;
    uint64_t buffer_size;
    HRESULT hr;

    hr = ID3D12Resource_GetDevice(texture, &IID_ID3D12Device, (void **)&device);
    assert_that(hr == S_OK, "Failed to get device, hr %#x.\n", hr);

    resource_desc = ID3D12Resource_GetDesc(texture);
    assert_that(resource_desc.Dimension != D3D12_RESOURCE_DIMENSION_BUFFER,
            "Resource %p is not texture.\n", texture);

    miplevel = sub_resource % resource_desc.MipLevels;
    layers = resource_desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D ?
        1 : resource_desc.DepthOrArraySize;
    plane = sub_resource / (resource_desc.MipLevels * layers);
    rb->width = max(1, resource_desc.Width >> miplevel);
    rb->width = align(rb->width, format_block_width(resource_desc.Format));
    rb->height = max(1, resource_desc.Height >> miplevel);
    rb->height = align(rb->height, format_block_height(resource_desc.Format));
    rb->depth = resource_desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D
            ? max(1, resource_desc.DepthOrArraySize >> miplevel) : 1;
    rb->row_pitch = align(rb->width * format_size_planar(resource_desc.Format, plane), D3D12_TEXTURE_DATA_PITCH_ALIGNMENT);
    rb->data = NULL;

    if (resource_desc.SampleDesc.Count > 1)
    {
        memset(&heap_properties, 0, sizeof(heap_properties));
        heap_properties.Type = D3D12_HEAP_TYPE_DEFAULT;

        resource_desc.Alignment = 0;
        resource_desc.DepthOrArraySize = 1;
        resource_desc.SampleDesc.Count = 1;
        resource_desc.SampleDesc.Quality = 0;
        hr = ID3D12Device_CreateCommittedResource(device, &heap_properties, D3D12_HEAP_FLAG_NONE,
                &resource_desc, D3D12_RESOURCE_STATE_RESOLVE_DEST, NULL,
                &IID_ID3D12Resource, (void **)&src_resource);
        assert_that(hr == S_OK, "Failed to create texture, hr %#x.\n", hr);

        ID3D12GraphicsCommandList_ResolveSubresource(command_list,
                src_resource, 0, texture, sub_resource, resource_desc.Format);
        transition_resource_state(command_list, src_resource,
                D3D12_RESOURCE_STATE_RESOLVE_DEST, D3D12_RESOURCE_STATE_COPY_SOURCE);

        sub_resource = 0;
    }
    else
    {
        src_resource = texture;
    }

    buffer_size = rb->row_pitch * rb->height * rb->depth;
    rb->resource = create_readback_buffer(device, buffer_size);

    dst_location.pResource = rb->resource;
    dst_location.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dst_location.PlacedFootprint.Offset = 0;
    dst_location.PlacedFootprint.Footprint.Format = format_to_footprint_format(resource_desc.Format, plane);
    dst_location.PlacedFootprint.Footprint.Width = rb->width;
    dst_location.PlacedFootprint.Footprint.Height = rb->height;
    dst_location.PlacedFootprint.Footprint.Depth = rb->depth;
    dst_location.PlacedFootprint.Footprint.RowPitch = rb->row_pitch;

    src_location.pResource = src_resource;
    src_location.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    src_location.SubresourceIndex = sub_resource;

    ID3D12GraphicsCommandList_CopyTextureRegion(command_list, &dst_location, 0, 0, 0, &src_location, NULL);
    hr = ID3D12GraphicsCommandList_Close(command_list);
    assert_that(hr == S_OK, "Failed to close command list, hr %#x.\n", hr);

    exec_command_list(queue, command_list);
    wait_queue_idle(device, queue);
    ID3D12Device_Release(device);

    if (src_resource != texture)
        ID3D12Resource_Release(src_resource);

    read_range.Begin = 0;
    read_range.End = buffer_size;
    hr = ID3D12Resource_Map(rb->resource, 0, &read_range, &rb->data);
    assert_that(hr == S_OK, "Failed to map readback buffer, hr %#x.\n", hr);
}

static inline void *get_readback_data(struct resource_readback *rb,
        unsigned int x, unsigned int y, unsigned int z, size_t element_size)
{
    unsigned int slice_pitch = rb->row_pitch * rb->height;
    return &((BYTE *)rb->data)[slice_pitch * z + rb->row_pitch * y + x * element_size];
}

static inline unsigned int get_readback_uint(struct resource_readback *rb,
        unsigned int x, unsigned int y, unsigned int z)
{
    return *(unsigned int *)get_readback_data(rb, x, y, z, sizeof(unsigned int));
}

static inline void release_resource_readback(struct resource_readback *rb)
{
    D3D12_RANGE range = {0, 0};
    ID3D12Resource_Unmap(rb->resource, 0, &range);
    ID3D12Resource_Release(rb->resource);
}

#define check_readback_data_uint(a, b, c, d) check_readback_data_uint_(__LINE__, a, b, c, d)
static inline void check_readback_data_uint_(unsigned int line, struct resource_readback *rb,
        const D3D12_BOX *box, unsigned int expected, unsigned int max_diff)
{
    D3D12_BOX b = {0, 0, 0, rb->width, rb->height, rb->depth};
    unsigned int x = 0, y = 0, z;
    bool all_match = true;
    unsigned int got = 0;

    if (box)
        b = *box;

    for (z = b.front; z < b.back; ++z)
    {
        for (y = b.top; y < b.bottom; ++y)
        {
            for (x = b.left; x < b.right; ++x)
            {
                got = get_readback_uint(rb, x, y, z);
                if (!compare_color(got, expected, max_diff))
                {
                    all_match = false;
                    break;
                }
            }
            if (!all_match)
                break;
        }
        if (!all_match)
            break;
    }
    ok_(line)(all_match, "Got 0x%08x, expected 0x%08x at (%u, %u, %u).\n", got, expected, x, y, z);
}

#define check_sub_resource_uint(a, b, c, d, e, f) check_sub_resource_uint_(__LINE__, a, b, c, d, e, f)
static inline void check_sub_resource_uint_(unsigned int line, ID3D12Resource *texture,
        unsigned int sub_resource_idx, ID3D12CommandQueue *queue, ID3D12GraphicsCommandList *command_list,
        unsigned int expected, unsigned int max_diff)
{
    struct resource_readback rb;

    get_texture_readback_with_command_list(texture, sub_resource_idx, &rb, queue, command_list);
    check_readback_data_uint_(line, &rb, NULL, expected, max_diff);
    release_resource_readback(&rb);
}

#define create_default_buffer(a, b, c, d) create_default_buffer_(__LINE__, a, b, c, d)
static inline ID3D12Resource *create_default_buffer_(unsigned int line, ID3D12Device *device,
        size_t size, D3D12_RESOURCE_FLAGS resource_flags, D3D12_RESOURCE_STATES initial_resource_state)
{
    return create_buffer_(line, device, D3D12_HEAP_TYPE_DEFAULT, size,
            resource_flags, initial_resource_state);
}

#define create_default_buffer2(a, b, c) create_default_buffer2_(__LINE__, a, b, c)
static inline ID3D12Resource *create_default_buffer2_(unsigned int line, ID3D12Device *device,
    size_t size, D3D12_RESOURCE_FLAGS resource_flags)
{
    return create_buffer2_(line, device, D3D12_HEAP_TYPE_DEFAULT, size,
            resource_flags);
}

static inline ID3D12Resource *create_default_texture_(unsigned int line, ID3D12Device *device,
        D3D12_RESOURCE_DIMENSION dimension, unsigned int width, unsigned int height,
        unsigned int depth_or_array_size, unsigned int miplevel_count, DXGI_FORMAT format,
        D3D12_RESOURCE_FLAGS flags, D3D12_RESOURCE_STATES initial_state)
{
    D3D12_HEAP_PROPERTIES heap_properties;
    D3D12_RESOURCE_DESC resource_desc;
    ID3D12Resource *texture;
    HRESULT hr;

    assert(dimension != D3D12_RESOURCE_DIMENSION_BUFFER);

    memset(&heap_properties, 0, sizeof(heap_properties));
    heap_properties.Type = D3D12_HEAP_TYPE_DEFAULT;

    memset(&resource_desc, 0, sizeof(resource_desc));
    resource_desc.Dimension = dimension;
    resource_desc.Width = width;
    resource_desc.Height = height;
    resource_desc.DepthOrArraySize = depth_or_array_size;
    resource_desc.MipLevels = miplevel_count;
    resource_desc.Format = format;
    resource_desc.SampleDesc.Count = 1;
    resource_desc.Flags = flags;
    hr = ID3D12Device_CreateCommittedResource(device, &heap_properties, D3D12_HEAP_FLAG_NONE,
            &resource_desc, initial_state, NULL, &IID_ID3D12Resource, (void **)&texture);
    ok_(line)(SUCCEEDED(hr), "Failed to create texture, hr %#x.\n", hr);

    return texture;
}

static inline ID3D12Resource *create_default_texture_enhanced_(unsigned int line, ID3D12Device *device,
        D3D12_RESOURCE_DIMENSION dimension, unsigned int width, unsigned int height,
        unsigned int depth_or_array_size, unsigned int miplevel_count, DXGI_FORMAT format,
        D3D12_RESOURCE_FLAGS flags, D3D12_BARRIER_LAYOUT initial_layout)
{
    D3D12_HEAP_PROPERTIES heap_properties;
    D3D12_RESOURCE_DESC1 resource_desc;
    ID3D12Device10 *device10;
    ID3D12Resource *texture;
    HRESULT hr;

    assert(dimension != D3D12_RESOURCE_DIMENSION_BUFFER);

    memset(&heap_properties, 0, sizeof(heap_properties));
    heap_properties.Type = D3D12_HEAP_TYPE_DEFAULT;

    if (FAILED(ID3D12Device_QueryInterface(device, &IID_ID3D12Device10, (void **)&device10)))
        return NULL;

    memset(&resource_desc, 0, sizeof(resource_desc));
    resource_desc.Dimension = dimension;
    resource_desc.Width = width;
    resource_desc.Height = height;
    resource_desc.DepthOrArraySize = depth_or_array_size;
    resource_desc.MipLevels = miplevel_count;
    resource_desc.Format = format;
    resource_desc.SampleDesc.Count = 1;
    resource_desc.Flags = flags;
    hr = ID3D12Device10_CreateCommittedResource3(device10, &heap_properties, D3D12_HEAP_FLAG_NONE,
        &resource_desc, initial_layout, NULL, NULL, 0, NULL, &IID_ID3D12Resource, (void **)&texture);
    ok_(line)(SUCCEEDED(hr), "Failed to create texture, hr %#x.\n", hr);

    ID3D12Device10_Release(device10);

    return texture;
}

#define create_default_texture(a, b, c, d, e, f) create_default_texture2d_(__LINE__, a, b, c, 1, 1, d, e, f)
#define create_default_texture2d(a, b, c, d, e, f, g, h) create_default_texture2d_(__LINE__, a, b, c, d, e, f, g, h)
static inline ID3D12Resource *create_default_texture2d_(unsigned int line, ID3D12Device *device,
        unsigned int width, unsigned int height, unsigned int array_size, unsigned int miplevel_count,
        DXGI_FORMAT format, D3D12_RESOURCE_FLAGS flags, D3D12_RESOURCE_STATES initial_state)
{
    return create_default_texture_(line, device, D3D12_RESOURCE_DIMENSION_TEXTURE2D,
            width, height, array_size, miplevel_count, format, flags, initial_state);
}

#define create_default_texture_enhanced(a, b, c, d, e, f) create_default_texture2d_enhanced_(__LINE__, a, b, c, 1, 1, d, e, f)
#define create_default_texture2d_enhanced(a, b, c, d, e, f, g, h) create_default_texture2d_enhanced_(__LINE__, a, b, c, d, e, f, g, h)
static inline ID3D12Resource *create_default_texture2d_enhanced_(unsigned int line, ID3D12Device *device,
    unsigned int width, unsigned int height, unsigned int array_size, unsigned int miplevel_count,
    DXGI_FORMAT format, D3D12_RESOURCE_FLAGS flags, D3D12_BARRIER_LAYOUT initial_layout)
{
    return create_default_texture_enhanced_(line, device, D3D12_RESOURCE_DIMENSION_TEXTURE2D,
        width, height, array_size, miplevel_count, format, flags, initial_layout);
}

#define create_default_texture3d(a, b, c, d, e, f, g, h) create_default_texture3d_(__LINE__, a, b, c, d, e, f, g, h)
static inline ID3D12Resource *create_default_texture3d_(unsigned int line, ID3D12Device *device,
        unsigned int width, unsigned int height, unsigned int depth, unsigned int miplevel_count,
        DXGI_FORMAT format, D3D12_RESOURCE_FLAGS flags, D3D12_RESOURCE_STATES initial_state)
{
    return create_default_texture_(line, device, D3D12_RESOURCE_DIMENSION_TEXTURE3D,
            width, height, depth, miplevel_count, format, flags, initial_state);
}

#define create_default_texture3d_enhanced(a, b, c, d, e, f, g, h) create_default_texture3d_enhanced_(__LINE__, a, b, c, d, e, f, g, h)
static inline ID3D12Resource *create_default_texture3d_enhanced_(unsigned int line, ID3D12Device *device,
    unsigned int width, unsigned int height, unsigned int depth, unsigned int miplevel_count,
    DXGI_FORMAT format, D3D12_RESOURCE_FLAGS flags, D3D12_BARRIER_LAYOUT initial_layout)
{
    return create_default_texture_enhanced_(line, device, D3D12_RESOURCE_DIMENSION_TEXTURE3D,
        width, height, depth, miplevel_count, format, flags, initial_layout);
}

static inline HRESULT create_root_signature(ID3D12Device *device, const D3D12_ROOT_SIGNATURE_DESC *desc,
        ID3D12RootSignature **root_signature)
{
    ID3DBlob *blob;
    HRESULT hr;

    if (FAILED(hr = D3D12SerializeRootSignature(desc, D3D_ROOT_SIGNATURE_VERSION_1_0, &blob, NULL)))
        return hr;

    hr = ID3D12Device_CreateRootSignature(device, 0, ID3D10Blob_GetBufferPointer(blob),
            ID3D10Blob_GetBufferSize(blob), &IID_ID3D12RootSignature, (void **)root_signature);
    ID3D10Blob_Release(blob);
    return hr;
}

static inline HRESULT create_versioned_root_signature(ID3D12Device *device, const D3D12_VERSIONED_ROOT_SIGNATURE_DESC *desc,
        ID3D12RootSignature **root_signature)
{
    ID3DBlob *blob;
    HRESULT hr;

    if (FAILED(hr = D3D12SerializeVersionedRootSignature(desc, &blob, NULL)))
        return hr;

    hr = ID3D12Device_CreateRootSignature(device, 0, ID3D10Blob_GetBufferPointer(blob),
        ID3D10Blob_GetBufferSize(blob), &IID_ID3D12RootSignature, (void **)root_signature);
    ID3D10Blob_Release(blob);
    return hr;
}

#define create_empty_root_signature(device, flags) create_empty_root_signature_(__LINE__, device, flags)
static inline ID3D12RootSignature *create_empty_root_signature_(unsigned int line,
        ID3D12Device *device, D3D12_ROOT_SIGNATURE_FLAGS flags)
{
    D3D12_ROOT_SIGNATURE_DESC root_signature_desc;
    ID3D12RootSignature *root_signature = NULL;
    HRESULT hr;

    root_signature_desc.NumParameters = 0;
    root_signature_desc.pParameters = NULL;
    root_signature_desc.NumStaticSamplers = 0;
    root_signature_desc.pStaticSamplers = NULL;
    root_signature_desc.Flags = flags;
    hr = create_root_signature(device, &root_signature_desc, &root_signature);
    ok_(line)(SUCCEEDED(hr), "Failed to create root signature, hr %#x.\n", hr);

    return root_signature;
}

static inline void init_pipeline_state_desc_shaders(D3D12_GRAPHICS_PIPELINE_STATE_DESC *desc,
        ID3D12RootSignature *root_signature, DXGI_FORMAT rt_format,
        const D3D12_INPUT_LAYOUT_DESC *input_layout,
        const void *vs_code, size_t vs_size,
        const void *ps_code, size_t ps_size)
{
    memset(desc, 0, sizeof(*desc));
    desc->pRootSignature = root_signature;
    desc->VS = shader_bytecode(vs_code, vs_size);
    desc->PS = shader_bytecode(ps_code, ps_size);
    desc->StreamOutput.RasterizedStream = 0;
    desc->BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    desc->RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    desc->RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
    if (input_layout)
        desc->InputLayout = *input_layout;
    desc->SampleMask = ~(UINT)0;
    desc->PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    if (rt_format)
    {
        desc->NumRenderTargets = 1;
        desc->RTVFormats[0] = rt_format;
    }
    desc->SampleDesc.Count = 1;
}

static inline void init_pipeline_state_desc(D3D12_GRAPHICS_PIPELINE_STATE_DESC *desc,
        ID3D12RootSignature *root_signature, DXGI_FORMAT rt_format,
        const D3D12_SHADER_BYTECODE *vs, const D3D12_SHADER_BYTECODE *ps,
        const D3D12_INPUT_LAYOUT_DESC *input_layout)
{
    static const DWORD vs_code[] =
    {
#if 0
        void main(uint id : SV_VertexID, out float4 position : SV_Position)
        {
            float2 coords = float2((id << 1) & 2, id & 2);
            position = float4(coords * float2(2, -2) + float2(-1, 1), 0, 1);
        }
#endif
        0x43425844, 0xf900d25e, 0x68bfefa7, 0xa63ac0a7, 0xa476af7a, 0x00000001, 0x0000018c, 0x00000003,
        0x0000002c, 0x00000060, 0x00000094, 0x4e475349, 0x0000002c, 0x00000001, 0x00000008, 0x00000020,
        0x00000000, 0x00000006, 0x00000001, 0x00000000, 0x00000101, 0x565f5653, 0x65747265, 0x00444978,
        0x4e47534f, 0x0000002c, 0x00000001, 0x00000008, 0x00000020, 0x00000000, 0x00000001, 0x00000003,
        0x00000000, 0x0000000f, 0x505f5653, 0x7469736f, 0x006e6f69, 0x58454853, 0x000000f0, 0x00010050,
        0x0000003c, 0x0100086a, 0x04000060, 0x00101012, 0x00000000, 0x00000006, 0x04000067, 0x001020f2,
        0x00000000, 0x00000001, 0x02000068, 0x00000001, 0x0b00008c, 0x00100012, 0x00000000, 0x00004001,
        0x00000001, 0x00004001, 0x00000001, 0x0010100a, 0x00000000, 0x00004001, 0x00000000, 0x07000001,
        0x00100042, 0x00000000, 0x0010100a, 0x00000000, 0x00004001, 0x00000002, 0x05000056, 0x00100032,
        0x00000000, 0x00100086, 0x00000000, 0x0f000032, 0x00102032, 0x00000000, 0x00100046, 0x00000000,
        0x00004002, 0x40000000, 0xc0000000, 0x00000000, 0x00000000, 0x00004002, 0xbf800000, 0x3f800000,
        0x00000000, 0x00000000, 0x08000036, 0x001020c2, 0x00000000, 0x00004002, 0x00000000, 0x00000000,
        0x00000000, 0x3f800000, 0x0100003e,
    };
    static const DWORD ps_code[] =
    {
#if 0
        void main(const in float4 position : SV_Position, out float4 target : SV_Target0)
        {
            target = float4(0.0f, 1.0f, 0.0f, 1.0f);
        }
#endif
        0x43425844, 0x8a4a8140, 0x5eba8e0b, 0x714e0791, 0xb4b8eed2, 0x00000001, 0x000000d8, 0x00000003,
        0x0000002c, 0x00000060, 0x00000094, 0x4e475349, 0x0000002c, 0x00000001, 0x00000008, 0x00000020,
        0x00000000, 0x00000001, 0x00000003, 0x00000000, 0x0000000f, 0x505f5653, 0x7469736f, 0x006e6f69,
        0x4e47534f, 0x0000002c, 0x00000001, 0x00000008, 0x00000020, 0x00000000, 0x00000000, 0x00000003,
        0x00000000, 0x0000000f, 0x545f5653, 0x65677261, 0xabab0074, 0x58454853, 0x0000003c, 0x00000050,
        0x0000000f, 0x0100086a, 0x03000065, 0x001020f2, 0x00000000, 0x08000036, 0x001020f2, 0x00000000,
        0x00004002, 0x00000000, 0x3f800000, 0x00000000, 0x3f800000, 0x0100003e,
    };

    init_pipeline_state_desc_shaders(desc, root_signature, rt_format, input_layout,
            vs ? vs->pShaderBytecode : vs_code, vs ? vs->BytecodeLength : sizeof(vs_code),
            ps ? ps->pShaderBytecode : ps_code, ps ? ps->BytecodeLength : sizeof(ps_code));
}

static inline void init_pipeline_state_desc_dxil(D3D12_GRAPHICS_PIPELINE_STATE_DESC *desc,
    ID3D12RootSignature *root_signature, DXGI_FORMAT rt_format,
    const D3D12_SHADER_BYTECODE *vs, const D3D12_SHADER_BYTECODE *ps,
    const D3D12_INPUT_LAYOUT_DESC *input_layout)
{
    static const BYTE vs_code[] =
    {
#if 0
        void main(uint id : SV_VertexID, out float4 position : SV_Position)
        {
            float2 coords = float2((id << 1) & 2, id & 2);
            position = float4(coords * float2(2, -2) + float2(-1, 1), 0, 1);
        }
#endif
        0x44, 0x58, 0x42, 0x43, 0xa3, 0x7f, 0x47, 0x2f, 0xa9, 0x8e, 0x7f, 0x60, 0x36, 0x4d, 0x1d, 0xe6, 0xe7, 0x22, 0x1c, 0xf9, 0x01, 0x00, 0x00, 0x00, 0x08, 0x06, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00,
        0x34, 0x00, 0x00, 0x00, 0x44, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00, 0xbc, 0x00, 0x00, 0x00, 0x34, 0x01, 0x00, 0x00, 0x53, 0x46, 0x49, 0x30, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x49, 0x53, 0x47, 0x31, 0x34, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x28, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x06, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x53, 0x56, 0x5f, 0x56, 0x65, 0x72, 0x74, 0x65, 0x78, 0x49, 0x44, 0x00,
        0x4f, 0x53, 0x47, 0x31, 0x34, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x28, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
        0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0f, 0xf0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x53, 0x56, 0x5f, 0x50, 0x6f, 0x73, 0x69, 0x74, 0x69, 0x6f, 0x6e, 0x00, 0x50, 0x53, 0x56, 0x30,
        0x70, 0x00, 0x00, 0x00, 0x24, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff,
        0x01, 0x00, 0x00, 0x00, 0x01, 0x01, 0x00, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x41, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x44, 0x03,
        0x03, 0x04, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x44, 0x58, 0x49, 0x4c, 0xcc, 0x04, 0x00, 0x00, 0x60, 0x00, 0x01, 0x00,
        0x33, 0x01, 0x00, 0x00, 0x44, 0x58, 0x49, 0x4c, 0x00, 0x01, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0xb4, 0x04, 0x00, 0x00, 0x42, 0x43, 0xc0, 0xde, 0x21, 0x0c, 0x00, 0x00, 0x2a, 0x01, 0x00, 0x00,
        0x0b, 0x82, 0x20, 0x00, 0x02, 0x00, 0x00, 0x00, 0x13, 0x00, 0x00, 0x00, 0x07, 0x81, 0x23, 0x91, 0x41, 0xc8, 0x04, 0x49, 0x06, 0x10, 0x32, 0x39, 0x92, 0x01, 0x84, 0x0c, 0x25, 0x05, 0x08, 0x19,
        0x1e, 0x04, 0x8b, 0x62, 0x80, 0x10, 0x45, 0x02, 0x42, 0x92, 0x0b, 0x42, 0x84, 0x10, 0x32, 0x14, 0x38, 0x08, 0x18, 0x4b, 0x0a, 0x32, 0x42, 0x88, 0x48, 0x90, 0x14, 0x20, 0x43, 0x46, 0x88, 0xa5,
        0x00, 0x19, 0x32, 0x42, 0xe4, 0x48, 0x0e, 0x90, 0x11, 0x22, 0xc4, 0x50, 0x41, 0x51, 0x81, 0x8c, 0xe1, 0x83, 0xe5, 0x8a, 0x04, 0x21, 0x46, 0x06, 0x51, 0x18, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00,
        0x1b, 0x8c, 0xe0, 0xff, 0xff, 0xff, 0xff, 0x07, 0x40, 0x02, 0xa8, 0x0d, 0x84, 0xf0, 0xff, 0xff, 0xff, 0xff, 0x03, 0x20, 0x01, 0x00, 0x00, 0x00, 0x49, 0x18, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00,
        0x13, 0x82, 0x60, 0x42, 0x20, 0x00, 0x00, 0x00, 0x89, 0x20, 0x00, 0x00, 0x0f, 0x00, 0x00, 0x00, 0x32, 0x22, 0x08, 0x09, 0x20, 0x64, 0x85, 0x04, 0x13, 0x22, 0xa4, 0x84, 0x04, 0x13, 0x22, 0xe3,
        0x84, 0xa1, 0x90, 0x14, 0x12, 0x4c, 0x88, 0x8c, 0x0b, 0x84, 0x84, 0x4c, 0x10, 0x30, 0x23, 0x00, 0x25, 0x00, 0x8a, 0x39, 0x02, 0x30, 0x98, 0x23, 0x40, 0x8a, 0x31, 0x33, 0x43, 0x43, 0x35, 0x03,
        0x50, 0x0c, 0x98, 0x19, 0x3a, 0xc2, 0x81, 0x80, 0x1c, 0x18, 0x00, 0x00, 0x13, 0x14, 0x72, 0xc0, 0x87, 0x74, 0x60, 0x87, 0x36, 0x68, 0x87, 0x79, 0x68, 0x03, 0x72, 0xc0, 0x87, 0x0d, 0xaf, 0x50,
        0x0e, 0x6d, 0xd0, 0x0e, 0x7a, 0x50, 0x0e, 0x6d, 0x00, 0x0f, 0x7a, 0x30, 0x07, 0x72, 0xa0, 0x07, 0x73, 0x20, 0x07, 0x6d, 0x90, 0x0e, 0x71, 0xa0, 0x07, 0x73, 0x20, 0x07, 0x6d, 0x90, 0x0e, 0x78,
        0xa0, 0x07, 0x73, 0x20, 0x07, 0x6d, 0x90, 0x0e, 0x71, 0x60, 0x07, 0x7a, 0x30, 0x07, 0x72, 0xd0, 0x06, 0xe9, 0x30, 0x07, 0x72, 0xa0, 0x07, 0x73, 0x20, 0x07, 0x6d, 0x90, 0x0e, 0x76, 0x40, 0x07,
        0x7a, 0x60, 0x07, 0x74, 0xd0, 0x06, 0xe6, 0x10, 0x07, 0x76, 0xa0, 0x07, 0x73, 0x20, 0x07, 0x6d, 0x60, 0x0e, 0x73, 0x20, 0x07, 0x7a, 0x30, 0x07, 0x72, 0xd0, 0x06, 0xe6, 0x60, 0x07, 0x74, 0xa0,
        0x07, 0x76, 0x40, 0x07, 0x6d, 0xe0, 0x0e, 0x78, 0xa0, 0x07, 0x71, 0x60, 0x07, 0x7a, 0x30, 0x07, 0x72, 0xa0, 0x07, 0x76, 0x40, 0x07, 0x43, 0x9e, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x86, 0x3c, 0x05, 0x10, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0c, 0x79, 0x10, 0x20, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xc8, 0x02, 0x01,
        0x0a, 0x00, 0x00, 0x00, 0x32, 0x1e, 0x98, 0x10, 0x19, 0x11, 0x4c, 0x90, 0x8c, 0x09, 0x26, 0x47, 0xc6, 0x04, 0x43, 0x9a, 0x12, 0x18, 0x01, 0x28, 0x84, 0x62, 0x20, 0x2a, 0x85, 0x12, 0x18, 0x01,
        0x28, 0x89, 0x32, 0x28, 0x04, 0xda, 0xb1, 0x86, 0x80, 0x18, 0x00, 0x00, 0x79, 0x18, 0x00, 0x00, 0x47, 0x00, 0x00, 0x00, 0x1a, 0x03, 0x4c, 0x90, 0x46, 0x02, 0x13, 0xc4, 0x88, 0x0c, 0x6f, 0xec,
        0xed, 0x4d, 0x0c, 0x44, 0x06, 0x26, 0x26, 0xc7, 0x05, 0xa6, 0xc6, 0x05, 0x06, 0x66, 0x43, 0x10, 0x4c, 0x10, 0x86, 0x61, 0x82, 0x30, 0x10, 0x1b, 0x84, 0x81, 0x98, 0x20, 0x0c, 0xc5, 0x06, 0x61,
        0x30, 0x28, 0xd8, 0xcd, 0x4d, 0x10, 0x06, 0x63, 0xc3, 0x80, 0x24, 0xc4, 0x04, 0x41, 0x00, 0x36, 0x00, 0x1b, 0x06, 0x82, 0x61, 0x36, 0x04, 0xcd, 0x86, 0x61, 0x58, 0x9c, 0x09, 0xc2, 0xd2, 0x6c,
        0x08, 0x20, 0x12, 0x6d, 0x61, 0x69, 0x6e, 0x5c, 0xa6, 0xac, 0xbe, 0xac, 0xca, 0xe4, 0xe8, 0xca, 0xf0, 0x92, 0x88, 0x26, 0x08, 0xc4, 0x31, 0x41, 0x20, 0x90, 0x0d, 0x01, 0x31, 0x41, 0x20, 0x92,
        0x0d, 0x0b, 0x31, 0x51, 0x95, 0x75, 0x0d, 0x15, 0x71, 0x01, 0x1b, 0x02, 0x8c, 0xcb, 0x94, 0xd5, 0x17, 0xd4, 0xdb, 0x5c, 0x1a, 0x5d, 0xda, 0x9b, 0xdb, 0x04, 0x81, 0x50, 0x26, 0x08, 0xc4, 0x32,
        0x41, 0x20, 0x98, 0x0d, 0x0b, 0xa1, 0x6d, 0x9c, 0xd5, 0x0d, 0x1d, 0x71, 0x01, 0x1b, 0x02, 0x6f, 0xc3, 0x90, 0x7d, 0xc0, 0x86, 0x62, 0x91, 0xc0, 0x00, 0x00, 0xaa, 0xb0, 0xb1, 0xd9, 0xb5, 0xb9,
        0xa4, 0x91, 0x95, 0xb9, 0xd1, 0x4d, 0x09, 0x82, 0x2a, 0x64, 0x78, 0x2e, 0x76, 0x65, 0x72, 0x73, 0x69, 0x6f, 0x6e, 0x53, 0x02, 0xa2, 0x09, 0x19, 0x9e, 0x8b, 0x5d, 0x18, 0x9b, 0x5d, 0x99, 0xdc,
        0x94, 0xc0, 0xa8, 0x43, 0x86, 0xe7, 0x32, 0x87, 0x16, 0x46, 0x56, 0x26, 0xd7, 0xf4, 0x46, 0x56, 0xc6, 0x36, 0x25, 0x48, 0x2a, 0x91, 0xe1, 0xb9, 0xd0, 0xe5, 0xc1, 0x95, 0x05, 0xb9, 0xb9, 0xbd,
        0xd1, 0x85, 0xd1, 0xa5, 0xbd, 0xb9, 0xcd, 0x4d, 0x09, 0x9c, 0x3a, 0x64, 0x78, 0x2e, 0x76, 0x69, 0x65, 0x77, 0x49, 0x64, 0x53, 0x74, 0x61, 0x74, 0x65, 0x53, 0x02, 0xa8, 0x0e, 0x19, 0x9e, 0x4b,
        0x99, 0x1b, 0x9d, 0x5c, 0x1e, 0xd4, 0x5b, 0x9a, 0x1b, 0xdd, 0xdc, 0x94, 0x00, 0x0c, 0x00, 0x00, 0x79, 0x18, 0x00, 0x00, 0x42, 0x00, 0x00, 0x00, 0x33, 0x08, 0x80, 0x1c, 0xc4, 0xe1, 0x1c, 0x66,
        0x14, 0x01, 0x3d, 0x88, 0x43, 0x38, 0x84, 0xc3, 0x8c, 0x42, 0x80, 0x07, 0x79, 0x78, 0x07, 0x73, 0x98, 0x71, 0x0c, 0xe6, 0x00, 0x0f, 0xed, 0x10, 0x0e, 0xf4, 0x80, 0x0e, 0x33, 0x0c, 0x42, 0x1e,
        0xc2, 0xc1, 0x1d, 0xce, 0xa1, 0x1c, 0x66, 0x30, 0x05, 0x3d, 0x88, 0x43, 0x38, 0x84, 0x83, 0x1b, 0xcc, 0x03, 0x3d, 0xc8, 0x43, 0x3d, 0x8c, 0x03, 0x3d, 0xcc, 0x78, 0x8c, 0x74, 0x70, 0x07, 0x7b,
        0x08, 0x07, 0x79, 0x48, 0x87, 0x70, 0x70, 0x07, 0x7a, 0x70, 0x03, 0x76, 0x78, 0x87, 0x70, 0x20, 0x87, 0x19, 0xcc, 0x11, 0x0e, 0xec, 0x90, 0x0e, 0xe1, 0x30, 0x0f, 0x6e, 0x30, 0x0f, 0xe3, 0xf0,
        0x0e, 0xf0, 0x50, 0x0e, 0x33, 0x10, 0xc4, 0x1d, 0xde, 0x21, 0x1c, 0xd8, 0x21, 0x1d, 0xc2, 0x61, 0x1e, 0x66, 0x30, 0x89, 0x3b, 0xbc, 0x83, 0x3b, 0xd0, 0x43, 0x39, 0xb4, 0x03, 0x3c, 0xbc, 0x83,
        0x3c, 0x84, 0x03, 0x3b, 0xcc, 0xf0, 0x14, 0x76, 0x60, 0x07, 0x7b, 0x68, 0x07, 0x37, 0x68, 0x87, 0x72, 0x68, 0x07, 0x37, 0x80, 0x87, 0x70, 0x90, 0x87, 0x70, 0x60, 0x07, 0x76, 0x28, 0x07, 0x76,
        0xf8, 0x05, 0x76, 0x78, 0x87, 0x77, 0x80, 0x87, 0x5f, 0x08, 0x87, 0x71, 0x18, 0x87, 0x72, 0x98, 0x87, 0x79, 0x98, 0x81, 0x2c, 0xee, 0xf0, 0x0e, 0xee, 0xe0, 0x0e, 0xf5, 0xc0, 0x0e, 0xec, 0x30,
        0x03, 0x62, 0xc8, 0xa1, 0x1c, 0xe4, 0xa1, 0x1c, 0xcc, 0xa1, 0x1c, 0xe4, 0xa1, 0x1c, 0xdc, 0x61, 0x1c, 0xca, 0x21, 0x1c, 0xc4, 0x81, 0x1d, 0xca, 0x61, 0x06, 0xd6, 0x90, 0x43, 0x39, 0xc8, 0x43,
        0x39, 0x98, 0x43, 0x39, 0xc8, 0x43, 0x39, 0xb8, 0xc3, 0x38, 0x94, 0x43, 0x38, 0x88, 0x03, 0x3b, 0x94, 0xc3, 0x2f, 0xbc, 0x83, 0x3c, 0xfc, 0x82, 0x3b, 0xd4, 0x03, 0x3b, 0xb0, 0x03, 0x00, 0x00,
        0x71, 0x20, 0x00, 0x00, 0x0b, 0x00, 0x00, 0x00, 0x16, 0x30, 0x0d, 0x97, 0xef, 0x3c, 0xfe, 0xe2, 0x00, 0x83, 0xd8, 0x3c, 0xd4, 0xe4, 0x23, 0xb7, 0x6d, 0x02, 0xd5, 0x70, 0xf9, 0xce, 0xe3, 0x4b,
        0x93, 0x13, 0x11, 0x28, 0x35, 0x3d, 0xd4, 0xe4, 0x17, 0xb7, 0x6d, 0x00, 0x04, 0x03, 0x20, 0x0d, 0x00, 0x00, 0x00, 0x00, 0x61, 0x20, 0x00, 0x00, 0x22, 0x00, 0x00, 0x00, 0x13, 0x04, 0x41, 0x2c,
        0x10, 0x00, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00, 0x34, 0xa5, 0x50, 0x04, 0x33, 0x00, 0x44, 0x45, 0x40, 0x37, 0x46, 0x00, 0x82, 0x20, 0x08, 0x82, 0xc1, 0x18, 0x01, 0x08, 0x82, 0x20, 0xfe, 0x8d,
        0x11, 0x80, 0x20, 0x08, 0xe2, 0xbf, 0x30, 0x02, 0x00, 0x00, 0x00, 0x00, 0x23, 0x06, 0x09, 0x00, 0x82, 0x60, 0x50, 0x54, 0x91, 0x24, 0x35, 0x46, 0x05, 0xd4, 0x55, 0x90, 0xe8, 0x05, 0x57, 0x45,
        0x2c, 0x7a, 0xc1, 0x95, 0x0d, 0x8a, 0x7c, 0x4c, 0x58, 0xe4, 0x63, 0x82, 0x02, 0x1f, 0x63, 0x84, 0xf8, 0x8c, 0x18, 0x24, 0x00, 0x08, 0x82, 0x01, 0xe2, 0x49, 0x1c, 0x77, 0x09, 0x23, 0x06, 0x09,
        0x00, 0x82, 0x60, 0x80, 0x78, 0x12, 0xc7, 0x61, 0xc1, 0x88, 0x41, 0x02, 0x80, 0x20, 0x18, 0x20, 0x9e, 0xc4, 0x71, 0xcf, 0x32, 0x62, 0x90, 0x00, 0x20, 0x08, 0x06, 0x88, 0x27, 0x71, 0x5c, 0xd5,
        0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    };
    static const BYTE ps_code[] =
    {
#if 0
        void main(const in float4 position : SV_Position, out float4 target : SV_Target0)
        {
            target = float4(0.0f, 1.0f, 0.0f, 1.0f);
        }
#endif
        0x44, 0x58, 0x42, 0x43, 0xb5, 0xf9, 0xc0, 0x71, 0x38, 0x03, 0x30, 0x75, 0x48, 0x24, 0x77, 0x41, 0xea, 0xac, 0x1a, 0x81, 0x01, 0x00, 0x00, 0x00, 0x92, 0x05, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00,
        0x34, 0x00, 0x00, 0x00, 0x44, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00, 0xba, 0x00, 0x00, 0x00, 0x32, 0x01, 0x00, 0x00, 0x53, 0x46, 0x49, 0x30, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x49, 0x53, 0x47, 0x31, 0x34, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x28, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x01, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x53, 0x56, 0x5f, 0x50, 0x6f, 0x73, 0x69, 0x74, 0x69, 0x6f, 0x6e, 0x00,
        0x4f, 0x53, 0x47, 0x31, 0x32, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x28, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0x00, 0x00, 0x00,
        0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0f, 0xf0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x53, 0x56, 0x5f, 0x54, 0x61, 0x72, 0x67, 0x65, 0x74, 0x00, 0x50, 0x53, 0x56, 0x30, 0x70, 0x00,
        0x00, 0x00, 0x24, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0x00, 0x00,
        0x00, 0x00, 0x01, 0x01, 0x00, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x44, 0x03, 0x03, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x44, 0x10, 0x03, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x44, 0x58, 0x49, 0x4c, 0x58, 0x04, 0x00, 0x00, 0x60, 0x00, 0x00, 0x00, 0x16, 0x01,
        0x00, 0x00, 0x44, 0x58, 0x49, 0x4c, 0x00, 0x01, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x40, 0x04, 0x00, 0x00, 0x42, 0x43, 0xc0, 0xde, 0x21, 0x0c, 0x00, 0x00, 0x0d, 0x01, 0x00, 0x00, 0x0b, 0x82,
        0x20, 0x00, 0x02, 0x00, 0x00, 0x00, 0x13, 0x00, 0x00, 0x00, 0x07, 0x81, 0x23, 0x91, 0x41, 0xc8, 0x04, 0x49, 0x06, 0x10, 0x32, 0x39, 0x92, 0x01, 0x84, 0x0c, 0x25, 0x05, 0x08, 0x19, 0x1e, 0x04,
        0x8b, 0x62, 0x80, 0x10, 0x45, 0x02, 0x42, 0x92, 0x0b, 0x42, 0x84, 0x10, 0x32, 0x14, 0x38, 0x08, 0x18, 0x4b, 0x0a, 0x32, 0x42, 0x88, 0x48, 0x90, 0x14, 0x20, 0x43, 0x46, 0x88, 0xa5, 0x00, 0x19,
        0x32, 0x42, 0xe4, 0x48, 0x0e, 0x90, 0x11, 0x22, 0xc4, 0x50, 0x41, 0x51, 0x81, 0x8c, 0xe1, 0x83, 0xe5, 0x8a, 0x04, 0x21, 0x46, 0x06, 0x51, 0x18, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x1b, 0x88,
        0xe0, 0xff, 0xff, 0xff, 0xff, 0x07, 0x40, 0x02, 0x00, 0x00, 0x49, 0x18, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x13, 0x82, 0x00, 0x00, 0x89, 0x20, 0x00, 0x00, 0x0e, 0x00, 0x00, 0x00, 0x32, 0x22,
        0x08, 0x09, 0x20, 0x64, 0x85, 0x04, 0x13, 0x22, 0xa4, 0x84, 0x04, 0x13, 0x22, 0xe3, 0x84, 0xa1, 0x90, 0x14, 0x12, 0x4c, 0x88, 0x8c, 0x0b, 0x84, 0x84, 0x4c, 0x10, 0x28, 0x23, 0x00, 0x25, 0x00,
        0x8a, 0x39, 0x02, 0x30, 0x98, 0x23, 0x40, 0x66, 0x00, 0x8a, 0x01, 0x33, 0x43, 0x45, 0x36, 0x10, 0x90, 0x06, 0x03, 0x00, 0x00, 0x00, 0x13, 0x14, 0x72, 0xc0, 0x87, 0x74, 0x60, 0x87, 0x36, 0x68,
        0x87, 0x79, 0x68, 0x03, 0x72, 0xc0, 0x87, 0x0d, 0xaf, 0x50, 0x0e, 0x6d, 0xd0, 0x0e, 0x7a, 0x50, 0x0e, 0x6d, 0x00, 0x0f, 0x7a, 0x30, 0x07, 0x72, 0xa0, 0x07, 0x73, 0x20, 0x07, 0x6d, 0x90, 0x0e,
        0x71, 0xa0, 0x07, 0x73, 0x20, 0x07, 0x6d, 0x90, 0x0e, 0x78, 0xa0, 0x07, 0x73, 0x20, 0x07, 0x6d, 0x90, 0x0e, 0x71, 0x60, 0x07, 0x7a, 0x30, 0x07, 0x72, 0xd0, 0x06, 0xe9, 0x30, 0x07, 0x72, 0xa0,
        0x07, 0x73, 0x20, 0x07, 0x6d, 0x90, 0x0e, 0x76, 0x40, 0x07, 0x7a, 0x60, 0x07, 0x74, 0xd0, 0x06, 0xe6, 0x10, 0x07, 0x76, 0xa0, 0x07, 0x73, 0x20, 0x07, 0x6d, 0x60, 0x0e, 0x73, 0x20, 0x07, 0x7a,
        0x30, 0x07, 0x72, 0xd0, 0x06, 0xe6, 0x60, 0x07, 0x74, 0xa0, 0x07, 0x76, 0x40, 0x07, 0x6d, 0xe0, 0x0e, 0x78, 0xa0, 0x07, 0x71, 0x60, 0x07, 0x7a, 0x30, 0x07, 0x72, 0xa0, 0x07, 0x76, 0x40, 0x07,
        0x43, 0x9e, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x86, 0x3c, 0x06, 0x10, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x64, 0x81, 0x00, 0x00, 0x0a, 0x00,
        0x00, 0x00, 0x32, 0x1e, 0x98, 0x10, 0x19, 0x11, 0x4c, 0x90, 0x8c, 0x09, 0x26, 0x47, 0xc6, 0x04, 0x43, 0x9a, 0x12, 0x18, 0x01, 0x28, 0x84, 0x62, 0x20, 0x2a, 0x89, 0x32, 0x28, 0x84, 0x11, 0x80,
        0x02, 0xa1, 0x1c, 0xcb, 0x20, 0x08, 0x00, 0x00, 0x00, 0x00, 0x79, 0x18, 0x00, 0x00, 0x45, 0x00, 0x00, 0x00, 0x1a, 0x03, 0x4c, 0x90, 0x46, 0x02, 0x13, 0xc4, 0x88, 0x0c, 0x6f, 0xec, 0xed, 0x4d,
        0x0c, 0x44, 0x06, 0x26, 0x26, 0xc7, 0x05, 0xa6, 0xc6, 0x05, 0x06, 0x66, 0x43, 0x10, 0x4c, 0x10, 0x06, 0x61, 0x82, 0x30, 0x0c, 0x1b, 0x84, 0x81, 0x98, 0x20, 0x0c, 0xc4, 0x06, 0x61, 0x30, 0x28,
        0xc0, 0xcd, 0x4d, 0x10, 0x86, 0x62, 0xc3, 0x80, 0x24, 0xc4, 0x04, 0x41, 0x00, 0x36, 0x00, 0x1b, 0x06, 0x82, 0x61, 0x36, 0x04, 0xcd, 0x86, 0x61, 0x58, 0x9c, 0x09, 0x42, 0xb2, 0x6c, 0x08, 0x20,
        0x12, 0x6d, 0x61, 0x69, 0x6e, 0x5c, 0xa6, 0xac, 0xbe, 0xa0, 0xde, 0xe6, 0xd2, 0xe8, 0xd2, 0xde, 0xdc, 0x26, 0x08, 0x84, 0x31, 0x41, 0x20, 0x8e, 0x0d, 0x01, 0x31, 0x41, 0x20, 0x90, 0x09, 0x02,
        0x91, 0x6c, 0x58, 0x88, 0x89, 0xaa, 0xac, 0x6b, 0xb8, 0x08, 0x0c, 0xd8, 0x10, 0x64, 0x4c, 0xa6, 0xac, 0xbe, 0xa8, 0xc2, 0xe4, 0xce, 0xca, 0xe8, 0x26, 0x08, 0x84, 0xb2, 0x61, 0x21, 0x36, 0x8a,
        0xb3, 0xb0, 0xe1, 0x22, 0x30, 0x60, 0x43, 0xd0, 0x6d, 0x18, 0x34, 0x0f, 0xd8, 0x50, 0x2c, 0xd2, 0x07, 0x00, 0x55, 0xd8, 0xd8, 0xec, 0xda, 0x5c, 0xd2, 0xc8, 0xca, 0xdc, 0xe8, 0xa6, 0x04, 0x41,
        0x15, 0x32, 0x3c, 0x17, 0xbb, 0x32, 0xb9, 0xb9, 0xb4, 0x37, 0xb7, 0x29, 0x01, 0xd1, 0x84, 0x0c, 0xcf, 0xc5, 0x2e, 0x8c, 0xcd, 0xae, 0x4c, 0x6e, 0x4a, 0x60, 0xd4, 0x21, 0xc3, 0x73, 0x99, 0x43,
        0x0b, 0x23, 0x2b, 0x93, 0x6b, 0x7a, 0x23, 0x2b, 0x63, 0x9b, 0x12, 0x24, 0x95, 0xc8, 0xf0, 0x5c, 0xe8, 0xf2, 0xe0, 0xca, 0x82, 0xdc, 0xdc, 0xde, 0xe8, 0xc2, 0xe8, 0xd2, 0xde, 0xdc, 0xe6, 0xa6,
        0x04, 0x4e, 0x1d, 0x32, 0x3c, 0x17, 0xbb, 0xb4, 0xb2, 0xbb, 0x24, 0xb2, 0x29, 0xba, 0x30, 0xba, 0xb2, 0x29, 0x01, 0x54, 0x87, 0x0c, 0xcf, 0xa5, 0xcc, 0x8d, 0x4e, 0x2e, 0x0f, 0xea, 0x2d, 0xcd,
        0x8d, 0x6e, 0x6e, 0x4a, 0xf0, 0x01, 0x79, 0x18, 0x00, 0x00, 0x42, 0x00, 0x00, 0x00, 0x33, 0x08, 0x80, 0x1c, 0xc4, 0xe1, 0x1c, 0x66, 0x14, 0x01, 0x3d, 0x88, 0x43, 0x38, 0x84, 0xc3, 0x8c, 0x42,
        0x80, 0x07, 0x79, 0x78, 0x07, 0x73, 0x98, 0x71, 0x0c, 0xe6, 0x00, 0x0f, 0xed, 0x10, 0x0e, 0xf4, 0x80, 0x0e, 0x33, 0x0c, 0x42, 0x1e, 0xc2, 0xc1, 0x1d, 0xce, 0xa1, 0x1c, 0x66, 0x30, 0x05, 0x3d,
        0x88, 0x43, 0x38, 0x84, 0x83, 0x1b, 0xcc, 0x03, 0x3d, 0xc8, 0x43, 0x3d, 0x8c, 0x03, 0x3d, 0xcc, 0x78, 0x8c, 0x74, 0x70, 0x07, 0x7b, 0x08, 0x07, 0x79, 0x48, 0x87, 0x70, 0x70, 0x07, 0x7a, 0x70,
        0x03, 0x76, 0x78, 0x87, 0x70, 0x20, 0x87, 0x19, 0xcc, 0x11, 0x0e, 0xec, 0x90, 0x0e, 0xe1, 0x30, 0x0f, 0x6e, 0x30, 0x0f, 0xe3, 0xf0, 0x0e, 0xf0, 0x50, 0x0e, 0x33, 0x10, 0xc4, 0x1d, 0xde, 0x21,
        0x1c, 0xd8, 0x21, 0x1d, 0xc2, 0x61, 0x1e, 0x66, 0x30, 0x89, 0x3b, 0xbc, 0x83, 0x3b, 0xd0, 0x43, 0x39, 0xb4, 0x03, 0x3c, 0xbc, 0x83, 0x3c, 0x84, 0x03, 0x3b, 0xcc, 0xf0, 0x14, 0x76, 0x60, 0x07,
        0x7b, 0x68, 0x07, 0x37, 0x68, 0x87, 0x72, 0x68, 0x07, 0x37, 0x80, 0x87, 0x70, 0x90, 0x87, 0x70, 0x60, 0x07, 0x76, 0x28, 0x07, 0x76, 0xf8, 0x05, 0x76, 0x78, 0x87, 0x77, 0x80, 0x87, 0x5f, 0x08,
        0x87, 0x71, 0x18, 0x87, 0x72, 0x98, 0x87, 0x79, 0x98, 0x81, 0x2c, 0xee, 0xf0, 0x0e, 0xee, 0xe0, 0x0e, 0xf5, 0xc0, 0x0e, 0xec, 0x30, 0x03, 0x62, 0xc8, 0xa1, 0x1c, 0xe4, 0xa1, 0x1c, 0xcc, 0xa1,
        0x1c, 0xe4, 0xa1, 0x1c, 0xdc, 0x61, 0x1c, 0xca, 0x21, 0x1c, 0xc4, 0x81, 0x1d, 0xca, 0x61, 0x06, 0xd6, 0x90, 0x43, 0x39, 0xc8, 0x43, 0x39, 0x98, 0x43, 0x39, 0xc8, 0x43, 0x39, 0xb8, 0xc3, 0x38,
        0x94, 0x43, 0x38, 0x88, 0x03, 0x3b, 0x94, 0xc3, 0x2f, 0xbc, 0x83, 0x3c, 0xfc, 0x82, 0x3b, 0xd4, 0x03, 0x3b, 0xb0, 0x03, 0x00, 0x00, 0x71, 0x20, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0x16, 0x50,
        0x0d, 0x97, 0xef, 0x3c, 0xbe, 0x34, 0x39, 0x11, 0x81, 0x52, 0xd3, 0x43, 0x4d, 0x7e, 0x71, 0xdb, 0x06, 0x40, 0x30, 0x00, 0xd2, 0x00, 0x61, 0x20, 0x00, 0x00, 0x14, 0x00, 0x00, 0x00, 0x13, 0x04,
        0x41, 0x2c, 0x10, 0x00, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00, 0x34, 0xa5, 0x40, 0x54, 0x02, 0x45, 0x40, 0x35, 0x02, 0x30, 0x46, 0x00, 0x82, 0x20, 0x88, 0x7f, 0x00, 0x00, 0x00, 0x00, 0x23, 0x06,
        0x09, 0x00, 0x82, 0x60, 0x60, 0x40, 0x85, 0xe3, 0x20, 0xc2, 0x88, 0x41, 0x02, 0x80, 0x20, 0x18, 0x18, 0x50, 0xe1, 0x38, 0x44, 0x30, 0x62, 0x90, 0x00, 0x20, 0x08, 0x06, 0x06, 0x54, 0x38, 0xce,
        0x20, 0x8c, 0x18, 0x24, 0x00, 0x08, 0x82, 0x81, 0x01, 0x15, 0x8e, 0xa3, 0x04, 0x08, 0x00, 0x00, 0x00, 0x00,
    };

    init_pipeline_state_desc_shaders(desc, root_signature, rt_format, input_layout,
        vs ? vs->pShaderBytecode : vs_code, vs ? vs->BytecodeLength : sizeof(vs_code),
        ps ? ps->pShaderBytecode : ps_code, ps ? ps->BytecodeLength : sizeof(ps_code));
}

#define create_pipeline_state(a, b, c, d, e, f) create_pipeline_state_(__LINE__, a, b, c, d, e, f)
static inline ID3D12PipelineState *create_pipeline_state_(unsigned int line, ID3D12Device *device,
        ID3D12RootSignature *root_signature, DXGI_FORMAT rt_format,
        const D3D12_SHADER_BYTECODE *vs, const D3D12_SHADER_BYTECODE *ps,
        const D3D12_INPUT_LAYOUT_DESC *input_layout)
{
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pipeline_state_desc;
    ID3D12PipelineState *pipeline_state;
    HRESULT hr;

    init_pipeline_state_desc(&pipeline_state_desc, root_signature, rt_format, vs, ps, input_layout);
    hr = ID3D12Device_CreateGraphicsPipelineState(device, &pipeline_state_desc,
            &IID_ID3D12PipelineState, (void **)&pipeline_state);
    ok_(line)(SUCCEEDED(hr), "Failed to create graphics pipeline state, hr %#x.\n", hr);

    return pipeline_state;
}

#define create_pipeline_state_dxil(a, b, c, d, e, f) create_pipeline_state_dxil_(__LINE__, a, b, c, d, e, f)
static inline ID3D12PipelineState *create_pipeline_state_dxil_(unsigned int line, ID3D12Device *device,
                                                        ID3D12RootSignature *root_signature, DXGI_FORMAT rt_format,
                                                        const D3D12_SHADER_BYTECODE *vs, const D3D12_SHADER_BYTECODE *ps,
                                                        const D3D12_INPUT_LAYOUT_DESC *input_layout)
{
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pipeline_state_desc;
    ID3D12PipelineState *pipeline_state;
    HRESULT hr;

    init_pipeline_state_desc_dxil(&pipeline_state_desc, root_signature, rt_format, vs, ps, input_layout);
    hr = ID3D12Device_CreateGraphicsPipelineState(device, &pipeline_state_desc,
                                                  &IID_ID3D12PipelineState, (void **)&pipeline_state);
    ok_(line)(SUCCEEDED(hr), "Failed to create graphics pipeline state, hr %#x.\n", hr);

    return pipeline_state;
}

#define create_pipeline_state_from_stream(device, desc, state) create_pipeline_state_from_stream_(device, desc, sizeof(*desc), state)
static inline HRESULT create_pipeline_state_from_stream_(ID3D12Device2 *device, void *stream, size_t size, ID3D12PipelineState **state)
{
    D3D12_PIPELINE_STATE_STREAM_DESC pipeline_desc;
    pipeline_desc.SizeInBytes = size;
    pipeline_desc.pPipelineStateSubobjectStream = stream;

    return ID3D12Device2_CreatePipelineState(device, &pipeline_desc, &IID_ID3D12PipelineState, (void **)state);
}

struct test_context_desc
{
    unsigned int rt_width, rt_height, rt_array_size;
    DXGI_FORMAT rt_format;
    DXGI_SAMPLE_DESC sample_desc;
    unsigned int rt_descriptor_count;
    unsigned int root_signature_flags;
    bool no_render_target;
    bool no_root_signature;
    bool no_pipeline;
    const D3D12_SHADER_BYTECODE *ps;
};

struct test_context
{
    ID3D12Device *device;

    ID3D12CommandQueue *queue;
    ID3D12CommandAllocator *allocator;
    ID3D12GraphicsCommandList *list;

    D3D12_RESOURCE_DESC render_target_desc;
    ID3D12Resource *render_target;

    ID3D12DescriptorHeap *rtv_heap;
    D3D12_CPU_DESCRIPTOR_HANDLE rtv;

    ID3D12RootSignature *root_signature;
    ID3D12PipelineState *pipeline_state;

    D3D12_VIEWPORT viewport;
    RECT scissor_rect;
};

#define create_render_target(context, a, b, c) create_render_target_(__LINE__, context, a, b, c)
static inline void create_render_target_(unsigned int line, struct test_context *context,
        const struct test_context_desc *desc, ID3D12Resource **render_target,
        const D3D12_CPU_DESCRIPTOR_HANDLE *rtv)
{
    D3D12_HEAP_PROPERTIES heap_properties;
    D3D12_RESOURCE_DESC resource_desc;
    D3D12_CLEAR_VALUE clear_value;
    HRESULT hr;

    memset(&heap_properties, 0, sizeof(heap_properties));
    heap_properties.Type = D3D12_HEAP_TYPE_DEFAULT;

    resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    resource_desc.Alignment = 0;
    resource_desc.Width = desc && desc->rt_width ? desc->rt_width : 32;
    resource_desc.Height = desc && desc->rt_height ? desc->rt_height : 32;
    resource_desc.DepthOrArraySize = desc && desc->rt_array_size ? desc->rt_array_size : 1;
    resource_desc.MipLevels = 1;
    resource_desc.Format = desc && desc->rt_format ? desc->rt_format : DXGI_FORMAT_R8G8B8A8_UNORM;
    resource_desc.SampleDesc.Count = desc && desc->sample_desc.Count ? desc->sample_desc.Count : 1;
    resource_desc.SampleDesc.Quality = desc && desc->sample_desc.Count ? desc->sample_desc.Quality : 0;
    resource_desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    resource_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    clear_value.Format = resource_desc.Format;
    clear_value.Color[0] = 1.0f;
    clear_value.Color[1] = 1.0f;
    clear_value.Color[2] = 1.0f;
    clear_value.Color[3] = 1.0f;
    hr = ID3D12Device_CreateCommittedResource(context->device,
            &heap_properties, D3D12_HEAP_FLAG_NONE, &resource_desc,
            D3D12_RESOURCE_STATE_RENDER_TARGET, &clear_value,
            &IID_ID3D12Resource, (void **)render_target);
    ok_(line)(hr == S_OK, "Failed to create texture, hr %#x.\n", hr);

    context->render_target_desc = resource_desc;

    if (rtv)
        ID3D12Device_CreateRenderTargetView(context->device, *render_target, NULL, *rtv);
}

/* Utility code for capturing native D3D12 tests, which is why this only covers Win32.
 * Launch the d3d12.exe test binary from RenderDoc UI.
 * For Vulkan capturing, use VKD3D_AUTO_CAPTURE_COUNTS and friends instead. */
#ifdef _WIN32
extern RENDERDOC_API_1_0_0 *renderdoc_api;

static inline void begin_renderdoc_capturing(ID3D12Device *device)
{
    pRENDERDOC_GetAPI get_api;
    HANDLE renderdoc;
    FARPROC fn_ptr;

    if (!renderdoc_api)
    {
        renderdoc = GetModuleHandleA("renderdoc.dll");
        if (renderdoc)
        {
            fn_ptr = GetProcAddress(renderdoc, "RENDERDOC_GetAPI");
            if (fn_ptr)
            {
                /* Workaround compiler warnings about casting to function pointer. */
                memcpy(&get_api, &fn_ptr, sizeof(fn_ptr));
                if (!get_api(eRENDERDOC_API_Version_1_0_0, (void **)&renderdoc_api))
                    renderdoc_api = NULL;
            }
        }
    }

    if (renderdoc_api)
        renderdoc_api->StartFrameCapture(device, NULL);
}

static inline void end_renderdoc_capturing(ID3D12Device *device)
{
    if (renderdoc_api)
        renderdoc_api->EndFrameCapture(device, NULL);
}
#endif

#define init_test_context(context, desc) init_test_context_(__LINE__, context, desc)
static inline bool init_test_context_(unsigned int line, struct test_context *context,
        const struct test_context_desc *desc)
{
    D3D12_DESCRIPTOR_HEAP_DESC rtv_heap_desc;
    ID3D12Device *device;
    HRESULT hr;

    memset(context, 0, sizeof(*context));

    if (!(context->device = create_device()))
    {
        skip_(line)("Failed to create device.\n");
        return false;
    }
    device = context->device;

#ifdef _WIN32
    begin_renderdoc_capturing(device);
#endif

    context->queue = create_command_queue_(line, device, D3D12_COMMAND_LIST_TYPE_DIRECT, D3D12_COMMAND_QUEUE_PRIORITY_NORMAL);

    hr = ID3D12Device_CreateCommandAllocator(device, D3D12_COMMAND_LIST_TYPE_DIRECT,
            &IID_ID3D12CommandAllocator, (void **)&context->allocator);
    ok_(line)(SUCCEEDED(hr), "Failed to create command allocator, hr %#x.\n", hr);

    hr = ID3D12Device_CreateCommandList(device, 0, D3D12_COMMAND_LIST_TYPE_DIRECT,
            context->allocator, NULL, &IID_ID3D12GraphicsCommandList, (void **)&context->list);
    ok_(line)(SUCCEEDED(hr), "Failed to create command list, hr %#x.\n", hr);

    if (desc && desc->no_render_target)
        return true;

    rtv_heap_desc.NumDescriptors = desc && desc->rt_descriptor_count ? desc->rt_descriptor_count : 1;
    rtv_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtv_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    rtv_heap_desc.NodeMask = 0;
    hr = ID3D12Device_CreateDescriptorHeap(device, &rtv_heap_desc,
            &IID_ID3D12DescriptorHeap, (void **)&context->rtv_heap);
    ok_(line)(SUCCEEDED(hr), "Failed to create descriptor heap, hr %#x.\n", hr);

    context->rtv = ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(context->rtv_heap);

    create_render_target_(line, context, desc, &context->render_target, &context->rtv);

    set_viewport(&context->viewport, 0.0f, 0.0f,
            context->render_target_desc.Width, context->render_target_desc.Height, 0.0f, 1.0f);
    set_rect(&context->scissor_rect, 0, 0,
            context->render_target_desc.Width, context->render_target_desc.Height);

    if (desc && desc->no_root_signature)
        return true;

    context->root_signature = create_empty_root_signature_(line,
            device, desc ? desc->root_signature_flags : D3D12_ROOT_SIGNATURE_FLAG_NONE);

    if (desc && desc->no_pipeline)
        return true;

    context->pipeline_state = create_pipeline_state_(line, device,
            context->root_signature, context->render_target_desc.Format,
            NULL, desc ? desc->ps : NULL, NULL);

    return true;
}

#define destroy_test_context(context) destroy_test_context_(__LINE__, context)
static inline void destroy_test_context_(unsigned int line, struct test_context *context)
{
    ULONG refcount;

#ifdef _WIN32
    end_renderdoc_capturing(context->device);
#endif

    if (context->pipeline_state)
        ID3D12PipelineState_Release(context->pipeline_state);
    if (context->root_signature)
        ID3D12RootSignature_Release(context->root_signature);

    if (context->rtv_heap)
        ID3D12DescriptorHeap_Release(context->rtv_heap);
    if (context->render_target)
        ID3D12Resource_Release(context->render_target);

    ID3D12CommandAllocator_Release(context->allocator);
    ID3D12CommandQueue_Release(context->queue);
    ID3D12GraphicsCommandList_Release(context->list);

    refcount = ID3D12Device_Release(context->device);
    ok_(line)(!refcount, "ID3D12Device has %u references left.\n", (unsigned int)refcount);
}

static inline D3D12_CPU_DESCRIPTOR_HANDLE get_cpu_handle(ID3D12Device *device,
        ID3D12DescriptorHeap *heap, D3D12_DESCRIPTOR_HEAP_TYPE heap_type, unsigned int offset)
{
    D3D12_CPU_DESCRIPTOR_HANDLE handle;
    unsigned int descriptor_size;

    handle = ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(heap);
    if (!offset)
        return handle;

    descriptor_size = ID3D12Device_GetDescriptorHandleIncrementSize(device, heap_type);
    handle.ptr += offset * descriptor_size;
    return handle;
}

static inline D3D12_GPU_DESCRIPTOR_HANDLE get_gpu_handle(ID3D12Device *device,
        ID3D12DescriptorHeap *heap, D3D12_DESCRIPTOR_HEAP_TYPE heap_type, unsigned int offset)
{
    D3D12_GPU_DESCRIPTOR_HANDLE handle;
    unsigned int descriptor_size;

    handle = ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(heap);
    if (!offset)
        return handle;

    descriptor_size = ID3D12Device_GetDescriptorHandleIncrementSize(device, heap_type);
    handle.ptr += offset * descriptor_size;
    return handle;
}

static inline D3D12_CPU_DESCRIPTOR_HANDLE get_cpu_descriptor_handle(struct test_context *context,
        ID3D12DescriptorHeap *heap, unsigned int offset)
{
    return get_cpu_handle(context->device, heap, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, offset);
}

static inline D3D12_CPU_DESCRIPTOR_HANDLE get_cpu_sampler_handle(struct test_context *context,
        ID3D12DescriptorHeap *heap, unsigned int offset)
{
    return get_cpu_handle(context->device, heap, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, offset);
}

static inline D3D12_CPU_DESCRIPTOR_HANDLE get_cpu_rtv_handle(struct test_context *context,
        ID3D12DescriptorHeap *heap, unsigned int offset)
{
    return get_cpu_handle(context->device, heap, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, offset);
}

static inline D3D12_CPU_DESCRIPTOR_HANDLE get_cpu_dsv_handle(struct test_context *context,
        ID3D12DescriptorHeap *heap, unsigned int offset)
{
    return get_cpu_handle(context->device, heap, D3D12_DESCRIPTOR_HEAP_TYPE_DSV, offset);
}

static inline D3D12_GPU_DESCRIPTOR_HANDLE get_gpu_descriptor_handle(struct test_context *context,
        ID3D12DescriptorHeap *heap, unsigned int offset)
{
    return get_gpu_handle(context->device, heap, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, offset);
}

static inline D3D12_GPU_DESCRIPTOR_HANDLE get_gpu_sampler_handle(struct test_context *context,
        ID3D12DescriptorHeap *heap, unsigned int offset)
{
    return get_gpu_handle(context->device, heap, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, offset);
}

struct vec2
{
    float x, y;
};

struct vec4
{
    float x, y, z, w;
};

struct uvec4
{
    unsigned int x, y, z, w;
};

struct ivec4
{
    int x, y, z, w;
};

struct dvec2
{
    double x, y;
};

bool compare_float(float f, float g, int ulps);
bool compare_vec4(const struct vec4 *v1, const struct vec4 *v2, unsigned int ulps);
bool compare_uvec4(const struct uvec4* v1, const struct uvec4 *v2);
bool compare_uint8(uint8_t a, uint8_t b, unsigned int max_diff);
bool compare_uint16(uint16_t a, uint16_t b, unsigned int max_diff);
bool compare_uint64(uint64_t a, uint64_t b, unsigned int max_diff);
ULONG get_refcount(void *iface);

void check_interface_(unsigned int line, IUnknown *iface, REFIID riid, bool supported);
void check_heap_properties_(unsigned int line,
        const D3D12_HEAP_PROPERTIES *properties, const D3D12_HEAP_PROPERTIES *expected_properties);
void check_heap_desc_(unsigned int line, const D3D12_HEAP_DESC *desc,
        const D3D12_HEAP_DESC *expected_desc);
void check_alignment_(unsigned int line, uint64_t size, uint64_t alignment);
void uav_barrier(ID3D12GraphicsCommandList *list, ID3D12Resource *resource);
void copy_sub_resource_data(const D3D12_MEMCPY_DEST *dst, const D3D12_SUBRESOURCE_DATA *src,
        unsigned int row_count, unsigned int slice_count, size_t row_size);
void upload_buffer_data_(unsigned int line, ID3D12Resource *buffer, size_t offset,
        size_t size, const void *data, ID3D12CommandQueue *queue, ID3D12GraphicsCommandList *command_list);
void upload_texture_data_(unsigned int line, ID3D12Resource *texture,
        const D3D12_SUBRESOURCE_DATA *data, unsigned int sub_resource_count,
        ID3D12CommandQueue *queue, ID3D12GraphicsCommandList *command_list);
void upload_texture_data_base_(unsigned int line, ID3D12Resource *texture,
        const D3D12_SUBRESOURCE_DATA *data,
        unsigned int first_subresource, unsigned int sub_resource_count,
        ID3D12CommandQueue *queue, ID3D12GraphicsCommandList *command_list);
void init_readback(struct resource_readback *rb, ID3D12Resource *buffer,
        uint64_t buffer_size, uint64_t width, uint64_t height, unsigned int depth, uint64_t row_pitch);

void get_buffer_readback_with_command_list(ID3D12Resource *buffer, DXGI_FORMAT format,
        struct resource_readback *rb, ID3D12CommandQueue *queue, ID3D12GraphicsCommandList *command_list);
uint8_t get_readback_uint8(struct resource_readback *rb, unsigned int x, unsigned int y);
uint16_t get_readback_uint16(struct resource_readback *rb, unsigned int x, unsigned int y);
uint64_t get_readback_uint64(struct resource_readback *rb, unsigned int x, unsigned int y);
float get_readback_float(struct resource_readback *rb, unsigned int x, unsigned int y);
const struct vec4 *get_readback_vec4(struct resource_readback *rb, unsigned int x, unsigned int y);
const struct uvec4 *get_readback_uvec4(struct resource_readback *rb, unsigned int x, unsigned int y);

void check_readback_data_float_(unsigned int line, struct resource_readback *rb,
        const RECT *rect, float expected, unsigned int max_diff);
void check_sub_resource_float_(unsigned int line, ID3D12Resource *texture,
        unsigned int sub_resource_idx, ID3D12CommandQueue *queue, ID3D12GraphicsCommandList *command_list,
        float expected, unsigned int max_diff);
void check_readback_data_uint8_(unsigned int line, struct resource_readback *rb,
        const RECT *rect, uint8_t expected, unsigned int max_diff);

void check_sub_resource_uint8_(unsigned int line, ID3D12Resource *texture,
        unsigned int sub_resource_idx, ID3D12CommandQueue *queue, ID3D12GraphicsCommandList *command_list,
        uint8_t expected, unsigned int max_diff);
void check_readback_data_uint16_(unsigned int line, struct resource_readback *rb,
        const RECT *rect, uint16_t expected, unsigned int max_diff);
void check_sub_resource_uint16_(unsigned int line, ID3D12Resource *texture,
        unsigned int sub_resource_idx, ID3D12CommandQueue *queue, ID3D12GraphicsCommandList *command_list,
        uint16_t expected, unsigned int max_diff);
void check_readback_data_uint64_(unsigned int line, struct resource_readback *rb,
        const RECT *rect, uint64_t expected, unsigned int max_diff);
void check_sub_resource_uint64_(unsigned int line, ID3D12Resource *texture,
        unsigned int sub_resource_idx, ID3D12CommandQueue *queue, ID3D12GraphicsCommandList *command_list,
        uint64_t expected, unsigned int max_diff);
void check_sub_resource_vec4_(unsigned int line, ID3D12Resource *texture,
        unsigned int sub_resource_idx, ID3D12CommandQueue *queue, ID3D12GraphicsCommandList *command_list,
        const struct vec4 *expected, unsigned int max_diff);
void check_sub_resource_uvec4_(unsigned int line, ID3D12Resource *texture,
        unsigned int sub_resource_idx, ID3D12CommandQueue *queue, ID3D12GraphicsCommandList *command_list,
        const struct uvec4 *expected_value);

bool broken_on_warp(bool condition);
bool is_min_max_filtering_supported(ID3D12Device *device);
D3D12_TILED_RESOURCES_TIER get_tiled_resources_tier(ID3D12Device *device);
bool is_standard_swizzle_64kb_supported(ID3D12Device *device);
bool is_memory_pool_L1_supported(ID3D12Device *device);
bool is_vrs_tier1_supported(ID3D12Device *device, bool *additional_shading_rates);
bool is_vrs_tier2_supported(ID3D12Device *device);

ID3D12RootSignature *create_cb_root_signature_(unsigned int line,
        ID3D12Device *device, unsigned int reg_idx, D3D12_SHADER_VISIBILITY shader_visibility,
        D3D12_ROOT_SIGNATURE_FLAGS flags);
ID3D12RootSignature *create_32bit_constants_root_signature_(unsigned int line,
        ID3D12Device *device, unsigned int reg_idx, unsigned int element_count,
        D3D12_SHADER_VISIBILITY shader_visibility, D3D12_ROOT_SIGNATURE_FLAGS flags);
ID3D12RootSignature *create_texture_root_signature_(unsigned int line,
        ID3D12Device *device, D3D12_SHADER_VISIBILITY shader_visibility,
        unsigned int constant_count, D3D12_ROOT_SIGNATURE_FLAGS flags,
        const D3D12_STATIC_SAMPLER_DESC *sampler_desc);

ID3D12CommandSignature *create_command_signature_(unsigned int line,
        ID3D12Device *device, D3D12_INDIRECT_ARGUMENT_TYPE argument_type);

bool context_supports_dxil_(unsigned int line, struct test_context *context);

bool init_compute_test_context_(unsigned int line, struct test_context *context);
ID3D12PipelineState *create_compute_pipeline_state_(unsigned int line, ID3D12Device *device,
        ID3D12RootSignature *root_signature, const D3D12_SHADER_BYTECODE cs);

struct depth_stencil_resource
{
    ID3D12Resource *texture;
    ID3D12DescriptorHeap *heap;
    D3D12_CPU_DESCRIPTOR_HANDLE dsv_handle;
};

void init_depth_stencil_(unsigned int line, struct depth_stencil_resource *ds,
        ID3D12Device *device, unsigned int width, unsigned int height, unsigned int array_size, unsigned int level_count,
        DXGI_FORMAT format, DXGI_FORMAT view_format, const D3D12_CLEAR_VALUE *clear_value);
void destroy_depth_stencil_(unsigned int line, struct depth_stencil_resource *ds);

#define check_interface(a, b, c) check_interface_(__LINE__, (IUnknown *)a, b, c)
#define check_heap_properties(a, b) check_heap_properties_(__LINE__, a, b)
#define check_heap_desc(a, b) check_heap_desc_(__LINE__, a, b)
#define check_alignment(a, b) check_alignment_(__LINE__, a, b)
#define upload_buffer_data(a, b, c, d, e, f) upload_buffer_data_(__LINE__, a, b, c, d, e, f)
#define upload_texture_data(a, b, c, d, e) upload_texture_data_(__LINE__, a, b, c, d, e)
#define upload_texture_data_base(a, b, c, d, e, f) upload_texture_data_base_(__LINE__, a, b, c, d, e, f)
#define check_readback_data_float(a, b, c, d) check_readback_data_float_(__LINE__, a, b, c, d)
#define check_sub_resource_float(a, b, c, d, e, f) check_sub_resource_float_(__LINE__, a, b, c, d, e, f)
#define check_readback_data_uint8(a, b, c, d) check_readback_data_uint8_(__LINE__, a, b, c, d)
#define check_sub_resource_uint8(a, b, c, d, e, f) check_sub_resource_uint8_(__LINE__, a, b, c, d, e, f)
#define check_readback_data_uint16(a, b, c, d) check_readback_data_uint16_(__LINE__, a, b, c, d)
#define check_sub_resource_uint16(a, b, c, d, e, f) check_sub_resource_uint16_(__LINE__, a, b, c, d, e, f)
#define check_readback_data_uint64(a, b, c, d) check_readback_data_uint64_(__LINE__, a, b, c, d)
#define check_sub_resource_uint64(a, b, c, d, e, f) check_sub_resource_uint64_(__LINE__, a, b, c, d, e, f)
#define check_sub_resource_vec4(a, b, c, d, e, f) check_sub_resource_vec4_(__LINE__, a, b, c, d, e, f)
#define check_sub_resource_uvec4(a, b, c, d, e) check_sub_resource_uvec4_(__LINE__, a, b, c, d, e)
#define create_cb_root_signature(a, b, c, e) create_cb_root_signature_(__LINE__, a, b, c, e)
#define create_32bit_constants_root_signature(a, b, c, e) \
create_32bit_constants_root_signature_(__LINE__, a, b, c, e, 0)
#define create_texture_root_signature(a, b, c, d) create_texture_root_signature_(__LINE__, a, b, c, d, NULL)
#define create_compute_pipeline_state(a, b, c) create_compute_pipeline_state_(__LINE__, a, b, c)
#define create_command_signature(a, b) create_command_signature_(__LINE__, a, b)
#define init_compute_test_context(context) init_compute_test_context_(__LINE__, context)
#define context_supports_dxil(context) context_supports_dxil_(__LINE__, context)
#define init_depth_stencil(a, b, c, d, e, f, g, h, i) init_depth_stencil_(__LINE__, a, b, c, d, e, f, g, h, i)
#define destroy_depth_stencil(depth_stencil) destroy_depth_stencil_(__LINE__, depth_stencil)

union d3d12_root_signature_subobject
{
    struct
    {
        D3D12_PIPELINE_STATE_SUBOBJECT_TYPE type;
        ID3D12RootSignature *root_signature;
    };
    void *dummy_align;
};

union d3d12_shader_bytecode_subobject
{
    struct
    {
        D3D12_PIPELINE_STATE_SUBOBJECT_TYPE type;
        D3D12_SHADER_BYTECODE shader_bytecode;
    };
    void *dummy_align;
};

union d3d12_stream_output_subobject
{
    struct
    {
        D3D12_PIPELINE_STATE_SUBOBJECT_TYPE type;
        D3D12_STREAM_OUTPUT_DESC stream_output_desc;
    };
    void *dummy_align;
};

union d3d12_blend_subobject
{
    struct
    {
        D3D12_PIPELINE_STATE_SUBOBJECT_TYPE type;
        D3D12_BLEND_DESC blend_desc;
    };
    void *dummy_align;
};

union d3d12_sample_mask_subobject
{
    struct
    {
        D3D12_PIPELINE_STATE_SUBOBJECT_TYPE type;
        UINT sample_mask;
    };
    void *dummy_align;
};

union d3d12_rasterizer_subobject
{
    struct
    {
        D3D12_PIPELINE_STATE_SUBOBJECT_TYPE type;
        D3D12_RASTERIZER_DESC rasterizer_desc;
    };
    void *dummy_align;
};

union d3d12_rasterizer1_subobject
{
    struct
    {
        D3D12_PIPELINE_STATE_SUBOBJECT_TYPE type;
        D3D12_RASTERIZER_DESC1 rasterizer_desc;
    };
    void *dummy_align;
};

union d3d12_rasterizer2_subobject
{
    struct
    {
        D3D12_PIPELINE_STATE_SUBOBJECT_TYPE type;
        D3D12_RASTERIZER_DESC2 rasterizer_desc;
    };
    void *dummy_align;
};

union d3d12_depth_stencil_subobject
{
    struct
    {
        D3D12_PIPELINE_STATE_SUBOBJECT_TYPE type;
        D3D12_DEPTH_STENCIL_DESC depth_stencil_desc;
    };
    void *dummy_align;
};

union d3d12_input_layout_subobject
{
    struct
    {
        D3D12_PIPELINE_STATE_SUBOBJECT_TYPE type;
        D3D12_INPUT_LAYOUT_DESC input_layout;
    };
    void *dummy_align;
};

union d3d12_ib_strip_cut_value_subobject
{
    struct
    {
        D3D12_PIPELINE_STATE_SUBOBJECT_TYPE type;
        D3D12_INDEX_BUFFER_STRIP_CUT_VALUE strip_cut_value;
    };
    void *dummy_align;
};

union d3d12_primitive_topology_subobject
{
    struct
    {
        D3D12_PIPELINE_STATE_SUBOBJECT_TYPE type;
        D3D12_PRIMITIVE_TOPOLOGY_TYPE primitive_topology_type;
    };
    void *dummy_align;
};

union d3d12_render_target_formats_subobject
{
    struct
    {
        D3D12_PIPELINE_STATE_SUBOBJECT_TYPE type;
        D3D12_RT_FORMAT_ARRAY render_target_formats;
    };
    void *dummy_align;
};

union d3d12_depth_stencil_format_subobject
{
    struct
    {
        D3D12_PIPELINE_STATE_SUBOBJECT_TYPE type;
        DXGI_FORMAT depth_stencil_format;
    };
    void *dummy_align;
};

union d3d12_sample_desc_subobject
{
    struct
    {
        D3D12_PIPELINE_STATE_SUBOBJECT_TYPE type;
        DXGI_SAMPLE_DESC sample_desc;
    };
    void *dummy_align;
};

union d3d12_node_mask_subobject
{
    struct
    {
        D3D12_PIPELINE_STATE_SUBOBJECT_TYPE type;
        UINT node_mask;
    };
    void *dummy_align;
};

union d3d12_cached_pso_subobject
{
    struct
    {
        D3D12_PIPELINE_STATE_SUBOBJECT_TYPE type;
        D3D12_CACHED_PIPELINE_STATE cached_pso;
    };
    void *dummy_align;
};

union d3d12_flags_subobject
{
    struct
    {
        D3D12_PIPELINE_STATE_SUBOBJECT_TYPE type;
        D3D12_PIPELINE_STATE_FLAGS flags;
    };
    void *dummy_align;
};

union d3d12_depth_stencil1_subobject
{
    struct
    {
        D3D12_PIPELINE_STATE_SUBOBJECT_TYPE type;
        D3D12_DEPTH_STENCIL_DESC1 depth_stencil_desc;
    };
    void *dummy_align;
};

union d3d12_depth_stencil2_subobject
{
    struct
    {
        D3D12_PIPELINE_STATE_SUBOBJECT_TYPE type;
        D3D12_DEPTH_STENCIL_DESC2 depth_stencil_desc;
    };
    void *dummy_align;
};

union d3d12_view_instancing_subobject
{
    struct
    {
        D3D12_PIPELINE_STATE_SUBOBJECT_TYPE type;
        D3D12_VIEW_INSTANCING_DESC view_instancing_desc;
    };
    void *dummy_align;
};

#endif  /* __VKD3D_D3D12_TEST_UTILS_H */
