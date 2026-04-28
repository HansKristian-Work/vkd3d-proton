/*
 * Copyright 2025 Krzysztof Bogacki
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
    if (flags & D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE)
        vk_flags |= VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    if (flags & D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_BUILD)
        vk_flags |= VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR;

    /* MINIMIZE_MEMORY dropped - VUID-11562 doesn't list LOW_MEMORY_BIT_KHR among
     * the allowed bits for OPACITY_MICROMAP_KHR builds. No translation concern,
     * worst case, excessive memory utilization. */

    /* ALLOW_UPDATE intentionally dropped per VUID-VkAccelerationStructureBuildGeometryInfoKHR-flags-11562
     * (OPACITY_MICROMAP_KHR builds may not include ALLOW_UPDATE). */
    if (flags & D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE)
        FIXME_ONCE("ALLOW_UPDATE on opacity micromap build is unsupported by Vulkan; ignoring.\n");

    return vk_flags;
}

static VkOpacityMicromapFormatKHR d3d12_format_to_vk(
        D3D12_RAYTRACING_OPACITY_MICROMAP_FORMAT format)
{
    switch (format)
    {
        case D3D12_RAYTRACING_OPACITY_MICROMAP_FORMAT_OC1_2_STATE:
            return VK_OPACITY_MICROMAP_FORMAT_2_STATE_KHR;
        case D3D12_RAYTRACING_OPACITY_MICROMAP_FORMAT_OC1_4_STATE:
            return VK_OPACITY_MICROMAP_FORMAT_4_STATE_KHR;
        default:
            FIXME("Unrecognized format #%x.\n", format);
            return (VkOpacityMicromapFormatKHR)format;
    }
}

VKD3D_UNUSED static char const* debug_omm_format(
        D3D12_RAYTRACING_OPACITY_MICROMAP_FORMAT format)
{
    switch (format)
    {
        #define ENUM_NAME(x) case x: return #x;
        ENUM_NAME(D3D12_RAYTRACING_OPACITY_MICROMAP_FORMAT_OC1_2_STATE)
        ENUM_NAME(D3D12_RAYTRACING_OPACITY_MICROMAP_FORMAT_OC1_4_STATE)
        #undef ENUM_NAME
    }

    return vkd3d_dbg_sprintf("Unknown D3D12_RAYTRACING_OPACITY_MICROMAP_FORMAT (%u)",
        (uint32_t)format);
}

static uint32_t vkd3d_opacity_micromap_convert_inputs_usages(
        const D3D12_RAYTRACING_OPACITY_MICROMAP_ARRAY_DESC *desc,
        VkMicromapUsageKHR *usages)
{
    const D3D12_RAYTRACING_OPACITY_MICROMAP_HISTOGRAM_ENTRY *histogram_entry;
    VkMicromapUsageKHR *usage;
    unsigned int i;

    for (i = 0; i < desc->NumOmmHistogramEntries; i++)
    {
        RT_TRACE(" Histogram entry %u:\n", i);

        histogram_entry = &desc->pOmmHistogram[i];
        usage = &usages[i];

        usage->count = histogram_entry->Count;
        usage->subdivisionLevel = histogram_entry->SubdivisionLevel;
        usage->format = d3d12_format_to_vk(histogram_entry->Format);

        RT_TRACE("  Count: %u\n", histogram_entry->Count);
        RT_TRACE("  Subdivision level: %u\n", histogram_entry->SubdivisionLevel);
        RT_TRACE("  Format: %s\n", debug_omm_format(histogram_entry->Format));
    }
    return desc->NumOmmHistogramEntries;
}

bool vkd3d_opacity_micromap_convert_inputs(const struct d3d12_device *device,
        const D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS *inputs,
        VkAccelerationStructureBuildGeometryInfoKHR *build_info,
        VkAccelerationStructureGeometryKHR *geometry_info,
        VkAccelerationStructureGeometryMicromapDataKHR *geometry_micromap_data,
        VkMicromapUsageKHR *usages)
{
    const D3D12_RAYTRACING_OPACITY_MICROMAP_ARRAY_DESC *desc = inputs->pOpacityMicromapArrayDesc;

    RT_TRACE("Converting inputs.\n");
    RT_TRACE("=====================\n");

    memset(build_info, 0, sizeof(*build_info));
    build_info->sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    build_info->type = VK_ACCELERATION_STRUCTURE_TYPE_OPACITY_MICROMAP_KHR;
    build_info->flags = d3d12_build_flags_to_vk(inputs->Flags);
    build_info->mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    build_info->geometryCount = 1;
    build_info->pGeometries = geometry_info;

    memset(geometry_info, 0, sizeof(*geometry_info));
    geometry_info->sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    geometry_info->geometryType = VK_GEOMETRY_TYPE_MICROMAP_KHR;
    geometry_info->pNext = geometry_micromap_data;

    memset(geometry_micromap_data, 0, sizeof(*geometry_micromap_data));
    geometry_micromap_data->sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_MICROMAP_DATA_KHR;
    geometry_micromap_data->usageCountsCount = vkd3d_opacity_micromap_convert_inputs_usages(desc, usages);
    geometry_micromap_data->pUsageCounts = usages;
    geometry_micromap_data->data = desc->InputBuffer;
    geometry_micromap_data->triangleArray = desc->PerOmmDescs.StartAddress;
    geometry_micromap_data->triangleArrayStride = desc->PerOmmDescs.StrideInBytes;

    RT_TRACE(" IBO VA: %"PRIx64"\n", desc->InputBuffer);
    RT_TRACE(" Triangles VA: %"PRIx64"\n", desc->PerOmmDescs.StartAddress);
    RT_TRACE(" Triangles stride: %"PRIu64" bytes\n", desc->PerOmmDescs.StrideInBytes);

    RT_TRACE("=====================\n");
    return true;
}

static void vkd3d_opacity_micromap_end_barrier(struct d3d12_command_list *list)
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

void vkd3d_opacity_micromap_write_postbuild_info(
        struct d3d12_command_list *list,
        const D3D12_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO_DESC *desc,
        VkDeviceSize desc_offset,
        VkAccelerationStructureKHR vk_opacity_micromap)
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
    stride = sizeof(uint64_t);

    switch (desc->InfoType)
    {
        case D3D12_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO_COMPACTED_SIZE:
            vk_query_type = VK_QUERY_TYPE_ACCELERATION_STRUCTURE_COMPACTED_SIZE_KHR;
            type_index = VKD3D_QUERY_TYPE_INDEX_RT_COMPACTED_SIZE;
            break;
        case D3D12_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO_CURRENT_SIZE:
            vk_query_type = VK_QUERY_TYPE_ACCELERATION_STRUCTURE_SIZE_KHR;
            type_index = VKD3D_QUERY_TYPE_INDEX_RT_CURRENT_SIZE;
            break;
        case D3D12_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO_SERIALIZATION:
            vk_query_type = VK_QUERY_TYPE_ACCELERATION_STRUCTURE_SERIALIZATION_SIZE_KHR;
            type_index = VKD3D_QUERY_TYPE_INDEX_RT_SERIALIZE_SIZE;
            break;
        default:
            FIXME("Unsupported InfoType %u.\n", desc->InfoType);
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

    VK_CALL(vkCmdWriteAccelerationStructuresPropertiesKHR(list->cmd.vk_command_buffer,
            1, &vk_opacity_micromap, vk_query_type, vk_query_pool, vk_query_index));
    VK_CALL(vkCmdCopyQueryPoolResults(list->cmd.vk_command_buffer,
            vk_query_pool, vk_query_index, 1,
            vk_buffer, offset, stride,
            VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT));

    if (desc->InfoType == D3D12_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO_SERIALIZATION)
    {
        VK_CALL(vkCmdFillBuffer(list->cmd.vk_command_buffer, vk_buffer, offset + sizeof(uint64_t),
                sizeof(uint64_t), 0));
    }
}

void vkd3d_opacity_micromap_emit_immediate_postbuild_info(
        struct d3d12_command_list *list, uint32_t count,
        const D3D12_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO_DESC *desc,
        VkAccelerationStructureKHR vk_opacity_micromap)
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
    barrier.dstStageMask = VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR | VK_PIPELINE_STAGE_2_COPY_BIT;
    barrier.dstAccessMask = VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_2_TRANSFER_WRITE_BIT;

    memset(&dep_info, 0, sizeof(dep_info));
    dep_info.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dep_info.memoryBarrierCount = 1;
    dep_info.pMemoryBarriers = &barrier;

    VK_CALL(vkCmdPipelineBarrier2(list->cmd.vk_command_buffer, &dep_info));

    for (i = 0; i < count; i++)
        vkd3d_opacity_micromap_write_postbuild_info(list, &desc[i], 0, vk_opacity_micromap);

    vkd3d_opacity_micromap_end_barrier(list);
}

static bool vkd3d_acceleration_structure_convert_opacity_micromap_index_type(DXGI_FORMAT format, VkIndexType *result)
{
    switch (format)
    {
        case DXGI_FORMAT_UNKNOWN:
            *result = VK_INDEX_TYPE_NONE_KHR;
            return true;
        case DXGI_FORMAT_R32_UINT:
            *result = VK_INDEX_TYPE_UINT32;
            return true;
        case DXGI_FORMAT_R16_UINT:
            *result = VK_INDEX_TYPE_UINT16;
            return true;
        case DXGI_FORMAT_R8_UINT:
            FIXME_ONCE("Using UINT8 as OMM index format is technically out of spec.\n");
            *result = VK_INDEX_TYPE_UINT8;
            return true;
        default:
            ERR("Unsupported OMM index format #%x.\n", format);
            return false;
    }
}

bool vkd3d_acceleration_structure_convert_opacity_micromap(struct d3d12_device *device,
        const D3D12_RAYTRACING_GEOMETRY_DESC *geom_desc,
        VkAccelerationStructureGeometryKHR *geometry_info,
        VkAccelerationStructureTrianglesOpacityMicromapKHR *omm_triangles_info)
{
    vk_prepend_struct(&geometry_info->geometry.triangles, omm_triangles_info);
    omm_triangles_info->sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_TRIANGLES_OPACITY_MICROMAP_KHR;
    omm_triangles_info->pNext = NULL;

    if (!vkd3d_acceleration_structure_convert_opacity_micromap_index_type(
            geom_desc->OmmTriangles.pOmmLinkage->OpacityMicromapIndexFormat, &omm_triangles_info->indexType))
    {
        return false;
    }
    omm_triangles_info->indexBuffer = geom_desc->OmmTriangles.pOmmLinkage->OpacityMicromapIndexBuffer.StartAddress;
    omm_triangles_info->indexStride = geom_desc->OmmTriangles.pOmmLinkage->OpacityMicromapIndexBuffer.StrideInBytes;
    omm_triangles_info->baseTriangle = geom_desc->OmmTriangles.pOmmLinkage->OpacityMicromapBaseLocation;

    if (geom_desc->OmmTriangles.pOmmLinkage->OpacityMicromapArray)
    {
        omm_triangles_info->micromap = vkd3d_va_map_place_acceleration_structure(
                &device->memory_allocator.va_map, device,
                geom_desc->OmmTriangles.pOmmLinkage->OpacityMicromapArray, true);

        if (omm_triangles_info->micromap == VK_NULL_HANDLE)
            ERR("Failed to place OMM at VA 0x%"PRIx64".\n", geom_desc->OmmTriangles.pOmmLinkage->OpacityMicromapArray);
    }

    RT_TRACE("  OMM Index type: %s\n", debug_dxgi_format(geom_desc->OmmTriangles.pOmmLinkage->OpacityMicromapIndexFormat));
    RT_TRACE("  OMM IBO VA: %"PRIx64"\n", geom_desc->OmmTriangles.pOmmLinkage->OpacityMicromapIndexBuffer.StartAddress);
    RT_TRACE("  OMM Index stride: %"PRIu64" bytes\n", geom_desc->OmmTriangles.pOmmLinkage->OpacityMicromapIndexBuffer.StrideInBytes);
    RT_TRACE("  OMM Base: %u\n", geom_desc->OmmTriangles.pOmmLinkage->OpacityMicromapBaseLocation);
    RT_TRACE("  OMM Micromap VA: %"PRIx64"\n", geom_desc->OmmTriangles.pOmmLinkage->OpacityMicromapArray);

    return true;
}
