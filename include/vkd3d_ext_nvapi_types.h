/*
 * * Copyright 2026 Krzysztof Bogacki
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

#ifndef __VKD3D_EXT_NVAPI_TYPES_H
#define __VKD3D_EXT_NVAPI_TYPES_H

#include "vkd3d_common.h"
#include "vkd3d_d3d12.h"

typedef enum D3D12_VK_EXT_RAYTRACING_GEOMETRY_TYPE
{
    D3D12_VK_EXT_RAYTRACING_GEOMETRY_TYPE_TRIANGLES = 0,
    D3D12_VK_EXT_RAYTRACING_GEOMETRY_TYPE_PROCEDURAL_PRIMITIVE_AABBS = 1,
    D3D12_VK_EXT_RAYTRACING_GEOMETRY_TYPE_OMM_TRIANGLES = 2,

    D3D12_VK_EXT_RAYTRACING_GEOMETRY_TYPE_SPHERES_NVAPI = 0x10de0004,
    D3D12_VK_EXT_RAYTRACING_GEOMETRY_TYPE_LSS_NVAPI = 0x10de0005,
} D3D12_VK_EXT_RAYTRACING_GEOMETRY_TYPE;

/*
 * Types with names ending with _NVAPI below are mostly renamed types from
 * NVAPI open source headers, originally licensed under terms of MIT license
 */

typedef struct D3D12_VK_EXT_RAYTRACING_GEOMETRY_SPHERES_DESC_NVAPI
{
    UINT VertexCount;
    UINT IndexCount;
    D3D12_GPU_VIRTUAL_ADDRESS_AND_STRIDE VertexPositionBuffer;
    DXGI_FORMAT VertexPositionFormat;
    D3D12_GPU_VIRTUAL_ADDRESS_AND_STRIDE VertexRadiusBuffer;
    DXGI_FORMAT VertexRadiusFormat;
    D3D12_GPU_VIRTUAL_ADDRESS_AND_STRIDE IndexBuffer; 
    DXGI_FORMAT IndexFormat;
} D3D12_VK_EXT_RAYTRACING_GEOMETRY_SPHERES_DESC_NVAPI;

typedef enum D3D12_VK_EXT_RAYTRACING_LSS_ENDCAP_MODE_NVAPI
{
    D3D12_VK_EXT_RAYTRACING_LSS_ENDCAP_MODE_NVAPI_NONE = 0,
    D3D12_VK_EXT_RAYTRACING_LSS_ENDCAP_MODE_NVAPI_CHAINED = 1,
} D3D12_VK_EXT_RAYTRACING_LSS_ENDCAP_MODE_NVAPI;

typedef enum D3D12_VK_EXT_RAYTRACING_LSS_PRIMITIVE_FORMAT_NVAPI
{
    D3D12_VK_EXT_RAYTRACING_LSS_PRIMITIVE_FORMAT_NVAPI_LIST = 0,
    D3D12_VK_EXT_RAYTRACING_LSS_PRIMITIVE_FORMAT_NVAPI_SUCCESSIVE_IMPLICIT = 1,
} D3D12_VK_EXT_RAYTRACING_LSS_PRIMITIVE_FORMAT_NVAPI;

typedef struct D3D12_VK_EXT_RAYTRACING_GEOMETRY_LSS_DESC_NVAPI
{
    UINT VertexCount;
    UINT IndexCount;
    UINT PrimitiveCount;
    D3D12_GPU_VIRTUAL_ADDRESS_AND_STRIDE VertexPositionBuffer;
    DXGI_FORMAT VertexPositionFormat;
    D3D12_GPU_VIRTUAL_ADDRESS_AND_STRIDE VertexRadiusBuffer;
    DXGI_FORMAT VertexRadiusFormat;
    D3D12_GPU_VIRTUAL_ADDRESS_AND_STRIDE IndexBuffer;
    DXGI_FORMAT IndexFormat;
    D3D12_VK_EXT_RAYTRACING_LSS_ENDCAP_MODE_NVAPI EndcapMode;
    D3D12_VK_EXT_RAYTRACING_LSS_PRIMITIVE_FORMAT_NVAPI PrimitiveFormat;
} D3D12_VK_EXT_RAYTRACING_GEOMETRY_LSS_DESC_NVAPI;

typedef struct D3D12_VK_EXT_RAYTRACING_GEOMETRY_DESC
{
    D3D12_VK_EXT_RAYTRACING_GEOMETRY_TYPE Type;
    D3D12_RAYTRACING_GEOMETRY_FLAGS Flags;
    union
    {
        D3D12_RAYTRACING_GEOMETRY_TRIANGLES_DESC Triangles;
        D3D12_RAYTRACING_GEOMETRY_AABBS_DESC AABBs;
        D3D12_RAYTRACING_GEOMETRY_OMM_TRIANGLES_DESC OmmTriangles;

        D3D12_VK_EXT_RAYTRACING_GEOMETRY_SPHERES_DESC_NVAPI* Spheres;
        D3D12_VK_EXT_RAYTRACING_GEOMETRY_LSS_DESC_NVAPI* LSS;
    };
} D3D12_VK_EXT_RAYTRACING_GEOMETRY_DESC;

STATIC_ASSERT(sizeof(D3D12_VK_EXT_RAYTRACING_GEOMETRY_DESC) == sizeof(D3D12_RAYTRACING_GEOMETRY_DESC));

#endif /* __VKD3D_EXT_NVAPI_TYPES_H */
