/*
 * Copyright 2016-2017 Józef Kucia for CodeWeavers
 * Copyright 2020-2023 Philip Rebohle for Valve Corporation
 * Copyright 2020-2023 Joshua Ashton for Valve Corporation
 * Copyright 2020-2023 Hans-Kristian Arntzen for Valve Corporation
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

void test_sampler_feedback_resource_creation(void)
{
    D3D12_FEATURE_DATA_D3D12_OPTIONS7 features7;
    D3D12_HEAP_PROPERTIES heap_props;
    D3D12_HEAP_DESC heap_desc;
    D3D12_RESOURCE_DESC1 desc;
    ID3D12Resource *resource;
    ID3D12Device8 *device8;
    ID3D12Device *device;
    ID3D12Heap *heap;
    unsigned int i;
    UINT ref_count;
    HRESULT hr;

    static const struct test
    {
        unsigned int width, height, layers, levels;
        D3D12_MIP_REGION mip_region;
        D3D12_RESOURCE_FLAGS flags;
        HRESULT expected;
    } tests[] = {
        { 9, 11, 5, 3, { 4, 4, 1 }, D3D12_RESOURCE_FLAG_NONE, S_OK /* This is invalid, but runtime does not throw an error here. We'll just imply UAV flag for these formats. */ },
        { 9, 11, 5, 3, { 4, 4, 1 }, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, S_OK },
        { 13, 11, 5, 3, { 5, 5, 1 }, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, E_INVALIDARG /* POT error */ },
        { 9, 11, 5, 3, { 0, 0, 0 }, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, E_INVALIDARG /* Must be at least 4x4 */ },
        { 9, 11, 5, 3, { 4, 4, 2 }, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, E_INVALIDARG /* Depth field must be 0 or 1. */ },
        { 9, 11, 5, 3, { 4, 4, 0 }, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, S_OK /* This is fine. */ },
        { 9, 11, 5, 3, { 8, 8, 0 }, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, E_INVALIDARG /* Must not be more than half the size. */ },
    };
    
    device = create_device();
    if (!device)
        return;

    if (FAILED(ID3D12Device_CheckFeatureSupport(device, D3D12_FEATURE_D3D12_OPTIONS7, &features7, sizeof(features7))) ||
            features7.SamplerFeedbackTier < D3D12_SAMPLER_FEEDBACK_TIER_0_9)
    {
        skip("Sampler feedback not supported.\n");
        ID3D12Device_Release(device);
        return;
    }

    hr = ID3D12Device_QueryInterface(device, &IID_ID3D12Device8, (void **)&device8);
    ok(SUCCEEDED(hr), "Failed to query Device8, hr #%x.\n", hr);

    memset(&desc, 0, sizeof(desc));
    memset(&heap_props, 0, sizeof(heap_props));
    memset(&heap_desc, 0, sizeof(heap_desc));

    heap_props.Type = D3D12_HEAP_TYPE_DEFAULT;
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;

    heap_desc.SizeInBytes = 1024 * 1024;
    heap_desc.Properties = heap_props;
    heap_desc.Flags = D3D12_HEAP_FLAG_CREATE_NOT_ZEROED;
    hr = ID3D12Device_CreateHeap(device, &heap_desc, &IID_ID3D12Heap, (void **)&heap);
    ok(SUCCEEDED(hr), "Failed to create heap, hr #%x.\n");

    for (i = 0; i < ARRAY_SIZE(tests); i++)
    {
        vkd3d_test_set_context("Test %u", i);
        desc.Width = tests[i].width;
        desc.Height = tests[i].height;
        desc.DepthOrArraySize = tests[i].layers;
        desc.MipLevels = tests[i].levels;
        desc.SamplerFeedbackMipRegion = tests[i].mip_region;
        desc.Flags = tests[i].flags;

        desc.Format = DXGI_FORMAT_SAMPLER_FEEDBACK_MIN_MIP_OPAQUE;

        hr = ID3D12Device8_CreateCommittedResource2(device8, &heap_props, D3D12_HEAP_FLAG_CREATE_NOT_ZEROED,
            &desc, D3D12_RESOURCE_STATE_COMMON, NULL, NULL, &IID_ID3D12Resource, (void **)&resource);
        ok(hr == tests[i].expected, "Unexpected hr, expected #%x, got #%x.\n", tests[i].expected, hr);
        if (SUCCEEDED(hr))
            ID3D12Resource_Release(resource);

        hr = ID3D12Device8_CreatePlacedResource1(device8, heap, 0, &desc, D3D12_RESOURCE_STATE_COMMON, NULL, &IID_ID3D12Resource, (void **)&resource);
        ok(hr == tests[i].expected, "Unexpected hr, expected #%x, got #%x.\n", tests[i].expected, hr);
        if (SUCCEEDED(hr))
            ID3D12Resource_Release(resource);

        desc.Format = DXGI_FORMAT_SAMPLER_FEEDBACK_MIP_REGION_USED_OPAQUE;

        hr = ID3D12Device8_CreateCommittedResource2(device8, &heap_props, D3D12_HEAP_FLAG_CREATE_NOT_ZEROED,
            &desc, D3D12_RESOURCE_STATE_COMMON, NULL, NULL, &IID_ID3D12Resource, (void **)&resource);
        ok(hr == tests[i].expected, "Unexpected hr, expected #%x, got #%x.\n", tests[i].expected, hr);
        if (SUCCEEDED(hr))
            ID3D12Resource_Release(resource);

        hr = ID3D12Device8_CreatePlacedResource1(device8, heap, 0, &desc, D3D12_RESOURCE_STATE_COMMON, NULL, &IID_ID3D12Resource, (void **)&resource);
        ok(hr == tests[i].expected, "Unexpected hr, expected #%x, got #%x.\n", tests[i].expected, hr);
        if (SUCCEEDED(hr))
            ID3D12Resource_Release(resource);

        /* Feedback resources cannot be sparse themselves, there is no desc1 variant. */
    }
    vkd3d_test_set_context(NULL);

    ID3D12Device_Release(device);
    ID3D12Heap_Release(heap);
    ref_count = ID3D12Device8_Release(device8);
    ok(ref_count == 0, "Unexpected ref-count %u.\n", ref_count);
}

