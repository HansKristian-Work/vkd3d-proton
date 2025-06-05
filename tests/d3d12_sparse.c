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

#define TILE_SIZE 65536

static uint32_t compute_tile_count(uint32_t resource_size, uint32_t mip, uint32_t tile_size)
{
    uint32_t mip_size = max(resource_size >> mip, 1u);
    return (mip_size / tile_size) + (mip_size % tile_size ? 1 : 0);
}

void test_get_resource_tiling(void)
{
    D3D12_FEATURE_DATA_D3D12_OPTIONS options;
    D3D12_SUBRESOURCE_TILING tilings_alt[64];
    D3D12_PACKED_MIP_INFO packed_mip_info;
    D3D12_SUBRESOURCE_TILING tilings[64];
    UINT num_resource_tiles, num_tilings;
    D3D12_RESOURCE_DESC resource_desc;
    struct test_context_desc desc;
    struct test_context context;
    D3D12_TILE_SHAPE tile_shape;
    ID3D12Resource *resource;
    unsigned int i, j;
    HRESULT hr;

    static const struct
    {
        D3D12_RESOURCE_DIMENSION dim;
        DXGI_FORMAT format;
        UINT width;
        UINT height;
        UINT depth_or_array_layers;
        UINT mip_levels;
        UINT expected_tile_count;
        UINT expected_tiling_count;
        UINT expected_standard_mips;
        UINT tile_shape_w;
        UINT tile_shape_h;
        UINT tile_shape_d;
        D3D12_TILED_RESOURCES_TIER min_tier;
        bool todo_radv;
    }
    tests[] =
    {
        /* Test buffers */
        { D3D12_RESOURCE_DIMENSION_BUFFER,    DXGI_FORMAT_UNKNOWN,            1024,    1,  1,  1,  1,  1,  0, 65536,   1,   1, D3D12_TILED_RESOURCES_TIER_1 },
        { D3D12_RESOURCE_DIMENSION_BUFFER,    DXGI_FORMAT_UNKNOWN,        16*65536,    1,  1,  1, 16,  1,  0, 65536,   1,   1, D3D12_TILED_RESOURCES_TIER_1 },
        /* Test small resource behavior */
        { D3D12_RESOURCE_DIMENSION_TEXTURE2D, DXGI_FORMAT_R8_UNORM,              1,    1,  1,  1,  1,  1,  0,   256, 256,   1, D3D12_TILED_RESOURCES_TIER_1 },
        { D3D12_RESOURCE_DIMENSION_TEXTURE2D, DXGI_FORMAT_R8_UNORM,              2,    2,  1,  2,  1,  2,  0,   256, 256,   1, D3D12_TILED_RESOURCES_TIER_1 },
        { D3D12_RESOURCE_DIMENSION_TEXTURE2D, DXGI_FORMAT_R8_UNORM,              4,    4,  1,  3,  1,  3,  0,   256, 256,   1, D3D12_TILED_RESOURCES_TIER_1 },
        { D3D12_RESOURCE_DIMENSION_TEXTURE2D, DXGI_FORMAT_R8_UNORM,              8,    8,  1,  4,  1,  4,  0,   256, 256,   1, D3D12_TILED_RESOURCES_TIER_1 },
        { D3D12_RESOURCE_DIMENSION_TEXTURE2D, DXGI_FORMAT_R8_UNORM,             16,   16,  1,  5,  1,  5,  0,   256, 256,   1, D3D12_TILED_RESOURCES_TIER_1 },
        { D3D12_RESOURCE_DIMENSION_TEXTURE2D, DXGI_FORMAT_R8_UNORM,             32,   32,  1,  6,  1,  6,  0,   256, 256,   1, D3D12_TILED_RESOURCES_TIER_1 },
        { D3D12_RESOURCE_DIMENSION_TEXTURE2D, DXGI_FORMAT_R8_UNORM,             64,   64,  1,  7,  1,  7,  0,   256, 256,   1, D3D12_TILED_RESOURCES_TIER_1 },
        { D3D12_RESOURCE_DIMENSION_TEXTURE2D, DXGI_FORMAT_R8_UNORM,            128,  128,  1,  8,  1,  8,  0,   256, 256,   1, D3D12_TILED_RESOURCES_TIER_1 },
        { D3D12_RESOURCE_DIMENSION_TEXTURE2D, DXGI_FORMAT_R8_UNORM,            256,  256,  1,  9,  2,  9,  1,   256, 256,   1, D3D12_TILED_RESOURCES_TIER_1 },
        /* Test various image formats */
        { D3D12_RESOURCE_DIMENSION_TEXTURE2D, DXGI_FORMAT_R8_UNORM,	           512,  512,  1,  1,  4,  1,  1,   256, 256,   1, D3D12_TILED_RESOURCES_TIER_1 },
        { D3D12_RESOURCE_DIMENSION_TEXTURE2D, DXGI_FORMAT_R8G8_UNORM,          512,  512,  1,  1,  8,  1,  1,   256, 128,   1, D3D12_TILED_RESOURCES_TIER_1 },
        { D3D12_RESOURCE_DIMENSION_TEXTURE2D, DXGI_FORMAT_R8G8B8A8_UNORM,      512,  512,  1,  1, 16,  1,  1,   128, 128,   1, D3D12_TILED_RESOURCES_TIER_1 },
        { D3D12_RESOURCE_DIMENSION_TEXTURE2D, DXGI_FORMAT_R16G16B16A16_UNORM,  512,  512,  1,  1, 32,  1,  1,   128,  64,   1, D3D12_TILED_RESOURCES_TIER_1 },
        { D3D12_RESOURCE_DIMENSION_TEXTURE2D, DXGI_FORMAT_R32G32B32A32_FLOAT,  512,  512,  1,  1, 64,  1,  1,    64,  64,   1, D3D12_TILED_RESOURCES_TIER_1 },
        { D3D12_RESOURCE_DIMENSION_TEXTURE2D, DXGI_FORMAT_D16_UNORM,           512,  512,  1,  1,  8,  1,  1,   256, 128,   1, D3D12_TILED_RESOURCES_TIER_1, true },
        { D3D12_RESOURCE_DIMENSION_TEXTURE2D, DXGI_FORMAT_D32_FLOAT,           512,  512,  1,  1, 16,  1,  1,   128, 128,   1, D3D12_TILED_RESOURCES_TIER_1, true },

        /* Test rectangular textures */
        { D3D12_RESOURCE_DIMENSION_TEXTURE2D, DXGI_FORMAT_R8G8B8A8_UNORM,     1024,  256,  1,  1, 16,  1,  1,   128, 128,   1, D3D12_TILED_RESOURCES_TIER_1 },
        { D3D12_RESOURCE_DIMENSION_TEXTURE2D, DXGI_FORMAT_R8G8B8A8_UNORM,      256, 1024,  1,  1, 16,  1,  1,   128, 128,   1, D3D12_TILED_RESOURCES_TIER_1 },
        { D3D12_RESOURCE_DIMENSION_TEXTURE2D, DXGI_FORMAT_R8G8B8A8_UNORM,      192,  128,  1,  1,  2,  1,  1,   128, 128,   1, D3D12_TILED_RESOURCES_TIER_2 },
        { D3D12_RESOURCE_DIMENSION_TEXTURE2D, DXGI_FORMAT_R8G8B8A8_UNORM,      128,  192,  1,  1,  2,  1,  1,   128, 128,   1, D3D12_TILED_RESOURCES_TIER_2 },
        { D3D12_RESOURCE_DIMENSION_TEXTURE2D, DXGI_FORMAT_R8G8B8A8_UNORM,      320,  192,  1,  1,  6,  1,  1,   128, 128,   1, D3D12_TILED_RESOURCES_TIER_2 },
        /* Test array layers and packed mip levels */
        { D3D12_RESOURCE_DIMENSION_TEXTURE2D, DXGI_FORMAT_R8G8B8A8_UNORM,      128,  128, 16,  1, 16, 16,  1,   128, 128,   1, D3D12_TILED_RESOURCES_TIER_1 },
        { D3D12_RESOURCE_DIMENSION_TEXTURE2D, DXGI_FORMAT_R8G8B8A8_UNORM,      128,  128,  1,  8,  1,  8,  1,   128, 128,   1, D3D12_TILED_RESOURCES_TIER_1 },
        { D3D12_RESOURCE_DIMENSION_TEXTURE2D, DXGI_FORMAT_R8G8B8A8_UNORM,      512,  512,  1, 10, 21, 10,  3,   128, 128,   1, D3D12_TILED_RESOURCES_TIER_1 },
        { D3D12_RESOURCE_DIMENSION_TEXTURE2D, DXGI_FORMAT_R8G8B8A8_UNORM,      512,  512,  4,  3, 84, 12,  3,   128, 128,   1, D3D12_TILED_RESOURCES_TIER_1 },
        { D3D12_RESOURCE_DIMENSION_TEXTURE2D, DXGI_FORMAT_R8G8B8A8_UNORM,       64,   64,  1,  1,  1,  1,  0,   128, 128,   1, D3D12_TILED_RESOURCES_TIER_1 },
        /* Test 3D textures */
        { D3D12_RESOURCE_DIMENSION_TEXTURE3D, DXGI_FORMAT_R8_UNORM,             64,   64, 64,  1,  4,  1,  1,    64,  32,  32, D3D12_TILED_RESOURCES_TIER_3 },
        { D3D12_RESOURCE_DIMENSION_TEXTURE3D, DXGI_FORMAT_R8G8_UNORM,           64,   64, 64,  1,  8,  1,  1,    32,  32,  32, D3D12_TILED_RESOURCES_TIER_3 },
        { D3D12_RESOURCE_DIMENSION_TEXTURE3D, DXGI_FORMAT_R8G8B8A8_UNORM,       64,   64, 64,  1, 16,  1,  1,    32,  32,  16, D3D12_TILED_RESOURCES_TIER_3 },
        { D3D12_RESOURCE_DIMENSION_TEXTURE3D, DXGI_FORMAT_R32G32B32A32_FLOAT,   64,   64, 64,  3, 73,  3,  3,    16,  16,  16, D3D12_TILED_RESOURCES_TIER_3 },

        /* Basic BC configurations. */
        { D3D12_RESOURCE_DIMENSION_TEXTURE2D, DXGI_FORMAT_BC1_UNORM,           512, 512,  1,  1,  2,  1,  1,   512, 256,   1, D3D12_TILED_RESOURCES_TIER_1 },
        { D3D12_RESOURCE_DIMENSION_TEXTURE2D, DXGI_FORMAT_BC2_UNORM,           512, 512,  1,  1,  4,  1,  1,   256, 256,   1, D3D12_TILED_RESOURCES_TIER_1 },
        { D3D12_RESOURCE_DIMENSION_TEXTURE2D, DXGI_FORMAT_BC3_UNORM,           512, 512,  1,  1,  4,  1,  1,   256, 256,   1, D3D12_TILED_RESOURCES_TIER_1 },
        { D3D12_RESOURCE_DIMENSION_TEXTURE2D, DXGI_FORMAT_BC4_UNORM,           512, 512,  1,  1,  2,  1,  1,   512, 256,   1, D3D12_TILED_RESOURCES_TIER_1 },
        { D3D12_RESOURCE_DIMENSION_TEXTURE2D, DXGI_FORMAT_BC5_UNORM,           512, 512,  1,  1,  4,  1,  1,   256, 256,   1, D3D12_TILED_RESOURCES_TIER_1 },
        { D3D12_RESOURCE_DIMENSION_TEXTURE2D, DXGI_FORMAT_BC6H_UF16,           512, 512,  1,  1,  4,  1,  1,   256, 256,   1, D3D12_TILED_RESOURCES_TIER_1 },
        { D3D12_RESOURCE_DIMENSION_TEXTURE2D, DXGI_FORMAT_BC6H_SF16,           512, 512,  1,  1,  4,  1,  1,   256, 256,   1, D3D12_TILED_RESOURCES_TIER_1 },
        { D3D12_RESOURCE_DIMENSION_TEXTURE2D, DXGI_FORMAT_BC7_UNORM,           512, 512,  1,  1,  4,  1,  1,   256, 256,   1, D3D12_TILED_RESOURCES_TIER_1 },

        /* Basic mipmapping with obvious tiling layouts. */
        { D3D12_RESOURCE_DIMENSION_TEXTURE2D, DXGI_FORMAT_BC1_UNORM,           512,  256,  1,  10,  2, 10,  1,   512, 256,   1, D3D12_TILED_RESOURCES_TIER_1 },
        { D3D12_RESOURCE_DIMENSION_TEXTURE2D, DXGI_FORMAT_BC1_UNORM,          1024,  512,  1,  10,  6, 10,  2,   512, 256,   1, D3D12_TILED_RESOURCES_TIER_1 },
        { D3D12_RESOURCE_DIMENSION_TEXTURE2D, DXGI_FORMAT_BC1_UNORM,          2048, 1024,  1,  10, 22, 10,  3,   512, 256,   1, D3D12_TILED_RESOURCES_TIER_1 },
        { D3D12_RESOURCE_DIMENSION_TEXTURE2D, DXGI_FORMAT_BC7_UNORM,           256,  256,  1,   9,  2,  9,  1,   256, 256,   1, D3D12_TILED_RESOURCES_TIER_1 },
        { D3D12_RESOURCE_DIMENSION_TEXTURE2D, DXGI_FORMAT_BC7_UNORM,           512,  512,  1,   9,  6,  9,  2,   256, 256,   1, D3D12_TILED_RESOURCES_TIER_1 },
        { D3D12_RESOURCE_DIMENSION_TEXTURE2D, DXGI_FORMAT_BC7_UNORM,          1024, 1024,  1,   9, 22,  9,  3,   256, 256,   1, D3D12_TILED_RESOURCES_TIER_1 },

        /* Wide shapes. On AMD, we keep observing standard mips even when the smallest dimension dips below the tile size.
         * This is not the case on NV however. */
        { D3D12_RESOURCE_DIMENSION_TEXTURE2D, DXGI_FORMAT_BC1_UNORM,          1024,  256,  1, 10,  3, 10,  1,   512, 256,   1, D3D12_TILED_RESOURCES_TIER_1 },
        { D3D12_RESOURCE_DIMENSION_TEXTURE2D, DXGI_FORMAT_BC1_UNORM,          2048,  256,  1, 10,  6, 10,  1,   512, 256,   1, D3D12_TILED_RESOURCES_TIER_1 },
        { D3D12_RESOURCE_DIMENSION_TEXTURE2D, DXGI_FORMAT_BC1_UNORM,          4096,  256,  1, 10, 11, 10,  1,   512, 256,   1, D3D12_TILED_RESOURCES_TIER_1 },
        { D3D12_RESOURCE_DIMENSION_TEXTURE2D, DXGI_FORMAT_BC7_UNORM,           512,  256,  1,  9,  3,  9,  1,   256, 256,   1, D3D12_TILED_RESOURCES_TIER_1 },
        { D3D12_RESOURCE_DIMENSION_TEXTURE2D, DXGI_FORMAT_BC7_UNORM,          1024,  256,  1,  9,  6,  9,  1,   256, 256,   1, D3D12_TILED_RESOURCES_TIER_1 },
        { D3D12_RESOURCE_DIMENSION_TEXTURE2D, DXGI_FORMAT_BC7_UNORM,          2048,  256,  1,  9, 11,  9,  1,   256, 256,   1, D3D12_TILED_RESOURCES_TIER_1 },

        /* Tall shapes. Similar to wide tests. */
        { D3D12_RESOURCE_DIMENSION_TEXTURE2D, DXGI_FORMAT_BC1_UNORM,           512,  512,  1, 10,  3, 10,  1,   512, 256,   1, D3D12_TILED_RESOURCES_TIER_1 },
        { D3D12_RESOURCE_DIMENSION_TEXTURE2D, DXGI_FORMAT_BC1_UNORM,           512, 1024,  1, 10,  6, 10,  1,   512, 256,   1, D3D12_TILED_RESOURCES_TIER_1 },
        { D3D12_RESOURCE_DIMENSION_TEXTURE2D, DXGI_FORMAT_BC1_UNORM,           512, 2048,  1, 10, 11, 10,  1,   512, 256,   1, D3D12_TILED_RESOURCES_TIER_1 },
        { D3D12_RESOURCE_DIMENSION_TEXTURE2D, DXGI_FORMAT_BC7_UNORM,           256,  512,  1,  9,  3,  9,  1,   256, 256,   1, D3D12_TILED_RESOURCES_TIER_1 },
        { D3D12_RESOURCE_DIMENSION_TEXTURE2D, DXGI_FORMAT_BC7_UNORM,           256, 1024,  1,  9,  6,  9,  1,   256, 256,   1, D3D12_TILED_RESOURCES_TIER_1 },
        { D3D12_RESOURCE_DIMENSION_TEXTURE2D, DXGI_FORMAT_BC7_UNORM,           256, 2048,  1,  9, 11,  9,  1,   256, 256,   1, D3D12_TILED_RESOURCES_TIER_1 },

        /* Array images with full mip chain; now allowed with tier 4 */
        { D3D12_RESOURCE_DIMENSION_TEXTURE2D, DXGI_FORMAT_R8G8B8A8_UNORM,      512,  512,  4, 10, 88, 40,  3,   128, 128,   1, D3D12_TILED_RESOURCES_TIER_4 },
    };

    memset(&desc, 0, sizeof(desc));
    desc.rt_width = 640;
    desc.rt_height = 480;
    desc.rt_format = DXGI_FORMAT_R8G8B8A8_UNORM;
    if (!init_test_context(&context, &desc))
        return;

    hr = ID3D12Device_CheckFeatureSupport(context.device, D3D12_FEATURE_D3D12_OPTIONS, &options, sizeof(options));
    ok(hr == S_OK, "Failed to check feature support, hr %#x.\n", hr);

    if (!options.TiledResourcesTier)
    {
        skip("Tiled resources not supported by device.\n");
        destroy_test_context(&context);
        return;
    }

    /* Test behaviour with various parameter combinations */
    resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    resource_desc.Alignment = 0;
    resource_desc.Width = 512;
    resource_desc.Height = 512;
    resource_desc.DepthOrArraySize = 1;
    resource_desc.MipLevels = 10;
    resource_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    resource_desc.SampleDesc.Count = 1;
    resource_desc.SampleDesc.Quality = 0;
    resource_desc.Layout = D3D12_TEXTURE_LAYOUT_64KB_UNDEFINED_SWIZZLE;
    resource_desc.Flags = D3D12_RESOURCE_FLAG_NONE;

    hr = ID3D12Device_CreateReservedResource(context.device, &resource_desc,
        D3D12_RESOURCE_STATE_GENERIC_READ, NULL, &IID_ID3D12Resource, (void **)&resource);
    ok(hr == S_OK, "Failed to create reserved resource, hr %#x.\n", hr);

    /* This is nonsense, but it doesn't crash or generate errors. */
    ID3D12Device_GetResourceTiling(context.device, resource, NULL, NULL, NULL, NULL, 0, NULL);

    /* If num_tilings is NULL, tilings_alt is ignored. */
    memset(tilings, 0, sizeof(tilings));
    memset(tilings_alt, 0, sizeof(tilings_alt));
    ID3D12Device_GetResourceTiling(context.device, resource, NULL, NULL, NULL, NULL, 0, tilings_alt);
    ok(memcmp(tilings, tilings_alt, sizeof(tilings_alt)) == 0, "Mismatch.\n");

    num_tilings = 0;
    ID3D12Device_GetResourceTiling(context.device, resource, NULL, NULL, NULL, &num_tilings, 0, NULL);
    ok(num_tilings == 0, "Unexpected tiling count %u.\n", num_tilings);

    num_tilings = ARRAY_SIZE(tilings);
    ID3D12Device_GetResourceTiling(context.device, resource, NULL, NULL, NULL, &num_tilings, 10, tilings);
    ok(num_tilings == 0, "Unexpected tiling count %u.\n", num_tilings);

    num_tilings = ARRAY_SIZE(tilings);
    ID3D12Device_GetResourceTiling(context.device, resource, NULL, NULL, NULL, &num_tilings, 2, tilings);
    ok(num_tilings == 8, "Unexpected tiling count %u.\n", num_tilings);
    ok(tilings[0].StartTileIndexInOverallResource == 20, "Unexpected start tile index %u.\n", tilings[0].StartTileIndexInOverallResource);

    num_tilings = 1;
    memset(&tilings, 0xaa, sizeof(tilings));
    ID3D12Device_GetResourceTiling(context.device, resource, NULL, NULL, NULL, &num_tilings, 2, tilings);
    ok(num_tilings == 1, "Unexpected tiling count %u.\n", num_tilings);
    ok(tilings[0].StartTileIndexInOverallResource == 20, "Unexpected start tile index %u.\n", tilings[0].StartTileIndexInOverallResource);
    ok(tilings[1].StartTileIndexInOverallResource == 0xaaaaaaaa, "Tiling array got modified.\n");

    ID3D12Resource_Release(resource);

    /* Test actual tiling properties */
    for (i = 0; i < ARRAY_SIZE(tests); i++)
    {
        unsigned int tile_index = 0;
        vkd3d_test_set_context("test %u", i);

        if (tests[i].min_tier > options.TiledResourcesTier)
        {
            skip("Tiled resources tier %u not supported.\n", tests[i].min_tier);
            continue;
        }

        memset(&packed_mip_info, 0xaa, sizeof(packed_mip_info));
        memset(&tile_shape, 0xaa, sizeof(tile_shape));
        memset(&tilings, 0xaa, sizeof(tilings));

        num_resource_tiles = 0xdeadbeef;
        num_tilings = ARRAY_SIZE(tilings);

        resource_desc.Dimension = tests[i].dim;
        resource_desc.Alignment = 0;
        resource_desc.Width = tests[i].width;
        resource_desc.Height = tests[i].height;
        resource_desc.DepthOrArraySize = tests[i].depth_or_array_layers;
        resource_desc.MipLevels = tests[i].mip_levels;
        resource_desc.Format = tests[i].format;
        resource_desc.SampleDesc.Count = 1;
        resource_desc.SampleDesc.Quality = 0;
        resource_desc.Layout = D3D12_TEXTURE_LAYOUT_64KB_UNDEFINED_SWIZZLE;
        resource_desc.Flags = D3D12_RESOURCE_FLAG_NONE;

        if (tests[i].dim == D3D12_RESOURCE_DIMENSION_BUFFER)
            resource_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        hr = ID3D12Device_CreateReservedResource(context.device, &resource_desc,
            D3D12_RESOURCE_STATE_GENERIC_READ, NULL, &IID_ID3D12Resource, (void **)&resource);
        todo_if(is_radv_device(context.device) && tests[i].todo_radv)
        ok(hr == S_OK, "Failed to create reserved resource, hr %#x.\n", hr);

        if (hr != S_OK)
            continue;

        ID3D12Device_GetResourceTiling(context.device, resource, &num_resource_tiles, &packed_mip_info, &tile_shape, &num_tilings, 0, tilings);

        ok(num_resource_tiles >= tests[i].expected_tile_count, "Unexpected resource tile count %u.\n", num_resource_tiles);
        ok(num_tilings == tests[i].expected_tiling_count, "Unexpected subresource tiling count %u.\n", num_tilings);

        ok(packed_mip_info.NumStandardMips >= tests[i].expected_standard_mips, "Unexpected standard mip count %u.\n", packed_mip_info.NumStandardMips);
        ok(packed_mip_info.NumPackedMips == (tests[i].dim == D3D12_RESOURCE_DIMENSION_BUFFER
                ? 0 : tests[i].mip_levels - packed_mip_info.NumStandardMips),
                "Unexpected packed mip count %u.\n", packed_mip_info.NumPackedMips);
        ok((packed_mip_info.NumTilesForPackedMips == 0) == (packed_mip_info.NumPackedMips == 0),
                "Unexpected packed tile count %u.\n", packed_mip_info.NumTilesForPackedMips);

        /* Docs say that tile shape should be cleared to zero if there is no standard mip, but drivers don't seem to care about this. */
        ok(tile_shape.WidthInTexels == tests[i].tile_shape_w, "Unexpected tile width %u.\n", tile_shape.WidthInTexels);
        ok(tile_shape.HeightInTexels == tests[i].tile_shape_h, "Unexpected tile height %u.\n", tile_shape.HeightInTexels);
        ok(tile_shape.DepthInTexels == tests[i].tile_shape_d, "Unexpected tile depth %u.\n", tile_shape.DepthInTexels);

        for (j = 0; j < tests[i].expected_tiling_count; j++)
        {
            uint32_t mip = j % tests[i].mip_levels;

            if (mip < packed_mip_info.NumStandardMips || !packed_mip_info.NumPackedMips)
            {
                uint32_t expected_w = compute_tile_count(tests[i].width, mip, tests[i].tile_shape_w);
                uint32_t expected_h = compute_tile_count(tests[i].height, mip, tests[i].tile_shape_h);
                uint32_t expected_d = 1;

                if (tests[i].dim == D3D12_RESOURCE_DIMENSION_TEXTURE3D)
                    expected_d = compute_tile_count(tests[i].depth_or_array_layers, mip, tests[i].tile_shape_d);

                ok(tilings[j].WidthInTiles == expected_w, "Unexpected width %u for subresource %u.\n", tilings[j].WidthInTiles, j);
                ok(tilings[j].HeightInTiles == expected_h, "Unexpected width %u for subresource %u.\n", tilings[j].HeightInTiles, j);
                ok(tilings[j].DepthInTiles == expected_d, "Unexpected width %u for subresource %u.\n", tilings[j].DepthInTiles, j);

                ok(tilings[j].StartTileIndexInOverallResource == tile_index, "Unexpected start tile index %u for subresource %u.\n",
                        tilings[j].StartTileIndexInOverallResource, j);

                tile_index += tilings[j].WidthInTiles * tilings[j].HeightInTiles * tilings[j].DepthInTiles;
            }
            else
            {
                ok(!tilings[j].WidthInTiles && !tilings[j].HeightInTiles && !tilings[j].DepthInTiles,
                        "Unexpected tile count (%u,%u,%u) for packed subresource %u.\n",
                        tilings[j].WidthInTiles, tilings[j].HeightInTiles, tilings[j].DepthInTiles, j);
                ok(tilings[j].StartTileIndexInOverallResource == 0xffffffff, "Unexpected start tile index %u for packed subresource %u.\n",
                        tilings[j].StartTileIndexInOverallResource, j);
            }
        }

        ok(num_resource_tiles == tile_index + packed_mip_info.NumTilesForPackedMips,
                "Unexpected resource tile count %u.\n", num_resource_tiles);
        ok(packed_mip_info.StartTileIndexInOverallResource == (packed_mip_info.NumPackedMips ? tile_index : 0),
                "Unexpected mip tail start tile index %u.\n", packed_mip_info.StartTileIndexInOverallResource);

        ID3D12Resource_Release(resource);
    }
    vkd3d_test_set_context(NULL);

    destroy_test_context(&context);
}

static void set_region_offset(D3D12_TILED_RESOURCE_COORDINATE *region, uint32_t x, uint32_t y, uint32_t z, uint32_t subresource)
{
    region->X = x;
    region->Y = y;
    region->Z = z;
    region->Subresource = subresource;
}

static void set_region_size(D3D12_TILE_REGION_SIZE *region, uint32_t num_tiles, bool use_box, uint32_t w, uint32_t h, uint32_t d)
{
    region->NumTiles = num_tiles;
    region->UseBox = use_box;
    region->Width = w;
    region->Height = h;
    region->Depth = d;
}

void test_update_tile_mappings_remap_stress(void)
{
    D3D12_FEATURE_DATA_D3D12_OPTIONS options;
    D3D12_ROOT_SIGNATURE_DESC rs_desc;
    D3D12_ROOT_PARAMETER rs_params[2];
    D3D12_RESOURCE_DESC resource_desc;
    D3D12_HEAP_PROPERTIES heap_props;
    ID3D12Resource *output_resource;
    struct test_context_desc desc;
    struct test_context context;
    struct resource_readback rb;
    D3D12_HEAP_DESC heap_desc;
    ID3D12Resource *resource;
    unsigned int i, j, iter;
    ID3D12Heap *heap = NULL;
    HRESULT hr;

#include "shaders/sparse/headers/update_tile_mappings.h"

    memset(&desc, 0, sizeof(desc));
    desc.no_render_target = true;
    desc.no_pipeline = true;
    desc.no_root_signature = true;
    if (!init_test_context(&context, &desc))
        return;

    hr = ID3D12Device_CheckFeatureSupport(context.device, D3D12_FEATURE_D3D12_OPTIONS, &options, sizeof(options));
    ok(hr == S_OK, "Failed to check feature support, hr %#x.\n", hr);

    if (!options.TiledResourcesTier)
    {
        skip("Tiled resources not supported by device.\n");
        destroy_test_context(&context);
        return;
    }

    memset(&heap_props, 0, sizeof(heap_props));
    heap_props.Type = D3D12_HEAP_TYPE_DEFAULT;

    heap_desc.Properties = heap_props;
    heap_desc.Alignment = 0;
    heap_desc.SizeInBytes = 64 * TILE_SIZE;
    heap_desc.Flags = D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS;

    resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    resource_desc.Alignment = 0;
    resource_desc.Width = 64 * TILE_SIZE;
    resource_desc.Height = 1;
    resource_desc.DepthOrArraySize = 1;
    resource_desc.MipLevels = 1;
    resource_desc.Format = DXGI_FORMAT_UNKNOWN;
    resource_desc.SampleDesc.Count = 1;
    resource_desc.SampleDesc.Quality = 0;
    resource_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    resource_desc.Flags = 0;

    hr = ID3D12Device_CreateReservedResource(context.device, &resource_desc,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS, NULL, &IID_ID3D12Resource, (void **)&resource);
    ok(hr == S_OK, "Failed to create reserved buffer, hr %#x.\n", hr);

    output_resource = create_default_buffer(context.device, 64 * sizeof(uint32_t),
            D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    memset(rs_params, 0, sizeof(rs_params));
    memset(&rs_desc, 0, sizeof(rs_desc));
    rs_desc.NumParameters = ARRAY_SIZE(rs_params);
    rs_desc.pParameters = rs_params;
    rs_params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    rs_params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    create_root_signature(context.device, &rs_desc, &context.root_signature);

    context.pipeline_state = create_compute_pipeline_state(
            context.device, context.root_signature, update_tile_mappings_dxbc);

    for (iter = 0; iter < 100; iter++)
    {
        D3D12_TILE_REGION_SIZE resource_tile_region_size;
        D3D12_TILED_RESOURCE_COORDINATE resource_coord;
        D3D12_TILE_RANGE_FLAGS heap_range_flags;
        ID3D12Resource *placed_resource;
        ID3D12Resource *upload;
        UINT heap_tile_offset;
        uint32_t *upload_data;
        UINT heap_tile_count;
        ID3D12Heap *new_heap;

        const struct mappings
        {
            UINT resource_tile;
            UINT heap_tile;
            UINT count;
            D3D12_TILE_RANGE_FLAGS flags;
        } mappings[] = {
            { 0, 0, 64, D3D12_TILE_RANGE_FLAG_NULL },
            { 4 + (iter & 31), 8 + (iter & 1), 3, D3D12_TILE_RANGE_FLAG_NONE },
            { 1 + (iter & 14), 2 + (iter & 4), 40, D3D12_TILE_RANGE_FLAG_NONE },
            { 13 + (iter & 9), 0, 9, D3D12_TILE_RANGE_FLAG_NULL },
            { 13 + (iter & 7), 19 + (iter & 4), 8, D3D12_TILE_RANGE_FLAG_REUSE_SINGLE_TILE },
            { 30 + (iter & 5), 0, 7, D3D12_TILE_RANGE_FLAG_NULL },
            { 1 + (iter & 3), 0, 7, D3D12_TILE_RANGE_FLAG_NULL },
        };

        hr = ID3D12Device_CreateHeap(context.device, &heap_desc, &IID_ID3D12Heap, (void **)&new_heap);
        ok(hr == S_OK, "Failed to create heap, hr %#x.\n", hr);

        hr = ID3D12Device_CreatePlacedResource(context.device, new_heap, 0, &resource_desc,
                D3D12_RESOURCE_STATE_COMMON, NULL, &IID_ID3D12Resource, (void **)&placed_resource);
        ok(SUCCEEDED(hr), "Failed to create resource, hr #%x.\n", hr);

        /* Destroy the old heap while it has mappings to the heap, then rebind those pages. Should not explode. */
        if (heap)
            ID3D12Heap_Release(heap);
        heap = new_heap;

        upload_data = malloc(resource_desc.Width);
        for (i = 0; i < resource_desc.Width / 4; i++)
            upload_data[i] = i + 1;
        upload = create_upload_buffer(context.device, resource_desc.Width, upload_data);
        ID3D12GraphicsCommandList_CopyResource(context.list, placed_resource, upload);
        ID3D12GraphicsCommandList_Close(context.list);
        exec_command_list(context.queue, context.list);
        ID3D12GraphicsCommandList_Reset(context.list, context.allocator, NULL);
        free(upload_data);

        for (i = 0; i < ARRAY_SIZE(mappings); i++)
        {
            memset(&resource_coord, 0, sizeof(resource_coord));
            memset(&resource_tile_region_size, 0, sizeof(resource_tile_region_size));
            resource_coord.X = mappings[i].resource_tile;
            resource_tile_region_size.NumTiles = mappings[i].count;
            resource_tile_region_size.UseBox = FALSE;
            heap_range_flags = mappings[i].flags;
            heap_tile_offset = mappings[i].heap_tile;
            heap_tile_count = mappings[i].count;

            ID3D12CommandQueue_UpdateTileMappings(context.queue, resource,
                    1, &resource_coord, &resource_tile_region_size, heap, 1,
                    &heap_range_flags, &heap_tile_offset, &heap_tile_count, D3D12_TILE_MAPPING_FLAG_NONE);
        }

#define OFFSET_INTO_PAGE 50000

        ID3D12GraphicsCommandList_SetComputeRootSignature(context.list, context.root_signature);
        ID3D12GraphicsCommandList_SetPipelineState(context.list, context.pipeline_state);
        ID3D12GraphicsCommandList_SetComputeRootShaderResourceView(context.list, 0,
                ID3D12Resource_GetGPUVirtualAddress(resource) + OFFSET_INTO_PAGE);
        ID3D12GraphicsCommandList_SetComputeRootUnorderedAccessView(context.list, 1,
                ID3D12Resource_GetGPUVirtualAddress(output_resource));
        ID3D12GraphicsCommandList_Dispatch(context.list, 1, 1, 1);
        transition_resource_state(context.list, output_resource, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                D3D12_RESOURCE_STATE_COPY_SOURCE);
        get_buffer_readback_with_command_list(output_resource, DXGI_FORMAT_R32_UINT, &rb, context.queue, context.list);
        reset_command_list(context.list, context.allocator);
        for (i = 0; i < 64; i++)
        {
            uint32_t expected = 0;
            for (j = 0; j < ARRAY_SIZE(mappings); j++)
            {
                if (i >= mappings[j].resource_tile && i < mappings[j].resource_tile + mappings[j].count)
                {
                    if (mappings[j].flags == D3D12_TILE_RANGE_FLAG_NULL)
                        expected = 0;
                    else if (mappings[j].flags == D3D12_TILE_RANGE_FLAG_REUSE_SINGLE_TILE)
                        expected = mappings[j].heap_tile * TILE_SIZE / 4 + OFFSET_INTO_PAGE / 4 + 1;
                    else
                        expected = (mappings[j].heap_tile + (i - mappings[j].resource_tile)) * TILE_SIZE / 4 + OFFSET_INTO_PAGE / 4 + 1;
                }
            }
            ok(expected == get_readback_uint(&rb, i, 0, 0),
                    "Iter %u: value %u: Expected %u, got %u.\n", iter, i, expected, get_readback_uint(&rb, i, 0, 0));
        }
        release_resource_readback(&rb);
        ID3D12Resource_Release(placed_resource);
        ID3D12Resource_Release(upload);
    }

    ID3D12Resource_Release(resource);
    ID3D12Resource_Release(output_resource);
    ID3D12Heap_Release(heap);
    destroy_test_context(&context);
}

void test_update_tile_mappings(void)
{
    D3D12_TILED_RESOURCE_COORDINATE region_offsets[8];
    ID3D12PipelineState *check_texture_array_pipeline;
    ID3D12PipelineState *check_texture_3d_pipeline;
    D3D12_ROOT_SIGNATURE_DESC root_signature_desc;
    ID3D12PipelineState *clear_texture_pipeline;
    ID3D12PipelineState *check_texture_pipeline;
    ID3D12PipelineState *check_buffer_pipeline;
    ID3D12Resource *resource, *readback_buffer;
    D3D12_UNORDERED_ACCESS_VIEW_DESC uav_desc;
    ID3D12DescriptorHeap *cpu_heap, *gpu_heap;
    ID3D12RootSignature *clear_root_signature;
    D3D12_FEATURE_DATA_D3D12_OPTIONS options;
    D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc;
    D3D12_DESCRIPTOR_RANGE descriptor_range;
    D3D12_ROOT_PARAMETER root_parameters[2];
    D3D12_TILE_REGION_SIZE region_sizes[8];
    D3D12_GPU_VIRTUAL_ADDRESS readback_va;
    D3D12_PACKED_MIP_INFO packed_mip_info;
    D3D12_HEAP_PROPERTIES heap_properties;
    D3D12_SUBRESOURCE_TILING tilings[10];
    D3D12_TILE_RANGE_FLAGS tile_flags[8];
    ID3D12RootSignature *root_signature;
    D3D12_RESOURCE_DESC resource_desc;
    struct test_context_desc desc;
    struct resource_readback rb;
    struct test_context context;
    D3D12_TILE_SHAPE tile_shape;
    unsigned int i, j, x, y, z;
    D3D12_HEAP_DESC heap_desc;
    UINT tile_offsets[8];
    UINT tile_counts[8];
    ID3D12Heap *heap;
    UINT num_tilings;
    D3D12_BOX box;
    HRESULT hr;

#include "shaders/sparse/headers/update_tile_mappings.h"
#include "shaders/sparse/headers/update_tile_mappings_texture.h"
#include "shaders/sparse/headers/update_tile_mappings_texture_array.h"
#include "shaders/sparse/headers/update_tile_mappings_texture_3d.h"
#include "shaders/sparse/headers/update_tile_mappings_cs_clear.h"

    static const uint32_t buffer_region_tiles[] =
    {
    /*     0   1   2   3   4   5   6   7   8   9 */
    /*0*/ 33, 34, 35, 36, 37,  6,  7,  8,  9, 10,
    /*1*/ 11, 12, 38, 39, 40, 41,  1, 18,  2, 20,
    /*2*/ 21, 22, 23,  3,  4,  4,  4,  0,  0, 25,
    /*3*/ 26, 27, 28, 29, 30, 36, 37, 38, 39, 40,
    /*4*/  9, 11, 43, 44, 45, 46, 45, 46, 49, 50,
    /*5*/  0,  0, 17, 18, 19, 20, 21, 22, 23, 24,
    /*6*/ 61, 62, 63, 12,
    };

    static const uint32_t texture_region_tiles[] =
    {
        1, 2, 4, 5, 6, 7, 1, 1, 9, 1, 17, 14, 8, 14, 3, 0,
        17, 18, 19, 18, 19, 22, 23, 24, 25, 26, 27, 28,
    };

    static const uint32_t texture_3d_region_tiles[] =
    {
        3, 2, 0, 7, 8, 2, 4, 5, 6,
    };

    memset(&desc, 0, sizeof(desc));
    desc.rt_width = 640;
    desc.rt_height = 480;
    desc.rt_format = DXGI_FORMAT_R8G8B8A8_UNORM;
    if (!init_test_context(&context, &desc))
        return;

    hr = ID3D12Device_CheckFeatureSupport(context.device, D3D12_FEATURE_D3D12_OPTIONS, &options, sizeof(options));
    ok(hr == S_OK, "Failed to check feature support, hr %#x.\n", hr);

    if (!options.TiledResourcesTier)
    {
        skip("Tiled resources not supported by device.\n");
        destroy_test_context(&context);
        return;
    }

    descriptor_range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    descriptor_range.NumDescriptors = 1;
    descriptor_range.BaseShaderRegister = 0;
    descriptor_range.RegisterSpace = 0;
    descriptor_range.OffsetInDescriptorsFromTableStart = 0;
    root_parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    root_parameters[0].DescriptorTable.NumDescriptorRanges = 1;
    root_parameters[0].DescriptorTable.pDescriptorRanges = &descriptor_range;
    root_parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    root_parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    root_parameters[1].Descriptor.ShaderRegister = 0;
    root_parameters[1].Descriptor.RegisterSpace = 0;
    root_parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    root_signature_desc.NumParameters = ARRAY_SIZE(root_parameters);
    root_signature_desc.pParameters = root_parameters;
    root_signature_desc.NumStaticSamplers = 0;
    root_signature_desc.pStaticSamplers = NULL;
    root_signature_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
    hr = create_root_signature(context.device, &root_signature_desc, &root_signature);
    ok(hr == S_OK, "Failed to create root signature, hr %#x.\n", hr);

    descriptor_range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    root_parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    root_parameters[1].Constants.ShaderRegister = 0;
    root_parameters[1].Constants.RegisterSpace = 0;
    root_parameters[1].Constants.Num32BitValues = 4;
    root_parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    hr = create_root_signature(context.device, &root_signature_desc, &clear_root_signature);
    ok(hr == S_OK, "Failed to create root signature, hr %#x.\n", hr);

    clear_texture_pipeline = create_compute_pipeline_state(context.device, clear_root_signature, update_tile_mappings_cs_clear_dxbc);
    check_texture_pipeline = create_compute_pipeline_state(context.device, root_signature, update_tile_mappings_texture_dxbc);
    check_texture_array_pipeline = create_compute_pipeline_state(context.device, root_signature, update_tile_mappings_texture_array_dxbc);
    check_texture_3d_pipeline = create_compute_pipeline_state(context.device, root_signature, update_tile_mappings_texture_3d_dxbc);
    check_buffer_pipeline = create_compute_pipeline_state(context.device, root_signature, update_tile_mappings_dxbc);

    cpu_heap = create_cpu_descriptor_heap(context.device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 64);
    gpu_heap = create_gpu_descriptor_heap(context.device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 64);

    memset(&heap_properties, 0, sizeof(heap_properties));
    heap_properties.Type = D3D12_HEAP_TYPE_DEFAULT;

    resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    resource_desc.Alignment = 0;
    resource_desc.Width = 64 * sizeof(uint32_t);
    resource_desc.Height = 1;
    resource_desc.DepthOrArraySize = 1;
    resource_desc.MipLevels = 1;
    resource_desc.Format = DXGI_FORMAT_UNKNOWN;
    resource_desc.SampleDesc.Count = 1;
    resource_desc.SampleDesc.Quality = 0;
    resource_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    resource_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    hr = ID3D12Device_CreateCommittedResource(context.device, &heap_properties, D3D12_HEAP_FLAG_NONE,
            &resource_desc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, NULL, &IID_ID3D12Resource, (void **)&readback_buffer);
    ok(hr == S_OK, "Failed to create readback buffer, hr %#x.\n", hr);

    readback_va = ID3D12Resource_GetGPUVirtualAddress(readback_buffer);

    /* Test buffer tile mappings */
    heap_desc.Properties = heap_properties;
    heap_desc.Alignment = 0;
    heap_desc.SizeInBytes = 64 * 65536;
    heap_desc.Flags = D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS;
    hr = ID3D12Device_CreateHeap(context.device, &heap_desc, &IID_ID3D12Heap, (void **)&heap);
    ok(hr == S_OK, "Failed to create heap, hr %#x.\n", hr);

    resource_desc.Width = 64 * 65536;
    hr = ID3D12Device_CreateReservedResource(context.device, &resource_desc,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS, NULL, &IID_ID3D12Resource, (void **)&resource);
    ok(hr == S_OK, "Failed to create reserved buffer, hr %#x.\n", hr);

    srv_desc.Format = DXGI_FORMAT_UNKNOWN;
    srv_desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv_desc.Buffer.FirstElement = 0;
    srv_desc.Buffer.NumElements = resource_desc.Width / sizeof(uint32_t);
    srv_desc.Buffer.StructureByteStride = sizeof(uint32_t);
    srv_desc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
    ID3D12Device_CreateShaderResourceView(context.device, resource, &srv_desc, get_cpu_descriptor_handle(&context, gpu_heap, 0));

    uav_desc.Format = DXGI_FORMAT_R32_UINT;
    uav_desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    uav_desc.Buffer.FirstElement = 0;
    uav_desc.Buffer.NumElements = resource_desc.Width / sizeof(uint32_t);
    uav_desc.Buffer.StructureByteStride = 0;
    uav_desc.Buffer.CounterOffsetInBytes = 0;
    uav_desc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;
    ID3D12Device_CreateUnorderedAccessView(context.device, resource, NULL, &uav_desc, get_cpu_descriptor_handle(&context, cpu_heap, 1));
    ID3D12Device_CreateUnorderedAccessView(context.device, resource, NULL, &uav_desc, get_cpu_descriptor_handle(&context, gpu_heap, 1));

    /* Map entire buffer, linearly, and initialize tile data */
    tile_offsets[0] = 0;
    ID3D12CommandQueue_UpdateTileMappings(context.queue, resource,
        1, NULL, NULL, heap, 1, NULL, tile_offsets, NULL, D3D12_TILE_MAPPING_FLAG_NONE);
    ID3D12GraphicsCommandList_SetDescriptorHeaps(context.list, 1, &gpu_heap);

    for (i = 0; i < 64; i++)
    {
        UINT clear_value[4] = { 0, 0, 0, 0 };
        D3D12_RECT clear_rect;

        set_rect(&clear_rect, 16384 * i, 0, 16384 * (i + 1), 1);
        clear_value[0] = i + 1;

        ID3D12GraphicsCommandList_ClearUnorderedAccessViewUint(context.list,
                get_gpu_descriptor_handle(&context, gpu_heap, 1),
                get_cpu_descriptor_handle(&context, cpu_heap, 1),
                resource, clear_value, 1, &clear_rect);
    }

    transition_resource_state(context.list, resource, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    ID3D12GraphicsCommandList_SetComputeRootSignature(context.list, root_signature);
    ID3D12GraphicsCommandList_SetPipelineState(context.list, check_buffer_pipeline);
    ID3D12GraphicsCommandList_SetComputeRootDescriptorTable(context.list, 0, get_gpu_descriptor_handle(&context, gpu_heap, 0));
    ID3D12GraphicsCommandList_SetComputeRootUnorderedAccessView(context.list, 1, readback_va);
    ID3D12GraphicsCommandList_Dispatch(context.list, 1, 1, 1);
    transition_resource_state(context.list, readback_buffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);

    get_buffer_readback_with_command_list(readback_buffer, DXGI_FORMAT_R32_UINT, &rb, context.queue, context.list);

    for (i = 0; i < 64; i++)
    {
        set_box(&box, i, 0, 0, i + 1, 1, 1);
        check_readback_data_uint(&rb, &box, i + 1, 0);
    }

    release_resource_readback(&rb);

    /* Test arbitrary tile mappings */
    set_region_offset(&region_offsets[0], 16, 0, 0, 0);
    set_region_offset(&region_offsets[1], 18, 0, 0, 0);
    set_region_offset(&region_offsets[2], 23, 0, 0, 0);
    set_region_offset(&region_offsets[3], 40, 0, 0, 0);
    set_region_offset(&region_offsets[4], 41, 0, 0, 0);
    set_region_offset(&region_offsets[5], 63, 0, 0, 0);

    tile_offsets[0] = 0;
    tile_offsets[1] = 8;
    tile_offsets[2] = 10;

    tile_counts[0] = 3;
    tile_counts[1] = 1;
    tile_counts[2] = 2;

    ID3D12CommandQueue_UpdateTileMappings(context.queue, resource,
            6, region_offsets, NULL, heap, 3, NULL, tile_offsets, tile_counts,
            D3D12_TILE_MAPPING_FLAG_NONE);

    set_region_offset(&region_offsets[0], 24, 0, 0, 0);
    set_region_offset(&region_offsets[1], 50, 0, 0, 0);
    set_region_offset(&region_offsets[2], 0, 0, 0, 0);
    set_region_offset(&region_offsets[3], 52, 0, 0, 0);
    set_region_offset(&region_offsets[4], 29, 0, 0, 0);

    set_region_size(&region_sizes[0], 5, false, 0, 0, 0);
    set_region_size(&region_sizes[1], 2, false, 0, 0, 0);
    set_region_size(&region_sizes[2], 16, false, 0, 0, 0);
    set_region_size(&region_sizes[3], 8, false, 0, 0, 0);
    set_region_size(&region_sizes[4], 6, false, 0, 0, 0);

    tile_flags[0] = D3D12_TILE_RANGE_FLAG_REUSE_SINGLE_TILE;
    tile_flags[1] = D3D12_TILE_RANGE_FLAG_NULL;
    tile_flags[2] = D3D12_TILE_RANGE_FLAG_NONE;
    tile_flags[3] = D3D12_TILE_RANGE_FLAG_SKIP;
    tile_flags[4] = D3D12_TILE_RANGE_FLAG_NONE;
    tile_flags[5] = D3D12_TILE_RANGE_FLAG_NONE;

    tile_offsets[0] = 3;
    tile_offsets[1] = 0;
    tile_offsets[2] = 32;
    tile_offsets[3] = 0;
    tile_offsets[4] = 37;
    tile_offsets[5] = 16;

    tile_counts[0] = 3;
    tile_counts[1] = 4;
    tile_counts[2] = 5;
    tile_counts[3] = 7;
    tile_counts[4] = 4;
    tile_counts[5] = 14;

    ID3D12CommandQueue_UpdateTileMappings(context.queue, resource,
        5, region_offsets, region_sizes, heap, 6, tile_flags, tile_offsets, tile_counts,
        D3D12_TILE_MAPPING_FLAG_NONE);

    set_region_offset(&region_offsets[0], 46, 0, 0, 0);
    set_region_offset(&region_offsets[1], 44, 0, 0, 0);
    set_region_size(&region_sizes[0], 2, false, 0, 0, 0);

    ID3D12CommandQueue_CopyTileMappings(context.queue,
        resource, &region_offsets[0], resource, &region_offsets[1],
        &region_sizes[0], D3D12_TILE_MAPPING_FLAG_NONE);

    reset_command_list(context.list, context.allocator);

    transition_resource_state(context.list, readback_buffer, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    ID3D12GraphicsCommandList_SetDescriptorHeaps(context.list, 1, &gpu_heap);
    ID3D12GraphicsCommandList_SetComputeRootSignature(context.list, root_signature);
    ID3D12GraphicsCommandList_SetPipelineState(context.list, check_buffer_pipeline);
    ID3D12GraphicsCommandList_SetComputeRootDescriptorTable(context.list, 0, get_gpu_descriptor_handle(&context, gpu_heap, 0));
    ID3D12GraphicsCommandList_SetComputeRootUnorderedAccessView(context.list, 1, readback_va);
    ID3D12GraphicsCommandList_Dispatch(context.list, 1, 1, 1);
    transition_resource_state(context.list, readback_buffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);

    get_buffer_readback_with_command_list(readback_buffer, DXGI_FORMAT_R32_UINT, &rb, context.queue, context.list);

    for (i = 0; i < ARRAY_SIZE(buffer_region_tiles); i++)
    {
        if (options.TiledResourcesTier > D3D12_TILED_RESOURCES_TIER_2 || buffer_region_tiles[i])
        {
            set_box(&box, i, 0, 0, i + 1, 1, 1);
            check_readback_data_uint(&rb, &box, buffer_region_tiles[i], 0);
        }
    }

    release_resource_readback(&rb);

    /* Test mapping a single tile twice in one call */
    set_region_offset(&region_offsets[0], 0, 0, 0, 0);
    set_region_offset(&region_offsets[1], 15, 0, 0, 0);

    set_region_size(&region_sizes[0], 64, false, 0, 0, 0);
    set_region_size(&region_sizes[1], 3, false, 0, 0, 0);

    tile_flags[0] = D3D12_TILE_RANGE_FLAG_NULL;
    tile_flags[1] = D3D12_TILE_RANGE_FLAG_NONE;

    tile_offsets[0] = 0;
    tile_offsets[1] = 8;

    tile_counts[0] = 64;
    tile_counts[1] = 4;

    ID3D12CommandQueue_UpdateTileMappings(context.queue, resource,
        2, region_offsets, region_sizes, heap, 2, tile_flags, tile_offsets, tile_counts,
        D3D12_TILE_MAPPING_FLAG_NONE);

    reset_command_list(context.list, context.allocator);

    transition_resource_state(context.list, readback_buffer, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    ID3D12GraphicsCommandList_SetDescriptorHeaps(context.list, 1, &gpu_heap);
    ID3D12GraphicsCommandList_SetComputeRootSignature(context.list, root_signature);
    ID3D12GraphicsCommandList_SetPipelineState(context.list, check_buffer_pipeline);
    ID3D12GraphicsCommandList_SetComputeRootDescriptorTable(context.list, 0, get_gpu_descriptor_handle(&context, gpu_heap, 0));
    ID3D12GraphicsCommandList_SetComputeRootUnorderedAccessView(context.list, 1, readback_va);
    ID3D12GraphicsCommandList_Dispatch(context.list, 1, 1, 1);
    transition_resource_state(context.list, readback_buffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);

    get_buffer_readback_with_command_list(readback_buffer, DXGI_FORMAT_R32_UINT, &rb, context.queue, context.list);

    for (i = 0; i < ARRAY_SIZE(buffer_region_tiles); i++)
    {
        uint32_t expected = 0u;

        if (i >= region_offsets[1].X && i < region_offsets[1].X + region_sizes[1].NumTiles)
            expected = tile_offsets[1] + i + 1 - region_offsets[1].X;

        set_box(&box, i, 0, 0, i + 1, 1, 1);

        /* WARP binds everything to NULL */
        bug_if(use_warp_device)
        check_readback_data_uint(&rb, &box, expected, 0);
    }

    release_resource_readback(&rb);

    ID3D12Resource_Release(resource);
    ID3D12Heap_Release(heap);

    /* Test 2D image tile mappings */
    heap_desc.Properties = heap_properties;
    heap_desc.Alignment = 0;
    heap_desc.SizeInBytes = 64 * 65536;
    heap_desc.Flags = D3D12_HEAP_FLAG_ALLOW_ONLY_NON_RT_DS_TEXTURES;
    hr = ID3D12Device_CreateHeap(context.device, &heap_desc, &IID_ID3D12Heap, (void **)&heap);
    ok(hr == S_OK, "Failed to create heap, hr %#x.\n", hr);

    resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    resource_desc.Alignment = 0;
    resource_desc.Width = 512;
    resource_desc.Height = 512;
    resource_desc.DepthOrArraySize = 1;
    resource_desc.MipLevels = 10;
    resource_desc.Format = DXGI_FORMAT_R32_UINT;
    resource_desc.SampleDesc.Count = 1;
    resource_desc.SampleDesc.Quality = 0;
    resource_desc.Layout = D3D12_TEXTURE_LAYOUT_64KB_UNDEFINED_SWIZZLE;
    resource_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    hr = ID3D12Device_CreateReservedResource(context.device, &resource_desc,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS, NULL, &IID_ID3D12Resource, (void **)&resource);
    ok(hr == S_OK, "Failed to create reserved texture, hr %#x.\n", hr);

    num_tilings = resource_desc.MipLevels;
    ID3D12Device_GetResourceTiling(context.device, resource, NULL, &packed_mip_info, &tile_shape, &num_tilings, 0, tilings);
    ok(packed_mip_info.NumStandardMips >= 3, "Unexpected number of standard mips %u.\n", packed_mip_info.NumStandardMips);

    srv_desc.Format = DXGI_FORMAT_R32_UINT;
    srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv_desc.Texture2D.MostDetailedMip = 0;
    srv_desc.Texture2D.MipLevels = resource_desc.MipLevels;
    srv_desc.Texture2D.PlaneSlice = 0;
    srv_desc.Texture2D.ResourceMinLODClamp = 0.0f;
    ID3D12Device_CreateShaderResourceView(context.device, resource, &srv_desc, get_cpu_descriptor_handle(&context, gpu_heap, 0));

    /* Map entire image */
    tile_offsets[0] = 0;
    ID3D12CommandQueue_UpdateTileMappings(context.queue, resource,
        1, NULL, NULL, heap, 1, NULL, tile_offsets, NULL, D3D12_TILE_MAPPING_FLAG_NONE);

    reset_command_list(context.list, context.allocator);
    ID3D12GraphicsCommandList_SetDescriptorHeaps(context.list, 1, &gpu_heap);

    for (i = 0, j = 0; i < resource_desc.MipLevels; i++)
    {
        uav_desc.Format = DXGI_FORMAT_R32_UINT;
        uav_desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        uav_desc.Texture2D.MipSlice = i;
        uav_desc.Texture2D.PlaneSlice = 0;
        ID3D12Device_CreateUnorderedAccessView(context.device, resource, NULL, &uav_desc, get_cpu_descriptor_handle(&context, cpu_heap, 1 + i));
        ID3D12Device_CreateUnorderedAccessView(context.device, resource, NULL, &uav_desc, get_cpu_descriptor_handle(&context, gpu_heap, 1 + i));

        for (y = 0; y < max(1u, tilings[i].HeightInTiles); y++)
        {
            for (x = 0; x < max(1u, tilings[i].WidthInTiles); x++)
            {
                UINT clear_value[4] = { 0, 0, 0, 0 };
                D3D12_RECT clear_rect;

                clear_value[0] = ++j;
                set_rect(&clear_rect,
                    x * tile_shape.WidthInTexels, y * tile_shape.HeightInTexels,
                    min(resource_desc.Width >> i, (x + 1) * tile_shape.WidthInTexels),
                    min(resource_desc.Height >> i, (y + 1) * tile_shape.HeightInTexels));

                ID3D12GraphicsCommandList_ClearUnorderedAccessViewUint(context.list,
                        get_gpu_descriptor_handle(&context, gpu_heap, 1 + i),
                        get_cpu_descriptor_handle(&context, cpu_heap, 1 + i),
                        resource, clear_value, 1, &clear_rect);
            }
        }
    }

    transition_resource_state(context.list, resource, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    transition_resource_state(context.list, readback_buffer, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    ID3D12GraphicsCommandList_SetComputeRootSignature(context.list, root_signature);
    ID3D12GraphicsCommandList_SetPipelineState(context.list, check_texture_pipeline);
    ID3D12GraphicsCommandList_SetComputeRootDescriptorTable(context.list, 0, get_gpu_descriptor_handle(&context, gpu_heap, 0));
    ID3D12GraphicsCommandList_SetComputeRootUnorderedAccessView(context.list, 1, readback_va);
    ID3D12GraphicsCommandList_Dispatch(context.list, 1, 1, 1);
    transition_resource_state(context.list, readback_buffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);

    get_buffer_readback_with_command_list(readback_buffer, DXGI_FORMAT_R32_UINT, &rb, context.queue, context.list);

    for (i = 0; i < j; i++)
    {
        set_box(&box, i, 0, 0, i + 1, 1, 1);
        check_readback_data_uint(&rb, &box, i + 1, 0);
    }

    release_resource_readback(&rb);

    set_region_offset(&region_offsets[0], 2, 0, 0, 0);
    set_region_offset(&region_offsets[1], 1, 1, 0, 0);
    set_region_offset(&region_offsets[2], 1, 1, 0, 1);
    set_region_offset(&region_offsets[3], 0, 3, 0, 0);

    set_region_size(&region_sizes[0], 3, false, 0, 0, 0);
    set_region_size(&region_sizes[1], 4, true, 2, 2, 1);
    set_region_size(&region_sizes[2], 2, false, 0, 0, 0);
    set_region_size(&region_sizes[3], 4, true, 4, 1, 1);

    tile_flags[0] = D3D12_TILE_RANGE_FLAG_NONE;
    tile_flags[1] = D3D12_TILE_RANGE_FLAG_REUSE_SINGLE_TILE;
    tile_flags[2] = D3D12_TILE_RANGE_FLAG_NONE;
    tile_flags[3] = D3D12_TILE_RANGE_FLAG_NONE;
    tile_flags[4] = D3D12_TILE_RANGE_FLAG_SKIP;
    tile_flags[5] = D3D12_TILE_RANGE_FLAG_NONE;
    tile_flags[6] = D3D12_TILE_RANGE_FLAG_NULL;

    tile_offsets[0] = 3;
    tile_offsets[1] = 0;
    tile_offsets[2] = 16;
    tile_offsets[3] = 7;
    tile_offsets[4] = 0;
    tile_offsets[5] = 2;
    tile_offsets[6] = 0;

    tile_counts[0] = 4;
    tile_counts[1] = 2;
    tile_counts[2] = 3;
    tile_counts[3] = 1;
    tile_counts[4] = 1;
    tile_counts[5] = 1;
    tile_counts[6] = 1;

    ID3D12CommandQueue_UpdateTileMappings(context.queue, resource,
        4, region_offsets, region_sizes, heap, 7, tile_flags, tile_offsets, tile_counts,
        D3D12_TILE_MAPPING_FLAG_NONE);

    set_region_offset(&region_offsets[0], 3, 1, 0, 0);
    set_region_offset(&region_offsets[1], 1, 2, 0, 0);
    set_region_size(&region_sizes[0], 2, true, 1, 2, 1);

    ID3D12CommandQueue_CopyTileMappings(context.queue,
        resource, &region_offsets[0], resource, &region_offsets[1],
        &region_sizes[0], D3D12_TILE_MAPPING_FLAG_NONE);

    reset_command_list(context.list, context.allocator);

    transition_resource_state(context.list, readback_buffer, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    ID3D12GraphicsCommandList_SetDescriptorHeaps(context.list, 1, &gpu_heap);
    ID3D12GraphicsCommandList_SetComputeRootSignature(context.list, root_signature);
    ID3D12GraphicsCommandList_SetPipelineState(context.list, check_texture_pipeline);
    ID3D12GraphicsCommandList_SetComputeRootDescriptorTable(context.list, 0, get_gpu_descriptor_handle(&context, gpu_heap, 0));
    ID3D12GraphicsCommandList_SetComputeRootUnorderedAccessView(context.list, 1, readback_va);
    ID3D12GraphicsCommandList_Dispatch(context.list, 1, 1, 1);
    transition_resource_state(context.list, readback_buffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);

    get_buffer_readback_with_command_list(readback_buffer, DXGI_FORMAT_R32_UINT, &rb, context.queue, context.list);

    for (i = 0; i < j; i++)
    {
        if (options.TiledResourcesTier > D3D12_TILED_RESOURCES_TIER_2 || texture_region_tiles[i])
        {
            set_box(&box, i, 0, 0, i + 1, 1, 1);
            check_readback_data_uint(&rb, &box, texture_region_tiles[i], 0);
        }
    }

    release_resource_readback(&rb);
    ID3D12Resource_Release(resource);

    if (options.TiledResourcesTier >= D3D12_TILED_RESOURCES_TIER_3)
    {
        /* Test 3D image tile mappings */
        resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE3D;
        resource_desc.Alignment = 0;
        resource_desc.Width = 64;
        resource_desc.Height = 64;
        resource_desc.DepthOrArraySize = 32;
        resource_desc.MipLevels = 2;
        resource_desc.Format = DXGI_FORMAT_R32_UINT;
        resource_desc.SampleDesc.Count = 1;
        resource_desc.SampleDesc.Quality = 0;
        resource_desc.Layout = D3D12_TEXTURE_LAYOUT_64KB_UNDEFINED_SWIZZLE;
        resource_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

        hr = ID3D12Device_CreateReservedResource(context.device, &resource_desc,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS, NULL, &IID_ID3D12Resource, (void **)&resource);
        ok(hr == S_OK, "Failed to create reserved texture, hr %#x.\n", hr);

        num_tilings = resource_desc.MipLevels;
        ID3D12Device_GetResourceTiling(context.device, resource, NULL, &packed_mip_info, &tile_shape, &num_tilings, 0, tilings);
        ok(packed_mip_info.NumStandardMips == 2, "Unexpected number of standard mips %u.\n", packed_mip_info.NumStandardMips);

        srv_desc.Format = DXGI_FORMAT_R32_UINT;
        srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
        srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv_desc.Texture3D.MostDetailedMip = 0;
        srv_desc.Texture3D.MipLevels = resource_desc.MipLevels;
        srv_desc.Texture3D.ResourceMinLODClamp = 0.0f;
        ID3D12Device_CreateShaderResourceView(context.device, resource, &srv_desc, get_cpu_descriptor_handle(&context, gpu_heap, 0));

        /* Map entire image */
        tile_offsets[0] = 0;
        ID3D12CommandQueue_UpdateTileMappings(context.queue, resource,
            1, NULL, NULL, heap, 1, NULL, tile_offsets, NULL, D3D12_TILE_MAPPING_FLAG_NONE);

        reset_command_list(context.list, context.allocator);

        for (i = 0, j = 0; i < resource_desc.MipLevels; i++)
        {
            uav_desc.Format = DXGI_FORMAT_R32_UINT;
            uav_desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE3D;
            uav_desc.Texture3D.MipSlice = i;
            uav_desc.Texture3D.FirstWSlice = 0;
            uav_desc.Texture3D.WSize = resource_desc.DepthOrArraySize >> i;
            ID3D12Device_CreateUnorderedAccessView(context.device, resource, NULL, &uav_desc, get_cpu_descriptor_handle(&context, cpu_heap, 1 + i));
            ID3D12Device_CreateUnorderedAccessView(context.device, resource, NULL, &uav_desc, get_cpu_descriptor_handle(&context, gpu_heap, 1 + i));

            /* ClearUnorderedAccessView only takes 2D coordinates so we have to
             * bring our own shader to initialize portions of a 3D image */
            ID3D12GraphicsCommandList_SetDescriptorHeaps(context.list, 1, &gpu_heap);
            ID3D12GraphicsCommandList_SetComputeRootSignature(context.list, clear_root_signature);
            ID3D12GraphicsCommandList_SetComputeRootDescriptorTable(context.list, 0, get_gpu_descriptor_handle(&context, gpu_heap, 1 + i));
            ID3D12GraphicsCommandList_SetPipelineState(context.list, clear_texture_pipeline);

            for (z = 0; z < max(1u, tilings[i].DepthInTiles); z++)
            {
                for (y = 0; y < max(1u, tilings[i].HeightInTiles); y++)
                {
                    for (x = 0; x < max(1u, tilings[i].WidthInTiles); x++)
                    {
                        UINT shader_args[4];
                        shader_args[0] = tile_shape.WidthInTexels * x;
                        shader_args[1] = tile_shape.HeightInTexels * y;
                        shader_args[2] = tile_shape.DepthInTexels * z;
                        shader_args[3] = ++j;

                        ID3D12GraphicsCommandList_SetComputeRoot32BitConstants(context.list,
                                1, ARRAY_SIZE(shader_args), shader_args, 0);
                        ID3D12GraphicsCommandList_Dispatch(context.list,
                                tile_shape.WidthInTexels / 4,
                                tile_shape.HeightInTexels / 4,
                                tile_shape.DepthInTexels / 4);
                    }
                }
            }
        }

        transition_resource_state(context.list, resource, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        transition_resource_state(context.list, readback_buffer, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        ID3D12GraphicsCommandList_SetDescriptorHeaps(context.list, 1, &gpu_heap);
        ID3D12GraphicsCommandList_SetComputeRootSignature(context.list, root_signature);
        ID3D12GraphicsCommandList_SetPipelineState(context.list, check_texture_3d_pipeline);
        ID3D12GraphicsCommandList_SetComputeRootDescriptorTable(context.list, 0, get_gpu_descriptor_handle(&context, gpu_heap, 0));
        ID3D12GraphicsCommandList_SetComputeRootUnorderedAccessView(context.list, 1, readback_va);
        ID3D12GraphicsCommandList_Dispatch(context.list, 1, 1, 1);
        transition_resource_state(context.list, readback_buffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);

        get_buffer_readback_with_command_list(readback_buffer, DXGI_FORMAT_R32_UINT, &rb, context.queue, context.list);

        for (i = 0; i < j; i++)
        {
            set_box(&box, i, 0, 0, i + 1, 1, 1);
            check_readback_data_uint(&rb, &box, i + 1, 0);
        }

        release_resource_readback(&rb);

        set_region_offset(&region_offsets[0], 0, 0, 0, 0);
        set_region_offset(&region_offsets[1], 0, 1, 1, 0);
        set_region_offset(&region_offsets[2], 1, 1, 0, 0);
        set_region_offset(&region_offsets[3], 1, 0, 0, 0);
        set_region_offset(&region_offsets[4], 0, 1, 0, 0);

        set_region_size(&region_sizes[0], 1, false, 0, 0, 0);
        set_region_size(&region_sizes[1], 3, false, 0, 0, 0);
        set_region_size(&region_sizes[2], 2, false, 0, 0, 0);
        set_region_size(&region_sizes[3], 2, true,  1, 1, 2);
        set_region_size(&region_sizes[4], 1, true,  1, 1, 1);

        tile_flags[0] = D3D12_TILE_RANGE_FLAG_NONE;
        tile_flags[1] = D3D12_TILE_RANGE_FLAG_REUSE_SINGLE_TILE;
        tile_flags[2] = D3D12_TILE_RANGE_FLAG_NULL;

        tile_offsets[0] = 2;
        tile_offsets[1] = 1;
        tile_offsets[2] = 0;

        tile_counts[0] = 6;
        tile_counts[1] = 2;
        tile_counts[2] = 1;

        ID3D12CommandQueue_UpdateTileMappings(context.queue, resource,
            5, region_offsets, region_sizes, heap, 3, tile_flags, tile_offsets, tile_counts,
            D3D12_TILE_MAPPING_FLAG_NONE);

        reset_command_list(context.list, context.allocator);

        transition_resource_state(context.list, readback_buffer, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        ID3D12GraphicsCommandList_SetDescriptorHeaps(context.list, 1, &gpu_heap);
        ID3D12GraphicsCommandList_SetComputeRootSignature(context.list, root_signature);
        ID3D12GraphicsCommandList_SetPipelineState(context.list, check_texture_3d_pipeline);
        ID3D12GraphicsCommandList_SetComputeRootDescriptorTable(context.list, 0, get_gpu_descriptor_handle(&context, gpu_heap, 0));
        ID3D12GraphicsCommandList_SetComputeRootUnorderedAccessView(context.list, 1, readback_va);
        ID3D12GraphicsCommandList_Dispatch(context.list, 1, 1, 1);
        transition_resource_state(context.list, readback_buffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);

        get_buffer_readback_with_command_list(readback_buffer, DXGI_FORMAT_R32_UINT, &rb, context.queue, context.list);

        for (i = 0; i < j; i++)
        {
            set_box(&box, i, 0, 0, i + 1, 1, 1);
            check_readback_data_uint(&rb, &box, texture_3d_region_tiles[i], 0);
        }

        release_resource_readback(&rb);
        ID3D12Resource_Release(resource);
    }
    else
    {
        skip("Tiled resources tier 3 not supported.\n");
    }

    if (options.TiledResourcesTier >= D3D12_TILED_RESOURCES_TIER_4)
    {
        /* Test 2D array image with mip tail */
        resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        resource_desc.Alignment = 0;
        resource_desc.Width = 256;
        resource_desc.Height = 256;
        resource_desc.DepthOrArraySize = 4;
        resource_desc.MipLevels = 9;
        resource_desc.Format = DXGI_FORMAT_R32_UINT;
        resource_desc.SampleDesc.Count = 1;
        resource_desc.SampleDesc.Quality = 0;
        resource_desc.Layout = D3D12_TEXTURE_LAYOUT_64KB_UNDEFINED_SWIZZLE;
        resource_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

        hr = ID3D12Device_CreateReservedResource(context.device, &resource_desc,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS, NULL, &IID_ID3D12Resource, (void **)&resource);
        ok(hr == S_OK, "Failed to create reserved texture, hr %#x.\n", hr);

        /* Map entire image */
        tile_offsets[0] = 0;
        ID3D12CommandQueue_UpdateTileMappings(context.queue, resource,
            1, NULL, NULL, heap, 1, NULL, tile_offsets, NULL, D3D12_TILE_MAPPING_FLAG_NONE);

        srv_desc.Format = DXGI_FORMAT_R32_UINT;
        srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
        srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv_desc.Texture2DArray.MostDetailedMip = 0;
        srv_desc.Texture2DArray.MipLevels = resource_desc.MipLevels;
        srv_desc.Texture2DArray.FirstArraySlice = 0;
        srv_desc.Texture2DArray.ArraySize = resource_desc.DepthOrArraySize;
        srv_desc.Texture2DArray.ResourceMinLODClamp = 0.0f;
        srv_desc.Texture2DArray.PlaneSlice = 0;
        ID3D12Device_CreateShaderResourceView(context.device, resource, &srv_desc, get_cpu_descriptor_handle(&context, gpu_heap, 0));

        reset_command_list(context.list, context.allocator);
        ID3D12GraphicsCommandList_SetDescriptorHeaps(context.list, 1, &gpu_heap);

        for (i = 0; i < (unsigned int)(resource_desc.MipLevels * resource_desc.DepthOrArraySize); i++)
        {
            UINT clear_value[4] = {};
            clear_value[0] = i;

            uav_desc.Format = DXGI_FORMAT_R32_UINT;
            uav_desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
            uav_desc.Texture2DArray.MipSlice = i % resource_desc.MipLevels;
            uav_desc.Texture2DArray.FirstArraySlice = i / resource_desc.MipLevels;
            uav_desc.Texture2DArray.ArraySize = 1;
            uav_desc.Texture2DArray.PlaneSlice = 0;
            ID3D12Device_CreateUnorderedAccessView(context.device, resource, NULL, &uav_desc, get_cpu_descriptor_handle(&context, cpu_heap, 1 + i));
            ID3D12Device_CreateUnorderedAccessView(context.device, resource, NULL, &uav_desc, get_cpu_descriptor_handle(&context, gpu_heap, 1 + i));

            ID3D12GraphicsCommandList_ClearUnorderedAccessViewUint(context.list,
                    get_gpu_descriptor_handle(&context, gpu_heap, 1 + i),
                    get_cpu_descriptor_handle(&context, cpu_heap, 1 + i),
                    resource, clear_value, 0, NULL);
        }

        transition_resource_state(context.list, readback_buffer, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        ID3D12GraphicsCommandList_SetDescriptorHeaps(context.list, 1, &gpu_heap);
        ID3D12GraphicsCommandList_SetComputeRootSignature(context.list, root_signature);
        ID3D12GraphicsCommandList_SetPipelineState(context.list, check_texture_array_pipeline);
        ID3D12GraphicsCommandList_SetComputeRootDescriptorTable(context.list, 0, get_gpu_descriptor_handle(&context, gpu_heap, 0));
        ID3D12GraphicsCommandList_SetComputeRootUnorderedAccessView(context.list, 1, readback_va);
        ID3D12GraphicsCommandList_Dispatch(context.list, 1, 1, 1);
        transition_resource_state(context.list, readback_buffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);

        get_buffer_readback_with_command_list(readback_buffer, DXGI_FORMAT_R32_UINT, &rb, context.queue, context.list);

        for (i = 0; i < (unsigned int)(resource_desc.MipLevels * resource_desc.DepthOrArraySize); i++)
        {
            set_box(&box, i, 0, 0, i + 1, 1, 1);
            check_readback_data_uint(&rb, &box, i, 0);
        }

        release_resource_readback(&rb);
        ID3D12Resource_Release(resource);
    }
    else
    {
        skip("Tiled resources tier 4 not supported.\n");
    }

    ID3D12Heap_Release(heap);

    ID3D12DescriptorHeap_Release(gpu_heap);
    ID3D12DescriptorHeap_Release(cpu_heap);
    ID3D12Resource_Release(readback_buffer);
    ID3D12PipelineState_Release(clear_texture_pipeline);
    ID3D12PipelineState_Release(check_texture_3d_pipeline);
    ID3D12PipelineState_Release(check_texture_array_pipeline);
    ID3D12PipelineState_Release(check_texture_pipeline);
    ID3D12PipelineState_Release(check_buffer_pipeline);
    ID3D12RootSignature_Release(clear_root_signature);
    ID3D12RootSignature_Release(root_signature);
    destroy_test_context(&context);
}

void test_copy_tiles(void)
{
    ID3D12Resource *tiled_resource, *dst_buffer, *src_buffer;
    D3D12_TILED_RESOURCE_COORDINATE region_offset;
    D3D12_FEATURE_DATA_D3D12_OPTIONS options;
    uint32_t tile_offset, buffer_offset;
    D3D12_TILE_REGION_SIZE region_size;
    D3D12_RESOURCE_DESC resource_desc;
    struct test_context_desc desc;
    struct resource_readback rb;
    struct test_context context;
    D3D12_HEAP_DESC heap_desc;
    uint32_t *buffer_data;
    unsigned int i, x, y;
    ID3D12Heap *heap;
    D3D12_BOX box;
    HRESULT hr;

    static const struct
    {
        uint32_t x;
        uint32_t y;
        uint32_t tile_idx;
    }
    image_tiles[] =
    {
        {1, 0, 0}, {2, 0, 1}, {1, 1, 2}, {2, 1, 3},
        {3, 1, 4}, {0, 2, 5}, {1, 2, 6},
    };

    memset(&desc, 0, sizeof(desc));
    desc.rt_width = 640;
    desc.rt_height = 480;
    desc.rt_format = DXGI_FORMAT_R8G8B8A8_UNORM;
    if (!init_test_context(&context, &desc))
        return;

    hr = ID3D12Device_CheckFeatureSupport(context.device, D3D12_FEATURE_D3D12_OPTIONS, &options, sizeof(options));
    ok(hr == S_OK, "Failed to check feature support, hr %#x.\n", hr);

    if (!options.TiledResourcesTier)
    {
        skip("Tiled resources not supported by device.\n");
        destroy_test_context(&context);
        return;
    }

    memset(&heap_desc, 0, sizeof(heap_desc));
    heap_desc.Properties.Type = D3D12_HEAP_TYPE_DEFAULT;
    heap_desc.SizeInBytes = TILE_SIZE * 16;

    resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    resource_desc.Alignment = 0;
    resource_desc.Width = heap_desc.SizeInBytes;
    resource_desc.Height = 1;
    resource_desc.DepthOrArraySize = 1;
    resource_desc.MipLevels = 1;
    resource_desc.Format = DXGI_FORMAT_UNKNOWN;
    resource_desc.SampleDesc.Count = 1;
    resource_desc.SampleDesc.Quality = 0;
    resource_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    resource_desc.Flags = D3D12_RESOURCE_FLAG_NONE;
    hr = ID3D12Device_CreateCommittedResource(context.device, &heap_desc.Properties, D3D12_HEAP_FLAG_NONE,
            &resource_desc, D3D12_RESOURCE_STATE_COPY_DEST, NULL, &IID_ID3D12Resource, (void **)&src_buffer);
    ok(hr == S_OK, "Failed to create buffer, hr %#x.\n", hr);
    hr = ID3D12Device_CreateCommittedResource(context.device, &heap_desc.Properties, D3D12_HEAP_FLAG_NONE,
            &resource_desc, D3D12_RESOURCE_STATE_COPY_DEST, NULL, &IID_ID3D12Resource, (void **)&dst_buffer);
    ok(hr == S_OK, "Failed to create buffer, hr %#x.\n", hr);

    buffer_data = malloc(resource_desc.Width);
    for (i = 0; i < resource_desc.Width / sizeof(*buffer_data); i++)
        buffer_data[i] = i;
    upload_buffer_data(src_buffer, 0, resource_desc.Width, buffer_data, context.queue, context.list);

    reset_command_list(context.list, context.allocator);
    transition_resource_state(context.list, src_buffer,
            D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COPY_SOURCE);

    /* Test buffer */
    hr = ID3D12Device_CreateReservedResource(context.device, &resource_desc,
            D3D12_RESOURCE_STATE_COPY_DEST, NULL, &IID_ID3D12Resource, (void **)&tiled_resource);
    ok(hr == S_OK, "Failed to create tiled buffer, hr %#x.\n", hr);

    heap_desc.Flags = D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS;
    hr = ID3D12Device_CreateHeap(context.device, &heap_desc, &IID_ID3D12Heap, (void **)&heap);
    ok(hr == S_OK, "Failed to create heap, hr %#x.\n", hr);

    tile_offset = 0;
    ID3D12CommandQueue_UpdateTileMappings(context.queue, tiled_resource,
            1, NULL, NULL, heap, 1, NULL, &tile_offset, NULL, D3D12_TILE_MAPPING_FLAG_NONE);

    /* Copy source tiles 0-2 with a 32-byte offset to buffer tiles 4-6 */
    set_region_offset(&region_offset, 4, 0, 0, 0);
    set_region_size(&region_size, 3, false, 0, 0, 0);

    buffer_offset = 32;

    ID3D12GraphicsCommandList_CopyTiles(context.list, tiled_resource,
            &region_offset, &region_size, src_buffer, buffer_offset,
            D3D12_TILE_COPY_FLAG_LINEAR_BUFFER_TO_SWIZZLED_TILED_RESOURCE);

    transition_resource_state(context.list, tiled_resource,
            D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COPY_SOURCE);

    get_buffer_readback_with_command_list(tiled_resource, DXGI_FORMAT_R32_UINT, &rb, context.queue, context.list);

    for (i = 0; i < 3 * TILE_SIZE / sizeof(*buffer_data); i += 1024)
    {
        uint32_t offset = i + 4 * TILE_SIZE / sizeof(*buffer_data);
        set_box(&box, offset, 0, 0, offset + 1, 1, 1);
        check_readback_data_uint(&rb, &box, buffer_data[i + buffer_offset / sizeof(*buffer_data)], 0);
    }

    release_resource_readback(&rb);

    reset_command_list(context.list, context.allocator);

    /* Read tiles 5-6 from the tiled resource */
    set_region_offset(&region_offset, 5, 0, 0, 0);
    set_region_size(&region_size, 1, false, 0, 0, 0);

    ID3D12GraphicsCommandList_CopyTiles(context.list, tiled_resource,
            &region_offset, &region_size, dst_buffer, 0,
            D3D12_TILE_COPY_FLAG_SWIZZLED_TILED_RESOURCE_TO_LINEAR_BUFFER);

    /* NONE behaves the same as SWIZZLED_TILED_RESOURCE_TO_LINEAR_BUFFER */
    set_region_offset(&region_offset, 6, 0, 0, 0);

    ID3D12GraphicsCommandList_CopyTiles(context.list, tiled_resource,
            &region_offset, &region_size, dst_buffer, TILE_SIZE,
            D3D12_TILE_COPY_FLAG_NONE);

    transition_resource_state(context.list, dst_buffer,
            D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COPY_SOURCE);

    get_buffer_readback_with_command_list(dst_buffer, DXGI_FORMAT_R32_UINT, &rb, context.queue, context.list);

    for (i = 0; i < 2 * TILE_SIZE / sizeof(*buffer_data); i += 1024)
    {
        uint32_t offset = i + (TILE_SIZE + buffer_offset) / sizeof(*buffer_data);
        set_box(&box, i, 0, 0, i + 1, 1, 1);
        check_readback_data_uint(&rb, &box, buffer_data[offset], 0);
    }

    release_resource_readback(&rb);

    ID3D12Resource_Release(tiled_resource);
    ID3D12Heap_Release(heap);

    /* Test image */
    resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    resource_desc.Alignment = 0;
    resource_desc.Width = 512;
    resource_desc.Height = 512;
    resource_desc.DepthOrArraySize = 1;
    resource_desc.MipLevels = 1;
    resource_desc.Format = DXGI_FORMAT_R32_UINT;
    resource_desc.SampleDesc.Count = 1;
    resource_desc.SampleDesc.Quality = 0;
    resource_desc.Layout = D3D12_TEXTURE_LAYOUT_64KB_UNDEFINED_SWIZZLE;
    resource_desc.Flags = D3D12_RESOURCE_FLAG_NONE;

    hr = ID3D12Device_CreateReservedResource(context.device, &resource_desc,
            D3D12_RESOURCE_STATE_COPY_DEST, NULL, &IID_ID3D12Resource, (void **)&tiled_resource);
    ok(hr == S_OK, "Failed to create tiled buffer, hr %#x.\n", hr);

    heap_desc.Flags = D3D12_HEAP_FLAG_ALLOW_ONLY_NON_RT_DS_TEXTURES;
    hr = ID3D12Device_CreateHeap(context.device, &heap_desc, &IID_ID3D12Heap, (void **)&heap);
    ok(hr == S_OK, "Failed to create heap, hr %#x.\n", hr);

    tile_offset = 0;
    ID3D12CommandQueue_UpdateTileMappings(context.queue, tiled_resource,
            1, NULL, NULL, heap, 1, NULL, &tile_offset, NULL, D3D12_TILE_MAPPING_FLAG_NONE);

    reset_command_list(context.list, context.allocator);

    /* Copy source tiles 0-3 to 2x2 region at (1,0) */
    set_region_offset(&region_offset, 1, 0, 0, 0);
    set_region_size(&region_size, 4, true, 2, 2, 1);

    ID3D12GraphicsCommandList_CopyTiles(context.list, tiled_resource,
            &region_offset, &region_size, src_buffer, 0,
            D3D12_TILE_COPY_FLAG_LINEAR_BUFFER_TO_SWIZZLED_TILED_RESOURCE);

    /* Copy source tiles 4-6 to (3,1), (0,2) and (1,2) */
    set_region_offset(&region_offset, 3, 1, 0, 0);
    set_region_size(&region_size, 3, false, 0, 0, 0);

    ID3D12GraphicsCommandList_CopyTiles(context.list, tiled_resource,
            &region_offset, &region_size, src_buffer, 4 * TILE_SIZE,
            D3D12_TILE_COPY_FLAG_LINEAR_BUFFER_TO_SWIZZLED_TILED_RESOURCE);

    transition_resource_state(context.list, tiled_resource,
            D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COPY_SOURCE);

    get_texture_readback_with_command_list(tiled_resource, 0, &rb, context.queue, context.list);

    for (i = 0; i < ARRAY_SIZE(image_tiles); i++)
    {
        for (y = 0; y < 128; y += 32)
        {
            for (x = 0; x < 128; x += 32)
            {
                uint32_t offset = image_tiles[i].tile_idx * TILE_SIZE / sizeof(*buffer_data) + 128 * y + x;
                set_box(&box, 128 * image_tiles[i].x + x, 128 * image_tiles[i].y + y, 0,
                        128 * image_tiles[i].x + x + 1, 128 * image_tiles[i].y + y + 1, 1);
                check_readback_data_uint(&rb, &box, buffer_data[offset], 0);
            }
        }
    }

    release_resource_readback(&rb);

    reset_command_list(context.list, context.allocator);

    transition_resource_state(context.list, dst_buffer,
            D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COPY_DEST);

    /* Read 0-3 to 2x2 region at (1,0) */
    set_region_offset(&region_offset, 1, 0, 0, 0);
    set_region_size(&region_size, 4, true, 2, 2, 1);

    ID3D12GraphicsCommandList_CopyTiles(context.list, tiled_resource,
            &region_offset, &region_size, dst_buffer, 0,
            D3D12_TILE_COPY_FLAG_SWIZZLED_TILED_RESOURCE_TO_LINEAR_BUFFER);

    /* Read tiles (3,1), (0,2) and (1,2) */
    set_region_offset(&region_offset, 3, 1, 0, 0);
    set_region_size(&region_size, 3, false, 0, 0, 0);

    ID3D12GraphicsCommandList_CopyTiles(context.list, tiled_resource,
            &region_offset, &region_size, dst_buffer, 4 * TILE_SIZE,
            D3D12_TILE_COPY_FLAG_NONE);

    transition_resource_state(context.list, dst_buffer,
            D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COPY_SOURCE);

    get_buffer_readback_with_command_list(dst_buffer, DXGI_FORMAT_R32_UINT, &rb, context.queue, context.list);

    for (i = 0; i < ARRAY_SIZE(image_tiles); i++)
    {
        for (x = 0; x < TILE_SIZE / sizeof(uint32_t); x += 1024)
        {
            uint32_t offset = image_tiles[i].tile_idx * TILE_SIZE / sizeof(uint32_t) + x;
            set_box(&box, offset, 0, 0, offset + 1, 1, 1);
            check_readback_data_uint(&rb, &box, buffer_data[offset], 0);
        }
    }

    release_resource_readback(&rb);

    ID3D12Resource_Release(tiled_resource);
    ID3D12Heap_Release(heap);

    ID3D12Resource_Release(src_buffer);
    ID3D12Resource_Release(dst_buffer);

    free(buffer_data);
    destroy_test_context(&context);
#undef TILE_SIZE
}

static void test_buffer_feedback_instructions(bool use_dxil)
{
#define TILE_SIZE 65536
    D3D12_TILED_RESOURCE_COORDINATE tile_regions[2];
    D3D12_ROOT_SIGNATURE_DESC root_signature_desc;
    D3D12_DESCRIPTOR_RANGE descriptor_ranges[2];
    ID3D12DescriptorHeap *cpu_heap, *gpu_heap;
    ID3D12Resource *tiled_buffer, *out_buffer;
    D3D12_UNORDERED_ACCESS_VIEW_DESC uav_desc;
    D3D12_FEATURE_DATA_D3D12_OPTIONS options;
    D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc;
    D3D12_ROOT_PARAMETER root_parameters[3];
    ID3D12PipelineState *pipeline_state;
    ID3D12RootSignature *root_signature;
    D3D12_RESOURCE_DESC resource_desc;
    struct test_context_desc desc;
    struct resource_readback rb;
    struct test_context context;
    D3D12_HEAP_DESC heap_desc;
    unsigned int i, j;
    ID3D12Heap *heap;
    UINT tile_offset;
    bool test_is_raw;
    HRESULT hr;

#include "shaders/sparse/headers/buffer_feedback_ld_typed.h"
#include "shaders/sparse/headers/buffer_feedback_ld_raw.h"
#include "shaders/sparse/headers/buffer_feedback_ld_structured.h"
#include "shaders/sparse/headers/buffer_feedback_ld_typed_uav.h"

    const struct
    {
        D3D12_SHADER_BYTECODE cs_dxbc;
        D3D12_SHADER_BYTECODE cs_dxil;
        bool is_structured;
        bool is_raw;
        bool is_uav;
    }
    tests[] =
    {
        { buffer_feedback_ld_typed_dxbc, buffer_feedback_ld_typed_dxil, false, false, false },
        { buffer_feedback_ld_typed_uav_dxbc, buffer_feedback_ld_typed_uav_dxil, false, false, true },
        { buffer_feedback_ld_raw_dxbc, buffer_feedback_ld_raw_dxil, false, true, false },
        { buffer_feedback_ld_structured_dxbc, buffer_feedback_ld_structured_dxil, true, false, false },
    };

    struct shader_args
    {
        uint32_t stride;
    }
    shader_args;

    memset(&desc, 0, sizeof(desc));
    desc.no_render_target = true;
    if (!init_test_context(&context, &desc))
        return;

    if (use_dxil && !context_supports_dxil(&context))
    {
        destroy_test_context(&context);
        return;
    }

    hr = ID3D12Device_CheckFeatureSupport(context.device, D3D12_FEATURE_D3D12_OPTIONS, &options, sizeof(options));
    ok(hr == S_OK, "Failed to check feature support, hr %#x.\n", hr);

    if (options.TiledResourcesTier < D3D12_TILED_RESOURCES_TIER_2)
    {
        skip("Tiled resources Tier 2 not supported by device.\n");
        destroy_test_context(&context);
        return;
    }

    descriptor_ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    descriptor_ranges[0].NumDescriptors = 1;
    descriptor_ranges[0].BaseShaderRegister = 0;
    descriptor_ranges[0].RegisterSpace = 0;
    descriptor_ranges[0].OffsetInDescriptorsFromTableStart = 0;
    descriptor_ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    descriptor_ranges[1].NumDescriptors = 1;
    descriptor_ranges[1].BaseShaderRegister = 1;
    descriptor_ranges[1].RegisterSpace = 0;
    descriptor_ranges[1].OffsetInDescriptorsFromTableStart = 1;
    root_parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    root_parameters[0].DescriptorTable.NumDescriptorRanges = ARRAY_SIZE(descriptor_ranges);
    root_parameters[0].DescriptorTable.pDescriptorRanges = descriptor_ranges;
    root_parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    root_parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    root_parameters[1].Descriptor.ShaderRegister = 0;
    root_parameters[1].Descriptor.RegisterSpace = 0;
    root_parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    root_parameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    root_parameters[2].Constants.Num32BitValues = sizeof(shader_args) / sizeof(uint32_t);
    root_parameters[2].Constants.ShaderRegister = 0;
    root_parameters[2].Constants.RegisterSpace = 0;
    root_parameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    root_signature_desc.NumParameters = ARRAY_SIZE(root_parameters);
    root_signature_desc.pParameters = root_parameters;
    root_signature_desc.NumStaticSamplers = 0;
    root_signature_desc.pStaticSamplers = NULL;
    root_signature_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
    hr = create_root_signature(context.device, &root_signature_desc, &root_signature);
    ok(hr == S_OK, "Failed to create root signature, hr %#x.\n", hr);

    memset(&heap_desc, 0, sizeof(heap_desc));
    heap_desc.Properties.Type = D3D12_HEAP_TYPE_DEFAULT;
    heap_desc.SizeInBytes = TILE_SIZE * 2;

    resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    resource_desc.Alignment = 0;
    resource_desc.Width = 128 * sizeof(uint32_t);
    resource_desc.Height = 1;
    resource_desc.DepthOrArraySize = 1;
    resource_desc.MipLevels = 1;
    resource_desc.Format = DXGI_FORMAT_UNKNOWN;
    resource_desc.SampleDesc.Count = 1;
    resource_desc.SampleDesc.Quality = 0;
    resource_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    resource_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    hr = ID3D12Device_CreateCommittedResource(context.device, &heap_desc.Properties, D3D12_HEAP_FLAG_NONE,
            &resource_desc, D3D12_RESOURCE_STATE_COPY_SOURCE, NULL, &IID_ID3D12Resource, (void **)&out_buffer);
    ok(hr == S_OK, "Failed to create committed resource, hr %#x.\n", hr);

    resource_desc.Width = 4 * TILE_SIZE;
    hr = ID3D12Device_CreateReservedResource(context.device, &resource_desc,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS, NULL, &IID_ID3D12Resource, (void **)&tiled_buffer);
    ok(hr == S_OK, "Failed to create reserved resource, hr %#x.\n", hr);

    hr = ID3D12Device_CreateHeap(context.device, &heap_desc, &IID_ID3D12Heap, (void **)&heap);
    ok(hr == S_OK, "Failed to create heap, hr %#x.\n", hr);

    /* Map the 0k-64k range and the 128k-192k range, leave the rest unmapped */
    set_region_offset(&tile_regions[0], 0, 0, 0, 0);
    set_region_offset(&tile_regions[1], 2, 0, 0, 0);
    tile_offset = 0;

    ID3D12CommandQueue_UpdateTileMappings(context.queue, tiled_buffer,
            ARRAY_SIZE(tile_regions), tile_regions, NULL, heap, 1, NULL,
            &tile_offset, NULL, D3D12_TILE_MAPPING_FLAG_NONE);

    gpu_heap = create_gpu_descriptor_heap(context.device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 2);
    cpu_heap = create_cpu_descriptor_heap(context.device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1);

    uav_desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    uav_desc.Format = DXGI_FORMAT_R32_UINT;
    uav_desc.Buffer.FirstElement = 0;
    uav_desc.Buffer.NumElements = resource_desc.Width / sizeof(uint32_t);
    uav_desc.Buffer.StructureByteStride = 0;
    uav_desc.Buffer.CounterOffsetInBytes = 0;
    uav_desc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;

    ID3D12Device_CreateUnorderedAccessView(context.device, tiled_buffer, NULL, &uav_desc, get_cpu_descriptor_handle(&context, cpu_heap, 0));
    ID3D12Device_CreateUnorderedAccessView(context.device, tiled_buffer, NULL, &uav_desc, get_cpu_descriptor_handle(&context, gpu_heap, 1));

    for (i = 0; i < resource_desc.Width / TILE_SIZE; i++)
    {
        UINT clear_values[] = { i + 1, 0, 0, 0 };
        D3D12_RECT rect;
        set_rect(&rect, i * TILE_SIZE / sizeof(uint32_t), 0, (i + 1) * TILE_SIZE / sizeof(uint32_t), 1);

        ID3D12GraphicsCommandList_ClearUnorderedAccessViewUint(context.list,
                get_gpu_descriptor_handle(&context, gpu_heap, 1),
                get_cpu_descriptor_handle(&context, cpu_heap, 0),
                tiled_buffer, clear_values, 1, &rect);
    };

    transition_resource_state(context.list, tiled_buffer,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

    for (i = 0; i < ARRAY_SIZE(tests); i++)
    {
        vkd3d_test_set_context("Test %u", i);
        test_is_raw = (i == 2) || (i == 3);

        todo_if(use_dxil && test_is_raw)
        pipeline_state = create_compute_pipeline_state(context.device,
                root_signature, use_dxil ? tests[i].cs_dxil : tests[i].cs_dxbc);

        /* This will fail for SSBO buffer feedback case on DXIL. */
        todo_if(use_dxil && test_is_raw)
        ok(!!pipeline_state, "Failed to create pipeline state.\n");
        if (!pipeline_state)
            continue;

        srv_desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        srv_desc.Format = tests[i].is_structured ? DXGI_FORMAT_UNKNOWN :
            (tests[i].is_raw ? DXGI_FORMAT_R32_TYPELESS : DXGI_FORMAT_R32_UINT);
        srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv_desc.Buffer.FirstElement = 0;
        srv_desc.Buffer.NumElements = resource_desc.Width / sizeof(uint32_t);
        srv_desc.Buffer.StructureByteStride = tests[i].is_structured ? sizeof(uint32_t) : 0;
        srv_desc.Buffer.Flags = tests[i].is_raw ? D3D12_BUFFER_SRV_FLAG_RAW : D3D12_BUFFER_SRV_FLAG_NONE;

        uav_desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        uav_desc.Format = srv_desc.Format;
        uav_desc.Buffer.FirstElement = srv_desc.Buffer.FirstElement;
        uav_desc.Buffer.NumElements = srv_desc.Buffer.NumElements;
        uav_desc.Buffer.StructureByteStride = srv_desc.Buffer.StructureByteStride;
        uav_desc.Buffer.CounterOffsetInBytes = 0;
        uav_desc.Buffer.Flags = tests[i].is_raw ? D3D12_BUFFER_UAV_FLAG_RAW : D3D12_BUFFER_UAV_FLAG_NONE;

        if (tests[i].is_uav)
        {
            ID3D12Device_CreateUnorderedAccessView(context.device, tiled_buffer, NULL, &uav_desc, get_cpu_descriptor_handle(&context, gpu_heap, 1));
            transition_resource_state(context.list, tiled_buffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        }
        else
            ID3D12Device_CreateShaderResourceView(context.device, tiled_buffer, &srv_desc, get_cpu_descriptor_handle(&context, gpu_heap, 0));

        transition_resource_state(context.list, out_buffer,
        D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

        shader_args.stride = resource_desc.Width / 64;

        if (!tests[i].is_raw)
            shader_args.stride /= sizeof(uint32_t);

        ID3D12GraphicsCommandList_SetDescriptorHeaps(context.list, 1, &gpu_heap);
        ID3D12GraphicsCommandList_SetComputeRootSignature(context.list, root_signature);
        ID3D12GraphicsCommandList_SetPipelineState(context.list, pipeline_state);
        ID3D12GraphicsCommandList_SetComputeRootDescriptorTable(context.list, 0, get_gpu_descriptor_handle(&context, gpu_heap, 0));
        ID3D12GraphicsCommandList_SetComputeRootUnorderedAccessView(context.list, 1, ID3D12Resource_GetGPUVirtualAddress(out_buffer));
        ID3D12GraphicsCommandList_SetComputeRoot32BitConstants(context.list, 2, sizeof(shader_args) / sizeof(uint32_t), &shader_args, 0);
        ID3D12GraphicsCommandList_Dispatch(context.list, 1, 1, 1);

        transition_resource_state(context.list, out_buffer,
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);

        if (tests[i].is_uav)
        {
            transition_resource_state(context.list, tiled_buffer,
                    D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        }

        get_buffer_readback_with_command_list(out_buffer, DXGI_FORMAT_R32_UINT, &rb, context.queue, context.list);

        for (j = 0; j < 64; j++)
        {
            D3D12_BOX box;
            UINT tile_index = j / 16;

            set_box(&box, 2 * j, 0, 0, 2 * j + 1, 1, 1);
            todo_if(test_is_raw) check_readback_data_uint(&rb, &box, (tile_index & 1) ? 0 : (tile_index + 1), 0);
            set_box(&box, 2 * j + 1, 0, 0, 2 * j + 2, 1, 1);
            todo_if(test_is_raw) check_readback_data_uint(&rb, &box, (tile_index & 1) ? 0 : 1, 0);
        }

        release_resource_readback(&rb);

        reset_command_list(context.list, context.allocator);

        ID3D12PipelineState_Release(pipeline_state);
    }

    ID3D12DescriptorHeap_Release(cpu_heap);
    ID3D12DescriptorHeap_Release(gpu_heap);
    ID3D12Heap_Release(heap);
    ID3D12Resource_Release(tiled_buffer);
    ID3D12Resource_Release(out_buffer);
    ID3D12RootSignature_Release(root_signature);
    destroy_test_context(&context);
#undef TILE_SIZE
}

void test_buffer_feedback_instructions_sm51(void)
{
    test_buffer_feedback_instructions(false);
}

void test_buffer_feedback_instructions_dxil(void)
{
    test_buffer_feedback_instructions(true);
}

static void test_texture_feedback_instructions(bool use_dxil)
{
#define TILE_SIZE 65536
    ID3D12DescriptorHeap *gpu_heap, *sampler_heap, *rtv_heap;
    ID3D12Resource *tiled_image, *color_rt, *residency_rt;
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pipeline_desc;
    D3D12_TILED_RESOURCE_COORDINATE tile_regions[3];
    D3D12_ROOT_SIGNATURE_DESC root_signature_desc;
    D3D12_DESCRIPTOR_RANGE descriptor_ranges[3];
    D3D12_UNORDERED_ACCESS_VIEW_DESC uav_desc;
    D3D12_FEATURE_DATA_D3D12_OPTIONS options;
    D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc;
    D3D12_ROOT_PARAMETER root_parameters[3];
    D3D12_RENDER_TARGET_VIEW_DESC rtv_desc;
    ID3D12PipelineState *pipeline_state;
    ID3D12RootSignature *root_signature;
    D3D12_RESOURCE_DESC resource_desc;
    D3D12_SAMPLER_DESC sampler_desc;
    struct test_context_desc desc;
    struct resource_readback rb;
    struct test_context context;
    D3D12_HEAP_DESC heap_desc;
    D3D12_VIEWPORT viewport;
    D3D12_RECT scissor;
    ID3D12Heap *heap;
    UINT tile_offset;
    unsigned int i;
    HRESULT hr;

#include "shaders/sparse/headers/texture_feedback_vs.h"
#include "shaders/sparse/headers/texture_feedback_sample.h"
#include "shaders/sparse/headers/texture_feedback_sample_bias.h"
#include "shaders/sparse/headers/texture_feedback_sample_grad.h"
#include "shaders/sparse/headers/texture_feedback_sample_lod.h"
#include "shaders/sparse/headers/texture_feedback_gather.h"
#include "shaders/sparse/headers/texture_feedback_gather_po.h"
#include "shaders/sparse/headers/texture_feedback_ld.h"
#include "shaders/sparse/headers/texture_feedback_ld_uav.h"

    struct shader_args
    {
        float args[4];
    };

    const struct
    {
        D3D12_SHADER_BYTECODE ps_dxbc;
        D3D12_SHADER_BYTECODE ps_dxil;
        uint32_t expected_mip;
        struct shader_args args;
        bool is_gather;
    }
    tests[] =
    {
        { texture_feedback_sample_dxbc, texture_feedback_sample_dxil,             0, {{0.0f}} },
        { texture_feedback_sample_dxbc, texture_feedback_sample_dxil,             1, {{1.0f}} },
        { texture_feedback_sample_bias_dxbc, texture_feedback_sample_bias_dxil,   0, {{0.0f, 0.0f}} },
        { texture_feedback_sample_bias_dxbc, texture_feedback_sample_bias_dxil,   1, {{1.0f, 0.0f}} },
        { texture_feedback_sample_bias_dxbc, texture_feedback_sample_bias_dxil,   1, {{0.0f, 1.0f}} },
        { texture_feedback_sample_grad_dxbc, texture_feedback_sample_grad_dxil,   0, {{0.0f, 0.0f, 0.0f}} },
        { texture_feedback_sample_grad_dxbc, texture_feedback_sample_grad_dxil,   1, {{0.0f, 1.0f, 1.0f}} },
        { texture_feedback_sample_lod_dxbc, texture_feedback_sample_lod_dxil,     0, {{0.0f}} },
        { texture_feedback_sample_lod_dxbc, texture_feedback_sample_lod_dxil,     1, {{1.0f}} },
        { texture_feedback_gather_dxbc, texture_feedback_gather_dxil,             0, {{0.0f}}, true },
        { texture_feedback_gather_po_dxbc, texture_feedback_gather_po_dxil,       0, {{0.0f, 0.0f}}, true },
        { texture_feedback_ld_dxbc, texture_feedback_ld_dxil,                     0, {{0.0f}} },
        { texture_feedback_ld_uav_dxbc, texture_feedback_ld_uav_dxil,             0, {{0.0f}} },
    };

    static const FLOAT clear_colors[2][4] = {
        { 0.2f, 0.4f, 0.6f, 0.8f },
        { 0.5f, 0.5f, 0.5f, 0.5f },
    };

    memset(&desc, 0, sizeof(desc));
    desc.no_render_target = true;
    if (!init_test_context(&context, &desc))
        return;

    if (use_dxil && !context_supports_dxil(&context))
    {
        destroy_test_context(&context);
        return;
    }

    hr = ID3D12Device_CheckFeatureSupport(context.device, D3D12_FEATURE_D3D12_OPTIONS, &options, sizeof(options));
    ok(hr == S_OK, "Failed to check feature support, hr %#x.\n", hr);

    if (options.TiledResourcesTier < D3D12_TILED_RESOURCES_TIER_2)
    {
        skip("Tiled resources Tier 2 not supported by device.\n");
        destroy_test_context(&context);
        return;
    }

    descriptor_ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    descriptor_ranges[0].NumDescriptors = 1;
    descriptor_ranges[0].BaseShaderRegister = 0;
    descriptor_ranges[0].RegisterSpace = 0;
    descriptor_ranges[0].OffsetInDescriptorsFromTableStart = 0;
    descriptor_ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    descriptor_ranges[1].NumDescriptors = 1;
    descriptor_ranges[1].BaseShaderRegister = 2;
    descriptor_ranges[1].RegisterSpace = 0;
    descriptor_ranges[1].OffsetInDescriptorsFromTableStart = 1;
    descriptor_ranges[2].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
    descriptor_ranges[2].NumDescriptors = 1;
    descriptor_ranges[2].BaseShaderRegister = 0;
    descriptor_ranges[2].RegisterSpace = 0;
    descriptor_ranges[2].OffsetInDescriptorsFromTableStart = 0;
    root_parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    root_parameters[0].DescriptorTable.NumDescriptorRanges = 2;
    root_parameters[0].DescriptorTable.pDescriptorRanges = &descriptor_ranges[0];
    root_parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    root_parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    root_parameters[1].DescriptorTable.NumDescriptorRanges = 1;
    root_parameters[1].DescriptorTable.pDescriptorRanges = &descriptor_ranges[2];
    root_parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    root_parameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    root_parameters[2].Constants.Num32BitValues = sizeof(struct shader_args) / sizeof(uint32_t);
    root_parameters[2].Constants.ShaderRegister = 0;
    root_parameters[2].Constants.RegisterSpace = 0;
    root_parameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    root_signature_desc.NumParameters = ARRAY_SIZE(root_parameters);
    root_signature_desc.pParameters = root_parameters;
    root_signature_desc.NumStaticSamplers = 0;
    root_signature_desc.pStaticSamplers = NULL;
    root_signature_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
    hr = create_root_signature(context.device, &root_signature_desc, &root_signature);
    ok(hr == S_OK, "Failed to create root signature, hr %#x.\n", hr);

    memset(&heap_desc, 0, sizeof(heap_desc));
    heap_desc.Properties.Type = D3D12_HEAP_TYPE_DEFAULT;
    heap_desc.SizeInBytes = TILE_SIZE * 3;

    resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    resource_desc.Alignment = 0;
    resource_desc.Width = 256;
    resource_desc.Height = 256;
    resource_desc.DepthOrArraySize = 1;
    resource_desc.MipLevels = 1;
    resource_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    resource_desc.SampleDesc.Count = 1;
    resource_desc.SampleDesc.Quality = 0;
    resource_desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    resource_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    hr = ID3D12Device_CreateCommittedResource(context.device, &heap_desc.Properties, D3D12_HEAP_FLAG_NONE,
            &resource_desc, D3D12_RESOURCE_STATE_RENDER_TARGET, NULL, &IID_ID3D12Resource, (void **)&color_rt);
    ok(hr == S_OK, "Failed to create committed resource, hr %#x.\n", hr);

    resource_desc.Format = DXGI_FORMAT_R32_UINT;
    hr = ID3D12Device_CreateCommittedResource(context.device, &heap_desc.Properties, D3D12_HEAP_FLAG_NONE,
            &resource_desc, D3D12_RESOURCE_STATE_RENDER_TARGET, NULL, &IID_ID3D12Resource, (void **)&residency_rt);
    ok(hr == S_OK, "Failed to create committed resource, hr %#x.\n", hr);

    resource_desc.MipLevels = 2;
    resource_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    resource_desc.Layout = D3D12_TEXTURE_LAYOUT_64KB_UNDEFINED_SWIZZLE;
    resource_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    hr = ID3D12Device_CreateReservedResource(context.device, &resource_desc,
            D3D12_RESOURCE_STATE_RENDER_TARGET, NULL, &IID_ID3D12Resource, (void **)&tiled_image);
    ok(hr == S_OK, "Failed to create reserved resource, hr %#x.\n", hr);

    hr = ID3D12Device_CreateHeap(context.device, &heap_desc, &IID_ID3D12Heap, (void **)&heap);
    ok(hr == S_OK, "Failed to create heap, hr %#x.\n", hr);

    /* Map top-left + bottom-right of mip 0 + entire mip 1 */
    set_region_offset(&tile_regions[0], 0, 0, 0, 0);
    set_region_offset(&tile_regions[1], 1, 1, 0, 0);
    set_region_offset(&tile_regions[2], 0, 0, 0, 1);
    tile_offset = 0;

    ID3D12CommandQueue_UpdateTileMappings(context.queue, tiled_image,
            ARRAY_SIZE(tile_regions), tile_regions, NULL, heap, 1, NULL,
            &tile_offset, NULL, D3D12_TILE_MAPPING_FLAG_NONE);

    rtv_heap = create_cpu_descriptor_heap(context.device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 4);
    gpu_heap = create_gpu_descriptor_heap(context.device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 2);
    sampler_heap = create_gpu_descriptor_heap(context.device, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, 1);

    rtv_desc.Format = DXGI_FORMAT_UNKNOWN;
    rtv_desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
    rtv_desc.Texture2D.MipSlice = 0;
    rtv_desc.Texture2D.PlaneSlice = 0;

    ID3D12Device_CreateRenderTargetView(context.device, color_rt, &rtv_desc, get_cpu_rtv_handle(&context, rtv_heap, 0));
    ID3D12Device_CreateRenderTargetView(context.device, residency_rt, &rtv_desc, get_cpu_rtv_handle(&context, rtv_heap, 1));
    ID3D12Device_CreateRenderTargetView(context.device, tiled_image, &rtv_desc, get_cpu_rtv_handle(&context, rtv_heap, 2));

    rtv_desc.Texture2D.MipSlice = 1;
    ID3D12Device_CreateRenderTargetView(context.device, tiled_image, &rtv_desc, get_cpu_rtv_handle(&context, rtv_heap, 3));

    ID3D12GraphicsCommandList_ClearRenderTargetView(context.list, get_cpu_rtv_handle(&context, rtv_heap, 2), clear_colors[0], 0, NULL);
    ID3D12GraphicsCommandList_ClearRenderTargetView(context.list, get_cpu_rtv_handle(&context, rtv_heap, 3), clear_colors[1], 0, NULL);
    transition_resource_state(context.list, tiled_image, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

    srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv_desc.Texture2D.MostDetailedMip = 0;
    srv_desc.Texture2D.MipLevels = ~0u;
    srv_desc.Texture2D.PlaneSlice = 0;
    srv_desc.Texture2D.ResourceMinLODClamp = 0;
    ID3D12Device_CreateShaderResourceView(context.device, tiled_image, &srv_desc, get_cpu_descriptor_handle(&context, gpu_heap, 0));

    uav_desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    uav_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    uav_desc.Texture2D.MipSlice = 0;
    uav_desc.Texture2D.PlaneSlice = 0;
    ID3D12Device_CreateUnorderedAccessView(context.device, tiled_image, NULL, &uav_desc, get_cpu_descriptor_handle(&context, gpu_heap, 1));

    memset(&sampler_desc, 0, sizeof(sampler_desc));
    sampler_desc.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
    sampler_desc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler_desc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler_desc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler_desc.MipLODBias = 0.0f;
    sampler_desc.MaxAnisotropy = 1;
    sampler_desc.MinLOD = -16.0f;
    sampler_desc.MaxLOD = 16.0f;
    ID3D12Device_CreateSampler(context.device, &sampler_desc, get_cpu_sampler_handle(&context, sampler_heap, 0));

    memset(&pipeline_desc, 0, sizeof(pipeline_desc));
    pipeline_desc.pRootSignature = root_signature;
    pipeline_desc.VS = use_dxil ? texture_feedback_vs_dxil : texture_feedback_vs_dxbc;
    pipeline_desc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    pipeline_desc.BlendState.RenderTarget[1].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    pipeline_desc.SampleMask = 0xFFFFFFFF;
    pipeline_desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    pipeline_desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    pipeline_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pipeline_desc.NumRenderTargets = 2;
    pipeline_desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    pipeline_desc.RTVFormats[1] = DXGI_FORMAT_R32_UINT;
    pipeline_desc.SampleDesc.Count = 1;

    set_viewport(&viewport, 0.0f, 0.0f, 256.0f, 256.0f, 0.0f, 1.0f);
    set_rect(&scissor, 0, 0, 256, 256);

    for (i = 0; i < ARRAY_SIZE(tests); i++)
    {
        D3D12_CPU_DESCRIPTOR_HANDLE rt_handle = get_cpu_rtv_handle(&context, rtv_heap, 0);
        ID3D12DescriptorHeap *descriptor_heaps[2] = { gpu_heap, sampler_heap };
        const FLOAT clear_residency_rt[] = { 100.0f, 0.0f, 0.0f, 0.0f };
        const FLOAT clear_color_rt[] = { 1.0f, 1.0f, 1.0f, 1.0f };
        unsigned int color, expected_a, expected_b;
        vkd3d_test_set_context("Test %u", i);

        expected_a = tests[i].expected_mip ? 0x80808080 : (tests[i].is_gather ? 0x33333333 : 0xcc996633);
        expected_b = tests[i].expected_mip ? expected_a : 0;
        
        pipeline_desc.PS = use_dxil ? tests[i].ps_dxil : tests[i].ps_dxbc;

        hr = ID3D12Device_CreateGraphicsPipelineState(context.device, &pipeline_desc, &IID_ID3D12PipelineState, (void **)&pipeline_state);
        ok(hr == S_OK, "Failed to compile graphics pipeline, hr %#x.\n", hr);

        ID3D12GraphicsCommandList_OMSetRenderTargets(context.list, 2, &rt_handle, true, NULL);
        ID3D12GraphicsCommandList_ClearRenderTargetView(context.list, get_cpu_rtv_handle(&context, rtv_heap, 1), clear_residency_rt, 0, NULL);
        ID3D12GraphicsCommandList_ClearRenderTargetView(context.list, get_cpu_rtv_handle(&context, rtv_heap, 0), clear_color_rt, 0, NULL);
        ID3D12GraphicsCommandList_IASetPrimitiveTopology(context.list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ID3D12GraphicsCommandList_RSSetViewports(context.list, 1, &viewport);
        ID3D12GraphicsCommandList_RSSetScissorRects(context.list, 1, &scissor);
        ID3D12GraphicsCommandList_SetDescriptorHeaps(context.list, ARRAY_SIZE(descriptor_heaps), descriptor_heaps);
        ID3D12GraphicsCommandList_SetGraphicsRootSignature(context.list, root_signature);
        ID3D12GraphicsCommandList_SetPipelineState(context.list, pipeline_state);
        ID3D12GraphicsCommandList_SetGraphicsRootDescriptorTable(context.list, 0, get_gpu_descriptor_handle(&context, gpu_heap, 0));
        ID3D12GraphicsCommandList_SetGraphicsRootDescriptorTable(context.list, 1, get_gpu_sampler_handle(&context, sampler_heap, 0));
        ID3D12GraphicsCommandList_SetGraphicsRoot32BitConstants(context.list, 2, sizeof(tests[i].args) / sizeof(uint32_t), tests[i].args.args, 0);
        ID3D12GraphicsCommandList_DrawInstanced(context.list, 3, 1, 0, 0);

        transition_resource_state(context.list, color_rt, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
        transition_resource_state(context.list, residency_rt, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);

        get_texture_readback_with_command_list(color_rt, 0, &rb, context.queue, context.list);

        color = get_readback_uint(&rb, 64, 64, 0);
        ok(compare_color(color, expected_a, 1), "Got color 0x%08x, expected %08x.\n", color, expected_a);
        color = get_readback_uint(&rb, 192, 64, 0);
        ok(compare_color(color, expected_b, 1), "Got color 0x%08x, expected %08x.\n", color, expected_b);
        color = get_readback_uint(&rb, 64, 192, 0);
        ok(compare_color(color, expected_b, 1), "Got color 0x%08x, expected %08x.\n", color, expected_b);
        color = get_readback_uint(&rb, 192, 192, 0);
        ok(compare_color(color, expected_a, 1), "Got color 0x%08x, expected %08x.\n", color, expected_a);

        release_resource_readback(&rb);
        reset_command_list(context.list, context.allocator);

        get_texture_readback_with_command_list(residency_rt, 0, &rb, context.queue, context.list);

        color = get_readback_uint(&rb, 64, 64, 0);
        ok(compare_color(color, !!expected_a, 0), "Got residency %#x, expected %#x.\n", color, !!expected_a);
        color = get_readback_uint(&rb, 192, 64, 0);
        ok(compare_color(color, !!expected_b, 0), "Got residency %#x, expected %#x.\n", color, !!expected_b);
        color = get_readback_uint(&rb, 64, 192, 0);
        ok(compare_color(color, !!expected_b, 0), "Got residency %#x, expected %#x.\n", color, !!expected_b);
        color = get_readback_uint(&rb, 192, 192, 0);
        ok(compare_color(color, !!expected_a, 0), "Got residency %#x, expected %#x.\n", color, !!expected_a);

        release_resource_readback(&rb);
        reset_command_list(context.list, context.allocator);

        transition_resource_state(context.list, color_rt, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
        transition_resource_state(context.list, residency_rt, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);

        ID3D12PipelineState_Release(pipeline_state);
    }

    ID3D12DescriptorHeap_Release(rtv_heap);
    ID3D12DescriptorHeap_Release(gpu_heap);
    ID3D12DescriptorHeap_Release(sampler_heap);
    ID3D12Heap_Release(heap);
    ID3D12Resource_Release(tiled_image);
    ID3D12Resource_Release(color_rt);
    ID3D12Resource_Release(residency_rt);
    ID3D12RootSignature_Release(root_signature);
    destroy_test_context(&context);
}

void test_texture_feedback_instructions_sm51(void)
{
    test_texture_feedback_instructions(false);
}

void test_texture_feedback_instructions_dxil(void)
{
    test_texture_feedback_instructions(true);
}

void test_sparse_buffer_memory_lifetime(void)
{
    /* Attempt to bind sparse memory, then free the underlying heap, but keep the sparse resource
     * alive. This should confuse drivers that attempt to track BO lifetimes. */
    D3D12_UNORDERED_ACCESS_VIEW_DESC uav_desc;
    D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc;
    D3D12_FEATURE_DATA_D3D12_OPTIONS options;
    const UINT values[] = { 42, 42, 42, 42 };
    D3D12_ROOT_PARAMETER root_parameters[2];
    D3D12_TILE_REGION_SIZE region_size;
    D3D12_GPU_DESCRIPTOR_HANDLE h_gpu;
    D3D12_CPU_DESCRIPTOR_HANDLE h_cpu;
    D3D12_ROOT_SIGNATURE_DESC rs_desc;
    D3D12_DESCRIPTOR_RANGE desc_range;
    struct test_context context;
    struct resource_readback rb;
    ID3D12DescriptorHeap *cpu;
    ID3D12DescriptorHeap *gpu;
    D3D12_HEAP_DESC heap_desc;
    D3D12_RESOURCE_DESC desc;
    ID3D12Resource *sparse;
    ID3D12Resource *buffer;
    ID3D12Heap *heap_live;
    ID3D12Heap *heap;
    unsigned int i;
    HRESULT hr;

#include "shaders/sparse/headers/sparse_query.h"

    if (!init_compute_test_context(&context))
        return;

    hr = ID3D12Device_CheckFeatureSupport(context.device, D3D12_FEATURE_D3D12_OPTIONS, &options, sizeof(options));
    ok(hr == S_OK, "Failed to check feature support, hr %#x.\n", hr);

    if (options.TiledResourcesTier < D3D12_TILED_RESOURCES_TIER_2)
    {
        skip("Tiled resources Tier 2 not supported by device.\n");
        destroy_test_context(&context);
        return;
    }

    memset(&rs_desc, 0, sizeof(rs_desc));
    memset(root_parameters, 0, sizeof(root_parameters));
    memset(&desc_range, 0, sizeof(desc_range));
    rs_desc.NumParameters = ARRAY_SIZE(root_parameters);
    rs_desc.pParameters = root_parameters;
    root_parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    root_parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    root_parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    root_parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    root_parameters[1].DescriptorTable.NumDescriptorRanges = 1;
    root_parameters[1].DescriptorTable.pDescriptorRanges = &desc_range;
    desc_range.NumDescriptors = 1;
    desc_range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    create_root_signature(context.device, &rs_desc, &context.root_signature);
    context.pipeline_state = create_compute_pipeline_state(context.device, context.root_signature, sparse_query_dxbc);

    memset(&heap_desc, 0, sizeof(heap_desc));
    heap_desc.SizeInBytes = 4 * 1024 * 1024;
    heap_desc.Properties.Type = D3D12_HEAP_TYPE_DEFAULT;
    heap_desc.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
    heap_desc.Flags = D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS;
    hr = ID3D12Device_CreateHeap(context.device, &heap_desc, &IID_ID3D12Heap, (void**)&heap);
    ok(SUCCEEDED(hr), "Failed to create heap, hr #%x.\n", hr);
    hr = ID3D12Device_CreateHeap(context.device, &heap_desc, &IID_ID3D12Heap, (void**)&heap_live);
    ok(SUCCEEDED(hr), "Failed to create heap, hr #%x.\n", hr);

    memset(&desc, 0, sizeof(desc));
    desc.Width = 64 * 1024 * 1024;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.SampleDesc.Count = 1;
    desc.Format = DXGI_FORMAT_UNKNOWN;
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.MipLevels = 1;
    desc.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    hr = ID3D12Device_CreateReservedResource(context.device, &desc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
            NULL, &IID_ID3D12Resource, (void**)&sparse);
    ok(SUCCEEDED(hr), "Failed to create reserved resource, hr #%x.\n", hr);

    {
        const D3D12_TILED_RESOURCE_COORDINATE region_start_coordinate = { 0 };
        const D3D12_TILE_RANGE_FLAGS range_flag = D3D12_TILE_RANGE_FLAG_NULL;
        const UINT offset = 0;
        const UINT count = desc.Width / (64 * 1024);
        region_size.UseBox = FALSE;
        region_size.NumTiles = desc.Width / (64 * 1024);
        ID3D12CommandQueue_UpdateTileMappings(context.queue, sparse, 1, &region_start_coordinate, &region_size,
                NULL, 1, &range_flag, &offset, &count, D3D12_TILE_MAPPING_FLAG_NONE);
    }

    region_size.UseBox = FALSE;
    region_size.NumTiles = 1;

    for (i = 0; i < 2; i++)
    {
        const D3D12_TILED_RESOURCE_COORDINATE region_start_coordinate = { i, 0, 0, 0 };
        const D3D12_TILE_RANGE_FLAGS range_flag = D3D12_TILE_RANGE_FLAG_NONE;
        const UINT offset = i;
        const UINT count = 1;

        ID3D12CommandQueue_UpdateTileMappings(context.queue, sparse, 1, &region_start_coordinate, &region_size,
                i == 0 ? heap : heap_live, 1, &range_flag, &offset, &count, D3D12_TILE_MAPPING_FLAG_NONE);
    }
    wait_queue_idle(context.device, context.queue);

    buffer = create_default_buffer(context.device, 128 * 1024,
            D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_DEST);
    cpu = create_cpu_descriptor_heap(context.device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1);
    gpu = create_gpu_descriptor_heap(context.device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 2);
    memset(&uav_desc, 0, sizeof(uav_desc));
    uav_desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    uav_desc.Format = DXGI_FORMAT_R32_UINT;
    uav_desc.Buffer.NumElements = 128 * 1024 / 4;
    uav_desc.Buffer.FirstElement = 0;
    ID3D12Device_CreateUnorderedAccessView(context.device, sparse, NULL, &uav_desc,
            ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(cpu));
    ID3D12Device_CreateUnorderedAccessView(context.device, sparse, NULL, &uav_desc,
            ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(gpu));

    memset(&srv_desc, 0, sizeof(srv_desc));
    srv_desc.Buffer.FirstElement = 0;
    srv_desc.Buffer.NumElements = 2 * 1024 * 1024;
    srv_desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    srv_desc.Format = DXGI_FORMAT_R32_UINT;
    srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

    h_cpu = ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(gpu);
    h_cpu.ptr += ID3D12Device_GetDescriptorHandleIncrementSize(context.device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    ID3D12Device_CreateShaderResourceView(context.device, sparse, &srv_desc, h_cpu);

    ID3D12GraphicsCommandList_SetDescriptorHeaps(context.list, 1, &gpu);
    ID3D12GraphicsCommandList_ClearUnorderedAccessViewUint(context.list,
            ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(gpu),
            ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(cpu), sparse, values, 0, NULL);
    transition_resource_state(context.list, sparse,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
    ID3D12GraphicsCommandList_CopyBufferRegion(context.list, buffer, 0, sparse, 0, 128 * 1024);
    transition_resource_state(context.list, buffer,
            D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COPY_SOURCE);
    get_buffer_readback_with_command_list(buffer, DXGI_FORMAT_R32_UINT,
            &rb, context.queue, context.list);
    reset_command_list(context.list, context.allocator);
    ok(get_readback_uint(&rb, 0, 0, 0) == 42, "Got #%x, expected 42.\n", get_readback_uint(&rb, 0, 0, 0));
    ok(get_readback_uint(&rb, 64 * 1024 / 4, 0, 0) == 42, "Got #%x, expected 42.\n", get_readback_uint(&rb, 64 * 1024 / 4, 0, 0));
    release_resource_readback(&rb);

    ID3D12Heap_Release(heap);

    /* Access a resource where we can hypothetically access the freed heap memory. */
    /* On AMD Windows native at least, if we read the freed region, we read garbage, which proves it's not required to unbind explicitly.
     * We'd read 0 in that case. */
    ID3D12GraphicsCommandList_CopyBufferRegion(context.list, buffer, 0, sparse, 64 * 1024, 64 * 1024);

#define EXPLORE_UNDEFINED_BEHAVIOR 0

#if EXPLORE_UNDEFINED_BEHAVIOR
    /* This reads unmapped memory. */
    ID3D12GraphicsCommandList_CopyBufferRegion(context.list, buffer, 1024, sparse, 1024, 1024);
#endif

    transition_resource_state(context.list, buffer, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    h_gpu = ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(gpu);
    h_gpu.ptr += ID3D12Device_GetDescriptorHandleIncrementSize(context.device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    ID3D12GraphicsCommandList_SetDescriptorHeaps(context.list, 1, &gpu);
    ID3D12GraphicsCommandList_SetComputeRootSignature(context.list, context.root_signature);
    ID3D12GraphicsCommandList_SetPipelineState(context.list, context.pipeline_state);
    ID3D12GraphicsCommandList_SetComputeRootUnorderedAccessView(context.list, 0, ID3D12Resource_GetGPUVirtualAddress(buffer));
    ID3D12GraphicsCommandList_SetComputeRootDescriptorTable(context.list, 1, h_gpu);
#if EXPLORE_UNDEFINED_BEHAVIOR
    ID3D12GraphicsCommandList_Dispatch(context.list, 1, 1, 1);
#endif

    transition_resource_state(context.list, buffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);

    get_buffer_readback_with_command_list(buffer, DXGI_FORMAT_R32_UINT,
            &rb, context.queue, context.list);

#if EXPLORE_UNDEFINED_BEHAVIOR
    skip("Reading undefined value #%x.\n", get_readback_uint(&rb, 0, 0, 0));
    skip("Reading value #%x (expect 0).\n", get_readback_uint(&rb, 1, 0, 0));
    skip("Reading undefined value #%x.\n", get_readback_uint(&rb, 1024 / 4, 0, 0));
#endif
    ok(get_readback_uint(&rb, 2048 / 4, 0, 0) == 42, "Got #%x, expected 42.\n", get_readback_uint(&rb, 2048 / 4, 0, 0));
    ok(get_readback_uint(&rb, 64 * 1024 / 4, 0, 0) == 42, "Got #%x, expected 42.\n", get_readback_uint(&rb, 64 * 1024 / 4, 0, 0));
    release_resource_readback(&rb);

    ID3D12Resource_Release(buffer);
    ID3D12Resource_Release(sparse);
    ID3D12DescriptorHeap_Release(cpu);
    ID3D12DescriptorHeap_Release(gpu);
    ID3D12Heap_Release(heap_live);
    destroy_test_context(&context);
}

void test_reserved_resource_mapping(void)
{
    struct test_context context;
    D3D12_FEATURE_DATA_D3D12_OPTIONS options;
    D3D12_RESOURCE_DESC desc;
    ID3D12Resource *res;
    HRESULT hr;
    void *ptr;

    if (!init_compute_test_context(&context))
        return;

    hr = ID3D12Device_CheckFeatureSupport(context.device, D3D12_FEATURE_D3D12_OPTIONS, &options, sizeof(options));
    ok(hr == S_OK, "Failed to check feature support, hr %#x.\n", hr);

    if (!options.TiledResourcesTier)
    {
        skip("Tiled resources not supported by device.\n");
        destroy_test_context(&context);
        return;
    }


    memset(&desc, 0, sizeof(desc));
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.MipLevels = 1;
    desc.DepthOrArraySize = 1;
    desc.Height = 1;
    desc.Width = 64 * 1024;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    desc.SampleDesc.Count = 1;
    hr = ID3D12Device_CreateReservedResource(context.device, &desc, D3D12_RESOURCE_STATE_COMMON, NULL, &IID_ID3D12Resource, (void **)&res);
    ok(SUCCEEDED(hr), "Failed to create reserved resource, hr #%x.\n", hr);

    hr = ID3D12Resource_Map(res, 0, NULL, &ptr);
    ok(hr == E_INVALIDARG, "Unexpected return value hr #%x.\n", hr);

    ID3D12Resource_Release(res);

    destroy_test_context(&context);
}

void test_sparse_depth_stencil_rendering(void)
{
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc;
    D3D12_FEATURE_DATA_D3D12_OPTIONS options;
    D3D12_FEATURE_DATA_FORMAT_SUPPORT format;
    struct test_context_desc context_desc;
    D3D12_ROOT_SIGNATURE_DESC rs_desc;
    D3D12_ROOT_PARAMETER rs_params[3];
    D3D12_RESOURCE_DESC resource_desc;
    ID3D12DescriptorHeap *dsv_heap;
    ID3D12DescriptorHeap *rtv_heap;
    ID3D12Resource *atomic_buffer;
    struct resource_readback rb;
    struct test_context context;
    D3D12_HEAP_DESC heap_desc;
    unsigned int iter, x, y;
    ID3D12Resource *ds;
    ID3D12Resource *rt;
    ID3D12Heap *heap;
    HRESULT hr;

#include "shaders/sparse/headers/ds_sparse_vs.h"
#include "shaders/sparse/headers/ds_sparse_ps.h"

    memset(&context_desc, 0, sizeof(context_desc));
    context_desc.no_pipeline = true;
    context_desc.no_root_signature = true;
    context_desc.no_render_target = true;
    if (!init_test_context(&context, &context_desc))
        return;

    if (FAILED(ID3D12Device_CheckFeatureSupport(context.device, D3D12_FEATURE_D3D12_OPTIONS, &options, sizeof(options))) ||
        options.TiledResourcesTier < D3D12_TILED_RESOURCES_TIER_2)
    {
        skip("Tiled resources TIER_2 not supported.\n");
        destroy_test_context(&context);
        return;
    }

    format.Format = DXGI_FORMAT_D32_FLOAT;
    hr = ID3D12Device_CheckFeatureSupport(context.device, D3D12_FEATURE_FORMAT_SUPPORT, &format, sizeof(format));
    ok(SUCCEEDED(hr), "Failed to query for format support.\n");

    if (!(format.Support2 & D3D12_FORMAT_SUPPORT2_TILED))
    {
        skip("Tiled is not supported for D32.\n");
        destroy_test_context(&context);
        return;
    }

    memset(&rs_desc, 0, sizeof(rs_desc));
    memset(rs_params, 0, sizeof(rs_params));
    rs_desc.NumParameters = ARRAY_SIZE(rs_params);
    rs_desc.pParameters = rs_params;

    rs_params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
    rs_params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    rs_params[0].Constants.Num32BitValues = 2;
    rs_params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rs_params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    rs_params[1].Constants.Num32BitValues = 2;
    rs_params[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rs_params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    rs_params[2].Descriptor.ShaderRegister = 1;

    create_root_signature(context.device, &rs_desc, &context.root_signature);

    init_pipeline_state_desc_shaders(&pso_desc, context.root_signature, DXGI_FORMAT_R8_UNORM, NULL,
        ds_sparse_vs_dxbc.pShaderBytecode, ds_sparse_vs_dxbc.BytecodeLength,
        ds_sparse_ps_dxbc.pShaderBytecode, ds_sparse_ps_dxbc.BytecodeLength);
    pso_desc.DepthStencilState.DepthEnable = TRUE;
    pso_desc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    pso_desc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_GREATER;
    pso_desc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    pso_desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    hr = ID3D12Device_CreateGraphicsPipelineState(context.device, &pso_desc, &IID_ID3D12PipelineState, (void **)&context.pipeline_state);
    ok(SUCCEEDED(hr), "Failed to create PSO, hr #%x.\n", hr);

    memset(&resource_desc, 0, sizeof(resource_desc));
    resource_desc.Width = 256;
    resource_desc.Height = 256;
    resource_desc.Format = DXGI_FORMAT_D32_FLOAT;
    resource_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    resource_desc.DepthOrArraySize = 1;
    resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    resource_desc.Layout = D3D12_TEXTURE_LAYOUT_64KB_UNDEFINED_SWIZZLE;
    resource_desc.MipLevels = 1;
    resource_desc.SampleDesc.Count = 1;
    hr = ID3D12Device_CreateReservedResource(context.device, &resource_desc, D3D12_RESOURCE_STATE_COPY_SOURCE, NULL, &IID_ID3D12Resource, (void**)&ds);
    ok(SUCCEEDED(hr), "Failed to create reserved resource, hr #%x\n", hr);

    if (FAILED(hr))
    {
        destroy_test_context(&context);
        return;
    }

    rt = create_default_texture2d(context.device, 256, 256, 1, 1,
        DXGI_FORMAT_R8_UNORM, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);

    atomic_buffer = create_default_buffer(context.device, 16, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    memset(&heap_desc, 0, sizeof(heap_desc));
    heap_desc.SizeInBytes = 64 * 1024 * 4;
    heap_desc.Flags = D3D12_HEAP_FLAG_ALLOW_ONLY_RT_DS_TEXTURES;
    heap_desc.Properties.Type = D3D12_HEAP_TYPE_DEFAULT;
    hr = ID3D12Device_CreateHeap(context.device, &heap_desc, &IID_ID3D12Heap, (void**)&heap);
    ok(SUCCEEDED(hr), "Failed to create heap, hr #%x\n", hr);

    dsv_heap = create_cpu_descriptor_heap(context.device, D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 1);
    rtv_heap = create_cpu_descriptor_heap(context.device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1);
    ID3D12Device_CreateDepthStencilView(context.device, ds, NULL, get_cpu_dsv_handle(&context, dsv_heap, 0));
    ID3D12Device_CreateRenderTargetView(context.device, rt, NULL, get_cpu_dsv_handle(&context, rtv_heap, 0));

    for (iter = 0; iter < 4; iter++)
    {
        D3D12_TILED_RESOURCE_COORDINATE region_start_coordinates;
        D3D12_CPU_DESCRIPTOR_HANDLE dsv_handle;
        D3D12_CPU_DESCRIPTOR_HANDLE rt_handle;
        D3D12_TILE_REGION_SIZE region_sizes;
        D3D12_TILE_RANGE_FLAGS range_flags;
        const FLOAT clear_value[4] = { 0 };
        UINT heap_range_offsets;
        UINT range_tile_counts;
        D3D12_VIEWPORT vp;
        float params[2];
        D3D12_RECT sci;

        region_start_coordinates.Subresource = 0;
        region_start_coordinates.X = iter & 1;
        region_start_coordinates.Y = iter / 2;
        region_start_coordinates.Z = 0;

        memset(&region_sizes, 0, sizeof(region_sizes));
        region_sizes.NumTiles = 1;

        heap_range_offsets = iter;
        range_tile_counts = 1;
        range_flags = D3D12_TILE_RANGE_FLAG_NONE;

        ID3D12CommandQueue_UpdateTileMappings(context.queue, ds, 1, &region_start_coordinates, &region_sizes,
            heap, 1, &range_flags, &heap_range_offsets, &range_tile_counts, D3D12_TILE_MAPPING_FLAG_NONE);

        transition_resource_state(context.list, ds, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE);
        transition_resource_state(context.list, rt, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);

        rt_handle = get_cpu_rtv_handle(&context, rtv_heap, 0);
        dsv_handle = get_cpu_dsv_handle(&context, dsv_heap, 0);

        /* https://learn.microsoft.com/en-us/windows/win32/direct3d11/rasterizer-behavior-with-non-mapped-tiles
         * It is allowed to temporarily cache writes to unmapped tiles in TIER 2 apparently.
         * Avoid this situation by forcing a full memory barrier in-between, making such caching impossible.
         * Unmapped tiles won't get cleared. */
        ID3D12GraphicsCommandList_ClearRenderTargetView(context.list, rt_handle, clear_value, 0, NULL);
        ID3D12GraphicsCommandList_ClearDepthStencilView(context.list, dsv_handle, D3D12_CLEAR_FLAG_DEPTH, 0.5f, 0, 0, NULL);
        ID3D12GraphicsCommandList_Close(context.list);
        exec_command_list(context.queue, context.list);
        ID3D12GraphicsCommandList_Reset(context.list, context.allocator, NULL);

        ID3D12GraphicsCommandList_OMSetRenderTargets(context.list, 1, &rt_handle, TRUE, &dsv_handle);

        ID3D12GraphicsCommandList_SetGraphicsRootSignature(context.list, context.root_signature);
        ID3D12GraphicsCommandList_SetPipelineState(context.list, context.pipeline_state);

        /* Unmapped pages should pass the depth test. We enable write in the PSO, but this write should be masked when passing.
         * We draw two full-screen primitives, the first with depth 0.25, the second one with 0.25 - delta.
         * If sparse isn't working as intended, we'll draw 0.25 depth, then 0.25 - delta fails, meaning we end up with 200 in RT.
         * If sparse is working as intended, the depth test will always compare against 0, so we'll end up with 150 in RT. */
        params[0] = 0.25f;
        params[1] = -0.05f;
        ID3D12GraphicsCommandList_SetGraphicsRoot32BitConstants(context.list, 0, ARRAY_SIZE(params), params, 0);
        params[0] = 200.0f / 255.0f;
        params[1] = -50.0f / 255.0f;
        ID3D12GraphicsCommandList_SetGraphicsRoot32BitConstants(context.list, 1, ARRAY_SIZE(params), params, 0);
        ID3D12GraphicsCommandList_SetGraphicsRootUnorderedAccessView(context.list, 2, ID3D12Resource_GetGPUVirtualAddress(atomic_buffer) + 4 * iter);

        set_viewport(&vp, 0, 0, 256, 256, 0, 1);
        set_rect(&sci, 0, 0, 256, 256);
        ID3D12GraphicsCommandList_RSSetViewports(context.list, 1, &vp);
        ID3D12GraphicsCommandList_RSSetScissorRects(context.list, 1, &sci);

        ID3D12GraphicsCommandList_IASetPrimitiveTopology(context.list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ID3D12GraphicsCommandList_DrawInstanced(context.list, 3, 2, 0, 0);

        transition_resource_state(context.list, rt, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
        transition_resource_state(context.list, ds, D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_COPY_SOURCE);

        get_texture_readback_with_command_list(rt, 0, &rb, context.queue, context.list);
        reset_command_list(context.list, context.allocator);

        for (y = 0; y < 2; y++)
        {
            for (x = 0; x < 2; x++)
            {
                uint8_t value, expected;
                value = get_readback_uint8(&rb, x * 128, y * 128);

                /* For mapped pages, we cleared to 0.5, so we don't expect to see FB write. */
                expected = y * 2 + x <= iter ? 0 : 150;

                /* We work around lack of sparse support currently. */
                todo_if(is_radv_device(context.device))
                ok(value == expected, "Iter %u, tile %u, %u: expected %u, got %u.\n", iter, x, y, expected, value);
            }
        }

        release_resource_readback(&rb);
    }

    transition_resource_state(context.list, atomic_buffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
    get_buffer_readback_with_command_list(atomic_buffer, DXGI_FORMAT_R32_UINT, &rb, context.queue, context.list);
    reset_command_list(context.list, context.allocator);

    /* Ensure that driver didn't try to mask draws. Pixel invocations must still happen as-is. */
    for (iter = 0; iter < 4; iter++)
    {
        uint32_t value, expected;
        value = get_readback_uint(&rb, iter, 0, 0);
        expected = 256 * 256 * 2;
        ok(value == expected, "Iter %u: expected %u, got %u.\n", iter, expected, value);
    }

    release_resource_readback(&rb);

    ID3D12Resource_Release(ds);
    ID3D12Resource_Release(rt);
    ID3D12Heap_Release(heap);
    ID3D12DescriptorHeap_Release(dsv_heap);
    ID3D12DescriptorHeap_Release(rtv_heap);
    ID3D12Resource_Release(atomic_buffer);

    destroy_test_context(&context);
}

void test_sparse_default_mapping(void)
{
#define TILE_SIZE 65536
    ID3D12Resource *tiled_buffer, *tiled_image, *feedback_buffer;
    D3D12_DESCRIPTOR_HEAP_DESC descriptor_heap_desc;
    D3D12_COMPUTE_PIPELINE_STATE_DESC pso_desc;
    D3D12_UNORDERED_ACCESS_VIEW_DESC uav_desc;
    D3D12_FEATURE_DATA_D3D12_OPTIONS options;
    D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc;
    D3D12_DESCRIPTOR_RANGE rs_descriptors[2];
    struct test_context_desc context_desc;
    D3D12_HEAP_PROPERTIES heap_properties;
    ID3D12DescriptorHeap *descriptor_heap;
    D3D12_RESOURCE_DESC resource_desc;
    D3D12_ROOT_SIGNATURE_DESC rs_desc;
    D3D12_ROOT_PARAMETER rs_args[2];
    struct resource_readback rb;
    struct test_context context;
    ID3D12PipelineState *pso;
    ID3D12RootSignature *rs;
    unsigned int i;
    HRESULT hr;

#include "shaders/sparse/headers/sparse_init_access.h"

    static const struct
    {
        uint32_t buf_fb;
        uint32_t img_fb;
    }
    expected_results[] =
    {
        { 0xffffffffu, 0x0033ffffu },
        { 0xffffffffu, 0x00010001u },
        { 0xffffffffu, 0x00010001u },
        { 0xffffffffu, 0x00010001u },
        { 0xffffffffu, 0x00010001u },
        { 0xffffffffu, 0x00000001u },
        { 0xffffffffu, 0x00000000u },
        { 0xffffffffu, 0x00000000u },
    };

    struct
    {
        uint32_t image_tile_w;
        uint32_t image_tile_h;
        uint32_t image_w;
        uint32_t image_h;
        uint32_t image_mips;
        uint32_t buffer_stride;
    }
    shader_args =
    {
        256, 256, 1024, 1024, 11, TILE_SIZE / 16
    };

    memset(&context_desc, 0, sizeof(context_desc));
    context_desc.no_pipeline = true;
    context_desc.no_root_signature = true;
    context_desc.no_render_target = true;
    if (!init_test_context(&context, &context_desc))
        return;

    if (FAILED(ID3D12Device_CheckFeatureSupport(context.device, D3D12_FEATURE_D3D12_OPTIONS, &options, sizeof(options))) ||
        options.TiledResourcesTier < D3D12_TILED_RESOURCES_TIER_2)
    {
        skip("Tiled resources TIER_2 not supported.\n");
        destroy_test_context(&context);
        return;
    }

    if (is_amd_windows_device(context.device))
    {
        /* AMD segfaults when compiling the compute shader */
        skip("Skipping test to avoid crash inside AMD driver.\n");
        destroy_test_context(&context);
        return;
    }

    memset(&heap_properties, 0, sizeof(heap_properties));
    heap_properties.Type = D3D12_HEAP_TYPE_DEFAULT;

    memset(&resource_desc, 0, sizeof(resource_desc));
    resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    resource_desc.Width = 64u;
    resource_desc.Height = 1u;
    resource_desc.DepthOrArraySize = 1u;
    resource_desc.MipLevels = 1u;
    resource_desc.SampleDesc.Count = 1u;
    resource_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    resource_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    hr = ID3D12Device_CreateCommittedResource(context.device, &heap_properties,
            D3D12_HEAP_FLAG_NONE, &resource_desc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
            NULL, &IID_ID3D12Resource, (void**)&feedback_buffer);
    ok(hr == S_OK, "Failed to create feedback buffer, hr %#x.\n", hr);

    resource_desc.Width = 256u * TILE_SIZE;
    resource_desc.Flags = D3D12_RESOURCE_FLAG_NONE;

    hr = ID3D12Device_CreateReservedResource(context.device, &resource_desc, D3D12_RESOURCE_STATE_GENERIC_READ,
            NULL, &IID_ID3D12Resource, (void**)&tiled_buffer);
    ok(hr == S_OK, "Failed to create tiled buffer, hr %#x.\n", hr);

    memset(&resource_desc, 0, sizeof(resource_desc));
    resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    resource_desc.Format = DXGI_FORMAT_R8_UNORM;
    resource_desc.Width = 1024u;
    resource_desc.Height = 1024u;
    resource_desc.DepthOrArraySize = 1u;
    resource_desc.MipLevels = 11u;
    resource_desc.SampleDesc.Count = 1u;
    resource_desc.Layout = D3D12_TEXTURE_LAYOUT_64KB_UNDEFINED_SWIZZLE;

    hr = ID3D12Device_CreateReservedResource(context.device, &resource_desc, D3D12_RESOURCE_STATE_GENERIC_READ,
            NULL, &IID_ID3D12Resource, (void**)&tiled_image);
    ok(hr == S_OK, "Failed to create tiled image, hr %#x.\n", hr);

    memset(rs_descriptors, 0, sizeof(rs_descriptors));
    rs_descriptors[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    rs_descriptors[0].NumDescriptors = 1u;

    rs_descriptors[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    rs_descriptors[1].NumDescriptors = 2u;
    rs_descriptors[1].OffsetInDescriptorsFromTableStart = 1u;

    memset(rs_args, 0, sizeof(rs_args));
    rs_args[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    rs_args[0].Constants.Num32BitValues = sizeof(shader_args) / sizeof(uint32_t);
    rs_args[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    rs_args[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rs_args[1].DescriptorTable.NumDescriptorRanges = ARRAY_SIZE(rs_descriptors);
    rs_args[1].DescriptorTable.pDescriptorRanges = rs_descriptors;
    rs_args[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    memset(&rs_desc, 0, sizeof(rs_desc));
    rs_desc.NumParameters = ARRAY_SIZE(rs_args);
    rs_desc.pParameters = rs_args;

    hr = create_root_signature(context.device, &rs_desc, &rs);
    ok(hr == S_OK, "Failed to create root signature, hr %#x.\n", hr);

    memset(&pso_desc, 0, sizeof(pso_desc));
    pso_desc.pRootSignature = rs;
    pso_desc.CS = sparse_init_access_dxil;

    hr = ID3D12Device_CreateComputePipelineState(context.device, &pso_desc, &IID_ID3D12PipelineState, (void**)&pso);
    ok(hr == S_OK, "Failed to create compute pipeline, hr %#x.\n", hr);

    memset(&descriptor_heap_desc, 0, sizeof(descriptor_heap_desc));
    descriptor_heap_desc.NumDescriptors = 3;
    descriptor_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    descriptor_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

    hr = ID3D12Device_CreateDescriptorHeap(context.device, &descriptor_heap_desc, &IID_ID3D12DescriptorHeap, (void**)&descriptor_heap);
    ok(hr == S_OK, "Failed to create descriptor heap, hr %#x.\n", hr);

    memset(&uav_desc, 0, sizeof(uav_desc));
    uav_desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    uav_desc.Buffer.NumElements = ARRAY_SIZE(expected_results);
    uav_desc.Buffer.StructureByteStride = sizeof(expected_results[0]);

    ID3D12Device_CreateUnorderedAccessView(context.device, feedback_buffer,
            NULL, &uav_desc, get_cpu_descriptor_handle(&context, descriptor_heap, 0));

    memset(&srv_desc, 0, sizeof(srv_desc));
    srv_desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    srv_desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    srv_desc.Buffer.NumElements = 256 * TILE_SIZE / 16;
    srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

    ID3D12Device_CreateShaderResourceView(context.device, tiled_buffer,
            &srv_desc, get_cpu_descriptor_handle(&context, descriptor_heap, 1));

    memset(&srv_desc, 0, sizeof(srv_desc));
    srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv_desc.Format = DXGI_FORMAT_R8_UNORM;
    srv_desc.Texture2D.MipLevels = 11;
    srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

    ID3D12Device_CreateShaderResourceView(context.device, tiled_image,
            &srv_desc, get_cpu_descriptor_handle(&context, descriptor_heap, 2));

    ID3D12GraphicsCommandList_SetDescriptorHeaps(context.list, 1, &descriptor_heap);
    ID3D12GraphicsCommandList_SetComputeRootSignature(context.list, rs);
    ID3D12GraphicsCommandList_SetComputeRoot32BitConstants(context.list, 0, sizeof(shader_args) / sizeof(uint32_t), &shader_args, 0);
    ID3D12GraphicsCommandList_SetComputeRootDescriptorTable(context.list, 1, get_gpu_descriptor_handle(&context, descriptor_heap, 0));
    ID3D12GraphicsCommandList_SetPipelineState(context.list, pso);
    ID3D12GraphicsCommandList_Dispatch(context.list, 1, 1, 1);

    transition_resource_state(context.list, feedback_buffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
    get_buffer_readback_with_command_list(feedback_buffer, DXGI_FORMAT_R32_UINT, &rb, context.queue, context.list);

    for (i = 0; i < ARRAY_SIZE(expected_results); i++)
    {
        uint32_t buf = get_readback_uint(&rb, 2u * i, 0u, 0u);
        uint32_t img = get_readback_uint(&rb, 2u * i + 1u, 0u, 0u);

        ok(buf == expected_results[i].buf_fb, "Got %#x, expected %#x for tiled buffer at %u.\n",
                buf, expected_results[i].buf_fb, i);
        ok(img == expected_results[i].img_fb, "Got %#x, expected %#x for tiled texture at %u.\n",
                img, expected_results[i].img_fb, i);
    }

    release_resource_readback(&rb);

    ID3D12DescriptorHeap_Release(descriptor_heap);

    ID3D12PipelineState_Release(pso);
    ID3D12RootSignature_Release(rs);

    ID3D12Resource_Release(feedback_buffer);
    ID3D12Resource_Release(tiled_buffer);
    ID3D12Resource_Release(tiled_image);

    destroy_test_context(&context);
#undef TILE_SIZE
}
