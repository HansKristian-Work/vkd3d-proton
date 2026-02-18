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

void test_create_device(void)
{
    ID3D12Device *device;
    ULONG refcount;
    HRESULT hr;

    if (!(device = create_device()))
    {
        skip("Failed to create device.\n");
        return;
    }

    check_interface(device, &IID_ID3D12Object, true);
    check_interface(device, &IID_ID3D12DeviceChild, false);
    check_interface(device, &IID_ID3D12Pageable, false);
    check_interface(device, &IID_ID3D12Device, true);

    refcount = ID3D12Device_Release(device);
    ok(!refcount, "ID3D12Device has %u references left.\n", (unsigned int)refcount);

    hr = D3D12CreateDevice(NULL, D3D_FEATURE_LEVEL_11_0, &IID_ID3D12Device, (void **)&device);
    ok(hr == S_OK, "Failed to create device, hr %#x.\n", hr);
    ID3D12Device_Release(device);

    hr = D3D12CreateDevice(NULL, D3D_FEATURE_LEVEL_11_0, &IID_ID3D12Device, NULL);
    ok(hr == S_FALSE, "Got unexpected hr %#x.\n", hr);
    hr = D3D12CreateDevice(NULL, D3D_FEATURE_LEVEL_11_0, NULL, NULL);
    ok(hr == S_FALSE, "Got unexpected hr %#x.\n", hr);
    hr = D3D12CreateDevice(NULL, D3D_FEATURE_LEVEL_11_0, &IID_ID3D12DeviceChild, NULL);
    ok(hr == S_FALSE, "Got unexpected hr %#x.\n", hr);

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
    ok(hr == E_INVALIDARG, "Got unexpected hr %#x.\n", hr);
    hr = D3D12CreateDevice(NULL, ~0u, &IID_ID3D12Device, (void **)&device);
    ok(hr == E_INVALIDARG, "Got unexpected hr %#x.\n", hr);
}

void test_node_count(void)
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

void test_check_feature_support(void)
{
    D3D12_FEATURE_DATA_GPU_VIRTUAL_ADDRESS_SUPPORT gpu_virtual_address;
    D3D12_FEATURE_DATA_FEATURE_LEVELS feature_levels;
    D3D12_FEATURE_DATA_ROOT_SIGNATURE root_signature;
    D3D_FEATURE_LEVEL max_supported_feature_level;
    D3D12_FEATURE_DATA_ARCHITECTURE architecture;
    D3D12_FEATURE_DATA_FORMAT_INFO format_info;
    unsigned int expected_plane_count;
    ID3D12Device *device;
    DXGI_FORMAT format;
    ULONG refcount;
    bool is_todo;
    HRESULT hr;

    static const D3D_FEATURE_LEVEL all_feature_levels[] =
    {
        D3D_FEATURE_LEVEL_12_2,
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
        D3D_FEATURE_LEVEL_12_2,
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
    ok(hr == S_OK, "Failed to check feature support, hr %#x.\n", hr);
    trace("Max supported feature level %#x.\n", feature_levels.MaxSupportedFeatureLevel);
    max_supported_feature_level = feature_levels.MaxSupportedFeatureLevel;

    feature_levels.NumFeatureLevels = ARRAY_SIZE(d3d12_feature_levels);
    feature_levels.pFeatureLevelsRequested = d3d12_feature_levels;
    feature_levels.MaxSupportedFeatureLevel = 0;
    hr = ID3D12Device_CheckFeatureSupport(device, D3D12_FEATURE_FEATURE_LEVELS,
            &feature_levels, sizeof(feature_levels));
    ok(hr == S_OK, "Failed to check feature support, hr %#x.\n", hr);
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
    ok(hr == S_OK, "Failed to check feature support, hr %#x.\n", hr);
    ok(feature_levels.MaxSupportedFeatureLevel == D3D_FEATURE_LEVEL_9_3,
            "Got unexpected max feature level %#x.\n", feature_levels.MaxSupportedFeatureLevel);

    feature_levels.NumFeatureLevels = ARRAY_SIZE(invalid_feature_levels);
    feature_levels.pFeatureLevelsRequested = invalid_feature_levels;
    feature_levels.MaxSupportedFeatureLevel = 0;
    hr = ID3D12Device_CheckFeatureSupport(device, D3D12_FEATURE_FEATURE_LEVELS,
            &feature_levels, sizeof(feature_levels));
    ok(hr == S_OK, "Failed to check feature support, hr %#x.\n", hr);
    ok(feature_levels.MaxSupportedFeatureLevel == 0x3000,
            "Got unexpected max feature level %#x.\n", feature_levels.MaxSupportedFeatureLevel);

    /* Format info. */
    memset(&format_info, 0, sizeof(format_info));
    hr = ID3D12Device_CheckFeatureSupport(device, D3D12_FEATURE_FORMAT_INFO,
            &format_info, sizeof(format_info));
    ok(hr == S_OK, "Failed to get format info, hr %#x.\n", hr);
    ok(format_info.Format == DXGI_FORMAT_UNKNOWN, "Got unexpected format %#x.\n", format_info.Format);
    ok(format_info.PlaneCount == 1, "Got unexpected plane count %u.\n", format_info.PlaneCount);

    for (format = DXGI_FORMAT_UNKNOWN; format <= DXGI_FORMAT_B4G4R4A4_UNORM; ++format)
    {
        vkd3d_test_set_context("format %#x", format);

        switch (format)
        {
            case DXGI_FORMAT_R32G8X24_TYPELESS:
            case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
            case DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS:
            case DXGI_FORMAT_X32_TYPELESS_G8X24_UINT:
            case DXGI_FORMAT_D24_UNORM_S8_UINT:
            case DXGI_FORMAT_R24G8_TYPELESS:
            case DXGI_FORMAT_R24_UNORM_X8_TYPELESS:
            case DXGI_FORMAT_X24_TYPELESS_G8_UINT:
            case DXGI_FORMAT_NV12:
            case DXGI_FORMAT_P010:
            case DXGI_FORMAT_P016:
            case DXGI_FORMAT_NV11:
                expected_plane_count = 2;
                break;
            default:
                expected_plane_count = 1;
                break;
        }

        is_todo = format == DXGI_FORMAT_R8G8_B8G8_UNORM
                || format == DXGI_FORMAT_G8R8_G8B8_UNORM
                || format == DXGI_FORMAT_B5G6R5_UNORM
                || format == DXGI_FORMAT_B5G5R5A1_UNORM
                || format == DXGI_FORMAT_R10G10B10_XR_BIAS_A2_UNORM
                || (DXGI_FORMAT_AYUV <= format && format <= DXGI_FORMAT_B4G4R4A4_UNORM);

        memset(&format_info, 0, sizeof(format_info));
        format_info.Format = format;
        hr = ID3D12Device_CheckFeatureSupport(device, D3D12_FEATURE_FORMAT_INFO,
                &format_info, sizeof(format_info));

        if (format == DXGI_FORMAT_R1_UNORM)
        {
            ok(hr == E_INVALIDARG, "Got unexpected hr %#x.\n", hr);
            continue;
        }

        todo_if(is_todo)
        ok(hr == S_OK, "Failed to get format info, hr %#x.\n", hr);
        ok(format_info.Format == format, "Got unexpected format %#x.\n", format_info.Format);
        todo_if(is_todo)
        ok(format_info.PlaneCount == expected_plane_count,
                "Got plane count %u, expected %u.\n", format_info.PlaneCount, expected_plane_count);
    }
    vkd3d_test_set_context(NULL);

    /* GPU virtual address */
    memset(&gpu_virtual_address, 0, sizeof(gpu_virtual_address));
    hr = ID3D12Device_CheckFeatureSupport(device, D3D12_FEATURE_GPU_VIRTUAL_ADDRESS_SUPPORT,
            &gpu_virtual_address, sizeof(gpu_virtual_address));
    ok(hr == S_OK, "Failed to check GPU virtual address support, hr %#x.\n", hr);
    trace("GPU virtual address bits per resource: %u.\n",
            gpu_virtual_address.MaxGPUVirtualAddressBitsPerResource);
    trace("GPU virtual address bits per process: %u.\n",
            gpu_virtual_address.MaxGPUVirtualAddressBitsPerProcess);

    root_signature.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_0;
    hr = ID3D12Device_CheckFeatureSupport(device, D3D12_FEATURE_ROOT_SIGNATURE,
            &root_signature, sizeof(root_signature));
    ok(hr == S_OK, "Failed to get root signature feature support, hr %#x.\n", hr);
    ok(root_signature.HighestVersion == D3D_ROOT_SIGNATURE_VERSION_1_0,
            "Got unexpected root signature feature version %#x.\n", root_signature.HighestVersion);

    root_signature.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;
    hr = ID3D12Device_CheckFeatureSupport(device, D3D12_FEATURE_ROOT_SIGNATURE,
            &root_signature, sizeof(root_signature));
    ok(hr == S_OK, "Failed to get root signature feature support, hr %#x.\n", hr);
    ok(root_signature.HighestVersion == D3D_ROOT_SIGNATURE_VERSION_1_0
            || root_signature.HighestVersion == D3D_ROOT_SIGNATURE_VERSION_1_1,
            "Got unexpected root signature feature version %#x.\n", root_signature.HighestVersion);

    root_signature.HighestVersion = 0;
    hr = ID3D12Device_CheckFeatureSupport(device, D3D12_FEATURE_ROOT_SIGNATURE,
            &root_signature, sizeof(root_signature));
    ok(hr == E_INVALIDARG, "Got unexpected hr %#x.\n", hr);
    ok(root_signature.HighestVersion == 0, "Got unexpected root signature feature version %#x.\n",
            root_signature.HighestVersion);

    root_signature.HighestVersion = 0xdeadbeef;
    hr = ID3D12Device_CheckFeatureSupport(device, D3D12_FEATURE_ROOT_SIGNATURE,
            &root_signature, sizeof(root_signature));
    ok(hr == E_INVALIDARG, "Got unexpected hr %#x.\n", hr);
    ok(root_signature.HighestVersion == 0xdeadbeef, "Got unexpected root signature feature version %#x.\n",
            root_signature.HighestVersion);

    refcount = ID3D12Device_Release(device);
    ok(!refcount, "ID3D12Device has %u references left.\n", (unsigned int)refcount);
}

static const DXGI_FORMAT depth_stencil_formats[] =
{
    DXGI_FORMAT_R32G8X24_TYPELESS,
    DXGI_FORMAT_D32_FLOAT_S8X24_UINT,
    DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS,
    DXGI_FORMAT_X32_TYPELESS_G8X24_UINT,
    DXGI_FORMAT_R32_TYPELESS,
    DXGI_FORMAT_D32_FLOAT,
    DXGI_FORMAT_R24G8_TYPELESS,
    DXGI_FORMAT_D24_UNORM_S8_UINT,
    DXGI_FORMAT_R24_UNORM_X8_TYPELESS,
    DXGI_FORMAT_X24_TYPELESS_G8_UINT,
    DXGI_FORMAT_R16_TYPELESS,
    DXGI_FORMAT_D16_UNORM,
};

void test_format_support(void)
{
    D3D12_FEATURE_DATA_FORMAT_SUPPORT format_support;
    D3D12_FEATURE_DATA_FEATURE_LEVELS feature_levels;
    bool required_but_fails;
    bool unspecified_format;
    bool optional_format;
    ID3D12Device *device;
    DXGI_FORMAT format;
    ULONG refcount;
    unsigned int i;
    HRESULT hr;

    static const DXGI_FORMAT known_required_but_fails[] =
    {
        DXGI_FORMAT_R8G8_B8G8_UNORM,
        DXGI_FORMAT_G8R8_G8B8_UNORM,
        DXGI_FORMAT_R10G10B10_XR_BIAS_A2_UNORM,
        DXGI_FORMAT_420_OPAQUE,
        DXGI_FORMAT_YUY2,
    };

#define MAX_FORMAT_VALUE DXGI_FORMAT_SAMPLER_FEEDBACK_MIP_REGION_USED_OPAQUE

    const struct dxgi_format_list
    {
        D3D_FEATURE_LEVEL feature_level;
        DXGI_FORMAT optional_formats[MAX_FORMAT_VALUE];
        DXGI_FORMAT unspecified_formats[MAX_FORMAT_VALUE];
    } *dxgi_format, dxgi_format_list[] = {
        /*
            https://devblogs.microsoft.com/directx/new-in-directx-feature-level-12_2/
            https://microsoft.github.io/DirectX-Specs/d3d/SamplerFeedback.html
        */
        {D3D_FEATURE_LEVEL_12_2, 
            {DXGI_FORMAT_AYUV, DXGI_FORMAT_Y410, DXGI_FORMAT_Y416, DXGI_FORMAT_P010, DXGI_FORMAT_P016, DXGI_FORMAT_Y210, DXGI_FORMAT_Y216, DXGI_FORMAT_NV11,
                DXGI_FORMAT_AI44, DXGI_FORMAT_IA44, DXGI_FORMAT_P8, DXGI_FORMAT_A8P8},
            {DXGI_FORMAT_R1_UNORM, DXGI_FORMAT_B4G4R4A4_UNORM, DXGI_FORMAT_P208, DXGI_FORMAT_V208, DXGI_FORMAT_V408}},
        /* https://github.com/MicrosoftDocs/win32/blob/docs/desktop-src/direct3ddxgi/hardware-support-for-direct3d-12-1-formats.md */
        {D3D_FEATURE_LEVEL_12_1, 
            {DXGI_FORMAT_AYUV, DXGI_FORMAT_Y410, DXGI_FORMAT_Y416, DXGI_FORMAT_P010, DXGI_FORMAT_P016, DXGI_FORMAT_Y210, DXGI_FORMAT_Y216, DXGI_FORMAT_NV11,
                DXGI_FORMAT_AI44, DXGI_FORMAT_IA44, DXGI_FORMAT_P8, DXGI_FORMAT_A8P8},
            {DXGI_FORMAT_R1_UNORM, DXGI_FORMAT_B4G4R4A4_UNORM, DXGI_FORMAT_P208, DXGI_FORMAT_V208, DXGI_FORMAT_V408, 
                DXGI_FORMAT_SAMPLER_FEEDBACK_MIN_MIP_OPAQUE, DXGI_FORMAT_SAMPLER_FEEDBACK_MIP_REGION_USED_OPAQUE}},
        /* https://github.com/MicrosoftDocs/win32/blob/docs/desktop-src/direct3ddxgi/hardware-support-for-direct3d-12-0-formats.md */
        {D3D_FEATURE_LEVEL_12_0, 
            {DXGI_FORMAT_AYUV, DXGI_FORMAT_Y410, DXGI_FORMAT_Y416, DXGI_FORMAT_P010, DXGI_FORMAT_P016, DXGI_FORMAT_Y210, DXGI_FORMAT_Y216, DXGI_FORMAT_NV11,
                DXGI_FORMAT_AI44, DXGI_FORMAT_IA44, DXGI_FORMAT_P8, DXGI_FORMAT_A8P8},
            {DXGI_FORMAT_R1_UNORM, DXGI_FORMAT_P208, DXGI_FORMAT_V208, DXGI_FORMAT_V408, 
                DXGI_FORMAT_SAMPLER_FEEDBACK_MIN_MIP_OPAQUE, DXGI_FORMAT_SAMPLER_FEEDBACK_MIP_REGION_USED_OPAQUE}},
        /* https://github.com/MicrosoftDocs/win32/blob/docs/desktop-src/direct3ddxgi/format-support-for-direct3d-11-1-feature-level-hardware.md */
        {D3D_FEATURE_LEVEL_11_1, 
            {DXGI_FORMAT_AYUV, DXGI_FORMAT_Y410, DXGI_FORMAT_Y416, DXGI_FORMAT_P010, DXGI_FORMAT_P016, DXGI_FORMAT_Y210, DXGI_FORMAT_Y216, DXGI_FORMAT_NV11,
                DXGI_FORMAT_AI44, DXGI_FORMAT_IA44, DXGI_FORMAT_P8, DXGI_FORMAT_A8P8}, 
            {DXGI_FORMAT_R1_UNORM, DXGI_FORMAT_P208, DXGI_FORMAT_V208, DXGI_FORMAT_V408, 
                DXGI_FORMAT_SAMPLER_FEEDBACK_MIN_MIP_OPAQUE, DXGI_FORMAT_SAMPLER_FEEDBACK_MIP_REGION_USED_OPAQUE}},
        /* https://github.com/MicrosoftDocs/win32/blob/docs/desktop-src/direct3ddxgi/format-support-for-direct3d-11-0-feature-level-hardware.md */
        {D3D_FEATURE_LEVEL_11_0, 
            {DXGI_FORMAT_AYUV, DXGI_FORMAT_Y410, DXGI_FORMAT_Y416, DXGI_FORMAT_P010, DXGI_FORMAT_P016, DXGI_FORMAT_Y210, DXGI_FORMAT_Y216, DXGI_FORMAT_NV11,
                DXGI_FORMAT_AI44, DXGI_FORMAT_IA44, DXGI_FORMAT_P8, DXGI_FORMAT_A8P8}, 
            {DXGI_FORMAT_R1_UNORM, DXGI_FORMAT_P208, DXGI_FORMAT_V208, DXGI_FORMAT_V408, 
                DXGI_FORMAT_SAMPLER_FEEDBACK_MIN_MIP_OPAQUE, DXGI_FORMAT_SAMPLER_FEEDBACK_MIP_REGION_USED_OPAQUE}},
        /* https://github.com/MicrosoftDocs/win32/blob/docs/desktop-src/direct3ddxgi/format-support-for-direct3d-feature-level-10-1-hardware.md */
        {D3D_FEATURE_LEVEL_10_1, 
            {DXGI_FORMAT_AYUV, DXGI_FORMAT_Y410, DXGI_FORMAT_Y416, DXGI_FORMAT_P010, DXGI_FORMAT_P016, DXGI_FORMAT_Y210, 
                DXGI_FORMAT_Y216, DXGI_FORMAT_NV11, DXGI_FORMAT_AI44, DXGI_FORMAT_IA44, DXGI_FORMAT_P8, DXGI_FORMAT_A8P8}, 
            {DXGI_FORMAT_R1_UNORM, DXGI_FORMAT_P208, DXGI_FORMAT_V208, DXGI_FORMAT_V408, 
                DXGI_FORMAT_SAMPLER_FEEDBACK_MIN_MIP_OPAQUE, DXGI_FORMAT_SAMPLER_FEEDBACK_MIP_REGION_USED_OPAQUE}},
        /* https://github.com/MicrosoftDocs/win32/blob/docs/desktop-src/direct3ddxgi/format-support-for-direct3d-feature-level-10-1-hardware.md */
        {D3D_FEATURE_LEVEL_10_0, 
            {DXGI_FORMAT_R10G10B10_XR_BIAS_A2_UNORM, DXGI_FORMAT_AYUV, DXGI_FORMAT_Y410, DXGI_FORMAT_Y416, DXGI_FORMAT_P010, DXGI_FORMAT_P016, 
                DXGI_FORMAT_Y210, DXGI_FORMAT_Y216, DXGI_FORMAT_NV11, DXGI_FORMAT_AI44, DXGI_FORMAT_IA44, DXGI_FORMAT_P8, DXGI_FORMAT_A8P8}, 
            {DXGI_FORMAT_R1_UNORM, DXGI_FORMAT_P208, DXGI_FORMAT_V208, DXGI_FORMAT_V408, 
                DXGI_FORMAT_SAMPLER_FEEDBACK_MIN_MIP_OPAQUE, DXGI_FORMAT_SAMPLER_FEEDBACK_MIP_REGION_USED_OPAQUE}},
    };

#undef MAX_FORMAT_VALUE

    static const D3D_FEATURE_LEVEL all_feature_levels[] =
    {
        D3D_FEATURE_LEVEL_12_2,
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

    if (!(device = create_device()))
    {
        skip("Failed to create device.\n");
        return;
    }

    memset(&format_support, 0, sizeof(format_support));
    hr = ID3D12Device_CheckFeatureSupport(device, D3D12_FEATURE_FORMAT_SUPPORT,
            &format_support, sizeof(format_support));
    ok(hr == S_OK, "Got unexpected hr %#x.\n", hr);
    ok(format_support.Support1 == D3D12_FORMAT_SUPPORT1_BUFFER,
            "Got unexpected support1 %#x.\n", format_support.Support1);
    ok(!format_support.Support2 || format_support.Support2 == D3D12_FORMAT_SUPPORT2_TILED,
            "Got unexpected support2 %#x.\n", format_support.Support2);

    memset(&feature_levels, 0, sizeof(feature_levels));
    feature_levels.NumFeatureLevels = ARRAY_SIZE(all_feature_levels);
    feature_levels.pFeatureLevelsRequested = all_feature_levels;
    hr = ID3D12Device_CheckFeatureSupport(device, D3D12_FEATURE_FEATURE_LEVELS, 
        &feature_levels, sizeof(feature_levels));
    ok(hr == S_OK, "Got unexpected hr %#x.\n", hr);

    dxgi_format = NULL;
    for (i = 0; i < ARRAY_SIZE(dxgi_format_list); ++i)
    {
        if (dxgi_format_list[i].feature_level == feature_levels.MaxSupportedFeatureLevel)
        {
            dxgi_format = &dxgi_format_list[i];
            break;
        }
    }
    assert(dxgi_format);

    for (format = 0; format <= DXGI_FORMAT_SAMPLER_FEEDBACK_MIP_REGION_USED_OPAQUE; ++format)
    {
        /* Undefined range, skip */
        if (format > DXGI_FORMAT_B4G4R4A4_UNORM && format < DXGI_FORMAT_P208)
            continue;

        vkd3d_test_set_context("format %#x", format);

        required_but_fails = false;
        unspecified_format = false;
        optional_format = false;

        for (i = 0; i < ARRAY_SIZE(known_required_but_fails); ++i)
        {
            if (known_required_but_fails[i] == format)
            {
                required_but_fails = true;
                break;
            }
        }

        if (!required_but_fails)
        {
            /* ASTC format are unspecified */
            if (format >= /* DXGI_FORMAT_UNDOCUMENTED_ASTC_FIRST */ (DXGI_FORMAT)0x85 && 
                format <= /* DXGI_FORMAT_UNDOCUMENTED_ASTC_LAST */  (DXGI_FORMAT)0xbc)
            {
                unspecified_format = true;
            }
            else
            {
                /* Check if the format is unspecified or optional */
                for (i = 0; i < ARRAY_SIZE(dxgi_format->unspecified_formats); ++i)
                {
                    /* Fixed size list with only part of the list filled */
                    if (dxgi_format->unspecified_formats[i] == DXGI_FORMAT_UNKNOWN)
                        break;
                    if (dxgi_format->unspecified_formats[i] == format)
                    {
                        unspecified_format = true;
                        break;
                    }
                }

                if (!unspecified_format)
                {
                    for (i = 0; i < ARRAY_SIZE(dxgi_format->optional_formats); ++i)
                    {
                        /* Fixed size list with only part of the list filled */
                        if (dxgi_format->optional_formats[i] == DXGI_FORMAT_UNKNOWN)
                            break;
                        if (dxgi_format->optional_formats[i] == format)
                        {
                            optional_format = true;
                            break;
                        }
                    }
                }
            }
        }

        memset(&format_support, 0, sizeof(format_support));
        format_support.Format = format;
        hr = ID3D12Device_CheckFeatureSupport(device, D3D12_FEATURE_FORMAT_SUPPORT,
                &format_support, sizeof(format_support));

        if (unspecified_format)
            ok(hr == S_OK || hr == E_FAIL, "Unspecified format %d got unexpected hr %#x.\n", format, hr);
        else if (optional_format)
            ok(hr == S_OK || hr == E_FAIL, "Optional format %d got unexpected hr %#x.\n", format, hr);
        else 
            todo_if(required_but_fails) ok(hr == S_OK, "Format %d got unexpected hr %#x.\n", format, hr);
    }
    vkd3d_test_set_context(NULL);

    refcount = ID3D12Device_Release(device);
    ok(!refcount, "ID3D12Device has %u references left.\n", (unsigned int)refcount);
}

void test_multisample_quality_levels(void)
{
    static const unsigned int sample_counts[] = {1, 2, 4, 8, 16, 32};
    D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS format_support;
    ID3D12Device *device;
    DXGI_FORMAT format;
    unsigned int i, j;
    ULONG refcount;
    HRESULT hr;

    if (!(device = create_device()))
    {
        skip("Failed to create device.\n");
        return;
    }

    memset(&format_support, 0, sizeof(format_support));
    format_support.NumQualityLevels = 0xdeadbeef;
    hr = ID3D12Device_CheckFeatureSupport(device, D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS,
            &format_support, sizeof(format_support));
    ok(hr == E_FAIL, "Got unexpected hr %#x.\n", hr);
    ok(!format_support.Flags, "Got unexpected flags %#x.\n", format_support.Flags);
    ok(!format_support.NumQualityLevels, "Got unexpected quality levels %u.\n", format_support.NumQualityLevels);

    format_support.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    format_support.NumQualityLevels = 0xdeadbeef;
    hr = ID3D12Device_CheckFeatureSupport(device, D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS,
            &format_support, sizeof(format_support));
    ok(hr == E_FAIL, "Got unexpected hr %#x.\n", hr);
    ok(!format_support.Flags, "Got unexpected flags %#x.\n", format_support.Flags);
    ok(!format_support.NumQualityLevels, "Got unexpected quality levels %u.\n", format_support.NumQualityLevels);

    /* 1 sample */
    for (format = DXGI_FORMAT_UNKNOWN; format <= DXGI_FORMAT_B4G4R4A4_UNORM; ++format)
    {
        if (format == DXGI_FORMAT_R1_UNORM)
            continue;

        vkd3d_test_set_context("format %#x", format);

        memset(&format_support, 0, sizeof(format_support));
        format_support.Format = format;
        format_support.SampleCount = 1;
        format_support.NumQualityLevels = 0xdeadbeef;
        hr = ID3D12Device_CheckFeatureSupport(device, D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS,
                &format_support, sizeof(format_support));
        ok(hr == S_OK, "Got unexpected hr %#x.\n", hr);
        ok(format_support.NumQualityLevels == 1, "Got unexpected quality levels %u.\n", format_support.NumQualityLevels);
    }
    vkd3d_test_set_context(NULL);

    /* DXGI_FORMAT_UNKNOWN */
    for (i = 1; i < ARRAY_SIZE(sample_counts); ++i)
    {
        vkd3d_test_set_context("samples %#x", sample_counts[i]);

        memset(&format_support, 0, sizeof(format_support));
        format_support.SampleCount = sample_counts[i];
        format_support.NumQualityLevels = 0xdeadbeef;
        hr = ID3D12Device_CheckFeatureSupport(device, D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS,
                &format_support, sizeof(format_support));
        ok(hr == S_OK, "Got unexpected hr %#x.\n", hr);
        ok(!format_support.Flags, "Got unexpected flags %#x.\n", format_support.Flags);
        ok(!format_support.NumQualityLevels, "Got unexpected quality levels %u.\n", format_support.NumQualityLevels);

        format_support.Flags = D3D12_MULTISAMPLE_QUALITY_LEVELS_FLAG_TILED_RESOURCE;
        format_support.NumQualityLevels = 0xdeadbeef;
        hr = ID3D12Device_CheckFeatureSupport(device, D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS,
                &format_support, sizeof(format_support));
        ok(hr == S_OK, "Got unexpected hr %#x.\n", hr);
        ok(format_support.Flags == D3D12_MULTISAMPLE_QUALITY_LEVELS_FLAG_TILED_RESOURCE,
                "Got unexpected flags %#x.\n", format_support.Flags);
        ok(!format_support.NumQualityLevels, "Got unexpected quality levels %u.\n", format_support.NumQualityLevels);
    }
    vkd3d_test_set_context(NULL);

    /* invalid sample counts */
    for (i = 1; i <= 32; ++i)
    {
        bool valid_sample_count = false;
        for (j = 0; j < ARRAY_SIZE(sample_counts); ++j)
        {
            if (sample_counts[j] == i)
            {
                valid_sample_count = true;
                break;
            }
        }
        if (valid_sample_count)
            continue;

        vkd3d_test_set_context("samples %#x", i);

        memset(&format_support, 0, sizeof(format_support));
        format_support.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        format_support.SampleCount = i;
        format_support.NumQualityLevels = 0xdeadbeef;
        hr = ID3D12Device_CheckFeatureSupport(device, D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS,
                &format_support, sizeof(format_support));
        ok(hr == S_OK, "Got unexpected hr %#x.\n", hr);
        ok(!format_support.Flags, "Got unexpected flags %#x.\n", format_support.Flags);
        ok(!format_support.NumQualityLevels, "Got unexpected quality levels %u.\n", format_support.NumQualityLevels);
    }
    vkd3d_test_set_context(NULL);

    /* DXGI_FORMAT_R8G8B8A8_UNORM */
    memset(&format_support, 0, sizeof(format_support));
    format_support.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    format_support.SampleCount = 4;
    format_support.NumQualityLevels = 0xdeadbeef;
    hr = ID3D12Device_CheckFeatureSupport(device, D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS,
            &format_support, sizeof(format_support));
    ok(hr == S_OK, "Got unexpected hr %#x.\n", hr);
    ok(!format_support.Flags, "Got unexpected flags %#x.\n", format_support.Flags);
    ok(format_support.NumQualityLevels >= 1, "Got unexpected quality levels %u.\n", format_support.NumQualityLevels);

    /* DXGI_FORMAT_D32_FLOAT_S8X24_UINT */
    format_support.Format = DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
    format_support.SampleCount = 4;
    format_support.NumQualityLevels = 0xdeadbeef;
    hr = ID3D12Device_CheckFeatureSupport(device, D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS,
            &format_support, sizeof(format_support));
    ok(hr == S_OK, "Got unexpected hr %#x.\n", hr);
    ok(format_support.NumQualityLevels >= 1, "Got unexpected quality levels %u.\n", format_support.NumQualityLevels);

    for (i = 0; i < ARRAY_SIZE(depth_stencil_formats); ++i)
    {
        memset(&format_support, 0, sizeof(format_support));
        format_support.Format = depth_stencil_formats[i];
        format_support.SampleCount = 4;
        format_support.NumQualityLevels = 0xdeadbeef;
        hr = ID3D12Device_CheckFeatureSupport(device, D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS,
                &format_support, sizeof(format_support));
        ok(hr == S_OK, "Got unexpected hr %#x.\n", hr);
    }

    refcount = ID3D12Device_Release(device);
    ok(!refcount, "ID3D12Device has %u references left.\n", (unsigned int)refcount);
}

void test_create_command_allocator(void)
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
    ok(SUCCEEDED(hr), "Failed to create command allocator, hr %#x.\n", hr);

    refcount = get_refcount(device);
    ok(refcount == 2, "Got unexpected refcount %u.\n", (unsigned int)refcount);
    hr = ID3D12CommandAllocator_GetDevice(command_allocator, &IID_ID3D12Device, (void **)&tmp_device);
    ok(SUCCEEDED(hr), "Failed to get device, hr %#x.\n", hr);
    refcount = get_refcount(device);
    ok(refcount == 3, "Got unexpected refcount %u.\n", (unsigned int)refcount);
    refcount = ID3D12Device_Release(tmp_device);
    ok(refcount == 2, "Got unexpected refcount %u.\n", (unsigned int)refcount);

    check_interface(command_allocator, &IID_ID3D12Object, true);
    check_interface(command_allocator, &IID_ID3D12DeviceChild, true);
    check_interface(command_allocator, &IID_ID3D12Pageable, true);
    check_interface(command_allocator, &IID_ID3D12CommandAllocator, true);

    refcount = ID3D12CommandAllocator_Release(command_allocator);
    ok(!refcount, "ID3D12CommandAllocator has %u references left.\n", (unsigned int)refcount);

    hr = ID3D12Device_CreateCommandAllocator(device, D3D12_COMMAND_LIST_TYPE_BUNDLE,
            &IID_ID3D12CommandAllocator, (void **)&command_allocator);
    ok(SUCCEEDED(hr), "Failed to create command allocator, hr %#x.\n", hr);
    refcount = ID3D12CommandAllocator_Release(command_allocator);
    ok(!refcount, "ID3D12CommandAllocator has %u references left.\n", (unsigned int)refcount);

    hr = ID3D12Device_CreateCommandAllocator(device, D3D12_COMMAND_LIST_TYPE_COMPUTE,
            &IID_ID3D12CommandAllocator, (void **)&command_allocator);
    ok(SUCCEEDED(hr), "Failed to create command allocator, hr %#x.\n", hr);
    refcount = ID3D12CommandAllocator_Release(command_allocator);
    ok(!refcount, "ID3D12CommandAllocator has %u references left.\n", (unsigned int)refcount);

    hr = ID3D12Device_CreateCommandAllocator(device, D3D12_COMMAND_LIST_TYPE_COPY,
            &IID_ID3D12CommandAllocator, (void **)&command_allocator);
    ok(SUCCEEDED(hr), "Failed to create command allocator, hr %#x.\n", hr);
    refcount = ID3D12CommandAllocator_Release(command_allocator);
    ok(!refcount, "ID3D12CommandAllocator has %u references left.\n", (unsigned int)refcount);

    hr = ID3D12Device_CreateCommandAllocator(device, ~0u,
            &IID_ID3D12CommandAllocator, (void **)&command_allocator);
    ok(hr == E_INVALIDARG, "Got unexpected hr %#x.\n", hr);

    refcount = ID3D12Device_Release(device);
    ok(!refcount, "ID3D12Device has %u references left.\n", (unsigned int)refcount);
}

void test_create_command_list(void)
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
    ok(SUCCEEDED(hr), "Failed to create command allocator, hr %#x.\n", hr);

    refcount = get_refcount(device);
    ok(refcount == 2, "Got unexpected refcount %u.\n", (unsigned int)refcount);

    hr = ID3D12Device_CreateCommandList(device, 0, D3D12_COMMAND_LIST_TYPE_DIRECT,
            command_allocator, NULL, &IID_ID3D12CommandList, (void **)&command_list);
    ok(SUCCEEDED(hr), "Failed to create command list, hr %#x.\n", hr);

    refcount = get_refcount(command_allocator);
    ok(refcount == 1, "Got unexpected refcount %u.\n", (unsigned int)refcount);

    refcount = get_refcount(device);
    ok(refcount == 3, "Got unexpected refcount %u.\n", (unsigned int)refcount);
    hr = ID3D12CommandList_GetDevice(command_list, &IID_ID3D12Device, (void **)&tmp_device);
    ok(SUCCEEDED(hr), "Failed to get device, hr %#x.\n", hr);
    refcount = get_refcount(device);
    ok(refcount == 4, "Got unexpected refcount %u.\n", (unsigned int)refcount);
    refcount = ID3D12Device_Release(tmp_device);
    ok(refcount == 3, "Got unexpected refcount %u.\n", (unsigned int)refcount);

    check_interface(command_list, &IID_ID3D12Object, true);
    check_interface(command_list, &IID_ID3D12DeviceChild, true);
    check_interface(command_list, &IID_ID3D12Pageable, false);
    check_interface(command_list, &IID_ID3D12CommandList, true);
    check_interface(command_list, &IID_ID3D12GraphicsCommandList, true);
    check_interface(command_list, &IID_ID3D12CommandAllocator, false);

    refcount = ID3D12CommandList_Release(command_list);
    ok(!refcount, "ID3D12CommandList has %u references left.\n", (unsigned int)refcount);
    refcount = ID3D12CommandAllocator_Release(command_allocator);
    ok(!refcount, "ID3D12CommandAllocator has %u references left.\n", (unsigned int)refcount);

    hr = ID3D12Device_CreateCommandAllocator(device, D3D12_COMMAND_LIST_TYPE_BUNDLE,
            &IID_ID3D12CommandAllocator, (void **)&command_allocator);
    ok(SUCCEEDED(hr), "Failed to create command allocator, hr %#x.\n", hr);
    hr = ID3D12Device_CreateCommandList(device, 0, D3D12_COMMAND_LIST_TYPE_DIRECT,
            command_allocator, NULL, &IID_ID3D12CommandList, (void **)&command_list);
    ok(hr == E_INVALIDARG, "Got unexpected hr %#x.\n", hr);
    hr = ID3D12Device_CreateCommandList(device, 0, D3D12_COMMAND_LIST_TYPE_BUNDLE,
            command_allocator, NULL, &IID_ID3D12CommandList, (void **)&command_list);
    ok(SUCCEEDED(hr), "Failed to create command list, hr %#x.\n", hr);
    check_interface(command_list, &IID_ID3D12GraphicsCommandList, true);
    refcount = ID3D12CommandList_Release(command_list);
    ok(!refcount, "ID3D12CommandList has %u references left.\n", (unsigned int)refcount);
    refcount = ID3D12CommandAllocator_Release(command_allocator);
    ok(!refcount, "ID3D12CommandAllocator has %u references left.\n", (unsigned int)refcount);

    hr = ID3D12Device_CreateCommandAllocator(device, D3D12_COMMAND_LIST_TYPE_COMPUTE,
            &IID_ID3D12CommandAllocator, (void **)&command_allocator);
    ok(SUCCEEDED(hr), "Failed to create command allocator, hr %#x.\n", hr);
    hr = ID3D12Device_CreateCommandList(device, 0, D3D12_COMMAND_LIST_TYPE_BUNDLE,
            command_allocator, NULL, &IID_ID3D12CommandList, (void **)&command_list);
    ok(hr == E_INVALIDARG, "Got unexpected hr %#x.\n", hr);
    hr = ID3D12Device_CreateCommandList(device, 0, D3D12_COMMAND_LIST_TYPE_COMPUTE,
            command_allocator, NULL, &IID_ID3D12CommandList, (void **)&command_list);
    ok(SUCCEEDED(hr), "Failed to create command list, hr %#x.\n", hr);
    check_interface(command_list, &IID_ID3D12GraphicsCommandList, true);
    refcount = ID3D12CommandList_Release(command_list);
    ok(!refcount, "ID3D12CommandList has %u references left.\n", (unsigned int)refcount);
    refcount = ID3D12CommandAllocator_Release(command_allocator);
    ok(!refcount, "ID3D12CommandAllocator has %u references left.\n", (unsigned int)refcount);

    hr = ID3D12Device_CreateCommandAllocator(device, D3D12_COMMAND_LIST_TYPE_COPY,
            &IID_ID3D12CommandAllocator, (void **)&command_allocator);
    ok(SUCCEEDED(hr), "Failed to create command allocator, hr %#x.\n", hr);
    hr = ID3D12Device_CreateCommandList(device, 0, D3D12_COMMAND_LIST_TYPE_DIRECT,
            command_allocator, NULL, &IID_ID3D12CommandList, (void **)&command_list);
    ok(hr == E_INVALIDARG, "Got unexpected hr %#x.\n", hr);
    hr = ID3D12Device_CreateCommandList(device, 0, D3D12_COMMAND_LIST_TYPE_COMPUTE,
            command_allocator, NULL, &IID_ID3D12CommandList, (void **)&command_list);
    ok(hr == E_INVALIDARG, "Got unexpected hr %#x.\n", hr);
    hr = ID3D12Device_CreateCommandList(device, 0, D3D12_COMMAND_LIST_TYPE_COPY,
            command_allocator, NULL, &IID_ID3D12CommandList, (void **)&command_list);
    ok(SUCCEEDED(hr), "Failed to create command list, hr %#x.\n", hr);
    check_interface(command_list, &IID_ID3D12GraphicsCommandList, true);
    refcount = ID3D12CommandList_Release(command_list);
    ok(!refcount, "ID3D12CommandList has %u references left.\n", (unsigned int)refcount);
    refcount = ID3D12CommandAllocator_Release(command_allocator);
    ok(!refcount, "ID3D12CommandAllocator has %u references left.\n", (unsigned int)refcount);

    refcount = ID3D12Device_Release(device);
    ok(!refcount, "ID3D12Device has %u references left.\n", (unsigned int)refcount);
}

void test_create_command_queue(void)
{
    ID3D12CommandQueue* direct_queues[8], *compute_queues[8];
    D3D12_COMMAND_QUEUE_DESC desc, result_desc;
    ID3D12Device *device, *tmp_device;
    ID3D12CommandQueue *queue;
    unsigned int i;
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
    ok(SUCCEEDED(hr), "Failed to create command queue, hr %#x.\n", hr);

    refcount = get_refcount(device);
    ok(refcount == 2, "Got unexpected refcount %u.\n", (unsigned int)refcount);
    hr = ID3D12CommandQueue_GetDevice(queue, &IID_ID3D12Device, (void **)&tmp_device);
    ok(SUCCEEDED(hr), "Failed to get device, hr %#x.\n", hr);
    refcount = get_refcount(device);
    ok(refcount == 3, "Got unexpected refcount %u.\n", (unsigned int)refcount);
    refcount = ID3D12Device_Release(tmp_device);
    ok(refcount == 2, "Got unexpected refcount %u.\n", (unsigned int)refcount);

    check_interface(queue, &IID_ID3D12Object, true);
    check_interface(queue, &IID_ID3D12DeviceChild, true);
    check_interface(queue, &IID_ID3D12Pageable, true);
    check_interface(queue, &IID_ID3D12CommandQueue, true);

    result_desc = ID3D12CommandQueue_GetDesc(queue);
    ok(result_desc.Type == desc.Type, "Got unexpected type %#x.\n", result_desc.Type);
    ok(result_desc.Priority == desc.Priority, "Got unexpected priority %#x.\n", result_desc.Priority);
    ok(result_desc.Flags == desc.Flags, "Got unexpected flags %#x.\n", result_desc.Flags);
    ok(result_desc.NodeMask == 0x1, "Got unexpected node mask 0x%08x.\n", result_desc.NodeMask);

    refcount = ID3D12CommandQueue_Release(queue);
    ok(!refcount, "ID3D12CommandQueue has %u references left.\n", (unsigned int)refcount);

    desc.Type = D3D12_COMMAND_LIST_TYPE_COMPUTE;
    hr = ID3D12Device_CreateCommandQueue(device, &desc, &IID_ID3D12CommandQueue, (void **)&queue);
    ok(SUCCEEDED(hr), "Failed to create command queue, hr %#x.\n", hr);

    result_desc = ID3D12CommandQueue_GetDesc(queue);
    ok(result_desc.Type == desc.Type, "Got unexpected type %#x.\n", result_desc.Type);
    ok(result_desc.Priority == desc.Priority, "Got unexpected priority %#x.\n", result_desc.Priority);
    ok(result_desc.Flags == desc.Flags, "Got unexpected flags %#x.\n", result_desc.Flags);
    ok(result_desc.NodeMask == 0x1, "Got unexpected node mask 0x%08x.\n", result_desc.NodeMask);

    refcount = ID3D12CommandQueue_Release(queue);
    ok(!refcount, "ID3D12CommandQueue has %u references left.\n", (unsigned int)refcount);

    desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    for (i = 0; i < ARRAY_SIZE(direct_queues); ++i)
    {
        hr = ID3D12Device_CreateCommandQueue(device, &desc, &IID_ID3D12CommandQueue, (void **)&direct_queues[i]);
        ok(hr == S_OK, "Failed to create direct command queue %u, hr %#x.\n", hr, i);
    }
    desc.Type = D3D12_COMMAND_LIST_TYPE_COMPUTE;
    for (i = 0; i < ARRAY_SIZE(compute_queues); ++i)
    {
        hr = ID3D12Device_CreateCommandQueue(device, &desc, &IID_ID3D12CommandQueue, (void **)&compute_queues[i]);
        ok(hr == S_OK, "Failed to create compute command queue %u, hr %#x.\n", hr, i);
    }

    for (i = 0; i < ARRAY_SIZE(direct_queues); ++i)
        ID3D12CommandQueue_Release(direct_queues[i]);
    for (i = 0; i < ARRAY_SIZE(compute_queues); ++i)
        ID3D12CommandQueue_Release(compute_queues[i]);

    refcount = ID3D12Device_Release(device);
    ok(!refcount, "ID3D12Device has %u references left.\n", (unsigned int)refcount);
}

void test_create_command_signature(void)
{
    D3D12_INDIRECT_ARGUMENT_DESC argument_desc[3];
    D3D12_COMMAND_SIGNATURE_DESC signature_desc;
    ID3D12CommandSignature *command_signature;
    ID3D12Device *device;
    unsigned int i;
    ULONG refcount;
    HRESULT hr;

    if (!(device = create_device()))
    {
        skip("Failed to create device.\n");
        return;
    }

    signature_desc.ByteStride = 1024;
    signature_desc.NumArgumentDescs = ARRAY_SIZE(argument_desc);
    signature_desc.pArgumentDescs = argument_desc;
    signature_desc.NodeMask = 0;

    for (i = 0; i < ARRAY_SIZE(argument_desc); ++i)
        argument_desc[i].Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW;
    hr = ID3D12Device_CreateCommandSignature(device, &signature_desc,
            NULL, &IID_ID3D12CommandSignature, (void **)&command_signature);
    ok(hr == E_INVALIDARG, "Got unexpected hr %#x.\n", hr);

    for (i = 0; i < ARRAY_SIZE(argument_desc); ++i)
        argument_desc[i].Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED;
    hr = ID3D12Device_CreateCommandSignature(device, &signature_desc,
            NULL, &IID_ID3D12CommandSignature, (void **)&command_signature);
    ok(hr == E_INVALIDARG, "Got unexpected hr %#x.\n", hr);

    for (i = 0; i < ARRAY_SIZE(argument_desc); ++i)
        argument_desc[i].Type = D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH;
    hr = ID3D12Device_CreateCommandSignature(device, &signature_desc,
            NULL, &IID_ID3D12CommandSignature, (void **)&command_signature);
    ok(hr == E_INVALIDARG, "Got unexpected hr %#x.\n", hr);

    argument_desc[0].Type = D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH;
    argument_desc[1].Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW;
    signature_desc.NumArgumentDescs = 2;
    hr = ID3D12Device_CreateCommandSignature(device, &signature_desc,
            NULL, &IID_ID3D12CommandSignature, (void **)&command_signature);
    ok(hr == E_INVALIDARG, "Got unexpected hr %#x.\n", hr);

    refcount = ID3D12Device_Release(device);
    ok(!refcount, "ID3D12Device has %u references left.\n", (unsigned int)refcount);
}

struct private_data
{
    ID3D12Object *object;
    GUID guid;
    unsigned int value;
};

static void private_data_thread_main(void *untyped_data)
{
    struct private_data *data = untyped_data;
    unsigned int i;
    HRESULT hr;

    hr = ID3D12Object_SetPrivateData(data->object, &data->guid, sizeof(data->value), &data->value);
    ok(hr == S_OK, "Got unexpected hr %#x.\n", hr);

    for (i = 0; i < 100000; ++i)
    {
        hr = ID3D12Object_SetPrivateData(data->object, &data->guid, 0, NULL);
        ok(hr == S_OK, "Got unexpected hr %#x.\n", hr);
        hr = ID3D12Object_SetPrivateData(data->object, &data->guid, sizeof(data->value), &data->value);
        ok(hr == S_OK, "Got unexpected hr %#x.\n", hr);
    }
}

struct private_data_interface
{
    ID3D12Object *object;
    GUID guid;
    IUnknown *iface;
};

static void private_data_interface_thread_main(void *untyped_data)
{
    struct private_data_interface *data = untyped_data;
    unsigned int i;
    HRESULT hr;

    for (i = 0; i < 100000; ++i)
    {
        hr = ID3D12Object_SetPrivateDataInterface(data->object, &data->guid, data->iface);
        ok(hr == S_OK, "Got unexpected hr %#x.\n", hr);
        hr = ID3D12Object_SetPrivateDataInterface(data->object, &data->guid, NULL);
        ok(hr == S_OK, "Got unexpected hr %#x.\n", hr);
        hr = ID3D12Object_SetPrivateDataInterface(data->object, &data->guid, data->iface);
        ok(hr == S_OK, "Got unexpected hr %#x.\n", hr);
    }
}

void test_multithread_private_data(void)
{
    static const GUID guid = {0xfdb37466, 0x428f, 0x4edf, {0xa3, 0x7f, 0x9b, 0x1d, 0xf4, 0x88, 0xc5, 0x00}};
    struct private_data_interface private_data_interface[4];
    HANDLE private_data_interface_thread[4];
    struct private_data private_data[4];
    ID3D12RootSignature *root_signature;
    HANDLE private_data_thread[4];
    IUnknown *test_object, *unk;
    ID3D12Device *device;
    ID3D12Object *object;
    unsigned int value;
    unsigned int size;
    unsigned int id;
    unsigned int i;
    ULONG refcount;
    HRESULT hr;

    if (!(device = create_device()))
    {
        skip("Failed to create device.\n");
        return;
    }

    root_signature = create_empty_root_signature(device,
            D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);
    hr = ID3D12RootSignature_QueryInterface(root_signature, &IID_ID3D12Object, (void **)&object);
    ok(hr == S_OK, "Got unexpected hr %#x.\n", hr);
    ID3D12RootSignature_Release(root_signature);

    hr = ID3D12Device_CreateFence(device, 0, D3D12_FENCE_FLAG_NONE,
            &IID_ID3D12Fence, (void **)&test_object);
    ok(hr == S_OK, "Failed to create fence, hr %#x.\n", hr);

    for (i = 0, id = 1; i < ARRAY_SIZE(private_data_interface); ++i, ++id)
    {
        private_data_interface[i].object = object;
        private_data_interface[i].guid = guid;
        private_data_interface[i].guid.Data4[7] = id;

        hr = ID3D12Device_CreateFence(device, 0, D3D12_FENCE_FLAG_NONE,
                &IID_ID3D12Fence, (void **)&private_data_interface[i].iface);
        ok(hr == S_OK, "Failed to create fence %u, hr %#x.\n", i, hr);
    }
    for (i = 0; i < ARRAY_SIZE(private_data); ++i, ++id)
    {
        private_data[i].object = object;
        private_data[i].guid = guid;
        private_data[i].guid.Data4[7] = id;
        private_data[i].value = id;
    }

    for (i = 0; i < 4; ++i)
    {
        private_data_interface_thread[i] = create_thread(private_data_interface_thread_main, &private_data_interface[i]);
        private_data_thread[i] = create_thread(private_data_thread_main, &private_data[i]);
    }

    for (i = 0; i < 100000; ++i)
    {
        hr = ID3D12Object_SetPrivateDataInterface(object, &guid, test_object);
        ok(hr == S_OK, "Got unexpected hr %#x.\n", hr);
        hr = ID3D12Object_SetPrivateDataInterface(object, &guid, NULL);
        ok(hr == S_OK, "Got unexpected hr %#x.\n", hr);
        hr = ID3D12Object_SetPrivateDataInterface(object, &guid, test_object);
        ok(hr == S_OK, "Got unexpected hr %#x.\n", hr);
    }

    for (i = 0; i < 4; ++i)
    {
        ok(join_thread(private_data_interface_thread[i]), "Failed to join thread %u.\n", i);
        ok(join_thread(private_data_thread[i]), "Failed to join thread %u.\n", i);
    }

    for (i = 0; i < ARRAY_SIZE(private_data_interface); ++i)
    {
        size = sizeof(unk);
        hr = ID3D12Object_GetPrivateData(object, &private_data_interface[i].guid, &size, &unk);
        ok(hr == S_OK, "Got unexpected hr %#x.\n", hr);

        ok(unk == private_data_interface[i].iface, "Got %p, expected %p.\n", unk, private_data_interface[i].iface);
        IUnknown_Release(unk);
        refcount = IUnknown_Release(private_data_interface[i].iface);
        ok(refcount == 1, "Got unexpected refcount %u.\n", (unsigned int)refcount);
    }
    for (i = 0; i < ARRAY_SIZE(private_data); ++i)
    {
        size = sizeof(value);
        hr = ID3D12Object_GetPrivateData(object, &private_data[i].guid, &size, &value);
        ok(hr == S_OK, "Got unexpected hr %#x.\n", hr);

        ok(value == private_data[i].value, "Got %u, expected %u.\n", value, private_data[i].value);
    }

    hr = ID3D12Object_SetPrivateDataInterface(object, &guid, NULL);
    ok(hr == S_OK, "Got unexpected hr %#x.\n", hr);
    refcount = IUnknown_Release(test_object);
    ok(!refcount, "Test object has %u references left.\n", (unsigned int)refcount);
    refcount = ID3D12Object_Release(object);
    ok(!refcount, "Object has %u references left.\n", (unsigned int)refcount);
    refcount = ID3D12Device_Release(device);
    ok(!refcount, "ID3D12Device has %u references left.\n", (unsigned int)refcount);
}

void test_reset_command_allocator(void)
{
    ID3D12CommandAllocator *command_allocator, *command_allocator2;
    ID3D12GraphicsCommandList *command_list, *command_list2;
    D3D12_COMMAND_QUEUE_DESC command_queue_desc;
    ID3D12CommandQueue *queue;
    ID3D12Device *device;
    unsigned int i;
    ULONG refcount;
    HRESULT hr;

    static const D3D12_COMMAND_LIST_TYPE tests[] =
    {
        D3D12_COMMAND_LIST_TYPE_DIRECT,
        D3D12_COMMAND_LIST_TYPE_BUNDLE,
    };

    if (!(device = create_device()))
    {
        skip("Failed to create device.\n");
        return;
    }

    for (i = 0; i < ARRAY_SIZE(tests); i++)
    {
        const D3D12_COMMAND_LIST_TYPE type = tests[i];

        hr = ID3D12Device_CreateCommandAllocator(device, type,
                &IID_ID3D12CommandAllocator, (void **)&command_allocator);
        ok(SUCCEEDED(hr), "Failed to create command allocator, hr %#x.\n", hr);

        hr = ID3D12CommandAllocator_Reset(command_allocator);
        ok(hr == S_OK, "Got unexpected hr %#x.\n", hr);
        hr = ID3D12CommandAllocator_Reset(command_allocator);
        ok(hr == S_OK, "Got unexpected hr %#x.\n", hr);

        hr = ID3D12Device_CreateCommandList(device, 0, type,
                command_allocator, NULL, &IID_ID3D12GraphicsCommandList, (void **)&command_list);
        ok(SUCCEEDED(hr), "Failed to create command list, hr %#x.\n", hr);

        hr = ID3D12CommandAllocator_Reset(command_allocator);
        ok(hr == E_FAIL, "Got unexpected hr %#x.\n", hr);
        hr = ID3D12CommandAllocator_Reset(command_allocator);
        ok(hr == E_FAIL, "Got unexpected hr %#x.\n", hr);

        hr = ID3D12GraphicsCommandList_Close(command_list);
        ok(SUCCEEDED(hr), "Failed to close command list, hr %#x.\n", hr);

        hr = ID3D12CommandAllocator_Reset(command_allocator);
        ok(hr == S_OK, "Got unexpected hr %#x.\n", hr);
        hr = ID3D12CommandAllocator_Reset(command_allocator);
        ok(hr == S_OK, "Got unexpected hr %#x.\n", hr);

        hr = ID3D12GraphicsCommandList_Reset(command_list, command_allocator, NULL);
        ok(SUCCEEDED(hr), "Failed to reset command list, hr %#x.\n", hr);

        hr = ID3D12CommandAllocator_Reset(command_allocator);
        ok(hr == E_FAIL, "Got unexpected hr %#x.\n", hr);

        hr = ID3D12GraphicsCommandList_Close(command_list);
        ok(SUCCEEDED(hr), "Failed to close command list, hr %#x.\n", hr);
        hr = ID3D12GraphicsCommandList_Reset(command_list, command_allocator, NULL);
        ok(SUCCEEDED(hr), "Failed to reset command list, hr %#x.\n", hr);

        hr = ID3D12Device_CreateCommandAllocator(device, type,
                &IID_ID3D12CommandAllocator, (void **)&command_allocator2);
        ok(SUCCEEDED(hr), "Failed to create command allocator, hr %#x.\n", hr);

        if (type != D3D12_COMMAND_LIST_TYPE_BUNDLE)
        {
            command_queue_desc.Type = type;
            command_queue_desc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
            command_queue_desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
            command_queue_desc.NodeMask = 0;
            hr = ID3D12Device_CreateCommandQueue(device, &command_queue_desc,
                    &IID_ID3D12CommandQueue, (void **)&queue);
            ok(SUCCEEDED(hr), "Failed to create command queue, hr %#x.\n", hr);

            uav_barrier(command_list, NULL);
            hr = ID3D12GraphicsCommandList_Close(command_list);
            ok(SUCCEEDED(hr), "Failed to close command list, hr %#x.\n", hr);
            exec_command_list(queue, command_list);

            /* A command list can be reset when it is in use. */
            hr = ID3D12GraphicsCommandList_Reset(command_list, command_allocator2, NULL);
            ok(SUCCEEDED(hr), "Failed to reset command list, hr %#x.\n", hr);
            hr = ID3D12GraphicsCommandList_Close(command_list);
            ok(SUCCEEDED(hr), "Failed to close command list, hr %#x.\n", hr);

            wait_queue_idle(device, queue);
            hr = ID3D12CommandAllocator_Reset(command_allocator);
            ok(hr == S_OK, "Got unexpected hr %#x.\n", hr);
            hr = ID3D12GraphicsCommandList_Reset(command_list, command_allocator, NULL);
            ok(SUCCEEDED(hr), "Failed to reset command list, hr %#x.\n", hr);

            uav_barrier(command_list, NULL);
            hr = ID3D12GraphicsCommandList_Close(command_list);
            ok(SUCCEEDED(hr), "Failed to close command list, hr %#x.\n", hr);
            exec_command_list(queue, command_list);

            hr = ID3D12GraphicsCommandList_Reset(command_list, command_allocator, NULL);
            ok(SUCCEEDED(hr), "Failed to reset command list, hr %#x.\n", hr);
            hr = ID3D12GraphicsCommandList_Close(command_list);
            ok(SUCCEEDED(hr), "Failed to close command list, hr %#x.\n", hr);

            wait_queue_idle(device, queue);

            hr = ID3D12CommandAllocator_Reset(command_allocator);
            ok(hr == S_OK, "Got unexpected hr %#x.\n", hr);
            hr = ID3D12GraphicsCommandList_Reset(command_list, command_allocator, NULL);
            ok(SUCCEEDED(hr), "Failed to reset command list, hr %#x.\n", hr);
            ID3D12CommandQueue_Release(queue);
        }

        /* A command allocator can be used with one command list at a time. */
        hr = ID3D12Device_CreateCommandList(device, 0, type,
                command_allocator, NULL, &IID_ID3D12GraphicsCommandList, (void **)&command_list2);
        ok(hr == E_INVALIDARG, "Got unexpected hr %#x.\n", hr);

        hr = ID3D12Device_CreateCommandList(device, 0, type,
                command_allocator2, NULL, &IID_ID3D12GraphicsCommandList, (void **)&command_list2);
        ok(hr == S_OK, "Failed to create command list, hr %#x.\n", hr);

        hr = ID3D12GraphicsCommandList_Close(command_list2);
        ok(SUCCEEDED(hr), "Failed to close command list, hr %#x.\n", hr);
        hr = ID3D12GraphicsCommandList_Reset(command_list2, command_allocator, NULL);
        ok(hr == E_INVALIDARG, "Got unexpected hr %#x.\n", hr);
        ID3D12GraphicsCommandList_Release(command_list2);

        /* A command allocator can be re-used after closing the command list. */
        hr = ID3D12Device_CreateCommandList(device, 0, type,
                command_allocator, NULL, &IID_ID3D12GraphicsCommandList, (void **)&command_list2);
        ok(hr == E_INVALIDARG, "Got unexpected hr %#x.\n", hr);
        hr = ID3D12GraphicsCommandList_Close(command_list);
        ok(SUCCEEDED(hr), "Failed to close command list, hr %#x.\n", hr);
        hr = ID3D12Device_CreateCommandList(device, 0, type,
                command_allocator, NULL, &IID_ID3D12GraphicsCommandList, (void **)&command_list2);
        ok(hr == S_OK, "Failed to create command list, hr %#x.\n", hr);

        ID3D12GraphicsCommandList_Release(command_list);
        ID3D12GraphicsCommandList_Release(command_list2);
        ID3D12CommandAllocator_Release(command_allocator);
        ID3D12CommandAllocator_Release(command_allocator2);
    }

    refcount = ID3D12Device_Release(device);
    ok(!refcount, "ID3D12Device has %u references left.\n", (unsigned int)refcount);
}

void test_object_interface_null_cases(void)
{
    ID3D12Device *device;
    ID3D12Fence *fence;
    HRESULT hr;
    UINT ref;

    if (!(device = create_device()))
    {
        skip("Failed to create device.\n");
        return;
    }

    hr = ID3D12Device_CreateFence(device, 0, D3D12_FENCE_FLAG_NONE, &IID_ID3D12Fence, (void **)&fence);
    ok(SUCCEEDED(hr), "Failed to create fence, hr #%x.\n", hr);

    hr = ID3D12Device_CreateFence(device, 0, D3D12_FENCE_FLAG_NONE, &IID_ID3D12Fence, NULL);
    ok(hr == S_FALSE, "Unexpected hr #%x.\n", hr);

    /* S_FALSE is returned even for bogus requested interfaces. */
    hr = ID3D12Device_CreateFence(device, 0, D3D12_FENCE_FLAG_NONE, &IID_ID3D12Heap, NULL);
    ok(hr == S_FALSE, "Unexpected hr #%x.\n", hr);

    /* E_POINTER is always returned if QueryInterface ppOutput is NULL. */
    hr = ID3D12Fence_QueryInterface(fence, &IID_IUnknown, NULL);
    ok(hr == E_POINTER, "Unexpected hr #%x.\n", hr);

    hr = ID3D12Fence_QueryInterface(fence, &IID_ID3D12Fence, NULL);
    ok(hr == E_POINTER, "Unexpected hr #%x.\n", hr);

    hr = ID3D12Fence_QueryInterface(fence, &IID_ID3D12Heap, NULL);
    ok(hr == E_POINTER, "Unexpected hr #%x.\n", hr);

    ID3D12Fence_Release(fence);
    ref = ID3D12Device_Release(device);
    ok(ref == 0, "Unexpected ref-count %u\n", ref);
}

void test_object_interface(void)
{
    D3D12_DESCRIPTOR_HEAP_DESC descriptor_heap_desc;
    D3D12_QUERY_HEAP_DESC query_heap_desc;
    ID3D12RootSignature *root_signature;
    ULONG refcount, expected_refcount;
    ID3D12CommandAllocator *allocator;
    D3D12_HEAP_DESC heap_desc;
    IUnknown *test_object;
    ID3D12Device *device;
    ID3D12Object *object;
    IUnknown *unknown;
    unsigned int size;
    unsigned int i;
    IUnknown *ptr;
    HRESULT hr;

    static const GUID test_guid
            = {0xfdb37466, 0x428f, 0x4edf, {0xa3, 0x7f, 0x9b, 0x1d, 0xf4, 0x88, 0xc5, 0xfc}};
    static const GUID test_guid2
            = {0x2e5afac2, 0x87b5, 0x4c10, {0x9b, 0x4b, 0x89, 0xd7, 0xd1, 0x12, 0xe7, 0x2b}};
    static const DWORD data[] = {1, 2, 3, 4};
    static const char terminated_name_a[] = { 'T', 'e', 's', 't', 'A', '\0' };
    static const char non_terminated_name_a[] = { 'T', 'e', 's', 't' };
    static const WCHAR non_terminated_name_w[] = { L'T', L'e', L's', L't', L'w' };
    WCHAR temp_name_buffer[1024];
    static const GUID *tests[] =
    {
        &IID_ID3D12CommandAllocator,
        &IID_ID3D12CommandList,
        &IID_ID3D12CommandQueue,
        &IID_ID3D12CommandSignature,
        &IID_ID3D12DescriptorHeap,
        &IID_ID3D12Device,
        &IID_ID3D12Fence,
        &IID_ID3D12Heap,
        &IID_ID3D12PipelineState,
        &IID_ID3D12QueryHeap,
        &IID_ID3D12Resource,
        &IID_ID3D12RootSignature,
    };

    if (!(device = create_device()))
    {
        skip("Failed to create device.\n");
        return;
    }

    for (i = 0; i < ARRAY_SIZE(tests); ++i)
    {
        if (IsEqualGUID(tests[i], &IID_ID3D12CommandAllocator))
        {
            vkd3d_test_set_context("command allocator");
            hr = ID3D12Device_CreateCommandAllocator(device, D3D12_COMMAND_LIST_TYPE_DIRECT,
                    &IID_IUnknown, (void **)&unknown);
            ok(hr == S_OK, "Failed to create command allocator, hr %#x.\n", hr);
        }
        else if (IsEqualGUID(tests[i], &IID_ID3D12CommandList))
        {
            vkd3d_test_set_context("command list");
            hr = ID3D12Device_CreateCommandAllocator(device, D3D12_COMMAND_LIST_TYPE_DIRECT,
                    &IID_ID3D12CommandAllocator, (void **)&allocator);
            ok(hr == S_OK, "Failed to create command allocator, hr %#x.\n", hr);
            hr = ID3D12Device_CreateCommandList(device, 0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                    allocator, NULL, &IID_IUnknown, (void **)&unknown);
            ok(hr == S_OK, "Failed to create command list, hr %#x.\n", hr);
            ID3D12CommandAllocator_Release(allocator);
        }
        else if (IsEqualGUID(tests[i], &IID_ID3D12CommandQueue))
        {
            vkd3d_test_set_context("command queue");
            unknown = (IUnknown *)create_command_queue(device,
                    D3D12_COMMAND_LIST_TYPE_DIRECT, D3D12_COMMAND_QUEUE_PRIORITY_NORMAL);
        }
        else if (IsEqualGUID(tests[i], &IID_ID3D12CommandSignature))
        {
            vkd3d_test_set_context("command signature");
            unknown = (IUnknown *)create_command_signature(device, D3D12_INDIRECT_ARGUMENT_TYPE_DRAW);
        }
        else if (IsEqualGUID(tests[i], &IID_ID3D12DescriptorHeap))
        {
            vkd3d_test_set_context("descriptor heap");
            descriptor_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
            descriptor_heap_desc.NumDescriptors = 16;
            descriptor_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
            descriptor_heap_desc.NodeMask = 0;
            hr = ID3D12Device_CreateDescriptorHeap(device, &descriptor_heap_desc,
                    &IID_ID3D12DescriptorHeap, (void **)&unknown);
            ok(hr == S_OK, "Failed to create descriptor heap, hr %#x.\n", hr);
        }
        else if (IsEqualGUID(tests[i], &IID_ID3D12Device))
        {
            vkd3d_test_set_context("device");
            unknown = (IUnknown *)create_device();
        }
        else if (IsEqualGUID(tests[i], &IID_ID3D12Fence))
        {
            vkd3d_test_set_context("fence");
            hr = ID3D12Device_CreateFence(device, 0, D3D12_FENCE_FLAG_NONE,
                    &IID_IUnknown, (void **)&unknown);
            ok(hr == S_OK, "Failed to create fence, hr %#x.\n", hr);
        }
        else if (IsEqualGUID(tests[i], &IID_ID3D12Heap))
        {
            vkd3d_test_set_context("heap");
            heap_desc.SizeInBytes = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
            memset(&heap_desc.Properties, 0, sizeof(heap_desc.Properties));
            heap_desc.Properties.Type = D3D12_HEAP_TYPE_DEFAULT;
            heap_desc.Alignment = 0;
            heap_desc.Flags = D3D12_HEAP_FLAG_ALLOW_ONLY_NON_RT_DS_TEXTURES;
            hr = ID3D12Device_CreateHeap(device, &heap_desc, &IID_ID3D12Heap, (void **)&unknown);
            ok(hr == S_OK, "Failed to create heap, hr %#x.\n", hr);
        }
        else if (IsEqualGUID(tests[i], &IID_ID3D12PipelineState))
        {
            vkd3d_test_set_context("pipeline state");
            root_signature = create_empty_root_signature(device, 0);
            unknown = (IUnknown *)create_pipeline_state(device,
                    root_signature, DXGI_FORMAT_R8G8B8A8_UNORM, NULL, NULL, NULL);
            ID3D12RootSignature_Release(root_signature);
        }
        else if (IsEqualGUID(tests[i], &IID_ID3D12QueryHeap))
        {
            vkd3d_test_set_context("query heap");
            query_heap_desc.Type = D3D12_QUERY_HEAP_TYPE_OCCLUSION;
            query_heap_desc.Count = 8;
            query_heap_desc.NodeMask = 0;
            hr = ID3D12Device_CreateQueryHeap(device, &query_heap_desc,
                    &IID_ID3D12QueryHeap, (void **)&unknown);
            ok(hr == S_OK, "Failed to create query heap, hr %#x.\n", hr);
        }
        else if (IsEqualGUID(tests[i], &IID_ID3D12Resource))
        {
            vkd3d_test_set_context("resource");
            unknown = (IUnknown *)create_readback_buffer(device, 512);
        }
        else if (IsEqualGUID(tests[i], &IID_ID3D12RootSignature))
        {
            vkd3d_test_set_context("root signature");
            unknown = (IUnknown *)create_empty_root_signature(device, 0);
        }
        else
        {
            unknown = NULL;
        }

        ok(unknown, "Unhandled object type %u.\n", i);
        object = NULL;
        hr = IUnknown_QueryInterface(unknown, &IID_ID3D12Object, (void **)&object);
        ok(hr == S_OK, "Got unexpected hr %#x.\n", hr);
        IUnknown_Release(unknown);

        hr = ID3D12Object_SetPrivateData(object, &test_guid, 0, NULL);
        ok(hr == S_FALSE, "Got unexpected hr %#x.\n", hr);
        hr = ID3D12Object_SetPrivateDataInterface(object, &test_guid, NULL);
        ok(hr == S_OK, "Got unexpected hr %#x.\n", hr);
        hr = ID3D12Object_SetPrivateData(object, &test_guid, ~0u, NULL);
        ok(hr == S_OK, "Got unexpected hr %#x.\n", hr);
        hr = ID3D12Object_SetPrivateData(object, &test_guid, ~0u, NULL);
        ok(hr == S_FALSE, "Got unexpected hr %#x.\n", hr);

        hr = ID3D12Object_SetPrivateDataInterface(object, &test_guid, NULL);
        ok(hr == S_OK, "Got unexpected hr %#x.\n", hr);
        size = sizeof(ptr) * 2;
        ptr = (IUnknown *)(uintptr_t)0xdeadbeef;
        hr = ID3D12Object_GetPrivateData(object, &test_guid, &size, &ptr);
        ok(hr == S_OK, "Got unexpected hr %#x.\n", hr);
        ok(!ptr, "Got unexpected pointer %p.\n", ptr);
        ok(size == sizeof(IUnknown *), "Got unexpected size %u.\n", size);

        hr = ID3D12Device_CreateFence(device, 0, D3D12_FENCE_FLAG_NONE,
                &IID_ID3D12Fence, (void **)&test_object);
        ok(hr == S_OK, "Failed to create fence, hr %#x.\n", hr);

        refcount = get_refcount(test_object);
        hr = ID3D12Object_SetPrivateDataInterface(object, &test_guid, (IUnknown *)test_object);
        ok(hr == S_OK, "Got unexpected hr %#x.\n", hr);
        expected_refcount = refcount + 1;
        refcount = get_refcount(test_object);
        ok(refcount == expected_refcount, "Got unexpected refcount %u, expected %u.\n",
                (unsigned int)refcount, (unsigned int)expected_refcount);
        hr = ID3D12Object_SetPrivateDataInterface(object, &test_guid, (IUnknown *)test_object);
        ok(hr == S_OK, "Got unexpected hr %#x.\n", hr);
        refcount = get_refcount(test_object);
        ok(refcount == expected_refcount, "Got unexpected refcount %u, expected %u.\n",
                (unsigned int)refcount, (unsigned int)expected_refcount);

        hr = ID3D12Object_SetPrivateDataInterface(object, &test_guid, NULL);
        ok(hr == S_OK, "Got unexpected hr %#x.\n", hr);
        --expected_refcount;
        refcount = get_refcount(test_object);
        ok(refcount == expected_refcount, "Got unexpected refcount %u, expected %u.\n",
                (unsigned int)refcount, (unsigned int)expected_refcount);

        hr = ID3D12Object_SetPrivateDataInterface(object, &test_guid, (IUnknown *)test_object);
        ok(hr == S_OK, "Got unexpected hr %#x.\n", hr);
        size = sizeof(data);
        hr = ID3D12Object_SetPrivateData(object, &test_guid, size, data);
        ok(hr == S_OK, "Got unexpected hr %#x.\n", hr);
        refcount = get_refcount(test_object);
        ok(refcount == expected_refcount, "Got unexpected refcount %u, expected %u.\n",
                (unsigned int)refcount, (unsigned int)expected_refcount);
        hr = ID3D12Object_SetPrivateData(object, &test_guid, 42, NULL);
        ok(hr == S_OK, "Got unexpected hr %#x.\n", hr);
        hr = ID3D12Object_SetPrivateData(object, &test_guid, 42, NULL);
        ok(hr == S_FALSE, "Got unexpected hr %#x.\n", hr);

        hr = ID3D12Object_SetPrivateDataInterface(object, &test_guid, (IUnknown *)test_object);
        ok(hr == S_OK, "Got unexpected hr %#x.\n", hr);
        ++expected_refcount;
        size = 2 * sizeof(ptr);
        ptr = NULL;
        hr = ID3D12Object_GetPrivateData(object, &test_guid, &size, &ptr);
        ok(hr == S_OK, "Got unexpected hr %#x.\n", hr);
        ok(size == sizeof(test_object), "Got unexpected size %u.\n", size);
        ++expected_refcount;
        refcount = get_refcount(test_object);
        ok(refcount == expected_refcount, "Got unexpected refcount %u, expected %u.\n",
                (unsigned int)refcount, (unsigned int)expected_refcount);
        IUnknown_Release(ptr);
        --expected_refcount;

        ptr = (IUnknown *)(uintptr_t)0xdeadbeef;
        size = 1;
        hr = ID3D12Object_GetPrivateData(object, &test_guid, &size, NULL);
        ok(hr == S_OK, "Got unexpected hr %#x.\n", hr);
        ok(size == sizeof(ptr), "Got unexpected size %u.\n", size);
        size = 2 * sizeof(ptr);
        hr = ID3D12Object_GetPrivateData(object, &test_guid, &size, NULL);
        ok(hr == S_OK, "Got unexpected hr %#x.\n", hr);
        ok(size == sizeof(ptr), "Got unexpected size %u.\n", size);
        refcount = get_refcount(test_object);
        ok(refcount == expected_refcount, "Got unexpected refcount %u, expected %u.\n",
                (unsigned int)refcount, (unsigned int)expected_refcount);

        size = 1;
        hr = ID3D12Object_GetPrivateData(object, &test_guid, &size, &ptr);
        ok(hr == DXGI_ERROR_MORE_DATA, "Got unexpected hr %#x.\n", hr);
        ok(size == sizeof(object), "Got unexpected size %u.\n", size);
        ok(ptr == (IUnknown *)(uintptr_t)0xdeadbeef, "Got unexpected pointer %p.\n", ptr);
        size = 1;
        hr = ID3D12Object_GetPrivateData(object, &test_guid2, &size, &ptr);
        ok(hr == DXGI_ERROR_NOT_FOUND, "Got unexpected hr %#x.\n", hr);
        ok(!size, "Got unexpected size %u.\n", size);
        ok(ptr == (IUnknown *)(uintptr_t)0xdeadbeef, "Got unexpected pointer %p.\n", ptr);

        if (IsEqualGUID(tests[i], &IID_ID3D12Device))
        {
            hr = ID3D12Object_SetPrivateDataInterface(object, &test_guid, NULL);
            ok(hr == S_OK, "Got unexpected hr %#x.\n", hr);
        }

        hr = ID3D12Object_SetName(object, u"");
        ok(hr == S_OK, "Got unexpected hr %#x.\n", hr);

        hr = ID3D12Object_SetName(object, u"deadbeef");
        ok(hr == S_OK, "Got unexpected hr %#x.\n", hr);

        size = 1;
        hr = ID3D12Object_GetPrivateData(object, &WKPDID_D3DDebugObjectNameW, &size, NULL);
        ok(hr == S_OK, "Got unexpected hr %#x.\n", hr);
        ok(size == 18, "Got unexpected size %u.\n", size);
        hr = ID3D12Object_GetPrivateData(object, &WKPDID_D3DDebugObjectNameW, &size, temp_name_buffer);
        ok(hr == S_OK, "Got unexpected hr %#x.\n", hr);
        ok(size == 18, "Got unexpected size %u.\n", size);
        ok(temp_name_buffer[1] == L'e', "Got unexpected name");
        size = 1;
        hr = ID3D12Object_GetPrivateData(object, &WKPDID_D3DDebugObjectName, &size, NULL);
        ok(hr == DXGI_ERROR_NOT_FOUND, "Got unexpected hr %#x.\n", hr);

#if 0
        /* NULL name crashes on Windows 11 22621. */
        hr = ID3D12Object_SetName(object, NULL);
        ok(hr == S_OK, "Got unexpected hr %#x.\n", hr);

        hr = ID3D12Object_GetPrivateData(object, &WKPDID_D3DDebugObjectNameW, &size, NULL);
        ok(hr == DXGI_ERROR_NOT_FOUND, "Got unexpected hr %#x.\n", hr);
#endif

        hr = ID3D12Object_SetPrivateData(object, &WKPDID_D3DDebugObjectName, sizeof(terminated_name_a), terminated_name_a);
        ok(hr == S_OK, "Got unexpected hr %#x.\n", hr);

        hr = ID3D12Object_SetPrivateData(object, &WKPDID_D3DDebugObjectName, sizeof(non_terminated_name_a), non_terminated_name_a);
        ok(hr == S_OK, "Got unexpected hr %#x.\n", hr);

        hr = ID3D12Object_SetPrivateData(object, &WKPDID_D3DDebugObjectNameW, sizeof(non_terminated_name_w), non_terminated_name_w);
        ok(hr == S_OK, "Got unexpected hr %#x.\n", hr);

        hr = ID3D12Object_SetPrivateData(object, &WKPDID_D3DDebugObjectNameW, 0, NULL);
        ok(hr == S_OK, "Got unexpected hr %#x.\n", hr);

        hr = ID3D12Object_SetPrivateData(object, &WKPDID_D3DDebugObjectName, 0, NULL);
        ok(hr == S_OK, "Got unexpected hr %#x.\n", hr);

        ID3D12Object_Release(object);

        refcount = IUnknown_Release(test_object);
        ok(!refcount, "Test object has %u references left.\n", (unsigned int)refcount);

        vkd3d_test_set_context(NULL);
    }

    refcount = ID3D12Device_Release(device);
    ok(!refcount, "ID3D12Device has %u references left.\n", (unsigned int)refcount);
}

void test_device_removed_reason(void)
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
    ok(SUCCEEDED(hr), "Failed to create command queue, hr %#x.\n", hr);

    hr = ID3D12Device_CreateCommandAllocator(device, D3D12_COMMAND_LIST_TYPE_DIRECT,
            &IID_ID3D12CommandAllocator, (void **)&command_allocator);
    ok(SUCCEEDED(hr), "Failed to create command allocator, hr %#x.\n", hr);

    hr = ID3D12Device_CreateCommandList(device, 0, D3D12_COMMAND_LIST_TYPE_DIRECT,
            command_allocator, NULL, &IID_ID3D12GraphicsCommandList, (void **)&command_list);
    ok(SUCCEEDED(hr), "Failed to create command list, hr %#x.\n", hr);

    /* Execute a command list in the recording state. */
    exec_command_list(queue, command_list);

    hr = ID3D12Device_GetDeviceRemovedReason(device);
    ok(hr == DXGI_ERROR_INVALID_CALL, "Got unexpected hr %#x.\n", hr);

    hr = ID3D12Device_CreateCommandQueue(device, &command_queue_desc,
            &IID_ID3D12CommandQueue, (void **)&tmp_queue);
    todo ok(hr == DXGI_ERROR_DEVICE_REMOVED, "Got unexpected hr %#x.\n", hr);
    if (SUCCEEDED(hr))
        ID3D12CommandQueue_Release(tmp_queue);

    hr = ID3D12Device_GetDeviceRemovedReason(device);
    ok(hr == DXGI_ERROR_INVALID_CALL, "Got unexpected hr %#x.\n", hr);

    ID3D12GraphicsCommandList_Release(command_list);
    ID3D12CommandAllocator_Release(command_allocator);
    ID3D12CommandQueue_Release(queue);
    refcount = ID3D12Device_Release(device);
    ok(!refcount, "ID3D12Device has %u references left.\n", (unsigned int)refcount);
}

void test_enumerate_meta_commands(void)
{
    D3D12_META_COMMAND_DESC dummy_desc, *descs;
    UINT desc_count, supported_count;
    ID3D12Device5 *device5;
    ID3D12Device *device;
    ULONG refcount;
    HRESULT hr;

    if (!(device = create_device()))
    {
        skip("Failed to create device.\n");
        return;
    }

    hr = ID3D12Device_QueryInterface(device, &IID_ID3D12Device5, (void**)&device5);
    ID3D12Device_Release(device);

    if (FAILED(hr))
    {
        skip("ID3D12Device5 not supported by implementation.\n");
        return;
    }

    hr = ID3D12Device5_EnumerateMetaCommands(device5, NULL, NULL);
    ok(hr == E_INVALIDARG, "Unexpected hr %#x.\n", hr);
    hr = ID3D12Device5_EnumerateMetaCommands(device5, NULL, &dummy_desc);
    ok(hr == E_INVALIDARG, "Unexpected hr %#x.\n", hr);

    desc_count = 0xdeadbeef;
    hr = ID3D12Device5_EnumerateMetaCommands(device5, &desc_count, NULL);
    ok(hr == S_OK, "Unexpected hr %#x.\n", hr);

    supported_count = desc_count;

    descs = calloc(supported_count, sizeof(*descs));
    memset(&dummy_desc, 0, sizeof(dummy_desc));

    hr = ID3D12Device5_EnumerateMetaCommands(device5, &desc_count, descs);
    ok(hr == S_OK, "Unexpected hr %#x.\n", hr);
    ok(desc_count == supported_count, "Unexpected desc count %u.\n", desc_count);

    if (desc_count)
    {
        desc_count -= 1;

        hr = ID3D12Device5_EnumerateMetaCommands(device5, &desc_count, descs);
        ok(hr == S_OK, "Unexpected hr %#x.\n", hr);
        ok(desc_count == supported_count, "Unexpected desc count %u.\n", desc_count);
    }

    /* The D3D12 runtime will crash or access random data if given a larger
     * count than what is supported by the device, so don't test that here. */

    free(descs);

    refcount = ID3D12Device5_Release(device5);
    ok(!refcount, "ID3D12Device has %u references left.\n", (unsigned int)refcount);
}

void test_vtable_origins(void)
{
#ifdef _WIN32
    HMODULE d3d12core_module, vtable_module;
    MEMORY_BASIC_INFORMATION info;
    ID3D12Device *device;
    SIZE_T ret;

    if (!(device = create_device()))
    {
        skip("Failed to create device.\n");
        return;
    }

    /* Skip this test if running on an older version of
     * Windows from before the D3D12 + D3D12Core split. */
    if (!(d3d12core_module = GetModuleHandleA("d3d12core")))
    {
        skip("No D3D12Core module, skipping.\n");
        ID3D12Device_Release(device);
        return;
    }

    ret = VirtualQuery(device->lpVtbl, &info, sizeof(info));
    ok(ret, "VirtualQuery of ID3D12Device VTable failed.\n");
    if (!ret)
    {
        skip("VirtualQuery failed, skipping vtable test.\n");
        ID3D12Device_Release(device);
        return;
    }

    vtable_module = (HMODULE)info.AllocationBase;

    /* Ensure the vtable for ID3D12Device comes from D3D12Core.dll */
    ok(vtable_module == d3d12core_module, "VTable for ID3D12Device not provided by D3D12Core.\n");

    ID3D12Device_Release(device);
#endif
}

static void __stdcall destruction_notifier_callback(void* userdata)
{
    bool *invoked = userdata;

    if (userdata)
        *invoked = true;
}

void test_destruction_notifier_callback(void)
{
    D3D12_HEAP_PROPERTIES heap_properties;
    ID3DDestructionNotifier *notifier;
    D3D12_RESOURCE_DESC resource_desc;
    ID3D12Resource *resource;
    ID3D12Device *device;
    UINT callback_id;
    unsigned int i;
    HRESULT hr;

    struct
    {
        bool expected;
        bool invoked;
    }
    tests[] =
    {
        { false }, { true }, { true }, { true },
    };

    if (!(device = create_device()))
    {
        skip("Failed to create device.\n");
        return;
    }

    memset(&heap_properties, 0, sizeof(heap_properties));
    heap_properties.Type = D3D12_HEAP_TYPE_DEFAULT;

    memset(&resource_desc, 0, sizeof(resource_desc));
    resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    resource_desc.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
    resource_desc.Width = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
    resource_desc.Height = 1;
    resource_desc.DepthOrArraySize = 1;
    resource_desc.MipLevels = 1;
    resource_desc.SampleDesc.Count = 1;
    resource_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    hr = ID3D12Device_CreateCommittedResource(device, &heap_properties, D3D12_HEAP_FLAG_NONE,
            &resource_desc, D3D12_RESOURCE_STATE_COPY_DEST, NULL, &IID_ID3D12Resource, (void**)&resource);
    ok(hr == S_OK, "Failed to create resource, hr %#x.\n", hr);

    hr = ID3D12Resource_QueryInterface(resource, &IID_ID3DDestructionNotifier, (void**)&notifier);
    ok(hr == S_OK, "Failed to query destruction notifier from resource, hr %#x.\n", hr);

    if (FAILED(hr))
    {
        skip("ID3DDestructionNotifier not supported by implementation.\n");
        ID3D12Resource_Release(resource);
        ID3D12Device_Release(device);
    }

    callback_id = 0xdeadbeef;

    hr = ID3DDestructionNotifier_RegisterDestructionCallback(notifier, NULL, NULL, &callback_id);
    ok(hr == DXGI_ERROR_INVALID_CALL, "Got hr %#x, expected DXGI_ERROR_INVALID_CALL.\n", hr);
    ok(callback_id == 0xdeadbeef, "Got callback ID %d\n", callback_id);

    hr = ID3DDestructionNotifier_RegisterDestructionCallback(notifier, &destruction_notifier_callback, &tests[0].invoked, &callback_id);
    ok(hr == S_OK, "Failed to register callback, hr %#x.\n", hr);
    ok(callback_id == 1, "Expected callback ID 0, got %d.\n", callback_id);

    /* Not capturing the callback ID is fine */
    hr = ID3DDestructionNotifier_RegisterDestructionCallback(notifier, &destruction_notifier_callback, &tests[1].invoked, NULL);
    ok(hr == S_OK, "Failed to register callback, hr %#x.\n", hr);

    hr = ID3DDestructionNotifier_UnregisterDestructionCallback(notifier, callback_id);
    ok(hr == S_OK, "Failed to unregister callback, hr %#x.\n", hr);

    hr = ID3DDestructionNotifier_RegisterDestructionCallback(notifier, &destruction_notifier_callback, &tests[2].invoked, &callback_id);
    ok(hr == S_OK, "Failed to register callback, hr %#x.\n", hr);
    ok(callback_id == 2, "Expected callback ID 0, got %d.\n", callback_id);

    hr = ID3DDestructionNotifier_RegisterDestructionCallback(notifier, &destruction_notifier_callback, &tests[3].invoked, &callback_id);
    ok(hr == S_OK, "Failed to register callback, hr %#x.\n", hr);
    ok(callback_id == 3, "Expected callback ID 2, got %d.\n", callback_id);

    hr = ID3DDestructionNotifier_UnregisterDestructionCallback(notifier, 0);
    ok(hr == DXGI_ERROR_NOT_FOUND, "Got hr %#x, expected DXGI_ERROR_NOT_FOUND.\n", hr);
    hr = ID3DDestructionNotifier_UnregisterDestructionCallback(notifier, 0xdeadbeef);
    ok(hr == DXGI_ERROR_NOT_FOUND, "Got hr %#x, expected DXGI_ERROR_NOT_FOUND.\n", hr);

    ID3DDestructionNotifier_Release(notifier);
    ID3D12Resource_Release(resource);

    for (i = 0; i < ARRAY_SIZE(tests); i++)
    {
        vkd3d_test_set_context("Test %u", i);

        ok(tests[i].invoked == tests[i].expected, "Got %d, expected %d.\n",
                tests[i].invoked, tests[i].expected);
    }

    ID3D12Device_Release(device);
}

void test_destruction_notifier_interfaces(void)
{
    D3D12_COMMAND_SIGNATURE_DESC command_signature_desc;
    D3D12_INDIRECT_ARGUMENT_DESC command_signature_arg;
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pipeline_desc;
    D3D12_DESCRIPTOR_HEAP_DESC descriptor_heap_desc;
    D3D12_ROOT_SIGNATURE_DESC root_signature_desc;
    ID3D12CommandAllocator *command_allocator;
    ID3D12CommandSignature *command_signature;
    ID3D12PipelineLibrary *pipeline_library;
    D3D12_QUERY_HEAP_DESC query_heap_desc;
    ID3D12DescriptorHeap *descriptor_heap;
    D3D12_COMMAND_QUEUE_DESC queue_desc;
    ID3D12RootSignature *root_signature;
    ID3DDestructionNotifier *notifier;
    D3D12_RESOURCE_DESC resource_desc;
    ID3D12CommandQueue *command_queue;
    ULONG refcount, expected_refcount;
    ID3D12CommandList *command_list;
    ID3D12PipelineState *pipeline;
    ID3D12QueryHeap *query_heap;
    D3D12_HEAP_DESC heap_desc;
    ID3D12Resource *resource;
    ID3D12Device1 *device1;
    ID3D12Device *device;
    ID3D12Fence *fence;
    ID3D12Heap *heap;
    unsigned int i;
    HRESULT hr;

    IUnknown** const tests[] =
    {
        (IUnknown**)&command_allocator,
        (IUnknown**)&command_list,
        (IUnknown**)&command_queue,
        (IUnknown**)&command_signature,
        (IUnknown**)&descriptor_heap,
        (IUnknown**)&device,
        (IUnknown**)&fence,
        (IUnknown**)&heap,
        (IUnknown**)&pipeline,
        (IUnknown**)&pipeline_library,
        (IUnknown**)&query_heap,
        (IUnknown**)&resource,
        (IUnknown**)&root_signature,
    };

    if (!(device = create_device()))
    {
        skip("Failed to create device.\n");
        return;
    }

    device1 = NULL;
    ID3D12Device_QueryInterface(device, &IID_ID3D12Device1, (void**)&device1);

    memset(&heap_desc, 0, sizeof(heap_desc));
    heap_desc.SizeInBytes = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
    heap_desc.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
    heap_desc.Properties.Type = D3D12_HEAP_TYPE_DEFAULT;
    heap_desc.Flags = D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS;

    hr = ID3D12Device_CreateHeap(device, &heap_desc, &IID_ID3D12Heap, (void**)&heap);
    ok(hr == S_OK, "Failed to create heap, hr %#x.\n", hr);

    memset(&resource_desc, 0, sizeof(resource_desc));
    resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    resource_desc.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
    resource_desc.Width = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
    resource_desc.Height = 1;
    resource_desc.DepthOrArraySize = 1;
    resource_desc.MipLevels = 1;
    resource_desc.SampleDesc.Count = 1;
    resource_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    hr = ID3D12Device_CreatePlacedResource(device, heap, 0, &resource_desc,
            D3D12_RESOURCE_STATE_COPY_DEST, NULL, &IID_ID3D12Resource, (void**)&resource);
    ok(hr == S_OK, "Failed to create resource, hr %#x.\n", hr);

    hr = ID3D12Device_CreateFence(device, 0, D3D12_FENCE_FLAG_NONE,
            &IID_ID3D12Fence, (void**)&fence);
    ok(hr == S_OK, "Failed to create fence, hr %#x.\n", hr);

    memset(&root_signature_desc, 0, sizeof(root_signature_desc));
    root_signature_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    hr = create_root_signature(device, &root_signature_desc, &root_signature);
    ok(hr == S_OK, "Failed to create root signature, hr %#x.\n", hr);

    memset(&query_heap_desc, 0, sizeof(query_heap_desc));
    query_heap_desc.Type = D3D12_QUERY_HEAP_TYPE_OCCLUSION;
    query_heap_desc.Count = 1024;

    hr = ID3D12Device_CreateQueryHeap(device, &query_heap_desc,
            &IID_ID3D12QueryHeap, (void**)&query_heap);
    ok(hr == S_OK, "Failed to create query heap, hr %#x.\n", hr);

    hr = ID3D12Device_CreateCommandAllocator(device, D3D12_COMMAND_LIST_TYPE_DIRECT,
            &IID_ID3D12CommandAllocator, (void**)&command_allocator);
    ok(hr == S_OK, "Failed to create command allocator, hr %#x.\n", hr);

    hr = ID3D12Device_CreateCommandList(device, 0, D3D12_COMMAND_LIST_TYPE_DIRECT,
            command_allocator, NULL, &IID_ID3D12CommandList, (void**)&command_list);
    ok(hr == S_OK, "Failed to create command list, hr %#x.\n", hr);

    memset(&queue_desc, 0, sizeof(queue_desc));
    queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    queue_desc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;

    hr = ID3D12Device_CreateCommandQueue(device, &queue_desc, &IID_ID3D12CommandQueue, (void**)&command_queue);
    ok(hr == S_OK, "Failed to create command queue, hr %#x.\n", hr);

    memset(&descriptor_heap_desc, 0, sizeof(descriptor_heap_desc));
    descriptor_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    descriptor_heap_desc.NumDescriptors = 1024;
    descriptor_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

    hr = ID3D12Device_CreateDescriptorHeap(device, &descriptor_heap_desc, &IID_ID3D12DescriptorHeap, (void**)&descriptor_heap);
    ok(hr == S_OK, "Failed to create command queue, hr %#x.\n", hr);

    pipeline_library = NULL;

    if (device1)
    {
        hr = ID3D12Device1_CreatePipelineLibrary(device1, NULL, 0, &IID_ID3D12PipelineLibrary, (void**)&pipeline_library);
        ok(hr == S_OK, "Failed to create pipeline library, hr %#x.\n", hr);
    }

    memset(&command_signature_arg, 0, sizeof(command_signature_arg));
    command_signature_arg.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW;

    memset(&command_signature_desc, 0, sizeof(command_signature_desc));
    command_signature_desc.NumArgumentDescs = 1;
    command_signature_desc.pArgumentDescs = &command_signature_arg;
    command_signature_desc.ByteStride = 16;

    hr = ID3D12Device_CreateCommandSignature(device, &command_signature_desc,
            NULL, &IID_ID3D12CommandSignature, (void**)&command_signature);
    ok(hr == S_OK, "Failed to create command signature, hr %#x.\n", hr);

    init_pipeline_state_desc(&pipeline_desc, root_signature,
            DXGI_FORMAT_R8G8B8A8_UNORM, NULL, NULL, NULL);
    hr = ID3D12Device_CreateGraphicsPipelineState(device, &pipeline_desc, &IID_ID3D12PipelineState, (void**)&pipeline);
    ok(hr == S_OK, "Failed to create pipeline, hr %#x.\n", hr);

    for (i = 0; i < ARRAY_SIZE(tests); i++)
    {
        IUnknown* iface = *tests[i];

        vkd3d_test_set_context("Test %u", i);

        if (!iface)
        {
            skip("Interface not available.\n");
            continue;
        }

        expected_refcount = get_refcount(iface) + 1u;

        notifier = NULL;
        hr = IUnknown_QueryInterface(iface, &IID_ID3DDestructionNotifier, (void**)&notifier);
        ok(hr == S_OK, "Failed to query ID3DDestructionNotifier, hr %#x.\n", hr);

        if (!notifier)
            continue;

        expected_refcount++;
        ID3DDestructionNotifier_AddRef(notifier);

        refcount = get_refcount(iface);
        ok(refcount == expected_refcount, "Expected object ref count to be %u, got %u.\n", expected_refcount, refcount);

        expected_refcount--;
        refcount = ID3DDestructionNotifier_Release(notifier);
        ok(refcount == expected_refcount, "Expected notifier ref count to be %u, got %u.\n", expected_refcount, refcount);

        refcount = get_refcount(iface);
        ok(refcount == expected_refcount, "Expected object ref count to be %u, got %u.\n", expected_refcount, refcount);

        expected_refcount--;
        ID3DDestructionNotifier_Release(notifier);

        refcount = get_refcount(iface);
        ok(refcount == expected_refcount, "Expected object ref count to be %u, got %u.\n", expected_refcount, refcount);
    }

    ID3D12PipelineState_Release(pipeline);
    ID3D12CommandSignature_Release(command_signature);

    if (pipeline_library)
        ID3D12PipelineLibrary_Release(pipeline_library);

    ID3D12DescriptorHeap_Release(descriptor_heap);
    ID3D12CommandQueue_Release(command_queue);
    ID3D12CommandList_Release(command_list);
    ID3D12CommandAllocator_Release(command_allocator);
    ID3D12QueryHeap_Release(query_heap);
    ID3D12RootSignature_Release(root_signature);
    ID3D12Fence_Release(fence);
    ID3D12Resource_Release(resource);
    ID3D12Heap_Release(heap);

    if (device1)
        ID3D12Device1_Release(device1);

    ID3D12Device_Release(device);
}

void test_sdk_configuration_creation(void)
{
    ID3D12SDKConfiguration *config, *config2;
    if (FAILED(pfn_D3D12GetInterface(&CLSID_D3D12SDKConfiguration, &IID_ID3D12SDKConfiguration, (void **)&config)))
    {
        skip("Failed to get SDK configuration interface.\n");
        return;
    }

    if (FAILED(pfn_D3D12GetInterface(&CLSID_D3D12SDKConfiguration, &IID_ID3D12SDKConfiguration, (void **)&config2)))
    {
        skip("Failed to get SDK configuration interface.\n");
        return;
    }

    /* D3D12GetInterface creates new pointers every time, which is rather surprising for a "GetInterface". */
    ok(config != config2, "Expected different pointers.\n");

    ok(ID3D12SDKConfiguration_Release(config) == 0, "Invalid refcount.\n");
    ok(ID3D12SDKConfiguration_Release(config2) == 0, "Invalid refcount.\n");
}

void test_device_factory_creation(void)
{
    /* The docs hints that this should work, but only in developer mode. It works anyway. */
    ID3D12DeviceFactory *factory, *factory2;
    if (FAILED(pfn_D3D12GetInterface(&CLSID_D3D12DeviceFactory, &IID_ID3D12DeviceFactory, (void **)&factory)))
    {
        skip("Failed to get device factory interface.\n");
        return;
    }

    if (FAILED(pfn_D3D12GetInterface(&CLSID_D3D12DeviceFactory, &IID_ID3D12DeviceFactory, (void **)&factory2)))
    {
        skip("Failed to get device factory interface.\n");
        return;
    }

    /* D3D12GetInterface creates new pointers every time, which is rather surprising for a "GetInterface". */
    ok(factory != factory2, "Expected different pointers.\n");

    ok(ID3D12DeviceFactory_Release(factory) == 0, "Invalid refcount.\n");
    ok(ID3D12DeviceFactory_Release(factory2) == 0, "Invalid refcount.\n");
}

#define SDK_VERSION 616

void test_sdk_configuration_set_sdk_path(void)
{
    ID3D12SDKConfiguration *config;
    HRESULT hr;

    if (FAILED(pfn_D3D12GetInterface(&CLSID_D3D12SDKConfiguration, &IID_ID3D12SDKConfiguration, (void **)&config)))
    {
        skip("Failed to get SDK configuration interface.\n");
        return;
    }

    /* Docs say that this is only allowed in developer mode, but that's not true.
     * It seems to just return S_OK always, even for bogus values, so *shrug* */
    hr = ID3D12SDKConfiguration_SetSDKVersion(config, 1234213432, "OMGIDONTCARE");
    ok(hr == S_OK, "Unexpected hr #%x.\n", hr);

    ok(ID3D12SDKConfiguration_Release(config) == 0, "Unexpected refcount.\n");
}

void test_sdk_configuration1(void)
{
    ID3D12SDKConfiguration1 *config;
    ID3D12DeviceFactory *factory;
    HRESULT hr;

    if (FAILED(pfn_D3D12GetInterface(&CLSID_D3D12SDKConfiguration, &IID_ID3D12SDKConfiguration1, (void **)&config)))
    {
        skip("Failed to get SDK configuration interface.\n");
        return;
    }

#ifdef _WIN32
    {
        MEMORY_BASIC_INFORMATION info;
        VirtualQuery(config->lpVtbl, &info, sizeof(info));
#if 0
        /* This will fail because we query for VKD3DDebugControl interface earlier, which forces d3d12core.dll to be loaded,
         * but commenting that out, it passes this test. */
        ok(GetModuleHandleA("d3d12core.dll") == NULL, "Expected d3d12core.dll to not be loaded yet.\n");
#endif
        ok(GetModuleHandleA("d3d12.dll") == (HMODULE)info.AllocationBase, "Expected ID3D12SDKConfiguration to come from d3d12.dll.\n");
    }
#endif

    /* No way to actually test this beyond it not crashing. */
    ID3D12SDKConfiguration1_FreeUnusedSDKs(config);

    /* This actually loads d3d12core.dll */
    hr = ID3D12SDKConfiguration1_CreateDeviceFactory(config, SDK_VERSION, "D3D12",
        &IID_ID3D12DeviceFactory, (void **)&factory);
    ok(SUCCEEDED(hr), "Failed to create device factory.\n");
    if (FAILED(hr))
    {
        ID3D12SDKConfiguration1_Release(config);
        return;
    }

#ifdef _WIN32
    {
        MEMORY_BASIC_INFORMATION info;
        VirtualQuery(factory->lpVtbl, &info, sizeof(info));
        ok(GetModuleHandleA("d3d12core.dll") != NULL, "Expected d3d12core.dll to be loaded now.\n");
        ok(GetModuleHandleA("d3d12core.dll") == (HMODULE)info.AllocationBase, "Expected ID3D12DeviceFactory to come from d3d12core.dll.\n");
    }
#endif

    ok(ID3D12DeviceFactory_Release(factory) == 0, "Unexpected refcount.\n");
    ok(ID3D12SDKConfiguration1_Release(config) == 0, "Unexpected refcount.\n");
}

void test_device_factory(void)
{
    D3D12_DEVICE_FACTORY_FLAGS flags;
    ID3D12SDKConfiguration1 *config;
    ID3D12DeviceFactory *factory;
    ID3D12Device *device1;
    ID3D12Device *device2;
    HRESULT hr;

    if (FAILED(pfn_D3D12GetInterface(&CLSID_D3D12SDKConfiguration, &IID_ID3D12SDKConfiguration1, (void **)&config)))
    {
        skip("Failed to get SDK configuration interface.\n");
        return;
    }

    hr = ID3D12SDKConfiguration1_CreateDeviceFactory(config, SDK_VERSION, "D3D12",
        &IID_ID3D12DeviceFactory, (void **)&factory);
    ok(SUCCEEDED(hr), "Failed to create device factory.\n");
    if (FAILED(hr))
    {
        ID3D12SDKConfiguration1_Release(config);
        return;
    }

    flags = ID3D12DeviceFactory_GetFlags(factory);
    ok(flags == D3D12_DEVICE_FACTORY_FLAG_NONE, "Unexpected flags #%x.\n", flags);
    ID3D12DeviceFactory_SetFlags(factory, D3D12_DEVICE_FACTORY_FLAG_DISALLOW_STORING_NEW_DEVICE_AS_SINGLETON);
    flags = ID3D12DeviceFactory_GetFlags(factory);
    ok(flags == D3D12_DEVICE_FACTORY_FLAG_DISALLOW_STORING_NEW_DEVICE_AS_SINGLETON, "Unexpected flags #%x.\n", flags);

    ID3D12DeviceFactory_ApplyToGlobalState(factory);

    ok(ID3D12DeviceFactory_Release(factory) == 0, "Unexpected refcount.\n");

    /* Flags does not affect global D3D12CreateDevice or global state. */
    device1 = create_device();
    device2 = create_device();
    ok(device1 && device2 && device1 == device2, "Unexpected device pointers %p, %p\n", device1, device2);
    if (device1)
        ID3D12Device_Release(device1);
    if (device2)
        ID3D12Device_Release(device2);

    hr = ID3D12SDKConfiguration1_CreateDeviceFactory(config, SDK_VERSION, "D3D12",
        &IID_ID3D12DeviceFactory, (void **)&factory);
    ok(SUCCEEDED(hr), "Failed to create device factory.\n");
    if (FAILED(hr))
    {
        ID3D12SDKConfiguration1_Release(config);
        return;
    }

    flags = ID3D12DeviceFactory_GetFlags(factory);
    ok(flags == D3D12_DEVICE_FACTORY_FLAG_NONE, "Unexpected flags #%x.\n", flags);
    /* This doesn't actually seem to work. Flags is not updated. */
    hr = ID3D12DeviceFactory_InitializeFromGlobalState(factory);
    ok(SUCCEEDED(hr), "Unexpected hr #%x.\n", hr);
    flags = ID3D12DeviceFactory_GetFlags(factory);
    ok(flags == D3D12_DEVICE_FACTORY_FLAG_NONE, "Unexpected flags #%x.\n", flags);

    {
        ID3D12Debug *debug;
        hr = ID3D12DeviceFactory_GetConfigurationInterface(factory,
            &CLSID_D3D12Debug, &IID_ID3D12Debug, (void **)&debug);
        todo ok(SUCCEEDED(hr), "Failed to get debug interface, hr #%x\n", hr);
        if (SUCCEEDED(hr))
        {
            ID3D12Debug_Release(debug);
            hr = ID3D12DeviceFactory_GetConfigurationInterface(factory,
                &CLSID_D3D12Debug, &IID_ID3D12Debug, NULL);
            ok(hr == S_FALSE, "Unexpected hr #%x.\n", hr);
        }
    }

    {
        ID3D12Tools *tools;
        hr = ID3D12DeviceFactory_GetConfigurationInterface(factory,
            &CLSID_D3D12Tools, &IID_ID3D12Tools, (void **)&tools);
        todo ok(SUCCEEDED(hr), "Failed to get tools interface, hr #%x\n", hr);
        if (SUCCEEDED(hr))
        {
            ID3D12Tools_Release(tools);
            hr = ID3D12DeviceFactory_GetConfigurationInterface(factory,
                &CLSID_D3D12Tools, &IID_ID3D12Tools, NULL);
            ok(hr == S_FALSE, "Unexpected hr #%x.\n", hr);
        }
    }

    {
        ID3D12DeviceRemovedExtendedDataSettings *dred;
        hr = ID3D12DeviceFactory_GetConfigurationInterface(factory,
            &CLSID_D3D12DeviceRemovedExtendedData, &IID_ID3D12DeviceRemovedExtendedDataSettings, (void **)&dred);
        /* We implement this in a stubbed form. */
        ok(SUCCEEDED(hr), "Failed to get dred interface, hr #%x\n", hr);
        if (SUCCEEDED(hr))
        {
            ID3D12DeviceRemovedExtendedDataSettings_Release(dred);
            /* S_FALSE is returned even if ExtendedData is not supported, as we expect from return_interface(). */
            hr = ID3D12DeviceFactory_GetConfigurationInterface(factory,
                &CLSID_D3D12DeviceRemovedExtendedData, &IID_ID3D12DeviceRemovedExtendedData, NULL);
            ok(hr == S_FALSE, "Unexpected hr #%x.\n", hr);
        }
    }

    ok(ID3D12DeviceFactory_Release(factory) == 0, "Unexpected refcount.\n");
    ok(ID3D12SDKConfiguration1_Release(config) == 0, "Unexpected refcount.\n");
}

static void check_create_devices(ID3D12DeviceFactory *factory, ID3D12Device *singleton_reference)
{
    UINT refcount1 = 0, refcount2 = 0;
    ID3D12Device *singleton1;
    ID3D12Device *singleton2;
    ID3D12Device *device1;
    ID3D12Device *device2;
    HRESULT hr;

    hr = ID3D12DeviceFactory_CreateDevice(factory, NULL, D3D_FEATURE_LEVEL_11_0, &IID_ID3D12Device, (void **)&device1);
    ok(SUCCEEDED(hr), "Failed to create device (hr #%x).\n", hr);
    if (FAILED(hr))
        device1 = NULL;
    hr = ID3D12DeviceFactory_CreateDevice(factory, NULL, D3D_FEATURE_LEVEL_11_0, &IID_ID3D12Device, (void **)&device2);
    ok(SUCCEEDED(hr), "Failed to create device (hr #%x).\n", hr);
    if (FAILED(hr))
        device2 = NULL;

    if (singleton_reference)
    {
        ok(device1 != device2, "Devices were not independent.\n");
        ok(device1 != singleton_reference, "Devices were not independent.\n");
        ok(device2 != singleton_reference, "Devices were not independent.\n");
    }
    else
    {
        ok(device1 != device2, "Devices were not independent.\n");

        singleton1 = create_device();
        singleton2 = create_device();
        ok(singleton1 && singleton2 && singleton1 == singleton2, "Expected singletons.\n");
        ok(singleton1 != device1, "Singleton should never match the device factory.\n");
        ok(singleton1 != device2, "Singleton should never match the device factory.\n");
        if (singleton1)
            ID3D12Device_Release(singleton1);
        if (singleton2)
            ID3D12Device_Release(singleton2);
    }

    if (device1)
        refcount1 = ID3D12Device_Release(device1);
    if (device2)
        refcount2 = ID3D12Device_Release(device2);

    ok(refcount1 == 0, "Unexpected refcount.\n");
    ok(refcount2 == 0, "Unexpected refcount.\n");
}

void test_device_factory_create_device(void)
{
    ID3D12SDKConfiguration1 *config;
    ID3D12DeviceFactory *factory;
    ID3D12Device *device = NULL;
    HRESULT hr;

    if (FAILED(pfn_D3D12GetInterface(&CLSID_D3D12SDKConfiguration, &IID_ID3D12SDKConfiguration1, (void **)&config)))
    {
        skip("Failed to get SDK configuration interface.\n");
        return;
    }

    hr = ID3D12SDKConfiguration1_CreateDeviceFactory(config, SDK_VERSION, "D3D12",
        &IID_ID3D12DeviceFactory, (void **)&factory);
    ok(SUCCEEDED(hr), "Failed to create device factory.\n");
    if (FAILED(hr))
    {
        ID3D12SDKConfiguration1_Release(config);
        return;
    }

    /* Base behavior. Create factory devices, then create singletons. No matter the flag, we always create independent devices.
     * The basic gist is that drivers may fallback to singletons if independent devices are unsupported,
     * but CreateDevice should always be independent devices. */
    ID3D12DeviceFactory_SetFlags(factory, D3D12_DEVICE_FACTORY_FLAG_NONE);
    check_create_devices(factory, NULL);
    ID3D12DeviceFactory_SetFlags(factory, D3D12_DEVICE_FACTORY_FLAG_ALLOW_RETURNING_EXISTING_DEVICE);
    check_create_devices(factory, NULL);
    /* We have no good way to distinguish incompatible vs compatible at the moment. */
    ID3D12DeviceFactory_SetFlags(factory, D3D12_DEVICE_FACTORY_FLAG_ALLOW_RETURNING_INCOMPATIBLE_EXISTING_DEVICE);
    check_create_devices(factory, NULL);
    ID3D12DeviceFactory_SetFlags(factory, D3D12_DEVICE_FACTORY_FLAG_DISALLOW_STORING_NEW_DEVICE_AS_SINGLETON);
    check_create_devices(factory, NULL);

    /* Create singleton first, then factory devices. */
    device = create_device();
    if (device)
    {
        ID3D12Device_AddRef(device);
        ok(ID3D12Device_Release(device) == 1, "Unexpected refcount.\n");
    }
    ok(device, "Failed to create device.\n");
    ID3D12DeviceFactory_SetFlags(factory, D3D12_DEVICE_FACTORY_FLAG_NONE);
    check_create_devices(factory, device);
    /* Even with an existing singleton, we don't get the singleton back. */
    ID3D12DeviceFactory_SetFlags(factory, D3D12_DEVICE_FACTORY_FLAG_ALLOW_RETURNING_EXISTING_DEVICE);
    check_create_devices(factory, device);
    /* We have no good way to distinguish incompatible vs compatible at the moment. */
    ID3D12DeviceFactory_SetFlags(factory, D3D12_DEVICE_FACTORY_FLAG_ALLOW_RETURNING_INCOMPATIBLE_EXISTING_DEVICE);
    check_create_devices(factory, device);
    ID3D12DeviceFactory_SetFlags(factory, D3D12_DEVICE_FACTORY_FLAG_DISALLOW_STORING_NEW_DEVICE_AS_SINGLETON);
    check_create_devices(factory, device);
    if (device)
        ok(ID3D12Device_Release(device) == 0, "Unexpected refcount.\n");

    ID3D12DeviceFactory_Release(factory);
    ID3D12SDKConfiguration1_Release(config);
}

static void test_root_signature_serialization(ID3D12DeviceConfiguration1 *config)
{
    const D3D12_VERSIONED_ROOT_SIGNATURE_DESC *parsed_desc;
    ID3D12VersionedRootSignatureDeserializer *deserializer;
    D3D12_VERSIONED_ROOT_SIGNATURE_DESC desc;
    D3D12_ROOT_PARAMETER1 param;
    ID3D10Blob *blob;
    HRESULT hr;

    memset(&desc, 0, sizeof(desc));
    desc.Version = D3D_ROOT_SIGNATURE_VERSION_1_2;
    desc.Desc_1_2.NumParameters = 1;
    desc.Desc_1_2.pParameters = &param;

    param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    param.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    param.Descriptor.Flags = D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC;
    param.Descriptor.RegisterSpace = 1;
    param.Descriptor.ShaderRegister = 2;

    hr = ID3D12DeviceConfiguration1_SerializeVersionedRootSignature(config, &desc, &blob, NULL);
    ok(SUCCEEDED(hr), "Failed to serialize versioned root signature, hr #%x.\n", hr);
    if (FAILED(hr))
        return;

    hr = ID3D12DeviceConfiguration1_CreateVersionedRootSignatureDeserializer(config, ID3D10Blob_GetBufferPointer(blob),
        ID3D10Blob_GetBufferSize(blob), &IID_ID3D12VersionedRootSignatureDeserializer, (void **)&deserializer);
    ID3D10Blob_Release(blob);
    if (FAILED(hr))
        return;

    /* Just sanity check that the command went through. Don't bother being completely exhaustive. */
    parsed_desc = ID3D12VersionedRootSignatureDeserializer_GetUnconvertedRootSignatureDesc(deserializer);
    ok(parsed_desc, "Expected non-null.\n");
    if (parsed_desc)
    {
        ok(parsed_desc->Version == D3D_ROOT_SIGNATURE_VERSION_1_2, "Bad root signature version.\n");
        ok(parsed_desc->Desc_1_2.NumParameters == 1, "Unexpected num parameters.\n");
        if (parsed_desc->Desc_1_2.pParameters)
            ok(parsed_desc->Desc_1_2.pParameters[0].ParameterType == D3D12_ROOT_PARAMETER_TYPE_CBV, "Unexpected type.\n");
    }
    ID3D12VersionedRootSignatureDeserializer_Release(deserializer);
}

static void test_root_signature_subobject_serialization(ID3D12DeviceConfiguration1 *config)
{
    ID3D12VersionedRootSignatureDeserializer *deserializer;
    const D3D12_VERSIONED_ROOT_SIGNATURE_DESC *parsed_desc;
    HRESULT hr;

#include "shaders/rt/headers/embedded_root_signature_subobject_rt.h"

    hr = ID3D12DeviceConfiguration1_CreateVersionedRootSignatureDeserializerFromSubobjectInLibrary(config,
        embedded_root_signature_subobject_rt_code_dxil, sizeof(embedded_root_signature_subobject_rt_code_dxil),
        u"grs", &IID_ID3D12VersionedRootSignatureDeserializer, (void **)&deserializer);
    ok(SUCCEEDED(hr), "Failed to create from subobject in library.\n");
    if (FAILED(hr))
        return;

    parsed_desc = ID3D12VersionedRootSignatureDeserializer_GetUnconvertedRootSignatureDesc(deserializer);
    ok(parsed_desc, "Expected non-null.\n");

    if (parsed_desc)
    {
        ok(parsed_desc->Version == D3D_ROOT_SIGNATURE_VERSION_1_1, "Bad root signature version.\n");
        ok(parsed_desc->Desc_1_1.NumParameters == 1, "Unexpected num parameters.\n");
        ok(parsed_desc->Desc_1_1.NumStaticSamplers == 0, "Unexpected num samplers.\n");
        if (parsed_desc->Desc_1_1.pParameters)
        {
            ok(parsed_desc->Desc_1_1.pParameters[0].ParameterType == D3D12_ROOT_PARAMETER_TYPE_UAV, "Unexpected type.\n");
            ok(parsed_desc->Desc_1_1.pParameters[0].Descriptor.RegisterSpace == 0, "Unexpected space.\n");
            ok(parsed_desc->Desc_1_1.pParameters[0].Descriptor.ShaderRegister == 0, "Unexpected register.\n");
        }
    }

    ID3D12VersionedRootSignatureDeserializer_Release(deserializer);
}

void test_device_configuration(void)
{
    D3D12_DEVICE_CONFIGURATION_DESC config_desc;
    ID3D12DeviceConfiguration1 *device_config1;
    ID3D12DeviceConfiguration1 *device_config2;
    unsigned int i;
    HRESULT hr;

    for (i = 0; i < 2; i++)
    {
        IUnknown *iface;

        if (i == 0)
        {
            vkd3d_test_set_context("ID3D12Device");
            iface = (IUnknown *)create_device();
        }
        else
        {
            ID3D12SDKConfiguration1 *config;
            vkd3d_test_set_context("ID3D12DeviceFactory");

            if (FAILED(pfn_D3D12GetInterface(&CLSID_D3D12SDKConfiguration, &IID_ID3D12SDKConfiguration1, (void **)&config)))
            {
                skip("Failed to get SDK configuration interface.\n");
                continue;
            }

            hr = ID3D12SDKConfiguration1_CreateDeviceFactory(config, SDK_VERSION, "D3D12",
                &IID_IUnknown, (void **)&iface);
            ok(SUCCEEDED(hr), "Failed to create device factory.\n");
            ID3D12SDKConfiguration1_Release(config);
            if (FAILED(hr))
                continue;
        }

        ok(iface, "Failed to create device.\n");

        hr = IUnknown_QueryInterface(iface, &IID_ID3D12DeviceConfiguration1, (void **)&device_config1);
        ok(SUCCEEDED(hr), "Failed to query ID3D12DeviceConfiguration1.\n");
        hr = IUnknown_QueryInterface(iface, &IID_ID3D12DeviceConfiguration1, (void **)&device_config2);
        ok(SUCCEEDED(hr), "Failed to query ID3D12DeviceConfiguration1.\n");
        ok(IUnknown_Release(iface) == 2, "Unexpected refcount.\n");
        if (FAILED(hr))
            continue;

        test_root_signature_serialization(device_config1);
        test_root_signature_subobject_serialization(device_config1);

        ok(device_config1 == device_config2, "Unexpected device config pointers.\n");
        config_desc = ID3D12DeviceConfiguration1_GetDesc(device_config1);
        ok(config_desc.Flags == D3D12_DEVICE_FLAG_NONE ||
            config_desc.Flags == D3D12_DEVICE_FLAG_DEBUG_LAYER_ENABLED, "Unexpected flags.\n");
        ok(config_desc.NumEnabledExperimentalFeatures == 0, "Unexpected enabled experimental features.\n");
        ok(config_desc.SDKVersion >= SDK_VERSION, "Unexpected SDK version.\n");
        ok(config_desc.GpuBasedValidationFlags == 0, "Unexpected GPU based validation flags.\n");
        ok(ID3D12DeviceConfiguration1_Release(device_config1) == 1, "Unexpected refcount.\n");
        ok(ID3D12DeviceConfiguration1_Release(device_config2) == 0, "Unexpected refcount.\n");
    }
    vkd3d_test_set_context(NULL);
}