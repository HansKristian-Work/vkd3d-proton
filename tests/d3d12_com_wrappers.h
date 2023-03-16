/*
 * Copyright 2023 Hans-Kristian Arntzen for Valve Corporation
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

#ifndef __D3D12_COM_WRAPPERS_H
#define __D3D12_COM_WRAPPERS_H

/* WIDL headers tend to be broken with INLINE_WRAPPER calls.
 * static and FORCEINLINE end up colliding.
 * Roll our own aggregate wrappers as needed to avoid some very brittle workarounds. */
#undef ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart
#undef ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart
#undef ID3D12Resource_GetDesc
#undef ID3D12Device_GetAdapterLuid
#undef ID3D12Heap_GetDesc
#undef ID3D12Device_GetCustomHeapProperties
#undef ID3D12CommandQueue_GetDesc
#undef ID3D12Device_GetResourceAllocationInfo
#undef ID3D12Device4_GetResourceAllocationInfo1

static inline D3D12_CPU_DESCRIPTOR_HANDLE ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(ID3D12DescriptorHeap* This)
{
    D3D12_CPU_DESCRIPTOR_HANDLE __ret;
    return *This->lpVtbl->GetCPUDescriptorHandleForHeapStart(This, &__ret);
}

static inline D3D12_GPU_DESCRIPTOR_HANDLE ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(ID3D12DescriptorHeap* This)
{
    D3D12_GPU_DESCRIPTOR_HANDLE __ret;
    return *This->lpVtbl->GetGPUDescriptorHandleForHeapStart(This, &__ret);
}

static inline D3D12_RESOURCE_DESC ID3D12Resource_GetDesc(ID3D12Resource* This)
{
    D3D12_RESOURCE_DESC __ret;
    return *This->lpVtbl->GetDesc(This, &__ret);
}

static inline LUID ID3D12Device_GetAdapterLuid(ID3D12Device* This)
{
    LUID __ret;
    return *This->lpVtbl->GetAdapterLuid(This, &__ret);
}

static inline D3D12_HEAP_DESC ID3D12Heap_GetDesc(ID3D12Heap* This)
{
    D3D12_HEAP_DESC __ret;
    return *This->lpVtbl->GetDesc(This, &__ret);
}

static inline D3D12_HEAP_PROPERTIES ID3D12Device_GetCustomHeapProperties(ID3D12Device* This, UINT node_mask, D3D12_HEAP_TYPE heap_type)
{
    D3D12_HEAP_PROPERTIES __ret;
    return *This->lpVtbl->GetCustomHeapProperties(This, &__ret, node_mask, heap_type);
}

static inline D3D12_COMMAND_QUEUE_DESC ID3D12CommandQueue_GetDesc(ID3D12CommandQueue* This)
{
    D3D12_COMMAND_QUEUE_DESC __ret;
    return *This->lpVtbl->GetDesc(This, &__ret);
}

static inline D3D12_RESOURCE_ALLOCATION_INFO ID3D12Device_GetResourceAllocationInfo(ID3D12Device* This,
        UINT visible_mask, UINT reource_desc_count, const D3D12_RESOURCE_DESC *resource_descs)
{
    D3D12_RESOURCE_ALLOCATION_INFO __ret;
    return *This->lpVtbl->GetResourceAllocationInfo(This, &__ret, visible_mask, reource_desc_count, resource_descs);
}

static inline D3D12_RESOURCE_ALLOCATION_INFO ID3D12Device4_GetResourceAllocationInfo1(ID3D12Device4* This,
        UINT visible_mask, UINT reource_desc_count, const D3D12_RESOURCE_DESC *resource_descs, D3D12_RESOURCE_ALLOCATION_INFO1 *resource_allocation_infos)
{
    D3D12_RESOURCE_ALLOCATION_INFO __ret;
    return *This->lpVtbl->GetResourceAllocationInfo1(This, &__ret, visible_mask, reource_desc_count, resource_descs, resource_allocation_infos);
}
#endif
