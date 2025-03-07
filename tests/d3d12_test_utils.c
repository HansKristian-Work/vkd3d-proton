/*
 * Copyright 2016-2017 Józef Kucia for CodeWeavers
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

PFN_D3D12_CREATE_VERSIONED_ROOT_SIGNATURE_DESERIALIZER pfn_D3D12CreateVersionedRootSignatureDeserializer;
PFN_D3D12_SERIALIZE_VERSIONED_ROOT_SIGNATURE pfn_D3D12SerializeVersionedRootSignature;
PFN_D3D12_CREATE_DEVICE pfn_D3D12CreateDevice;
PFN_D3D12_ENABLE_EXPERIMENTAL_FEATURES pfn_D3D12EnableExperimentalFeatures;
PFN_D3D12_GET_DEBUG_INTERFACE pfn_D3D12GetDebugInterface;
PFN_D3D12_GET_INTERFACE pfn_D3D12GetInterface;
D3D_FEATURE_LEVEL vkd3d_device_feature_level = D3D_FEATURE_LEVEL_11_0;
const char *vkd3d_test_platform = "other";
struct vkd3d_test_state_context vkd3d_test_state;
bool use_warp_device;
unsigned int use_adapter_idx;

#ifdef _WIN32
RENDERDOC_API_1_0_0 *renderdoc_api;
#endif

bool compare_float(float f, float g, int ulps)
{
    int x, y;
    union
    {
        float f;
        int i;
    } u;

    u.f = f;
    x = u.i;
    u.f = g;
    y = u.i;

    if (x < 0)
        x = INT_MIN - x;
    if (y < 0)
        y = INT_MIN - y;

    if (abs(x - y) > ulps)
        return false;

    return true;
}

bool compare_vec4(const struct vec4 *v1, const struct vec4 *v2, unsigned int ulps)
{
    return compare_float(v1->x, v2->x, ulps)
            && compare_float(v1->y, v2->y, ulps)
            && compare_float(v1->z, v2->z, ulps)
            && compare_float(v1->w, v2->w, ulps);
}

bool compare_uvec4(const struct uvec4* v1, const struct uvec4 *v2)
{
    return v1->x == v2->x && v1->y == v2->y && v1->z == v2->z && v1->w == v2->w;
}

bool compare_uint8(uint8_t a, uint8_t b, unsigned int max_diff)
{
    return delta_uint8(a, b) <= max_diff;
}

bool compare_uint16(uint16_t a, uint16_t b, unsigned int max_diff)
{
    return delta_uint16(a, b) <= max_diff;
}

bool compare_uint64(uint64_t a, uint64_t b, unsigned int max_diff)
{
    return delta_uint64(a, b) <= max_diff;
}

ULONG get_refcount(void *iface)
{
    IUnknown *unk = iface;
    IUnknown_AddRef(unk);
    return IUnknown_Release(unk);
}

void check_interface_(unsigned int line, IUnknown *iface, REFIID riid, bool supported)
{
    HRESULT hr, expected_hr;
    IUnknown *unk;

    expected_hr = supported ? S_OK : E_NOINTERFACE;

    hr = IUnknown_QueryInterface(iface, riid, (void **)&unk);
    ok_(line)(hr == expected_hr, "Got hr %#x, expected %#x.\n", hr, expected_hr);
    if (SUCCEEDED(hr))
        IUnknown_Release(unk);
}

void check_heap_properties_(unsigned int line,
        const D3D12_HEAP_PROPERTIES *properties, const D3D12_HEAP_PROPERTIES *expected_properties)
{
    D3D12_HEAP_PROPERTIES expected = *expected_properties;

    if (!expected.CreationNodeMask)
        expected.CreationNodeMask = 0x1;
    if (!expected.VisibleNodeMask)
        expected.VisibleNodeMask = 0x1;

    ok_(line)(properties->Type == expected.Type,
            "Got type %#x, expected %#x.\n", properties->Type, expected.Type);
    ok_(line)(properties->CPUPageProperty == expected.CPUPageProperty,
            "Got CPU page properties %#x, expected %#x.\n",
            properties->CPUPageProperty, expected.CPUPageProperty);
    ok_(line)(properties->MemoryPoolPreference == expected.MemoryPoolPreference,
            "Got memory pool %#x, expected %#x.\n",
            properties->MemoryPoolPreference, expected.MemoryPoolPreference);
    ok_(line)(properties->CreationNodeMask == expected.CreationNodeMask,
            "Got creation node mask %#x, expected %#x.\n",
            properties->CreationNodeMask, expected.CreationNodeMask);
    ok_(line)(properties->VisibleNodeMask == expected.VisibleNodeMask,
            "Got visible node mask %#x, expected %#x.\n",
            properties->VisibleNodeMask, expected.VisibleNodeMask);
}

void check_heap_desc_(unsigned int line, const D3D12_HEAP_DESC *desc,
        const D3D12_HEAP_DESC *expected_desc)
{
    D3D12_HEAP_DESC expected = *expected_desc;

    if (!expected.Alignment)
        expected.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;

    ok_(line)(desc->SizeInBytes == expected.SizeInBytes,
            "Got size %"PRIu64", expected %"PRIu64".\n",
            desc->SizeInBytes, expected.SizeInBytes);
    check_heap_properties_(line, &desc->Properties, &expected.Properties);
    ok_(line)(desc->Alignment == expected.Alignment,
            "Got alignment %"PRIu64", expected %"PRIu64".\n",
            desc->Alignment, expected.Alignment);
    ok_(line)(desc->Flags == expected.Flags,
            "Got flags %#x, expected %#x.\n", desc->Flags, expected.Flags);
}

void check_alignment_(unsigned int line, uint64_t size, uint64_t alignment)
{
    uint64_t aligned_size = align(size, alignment);
    ok_(line)(aligned_size == size, "Got unaligned size %"PRIu64", expected %"PRIu64".\n",
            size, aligned_size);
}

void uav_barrier(ID3D12GraphicsCommandList *list, ID3D12Resource *resource)
{
    D3D12_RESOURCE_BARRIER barrier;

    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier.UAV.pResource = resource;

    ID3D12GraphicsCommandList_ResourceBarrier(list, 1, &barrier);
}

void copy_sub_resource_data(const D3D12_MEMCPY_DEST *dst, const D3D12_SUBRESOURCE_DATA *src,
        unsigned int row_count, unsigned int slice_count, size_t row_size)
{
    const BYTE *src_slice_ptr;
    BYTE *dst_slice_ptr;
    unsigned int z, y;

    for (z = 0; z < slice_count; ++z)
    {
        dst_slice_ptr = (BYTE *)dst->pData + z * dst->SlicePitch;
        src_slice_ptr = (const BYTE*)src->pData + z * src->SlicePitch;
        for (y = 0; y < row_count; ++y)
            memcpy(dst_slice_ptr + y * dst->RowPitch, src_slice_ptr + y * src->RowPitch, row_size);
    }
}

void upload_buffer_data_(unsigned int line, ID3D12Resource *buffer, size_t offset,
        size_t size, const void *data, ID3D12CommandQueue *queue, ID3D12GraphicsCommandList *command_list)
{
    ID3D12Resource *upload_buffer;
    ID3D12Device *device;
    HRESULT hr;

    hr = ID3D12Resource_GetDevice(buffer, &IID_ID3D12Device, (void **)&device);
    ok_(line)(SUCCEEDED(hr), "Failed to get device, hr %#x.\n", hr);

    upload_buffer = create_upload_buffer_(line, device, size, data);

    ID3D12GraphicsCommandList_CopyBufferRegion(command_list, buffer, offset,
            upload_buffer, 0, size);

    hr = ID3D12GraphicsCommandList_Close(command_list);
    ok_(line)(SUCCEEDED(hr), "Failed to close command list, hr %#x.\n", hr);
    exec_command_list(queue, command_list);
    wait_queue_idle(device, queue);

    ID3D12Resource_Release(upload_buffer);
    ID3D12Device_Release(device);
}

void upload_texture_data_base_(unsigned int line, ID3D12Resource *texture,
        const D3D12_SUBRESOURCE_DATA *data, unsigned int first_subresource, unsigned int sub_resource_count,
        ID3D12CommandQueue *queue, ID3D12GraphicsCommandList *command_list)
{
    D3D12_TEXTURE_COPY_LOCATION dst_location, src_location;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT *layouts;
    D3D12_RESOURCE_DESC resource_desc;
    UINT64 *row_sizes, required_size;
    ID3D12Resource *upload_buffer;
    D3D12_MEMCPY_DEST dst_data;
    ID3D12Device *device;
    UINT *row_counts;
    unsigned int i;
    HRESULT hr;
    void *ptr;

    layouts = calloc(sub_resource_count, sizeof(*layouts));
    ok(layouts, "Failed to allocate memory.\n");
    row_counts = calloc(sub_resource_count, sizeof(*row_counts));
    ok(row_counts, "Failed to allocate memory.\n");
    row_sizes = calloc(sub_resource_count, sizeof(*row_sizes));
    ok(row_sizes, "Failed to allocate memory.\n");

    resource_desc = ID3D12Resource_GetDesc(texture);
    hr = ID3D12Resource_GetDevice(texture, &IID_ID3D12Device, (void **)&device);
    ok_(line)(SUCCEEDED(hr), "Failed to get device, hr %#x.\n", hr);

    ID3D12Device_GetCopyableFootprints(device, &resource_desc, first_subresource, sub_resource_count,
            0, layouts, row_counts, row_sizes, &required_size);

    upload_buffer = create_upload_buffer_(line, device, required_size, NULL);

    hr = ID3D12Resource_Map(upload_buffer, 0, NULL, (void **)&ptr);
    ok_(line)(SUCCEEDED(hr), "Failed to map upload buffer, hr %#x.\n", hr);
    for (i = 0; i < sub_resource_count; ++i)
    {
        dst_data.pData = (BYTE *)ptr + layouts[i].Offset;
        dst_data.RowPitch = layouts[i].Footprint.RowPitch;
        dst_data.SlicePitch = layouts[i].Footprint.RowPitch * row_counts[i];
        copy_sub_resource_data(&dst_data, &data[i],
                row_counts[i], layouts[i].Footprint.Depth, row_sizes[i]);
    }
    ID3D12Resource_Unmap(upload_buffer, 0, NULL);

    for (i = 0; i < sub_resource_count; ++i)
    {
        dst_location.pResource = texture;
        dst_location.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        dst_location.SubresourceIndex = i + first_subresource;

        src_location.pResource = upload_buffer;
        src_location.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        src_location.PlacedFootprint = layouts[i];

        ID3D12GraphicsCommandList_CopyTextureRegion(command_list,
                &dst_location, 0, 0, 0, &src_location, NULL);
    }

    hr = ID3D12GraphicsCommandList_Close(command_list);
    ok_(line)(SUCCEEDED(hr), "Failed to close command list, hr %#x.\n", hr);

    exec_command_list(queue, command_list);
    wait_queue_idle(device, queue);

    ID3D12Resource_Release(upload_buffer);
    ID3D12Device_Release(device);

    free(layouts);
    free(row_counts);
    free(row_sizes);
}

void upload_texture_data_(unsigned int line, ID3D12Resource *texture,
    const D3D12_SUBRESOURCE_DATA *data, unsigned int sub_resource_count,
    ID3D12CommandQueue *queue, ID3D12GraphicsCommandList *command_list)
{
    upload_texture_data_base_(line, texture, data, 0, sub_resource_count, queue, command_list);
}

void init_readback(struct resource_readback *rb, ID3D12Resource *buffer,
        uint64_t buffer_size, uint64_t width, uint64_t height, unsigned int depth, uint64_t row_pitch)
{
    D3D12_RANGE read_range;
    HRESULT hr;

    rb->width = width;
    rb->height = height;
    rb->depth = depth;
    rb->resource = buffer;
    rb->row_pitch = row_pitch;
    rb->data = NULL;

    ID3D12Resource_AddRef(rb->resource);

    read_range.Begin = 0;
    read_range.End = buffer_size;
    hr = ID3D12Resource_Map(rb->resource, 0, &read_range, &rb->data);
    ok(hr == S_OK, "Failed to map readback buffer, hr %#x.\n", hr);
}

void get_buffer_readback_with_command_list(ID3D12Resource *buffer, DXGI_FORMAT format,
        struct resource_readback *rb, ID3D12CommandQueue *queue, ID3D12GraphicsCommandList *command_list)
{
    D3D12_HEAP_PROPERTIES heap_properties;
    D3D12_RESOURCE_DESC resource_desc;
    ID3D12Resource *rb_buffer;
    D3D12_RANGE read_range;
    ID3D12Device *device;
    HRESULT hr;

    hr = ID3D12Resource_GetDevice(buffer, &IID_ID3D12Device, (void **)&device);
    ok(SUCCEEDED(hr), "Failed to get device, hr %#x.\n", hr);

    resource_desc = ID3D12Resource_GetDesc(buffer);
    assert(resource_desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER);
    resource_desc.Flags = D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE;

    hr = ID3D12Resource_GetHeapProperties(buffer, &heap_properties, NULL);
    if (SUCCEEDED(hr) && heap_properties.Type == D3D12_HEAP_TYPE_READBACK)
    {
        rb_buffer = buffer;
        ID3D12Resource_AddRef(rb_buffer);
    }
    else
    {
        rb_buffer = create_readback_buffer(device, resource_desc.Width);
        ID3D12GraphicsCommandList_CopyBufferRegion(command_list, rb_buffer, 0,
                buffer, 0, resource_desc.Width);
    }

    hr = ID3D12GraphicsCommandList_Close(command_list);
    ok(SUCCEEDED(hr), "Failed to close command list, hr %#x.\n", hr);

    exec_command_list(queue, command_list);
    wait_queue_idle(device, queue);
    ID3D12Device_Release(device);

    rb->width = resource_desc.Width / format_size(format);
    rb->height = 1;
    rb->depth = 1;
    rb->resource = rb_buffer;
    rb->row_pitch = resource_desc.Width;
    rb->data = NULL;

    read_range.Begin = 0;
    read_range.End = resource_desc.Width;
    hr = ID3D12Resource_Map(rb_buffer, 0, &read_range, &rb->data);
    ok(SUCCEEDED(hr), "Failed to map readback buffer, hr %#x.\n", hr);
}

uint8_t get_readback_uint8(struct resource_readback *rb, unsigned int x, unsigned int y)
{
    return *(uint8_t *)get_readback_data(rb, x, y, 0, sizeof(uint8_t));
}

uint16_t get_readback_uint16(struct resource_readback *rb, unsigned int x, unsigned int y)
{
    return *(uint16_t *)get_readback_data(rb, x, y, 0, sizeof(uint16_t));
}

uint64_t get_readback_uint64(struct resource_readback *rb, unsigned int x, unsigned int y)
{
    return *(uint64_t *)get_readback_data(rb, x, y, 0, sizeof(uint64_t));
}

float get_readback_float(struct resource_readback *rb, unsigned int x, unsigned int y)
{
    return *(float *)get_readback_data(rb, x, y, 0, sizeof(float));
}

const struct vec4 *get_readback_vec4(struct resource_readback *rb, unsigned int x, unsigned int y)
{
    return get_readback_data(rb, x, y, 0, sizeof(struct vec4));
}

const struct uvec4 *get_readback_uvec4(struct resource_readback *rb, unsigned int x, unsigned int y)
{
    return get_readback_data(rb, x, y, 0, sizeof(struct uvec4));
}

void check_readback_data_float_(unsigned int line, struct resource_readback *rb,
        const RECT *rect, float expected, unsigned int max_diff)
{
    RECT r = {0, 0, rb->width, rb->height};
    int x = 0, y;
    bool all_match = true;
    float got = 0;

    if (rect)
        r = *rect;

    for (y = r.top; y < r.bottom; ++y)
    {
        for (x = r.left; x < r.right; ++x)
        {
            got = get_readback_float(rb, x, y);
            if (!compare_float(got, expected, max_diff))
            {
                all_match = false;
                break;
            }
        }
        if (!all_match)
            break;
    }
    ok_(line)(all_match, "Got %.8e, expected %.8e at (%u, %u).\n", got, expected, x, y);
}

void check_sub_resource_float_(unsigned int line, ID3D12Resource *texture,
        unsigned int sub_resource_idx, ID3D12CommandQueue *queue, ID3D12GraphicsCommandList *command_list,
        float expected, unsigned int max_diff)
{
    struct resource_readback rb;

    get_texture_readback_with_command_list(texture, sub_resource_idx, &rb, queue, command_list);
    check_readback_data_float_(line, &rb, NULL, expected, max_diff);
    release_resource_readback(&rb);
}

void check_readback_data_uint8_(unsigned int line, struct resource_readback *rb,
        const RECT *rect, uint8_t expected, unsigned int max_diff)
{
    RECT r = {0, 0, rb->width, rb->height};
    int x = 0, y;
    bool all_match = true;
    uint8_t got = 0;

    if (rect)
        r = *rect;

    for (y = r.top; y < r.bottom; ++y)
    {
        for (x = r.left; x < r.right; ++x)
        {
            got = get_readback_uint8(rb, x, y);
            if (!compare_uint8(got, expected, max_diff))
            {
                all_match = false;
                break;
            }
        }
        if (!all_match)
            break;
    }
    ok_(line)(all_match, "Got 0x%02x, expected 0x%02x at (%u, %u).\n", got, expected, x, y);
}

void check_sub_resource_uint8_(unsigned int line, ID3D12Resource *texture,
        unsigned int sub_resource_idx, ID3D12CommandQueue *queue, ID3D12GraphicsCommandList *command_list,
        uint8_t expected, unsigned int max_diff)
{
    struct resource_readback rb;

    get_texture_readback_with_command_list(texture, sub_resource_idx, &rb, queue, command_list);
    check_readback_data_uint8_(line, &rb, NULL, expected, max_diff);
    release_resource_readback(&rb);
}

void check_readback_data_uint16_(unsigned int line, struct resource_readback *rb,
        const RECT *rect, uint16_t expected, unsigned int max_diff)
{
    RECT r = {0, 0, rb->width, rb->height};
    int x = 0, y;
    bool all_match = true;
    uint16_t got = 0;

    if (rect)
        r = *rect;

    for (y = r.top; y < r.bottom; ++y)
    {
        for (x = r.left; x < r.right; ++x)
        {
            got = get_readback_uint16(rb, x, y);
            if (!compare_uint16(got, expected, max_diff))
            {
                all_match = false;
                break;
            }
        }
        if (!all_match)
            break;
    }
    ok_(line)(all_match, "Got 0x%04x, expected 0x%04x at (%u, %u).\n", got, expected, x, y);
}

void check_sub_resource_uint16_(unsigned int line, ID3D12Resource *texture,
        unsigned int sub_resource_idx, ID3D12CommandQueue *queue, ID3D12GraphicsCommandList *command_list,
        uint16_t expected, unsigned int max_diff)
{
    struct resource_readback rb;

    get_texture_readback_with_command_list(texture, sub_resource_idx, &rb, queue, command_list);
    check_readback_data_uint16_(line, &rb, NULL, expected, max_diff);
    release_resource_readback(&rb);
}

void check_readback_data_uint64_(unsigned int line, struct resource_readback *rb,
        const RECT *rect, uint64_t expected, unsigned int max_diff)
{
    RECT r = {0, 0, rb->width, rb->height};
    int x = 0, y;
    bool all_match = true;
    uint64_t got = 0;

    if (rect)
        r = *rect;

    for (y = r.top; y < r.bottom; ++y)
    {
        for (x = r.left; x < r.right; ++x)
        {
            got = get_readback_uint64(rb, x, y);
            if (!compare_uint64(got, expected, max_diff))
            {
                all_match = false;
                break;
            }
        }
        if (!all_match)
            break;
    }
    ok_(line)(all_match, "Got %#"PRIx64", expected %#"PRIx64" at (%u, %u).\n", got, expected, x, y);
}

void check_sub_resource_uint64_(unsigned int line, ID3D12Resource *texture,
        unsigned int sub_resource_idx, ID3D12CommandQueue *queue, ID3D12GraphicsCommandList *command_list,
        uint64_t expected, unsigned int max_diff)
{
    struct resource_readback rb;

    get_texture_readback_with_command_list(texture, sub_resource_idx, &rb, queue, command_list);
    check_readback_data_uint64_(line, &rb, NULL, expected, max_diff);
    release_resource_readback(&rb);
}

void check_sub_resource_vec4_(unsigned int line, ID3D12Resource *texture,
        unsigned int sub_resource_idx, ID3D12CommandQueue *queue, ID3D12GraphicsCommandList *command_list,
        const struct vec4 *expected, unsigned int max_diff)
{
    struct resource_readback rb;
    unsigned int x = 0, y;
    bool all_match = true;
    struct vec4 got = {0};

    get_texture_readback_with_command_list(texture, sub_resource_idx, &rb, queue, command_list);
    for (y = 0; y < rb.height; ++y)
    {
        for (x = 0; x < rb.width; ++x)
        {
            got = *get_readback_vec4(&rb, x, y);
            if (!compare_vec4(&got, expected, max_diff))
            {
                all_match = false;
                break;
            }
        }
        if (!all_match)
            break;
    }
    release_resource_readback(&rb);

    ok_(line)(all_match, "Got {%.8e, %.8e, %.8e, %.8e}, expected {%.8e, %.8e, %.8e, %.8e} at (%u, %u).\n",
            got.x, got.y, got.z, got.w, expected->x, expected->y, expected->z, expected->w, x, y);
}

void check_sub_resource_uvec4_(unsigned int line, ID3D12Resource *texture,
        unsigned int sub_resource_idx, ID3D12CommandQueue *queue, ID3D12GraphicsCommandList *command_list,
        const struct uvec4 *expected_value)
{
    struct resource_readback rb;
    struct uvec4 value = {0};
    unsigned int x = 0, y;
    bool all_match = true;

    get_texture_readback_with_command_list(texture, sub_resource_idx, &rb, queue, command_list);
    for (y = 0; y < rb.height; ++y)
    {
        for (x = 0; x < rb.width; ++x)
        {
            value = *get_readback_uvec4(&rb, x, y);
            if (!compare_uvec4(&value, expected_value))
            {
                all_match = false;
                break;
            }
        }
        if (!all_match)
            break;
    }
    release_resource_readback(&rb);

    ok_(line)(all_match,
            "Got {0x%08x, 0x%08x, 0x%08x, 0x%08x}, expected {0x%08x, 0x%08x, 0x%08x, 0x%08x} at (%u, %u).\n",
            value.x, value.y, value.z, value.w,
            expected_value->x, expected_value->y, expected_value->z, expected_value->w, x, y);
}

bool broken_on_warp(bool condition)
{
    return broken(use_warp_device && condition);
}

bool is_min_max_filtering_supported(ID3D12Device *device)
{
    D3D12_FEATURE_DATA_D3D12_OPTIONS options;
    HRESULT hr;

    if (FAILED(hr = ID3D12Device_CheckFeatureSupport(device,
            D3D12_FEATURE_D3D12_OPTIONS, &options, sizeof(options))))
    {
        trace("Failed to check feature support, hr %#x.\n", hr);
        return false;
    }

    /* D3D12 validation layer says tiled resource tier 2+ support implies min/max filtering support. */
    return options.TiledResourcesTier >= D3D12_TILED_RESOURCES_TIER_2;
}

D3D12_TILED_RESOURCES_TIER get_tiled_resources_tier(ID3D12Device *device)
{
    D3D12_FEATURE_DATA_D3D12_OPTIONS options;
    HRESULT hr;

    if (FAILED(hr = ID3D12Device_CheckFeatureSupport(device,
            D3D12_FEATURE_D3D12_OPTIONS, &options, sizeof(options))))
    {
        trace("Failed to check feature support, hr %#x.\n", hr);
        return D3D12_TILED_RESOURCES_TIER_NOT_SUPPORTED;
    }

    return options.TiledResourcesTier;
}

bool is_standard_swizzle_64kb_supported(ID3D12Device *device)
{
    D3D12_FEATURE_DATA_D3D12_OPTIONS options;
    HRESULT hr;

    if (FAILED(hr = ID3D12Device_CheckFeatureSupport(device,
            D3D12_FEATURE_D3D12_OPTIONS, &options, sizeof(options))))
    {
        trace("Failed to check feature support, hr %#x.\n", hr);
        return false;
    }

    return options.StandardSwizzle64KBSupported;
}

bool is_memory_pool_L1_supported(ID3D12Device *device)
{
    D3D12_FEATURE_DATA_ARCHITECTURE architecture;
    HRESULT hr;

    memset(&architecture, 0, sizeof(architecture));
    if (FAILED(hr = ID3D12Device_CheckFeatureSupport(device, D3D12_FEATURE_ARCHITECTURE,
            &architecture, sizeof(architecture))))
    {
        trace("Failed to check feature support, hr %#x.\n", hr);
        return false;
    }

    return !architecture.UMA;
}

bool is_vrs_tier1_supported(ID3D12Device *device, bool *additional_shading_rates)
{
    D3D12_FEATURE_DATA_D3D12_OPTIONS6 options;
    HRESULT hr;

    if (additional_shading_rates)
        *additional_shading_rates = false;

    if (FAILED(hr = ID3D12Device_CheckFeatureSupport(device,
            D3D12_FEATURE_D3D12_OPTIONS6, &options, sizeof(options))))
    {
        trace("Failed to check feature support, hr %#x.\n", hr);
        return false;
    }

    if (additional_shading_rates)
        *additional_shading_rates = options.AdditionalShadingRatesSupported;

    return options.VariableShadingRateTier >= D3D12_VARIABLE_SHADING_RATE_TIER_1;
}

bool is_vrs_tier2_supported(ID3D12Device *device)
{
    D3D12_FEATURE_DATA_D3D12_OPTIONS6 options;
    HRESULT hr;

    if (FAILED(hr = ID3D12Device_CheckFeatureSupport(device,
            D3D12_FEATURE_D3D12_OPTIONS6, &options, sizeof(options))))
    {
        trace("Failed to check feature support, hr %#x.\n", hr);
        return false;
    }
    return options.VariableShadingRateTier >= D3D12_VARIABLE_SHADING_RATE_TIER_2;
}

ID3D12RootSignature *create_cb_root_signature_(unsigned int line,
        ID3D12Device *device, unsigned int reg_idx, D3D12_SHADER_VISIBILITY shader_visibility,
        D3D12_ROOT_SIGNATURE_FLAGS flags)
{
    D3D12_ROOT_SIGNATURE_DESC root_signature_desc;
    ID3D12RootSignature *root_signature = NULL;
    D3D12_ROOT_PARAMETER root_parameter;
    HRESULT hr;

    root_parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    root_parameter.Descriptor.ShaderRegister = reg_idx;
    root_parameter.Descriptor.RegisterSpace = 0;
    root_parameter.ShaderVisibility = shader_visibility;

    memset(&root_signature_desc, 0, sizeof(root_signature_desc));
    root_signature_desc.NumParameters = 1;
    root_signature_desc.pParameters = &root_parameter;
    root_signature_desc.Flags = flags;
    hr = create_root_signature(device, &root_signature_desc, &root_signature);
    ok_(line)(SUCCEEDED(hr), "Failed to create root signature, hr %#x.\n", hr);

    return root_signature;
}

ID3D12RootSignature *create_32bit_constants_root_signature_(unsigned int line,
        ID3D12Device *device, unsigned int reg_idx, unsigned int element_count,
        D3D12_SHADER_VISIBILITY shader_visibility, D3D12_ROOT_SIGNATURE_FLAGS flags)
{
    D3D12_ROOT_SIGNATURE_DESC root_signature_desc;
    ID3D12RootSignature *root_signature = NULL;
    D3D12_ROOT_PARAMETER root_parameter;
    HRESULT hr;

    root_parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    root_parameter.Constants.ShaderRegister = reg_idx;
    root_parameter.Constants.RegisterSpace = 0;
    root_parameter.Constants.Num32BitValues = element_count;
    root_parameter.ShaderVisibility = shader_visibility;

    memset(&root_signature_desc, 0, sizeof(root_signature_desc));
    root_signature_desc.NumParameters = 1;
    root_signature_desc.pParameters = &root_parameter;
    root_signature_desc.Flags = flags;
    hr = create_root_signature(device, &root_signature_desc, &root_signature);
    ok_(line)(SUCCEEDED(hr), "Failed to create root signature, hr %#x.\n", hr);

    return root_signature;
}

ID3D12RootSignature *create_texture_root_signature_(unsigned int line,
        ID3D12Device *device, D3D12_SHADER_VISIBILITY shader_visibility,
        unsigned int constant_count, D3D12_ROOT_SIGNATURE_FLAGS flags,
        const D3D12_STATIC_SAMPLER_DESC *sampler_desc)
{
    D3D12_ROOT_SIGNATURE_DESC root_signature_desc;
    ID3D12RootSignature *root_signature = NULL;
    D3D12_STATIC_SAMPLER_DESC static_sampler;
    D3D12_DESCRIPTOR_RANGE descriptor_range;
    D3D12_ROOT_PARAMETER root_parameters[2];
    HRESULT hr;

    if (sampler_desc)
    {
        static_sampler = *sampler_desc;
    }
    else
    {
        memset(&static_sampler, 0, sizeof(static_sampler));
        static_sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
        static_sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        static_sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        static_sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        static_sampler.MaxLOD = D3D12_FLOAT32_MAX;
        static_sampler.ShaderRegister = 0;
        static_sampler.RegisterSpace = 0;
        static_sampler.ShaderVisibility = shader_visibility;
    }

    descriptor_range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    descriptor_range.NumDescriptors = 1;
    descriptor_range.BaseShaderRegister = 0;
    descriptor_range.RegisterSpace = 0;
    descriptor_range.OffsetInDescriptorsFromTableStart = 0;
    root_parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    root_parameters[0].DescriptorTable.NumDescriptorRanges = 1;
    root_parameters[0].DescriptorTable.pDescriptorRanges = &descriptor_range;
    root_parameters[0].ShaderVisibility = shader_visibility;

    root_parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    root_parameters[1].Constants.ShaderRegister = 0;
    root_parameters[1].Constants.RegisterSpace = 0;
    root_parameters[1].Constants.Num32BitValues = constant_count;
    root_parameters[1].ShaderVisibility = shader_visibility;

    memset(&root_signature_desc, 0, sizeof(root_signature_desc));
    root_signature_desc.NumParameters = constant_count ? 2 : 1;
    root_signature_desc.pParameters = root_parameters;
    root_signature_desc.NumStaticSamplers = 1;
    root_signature_desc.pStaticSamplers = &static_sampler;
    root_signature_desc.Flags = flags;

    hr = create_root_signature(device, &root_signature_desc, &root_signature);
    ok_(line)(SUCCEEDED(hr), "Failed to create root signature, hr %#x.\n", hr);

    return root_signature;
}

ID3D12PipelineState *create_compute_pipeline_state_(unsigned int line, ID3D12Device *device,
        ID3D12RootSignature *root_signature, const D3D12_SHADER_BYTECODE cs)
{
    D3D12_COMPUTE_PIPELINE_STATE_DESC pipeline_state_desc;
    ID3D12PipelineState *pipeline_state = NULL;
    HRESULT hr;

    memset(&pipeline_state_desc, 0, sizeof(pipeline_state_desc));
    pipeline_state_desc.pRootSignature = root_signature;
    pipeline_state_desc.CS = cs;
    pipeline_state_desc.NodeMask = 0;
    pipeline_state_desc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
    hr = ID3D12Device_CreateComputePipelineState(device, &pipeline_state_desc,
            &IID_ID3D12PipelineState, (void **)&pipeline_state);
    ok_(line)(SUCCEEDED(hr), "Failed to create compute pipeline state, hr %#x.\n", hr);
    return pipeline_state;
}

ID3D12CommandSignature *create_command_signature_(unsigned int line,
        ID3D12Device *device, D3D12_INDIRECT_ARGUMENT_TYPE argument_type)
{
    ID3D12CommandSignature *command_signature = NULL;
    D3D12_COMMAND_SIGNATURE_DESC signature_desc;
    D3D12_INDIRECT_ARGUMENT_DESC argument_desc;
    HRESULT hr;

    argument_desc.Type = argument_type;

    switch (argument_type)
    {
        case D3D12_INDIRECT_ARGUMENT_TYPE_DRAW:
            signature_desc.ByteStride = sizeof(D3D12_DRAW_ARGUMENTS);
            break;
        case D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED:
            signature_desc.ByteStride = sizeof(D3D12_DRAW_INDEXED_ARGUMENTS);
            break;
        case D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH:
            signature_desc.ByteStride = sizeof(D3D12_DISPATCH_ARGUMENTS);
            break;
        case D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH_RAYS:
            signature_desc.ByteStride = sizeof(D3D12_DISPATCH_RAYS_DESC);
            break;
        default:
            return NULL;
    }

    signature_desc.NumArgumentDescs = 1;
    signature_desc.pArgumentDescs = &argument_desc;
    signature_desc.NodeMask = 0;
    hr = ID3D12Device_CreateCommandSignature(device, &signature_desc,
            NULL, &IID_ID3D12CommandSignature, (void **)&command_signature);
    ok_(line)(hr == S_OK, "Failed to create command signature, hr %#x.\n", hr);

    return command_signature;
}

bool init_compute_test_context_(unsigned int line, struct test_context *context)
{
    D3D12_COMMAND_LIST_TYPE command_list_type = D3D12_COMMAND_LIST_TYPE_COMPUTE;
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
    /* Workaround RenderDoc bug. It expects a DIRECT command queue to exist. */
    if (renderdoc_api)
        command_list_type = D3D12_COMMAND_LIST_TYPE_DIRECT;
#endif

    context->queue = create_command_queue_(line, device,
            command_list_type, D3D12_COMMAND_QUEUE_PRIORITY_NORMAL);

    hr = ID3D12Device_CreateCommandAllocator(device, command_list_type,
            &IID_ID3D12CommandAllocator, (void **)&context->allocator);
    ok_(line)(hr == S_OK, "Failed to create command allocator, hr %#x.\n", hr);

    hr = ID3D12Device_CreateCommandList(device, 0, command_list_type,
            context->allocator, NULL, &IID_ID3D12GraphicsCommandList, (void **)&context->list);
    ok_(line)(hr == S_OK, "Failed to create command list, hr %#x.\n", hr);

    return true;
}

bool context_supports_dxil_(unsigned int line, struct test_context *context)
{
    D3D12_FEATURE_DATA_SHADER_MODEL model;
    HRESULT hr;
    model.HighestShaderModel = D3D_SHADER_MODEL_6_0;
    hr = ID3D12Device_CheckFeatureSupport(context->device, D3D12_FEATURE_SHADER_MODEL, &model, sizeof(model));
    ok_(line)(hr == S_OK, "Failed to query shader model support, hr %#x.\n", hr);

    if (hr != S_OK)
        return false;

    if (model.HighestShaderModel < D3D_SHADER_MODEL_6_0)
    {
        skip_(line)("Device does not support shader model 6.0, skipping DXIL tests.\n");
        return false;
    }
    else
        return true;
}

void init_depth_stencil_(unsigned int line, struct depth_stencil_resource *ds,
        ID3D12Device *device, unsigned int width, unsigned int height, unsigned int array_size, unsigned int level_count,
        DXGI_FORMAT format, DXGI_FORMAT view_format, const D3D12_CLEAR_VALUE *clear_value)
{
    D3D12_DEPTH_STENCIL_VIEW_DESC dsv_desc, *view_desc;
    D3D12_HEAP_PROPERTIES heap_properties;
    D3D12_RESOURCE_DESC resource_desc;
    HRESULT hr;

    memset(ds, 0, sizeof(*ds));

    ds->heap = create_cpu_descriptor_heap(device, D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 1);

    memset(&heap_properties, 0, sizeof(heap_properties));
    heap_properties.Type = D3D12_HEAP_TYPE_DEFAULT;
    resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    resource_desc.Alignment = 0;
    resource_desc.Width = width;
    resource_desc.Height = height;
    resource_desc.DepthOrArraySize = array_size;
    resource_desc.MipLevels = level_count;
    resource_desc.Format = format;
    resource_desc.SampleDesc.Count = 1;
    resource_desc.SampleDesc.Quality = 0;
    resource_desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    resource_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    hr = ID3D12Device_CreateCommittedResource(device, &heap_properties, D3D12_HEAP_FLAG_NONE,
            &resource_desc, D3D12_RESOURCE_STATE_DEPTH_WRITE, clear_value,
            &IID_ID3D12Resource, (void **)&ds->texture);
    ok_(line)(SUCCEEDED(hr), "Failed to create texture, hr %#x.\n", hr);

    view_desc = NULL;
    if (view_format)
    {
        memset(&dsv_desc, 0, sizeof(dsv_desc));
        dsv_desc.Format = view_format;
        dsv_desc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
        view_desc = &dsv_desc;
    }
    ds->dsv_handle = ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(ds->heap);
    ID3D12Device_CreateDepthStencilView(device, ds->texture, view_desc, ds->dsv_handle);
}

void destroy_depth_stencil_(unsigned int line, struct depth_stencil_resource *ds)
{
    ID3D12DescriptorHeap_Release(ds->heap);
    ID3D12Resource_Release(ds->texture);
}
