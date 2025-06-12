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

void test_enhanced_barrier_castable_dsv(void)
{
    enum { PSO_INDEX_FLOAT = 0, PSO_INDEX_UINT, PSO_INDEX_UINT16, PSO_INDEX_COUNT };
    D3D12_FEATURE_DATA_D3D12_OPTIONS12 features12;
    ID3D12PipelineState *psos[PSO_INDEX_COUNT];
    D3D12_UNORDERED_ACCESS_VIEW_DESC uav_desc;
    D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc;
    D3D12_DEPTH_STENCIL_VIEW_DESC dsv_desc;
    struct test_context_desc context_desc;
    D3D12_DESCRIPTOR_RANGE desc_range[2];
    ID3D12DescriptorHeap *resource_heap;
    D3D12_CPU_DESCRIPTOR_HANDLE cpu_h;
    D3D12_ROOT_SIGNATURE_DESC rs_desc;
    D3D12_ROOT_PARAMETER rs_params[1];
    D3D12_HEAP_PROPERTIES heap_props;
    ID3D12DescriptorHeap *dsv_heap;
    struct test_context context;
    D3D12_RESOURCE_DESC1 desc1;
    ID3D12Device10 *device10;
    ID3D12Resource *resource;
    ID3D12Resource *readback;
    unsigned int i;
    HRESULT hr;

    static const struct test
    {
        DXGI_FORMAT tex_format;
        DXGI_FORMAT dsv_format;
        DXGI_FORMAT srv_format;
        float clear_value;
        uint32_t reference_value;
        unsigned int pso_index;
    } tests[] = {
        { DXGI_FORMAT_D32_FLOAT, DXGI_FORMAT_D32_FLOAT, DXGI_FORMAT_R32_FLOAT, 1.0f, 0x3f800000, PSO_INDEX_FLOAT },
        { DXGI_FORMAT_D32_FLOAT, DXGI_FORMAT_D32_FLOAT, DXGI_FORMAT_R32_FLOAT, 0.5f, 0x3f000000, PSO_INDEX_FLOAT },
        { DXGI_FORMAT_D32_FLOAT, DXGI_FORMAT_D32_FLOAT, DXGI_FORMAT_R32_FLOAT, 0.0f, 0, PSO_INDEX_FLOAT },
        { DXGI_FORMAT_R32_TYPELESS, DXGI_FORMAT_D32_FLOAT, DXGI_FORMAT_R32_FLOAT, 1.0f, 0x3f800000, PSO_INDEX_FLOAT },
        { DXGI_FORMAT_R32_TYPELESS, DXGI_FORMAT_D32_FLOAT, DXGI_FORMAT_R32_FLOAT, 0.5f, 0x3f000000, PSO_INDEX_FLOAT },
        { DXGI_FORMAT_R32_TYPELESS, DXGI_FORMAT_D32_FLOAT, DXGI_FORMAT_R32_FLOAT, 0.0f, 0, PSO_INDEX_FLOAT },
        { DXGI_FORMAT_R32_FLOAT, DXGI_FORMAT_D32_FLOAT, DXGI_FORMAT_R32_FLOAT, 1.0f, 0x3f800000, PSO_INDEX_FLOAT },
        { DXGI_FORMAT_R32_FLOAT, DXGI_FORMAT_D32_FLOAT, DXGI_FORMAT_R32_FLOAT, 0.5f, 0x3f000000, PSO_INDEX_FLOAT },
        { DXGI_FORMAT_R32_FLOAT, DXGI_FORMAT_D32_FLOAT, DXGI_FORMAT_R32_FLOAT, 0.0f, 0, PSO_INDEX_FLOAT },
        { DXGI_FORMAT_D16_UNORM, DXGI_FORMAT_D16_UNORM, DXGI_FORMAT_R16_UNORM, 0.0f, 0, PSO_INDEX_FLOAT },
        { DXGI_FORMAT_D16_UNORM, DXGI_FORMAT_D16_UNORM, DXGI_FORMAT_R16_UNORM, 1.0f, 0x3f800000, PSO_INDEX_FLOAT },
        { DXGI_FORMAT_R16_TYPELESS, DXGI_FORMAT_D16_UNORM, DXGI_FORMAT_R16_UNORM, 0.0f, 0, PSO_INDEX_FLOAT },
        { DXGI_FORMAT_R16_TYPELESS, DXGI_FORMAT_D16_UNORM, DXGI_FORMAT_R16_UNORM, 1.0f, 0x3f800000, PSO_INDEX_FLOAT },
        { DXGI_FORMAT_R16_UNORM, DXGI_FORMAT_D16_UNORM, DXGI_FORMAT_R16_UNORM, 0.0f, 0, PSO_INDEX_FLOAT },
        { DXGI_FORMAT_R16_UNORM, DXGI_FORMAT_D16_UNORM, DXGI_FORMAT_R16_UNORM, 1.0f, 0x3f800000, PSO_INDEX_FLOAT },
#if 0
        /* The clear 1.0f cases fail on AMD, so we don't have to support it. */
        { DXGI_FORMAT_D32_FLOAT, DXGI_FORMAT_D32_FLOAT, DXGI_FORMAT_R32_UINT, 1.0f, 0x3f800000, PSO_INDEX_UINT },
        { DXGI_FORMAT_D32_FLOAT, DXGI_FORMAT_D32_FLOAT, DXGI_FORMAT_R32_UINT, 0.5f, 0x3f000000, PSO_INDEX_UINT },
        { DXGI_FORMAT_D32_FLOAT, DXGI_FORMAT_D32_FLOAT, DXGI_FORMAT_R32_UINT, 0.0f, 0, PSO_INDEX_UINT },
        { DXGI_FORMAT_D32_FLOAT, DXGI_FORMAT_D32_FLOAT, DXGI_FORMAT_R16G16_UINT, 1.0f, 0x3f800000, PSO_INDEX_UINT16 },
        { DXGI_FORMAT_D32_FLOAT, DXGI_FORMAT_D32_FLOAT, DXGI_FORMAT_R16G16_UINT, 0.5f, 0x3f000000, PSO_INDEX_UINT16 },
        { DXGI_FORMAT_D32_FLOAT, DXGI_FORMAT_D32_FLOAT, DXGI_FORMAT_R16G16_UINT, 0.0f, 0, PSO_INDEX_UINT16 },
        { DXGI_FORMAT_R32_TYPELESS, DXGI_FORMAT_D32_FLOAT, DXGI_FORMAT_R32_UINT, 1.0f, 0x3f800000, PSO_INDEX_UINT },
        { DXGI_FORMAT_R32_TYPELESS, DXGI_FORMAT_D32_FLOAT, DXGI_FORMAT_R32_UINT, 0.5f, 0x3f000000, PSO_INDEX_UINT },
        { DXGI_FORMAT_R32_TYPELESS, DXGI_FORMAT_D32_FLOAT, DXGI_FORMAT_R32_UINT, 0.0f, 0, PSO_INDEX_UINT },
        { DXGI_FORMAT_R32_TYPELESS, DXGI_FORMAT_D32_FLOAT, DXGI_FORMAT_R16G16_UINT, 1.0f, 0x3f800000, PSO_INDEX_UINT16 },
        { DXGI_FORMAT_R32_TYPELESS, DXGI_FORMAT_D32_FLOAT, DXGI_FORMAT_R16G16_UINT, 0.5f, 0x3f000000, PSO_INDEX_UINT16 },
        { DXGI_FORMAT_R32_TYPELESS, DXGI_FORMAT_D32_FLOAT, DXGI_FORMAT_R16G16_UINT, 0.0f, 0, PSO_INDEX_UINT16 },
#endif
    };

#if 0
    Texture2D<float> SRVFloat : register(t0);
    Texture2D<uint> SRVUint : register(t0);
    Texture2D<uint2> SRVUint16 : register(t0);
    RWTexture2D<uint> UAVUint32 : register(u0);

    [numthreads(8, 8, 1)]
    void ReadFloat(uint2 thr : SV_DispatchThreadID)
    {
        UAVUint32[thr] = asuint(SRVFloat[thr]);
    }

    [numthreads(8, 8, 1)]
    void ReadUint(uint2 thr : SV_DispatchThreadID)
    {
        UAVUint32[thr] = SRVUint[thr];
    }

    [numthreads(8, 8, 1)]
    void ReadUint16(uint2 thr : SV_DispatchThreadID)
    {
        uint2 v = SRVUint16[thr];
        UAVUint32[thr] = v.x | (v.y << 16);
    }
#endif

    static const DWORD read_float_dxbc[] =
    {
        0x43425844, 0x471fb384, 0x6ed7016b, 0x0d3ddbb9, 0x286bd3d3, 0x00000001, 0x00000110, 0x00000003,
        0x0000002c, 0x0000003c, 0x0000004c, 0x4e475349, 0x00000008, 0x00000000, 0x00000008, 0x4e47534f,
        0x00000008, 0x00000000, 0x00000008, 0x58454853, 0x000000bc, 0x00050050, 0x0000002f, 0x0100086a,
        0x04001858, 0x00107000, 0x00000000, 0x00005555, 0x0400189c, 0x0011e000, 0x00000000, 0x00004444,
        0x0200005f, 0x00020032, 0x02000068, 0x00000001, 0x0400009b, 0x00000008, 0x00000008, 0x00000001,
        0x04000036, 0x00100032, 0x00000000, 0x00020046, 0x08000036, 0x001000c2, 0x00000000, 0x00004002,
        0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x8900002d, 0x800000c2, 0x00155543, 0x00100012,
        0x00000000, 0x00100e46, 0x00000000, 0x00107e46, 0x00000000, 0x060000a4, 0x0011e0f2, 0x00000000,
        0x00020546, 0x00100006, 0x00000000, 0x0100003e,
    };

    static const DWORD read_uint_dxbc[] =
    {
        0x43425844, 0x7eedb2c6, 0x9f1524d0, 0xbf6128f2, 0x09358ca2, 0x00000001, 0x00000110, 0x00000003,
        0x0000002c, 0x0000003c, 0x0000004c, 0x4e475349, 0x00000008, 0x00000000, 0x00000008, 0x4e47534f,
        0x00000008, 0x00000000, 0x00000008, 0x58454853, 0x000000bc, 0x00050050, 0x0000002f, 0x0100086a,
        0x04001858, 0x00107000, 0x00000000, 0x00004444, 0x0400189c, 0x0011e000, 0x00000000, 0x00004444,
        0x0200005f, 0x00020032, 0x02000068, 0x00000001, 0x0400009b, 0x00000008, 0x00000008, 0x00000001,
        0x04000036, 0x00100032, 0x00000000, 0x00020046, 0x08000036, 0x001000c2, 0x00000000, 0x00004002,
        0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x8900002d, 0x800000c2, 0x00111103, 0x00100012,
        0x00000000, 0x00100e46, 0x00000000, 0x00107e46, 0x00000000, 0x060000a4, 0x0011e0f2, 0x00000000,
        0x00020546, 0x00100006, 0x00000000, 0x0100003e,

    };

    static const DWORD read_uint16_dxbc[] =
    {
        0x43425844, 0xac8268a7, 0x6ff5ff4b, 0xda3e1bbd, 0x675ab903, 0x00000001, 0x00000148, 0x00000003,
        0x0000002c, 0x0000003c, 0x0000004c, 0x4e475349, 0x00000008, 0x00000000, 0x00000008, 0x4e47534f,
        0x00000008, 0x00000000, 0x00000008, 0x58454853, 0x000000f4, 0x00050050, 0x0000003d, 0x0100086a,
        0x04001858, 0x00107000, 0x00000000, 0x00004444, 0x0400189c, 0x0011e000, 0x00000000, 0x00004444,
        0x0200005f, 0x00020032, 0x02000068, 0x00000001, 0x0400009b, 0x00000008, 0x00000008, 0x00000001,
        0x04000036, 0x00100032, 0x00000000, 0x00020046, 0x08000036, 0x001000c2, 0x00000000, 0x00004002,
        0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x8900002d, 0x800000c2, 0x00111103, 0x00100032,
        0x00000000, 0x00100e46, 0x00000000, 0x00107e46, 0x00000000, 0x07000029, 0x00100022, 0x00000000,
        0x0010001a, 0x00000000, 0x00004001, 0x00000010, 0x0700003c, 0x00100012, 0x00000000, 0x0010001a,
        0x00000000, 0x0010000a, 0x00000000, 0x060000a4, 0x0011e0f2, 0x00000000, 0x00020546, 0x00100006,
        0x00000000, 0x0100003e,
    };

    static const D3D12_SHADER_BYTECODE cs_read_float_dxbc = SHADER_BYTECODE(read_float_dxbc);
    static const D3D12_SHADER_BYTECODE cs_read_uint_dxbc = SHADER_BYTECODE(read_uint_dxbc);
    static const D3D12_SHADER_BYTECODE cs_read_uint16_dxbc = SHADER_BYTECODE(read_uint16_dxbc);

    memset(&context_desc, 0, sizeof(context_desc));
    context_desc.no_pipeline = true;
    context_desc.no_render_target = true;
    context_desc.no_root_signature = true;
    if (!init_test_context(&context, &context_desc))
        return;

    if (FAILED(hr = ID3D12Device_CheckFeatureSupport(context.device, D3D12_FEATURE_D3D12_OPTIONS12, &features12, sizeof(features12))) ||
        !features12.RelaxedFormatCastingSupported)
    {
        destroy_test_context(&context);
        skip("RelaxedFormatCasting is not supported.\n");
        return;
    }

    if (FAILED(ID3D12Device_QueryInterface(context.device, &IID_ID3D12Device10, (void **)&device10)))
    {
        destroy_test_context(&context);
        skip("ID3D12Device10 not supported, skipping test.\n");
        return;
    }

    memset(&rs_desc, 0, sizeof(rs_desc));
    memset(rs_params, 0, sizeof(rs_params));
    memset(desc_range, 0, sizeof(desc_range));
    rs_desc.NumParameters = ARRAY_SIZE(rs_params);
    rs_desc.pParameters = rs_params;
    rs_params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    rs_params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rs_params[0].DescriptorTable.NumDescriptorRanges = ARRAY_SIZE(desc_range);
    rs_params[0].DescriptorTable.pDescriptorRanges = desc_range;
    desc_range[0].NumDescriptors = 1;
    desc_range[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    desc_range[1].NumDescriptors = 1;
    desc_range[1].OffsetInDescriptorsFromTableStart = 1;
    desc_range[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;

    hr = create_root_signature(context.device, &rs_desc, &context.root_signature);
    ok(SUCCEEDED(hr), "Failed to create root signature, hr #%x.\n", hr);

    psos[PSO_INDEX_FLOAT] = create_compute_pipeline_state(context.device, context.root_signature, cs_read_float_dxbc);
    psos[PSO_INDEX_UINT] = create_compute_pipeline_state(context.device, context.root_signature, cs_read_uint_dxbc);
    psos[PSO_INDEX_UINT16] = create_compute_pipeline_state(context.device, context.root_signature, cs_read_uint16_dxbc);

    memset(&desc1, 0, sizeof(desc1));
    memset(&heap_props, 0, sizeof(heap_props));
    desc1.Width = 1024;
    desc1.Height = 1024;
    desc1.DepthOrArraySize = 1;
    desc1.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc1.MipLevels = 1;
    desc1.SampleDesc.Count = 1;
    desc1.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    desc1.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    heap_props.Type = D3D12_HEAP_TYPE_DEFAULT;

    resource_heap = create_gpu_descriptor_heap(context.device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 2);
    dsv_heap = create_cpu_descriptor_heap(context.device, D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 1);

    for (i = 0; i < ARRAY_SIZE(tests); i++)
    {
        DXGI_FORMAT castable_formats[2];
        UINT castable_format_count = 0;

        vkd3d_test_set_context("Test %u", i);
        castable_formats[castable_format_count++] = tests[i].srv_format;
        castable_formats[castable_format_count++] = tests[i].dsv_format;

        desc1.Format = tests[i].tex_format;
        hr = ID3D12Device10_CreateCommittedResource3(device10, &heap_props, D3D12_HEAP_FLAG_CREATE_NOT_ZEROED,
                &desc1, D3D12_BARRIER_LAYOUT_COMMON, NULL, NULL,
                castable_format_count, castable_formats,
                &IID_ID3D12Resource, (void **)&resource);
        ok(SUCCEEDED(hr), "Failed to create resource, hr #%x.\n", hr);

        cpu_h = ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(dsv_heap);

        memset(&dsv_desc, 0, sizeof(dsv_desc));
        dsv_desc.Format = tests[i].dsv_format;
        dsv_desc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;

        ID3D12Device_CreateDepthStencilView(context.device, resource, &dsv_desc, cpu_h);
        /* Transition away from enhanced barriers to legacy barriers. Input resource state must be COMMON. */
        transition_resource_state(context.list, resource, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_DEPTH_WRITE);
        ID3D12GraphicsCommandList_DiscardResource(context.list, resource, NULL);
        ID3D12GraphicsCommandList_ClearDepthStencilView(context.list, cpu_h, D3D12_CLEAR_FLAG_DEPTH, tests[i].clear_value, 0, 0, NULL);
        transition_resource_state(context.list, resource, D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

        cpu_h = ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(resource_heap);
        memset(&srv_desc, 0, sizeof(srv_desc));
        srv_desc.Format = tests[i].srv_format;
        srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srv_desc.Texture2D.MipLevels = 1;
        ID3D12Device_CreateShaderResourceView(context.device, resource, &srv_desc, cpu_h);

        readback = create_default_texture2d(context.device, desc1.Width, desc1.Height, 1, 1, DXGI_FORMAT_R32_UINT,
                D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

        cpu_h.ptr += ID3D12Device_GetDescriptorHandleIncrementSize(context.device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        memset(&uav_desc, 0, sizeof(uav_desc));
        uav_desc.Format = DXGI_FORMAT_R32_UINT;
        uav_desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        ID3D12Device_CreateUnorderedAccessView(context.device, readback, NULL, &uav_desc, cpu_h);

        ID3D12GraphicsCommandList_SetComputeRootSignature(context.list, context.root_signature);
        ID3D12GraphicsCommandList_SetDescriptorHeaps(context.list, 1, &resource_heap);
        ID3D12GraphicsCommandList_SetPipelineState(context.list, psos[tests[i].pso_index]);
        ID3D12GraphicsCommandList_SetComputeRootDescriptorTable(context.list, 0,
                ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(resource_heap));
        ID3D12GraphicsCommandList_Dispatch(context.list, desc1.Width / 8, desc1.Height / 8, 1);

        transition_resource_state(context.list, readback, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
        check_sub_resource_uint(readback, 0, context.queue, context.list, tests[i].reference_value, 0);

        ID3D12Resource_Release(resource);
        ID3D12Resource_Release(readback);
        reset_command_list(context.list, context.allocator);
    }
    vkd3d_test_set_context(NULL);

    ID3D12DescriptorHeap_Release(resource_heap);
    ID3D12DescriptorHeap_Release(dsv_heap);
    for (i = 0; i < ARRAY_SIZE(psos); i++)
        ID3D12PipelineState_Release(psos[i]);
    ID3D12Device10_Release(device10);
    destroy_test_context(&context);
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

void test_enhanced_barrier_buffer_transfer(void)
{
    D3D12_FEATURE_DATA_D3D12_OPTIONS12 features12;
    D3D12_GLOBAL_BARRIER global_barrier;
    D3D12_BUFFER_BARRIER buffer_barrier;
    D3D12_BARRIER_GROUP barrier_group;
    ID3D12GraphicsCommandList7 *list7;
    struct test_context_desc desc;
    struct test_context context;
    struct resource_readback rb;
    uint32_t input_data[64][64];
    ID3D12Resource *dst_overlap;
    ID3D12Resource *dst_serial;
    ID3D12Resource *src;
    unsigned int i, j;
    HRESULT hr;

    memset(&desc, 0, sizeof(desc));
    desc.no_pipeline = true;
    desc.no_render_target = true;
    desc.no_root_signature = true;
    if (!init_test_context(&context, &desc))
        return;

    if (FAILED(ID3D12Device_CheckFeatureSupport(context.device, D3D12_FEATURE_D3D12_OPTIONS12, &features12, sizeof(features12))) ||
        !features12.EnhancedBarriersSupported)
    {
        skip("Enhanced barriers not supported.\n");
        destroy_test_context(&context);
        return;
    }

    hr = ID3D12GraphicsCommandList_QueryInterface(context.list, &IID_ID3D12GraphicsCommandList7, (void **)&list7);
    ok(SUCCEEDED(hr), "Failed to query gcl7.\n");

    /* Verify that even for resources created with the new APIs, we cannot relax our tracking code. */
    dst_overlap = create_default_buffer2(context.device, sizeof(input_data), D3D12_RESOURCE_FLAG_NONE);
    dst_serial = create_default_buffer2(context.device, sizeof(input_data), D3D12_RESOURCE_FLAG_NONE);

    for (i = 0; i < 64; i++)
        for (j = 0; j < 64; j++)
            input_data[i][j] = i + 1;
    src = create_upload_buffer2(context.device, sizeof(input_data), input_data);

    memset(&barrier_group, 0, sizeof(barrier_group));
    memset(&global_barrier, 0, sizeof(global_barrier));
    memset(&buffer_barrier, 0, sizeof(buffer_barrier));
    barrier_group.Type = D3D12_BARRIER_TYPE_GLOBAL;
    barrier_group.NumBarriers = 1;
    barrier_group.pGlobalBarriers = &global_barrier;
    global_barrier.SyncBefore = D3D12_BARRIER_SYNC_COPY;
    global_barrier.AccessBefore = D3D12_BARRIER_ACCESS_COPY_DEST;
    global_barrier.SyncAfter = D3D12_BARRIER_SYNC_COPY;
    global_barrier.AccessAfter = D3D12_BARRIER_ACCESS_COPY_SOURCE;
    buffer_barrier.SyncBefore = D3D12_BARRIER_SYNC_COPY;
    buffer_barrier.AccessBefore = D3D12_BARRIER_ACCESS_COPY_DEST;
    buffer_barrier.SyncAfter = D3D12_BARRIER_SYNC_COPY;
    buffer_barrier.AccessAfter = D3D12_BARRIER_ACCESS_COPY_SOURCE;
    buffer_barrier.Size = UINT64_MAX;

    /* Overlapping test. */
    {
        for (i = 0; i < 64; i++)
        {
            /* Trip WAW hazards. We are still supposed to handle this gracefully. */
            ID3D12GraphicsCommandList_CopyBufferRegion(context.list, dst_overlap, 63 * sizeof(uint32_t) * i, src, 64 * sizeof(uint32_t) * i, 64 * sizeof(uint32_t));
        }
        ID3D12GraphicsCommandList7_Barrier(list7, 1, &barrier_group);
        get_buffer_readback_with_command_list(dst_overlap, DXGI_FORMAT_UNKNOWN, &rb, context.queue, context.list);
        for (i = 0; i < 64; i++)
        {
            for (j = 0; j < 63; j++)
            {
                uint32_t value, expected;
                value = get_readback_uint(&rb, i * 63 + j, 0, 0);
                expected = i + 1;
                ok(value == expected, "Index %u: expected %u, got %u.\n", i * 63 + j, expected, value);
            }
        }
        reset_command_list(context.list, context.allocator);
        release_resource_readback(&rb);
    }

    /* Serial test with self-copies. Here we must have barriers, or sync errors are observed. */
    {
        ID3D12GraphicsCommandList_CopyBufferRegion(context.list, dst_serial, 0, src, 0, 64 * sizeof(uint32_t));

        for (i = 1; i < 64; i++)
        {
            /* Alternate between global and buffer barriers for more test coverage. */
            if (i & 1)
            {
                barrier_group.Type = D3D12_BARRIER_TYPE_BUFFER;
                barrier_group.pBufferBarriers = &buffer_barrier;
                buffer_barrier.pResource = dst_serial;
            }
            else
            {
                barrier_group.Type = D3D12_BARRIER_TYPE_GLOBAL;
                barrier_group.pGlobalBarriers = &global_barrier;
            }

            ID3D12GraphicsCommandList7_Barrier(list7, 1, &barrier_group);
            ID3D12GraphicsCommandList_CopyBufferRegion(context.list, dst_serial, 64 * sizeof(uint32_t) * i, dst_serial, 64 * sizeof(uint32_t) * (i - 1), 64 * sizeof(uint32_t));
        }

        barrier_group.Type = D3D12_BARRIER_TYPE_GLOBAL;
        barrier_group.pGlobalBarriers = &global_barrier;
        ID3D12GraphicsCommandList7_Barrier(list7, 1, &barrier_group);

        get_buffer_readback_with_command_list(dst_serial, DXGI_FORMAT_UNKNOWN, &rb, context.queue, context.list);
        for (i = 0; i < 64 * 64; i++)
        {
            uint32_t value, expected;
            value = get_readback_uint(&rb, i, 0, 0);
            expected = 1;
            ok(value == expected, "Index %u: expected %u, got %u.\n", i, expected, value);
        }
        release_resource_readback(&rb);
    }

    ID3D12Resource_Release(dst_overlap);
    ID3D12Resource_Release(dst_serial);
    ID3D12Resource_Release(src);
    ID3D12GraphicsCommandList7_Release(list7);
    destroy_test_context(&context);
}

void test_enhanced_barrier_global_direct_queue_smoke(void)
{
    /* Attempt to use every possible stage / access pattern and make sure that we don't trip any validation.
     * It would be extremely tedious to write GPU work tests that depend on the exact barriers working.
     * It would also be almost impossible to test everything in a meaningful way.
     * The only way we can screw this up is if we mistranslate the stages / access masks for whatever reason. */
    D3D12_FEATURE_DATA_D3D12_OPTIONS12 features12;
    D3D12_FEATURE_DATA_D3D12_OPTIONS5 features5;
    D3D12_FEATURE_DATA_D3D12_OPTIONS6 features6;
    D3D12_BARRIER_GROUP barrier_group;
    ID3D12GraphicsCommandList7 *list7;
    struct test_context_desc desc;
    struct test_context context;
    unsigned int i;
    HRESULT hr;

#define B(s) { D3D12_BARRIER_SYNC_##s, D3D12_BARRIER_SYNC_##s, D3D12_BARRIER_ACCESS_NO_ACCESS, D3D12_BARRIER_ACCESS_NO_ACCESS }
#define BC(s) { D3D12_BARRIER_SYNC_##s, D3D12_BARRIER_SYNC_##s, D3D12_BARRIER_ACCESS_COMMON, D3D12_BARRIER_ACCESS_COMMON }
#define BA(s, a) { D3D12_BARRIER_SYNC_##s, D3D12_BARRIER_SYNC_##s, D3D12_BARRIER_ACCESS_##a, D3D12_BARRIER_ACCESS_##a }

    static const D3D12_GLOBAL_BARRIER barriers[] =
    {
        /* Exhaustively test all SYNC stages (except SPLIT, which deserves its own test). */
        B(ALL),
        B(DRAW),
        B(INDEX_INPUT),
        B(VERTEX_SHADING),
        B(PIXEL_SHADING),
        B(DEPTH_STENCIL),
        B(RENDER_TARGET),
        B(COMPUTE_SHADING),
        B(COPY),
        B(RESOLVE),
        B(EXECUTE_INDIRECT),
        B(PREDICATION),
        B(ALL_SHADING),
        B(NON_PIXEL_SHADING),
        BA(CLEAR_UNORDERED_ACCESS_VIEW, UNORDERED_ACCESS),

        BC(ALL),
        BC(DRAW),
        BC(INDEX_INPUT),
        BC(VERTEX_SHADING),
        BC(PIXEL_SHADING),
        BC(DEPTH_STENCIL),
        BC(RENDER_TARGET),
        BC(COMPUTE_SHADING),
        BC(COPY),
        BC(RESOLVE),
        BC(EXECUTE_INDIRECT),
        BC(PREDICATION),
        BC(ALL_SHADING),
        BC(NON_PIXEL_SHADING),
        /* COMMON is not compatible with ClearUAV stage in validation despite docs saying so. */

        /* Test access masks.
         * Reference: https://microsoft.github.io/DirectX-Specs/d3d/D3D12EnhancedBarriers.html#access-bits-barrier-sync-compatibility */
        BA(ALL, VERTEX_BUFFER),
        BA(VERTEX_SHADING, VERTEX_BUFFER),
        BA(DRAW, VERTEX_BUFFER),
        BA(ALL_SHADING, VERTEX_BUFFER),

        BA(ALL, CONSTANT_BUFFER),
        BA(VERTEX_SHADING, CONSTANT_BUFFER),
        BA(PIXEL_SHADING, CONSTANT_BUFFER),
        BA(COMPUTE_SHADING, CONSTANT_BUFFER),
        BA(DRAW, CONSTANT_BUFFER),
        BA(ALL_SHADING, CONSTANT_BUFFER),
        BA(NON_PIXEL_SHADING, CONSTANT_BUFFER), /* missing from list */

        BA(ALL, INDEX_BUFFER),
        BA(INDEX_INPUT, INDEX_BUFFER),
        BA(DRAW, INDEX_BUFFER),

        BA(ALL, RENDER_TARGET),
        /* BA(DRAW, RENDER_TARGET),  This is an error in validation despite being part of list */
        BA(RENDER_TARGET, RENDER_TARGET),

        BA(ALL, UNORDERED_ACCESS),
        BA(VERTEX_SHADING, UNORDERED_ACCESS),
        BA(PIXEL_SHADING, UNORDERED_ACCESS),
        BA(COMPUTE_SHADING, UNORDERED_ACCESS),
        BA(NON_PIXEL_SHADING, UNORDERED_ACCESS), /* missing from list */
        BA(DRAW, UNORDERED_ACCESS),
        BA(ALL_SHADING, UNORDERED_ACCESS),
        BA(CLEAR_UNORDERED_ACCESS_VIEW, UNORDERED_ACCESS),

        BA(ALL, DEPTH_STENCIL_WRITE),
        BA(DRAW, DEPTH_STENCIL_WRITE),
        BA(DEPTH_STENCIL, DEPTH_STENCIL_WRITE),

        BA(ALL, DEPTH_STENCIL_READ),
        BA(DRAW, DEPTH_STENCIL_READ),
        BA(DEPTH_STENCIL, DEPTH_STENCIL_READ),

        BA(ALL, SHADER_RESOURCE),
        BA(VERTEX_SHADING, SHADER_RESOURCE),
        BA(PIXEL_SHADING, SHADER_RESOURCE),
        BA(COMPUTE_SHADING, SHADER_RESOURCE),
        BA(DRAW, SHADER_RESOURCE),
        BA(ALL_SHADING, SHADER_RESOURCE),
        BA(NON_PIXEL_SHADING, SHADER_RESOURCE), /* missing from list */

        BA(ALL, STREAM_OUTPUT),
        BA(VERTEX_SHADING, STREAM_OUTPUT),
        BA(DRAW, STREAM_OUTPUT),
        BA(ALL_SHADING, STREAM_OUTPUT),
        BA(NON_PIXEL_SHADING, STREAM_OUTPUT), /* missing from list */

        BA(ALL, INDIRECT_ARGUMENT),
        BA(EXECUTE_INDIRECT, INDIRECT_ARGUMENT),
        BA(ALL, PREDICATION), /* dupe */
        BA(PREDICATION, PREDICATION),

        BA(ALL, COPY_DEST),
        BA(COPY, COPY_DEST),

        BA(ALL, COPY_SOURCE),
        BA(COPY, COPY_SOURCE),

        BA(ALL, RESOLVE_DEST),
        BA(RESOLVE, RESOLVE_DEST),

        BA(ALL, RESOLVE_SOURCE),
        BA(RESOLVE, RESOLVE_SOURCE),

        /* Ignore video decode/encode */
    };

    static const D3D12_GLOBAL_BARRIER barriers_vrs[] =
    {
        BA(ALL, SHADING_RATE_SOURCE),
        BA(PIXEL_SHADING, SHADING_RATE_SOURCE),
        BA(ALL_SHADING, SHADING_RATE_SOURCE),
    };

    static const D3D12_GLOBAL_BARRIER barriers_dxr[] =
    {
        B(RAYTRACING),
        B(BUILD_RAYTRACING_ACCELERATION_STRUCTURE),
        B(COPY_RAYTRACING_ACCELERATION_STRUCTURE),
        B(EMIT_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO),

        BC(RAYTRACING),
        BC(BUILD_RAYTRACING_ACCELERATION_STRUCTURE),
        BC(COPY_RAYTRACING_ACCELERATION_STRUCTURE),
        BC(EMIT_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO),

        BA(ALL, RAYTRACING_ACCELERATION_STRUCTURE_READ),
        BA(COMPUTE_SHADING, RAYTRACING_ACCELERATION_STRUCTURE_READ),
        BA(RAYTRACING, RAYTRACING_ACCELERATION_STRUCTURE_READ),
        BA(ALL_SHADING, RAYTRACING_ACCELERATION_STRUCTURE_READ),
        BA(NON_PIXEL_SHADING, RAYTRACING_ACCELERATION_STRUCTURE_READ), /* not part of list */
        /* Vertex / Pixel is banned for some reason. */
        BA(BUILD_RAYTRACING_ACCELERATION_STRUCTURE, RAYTRACING_ACCELERATION_STRUCTURE_READ),
        BA(COPY_RAYTRACING_ACCELERATION_STRUCTURE, RAYTRACING_ACCELERATION_STRUCTURE_READ),
        BA(EMIT_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO, RAYTRACING_ACCELERATION_STRUCTURE_READ),

        BA(ALL, RAYTRACING_ACCELERATION_STRUCTURE_WRITE),
        BA(COMPUTE_SHADING, RAYTRACING_ACCELERATION_STRUCTURE_WRITE), /* What? */
        BA(RAYTRACING, RAYTRACING_ACCELERATION_STRUCTURE_WRITE),
        BA(ALL_SHADING, RAYTRACING_ACCELERATION_STRUCTURE_WRITE),
        BA(NON_PIXEL_SHADING, RAYTRACING_ACCELERATION_STRUCTURE_WRITE), /* not part of list */
        /* Vertex / Pixel is banned for some reason. */

        BA(BUILD_RAYTRACING_ACCELERATION_STRUCTURE, RAYTRACING_ACCELERATION_STRUCTURE_WRITE),
        BA(COPY_RAYTRACING_ACCELERATION_STRUCTURE, RAYTRACING_ACCELERATION_STRUCTURE_WRITE),
    };
#undef B
#undef BC
#undef BA

    memset(&desc, 0, sizeof(desc));
    desc.no_pipeline = true;
    desc.no_render_target = true;
    desc.no_root_signature = true;
    if (!init_test_context(&context, &desc))
        return;

    if (FAILED(ID3D12Device_CheckFeatureSupport(context.device, D3D12_FEATURE_D3D12_OPTIONS12, &features12, sizeof(features12))) ||
            !features12.EnhancedBarriersSupported)
    {
        skip("Enhanced barriers not supported.\n");
        destroy_test_context(&context);
        return;
    }

    if (FAILED(ID3D12Device_CheckFeatureSupport(context.device, D3D12_FEATURE_D3D12_OPTIONS5, &features5, sizeof(features5))))
        memset(&features5, 0, sizeof(features5));

    if (FAILED(ID3D12Device_CheckFeatureSupport(context.device, D3D12_FEATURE_D3D12_OPTIONS6, &features6, sizeof(features6))))
        memset(&features6, 0, sizeof(features6));

    hr = ID3D12GraphicsCommandList_QueryInterface(context.list, &IID_ID3D12GraphicsCommandList7, (void **)&list7);
    ok(SUCCEEDED(hr), "Failed to query gcl7.\n");

    memset(&barrier_group, 0, sizeof(barrier_group));
    barrier_group.Type = D3D12_BARRIER_TYPE_GLOBAL;
    barrier_group.NumBarriers = 1;
    for (i = 0; i < ARRAY_SIZE(barriers); i++)
    {
        barrier_group.pGlobalBarriers = &barriers[i];
        ID3D12GraphicsCommandList7_Barrier(list7, 1, &barrier_group);
    }

    if (features6.VariableShadingRateTier >= D3D12_VARIABLE_SHADING_RATE_TIER_2)
    {
        for (i = 0; i < ARRAY_SIZE(barriers_vrs); i++)
        {
            barrier_group.pGlobalBarriers = &barriers_vrs[i];
            ID3D12GraphicsCommandList7_Barrier(list7, 1, &barrier_group);
        }
    }

    if (features5.RaytracingTier >= D3D12_RAYTRACING_TIER_1_0)
    {
        for (i = 0; i < ARRAY_SIZE(barriers_dxr); i++)
        {
            barrier_group.pGlobalBarriers = &barriers_dxr[i];
            ID3D12GraphicsCommandList7_Barrier(list7, 1, &barrier_group);
        }
    }

    ID3D12GraphicsCommandList7_Release(list7);
    ID3D12GraphicsCommandList7_Close(list7);
    destroy_test_context(&context);
}

void test_enhanced_barrier_split_barrier(void)
{
    /* Agility SDK 610 is completely broken w.r.t. split barriers. Even the most basic thing trips device lost due to bogus validation error.
     * Keep the test around for later however. */
    D3D12_FEATURE_DATA_D3D12_OPTIONS12 features12;
    D3D12_UNORDERED_ACCESS_VIEW_DESC uav_desc;
    D3D12_GLOBAL_BARRIER global_barrier;
    D3D12_BUFFER_BARRIER buffer_barrier;
    D3D12_BARRIER_GROUP barrier_group;
    ID3D12GraphicsCommandList7 *list7;
    ID3D12DescriptorHeap *gpu_heap;
    ID3D12DescriptorHeap *cpu_heap;
    ID3D12Resource *clear_resource;
    ID3D12Resource *read_resource;
    struct test_context_desc desc;
    struct resource_readback rb;
    struct test_context context;
    unsigned int i, j;
    HRESULT hr;

    memset(&desc, 0, sizeof(desc));
    desc.no_pipeline = true;
    desc.no_render_target = true;
    desc.no_root_signature = true;
    if (!init_test_context(&context, &desc))
        return;

    if (FAILED(ID3D12Device_CheckFeatureSupport(context.device, D3D12_FEATURE_D3D12_OPTIONS12, &features12, sizeof(features12))) ||
            !features12.EnhancedBarriersSupported)
    {
        skip("Enhanced barriers not supported.\n");
        destroy_test_context(&context);
        return;
    }

    hr = ID3D12GraphicsCommandList_QueryInterface(context.list, &IID_ID3D12GraphicsCommandList7, (void **)&list7);
    ok(SUCCEEDED(hr), "Failed to query gcl7.\n");

    memset(&global_barrier, 0, sizeof(global_barrier));
    memset(&buffer_barrier, 0, sizeof(buffer_barrier));
    memset(&barrier_group, 0, sizeof(barrier_group));

    gpu_heap = create_gpu_descriptor_heap(context.device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1);
    cpu_heap = create_cpu_descriptor_heap(context.device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1);

    clear_resource = create_default_buffer2(context.device, 4 * 64 * 1024 * sizeof(uint32_t), D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    read_resource = create_default_buffer2(context.device, 4 * 64 * 1024 * sizeof(uint32_t), D3D12_RESOURCE_FLAG_NONE);

    memset(&uav_desc, 0, sizeof(uav_desc));
    uav_desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    uav_desc.Format = DXGI_FORMAT_R32_UINT;
    uav_desc.Buffer.NumElements = 4 * 64 * 1024;
    ID3D12Device_CreateUnorderedAccessView(context.device, clear_resource, NULL,
            &uav_desc, ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(gpu_heap));
    ID3D12Device_CreateUnorderedAccessView(context.device, clear_resource, NULL,
            &uav_desc, ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(cpu_heap));
    ID3D12GraphicsCommandList_SetDescriptorHeaps(context.list, 1, &gpu_heap);

    /* The most basic tests break on AgilitySDK 610. TODO: Flesh this out later. */
    {
        UINT uint_values[4] = { 0 };
        D3D12_RECT rect;

        barrier_group.Type = D3D12_BARRIER_TYPE_BUFFER;
        barrier_group.NumBarriers = 1;
        barrier_group.pBufferBarriers = &buffer_barrier;
        buffer_barrier.SyncBefore = D3D12_BARRIER_SYNC_CLEAR_UNORDERED_ACCESS_VIEW;
        buffer_barrier.SyncAfter = D3D12_BARRIER_SYNC_SPLIT;
        buffer_barrier.AccessBefore = D3D12_BARRIER_ACCESS_UNORDERED_ACCESS;
        buffer_barrier.AccessAfter = D3D12_BARRIER_ACCESS_COPY_SOURCE;
        buffer_barrier.pResource = clear_resource;
        buffer_barrier.Size = UINT64_MAX;

        for (i = 0; i < 4; i++)
        {
            uint_values[0] = i + 1;
            set_rect(&rect, i * 64 * 1024, 0, (i + 1) * 64 * 1024, 1);
            ID3D12GraphicsCommandList_ClearUnorderedAccessViewUint(context.list,
                    ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(gpu_heap),
                    ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(cpu_heap),
                    clear_resource, uint_values, 1, &rect);

#if 0
            /* This trips device lost with nonsense validation errors about SYNC not supporting AccessAfter ... <_< */
            buffer_barrier.SyncBefore = D3D12_BARRIER_SYNC_CLEAR_UNORDERED_ACCESS_VIEW;
            buffer_barrier.SyncAfter = D3D12_BARRIER_SYNC_SPLIT;
            buffer_barrier.AccessBefore = D3D12_BARRIER_ACCESS_UNORDERED_ACCESS;
            buffer_barrier.AccessAfter = D3D12_BARRIER_ACCESS_COPY_SOURCE;
            ID3D12GraphicsCommandList7_Barrier(list7, 1, &barrier_group);

            buffer_barrier.SyncBefore = D3D12_BARRIER_SYNC_SPLIT;
            buffer_barrier.SyncAfter = D3D12_BARRIER_SYNC_COPY;
            buffer_barrier.AccessBefore = D3D12_BARRIER_ACCESS_UNORDERED_ACCESS;
            buffer_barrier.AccessAfter = D3D12_BARRIER_ACCESS_COPY_SOURCE;
            ID3D12GraphicsCommandList7_Barrier(list7, 1, &barrier_group);
#else
            buffer_barrier.SyncBefore = D3D12_BARRIER_SYNC_CLEAR_UNORDERED_ACCESS_VIEW;
            buffer_barrier.SyncAfter = D3D12_BARRIER_SYNC_COPY;
            buffer_barrier.AccessBefore = D3D12_BARRIER_ACCESS_UNORDERED_ACCESS;
            buffer_barrier.AccessAfter = D3D12_BARRIER_ACCESS_COPY_SOURCE;
            ID3D12GraphicsCommandList7_Barrier(list7, 1, &barrier_group);
#endif

            ID3D12GraphicsCommandList_CopyBufferRegion(context.list, read_resource, 64 * 1024 * i * sizeof(uint32_t),
                    clear_resource, 64 * 1024 * i * sizeof(uint32_t), 64 * 1024 * sizeof(uint32_t));
        }

        buffer_barrier.SyncBefore = D3D12_BARRIER_SYNC_COPY;
        buffer_barrier.SyncAfter = D3D12_BARRIER_SYNC_COPY;
        buffer_barrier.AccessBefore = D3D12_BARRIER_ACCESS_COPY_DEST;
        buffer_barrier.AccessAfter = D3D12_BARRIER_ACCESS_COPY_SOURCE;
        buffer_barrier.pResource = read_resource;
        ID3D12GraphicsCommandList7_Barrier(list7, 1, &barrier_group);

        get_buffer_readback_with_command_list(read_resource, DXGI_FORMAT_UNKNOWN, &rb, context.queue, context.list);

        for (i = 0; i < 4; i++)
        {
            for (j = 0; j < 64 * 1024; j++)
            {
                uint32_t value, expected;
                value = get_readback_uint(&rb, i * 64 * 1024 + j, 0, 0);
                expected = i + 1;
                ok(value == expected, "Copy %u, elem %u, expected %u, got %u.\n", i, j, expected, value);
            }
        }

        reset_command_list(context.list, context.allocator);
        release_resource_readback(&rb);
    }

    ID3D12DescriptorHeap_Release(gpu_heap);
    ID3D12DescriptorHeap_Release(cpu_heap);
    ID3D12Resource_Release(clear_resource);
    ID3D12Resource_Release(read_resource);
    ID3D12GraphicsCommandList7_Release(list7);
    destroy_test_context(&context);
}

void test_enhanced_barrier_discard_behavior(void)
{
    static const FLOAT white[] = { 1.0f, 1.0f, 1.0f, 1.0f };
    D3D12_FEATURE_DATA_D3D12_OPTIONS12 features12;
    D3D12_RESOURCE_ALLOCATION_INFO alloc_info;
    ID3D12GraphicsCommandList7 *list7;
    ID3D12DescriptorHeap *rtv_heap;
    struct test_context_desc desc;
    D3D12_TEXTURE_BARRIER barrier;
    struct test_context context;
    struct resource_readback rb;
    D3D12_RESOURCE_DESC1 desc1;
    D3D12_BARRIER_GROUP group;
    D3D12_HEAP_DESC heap_desc;
    ID3D12Device10 *device10;
    ID3D12Resource *rtv;
    ID3D12Heap *heap;
    HRESULT hr;

    memset(&desc, 0, sizeof(desc));
    desc.no_pipeline = true;
    desc.no_render_target = true;
    desc.no_root_signature = true;
    if (!init_test_context(&context, &desc))
        return;

    if (FAILED(ID3D12Device_CheckFeatureSupport(context.device, D3D12_FEATURE_D3D12_OPTIONS12, &features12, sizeof(features12))) ||
            !features12.EnhancedBarriersSupported)
    {
        skip("Enhanced barriers not supported.\n");
        destroy_test_context(&context);
        return;
    }

    hr = ID3D12GraphicsCommandList_QueryInterface(context.list, &IID_ID3D12GraphicsCommandList7, (void **)&list7);
    ok(SUCCEEDED(hr), "Failed to query gcl7.\n");

    ID3D12Device_QueryInterface(context.device, &IID_ID3D12Device10, (void **)&device10);

    /* Force placed resources, since committed resources can often just ignore metadata clears, etc.
     * This is the case on AMD it seems. */
    memset(&desc1, 0, sizeof(desc1));
    desc1.Width = 1024;
    desc1.Height = 1024;
    desc1.DepthOrArraySize = 1;
    desc1.MipLevels = 1;
    desc1.SampleDesc.Count = 1;
    desc1.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc1.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc1.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    desc1.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    alloc_info = ID3D12Device10_GetResourceAllocationInfo2(device10, 0, 1, &desc1, NULL);

    memset(&heap_desc, 0, sizeof(heap_desc));
    heap_desc.Properties.Type = D3D12_HEAP_TYPE_DEFAULT;
    heap_desc.SizeInBytes = alloc_info.SizeInBytes;
    heap_desc.Flags = D3D12_HEAP_FLAG_ALLOW_ONLY_RT_DS_TEXTURES;
    hr = ID3D12Device_CreateHeap(context.device, &heap_desc, &IID_ID3D12Heap, (void **)&heap);
    ok(SUCCEEDED(hr), "Failed to create heap, hr #%x.\n", hr);

    hr = ID3D12Device10_CreatePlacedResource2(device10, heap, 0, &desc1, D3D12_BARRIER_LAYOUT_RENDER_TARGET, NULL, 0, NULL, &IID_ID3D12Resource, (void **)&rtv);
    ok(SUCCEEDED(hr), "Failed to create resource, hr #%x.\n", hr);

    rtv_heap = create_cpu_descriptor_heap(context.device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1);
    ID3D12Device_CreateRenderTargetView(context.device, rtv, NULL,
            ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(rtv_heap));

    memset(&barrier, 0, sizeof(barrier));
    group.NumBarriers = 1;
    group.Type = D3D12_BARRIER_TYPE_TEXTURE;
    group.pTextureBarriers = &barrier;
    barrier.pResource = rtv;
    barrier.LayoutBefore = D3D12_BARRIER_LAYOUT_UNDEFINED;
    barrier.LayoutAfter = D3D12_BARRIER_LAYOUT_RENDER_TARGET;
    barrier.SyncBefore = D3D12_BARRIER_SYNC_NONE;
    barrier.SyncAfter = D3D12_BARRIER_SYNC_RENDER_TARGET;
    barrier.AccessBefore = D3D12_BARRIER_ACCESS_NO_ACCESS;
    barrier.AccessAfter = D3D12_BARRIER_ACCESS_RENDER_TARGET;
    ID3D12GraphicsCommandList7_Barrier(list7, 1, &group);

    ID3D12GraphicsCommandList_ClearRenderTargetView(context.list, ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(rtv_heap), white, 0, NULL);

    /* Enhanced barriers are really weird in the sense that there is a separate DISCARD flag, unrelated to the layout.
     * We need to know if UNDEFINED -> blah transitions are allowed to actually discard or not. */

    /* Flush cache. UNDEFINED requires ACCESS_NO_ACCESS unless both layouts are UNDEFINED. */
    barrier.LayoutBefore = D3D12_BARRIER_LAYOUT_UNDEFINED;
    barrier.LayoutAfter = D3D12_BARRIER_LAYOUT_UNDEFINED;
    barrier.SyncBefore = D3D12_BARRIER_SYNC_RENDER_TARGET;
    barrier.SyncAfter = D3D12_BARRIER_SYNC_RENDER_TARGET;
    barrier.AccessBefore = D3D12_BARRIER_ACCESS_RENDER_TARGET;
    barrier.AccessAfter = D3D12_BARRIER_ACCESS_NO_ACCESS;
    ID3D12GraphicsCommandList7_Barrier(list7, 1, &group);

    /* Try soft-discarding. */
    barrier.LayoutBefore = D3D12_BARRIER_LAYOUT_UNDEFINED;
    barrier.LayoutAfter = D3D12_BARRIER_LAYOUT_RENDER_TARGET;
    barrier.SyncBefore = D3D12_BARRIER_SYNC_RENDER_TARGET;
    barrier.SyncAfter = D3D12_BARRIER_SYNC_RENDER_TARGET;
    barrier.AccessBefore = D3D12_BARRIER_ACCESS_NO_ACCESS;
    barrier.AccessAfter = D3D12_BARRIER_ACCESS_RENDER_TARGET; /* D3D12 refuses NO_ACCESS here, so much for Vulkan compat :) */
    /* With this flag, we observe that AMD clears to 0 iff the resource is PLACED. */
    /* barrier.Flags = D3D12_TEXTURE_BARRIER_FLAG_DISCARD; */
    ID3D12GraphicsCommandList7_Barrier(list7, 1, &group);

    barrier.LayoutBefore = D3D12_BARRIER_LAYOUT_RENDER_TARGET;
    barrier.LayoutAfter = D3D12_BARRIER_LAYOUT_COPY_SOURCE;
    barrier.SyncBefore = D3D12_BARRIER_SYNC_RENDER_TARGET;
    barrier.SyncAfter = D3D12_BARRIER_SYNC_COPY;
    barrier.AccessBefore = D3D12_BARRIER_ACCESS_RENDER_TARGET; /* D3D12 refuses NO_ACCESS here, so much for Vulkan compat :) */
    barrier.AccessAfter = D3D12_BARRIER_ACCESS_COPY_SOURCE;
    ID3D12GraphicsCommandList7_Barrier(list7, 1, &group);

    get_texture_readback_with_command_list(rtv, 0, &rb, context.queue, context.list);

    /* The value is preserved here. This proves that we cannot do UNDEFINED -> blah transitions in Vulkan unless
     * DISCARD flag is also used. */
    {
        uint32_t v = get_readback_uint(&rb, 0, 0, 0);

        /* The spec is extremely vague here though. It might be okay to discard anyways?
         * vkd3d-proton will discard here at any rate ... */
        todo ok(v == ~0u, "Unexpected value #%x.\n", v);
    }

    release_resource_readback(&rb);
    ID3D12DescriptorHeap_Release(rtv_heap);
    ID3D12GraphicsCommandList7_Release(list7);
    ID3D12Resource_Release(rtv);
    ID3D12Device10_Release(device10);
    ID3D12Heap_Release(heap);
    destroy_test_context(&context);
}

void test_enhanced_barrier_subresource(void)
{
    static const FLOAT black[] = { 0.0f, 0.0f, 0.0f, 0.0f };
    static const FLOAT white[] = { 1.0f, 1.0f, 1.0f, 1.0f };
    D3D12_FEATURE_DATA_D3D12_OPTIONS12 features12;
    D3D12_RENDER_TARGET_VIEW_DESC rtv_desc;
    ID3D12GraphicsCommandList7 *list7;
    D3D12_TEXTURE_BARRIER barrier[4];
    ID3D12DescriptorHeap *rtv_heap;
    struct test_context_desc desc;
    D3D12_BARRIER_GROUP group[2];
    struct test_context context;
    struct resource_readback rb;
    ID3D12Resource *readback;
    ID3D12Resource *rtv;
    unsigned int i;
    HRESULT hr;

    memset(&desc, 0, sizeof(desc));
    desc.no_pipeline = true;
    desc.no_render_target = true;
    desc.no_root_signature = true;
    if (!init_test_context(&context, &desc))
        return;

    if (FAILED(ID3D12Device_CheckFeatureSupport(context.device, D3D12_FEATURE_D3D12_OPTIONS12, &features12, sizeof(features12))) ||
            !features12.EnhancedBarriersSupported)
    {
        skip("Enhanced barriers not supported.\n");
        destroy_test_context(&context);
        return;
    }

    hr = ID3D12GraphicsCommandList_QueryInterface(context.list, &IID_ID3D12GraphicsCommandList7, (void **)&list7);
    ok(SUCCEEDED(hr), "Failed to query gcl7.\n");

    rtv = create_default_texture2d_enhanced(context.device, 1024, 1024, 4, 4, DXGI_FORMAT_R32_UINT,
            D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET, D3D12_BARRIER_LAYOUT_UNDEFINED);
    rtv_heap = create_cpu_descriptor_heap(context.device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 4 * 4);

    readback = create_default_texture2d_enhanced(context.device, 1024, 1024, 4, 4, DXGI_FORMAT_R32_UINT,
            D3D12_RESOURCE_FLAG_NONE, D3D12_BARRIER_LAYOUT_COMMON /* Should auto-promote to COPY_DEST */);

    for (i = 0; i < 4 * 4; i++)
    {
        memset(&rtv_desc, 0, sizeof(rtv_desc));
        rtv_desc.Format = DXGI_FORMAT_R32_UINT;
        rtv_desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DARRAY;
        rtv_desc.Texture2DArray.ArraySize = 1;
        rtv_desc.Texture2DArray.FirstArraySlice = i / 4;
        rtv_desc.Texture2DArray.MipSlice = i % 4;
        rtv_desc.Texture2DArray.PlaneSlice = 0;

        ID3D12Device_CreateRenderTargetView(context.device, rtv, &rtv_desc,
                get_cpu_handle(context.device, rtv_heap, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, i));
    }

    memset(barrier, 0, sizeof(barrier));
    memset(group, 0, sizeof(group));
    group[0].NumBarriers = 1;
    group[0].Type = D3D12_BARRIER_TYPE_TEXTURE;
    group[0].pTextureBarriers = barrier;
    barrier[0].pResource = rtv;
    barrier[0].LayoutBefore = D3D12_BARRIER_LAYOUT_UNDEFINED;
    barrier[0].LayoutAfter = D3D12_BARRIER_LAYOUT_RENDER_TARGET;
    barrier[0].SyncBefore = D3D12_BARRIER_SYNC_NONE;
    barrier[0].SyncAfter = D3D12_BARRIER_SYNC_RENDER_TARGET;
    barrier[0].AccessBefore = D3D12_BARRIER_ACCESS_NO_ACCESS;
    barrier[0].AccessAfter = D3D12_BARRIER_ACCESS_RENDER_TARGET;
    /* MipLevels == 0, flags this as subresource index, and -1 means all subresources. */
    barrier[0].Subresources.IndexOrFirstMipLevel = -1;
    ID3D12GraphicsCommandList7_Barrier(list7, 1, group);

    /* Activate all subresources. */
    for (i = 0; i < 4 * 4; i++)
        ID3D12GraphicsCommandList_ClearRenderTargetView(context.list, get_cpu_handle(context.device, rtv_heap, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, i), black, 0, NULL);

    /* Enhanced barriers are really weird in the sense that there is a separate DISCARD flag, unrelated to the layout.
     * We need to know if UNDEFINED -> blah transitions are allowed to actually discard or not. */

    /* Try different edge cases of the subresources struct. */

    /* NumPlanes = 0 -> nothing happens */
    {
        for (i = 0; i < ARRAY_SIZE(barrier); i++)
        {
            barrier[i].LayoutBefore = D3D12_BARRIER_LAYOUT_RENDER_TARGET;
            barrier[i].LayoutAfter = D3D12_BARRIER_LAYOUT_COPY_SOURCE;
            barrier[i].SyncBefore = D3D12_BARRIER_SYNC_RENDER_TARGET;
            barrier[i].SyncAfter = D3D12_BARRIER_SYNC_COPY;
            barrier[i].AccessBefore = D3D12_BARRIER_ACCESS_RENDER_TARGET;
            barrier[i].AccessAfter = D3D12_BARRIER_ACCESS_COPY_SOURCE;
            barrier[i].pResource = rtv;
        }

        /* Here, PlaneCount is 0, so this should be a no-op. */
        for (i = 0; i < ARRAY_SIZE(barrier); i++)
        {
            barrier[i].Subresources.IndexOrFirstMipLevel = 0;
            barrier[i].Subresources.NumMipLevels = 1;
            barrier[i].Subresources.NumArraySlices = 1;
            barrier[i].Subresources.NumPlanes = 0;
        }

        group[0].NumBarriers = ARRAY_SIZE(barrier);
        ID3D12GraphicsCommandList7_Barrier(list7, 1, group);
        /* This passes validation. */
        ID3D12GraphicsCommandList7_Barrier(list7, 1, group);
    }

    /* NumArraySlices == 0 -> 1 slice (?!?!?!) */
    {
        barrier[0].LayoutBefore = D3D12_BARRIER_LAYOUT_RENDER_TARGET;
        barrier[0].LayoutAfter = D3D12_BARRIER_LAYOUT_COPY_SOURCE;
        barrier[0].SyncBefore = D3D12_BARRIER_SYNC_RENDER_TARGET;
        barrier[0].SyncAfter = D3D12_BARRIER_SYNC_COPY;
        barrier[0].AccessBefore = D3D12_BARRIER_ACCESS_RENDER_TARGET;
        barrier[0].AccessAfter = D3D12_BARRIER_ACCESS_COPY_SOURCE;

        barrier[0].Subresources.IndexOrFirstMipLevel = 0;
        barrier[0].Subresources.NumMipLevels = 1;
        barrier[0].Subresources.NumArraySlices = 0; /* Apparently, this means 1 layer. */
        barrier[0].Subresources.NumPlanes = 1;
        group[0].NumBarriers = 1;
        ID3D12GraphicsCommandList7_Barrier(list7, 1, group);

        barrier[0].Subresources.NumArraySlices = 1;
        barrier[0].LayoutBefore = D3D12_BARRIER_LAYOUT_COPY_SOURCE;
        barrier[0].LayoutAfter = D3D12_BARRIER_LAYOUT_RENDER_TARGET;
        barrier[0].SyncBefore = D3D12_BARRIER_SYNC_COPY;
        barrier[0].SyncAfter = D3D12_BARRIER_SYNC_RENDER_TARGET;
        barrier[0].AccessBefore = D3D12_BARRIER_ACCESS_COPY_SOURCE;
        barrier[0].AccessAfter = D3D12_BARRIER_ACCESS_RENDER_TARGET;
        ID3D12GraphicsCommandList7_Barrier(list7, 1, group);
    }

    /* -1 ArraySlices is not allowed. Trips validation. */
    /* -1 MipLevels is not allowed. Trips validation. */
    /* -1 NumPlanes is not allowed. Trips validation. */
    /* Cannot test MipLevels == 0, because that signals use of subresource index. */

    /* Test chained transition. */
    {
        barrier[0].LayoutBefore = D3D12_BARRIER_LAYOUT_RENDER_TARGET;
        barrier[0].LayoutAfter = D3D12_BARRIER_LAYOUT_COPY_SOURCE;
        barrier[0].SyncBefore = D3D12_BARRIER_SYNC_RENDER_TARGET;
        barrier[0].SyncAfter = D3D12_BARRIER_SYNC_COPY;
        barrier[0].AccessBefore = D3D12_BARRIER_ACCESS_RENDER_TARGET;
        barrier[0].AccessAfter = D3D12_BARRIER_ACCESS_COPY_SOURCE;
        barrier[0].Subresources.IndexOrFirstMipLevel = -1;
        barrier[0].Subresources.NumMipLevels = 0;
        barrier[0].Subresources.NumPlanes = 1;

        barrier[1].LayoutBefore = D3D12_BARRIER_LAYOUT_COPY_SOURCE;
        barrier[1].LayoutAfter = D3D12_BARRIER_LAYOUT_RENDER_TARGET;
        barrier[1].SyncBefore = D3D12_BARRIER_SYNC_COPY;
        barrier[1].SyncAfter = D3D12_BARRIER_SYNC_RENDER_TARGET;
        barrier[1].AccessBefore = D3D12_BARRIER_ACCESS_COPY_SOURCE;
        barrier[1].AccessAfter = D3D12_BARRIER_ACCESS_RENDER_TARGET;
        barrier[1].Subresources.IndexOrFirstMipLevel = -1;
        barrier[1].Subresources.NumMipLevels = 0;
        barrier[1].Subresources.NumPlanes = 1;

        /* Single group style. */
        group[0].NumBarriers = 2;
        ID3D12GraphicsCommandList7_Barrier(list7, 1, group);
        ID3D12GraphicsCommandList7_Barrier(list7, 1, group);

        /* Multiple group style. */
        group[0].NumBarriers = 1;
        group[1].NumBarriers = 1;
        group[1].pTextureBarriers = &barrier[1];
        group[1].Type = D3D12_BARRIER_TYPE_TEXTURE;
        ID3D12GraphicsCommandList7_Barrier(list7, 2, group);
        ID3D12GraphicsCommandList7_Barrier(list7, 2, group);
    }

    for (i = 0; i < 4 * 4; i++)
        ID3D12GraphicsCommandList_ClearRenderTargetView(context.list, get_cpu_handle(context.device, rtv_heap, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, i), white, 0, NULL);

    /* Complicated way of transitioning all subresources. */
    {
        barrier[0].LayoutBefore = D3D12_BARRIER_LAYOUT_RENDER_TARGET;
        barrier[0].LayoutAfter = D3D12_BARRIER_LAYOUT_COPY_SOURCE;
        barrier[0].SyncBefore = D3D12_BARRIER_SYNC_RENDER_TARGET;
        barrier[0].SyncAfter = D3D12_BARRIER_SYNC_COPY;
        barrier[0].AccessBefore = D3D12_BARRIER_ACCESS_RENDER_TARGET;
        barrier[0].AccessAfter = D3D12_BARRIER_ACCESS_COPY_SOURCE;
        barrier[0].Subresources.NumPlanes = 1;
        barrier[0].Subresources.NumMipLevels = 2;
        barrier[0].Subresources.NumArraySlices = 2;

        group[0].NumBarriers = 4;

        for (i = 0; i < 4; i++)
        {
            if (i != 0)
                barrier[i] = barrier[0];
            barrier[i].Subresources.IndexOrFirstMipLevel = (i & 1) * 2;
            barrier[i].Subresources.FirstArraySlice = i & 2;
        }

        ID3D12GraphicsCommandList7_Barrier(list7, 1, group);
    }

    for (i = 0; i < 4 * 4; i++)
    {
        D3D12_TEXTURE_COPY_LOCATION dst_loc, src_loc;
        D3D12_BOX src_box;

        dst_loc.pResource = readback;
        dst_loc.SubresourceIndex = i;
        dst_loc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;

        src_loc.pResource = rtv;
        src_loc.SubresourceIndex = i;
        src_loc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;

        set_box(&src_box, 0, 0, 0, 1024 >> (i & 3), 1024 >> (i & 3), 1);
        ID3D12GraphicsCommandList_CopyTextureRegion(context.list, &dst_loc, 0, 0, 0, &src_loc, &src_box);
    }

    /* Validation does not complain about COPY_SOURCE use after COMMON dst copy (wtf ...), but it *does* if initial layout is COPY_DEST.
     * We might be expected to deal with it (we already do anyways since transfer layouts are a mess in D3D12). */

    for (i = 0; i < 4 * 4; i++)
    {
        uint32_t value;
        get_texture_readback_with_command_list(readback, i, &rb, context.queue, context.list);
        value = get_readback_uint(&rb, 100, 100, 0);
        ok(value == 1, "Subresource %u: Expected %u, got %u.\n", i, 1, value);
        reset_command_list(context.list, context.allocator);
        release_resource_readback(&rb);
    }

    ID3D12Resource_Release(readback);
    ID3D12DescriptorHeap_Release(rtv_heap);
    ID3D12GraphicsCommandList7_Release(list7);
    ID3D12Resource_Release(rtv);
    destroy_test_context(&context);
}

void test_enhanced_barrier_self_copy(void)
{
    static const FLOAT gray[] = { 127.0f / 255.0f, 127.0f / 255.0f, 127.0f / 255.0f, 127.0f / 255.0f };
    static const FLOAT white[] = { 1.0f, 1.0f, 1.0f, 1.0f };
    D3D12_FEATURE_DATA_D3D12_OPTIONS12 features12;
    D3D12_RESOURCE_ALLOCATION_INFO alloc_info;
    ID3D12GraphicsCommandList7 *list7;
    ID3D12DescriptorHeap *rtv_heap;
    struct test_context_desc desc;
    D3D12_TEXTURE_BARRIER barrier;
    struct test_context context;
    D3D12_RESOURCE_DESC1 desc1;
    D3D12_BARRIER_GROUP group;
    D3D12_HEAP_DESC heap_desc;
    ID3D12Device10 *device10;
    ID3D12Resource *rtv;
    ID3D12Heap *heap;
    D3D12_RECT rect;
    unsigned int i;
    HRESULT hr;

    memset(&desc, 0, sizeof(desc));
    desc.no_pipeline = true;
    desc.no_render_target = true;
    desc.no_root_signature = true;
    if (!init_test_context(&context, &desc))
        return;

    if (FAILED(ID3D12Device_CheckFeatureSupport(context.device, D3D12_FEATURE_D3D12_OPTIONS12, &features12, sizeof(features12))) ||
        !features12.EnhancedBarriersSupported)
    {
        skip("Enhanced barriers not supported.\n");
        destroy_test_context(&context);
        return;
    }

    hr = ID3D12GraphicsCommandList_QueryInterface(context.list, &IID_ID3D12GraphicsCommandList7, (void **)&list7);
    ok(SUCCEEDED(hr), "Failed to query gcl7.\n");

    ID3D12Device_QueryInterface(context.device, &IID_ID3D12Device10, (void **)&device10);

    /* Force placed resources, since committed resources can often just ignore metadata clears, etc.
     * This is the case on AMD it seems. */
    memset(&desc1, 0, sizeof(desc1));
    desc1.Width = 64;
    desc1.Height = 64;
    desc1.DepthOrArraySize = 1;
    desc1.MipLevels = 1;
    desc1.SampleDesc.Count = 1;
    desc1.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc1.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc1.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    desc1.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    alloc_info = ID3D12Device10_GetResourceAllocationInfo2(device10, 0, 1, &desc1, NULL);

    memset(&heap_desc, 0, sizeof(heap_desc));
    heap_desc.Properties.Type = D3D12_HEAP_TYPE_DEFAULT;
    heap_desc.SizeInBytes = alloc_info.SizeInBytes;
    heap_desc.Flags = D3D12_HEAP_FLAG_ALLOW_ONLY_RT_DS_TEXTURES;
    hr = ID3D12Device_CreateHeap(context.device, &heap_desc, &IID_ID3D12Heap, (void **)&heap);
    ok(SUCCEEDED(hr), "Failed to create heap, hr #%x.\n", hr);

    hr = ID3D12Device10_CreatePlacedResource2(device10, heap, 0, &desc1, D3D12_BARRIER_LAYOUT_RENDER_TARGET, NULL, 0, NULL, &IID_ID3D12Resource, (void **)&rtv);
    ok(SUCCEEDED(hr), "Failed to create resource, hr #%x.\n", hr);

    rtv_heap = create_cpu_descriptor_heap(context.device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1);
    ID3D12Device_CreateRenderTargetView(context.device, rtv, NULL,
        ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(rtv_heap));

    ID3D12GraphicsCommandList_ClearRenderTargetView(context.list, ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(rtv_heap), white, 0, NULL);
    set_rect(&rect, 0, 0, 32, 32);
    ID3D12GraphicsCommandList_ClearRenderTargetView(context.list, ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(rtv_heap), gray, 1, &rect);

    memset(&barrier, 0, sizeof(barrier));
    group.NumBarriers = 1;
    group.Type = D3D12_BARRIER_TYPE_TEXTURE;
    group.pTextureBarriers = &barrier;
    barrier.pResource = rtv;
    barrier.LayoutBefore = D3D12_BARRIER_LAYOUT_RENDER_TARGET;
    barrier.LayoutAfter = D3D12_BARRIER_LAYOUT_COMMON;
    barrier.SyncBefore = D3D12_BARRIER_SYNC_RENDER_TARGET;
    barrier.SyncAfter = D3D12_BARRIER_SYNC_COPY;
    barrier.AccessBefore = D3D12_BARRIER_ACCESS_RENDER_TARGET;
    barrier.AccessAfter = D3D12_BARRIER_ACCESS_COPY_SOURCE | D3D12_BARRIER_ACCESS_COPY_DEST;
    ID3D12GraphicsCommandList7_Barrier(list7, 1, &group);

    /* Copy to our own subresource to fill out each quadrant. */
    for (i = 1; i < 4; i++)
    {
        D3D12_TEXTURE_COPY_LOCATION dst_loc, src_loc;
        unsigned int input_i = i - 1;
        D3D12_BOX src_box;

        dst_loc.pResource = rtv;
        dst_loc.SubresourceIndex = 0;
        dst_loc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        src_loc.pResource = rtv;
        src_loc.SubresourceIndex = 0;
        src_loc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        set_box(&src_box, (input_i & 1) * 32, (input_i & 2) * 16, 0,
                (input_i & 1) * 32 + 32, (input_i & 2) * 16 + 32, 1);
        ID3D12GraphicsCommandList_CopyTextureRegion(context.list, &dst_loc, (i & 1) * 32, (i & 2) * 16, 0, &src_loc, &src_box);

        /* There is no implicit barrier between these overlapping copies. */
        barrier.LayoutBefore = D3D12_BARRIER_LAYOUT_COMMON;
        barrier.LayoutAfter = D3D12_BARRIER_LAYOUT_COMMON;
        barrier.SyncBefore = D3D12_BARRIER_SYNC_COPY;
        barrier.SyncAfter = D3D12_BARRIER_SYNC_COPY;
        barrier.AccessBefore = D3D12_BARRIER_ACCESS_COPY_DEST;
        barrier.AccessAfter = D3D12_BARRIER_ACCESS_COPY_SOURCE;
        ID3D12GraphicsCommandList7_Barrier(list7, 1, &group);
    }

    check_sub_resource_uint(rtv, 0, context.queue, context.list, 127u * 0x01010101u, 0);

    ID3D12DescriptorHeap_Release(rtv_heap);
    ID3D12GraphicsCommandList7_Release(list7);
    ID3D12Resource_Release(rtv);
    ID3D12Device10_Release(device10);
    ID3D12Heap_Release(heap);
    destroy_test_context(&context);
}
