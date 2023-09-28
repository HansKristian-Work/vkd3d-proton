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

#define RT_TRACE TRACE

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

static VkGeometryFlagsKHR d3d12_geometry_flags_to_vk(D3D12_RAYTRACING_GEOMETRY_FLAGS flags)
{
    VkGeometryFlagsKHR vk_flags = 0;

    if (flags & D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE)
        vk_flags |= VK_GEOMETRY_OPAQUE_BIT_KHR;
    if (flags & D3D12_RAYTRACING_GEOMETRY_FLAG_NO_DUPLICATE_ANYHIT_INVOCATION)
        vk_flags |= VK_GEOMETRY_NO_DUPLICATE_ANY_HIT_INVOCATION_BIT_KHR;

    return vk_flags;
}

uint32_t vkd3d_acceleration_structure_get_geometry_count(
        const D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS *desc)
{
    if (desc->Type == D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL)
        return 1;
    else
        return desc->NumDescs;
}

bool vkd3d_acceleration_structure_convert_inputs(const struct d3d12_device *device,
        const D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS *desc,
        VkAccelerationStructureBuildGeometryInfoKHR *build_info,
        VkAccelerationStructureGeometryKHR *geometry_infos,
        VkAccelerationStructureBuildRangeInfoKHR *range_infos,
        uint32_t *primitive_counts)
{
    VkAccelerationStructureGeometryTrianglesDataKHR *triangles;
    VkAccelerationStructureGeometryAabbsDataKHR *aabbs;
    const D3D12_RAYTRACING_GEOMETRY_DESC *geom_desc;
    bool have_triangles, have_aabbs;
    uint32_t primitive_count;
    unsigned int i;

    RT_TRACE("Converting inputs.\n");
    RT_TRACE("=====================\n");

    memset(build_info, 0, sizeof(*build_info));
    build_info->sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;

    if (desc->Type == D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL)
    {
        build_info->type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
        RT_TRACE("Top level build.\n");
    }
    else
    {
        build_info->type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
        RT_TRACE("Bottom level build.\n");
    }

    build_info->flags = d3d12_build_flags_to_vk(desc->Flags);

    if (desc->Flags & D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PERFORM_UPDATE)
    {
        RT_TRACE("BUILD_FLAG_PERFORM_UPDATE.\n");
        build_info->mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR;
    }
    else
        build_info->mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;

    if (desc->Type == D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL)
    {
        memset(geometry_infos, 0, sizeof(*geometry_infos));
        geometry_infos[0].sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
        geometry_infos[0].geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
        geometry_infos[0].geometry.instances.sType =
                VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
        geometry_infos[0].geometry.instances.arrayOfPointers =
                desc->DescsLayout == D3D12_ELEMENTS_LAYOUT_ARRAY_OF_POINTERS ? VK_TRUE : VK_FALSE;
        geometry_infos[0].geometry.instances.data.deviceAddress = desc->InstanceDescs;

        if (primitive_counts)
            primitive_counts[0] = desc->NumDescs;

        if (range_infos)
        {
            range_infos[0].primitiveCount = desc->NumDescs;
            range_infos[0].firstVertex = 0;
            range_infos[0].primitiveOffset = 0;
            range_infos[0].transformOffset = 0;
        }

        build_info->geometryCount = 1;
        RT_TRACE("  ArrayOfPointers: %u.\n",
                desc->DescsLayout == D3D12_ELEMENTS_LAYOUT_ARRAY_OF_POINTERS ? 1 : 0);
        RT_TRACE("  NumDescs: %u.\n", desc->NumDescs);
    }
    else
    {
        have_triangles = false;
        have_aabbs = false;

        memset(geometry_infos, 0, sizeof(*geometry_infos) * desc->NumDescs);

        if (primitive_counts)
            memset(primitive_counts, 0, sizeof(*primitive_counts) * desc->NumDescs);

        build_info->geometryCount = desc->NumDescs;

        for (i = 0; i < desc->NumDescs; i++)
        {
            geometry_infos[i].sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
            RT_TRACE(" Geom %u:\n", i);

            if (desc->DescsLayout == D3D12_ELEMENTS_LAYOUT_ARRAY_OF_POINTERS)
            {
                geom_desc = desc->ppGeometryDescs[i];
                RT_TRACE("  ArrayOfPointers\n");
            }
            else
            {
                geom_desc = &desc->pGeometryDescs[i];
                RT_TRACE("  PointerToArray\n");
            }

            geometry_infos[i].flags = d3d12_geometry_flags_to_vk(geom_desc->Flags);
            RT_TRACE("  Flags = #%x\n", geom_desc->Flags);

            switch (geom_desc->Type)
            {
                case D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES:
                    /* Runtime validates this. */
                    if (have_aabbs)
                    {
                        ERR("Cannot mix and match geometry types in a BLAS.\n");
                        return false;
                    }
                    have_triangles = true;

                    geometry_infos[i].geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
                    triangles = &geometry_infos[i].geometry.triangles;
                    triangles->sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
                    triangles->indexData.deviceAddress = geom_desc->Triangles.IndexBuffer;
                    if (geom_desc->Triangles.IndexFormat != DXGI_FORMAT_UNKNOWN)
                    {
                        if (!geom_desc->Triangles.IndexBuffer)
                            WARN("Application is using IndexBuffer = 0 and IndexFormat != UNKNOWN. Likely application bug.\n");

                        triangles->indexType =
                                geom_desc->Triangles.IndexFormat == DXGI_FORMAT_R16_UINT ?
                                        VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32;
                        primitive_count = geom_desc->Triangles.IndexCount / 3;
                        RT_TRACE("  Indexed : Index count = %u (%u bits)\n",
                                geom_desc->Triangles.IndexCount,
                                triangles->indexType == VK_INDEX_TYPE_UINT16 ? 16 : 32);
                        RT_TRACE("  Vertex count: %u\n", geom_desc->Triangles.VertexCount);
                        RT_TRACE("  IBO VA: %"PRIx64".\n", geom_desc->Triangles.IndexBuffer);
                    }
                    else
                    {
                        primitive_count = geom_desc->Triangles.VertexCount / 3;
                        triangles->indexType = VK_INDEX_TYPE_NONE_KHR;
                        RT_TRACE("  Triangle list : Vertex count: %u\n", geom_desc->Triangles.VertexCount);
                    }

                    triangles->maxVertex = max(1, geom_desc->Triangles.VertexCount) - 1;
                    triangles->vertexStride = geom_desc->Triangles.VertexBuffer.StrideInBytes;
                    triangles->vertexFormat = vkd3d_internal_get_vk_format(device, geom_desc->Triangles.VertexFormat);
                    triangles->vertexData.deviceAddress = geom_desc->Triangles.VertexBuffer.StartAddress;
                    triangles->transformData.deviceAddress = geom_desc->Triangles.Transform3x4;

                    RT_TRACE("  Transform3x4: %s\n", geom_desc->Triangles.Transform3x4 ? "on" : "off");
                    RT_TRACE("  Vertex format: %s\n", debug_dxgi_format(geom_desc->Triangles.VertexFormat));
                    RT_TRACE("  VBO VA: %"PRIx64"\n", geom_desc->Triangles.VertexBuffer.StartAddress);
                    RT_TRACE("  Vertex stride: %"PRIu64" bytes\n", geom_desc->Triangles.VertexBuffer.StrideInBytes);
                    break;

                case D3D12_RAYTRACING_GEOMETRY_TYPE_PROCEDURAL_PRIMITIVE_AABBS:
                    /* Runtime validates this. */
                    if (have_triangles)
                    {
                        ERR("Cannot mix and match geometry types in a BLAS.\n");
                        return false;
                    }
                    have_aabbs = true;

                    geometry_infos[i].geometryType = VK_GEOMETRY_TYPE_AABBS_KHR;
                    aabbs = &geometry_infos[i].geometry.aabbs;
                    aabbs->sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_AABBS_DATA_KHR;
                    aabbs->stride = geom_desc->AABBs.AABBs.StrideInBytes;
                    aabbs->data.deviceAddress = geom_desc->AABBs.AABBs.StartAddress;
                    primitive_count = geom_desc->AABBs.AABBCount;
                    RT_TRACE("  AABB stride: %"PRIu64" bytes\n", geom_desc->AABBs.AABBs.StrideInBytes);
                    break;

                default:
                    FIXME("Unsupported geometry type %u.\n", geom_desc->Type);
                    return false;
            }

            if (primitive_counts)
                primitive_counts[i] = primitive_count;

            if (range_infos)
            {
                range_infos[i].primitiveCount = primitive_count;
                range_infos[i].firstVertex = 0;
                range_infos[i].primitiveOffset = 0;
                range_infos[i].transformOffset = 0;
            }

            RT_TRACE("  Primitive count %u.\n", primitive_count);
        }
    }

    RT_TRACE("=====================\n");
    return true;
}

static void vkd3d_acceleration_structure_end_barrier(struct d3d12_command_list *list)
{
    /* We resolve the query in TRANSFER, but DXR expects UNORDERED_ACCESS. */
    const struct vkd3d_vk_device_procs *vk_procs = &list->device->vk_procs;
    VkDependencyInfo dep_info;
    VkMemoryBarrier2 barrier;

    memset(&barrier, 0, sizeof(barrier));
    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
    barrier.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
    barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    barrier.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;

    memset(&dep_info, 0, sizeof(dep_info));
    dep_info.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dep_info.memoryBarrierCount = 1;
    dep_info.pMemoryBarriers = &barrier;

    VK_CALL(vkCmdPipelineBarrier2(list->cmd.vk_command_buffer, &dep_info));
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
    else if (desc->InfoType == D3D12_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO_CURRENT_SIZE &&
            list->device->device_info.ray_tracing_maintenance1_features.rayTracingMaintenance1)
    {
        vk_query_type = VK_QUERY_TYPE_ACCELERATION_STRUCTURE_SIZE_KHR;
        type_index = VKD3D_QUERY_TYPE_INDEX_RT_CURRENT_SIZE;
        stride = sizeof(uint64_t);
    }
    else if (desc->InfoType == D3D12_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO_SERIALIZATION)
    {
        vk_query_type = VK_QUERY_TYPE_ACCELERATION_STRUCTURE_SERIALIZATION_SIZE_KHR;
        type_index = VKD3D_QUERY_TYPE_INDEX_RT_SERIALIZE_SIZE;
        stride = sizeof(uint64_t);
    }
    else
    {
        FIXME("Unsupported InfoType %u.\n", desc->InfoType);
        /* TODO: CURRENT_SIZE is something we cannot query in Vulkan, so
         * we'll need to keep around a buffer to handle this.
         * For now, just clear to 0. */
        VK_CALL(vkCmdFillBuffer(list->cmd.vk_command_buffer, vk_buffer, offset,
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

    VK_CALL(vkCmdWriteAccelerationStructuresPropertiesKHR(list->cmd.vk_command_buffer,
            1, &vk_acceleration_structure, vk_query_type, vk_query_pool, vk_query_index));
    VK_CALL(vkCmdCopyQueryPoolResults(list->cmd.vk_command_buffer,
            vk_query_pool, vk_query_index, 1,
            vk_buffer, offset, stride,
            VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT));

    if (desc->InfoType == D3D12_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO_SERIALIZATION)
    {
        if (list->device->device_info.ray_tracing_maintenance1_features.rayTracingMaintenance1)
        {
            type_index = VKD3D_QUERY_TYPE_INDEX_RT_SERIALIZE_SIZE_BOTTOM_LEVEL_POINTERS;
            if (!d3d12_command_allocator_allocate_query_from_type_index(list->allocator,
                    type_index, &vk_query_pool, &vk_query_index))
            {
                ERR("Failed to allocate query.\n");
                return;
            }

            d3d12_command_list_reset_query(list, vk_query_pool, vk_query_index);

            VK_CALL(vkCmdWriteAccelerationStructuresPropertiesKHR(list->cmd.vk_command_buffer,
                    1, &vk_acceleration_structure, VK_QUERY_TYPE_ACCELERATION_STRUCTURE_SERIALIZATION_BOTTOM_LEVEL_POINTERS_KHR,
                    vk_query_pool, vk_query_index));
            VK_CALL(vkCmdCopyQueryPoolResults(list->cmd.vk_command_buffer,
                    vk_query_pool, vk_query_index, 1,
                    vk_buffer, offset + sizeof(uint64_t), stride,
                    VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT));
        }
        else
        {
            FIXME("NumBottomLevelPointers will always return 0.\n");
            VK_CALL(vkCmdFillBuffer(list->cmd.vk_command_buffer, vk_buffer, offset + sizeof(uint64_t),
                    sizeof(uint64_t), 0));
        }
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
    VkDependencyInfo dep_info;
    VkMemoryBarrier2 barrier;
    VkDeviceSize stride;
    uint32_t i;

    /* We resolve the query in TRANSFER, but DXR expects UNORDERED_ACCESS. */
    memset(&barrier, 0, sizeof(barrier));
    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
    barrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    barrier.dstStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
    barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;

    memset(&dep_info, 0, sizeof(dep_info));
    dep_info.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dep_info.memoryBarrierCount = 1;
    dep_info.pMemoryBarriers = &barrier;

    VK_CALL(vkCmdPipelineBarrier2(list->cmd.vk_command_buffer, &dep_info));

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
    VkDependencyInfo dep_info;
    VkMemoryBarrier2 barrier;
    uint32_t i;

    memset(&barrier, 0, sizeof(barrier));
    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
    barrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    barrier.srcAccessMask = VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
    /* The query accesses STRUCTURE_READ_BIT in BUILD_BIT stage. */
    barrier.dstStageMask = VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR | VK_PIPELINE_STAGE_2_COPY_BIT;
    barrier.dstAccessMask = VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_2_TRANSFER_WRITE_BIT;

    /* Writing to the result buffer is supposed to happen in UNORDERED_ACCESS on DXR for
     * some bizarre reason, so we have to satisfy a transfer barrier.
     * Have to basically do a full stall to make this work ... */
    memset(&dep_info, 0, sizeof(dep_info));
    dep_info.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dep_info.memoryBarrierCount = 1;
    dep_info.pMemoryBarriers = &barrier;

    VK_CALL(vkCmdPipelineBarrier2(list->cmd.vk_command_buffer, &dep_info));

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
        VK_CALL(vkCmdCopyAccelerationStructureKHR(list->cmd.vk_command_buffer, &info));
}
