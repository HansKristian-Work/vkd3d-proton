/*
 * Copyright 2016 Józef Kucia for CodeWeavers
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */


/* Hack for MinGW-w64 headers.
 *
 * We want to use WIDL C inline wrappers because some methods
 * in D3D12 interfaces return aggregate objects. Unfortunately,
 * WIDL C inline wrappers are broken when used with MinGW-w64
 * headers because FORCEINLINE expands to extern inline
 * which leads to the "multiple storage classes in declaration
 * specifiers" compiler error.
 */
#ifdef __MINGW32__
# include <_mingw.h>
# ifdef __MINGW64_VERSION_MAJOR
#  undef __forceinline
#  define __forceinline __inline__ __attribute__((__always_inline__,__gnu_inline__))
# endif

# define _HRESULT_DEFINED
typedef int HRESULT;
#endif

#define COBJMACROS
#define INITGUID
#include "vkd3d_test.h"
#include "vkd3d_windows.h"
#define WIDL_C_INLINE_WRAPPERS
#include "d3d12.h"

#define ARRAY_SIZE(x) (sizeof(x) / sizeof(*x))

static size_t align(size_t addr, unsigned int alignment)
{
    return (addr + (alignment - 1)) & ~(alignment - 1);
}

#define get_refcount(a) get_refcount_((IUnknown *)a)
static ULONG get_refcount_(IUnknown *iface)
{
    IUnknown_AddRef(iface);
    return IUnknown_Release(iface);
}

#define check_interface(a, b, c) check_interface_(__LINE__, (IUnknown *)a, b, c)
static void check_interface_(unsigned int line, IUnknown *iface, REFIID riid, BOOL supported)
{
    HRESULT hr, expected_hr;
    IUnknown *unk;

    expected_hr = supported ? S_OK : E_NOINTERFACE;

    hr = IUnknown_QueryInterface(iface, riid, (void **)&unk);
    ok_(line)(hr == expected_hr, "Got hr %#x, expected %#x.\n", hr, expected_hr);
    if (SUCCEEDED(hr))
        IUnknown_Release(unk);
}

#define create_root_signature(a, b, c) create_root_signature_(__LINE__, a, b, c)
#if _WIN32
static HRESULT create_root_signature_(unsigned int line, ID3D12Device *device,
        const D3D12_ROOT_SIGNATURE_DESC *desc, ID3D12RootSignature **root_signature)
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
#else
/* XXX: Root signature byte code is not supported yet. We allow to pass D3D12_ROOT_SIGNATURE_DESC
 * directly to CreateRootSignature(). */
static HRESULT create_root_signature_(unsigned int line, ID3D12Device *device,
        const D3D12_ROOT_SIGNATURE_DESC *desc, ID3D12RootSignature **root_signature)
{
    return ID3D12Device_CreateRootSignature(device, 0, desc, ~0u,
            &IID_ID3D12RootSignature, (void **)root_signature);
}
#endif

static D3D12_SHADER_BYTECODE shader_bytecode(const DWORD *code, size_t size)
{
    D3D12_SHADER_BYTECODE shader_bytecode = { code, size };
    return shader_bytecode;
}

#if _WIN32
# define SHADER_BYTECODE(dxbc, spirv) ((void)spirv, shader_bytecode(dxbc, sizeof(dxbc)))
#else
# define SHADER_BYTECODE(dxbc, spirv) ((void)dxbc, shader_bytecode(spirv, sizeof(spirv)))
#endif

static void exec_command_list(ID3D12CommandQueue *queue, ID3D12GraphicsCommandList *list)
{
    ID3D12CommandList *lists[] = {(ID3D12CommandList *)list};
    ID3D12CommandQueue_ExecuteCommandLists(queue, 1, lists);
}

#ifdef _WIN32
static HANDLE create_event(void)
{
    return CreateEventA(NULL, FALSE, FALSE, NULL);
}

static void signal_event(HANDLE event)
{
    SetEvent(event);
}

static unsigned int wait_event(HANDLE event, unsigned int milliseconds)
{
    return WaitForSingleObject(event, milliseconds);
}

static void destroy_event(HANDLE event)
{
    CloseHandle(event);
}
#else
static HANDLE create_event(void)
{
    return VKD3DCreateEvent();
}

static void signal_event(HANDLE event)
{
    VKD3DSignalEvent(event);
}

static unsigned int wait_event(HANDLE event, unsigned int milliseconds)
{
    return VKD3DWaitEvent(event, milliseconds);
}

static void destroy_event(HANDLE event)
{
    VKD3DDestroyEvent(event);
}
#endif

#define wait_queue_idle(a, b) wait_queue_idle_(__LINE__, a, b)
#if _WIN32
static void wait_queue_idle_(unsigned int line, ID3D12Device *device, ID3D12CommandQueue *queue)
{
    const UINT64 value = 1;
    ID3D12Fence *fence;
    unsigned int ret;
    HANDLE event;
    HRESULT hr;

    hr = ID3D12Device_CreateFence(device, 0, D3D12_FENCE_FLAG_NONE,
            &IID_ID3D12Fence, (void **)&fence);
    ok_(line)(SUCCEEDED(hr), "CreateFence failed, hr %#x.\n", hr);

    hr = ID3D12CommandQueue_Signal(queue, fence, value);
    ok_(line)(SUCCEEDED(hr), "Failed to signal fence, hr %#x.\n", hr);

    if (ID3D12Fence_GetCompletedValue(fence) < value)
    {
        event = create_event();
        ok_(line)(!!event, "CreateEvent failed.\n");

        hr = ID3D12Fence_SetEventOnCompletion(fence, value, event);
        ok_(line)(SUCCEEDED(hr), "SetEventOnCompletion failed, hr %#x.\n", hr);
        ret = wait_event(event, INFINITE);
        ok_(line)(ret == WAIT_OBJECT_0, "WaitForSingleObject failed, ret %#x.\n", ret);

        destroy_event(event);
    }

    ID3D12Fence_Release(fence);
}
#else
static void wait_queue_idle_(unsigned int line, ID3D12Device *device, ID3D12CommandQueue *queue)
{
    todo_(line)(0, "wait_queue_idle() not implemented.\n");
}
#endif

static unsigned int format_size(DXGI_FORMAT format)
{
    switch (format)
    {
        case DXGI_FORMAT_R8G8B8A8_UNORM:
        case DXGI_FORMAT_B8G8R8A8_UNORM:
            return 4;
        default:
            trace("Unhandled format %#x.\n", format);
            return 1;
    }
}

struct resource_readback
{
    unsigned int width;
    unsigned int height;
    ID3D12Resource *resource;
    unsigned int row_pitch;
    void *data;
};

static void get_texture_readback_with_command_list(ID3D12Resource *texture, unsigned int sub_resource,
        struct resource_readback *rb, ID3D12CommandQueue *queue, ID3D12GraphicsCommandList *command_list)
{
    D3D12_TEXTURE_COPY_LOCATION dst_location, src_location;
    D3D12_HEAP_PROPERTIES heap_properties;
    D3D12_RESOURCE_DESC resource_desc;
    ID3D12Resource *resource;
    unsigned int miplevel;
    ID3D12Device *device;
    DXGI_FORMAT format;
    HRESULT hr;

    hr = ID3D12Resource_GetDevice(texture, &IID_ID3D12Device, (void **)&device);
    ok(SUCCEEDED(hr), "Failed to get device, hr %#x.\n", hr);

    resource_desc = ID3D12Resource_GetDesc(texture);
    ok(resource_desc.Dimension != D3D12_RESOURCE_DIMENSION_BUFFER,
            "Resource %p is not texture.\n", texture);
    ok(resource_desc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE3D,
            "Readback not implemented for 3D textures.\n");

    miplevel = sub_resource % resource_desc.MipLevels;
    rb->width = max(1, resource_desc.Width >> miplevel);
    rb->height = max(1, resource_desc.Height >> miplevel);
    rb->row_pitch = align(rb->width * format_size(resource_desc.Format), D3D12_TEXTURE_DATA_PITCH_ALIGNMENT);
    rb->data = NULL;

    format = resource_desc.Format;

    resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    resource_desc.Alignment = 0;
    resource_desc.Width = rb->row_pitch * rb->height;
    resource_desc.Height = 1;
    resource_desc.DepthOrArraySize = 1;
    resource_desc.MipLevels = 1;
    resource_desc.Format = DXGI_FORMAT_UNKNOWN;
    resource_desc.SampleDesc.Count = 1;
    resource_desc.SampleDesc.Quality = 0;
    resource_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    resource_desc.Flags = D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE;

    memset(&heap_properties, 0, sizeof(heap_properties));
    heap_properties.Type = D3D12_HEAP_TYPE_READBACK;
    hr = ID3D12Device_CreateCommittedResource(device,
            &heap_properties, D3D12_HEAP_FLAG_NONE, &resource_desc,
            D3D12_RESOURCE_STATE_COPY_DEST, NULL,
            &IID_ID3D12Resource, (void **)&resource);
    ok(SUCCEEDED(hr), "CreateCommittedResource failed, hr %#x.\n", hr);
    rb->resource = resource;

    dst_location.pResource = resource;
    dst_location.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dst_location.PlacedFootprint.Offset = 0;
    dst_location.PlacedFootprint.Footprint.Format = format;
    dst_location.PlacedFootprint.Footprint.Width = rb->width;
    dst_location.PlacedFootprint.Footprint.Height = rb->height;
    dst_location.PlacedFootprint.Footprint.Depth = 1;
    dst_location.PlacedFootprint.Footprint.RowPitch = rb->row_pitch;

    src_location.pResource = texture;
    src_location.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    src_location.SubresourceIndex = sub_resource;

    ID3D12GraphicsCommandList_CopyTextureRegion(command_list, &dst_location, 0, 0, 0, &src_location, NULL);
    hr = ID3D12GraphicsCommandList_Close(command_list);
    ok(SUCCEEDED(hr), "Close failed, hr %#x.\n", hr);

    exec_command_list(queue, command_list);
    wait_queue_idle(device, queue);

    hr = ID3D12Resource_Map(resource, 0, NULL, &rb->data);
    ok(SUCCEEDED(hr), "Map failed, hr %#x.\n", hr);

    ID3D12Device_Release(device);
}

static void *get_readback_data(struct resource_readback *rb, unsigned int x, unsigned int y,
        size_t element_size)
{
    return &((BYTE *)rb->data)[rb->row_pitch * y + x * element_size];
}

static unsigned int get_readback_uint(struct resource_readback *rb, unsigned int x, unsigned int y)
{
    return *(unsigned int *)get_readback_data(rb, x, y, sizeof(unsigned int));
}

static void release_resource_readback(struct resource_readback *rb)
{
    D3D12_RANGE range = {0, 0};
    ID3D12Resource_Unmap(rb->resource, 0, &range);
    ID3D12Resource_Release(rb->resource);
}

static ID3D12Device *create_device(void)
{
    ID3D12Device *device;
    HRESULT hr;

    if (FAILED(hr = D3D12CreateDevice(NULL, D3D_FEATURE_LEVEL_11_0, &IID_ID3D12Device, (void **)&device)))
        return NULL;

    return device;
}

static void test_create_device(void)
{
    ID3D12Device *device;
    ULONG refcount;
    HRESULT hr;

    if (!(device = create_device()))
    {
        skip("Failed to create device.\n");
        return;
    }

    check_interface(device, &IID_ID3D12Object, TRUE);
    check_interface(device, &IID_ID3D12DeviceChild, FALSE);
    check_interface(device, &IID_ID3D12Pageable, FALSE);
    check_interface(device, &IID_ID3D12Device, TRUE);

    refcount = ID3D12Device_Release(device);
    ok(!refcount, "ID3D12Device has %u references left.\n", (unsigned int)refcount);

    hr = D3D12CreateDevice(NULL, D3D_FEATURE_LEVEL_11_0, &IID_ID3D12Device, (void **)&device);
    ok(hr == S_OK, "D3D12CreateDevice failed, hr %#x.\n", hr);
    ID3D12Device_Release(device);

    hr = D3D12CreateDevice(NULL, D3D_FEATURE_LEVEL_9_1, &IID_ID3D12Device, (void **)&device);
    ok(hr == E_INVALIDARG, "Got unexpected hr %#x.\n", hr);
    hr = D3D12CreateDevice(NULL, D3D_FEATURE_LEVEL_9_2, &IID_ID3D12Device, (void **)&device);
    ok(hr == E_INVALIDARG, "Got unexpected hr %#x.\n", hr);
    hr = D3D12CreateDevice(NULL, D3D_FEATURE_LEVEL_9_3, &IID_ID3D12Device, (void **)&device);
    ok(hr == E_INVALIDARG, "Got unexpected hr %#x.\n", hr);
    hr = D3D12CreateDevice(NULL, D3D_FEATURE_LEVEL_10_0, &IID_ID3D12Device, (void **)&device);
    ok(hr == E_INVALIDARG, "Got unexpected hr %#x.\n", hr);
    hr = D3D12CreateDevice(NULL, D3D_FEATURE_LEVEL_10_1, &IID_ID3D12Device, (void **)&device);
    ok(hr == E_INVALIDARG, "Got unexpected hr %#x.\n", hr);

    hr = D3D12CreateDevice(NULL, 0, &IID_ID3D12Device, (void **)&device);
    ok(hr == E_INVALIDARG, "D3D12CreateDevice failed, hr %#x.\n", hr);
    hr = D3D12CreateDevice(NULL, ~0u, &IID_ID3D12Device, (void **)&device);
    ok(hr == E_INVALIDARG, "D3D12CreateDevice failed, hr %#x.\n", hr);
}

static void test_node_count(void)
{
    ID3D12Device *device;
    UINT node_count;
    ULONG refcount;

    if (!(device = create_device()))
    {
        skip("Failed to create device.\n");
        return;
    }

    node_count = ID3D12Device_GetNodeCount(device);
    trace("Node count: %u.\n", node_count);
    ok(1 <= node_count && node_count <= 32, "Got unexpected node count %u.\n", node_count);

    refcount = ID3D12Device_Release(device);
    ok(!refcount, "ID3D12Device has %u references left.\n", (unsigned int)refcount);
}

static void test_check_feature_support(void)
{
    D3D12_FEATURE_DATA_FEATURE_LEVELS feature_levels;
    D3D_FEATURE_LEVEL max_supported_feature_level;
    ID3D12Device *device;
    ULONG refcount;
    HRESULT hr;

    static const D3D_FEATURE_LEVEL all_feature_levels[] =
    {
        D3D_FEATURE_LEVEL_12_1,
        D3D_FEATURE_LEVEL_12_0,
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0,
        D3D_FEATURE_LEVEL_9_3,
        D3D_FEATURE_LEVEL_9_2,
        D3D_FEATURE_LEVEL_9_1,
    };
    static const D3D_FEATURE_LEVEL d3d12_feature_levels[] =
    {
        D3D_FEATURE_LEVEL_12_1,
        D3D_FEATURE_LEVEL_12_0,
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
    };
    static const D3D_FEATURE_LEVEL d3d_9_x_feature_levels[] =
    {
        D3D_FEATURE_LEVEL_9_3,
        D3D_FEATURE_LEVEL_9_2,
        D3D_FEATURE_LEVEL_9_1,
    };
    static const D3D_FEATURE_LEVEL invalid_feature_levels[] =
    {
        0x0000,
        0x3000,
    };

    if (!(device = create_device()))
    {
        skip("Failed to create device.\n");
        return;
    }

    /* Feature levels */
    memset(&feature_levels, 0, sizeof(feature_levels));
    hr = ID3D12Device_CheckFeatureSupport(device, D3D12_FEATURE_FEATURE_LEVELS,
            &feature_levels, sizeof(feature_levels));
    ok(hr == E_INVALIDARG, "Got unexpected hr %#x.\n", hr);

    feature_levels.NumFeatureLevels = ARRAY_SIZE(all_feature_levels);
    feature_levels.pFeatureLevelsRequested = all_feature_levels;
    feature_levels.MaxSupportedFeatureLevel = 0;
    hr = ID3D12Device_CheckFeatureSupport(device, D3D12_FEATURE_FEATURE_LEVELS,
            &feature_levels, sizeof(feature_levels));
    ok(SUCCEEDED(hr), "CheckFeatureSupport failed, hr %#x.\n", hr);
    trace("Max supported feature level %#x.\n", feature_levels.MaxSupportedFeatureLevel);
    max_supported_feature_level = feature_levels.MaxSupportedFeatureLevel;

    feature_levels.NumFeatureLevels = ARRAY_SIZE(d3d12_feature_levels);
    feature_levels.pFeatureLevelsRequested = d3d12_feature_levels;
    feature_levels.MaxSupportedFeatureLevel = 0;
    hr = ID3D12Device_CheckFeatureSupport(device, D3D12_FEATURE_FEATURE_LEVELS,
            &feature_levels, sizeof(feature_levels));
    ok(SUCCEEDED(hr), "CheckFeatureSupport failed, hr %#x.\n", hr);
    ok(feature_levels.MaxSupportedFeatureLevel == max_supported_feature_level,
            "Got unexpected feature level %#x, expected %#x.\n",
            feature_levels.MaxSupportedFeatureLevel, max_supported_feature_level);

    /* Check invalid size. */
    hr = ID3D12Device_CheckFeatureSupport(device, D3D12_FEATURE_FEATURE_LEVELS,
            &feature_levels, sizeof(feature_levels) + 1);
    ok(hr == E_INVALIDARG, "Got unexpected hr %#x.\n", hr);
    hr = ID3D12Device_CheckFeatureSupport(device, D3D12_FEATURE_FEATURE_LEVELS,
            &feature_levels, sizeof(feature_levels) - 1);
    ok(hr == E_INVALIDARG, "Got unexpected hr %#x.\n", hr);

    feature_levels.NumFeatureLevels = ARRAY_SIZE(d3d_9_x_feature_levels);
    feature_levels.pFeatureLevelsRequested = d3d_9_x_feature_levels;
    feature_levels.MaxSupportedFeatureLevel = 0;
    hr = ID3D12Device_CheckFeatureSupport(device, D3D12_FEATURE_FEATURE_LEVELS,
            &feature_levels, sizeof(feature_levels));
    ok(SUCCEEDED(hr), "CheckFeatureSupport failed, hr %#x.\n", hr);
    ok(feature_levels.MaxSupportedFeatureLevel == D3D_FEATURE_LEVEL_9_3,
            "Got unexpected max feature level %#x.\n", feature_levels.MaxSupportedFeatureLevel);

    feature_levels.NumFeatureLevels = ARRAY_SIZE(invalid_feature_levels);
    feature_levels.pFeatureLevelsRequested = invalid_feature_levels;
    feature_levels.MaxSupportedFeatureLevel = 0;
    hr = ID3D12Device_CheckFeatureSupport(device, D3D12_FEATURE_FEATURE_LEVELS,
            &feature_levels, sizeof(feature_levels));
    ok(SUCCEEDED(hr), "CheckFeatureSupport failed, hr %#x.\n", hr);
    ok(feature_levels.MaxSupportedFeatureLevel == 0x3000,
            "Got unexpected max feature level %#x.\n", feature_levels.MaxSupportedFeatureLevel);

    refcount = ID3D12Device_Release(device);
    ok(!refcount, "ID3D12Device has %u references left.\n", (unsigned int)refcount);
}

static void test_create_command_allocator(void)
{
    ID3D12CommandAllocator *command_allocator;
    ID3D12Device *device, *tmp_device;
    ULONG refcount;
    HRESULT hr;

    if (!(device = create_device()))
    {
        skip("Failed to create device.\n");
        return;
    }

    hr = ID3D12Device_CreateCommandAllocator(device, D3D12_COMMAND_LIST_TYPE_DIRECT,
            &IID_ID3D12CommandAllocator, (void **)&command_allocator);
    ok(SUCCEEDED(hr), "CreateCommandAllocator failed, hr %#x.\n", hr);

    refcount = get_refcount(device);
    ok(refcount == 2, "Got unexpected refcount %u.\n", (unsigned int)refcount);
    hr = ID3D12CommandAllocator_GetDevice(command_allocator, &IID_ID3D12Device, (void **)&tmp_device);
    ok(SUCCEEDED(hr), "GetDevice failed, hr %#x.\n", hr);
    refcount = get_refcount(device);
    ok(refcount == 3, "Got unexpected refcount %u.\n", (unsigned int)refcount);
    refcount = ID3D12Device_Release(tmp_device);
    ok(refcount == 2, "Got unexpected refcount %u.\n", (unsigned int)refcount);

    check_interface(command_allocator, &IID_ID3D12Object, TRUE);
    check_interface(command_allocator, &IID_ID3D12DeviceChild, TRUE);
    check_interface(command_allocator, &IID_ID3D12Pageable, TRUE);
    check_interface(command_allocator, &IID_ID3D12CommandAllocator, TRUE);

    refcount = ID3D12CommandAllocator_Release(command_allocator);
    ok(!refcount, "ID3D12CommandAllocator has %u references left.\n", (unsigned int)refcount);

    hr = ID3D12Device_CreateCommandAllocator(device, D3D12_COMMAND_LIST_TYPE_BUNDLE,
            &IID_ID3D12CommandAllocator, (void **)&command_allocator);
    ok(SUCCEEDED(hr), "CreateCommandAllocator failed, hr %#x.\n", hr);
    refcount = ID3D12CommandAllocator_Release(command_allocator);
    ok(!refcount, "ID3D12CommandAllocator has %u references left.\n", (unsigned int)refcount);

    hr = ID3D12Device_CreateCommandAllocator(device, D3D12_COMMAND_LIST_TYPE_COMPUTE,
            &IID_ID3D12CommandAllocator, (void **)&command_allocator);
    ok(SUCCEEDED(hr), "CreateCommandAllocator failed, hr %#x.\n", hr);
    refcount = ID3D12CommandAllocator_Release(command_allocator);
    ok(!refcount, "ID3D12CommandAllocator has %u references left.\n", (unsigned int)refcount);

    hr = ID3D12Device_CreateCommandAllocator(device, D3D12_COMMAND_LIST_TYPE_COPY,
            &IID_ID3D12CommandAllocator, (void **)&command_allocator);
    ok(SUCCEEDED(hr), "CreateCommandAllocator failed, hr %#x.\n", hr);
    refcount = ID3D12CommandAllocator_Release(command_allocator);
    ok(!refcount, "ID3D12CommandAllocator has %u references left.\n", (unsigned int)refcount);

    hr = ID3D12Device_CreateCommandAllocator(device, ~0u,
            &IID_ID3D12CommandAllocator, (void **)&command_allocator);
    ok(hr == E_INVALIDARG, "CreateCommandAllocator failed, hr %#x.\n", hr);

    refcount = ID3D12Device_Release(device);
    ok(!refcount, "ID3D12Device has %u references left.\n", (unsigned int)refcount);
}

static void test_create_command_list(void)
{
    ID3D12CommandAllocator *command_allocator;
    ID3D12Device *device, *tmp_device;
    ID3D12CommandList *command_list;
    ULONG refcount;
    HRESULT hr;

    if (!(device = create_device()))
    {
        skip("Failed to create device.\n");
        return;
    }

    hr = ID3D12Device_CreateCommandList(device, 0, D3D12_COMMAND_LIST_TYPE_DIRECT,
            NULL, NULL, &IID_ID3D12CommandList, (void **)&command_list);
    ok(hr == E_INVALIDARG, "Got unexpected hr %#x.\n", hr);

    hr = ID3D12Device_CreateCommandAllocator(device, D3D12_COMMAND_LIST_TYPE_DIRECT,
            &IID_ID3D12CommandAllocator, (void **)&command_allocator);
    ok(SUCCEEDED(hr), "CreateCommandAllocator failed, hr %#x.\n", hr);

    refcount = get_refcount(device);
    ok(refcount == 2, "Got unexpected refcount %u.\n", (unsigned int)refcount);

    hr = ID3D12Device_CreateCommandList(device, 0, D3D12_COMMAND_LIST_TYPE_DIRECT,
            command_allocator, NULL, &IID_ID3D12CommandList, (void **)&command_list);
    ok(SUCCEEDED(hr), "CreateCommandList failed, hr %#x.\n", hr);

    refcount = get_refcount(command_allocator);
    ok(refcount == 1, "Got unexpected refcount %u.\n", (unsigned int)refcount);

    refcount = get_refcount(device);
    ok(refcount == 3, "Got unexpected refcount %u.\n", (unsigned int)refcount);
    hr = ID3D12CommandList_GetDevice(command_list, &IID_ID3D12Device, (void **)&tmp_device);
    ok(SUCCEEDED(hr), "GetDevice failed, hr %#x.\n", hr);
    refcount = get_refcount(device);
    ok(refcount == 4, "Got unexpected refcount %u.\n", (unsigned int)refcount);
    refcount = ID3D12Device_Release(tmp_device);
    ok(refcount == 3, "Got unexpected refcount %u.\n", (unsigned int)refcount);

    check_interface(command_list, &IID_ID3D12Object, TRUE);
    check_interface(command_list, &IID_ID3D12DeviceChild, TRUE);
    check_interface(command_list, &IID_ID3D12Pageable, FALSE);
    check_interface(command_list, &IID_ID3D12CommandList, TRUE);
    check_interface(command_list, &IID_ID3D12GraphicsCommandList, TRUE);
    check_interface(command_list, &IID_ID3D12CommandAllocator, FALSE);

    refcount = ID3D12CommandList_Release(command_list);
    ok(!refcount, "ID3D12CommandList has %u references left.\n", (unsigned int)refcount);
    refcount = ID3D12CommandAllocator_Release(command_allocator);
    ok(!refcount, "ID3D12CommandAllocator has %u references left.\n", (unsigned int)refcount);

    hr = ID3D12Device_CreateCommandAllocator(device, D3D12_COMMAND_LIST_TYPE_BUNDLE,
            &IID_ID3D12CommandAllocator, (void **)&command_allocator);
    ok(SUCCEEDED(hr), "CreateCommandAllocator failed, hr %#x.\n", hr);
    hr = ID3D12Device_CreateCommandList(device, 0, D3D12_COMMAND_LIST_TYPE_DIRECT,
            command_allocator, NULL, &IID_ID3D12CommandList, (void **)&command_list);
    ok(hr == E_INVALIDARG, "Got unexpected hr %#x.\n", hr);
    hr = ID3D12Device_CreateCommandList(device, 0, D3D12_COMMAND_LIST_TYPE_BUNDLE,
            command_allocator, NULL, &IID_ID3D12CommandList, (void **)&command_list);
    ok(SUCCEEDED(hr), "CreateCommandList failed, hr %#x.\n", hr);
    check_interface(command_list, &IID_ID3D12GraphicsCommandList, TRUE);
    refcount = ID3D12CommandList_Release(command_list);
    ok(!refcount, "ID3D12CommandList has %u references left.\n", (unsigned int)refcount);
    refcount = ID3D12CommandAllocator_Release(command_allocator);
    ok(!refcount, "ID3D12CommandAllocator has %u references left.\n", (unsigned int)refcount);

    hr = ID3D12Device_CreateCommandAllocator(device, D3D12_COMMAND_LIST_TYPE_COMPUTE,
            &IID_ID3D12CommandAllocator, (void **)&command_allocator);
    ok(SUCCEEDED(hr), "CreateCommandAllocator failed, hr %#x.\n", hr);
    hr = ID3D12Device_CreateCommandList(device, 0, D3D12_COMMAND_LIST_TYPE_BUNDLE,
            command_allocator, NULL, &IID_ID3D12CommandList, (void **)&command_list);
    ok(hr == E_INVALIDARG, "Got unexpected hr %#x.\n", hr);
    hr = ID3D12Device_CreateCommandList(device, 0, D3D12_COMMAND_LIST_TYPE_COMPUTE,
            command_allocator, NULL, &IID_ID3D12CommandList, (void **)&command_list);
    ok(SUCCEEDED(hr), "CreateCommandList failed, hr %#x.\n", hr);
    check_interface(command_list, &IID_ID3D12GraphicsCommandList, TRUE);
    refcount = ID3D12CommandList_Release(command_list);
    ok(!refcount, "ID3D12CommandList has %u references left.\n", (unsigned int)refcount);
    refcount = ID3D12CommandAllocator_Release(command_allocator);
    ok(!refcount, "ID3D12CommandAllocator has %u references left.\n", (unsigned int)refcount);

    hr = ID3D12Device_CreateCommandAllocator(device, D3D12_COMMAND_LIST_TYPE_COPY,
            &IID_ID3D12CommandAllocator, (void **)&command_allocator);
    ok(SUCCEEDED(hr), "CreateCommandAllocator failed, hr %#x.\n", hr);
    hr = ID3D12Device_CreateCommandList(device, 0, D3D12_COMMAND_LIST_TYPE_DIRECT,
            command_allocator, NULL, &IID_ID3D12CommandList, (void **)&command_list);
    ok(hr == E_INVALIDARG, "Got unexpected hr %#x.\n", hr);
    hr = ID3D12Device_CreateCommandList(device, 0, D3D12_COMMAND_LIST_TYPE_COMPUTE,
            command_allocator, NULL, &IID_ID3D12CommandList, (void **)&command_list);
    ok(hr == E_INVALIDARG, "Got unexpected hr %#x.\n", hr);
    hr = ID3D12Device_CreateCommandList(device, 0, D3D12_COMMAND_LIST_TYPE_COPY,
            command_allocator, NULL, &IID_ID3D12CommandList, (void **)&command_list);
    ok(SUCCEEDED(hr), "CreateCommandList failed, hr %#x.\n", hr);
    check_interface(command_list, &IID_ID3D12GraphicsCommandList, TRUE);
    refcount = ID3D12CommandList_Release(command_list);
    ok(!refcount, "ID3D12CommandList has %u references left.\n", (unsigned int)refcount);
    refcount = ID3D12CommandAllocator_Release(command_allocator);
    ok(!refcount, "ID3D12CommandAllocator has %u references left.\n", (unsigned int)refcount);

    refcount = ID3D12Device_Release(device);
    ok(!refcount, "ID3D12Device has %u references left.\n", (unsigned int)refcount);
}

static void test_create_command_queue(void)
{
    D3D12_COMMAND_QUEUE_DESC desc, result_desc;
    ID3D12Device *device, *tmp_device;
    ID3D12CommandQueue *queue;
    ULONG refcount;
    HRESULT hr;

    if (!(device = create_device()))
    {
        skip("Failed to create device.\n");
        return;
    }

    desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    desc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
    desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    desc.NodeMask = 0;
    hr = ID3D12Device_CreateCommandQueue(device, &desc, &IID_ID3D12CommandQueue, (void **)&queue);
    ok(SUCCEEDED(hr), "CreateCommandQueue failed, hr %#x.\n", hr);

    refcount = get_refcount(device);
    ok(refcount == 2, "Got unexpected refcount %u.\n", (unsigned int)refcount);
    hr = ID3D12CommandQueue_GetDevice(queue, &IID_ID3D12Device, (void **)&tmp_device);
    ok(SUCCEEDED(hr), "GetDevice failed, hr %#x.\n", hr);
    refcount = get_refcount(device);
    ok(refcount == 3, "Got unexpected refcount %u.\n", (unsigned int)refcount);
    refcount = ID3D12Device_Release(tmp_device);
    ok(refcount == 2, "Got unexpected refcount %u.\n", (unsigned int)refcount);

    check_interface(queue, &IID_ID3D12Object, TRUE);
    check_interface(queue, &IID_ID3D12DeviceChild, TRUE);
    check_interface(queue, &IID_ID3D12Pageable, TRUE);
    check_interface(queue, &IID_ID3D12CommandQueue, TRUE);

    result_desc = ID3D12CommandQueue_GetDesc(queue);
    ok(result_desc.Type == desc.Type, "Got unexpected type %#x.\n", result_desc.Type);
    ok(result_desc.Priority == desc.Priority, "Got unexpected priority %#x.\n", result_desc.Priority);
    ok(result_desc.Flags == desc.Flags, "Got unexpected flags %#x.\n", result_desc.Flags);
    ok(result_desc.NodeMask == 0x1, "Got unexpected node mask 0x%08x.\n", result_desc.NodeMask);

    refcount = ID3D12CommandQueue_Release(queue);
    ok(!refcount, "ID3D12CommandQueue has %u references left.\n", (unsigned int)refcount);

    refcount = ID3D12Device_Release(device);
    ok(!refcount, "ID3D12Device has %u references left.\n", (unsigned int)refcount);
}

static void test_create_committed_resource(void)
{
    D3D12_HEAP_PROPERTIES heap_properties;
    D3D12_RESOURCE_DESC resource_desc;
    ID3D12Device *device, *tmp_device;
    D3D12_CLEAR_VALUE clear_value;
    ID3D12Resource *resource;
    ULONG refcount;
    HRESULT hr;

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
    ok(SUCCEEDED(hr), "CreateCommittedResource failed, hr %#x.\n", hr);

    refcount = get_refcount(device);
    ok(refcount == 2, "Got unexpected refcount %u.\n", (unsigned int)refcount);
    hr = ID3D12Resource_GetDevice(resource, &IID_ID3D12Device, (void **)&tmp_device);
    ok(SUCCEEDED(hr), "GetDevice failed, hr %#x.\n", hr);
    refcount = get_refcount(device);
    ok(refcount == 3, "Got unexpected refcount %u.\n", (unsigned int)refcount);
    refcount = ID3D12Device_Release(tmp_device);
    ok(refcount == 2, "Got unexpected refcount %u.\n", (unsigned int)refcount);

    check_interface(resource, &IID_ID3D12Object, TRUE);
    check_interface(resource, &IID_ID3D12DeviceChild, TRUE);
    check_interface(resource, &IID_ID3D12Pageable, TRUE);
    check_interface(resource, &IID_ID3D12Resource, TRUE);

    refcount = ID3D12Resource_Release(resource);
    ok(!refcount, "ID3D12Resource has %u references left.\n", (unsigned int)refcount);

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
    ok(SUCCEEDED(hr), "CreateCommittedResource failed, hr %#x.\n", hr);

    check_interface(resource, &IID_ID3D12Object, TRUE);
    check_interface(resource, &IID_ID3D12DeviceChild, TRUE);
    check_interface(resource, &IID_ID3D12Pageable, TRUE);
    check_interface(resource, &IID_ID3D12Resource, TRUE);

    refcount = ID3D12Resource_Release(resource);
    ok(!refcount, "ID3D12Resource has %u references left.\n", (unsigned int)refcount);

    refcount = ID3D12Device_Release(device);
    ok(!refcount, "ID3D12Device has %u references left.\n", (unsigned int)refcount);
}

static void test_create_descriptor_heap(void)
{
    D3D12_DESCRIPTOR_HEAP_DESC heap_desc;
    ID3D12Device *device, *tmp_device;
    ID3D12DescriptorHeap *heap;
    ULONG refcount;
    HRESULT hr;

    if (!(device = create_device()))
    {
        skip("Failed to create device.\n");
        return;
    }

    heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heap_desc.NumDescriptors = 16;
    heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    heap_desc.NodeMask = 0;
    hr = ID3D12Device_CreateDescriptorHeap(device, &heap_desc, &IID_ID3D12DescriptorHeap, (void **)&heap);
    ok(SUCCEEDED(hr), "CreateDescriptorHeap failed, hr %#x.\n", hr);

    refcount = get_refcount(device);
    ok(refcount == 2, "Got unexpected refcount %u.\n", (unsigned int)refcount);
    hr = ID3D12DescriptorHeap_GetDevice(heap, &IID_ID3D12Device, (void **)&tmp_device);
    ok(SUCCEEDED(hr), "GetDevice failed, hr %#x.\n", hr);
    refcount = get_refcount(device);
    ok(refcount == 3, "Got unexpected refcount %u.\n", (unsigned int)refcount);
    refcount = ID3D12Device_Release(tmp_device);
    ok(refcount == 2, "Got unexpected refcount %u.\n", (unsigned int)refcount);

    check_interface(heap, &IID_ID3D12Object, TRUE);
    check_interface(heap, &IID_ID3D12DeviceChild, TRUE);
    check_interface(heap, &IID_ID3D12Pageable, TRUE);
    check_interface(heap, &IID_ID3D12DescriptorHeap, TRUE);

    refcount = ID3D12DescriptorHeap_Release(heap);
    ok(!refcount, "ID3D12DescriptorHeap has %u references left.\n", (unsigned int)refcount);

    heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    hr = ID3D12Device_CreateDescriptorHeap(device, &heap_desc, &IID_ID3D12DescriptorHeap, (void **)&heap);
    ok(SUCCEEDED(hr), "CreateDescriptorHeap failed, hr %#x.\n", hr);
    refcount = ID3D12DescriptorHeap_Release(heap);
    ok(!refcount, "ID3D12DescriptorHeap has %u references left.\n", (unsigned int)refcount);

    heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
    heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    hr = ID3D12Device_CreateDescriptorHeap(device, &heap_desc, &IID_ID3D12DescriptorHeap, (void **)&heap);
    ok(SUCCEEDED(hr), "CreateDescriptorHeap failed, hr %#x.\n", hr);
    refcount = ID3D12DescriptorHeap_Release(heap);
    ok(!refcount, "ID3D12DescriptorHeap has %u references left.\n", (unsigned int)refcount);

    heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    hr = ID3D12Device_CreateDescriptorHeap(device, &heap_desc, &IID_ID3D12DescriptorHeap, (void **)&heap);
    ok(SUCCEEDED(hr), "CreateDescriptorHeap failed, hr %#x.\n", hr);
    refcount = ID3D12DescriptorHeap_Release(heap);
    ok(!refcount, "ID3D12DescriptorHeap has %u references left.\n", (unsigned int)refcount);

    heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    hr = ID3D12Device_CreateDescriptorHeap(device, &heap_desc, &IID_ID3D12DescriptorHeap, (void **)&heap);
    ok(SUCCEEDED(hr), "CreateDescriptorHeap failed, hr %#x.\n", hr);
    refcount = ID3D12DescriptorHeap_Release(heap);
    ok(!refcount, "ID3D12DescriptorHeap has %u references left.\n", (unsigned int)refcount);

    refcount = ID3D12Device_Release(device);
    ok(!refcount, "ID3D12Device has %u references left.\n", (unsigned int)refcount);
}

static void test_create_root_signature(void)
{
    D3D12_ROOT_SIGNATURE_DESC root_signature_desc;
    D3D12_DESCRIPTOR_RANGE descriptor_ranges[1];
    D3D12_ROOT_PARAMETER root_parameters[1];
    ID3D12RootSignature *root_signature;
    ID3D12Device *device, *tmp_device;
    ULONG refcount;
    HRESULT hr;

    if (!(device = create_device()))
    {
        skip("Failed to create device.\n");
        return;
    }

    descriptor_ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    descriptor_ranges[0].NumDescriptors = 1;
    descriptor_ranges[0].BaseShaderRegister = 0;
    descriptor_ranges[0].RegisterSpace = 0;
    descriptor_ranges[0].OffsetInDescriptorsFromTableStart = 0;
    root_parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    root_parameters[0].DescriptorTable.NumDescriptorRanges = 1;
    root_parameters[0].DescriptorTable.pDescriptorRanges = descriptor_ranges;
    root_parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    root_signature_desc.NumParameters = 1;
    root_signature_desc.pParameters = root_parameters;
    root_signature_desc.NumStaticSamplers = 0;
    root_signature_desc.pStaticSamplers = NULL;
    root_signature_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
    hr = create_root_signature(device, &root_signature_desc, &root_signature);
    ok(SUCCEEDED(hr), "Failed to create root signature, hr %#x.\n", hr);

    refcount = get_refcount(device);
    ok(refcount == 2, "Got unexpected refcount %u.\n", (unsigned int)refcount);
    hr = ID3D12RootSignature_GetDevice(root_signature, &IID_ID3D12Device, (void **)&tmp_device);
    ok(SUCCEEDED(hr), "GetDevice failed, hr %#x.\n", hr);
    refcount = get_refcount(device);
    ok(refcount == 3, "Got unexpected refcount %u.\n", (unsigned int)refcount);
    refcount = ID3D12Device_Release(tmp_device);
    ok(refcount == 2, "Got unexpected refcount %u.\n", (unsigned int)refcount);

    check_interface(root_signature, &IID_ID3D12Object, TRUE);
    check_interface(root_signature, &IID_ID3D12DeviceChild, TRUE);
    check_interface(root_signature, &IID_ID3D12Pageable, FALSE);
    check_interface(root_signature, &IID_ID3D12RootSignature, TRUE);

    refcount = ID3D12RootSignature_Release(root_signature);
    ok(!refcount, "ID3D12RootSignature has %u references left.\n", (unsigned int)refcount);

    root_signature_desc.NumParameters = 0;
    root_signature_desc.pParameters = NULL;
    root_signature_desc.NumStaticSamplers = 0;
    root_signature_desc.pStaticSamplers = NULL;
    root_signature_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
    hr = create_root_signature(device, &root_signature_desc, &root_signature);
    ok(SUCCEEDED(hr), "Failed to create root signature, hr %#x.\n", hr);
    refcount = ID3D12RootSignature_Release(root_signature);
    ok(!refcount, "ID3D12RootSignature has %u references left.\n", (unsigned int)refcount);

    refcount = ID3D12Device_Release(device);
    ok(!refcount, "ID3D12Device has %u references left.\n", (unsigned int)refcount);
}

static void test_create_pipeline_state(void)
{
    D3D12_COMPUTE_PIPELINE_STATE_DESC pipeline_state_desc;
    D3D12_ROOT_SIGNATURE_DESC root_signature_desc;
    ID3D12RootSignature *root_signature;
    ID3D12PipelineState *pipeline_state;
    ID3D12Device *device, *tmp_device;
    ULONG refcount;
    HRESULT hr;

    static const DWORD dxbc_code[] =
    {
#if 0
        [numthreads(1, 1, 1)]
        void main() { }
#endif
        0x43425844, 0x1acc3ad0, 0x71c7b057, 0xc72c4306, 0xf432cb57, 0x00000001, 0x00000074, 0x00000003,
        0x0000002c, 0x0000003c, 0x0000004c, 0x4e475349, 0x00000008, 0x00000000, 0x00000008, 0x4e47534f,
        0x00000008, 0x00000000, 0x00000008, 0x58454853, 0x00000020, 0x00050050, 0x00000008, 0x0100086a,
        0x0400009b, 0x00000001, 0x00000001, 0x00000001, 0x0100003e,
    };
    static const DWORD spv_code[] =
    {
#if 0
        #version 450 core
        void main() { }
#endif
        0x07230203, 0x00010000, 0x00080001, 0x00003ee8, 0x00000000, 0x00020011, 0x00000001, 0x0006000b,
        0x00000001, 0x4c534c47, 0x6474732e, 0x3035342e, 0x00000000, 0x0003000e, 0x00000000, 0x00000001,
        0x0005000f, 0x00000005, 0x0000161f, 0x6e69616d, 0x00000000, 0x00060010, 0x0000161f, 0x00000011,
        0x00000001, 0x00000001, 0x00000001, 0x00020013, 0x00000008, 0x00030021, 0x00000502, 0x00000008,
        0x00050036, 0x00000008, 0x0000161f, 0x00000000, 0x00000502, 0x000200f8, 0x00003ee7, 0x000100fd,
        0x00010038,
    };

    if (!(device = create_device()))
    {
        skip("Failed to create device.\n");
        return;
    }

    root_signature_desc.NumParameters = 0;
    root_signature_desc.pParameters = NULL;
    root_signature_desc.NumStaticSamplers = 0;
    root_signature_desc.pStaticSamplers = NULL;
    root_signature_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
    hr = create_root_signature(device, &root_signature_desc, &root_signature);
    ok(SUCCEEDED(hr), "Failed to create root signature, hr %#x.\n", hr);

    refcount = get_refcount(device);
    ok(refcount == 2, "Got unexpected refcount %u.\n", (unsigned int)refcount);

    memset(&pipeline_state_desc, 0, sizeof(pipeline_state_desc));
    pipeline_state_desc.pRootSignature = root_signature;
    pipeline_state_desc.CS = SHADER_BYTECODE(dxbc_code, spv_code);
    pipeline_state_desc.NodeMask = 0;
    pipeline_state_desc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;

    hr = ID3D12Device_CreateComputePipelineState(device, &pipeline_state_desc,
            &IID_ID3D12PipelineState, (void **)&pipeline_state);
    ok(SUCCEEDED(hr), "CreateComputePipelineState failed, hr %#x.\n", hr);

    refcount = get_refcount(root_signature);
    ok(refcount == 1, "Got unexpected refcount %u.\n", (unsigned int)refcount);

    refcount = get_refcount(device);
    ok(refcount == 3, "Got unexpected refcount %u.\n", (unsigned int)refcount);
    hr = ID3D12PipelineState_GetDevice(pipeline_state, &IID_ID3D12Device, (void **)&tmp_device);
    ok(SUCCEEDED(hr), "GetDevice failed, hr %#x.\n", hr);
    refcount = get_refcount(device);
    ok(refcount == 4, "Got unexpected refcount %u.\n", (unsigned int)refcount);
    refcount = ID3D12Device_Release(tmp_device);
    ok(refcount == 3, "Got unexpected refcount %u.\n", (unsigned int)refcount);

    check_interface(pipeline_state, &IID_ID3D12Object, TRUE);
    check_interface(pipeline_state, &IID_ID3D12DeviceChild, TRUE);
    check_interface(pipeline_state, &IID_ID3D12Pageable, TRUE);
    check_interface(pipeline_state, &IID_ID3D12PipelineState, TRUE);

    refcount = ID3D12PipelineState_Release(pipeline_state);
    ok(!refcount, "ID3D12PipelineState has %u references left.\n", (unsigned int)refcount);
    refcount = ID3D12RootSignature_Release(root_signature);
    ok(!refcount, "ID3D12RootSignature has %u references left.\n", (unsigned int)refcount);

    refcount = ID3D12Device_Release(device);
    ok(!refcount, "ID3D12Device has %u references left.\n", (unsigned int)refcount);
}

static void test_create_fence(void)
{
    ID3D12Device *device, *tmp_device;
    ID3D12Fence *fence;
    ULONG refcount;
    UINT64 value;
    HRESULT hr;

    if (!(device = create_device()))
    {
        skip("Failed to create device.\n");
        return;
    }

    hr = ID3D12Device_CreateFence(device, 0, D3D12_FENCE_FLAG_NONE,
            &IID_ID3D12Fence, (void **)&fence);
    ok(SUCCEEDED(hr), "CreateFence failed, hr %#x.\n", hr);

    refcount = get_refcount(device);
    ok(refcount == 2, "Got unexpected refcount %u.\n", (unsigned int)refcount);
    hr = ID3D12Fence_GetDevice(fence, &IID_ID3D12Device, (void **)&tmp_device);
    ok(SUCCEEDED(hr), "GetDevice failed, hr %#x.\n", hr);
    refcount = get_refcount(device);
    ok(refcount == 3, "Got unexpected refcount %u.\n", (unsigned int)refcount);
    refcount = ID3D12Device_Release(tmp_device);
    ok(refcount == 2, "Got unexpected refcount %u.\n", (unsigned int)refcount);

    check_interface(fence, &IID_ID3D12Object, TRUE);
    check_interface(fence, &IID_ID3D12DeviceChild, TRUE);
    check_interface(fence, &IID_ID3D12Pageable, TRUE);
    check_interface(fence, &IID_ID3D12Fence, TRUE);

    value = ID3D12Fence_GetCompletedValue(fence);
    ok(value == 0, "Got unexpected value %lu.\n", (unsigned long)value);

    refcount = ID3D12Fence_Release(fence);
    ok(!refcount, "ID3D12Fence has %u references left.\n", (unsigned int)refcount);

    hr = ID3D12Device_CreateFence(device, 99, D3D12_FENCE_FLAG_NONE,
            &IID_ID3D12Fence, (void **)&fence);
    ok(SUCCEEDED(hr), "CreateFence failed, hr %#x.\n", hr);
    value = ID3D12Fence_GetCompletedValue(fence);
    ok(value == 99, "Got unexpected value %lu.\n", (unsigned long)value);
    refcount = ID3D12Fence_Release(fence);
    ok(!refcount, "ID3D12Fence has %u references left.\n", (unsigned int)refcount);

    refcount = ID3D12Device_Release(device);
    ok(!refcount, "ID3D12Device has %u references left.\n", (unsigned int)refcount);
}

static void test_reset_command_allocator(void)
{
    ID3D12CommandAllocator *command_allocator;
    ID3D12GraphicsCommandList *command_list;
    ID3D12Device *device;
    ULONG refcount;
    HRESULT hr;

    if (!(device = create_device()))
    {
        skip("Failed to create device.\n");
        return;
    }

    hr = ID3D12Device_CreateCommandAllocator(device, D3D12_COMMAND_LIST_TYPE_DIRECT,
            &IID_ID3D12CommandAllocator, (void **)&command_allocator);
    ok(SUCCEEDED(hr), "CreateCommandAllocator failed, hr %#x.\n", hr);

    hr = ID3D12CommandAllocator_Reset(command_allocator);
    ok(hr == S_OK, "Got unexpected hr %#x.\n", hr);
    hr = ID3D12CommandAllocator_Reset(command_allocator);
    ok(hr == S_OK, "Got unexpected hr %#x.\n", hr);

    hr = ID3D12Device_CreateCommandList(device, 0, D3D12_COMMAND_LIST_TYPE_DIRECT,
            command_allocator, NULL, &IID_ID3D12GraphicsCommandList, (void **)&command_list);
    ok(SUCCEEDED(hr), "CreateCommandList failed, hr %#x.\n", hr);

    hr = ID3D12CommandAllocator_Reset(command_allocator);
    ok(hr == E_FAIL, "Got unexpected hr %#x.\n", hr);
    hr = ID3D12CommandAllocator_Reset(command_allocator);
    ok(hr == E_FAIL, "Got unexpected hr %#x.\n", hr);

    hr = ID3D12GraphicsCommandList_Close(command_list);
    ok(SUCCEEDED(hr), "Close failed, hr %#x.\n", hr);

    hr = ID3D12CommandAllocator_Reset(command_allocator);
    ok(hr == S_OK, "Got unexpected hr %#x.\n", hr);
    hr = ID3D12CommandAllocator_Reset(command_allocator);
    ok(hr == S_OK, "Got unexpected hr %#x.\n", hr);

    hr = ID3D12GraphicsCommandList_Reset(command_list, command_allocator, NULL);
    ok(SUCCEEDED(hr), "Resetting Command list failed, hr %#x.\n", hr);

    hr = ID3D12CommandAllocator_Reset(command_allocator);
    ok(hr == E_FAIL, "Got unexpected hr %#x.\n", hr);

    hr = ID3D12GraphicsCommandList_Close(command_list);
    ok(SUCCEEDED(hr), "Close failed, hr %#x.\n", hr);
    hr = ID3D12GraphicsCommandList_Reset(command_list, command_allocator, NULL);
    ok(SUCCEEDED(hr), "Resetting command list failed, hr %#x.\n", hr);

    ID3D12GraphicsCommandList_Release(command_list);
    ID3D12CommandAllocator_Release(command_allocator);
    refcount = ID3D12Device_Release(device);
    ok(!refcount, "ID3D12Device has %u references left.\n", (unsigned int)refcount);
}

static void test_cpu_signal_fence(void)
{
    HANDLE event1, event2;
    ID3D12Device *device;
    unsigned int i, ret;
    ID3D12Fence *fence;
    ULONG refcount;
    UINT64 value;
    HRESULT hr;

    if (!(device = create_device()))
    {
        skip("Failed to create device.\n");
        return;
    }

    hr = ID3D12Device_CreateFence(device, 0, D3D12_FENCE_FLAG_NONE,
            &IID_ID3D12Fence, (void **)&fence);
    ok(SUCCEEDED(hr), "CreateFence failed, hr %#x.\n", hr);

    hr = ID3D12Fence_Signal(fence, 1);
    ok(SUCCEEDED(hr), "Signal failed, hr %#x.\n", hr);
    value = ID3D12Fence_GetCompletedValue(fence);
    ok(value == 1, "Got unexpected value %lu.\n", (unsigned long)value);

    hr = ID3D12Fence_Signal(fence, 10);
    ok(SUCCEEDED(hr), "Signal failed, hr %#x.\n", hr);
    value = ID3D12Fence_GetCompletedValue(fence);
    ok(value == 10, "Got unexpected value %lu.\n", (unsigned long)value);

    hr = ID3D12Fence_Signal(fence, 5);
    ok(SUCCEEDED(hr), "Signal failed, hr %#x.\n", hr);
    value = ID3D12Fence_GetCompletedValue(fence);
    ok(value == 5, "Got unexpected value %lu.\n", (unsigned long)value);

    hr = ID3D12Fence_Signal(fence, 0);
    ok(SUCCEEDED(hr), "Signal failed, hr %#x.\n", hr);
    value = ID3D12Fence_GetCompletedValue(fence);
    ok(value == 0, "Got unexpected value %lu.\n", (unsigned long)value);

    /* Basic tests with single event. */
    event1 = create_event();
    ok(!!event1, "create_event failed.\n");
    ret = wait_event(event1, 0);
    ok(ret == WAIT_TIMEOUT, "Got unexpected return value %#x.\n", ret);

    hr = ID3D12Fence_SetEventOnCompletion(fence, 5, event1);
    ok(SUCCEEDED(hr), "SetEventOnCompletion failed, hr %#x.\n", hr);
    ret = wait_event(event1, 0);
    ok(ret == WAIT_TIMEOUT, "Got unexpected return value %#x.\n", ret);
    hr = ID3D12Fence_Signal(fence, 5);
    ok(SUCCEEDED(hr), "Signal failed, hr %#x.\n", hr);
    ret = wait_event(event1, 0);
    ok(ret == WAIT_OBJECT_0, "Got unexpected return value %#x.\n", ret);
    ret = wait_event(event1, 0);
    ok(ret == WAIT_TIMEOUT, "Got unexpected return value %#x.\n", ret);

    hr = ID3D12Fence_SetEventOnCompletion(fence, 6, event1);
    ok(SUCCEEDED(hr), "SetEventOnCompletion failed, hr %#x.\n", hr);
    ret = wait_event(event1, 0);
    ok(ret == WAIT_TIMEOUT, "Got unexpected return value %#x.\n", ret);
    hr = ID3D12Fence_Signal(fence, 7);
    ok(SUCCEEDED(hr), "Signal failed, hr %#x.\n", hr);
    ret = wait_event(event1, 0);
    ok(ret == WAIT_OBJECT_0, "Got unexpected return value %#x.\n", ret);
    ret = wait_event(event1, 0);
    ok(ret == WAIT_TIMEOUT, "Got unexpected return value %#x.\n", ret);

    hr = ID3D12Fence_Signal(fence, 10);
    ok(SUCCEEDED(hr), "Signal failed, hr %#x.\n", hr);
    ret = wait_event(event1, 0);
    ok(ret == WAIT_TIMEOUT, "Got unexpected return value %#x.\n", ret);

    /* Event is signaled immediately when value <= GetCompletedValue(). */
    ret = wait_event(event1, 0);
    ok(ret == WAIT_TIMEOUT, "Got unexpected return value %#x.\n", ret);
    for (i = 0; i <= ID3D12Fence_GetCompletedValue(fence); ++i)
    {
        hr = ID3D12Fence_SetEventOnCompletion(fence, i, event1);
        ok(SUCCEEDED(hr), "SetEventOnCompletion failed, hr %#x.\n", hr);
        ret = wait_event(event1, 0);
        ok(ret == WAIT_OBJECT_0, "Got unexpected return value %#x for %u.\n", ret, i);
        ret = wait_event(event1, 0);
        ok(ret == WAIT_TIMEOUT, "Got unexpected return value %#x for %u.\n", ret, i);
    }
    hr = ID3D12Fence_SetEventOnCompletion(fence, i, event1);
    ok(SUCCEEDED(hr), "SetEventOnCompletion failed, hr %#x.\n", hr);
    ret = wait_event(event1, 0);
    ok(ret == WAIT_TIMEOUT, "Got unexpected return value %#x.\n", ret);
    hr = ID3D12Fence_Signal(fence, i);
    ok(SUCCEEDED(hr), "Signal failed, hr %#x.\n", hr);
    ret = wait_event(event1, 0);
    ok(ret == WAIT_OBJECT_0, "Got unexpected return value %#x.\n", ret);
    ret = wait_event(event1, 0);
    ok(ret == WAIT_TIMEOUT, "Got unexpected return value %#x.\n", ret);

    /* Attach event to multiple values. */
    hr = ID3D12Fence_Signal(fence, 0);
    ok(SUCCEEDED(hr), "Signal failed, hr %#x.\n", hr);
    ret = wait_event(event1, 0);
    ok(ret == WAIT_TIMEOUT, "Got unexpected return value %#x.\n", ret);

    hr = ID3D12Fence_SetEventOnCompletion(fence, 3, event1);
    ok(SUCCEEDED(hr), "SetEventOnCompletion failed, hr %#x.\n", hr);
    hr = ID3D12Fence_SetEventOnCompletion(fence, 5, event1);
    ok(SUCCEEDED(hr), "SetEventOnCompletion failed, hr %#x.\n", hr);
    hr = ID3D12Fence_SetEventOnCompletion(fence, 9, event1);
    ok(SUCCEEDED(hr), "SetEventOnCompletion failed, hr %#x.\n", hr);
    hr = ID3D12Fence_SetEventOnCompletion(fence, 12, event1);
    ok(SUCCEEDED(hr), "SetEventOnCompletion failed, hr %#x.\n", hr);
    hr = ID3D12Fence_SetEventOnCompletion(fence, 12, event1);
    ok(SUCCEEDED(hr), "SetEventOnCompletion failed, hr %#x.\n", hr);
    ret = wait_event(event1, 0);
    ok(ret == WAIT_TIMEOUT, "Got unexpected return value %#x.\n", ret);
    for (i = 1; i < 13; ++i)
    {
        hr = ID3D12Fence_Signal(fence, i);
        ok(SUCCEEDED(hr), "Signal failed, hr %#x.\n", hr);
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
    ok(SUCCEEDED(hr), "Signal failed, hr %#x.\n", hr);
    value = ID3D12Fence_GetCompletedValue(fence);
    ok(value == 0, "Got unexpected value %lu.\n", (unsigned long)value);

    event2 = create_event();
    ok(!!event2, "create_event failed.\n");

    ret = wait_event(event1, 0);
    ok(ret == WAIT_TIMEOUT, "Got unexpected return value %#x.\n", ret);
    ret = wait_event(event2, 0);
    ok(ret == WAIT_TIMEOUT, "Got unexpected return value %#x.\n", ret);
    hr = ID3D12Fence_SetEventOnCompletion(fence, 100, event1);
    ok(SUCCEEDED(hr), "SetEventOnCompletion failed, hr %#x.\n", hr);
    hr = ID3D12Fence_SetEventOnCompletion(fence, ~(UINT64)0, event2);
    ok(SUCCEEDED(hr), "SetEventOnCompletion failed, hr %#x.\n", hr);

    hr = ID3D12Fence_Signal(fence, 50);
    ok(SUCCEEDED(hr), "Signal failed, hr %#x.\n", hr);
    ret = wait_event(event1, 0);
    ok(ret == WAIT_TIMEOUT, "Got unexpected return value %#x.\n", ret);
    ret = wait_event(event2, 0);
    ok(ret == WAIT_TIMEOUT, "Got unexpected return value %#x.\n", ret);

    hr = ID3D12Fence_Signal(fence, 99);
    ok(SUCCEEDED(hr), "Signal failed, hr %#x.\n", hr);
    ret = wait_event(event1, 0);
    ok(ret == WAIT_TIMEOUT, "Got unexpected return value %#x.\n", ret);
    ret = wait_event(event2, 0);
    ok(ret == WAIT_TIMEOUT, "Got unexpected return value %#x.\n", ret);

    hr = ID3D12Fence_Signal(fence, 100);
    ok(SUCCEEDED(hr), "Signal failed, hr %#x.\n", hr);
    ret = wait_event(event1, 0);
    ok(ret == WAIT_OBJECT_0, "Got unexpected return value %#x.\n", ret);
    ret = wait_event(event1, 0);
    ok(ret == WAIT_TIMEOUT, "Got unexpected return value %#x.\n", ret);
    ret = wait_event(event2, 0);
    ok(ret == WAIT_TIMEOUT, "Got unexpected return value %#x.\n", ret);

    hr = ID3D12Fence_Signal(fence, 101);
    ok(SUCCEEDED(hr), "Signal failed, hr %#x.\n", hr);
    ret = wait_event(event1, 0);
    ok(ret == WAIT_TIMEOUT, "Got unexpected return value %#x.\n", ret);
    ret = wait_event(event2, 0);
    ok(ret == WAIT_TIMEOUT, "Got unexpected return value %#x.\n", ret);

    hr = ID3D12Fence_Signal(fence, 0);
    ok(SUCCEEDED(hr), "Signal failed, hr %#x.\n", hr);
    ret = wait_event(event1, 0);
    ok(ret == WAIT_TIMEOUT, "Got unexpected return value %#x.\n", ret);
    ret = wait_event(event2, 0);
    ok(ret == WAIT_TIMEOUT, "Got unexpected return value %#x.\n", ret);

    hr = ID3D12Fence_Signal(fence, 100);
    ok(SUCCEEDED(hr), "Signal failed, hr %#x.\n", hr);
    ret = wait_event(event1, 0);
    ok(ret == WAIT_TIMEOUT, "Got unexpected return value %#x.\n", ret);
    ret = wait_event(event2, 0);
    ok(ret == WAIT_TIMEOUT, "Got unexpected return value %#x.\n", ret);

    hr = ID3D12Fence_Signal(fence, ~(UINT64)0);
    ok(SUCCEEDED(hr), "Signal failed, hr %#x.\n", hr);
    ret = wait_event(event1, 0);
    ok(ret == WAIT_TIMEOUT, "Got unexpected return value %#x.\n", ret);
    ret = wait_event(event2, 0);
    ok(ret == WAIT_OBJECT_0, "Got unexpected return value %#x.\n", ret);
    ret = wait_event(event2, 0);
    ok(ret == WAIT_TIMEOUT, "Got unexpected return value %#x.\n", ret);

    hr = ID3D12Fence_Signal(fence, ~(UINT64)0);
    ok(SUCCEEDED(hr), "Signal failed, hr %#x.\n", hr);
    ret = wait_event(event1, 0);
    ok(ret == WAIT_TIMEOUT, "Got unexpected return value %#x.\n", ret);
    ret = wait_event(event2, 0);
    ok(ret == WAIT_TIMEOUT, "Got unexpected return value %#x.\n", ret);
    hr = ID3D12Fence_Signal(fence, 0);
    ok(SUCCEEDED(hr), "Signal failed, hr %#x.\n", hr);
    ret = wait_event(event1, 0);
    ok(ret == WAIT_TIMEOUT, "Got unexpected return value %#x.\n", ret);
    ret = wait_event(event2, 0);
    ok(ret == WAIT_TIMEOUT, "Got unexpected return value %#x.\n", ret);

    /* Attach two events to the same value. */
    hr = ID3D12Fence_Signal(fence, 0);
    ok(SUCCEEDED(hr), "Signal failed, hr %#x.\n", hr);
    ret = wait_event(event1, 0);
    ok(ret == WAIT_TIMEOUT, "Got unexpected return value %#x.\n", ret);
    ret = wait_event(event2, 0);
    ok(ret == WAIT_TIMEOUT, "Got unexpected return value %#x.\n", ret);

    hr = ID3D12Fence_SetEventOnCompletion(fence, 1, event1);
    ok(SUCCEEDED(hr), "SetEventOnCompletion failed, hr %#x.\n", hr);
    hr = ID3D12Fence_SetEventOnCompletion(fence, 1, event2);
    ok(SUCCEEDED(hr), "SetEventOnCompletion failed, hr %#x.\n", hr);
    ret = wait_event(event1, 0);
    ok(ret == WAIT_TIMEOUT, "Got unexpected return value %#x.\n", ret);
    ret = wait_event(event2, 0);
    ok(ret == WAIT_TIMEOUT, "Got unexpected return value %#x.\n", ret);
    hr = ID3D12Fence_Signal(fence, 3);
    ok(SUCCEEDED(hr), "Signal failed, hr %#x.\n", hr);
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
    ok(SUCCEEDED(hr), "Signal failed, hr %#x.\n", hr);
    value = ID3D12Fence_GetCompletedValue(fence);
    ok(value == 20, "Got unexpected value %lu.\n", (unsigned long)value);
    ret = wait_event(event1, 0);
    ok(ret == WAIT_TIMEOUT, "Got unexpected return value %#x.\n", ret);

    signal_event(event1);
    hr = ID3D12Fence_SetEventOnCompletion(fence, 30, event1);
    ok(SUCCEEDED(hr), "SetEventOnCompletion failed, hr %#x.\n", hr);
    ret = wait_event(event1, 0);
    ok(ret == WAIT_OBJECT_0, "Got unexpected return value %#x.\n", ret);
    ret = wait_event(event1, 0);
    ok(ret == WAIT_TIMEOUT, "Got unexpected return value %#x.\n", ret);

    hr = ID3D12Fence_Signal(fence, 30);
    ok(SUCCEEDED(hr), "Signal failed, hr %#x.\n", hr);
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

static void test_clear_render_target_view(void)
{
    static const float green[] = { 0.0f, 1.0f, 0.0f, 1.0f };
    D3D12_COMMAND_QUEUE_DESC command_queue_desc;
    ID3D12CommandAllocator *command_allocator;
    D3D12_DESCRIPTOR_HEAP_DESC rtv_heap_desc;
    ID3D12GraphicsCommandList *command_list;
    D3D12_CPU_DESCRIPTOR_HANDLE rtv_handle;
    D3D12_HEAP_PROPERTIES heap_properties;
    D3D12_RESOURCE_DESC resource_desc;
    unsigned int rtv_increment_size;
    ID3D12DescriptorHeap *rtv_heap;
    D3D12_CLEAR_VALUE clear_value;
    struct resource_readback rb;
    ID3D12CommandQueue *queue;
    ID3D12Resource *resource;
    ID3D12Device *device;
    unsigned int x, y;
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
    ok(SUCCEEDED(hr), "CreateCommandQueue failed, hr %#x.\n", hr);

    hr = ID3D12Device_CreateCommandAllocator(device, D3D12_COMMAND_LIST_TYPE_DIRECT,
            &IID_ID3D12CommandAllocator, (void **)&command_allocator);
    ok(SUCCEEDED(hr), "CreateCommandAllocator failed, hr %#x.\n", hr);

    hr = ID3D12Device_CreateCommandList(device, 0, D3D12_COMMAND_LIST_TYPE_DIRECT,
            command_allocator, NULL, &IID_ID3D12GraphicsCommandList, (void **)&command_list);
    ok(SUCCEEDED(hr), "CreateCommandList failed, hr %#x.\n", hr);

    rtv_heap_desc.NumDescriptors = 1;
    rtv_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtv_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    rtv_heap_desc.NodeMask = 0;
    hr = ID3D12Device_CreateDescriptorHeap(device, &rtv_heap_desc,
            &IID_ID3D12DescriptorHeap, (void **)&rtv_heap);
    ok(SUCCEEDED(hr), "CreateDescriptorHeap failed, hr %#x.\n", hr);

    rtv_increment_size = ID3D12Device_GetDescriptorHandleIncrementSize(device,
            D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    trace("RTV descriptor handle increment size: %u.\n", rtv_increment_size);

    rtv_handle = ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(rtv_heap);

    memset(&heap_properties, 0, sizeof(heap_properties));
    heap_properties.Type = D3D12_HEAP_TYPE_DEFAULT;
    resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D,
    resource_desc.Alignment = 0,
    resource_desc.Width = 32,
    resource_desc.Height = 32,
    resource_desc.DepthOrArraySize = 1,
    resource_desc.MipLevels = 1,
    resource_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM,
    resource_desc.SampleDesc.Count = 1;
    resource_desc.SampleDesc.Quality = 0;
    resource_desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN,
    resource_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,
    clear_value.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    clear_value.Color[0] = 1.0f;
    clear_value.Color[1] = 0.0f;
    clear_value.Color[2] = 0.0f;
    clear_value.Color[3] = 1.0f;
    hr = ID3D12Device_CreateCommittedResource(device,
            &heap_properties, D3D12_HEAP_FLAG_NONE, &resource_desc,
            D3D12_RESOURCE_STATE_RENDER_TARGET, &clear_value,
            &IID_ID3D12Resource, (void **)&resource);
    ok(SUCCEEDED(hr), "CreateCommittedResource failed, hr %#x.\n", hr);

    ID3D12Device_CreateRenderTargetView(device, resource, NULL, rtv_handle);

    ID3D12GraphicsCommandList_ClearRenderTargetView(command_list, rtv_handle, green, 0, NULL);
    hr = ID3D12GraphicsCommandList_Close(command_list);
    ok(SUCCEEDED(hr), "Close failed, hr %#x.\n", hr);

    exec_command_list(queue, command_list);
    wait_queue_idle(device, queue);
    hr = ID3D12CommandAllocator_Reset(command_allocator);
    ok(SUCCEEDED(hr), "Command allocator reset failed, hr %#x.\n", hr);
    hr = ID3D12GraphicsCommandList_Reset(command_list, command_allocator, NULL);
    ok(SUCCEEDED(hr), "Command list reset failed, hr %#x.\n", hr);

    get_texture_readback_with_command_list(resource, 0, &rb, queue, command_list);
    for (y = 0; y < resource_desc.Height; ++y)
    {
        for (x = 0; x < resource_desc.Width; ++x)
        {
           unsigned int v = get_readback_uint(&rb, x, y);
           todo(v == 0xff00ff00, "Got unexpected value 0x%08x at (%u, %u).\n", v, x, y);
        }
    }
    release_resource_readback(&rb);

    ID3D12GraphicsCommandList_Release(command_list);
    ID3D12CommandAllocator_Release(command_allocator);
    ID3D12Resource_Release(resource);
    ID3D12CommandQueue_Release(queue);
    ID3D12DescriptorHeap_Release(rtv_heap);
    refcount = ID3D12Device_Release(device);
    ok(!refcount, "ID3D12Device has %u references left.\n", (unsigned int)refcount);
}

START_TEST(d3d12)
{
    ID3D12Debug *debug;

    if (SUCCEEDED(D3D12GetDebugInterface(&IID_ID3D12Debug, (void **)&debug)))
    {
        ID3D12Debug_EnableDebugLayer(debug);
        ID3D12Debug_Release(debug);
    }

    test_create_device();
    test_node_count();
    test_check_feature_support();
    test_create_command_allocator();
    test_create_command_list();
    test_create_command_queue();
    test_create_committed_resource();
    test_create_descriptor_heap();
    test_create_root_signature();
    test_create_pipeline_state();
    test_create_fence();
    test_reset_command_allocator();
    test_cpu_signal_fence();
    test_clear_render_target_view();
}
