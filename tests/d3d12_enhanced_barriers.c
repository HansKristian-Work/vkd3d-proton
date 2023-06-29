/*
 * Copyright 2023 Hans-Kristian Arntzen for Valve Corporation
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

void test_enhanced_barrier_castable_formats_buffer(void)
{
    const DXGI_FORMAT formats0[] = { DXGI_FORMAT_R32_UINT };
    const DXGI_FORMAT formats1[] = { DXGI_FORMAT_UNKNOWN };
    D3D12_FEATURE_DATA_D3D12_OPTIONS12 features12;
    D3D12_HEAP_PROPERTIES heap_props;
    D3D12_RESOURCE_DESC1 desc1;
    ID3D12Device10 *device10;
    ID3D12Device *device;
    HRESULT hr;

    if (!(device = create_device()))
        return;

    if (FAILED(hr = ID3D12Device_CheckFeatureSupport(device, D3D12_FEATURE_D3D12_OPTIONS12, &features12, sizeof(features12))) ||
        !features12.RelaxedFormatCastingSupported)
    {
        ID3D12Device_Release(device);
        skip("RelaxedFormatCasting is not supported.\n");
        return;
    }

    if (FAILED(hr = ID3D12Device_QueryInterface(device, &IID_ID3D12Device10, (void **)&device10)))
    {
        skip("ID3D12Device10 not available.\n");
        ID3D12Device_Release(device);
        return;
    }

    memset(&desc1, 0, sizeof(desc1));
    memset(&heap_props, 0, sizeof(heap_props));
    desc1.Width = 64 * 1024;
    desc1.Height = 1;
    desc1.DepthOrArraySize = 1;
    desc1.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc1.Format = DXGI_FORMAT_UNKNOWN;
    desc1.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    desc1.SampleDesc.Count = 1;
    desc1.MipLevels = 1;

    /* Barrier layout must be UNDEFINED for buffers. */
    heap_props.Type = D3D12_HEAP_TYPE_DEFAULT;
    hr = ID3D12Device10_CreateCommittedResource3(device10, &heap_props, D3D12_HEAP_FLAG_NONE,
            &desc1, D3D12_BARRIER_LAYOUT_UNORDERED_ACCESS,
            NULL, NULL,
            0, NULL, &IID_ID3D12Resource, NULL);
    ok(hr == E_INVALIDARG, "Unexpected hr #%x.\n", hr);

    hr = ID3D12Device10_CreateCommittedResource3(device10, &heap_props, D3D12_HEAP_FLAG_NONE,
            &desc1, D3D12_BARRIER_LAYOUT_UNDEFINED,
            NULL, NULL, 0, NULL, &IID_ID3D12Resource, NULL);
    ok(hr == S_FALSE, "Unexpected hr #%x.\n", hr);

    /* Castable formats are not allowed for buffers. */
    hr = ID3D12Device10_CreateCommittedResource3(device10, &heap_props, D3D12_HEAP_FLAG_NONE,
            &desc1, D3D12_BARRIER_LAYOUT_UNDEFINED,
            NULL, NULL,
            ARRAY_SIZE(formats0), formats0, &IID_ID3D12Resource, NULL);
    ok(hr == E_INVALIDARG, "Unexpected hr #%x.\n", hr);

    /* For some reason, this is allowed for buffers. It seems to compare byte size of UNKNOWN vs UNKNOWN (0 == 0) based on validation error above. */
    hr = ID3D12Device10_CreateCommittedResource3(device10, &heap_props, D3D12_HEAP_FLAG_NONE,
            &desc1, D3D12_BARRIER_LAYOUT_UNDEFINED,
            NULL, NULL,
            ARRAY_SIZE(formats1), formats1, &IID_ID3D12Resource, NULL);
    ok(hr == S_FALSE, "Unexpected hr #%x.\n", hr);

    ID3D12Device_Release(device);
    ID3D12Device10_Release(device10);
}

void test_enhanced_barrier_castable_formats_validation(void)
{
    /* The runtime is supposed to validate the format casting list. */
    D3D12_RESOURCE_ALLOCATION_INFO allocation_info;
    D3D12_FEATURE_DATA_D3D12_OPTIONS12 features12;
    D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc;
    D3D12_DEPTH_STENCIL_VIEW_DESC dsv_desc;
    ID3D12DescriptorHeap *resource_heap;
    D3D12_HEAP_PROPERTIES heap_props;
    ID3D12DescriptorHeap *dsv_heap;
    D3D12_RESOURCE_DESC1 desc1;
    ID3D12Device12 *device12;
    ID3D12Resource *resource;
    ID3D12Device *device;
    unsigned int i, j;
    HRESULT hr;

    static const struct test
    {
        DXGI_FORMAT tex_format;
        UINT32 num_formats;
        UINT flags;
        bool valid;
        DXGI_FORMAT cast_formats[16];
    } tests[] = {
        { DXGI_FORMAT_R32_UINT, 1, D3D12_RESOURCE_FLAG_NONE, true, { DXGI_FORMAT_R32_UINT } },
        { DXGI_FORMAT_R32_UINT, 2, D3D12_RESOURCE_FLAG_NONE, true, { DXGI_FORMAT_R32_UINT, DXGI_FORMAT_R16G16_FLOAT } },
        { DXGI_FORMAT_R32_UINT, 3, D3D12_RESOURCE_FLAG_NONE, true, { DXGI_FORMAT_R32_UINT, DXGI_FORMAT_R16G16_FLOAT, DXGI_FORMAT_R16G16_FLOAT } },
        { DXGI_FORMAT_R32_UINT, 3, D3D12_RESOURCE_FLAG_NONE, true, { DXGI_FORMAT_R32_UINT, DXGI_FORMAT_R16G16_FLOAT, DXGI_FORMAT_R9G9B9E5_SHAREDEXP } },
        { DXGI_FORMAT_R32_UINT, 3, D3D12_RESOURCE_FLAG_NONE, false, { DXGI_FORMAT_R32_UINT, DXGI_FORMAT_R16G16_FLOAT, DXGI_FORMAT_R16_FLOAT } },
        { DXGI_FORMAT_R32_UINT, 1, D3D12_RESOURCE_FLAG_NONE, false, { DXGI_FORMAT_UNKNOWN } },
        /* Block format casting is still not allowed. */
        { DXGI_FORMAT_R32G32B32A32_UINT, 1, D3D12_RESOURCE_FLAG_NONE, false, { DXGI_FORMAT_BC7_UNORM } },
        { DXGI_FORMAT_R32G32_UINT, 1, D3D12_RESOURCE_FLAG_NONE, false, { DXGI_FORMAT_BC1_UNORM } },
        /* ... unless the base format is compressed! Similar to Vulkan. */
        { DXGI_FORMAT_BC1_UNORM, 1, D3D12_RESOURCE_FLAG_NONE, true, { DXGI_FORMAT_R32G32_UINT } },
        { DXGI_FORMAT_BC7_UNORM, 1, D3D12_RESOURCE_FLAG_NONE, true, { DXGI_FORMAT_R32G32B32A32_UINT } },
#if 0
        /* Block format casting between similar block size *is* allowed apparently? This is not allowed in Vulkan.
         * This will likely "just work" on any reasonable driver, but it will throw a validation error. */
        { DXGI_FORMAT_BC1_UNORM, 1, D3D12_RESOURCE_FLAG_NONE, true, { DXGI_FORMAT_BC4_UNORM } },
        { DXGI_FORMAT_BC7_UNORM, 1, D3D12_RESOURCE_FLAG_NONE, true, { DXGI_FORMAT_BC6H_UF16 } },
#endif
        /* If one format supports usage flag, it's allowed. */
        { DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, 1, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, true, { DXGI_FORMAT_R8G8B8A8_UNORM } },
        { DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, 1, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, true, { DXGI_FORMAT_R8G8B8A8_UINT } },
        { DXGI_FORMAT_BC1_UNORM, 1, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET, false, { DXGI_FORMAT_BC1_UNORM } },
        { DXGI_FORMAT_BC1_UNORM, 1, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET, true, { DXGI_FORMAT_R32G32_UINT } },
        /* Depth-stencil does not need typeless here. */
        { DXGI_FORMAT_D32_FLOAT, 1, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL, true, { DXGI_FORMAT_R32_FLOAT } },
        { DXGI_FORMAT_R32_FLOAT, 1, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL, true, { DXGI_FORMAT_D32_FLOAT } },
#if 0
        /* This does not trip an error for some wild reason, but as other tests proved, this is non-sense, and likely unintentional ... */
        { DXGI_FORMAT_D32_FLOAT, 1, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL, true, { DXGI_FORMAT_R32_UINT } },
        { DXGI_FORMAT_D32_FLOAT, 2, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL, true, { DXGI_FORMAT_R32_FLOAT, DXGI_FORMAT_R16G16_UINT } },
        { DXGI_FORMAT_R32_FLOAT, 2, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL, true, { DXGI_FORMAT_D32_FLOAT, DXGI_FORMAT_R16G16_UINT } },
        { DXGI_FORMAT_R16G16_FLOAT, 2, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL, true, { DXGI_FORMAT_D32_FLOAT, DXGI_FORMAT_R16G16_UINT } },
#endif

        { DXGI_FORMAT_R32_TYPELESS, 1, D3D12_RESOURCE_FLAG_NONE, true, { DXGI_FORMAT_R32_TYPELESS } },

        /* Somehow, this is allowed, even if no format in the cast list supports depth-stencil directly.
         * Trying to cast to D32_FLOAT here however triggers device lost, meaning that TYPELESS resources in castable formats is meaningless.
         * Same goes for R32_FLOAT. This means that typeless formats in cast list must not contribute to allowed format list and should be ignored,
         * except for resolving resource desc validation ... (?!?!). */
        { DXGI_FORMAT_R32_TYPELESS, 1, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL, true, { DXGI_FORMAT_R32_TYPELESS } },

        /* Somehow, this is allowed, even if no format in the cast list supports depth-stencil directly. */
        { DXGI_FORMAT_R32_FLOAT, 1, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL, true, { DXGI_FORMAT_R32_TYPELESS } },

        /* Even THIS is allowed, dear lord ... */
        { DXGI_FORMAT_R32_FLOAT, 1, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL, true, { DXGI_FORMAT_R32_FLOAT } },

        /* Also test castable formats count = 0, then old style rules should apply. */
        /* This succeeds because of relaxed format casting rules in legacy model (sRGB can be reinterpreted as UNORM or UINT). */
        { DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, 0, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, true },

        { DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, 0, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL, false },
        { DXGI_FORMAT_BC1_UNORM, 0, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET, false },
        { DXGI_FORMAT_BC1_UNORM, 0, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL, false },
        { DXGI_FORMAT_R32_TYPELESS, 0, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL, true },
        { DXGI_FORMAT_R16_TYPELESS, 0, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL, true },
        { DXGI_FORMAT_R32_FLOAT, 0, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL, true },
        { DXGI_FORMAT_R16_UNORM, 0, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL, true },
        { DXGI_FORMAT_R8G8B8A8_UINT, 0, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL, false },
    };

    if (!(device = create_device()))
        return;

    if (FAILED(hr = ID3D12Device_CheckFeatureSupport(device, D3D12_FEATURE_D3D12_OPTIONS12, &features12, sizeof(features12))) ||
            !features12.RelaxedFormatCastingSupported)
    {
        ID3D12Device_Release(device);
        skip("RelaxedFormatCasting is not supported.\n");
        return;
    }

    if (FAILED(hr = ID3D12Device_QueryInterface(device, &IID_ID3D12Device12, (void **)&device12)))
    {
        skip("ID3D12Device12 not available.\n");
        ID3D12Device_Release(device);
        return;
    }

    resource_heap = create_cpu_descriptor_heap(device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1);
    dsv_heap = create_cpu_descriptor_heap(device, D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 1);

    memset(&desc1, 0, sizeof(desc1));
    memset(&heap_props, 0, sizeof(heap_props));
    desc1.Width = 64;
    desc1.Height = 64;
    desc1.DepthOrArraySize = 1;
    desc1.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc1.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    desc1.SampleDesc.Count = 1;
    desc1.MipLevels = 1;
    heap_props.Type = D3D12_HEAP_TYPE_DEFAULT;

    memset(&srv_desc, 0, sizeof(srv_desc));
    srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv_desc.Texture2D.MipLevels = 1;
    memset(&dsv_desc, 0, sizeof(dsv_desc));
    dsv_desc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;

    for (i = 0; i < ARRAY_SIZE(tests); i++)
    {
        const DXGI_FORMAT *p_formats = tests[i].cast_formats;
        desc1.Format = tests[i].tex_format;
        desc1.Flags = tests[i].flags;
        vkd3d_test_set_context("Test %u", i);

        allocation_info = ID3D12Device12_GetResourceAllocationInfo3(device12, 0, 1, &desc1,
                tests[i].num_formats ? &tests[i].num_formats : NULL,
                tests[i].num_formats ? &p_formats : NULL, NULL);

        if (tests[i].valid)
            ok(allocation_info.SizeInBytes != UINT64_MAX, "Unexpected failure in GetResourceAllocationInfo3.\n");
        else
            ok(allocation_info.SizeInBytes == UINT64_MAX, "Unexpected success in GetResourceAllocationInfo3.\n");

        /* Test older API as well to make sure there are no "special" validation rules for AllocationInfo3. */
        if (tests[i].num_formats == 0)
        {
            allocation_info = ID3D12Device12_GetResourceAllocationInfo2(device12, 0, 1, &desc1, NULL);

            if (tests[i].valid)
                ok(allocation_info.SizeInBytes != UINT64_MAX, "Unexpected failure in GetResourceAllocationInfo2.\n");
            else
                ok(allocation_info.SizeInBytes == UINT64_MAX, "Unexpected success in GetResourceAllocationInfo2.\n");
        }

        hr = ID3D12Device12_CreateCommittedResource3(device12, &heap_props,
                D3D12_HEAP_FLAG_CREATE_NOT_ZEROED, &desc1, D3D12_BARRIER_LAYOUT_UNDEFINED,
                NULL, NULL,
                tests[i].num_formats, tests[i].cast_formats,
                &IID_ID3D12Resource, (void**)&resource);

        if (FAILED(hr))
            resource = NULL;

        if (tests[i].valid)
            ok(hr == S_OK, "Unexpected failure in GetResourceAllocationInfo3, hr #%x.\n", hr);
        else
            ok(hr == E_INVALIDARG, "Unexpected success in GetResourceAllocationInfo3.\n");

        if (tests[i].num_formats == 0)
        {
            hr = ID3D12Device12_CreateCommittedResource2(device12, &heap_props,
                    D3D12_HEAP_FLAG_CREATE_NOT_ZEROED, &desc1, D3D12_RESOURCE_STATE_COMMON,
                    NULL, NULL,
                    &IID_ID3D12Resource, NULL);

            if (tests[i].valid)
                ok(hr == S_FALSE, "Unexpected failure in GetResourceAllocationInfo2, hr #%x.\n", hr);
            else
                ok(hr == E_INVALIDARG, "Unexpected success in GetResourceAllocationInfo2.\n");
        }

        /* Try to create all possible views. Tests that it does not blow up.
         * Also useful to see if validation blows up. */
        if (resource)
        {
            if (tests[i].tex_format != DXGI_FORMAT_R16_TYPELESS && tests[i].tex_format != DXGI_FORMAT_R32_TYPELESS)
            {
                if (tests[i].tex_format != DXGI_FORMAT_D32_FLOAT && tests[i].tex_format != DXGI_FORMAT_D16_UNORM)
                {
                    srv_desc.Format = tests[i].tex_format;
                    ID3D12Device_CreateShaderResourceView(device, resource, &srv_desc,
                            ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(resource_heap));
                }
                else if (tests[i].flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL)
                {
                    dsv_desc.Format = tests[i].tex_format;
                    ID3D12Device_CreateDepthStencilView(device, resource, &dsv_desc,
                            ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(dsv_heap));
                }
            }

            for (j = 0; j < tests[i].num_formats; j++)
            {
                if (tests[i].cast_formats[j] == DXGI_FORMAT_R16_TYPELESS || tests[i].cast_formats[j] == DXGI_FORMAT_R32_TYPELESS)
                    continue;

                if (tests[i].cast_formats[j] != DXGI_FORMAT_D32_FLOAT && tests[i].tex_format != DXGI_FORMAT_D16_UNORM)
                {
                    srv_desc.Format = tests[i].cast_formats[j];
                    ID3D12Device_CreateShaderResourceView(device, resource, &srv_desc,
                            ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(resource_heap));
                }
                else if (tests[i].flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL)
                {
                    dsv_desc.Format = tests[i].cast_formats[j];
                    ID3D12Device_CreateDepthStencilView(device, resource, &dsv_desc,
                            ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(dsv_heap));
                }
            }
            ID3D12Resource_Release(resource);
        }
    }
    vkd3d_test_set_context(NULL);

    ID3D12DescriptorHeap_Release(resource_heap);
    ID3D12DescriptorHeap_Release(dsv_heap);
    ID3D12Device_Release(device);
    ID3D12Device12_Release(device12);
}

void test_enhanced_barrier_castable_formats(void)
{
    const DXGI_FORMAT castable_formats[] = { DXGI_FORMAT_R32_UINT };
    D3D12_RESOURCE_ALLOCATION_INFO1 allocation_info1;
    D3D12_RESOURCE_ALLOCATION_INFO allocation_info;
    D3D12_FEATURE_DATA_D3D12_OPTIONS12 features12;
    D3D12_UNORDERED_ACCESS_VIEW_DESC uav_desc;
    D3D12_FEATURE_DATA_D3D12_OPTIONS features;
    const UINT num_castable_formats = 1;
    D3D12_ROOT_SIGNATURE_DESC rs_desc;
    D3D12_ROOT_PARAMETER rs_params[2];
    D3D12_DESCRIPTOR_RANGE desc_range;
    D3D12_CPU_DESCRIPTOR_HANDLE cpu_h;
    D3D12_GPU_DESCRIPTOR_HANDLE gpu_h;
    D3D12_HEAP_PROPERTIES heap_props;
    ID3D12PipelineState *write_pso;
    ID3D12PipelineState *read_pso;
    ID3D12Resource *resources[3];
    struct resource_readback rb;
    struct test_context context;
    D3D12_RESOURCE_DESC1 desc1;
    ID3D12DescriptorHeap *heap;
    D3D12_HEAP_DESC heap_desc;
    ID3D12Device12 *device12;
    ID3D12Resource *readback;
    ID3D12Heap *place_heap;
    unsigned int i, j;
    HRESULT hr;

#if 0
    RWTexture2D<float4> UAVUnorm8x4 : register(u0);
    RWTexture2D<uint> UAVUint32 : register(u1);
    RWStructuredBuffer<uint> Readback : register(u2);

    [numthreads(8, 8, 1)]
    void WriteMain(uint2 thr : SV_DispatchThreadID)
    {
        UAVUnorm8x4[thr] = (thr / 255.0).xyxy;
    }

    [numthreads(8, 8, 1)]
    void ReadbackMain(uint2 thr : SV_DispatchThreadID)
    {
        Readback[thr.y * 128 + thr.x] = UAVUint32[thr];
    }
#endif

    static const DWORD write_dxbc[] =
    {
        0x43425844, 0xa0abe594, 0xa30f5000, 0x08bb881d, 0x5d30053d, 0x00000001, 0x00000220, 0x00000005,
        0x00000034, 0x000000cc, 0x000000dc, 0x000000ec, 0x00000184, 0x46454452, 0x00000090, 0x00000000,
        0x00000000, 0x00000001, 0x0000003c, 0x43530500, 0x00000100, 0x00000068, 0x31314452, 0x0000003c,
        0x00000018, 0x00000020, 0x00000028, 0x00000024, 0x0000000c, 0x00000000, 0x0000005c, 0x00000004,
        0x00000005, 0x00000004, 0xffffffff, 0x00000000, 0x00000001, 0x0000000d, 0x55564155, 0x6d726f6e,
        0x00347838, 0x7263694d, 0x666f736f, 0x52282074, 0x4c482029, 0x53204c53, 0x65646168, 0x6f432072,
        0x6c69706d, 0x31207265, 0x00312e30, 0x4e475349, 0x00000008, 0x00000000, 0x00000008, 0x4e47534f,
        0x00000008, 0x00000000, 0x00000008, 0x58454853, 0x00000090, 0x00050050, 0x00000024, 0x0100086a,
        0x0400189c, 0x0011e000, 0x00000000, 0x00005555, 0x0200005f, 0x00020032, 0x02000068, 0x00000001,
        0x0400009b, 0x00000008, 0x00000008, 0x00000001, 0x04000056, 0x001000f2, 0x00000000, 0x00020446,
        0x0a000038, 0x001000f2, 0x00000000, 0x00100e46, 0x00000000, 0x00004002, 0x3b808081, 0x3b808081,
        0x3b808081, 0x3b808081, 0x060000a4, 0x0011e0f2, 0x00000000, 0x00020546, 0x00100e46, 0x00000000,
        0x0100003e, 0x54415453, 0x00000094, 0x00000004, 0x00000001, 0x00000000, 0x00000001, 0x00000001,
        0x00000000, 0x00000000, 0x00000001, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
        0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
        0x00000001, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
        0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000001,
    };
    static const D3D12_SHADER_BYTECODE cs_write_dxbc = SHADER_BYTECODE(write_dxbc);

    static const DWORD read_dxbc[] =
    {
        0x43425844, 0xb52b65b0, 0x025c1f8b, 0x5e6c674a, 0x57186bfd, 0x00000001, 0x000002f0, 0x00000005,
        0x00000034, 0x00000168, 0x00000178, 0x00000188, 0x00000254, 0x46454452, 0x0000012c, 0x00000001,
        0x00000090, 0x00000002, 0x0000003c, 0x43530500, 0x00000100, 0x00000104, 0x31314452, 0x0000003c,
        0x00000018, 0x00000020, 0x00000028, 0x00000024, 0x0000000c, 0x00000000, 0x0000007c, 0x00000004,
        0x00000004, 0x00000004, 0xffffffff, 0x00000001, 0x00000001, 0x00000001, 0x00000086, 0x00000006,
        0x00000006, 0x00000001, 0x00000004, 0x00000002, 0x00000001, 0x00000001, 0x55564155, 0x33746e69,
        0x65520032, 0x61626461, 0xab006b63, 0x00000086, 0x00000001, 0x000000a8, 0x00000004, 0x00000000,
        0x00000003, 0x000000d0, 0x00000000, 0x00000004, 0x00000002, 0x000000e0, 0x00000000, 0xffffffff,
        0x00000000, 0xffffffff, 0x00000000, 0x656c4524, 0x746e656d, 0x6f776400, 0xab006472, 0x00130000,
        0x00010001, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x000000d9,
        0x7263694d, 0x666f736f, 0x52282074, 0x4c482029, 0x53204c53, 0x65646168, 0x6f432072, 0x6c69706d,
        0x31207265, 0x00312e30, 0x4e475349, 0x00000008, 0x00000000, 0x00000008, 0x4e47534f, 0x00000008,
        0x00000000, 0x00000008, 0x58454853, 0x000000c4, 0x00050050, 0x00000031, 0x0100086a, 0x0400189c,
        0x0011e000, 0x00000001, 0x00004444, 0x0400009e, 0x0011e000, 0x00000002, 0x00000004, 0x0200005f,
        0x00020032, 0x02000068, 0x00000001, 0x0400009b, 0x00000008, 0x00000008, 0x00000001, 0x06000029,
        0x00100012, 0x00000000, 0x0002001a, 0x00004001, 0x00000007, 0x0600001e, 0x00100012, 0x00000000,
        0x0010000a, 0x00000000, 0x0002000a, 0x880000a3, 0x800000c2, 0x00111103, 0x00100022, 0x00000000,
        0x00020546, 0x0011ee16, 0x00000001, 0x090000a8, 0x0011e012, 0x00000002, 0x0010000a, 0x00000000,
        0x00004001, 0x00000000, 0x0010001a, 0x00000000, 0x0100003e, 0x54415453, 0x00000094, 0x00000005,
        0x00000001, 0x00000000, 0x00000001, 0x00000000, 0x00000002, 0x00000000, 0x00000001, 0x00000000,
        0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000001, 0x00000000,
        0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
        0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
        0x00000000, 0x00000000, 0x00000000, 0x00000001,
    };
    static const D3D12_SHADER_BYTECODE cs_read_dxbc = SHADER_BYTECODE(read_dxbc);
    const DXGI_FORMAT *p_castable_formats = castable_formats;

    if (!init_compute_test_context(&context))
        return;

    if (FAILED(hr = ID3D12Device_CheckFeatureSupport(context.device, D3D12_FEATURE_D3D12_OPTIONS12, &features12, sizeof(features12))) ||
        !features12.RelaxedFormatCastingSupported)
    {
        destroy_test_context(&context);
        skip("RelaxedFormatCasting is not supported.\n");
        return;
    }

    if (FAILED(ID3D12Device_QueryInterface(context.device, &IID_ID3D12Device12, (void **)&device12)))
    {
        destroy_test_context(&context);
        skip("ID3D12Device12 not supported, skipping test.\n");
        return;
    }

    memset(&heap_props, 0, sizeof(heap_props));
    heap_props.Type = D3D12_HEAP_TYPE_DEFAULT;
    memset(&desc1, 0, sizeof(desc1));

    desc1.Width = 128;
    desc1.Height = 128;
    desc1.DepthOrArraySize = 1;
    desc1.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc1.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc1.MipLevels = 1;
    desc1.SampleDesc.Count = 1;

    /* Use SIMULTANEOUS ACCESS to workaround some layout jank in VVL.
     * We need to initialize the resource with Discard(), but we cannot immediately discard a resource
     * using modern layout, since we first have to transition into legacy model.
     * To avoid having a dependency on GraphicsCommandList7::Barrier(), this is a temporary solution. */
    desc1.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS | D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET |
            D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS;
    desc1.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;

    heap = create_gpu_descriptor_heap(context.device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, ARRAY_SIZE(resources) * 2);

    hr = ID3D12Device12_CreateCommittedResource3(device12, &heap_props, D3D12_HEAP_FLAG_NONE,
            &desc1, D3D12_BARRIER_LAYOUT_COMMON, NULL, NULL,
            ARRAY_SIZE(castable_formats), castable_formats,
            &IID_ID3D12Resource, (void **)&resources[0]);
    ok(SUCCEEDED(hr), "Failed to create resource, hr #%x.\n", hr);
    /* Transition to legacy model. */
    transition_resource_state(context.list, resources[0], D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    allocation_info = ID3D12Device12_GetResourceAllocationInfo3(device12, 0, 1, &desc1, &num_castable_formats, &p_castable_formats, &allocation_info1);
    ok(allocation_info.SizeInBytes != UINT64_MAX, "Failed to query allocation info.\n");
    ok(allocation_info1.Offset == 0, "Unexpected offset %"PRIu64".\n", allocation_info1.Offset);
    ok(allocation_info.Alignment == allocation_info1.Alignment,
            "Unexpected alignment, %"PRIu64" != %"PRIu64".\n",
            allocation_info.Alignment, allocation_info1.Alignment);
    ok(allocation_info.Alignment <= 64 * 1024, "Unexpected alignment %"PRIu64".\n", allocation_info.Alignment);

    memset(&heap_desc, 0, sizeof(heap_desc));
    heap_desc.SizeInBytes = allocation_info.SizeInBytes + 64 * 1024;
    heap_desc.Properties = heap_props;
    heap_desc.Flags = D3D12_HEAP_FLAG_CREATE_NOT_ZEROED;
    hr = ID3D12Device_CreateHeap(context.device, &heap_desc, &IID_ID3D12Heap, (void **)&place_heap);
    ok(SUCCEEDED(hr), "Failed to allocate heap, hr #%x.\n", hr);

    hr = ID3D12Device12_CreatePlacedResource2(device12, place_heap, 64 * 1024, &desc1,
            D3D12_BARRIER_LAYOUT_COMMON, NULL,
            ARRAY_SIZE(castable_formats), castable_formats, &IID_ID3D12Resource, (void **)&resources[1]);
    ok(SUCCEEDED(hr), "Failed to create resource, hr #%x.\n", hr);
    /* Transition to legacy model. */
    transition_resource_state(context.list, resources[1], D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    resources[2] = NULL;

    if (SUCCEEDED(ID3D12Device_CheckFeatureSupport(context.device, D3D12_FEATURE_D3D12_OPTIONS, &features, sizeof(features))) &&
        features.TiledResourcesTier >= D3D12_TILED_RESOURCES_TIER_1)
    {
        desc1.Layout = D3D12_TEXTURE_LAYOUT_64KB_UNDEFINED_SWIZZLE;
        hr = ID3D12Device12_CreateReservedResource2(device12, (const D3D12_RESOURCE_DESC *)&desc1, D3D12_BARRIER_LAYOUT_COMMON,
                NULL, NULL,
                ARRAY_SIZE(castable_formats), castable_formats,
                &IID_ID3D12Resource, (void **)&resources[2]);
        ok(SUCCEEDED(hr), "Failed to create resource, hr #%x.\n", hr);
        /* Transition to legacy model. */
        transition_resource_state(context.list, resources[2], D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

        {
            D3D12_TILE_RANGE_FLAGS range_flags = D3D12_TILE_RANGE_FLAG_NONE;
            D3D12_TILED_RESOURCE_COORDINATE tiled_coord;
            D3D12_TILE_REGION_SIZE tile_region_size;
            UINT range_tile_counts = 1;
            UINT range_offsets = 0;

            memset(&tiled_coord, 0, sizeof(tiled_coord));
            memset(&tile_region_size, 0, sizeof(tile_region_size));
            tile_region_size.NumTiles = 1;
            ID3D12CommandQueue_UpdateTileMappings(context.queue, resources[2], 1,
                    &tiled_coord, &tile_region_size,
                    place_heap, 1, &range_flags,
                    &range_offsets, &range_tile_counts,
                    D3D12_TILE_MAPPING_FLAG_NONE);
        }
    }

    memset(&uav_desc, 0, sizeof(uav_desc));
    uav_desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;

    /* Self format is always allowed.
     * If NumCastableFormats is not zero, default casting rules are turned off.
     * E.g. trying to use relaxed rules to cast to RGBA8_UINT will trigger validation error here. */
    cpu_h = ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(heap);

    for (i = 0; i < ARRAY_SIZE(resources); i++)
    {
        uav_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        ID3D12Device_CreateUnorderedAccessView(context.device, resources[i], NULL, &uav_desc, cpu_h);
        cpu_h.ptr += ID3D12Device_GetDescriptorHandleIncrementSize(context.device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        uav_desc.Format = DXGI_FORMAT_R32_UINT;
        ID3D12Device_CreateUnorderedAccessView(context.device, resources[i], NULL, &uav_desc, cpu_h);
        cpu_h.ptr += ID3D12Device_GetDescriptorHandleIncrementSize(context.device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    }

    memset(&rs_desc, 0, sizeof(rs_desc));
    memset(rs_params, 0, sizeof(rs_params));
    memset(&desc_range, 0, sizeof(desc_range));
    rs_desc.NumParameters = ARRAY_SIZE(rs_params);
    rs_desc.pParameters = rs_params;
    rs_params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    rs_params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rs_params[0].DescriptorTable.NumDescriptorRanges = 1;
    rs_params[0].DescriptorTable.pDescriptorRanges = &desc_range;
    desc_range.NumDescriptors = 2;
    desc_range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    rs_params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    rs_params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    rs_params[1].Descriptor.ShaderRegister = 2;

    hr = create_root_signature(context.device, &rs_desc, &context.root_signature);
    ok(SUCCEEDED(hr), "Failed to create root signature, hr #%x.\n", hr);

    write_pso = create_compute_pipeline_state(context.device, context.root_signature, cs_write_dxbc);
    read_pso = create_compute_pipeline_state(context.device, context.root_signature, cs_read_dxbc);

    readback = create_default_buffer(context.device, desc1.Width * desc1.Height * sizeof(uint32_t),
            D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    for (i = 0; i < ARRAY_SIZE(resources); i++)
    {
        vkd3d_test_set_context("Test %u", i);
        if (!resources[i])
        {
            skip("Sparse not supported, skipping.\n");
            continue;
        }

        ID3D12GraphicsCommandList_DiscardResource(context.list, resources[i], NULL);

        gpu_h = ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(heap);
        gpu_h.ptr += ID3D12Device_GetDescriptorHandleIncrementSize(context.device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV) * 2 * i;
        ID3D12GraphicsCommandList_SetPipelineState(context.list, write_pso);
        ID3D12GraphicsCommandList_SetComputeRootSignature(context.list, context.root_signature);
        ID3D12GraphicsCommandList_SetDescriptorHeaps(context.list, 1, &heap);
        ID3D12GraphicsCommandList_SetComputeRootDescriptorTable(context.list, 0, gpu_h);
        ID3D12GraphicsCommandList_SetComputeRootUnorderedAccessView(context.list, 1, ID3D12Resource_GetGPUVirtualAddress(readback));
        ID3D12GraphicsCommandList_Dispatch(context.list, desc1.Width / 8, desc1.Height / 8, 1);
        uav_barrier(context.list, resources[i]);
        ID3D12GraphicsCommandList_SetPipelineState(context.list, read_pso);
        ID3D12GraphicsCommandList_Dispatch(context.list, desc1.Width / 8, desc1.Height / 8, 1);

        transition_resource_state(context.list, readback, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
        get_buffer_readback_with_command_list(readback, DXGI_FORMAT_UNKNOWN, &rb, context.queue, context.list);

        for (j = 0; j < 128; j++)
        {
            for (i = 0; i < 128; i++)
            {
                uint32_t value, reference;
                value = get_readback_uint(&rb, j * 128 + i, 0, 0);
                reference = j * 256 + i;
                reference |= reference << 16;
                ok(reference == value, "Pixel %u, %u mismatch: expected %u, got %u.\n", i, j, reference, value);
            }
        }

        release_resource_readback(&rb);
        reset_command_list(context.list, context.allocator);
    }
    vkd3d_test_set_context(NULL);

    ID3D12Heap_Release(place_heap);
    ID3D12Resource_Release(readback);
    for (i = 0; i < ARRAY_SIZE(resources); i++)
        if (resources[i])
            ID3D12Resource_Release(resources[i]);
    ID3D12Device12_Release(device12);
    ID3D12DescriptorHeap_Release(heap);
    ID3D12PipelineState_Release(write_pso);
    ID3D12PipelineState_Release(read_pso);
    destroy_test_context(&context);
}
