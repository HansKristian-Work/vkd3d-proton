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

static VkFormat convert_rt_vertex_format(DXGI_FORMAT format)
{
    /* DXR specifies RGBA16 or RGBA8 here, but it also completely ignores A,
     * so it's *actually* DXGI_FORMAT_RGB{8,16}_{FLOAT,SNORM},
     * but those formats don't exist in D3D,
     * and they couldn't be bothered to add those apparently <_<. */
    switch (format)
    {
        case DXGI_FORMAT_R16G16B16A16_FLOAT:
            return VK_FORMAT_R16G16B16_SFLOAT;
        case DXGI_FORMAT_R16G16B16A16_SNORM:
            return VK_FORMAT_R16G16B16_SNORM;
        case DXGI_FORMAT_R16G16B16A16_UNORM:
            return VK_FORMAT_R16G16B16_UNORM;
        case DXGI_FORMAT_R8G8B8A8_SNORM:
            return VK_FORMAT_R8G8B8_SNORM;
        case DXGI_FORMAT_R8G8B8A8_UNORM:
            return VK_FORMAT_R8G8B8_UNORM;
        default:
            return vkd3d_get_vk_format(format);
    }
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
                    triangles->vertexFormat = convert_rt_vertex_format(geom_desc->Triangles.VertexFormat);
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

static void vkd3d_acceleration_structure_end_barrier(struct d3d12_command_list *list)
{
    /* We resolve the query in TRANSFER, but DXR expects UNORDERED_ACCESS. */
    const struct vkd3d_vk_device_procs *vk_procs = &list->device->vk_procs;
    VkMemoryBarrier barrier;

    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    barrier.pNext = NULL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = 0;

    VK_CALL(vkCmdPipelineBarrier(list->vk_command_buffer,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0,
            1, &barrier, 0, NULL, 0, NULL));
}

static void vkd3d_acceleration_structure_write_postbuild_info(
        struct d3d12_command_list *list,
        const D3D12_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO_DESC *desc,
        VkDeviceSize desc_offset,
        VkAccelerationStructureKHR vk_acceleration_structure)
{
    const struct vkd3d_vk_device_procs *vk_procs = &list->device->vk_procs;
    const struct vkd3d_unique_resource *resource;
    VkQueryPool vk_query_pool;
    VkQueryType vk_query_type;
    uint32_t vk_query_index;
    VkDeviceSize stride;
    uint32_t type_index;
    VkBuffer vk_buffer;
    uint32_t offset;

    resource = vkd3d_va_map_deref(&list->device->memory_allocator.va_map, desc->DestBuffer);
    if (!resource)
    {
        ERR("Invalid resource.\n");
        return;
    }

    vk_buffer = resource->vk_buffer;
    offset = desc->DestBuffer - resource->va;
    offset += desc_offset;

    if (desc->InfoType == D3D12_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO_COMPACTED_SIZE)
    {
        vk_query_type = VK_QUERY_TYPE_ACCELERATION_STRUCTURE_COMPACTED_SIZE_KHR;
        type_index = VKD3D_QUERY_TYPE_INDEX_RT_COMPACTED_SIZE;
        stride = sizeof(uint64_t);
    }
    else if (desc->InfoType == D3D12_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO_SERIALIZATION)
    {
        vk_query_type = VK_QUERY_TYPE_ACCELERATION_STRUCTURE_SERIALIZATION_SIZE_KHR;
        type_index = VKD3D_QUERY_TYPE_INDEX_RT_SERIALIZE_SIZE;
        stride = sizeof(uint64_t);
        FIXME("NumBottomLevelPointers will always return 0.\n");
    }
    else
    {
        FIXME("Unsupported InfoType %u.\n", desc->InfoType);
        /* TODO: CURRENT_SIZE is something we cannot query in Vulkan, so
         * we'll need to keep around a buffer to handle this.
         * For now, just clear to 0. */
        VK_CALL(vkCmdFillBuffer(list->vk_command_buffer, vk_buffer, offset,
                sizeof(uint64_t), 0));
        return;
    }

    if (!d3d12_command_allocator_allocate_query_from_type_index(list->allocator,
            type_index, &vk_query_pool, &vk_query_index))
    {
        ERR("Failed to allocate query.\n");
        return;
    }

    d3d12_command_list_reset_query(list, vk_query_pool, vk_query_index);

    VK_CALL(vkCmdWriteAccelerationStructuresPropertiesKHR(list->vk_command_buffer,
            1, &vk_acceleration_structure, vk_query_type, vk_query_pool, vk_query_index));
    VK_CALL(vkCmdCopyQueryPoolResults(list->vk_command_buffer,
            vk_query_pool, vk_query_index, 1,
            vk_buffer, offset, stride,
            VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT));

    if (desc->InfoType == D3D12_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO_SERIALIZATION)
    {
        /* TODO: We'll need some way to store these values for later use and copy them here instead. */
        VK_CALL(vkCmdFillBuffer(list->vk_command_buffer, vk_buffer, offset + sizeof(uint64_t),
                sizeof(uint64_t), 0));
    }
}

void vkd3d_acceleration_structure_emit_postbuild_info(
        struct d3d12_command_list *list,
        const D3D12_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO_DESC *desc,
        uint32_t count,
        const D3D12_GPU_VIRTUAL_ADDRESS *addresses)
{
    const struct vkd3d_vk_device_procs *vk_procs = &list->device->vk_procs;
    VkAccelerationStructureKHR vk_acceleration_structure;
    VkMemoryBarrier barrier;
    VkDeviceSize stride;
    uint32_t i;

    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    barrier.pNext = NULL;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

    /* We resolve the query in TRANSFER, but DXR expects UNORDERED_ACCESS. */
    VK_CALL(vkCmdPipelineBarrier(list->vk_command_buffer,
            VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
            1, &barrier, 0, NULL, 0, NULL));

    stride = desc->InfoType == D3D12_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO_SERIALIZATION ?
            2 * sizeof(uint64_t) : sizeof(uint64_t);

    for (i = 0; i < count; i++)
    {
        vk_acceleration_structure = vkd3d_va_map_place_acceleration_structure(
                &list->device->memory_allocator.va_map, list->device, addresses[i]);
        if (vk_acceleration_structure)
            vkd3d_acceleration_structure_write_postbuild_info(list, desc, i * stride, vk_acceleration_structure);
        else
            ERR("Failed to query acceleration structure for VA 0x%"PRIx64".\n", addresses[i]);
    }

    vkd3d_acceleration_structure_end_barrier(list);
}

void vkd3d_acceleration_structure_emit_immediate_postbuild_info(
        struct d3d12_command_list *list, uint32_t count,
        const D3D12_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO_DESC *desc,
        VkAccelerationStructureKHR vk_acceleration_structure)
{
    /* In D3D12 we are supposed to be able to emit without an explicit barrier,
     * but we need to emit them for Vulkan. */

    const struct vkd3d_vk_device_procs *vk_procs = &list->device->vk_procs;
    VkMemoryBarrier barrier;
    uint32_t i;

    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    barrier.pNext = NULL;
    barrier.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
    /* The query accesses STRUCTURE_READ_BIT in BUILD_BIT stage. */
    barrier.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_TRANSFER_WRITE_BIT;

    /* Writing to the result buffer is supposed to happen in UNORDERED_ACCESS on DXR for
     * some bizarre reason, so we have to satisfy a transfer barrier.
     * Have to basically do a full stall to make this work ... */
    VK_CALL(vkCmdPipelineBarrier(list->vk_command_buffer,
            VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
            VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR | VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
            1, &barrier, 0, NULL, 0, NULL));

    /* Could optimize a bit by batching more aggressively, but no idea if it's going to help in practice. */
    for (i = 0; i < count; i++)
        vkd3d_acceleration_structure_write_postbuild_info(list, &desc[i], 0, vk_acceleration_structure);

    vkd3d_acceleration_structure_end_barrier(list);
}

static bool convert_copy_mode(
        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_COPY_MODE mode,
        VkCopyAccelerationStructureModeKHR *vk_mode)
{
    switch (mode)
    {
        case D3D12_RAYTRACING_ACCELERATION_STRUCTURE_COPY_MODE_CLONE:
            *vk_mode = VK_COPY_ACCELERATION_STRUCTURE_MODE_CLONE_KHR;
            return true;
        case D3D12_RAYTRACING_ACCELERATION_STRUCTURE_COPY_MODE_COMPACT:
            *vk_mode = VK_COPY_ACCELERATION_STRUCTURE_MODE_COMPACT_KHR;
            return true;
        default:
            FIXME("Unsupported RTAS copy mode #%x.\n", mode);
            return false;
    }
}

void vkd3d_acceleration_structure_copy(
        struct d3d12_command_list *list,
        D3D12_GPU_VIRTUAL_ADDRESS dst, D3D12_GPU_VIRTUAL_ADDRESS src,
        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_COPY_MODE mode)
{
    const struct vkd3d_vk_device_procs *vk_procs = &list->device->vk_procs;
    VkAccelerationStructureKHR dst_as, src_as;
    VkCopyAccelerationStructureInfoKHR info;

    dst_as = vkd3d_va_map_place_acceleration_structure(&list->device->memory_allocator.va_map, list->device, dst);
    if (dst_as == VK_NULL_HANDLE)
    {
        ERR("Invalid dst address #%"PRIx64" for RTAS copy.\n", dst);
        return;
    }

    src_as = vkd3d_va_map_place_acceleration_structure(&list->device->memory_allocator.va_map, list->device, src);
    if (src_as == VK_NULL_HANDLE)
    {
        ERR("Invalid src address #%"PRIx64" for RTAS copy.\n", src);
        return;
    }

    info.sType = VK_STRUCTURE_TYPE_COPY_ACCELERATION_STRUCTURE_INFO_KHR;
    info.pNext = NULL;
    info.dst = dst_as;
    info.src = src_as;
    if (convert_copy_mode(mode, &info.mode))
        VK_CALL(vkCmdCopyAccelerationStructureKHR(list->vk_command_buffer, &info));
}
