/*
 * Copyright 2016 Józef Kucia for CodeWeavers
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

#include "vkd3d_private.h"

#include <errno.h>

#define VKD3D_MAX_DXGI_FORMAT DXGI_FORMAT_A4B4G4R4_UNORM

#define COLOR         (VK_IMAGE_ASPECT_COLOR_BIT)
#define DEPTH         (VK_IMAGE_ASPECT_DEPTH_BIT)
#define STENCIL       (VK_IMAGE_ASPECT_STENCIL_BIT)
#define DEPTH_STENCIL (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT)
#define PLANAR        (VK_IMAGE_ASPECT_PLANE_0_BIT | VK_IMAGE_ASPECT_PLANE_1_BIT)
#define TYPELESS      VKD3D_FORMAT_TYPE_TYPELESS
#define SINT          VKD3D_FORMAT_TYPE_SINT
#define UINT          VKD3D_FORMAT_TYPE_UINT

static const struct vkd3d_format_footprint nv12_copy_footprints[] =
{
    { DXGI_FORMAT_R8_TYPELESS, 1, 1, 1, 0, 0 },
    { DXGI_FORMAT_R8G8_TYPELESS, 1, 1, 2, 1, 1 },
};

static const struct vkd3d_format_footprint p016_copy_footprints[] =
{
    { DXGI_FORMAT_R16_TYPELESS, 1, 1, 2, 0, 0 },
    { DXGI_FORMAT_R16G16_TYPELESS, 1, 1, 4, 1, 1 },
};

static const struct vkd3d_format vkd3d_formats[] =
{
    {DXGI_FORMAT_R32G32B32A32_TYPELESS, VK_FORMAT_R32G32B32A32_SFLOAT,      16, 1, 1,  1, COLOR, 1, TYPELESS},
    {DXGI_FORMAT_R32G32B32A32_FLOAT,    VK_FORMAT_R32G32B32A32_SFLOAT,      16, 1, 1,  1, COLOR, 1},
    {DXGI_FORMAT_R32G32B32A32_UINT,     VK_FORMAT_R32G32B32A32_UINT,        16, 1, 1,  1, COLOR, 1, UINT},
    {DXGI_FORMAT_R32G32B32A32_SINT,     VK_FORMAT_R32G32B32A32_SINT,        16, 1, 1,  1, COLOR, 1, SINT},
    {DXGI_FORMAT_R32G32B32_TYPELESS,    VK_FORMAT_R32G32B32_SFLOAT,         12, 1, 1,  1, COLOR, 1, TYPELESS},
    {DXGI_FORMAT_R32G32B32_FLOAT,       VK_FORMAT_R32G32B32_SFLOAT,         12, 1, 1,  1, COLOR, 1},
    {DXGI_FORMAT_R32G32B32_UINT,        VK_FORMAT_R32G32B32_UINT,           12, 1, 1,  1, COLOR, 1, UINT},
    {DXGI_FORMAT_R32G32B32_SINT,        VK_FORMAT_R32G32B32_SINT,           12, 1, 1,  1, COLOR, 1, SINT},
    {DXGI_FORMAT_R16G16B16A16_TYPELESS, VK_FORMAT_R16G16B16A16_SFLOAT,      8,  1, 1,  1, COLOR, 1, TYPELESS},
    {DXGI_FORMAT_R16G16B16A16_FLOAT,    VK_FORMAT_R16G16B16A16_SFLOAT,      8,  1, 1,  1, COLOR, 1},
    {DXGI_FORMAT_R16G16B16A16_UNORM,    VK_FORMAT_R16G16B16A16_UNORM,       8,  1, 1,  1, COLOR, 1},
    {DXGI_FORMAT_R16G16B16A16_UINT,     VK_FORMAT_R16G16B16A16_UINT,        8,  1, 1,  1, COLOR, 1, UINT},
    {DXGI_FORMAT_R16G16B16A16_SNORM,    VK_FORMAT_R16G16B16A16_SNORM,       8,  1, 1,  1, COLOR, 1},
    {DXGI_FORMAT_R16G16B16A16_SINT,     VK_FORMAT_R16G16B16A16_SINT,        8,  1, 1,  1, COLOR, 1, SINT},
    {DXGI_FORMAT_R32G32_TYPELESS,       VK_FORMAT_R32G32_SFLOAT,            8,  1, 1,  1, COLOR, 1, TYPELESS},
    {DXGI_FORMAT_R32G32_FLOAT,          VK_FORMAT_R32G32_SFLOAT,            8,  1, 1,  1, COLOR, 1},
    {DXGI_FORMAT_R32G32_UINT,           VK_FORMAT_R32G32_UINT,              8,  1, 1,  1, COLOR, 1, UINT},
    {DXGI_FORMAT_R32G32_SINT,           VK_FORMAT_R32G32_SINT,              8,  1, 1,  1, COLOR, 1, SINT},
    {DXGI_FORMAT_R10G10B10A2_TYPELESS,  VK_FORMAT_A2B10G10R10_UNORM_PACK32, 4,  1, 1,  1, COLOR, 1, TYPELESS},
    {DXGI_FORMAT_R10G10B10A2_UNORM,     VK_FORMAT_A2B10G10R10_UNORM_PACK32, 4,  1, 1,  1, COLOR, 1},
    {DXGI_FORMAT_R10G10B10A2_UINT,      VK_FORMAT_A2B10G10R10_UINT_PACK32,  4,  1, 1,  1, COLOR, 1, UINT},
    {DXGI_FORMAT_R11G11B10_FLOAT,       VK_FORMAT_B10G11R11_UFLOAT_PACK32,  4,  1, 1,  1, COLOR, 1},
    {DXGI_FORMAT_R8G8_TYPELESS,         VK_FORMAT_R8G8_UNORM,               2,  1, 1,  1, COLOR, 1, TYPELESS},
    {DXGI_FORMAT_R8G8_UNORM,            VK_FORMAT_R8G8_UNORM,               2,  1, 1,  1, COLOR, 1},
    {DXGI_FORMAT_R8G8_UINT,             VK_FORMAT_R8G8_UINT,                2,  1, 1,  1, COLOR, 1, UINT},
    {DXGI_FORMAT_R8G8_SNORM,            VK_FORMAT_R8G8_SNORM,               2,  1, 1,  1, COLOR, 1},
    {DXGI_FORMAT_R8G8_SINT,             VK_FORMAT_R8G8_SINT,                2,  1, 1,  1, COLOR, 1, SINT},
    {DXGI_FORMAT_R8G8B8A8_TYPELESS,     VK_FORMAT_R8G8B8A8_UNORM,           4,  1, 1,  1, COLOR, 1, TYPELESS},
    {DXGI_FORMAT_R8G8B8A8_UNORM,        VK_FORMAT_R8G8B8A8_UNORM,           4,  1, 1,  1, COLOR, 1},
    {DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,   VK_FORMAT_R8G8B8A8_SRGB,            4,  1, 1,  1, COLOR, 1},
    {DXGI_FORMAT_R8G8B8A8_UINT,         VK_FORMAT_R8G8B8A8_UINT,            4,  1, 1,  1, COLOR, 1, UINT},
    {DXGI_FORMAT_R8G8B8A8_SNORM,        VK_FORMAT_R8G8B8A8_SNORM,           4,  1, 1,  1, COLOR, 1},
    {DXGI_FORMAT_R8G8B8A8_SINT,         VK_FORMAT_R8G8B8A8_SINT,            4,  1, 1,  1, COLOR, 1, SINT},
    {DXGI_FORMAT_R16G16_TYPELESS,       VK_FORMAT_R16G16_SFLOAT,            4,  1, 1,  1, COLOR, 1, TYPELESS},
    {DXGI_FORMAT_R16G16_FLOAT,          VK_FORMAT_R16G16_SFLOAT,            4,  1, 1,  1, COLOR, 1},
    {DXGI_FORMAT_R16G16_UNORM,          VK_FORMAT_R16G16_UNORM,             4,  1, 1,  1, COLOR, 1},
    {DXGI_FORMAT_R16G16_UINT,           VK_FORMAT_R16G16_UINT,              4,  1, 1,  1, COLOR, 1, UINT},
    {DXGI_FORMAT_R16G16_SNORM,          VK_FORMAT_R16G16_SNORM,             4,  1, 1,  1, COLOR, 1},
    {DXGI_FORMAT_R16G16_SINT,           VK_FORMAT_R16G16_SINT,              4,  1, 1,  1, COLOR, 1, SINT},
    {DXGI_FORMAT_R32_TYPELESS,          VK_FORMAT_R32_UINT,                 4,  1, 1,  1, COLOR, 1, TYPELESS},
    {DXGI_FORMAT_D32_FLOAT,             VK_FORMAT_D32_SFLOAT,               4,  1, 1,  1, DEPTH, 1},
    {DXGI_FORMAT_R32_FLOAT,             VK_FORMAT_R32_SFLOAT,               4,  1, 1,  1, COLOR, 1},
    {DXGI_FORMAT_R32_UINT,              VK_FORMAT_R32_UINT,                 4,  1, 1,  1, COLOR, 1, UINT},
    {DXGI_FORMAT_R32_SINT,              VK_FORMAT_R32_SINT,                 4,  1, 1,  1, COLOR, 1, SINT},
    {DXGI_FORMAT_R16_TYPELESS,          VK_FORMAT_R16_UINT,                 2,  1, 1,  1, COLOR, 1, TYPELESS},
    {DXGI_FORMAT_R16_FLOAT,             VK_FORMAT_R16_SFLOAT,               2,  1, 1,  1, COLOR, 1},
    {DXGI_FORMAT_D16_UNORM,             VK_FORMAT_D16_UNORM,                2,  1, 1,  1, DEPTH, 1},
    {DXGI_FORMAT_R16_UNORM,             VK_FORMAT_R16_UNORM,                2,  1, 1,  1, COLOR, 1},
    {DXGI_FORMAT_R16_UINT,              VK_FORMAT_R16_UINT,                 2,  1, 1,  1, COLOR, 1, UINT},
    {DXGI_FORMAT_R16_SNORM,             VK_FORMAT_R16_SNORM,                2,  1, 1,  1, COLOR, 1},
    {DXGI_FORMAT_R16_SINT,              VK_FORMAT_R16_SINT,                 2,  1, 1,  1, COLOR, 1, SINT},
    {DXGI_FORMAT_R8_TYPELESS,           VK_FORMAT_R8_UNORM,                 1,  1, 1,  1, COLOR, 1, TYPELESS},
    {DXGI_FORMAT_R8_UNORM,              VK_FORMAT_R8_UNORM,                 1,  1, 1,  1, COLOR, 1},
    {DXGI_FORMAT_R8_UINT,               VK_FORMAT_R8_UINT,                  1,  1, 1,  1, COLOR, 1, UINT},
    {DXGI_FORMAT_R8_SNORM,              VK_FORMAT_R8_SNORM,                 1,  1, 1,  1, COLOR, 1},
    {DXGI_FORMAT_R8_SINT,               VK_FORMAT_R8_SINT,                  1,  1, 1,  1, COLOR, 1, SINT},
    {DXGI_FORMAT_A8_UNORM,              VK_FORMAT_A8_UNORM_KHR,             1,  1, 1,  1, COLOR, 1},
    {DXGI_FORMAT_B8G8R8A8_UNORM,        VK_FORMAT_B8G8R8A8_UNORM,           4,  1, 1,  1, COLOR, 1},
    {DXGI_FORMAT_B8G8R8X8_UNORM,        VK_FORMAT_B8G8R8A8_UNORM,           4,  1, 1,  1, COLOR, 1},
    {DXGI_FORMAT_B8G8R8A8_TYPELESS,     VK_FORMAT_B8G8R8A8_UNORM,           4,  1, 1,  1, COLOR, 1, TYPELESS},
    {DXGI_FORMAT_B8G8R8A8_UNORM_SRGB,   VK_FORMAT_B8G8R8A8_SRGB,            4,  1, 1,  1, COLOR, 1},
    {DXGI_FORMAT_B8G8R8X8_TYPELESS,     VK_FORMAT_B8G8R8A8_UNORM,           4,  1, 1,  1, COLOR, 1},
    {DXGI_FORMAT_B8G8R8X8_UNORM_SRGB,   VK_FORMAT_B8G8R8A8_SRGB,            4,  1, 1,  1, COLOR, 1},
    {DXGI_FORMAT_R9G9B9E5_SHAREDEXP,    VK_FORMAT_E5B9G9R9_UFLOAT_PACK32,   4,  1, 1,  1, COLOR, 1},
    {DXGI_FORMAT_B5G6R5_UNORM,          VK_FORMAT_R5G6B5_UNORM_PACK16,      2,  1, 1,  1, COLOR, 1},
    {DXGI_FORMAT_B5G5R5A1_UNORM,        VK_FORMAT_A1R5G5B5_UNORM_PACK16,    2,  1, 1,  1, COLOR, 1},
    {DXGI_FORMAT_BC1_TYPELESS,          VK_FORMAT_BC1_RGBA_UNORM_BLOCK,     1,  4, 4,  8, COLOR, 1, TYPELESS},
    {DXGI_FORMAT_BC1_UNORM,             VK_FORMAT_BC1_RGBA_UNORM_BLOCK,     1,  4, 4,  8, COLOR, 1},
    {DXGI_FORMAT_BC1_UNORM_SRGB,        VK_FORMAT_BC1_RGBA_SRGB_BLOCK,      1,  4, 4,  8, COLOR, 1},
    {DXGI_FORMAT_BC2_TYPELESS,          VK_FORMAT_BC2_UNORM_BLOCK,          1,  4, 4, 16, COLOR, 1, TYPELESS},
    {DXGI_FORMAT_BC2_UNORM,             VK_FORMAT_BC2_UNORM_BLOCK,          1,  4, 4, 16, COLOR, 1},
    {DXGI_FORMAT_BC2_UNORM_SRGB,        VK_FORMAT_BC2_SRGB_BLOCK,           1,  4, 4, 16, COLOR, 1},
    {DXGI_FORMAT_BC3_TYPELESS,          VK_FORMAT_BC3_UNORM_BLOCK,          1,  4, 4, 16, COLOR, 1, TYPELESS},
    {DXGI_FORMAT_BC3_UNORM,             VK_FORMAT_BC3_UNORM_BLOCK,          1,  4, 4, 16, COLOR, 1},
    {DXGI_FORMAT_BC3_UNORM_SRGB,        VK_FORMAT_BC3_SRGB_BLOCK,           1,  4, 4, 16, COLOR, 1},
    {DXGI_FORMAT_BC4_TYPELESS,          VK_FORMAT_BC4_UNORM_BLOCK,          1,  4, 4,  8, COLOR, 1, TYPELESS},
    {DXGI_FORMAT_BC4_UNORM,             VK_FORMAT_BC4_UNORM_BLOCK,          1,  4, 4,  8, COLOR, 1},
    {DXGI_FORMAT_BC4_SNORM,             VK_FORMAT_BC4_SNORM_BLOCK,          1,  4, 4,  8, COLOR, 1},
    {DXGI_FORMAT_BC5_TYPELESS,          VK_FORMAT_BC5_UNORM_BLOCK,          1,  4, 4, 16, COLOR, 1, TYPELESS},
    {DXGI_FORMAT_BC5_UNORM,             VK_FORMAT_BC5_UNORM_BLOCK,          1,  4, 4, 16, COLOR, 1},
    {DXGI_FORMAT_BC5_SNORM,             VK_FORMAT_BC5_SNORM_BLOCK,          1,  4, 4, 16, COLOR, 1},
    {DXGI_FORMAT_BC6H_TYPELESS,         VK_FORMAT_BC6H_UFLOAT_BLOCK,        1,  4, 4, 16, COLOR, 1, TYPELESS},
    {DXGI_FORMAT_BC6H_UF16,             VK_FORMAT_BC6H_UFLOAT_BLOCK,        1,  4, 4, 16, COLOR, 1},
    {DXGI_FORMAT_BC6H_SF16,             VK_FORMAT_BC6H_SFLOAT_BLOCK,        1,  4, 4, 16, COLOR, 1},
    {DXGI_FORMAT_BC7_TYPELESS,          VK_FORMAT_BC7_UNORM_BLOCK,          1,  4, 4, 16, COLOR, 1, TYPELESS},
    {DXGI_FORMAT_BC7_UNORM,             VK_FORMAT_BC7_UNORM_BLOCK,          1,  4, 4, 16, COLOR, 1},
    {DXGI_FORMAT_BC7_UNORM_SRGB,        VK_FORMAT_BC7_SRGB_BLOCK,           1,  4, 4, 16, COLOR, 1},
    {DXGI_FORMAT_B4G4R4A4_UNORM,        VK_FORMAT_A4R4G4B4_UNORM_PACK16,    2,  1, 1,  1, COLOR, 1},
    {DXGI_FORMAT_A4B4G4R4_UNORM,        VK_FORMAT_R4G4B4A4_UNORM_PACK16,    2,  1, 1,  1, COLOR, 1},

    {DXGI_FORMAT_NV12,                  VK_FORMAT_G8_B8R8_2PLANE_420_UNORM, 0,  1, 1,  1, PLANAR, 2, TYPELESS, false, nv12_copy_footprints},
    /* P010 and P016 are functionally equivalent, there is no way to interpret data as R10X6 in D3D. */
    {DXGI_FORMAT_P010,                  VK_FORMAT_G16_B16R16_2PLANE_420_UNORM, 0,  1, 1,  1, PLANAR, 2, TYPELESS, false, p016_copy_footprints},
    {DXGI_FORMAT_P016,                  VK_FORMAT_G16_B16R16_2PLANE_420_UNORM, 0,  1, 1,  1, PLANAR, 2, TYPELESS, false, p016_copy_footprints},

    {DXGI_FORMAT_SAMPLER_FEEDBACK_MIN_MIP_OPAQUE, VK_FORMAT_R32G32_UINT, 8,  1, 1,  1, COLOR, 1},
    {DXGI_FORMAT_SAMPLER_FEEDBACK_MIP_REGION_USED_OPAQUE, VK_FORMAT_R32G32_UINT, 8,  1, 1,  1, COLOR, 1},
};

static const struct vkd3d_format_footprint depth_stencil_copy_footprints[] =
{
    { DXGI_FORMAT_R32_TYPELESS, 1, 1, 4, 0, 0 },
    { DXGI_FORMAT_R8_TYPELESS, 1, 1, 1, 0, 0 },
};

/* Each depth/stencil format is only compatible with itself in Vulkan. */
static const struct vkd3d_format vkd3d_depth_stencil_formats[] =
{
    {DXGI_FORMAT_R32G8X24_TYPELESS,        VK_FORMAT_D32_SFLOAT_S8_UINT, 8,  1, 1, 1, DEPTH_STENCIL, 2, TYPELESS, false, depth_stencil_copy_footprints},
    {DXGI_FORMAT_D32_FLOAT_S8X24_UINT,     VK_FORMAT_D32_SFLOAT_S8_UINT, 8,  1, 1, 1, DEPTH_STENCIL, 2, 0, false, depth_stencil_copy_footprints},
    {DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS, VK_FORMAT_D32_SFLOAT_S8_UINT, 8,  1, 1, 1, DEPTH,         2},
    {DXGI_FORMAT_X32_TYPELESS_G8X24_UINT,  VK_FORMAT_D32_SFLOAT_S8_UINT, 8,  1, 1, 1, STENCIL,       2},
    {DXGI_FORMAT_R32_TYPELESS,             VK_FORMAT_D32_SFLOAT,         4,  1, 1, 1, DEPTH,         1, TYPELESS},
    {DXGI_FORMAT_R32_FLOAT,                VK_FORMAT_D32_SFLOAT,         4,  1, 1, 1, DEPTH,         1},
    {DXGI_FORMAT_R24G8_TYPELESS,           VK_FORMAT_D24_UNORM_S8_UINT,  4,  1, 1, 1, DEPTH_STENCIL, 2, TYPELESS, false, depth_stencil_copy_footprints},
    {DXGI_FORMAT_D24_UNORM_S8_UINT,        VK_FORMAT_D24_UNORM_S8_UINT,  4,  1, 1, 1, DEPTH_STENCIL, 2, 0, false, depth_stencil_copy_footprints},
    {DXGI_FORMAT_R24_UNORM_X8_TYPELESS,    VK_FORMAT_D24_UNORM_S8_UINT,  4,  1, 1, 1, DEPTH,         2},
    {DXGI_FORMAT_X24_TYPELESS_G8_UINT,     VK_FORMAT_D24_UNORM_S8_UINT,  4,  1, 1, 1, STENCIL,       2},
    {DXGI_FORMAT_R16_TYPELESS,             VK_FORMAT_D16_UNORM,          2,  1, 1, 1, DEPTH,         1, TYPELESS},
    {DXGI_FORMAT_R16_UNORM,                VK_FORMAT_D16_UNORM,          2,  1, 1, 1, DEPTH,         1},
};
#undef COLOR
#undef DEPTH
#undef STENCIL
#undef DEPTH_STENCIL
#undef TYPELESS
#undef SINT
#undef UINT

static const struct dxgi_format_compatibility_list
{
    DXGI_FORMAT image_format;
    DXGI_FORMAT view_formats[VKD3D_MAX_COMPATIBLE_FORMAT_COUNT];
    DXGI_FORMAT uint_format; /* for ClearUAVUint */
}
dxgi_format_compatibility_list[] =
{
    {DXGI_FORMAT_R32G32B32A32_TYPELESS,
            {DXGI_FORMAT_R32G32B32A32_FLOAT, DXGI_FORMAT_R32G32B32A32_UINT, DXGI_FORMAT_R32G32B32A32_SINT},
            DXGI_FORMAT_R32G32B32A32_UINT},
    {DXGI_FORMAT_R32G32B32A32_FLOAT, {DXGI_FORMAT_UNKNOWN},
            DXGI_FORMAT_R32G32B32A32_UINT},
    {DXGI_FORMAT_R32G32B32A32_UINT,
            {DXGI_FORMAT_R32G32B32A32_SINT},
            DXGI_FORMAT_R32G32B32A32_UINT},
    {DXGI_FORMAT_R32G32B32A32_SINT,
            {DXGI_FORMAT_R32G32B32A32_UINT},
            DXGI_FORMAT_R32G32B32A32_UINT},

    {DXGI_FORMAT_R32G32B32_TYPELESS,
            {DXGI_FORMAT_R32G32B32_FLOAT, DXGI_FORMAT_R32G32B32_UINT, DXGI_FORMAT_R32G32B32_SINT},
            DXGI_FORMAT_R32G32B32_UINT},
    {DXGI_FORMAT_R32G32B32_FLOAT, {DXGI_FORMAT_UNKNOWN},
            DXGI_FORMAT_R32G32B32_UINT},
    {DXGI_FORMAT_R32G32B32_UINT,
            {DXGI_FORMAT_R32G32B32_SINT},
            DXGI_FORMAT_R32G32B32_UINT},
    {DXGI_FORMAT_R32G32B32_SINT,
            {DXGI_FORMAT_R32G32B32_UINT},
            DXGI_FORMAT_R32G32B32_UINT},

    {DXGI_FORMAT_R16G16B16A16_TYPELESS,
            {DXGI_FORMAT_R16G16B16A16_FLOAT, DXGI_FORMAT_R16G16B16A16_UNORM, DXGI_FORMAT_R16G16B16A16_SNORM, DXGI_FORMAT_R16G16B16A16_UINT, DXGI_FORMAT_R16G16B16A16_SINT},
            DXGI_FORMAT_R16G16B16A16_UINT},
    {DXGI_FORMAT_R16G16B16A16_FLOAT, {DXGI_FORMAT_UNKNOWN},
            DXGI_FORMAT_R16G16B16A16_UINT},
    {DXGI_FORMAT_R16G16B16A16_UINT,
            {DXGI_FORMAT_R16G16B16A16_SINT, DXGI_FORMAT_R16G16B16A16_UNORM, DXGI_FORMAT_R16G16B16A16_SNORM},
            DXGI_FORMAT_R16G16B16A16_UINT},
    {DXGI_FORMAT_R16G16B16A16_SINT,
            {DXGI_FORMAT_R16G16B16A16_UINT, DXGI_FORMAT_R16G16B16A16_UNORM, DXGI_FORMAT_R16G16B16A16_SNORM},
            DXGI_FORMAT_R16G16B16A16_UINT},
    {DXGI_FORMAT_R16G16B16A16_UNORM,
            {DXGI_FORMAT_R16G16B16A16_UINT, DXGI_FORMAT_R16G16B16A16_SINT},
            DXGI_FORMAT_R16G16B16A16_UINT},
    {DXGI_FORMAT_R16G16B16A16_SNORM,
            {DXGI_FORMAT_R16G16B16A16_UINT, DXGI_FORMAT_R16G16B16A16_SINT},
            DXGI_FORMAT_R16G16B16A16_UINT},

    {DXGI_FORMAT_R32G32_TYPELESS,
            {DXGI_FORMAT_R32G32_FLOAT, DXGI_FORMAT_R32G32_UINT, DXGI_FORMAT_R32G32_SINT},
            DXGI_FORMAT_R32G32_UINT},
    {DXGI_FORMAT_R32G32_FLOAT, {DXGI_FORMAT_UNKNOWN},
            DXGI_FORMAT_R32G32_UINT},
    {DXGI_FORMAT_R32G32_UINT,
            {DXGI_FORMAT_R32G32_SINT},
            DXGI_FORMAT_R32G32_UINT},
    {DXGI_FORMAT_R32G32_SINT,
            {DXGI_FORMAT_R32G32_UINT},
            DXGI_FORMAT_R32G32_UINT},

    {DXGI_FORMAT_R10G10B10A2_TYPELESS,
            {DXGI_FORMAT_R10G10B10A2_UNORM, DXGI_FORMAT_R10G10B10A2_UINT},
            DXGI_FORMAT_R10G10B10A2_UINT},
    {DXGI_FORMAT_R10G10B10A2_UINT,
            {DXGI_FORMAT_R10G10B10A2_UNORM},
            DXGI_FORMAT_R10G10B10A2_UINT},
    {DXGI_FORMAT_R10G10B10A2_UNORM,
            {DXGI_FORMAT_R10G10B10A2_UINT},
            DXGI_FORMAT_R10G10B10A2_UINT},

    {DXGI_FORMAT_R11G11B10_FLOAT, {DXGI_FORMAT_UNKNOWN},
            DXGI_FORMAT_R32_UINT},

    {DXGI_FORMAT_R9G9B9E5_SHAREDEXP, {DXGI_FORMAT_UNKNOWN},
            DXGI_FORMAT_R32_UINT},

    {DXGI_FORMAT_R8G8_TYPELESS,
            {DXGI_FORMAT_R8G8_UINT, DXGI_FORMAT_R8G8_SINT, DXGI_FORMAT_R8G8_UNORM, DXGI_FORMAT_R8G8_SNORM},
            DXGI_FORMAT_R8G8_UINT},
    {DXGI_FORMAT_R8G8_UINT,
            {DXGI_FORMAT_R8G8_SINT, DXGI_FORMAT_R8G8_UNORM, DXGI_FORMAT_R8G8_SNORM},
            DXGI_FORMAT_R8G8_UINT},
    {DXGI_FORMAT_R8G8_SINT,
            {DXGI_FORMAT_R8G8_UINT, DXGI_FORMAT_R8G8_UNORM, DXGI_FORMAT_R8G8_SNORM},
            DXGI_FORMAT_R8G8_UINT},
    {DXGI_FORMAT_R8G8_UNORM,
            {DXGI_FORMAT_R8G8_UINT, DXGI_FORMAT_R8G8_SINT},
            DXGI_FORMAT_R8G8_UINT},
    {DXGI_FORMAT_R8G8_SNORM,
            {DXGI_FORMAT_R8G8_UINT, DXGI_FORMAT_R8G8_SINT},
            DXGI_FORMAT_R8G8_UINT},

    {DXGI_FORMAT_R8G8B8A8_TYPELESS,
            {DXGI_FORMAT_R8G8B8A8_UINT, DXGI_FORMAT_R8G8B8A8_SINT, DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, DXGI_FORMAT_R8G8B8A8_SNORM},
            DXGI_FORMAT_R8G8B8A8_UINT},
    {DXGI_FORMAT_R8G8B8A8_UINT,
            {DXGI_FORMAT_R8G8B8A8_SINT, DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, DXGI_FORMAT_R8G8B8A8_SNORM},
            DXGI_FORMAT_R8G8B8A8_UINT},
    {DXGI_FORMAT_R8G8B8A8_SINT,
            {DXGI_FORMAT_R8G8B8A8_UINT, DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, DXGI_FORMAT_R8G8B8A8_SNORM},
            DXGI_FORMAT_R8G8B8A8_UINT},
    {DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,
            {DXGI_FORMAT_R8G8B8A8_UINT, DXGI_FORMAT_R8G8B8A8_SINT, DXGI_FORMAT_R8G8B8A8_UNORM},
            DXGI_FORMAT_R8G8B8A8_UINT},
    {DXGI_FORMAT_R8G8B8A8_UNORM,
            {DXGI_FORMAT_R8G8B8A8_UINT, DXGI_FORMAT_R8G8B8A8_SINT, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB},
            DXGI_FORMAT_R8G8B8A8_UINT},
    {DXGI_FORMAT_R8G8B8A8_SNORM,
            {DXGI_FORMAT_R8G8B8A8_UINT, DXGI_FORMAT_R8G8B8A8_SINT},
            DXGI_FORMAT_R8G8B8A8_UINT},

    {DXGI_FORMAT_R16G16_TYPELESS,
            {DXGI_FORMAT_R16G16_FLOAT, DXGI_FORMAT_R16G16_UINT, DXGI_FORMAT_R16G16_SINT, DXGI_FORMAT_R16G16_UNORM, DXGI_FORMAT_R16G16_SNORM},
            DXGI_FORMAT_R16G16_UINT},
    {DXGI_FORMAT_R16G16_FLOAT, {DXGI_FORMAT_UNKNOWN},
            DXGI_FORMAT_R16G16_UINT},
    {DXGI_FORMAT_R16G16_UINT,
            {DXGI_FORMAT_R16G16_SINT, DXGI_FORMAT_R16G16_UNORM, DXGI_FORMAT_R16G16_SNORM},
            DXGI_FORMAT_R16G16_UINT},
    {DXGI_FORMAT_R16G16_SINT,
            {DXGI_FORMAT_R16G16_UINT, DXGI_FORMAT_R16G16_UNORM, DXGI_FORMAT_R16G16_SNORM},
            DXGI_FORMAT_R16G16_UINT},
    {DXGI_FORMAT_R16G16_UNORM,
            {DXGI_FORMAT_R16G16_UINT, DXGI_FORMAT_R16G16_SINT},
            DXGI_FORMAT_R16G16_UINT},
    {DXGI_FORMAT_R16G16_SNORM,
            {DXGI_FORMAT_R16G16_UINT, DXGI_FORMAT_R16G16_SINT},
            DXGI_FORMAT_R16G16_UINT},

    {DXGI_FORMAT_R32_TYPELESS,
            {DXGI_FORMAT_R32_FLOAT, DXGI_FORMAT_R32_UINT, DXGI_FORMAT_R32_SINT},
            DXGI_FORMAT_R32_UINT},
    {DXGI_FORMAT_R32_FLOAT, {DXGI_FORMAT_UNKNOWN},
            DXGI_FORMAT_R32_UINT},
    {DXGI_FORMAT_R32_UINT,
            {DXGI_FORMAT_R32_SINT},
            DXGI_FORMAT_R32_UINT},
    {DXGI_FORMAT_R32_SINT,
            {DXGI_FORMAT_R32_UINT},
            DXGI_FORMAT_R32_UINT},

    {DXGI_FORMAT_R16_TYPELESS,
            {DXGI_FORMAT_R16_FLOAT, DXGI_FORMAT_R16_UINT, DXGI_FORMAT_R16_SINT, DXGI_FORMAT_R16_UNORM, DXGI_FORMAT_R16_SNORM},
            DXGI_FORMAT_R16_UINT},
    {DXGI_FORMAT_R16_FLOAT, {DXGI_FORMAT_UNKNOWN},
            DXGI_FORMAT_R16_UINT},
    {DXGI_FORMAT_R16_UINT,
            {DXGI_FORMAT_R16_SINT, DXGI_FORMAT_R16_UNORM, DXGI_FORMAT_R16_SNORM},
            DXGI_FORMAT_R16_UINT},
    {DXGI_FORMAT_R16_SINT,
            {DXGI_FORMAT_R16_UINT, DXGI_FORMAT_R16_UNORM, DXGI_FORMAT_R16_SNORM},
            DXGI_FORMAT_R16_UINT},
    {DXGI_FORMAT_R16_UNORM,
            {DXGI_FORMAT_R16_UINT, DXGI_FORMAT_R16_SINT},
            DXGI_FORMAT_R16_UINT},
    {DXGI_FORMAT_R16_SNORM,
            {DXGI_FORMAT_R16_UINT, DXGI_FORMAT_R16_SINT},
            DXGI_FORMAT_R16_UINT},

    {DXGI_FORMAT_R8_TYPELESS,
            {DXGI_FORMAT_R8_UINT, DXGI_FORMAT_R8_SINT, DXGI_FORMAT_R8_UNORM, DXGI_FORMAT_R8_SNORM},
            DXGI_FORMAT_R8_UINT},
    {DXGI_FORMAT_R8_UINT,
            {DXGI_FORMAT_R8_SINT, DXGI_FORMAT_R8_UNORM, DXGI_FORMAT_R8_SNORM},
            DXGI_FORMAT_R8_UINT},
    {DXGI_FORMAT_R8_SINT,
            {DXGI_FORMAT_R8_UINT, DXGI_FORMAT_R8_UNORM, DXGI_FORMAT_R8_SNORM},
            DXGI_FORMAT_R8_UINT},
    {DXGI_FORMAT_R8_UNORM,
            {DXGI_FORMAT_R8_UINT, DXGI_FORMAT_R8_SINT},
            DXGI_FORMAT_R8_UINT},
    {DXGI_FORMAT_R8_SNORM,
            {DXGI_FORMAT_R8_UINT, DXGI_FORMAT_R8_SINT},
            DXGI_FORMAT_R8_UINT},
    {DXGI_FORMAT_A8_UNORM, {DXGI_FORMAT_UNKNOWN},
            DXGI_FORMAT_A8_UNORM},

    {DXGI_FORMAT_B8G8R8A8_TYPELESS,
            {DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_FORMAT_B8G8R8A8_UNORM_SRGB},
            DXGI_FORMAT_R8G8B8A8_UINT},
    {DXGI_FORMAT_B8G8R8A8_UNORM,
            {DXGI_FORMAT_B8G8R8A8_UNORM_SRGB},
            DXGI_FORMAT_R8G8B8A8_UINT},
    {DXGI_FORMAT_B8G8R8A8_UNORM_SRGB,
            {DXGI_FORMAT_B8G8R8A8_UNORM},
            DXGI_FORMAT_R8G8B8A8_UINT},

    {DXGI_FORMAT_B8G8R8X8_TYPELESS,
            {DXGI_FORMAT_B8G8R8X8_UNORM, DXGI_FORMAT_B8G8R8X8_UNORM_SRGB},
            DXGI_FORMAT_R8G8B8A8_UINT},
    {DXGI_FORMAT_B8G8R8X8_UNORM,
            {DXGI_FORMAT_B8G8R8X8_UNORM_SRGB},
            DXGI_FORMAT_R8G8B8A8_UINT},
    {DXGI_FORMAT_B8G8R8X8_UNORM_SRGB,
            {DXGI_FORMAT_B8G8R8X8_UNORM},
            DXGI_FORMAT_R8G8B8A8_UINT},

    {DXGI_FORMAT_BC1_TYPELESS,
            {DXGI_FORMAT_BC1_UNORM, DXGI_FORMAT_BC1_UNORM_SRGB}},
    {DXGI_FORMAT_BC1_UNORM,
            {DXGI_FORMAT_BC1_UNORM_SRGB}},
    {DXGI_FORMAT_BC1_UNORM_SRGB,
            {DXGI_FORMAT_BC1_UNORM}},

    {DXGI_FORMAT_BC2_TYPELESS,
            {DXGI_FORMAT_BC2_UNORM, DXGI_FORMAT_BC2_UNORM_SRGB}},
    {DXGI_FORMAT_BC2_UNORM,
            {DXGI_FORMAT_BC2_UNORM_SRGB}},
    {DXGI_FORMAT_BC2_UNORM_SRGB,
            {DXGI_FORMAT_BC2_UNORM}},

    {DXGI_FORMAT_BC3_TYPELESS,
            {DXGI_FORMAT_BC3_UNORM, DXGI_FORMAT_BC3_UNORM_SRGB}},
    {DXGI_FORMAT_BC3_UNORM,
            {DXGI_FORMAT_BC3_UNORM_SRGB}},
    {DXGI_FORMAT_BC3_UNORM_SRGB,
            {DXGI_FORMAT_BC3_UNORM}},

    {DXGI_FORMAT_BC4_TYPELESS,
            {DXGI_FORMAT_BC4_UNORM, DXGI_FORMAT_BC4_SNORM}},
    {DXGI_FORMAT_BC5_TYPELESS,
            {DXGI_FORMAT_BC5_UNORM, DXGI_FORMAT_BC5_SNORM}},
    {DXGI_FORMAT_BC6H_TYPELESS,
            {DXGI_FORMAT_BC6H_UF16, DXGI_FORMAT_BC6H_SF16}},

    {DXGI_FORMAT_BC7_TYPELESS,
            {DXGI_FORMAT_BC7_UNORM, DXGI_FORMAT_BC7_UNORM_SRGB}},
    {DXGI_FORMAT_BC7_UNORM,
            {DXGI_FORMAT_BC7_UNORM_SRGB}},
    {DXGI_FORMAT_BC7_UNORM_SRGB,
            {DXGI_FORMAT_BC7_UNORM}},

    {DXGI_FORMAT_NV12,
            {DXGI_FORMAT_R8_UNORM, DXGI_FORMAT_R8_UINT, DXGI_FORMAT_R8G8_UNORM, DXGI_FORMAT_R8G8_UINT}},
    {DXGI_FORMAT_P010,
            {DXGI_FORMAT_R16_UNORM, DXGI_FORMAT_R16_UINT, DXGI_FORMAT_R16G16_UNORM, DXGI_FORMAT_R16G16_UINT}},
    {DXGI_FORMAT_P016,
            {DXGI_FORMAT_R16_UNORM, DXGI_FORMAT_R16_UINT, DXGI_FORMAT_R16G16_UNORM, DXGI_FORMAT_R16G16_UINT}},

    /* Internal implementation detail. We desire 64-bit atomics and R32G32 UAV will trigger that compat
     * similar to other 64-bit images. */
    {DXGI_FORMAT_SAMPLER_FEEDBACK_MIN_MIP_OPAQUE,
            {DXGI_FORMAT_R32G32_UINT}},
    {DXGI_FORMAT_SAMPLER_FEEDBACK_MIP_REGION_USED_OPAQUE,
            {DXGI_FORMAT_R32G32_UINT}},
};

void vkd3d_format_compatibility_list_add_format(struct vkd3d_format_compatibility_list *list, VkFormat vk_format)
{
    unsigned int i;
    bool found = false;

    for (i = 0; i < list->format_count && !found; i++)
        found = list->vk_formats[i] == vk_format;

    if (!found)
    {
        if (list->format_count < ARRAY_SIZE(list->vk_formats))
            list->vk_formats[list->format_count++] = vk_format;
        else
            WARN("Format compatiblity list overflowed.\n");
    }
}

static HRESULT vkd3d_init_format_compatibility_lists(struct d3d12_device *device)
{
    struct vkd3d_format_compatibility_list *lists, *dst;
    const struct dxgi_format_compatibility_list *src;
    unsigned int count;
    unsigned int i, j;

    device->format_compatibility_list_count = 0;
    device->format_compatibility_lists = NULL;

    count = 0;
    for (i = 0; i < ARRAY_SIZE(dxgi_format_compatibility_list); ++i)
        count = max(count, dxgi_format_compatibility_list[i].image_format + 1);

    if (!(lists = vkd3d_calloc(count, sizeof(*lists))))
        return E_OUTOFMEMORY;

    for (i = 0; i < ARRAY_SIZE(dxgi_format_compatibility_list); ++i)
    {
        src = &dxgi_format_compatibility_list[i];
        dst = &lists[src->image_format];

        dst->uint_format = src->uint_format;
        dst->vk_formats[dst->format_count++] = vkd3d_get_vk_format(src->image_format);

        for (j = 0; j < ARRAY_SIZE(src->view_formats) && src->view_formats[j]; j++)
            vkd3d_format_compatibility_list_add_format(dst, vkd3d_get_vk_format(src->view_formats[j]));
    }

    device->format_compatibility_list_count = count;
    device->format_compatibility_lists = lists;
    return S_OK;
}

static void vkd3d_cleanup_format_compatibility_lists(struct d3d12_device *device)
{
    vkd3d_free((void *)device->format_compatibility_lists);

    device->format_compatibility_lists = NULL;
    device->format_compatibility_list_count = 0;
}

static void vkd3d_get_vk_format_properties(struct d3d12_device *device, VkFormat vk_format, VkFormatProperties3 *properties3)
{
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    VkFormatProperties2 properties;

    memset(properties3, 0, sizeof(*properties3));
    properties3->sType = VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_3;

    memset(&properties, 0, sizeof(properties));
    properties.sType = VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2;
    properties.pNext = properties3;

    VK_CALL(vkGetPhysicalDeviceFormatProperties2(device->vk_physical_device, vk_format, &properties));
}

static void vkd3d_init_format_sample_counts(struct d3d12_device *device, struct vkd3d_format *format)
{
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    VkPhysicalDeviceSparseImageFormatInfo2 sparse_info;
    VkPhysicalDeviceImageFormatInfo2 info;
    VkSampleCountFlagBits sample_count;
    VkImageFormatProperties2 props;
    uint32_t info_count;

    format->supported_sample_counts = 0;
    format->supported_sparse_sample_counts = 0;

    memset(&props, 0, sizeof(props));
    props.sType = VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2;

    memset(&info, 0, sizeof(info));
    info.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2;
    info.format = format->vk_format;
    info.type = VK_IMAGE_TYPE_2D;
    info.tiling = format->vk_image_tiling;
    info.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;

    if (VK_CALL(vkGetPhysicalDeviceImageFormatProperties2(device->vk_physical_device, &info, &props)))
        return;

    format->supported_sample_counts = props.imageFormatProperties.sampleCounts;

    /* Planar and depth-stencil formats do not support sparse in D3D12 */
    if (format->plane_count > 1 || !device->device_info.features2.features.sparseResidencyImage2D)
        return;

    info.flags |= VK_IMAGE_CREATE_SPARSE_RESIDENCY_BIT |
            VK_IMAGE_CREATE_SPARSE_ALIASED_BIT |
            VK_IMAGE_CREATE_SPARSE_BINDING_BIT;

    if (VK_CALL(vkGetPhysicalDeviceImageFormatProperties2(device->vk_physical_device, &info, &props)))
        return;

    while (props.imageFormatProperties.sampleCounts)
    {
        /* VUID 01094. Samples must be marked as supported by ImageFormatProperties. */
        sample_count = 1u << vkd3d_bitmask_iter32(&props.imageFormatProperties.sampleCounts);

        memset(&sparse_info, 0, sizeof(sparse_info));
        sparse_info.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SPARSE_IMAGE_FORMAT_INFO_2;
        sparse_info.format = info.format;
        sparse_info.type = info.type;
        sparse_info.samples = sample_count;
        sparse_info.usage = info.usage;
        sparse_info.tiling = info.tiling;

        VK_CALL(vkGetPhysicalDeviceSparseImageFormatProperties2(
                device->vk_physical_device, &sparse_info, &info_count, NULL));

        if (info_count > 0)
            format->supported_sparse_sample_counts |= sample_count;
    }
}

static HRESULT vkd3d_init_depth_stencil_formats(struct d3d12_device *device)
{
    struct vkd3d_format *formats, *format;
    VkFormatProperties3 d24s8_properties;
    VkFormatProperties3 properties;
    unsigned int i;

    if (!(formats = vkd3d_calloc(VKD3D_MAX_DXGI_FORMAT + 1, sizeof(*formats))))
        return E_OUTOFMEMORY;

    vkd3d_get_vk_format_properties(device, VK_FORMAT_D24_UNORM_S8_UINT, &d24s8_properties);

    if (!(d24s8_properties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT))
    {
        /* AMD doesn't support VK_FORMAT_D24_UNORM_S8_UINT. */
        WARN("Mapping VK_FORMAT_D24_UNORM_S8_UINT to VK_FORMAT_D32_SFLOAT_S8_UINT.\n");
    }

    for (i = 0; i < ARRAY_SIZE(vkd3d_depth_stencil_formats); ++i)
    {
        assert(vkd3d_depth_stencil_formats[i].dxgi_format <= VKD3D_MAX_DXGI_FORMAT);
        format = &formats[vkd3d_depth_stencil_formats[i].dxgi_format];
        *format = vkd3d_depth_stencil_formats[i];
        if (format->vk_format == VK_FORMAT_D24_UNORM_S8_UINT &&
            !(d24s8_properties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT))
        {
            format->vk_format = VK_FORMAT_D32_SFLOAT_S8_UINT;
            format->is_emulated = true;
        }

        vkd3d_get_vk_format_properties(device, format->vk_format, &properties);

        /* We cannot cast depth stencil formats in Vulkan, so features == castable. */
        format->vk_image_tiling = VK_IMAGE_TILING_OPTIMAL;
        format->vk_format_features = properties.optimalTilingFeatures;
        format->vk_format_features_castable = properties.optimalTilingFeatures;
        format->vk_format_features_buffer = 0;

        vkd3d_init_format_sample_counts(device, format);
    }

    device->depth_stencil_formats = formats;

    return S_OK;
}

static void vkd3d_cleanup_depth_stencil_formats(struct d3d12_device *device)
{
    vkd3d_free((void *)device->depth_stencil_formats);

    device->depth_stencil_formats = NULL;
}

static HRESULT vkd3d_init_formats(struct d3d12_device *device)
{
    const struct vkd3d_format_compatibility_list *list;
    struct vkd3d_format *formats, *format;
    VkFormatProperties3 properties;
    DXGI_FORMAT dxgi_format;
    unsigned int i, j;
    bool emulate_a8;

    static const VkFormatFeatureFlags2 feature_mask =
            VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
            VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT |
            VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT |
            VK_FORMAT_FEATURE_TRANSFER_SRC_BIT |
            VK_FORMAT_FEATURE_TRANSFER_DST_BIT;

    if (!(formats = vkd3d_calloc(VKD3D_MAX_DXGI_FORMAT + 1, sizeof(*formats))))
        return E_OUTOFMEMORY;

    if (device->device_info.maintenance_5_features.maintenance5)
    {
        const VkFormatFeatureFlags a8_required_features =
                VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT |
                VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT |
                VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
                VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT |
                VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT |
                VK_FORMAT_FEATURE_UNIFORM_TEXEL_BUFFER_BIT |
                VK_FORMAT_FEATURE_STORAGE_TEXEL_BUFFER_BIT;

        VkFormatProperties3 a8_properties, r8_properties;
        VkFormatFeatureFlags r8_unique_features;

        vkd3d_get_vk_format_properties(device, VK_FORMAT_A8_UNORM_KHR, &a8_properties);
        vkd3d_get_vk_format_properties(device, VK_FORMAT_R8_UNORM, &r8_properties);

        /* Only use the native A8_UNORM format if support is at least as good
         * as for the R8_UNORM fallback format for relevant features. */
        r8_unique_features = (r8_properties.optimalTilingFeatures | r8_properties.bufferFeatures) &
                ~(a8_properties.optimalTilingFeatures | a8_properties.bufferFeatures);

        emulate_a8 = (r8_unique_features & a8_required_features) != 0;
    }
    else
        emulate_a8 = true;

    if (emulate_a8)
        WARN("Mapping VK_FORMAT_A8_UNORM_KHR to VK_FORMAT_R8_UNORM.\n");

    for (i = 0; i < ARRAY_SIZE(vkd3d_formats); ++i)
    {
        assert(vkd3d_formats[i].dxgi_format <= VKD3D_MAX_DXGI_FORMAT);
        dxgi_format = vkd3d_formats[i].dxgi_format;
        format = &formats[dxgi_format];
        *format = vkd3d_formats[i];

        if (format->vk_format == VK_FORMAT_A8_UNORM_KHR && emulate_a8)
        {
            format->vk_format = VK_FORMAT_R8_UNORM;
            format->is_emulated = true;
        }

        vkd3d_get_vk_format_properties(device, format->vk_format, &properties);

        if ((properties.optimalTilingFeatures & feature_mask) || !(properties.linearTilingFeatures & feature_mask))
        {
            format->vk_image_tiling = VK_IMAGE_TILING_OPTIMAL;
            format->vk_format_features = properties.optimalTilingFeatures;
            format->vk_format_features_castable = properties.optimalTilingFeatures;
        }
        else
        {
            format->vk_image_tiling = VK_IMAGE_TILING_LINEAR;
            format->vk_format_features = properties.linearTilingFeatures;
            format->vk_format_features_castable = properties.linearTilingFeatures;
        }

        format->vk_format_features_buffer = properties.bufferFeatures;

        if (dxgi_format < device->format_compatibility_list_count)
        {
            list = &device->format_compatibility_lists[dxgi_format];
            for (j = 0; j < list->format_count; j++)
            {
                vkd3d_get_vk_format_properties(device, list->vk_formats[j], &properties);
                format->vk_format_features_castable |= format->vk_image_tiling == VK_IMAGE_TILING_OPTIMAL
                        ? properties.optimalTilingFeatures
                        : properties.linearTilingFeatures;
            }
        }

        vkd3d_init_format_sample_counts(device, format);
    }

    device->formats = formats;

    return S_OK;
}

static void vkd3d_cleanup_formats(struct d3d12_device *device)
{
    vkd3d_free((void *)device->formats);

    device->formats = NULL;
}

HRESULT vkd3d_init_format_info(struct d3d12_device *device)
{
    HRESULT hr;

    if (FAILED(hr = vkd3d_init_depth_stencil_formats(device)))
        return hr;

    if (FAILED(hr = vkd3d_init_format_compatibility_lists(device)))
    {
        vkd3d_cleanup_depth_stencil_formats(device);
        return hr;
    }

    if (FAILED(hr = vkd3d_init_formats(device)))
    {
        vkd3d_cleanup_depth_stencil_formats(device);
        vkd3d_cleanup_format_compatibility_lists(device);
    }

    return hr;
}

void vkd3d_cleanup_format_info(struct d3d12_device *device)
{
    vkd3d_cleanup_depth_stencil_formats(device);
    vkd3d_cleanup_format_compatibility_lists(device);
    vkd3d_cleanup_formats(device);
}

/* We use overrides for depth/stencil formats. This is required in order to
 * properly support typeless formats because depth/stencil formats are only
 * compatible with themselves in Vulkan.
 */
static const struct vkd3d_format *vkd3d_get_depth_stencil_format(const struct d3d12_device *device,
        DXGI_FORMAT dxgi_format)
{
    const struct vkd3d_format *format;

    assert(device);
    format = &device->depth_stencil_formats[dxgi_format];

    return format->dxgi_format ? format : NULL;
}

const struct vkd3d_format *vkd3d_get_format(const struct d3d12_device *device,
        DXGI_FORMAT dxgi_format, bool depth_stencil)
{
    const struct vkd3d_format *format;

    if (!is_valid_format(dxgi_format))
    {
        ERR("Invalid format %d.\n", dxgi_format);
        return NULL;
    }

    /* If we request a depth-stencil format (or typeless variant) that is planar,
     * there cannot be any ambiguity which format to select, we must choose a depth-stencil format.
     * For single aspect formats,
     * there are cases where we need to choose either COLOR or DEPTH aspect variants based on depth_stencil argument,
     * but there cannot be any such issue for DEPTH_STENCIL types.
     * This fixes issues where e.g. R24_UNORM_X8_TYPELESS format is used without ALLOW_DEPTH_STENCIL. */
    format = vkd3d_get_depth_stencil_format(device, dxgi_format);
    if (format && (depth_stencil || format->plane_count > 1))
        return format;

    format = &device->formats[dxgi_format];

    return format->dxgi_format ? format : NULL;
}

struct vkd3d_format_footprint vkd3d_format_footprint_for_plane(const struct vkd3d_format *format, unsigned int plane_idx)
{
    if (format->plane_footprints)
    {
        return format->plane_footprints[plane_idx];
    }
    else
    {
        struct vkd3d_format_footprint footprint;
        footprint.dxgi_format = format->dxgi_format;
        footprint.block_width = format->block_width;
        footprint.block_height = format->block_height;
        footprint.subsample_x_log2 = 0;
        footprint.subsample_y_log2 = 0;
        footprint.block_byte_count = format->byte_count * format->block_byte_count;
        return footprint;
    }
}

VkFormat vkd3d_internal_get_vk_format(const struct d3d12_device *device, DXGI_FORMAT dxgi_format)
{
    const struct vkd3d_format *format;

    if ((format = vkd3d_get_format(device, dxgi_format, false)))
        return format->vk_format;

    return VK_FORMAT_UNDEFINED;
}

void vkd3d_format_copy_data(const struct vkd3d_format *format, const uint8_t *src,
        unsigned int src_row_pitch, unsigned int src_slice_pitch, uint8_t *dst, unsigned int dst_row_pitch,
        unsigned int dst_slice_pitch, unsigned int w, unsigned int h, unsigned int d)
{
    unsigned int row_block_count, row_count, row_size, slice, row;
    unsigned int slice_count = d;
    const uint8_t *src_row;
    uint8_t *dst_row;

    row_block_count = (w + format->block_width - 1) / format->block_width;
    row_count = (h + format->block_height - 1) / format->block_height;
    row_size = row_block_count * format->byte_count * format->block_byte_count;

    for (slice = 0; slice < slice_count; ++slice)
    {
        for (row = 0; row < row_count; ++row)
        {
            src_row = &src[slice * src_slice_pitch + row * src_row_pitch];
            dst_row = &dst[slice * dst_slice_pitch + row * dst_row_pitch];
            memcpy(dst_row, src_row, row_size);
        }
    }
}

VkFormat vkd3d_get_vk_format(DXGI_FORMAT format)
{
    unsigned int i;

    for (i = 0; i < ARRAY_SIZE(vkd3d_formats); ++i)
    {
        if (vkd3d_formats[i].dxgi_format == format)
            return vkd3d_formats[i].vk_format;
    }

    return VK_FORMAT_UNDEFINED;
}

/* Get some size-based low bits for memory prioritization in the same
   way as d3d12; d3d12 bumps certain resource priorities to
   D3D12_RESIDENCY_PRIORITY_HIGH + size-based bits (10MB resolution)
   see: https://learn.microsoft.com/en-us/windows/win32/direct3d12/residency#default-priority-algorithm */
uint32_t vkd3d_get_priority_adjust(VkDeviceSize size)
{
    return min((size / (10 * 1048576)), 0xffffUL);
}

static float vkd3d_lerp_u32_to_float(uint32_t uval, uint32_t ustart, uint32_t uend, float fstart, float fend)
{
    float a;

    if (uval <= ustart) return fstart;
    else if (uval >= uend) return fend;

    a = (float)(uval - ustart) / (float)(uend - ustart);
    return fstart * (1.0f - a) + (fend * a);
}

/* map from 32-bit d3d prio to float (0..1) vk prio. */
float vkd3d_convert_to_vk_prio(D3D12_RESIDENCY_PRIORITY d3d12prio)
{
    float result;

    /* align D3D12_RESIDENCY_PRIORITY_NORMAL (the default d3d12 prio) with
       0.5 (the default vk prio) so neither kind wins without explicit prio */
    if (d3d12prio <= D3D12_RESIDENCY_PRIORITY_NORMAL)
    {
        result = vkd3d_lerp_u32_to_float(d3d12prio,
            0, D3D12_RESIDENCY_PRIORITY_NORMAL,
            0.001f, 0.500f);
    }
    else if (d3d12prio <= D3D12_RESIDENCY_PRIORITY_HIGH)
    {
        result = vkd3d_lerp_u32_to_float(d3d12prio,
            D3D12_RESIDENCY_PRIORITY_NORMAL, D3D12_RESIDENCY_PRIORITY_HIGH,
            0.500f, 0.700f);
    }
    else if (d3d12prio <= D3D12_RESIDENCY_PRIORITY_HIGH+0xffff)
    {
        result = vkd3d_lerp_u32_to_float(d3d12prio,
            D3D12_RESIDENCY_PRIORITY_HIGH, D3D12_RESIDENCY_PRIORITY_HIGH+0xffff,
            0.700f, 0.800f);
    }
    else
    {
        result = vkd3d_lerp_u32_to_float(d3d12prio,
            D3D12_RESIDENCY_PRIORITY_HIGH+0xffff, UINT32_MAX,
            0.800f, 1.000f);
    }

    /* Note: A naive conversion from a UINT32 d3d priority to a float32 vk priority
       loses around 9 of the 16 lower-order bits which encode size-based subranking
       in the D3D12_RESIDENCY_PRIORITY_HIGH to HIGH+0xffff domain.  The above expansion
       of that domain into a proportionally wider range works around this. */

    /* 0.0f is reserved for explicitly evicted resources */
    return max(min(result, 1.f), 0.001f);
}

DXGI_FORMAT vkd3d_get_dxgi_format(VkFormat format)
{
    DXGI_FORMAT dxgi_format;
    VkFormat vk_format;
    unsigned int i;

    for (i = 0; i < ARRAY_SIZE(vkd3d_formats); ++i)
    {
        vk_format = vkd3d_formats[i].vk_format;
        dxgi_format = vkd3d_formats[i].dxgi_format;
        if (vk_format == format && vkd3d_formats[i].type != VKD3D_FORMAT_TYPE_TYPELESS)
            return dxgi_format;
    }

    FIXME("Unhandled Vulkan format %#x.\n", format);
    return DXGI_FORMAT_UNKNOWN;
}

bool is_valid_feature_level(D3D_FEATURE_LEVEL feature_level)
{
    static const D3D_FEATURE_LEVEL valid_feature_levels[] =
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
    unsigned int i;

    for (i = 0; i < ARRAY_SIZE(valid_feature_levels); ++i)
    {
        if (valid_feature_levels[i] == feature_level)
            return true;
    }

    return false;
}

bool is_write_resource_state(D3D12_RESOURCE_STATES state)
{
    return state & (D3D12_RESOURCE_STATE_RENDER_TARGET
            | D3D12_RESOURCE_STATE_UNORDERED_ACCESS
            | D3D12_RESOURCE_STATE_DEPTH_WRITE
            | D3D12_RESOURCE_STATE_STREAM_OUT
            | D3D12_RESOURCE_STATE_COPY_DEST
            | D3D12_RESOURCE_STATE_RESOLVE_DEST);
}

bool is_valid_resource_state(D3D12_RESOURCE_STATES state)
{
    const D3D12_RESOURCE_STATES valid_states =
            D3D12_RESOURCE_STATE_COMMON |
            D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER |
            D3D12_RESOURCE_STATE_INDEX_BUFFER |
            D3D12_RESOURCE_STATE_RENDER_TARGET |
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS |
            D3D12_RESOURCE_STATE_DEPTH_WRITE |
            D3D12_RESOURCE_STATE_DEPTH_READ |
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE |
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
            D3D12_RESOURCE_STATE_STREAM_OUT |
            D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT |
            D3D12_RESOURCE_STATE_COPY_DEST |
            D3D12_RESOURCE_STATE_COPY_SOURCE |
            D3D12_RESOURCE_STATE_RESOLVE_DEST |
            D3D12_RESOURCE_STATE_RESOLVE_SOURCE |
            D3D12_RESOURCE_STATE_GENERIC_READ |
            D3D12_RESOURCE_STATE_PRESENT |
            D3D12_RESOURCE_STATE_PREDICATION |
            D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE |
            D3D12_RESOURCE_STATE_SHADING_RATE_SOURCE;

    if (state & ~valid_states)
    {
        WARN("Invalid resource states %#x.\n", state & ~valid_states);
        return false;
    }

    /* Exactly one bit must be set for write states. */
    if (is_write_resource_state(state) && !is_power_of_two(state))
    {
        WARN("Write state cannot be mixed with other states: %#x.\n", state);
        return false;
    }

    return true;
}

bool is_valid_format(DXGI_FORMAT dxgi_format)
{
    if (dxgi_format >= DXGI_FORMAT_UNKNOWN && dxgi_format <= DXGI_FORMAT_B4G4R4A4_UNORM)
        return true;
    if (dxgi_format >= DXGI_FORMAT_P208 && dxgi_format <= DXGI_FORMAT_A4B4G4R4_UNORM)
        return true;
    return false;
}

HRESULT return_interface(void *iface, REFIID iface_iid,
        REFIID requested_iid, void **object)
{
    IUnknown *unknown = iface;
    HRESULT hr;

    /* Don't check IID here, we always have to return S_FALSE. */
    if (!object)
    {
        IUnknown_Release(unknown);
        return S_FALSE;
    }

    if (IsEqualGUID(iface_iid, requested_iid))
    {
        *object = unknown;
        return S_OK;
    }

    hr = IUnknown_QueryInterface(unknown, requested_iid, object);
    IUnknown_Release(unknown);
    return hr;
}

const char *debug_dxgi_format(DXGI_FORMAT format)
{
    #define ENUM_NAME(x) \
        case x: return #x;

    switch(format)
    {
        ENUM_NAME(DXGI_FORMAT_UNKNOWN)
        ENUM_NAME(DXGI_FORMAT_R32G32B32A32_TYPELESS)
        ENUM_NAME(DXGI_FORMAT_R32G32B32A32_FLOAT)
        ENUM_NAME(DXGI_FORMAT_R32G32B32A32_UINT)
        ENUM_NAME(DXGI_FORMAT_R32G32B32A32_SINT)
        ENUM_NAME(DXGI_FORMAT_R32G32B32_TYPELESS)
        ENUM_NAME(DXGI_FORMAT_R32G32B32_FLOAT)
        ENUM_NAME(DXGI_FORMAT_R32G32B32_UINT)
        ENUM_NAME(DXGI_FORMAT_R32G32B32_SINT)
        ENUM_NAME(DXGI_FORMAT_R16G16B16A16_TYPELESS)
        ENUM_NAME(DXGI_FORMAT_R16G16B16A16_FLOAT)
        ENUM_NAME(DXGI_FORMAT_R16G16B16A16_UNORM)
        ENUM_NAME(DXGI_FORMAT_R16G16B16A16_UINT)
        ENUM_NAME(DXGI_FORMAT_R16G16B16A16_SNORM)
        ENUM_NAME(DXGI_FORMAT_R16G16B16A16_SINT)
        ENUM_NAME(DXGI_FORMAT_R32G32_TYPELESS)
        ENUM_NAME(DXGI_FORMAT_R32G32_FLOAT)
        ENUM_NAME(DXGI_FORMAT_R32G32_UINT)
        ENUM_NAME(DXGI_FORMAT_R32G32_SINT)
        ENUM_NAME(DXGI_FORMAT_R32G8X24_TYPELESS)
        ENUM_NAME(DXGI_FORMAT_D32_FLOAT_S8X24_UINT)
        ENUM_NAME(DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS)
        ENUM_NAME(DXGI_FORMAT_X32_TYPELESS_G8X24_UINT)
        ENUM_NAME(DXGI_FORMAT_R10G10B10A2_TYPELESS)
        ENUM_NAME(DXGI_FORMAT_R10G10B10A2_UNORM)
        ENUM_NAME(DXGI_FORMAT_R10G10B10A2_UINT)
        ENUM_NAME(DXGI_FORMAT_R11G11B10_FLOAT)
        ENUM_NAME(DXGI_FORMAT_R8G8B8A8_TYPELESS)
        ENUM_NAME(DXGI_FORMAT_R8G8B8A8_UNORM)
        ENUM_NAME(DXGI_FORMAT_R8G8B8A8_UNORM_SRGB)
        ENUM_NAME(DXGI_FORMAT_R8G8B8A8_UINT)
        ENUM_NAME(DXGI_FORMAT_R8G8B8A8_SNORM)
        ENUM_NAME(DXGI_FORMAT_R8G8B8A8_SINT)
        ENUM_NAME(DXGI_FORMAT_R16G16_TYPELESS)
        ENUM_NAME(DXGI_FORMAT_R16G16_FLOAT)
        ENUM_NAME(DXGI_FORMAT_R16G16_UNORM)
        ENUM_NAME(DXGI_FORMAT_R16G16_UINT)
        ENUM_NAME(DXGI_FORMAT_R16G16_SNORM)
        ENUM_NAME(DXGI_FORMAT_R16G16_SINT)
        ENUM_NAME(DXGI_FORMAT_R32_TYPELESS)
        ENUM_NAME(DXGI_FORMAT_D32_FLOAT)
        ENUM_NAME(DXGI_FORMAT_R32_FLOAT)
        ENUM_NAME(DXGI_FORMAT_R32_UINT)
        ENUM_NAME(DXGI_FORMAT_R32_SINT)
        ENUM_NAME(DXGI_FORMAT_R24G8_TYPELESS)
        ENUM_NAME(DXGI_FORMAT_D24_UNORM_S8_UINT)
        ENUM_NAME(DXGI_FORMAT_R24_UNORM_X8_TYPELESS)
        ENUM_NAME(DXGI_FORMAT_X24_TYPELESS_G8_UINT)
        ENUM_NAME(DXGI_FORMAT_R8G8_TYPELESS)
        ENUM_NAME(DXGI_FORMAT_R8G8_UNORM)
        ENUM_NAME(DXGI_FORMAT_R8G8_UINT)
        ENUM_NAME(DXGI_FORMAT_R8G8_SNORM)
        ENUM_NAME(DXGI_FORMAT_R8G8_SINT)
        ENUM_NAME(DXGI_FORMAT_R16_TYPELESS)
        ENUM_NAME(DXGI_FORMAT_R16_FLOAT)
        ENUM_NAME(DXGI_FORMAT_D16_UNORM)
        ENUM_NAME(DXGI_FORMAT_R16_UNORM)
        ENUM_NAME(DXGI_FORMAT_R16_UINT)
        ENUM_NAME(DXGI_FORMAT_R16_SNORM)
        ENUM_NAME(DXGI_FORMAT_R16_SINT)
        ENUM_NAME(DXGI_FORMAT_R8_TYPELESS)
        ENUM_NAME(DXGI_FORMAT_R8_UNORM)
        ENUM_NAME(DXGI_FORMAT_R8_UINT)
        ENUM_NAME(DXGI_FORMAT_R8_SNORM)
        ENUM_NAME(DXGI_FORMAT_R8_SINT)
        ENUM_NAME(DXGI_FORMAT_A8_UNORM)
        ENUM_NAME(DXGI_FORMAT_R1_UNORM)
        ENUM_NAME(DXGI_FORMAT_R9G9B9E5_SHAREDEXP)
        ENUM_NAME(DXGI_FORMAT_R8G8_B8G8_UNORM)
        ENUM_NAME(DXGI_FORMAT_G8R8_G8B8_UNORM)
        ENUM_NAME(DXGI_FORMAT_BC1_TYPELESS)
        ENUM_NAME(DXGI_FORMAT_BC1_UNORM)
        ENUM_NAME(DXGI_FORMAT_BC1_UNORM_SRGB)
        ENUM_NAME(DXGI_FORMAT_BC2_TYPELESS)
        ENUM_NAME(DXGI_FORMAT_BC2_UNORM)
        ENUM_NAME(DXGI_FORMAT_BC2_UNORM_SRGB)
        ENUM_NAME(DXGI_FORMAT_BC3_TYPELESS)
        ENUM_NAME(DXGI_FORMAT_BC3_UNORM)
        ENUM_NAME(DXGI_FORMAT_BC3_UNORM_SRGB)
        ENUM_NAME(DXGI_FORMAT_BC4_TYPELESS)
        ENUM_NAME(DXGI_FORMAT_BC4_UNORM)
        ENUM_NAME(DXGI_FORMAT_BC4_SNORM)
        ENUM_NAME(DXGI_FORMAT_BC5_TYPELESS)
        ENUM_NAME(DXGI_FORMAT_BC5_UNORM)
        ENUM_NAME(DXGI_FORMAT_BC5_SNORM)
        ENUM_NAME(DXGI_FORMAT_B5G6R5_UNORM)
        ENUM_NAME(DXGI_FORMAT_B5G5R5A1_UNORM)
        ENUM_NAME(DXGI_FORMAT_B8G8R8A8_UNORM)
        ENUM_NAME(DXGI_FORMAT_B8G8R8X8_UNORM)
        ENUM_NAME(DXGI_FORMAT_R10G10B10_XR_BIAS_A2_UNORM)
        ENUM_NAME(DXGI_FORMAT_B8G8R8A8_TYPELESS)
        ENUM_NAME(DXGI_FORMAT_B8G8R8A8_UNORM_SRGB)
        ENUM_NAME(DXGI_FORMAT_B8G8R8X8_TYPELESS)
        ENUM_NAME(DXGI_FORMAT_B8G8R8X8_UNORM_SRGB)
        ENUM_NAME(DXGI_FORMAT_BC6H_TYPELESS)
        ENUM_NAME(DXGI_FORMAT_BC6H_UF16)
        ENUM_NAME(DXGI_FORMAT_BC6H_SF16)
        ENUM_NAME(DXGI_FORMAT_BC7_TYPELESS)
        ENUM_NAME(DXGI_FORMAT_BC7_UNORM)
        ENUM_NAME(DXGI_FORMAT_BC7_UNORM_SRGB)
        ENUM_NAME(DXGI_FORMAT_AYUV)
        ENUM_NAME(DXGI_FORMAT_Y410)
        ENUM_NAME(DXGI_FORMAT_Y416)
        ENUM_NAME(DXGI_FORMAT_NV12)
        ENUM_NAME(DXGI_FORMAT_P010)
        ENUM_NAME(DXGI_FORMAT_P016)
        ENUM_NAME(DXGI_FORMAT_420_OPAQUE)
        ENUM_NAME(DXGI_FORMAT_YUY2)
        ENUM_NAME(DXGI_FORMAT_Y210)
        ENUM_NAME(DXGI_FORMAT_Y216)
        ENUM_NAME(DXGI_FORMAT_NV11)
        ENUM_NAME(DXGI_FORMAT_AI44)
        ENUM_NAME(DXGI_FORMAT_IA44)
        ENUM_NAME(DXGI_FORMAT_P8)
        ENUM_NAME(DXGI_FORMAT_A8P8)
        ENUM_NAME(DXGI_FORMAT_B4G4R4A4_UNORM)
        ENUM_NAME(DXGI_FORMAT_P208)
        ENUM_NAME(DXGI_FORMAT_V208)
        ENUM_NAME(DXGI_FORMAT_V408)
        ENUM_NAME(DXGI_FORMAT_UNDOCUMENTED_ASTC_FIRST)
        ENUM_NAME(DXGI_FORMAT_UNDOCUMENTED_ASTC_LAST)
        ENUM_NAME(DXGI_FORMAT_SAMPLER_FEEDBACK_MIN_MIP_OPAQUE)
        ENUM_NAME(DXGI_FORMAT_SAMPLER_FEEDBACK_MIP_REGION_USED_OPAQUE)
        ENUM_NAME(DXGI_FORMAT_A4B4G4R4_UNORM)
        ENUM_NAME(DXGI_FORMAT_FORCE_UINT)
    }
    #undef ENUM_NAME

    return vkd3d_dbg_sprintf("Unknown DXGI_FORMAT (%u)",
        (uint32_t) format);
}

const char *debug_d3d12_box(const D3D12_BOX *box)
{
    if (!box)
        return "(null)";

    return vkd3d_dbg_sprintf("(%u, %u, %u)-(%u, %u, %u)",
            box->left, box->top, box->front,
            box->right, box->bottom, box->back);
}

static const char *debug_d3d12_shader_component(D3D12_SHADER_COMPONENT_MAPPING component)
{
    switch (component)
    {
        case D3D12_SHADER_COMPONENT_MAPPING_FROM_MEMORY_COMPONENT_0:
            return "r";
        case D3D12_SHADER_COMPONENT_MAPPING_FROM_MEMORY_COMPONENT_1:
            return "g";
        case D3D12_SHADER_COMPONENT_MAPPING_FROM_MEMORY_COMPONENT_2:
            return "b";
        case D3D12_SHADER_COMPONENT_MAPPING_FROM_MEMORY_COMPONENT_3:
            return "a";
        case D3D12_SHADER_COMPONENT_MAPPING_FORCE_VALUE_0:
            return "0";
        case D3D12_SHADER_COMPONENT_MAPPING_FORCE_VALUE_1:
            return "1";
    }

    FIXME("Invalid component mapping %#x.\n", component);
    return "invalid";
}

const char *debug_d3d12_shader_component_mapping(unsigned int mapping)
{
    return vkd3d_dbg_sprintf("{%s, %s, %s, %s}",
            debug_d3d12_shader_component(D3D12_DECODE_SHADER_4_COMPONENT_MAPPING(0, mapping)),
            debug_d3d12_shader_component(D3D12_DECODE_SHADER_4_COMPONENT_MAPPING(1, mapping)),
            debug_d3d12_shader_component(D3D12_DECODE_SHADER_4_COMPONENT_MAPPING(2, mapping)),
            debug_d3d12_shader_component(D3D12_DECODE_SHADER_4_COMPONENT_MAPPING(3, mapping)));
}

const char *debug_vk_extent_3d(VkExtent3D extent)
{
    return vkd3d_dbg_sprintf("(%u, %u, %u)",
            (unsigned int)extent.width,
            (unsigned int)extent.height,
            (unsigned int)extent.depth);
}

const char *debug_vk_queue_flags(VkQueueFlags flags, char buffer[VKD3D_DEBUG_FLAGS_BUFFER_SIZE])
{
    buffer[0] = '\0';
#define FLAG_TO_STR(f) if (flags & f) { strcat(buffer, " | "#f); flags &= ~f; }
    FLAG_TO_STR(VK_QUEUE_GRAPHICS_BIT)
    FLAG_TO_STR(VK_QUEUE_COMPUTE_BIT)
    FLAG_TO_STR(VK_QUEUE_TRANSFER_BIT)
    FLAG_TO_STR(VK_QUEUE_SPARSE_BINDING_BIT)
    FLAG_TO_STR(VK_QUEUE_PROTECTED_BIT)
    FLAG_TO_STR(VK_QUEUE_OPTICAL_FLOW_BIT_NV)
#undef FLAG_TO_STR
    if (flags)
        FIXME("Unrecognized flag(s) %#x.\n", flags);

    if (!buffer[0])
        return "0";
    return vkd3d_dbg_sprintf("%s", &buffer[3]);
}

const char *debug_vk_memory_heap_flags(VkMemoryHeapFlags flags, char buffer[VKD3D_DEBUG_FLAGS_BUFFER_SIZE])
{
    buffer[0] = '\0';
#define FLAG_TO_STR(f) if (flags & f) { strcat(buffer, " | "#f); flags &= ~f; }
    FLAG_TO_STR(VK_MEMORY_HEAP_DEVICE_LOCAL_BIT)
    FLAG_TO_STR(VK_MEMORY_HEAP_MULTI_INSTANCE_BIT)
#undef FLAG_TO_STR
    if (flags)
        FIXME("Unrecognized flag(s) %#x.\n", flags);

    if (!buffer[0])
        return "0";
    return vkd3d_dbg_sprintf("%s", &buffer[3]);
}

const char *debug_vk_memory_property_flags(VkMemoryPropertyFlags flags, char buffer[VKD3D_DEBUG_FLAGS_BUFFER_SIZE])
{
    buffer[0] = '\0';
#define FLAG_TO_STR(f) if (flags & f) { strcat(buffer, " | "#f); flags &= ~f; }
    FLAG_TO_STR(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
    FLAG_TO_STR(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)
    FLAG_TO_STR(VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
    FLAG_TO_STR(VK_MEMORY_PROPERTY_HOST_CACHED_BIT)
    FLAG_TO_STR(VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT)
    FLAG_TO_STR(VK_MEMORY_PROPERTY_PROTECTED_BIT)
    FLAG_TO_STR(VK_MEMORY_PROPERTY_DEVICE_COHERENT_BIT_AMD)
    FLAG_TO_STR(VK_MEMORY_PROPERTY_DEVICE_UNCACHED_BIT_AMD)
#undef FLAG_TO_STR
    if (flags)
        FIXME("Unrecognized flag(s) %#x.\n", flags);

    if (!buffer[0])
        return "0";
    return vkd3d_dbg_sprintf("%s", &buffer[3]);
}

HRESULT hresult_from_errno(int rc)
{
    switch (rc)
    {
        case 0:
            return S_OK;
        case ENOMEM:
            return E_OUTOFMEMORY;
        case EINVAL:
            return E_INVALIDARG;
        default:
            FIXME("Unhandled errno %d.\n", rc);
            return E_FAIL;
    }
}

HRESULT hresult_from_vk_result(VkResult vr)
{
    /* Wine tends to dispatch Vulkan calls to their own syscall stack.
     * Crashes are captured and return this magic VkResult.
     * Report it explicitly here so it's easier to debug when it happens. */
    if (vr == -1073741819)
    {
        ERR("Detected segfault in Wine syscall handler.\n");
        /* HACK: For ad-hoc debugging can also trigger backtrace printing here. */
        return E_POINTER;
    }

    switch (vr)
    {
        case VK_SUCCESS:
            return S_OK;
        case VK_ERROR_OUT_OF_DEVICE_MEMORY:
            WARN("Out of device memory.\n");
            /* fall-through */
        case VK_ERROR_OUT_OF_HOST_MEMORY:
            return E_OUTOFMEMORY;
        case VK_ERROR_VALIDATION_FAILED_EXT:
            /* NV driver sometimes returns this on invalid API usage. */
            return E_INVALIDARG;
        default:
            FIXME("Unhandled VkResult %d.\n", vr);
            /* fall-through */
        case VK_ERROR_DEVICE_LOST:
        case VK_ERROR_EXTENSION_NOT_PRESENT:
            return E_FAIL;
    }
}

HRESULT hresult_from_vkd3d_result(int vkd3d_result)
{
    switch (vkd3d_result)
    {
        case VKD3D_OK:
            return S_OK;
        case VKD3D_ERROR_INVALID_SHADER:
            WARN("Invalid shader bytecode.\n");
            /* fall-through */
        case VKD3D_ERROR:
            return E_FAIL;
        case VKD3D_ERROR_OUT_OF_MEMORY:
            return E_OUTOFMEMORY;
        case VKD3D_ERROR_INVALID_ARGUMENT:
            return E_INVALIDARG;
        case VKD3D_ERROR_NOT_IMPLEMENTED:
            return E_NOTIMPL;
        default:
            FIXME("Unhandled vkd3d result %d.\n", vkd3d_result);
            return E_FAIL;
    }
}

#define LOAD_GLOBAL_PFN(name) \
    if (!(procs->name = (void *)vkGetInstanceProcAddr(NULL, #name))) \
    { \
        ERR("Could not get global proc addr for '" #name "'.\n"); \
        return E_FAIL; \
    }
#define MAYBE_LOAD_GLOBAL_PFN(name) \
    procs->name = (void *)vkGetInstanceProcAddr(NULL, #name);

HRESULT vkd3d_load_vk_global_procs(struct vkd3d_vk_global_procs *procs,
        PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr)
{
    memset(procs, 0, sizeof(*procs));

    procs->vkGetInstanceProcAddr = vkGetInstanceProcAddr;

    LOAD_GLOBAL_PFN(vkCreateInstance)
    LOAD_GLOBAL_PFN(vkEnumerateInstanceExtensionProperties)
    LOAD_GLOBAL_PFN(vkEnumerateInstanceLayerProperties)
    MAYBE_LOAD_GLOBAL_PFN(vkEnumerateInstanceVersion)

    TRACE("Loaded global Vulkan procs.\n");
    return S_OK;
}

#define LOAD_INSTANCE_PFN(name) \
    if (!(procs->name = (void *)global_procs->vkGetInstanceProcAddr(instance, #name))) \
    { \
        ERR("Could not get instance proc addr for '" #name "'.\n"); \
        return E_FAIL; \
    }
#define LOAD_INSTANCE_OPTIONAL_PFN(name) \
    procs->name = (void *)global_procs->vkGetInstanceProcAddr(instance, #name);

HRESULT vkd3d_load_vk_instance_procs(struct vkd3d_vk_instance_procs *procs,
        const struct vkd3d_vk_global_procs *global_procs, VkInstance instance)
{
    memset(procs, 0, sizeof(*procs));

#define VK_INSTANCE_PFN     LOAD_INSTANCE_PFN
#define VK_INSTANCE_EXT_PFN LOAD_INSTANCE_OPTIONAL_PFN
#include "vulkan_procs.h"

    TRACE("Loaded procs for VkInstance %p.\n", instance);
    return S_OK;
}

#define COPY_PARENT_PFN(name) procs->name = parent_procs->name;
#define LOAD_DEVICE_PFN(name) \
    if (!(procs->name = (void *)procs->vkGetDeviceProcAddr(device, #name))) \
    { \
        ERR("Could not get device proc addr for '" #name "'.\n"); \
        return E_FAIL; \
    }
#define LOAD_DEVICE_OPTIONAL_PFN(name) \
    procs->name = (void *)procs->vkGetDeviceProcAddr(device, #name);

HRESULT vkd3d_load_vk_device_procs(struct vkd3d_vk_device_procs *procs,
        const struct vkd3d_vk_instance_procs *parent_procs, VkDevice device)
{
    memset(procs, 0, sizeof(*procs));

#define VK_INSTANCE_PFN       COPY_PARENT_PFN
#define VK_INSTANCE_EXT_PFN   COPY_PARENT_PFN
#define VK_DEVICE_PFN         LOAD_DEVICE_PFN
#define VK_DEVICE_EXT_PFN     LOAD_DEVICE_OPTIONAL_PFN
#include "vulkan_procs.h"

    TRACE("Loaded procs for VkDevice %p.\n", device);
    return S_OK;
}

static struct vkd3d_private_data *vkd3d_private_store_get_private_data(
        const struct vkd3d_private_store *store, const GUID *tag)
{
    struct vkd3d_private_data *data;

    LIST_FOR_EACH_ENTRY(data, &store->content, struct vkd3d_private_data, entry)
    {
        if (IsEqualGUID(&data->tag, tag))
            return data;
    }

    return NULL;
}

HRESULT vkd3d_private_store_set_private_data(struct vkd3d_private_store *store,
        const GUID *tag, const void *data, unsigned int data_size, bool is_object)
{
    struct vkd3d_private_data *d, *old_data;
    const void *ptr = data;

    if (!data)
    {
        if ((d = vkd3d_private_store_get_private_data(store, tag)))
        {
            vkd3d_private_data_destroy(d);
            return S_OK;
        }

        return S_FALSE;
    }

    if (is_object)
    {
        if (data_size != sizeof(IUnknown *))
            return E_INVALIDARG;
        ptr = &data;
    }

    if (!(d = vkd3d_malloc(offsetof(struct vkd3d_private_data, data[data_size]))))
        return E_OUTOFMEMORY;

    d->tag = *tag;
    d->size = data_size;
    d->is_object = is_object;
    memcpy(d->data, ptr, data_size);
    if (is_object)
        IUnknown_AddRef(d->object);

    if ((old_data = vkd3d_private_store_get_private_data(store, tag)))
        vkd3d_private_data_destroy(old_data);
    list_add_tail(&store->content, &d->entry);

    return S_OK;
}

HRESULT vkd3d_get_private_data(struct vkd3d_private_store *store,
        const GUID *tag, unsigned int *out_size, void *out)
{
    const struct vkd3d_private_data *data;
    unsigned int size;
    HRESULT hr;

    if (!out_size)
        return E_INVALIDARG;

    if (FAILED(hr = vkd3d_private_data_lock(store)))
        return hr;

    if (!(data = vkd3d_private_store_get_private_data(store, tag)))
    {
        *out_size = 0;
        hr = DXGI_ERROR_NOT_FOUND;
        goto done;
    }

    size = *out_size;
    *out_size = data->size;
    if (!out)
        goto done;

    if (size < data->size)
    {
        hr = DXGI_ERROR_MORE_DATA;
        goto done;
    }

    if (data->is_object)
        IUnknown_AddRef(data->object);
    memcpy(out, data->data, data->size);

done:
    vkd3d_private_data_unlock(store);
    return hr;
}

HRESULT STDMETHODCALLTYPE d3d12_object_SetName(ID3D12Object *iface, const WCHAR *name)
{
    size_t size = 0;

    TRACE("iface %p, name %s.\n", iface, debugstr_w(name));

    if (name)
        size = sizeof(WCHAR) * (vkd3d_wcslen(name) + 1);

    return ID3D12Object_SetPrivateData(iface, &WKPDID_D3DDebugObjectNameW, size, name);
}

HRESULT vkd3d_set_vk_object_name(struct d3d12_device *device, uint64_t vk_object,
        VkObjectType vk_object_type, const char *name)
{
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    VkDebugUtilsObjectNameInfoEXT info;
    VkResult vr;

    if (!device->vk_info.EXT_debug_utils)
        return VK_SUCCESS;

    info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
    info.pNext = NULL;
    info.objectType = vk_object_type;
    info.objectHandle = vk_object;
    info.pObjectName = name;

    vr = VK_CALL(vkSetDebugUtilsObjectNameEXT(device->vk_device, &info));
    return hresult_from_vk_result(vr);
}

static struct d3d_blob *impl_from_ID3DBlob(ID3DBlob *iface)
{
    return CONTAINING_RECORD(iface, struct d3d_blob, ID3DBlob_iface);
}

static HRESULT STDMETHODCALLTYPE d3d_blob_QueryInterface(ID3DBlob *iface, REFIID riid, void **object)
{
    TRACE("iface %p, riid %s, object %p.\n", iface, debugstr_guid(riid), object);

    if (IsEqualGUID(riid, &IID_ID3DBlob)
            || IsEqualGUID(riid, &IID_IUnknown))
    {
        ID3D10Blob_AddRef(iface);
        *object = iface;
        return S_OK;
    }

    WARN("%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid(riid));

    *object = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE d3d_blob_AddRef(ID3DBlob *iface)
{
    struct d3d_blob *blob = impl_from_ID3DBlob(iface);
    ULONG refcount = InterlockedIncrement(&blob->refcount);

    TRACE("%p increasing refcount to %u.\n", blob, refcount);

    return refcount;
}

static ULONG STDMETHODCALLTYPE d3d_blob_Release(ID3DBlob *iface)
{
    struct d3d_blob *blob = impl_from_ID3DBlob(iface);
    ULONG refcount = InterlockedDecrement(&blob->refcount);

    TRACE("%p decreasing refcount to %u.\n", blob, refcount);

    if (!refcount)
    {
        vkd3d_free(blob->buffer);

        vkd3d_free(blob);
    }

    return refcount;
}

static void * STDMETHODCALLTYPE d3d_blob_GetBufferPointer(ID3DBlob *iface)
{
    struct d3d_blob *blob = impl_from_ID3DBlob(iface);

    TRACE("iface %p.\n", iface);

    return blob->buffer;
}

static SIZE_T STDMETHODCALLTYPE d3d_blob_GetBufferSize(ID3DBlob *iface)
{
    struct d3d_blob *blob = impl_from_ID3DBlob(iface);

    TRACE("iface %p.\n", iface);

    return blob->size;
}

static CONST_VTBL struct ID3D10BlobVtbl d3d_blob_vtbl =
{
    /* IUnknown methods */
    d3d_blob_QueryInterface,
    d3d_blob_AddRef,
    d3d_blob_Release,
    /* ID3DBlob methods */
    d3d_blob_GetBufferPointer,
    d3d_blob_GetBufferSize
};

static void d3d_blob_init(struct d3d_blob *blob, void *buffer, SIZE_T size)
{
    blob->ID3DBlob_iface.lpVtbl = &d3d_blob_vtbl;
    blob->refcount = 1;

    blob->buffer = buffer;
    blob->size = size;
}

HRESULT d3d_blob_create(void *buffer, SIZE_T size, struct d3d_blob **blob)
{
    struct d3d_blob *object;

    if (!(object = vkd3d_malloc(sizeof(*object))))
        return E_OUTOFMEMORY;

    d3d_blob_init(object, buffer, size);

    TRACE("Created blob object %p.\n", object);

    *blob = object;

    return S_OK;
}


static struct d3d_destruction_notifier *impl_from_ID3DDestructionNotifier(ID3DDestructionNotifier *iface)
{
    return CONTAINING_RECORD(iface, struct d3d_destruction_notifier, ID3DDestructionNotifier_iface);
}

static HRESULT STDMETHODCALLTYPE d3d_destruction_notifier_QueryInterface(ID3DDestructionNotifier *iface, REFIID riid, void **object)
{
    struct d3d_destruction_notifier *notifier = impl_from_ID3DDestructionNotifier(iface);

    TRACE("iface %p, riid %s, object %p.\n", iface, debugstr_guid(riid), object);

    return IUnknown_QueryInterface(notifier->parent, riid, object);
}

static ULONG STDMETHODCALLTYPE d3d_destruction_notifier_AddRef(ID3DDestructionNotifier *iface)
{
    struct d3d_destruction_notifier *notifier = impl_from_ID3DDestructionNotifier(iface);
    ULONG refcount = IUnknown_AddRef(notifier->parent);

    TRACE("%p increasing refcount to %u.\n", notifier, refcount);

    return refcount;
}

static ULONG STDMETHODCALLTYPE d3d_destruction_notifier_Release(ID3DDestructionNotifier *iface)
{
    struct d3d_destruction_notifier *notifier = impl_from_ID3DDestructionNotifier(iface);
    ULONG refcount = IUnknown_Release(notifier->parent);

    TRACE("%p decreasing refcount to %u.\n", notifier, refcount);

    return refcount;
}

static HRESULT STDMETHODCALLTYPE d3d_destruction_notifier_RegisterDestructionCallback(
        ID3DDestructionNotifier *iface, PFN_DESTRUCTION_CALLBACK callback, void *data,
        UINT *callback_id)
{
    struct d3d_destruction_notifier *notifier = impl_from_ID3DDestructionNotifier(iface);
    struct d3d_destruction_callback_entry *entry;

    TRACE("iface %p, callback %p, data %p, callback_id %p.\n", iface, (void*)callback, data, callback_id);

    if (!callback)
        return DXGI_ERROR_INVALID_CALL;

    pthread_mutex_lock(&notifier->mutex);

    if (!vkd3d_array_reserve((void**)&notifier->callbacks, &notifier->callback_size,
            notifier->callback_count + 1, sizeof(*notifier->callbacks)))
    {
        ERR("Failed to allocate callback array.\n");
        pthread_mutex_unlock(&notifier->mutex);
        return E_OUTOFMEMORY;
    }

    entry = &notifier->callbacks[notifier->callback_count++];
    entry->callback = callback;
    entry->userdata = data;
    entry->callback_id = 0;

    if (callback_id)
    {
        entry->callback_id = ++notifier->next_callback_id;
        *callback_id = entry->callback_id;
    }

    pthread_mutex_unlock(&notifier->mutex);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d3d_destruction_notifier_UnregisterDestructionCallback(
        ID3DDestructionNotifier *iface, UINT callback_id)
{
    struct d3d_destruction_notifier *notifier = impl_from_ID3DDestructionNotifier(iface);
    unsigned int i, entry_index;

    TRACE("iface %p, callback_id %u.\n", iface, callback_id);

    if (!callback_id)
        return DXGI_ERROR_NOT_FOUND;

    pthread_mutex_lock(&notifier->mutex);

    entry_index = -1u;

    for (i = 0; i < notifier->callback_count; i++)
    {
        if (notifier->callbacks[i].callback_id == callback_id)
        {
            entry_index = i;
            break;
        }
    }

    if (entry_index == -1u)
    {
        pthread_mutex_unlock(&notifier->mutex);
        return DXGI_ERROR_NOT_FOUND;
    }

    notifier->callbacks[entry_index] = notifier->callbacks[--notifier->callback_count];

    pthread_mutex_unlock(&notifier->mutex);
    return S_OK;
}

static CONST_VTBL struct ID3DDestructionNotifierVtbl d3d_destruction_notifier_vtbl =
{
    /* IUnknown methods */
    d3d_destruction_notifier_QueryInterface,
    d3d_destruction_notifier_AddRef,
    d3d_destruction_notifier_Release,
    /* ID3DDestructionNotifier methods */
    d3d_destruction_notifier_RegisterDestructionCallback,
    d3d_destruction_notifier_UnregisterDestructionCallback
};

void d3d_destruction_notifier_init(struct d3d_destruction_notifier *notifier, IUnknown *parent)
{
    memset(notifier, 0, sizeof(*notifier));

    notifier->ID3DDestructionNotifier_iface.lpVtbl = &d3d_destruction_notifier_vtbl;
    notifier->parent = parent;

    pthread_mutex_init(&notifier->mutex, NULL);
}

void d3d_destruction_notifier_free(struct d3d_destruction_notifier *notifier)
{
    d3d_destruction_notifier_notify(notifier);

    pthread_mutex_destroy(&notifier->mutex);
}

void d3d_destruction_notifier_notify(struct d3d_destruction_notifier *notifier)
{
    size_t i;

    for (i = 0; i < notifier->callback_count; i++)
    {
        const struct d3d_destruction_callback_entry *entry = &notifier->callbacks[i];

        entry->callback(entry->userdata);
    }

    vkd3d_free(notifier->callbacks);

    notifier->callbacks = NULL;
    notifier->callback_count = 0u;
    notifier->callback_size = 0u;
}

