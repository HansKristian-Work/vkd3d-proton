/*
 * Copyright 2020 Hans-Kristian Arntzen for Valve Corporation
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

#ifndef __VKD3D_COMMAND_LIST_PROFILED
#define __VKD3D_COMMAND_LIST_PROFILED

#define COMMAND_LIST_PROFILED_CALL(name, ...) \
    VKD3D_REGION_DECL(name); \
    VKD3D_REGION_BEGIN(name); \
    d3d12_command_list_##name(__VA_ARGS__); \
    VKD3D_REGION_END(name)

static void STDMETHODCALLTYPE d3d12_command_list_DrawInstanced_profiled(d3d12_command_list_iface *iface,
        UINT vertex_count_per_instance, UINT instance_count, UINT start_vertex_location,
        UINT start_instance_location)
{
    COMMAND_LIST_PROFILED_CALL(DrawInstanced, iface, vertex_count_per_instance,
            instance_count, start_vertex_location, start_instance_location);
}

static void STDMETHODCALLTYPE d3d12_command_list_DrawIndexedInstanced_profiled(d3d12_command_list_iface *iface,
        UINT index_count_per_instance, UINT instance_count, UINT start_vertex_location,
        INT base_vertex_location, UINT start_instance_location)
{
    COMMAND_LIST_PROFILED_CALL(DrawIndexedInstanced, iface, index_count_per_instance, instance_count,
            start_vertex_location, base_vertex_location, start_instance_location);
}

static void STDMETHODCALLTYPE d3d12_command_list_Dispatch_profiled(d3d12_command_list_iface *iface,
        UINT x, UINT y, UINT z)
{
    COMMAND_LIST_PROFILED_CALL(Dispatch, iface, x, y, z);
}

static void STDMETHODCALLTYPE d3d12_command_list_CopyBufferRegion_profiled(d3d12_command_list_iface *iface,
        ID3D12Resource *dst, UINT64 dst_offset, ID3D12Resource *src, UINT64 src_offset, UINT64 byte_count)
{
    COMMAND_LIST_PROFILED_CALL(CopyBufferRegion, iface, dst, dst_offset, src, src_offset, byte_count);
}

static void STDMETHODCALLTYPE d3d12_command_list_CopyTextureRegion_profiled(d3d12_command_list_iface *iface,
        const D3D12_TEXTURE_COPY_LOCATION *dst, UINT dst_x, UINT dst_y, UINT dst_z,
        const D3D12_TEXTURE_COPY_LOCATION *src, const D3D12_BOX *src_box)
{
    COMMAND_LIST_PROFILED_CALL(CopyTextureRegion, iface, dst, dst_x, dst_y, dst_z, src, src_box);
}

static void STDMETHODCALLTYPE d3d12_command_list_CopyResource_profiled(d3d12_command_list_iface *iface,
        ID3D12Resource *dst, ID3D12Resource *src)
{
    COMMAND_LIST_PROFILED_CALL(CopyResource, iface, dst, src);
}

static void STDMETHODCALLTYPE d3d12_command_list_CopyTiles_profiled(d3d12_command_list_iface *iface,
        ID3D12Resource *tiled_resource, const D3D12_TILED_RESOURCE_COORDINATE *region_coord,
        const D3D12_TILE_REGION_SIZE *region_size, ID3D12Resource *buffer, UINT64 buffer_offset,
        D3D12_TILE_COPY_FLAGS flags)
{
    COMMAND_LIST_PROFILED_CALL(CopyTiles, iface, tiled_resource, region_coord, region_size, buffer, buffer_offset, flags);
}

static void STDMETHODCALLTYPE d3d12_command_list_ResolveSubresource_profiled(d3d12_command_list_iface *iface,
        ID3D12Resource *dst, UINT dst_sub_resource_idx,
        ID3D12Resource *src, UINT src_sub_resource_idx, DXGI_FORMAT format)
{
    COMMAND_LIST_PROFILED_CALL(ResolveSubresource, iface, dst, dst_sub_resource_idx, src, src_sub_resource_idx, format);
}

static void STDMETHODCALLTYPE d3d12_command_list_IASetPrimitiveTopology_profiled(d3d12_command_list_iface *iface,
        D3D12_PRIMITIVE_TOPOLOGY topology)
{
    COMMAND_LIST_PROFILED_CALL(IASetPrimitiveTopology, iface, topology);
}

static void STDMETHODCALLTYPE d3d12_command_list_RSSetViewports_profiled(d3d12_command_list_iface *iface,
        UINT viewport_count, const D3D12_VIEWPORT *viewports)
{
    COMMAND_LIST_PROFILED_CALL(RSSetViewports, iface, viewport_count, viewports);
}

static void STDMETHODCALLTYPE d3d12_command_list_RSSetScissorRects_profiled(d3d12_command_list_iface *iface,
        UINT rect_count, const D3D12_RECT *rects)
{
    COMMAND_LIST_PROFILED_CALL(RSSetScissorRects, iface, rect_count, rects);
}

static void STDMETHODCALLTYPE d3d12_command_list_OMSetBlendFactor_profiled(d3d12_command_list_iface *iface,
        const FLOAT blend_factor[4])
{
    COMMAND_LIST_PROFILED_CALL(OMSetBlendFactor, iface, blend_factor);
}

static void STDMETHODCALLTYPE d3d12_command_list_OMSetStencilRef_profiled(d3d12_command_list_iface *iface,
        UINT stencil_ref)
{
    COMMAND_LIST_PROFILED_CALL(OMSetStencilRef, iface, stencil_ref);
}

static void STDMETHODCALLTYPE d3d12_command_list_SetPipelineState_profiled(d3d12_command_list_iface *iface,
        ID3D12PipelineState *pipeline_state)
{
    COMMAND_LIST_PROFILED_CALL(SetPipelineState, iface, pipeline_state);
}

static void STDMETHODCALLTYPE d3d12_command_list_ResourceBarrier_profiled(d3d12_command_list_iface *iface,
        UINT barrier_count, const D3D12_RESOURCE_BARRIER *barriers)
{
    COMMAND_LIST_PROFILED_CALL(ResourceBarrier, iface, barrier_count, barriers);
}

static void STDMETHODCALLTYPE d3d12_command_list_ExecuteBundle_profiled(d3d12_command_list_iface *iface,
        ID3D12GraphicsCommandList *command_list)
{
    COMMAND_LIST_PROFILED_CALL(ExecuteBundle, iface, command_list);
}

static void STDMETHODCALLTYPE d3d12_command_list_SetDescriptorHeaps_profiled(d3d12_command_list_iface *iface,
        UINT heap_count, ID3D12DescriptorHeap *const *heaps)
{
    COMMAND_LIST_PROFILED_CALL(SetDescriptorHeaps, iface, heap_count, heaps);
}

static void STDMETHODCALLTYPE d3d12_command_list_SetComputeRootSignature_profiled(d3d12_command_list_iface *iface,
        ID3D12RootSignature *root_signature)
{
    COMMAND_LIST_PROFILED_CALL(SetComputeRootSignature, iface, root_signature);
}

static void STDMETHODCALLTYPE d3d12_command_list_SetGraphicsRootSignature_profiled(d3d12_command_list_iface *iface,
        ID3D12RootSignature *root_signature)
{
    COMMAND_LIST_PROFILED_CALL(SetGraphicsRootSignature, iface, root_signature);
}

static void STDMETHODCALLTYPE d3d12_command_list_SetComputeRootDescriptorTable_profiled(d3d12_command_list_iface *iface,
        UINT root_parameter_index, D3D12_GPU_DESCRIPTOR_HANDLE base_descriptor)
{
    COMMAND_LIST_PROFILED_CALL(SetComputeRootDescriptorTable, iface, root_parameter_index, base_descriptor);
}

static void STDMETHODCALLTYPE d3d12_command_list_SetGraphicsRootDescriptorTable_profiled(d3d12_command_list_iface *iface,
        UINT root_parameter_index, D3D12_GPU_DESCRIPTOR_HANDLE base_descriptor)
{
    COMMAND_LIST_PROFILED_CALL(SetGraphicsRootDescriptorTable, iface, root_parameter_index, base_descriptor);
}

static void STDMETHODCALLTYPE d3d12_command_list_SetComputeRoot32BitConstant_profiled(d3d12_command_list_iface *iface,
        UINT root_parameter_index, UINT data, UINT dst_offset)
{
    COMMAND_LIST_PROFILED_CALL(SetComputeRoot32BitConstant, iface, root_parameter_index, data, dst_offset);
}

static void STDMETHODCALLTYPE d3d12_command_list_SetGraphicsRoot32BitConstant_profiled(d3d12_command_list_iface *iface,
        UINT root_parameter_index, UINT data, UINT dst_offset)
{
    COMMAND_LIST_PROFILED_CALL(SetGraphicsRoot32BitConstant, iface, root_parameter_index, data, dst_offset);
}

static void STDMETHODCALLTYPE d3d12_command_list_SetComputeRoot32BitConstants_profiled(d3d12_command_list_iface *iface,
        UINT root_parameter_index, UINT constant_count, const void *data, UINT dst_offset)
{
    COMMAND_LIST_PROFILED_CALL(SetComputeRoot32BitConstants, iface, root_parameter_index, constant_count, data, dst_offset);
}

static void STDMETHODCALLTYPE d3d12_command_list_SetGraphicsRoot32BitConstants_profiled(d3d12_command_list_iface *iface,
        UINT root_parameter_index, UINT constant_count, const void *data, UINT dst_offset)
{
    COMMAND_LIST_PROFILED_CALL(SetGraphicsRoot32BitConstants, iface, root_parameter_index, constant_count, data, dst_offset);
}

static void STDMETHODCALLTYPE d3d12_command_list_SetComputeRootConstantBufferView_profiled(
        d3d12_command_list_iface *iface, UINT root_parameter_index, D3D12_GPU_VIRTUAL_ADDRESS address)
{
    COMMAND_LIST_PROFILED_CALL(SetComputeRootConstantBufferView, iface, root_parameter_index, address);
}

static void STDMETHODCALLTYPE d3d12_command_list_SetGraphicsRootConstantBufferView_profiled(
        d3d12_command_list_iface *iface, UINT root_parameter_index, D3D12_GPU_VIRTUAL_ADDRESS address)
{
    COMMAND_LIST_PROFILED_CALL(SetGraphicsRootConstantBufferView, iface, root_parameter_index, address);
}

static void STDMETHODCALLTYPE d3d12_command_list_SetComputeRootShaderResourceView_profiled(
        d3d12_command_list_iface *iface, UINT root_parameter_index, D3D12_GPU_VIRTUAL_ADDRESS address)
{
    COMMAND_LIST_PROFILED_CALL(SetComputeRootShaderResourceView, iface, root_parameter_index, address);
}

static void STDMETHODCALLTYPE d3d12_command_list_SetGraphicsRootShaderResourceView_profiled(
        d3d12_command_list_iface *iface, UINT root_parameter_index, D3D12_GPU_VIRTUAL_ADDRESS address)
{
    COMMAND_LIST_PROFILED_CALL(SetGraphicsRootShaderResourceView, iface, root_parameter_index, address);
}

static void STDMETHODCALLTYPE d3d12_command_list_SetComputeRootUnorderedAccessView_profiled(
        d3d12_command_list_iface *iface, UINT root_parameter_index, D3D12_GPU_VIRTUAL_ADDRESS address)
{
    COMMAND_LIST_PROFILED_CALL(SetComputeRootUnorderedAccessView, iface, root_parameter_index, address);
}

static void STDMETHODCALLTYPE d3d12_command_list_SetGraphicsRootUnorderedAccessView_profiled(
        d3d12_command_list_iface *iface, UINT root_parameter_index, D3D12_GPU_VIRTUAL_ADDRESS address)
{
    COMMAND_LIST_PROFILED_CALL(SetGraphicsRootUnorderedAccessView, iface, root_parameter_index, address);
}

static void STDMETHODCALLTYPE d3d12_command_list_IASetIndexBuffer_profiled(d3d12_command_list_iface *iface,
        const D3D12_INDEX_BUFFER_VIEW *view)
{
    COMMAND_LIST_PROFILED_CALL(IASetIndexBuffer, iface, view);
}

static void STDMETHODCALLTYPE d3d12_command_list_IASetVertexBuffers_profiled(d3d12_command_list_iface *iface,
        UINT start_slot, UINT view_count, const D3D12_VERTEX_BUFFER_VIEW *views)
{
    COMMAND_LIST_PROFILED_CALL(IASetVertexBuffers, iface, start_slot, view_count, views);
}

static void STDMETHODCALLTYPE d3d12_command_list_SOSetTargets_profiled(d3d12_command_list_iface *iface,
        UINT start_slot, UINT view_count, const D3D12_STREAM_OUTPUT_BUFFER_VIEW *views)
{
    COMMAND_LIST_PROFILED_CALL(SOSetTargets, iface, start_slot, view_count, views);
}

static void STDMETHODCALLTYPE d3d12_command_list_OMSetRenderTargets_profiled(d3d12_command_list_iface *iface,
        UINT render_target_descriptor_count, const D3D12_CPU_DESCRIPTOR_HANDLE *render_target_descriptors,
        BOOL single_descriptor_handle, const D3D12_CPU_DESCRIPTOR_HANDLE *depth_stencil_descriptor)
{
    COMMAND_LIST_PROFILED_CALL(OMSetRenderTargets, iface, render_target_descriptor_count, render_target_descriptors,
            single_descriptor_handle, depth_stencil_descriptor);
}

static void STDMETHODCALLTYPE d3d12_command_list_ClearDepthStencilView_profiled(d3d12_command_list_iface *iface,
        D3D12_CPU_DESCRIPTOR_HANDLE dsv, D3D12_CLEAR_FLAGS flags, float depth, UINT8 stencil,
        UINT rect_count, const D3D12_RECT *rects)
{
    COMMAND_LIST_PROFILED_CALL(ClearDepthStencilView, iface, dsv, flags, depth, stencil, rect_count, rects);
}

static void STDMETHODCALLTYPE d3d12_command_list_ClearRenderTargetView_profiled(d3d12_command_list_iface *iface,
        D3D12_CPU_DESCRIPTOR_HANDLE rtv, const FLOAT color[4], UINT rect_count, const D3D12_RECT *rects)
{
    COMMAND_LIST_PROFILED_CALL(ClearRenderTargetView, iface, rtv, color, rect_count, rects);
}

static void STDMETHODCALLTYPE d3d12_command_list_ClearUnorderedAccessViewUint_profiled(d3d12_command_list_iface *iface,
        D3D12_GPU_DESCRIPTOR_HANDLE gpu_handle, D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle, ID3D12Resource *resource,
        const UINT values[4], UINT rect_count, const D3D12_RECT *rects)
{
    COMMAND_LIST_PROFILED_CALL(ClearUnorderedAccessViewUint, iface, gpu_handle, cpu_handle, resource, values, rect_count, rects);
}

static void STDMETHODCALLTYPE d3d12_command_list_ClearUnorderedAccessViewFloat_profiled(d3d12_command_list_iface *iface,
        D3D12_GPU_DESCRIPTOR_HANDLE gpu_handle, D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle, ID3D12Resource *resource,
        const float values[4], UINT rect_count, const D3D12_RECT *rects)
{
    COMMAND_LIST_PROFILED_CALL(ClearUnorderedAccessViewFloat, iface, gpu_handle, cpu_handle, resource, values, rect_count, rects);
}

static void STDMETHODCALLTYPE d3d12_command_list_DiscardResource_profiled(d3d12_command_list_iface *iface,
        ID3D12Resource *resource, const D3D12_DISCARD_REGION *region)
{
    COMMAND_LIST_PROFILED_CALL(DiscardResource, iface, resource, region);
}

static void STDMETHODCALLTYPE d3d12_command_list_BeginQuery_profiled(d3d12_command_list_iface *iface,
        ID3D12QueryHeap *heap, D3D12_QUERY_TYPE type, UINT index)
{
    COMMAND_LIST_PROFILED_CALL(BeginQuery, iface, heap, type, index);
}

static void STDMETHODCALLTYPE d3d12_command_list_EndQuery_profiled(d3d12_command_list_iface *iface,
        ID3D12QueryHeap *heap, D3D12_QUERY_TYPE type, UINT index)
{
    COMMAND_LIST_PROFILED_CALL(EndQuery, iface, heap, type, index);
}

static void STDMETHODCALLTYPE d3d12_command_list_ResolveQueryData_profiled(d3d12_command_list_iface *iface,
        ID3D12QueryHeap *heap, D3D12_QUERY_TYPE type, UINT start_index, UINT query_count,
        ID3D12Resource *dst_buffer, UINT64 aligned_dst_buffer_offset)
{
    COMMAND_LIST_PROFILED_CALL(ResolveQueryData, iface, heap, type, start_index, query_count, dst_buffer, aligned_dst_buffer_offset);
}

static void STDMETHODCALLTYPE d3d12_command_list_SetPredication_profiled(d3d12_command_list_iface *iface,
        ID3D12Resource *buffer, UINT64 aligned_buffer_offset, D3D12_PREDICATION_OP operation)
{
    COMMAND_LIST_PROFILED_CALL(SetPredication, iface, buffer, aligned_buffer_offset, operation);
}

static void STDMETHODCALLTYPE d3d12_command_list_SetMarker_profiled(d3d12_command_list_iface *iface,
        UINT metadata, const void *data, UINT size)
{
    COMMAND_LIST_PROFILED_CALL(SetMarker, iface, metadata, data, size);
}

static void STDMETHODCALLTYPE d3d12_command_list_BeginEvent_profiled(d3d12_command_list_iface *iface,
        UINT metadata, const void *data, UINT size)
{
    COMMAND_LIST_PROFILED_CALL(BeginEvent, iface, metadata, data, size);
}

static void STDMETHODCALLTYPE d3d12_command_list_EndEvent_profiled(d3d12_command_list_iface *iface)
{
    COMMAND_LIST_PROFILED_CALL(EndEvent, iface);
}

static void STDMETHODCALLTYPE d3d12_command_list_ExecuteIndirect_profiled(d3d12_command_list_iface *iface,
        ID3D12CommandSignature *command_signature, UINT max_command_count, ID3D12Resource *arg_buffer,
        UINT64 arg_buffer_offset, ID3D12Resource *count_buffer, UINT64 count_buffer_offset)
{
    COMMAND_LIST_PROFILED_CALL(ExecuteIndirect, iface, command_signature, max_command_count, arg_buffer, arg_buffer_offset, count_buffer, count_buffer_offset);
}

static void STDMETHODCALLTYPE d3d12_command_list_AtomicCopyBufferUINT_profiled(d3d12_command_list_iface *iface,
        ID3D12Resource *dst_buffer, UINT64 dst_offset,
        ID3D12Resource *src_buffer, UINT64 src_offset,
        UINT dependent_resource_count, ID3D12Resource * const *dependent_resources,
        const D3D12_SUBRESOURCE_RANGE_UINT64 *dependent_sub_resource_ranges)
{
    COMMAND_LIST_PROFILED_CALL(AtomicCopyBufferUINT, iface, dst_buffer, dst_offset, src_buffer, src_offset,
            dependent_resource_count, dependent_resources, dependent_sub_resource_ranges);
}

static void STDMETHODCALLTYPE d3d12_command_list_AtomicCopyBufferUINT64_profiled(d3d12_command_list_iface *iface,
        ID3D12Resource *dst_buffer, UINT64 dst_offset,
        ID3D12Resource *src_buffer, UINT64 src_offset,
        UINT dependent_resource_count, ID3D12Resource * const *dependent_resources,
        const D3D12_SUBRESOURCE_RANGE_UINT64 *dependent_sub_resource_ranges)
{
    COMMAND_LIST_PROFILED_CALL(AtomicCopyBufferUINT64, iface, dst_buffer, dst_offset,
            src_buffer, src_offset,
            dependent_resource_count, dependent_resources,
            dependent_sub_resource_ranges);
}

static void STDMETHODCALLTYPE d3d12_command_list_OMSetDepthBounds_profiled(d3d12_command_list_iface *iface,
        FLOAT min, FLOAT max)
{
    COMMAND_LIST_PROFILED_CALL(OMSetDepthBounds, iface, min, max);
}

static void STDMETHODCALLTYPE d3d12_command_list_SetSamplePositions_profiled(d3d12_command_list_iface *iface,
        UINT sample_count, UINT pixel_count, D3D12_SAMPLE_POSITION *sample_positions)
{
    COMMAND_LIST_PROFILED_CALL(SetSamplePositions, iface, sample_count, pixel_count, sample_positions);
}

static void STDMETHODCALLTYPE d3d12_command_list_ResolveSubresourceRegion_profiled(d3d12_command_list_iface *iface,
        ID3D12Resource *dst_resource, UINT dst_sub_resource_idx, UINT dst_x, UINT dst_y,
        ID3D12Resource *src_resource, UINT src_sub_resource_idx,
        D3D12_RECT *src_rect, DXGI_FORMAT format, D3D12_RESOLVE_MODE mode)
{
    COMMAND_LIST_PROFILED_CALL(ResolveSubresourceRegion, iface, dst_resource, dst_sub_resource_idx,
            dst_x, dst_y, src_resource, src_sub_resource_idx,
            src_rect, format, mode);
}

static void STDMETHODCALLTYPE d3d12_command_list_SetViewInstanceMask_profiled(d3d12_command_list_iface *iface, UINT mask)
{
    COMMAND_LIST_PROFILED_CALL(SetViewInstanceMask, iface, mask);
}

static void STDMETHODCALLTYPE d3d12_command_list_WriteBufferImmediate_profiled(d3d12_command_list_iface *iface,
        UINT count, const D3D12_WRITEBUFFERIMMEDIATE_PARAMETER *parameters,
        const D3D12_WRITEBUFFERIMMEDIATE_MODE *modes)
{
    COMMAND_LIST_PROFILED_CALL(WriteBufferImmediate, iface, count, parameters, modes);
}

static void STDMETHODCALLTYPE d3d12_command_list_SetProtectedResourceSession_profiled(d3d12_command_list_iface *iface,
        ID3D12ProtectedResourceSession *protected_session)
{
    COMMAND_LIST_PROFILED_CALL(SetProtectedResourceSession, iface, protected_session);
}

static void STDMETHODCALLTYPE d3d12_command_list_BeginRenderPass_profiled(d3d12_command_list_iface *iface,
        UINT rt_count, const D3D12_RENDER_PASS_RENDER_TARGET_DESC *render_targets,
        const D3D12_RENDER_PASS_DEPTH_STENCIL_DESC *depth_stencil, D3D12_RENDER_PASS_FLAGS flags)
{
    COMMAND_LIST_PROFILED_CALL(BeginRenderPass, iface, rt_count, render_targets, depth_stencil, flags);
}

static void STDMETHODCALLTYPE d3d12_command_list_EndRenderPass_profiled(d3d12_command_list_iface *iface)
{
    COMMAND_LIST_PROFILED_CALL(EndRenderPass, iface);
}

static void STDMETHODCALLTYPE d3d12_command_list_InitializeMetaCommand_profiled(d3d12_command_list_iface *iface,
        ID3D12MetaCommand *meta_command, const void *parameter_data, SIZE_T parameter_size)
{
    COMMAND_LIST_PROFILED_CALL(InitializeMetaCommand, iface, meta_command, parameter_data, parameter_size);
}

static void STDMETHODCALLTYPE d3d12_command_list_ExecuteMetaCommand_profiled(d3d12_command_list_iface *iface,
        ID3D12MetaCommand *meta_command, const void *parameter_data, SIZE_T parameter_size)
{
    COMMAND_LIST_PROFILED_CALL(ExecuteMetaCommand, iface, meta_command, parameter_data, parameter_size);
}

static void STDMETHODCALLTYPE d3d12_command_list_BuildRaytracingAccelerationStructure_profiled(d3d12_command_list_iface *iface,
        const D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC *desc, UINT num_postbuild_info_descs,
        const D3D12_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO_DESC *postbuild_info_descs)
{
    COMMAND_LIST_PROFILED_CALL(BuildRaytracingAccelerationStructure, iface, desc, num_postbuild_info_descs,
            postbuild_info_descs);
}

static void STDMETHODCALLTYPE d3d12_command_list_EmitRaytracingAccelerationStructurePostbuildInfo_profiled(d3d12_command_list_iface *iface,
        const D3D12_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO_DESC *desc, UINT num_acceleration_structures,
        const D3D12_GPU_VIRTUAL_ADDRESS *src_data)
{
    COMMAND_LIST_PROFILED_CALL(EmitRaytracingAccelerationStructurePostbuildInfo, iface, desc, num_acceleration_structures, src_data);
}

static void STDMETHODCALLTYPE d3d12_command_list_CopyRaytracingAccelerationStructure_profiled(d3d12_command_list_iface *iface,
        D3D12_GPU_VIRTUAL_ADDRESS dst_data, D3D12_GPU_VIRTUAL_ADDRESS src_data,
        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_COPY_MODE mode)
{
    COMMAND_LIST_PROFILED_CALL(CopyRaytracingAccelerationStructure, iface, dst_data, src_data, mode);
}

static void STDMETHODCALLTYPE d3d12_command_list_SetPipelineState1_profiled(d3d12_command_list_iface *iface,
        ID3D12StateObject *state_object)
{
    COMMAND_LIST_PROFILED_CALL(SetPipelineState1, iface, state_object);
}

static void STDMETHODCALLTYPE d3d12_command_list_DispatchRays_profiled(d3d12_command_list_iface *iface,
        const D3D12_DISPATCH_RAYS_DESC *desc)
{
    COMMAND_LIST_PROFILED_CALL(DispatchRays, iface, desc);
}

static void STDMETHODCALLTYPE d3d12_command_list_RSSetShadingRate_profiled(d3d12_command_list_iface *iface,
        D3D12_SHADING_RATE base, const D3D12_SHADING_RATE_COMBINER *combiners)
{
    COMMAND_LIST_PROFILED_CALL(RSSetShadingRate, iface, base, combiners);
}

static void STDMETHODCALLTYPE d3d12_command_list_RSSetShadingRateImage_profiled(d3d12_command_list_iface *iface,
        ID3D12Resource *image)
{
    COMMAND_LIST_PROFILED_CALL(RSSetShadingRateImage, iface, image);
}

static CONST_VTBL struct ID3D12GraphicsCommandList5Vtbl d3d12_command_list_vtbl_profiled =
{
    /* IUnknown methods */
    d3d12_command_list_QueryInterface,
    d3d12_command_list_AddRef,
    d3d12_command_list_Release,
    /* ID3D12Object methods */
    d3d12_command_list_GetPrivateData,
    d3d12_command_list_SetPrivateData,
    d3d12_command_list_SetPrivateDataInterface,
    (void *)d3d12_object_SetName,
    /* ID3D12DeviceChild methods */
    d3d12_command_list_GetDevice,
    /* ID3D12CommandList methods */
    d3d12_command_list_GetType,
    /* ID3D12GraphicsCommandList methods */
    d3d12_command_list_Close,
    d3d12_command_list_Reset,
    d3d12_command_list_ClearState,
    d3d12_command_list_DrawInstanced_profiled,
    d3d12_command_list_DrawIndexedInstanced_profiled,
    d3d12_command_list_Dispatch_profiled,
    d3d12_command_list_CopyBufferRegion_profiled,
    d3d12_command_list_CopyTextureRegion_profiled,
    d3d12_command_list_CopyResource_profiled,
    d3d12_command_list_CopyTiles_profiled,
    d3d12_command_list_ResolveSubresource_profiled,
    d3d12_command_list_IASetPrimitiveTopology_profiled,
    d3d12_command_list_RSSetViewports_profiled,
    d3d12_command_list_RSSetScissorRects_profiled,
    d3d12_command_list_OMSetBlendFactor_profiled,
    d3d12_command_list_OMSetStencilRef_profiled,
    d3d12_command_list_SetPipelineState_profiled,
    d3d12_command_list_ResourceBarrier_profiled,
    d3d12_command_list_ExecuteBundle_profiled,
    d3d12_command_list_SetDescriptorHeaps_profiled,
    d3d12_command_list_SetComputeRootSignature_profiled,
    d3d12_command_list_SetGraphicsRootSignature_profiled,
    d3d12_command_list_SetComputeRootDescriptorTable_profiled,
    d3d12_command_list_SetGraphicsRootDescriptorTable_profiled,
    d3d12_command_list_SetComputeRoot32BitConstant_profiled,
    d3d12_command_list_SetGraphicsRoot32BitConstant_profiled,
    d3d12_command_list_SetComputeRoot32BitConstants_profiled,
    d3d12_command_list_SetGraphicsRoot32BitConstants_profiled,
    d3d12_command_list_SetComputeRootConstantBufferView_profiled,
    d3d12_command_list_SetGraphicsRootConstantBufferView_profiled,
    d3d12_command_list_SetComputeRootShaderResourceView_profiled,
    d3d12_command_list_SetGraphicsRootShaderResourceView_profiled,
    d3d12_command_list_SetComputeRootUnorderedAccessView_profiled,
    d3d12_command_list_SetGraphicsRootUnorderedAccessView_profiled,
    d3d12_command_list_IASetIndexBuffer_profiled,
    d3d12_command_list_IASetVertexBuffers_profiled,
    d3d12_command_list_SOSetTargets_profiled,
    d3d12_command_list_OMSetRenderTargets_profiled,
    d3d12_command_list_ClearDepthStencilView_profiled,
    d3d12_command_list_ClearRenderTargetView_profiled,
    d3d12_command_list_ClearUnorderedAccessViewUint_profiled,
    d3d12_command_list_ClearUnorderedAccessViewFloat_profiled,
    d3d12_command_list_DiscardResource_profiled,
    d3d12_command_list_BeginQuery_profiled,
    d3d12_command_list_EndQuery_profiled,
    d3d12_command_list_ResolveQueryData_profiled,
    d3d12_command_list_SetPredication_profiled,
    d3d12_command_list_SetMarker_profiled,
    d3d12_command_list_BeginEvent_profiled,
    d3d12_command_list_EndEvent_profiled,
    d3d12_command_list_ExecuteIndirect_profiled,
    /* ID3D12GraphicsCommandList1 methods */
    d3d12_command_list_AtomicCopyBufferUINT_profiled,
    d3d12_command_list_AtomicCopyBufferUINT64_profiled,
    d3d12_command_list_OMSetDepthBounds_profiled,
    d3d12_command_list_SetSamplePositions_profiled,
    d3d12_command_list_ResolveSubresourceRegion_profiled,
    d3d12_command_list_SetViewInstanceMask_profiled,
    /* ID3D12GraphicsCommandList2 methods */
    d3d12_command_list_WriteBufferImmediate_profiled,
    /* ID3D12GraphicsCommandList3 methods */
    d3d12_command_list_SetProtectedResourceSession_profiled,
    /* ID3D12GraphicsCommandList4 methods */
    d3d12_command_list_BeginRenderPass_profiled,
    d3d12_command_list_EndRenderPass_profiled,
    d3d12_command_list_InitializeMetaCommand_profiled,
    d3d12_command_list_ExecuteMetaCommand_profiled,
    d3d12_command_list_BuildRaytracingAccelerationStructure_profiled,
    d3d12_command_list_EmitRaytracingAccelerationStructurePostbuildInfo_profiled,
    d3d12_command_list_CopyRaytracingAccelerationStructure_profiled,
    d3d12_command_list_SetPipelineState1_profiled,
    d3d12_command_list_DispatchRays_profiled,
    /* ID3D12GraphicsCommandList5 methods */
    d3d12_command_list_RSSetShadingRate_profiled,
    d3d12_command_list_RSSetShadingRateImage_profiled,
};

#endif
