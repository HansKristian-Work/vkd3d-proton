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

static VkBuildMicromapFlagsEXT d3d12_build_flags_to_vk(
        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS flags)
{
    VkBuildMicromapFlagsEXT vk_flags = 0;

    if (flags & D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_COMPACTION)
        vk_flags |= VK_BUILD_MICROMAP_ALLOW_COMPACTION_BIT_EXT;
    if (flags & D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE)
        vk_flags |= VK_BUILD_MICROMAP_PREFER_FAST_TRACE_BIT_EXT;
    if (flags & D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_BUILD)
        vk_flags |= VK_BUILD_MICROMAP_PREFER_FAST_BUILD_BIT_EXT;

    return vk_flags;
}

static VkOpacityMicromapFormatEXT d3d12_format_to_vk(
        D3D12_RAYTRACING_OPACITY_MICROMAP_FORMAT format)
{
    switch (format)
    {
        case D3D12_RAYTRACING_OPACITY_MICROMAP_FORMAT_OC1_2_STATE:
            return VK_OPACITY_MICROMAP_FORMAT_2_STATE_EXT;
        case D3D12_RAYTRACING_OPACITY_MICROMAP_FORMAT_OC1_4_STATE:
            return VK_OPACITY_MICROMAP_FORMAT_4_STATE_EXT;
        default:
            FIXME("Unrecognized format #%x.\n", format);
            return (VkOpacityMicromapFormatEXT)format;
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

bool vkd3d_opacity_micromap_convert_inputs(const struct d3d12_device *device,
        const D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS *inputs,
        VkMicromapBuildInfoEXT *build_info,
        VkMicromapUsageEXT *usages)
{
    const D3D12_RAYTRACING_OPACITY_MICROMAP_ARRAY_DESC *desc = inputs->pOpacityMicromapArrayDesc;
    const D3D12_RAYTRACING_OPACITY_MICROMAP_HISTOGRAM_ENTRY *histogram_entry;
    VkMicromapUsageEXT *usage;
    unsigned int i;

    RT_TRACE("Converting inputs.\n");
    RT_TRACE("=====================\n");

    memset(build_info, 0, sizeof(*build_info));
    build_info->sType = VK_STRUCTURE_TYPE_MICROMAP_BUILD_INFO_EXT;
    build_info->type = VK_MICROMAP_TYPE_OPACITY_MICROMAP_EXT;
    build_info->flags = d3d12_build_flags_to_vk(inputs->Flags);
    build_info->mode = VK_BUILD_MICROMAP_MODE_BUILD_EXT;
    build_info->usageCountsCount = desc->NumOmmHistogramEntries;

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

    build_info->pUsageCounts = usages;
    build_info->data.deviceAddress = desc->InputBuffer;
    build_info->triangleArray.deviceAddress = desc->PerOmmDescs.StartAddress;
    build_info->triangleArrayStride = desc->PerOmmDescs.StrideInBytes;

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
        VkMicromapEXT vk_opacity_micromap)
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

    switch (desc->InfoType)
    {
        case D3D12_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO_COMPACTED_SIZE:
            vk_query_type = VK_QUERY_TYPE_MICROMAP_COMPACTED_SIZE_EXT;
            type_index = VKD3D_QUERY_TYPE_INDEX_OMM_COMPACTED_SIZE;
            stride = sizeof(uint64_t);
            break;
        case D3D12_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO_SERIALIZATION:
            vk_query_type = VK_QUERY_TYPE_MICROMAP_SERIALIZATION_SIZE_EXT;
            type_index = VKD3D_QUERY_TYPE_INDEX_OMM_SERIALIZE_SIZE;
            stride = sizeof(uint64_t);
            break;
        default:
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

    VK_CALL(vkCmdWriteMicromapsPropertiesEXT(list->cmd.vk_command_buffer,
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

void vkd3d_opacity_micromap_emit_postbuild_info(
        struct d3d12_command_list *list,
        const D3D12_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO_DESC *desc,
        uint32_t count,
        const D3D12_GPU_VIRTUAL_ADDRESS *addresses)
{
    const struct vkd3d_vk_device_procs *vk_procs = &list->device->vk_procs;
    VkMicromapEXT vk_opacity_micromap;
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
        vk_opacity_micromap = vkd3d_va_map_place_opacity_micromap(
                &list->device->memory_allocator.va_map, list->device, addresses[i]);
        if (vk_opacity_micromap)
            vkd3d_opacity_micromap_write_postbuild_info(list, desc, i * stride, vk_opacity_micromap);
        else
            ERR("Failed to query opacity micromap for VA 0x%"PRIx64".\n", addresses[i]);
    }

    vkd3d_opacity_micromap_end_barrier(list);
}

void vkd3d_opacity_micromap_emit_immediate_postbuild_info(
        struct d3d12_command_list *list, uint32_t count,
        const D3D12_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO_DESC *desc,
        VkMicromapEXT vk_opacity_micromap)
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
    barrier.srcAccessMask = VK_ACCESS_2_MICROMAP_WRITE_BIT_EXT;
    /* The query accesses MICROMAP_READ_BIT in BUILD_BIT stage. */
    barrier.dstStageMask = VK_PIPELINE_STAGE_2_MICROMAP_BUILD_BIT_EXT | VK_PIPELINE_STAGE_2_COPY_BIT;
    barrier.dstAccessMask = VK_ACCESS_2_MICROMAP_READ_BIT_EXT | VK_ACCESS_2_TRANSFER_WRITE_BIT;

    memset(&dep_info, 0, sizeof(dep_info));
    dep_info.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dep_info.memoryBarrierCount = 1;
    dep_info.pMemoryBarriers = &barrier;

    VK_CALL(vkCmdPipelineBarrier2(list->cmd.vk_command_buffer, &dep_info));

    for (i = 0; i < count; i++)
        vkd3d_opacity_micromap_write_postbuild_info(list, &desc[i], 0, vk_opacity_micromap);

    vkd3d_opacity_micromap_end_barrier(list);
}

static bool convert_copy_mode(
        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_COPY_MODE mode,
        VkCopyMicromapModeEXT *vk_mode)
{
    switch (mode)
    {
        case D3D12_RAYTRACING_ACCELERATION_STRUCTURE_COPY_MODE_CLONE:
            *vk_mode = VK_COPY_MICROMAP_MODE_CLONE_EXT;
            return true;
        case D3D12_RAYTRACING_ACCELERATION_STRUCTURE_COPY_MODE_COMPACT:
            *vk_mode = VK_COPY_MICROMAP_MODE_COMPACT_EXT;
            return true;
        default:
            FIXME("Unsupported OMM copy mode #%x.\n", mode);
            return false;
    }
}

void vkd3d_opacity_micromap_copy(
        struct d3d12_command_list *list,
        D3D12_GPU_VIRTUAL_ADDRESS dst, VkMicromapEXT src_omm,
        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_COPY_MODE mode)
{
    const struct vkd3d_vk_device_procs *vk_procs = &list->device->vk_procs;
    VkCopyMicromapInfoEXT info;
    VkMicromapEXT dst_omm;

    dst_omm = vkd3d_va_map_place_opacity_micromap(&list->device->memory_allocator.va_map, list->device, dst);
    if (dst_omm == VK_NULL_HANDLE)
    {
        ERR("Invalid dst address #%"PRIx64" for OMM copy.\n", dst);
        return;
    }

    info.sType = VK_STRUCTURE_TYPE_COPY_MICROMAP_INFO_EXT;
    info.pNext = NULL;
    info.dst = dst_omm;
    info.src = src_omm;
    if (convert_copy_mode(mode, &info.mode))
        VK_CALL(vkCmdCopyMicromapEXT(list->cmd.vk_command_buffer, &info));
}
