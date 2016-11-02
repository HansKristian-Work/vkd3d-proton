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

#include <inttypes.h>

#define COBJMACROS
#define INITGUID
#include "vkd3d_test.h"
#include "vkd3d_windows.h"
#define WIDL_C_INLINE_WRAPPERS
#include "d3d12.h"

#ifndef _WIN32
# include <pthread.h>
# include "vkd3d_utils.h"
#endif

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
static void check_interface_(unsigned int line, IUnknown *iface, REFIID riid, bool supported)
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
    return ID3D12Device_CreateRootSignature(device, 0, desc, ~(SIZE_T)0,
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

static void transition_resource_state(ID3D12GraphicsCommandList *list, ID3D12Resource *resource,
        D3D12_RESOURCE_STATES state_before, D3D12_RESOURCE_STATES state_after)
{
    D3D12_RESOURCE_BARRIER barrier;

    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier.Transition.pResource = resource;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = state_before;
    barrier.Transition.StateAfter = state_after;

    ID3D12GraphicsCommandList_ResourceBarrier(list, 1, &barrier);
}

static void uav_barrier(ID3D12GraphicsCommandList *list, ID3D12Resource *resource)
{
    D3D12_RESOURCE_BARRIER barrier;

    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier.UAV.pResource = resource;

    ID3D12GraphicsCommandList_ResourceBarrier(list, 1, &barrier);
}

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
    return vkd3d_create_event();
}

static void signal_event(HANDLE event)
{
    vkd3d_signal_event(event);
}

static unsigned int wait_event(HANDLE event, unsigned int milliseconds)
{
    return vkd3d_wait_event(event, milliseconds);
}

static void destroy_event(HANDLE event)
{
    vkd3d_destroy_event(event);
}
#endif

typedef void (*thread_main_pfn)(void *data);

struct test_thread_data
{
    thread_main_pfn main_pfn;
    void *user_data;
};

#ifdef _WIN32
static DWORD WINAPI test_thread_main(void *untyped_data)
{
    struct test_thread_data *data = untyped_data;
    data->main_pfn(data->user_data);
    free(untyped_data);
    return 0;
}

static HANDLE create_thread(thread_main_pfn main_pfn, void *user_data)
{
    struct test_thread_data *data;

    if (!(data = malloc(sizeof(*data))))
        return NULL;
    data->main_pfn = main_pfn;
    data->user_data = user_data;

    return CreateThread(NULL, 0, test_thread_main, data, 0, NULL);
}

static bool join_thread(HANDLE thread)
{
    int ret;

    ret = WaitForSingleObject(thread, INFINITE);
    CloseHandle(thread);
    return ret == WAIT_OBJECT_0;
}
#else
static void *test_thread_main(void *untyped_data)
{
    struct test_thread_data *data = untyped_data;
    data->main_pfn(data->user_data);
    free(untyped_data);
    return NULL;
}

static HANDLE create_thread(thread_main_pfn main_pfn, void *user_data)
{
    struct test_thread_data *data;
    pthread_t *thread;

    if (!(thread = malloc(sizeof(*thread))))
        return NULL;

    if (!(data = malloc(sizeof(*data))))
    {
        free(thread);
        return NULL;
    }
    data->main_pfn = main_pfn;
    data->user_data = user_data;

    if (pthread_create(thread, NULL, test_thread_main, data))
    {
        free(data);
        free(thread);
        return NULL;
    }

    return thread;
}

static bool join_thread(HANDLE untyped_thread)
{
    pthread_t *thread = untyped_thread;
    int rc;

    rc = pthread_join(*thread, NULL);
    free(thread);
    return !rc;
}
#endif

static HRESULT wait_for_fence(ID3D12Fence *fence, UINT64 value)
{
    unsigned int ret;
    HANDLE event;
    HRESULT hr;

    if (ID3D12Fence_GetCompletedValue(fence) >= value)
        return S_OK;

    if (!(event = create_event()))
        return E_FAIL;

    if (FAILED(hr = ID3D12Fence_SetEventOnCompletion(fence, value, event)))
    {
        destroy_event(event);
        return hr;
    }

    ret = wait_event(event, INFINITE);
    destroy_event(event);

    return ret == WAIT_OBJECT_0;
}

#define wait_queue_idle(a, b) wait_queue_idle_(__LINE__, a, b)
static void wait_queue_idle_(unsigned int line, ID3D12Device *device, ID3D12CommandQueue *queue)
{
    ID3D12Fence *fence;
    HRESULT hr;

    hr = ID3D12Device_CreateFence(device, 0, D3D12_FENCE_FLAG_NONE,
            &IID_ID3D12Fence, (void **)&fence);
    ok_(line)(SUCCEEDED(hr), "CreateFence failed, hr %#x.\n", hr);

    hr = ID3D12CommandQueue_Signal(queue, fence, 1);
    ok_(line)(SUCCEEDED(hr), "Failed to signal fence, hr %#x.\n", hr);
    hr = wait_for_fence(fence, 1);
    ok_(line)(SUCCEEDED(hr), "Failed to wait for fence, hr %#x.\n", hr);

    ID3D12Fence_Release(fence);
}

static unsigned int format_size(DXGI_FORMAT format)
{
    switch (format)
    {
        case DXGI_FORMAT_D32_FLOAT:
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
    D3D12_RANGE read_range;
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

    read_range.Begin = 0;
    read_range.End = resource_desc.Width;
    hr = ID3D12Resource_Map(resource, 0, &read_range, &rb->data);
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

struct draw_test_context
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
};

#define init_draw_test_context(context) init_draw_test_context_(__LINE__, context)
static bool init_draw_test_context_(unsigned int line, struct draw_test_context *context)
{
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pipeline_state_desc;
    D3D12_ROOT_SIGNATURE_DESC root_signature_desc;
    D3D12_COMMAND_QUEUE_DESC command_queue_desc;
    D3D12_DESCRIPTOR_HEAP_DESC rtv_heap_desc;
    D3D12_HEAP_PROPERTIES heap_properties;
    D3D12_RESOURCE_DESC resource_desc;
    D3D12_CLEAR_VALUE clear_value;
    ID3D12Device *device;
    HRESULT hr;

    static const DWORD dxbc_vs_code[] =
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
    static const DWORD spv_vs_code[] =
    {
#if 0
        #version 450 core

        void main()
        {
            uint id = gl_VertexIndex;
            vec2 coords = vec2((id << 1) & 2, id & 2);
            gl_Position = vec4(coords * vec2(2, -2) + vec2(-1, 1), 0, 1);
        }
#endif
        0x07230203, 0x00010000, 0x00080001, 0x00000032, 0x00000000, 0x00020011, 0x00000001, 0x0006000b,
        0x00000001, 0x4c534c47, 0x6474732e, 0x3035342e, 0x00000000, 0x0003000e, 0x00000000, 0x00000001,
        0x0007000f, 0x00000000, 0x00000004, 0x6e69616d, 0x00000000, 0x0000000b, 0x00000021, 0x00040047,
        0x0000000b, 0x0000000b, 0x0000002a, 0x00050048, 0x0000001f, 0x00000000, 0x0000000b, 0x00000000,
        0x00050048, 0x0000001f, 0x00000001, 0x0000000b, 0x00000001, 0x00050048, 0x0000001f, 0x00000002,
        0x0000000b, 0x00000003, 0x00050048, 0x0000001f, 0x00000003, 0x0000000b, 0x00000004, 0x00030047,
        0x0000001f, 0x00000002, 0x00020013, 0x00000002, 0x00030021, 0x00000003, 0x00000002, 0x00040015,
        0x00000006, 0x00000020, 0x00000000, 0x00040020, 0x00000007, 0x00000007, 0x00000006, 0x00040015,
        0x00000009, 0x00000020, 0x00000001, 0x00040020, 0x0000000a, 0x00000001, 0x00000009, 0x0004003b,
        0x0000000a, 0x0000000b, 0x00000001, 0x00030016, 0x0000000e, 0x00000020, 0x00040017, 0x0000000f,
        0x0000000e, 0x00000002, 0x00040020, 0x00000010, 0x00000007, 0x0000000f, 0x0004002b, 0x00000009,
        0x00000013, 0x00000001, 0x0004002b, 0x00000006, 0x00000015, 0x00000002, 0x00040017, 0x0000001c,
        0x0000000e, 0x00000004, 0x0004002b, 0x00000006, 0x0000001d, 0x00000001, 0x0004001c, 0x0000001e,
        0x0000000e, 0x0000001d, 0x0006001e, 0x0000001f, 0x0000001c, 0x0000000e, 0x0000001e, 0x0000001e,
        0x00040020, 0x00000020, 0x00000003, 0x0000001f, 0x0004003b, 0x00000020, 0x00000021, 0x00000003,
        0x0004002b, 0x00000009, 0x00000022, 0x00000000, 0x0004002b, 0x0000000e, 0x00000024, 0x40000000,
        0x0004002b, 0x0000000e, 0x00000025, 0xc0000000, 0x0005002c, 0x0000000f, 0x00000026, 0x00000024,
        0x00000025, 0x0004002b, 0x0000000e, 0x00000028, 0xbf800000, 0x0004002b, 0x0000000e, 0x00000029,
        0x3f800000, 0x0005002c, 0x0000000f, 0x0000002a, 0x00000028, 0x00000029, 0x0004002b, 0x0000000e,
        0x0000002c, 0x00000000, 0x00040020, 0x00000030, 0x00000003, 0x0000001c, 0x00050036, 0x00000002,
        0x00000004, 0x00000000, 0x00000003, 0x000200f8, 0x00000005, 0x0004003b, 0x00000007, 0x00000008,
        0x00000007, 0x0004003b, 0x00000010, 0x00000011, 0x00000007, 0x0004003d, 0x00000009, 0x0000000c,
        0x0000000b, 0x0004007c, 0x00000006, 0x0000000d, 0x0000000c, 0x0003003e, 0x00000008, 0x0000000d,
        0x0004003d, 0x00000006, 0x00000012, 0x00000008, 0x000500c4, 0x00000006, 0x00000014, 0x00000012,
        0x00000013, 0x000500c7, 0x00000006, 0x00000016, 0x00000014, 0x00000015, 0x00040070, 0x0000000e,
        0x00000017, 0x00000016, 0x0004003d, 0x00000006, 0x00000018, 0x00000008, 0x000500c7, 0x00000006,
        0x00000019, 0x00000018, 0x00000015, 0x00040070, 0x0000000e, 0x0000001a, 0x00000019, 0x00050050,
        0x0000000f, 0x0000001b, 0x00000017, 0x0000001a, 0x0003003e, 0x00000011, 0x0000001b, 0x0004003d,
        0x0000000f, 0x00000023, 0x00000011, 0x00050085, 0x0000000f, 0x00000027, 0x00000023, 0x00000026,
        0x00050081, 0x0000000f, 0x0000002b, 0x00000027, 0x0000002a, 0x00050051, 0x0000000e, 0x0000002d,
        0x0000002b, 0x00000000, 0x00050051, 0x0000000e, 0x0000002e, 0x0000002b, 0x00000001, 0x00070050,
        0x0000001c, 0x0000002f, 0x0000002d, 0x0000002e, 0x0000002c, 0x00000029, 0x00050041, 0x00000030,
        0x00000031, 0x00000021, 0x00000022, 0x0003003e, 0x00000031, 0x0000002f, 0x000100fd, 0x00010038,
    };
    static const DWORD dxbc_ps_code[] =
    {
#if 0
        void main(const in float4 position : SV_Position, out float4 target : SV_Target0)
        {
            target = float4(1.0f, 1.0f, 1.0f, 1.0f);
        }
#endif
        0x43425844, 0xc1e910e0, 0xe168c974, 0xc32ae666, 0x82f81f71, 0x00000001, 0x000000d8, 0x00000003,
        0x0000002c, 0x00000060, 0x00000094, 0x4e475349, 0x0000002c, 0x00000001, 0x00000008, 0x00000020,
        0x00000000, 0x00000001, 0x00000003, 0x00000000, 0x0000000f, 0x505f5653, 0x7469736f, 0x006e6f69,
        0x4e47534f, 0x0000002c, 0x00000001, 0x00000008, 0x00000020, 0x00000000, 0x00000000, 0x00000003,
        0x00000000, 0x0000000f, 0x545f5653, 0x65677261, 0xabab0074, 0x58454853, 0x0000003c, 0x00000050,
        0x0000000f, 0x0100086a, 0x03000065, 0x001020f2, 0x00000000, 0x08000036, 0x001020f2, 0x00000000,
        0x00004002, 0x3f800000, 0x3f800000, 0x3f800000, 0x3f800000, 0x0100003e,
    };
    static const DWORD spv_ps_code[] =
    {
#if 0
        #version 450 core

        layout(location = 0) out vec4 target;

        void main()
        {
            target = vec4(1.0f, 1.0f, 1.0f, 1.0f);
        }
#endif
        0x07230203, 0x00010000, 0x00080001, 0x0000000c, 0x00000000, 0x00020011, 0x00000001, 0x0006000b,
        0x00000001, 0x4c534c47, 0x6474732e, 0x3035342e, 0x00000000, 0x0003000e, 0x00000000, 0x00000001,
        0x0006000f, 0x00000004, 0x00000004, 0x6e69616d, 0x00000000, 0x00000009, 0x00030010, 0x00000004,
        0x00000007, 0x00040047, 0x00000009, 0x0000001e, 0x00000000, 0x00020013, 0x00000002, 0x00030021,
        0x00000003, 0x00000002, 0x00030016, 0x00000006, 0x00000020, 0x00040017, 0x00000007, 0x00000006,
        0x00000004, 0x00040020, 0x00000008, 0x00000003, 0x00000007, 0x0004003b, 0x00000008, 0x00000009,
        0x00000003, 0x0004002b, 0x00000006, 0x0000000a, 0x3f800000, 0x0007002c, 0x00000007, 0x0000000b,
        0x0000000a, 0x0000000a, 0x0000000a, 0x0000000a, 0x00050036, 0x00000002, 0x00000004, 0x00000000,
        0x00000003, 0x000200f8, 0x00000005, 0x0003003e, 0x00000009, 0x0000000b, 0x000100fd, 0x00010038,
    };

    if (!(context->device = create_device()))
    {
        skip_(line)("Failed to create device.\n");
        return false;
    }
    device = context->device;

    command_queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    command_queue_desc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
    command_queue_desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    command_queue_desc.NodeMask = 0;
    hr = ID3D12Device_CreateCommandQueue(device, &command_queue_desc,
            &IID_ID3D12CommandQueue, (void **)&context->queue);
    ok_(line)(SUCCEEDED(hr), "CreateCommandQueue failed, hr %#x.\n", hr);

    hr = ID3D12Device_CreateCommandAllocator(device, D3D12_COMMAND_LIST_TYPE_DIRECT,
            &IID_ID3D12CommandAllocator, (void **)&context->allocator);
    ok_(line)(SUCCEEDED(hr), "CreateCommandAllocator failed, hr %#x.\n", hr);

    hr = ID3D12Device_CreateCommandList(device, 0, D3D12_COMMAND_LIST_TYPE_DIRECT,
            context->allocator, NULL, &IID_ID3D12GraphicsCommandList, (void **)&context->list);
    ok_(line)(SUCCEEDED(hr), "CreateCommandList failed, hr %#x.\n", hr);

    rtv_heap_desc.NumDescriptors = 1;
    rtv_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtv_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    rtv_heap_desc.NodeMask = 0;
    hr = ID3D12Device_CreateDescriptorHeap(device, &rtv_heap_desc,
            &IID_ID3D12DescriptorHeap, (void **)&context->rtv_heap);
    ok_(line)(SUCCEEDED(hr), "CreateDescriptorHeap failed, hr %#x.\n", hr);

    context->rtv = ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(context->rtv_heap);

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
    clear_value.Color[0] = 0.0f;
    clear_value.Color[1] = 1.0f;
    clear_value.Color[2] = 0.0f;
    clear_value.Color[3] = 1.0f;
    hr = ID3D12Device_CreateCommittedResource(device,
            &heap_properties, D3D12_HEAP_FLAG_NONE, &resource_desc,
            D3D12_RESOURCE_STATE_RENDER_TARGET, &clear_value,
            &IID_ID3D12Resource, (void **)&context->render_target);
    ok_(line)(SUCCEEDED(hr), "CreateCommittedResource failed, hr %#x.\n", hr);

    context->render_target_desc = resource_desc;

    ID3D12Device_CreateRenderTargetView(device, context->render_target, NULL, context->rtv);

    root_signature_desc.NumParameters = 0;
    root_signature_desc.pParameters = NULL;
    root_signature_desc.NumStaticSamplers = 0;
    root_signature_desc.pStaticSamplers = NULL;
    root_signature_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
    hr = create_root_signature(device, &root_signature_desc, &context->root_signature);
    ok_(line)(SUCCEEDED(hr), "Failed to create root signature, hr %#x.\n", hr);

    memset(&pipeline_state_desc, 0, sizeof(pipeline_state_desc));
    pipeline_state_desc.pRootSignature = context->root_signature;
    pipeline_state_desc.VS = SHADER_BYTECODE(dxbc_vs_code, spv_vs_code);
    pipeline_state_desc.PS = SHADER_BYTECODE(dxbc_ps_code, spv_ps_code);
    pipeline_state_desc.StreamOutput.RasterizedStream = 0;
    pipeline_state_desc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    pipeline_state_desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    pipeline_state_desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    pipeline_state_desc.SampleMask = ~(UINT)0;
    pipeline_state_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pipeline_state_desc.NumRenderTargets = 1;
    pipeline_state_desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    pipeline_state_desc.SampleDesc.Count = 1;
    hr = ID3D12Device_CreateGraphicsPipelineState(device, &pipeline_state_desc,
            &IID_ID3D12PipelineState, (void **)&context->pipeline_state);
    ok_(line)(SUCCEEDED(hr), "Failed to create graphics pipeline state, hr %#x.\n", hr);

    return true;
}

#define destroy_draw_test_context(context) destroy_draw_test_context_(__LINE__, context)
static void destroy_draw_test_context_(unsigned int line, struct draw_test_context *context)
{
    ULONG refcount;

    ID3D12PipelineState_Release(context->pipeline_state);
    ID3D12RootSignature_Release(context->root_signature);

    ID3D12DescriptorHeap_Release(context->rtv_heap);
    ID3D12Resource_Release(context->render_target);

    ID3D12CommandAllocator_Release(context->allocator);
    ID3D12CommandQueue_Release(context->queue);
    ID3D12GraphicsCommandList_Release(context->list);

    refcount = ID3D12Device_Release(context->device);
    ok_(line)(!refcount, "ID3D12Device has %u references left.\n", (unsigned int)refcount);
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
    D3D12_FEATURE_DATA_ARCHITECTURE architecture;
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

    /* Architecture. */
    memset(&architecture, 0, sizeof(architecture));
    hr = ID3D12Device_CheckFeatureSupport(device, D3D12_FEATURE_ARCHITECTURE,
            &architecture, sizeof(architecture));
    ok(hr == S_OK, "Got unexpected hr %#x.\n", hr);
    ok(!architecture.NodeIndex, "Got unexpected node %u.\n", architecture.NodeIndex);
    ok(!architecture.CacheCoherentUMA || architecture.UMA,
            "Got unexpected cache coherent UMA %#x (UMA %#x).\n",
            architecture.CacheCoherentUMA, architecture.UMA);
    trace("UMA %#x, cache coherent UMA %#x, tile based renderer %#x.\n",
            architecture.UMA, architecture.CacheCoherentUMA, architecture.TileBasedRenderer);

    if (ID3D12Device_GetNodeCount(device) == 1)
    {
        memset(&architecture, 0, sizeof(architecture));
        architecture.NodeIndex = 1;
        hr = ID3D12Device_CheckFeatureSupport(device, D3D12_FEATURE_ARCHITECTURE,
                &architecture, sizeof(architecture));
        ok(hr == E_INVALIDARG, "Got unexpected hr %#x.\n", hr);
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
    D3D12_GPU_VIRTUAL_ADDRESS gpu_address;
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

    gpu_address = ID3D12Resource_GetGPUVirtualAddress(resource);
    ok(!gpu_address, "Got unexpected GPU virtual address %#"PRIx64".\n", gpu_address);

    refcount = ID3D12Resource_Release(resource);
    ok(!refcount, "ID3D12Resource has %u references left.\n", (unsigned int)refcount);

    hr = ID3D12Device_CreateCommittedResource(device, &heap_properties, D3D12_HEAP_FLAG_NONE,
            &resource_desc, D3D12_RESOURCE_STATE_RENDER_TARGET | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
            &clear_value, &IID_ID3D12Resource, (void **)&resource);
    ok(hr == E_INVALIDARG, "Got unexpected hr %#x.\n", hr);

    /* For D3D12_RESOURCE_STATE_RENDER_TARGET the D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET flag is required. */
    resource_desc.Flags = 0;
    hr = ID3D12Device_CreateCommittedResource(device, &heap_properties, D3D12_HEAP_FLAG_NONE,
            &resource_desc, D3D12_RESOURCE_STATE_RENDER_TARGET, NULL,
            &IID_ID3D12Resource, (void **)&resource);
    todo(hr == E_INVALIDARG, "Got unexpected hr %#x.\n", hr);
    if (SUCCEEDED(hr))
        ID3D12Resource_Release(resource);

    /* A texture cannot be created on a UPLOAD heap. */
    heap_properties.Type = D3D12_HEAP_TYPE_UPLOAD;
    hr = ID3D12Device_CreateCommittedResource(device, &heap_properties, D3D12_HEAP_FLAG_NONE,
            &resource_desc, D3D12_RESOURCE_STATE_GENERIC_READ, NULL,
            &IID_ID3D12Resource, (void **)&resource);
    ok(hr == E_INVALIDARG, "Got unexpected hr %#x.\n", hr);

    /* A texture cannot be created on a READBACK heap. */
    heap_properties.Type = D3D12_HEAP_TYPE_READBACK;
    hr = ID3D12Device_CreateCommittedResource(device, &heap_properties, D3D12_HEAP_FLAG_NONE,
            &resource_desc, D3D12_RESOURCE_STATE_COPY_DEST, NULL,
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
    ok(SUCCEEDED(hr), "CreateCommittedResource failed, hr %#x.\n", hr);

    check_interface(resource, &IID_ID3D12Object, TRUE);
    check_interface(resource, &IID_ID3D12DeviceChild, TRUE);
    check_interface(resource, &IID_ID3D12Pageable, TRUE);
    check_interface(resource, &IID_ID3D12Resource, TRUE);

    gpu_address = ID3D12Resource_GetGPUVirtualAddress(resource);
    ok(gpu_address, "Got unexpected GPU virtual address %#"PRIx64".\n", gpu_address);

    refcount = ID3D12Resource_Release(resource);
    ok(!refcount, "ID3D12Resource has %u references left.\n", (unsigned int)refcount);

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
    ok(SUCCEEDED(hr), "CreateCommittedResource failed, hr %#x.\n", hr);
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
    ok(value == 0, "Got unexpected value %"PRIu64".\n", value);

    refcount = ID3D12Fence_Release(fence);
    ok(!refcount, "ID3D12Fence has %u references left.\n", (unsigned int)refcount);

    hr = ID3D12Device_CreateFence(device, 99, D3D12_FENCE_FLAG_NONE,
            &IID_ID3D12Fence, (void **)&fence);
    ok(SUCCEEDED(hr), "CreateFence failed, hr %#x.\n", hr);
    value = ID3D12Fence_GetCompletedValue(fence);
    ok(value == 99, "Got unexpected value %"PRIu64".\n", value);
    refcount = ID3D12Fence_Release(fence);
    ok(!refcount, "ID3D12Fence has %u references left.\n", (unsigned int)refcount);

    refcount = ID3D12Device_Release(device);
    ok(!refcount, "ID3D12Device has %u references left.\n", (unsigned int)refcount);
}

static void test_reset_command_allocator(void)
{
    ID3D12CommandAllocator *command_allocator, *command_allocator2;
    ID3D12GraphicsCommandList *command_list, *command_list2;
    D3D12_COMMAND_QUEUE_DESC command_queue_desc;
    ID3D12CommandQueue *queue;
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

    command_queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    command_queue_desc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
    command_queue_desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    command_queue_desc.NodeMask = 0;
    hr = ID3D12Device_CreateCommandQueue(device, &command_queue_desc,
            &IID_ID3D12CommandQueue, (void **)&queue);
    ok(SUCCEEDED(hr), "CreateCommandQueue failed, hr %#x.\n", hr);
    hr = ID3D12Device_CreateCommandAllocator(device, D3D12_COMMAND_LIST_TYPE_DIRECT,
            &IID_ID3D12CommandAllocator, (void **)&command_allocator2);
    ok(SUCCEEDED(hr), "CreateCommandAllocator failed, hr %#x.\n", hr);

    uav_barrier(command_list, NULL);
    hr = ID3D12GraphicsCommandList_Close(command_list);
    ok(SUCCEEDED(hr), "Close failed, hr %#x.\n", hr);
    exec_command_list(queue, command_list);

    /* A command list can be reset when it is in use. */
    hr = ID3D12GraphicsCommandList_Reset(command_list, command_allocator2, NULL);
    ok(SUCCEEDED(hr), "Resetting command list failed, hr %#x.\n", hr);
    hr = ID3D12GraphicsCommandList_Close(command_list);
    ok(SUCCEEDED(hr), "Close failed, hr %#x.\n", hr);

    wait_queue_idle(device, queue);
    hr = ID3D12CommandAllocator_Reset(command_allocator);
    ok(hr == S_OK, "Got unexpected hr %#x.\n", hr);
    hr = ID3D12GraphicsCommandList_Reset(command_list, command_allocator, NULL);
    ok(SUCCEEDED(hr), "Resetting command list failed, hr %#x.\n", hr);

    uav_barrier(command_list, NULL);
    hr = ID3D12GraphicsCommandList_Close(command_list);
    ok(SUCCEEDED(hr), "Close failed, hr %#x.\n", hr);
    exec_command_list(queue, command_list);

    hr = ID3D12GraphicsCommandList_Reset(command_list, command_allocator, NULL);
    ok(SUCCEEDED(hr), "Resetting command list failed, hr %#x.\n", hr);
    hr = ID3D12GraphicsCommandList_Close(command_list);
    ok(SUCCEEDED(hr), "Close failed, hr %#x.\n", hr);

    wait_queue_idle(device, queue);
    hr = ID3D12CommandAllocator_Reset(command_allocator);
    ok(hr == S_OK, "Got unexpected hr %#x.\n", hr);
    hr = ID3D12GraphicsCommandList_Reset(command_list, command_allocator, NULL);
    ok(SUCCEEDED(hr), "Resetting command list failed, hr %#x.\n", hr);

    /* A command allocator can be used with one command list at a time. */
    hr = ID3D12Device_CreateCommandList(device, 0, D3D12_COMMAND_LIST_TYPE_DIRECT,
            command_allocator, NULL, &IID_ID3D12GraphicsCommandList, (void **)&command_list2);
    ok(hr == E_INVALIDARG, "Got unexpected hr %#x.\n", hr);

    hr = ID3D12Device_CreateCommandList(device, 0, D3D12_COMMAND_LIST_TYPE_DIRECT,
            command_allocator2, NULL, &IID_ID3D12GraphicsCommandList, (void **)&command_list2);
    ok(hr == S_OK, "Got unexpected hr %#x.\n", hr);

    hr = ID3D12GraphicsCommandList_Close(command_list2);
    ok(SUCCEEDED(hr), "Close failed, hr %#x.\n", hr);
    hr = ID3D12GraphicsCommandList_Reset(command_list2, command_allocator, NULL);
    ok(hr == E_INVALIDARG, "Got unexpected hr %#x.\n", hr);

    ID3D12CommandAllocator_Release(command_allocator);
    ID3D12CommandAllocator_Release(command_allocator2);
    ID3D12CommandQueue_Release(queue);
    ID3D12GraphicsCommandList_Release(command_list);
    ID3D12GraphicsCommandList_Release(command_list2);
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
    ok(value == 1, "Got unexpected value %"PRIu64".\n", value);

    hr = ID3D12Fence_Signal(fence, 10);
    ok(SUCCEEDED(hr), "Signal failed, hr %#x.\n", hr);
    value = ID3D12Fence_GetCompletedValue(fence);
    ok(value == 10, "Got unexpected value %"PRIu64".\n", value);

    hr = ID3D12Fence_Signal(fence, 5);
    ok(SUCCEEDED(hr), "Signal failed, hr %#x.\n", hr);
    value = ID3D12Fence_GetCompletedValue(fence);
    ok(value == 5, "Got unexpected value %"PRIu64".\n", value);

    hr = ID3D12Fence_Signal(fence, 0);
    ok(SUCCEEDED(hr), "Signal failed, hr %#x.\n", hr);
    value = ID3D12Fence_GetCompletedValue(fence);
    ok(value == 0, "Got unexpected value %"PRIu64".\n", value);

    /* Basic tests with single event. */
    event1 = create_event();
    ok(!!event1, "Failed to create event.\n");
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
    ok(value == 0, "Got unexpected value %"PRIu64".\n", value);

    event2 = create_event();
    ok(!!event2, "Failed to create event.\n");

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
    ok(value == 20, "Got unexpected value %"PRIu64".\n", value);
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

static void test_gpu_signal_fence(void)
{
    D3D12_COMMAND_QUEUE_DESC command_queue_desc;
    ID3D12CommandQueue *queue;
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

    command_queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    command_queue_desc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
    command_queue_desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    command_queue_desc.NodeMask = 0;
    hr = ID3D12Device_CreateCommandQueue(device, &command_queue_desc,
            &IID_ID3D12CommandQueue, (void **)&queue);
    ok(SUCCEEDED(hr), "CreateCommandQueue failed, hr %#x.\n", hr);

    hr = ID3D12Device_CreateFence(device, 0, D3D12_FENCE_FLAG_NONE,
            &IID_ID3D12Fence, (void **)&fence);
    ok(SUCCEEDED(hr), "CreateFence failed, hr %#x.\n", hr);

    /* XXX: It seems that when a queue is idle a fence is signalled immediately
     * in D3D12. Vulkan implementations don't signal a fence immediately so
     * libvkd3d doesn't as well. In order to make this test reliable
     * wait_queue_idle() is inserted after every ID3D12CommandQueue_Signal(). */
    hr = ID3D12CommandQueue_Signal(queue, fence, 10);
    ok(SUCCEEDED(hr), "Signal failed, hr %#x.\n", hr);
    wait_queue_idle(device, queue);
    value = ID3D12Fence_GetCompletedValue(fence);
    ok(value == 10, "Got unexpected value %"PRIu64".\n", value);

    hr = ID3D12CommandQueue_Signal(queue, fence, 0);
    ok(SUCCEEDED(hr), "Signal failed, hr %#x.\n", hr);
    wait_queue_idle(device, queue);
    value = ID3D12Fence_GetCompletedValue(fence);
    ok(value == 0, "Got unexpected value %"PRIu64".\n", value);

    /* Basic tests with single event. */
    event1 = create_event();
    ok(!!event1, "Failed to create event.\n");
    ret = wait_event(event1, 0);
    ok(ret == WAIT_TIMEOUT, "Got unexpected return value %#x.\n", ret);

    hr = ID3D12Fence_SetEventOnCompletion(fence, 5, event1);
    ok(SUCCEEDED(hr), "SetEventOnCompletion failed, hr %#x.\n", hr);
    ret = wait_event(event1, 0);
    ok(ret == WAIT_TIMEOUT, "Got unexpected return value %#x.\n", ret);
    hr = ID3D12CommandQueue_Signal(queue, fence, 5);
    ok(SUCCEEDED(hr), "Signal failed, hr %#x.\n", hr);
    wait_queue_idle(device, queue);
    ret = wait_event(event1, 0);
    ok(ret == WAIT_OBJECT_0, "Got unexpected return value %#x.\n", ret);
    ret = wait_event(event1, 0);
    ok(ret == WAIT_TIMEOUT, "Got unexpected return value %#x.\n", ret);

    hr = ID3D12Fence_SetEventOnCompletion(fence, 6, event1);
    ok(SUCCEEDED(hr), "SetEventOnCompletion failed, hr %#x.\n", hr);
    ret = wait_event(event1, 0);
    ok(ret == WAIT_TIMEOUT, "Got unexpected return value %#x.\n", ret);
    hr = ID3D12CommandQueue_Signal(queue, fence, 7);
    ok(SUCCEEDED(hr), "Signal failed, hr %#x.\n", hr);
    wait_queue_idle(device, queue);
    ret = wait_event(event1, 0);
    ok(ret == WAIT_OBJECT_0, "Got unexpected return value %#x.\n", ret);
    ret = wait_event(event1, 0);
    ok(ret == WAIT_TIMEOUT, "Got unexpected return value %#x.\n", ret);

    hr = ID3D12CommandQueue_Signal(queue, fence, 10);
    ok(SUCCEEDED(hr), "Signal failed, hr %#x.\n", hr);
    wait_queue_idle(device, queue);
    ret = wait_event(event1, 0);
    ok(ret == WAIT_TIMEOUT, "Got unexpected return value %#x.\n", ret);

    /* Attach one event to multiple values. */
    hr = ID3D12CommandQueue_Signal(queue, fence, 0);
    ok(SUCCEEDED(hr), "Signal failed, hr %#x.\n", hr);
    wait_queue_idle(device, queue);
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
        hr = ID3D12CommandQueue_Signal(queue, fence, i);
        ok(SUCCEEDED(hr), "Signal failed, hr %#x.\n", hr);
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
    hr = ID3D12CommandQueue_Signal(queue, fence, 0);
    ok(SUCCEEDED(hr), "Signal failed, hr %#x.\n", hr);
    wait_queue_idle(device, queue);
    value = ID3D12Fence_GetCompletedValue(fence);
    ok(value == 0, "Got unexpected value %"PRIu64".\n", value);

    event2 = create_event();
    ok(!!event2, "Failed to create event.\n");

    ret = wait_event(event1, 0);
    ok(ret == WAIT_TIMEOUT, "Got unexpected return value %#x.\n", ret);
    ret = wait_event(event2, 0);
    ok(ret == WAIT_TIMEOUT, "Got unexpected return value %#x.\n", ret);
    hr = ID3D12Fence_SetEventOnCompletion(fence, 100, event1);
    ok(SUCCEEDED(hr), "SetEventOnCompletion failed, hr %#x.\n", hr);
    hr = ID3D12Fence_SetEventOnCompletion(fence, ~(UINT64)0, event2);
    ok(SUCCEEDED(hr), "SetEventOnCompletion failed, hr %#x.\n", hr);

    hr = ID3D12CommandQueue_Signal(queue, fence, 50);
    ok(SUCCEEDED(hr), "Signal failed, hr %#x.\n", hr);
    wait_queue_idle(device, queue);
    ret = wait_event(event1, 0);
    ok(ret == WAIT_TIMEOUT, "Got unexpected return value %#x.\n", ret);
    ret = wait_event(event2, 0);
    ok(ret == WAIT_TIMEOUT, "Got unexpected return value %#x.\n", ret);

    hr = ID3D12CommandQueue_Signal(queue, fence, 99);
    ok(SUCCEEDED(hr), "Signal failed, hr %#x.\n", hr);
    wait_queue_idle(device, queue);
    ret = wait_event(event1, 0);
    ok(ret == WAIT_TIMEOUT, "Got unexpected return value %#x.\n", ret);
    ret = wait_event(event2, 0);
    ok(ret == WAIT_TIMEOUT, "Got unexpected return value %#x.\n", ret);

    hr = ID3D12CommandQueue_Signal(queue, fence, 100);
    ok(SUCCEEDED(hr), "Signal failed, hr %#x.\n", hr);
    wait_queue_idle(device, queue);
    ret = wait_event(event1, 0);
    ok(ret == WAIT_OBJECT_0, "Got unexpected return value %#x.\n", ret);
    ret = wait_event(event1, 0);
    ok(ret == WAIT_TIMEOUT, "Got unexpected return value %#x.\n", ret);
    ret = wait_event(event2, 0);
    ok(ret == WAIT_TIMEOUT, "Got unexpected return value %#x.\n", ret);

    hr = ID3D12CommandQueue_Signal(queue, fence, 101);
    ok(SUCCEEDED(hr), "Signal failed, hr %#x.\n", hr);
    wait_queue_idle(device, queue);
    ret = wait_event(event1, 0);
    ok(ret == WAIT_TIMEOUT, "Got unexpected return value %#x.\n", ret);
    ret = wait_event(event2, 0);
    ok(ret == WAIT_TIMEOUT, "Got unexpected return value %#x.\n", ret);

    hr = ID3D12CommandQueue_Signal(queue, fence, 0);
    ok(SUCCEEDED(hr), "Signal failed, hr %#x.\n", hr);
    wait_queue_idle(device, queue);
    ret = wait_event(event1, 0);
    ok(ret == WAIT_TIMEOUT, "Got unexpected return value %#x.\n", ret);
    ret = wait_event(event2, 0);
    ok(ret == WAIT_TIMEOUT, "Got unexpected return value %#x.\n", ret);

    hr = ID3D12CommandQueue_Signal(queue, fence, 100);
    ok(SUCCEEDED(hr), "Signal failed, hr %#x.\n", hr);
    wait_queue_idle(device, queue);
    ret = wait_event(event1, 0);
    ok(ret == WAIT_TIMEOUT, "Got unexpected return value %#x.\n", ret);
    ret = wait_event(event2, 0);
    ok(ret == WAIT_TIMEOUT, "Got unexpected return value %#x.\n", ret);

    hr = ID3D12CommandQueue_Signal(queue, fence, ~(UINT64)0);
    ok(SUCCEEDED(hr), "Signal failed, hr %#x.\n", hr);
    wait_queue_idle(device, queue);
    ret = wait_event(event1, 0);
    ok(ret == WAIT_TIMEOUT, "Got unexpected return value %#x.\n", ret);
    ret = wait_event(event2, 0);
    ok(ret == WAIT_OBJECT_0, "Got unexpected return value %#x.\n", ret);
    ret = wait_event(event2, 0);
    ok(ret == WAIT_TIMEOUT, "Got unexpected return value %#x.\n", ret);

    hr = ID3D12CommandQueue_Signal(queue, fence, ~(UINT64)0);
    ok(SUCCEEDED(hr), "Signal failed, hr %#x.\n", hr);
    wait_queue_idle(device, queue);
    ret = wait_event(event1, 0);
    ok(ret == WAIT_TIMEOUT, "Got unexpected return value %#x.\n", ret);
    ret = wait_event(event2, 0);
    ok(ret == WAIT_TIMEOUT, "Got unexpected return value %#x.\n", ret);
    hr = ID3D12CommandQueue_Signal(queue, fence, 0);
    ok(SUCCEEDED(hr), "Signal failed, hr %#x.\n", hr);
    wait_queue_idle(device, queue);
    ret = wait_event(event1, 0);
    ok(ret == WAIT_TIMEOUT, "Got unexpected return value %#x.\n", ret);
    ret = wait_event(event2, 0);
    ok(ret == WAIT_TIMEOUT, "Got unexpected return value %#x.\n", ret);

    /* Attach two events to the same value. */
    hr = ID3D12CommandQueue_Signal(queue, fence, 0);
    ok(SUCCEEDED(hr), "Signal failed, hr %#x.\n", hr);
    wait_queue_idle(device, queue);
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
    hr = ID3D12CommandQueue_Signal(queue, fence, 3);
    ok(SUCCEEDED(hr), "Signal failed, hr %#x.\n", hr);
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
    UINT64 value;
};

static void fence_event_wait_main(void *untyped_data)
{
    struct multithread_fence_wait_data *data = untyped_data;
    HANDLE event;
    HRESULT hr;
    int ret;

    event = create_event();
    ok(!!event, "Failed to create event.\n");

    hr = ID3D12Fence_SetEventOnCompletion(data->fence, data->value, event);
    ok(SUCCEEDED(hr), "SetEventOnCompletion failed, hr %#x.\n", hr);

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

static void test_multithread_fence_wait(void)
{
    struct multithread_fence_wait_data thread_data;
    D3D12_COMMAND_QUEUE_DESC command_queue_desc;
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

    command_queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    command_queue_desc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
    command_queue_desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    command_queue_desc.NodeMask = 0;
    hr = ID3D12Device_CreateCommandQueue(device, &command_queue_desc,
            &IID_ID3D12CommandQueue, (void **)&queue);
    ok(SUCCEEDED(hr), "CreateCommandQueue failed, hr %#x.\n", hr);

    thread_data.event = create_event();
    thread_data.value = 0;
    ok(!!thread_data.event, "Failed to create event.\n");
    hr = ID3D12Device_CreateFence(device, thread_data.value, D3D12_FENCE_FLAG_NONE,
            &IID_ID3D12Fence, (void **)&thread_data.fence);
    ok(SUCCEEDED(hr), "CreateFence failed, hr %#x.\n", hr);

    /* Signal fence on host. */
    ++thread_data.value;
    thread = create_thread(fence_event_wait_main, &thread_data);
    ok(!!thread, "Failed to create thread.\n");
    ret = wait_event(thread_data.event, INFINITE);
    ok(ret == WAIT_OBJECT_0, "Failed to wait for thread start, return value %#x.\n", ret);

    hr = ID3D12Fence_Signal(thread_data.fence, thread_data.value);
    ok(SUCCEEDED(hr), "Failed to signal fence, hr %#x.\n", hr);

    ok(join_thread(thread), "Failed to join thread.\n");

    ++thread_data.value;
    thread = create_thread(fence_busy_wait_main, &thread_data);
    ok(!!thread, "Failed to create thread.\n");
    ret = wait_event(thread_data.event, INFINITE);
    ok(ret == WAIT_OBJECT_0, "Failed to wait for thread start, return value %#x.\n", ret);

    hr = ID3D12Fence_Signal(thread_data.fence, thread_data.value);
    ok(SUCCEEDED(hr), "Failed to signal fence, hr %#x.\n", hr);

    ok(join_thread(thread), "Failed to join thread.\n");

    /* Signal fence on device. */
    ++thread_data.value;
    thread = create_thread(fence_event_wait_main, &thread_data);
    ok(!!thread, "Failed to create thread.\n");
    ret = wait_event(thread_data.event, INFINITE);
    ok(ret == WAIT_OBJECT_0, "Failed to wait for thread start, return value %#x.\n", ret);

    hr = ID3D12CommandQueue_Signal(queue, thread_data.fence, thread_data.value);
    ok(SUCCEEDED(hr), "Failed to signal fence, hr %#x.\n", hr);

    ok(join_thread(thread), "Failed to join thread.\n");

    ++thread_data.value;
    thread = create_thread(fence_busy_wait_main, &thread_data);
    ok(!!thread, "Failed to create thread.\n");
    ret = wait_event(thread_data.event, INFINITE);
    ok(ret == WAIT_OBJECT_0, "Failed to wait for thread start, return value %#x.\n", ret);

    hr = ID3D12CommandQueue_Signal(queue, thread_data.fence, thread_data.value);
    ok(SUCCEEDED(hr), "Failed to signal fence, hr %#x.\n", hr);

    ok(join_thread(thread), "Failed to join thread.\n");

    destroy_event(thread_data.event);
    ID3D12Fence_Release(thread_data.fence);
    ID3D12CommandQueue_Release(queue);
    refcount = ID3D12Device_Release(device);
    ok(!refcount, "ID3D12Device has %u references left.\n", (unsigned int)refcount);
}

static void test_clear_depth_stencil_view(void)
{
    D3D12_COMMAND_QUEUE_DESC command_queue_desc;
    ID3D12CommandAllocator *command_allocator;
    D3D12_DESCRIPTOR_HEAP_DESC dsv_heap_desc;
    ID3D12GraphicsCommandList *command_list;
    D3D12_CPU_DESCRIPTOR_HANDLE dsv_handle;
    D3D12_HEAP_PROPERTIES heap_properties;
    D3D12_RESOURCE_DESC resource_desc;
    unsigned int dsv_increment_size;
    ID3D12DescriptorHeap *dsv_heap;
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

    dsv_heap_desc.NumDescriptors = 1;
    dsv_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    dsv_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    dsv_heap_desc.NodeMask = 0;
    hr = ID3D12Device_CreateDescriptorHeap(device, &dsv_heap_desc,
            &IID_ID3D12DescriptorHeap, (void **)&dsv_heap);
    ok(SUCCEEDED(hr), "CreateDescriptorHeap failed, hr %#x.\n", hr);

    dsv_increment_size = ID3D12Device_GetDescriptorHandleIncrementSize(device,
            D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
    trace("DSV descriptor handle increment size: %u.\n", dsv_increment_size);
    ok(dsv_increment_size, "Got unexpectd increment size %#x.\n", dsv_increment_size);

    memset(&heap_properties, 0, sizeof(heap_properties));
    heap_properties.Type = D3D12_HEAP_TYPE_DEFAULT;
    resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    resource_desc.Alignment = 0;
    resource_desc.Width = 32;
    resource_desc.Height = 32;
    resource_desc.DepthOrArraySize = 1;
    resource_desc.MipLevels = 1;
    resource_desc.Format = DXGI_FORMAT_D32_FLOAT;
    resource_desc.SampleDesc.Count = 1;
    resource_desc.SampleDesc.Quality = 0;
    resource_desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    resource_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    clear_value.Format = DXGI_FORMAT_D32_FLOAT;
    clear_value.DepthStencil.Depth = 0.5f;
    clear_value.DepthStencil.Stencil = 0x3;
    hr = ID3D12Device_CreateCommittedResource(device, &heap_properties, D3D12_HEAP_FLAG_NONE, &resource_desc,
            D3D12_RESOURCE_STATE_DEPTH_WRITE, &clear_value, &IID_ID3D12Resource, (void **)&resource);
    ok(SUCCEEDED(hr), "CreateCommittedResource failed, hr %#x.\n", hr);

    dsv_handle = ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(dsv_heap);
    ID3D12Device_CreateDepthStencilView(device, resource, NULL, dsv_handle);

    ID3D12GraphicsCommandList_ClearDepthStencilView(command_list, dsv_handle,
            D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 0.75f, 0x7, 0, NULL);
    transition_resource_state(command_list, resource,
            D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_COPY_SOURCE);
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
           ok(v == 0x3f400000, "Got unexpected value 0x%08x at (%u, %u).\n", v, x, y);
        }
    }
    release_resource_readback(&rb);

    ID3D12GraphicsCommandList_Release(command_list);
    ID3D12CommandAllocator_Release(command_allocator);
    ID3D12Resource_Release(resource);
    ID3D12CommandQueue_Release(queue);
    ID3D12DescriptorHeap_Release(dsv_heap);
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
    hr = ID3D12Device_CreateCommittedResource(device,
            &heap_properties, D3D12_HEAP_FLAG_NONE, &resource_desc,
            D3D12_RESOURCE_STATE_RENDER_TARGET, &clear_value,
            &IID_ID3D12Resource, (void **)&resource);
    ok(SUCCEEDED(hr), "CreateCommittedResource failed, hr %#x.\n", hr);

    ID3D12Device_CreateRenderTargetView(device, resource, NULL, rtv_handle);

    ID3D12GraphicsCommandList_ClearRenderTargetView(command_list, rtv_handle, green, 0, NULL);
    transition_resource_state(command_list, resource,
            D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
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
           ok(v == 0xff00ff00, "Got unexpected value 0x%08x at (%u, %u).\n", v, x, y);
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

static void test_draw_instanced(void)
{
    static const float green[] = { 0.0f, 1.0f, 0.0f, 1.0f };
    ID3D12GraphicsCommandList *command_list;
    struct draw_test_context context;
    struct resource_readback rb;
    ID3D12CommandQueue *queue;
    D3D12_VIEWPORT viewport;
    RECT scissor_rect;
    unsigned int x, y;

    if (!init_draw_test_context(&context))
        return;
    command_list = context.list;
    queue = context.queue;

    viewport.TopLeftX = 0.0f;
    viewport.TopLeftY = 0.0f;
    viewport.Width = context.render_target_desc.Width;
    viewport.Height = context.render_target_desc.Height;
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 0.0f;

    scissor_rect.left = scissor_rect.top = 0;
    scissor_rect.right = context.render_target_desc.Width;
    scissor_rect.bottom = context.render_target_desc.Height;

    ID3D12GraphicsCommandList_ClearRenderTargetView(command_list, context.rtv, green, 0, NULL);

    /* This draw call is ignored. */
    ID3D12GraphicsCommandList_DrawInstanced(command_list, 3, 1, 0, 0);

    ID3D12GraphicsCommandList_OMSetRenderTargets(command_list, 1, &context.rtv, FALSE, NULL);
    ID3D12GraphicsCommandList_SetGraphicsRootSignature(command_list, context.root_signature);
    ID3D12GraphicsCommandList_SetPipelineState(command_list, context.pipeline_state);
    ID3D12GraphicsCommandList_IASetPrimitiveTopology(command_list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D12GraphicsCommandList_RSSetViewports(command_list, 1, &viewport);
    ID3D12GraphicsCommandList_RSSetScissorRects(command_list, 1, &scissor_rect);
    ID3D12GraphicsCommandList_DrawInstanced(command_list, 3, 1, 0, 0);

    transition_resource_state(command_list, context.render_target,
            D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);

    get_texture_readback_with_command_list(context.render_target, 0, &rb, queue, command_list);
    for (y = 0; y < context.render_target_desc.Height; ++y)
    {
        for (x = 0; x < context.render_target_desc.Width; ++x)
        {
           unsigned int v = get_readback_uint(&rb, x, y);
           ok(v == 0xffffffff, "Got unexpected value 0x%08x at (%u, %u).\n", v, x, y);
        }
    }
    release_resource_readback(&rb);

    destroy_draw_test_context(&context);
}

static void test_texture_resource_barriers(void)
{
    D3D12_COMMAND_QUEUE_DESC command_queue_desc;
    ID3D12CommandAllocator *command_allocator;
    ID3D12GraphicsCommandList *command_list;
    D3D12_HEAP_PROPERTIES heap_properties;
    D3D12_RESOURCE_BARRIER barriers[8];
    D3D12_RESOURCE_DESC resource_desc;
    ID3D12CommandQueue *queue;
    ID3D12Resource *resource;
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
    ok(SUCCEEDED(hr), "CreateCommandQueue failed, hr %#x.\n", hr);

    hr = ID3D12Device_CreateCommandAllocator(device, D3D12_COMMAND_LIST_TYPE_DIRECT,
            &IID_ID3D12CommandAllocator, (void **)&command_allocator);
    ok(SUCCEEDED(hr), "CreateCommandAllocator failed, hr %#x.\n", hr);

    hr = ID3D12Device_CreateCommandList(device, 0, D3D12_COMMAND_LIST_TYPE_DIRECT,
            command_allocator, NULL, &IID_ID3D12GraphicsCommandList, (void **)&command_list);
    ok(SUCCEEDED(hr), "CreateCommandList failed, hr %#x.\n", hr);

    memset(&heap_properties, 0, sizeof(heap_properties));
    heap_properties.Type = D3D12_HEAP_TYPE_DEFAULT;
    resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    resource_desc.Alignment = 0;
    resource_desc.Width = 32;
    resource_desc.Height = 32;
    resource_desc.DepthOrArraySize = 1;
    resource_desc.MipLevels = 1;
    resource_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM,
    resource_desc.SampleDesc.Count = 1;
    resource_desc.SampleDesc.Quality = 0;
    resource_desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    resource_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    hr = ID3D12Device_CreateCommittedResource(device,
            &heap_properties, D3D12_HEAP_FLAG_NONE, &resource_desc,
            D3D12_RESOURCE_STATE_COMMON, NULL,
            &IID_ID3D12Resource, (void **)&resource);
    ok(SUCCEEDED(hr), "CreateCommittedResource failed, hr %#x.\n", hr);

    barriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barriers[0].Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barriers[0].Transition.pResource = resource;
    barriers[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
    barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    ID3D12GraphicsCommandList_ResourceBarrier(command_list, 1, &barriers[0]);

    barriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    barriers[1].Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barriers[1].UAV.pResource = resource;
    ID3D12GraphicsCommandList_ResourceBarrier(command_list, 1, &barriers[1]);

    barriers[2].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barriers[2].Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barriers[2].Transition.pResource = resource;
    barriers[2].Transition.Subresource = 0;
    barriers[2].Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    barriers[2].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    ID3D12GraphicsCommandList_ResourceBarrier(command_list, 1, &barriers[2]);

    barriers[3].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barriers[3].Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barriers[3].Transition.pResource = resource;
    barriers[3].Transition.Subresource = 0;
    barriers[3].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
    barriers[3].Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE
            | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    ID3D12GraphicsCommandList_ResourceBarrier(command_list, 1, &barriers[3]);

    barriers[4].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barriers[4].Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barriers[4].Transition.pResource = resource;
    barriers[4].Transition.Subresource = 0;
    barriers[4].Transition.StateBefore = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE
            | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    barriers[4].Transition.StateAfter  = D3D12_RESOURCE_STATE_COPY_SOURCE;
    ID3D12GraphicsCommandList_ResourceBarrier(command_list, 1, &barriers[4]);

    barriers[5].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barriers[5].Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barriers[5].Transition.pResource = resource;
    barriers[5].Transition.Subresource = 0;
    barriers[5].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
    barriers[5].Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    ID3D12GraphicsCommandList_ResourceBarrier(command_list, 1, &barriers[5]);

    barriers[6].Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    barriers[6].Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barriers[6].UAV.pResource = resource;
    ID3D12GraphicsCommandList_ResourceBarrier(command_list, 1, &barriers[6]);
    ID3D12GraphicsCommandList_ResourceBarrier(command_list, 1, &barriers[6]);

    barriers[7].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barriers[7].Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barriers[7].Transition.pResource = resource;
    barriers[7].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barriers[7].Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    barriers[7].Transition.StateAfter = D3D12_RESOURCE_STATE_COMMON;
    ID3D12GraphicsCommandList_ResourceBarrier(command_list, 1, &barriers[7]);

    ID3D12GraphicsCommandList_ResourceBarrier(command_list, 8, barriers);

    hr = ID3D12GraphicsCommandList_Close(command_list);
    ok(SUCCEEDED(hr), "Close failed, hr %#x.\n", hr);
    exec_command_list(queue, command_list);
    wait_queue_idle(device, queue);

    ID3D12GraphicsCommandList_Release(command_list);
    ID3D12CommandAllocator_Release(command_allocator);
    ID3D12Resource_Release(resource);
    ID3D12CommandQueue_Release(queue);
    refcount = ID3D12Device_Release(device);
    ok(!refcount, "ID3D12Device has %u references left.\n", (unsigned int)refcount);
}

static void test_invalid_texture_resource_barriers(void)
{
    ID3D12Resource *texture, *readback_buffer, *upload_buffer;
    D3D12_COMMAND_QUEUE_DESC command_queue_desc;
    ID3D12CommandAllocator *command_allocator;
    ID3D12GraphicsCommandList *command_list;
    D3D12_HEAP_PROPERTIES heap_properties;
    D3D12_RESOURCE_DESC resource_desc;
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
    ok(SUCCEEDED(hr), "CreateCommandQueue failed, hr %#x.\n", hr);

    hr = ID3D12Device_CreateCommandAllocator(device, D3D12_COMMAND_LIST_TYPE_DIRECT,
            &IID_ID3D12CommandAllocator, (void **)&command_allocator);
    ok(SUCCEEDED(hr), "CreateCommandAllocator failed, hr %#x.\n", hr);

    hr = ID3D12Device_CreateCommandList(device, 0, D3D12_COMMAND_LIST_TYPE_DIRECT,
            command_allocator, NULL, &IID_ID3D12GraphicsCommandList, (void **)&command_list);
    ok(SUCCEEDED(hr), "CreateCommandList failed, hr %#x.\n", hr);

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
    resource_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    hr = ID3D12Device_CreateCommittedResource(device,
            &heap_properties, D3D12_HEAP_FLAG_NONE, &resource_desc,
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, NULL,
            &IID_ID3D12Resource, (void **)&texture);
    ok(SUCCEEDED(hr), "CreateCommittedResource failed, hr %#x.\n", hr);

    resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    resource_desc.Height = 1;
    resource_desc.Format = DXGI_FORMAT_UNKNOWN;
    resource_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    resource_desc.Flags = 0;

    heap_properties.Type = D3D12_HEAP_TYPE_UPLOAD;
    hr = ID3D12Device_CreateCommittedResource(device,
            &heap_properties, D3D12_HEAP_FLAG_NONE, &resource_desc,
            D3D12_RESOURCE_STATE_GENERIC_READ, NULL,
            &IID_ID3D12Resource, (void **)&upload_buffer);
    ok(SUCCEEDED(hr), "CreateCommittedResource failed, hr %#x.\n", hr);

    heap_properties.Type = D3D12_HEAP_TYPE_READBACK;
    hr = ID3D12Device_CreateCommittedResource(device,
            &heap_properties, D3D12_HEAP_FLAG_NONE, &resource_desc,
            D3D12_RESOURCE_STATE_COPY_DEST, NULL,
            &IID_ID3D12Resource, (void **)&readback_buffer);
    ok(SUCCEEDED(hr), "CreateCommittedResource failed, hr %#x.\n", hr);

    /* The following invalid barrier is not detected by the runtime. */
    transition_resource_state(command_list, texture,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
    hr = ID3D12GraphicsCommandList_Close(command_list);
    ok(SUCCEEDED(hr), "Close failed, hr %#x.\n", hr);

    hr = ID3D12CommandAllocator_Reset(command_allocator);
    ok(SUCCEEDED(hr), "Command allocator reset failed, hr %#x.\n", hr);
    hr = ID3D12GraphicsCommandList_Reset(command_list, command_allocator, NULL);
    ok(SUCCEEDED(hr), "Command list reset failed, hr %#x.\n", hr);

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
    ok(SUCCEEDED(hr), "Command allocator reset failed, hr %#x.\n", hr);
    hr = ID3D12Device_CreateCommandList(device, 0, D3D12_COMMAND_LIST_TYPE_DIRECT,
            command_allocator, NULL, &IID_ID3D12GraphicsCommandList, (void **)&command_list);
    ok(SUCCEEDED(hr), "CreateCommandList failed, hr %#x.\n", hr);

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
    ok(SUCCEEDED(hr), "Command allocator reset failed, hr %#x.\n", hr);
    ID3D12GraphicsCommandList_Release(command_list);
    hr = ID3D12Device_CreateCommandList(device, 0, D3D12_COMMAND_LIST_TYPE_DIRECT,
            command_allocator, NULL, &IID_ID3D12GraphicsCommandList, (void **)&command_list);
    ok(SUCCEEDED(hr), "CreateCommandList failed, hr %#x.\n", hr);

    /* Exactly one write state or a combination of read-only states are allowed. */
    transition_resource_state(command_list, texture,
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    hr = ID3D12GraphicsCommandList_Close(command_list);
    ok(hr == E_INVALIDARG, "Got unexpected hr %#x.\n", hr);

    hr = ID3D12CommandAllocator_Reset(command_allocator);
    ok(SUCCEEDED(hr), "Command allocator reset failed, hr %#x.\n", hr);
    ID3D12GraphicsCommandList_Release(command_list);
    hr = ID3D12Device_CreateCommandList(device, 0, D3D12_COMMAND_LIST_TYPE_DIRECT,
            command_allocator, NULL, &IID_ID3D12GraphicsCommandList, (void **)&command_list);
    ok(SUCCEEDED(hr), "CreateCommandList failed, hr %#x.\n", hr);

    /* Readback resources cannot transition from D3D12_RESOURCE_STATE_COPY_DEST. */
    transition_resource_state(command_list, readback_buffer,
            D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON);
    hr = ID3D12GraphicsCommandList_Close(command_list);
    todo(hr == E_INVALIDARG, "Got unexpected hr %#x.\n", hr);

    hr = ID3D12CommandAllocator_Reset(command_allocator);
    ok(SUCCEEDED(hr), "Command allocator reset failed, hr %#x.\n", hr);
    ID3D12GraphicsCommandList_Release(command_list);
    hr = ID3D12Device_CreateCommandList(device, 0, D3D12_COMMAND_LIST_TYPE_DIRECT,
            command_allocator, NULL, &IID_ID3D12GraphicsCommandList, (void **)&command_list);
    ok(SUCCEEDED(hr), "CreateCommandList failed, hr %#x.\n", hr);

    /* Upload resources cannot transition from D3D12_RESOURCE_STATE_GENERIC_READ. */
    transition_resource_state(command_list, upload_buffer,
            D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_STATE_COMMON);
    hr = ID3D12GraphicsCommandList_Close(command_list);
    todo(hr == E_INVALIDARG, "Got unexpected hr %#x.\n", hr);

    ID3D12CommandAllocator_Release(command_allocator);
    ID3D12CommandQueue_Release(queue);
    ID3D12GraphicsCommandList_Release(command_list);
    ID3D12Resource_Release(readback_buffer);
    ID3D12Resource_Release(texture);
    ID3D12Resource_Release(upload_buffer);
    refcount = ID3D12Device_Release(device);
    ok(!refcount, "ID3D12Device has %u references left.\n", (unsigned int)refcount);
}

static void test_device_removed_reason(void)
{
    D3D12_COMMAND_QUEUE_DESC command_queue_desc;
    ID3D12CommandAllocator *command_allocator;
    ID3D12GraphicsCommandList *command_list;
    ID3D12CommandQueue *queue, *tmp_queue;
    ID3D12Device *device;
    ULONG refcount;
    HRESULT hr;

    if (!(device = create_device()))
    {
        skip("Failed to create device.\n");
        return;
    }

    hr = ID3D12Device_GetDeviceRemovedReason(device);
    ok(hr == S_OK, "Got unexpected hr %#x.\n", hr);

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

    /* Execute a command list in the recording state. */
    exec_command_list(queue, command_list);

    hr = ID3D12Device_GetDeviceRemovedReason(device);
    todo(hr == DXGI_ERROR_INVALID_CALL, "Got unexpected hr %#x.\n", hr);

    hr = ID3D12Device_CreateCommandQueue(device, &command_queue_desc,
            &IID_ID3D12CommandQueue, (void **)&tmp_queue);
    todo(hr == DXGI_ERROR_DEVICE_REMOVED, "Got unexpected hr %#x.\n", hr);
    if (SUCCEEDED(hr))
        ID3D12CommandQueue_Release(tmp_queue);

    hr = ID3D12Device_GetDeviceRemovedReason(device);
    todo(hr == DXGI_ERROR_INVALID_CALL, "Got unexpected hr %#x.\n", hr);

    ID3D12GraphicsCommandList_Release(command_list);
    ID3D12CommandAllocator_Release(command_allocator);
    ID3D12CommandQueue_Release(queue);
    refcount = ID3D12Device_Release(device);
    ok(!refcount, "ID3D12Device has %u references left.\n", (unsigned int)refcount);
}

static void test_map_resource(void)
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
    ok(SUCCEEDED(hr), "CreateCommittedResource failed, hr %#x.\n", hr);

    /* Resources on a DEFAULT heap cannot be mapped. */
    hr = ID3D12Resource_Map(resource, 0, NULL, &data);
    ok(hr == E_INVALIDARG, "Got unexpected hr %#x.\n", hr);

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
        hr = ID3D12Resource_Map(resource, 0, NULL, &data);
        ok(hr == E_INVALIDARG, "Got unexpected hr %#x.\n", hr);

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
    ok(SUCCEEDED(hr), "CreateCommittedResource failed, hr %#x.\n", hr);

    /* Resources on a DEFAULT heap cannot be mapped. */
    hr = ID3D12Resource_Map(resource, 0, NULL, &data);
    ok(hr == E_INVALIDARG, "Got unexpected hr %#x.\n", hr);

    ID3D12Resource_Release(resource);

    refcount = ID3D12Device_Release(device);
    ok(!refcount, "ID3D12Device has %u references left.\n", (unsigned int)refcount);
}

static void test_bundle_state_inheritance(void)
{
    static const float green[] = { 0.0f, 1.0f, 0.0f, 1.0f };
    ID3D12GraphicsCommandList *command_list, *bundle;
    ID3D12CommandAllocator *bundle_allocator;
    struct draw_test_context context;
    struct resource_readback rb;
    ID3D12CommandQueue *queue;
    D3D12_VIEWPORT viewport;
    ID3D12Device *device;
    RECT scissor_rect;
    unsigned int x, y;
    HRESULT hr;

#ifndef _WIN32
    /* Avoid 2048 test todos. */
    skip("Bundles are not implemented yet.\n");
    return;
#endif

    if (!init_draw_test_context(&context))
        return;
    device = context.device;
    command_list = context.list;
    queue = context.queue;

    hr = ID3D12Device_CreateCommandAllocator(device, D3D12_COMMAND_LIST_TYPE_BUNDLE,
            &IID_ID3D12CommandAllocator, (void **)&bundle_allocator);
    ok(SUCCEEDED(hr), "CreateCommandAllocator failed, hr %#x.\n", hr);
    hr = ID3D12Device_CreateCommandList(device, 0, D3D12_COMMAND_LIST_TYPE_BUNDLE,
            bundle_allocator, NULL, &IID_ID3D12GraphicsCommandList, (void **)&bundle);
    ok(SUCCEEDED(hr), "CreateCommandList failed, hr %#x.\n", hr);

    viewport.TopLeftX = 0.0f;
    viewport.TopLeftY = 0.0f;
    viewport.Width = context.render_target_desc.Width;
    viewport.Height = context.render_target_desc.Height;
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 0.0f;

    scissor_rect.left = scissor_rect.top = 0;
    scissor_rect.right = context.render_target_desc.Width;
    scissor_rect.bottom = context.render_target_desc.Height;

    /* A bundle does not inherit the current pipeline state. */
    ID3D12GraphicsCommandList_ClearRenderTargetView(command_list, context.rtv, green, 0, NULL);

    ID3D12GraphicsCommandList_OMSetRenderTargets(command_list, 1, &context.rtv, FALSE, NULL);
    ID3D12GraphicsCommandList_SetGraphicsRootSignature(command_list, context.root_signature);
    ID3D12GraphicsCommandList_SetPipelineState(command_list, context.pipeline_state);
    ID3D12GraphicsCommandList_IASetPrimitiveTopology(command_list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D12GraphicsCommandList_RSSetViewports(command_list, 1, &viewport);
    ID3D12GraphicsCommandList_RSSetScissorRects(command_list, 1, &scissor_rect);

    ID3D12GraphicsCommandList_DrawInstanced(bundle, 3, 1, 0, 0);
    hr = ID3D12GraphicsCommandList_Close(bundle);
    ok(SUCCEEDED(hr), "Failed to close bundle, hr %#x.\n", hr);

    ID3D12GraphicsCommandList_ExecuteBundle(command_list, bundle);

    transition_resource_state(command_list, context.render_target,
            D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);

    get_texture_readback_with_command_list(context.render_target, 0, &rb, queue, command_list);
    for (y = 0; y < context.render_target_desc.Height; ++y)
    {
        for (x = 0; x < context.render_target_desc.Width; ++x)
        {
           unsigned int v = get_readback_uint(&rb, x, y);
           /* This works on AMD. */
           ok(v == 0xff00ff00 || v == 0xffffffff, "Got unexpected value 0x%08x at (%u, %u).\n", v, x, y);
        }
    }
    release_resource_readback(&rb);

    hr = ID3D12CommandAllocator_Reset(context.allocator);
    ok(SUCCEEDED(hr), "Command allocator reset failed, hr %#x.\n", hr);
    hr = ID3D12GraphicsCommandList_Reset(command_list, context.allocator, NULL);
    ok(SUCCEEDED(hr), "Command list reset failed, hr %#x.\n", hr);
    hr = ID3D12CommandAllocator_Reset(bundle_allocator);
    ok(SUCCEEDED(hr), "Command allocator reset failed, hr %#x.\n", hr);
    hr = ID3D12GraphicsCommandList_Reset(bundle, bundle_allocator, NULL);
    ok(SUCCEEDED(hr), "Command list reset failed, hr %#x.\n", hr);

    /* A bundle does not inherit the current primitive topology. */
    transition_resource_state(command_list, context.render_target,
            D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
    ID3D12GraphicsCommandList_ClearRenderTargetView(command_list, context.rtv, green, 0, NULL);

    ID3D12GraphicsCommandList_OMSetRenderTargets(command_list, 1, &context.rtv, FALSE, NULL);
    ID3D12GraphicsCommandList_SetGraphicsRootSignature(command_list, context.root_signature);
    ID3D12GraphicsCommandList_SetPipelineState(command_list, context.pipeline_state);
    ID3D12GraphicsCommandList_IASetPrimitiveTopology(command_list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D12GraphicsCommandList_RSSetViewports(command_list, 1, &viewport);
    ID3D12GraphicsCommandList_RSSetScissorRects(command_list, 1, &scissor_rect);

    ID3D12GraphicsCommandList_SetPipelineState(bundle, context.pipeline_state);
    ID3D12GraphicsCommandList_DrawInstanced(bundle, 3, 1, 0, 0);
    hr = ID3D12GraphicsCommandList_Close(bundle);
    ok(SUCCEEDED(hr), "Failed to close bundle, hr %#x.\n", hr);

    ID3D12GraphicsCommandList_ExecuteBundle(command_list, bundle);

    transition_resource_state(command_list, context.render_target,
            D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);

    get_texture_readback_with_command_list(context.render_target, 0, &rb, queue, command_list);
    for (y = 0; y < context.render_target_desc.Height; ++y)
    {
        for (x = 0; x < context.render_target_desc.Width; ++x)
        {
           unsigned int v = get_readback_uint(&rb, x, y);
           /* This works on AMD, even though the debug layer says that the primitive topology is undefined. */
           ok(v == 0xff00ff00 || v == 0xffffffff, "Got unexpected value 0x%08x at (%u, %u).\n", v, x, y);
        }
    }
    release_resource_readback(&rb);

    hr = ID3D12CommandAllocator_Reset(context.allocator);
    ok(SUCCEEDED(hr), "Command allocator reset failed, hr %#x.\n", hr);
    hr = ID3D12GraphicsCommandList_Reset(command_list, context.allocator, NULL);
    ok(SUCCEEDED(hr), "Command list reset failed, hr %#x.\n", hr);
    hr = ID3D12CommandAllocator_Reset(bundle_allocator);
    ok(SUCCEEDED(hr), "Command allocator reset failed, hr %#x.\n", hr);
    hr = ID3D12GraphicsCommandList_Reset(bundle, bundle_allocator, NULL);
    ok(SUCCEEDED(hr), "Command list reset failed, hr %#x.\n", hr);

    /* A bundle inherit all other states. */
    transition_resource_state(command_list, context.render_target,
            D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
    ID3D12GraphicsCommandList_ClearRenderTargetView(command_list, context.rtv, green, 0, NULL);

    ID3D12GraphicsCommandList_OMSetRenderTargets(command_list, 1, &context.rtv, FALSE, NULL);
    ID3D12GraphicsCommandList_SetGraphicsRootSignature(command_list, context.root_signature);
    ID3D12GraphicsCommandList_RSSetViewports(command_list, 1, &viewport);
    ID3D12GraphicsCommandList_RSSetScissorRects(command_list, 1, &scissor_rect);

    ID3D12GraphicsCommandList_SetPipelineState(bundle, context.pipeline_state);
    ID3D12GraphicsCommandList_IASetPrimitiveTopology(bundle, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D12GraphicsCommandList_DrawInstanced(bundle, 3, 1, 0, 0);
    hr = ID3D12GraphicsCommandList_Close(bundle);
    ok(SUCCEEDED(hr), "Failed to close bundle, hr %#x.\n", hr);

    ID3D12GraphicsCommandList_ExecuteBundle(command_list, bundle);

    transition_resource_state(command_list, context.render_target,
            D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);

    get_texture_readback_with_command_list(context.render_target, 0, &rb, queue, command_list);
    for (y = 0; y < context.render_target_desc.Height; ++y)
    {
        for (x = 0; x < context.render_target_desc.Width; ++x)
        {
           unsigned int v = get_readback_uint(&rb, x, y);
           todo(v == 0xffffffff, "Got unexpected value 0x%08x at (%u, %u).\n", v, x, y);
        }
    }
    release_resource_readback(&rb);

    hr = ID3D12CommandAllocator_Reset(context.allocator);
    ok(SUCCEEDED(hr), "Command allocator reset failed, hr %#x.\n", hr);
    hr = ID3D12GraphicsCommandList_Reset(command_list, context.allocator, NULL);
    ok(SUCCEEDED(hr), "Command list reset failed, hr %#x.\n", hr);
    hr = ID3D12CommandAllocator_Reset(bundle_allocator);
    ok(SUCCEEDED(hr), "Command allocator reset failed, hr %#x.\n", hr);
    hr = ID3D12GraphicsCommandList_Reset(bundle, bundle_allocator, NULL);
    ok(SUCCEEDED(hr), "Command list reset failed, hr %#x.\n", hr);

    /* All state that is set in a bundle affects a command list. */
    transition_resource_state(command_list, context.render_target,
            D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
    ID3D12GraphicsCommandList_ClearRenderTargetView(command_list, context.rtv, green, 0, NULL);
    ID3D12GraphicsCommandList_OMSetRenderTargets(command_list, 1, &context.rtv, FALSE, NULL);
    ID3D12GraphicsCommandList_RSSetViewports(command_list, 1, &viewport);
    ID3D12GraphicsCommandList_RSSetScissorRects(command_list, 1, &scissor_rect);

    ID3D12GraphicsCommandList_SetGraphicsRootSignature(bundle, context.root_signature);
    ID3D12GraphicsCommandList_SetPipelineState(bundle, context.pipeline_state);
    ID3D12GraphicsCommandList_IASetPrimitiveTopology(bundle, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    hr = ID3D12GraphicsCommandList_Close(bundle);
    ok(SUCCEEDED(hr), "Failed to close bundle, hr %#x.\n", hr);

    ID3D12GraphicsCommandList_ExecuteBundle(command_list, bundle);

    ID3D12GraphicsCommandList_DrawInstanced(command_list, 3, 1, 0, 0);

    transition_resource_state(command_list, context.render_target,
            D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);

    get_texture_readback_with_command_list(context.render_target, 0, &rb, queue, command_list);
    for (y = 0; y < context.render_target_desc.Height; ++y)
    {
        for (x = 0; x < context.render_target_desc.Width; ++x)
        {
           unsigned int v = get_readback_uint(&rb, x, y);
           todo(v == 0xffffffff, "Got unexpected value 0x%08x at (%u, %u).\n", v, x, y);
        }
    }
    release_resource_readback(&rb);

    ID3D12CommandAllocator_Release(bundle_allocator);
    ID3D12GraphicsCommandList_Release(bundle);
    destroy_draw_test_context(&context);
}

START_TEST(d3d12)
{
    BOOL enable_debug_layer = FALSE;
    ID3D12Debug *debug;

    if (argc >= 2 && !strcmp(argv[1], "--validate"))
        enable_debug_layer = TRUE;

    if (enable_debug_layer && SUCCEEDED(D3D12GetDebugInterface(&IID_ID3D12Debug, (void **)&debug)))
    {
        ID3D12Debug_EnableDebugLayer(debug);
        ID3D12Debug_Release(debug);
    }

    run_test(test_create_device);
    run_test(test_node_count);
    run_test(test_check_feature_support);
    run_test(test_create_command_allocator);
    run_test(test_create_command_list);
    run_test(test_create_command_queue);
    run_test(test_create_committed_resource);
    run_test(test_create_descriptor_heap);
    run_test(test_create_root_signature);
    run_test(test_create_pipeline_state);
    run_test(test_create_fence);
    run_test(test_reset_command_allocator);
    run_test(test_cpu_signal_fence);
    run_test(test_gpu_signal_fence);
    run_test(test_multithread_fence_wait);
    run_test(test_clear_depth_stencil_view);
    run_test(test_clear_render_target_view);
    run_test(test_draw_instanced);
    run_test(test_texture_resource_barriers);
    run_test(test_invalid_texture_resource_barriers);
    run_test(test_device_removed_reason);
    run_test(test_map_resource);
    run_test(test_bundle_state_inheritance);
}
