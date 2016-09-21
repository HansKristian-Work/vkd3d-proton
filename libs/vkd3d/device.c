/*
 * Copyright 2016 Józef Kucia for CodeWeavers
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include "vkd3d_private.h"

/* ID3D12Device */
static inline struct d3d12_device *impl_from_ID3D12Device(ID3D12Device *iface)
{
    return CONTAINING_RECORD(iface, struct d3d12_device, ID3D12Device_iface);
}

static HRESULT STDMETHODCALLTYPE d3d12_device_QueryInterface(ID3D12Device *iface,
        REFIID riid, void **object)
{
    TRACE("iface %p, riid %s, object %p.\n", iface, debugstr_guid(riid), object);

    if (IsEqualGUID(riid, &IID_ID3D12Device)
            || IsEqualGUID(riid, &IID_ID3D12Object)
            || IsEqualGUID(riid, &IID_IUnknown))
    {
        ID3D12Device_AddRef(iface);
        *object = iface;
        return S_OK;
    }

    WARN("%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid(riid));

    *object = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE d3d12_device_AddRef(ID3D12Device *iface)
{
    struct d3d12_device *device = impl_from_ID3D12Device(iface);
    ULONG refcount = InterlockedIncrement(&device->refcount);

    TRACE("%p increasing refcount to %u.\n", device, refcount);

    return refcount;
}

static ULONG STDMETHODCALLTYPE d3d12_device_Release(ID3D12Device *iface)
{
    struct d3d12_device *device = impl_from_ID3D12Device(iface);
    ULONG refcount = InterlockedDecrement(&device->refcount);

    TRACE("%p decreasing refcount to %u.\n", device, refcount);

    if (!refcount)
        vkd3d_free(device);

    return refcount;
}

static HRESULT STDMETHODCALLTYPE d3d12_device_GetPrivateData(ID3D12Device *iface,
        REFGUID guid, UINT *data_size, void *data)
{
    FIXME("iface %p, guid %s, data_size %p, data %p stub!",
            iface, debugstr_guid(guid), data_size, data);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d12_device_SetPrivateData(ID3D12Device *iface,
        REFGUID guid, UINT data_size, const void *data)
{
    FIXME("iface %p, guid %s, data_size %u, data %p stub!\n",
            iface, debugstr_guid(guid), data_size, data);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d12_device_SetPrivateDataInterface(ID3D12Device *iface,
        REFGUID guid, const IUnknown *data)
{
    FIXME("iface %p, guid %s, data %p stub!\n", iface, debugstr_guid(guid), data);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d12_device_SetName(ID3D12Device *iface, const WCHAR *name)
{
    FIXME("iface %p, name %s stub!\n", iface, debugstr_w(name));

    return E_NOTIMPL;
}

static UINT STDMETHODCALLTYPE d3d12_device_GetNodeCount(ID3D12Device *iface)
{
    TRACE("iface %p.\n", iface);

    return 1;
}

static HRESULT STDMETHODCALLTYPE d3d12_device_CreateCommandQueue(ID3D12Device *iface,
        const D3D12_COMMAND_QUEUE_DESC *desc, REFIID riid, void **command_queue)
{
    struct d3d12_device *device = impl_from_ID3D12Device(iface);
    struct d3d12_command_queue *object;
    HRESULT hr;

    TRACE("iface %p, desc %p, riid %s, command_queue %p.\n",
            iface, desc, debugstr_guid(riid), command_queue);

    if (FAILED(hr = d3d12_command_queue_create(device, desc, &object)))
        return hr;

    return return_interface((IUnknown *)&object->ID3D12CommandQueue_iface, &IID_ID3D12CommandQueue,
            riid, command_queue);
}

static HRESULT STDMETHODCALLTYPE d3d12_device_CreateCommandAllocator(ID3D12Device *iface,
        D3D12_COMMAND_LIST_TYPE type, REFIID riid, void **command_allocator)
{
    struct d3d12_device *device = impl_from_ID3D12Device(iface);
    struct d3d12_command_allocator *object;
    HRESULT hr;

    TRACE("iface %p, type %#x, riid %s, command_allocator %p.\n",
            iface, type, debugstr_guid(riid), command_allocator);

    if (FAILED(hr = d3d12_command_allocator_create(device, type, &object)))
        return hr;

    return return_interface((IUnknown *)&object->ID3D12CommandAllocator_iface, &IID_ID3D12CommandAllocator,
            riid, command_allocator);
}

static HRESULT STDMETHODCALLTYPE d3d12_device_CreateGraphicsPipelineState(ID3D12Device *iface,
        const D3D12_GRAPHICS_PIPELINE_STATE_DESC *desc, REFIID riid, void **pipeline_state)
{
    FIXME("iface %p, desc %p, riid %s, pipeline_state %p stub!\n",
            iface, desc, debugstr_guid(riid), pipeline_state);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d12_device_CreateComputePipelineState(ID3D12Device *iface,
        const D3D12_COMPUTE_PIPELINE_STATE_DESC *desc, REFIID riid, void **pipeline_state)
{
    FIXME("iface %p, desc %p, riid %s, pipeline_state %p stub!\n",
            iface, desc, debugstr_guid(riid), pipeline_state);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d12_device_CreateCommandList(ID3D12Device *iface,
        UINT node_mask, D3D12_COMMAND_LIST_TYPE type, ID3D12CommandAllocator *command_allocator,
        ID3D12PipelineState *initial_pipeline_state, REFIID riid, void **command_list)
{
    FIXME("iface %p, node_mask 0x%08x, type %#x, command_allocator %p, "
            "initial_pipeline_state %p, riid %s, command_list %p stub!\n",
            iface, node_mask, type, command_allocator,
            initial_pipeline_state, debugstr_guid(riid), command_list);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d12_device_CheckFeatureSupport(ID3D12Device *iface,
        D3D12_FEATURE feature, void *feature_data, UINT feature_data_size)
{
    TRACE("iface %p, feature %#x, feature_data %p, feature_data_size %u.\n",
            iface, feature, feature_data, feature_data_size);

    switch (feature)
    {
        case D3D12_FEATURE_FEATURE_LEVELS:
        {
            D3D12_FEATURE_DATA_FEATURE_LEVELS *data = feature_data;
            unsigned int i;

            if (feature_data_size != sizeof(*data))
            {
                WARN("Invalid size %u.\n", feature_data_size);
                return E_INVALIDARG;
            }
            if (!data->NumFeatureLevels)
                return E_INVALIDARG;

            data->MaxSupportedFeatureLevel = 0;
            for (i = 0; i < data->NumFeatureLevels; ++i)
            {
                D3D_FEATURE_LEVEL fl = data->pFeatureLevelsRequested[i];
                if (data->MaxSupportedFeatureLevel < fl && check_feature_level_support(fl))
                    data->MaxSupportedFeatureLevel = fl;
            }
            break;
        }

        default:
            FIXME("Unhandled feature %#x.\n", feature);
            return E_NOTIMPL;
    }

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d3d12_device_CreateDescriptorHeap(ID3D12Device *iface,
        const D3D12_DESCRIPTOR_HEAP_DESC *desc, REFIID riid, void **descriptor_heap)
{
    FIXME("iface %p, desc %p, riid %s, descriptor_heap %p stub!\n",
            iface, desc, debugstr_guid(riid), descriptor_heap);

    return E_NOTIMPL;
}

static UINT STDMETHODCALLTYPE d3d12_device_GetDescriptorHandleIncrementSize(ID3D12Device *iface,
        D3D12_DESCRIPTOR_HEAP_TYPE descriptor_heap_type)
{
    FIXME("iface %p, descriptor_heap_type %#x stub!\n", iface, descriptor_heap_type);

    return 0;
}

static HRESULT STDMETHODCALLTYPE d3d12_device_CreateRootSignature(ID3D12Device *iface,
        UINT node_mask, const void *bytecode, SIZE_T bytecode_length,
        REFIID riid, void **root_signature)
{
    FIXME("iface %p, node_mask 0x%08x, bytecode %p, bytecode_length %lu, "
            "riid %s, root_signature %p stub!\n",
            iface, node_mask, bytecode, bytecode_length, debugstr_guid(riid), root_signature);

    return E_NOTIMPL;
}

static void STDMETHODCALLTYPE d3d12_device_CreateConstantBufferView(ID3D12Device *iface,
        const D3D12_CONSTANT_BUFFER_VIEW_DESC *desc, D3D12_CPU_DESCRIPTOR_HANDLE descriptor)
{
    FIXME("iface %p, desc %p, descriptor %#lx stub!\n", iface, desc, descriptor.ptr);
}

static void STDMETHODCALLTYPE d3d12_device_CreateShaderResourceView(ID3D12Device *iface,
        ID3D12Resource *resource, const D3D12_SHADER_RESOURCE_VIEW_DESC *desc,
        D3D12_CPU_DESCRIPTOR_HANDLE descriptor)
{
    FIXME("iface %p, resource %p, desc %p, descriptor %#lx stub!\n",
            iface, resource, desc, descriptor.ptr);
}

static void STDMETHODCALLTYPE d3d12_device_CreateUnorderedAccessView(ID3D12Device *iface,
        ID3D12Resource *resource, ID3D12Resource *counter_resource,
        const D3D12_UNORDERED_ACCESS_VIEW_DESC *desc, D3D12_CPU_DESCRIPTOR_HANDLE descriptor)
{
    FIXME("iface %p, resource %p, counter_resource %p, desc %p, descriptor %#lx stub!\n",
            iface, resource, counter_resource, desc, descriptor.ptr);
}

static void STDMETHODCALLTYPE d3d12_device_CreateRenderTargetView(ID3D12Device *iface,
        ID3D12Resource *resource, const D3D12_RENDER_TARGET_VIEW_DESC *desc,
        D3D12_CPU_DESCRIPTOR_HANDLE descriptor)
{
    FIXME("iface %p, resource %p, desc %p, descriptor %#lx stub!\n",
            iface, resource, desc, descriptor.ptr);
}

static void STDMETHODCALLTYPE d3d12_device_CreateDepthStencilView(ID3D12Device *iface,
        ID3D12Resource *resource, const D3D12_DEPTH_STENCIL_VIEW_DESC *desc,
        D3D12_CPU_DESCRIPTOR_HANDLE descriptor)
{
    FIXME("iface %p, resource %p, desc %p, descriptor %#lx stub!\n",
            iface, resource, desc, descriptor.ptr);
}

static void STDMETHODCALLTYPE d3d12_device_CreateSampler(ID3D12Device *iface,
        const D3D12_SAMPLER_DESC *desc, D3D12_CPU_DESCRIPTOR_HANDLE descriptor)
{
    FIXME("iface %p, desc %p, descriptor %#lx stub!\n",
            iface, desc, descriptor.ptr);
}

static void STDMETHODCALLTYPE d3d12_device_CopyDescriptors(ID3D12Device *iface,
        UINT dst_descriptor_range_count, const D3D12_CPU_DESCRIPTOR_HANDLE *dst_descriptor_range_offsets,
        const UINT *dst_descriptor_range_sizes,
        UINT src_descriptor_range_count, const D3D12_CPU_DESCRIPTOR_HANDLE *src_descriptor_range_offsets,
        const UINT *src_descriptor_range_sizes,
        D3D12_DESCRIPTOR_HEAP_TYPE descriptor_heap_type)
{
    FIXME("iface %p, dst_descriptor_range_count %u, dst_descriptor_range_offsets %p, "
            "dst_descriptor_range_sizes %p, src_descriptor_range_count %u, "
            "src_descriptor_range_offsets %p, src_descriptor_range_sizes %p, "
            "descriptor_heap_type %#x stub!\n",
            iface, dst_descriptor_range_count, dst_descriptor_range_offsets,
            dst_descriptor_range_sizes, src_descriptor_range_count, src_descriptor_range_offsets,
            src_descriptor_range_sizes, descriptor_heap_type);
}

static void STDMETHODCALLTYPE d3d12_device_CopyDescriptorsSimple(ID3D12Device *iface,
        UINT descriptor_count, const D3D12_CPU_DESCRIPTOR_HANDLE dst_descriptor_range_offset,
        const D3D12_CPU_DESCRIPTOR_HANDLE src_descriptor_range_offset,
        D3D12_DESCRIPTOR_HEAP_TYPE descriptor_heap_type)
{
    FIXME("iface %p, descriptor_count %u, dst_descriptor_range_offset %#lx, "
            "src_descriptor_range_offset %#lx, descriptor_heap_type %#x stub!\n",
            iface, descriptor_count, dst_descriptor_range_offset.ptr, src_descriptor_range_offset.ptr,
            descriptor_heap_type);
}

static D3D12_RESOURCE_ALLOCATION_INFO * STDMETHODCALLTYPE d3d12_device_GetResourceAllocationInfo(
        ID3D12Device *iface, D3D12_RESOURCE_ALLOCATION_INFO *allocation_info, UINT visible_mask,
        UINT resource_desc_count, const D3D12_RESOURCE_DESC *resource_descs)
{
    FIXME("iface %p, allocation_info %p, visible_mask 0x%08x, resource_desc_count %u, "
            "resource_descs %p stub!\n",
            iface, allocation_info, visible_mask, resource_desc_count, resource_descs);

    return allocation_info;
}

static D3D12_HEAP_PROPERTIES * STDMETHODCALLTYPE d3d12_device_GetCustomHeapProperties(ID3D12Device *iface,
        D3D12_HEAP_PROPERTIES *heap_properties, UINT node_mask, D3D12_HEAP_TYPE heap_type)
{
    FIXME("iface %p, heap_properties %p, node_mask 0x%08x, heap_type %#x stub!\n",
            iface, heap_properties, node_mask, heap_type);

    return heap_properties;
}

static HRESULT STDMETHODCALLTYPE d3d12_device_CreateCommittedResource(ID3D12Device *iface,
        const D3D12_HEAP_PROPERTIES *heap_properties, D3D12_HEAP_FLAGS heap_flags,
        const D3D12_RESOURCE_DESC *desc, D3D12_RESOURCE_STATES initial_state,
        const D3D12_CLEAR_VALUE *optimized_clear_value, REFIID riid, void **resource)
{
    FIXME("iface %p, heap_properties %p, heap_flags %#x,  desc %p, initial_state %#x, "
            "optimized_clear_value %p, riid %s, resource %p stub!\n",
            iface, heap_properties, heap_flags, desc, initial_state,
            optimized_clear_value, debugstr_guid(riid), resource);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d12_device_CreateHeap(ID3D12Device *iface,
        const D3D12_HEAP_DESC *desc, REFIID riid, void **heap)
{
    FIXME("iface %p, desc %p, riid %s, heap %p stub!\n",
            iface, desc, debugstr_guid(riid), heap);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d12_device_CreatePlacedResource(ID3D12Device *iface,
        ID3D12Heap *heap, UINT64 heap_offset,
        const D3D12_RESOURCE_DESC *desc, D3D12_RESOURCE_STATES initial_state,
        const D3D12_CLEAR_VALUE *optimized_clear_value,
        REFIID riid, void **resource)
{
    FIXME("iface %p, heap %p, heap_offset %s, desc %p, initial_state %#x, "
            "optimized_clear_value %p, riid %s, resource %p stub!\n",
            iface, heap, debugstr_uint64(heap_offset), desc, initial_state,
            optimized_clear_value, debugstr_guid(riid), resource);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d12_device_CreateReservedResource(ID3D12Device *iface,
        const D3D12_RESOURCE_DESC *desc, D3D12_RESOURCE_STATES initial_state,
        const D3D12_CLEAR_VALUE *optimized_clear_value,
        REFIID riid, void **resource)
{
    FIXME("iface %p, desc %p, initial_state %#x, optimized_clear_value %p, "
            "riid %s, resource %p stub!\n",
            iface, desc, initial_state, optimized_clear_value, debugstr_guid(riid), resource);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d12_device_CreateSharedHandle(ID3D12Device *iface,
        ID3D12DeviceChild *object, const SECURITY_ATTRIBUTES *attributes, DWORD access,
        const WCHAR *name, HANDLE *handle)
{
    FIXME("iface %p, object %p, attributes %p, access %#x, name %s, handle %p stub!\n",
            iface, object, attributes, access, debugstr_w(name), handle);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d12_device_OpenSharedHandle(ID3D12Device *iface,
        HANDLE handle, REFIID riid, void **object)
{
    FIXME("iface %p, handle %p, riid %s, object %p stub!\n",
            iface, handle, debugstr_guid(riid), object);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d12_device_OpenSharedHandleByName(ID3D12Device *iface,
        const WCHAR *name, DWORD access, HANDLE *handle)
{
    FIXME("iface %p, name %s, access %#x, handle %p stub!\n",
            iface, debugstr_w(name), access, handle);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d12_device_MakeResident(ID3D12Device *iface,
        UINT object_count, ID3D12Pageable * const *objects)
{
    FIXME("iface %p, object_count %u, objects %p stub!\n",
            iface, object_count, objects);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d12_device_Evict(ID3D12Device *iface,
        UINT object_count, ID3D12Pageable * const *objects)
{
    FIXME("iface %p, object_count %u, objects %p stub!\n",
            iface, object_count, objects);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d12_device_CreateFence(ID3D12Device *iface,
        UINT64 initial_value, D3D12_FENCE_FLAGS flags, REFIID riid, void **fence)
{
    FIXME("iface %p, intial_value %s, flags %#x, riid %s, fence %p stub!\n",
            iface, debugstr_uint64(initial_value), flags, debugstr_guid(riid), fence);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d12_device_GetDeviceRemovedReason(ID3D12Device *iface)
{
    FIXME("iface %p stub!\n", iface);

    return E_NOTIMPL;
}

static void STDMETHODCALLTYPE d3d12_device_GetCopyableFootprints(ID3D12Device *iface,
        const D3D12_RESOURCE_DESC *desc,
        UINT first_sub_resource,
        UINT sub_resource_count,
        UINT64 base_offset,
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT *layouts,
        UINT *row_count,
        UINT64 *row_size,
        UINT64 *total_bytes)
{
    FIXME("iface %p, desc %p, first_sub_resource %u, sub_resource_count %u, base_offset %s, "
            "layouts %p, row_count %p, row_size %p, total_bytes %p stub!\n",
            iface, desc, first_sub_resource, sub_resource_count, debugstr_uint64(base_offset), layouts,
            row_count, row_size, total_bytes);
}

static HRESULT STDMETHODCALLTYPE d3d12_device_CreateQueryHeap(ID3D12Device *iface,
        const D3D12_QUERY_HEAP_DESC *desc, REFIID riid, void **heap)
{
    FIXME("iface %p, desc %p, riid %s, heap %p stub!\n",
            iface, desc, debugstr_guid(riid), heap);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d12_device_SetStablePowerState(ID3D12Device *iface, BOOL enable)
{
    FIXME("iface %p, enable %#x stub!\n", iface, enable);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d12_device_CreateCommandSignature(ID3D12Device *iface,
        const D3D12_COMMAND_SIGNATURE_DESC *desc, ID3D12RootSignature *root_signature,
        REFIID riid, void **command_signature)
{
    FIXME("iface %p, desc %p, root_signature %p, riid %s, command_signature %p stub!\n",
            iface, desc, root_signature, debugstr_guid(riid), command_signature);

    return E_NOTIMPL;
}

static void STDMETHODCALLTYPE d3d12_device_GetResourceTiling(ID3D12Device *iface,
        ID3D12Resource *resource, UINT *total_tile_count,
        D3D12_PACKED_MIP_INFO *packed_mip_info, D3D12_TILE_SHAPE *standard_tile_shape,
        UINT *sub_resource_tiling_count, UINT first_sub_resource_tiling,
        D3D12_SUBRESOURCE_TILING *sub_resource_tilings)
{
    FIXME("iface %p, resource %p, total_tile_count %p, packed_mip_info %p, "
            "standard_title_shape %p, sub_resource_tiling_count %p, "
            "first_sub_resource_tiling %u, sub_resource_tilings %p stub!\n",
            iface, resource, total_tile_count, packed_mip_info, standard_tile_shape,
            sub_resource_tiling_count, first_sub_resource_tiling,
            sub_resource_tilings);
}

static LUID * STDMETHODCALLTYPE d3d12_device_GetAdapterLuid(ID3D12Device *iface, LUID *luid)
{
    FIXME("iface %p, luid %p stub!\n", iface, luid);

    return luid;
}

static const struct ID3D12DeviceVtbl d3d12_device_vtbl =
{
    /* IUnknown methods */
    d3d12_device_QueryInterface,
    d3d12_device_AddRef,
    d3d12_device_Release,
    /* ID3D12Object methods */
    d3d12_device_GetPrivateData,
    d3d12_device_SetPrivateData,
    d3d12_device_SetPrivateDataInterface,
    d3d12_device_SetName,
    /* ID3D12Device methods */
    d3d12_device_GetNodeCount,
    d3d12_device_CreateCommandQueue,
    d3d12_device_CreateCommandAllocator,
    d3d12_device_CreateGraphicsPipelineState,
    d3d12_device_CreateComputePipelineState,
    d3d12_device_CreateCommandList,
    d3d12_device_CheckFeatureSupport,
    d3d12_device_CreateDescriptorHeap,
    d3d12_device_GetDescriptorHandleIncrementSize,
    d3d12_device_CreateRootSignature,
    d3d12_device_CreateConstantBufferView,
    d3d12_device_CreateShaderResourceView,
    d3d12_device_CreateUnorderedAccessView,
    d3d12_device_CreateRenderTargetView,
    d3d12_device_CreateDepthStencilView,
    d3d12_device_CreateSampler,
    d3d12_device_CopyDescriptors,
    d3d12_device_CopyDescriptorsSimple,
    d3d12_device_GetResourceAllocationInfo,
    d3d12_device_GetCustomHeapProperties,
    d3d12_device_CreateCommittedResource,
    d3d12_device_CreateHeap,
    d3d12_device_CreatePlacedResource,
    d3d12_device_CreateReservedResource,
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
};

static void d3d12_device_init(struct d3d12_device *device)
{
    device->ID3D12Device_iface.lpVtbl = &d3d12_device_vtbl;
    device->refcount = 1;
}

HRESULT d3d12_device_create(struct d3d12_device **device)
{
    struct d3d12_device *object;

    if (!(object = vkd3d_malloc(sizeof(*object))))
        return E_OUTOFMEMORY;

    d3d12_device_init(object);

    TRACE("Created device %p.\n", object);

    *device = object;

    return S_OK;
}
