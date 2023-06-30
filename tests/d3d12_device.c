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
    ID3D12Device *device;
    ULONG refcount;
    unsigned int i;
    HRESULT hr;

    if (!(device = create_device()))
    {
        skip("Failed to create device.\n");
        return;
    }

    memset(&format_support, 0, sizeof(format_support));
    hr = ID3D12Device_CheckFeatureSupport(device, D3D12_FEATURE_FORMAT_SUPPORT,
            &format_support, sizeof(format_support));
    todo ok(hr == S_OK, "Got unexpected hr %#x.\n", hr);
    todo ok(format_support.Support1 == D3D12_FORMAT_SUPPORT1_BUFFER,
            "Got unexpected support1 %#x.\n", format_support.Support1);
    ok(!format_support.Support2 || format_support.Support2 == D3D12_FORMAT_SUPPORT2_TILED,
            "Got unexpected support2 %#x.\n", format_support.Support2);

    for (i = 0; i < ARRAY_SIZE(depth_stencil_formats); ++i)
    {
        memset(&format_support, 0, sizeof(format_support));
        format_support.Format = depth_stencil_formats[i];
        hr = ID3D12Device_CheckFeatureSupport(device, D3D12_FEATURE_FORMAT_SUPPORT,
                &format_support, sizeof(format_support));
        ok(hr == S_OK, "Got unexpected hr %#x.\n", hr);
    }

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
