/*
 * Copyright 2021 Hans-Kristian Arntzen for Valve Corporation
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

void vkd3d_acceleration_structure_build_info_cleanup(
        struct vkd3d_acceleration_structure_build_info *info)
{
    if (info->primitive_counts != info->primitive_counts_stack)
        vkd3d_free(info->primitive_counts);
    if (info->geometries != info->geometries_stack)
        vkd3d_free(info->geometries);
    if (info->build_range_ptrs != info->build_range_ptr_stack)
        vkd3d_free(info->build_range_ptrs);
    if (info->build_ranges != info->build_range_stack)
        vkd3d_free(info->build_ranges);
}

static VkBuildAccelerationStructureFlagsKHR d3d12_build_flags_to_vk(
        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS flags)
{
    VkBuildAccelerationStructureFlagsKHR vk_flags = 0;

    if (flags & D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_COMPACTION)
        vk_flags |= VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_COMPACTION_BIT_KHR;
    if (flags & D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE)
        vk_flags |= VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR;
    if (flags & D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_MINIMIZE_MEMORY)
        vk_flags |= VK_BUILD_ACCELERATION_STRUCTURE_LOW_MEMORY_BIT_KHR;
    if (flags & D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_BUILD)
        vk_flags |= VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR;
    if (flags & D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE)
        vk_flags |= VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;

    return vk_flags;
}

bool vkd3d_acceleration_structure_convert_inputs(
        struct vkd3d_acceleration_structure_build_info *info,
        const D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS *desc)
{
    VkAccelerationStructureGeometryTrianglesDataKHR *triangles;
    VkAccelerationStructureBuildGeometryInfoKHR *build_info;
    VkAccelerationStructureGeometryAabbsDataKHR *aabbs;
    const D3D12_RAYTRACING_GEOMETRY_DESC *geom_desc;
    unsigned int i;

    build_info = &info->build_info;
    memset(build_info, 0, sizeof(*build_info));
    build_info->sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;

    if (desc->Type == D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL)
        build_info->type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    else
        build_info->type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;

    build_info->flags = d3d12_build_flags_to_vk(desc->Flags);

    if (desc->Flags & D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PERFORM_UPDATE)
        build_info->mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR;
    else
        build_info->mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;

    info->geometries = info->geometries_stack;
    info->primitive_counts = info->primitive_counts_stack;
    info->build_ranges = info->build_range_stack;
    info->build_range_ptrs = info->build_range_ptr_stack;

    if (desc->Type == D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL)
    {
        memset(info->geometries, 0, sizeof(*info->geometries));
        info->geometries[0].sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
        info->geometries[0].geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
        info->geometries[0].geometry.instances.sType =
                VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
        info->geometries[0].geometry.instances.arrayOfPointers =
                desc->DescsLayout == D3D12_ELEMENTS_LAYOUT_ARRAY_OF_POINTERS ? VK_TRUE : VK_FALSE;
        info->geometries[0].geometry.instances.data.deviceAddress = desc->InstanceDescs;

        info->primitive_counts = info->primitive_counts_stack;
        info->primitive_counts[0] = desc->NumDescs;
        build_info->geometryCount = 1;
    }
    else
    {
        if (desc->NumDescs <= VKD3D_BUILD_INFO_STACK_COUNT)
        {
            memset(info->geometries, 0, sizeof(*info->geometries) * desc->NumDescs);
            memset(info->primitive_counts, 0, sizeof(*info->primitive_counts) * desc->NumDescs);
        }
        else
        {
            info->geometries = vkd3d_calloc(desc->NumDescs, sizeof(*info->geometries));
            info->primitive_counts = vkd3d_calloc(desc->NumDescs, sizeof(*info->primitive_counts));
            info->build_ranges = vkd3d_malloc(desc->NumDescs * sizeof(*info->build_ranges));
            info->build_range_ptrs = vkd3d_malloc(desc->NumDescs * sizeof(*info->build_range_ptrs));
        }
        build_info->geometryCount = desc->NumDescs;

        for (i = 0; i < desc->NumDescs; i++)
        {
            info->geometries[i].sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;

            if (desc->DescsLayout == D3D12_ELEMENTS_LAYOUT_ARRAY_OF_POINTERS)
                geom_desc = desc->ppGeometryDescs[i];
            else
                geom_desc = &desc->pGeometryDescs[i];

            switch (geom_desc->Type)
            {
                case D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES:
                    info->geometries[i].geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
                    triangles = &info->geometries[i].geometry.triangles;
                    triangles->sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
                    triangles->indexData.deviceAddress = geom_desc->Triangles.IndexBuffer;
                    if (geom_desc->Triangles.IndexBuffer)
                    {
                        triangles->indexType =
                                geom_desc->Triangles.IndexFormat == DXGI_FORMAT_R16_UINT ?
                                        VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32;
                        info->primitive_counts[i] = geom_desc->Triangles.IndexCount / 3;
                    }
                    else
                    {
                        info->primitive_counts[i] = geom_desc->Triangles.VertexCount / 3;
                        triangles->indexType = VK_INDEX_TYPE_NONE_KHR;
                    }

                    triangles->maxVertex = max(1, geom_desc->Triangles.VertexCount) - 1;
                    triangles->vertexStride = geom_desc->Triangles.VertexBuffer.StrideInBytes;
                    triangles->vertexFormat = vkd3d_get_vk_format(geom_desc->Triangles.VertexFormat);
                    triangles->vertexData.deviceAddress = geom_desc->Triangles.VertexBuffer.StartAddress;
                    triangles->transformData.deviceAddress = geom_desc->Triangles.Transform3x4;
                    break;

                case D3D12_RAYTRACING_GEOMETRY_TYPE_PROCEDURAL_PRIMITIVE_AABBS:
                    info->geometries[i].geometryType = VK_GEOMETRY_TYPE_AABBS_KHR;
                    aabbs = &info->geometries[i].geometry.aabbs;
                    aabbs->sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_AABBS_DATA_KHR;
                    aabbs->stride = geom_desc->AABBs.AABBs.StrideInBytes;
                    aabbs->data.deviceAddress = geom_desc->AABBs.AABBs.StartAddress;
                    info->primitive_counts[i] = geom_desc->AABBs.AABBCount;
                    break;

                default:
                    FIXME("Unsupported geometry type %u.\n", geom_desc->Type);
                    return false;
            }
        }
    }

    for (i = 0; i < build_info->geometryCount; i++)
    {
        info->build_range_ptrs[i] = &info->build_ranges[i];
        info->build_ranges[i].primitiveCount = info->primitive_counts[i];
        info->build_ranges[i].firstVertex = 0;
        info->build_ranges[i].primitiveOffset = 0;
        info->build_ranges[i].transformOffset = 0;
    }

    build_info->pGeometries = info->geometries;
    return true;
}
