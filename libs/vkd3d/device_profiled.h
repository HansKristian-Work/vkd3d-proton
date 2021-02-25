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

#ifndef __VKD3D_DEVICE_PROFILED_H
#define __VKD3D_DEVICE_PROFILED_H

/* Only profile device commands which we know are somewhat performance sensitive. */

#define DEVICE_PROFILED_CALL_HRESULT(name, ...) \
    HRESULT hr; \
    VKD3D_REGION_DECL(name); \
    VKD3D_REGION_BEGIN(name); \
    hr = d3d12_device_##name(__VA_ARGS__); \
    VKD3D_REGION_END(name); \
    return hr

#define DEVICE_PROFILED_CALL(name, ...) \
    VKD3D_REGION_DECL(name); \
    VKD3D_REGION_BEGIN(name); \
    d3d12_device_##name(__VA_ARGS__); \
    VKD3D_REGION_END(name)

static HRESULT STDMETHODCALLTYPE d3d12_device_CreateGraphicsPipelineState_profiled(d3d12_device_iface *iface,
        const D3D12_GRAPHICS_PIPELINE_STATE_DESC *desc, REFIID riid, void **pipeline_state)
{
    DEVICE_PROFILED_CALL_HRESULT(CreateGraphicsPipelineState, iface, desc, riid, pipeline_state);
}

static HRESULT STDMETHODCALLTYPE d3d12_device_CreateComputePipelineState_profiled(d3d12_device_iface *iface,
        const D3D12_COMPUTE_PIPELINE_STATE_DESC *desc, REFIID riid, void **pipeline_state)
{
    DEVICE_PROFILED_CALL_HRESULT(CreateComputePipelineState, iface, desc, riid, pipeline_state);
}

static HRESULT STDMETHODCALLTYPE d3d12_device_CreateDescriptorHeap_profiled(d3d12_device_iface *iface,
        const D3D12_DESCRIPTOR_HEAP_DESC *desc, REFIID riid, void **descriptor_heap)
{
    DEVICE_PROFILED_CALL_HRESULT(CreateDescriptorHeap, iface, desc, riid, descriptor_heap);
}

static HRESULT STDMETHODCALLTYPE d3d12_device_CreateRootSignature_profiled(d3d12_device_iface *iface,
        UINT node_mask, const void *bytecode, SIZE_T bytecode_length,
        REFIID riid, void **root_signature)
{
    DEVICE_PROFILED_CALL_HRESULT(CreateRootSignature, iface, node_mask, bytecode, bytecode_length,
            riid, root_signature);
}

static void STDMETHODCALLTYPE d3d12_device_CreateConstantBufferView_profiled(d3d12_device_iface *iface,
        const D3D12_CONSTANT_BUFFER_VIEW_DESC *desc, D3D12_CPU_DESCRIPTOR_HANDLE descriptor)
{
    DEVICE_PROFILED_CALL(CreateConstantBufferView, iface, desc, descriptor);
}

static void STDMETHODCALLTYPE d3d12_device_CreateShaderResourceView_profiled(d3d12_device_iface *iface,
        ID3D12Resource *resource, const D3D12_SHADER_RESOURCE_VIEW_DESC *desc,
        D3D12_CPU_DESCRIPTOR_HANDLE descriptor)
{
    DEVICE_PROFILED_CALL(CreateShaderResourceView, iface, resource, desc, descriptor);
}

static void STDMETHODCALLTYPE d3d12_device_CreateUnorderedAccessView_profiled(d3d12_device_iface *iface,
        ID3D12Resource *resource, ID3D12Resource *counter_resource,
        const D3D12_UNORDERED_ACCESS_VIEW_DESC *desc, D3D12_CPU_DESCRIPTOR_HANDLE descriptor)
{
    DEVICE_PROFILED_CALL(CreateUnorderedAccessView, iface, resource, counter_resource, desc, descriptor);
}

static void STDMETHODCALLTYPE d3d12_device_CreateRenderTargetView_profiled(d3d12_device_iface *iface,
        ID3D12Resource *resource, const D3D12_RENDER_TARGET_VIEW_DESC *desc,
        D3D12_CPU_DESCRIPTOR_HANDLE descriptor)
{
    DEVICE_PROFILED_CALL(CreateRenderTargetView, iface, resource, desc, descriptor);
}

static void STDMETHODCALLTYPE d3d12_device_CreateDepthStencilView_profiled(d3d12_device_iface *iface,
        ID3D12Resource *resource, const D3D12_DEPTH_STENCIL_VIEW_DESC *desc,
        D3D12_CPU_DESCRIPTOR_HANDLE descriptor)
{
    DEVICE_PROFILED_CALL(CreateDepthStencilView, iface, resource, desc, descriptor);
}

static void STDMETHODCALLTYPE d3d12_device_CreateSampler_profiled(d3d12_device_iface *iface,
        const D3D12_SAMPLER_DESC *desc, D3D12_CPU_DESCRIPTOR_HANDLE descriptor)
{
    DEVICE_PROFILED_CALL(CreateSampler, iface, desc, descriptor);
}

static void STDMETHODCALLTYPE d3d12_device_CopyDescriptors_profiled(d3d12_device_iface *iface,
        UINT dst_descriptor_range_count, const D3D12_CPU_DESCRIPTOR_HANDLE *dst_descriptor_range_offsets,
        const UINT *dst_descriptor_range_sizes,
        UINT src_descriptor_range_count, const D3D12_CPU_DESCRIPTOR_HANDLE *src_descriptor_range_offsets,
        const UINT *src_descriptor_range_sizes,
        D3D12_DESCRIPTOR_HEAP_TYPE descriptor_heap_type)
{
    VKD3D_REGION_DECL(CopyDescriptors);
    unsigned int total_descriptors, total_descriptors_src, total_descriptors_dst, i;

    if (src_descriptor_range_sizes)
    {
        for (i = 0, total_descriptors_src = 0; i < src_descriptor_range_count; i++)
            total_descriptors_src += src_descriptor_range_sizes[i];
    }
    else
        total_descriptors_src = src_descriptor_range_count;

    if (dst_descriptor_range_sizes)
    {
        for (i = 0, total_descriptors_dst = 0; i < dst_descriptor_range_count; i++)
            total_descriptors_dst += dst_descriptor_range_sizes[i];
    }
    else
        total_descriptors_dst = dst_descriptor_range_count;

    VKD3D_REGION_BEGIN(CopyDescriptors);
    d3d12_device_CopyDescriptors(iface,
            dst_descriptor_range_count, dst_descriptor_range_offsets,
            dst_descriptor_range_sizes,
            src_descriptor_range_count, src_descriptor_range_offsets,
            src_descriptor_range_sizes,
            descriptor_heap_type);

    total_descriptors = total_descriptors_src < total_descriptors_dst ? total_descriptors_src : total_descriptors_dst;
    VKD3D_REGION_END_ITERATIONS(CopyDescriptors, total_descriptors);
}

static void STDMETHODCALLTYPE d3d12_device_CopyDescriptorsSimple_profiled(d3d12_device_iface *iface,
        UINT descriptor_count, const D3D12_CPU_DESCRIPTOR_HANDLE dst_descriptor_range_offset,
        const D3D12_CPU_DESCRIPTOR_HANDLE src_descriptor_range_offset,
        D3D12_DESCRIPTOR_HEAP_TYPE descriptor_heap_type)
{
    VKD3D_REGION_DECL(CopyDescriptorsSimple);
    VKD3D_REGION_BEGIN(CopyDescriptorsSimple);
    d3d12_device_CopyDescriptorsSimple(iface, descriptor_count, dst_descriptor_range_offset,
            src_descriptor_range_offset, descriptor_heap_type);
    VKD3D_REGION_END_ITERATIONS(CopyDescriptorsSimple, descriptor_count);
}

static HRESULT STDMETHODCALLTYPE d3d12_device_CreateCommittedResource_profiled(d3d12_device_iface *iface,
        const D3D12_HEAP_PROPERTIES *heap_properties, D3D12_HEAP_FLAGS heap_flags,
        const D3D12_RESOURCE_DESC *desc, D3D12_RESOURCE_STATES initial_state,
        const D3D12_CLEAR_VALUE *optimized_clear_value, REFIID iid, void **resource)
{
    DEVICE_PROFILED_CALL_HRESULT(CreateCommittedResource, iface, heap_properties, heap_flags,
            desc, initial_state,
            optimized_clear_value, iid, resource);
}

static HRESULT STDMETHODCALLTYPE d3d12_device_CreateCommittedResource1_profiled(d3d12_device_iface *iface,
        const D3D12_HEAP_PROPERTIES *heap_properties, D3D12_HEAP_FLAGS heap_flags,
        const D3D12_RESOURCE_DESC *desc, D3D12_RESOURCE_STATES initial_state,
        const D3D12_CLEAR_VALUE *optimized_clear_value,
        ID3D12ProtectedResourceSession *protected_session,
        REFIID iid, void **resource)
{
    DEVICE_PROFILED_CALL_HRESULT(CreateCommittedResource1, iface, heap_properties, heap_flags,
            desc, initial_state,
            optimized_clear_value, protected_session, iid, resource);
}

static HRESULT STDMETHODCALLTYPE d3d12_device_CreateHeap1_profiled(d3d12_device_iface *iface,
        const D3D12_HEAP_DESC *desc, ID3D12ProtectedResourceSession *protected_session,
        REFIID iid, void **heap)
{
    DEVICE_PROFILED_CALL_HRESULT(CreateHeap1, iface, desc, protected_session, iid, heap);
}

static HRESULT STDMETHODCALLTYPE d3d12_device_CreateHeap_profiled(d3d12_device_iface *iface,
        const D3D12_HEAP_DESC *desc, REFIID iid, void **heap)
{
    DEVICE_PROFILED_CALL_HRESULT(CreateHeap, iface, desc, iid, heap);
}

static HRESULT STDMETHODCALLTYPE d3d12_device_CreatePlacedResource_profiled(d3d12_device_iface *iface,
        ID3D12Heap *heap, UINT64 heap_offset,
        const D3D12_RESOURCE_DESC *desc, D3D12_RESOURCE_STATES initial_state,
        const D3D12_CLEAR_VALUE *optimized_clear_value, REFIID iid, void **resource)
{
    DEVICE_PROFILED_CALL_HRESULT(CreatePlacedResource, iface, heap, heap_offset,
            desc, initial_state, optimized_clear_value, iid, resource);
}

static HRESULT STDMETHODCALLTYPE d3d12_device_CreateReservedResource1_profiled(d3d12_device_iface *iface,
        const D3D12_RESOURCE_DESC *desc, D3D12_RESOURCE_STATES initial_state, const D3D12_CLEAR_VALUE *optimized_clear_value,
        ID3D12ProtectedResourceSession *protected_session, REFIID iid, void **resource)
{
    DEVICE_PROFILED_CALL_HRESULT(CreateReservedResource1, iface, desc, initial_state, optimized_clear_value, protected_session, iid, resource);
}

static HRESULT STDMETHODCALLTYPE d3d12_device_CreateReservedResource_profiled(d3d12_device_iface *iface,
        const D3D12_RESOURCE_DESC *desc, D3D12_RESOURCE_STATES initial_state,
        const D3D12_CLEAR_VALUE *optimized_clear_value, REFIID iid, void **resource)
{
    DEVICE_PROFILED_CALL_HRESULT(CreateReservedResource, iface, desc, initial_state, optimized_clear_value, iid, resource);
}

static HRESULT STDMETHODCALLTYPE d3d12_device_CreatePipelineState_profiled(d3d12_device_iface *iface,
        const D3D12_PIPELINE_STATE_STREAM_DESC *desc, REFIID riid, void **pipeline_state)
{
    DEVICE_PROFILED_CALL_HRESULT(CreatePipelineState, iface, desc, riid, pipeline_state);
}

static CONST_VTBL struct ID3D12Device6Vtbl d3d12_device_vtbl_profiled =
{
    /* IUnknown methods */
    d3d12_device_QueryInterface,
    d3d12_device_AddRef,
    d3d12_device_Release,
    /* ID3D12Object methods */
    d3d12_device_GetPrivateData,
    d3d12_device_SetPrivateData,
    d3d12_device_SetPrivateDataInterface,
    (void *)d3d12_object_SetName,
    /* ID3D12Device methods */
    d3d12_device_GetNodeCount,
    d3d12_device_CreateCommandQueue,
    d3d12_device_CreateCommandAllocator,
    d3d12_device_CreateGraphicsPipelineState_profiled,
    d3d12_device_CreateComputePipelineState_profiled,
    d3d12_device_CreateCommandList,
    d3d12_device_CheckFeatureSupport,
    d3d12_device_CreateDescriptorHeap_profiled,
    d3d12_device_GetDescriptorHandleIncrementSize,
    d3d12_device_CreateRootSignature_profiled,
    d3d12_device_CreateConstantBufferView_profiled,
    d3d12_device_CreateShaderResourceView_profiled,
    d3d12_device_CreateUnorderedAccessView_profiled,
    d3d12_device_CreateRenderTargetView_profiled,
    d3d12_device_CreateDepthStencilView_profiled,
    d3d12_device_CreateSampler_profiled,
    d3d12_device_CopyDescriptors_profiled,
    d3d12_device_CopyDescriptorsSimple_profiled,
    d3d12_device_GetResourceAllocationInfo,
    d3d12_device_GetCustomHeapProperties,
    d3d12_device_CreateCommittedResource_profiled,
    d3d12_device_CreateHeap_profiled,
    d3d12_device_CreatePlacedResource_profiled,
    d3d12_device_CreateReservedResource_profiled,
    d3d12_device_CreateSharedHandle,
    d3d12_device_OpenSharedHandle,
    d3d12_device_OpenSharedHandleByName,
    d3d12_device_MakeResident,
    d3d12_device_Evict,
    d3d12_device_CreateFence,
    d3d12_device_GetDeviceRemovedReason,
    d3d12_device_GetCopyableFootprints,
    d3d12_device_CreateQueryHeap,
    d3d12_device_SetStablePowerState,
    d3d12_device_CreateCommandSignature,
    d3d12_device_GetResourceTiling,
    d3d12_device_GetAdapterLuid,
    /* ID3D12Device1 methods */
    d3d12_device_CreatePipelineLibrary,
    d3d12_device_SetEventOnMultipleFenceCompletion,
    d3d12_device_SetResidencyPriority,
    /* ID3D12Device2 methods */
    d3d12_device_CreatePipelineState_profiled,
    /* ID3D12Device3 methods */
    d3d12_device_OpenExistingHeapFromAddress,
    d3d12_device_OpenExistingHeapFromFileMapping,
    d3d12_device_EnqueueMakeResident,
    /* ID3D12Device4 methods */
    d3d12_device_CreateCommandList1,
    d3d12_device_CreateProtectedResourceSession,
    d3d12_device_CreateCommittedResource1_profiled,
    d3d12_device_CreateHeap1_profiled,
    d3d12_device_CreateReservedResource1_profiled,
    d3d12_device_GetResourceAllocationInfo1,
    /* ID3D12Device5 methods */
    d3d12_device_CreateLifetimeTracker,
    d3d12_device_RemoveDevice,
    d3d12_device_EnumerateMetaCommands,
    d3d12_device_EnumerateMetaCommandParameters,
    d3d12_device_CreateMetaCommand,
    d3d12_device_CreateStateObject,
    d3d12_device_GetRaytracingAccelerationStructurePrebuildInfo,
    d3d12_device_CheckDriverMatchingIdentifier,
    /* ID3D12Device6 methods */
    d3d12_device_SetBackgroundProcessingMode,
};

#endif
