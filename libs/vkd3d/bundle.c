/*
 * Copyright 2021 Philip Rebohle for Valve Corporation
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

/* ID3D12CommandAllocator */
static inline struct d3d12_bundle_allocator *impl_from_ID3D12CommandAllocator(ID3D12CommandAllocator *iface)
{
    return CONTAINING_RECORD(iface, struct d3d12_bundle_allocator, ID3D12CommandAllocator_iface);
}

static void *d3d12_bundle_allocator_alloc_chunk_data(struct d3d12_bundle_allocator *allocator, size_t size)
{
    size_t chunk_offset = 0;
    void *chunk = NULL;

    size = align(size, VKD3D_BUNDLE_COMMAND_ALIGNMENT);

    if (allocator->chunks_count)
    {
        chunk = allocator->chunks[allocator->chunks_count - 1];
        chunk_offset = allocator->chunk_offset;
    }

    if (!chunk || chunk_offset + size > VKD3D_BUNDLE_CHUNK_SIZE)
    {
        if (!vkd3d_array_reserve((void **)&allocator->chunks, &allocator->chunks_size,
                allocator->chunks_count + 1, sizeof(*allocator->chunks)))
            return NULL;

        if (!(chunk = vkd3d_malloc(VKD3D_BUNDLE_CHUNK_SIZE)))
            return NULL;

        allocator->chunks[allocator->chunks_count++] = chunk;
        allocator->chunk_offset = chunk_offset = 0;
    }

    allocator->chunk_offset = chunk_offset + size;
    return void_ptr_offset(chunk, chunk_offset);
}

static void d3d12_bundle_allocator_free_chunks(struct d3d12_bundle_allocator *allocator)
{
    size_t i;

    for (i = 0; i < allocator->chunks_count; i++)
        vkd3d_free(allocator->chunks[i]);

    vkd3d_free(allocator->chunks);
    allocator->chunks = NULL;
    allocator->chunks_size = 0;
    allocator->chunks_count = 0;
    allocator->chunk_offset = 0;
}

static HRESULT STDMETHODCALLTYPE d3d12_bundle_allocator_QueryInterface(ID3D12CommandAllocator *iface,
        REFIID riid, void **object)
{
    TRACE("iface %p, riid %s, object %p.\n", iface, debugstr_guid(riid), object);

    if (IsEqualGUID(riid, &IID_ID3D12CommandAllocator)
            || IsEqualGUID(riid, &IID_ID3D12Pageable)
            || IsEqualGUID(riid, &IID_ID3D12DeviceChild)
            || IsEqualGUID(riid, &IID_ID3D12Object)
            || IsEqualGUID(riid, &IID_IUnknown))
    {
        ID3D12CommandAllocator_AddRef(iface);
        *object = iface;
        return S_OK;
    }

    WARN("%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid(riid));

    *object = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE d3d12_bundle_allocator_AddRef(ID3D12CommandAllocator *iface)
{
    struct d3d12_bundle_allocator *allocator = impl_from_ID3D12CommandAllocator(iface);
    ULONG refcount = InterlockedIncrement(&allocator->refcount);

    TRACE("%p increasing refcount to %u.\n", allocator, refcount);

    return refcount;
}

static ULONG STDMETHODCALLTYPE d3d12_bundle_allocator_Release(ID3D12CommandAllocator *iface)
{
    struct d3d12_bundle_allocator *allocator = impl_from_ID3D12CommandAllocator(iface);
    ULONG refcount = InterlockedDecrement(&allocator->refcount);

    TRACE("%p decreasing refcount to %u.\n", allocator, refcount);

    if (!refcount)
    {
        d3d12_bundle_allocator_free_chunks(allocator);
        vkd3d_free(allocator);
    }

    return refcount;
}

static HRESULT STDMETHODCALLTYPE d3d12_bundle_allocator_GetPrivateData(ID3D12CommandAllocator *iface,
        REFGUID guid, UINT *data_size, void *data)
{
    struct d3d12_bundle_allocator *allocator = impl_from_ID3D12CommandAllocator(iface);

    TRACE("iface %p, guid %s, data_size %p, data %p.\n", iface, debugstr_guid(guid), data_size, data);

    return vkd3d_get_private_data(&allocator->private_store, guid, data_size, data);
}

static HRESULT STDMETHODCALLTYPE d3d12_bundle_allocator_SetPrivateData(ID3D12CommandAllocator *iface,
        REFGUID guid, UINT data_size, const void *data)
{
    struct d3d12_bundle_allocator *allocator = impl_from_ID3D12CommandAllocator(iface);

    TRACE("iface %p, guid %s, data_size %u, data %p.\n", iface, debugstr_guid(guid), data_size, data);

    return vkd3d_set_private_data(&allocator->private_store, guid, data_size, data, NULL, allocator);
}

static HRESULT STDMETHODCALLTYPE d3d12_bundle_allocator_SetPrivateDataInterface(ID3D12CommandAllocator *iface,
        REFGUID guid, const IUnknown *data)
{
    struct d3d12_bundle_allocator *allocator = impl_from_ID3D12CommandAllocator(iface);

    TRACE("iface %p, guid %s, data %p.\n", iface, debugstr_guid(guid), data);

    return vkd3d_set_private_data_interface(&allocator->private_store, guid, data, NULL, allocator);
}

static HRESULT STDMETHODCALLTYPE d3d12_bundle_allocator_GetDevice(ID3D12CommandAllocator *iface, REFIID iid, void **device)
{
    struct d3d12_bundle_allocator *allocator = impl_from_ID3D12CommandAllocator(iface);

    TRACE("iface %p, iid %s, device %p.\n", iface, debugstr_guid(iid), device);

    return d3d12_device_query_interface(allocator->device, iid, device);
}

static HRESULT STDMETHODCALLTYPE d3d12_bundle_allocator_Reset(ID3D12CommandAllocator *iface)
{
    struct d3d12_bundle_allocator *allocator = impl_from_ID3D12CommandAllocator(iface);
    struct d3d12_bundle *bundle;

    TRACE("iface %p.\n", iface);

    if ((bundle = allocator->current_bundle))
    {
        if (bundle->is_recording)
        {
            WARN("Command allocator has bundle in recording state.\n");
            return E_FAIL;
        }

        bundle->head = NULL;
        bundle->tail = NULL;
    }

    d3d12_bundle_allocator_free_chunks(allocator);
    return S_OK;
}

static CONST_VTBL struct ID3D12CommandAllocatorVtbl d3d12_bundle_allocator_vtbl =
{
    /* IUnknown methods */
    d3d12_bundle_allocator_QueryInterface,
    d3d12_bundle_allocator_AddRef,
    d3d12_bundle_allocator_Release,
    /* ID3D12Object methods */
    d3d12_bundle_allocator_GetPrivateData,
    d3d12_bundle_allocator_SetPrivateData,
    d3d12_bundle_allocator_SetPrivateDataInterface,
    (void *)d3d12_object_SetName,
    /* ID3D12DeviceChild methods */
    d3d12_bundle_allocator_GetDevice,
    /* ID3D12CommandAllocator methods */
    d3d12_bundle_allocator_Reset,
};

HRESULT d3d12_bundle_allocator_create(struct d3d12_device *device,
        struct d3d12_bundle_allocator **allocator)
{
    struct d3d12_bundle_allocator *object;
    HRESULT hr;
    
    if (!(object = vkd3d_calloc(1, sizeof(*object))))
        return E_OUTOFMEMORY;

    object->ID3D12CommandAllocator_iface.lpVtbl = &d3d12_bundle_allocator_vtbl;
    object->refcount = 1;
    object->device = device;

    if (FAILED(hr = vkd3d_private_store_init(&object->private_store)))
    {
        vkd3d_free(object);
        return hr;
    }

    *allocator = object;
    return S_OK;
}

static struct d3d12_bundle_allocator *d3d12_bundle_allocator_from_iface(ID3D12CommandAllocator *iface)
{
    if (!iface || iface->lpVtbl != &d3d12_bundle_allocator_vtbl)
        return NULL;

    return impl_from_ID3D12CommandAllocator(iface);
}

/* ID3D12GraphicsCommandList */
static inline struct d3d12_bundle *impl_from_ID3D12GraphicsCommandList(d3d12_command_list_iface *iface)
{
    return CONTAINING_RECORD(iface, struct d3d12_bundle, ID3D12GraphicsCommandList_iface);
}

void *d3d12_bundle_add_command(struct d3d12_bundle *bundle, pfn_d3d12_bundle_command proc, size_t size)
{
    struct d3d12_bundle_command *command = d3d12_bundle_allocator_alloc_chunk_data(bundle->allocator, size);

    command->proc = proc;
    command->next = NULL;

    if (bundle->tail)
        bundle->tail->next = command;
    else
        bundle->head = command;

    bundle->tail = command;
    return command;
}

static HRESULT STDMETHODCALLTYPE d3d12_bundle_QueryInterface(d3d12_command_list_iface *iface,
        REFIID iid, void **object)
{
    TRACE("iface %p, iid %s, object %p.\n", iface, debugstr_guid(iid), object);

    if (IsEqualGUID(iid, &IID_ID3D12GraphicsCommandList)
            || IsEqualGUID(iid, &IID_ID3D12GraphicsCommandList1)
            || IsEqualGUID(iid, &IID_ID3D12GraphicsCommandList2)
            || IsEqualGUID(iid, &IID_ID3D12GraphicsCommandList3)
            || IsEqualGUID(iid, &IID_ID3D12GraphicsCommandList4)
            || IsEqualGUID(iid, &IID_ID3D12GraphicsCommandList5)
            || IsEqualGUID(iid, &IID_ID3D12GraphicsCommandList6)
            || IsEqualGUID(iid, &IID_ID3D12GraphicsCommandList7)
            || IsEqualGUID(iid, &IID_ID3D12GraphicsCommandList8)
            || IsEqualGUID(iid, &IID_ID3D12GraphicsCommandList9)
            || IsEqualGUID(iid, &IID_ID3D12CommandList)
            || IsEqualGUID(iid, &IID_ID3D12DeviceChild)
            || IsEqualGUID(iid, &IID_ID3D12Object)
            || IsEqualGUID(iid, &IID_IUnknown))
    {
        ID3D12GraphicsCommandList9_AddRef(iface);
        *object = iface;
        return S_OK;
    }

    WARN("%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid(iid));

    *object = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE d3d12_bundle_AddRef(d3d12_command_list_iface *iface)
{
    struct d3d12_bundle *bundle = impl_from_ID3D12GraphicsCommandList(iface);
    ULONG refcount = InterlockedIncrement(&bundle->refcount);

    TRACE("%p increasing refcount to %u.\n", bundle, refcount);

    return refcount;
}

static ULONG STDMETHODCALLTYPE d3d12_bundle_Release(d3d12_command_list_iface *iface)
{
    struct d3d12_bundle *bundle = impl_from_ID3D12GraphicsCommandList(iface);
    ULONG refcount = InterlockedDecrement(&bundle->refcount);

    TRACE("%p decreasing refcount to %u.\n", bundle, refcount);

    if (!refcount)
    {
        if (bundle->allocator && bundle->allocator->current_bundle == bundle)
            bundle->allocator->current_bundle = NULL;

        d3d12_device_release(bundle->device);
        vkd3d_free(bundle);
    }

    return refcount;
}

static HRESULT STDMETHODCALLTYPE d3d12_bundle_GetPrivateData(d3d12_command_list_iface *iface,
        REFGUID guid, UINT *data_size, void *data)
{
    struct d3d12_bundle *bundle = impl_from_ID3D12GraphicsCommandList(iface);

    TRACE("iface %p, guid %s, data_size %p, data %p.\n", iface, debugstr_guid(guid), data_size, data);

    return vkd3d_get_private_data(&bundle->private_store, guid, data_size, data);
}

static HRESULT STDMETHODCALLTYPE d3d12_bundle_SetPrivateData(d3d12_command_list_iface *iface,
        REFGUID guid, UINT data_size, const void *data)
{
    struct d3d12_bundle *bundle = impl_from_ID3D12GraphicsCommandList(iface);

    TRACE("iface %p, guid %s, data_size %u, data %p.\n", iface, debugstr_guid(guid), data_size, data);

    return vkd3d_set_private_data(&bundle->private_store, guid, data_size, data,
            NULL, NULL);
}

static HRESULT STDMETHODCALLTYPE d3d12_bundle_SetPrivateDataInterface(d3d12_command_list_iface *iface,
        REFGUID guid, const IUnknown *data)
{
    struct d3d12_bundle *bundle = impl_from_ID3D12GraphicsCommandList(iface);

    TRACE("iface %p, guid %s, data %p.\n", iface, debugstr_guid(guid), data);

    return vkd3d_set_private_data_interface(&bundle->private_store, guid, data,
            NULL, NULL);
}

static HRESULT STDMETHODCALLTYPE d3d12_bundle_GetDevice(d3d12_command_list_iface *iface, REFIID iid, void **device)
{
    struct d3d12_bundle *bundle = impl_from_ID3D12GraphicsCommandList(iface);

    TRACE("iface %p, iid %s, device %p.\n", iface, debugstr_guid(iid), device);

    return d3d12_device_query_interface(bundle->device, iid, device);
}

static D3D12_COMMAND_LIST_TYPE STDMETHODCALLTYPE d3d12_bundle_GetType(d3d12_command_list_iface *iface)
{
    TRACE("iface %p.\n", iface);

    return D3D12_COMMAND_LIST_TYPE_BUNDLE;
}

static HRESULT STDMETHODCALLTYPE d3d12_bundle_Close(d3d12_command_list_iface *iface)
{
    struct d3d12_bundle *bundle = impl_from_ID3D12GraphicsCommandList(iface);

    TRACE("iface %p.\n", iface);

    if (!bundle->is_recording)
    {
        WARN("Bundle is not in the recording state.\n");
        return E_FAIL;
    }

    bundle->is_recording = false;
    return S_OK;
}

static void STDMETHODCALLTYPE d3d12_bundle_SetPipelineState(d3d12_command_list_iface *iface,
        ID3D12PipelineState *pipeline_state);

static HRESULT STDMETHODCALLTYPE d3d12_bundle_Reset(d3d12_command_list_iface *iface,
        ID3D12CommandAllocator *allocator, ID3D12PipelineState *initial_pipeline_state)
{
    struct d3d12_bundle_allocator *bundle_allocator = d3d12_bundle_allocator_from_iface(allocator);
    struct d3d12_bundle *bundle = impl_from_ID3D12GraphicsCommandList(iface);

    if (bundle->is_recording)
    {
        WARN("Bundle is in the recording state.\n");
        return E_FAIL;
    }

    if (!bundle_allocator)
    {
        WARN("Invalid command allocator.\n");
        return E_INVALIDARG;
    }

    if ((bundle_allocator->current_bundle && bundle_allocator->current_bundle->is_recording))
    {
        WARN("Command allocator in use.\n");
        return E_INVALIDARG;
    }

    bundle->is_recording = true;
    bundle->allocator = bundle_allocator;
    bundle->head = NULL;
    bundle->tail = NULL;

    bundle_allocator->current_bundle = bundle;

    if (initial_pipeline_state)
        d3d12_bundle_SetPipelineState(iface, initial_pipeline_state);

    return S_OK;
}

static void STDMETHODCALLTYPE d3d12_bundle_ClearState(d3d12_command_list_iface *iface,
        ID3D12PipelineState *pipeline_state)
{
    WARN("iface %p, pipeline_state %p ignored!\n", iface, pipeline_state);
}

struct d3d12_draw_instanced_command
{
    struct d3d12_bundle_command head;
    UINT vertex_count;
    UINT instance_count;
    UINT first_vertex;
    UINT first_instance;
};

static void d3d12_bundle_exec_draw_instanced(d3d12_command_list_iface *list, const void *args_v)
{
    const struct d3d12_draw_instanced_command *args = args_v;

    ID3D12GraphicsCommandList9_DrawInstanced(list, args->vertex_count,
            args->instance_count, args->first_vertex, args->first_instance);
}

static void STDMETHODCALLTYPE d3d12_bundle_DrawInstanced(d3d12_command_list_iface *iface,
        UINT vertex_count_per_instance, UINT instance_count, UINT start_vertex_location,
        UINT start_instance_location)
{
    struct d3d12_bundle *bundle = impl_from_ID3D12GraphicsCommandList(iface);
    struct d3d12_draw_instanced_command *args;

    TRACE("iface %p, vertex_count_per_instance %u, instance_count %u, "
            "start_vertex_location %u, start_instance_location %u.\n",
            iface, vertex_count_per_instance, instance_count,
            start_vertex_location, start_instance_location);

    args = d3d12_bundle_add_command(bundle, &d3d12_bundle_exec_draw_instanced, sizeof(*args));
    args->vertex_count = vertex_count_per_instance;
    args->instance_count = instance_count;
    args->first_vertex = start_vertex_location;
    args->first_instance = start_instance_location;
}

struct d3d12_draw_indexed_instanced_command
{
    struct d3d12_bundle_command head;
    UINT index_count;
    UINT instance_count;
    UINT first_index;
    UINT vertex_offset;
    UINT first_instance;
};

static void d3d12_bundle_exec_draw_indexed_instanced(d3d12_command_list_iface *list, const void *args_v)
{
    const struct d3d12_draw_indexed_instanced_command *args = args_v;

    ID3D12GraphicsCommandList9_DrawIndexedInstanced(list, args->index_count,
            args->instance_count, args->first_index, args->vertex_offset,
            args->first_instance);
}

static void STDMETHODCALLTYPE d3d12_bundle_DrawIndexedInstanced(d3d12_command_list_iface *iface,
        UINT index_count_per_instance, UINT instance_count, UINT start_vertex_location,
        INT base_vertex_location, UINT start_instance_location)
{
    struct d3d12_bundle *bundle = impl_from_ID3D12GraphicsCommandList(iface);
    struct d3d12_draw_indexed_instanced_command *args;

    TRACE("iface %p, index_count_per_instance %u, instance_count %u, start_vertex_location %u, "
            "base_vertex_location %d, start_instance_location %u.\n",
            iface, index_count_per_instance, instance_count, start_vertex_location,
            base_vertex_location, start_instance_location);

    args = d3d12_bundle_add_command(bundle, &d3d12_bundle_exec_draw_indexed_instanced, sizeof(*args));
    args->index_count = index_count_per_instance;
    args->instance_count = instance_count;
    args->first_index = start_vertex_location;
    args->vertex_offset = base_vertex_location;
    args->first_instance = start_instance_location;
}

struct d3d12_dispatch_command
{
    struct d3d12_bundle_command head;
    UINT x, y, z;
};

static void d3d12_bundle_exec_dispatch(d3d12_command_list_iface *list, const void *args_v)
{
    const struct d3d12_dispatch_command *args = args_v;

    ID3D12GraphicsCommandList9_Dispatch(list, args->x, args->y, args->z);
}

static void STDMETHODCALLTYPE d3d12_bundle_Dispatch(d3d12_command_list_iface *iface,
        UINT x, UINT y, UINT z)
{
    struct d3d12_bundle *bundle = impl_from_ID3D12GraphicsCommandList(iface);
    struct d3d12_dispatch_command *args;

    TRACE("iface %p, x %u, y %u, z %u.\n", iface, x, y, z);

    args = d3d12_bundle_add_command(bundle, &d3d12_bundle_exec_dispatch, sizeof(*args));
    args->x = x;
    args->y = y;
    args->z = z;
}

static void STDMETHODCALLTYPE d3d12_bundle_CopyBufferRegion(d3d12_command_list_iface *iface,
        ID3D12Resource *dst, UINT64 dst_offset, ID3D12Resource *src, UINT64 src_offset, UINT64 byte_count)
{
    WARN("iface %p, dst_resource %p, dst_offset %#"PRIx64", src_resource %p, "
            "src_offset %#"PRIx64", byte_count %#"PRIx64" ignored!\n",
            iface, dst, dst_offset, src, src_offset, byte_count);
}

static void STDMETHODCALLTYPE d3d12_bundle_CopyTextureRegion(d3d12_command_list_iface *iface,
        const D3D12_TEXTURE_COPY_LOCATION *dst, UINT dst_x, UINT dst_y, UINT dst_z,
        const D3D12_TEXTURE_COPY_LOCATION *src, const D3D12_BOX *src_box)
{
    WARN("iface %p, dst %p, dst_x %u, dst_y %u, dst_z %u, src %p, src_box %p ignored!\n",
            iface, dst, dst_x, dst_y, dst_z, src, src_box);
}

static void STDMETHODCALLTYPE d3d12_bundle_CopyResource(d3d12_command_list_iface *iface,
        ID3D12Resource *dst, ID3D12Resource *src)
{
    WARN("iface %p, dst_resource %p, src_resource %p ignored!\n", iface, dst, src);
}

static void STDMETHODCALLTYPE d3d12_bundle_CopyTiles(d3d12_command_list_iface *iface,
        ID3D12Resource *tiled_resource, const D3D12_TILED_RESOURCE_COORDINATE *region_coord,
        const D3D12_TILE_REGION_SIZE *region_size, ID3D12Resource *buffer, UINT64 buffer_offset,
        D3D12_TILE_COPY_FLAGS flags)
{
    WARN("iface %p, tiled_resource %p, region_coord %p, region_size %p, "
            "buffer %p, buffer_offset %#"PRIx64", flags %#x ignored!\n",
            iface, tiled_resource, region_coord, region_size,
            buffer, buffer_offset, flags);
}

static void STDMETHODCALLTYPE d3d12_bundle_ResolveSubresource(d3d12_command_list_iface *iface,
        ID3D12Resource *dst, UINT dst_sub_resource_idx,
        ID3D12Resource *src, UINT src_sub_resource_idx, DXGI_FORMAT format)
{
    WARN("iface %p, dst_resource %p, dst_sub_resource_idx %u, src_resource %p, src_sub_resource_idx %u, "
            "format %#x ignored!\n", iface, dst, dst_sub_resource_idx, src, src_sub_resource_idx, format);
}

struct d3d12_ia_set_primitive_topology_command
{
    struct d3d12_bundle_command command;
    D3D12_PRIMITIVE_TOPOLOGY topology;
};

static void d3d12_bundle_exec_ia_set_primitive_topology(d3d12_command_list_iface *list, const void *args_v)
{
    const struct d3d12_ia_set_primitive_topology_command *args = args_v;

    ID3D12GraphicsCommandList9_IASetPrimitiveTopology(list, args->topology);
}

static void STDMETHODCALLTYPE d3d12_bundle_IASetPrimitiveTopology(d3d12_command_list_iface *iface,
        D3D12_PRIMITIVE_TOPOLOGY topology)
{
    struct d3d12_bundle *bundle = impl_from_ID3D12GraphicsCommandList(iface);
    struct d3d12_ia_set_primitive_topology_command *args;

    TRACE("iface %p, topology %#x.\n", iface, topology);

    args = d3d12_bundle_add_command(bundle, &d3d12_bundle_exec_ia_set_primitive_topology, sizeof(*args));
    args->topology = topology;
}

static void STDMETHODCALLTYPE d3d12_bundle_RSSetViewports(d3d12_command_list_iface *iface,
        UINT viewport_count, const D3D12_VIEWPORT *viewports)
{
    WARN("iface %p, viewport_count %u, viewports %p ignored!\n", iface, viewport_count, viewports);
}

static void STDMETHODCALLTYPE d3d12_bundle_RSSetScissorRects(d3d12_command_list_iface *iface,
        UINT rect_count, const D3D12_RECT *rects)
{
    WARN("iface %p, rect_count %u, rects %p ignored!\n", iface, rect_count, rects);
}

struct d3d12_om_set_blend_factor_command
{
    struct d3d12_bundle_command command;
    FLOAT blend_factor[4];
};

static void d3d12_bundle_exec_om_set_blend_factor(d3d12_command_list_iface *list, const void *args_v)
{
    const struct d3d12_om_set_blend_factor_command *args = args_v;

    ID3D12GraphicsCommandList9_OMSetBlendFactor(list, args->blend_factor);
}

static void STDMETHODCALLTYPE d3d12_bundle_OMSetBlendFactor(d3d12_command_list_iface *iface,
        const FLOAT blend_factor[4])
{
    struct d3d12_bundle *bundle = impl_from_ID3D12GraphicsCommandList(iface);
    struct d3d12_om_set_blend_factor_command *args;
    unsigned int i;

    TRACE("iface %p, blend_factor %p.\n", iface, blend_factor);

    args = d3d12_bundle_add_command(bundle, &d3d12_bundle_exec_om_set_blend_factor, sizeof(*args));

    for (i = 0; i < 4; i++)
        args->blend_factor[i] = blend_factor[i];
}

struct d3d12_om_set_stencil_ref_command
{
    struct d3d12_bundle_command command;
    UINT stencil_ref;
};

static void d3d12_bundle_exec_om_set_stencil_ref(d3d12_command_list_iface *list, const void *args_v)
{
    const struct d3d12_om_set_stencil_ref_command *args = args_v;

    ID3D12GraphicsCommandList9_OMSetStencilRef(list, args->stencil_ref);
}

static void STDMETHODCALLTYPE d3d12_bundle_OMSetStencilRef(d3d12_command_list_iface *iface,
        UINT stencil_ref)
{
    struct d3d12_bundle *bundle = impl_from_ID3D12GraphicsCommandList(iface);
    struct d3d12_om_set_stencil_ref_command *args;

    TRACE("iface %p, stencil_ref %u.\n", iface, stencil_ref);

    args = d3d12_bundle_add_command(bundle, &d3d12_bundle_exec_om_set_stencil_ref, sizeof(*args));
    args->stencil_ref = stencil_ref;
}

struct d3d12_set_pipeline_state_command
{
    struct d3d12_bundle_command command;
    ID3D12PipelineState *pipeline_state;
};

static void d3d12_bundle_exec_set_pipeline_state(d3d12_command_list_iface *list, const void *args_v)
{
    const struct d3d12_set_pipeline_state_command *args = args_v;

    ID3D12GraphicsCommandList9_SetPipelineState(list, args->pipeline_state);
}

static void STDMETHODCALLTYPE d3d12_bundle_SetPipelineState(d3d12_command_list_iface *iface,
        ID3D12PipelineState *pipeline_state)
{
    struct d3d12_bundle *bundle = impl_from_ID3D12GraphicsCommandList(iface);
    struct d3d12_set_pipeline_state_command *args;

    TRACE("iface %p, pipeline_state %p.\n", iface, pipeline_state);

    args = d3d12_bundle_add_command(bundle, &d3d12_bundle_exec_set_pipeline_state, sizeof(*args));
    args->pipeline_state = pipeline_state;
}

static void STDMETHODCALLTYPE d3d12_bundle_ResourceBarrier(d3d12_command_list_iface *iface,
        UINT barrier_count, const D3D12_RESOURCE_BARRIER *barriers)
{
    WARN("iface %p, barrier_count %u, barriers %p ignored!\n", iface, barrier_count, barriers);
}

static void STDMETHODCALLTYPE d3d12_bundle_ExecuteBundle(d3d12_command_list_iface *iface,
        ID3D12GraphicsCommandList *command_list)
{
    WARN("iface %p, command_list %p ignored!\n", iface, command_list);
}

static void STDMETHODCALLTYPE d3d12_bundle_SetDescriptorHeaps(d3d12_command_list_iface *iface,
        UINT heap_count, ID3D12DescriptorHeap *const *heaps)
{
    TRACE("iface %p, heap_count %u, heaps %p.\n", iface, heap_count, heaps);
    /* Apparently it is legal to call this method, but behaves like a no-op */
}

struct d3d12_set_root_signature_command
{
    struct d3d12_bundle_command command;
    ID3D12RootSignature *root_signature;
};

static void d3d12_bundle_exec_set_compute_root_signature(d3d12_command_list_iface *list, const void *args_v)
{
    const struct d3d12_set_root_signature_command *args = args_v;

    ID3D12GraphicsCommandList9_SetComputeRootSignature(list, args->root_signature);
}

static void STDMETHODCALLTYPE d3d12_bundle_SetComputeRootSignature(d3d12_command_list_iface *iface,
        ID3D12RootSignature *root_signature)
{
    struct d3d12_bundle *bundle = impl_from_ID3D12GraphicsCommandList(iface);
    struct d3d12_set_root_signature_command *args;

    TRACE("iface %p, root_signature %p.\n", iface, root_signature);

    args = d3d12_bundle_add_command(bundle, &d3d12_bundle_exec_set_compute_root_signature, sizeof(*args));
    args->root_signature = root_signature;
}

static void d3d12_bundle_exec_set_graphics_root_signature(d3d12_command_list_iface *list, const void *args_v)
{
    const struct d3d12_set_root_signature_command *args = args_v;

    ID3D12GraphicsCommandList9_SetGraphicsRootSignature(list, args->root_signature);
}

static void STDMETHODCALLTYPE d3d12_bundle_SetGraphicsRootSignature(d3d12_command_list_iface *iface,
        ID3D12RootSignature *root_signature)
{
    struct d3d12_bundle *bundle = impl_from_ID3D12GraphicsCommandList(iface);
    struct d3d12_set_root_signature_command *args;

    TRACE("iface %p, root_signature %p.\n", iface, root_signature);

    args = d3d12_bundle_add_command(bundle, &d3d12_bundle_exec_set_graphics_root_signature, sizeof(*args));
    args->root_signature = root_signature;
}

struct d3d12_set_root_descriptor_table_command
{
    struct d3d12_bundle_command command;
    UINT parameter_index;
    D3D12_GPU_DESCRIPTOR_HANDLE base_descriptor;
};

static void d3d12_bundle_exec_set_compute_root_descriptor_table(d3d12_command_list_iface *list, const void *args_v)
{
    const struct d3d12_set_root_descriptor_table_command *args = args_v;

    ID3D12GraphicsCommandList9_SetComputeRootDescriptorTable(list, args->parameter_index, args->base_descriptor);
}

static void STDMETHODCALLTYPE d3d12_bundle_SetComputeRootDescriptorTable(d3d12_command_list_iface *iface,
        UINT root_parameter_index, D3D12_GPU_DESCRIPTOR_HANDLE base_descriptor)
{
    struct d3d12_bundle *bundle = impl_from_ID3D12GraphicsCommandList(iface);
    struct d3d12_set_root_descriptor_table_command *args;

    TRACE("iface %p, root_parameter_index %u, base_descriptor %#"PRIx64".\n",
            iface, root_parameter_index, base_descriptor.ptr);

    args = d3d12_bundle_add_command(bundle, &d3d12_bundle_exec_set_compute_root_descriptor_table, sizeof(*args));
    args->parameter_index = root_parameter_index;
    args->base_descriptor = base_descriptor;
}

static void d3d12_bundle_exec_set_graphics_root_descriptor_table(d3d12_command_list_iface *list, const void *args_v)
{
    const struct d3d12_set_root_descriptor_table_command *args = args_v;

    ID3D12GraphicsCommandList9_SetGraphicsRootDescriptorTable(list, args->parameter_index, args->base_descriptor);
}

static void STDMETHODCALLTYPE d3d12_bundle_SetGraphicsRootDescriptorTable(d3d12_command_list_iface *iface,
        UINT root_parameter_index, D3D12_GPU_DESCRIPTOR_HANDLE base_descriptor)
{
    struct d3d12_bundle *bundle = impl_from_ID3D12GraphicsCommandList(iface);
    struct d3d12_set_root_descriptor_table_command *args;

    TRACE("iface %p, root_parameter_index %u, base_descriptor %#"PRIx64".\n",
            iface, root_parameter_index, base_descriptor.ptr);

    args = d3d12_bundle_add_command(bundle, &d3d12_bundle_exec_set_graphics_root_descriptor_table, sizeof(*args));
    args->parameter_index = root_parameter_index;
    args->base_descriptor = base_descriptor;
}

struct d3d12_set_root_32bit_constant_command
{
    struct d3d12_bundle_command command;
    UINT parameter_index;
    UINT data;
    UINT offset;
};

static void d3d12_bundle_exec_set_compute_root_32bit_constant(d3d12_command_list_iface *list, const void *args_v)
{
    const struct d3d12_set_root_32bit_constant_command *args = args_v;

    ID3D12GraphicsCommandList9_SetComputeRoot32BitConstant(list, args->parameter_index, args->data, args->offset);
}

static void STDMETHODCALLTYPE d3d12_bundle_SetComputeRoot32BitConstant(d3d12_command_list_iface *iface,
        UINT root_parameter_index, UINT data, UINT dst_offset)
{
    struct d3d12_bundle *bundle = impl_from_ID3D12GraphicsCommandList(iface);
    struct d3d12_set_root_32bit_constant_command *args;

    TRACE("iface %p, root_parameter_index %u, data 0x%08x, dst_offset %u.\n",
            iface, root_parameter_index, data, dst_offset);

    args = d3d12_bundle_add_command(bundle, &d3d12_bundle_exec_set_compute_root_32bit_constant, sizeof(*args));
    args->parameter_index = root_parameter_index;
    args->data = data;
    args->offset = dst_offset;
}

static void d3d12_bundle_exec_set_graphics_root_32bit_constant(d3d12_command_list_iface *list, const void *args_v)
{
    const struct d3d12_set_root_32bit_constant_command *args = args_v;

    ID3D12GraphicsCommandList9_SetGraphicsRoot32BitConstant(list, args->parameter_index, args->data, args->offset);
}

static void STDMETHODCALLTYPE d3d12_bundle_SetGraphicsRoot32BitConstant(d3d12_command_list_iface *iface,
        UINT root_parameter_index, UINT data, UINT dst_offset)
{
    struct d3d12_bundle *bundle = impl_from_ID3D12GraphicsCommandList(iface);
    struct d3d12_set_root_32bit_constant_command *args;

    TRACE("iface %p, root_parameter_index %u, data 0x%08x, dst_offset %u.\n",
            iface, root_parameter_index, data, dst_offset);

    args = d3d12_bundle_add_command(bundle, &d3d12_bundle_exec_set_graphics_root_32bit_constant, sizeof(*args));
    args->parameter_index = root_parameter_index;
    args->data = data;
    args->offset = dst_offset;
}

struct d3d12_set_root_32bit_constants_command
{
    struct d3d12_bundle_command command;
    UINT parameter_index;
    UINT constant_count;
    UINT offset;
    UINT data[];
};

static void d3d12_bundle_exec_set_compute_root_32bit_constants(d3d12_command_list_iface *list, const void *args_v)
{
    const struct d3d12_set_root_32bit_constants_command *args = args_v;

    ID3D12GraphicsCommandList9_SetComputeRoot32BitConstants(list, args->parameter_index,
            args->constant_count, args->data, args->offset);
}

static void STDMETHODCALLTYPE d3d12_bundle_SetComputeRoot32BitConstants(d3d12_command_list_iface *iface,
        UINT root_parameter_index, UINT constant_count, const void *data, UINT dst_offset)
{
    struct d3d12_bundle *bundle = impl_from_ID3D12GraphicsCommandList(iface);
    struct d3d12_set_root_32bit_constants_command *args;

    TRACE("iface %p, root_parameter_index %u, constant_count %u, data %p, dst_offset %u.\n",
            iface, root_parameter_index, constant_count, data, dst_offset);

    if (!constant_count)
        return;

    args = d3d12_bundle_add_command(bundle, &d3d12_bundle_exec_set_compute_root_32bit_constants,
            sizeof(*args) + sizeof(UINT) * constant_count);
    args->parameter_index = root_parameter_index;
    args->constant_count = constant_count;
    args->offset = dst_offset;
    memcpy(args->data, data, sizeof(UINT) * constant_count);
}

static void d3d12_bundle_exec_set_graphics_root_32bit_constants(d3d12_command_list_iface *list, const void *args_v)
{
    const struct d3d12_set_root_32bit_constants_command *args = args_v;

    ID3D12GraphicsCommandList9_SetGraphicsRoot32BitConstants(list, args->parameter_index,
            args->constant_count, args->data, args->offset);
}

static void STDMETHODCALLTYPE d3d12_bundle_SetGraphicsRoot32BitConstants(d3d12_command_list_iface *iface,
        UINT root_parameter_index, UINT constant_count, const void *data, UINT dst_offset)
{
    struct d3d12_bundle *bundle = impl_from_ID3D12GraphicsCommandList(iface);
    struct d3d12_set_root_32bit_constants_command *args;

    TRACE("iface %p, root_parameter_index %u, constant_count %u, data %p, dst_offset %u.\n",
            iface, root_parameter_index, constant_count, data, dst_offset);

    if (!constant_count)
        return;

    args = d3d12_bundle_add_command(bundle, &d3d12_bundle_exec_set_graphics_root_32bit_constants,
            sizeof(*args) + sizeof(UINT) * constant_count);
    args->parameter_index = root_parameter_index;
    args->constant_count = constant_count;
    args->offset = dst_offset;
    memcpy(args->data, data, sizeof(UINT) * constant_count);
}

struct d3d12_set_root_descriptor_command
{
    struct d3d12_bundle_command command;
    UINT parameter_index;
    D3D12_GPU_VIRTUAL_ADDRESS address;
};

static void d3d12_bundle_exec_set_compute_root_cbv(d3d12_command_list_iface *list, const void *args_v)
{
    const struct d3d12_set_root_descriptor_command *args = args_v;

    ID3D12GraphicsCommandList9_SetComputeRootConstantBufferView(list, args->parameter_index, args->address);
}

static void STDMETHODCALLTYPE d3d12_bundle_SetComputeRootConstantBufferView(
        d3d12_command_list_iface *iface, UINT root_parameter_index, D3D12_GPU_VIRTUAL_ADDRESS address)
{
    struct d3d12_bundle *bundle = impl_from_ID3D12GraphicsCommandList(iface);
    struct d3d12_set_root_descriptor_command *args;

    TRACE("iface %p, root_parameter_index %u, address %#"PRIx64".\n",
            iface, root_parameter_index, address);

    args = d3d12_bundle_add_command(bundle, &d3d12_bundle_exec_set_compute_root_cbv, sizeof(*args));
    args->parameter_index = root_parameter_index;
    args->address = address;
}

static void d3d12_bundle_exec_set_graphics_root_cbv(d3d12_command_list_iface *list, const void *args_v)
{
    const struct d3d12_set_root_descriptor_command *args = args_v;

    ID3D12GraphicsCommandList9_SetGraphicsRootConstantBufferView(list, args->parameter_index, args->address);
}

static void STDMETHODCALLTYPE d3d12_bundle_SetGraphicsRootConstantBufferView(
        d3d12_command_list_iface *iface, UINT root_parameter_index, D3D12_GPU_VIRTUAL_ADDRESS address)
{
    struct d3d12_bundle *bundle = impl_from_ID3D12GraphicsCommandList(iface);
    struct d3d12_set_root_descriptor_command *args;

    TRACE("iface %p, root_parameter_index %u, address %#"PRIx64".\n",
            iface, root_parameter_index, address);

    args = d3d12_bundle_add_command(bundle, &d3d12_bundle_exec_set_graphics_root_cbv, sizeof(*args));
    args->parameter_index = root_parameter_index;
    args->address = address;
}

static void d3d12_bundle_exec_set_compute_root_srv(d3d12_command_list_iface *list, const void *args_v)
{
    const struct d3d12_set_root_descriptor_command *args = args_v;

    ID3D12GraphicsCommandList9_SetComputeRootShaderResourceView(list, args->parameter_index, args->address);
}

static void STDMETHODCALLTYPE d3d12_bundle_SetComputeRootShaderResourceView(
        d3d12_command_list_iface *iface, UINT root_parameter_index, D3D12_GPU_VIRTUAL_ADDRESS address)
{
    struct d3d12_bundle *bundle = impl_from_ID3D12GraphicsCommandList(iface);
    struct d3d12_set_root_descriptor_command *args;

    TRACE("iface %p, root_parameter_index %u, address %#"PRIx64".\n",
            iface, root_parameter_index, address);

    args = d3d12_bundle_add_command(bundle, &d3d12_bundle_exec_set_compute_root_srv, sizeof(*args));
    args->parameter_index = root_parameter_index;
    args->address = address;
}

static void d3d12_bundle_exec_set_graphics_root_srv(d3d12_command_list_iface *list, const void *args_v)
{
    const struct d3d12_set_root_descriptor_command *args = args_v;

    ID3D12GraphicsCommandList9_SetGraphicsRootShaderResourceView(list, args->parameter_index, args->address);
}

static void STDMETHODCALLTYPE d3d12_bundle_SetGraphicsRootShaderResourceView(
        d3d12_command_list_iface *iface, UINT root_parameter_index, D3D12_GPU_VIRTUAL_ADDRESS address)
{
    struct d3d12_bundle *bundle = impl_from_ID3D12GraphicsCommandList(iface);
    struct d3d12_set_root_descriptor_command *args;

    TRACE("iface %p, root_parameter_index %u, address %#"PRIx64".\n",
            iface, root_parameter_index, address);

    args = d3d12_bundle_add_command(bundle, &d3d12_bundle_exec_set_graphics_root_srv, sizeof(*args));
    args->parameter_index = root_parameter_index;
    args->address = address;
}

static void d3d12_bundle_exec_set_compute_root_uav(d3d12_command_list_iface *list, const void *args_v)
{
    const struct d3d12_set_root_descriptor_command *args = args_v;

    ID3D12GraphicsCommandList9_SetComputeRootUnorderedAccessView(list, args->parameter_index, args->address);
}

static void STDMETHODCALLTYPE d3d12_bundle_SetComputeRootUnorderedAccessView(
        d3d12_command_list_iface *iface, UINT root_parameter_index, D3D12_GPU_VIRTUAL_ADDRESS address)
{
    struct d3d12_bundle *bundle = impl_from_ID3D12GraphicsCommandList(iface);
    struct d3d12_set_root_descriptor_command *args;

    TRACE("iface %p, root_parameter_index %u, address %#"PRIx64".\n",
            iface, root_parameter_index, address);

    args = d3d12_bundle_add_command(bundle, &d3d12_bundle_exec_set_compute_root_uav, sizeof(*args));
    args->parameter_index = root_parameter_index;
    args->address = address;
}

static void d3d12_bundle_exec_set_graphics_root_uav(d3d12_command_list_iface *list, const void *args_v)
{
    const struct d3d12_set_root_descriptor_command *args = args_v;

    ID3D12GraphicsCommandList9_SetGraphicsRootUnorderedAccessView(list, args->parameter_index, args->address);
}

static void STDMETHODCALLTYPE d3d12_bundle_SetGraphicsRootUnorderedAccessView(
        d3d12_command_list_iface *iface, UINT root_parameter_index, D3D12_GPU_VIRTUAL_ADDRESS address)
{
    struct d3d12_bundle *bundle = impl_from_ID3D12GraphicsCommandList(iface);
    struct d3d12_set_root_descriptor_command *args;

    TRACE("iface %p, root_parameter_index %u, address %#"PRIx64".\n",
            iface, root_parameter_index, address);

    args = d3d12_bundle_add_command(bundle, &d3d12_bundle_exec_set_graphics_root_uav, sizeof(*args));
    args->parameter_index = root_parameter_index;
    args->address = address;
}

struct d3d12_ia_set_index_buffer_command
{
    struct d3d12_bundle_command command;
    D3D12_INDEX_BUFFER_VIEW view;
};

static void d3d12_bundle_exec_ia_set_index_buffer_null(d3d12_command_list_iface *list, const void *args_v)
{
    ID3D12GraphicsCommandList9_IASetIndexBuffer(list, NULL);
}

static void d3d12_bundle_exec_ia_set_index_buffer(d3d12_command_list_iface *list, const void *args_v)
{
    const struct d3d12_ia_set_index_buffer_command *args = args_v;

    ID3D12GraphicsCommandList9_IASetIndexBuffer(list, &args->view);
}

static void STDMETHODCALLTYPE d3d12_bundle_IASetIndexBuffer(d3d12_command_list_iface *iface,
        const D3D12_INDEX_BUFFER_VIEW *view)
{
    struct d3d12_bundle *bundle = impl_from_ID3D12GraphicsCommandList(iface);

    TRACE("iface %p, view %p.\n", iface, view);

    if (view)
    {
        struct d3d12_ia_set_index_buffer_command *args;
        args = d3d12_bundle_add_command(bundle, &d3d12_bundle_exec_ia_set_index_buffer, sizeof(*args));
        args->view = *view;
    }
    else
    {
        /* Faithfully pass NULL to the command list during replay to avoid potential pitfalls */
        d3d12_bundle_add_command(bundle, &d3d12_bundle_exec_ia_set_index_buffer_null, sizeof(struct d3d12_bundle_command));
    }
}

struct d3d12_ia_set_vertex_buffers_command
{
    struct d3d12_bundle_command command;
    UINT start_slot;
    UINT view_count;
    D3D12_VERTEX_BUFFER_VIEW views[];
};

static void d3d12_bundle_exec_ia_set_vertex_buffers(d3d12_command_list_iface *list, const void *args_v)
{
    const struct d3d12_ia_set_vertex_buffers_command *args = args_v;

    ID3D12GraphicsCommandList9_IASetVertexBuffers(list, args->start_slot, args->view_count, args->views);
}

static void STDMETHODCALLTYPE d3d12_bundle_IASetVertexBuffers(d3d12_command_list_iface *iface,
        UINT start_slot, UINT view_count, const D3D12_VERTEX_BUFFER_VIEW *views)
{
    struct d3d12_bundle *bundle = impl_from_ID3D12GraphicsCommandList(iface);
    struct d3d12_ia_set_vertex_buffers_command *args;

    TRACE("iface %p, start_slot %u, view_count %u, views %p.\n", iface, start_slot, view_count, views);

    if (!view_count || !views)
        return;

    args = d3d12_bundle_add_command(bundle, &d3d12_bundle_exec_ia_set_vertex_buffers,
            sizeof(*args) + sizeof(*views) * view_count);
    args->start_slot = start_slot;
    args->view_count = view_count;
    memcpy(args->views, views, sizeof(*views) * view_count);
}

static void STDMETHODCALLTYPE d3d12_bundle_SOSetTargets(d3d12_command_list_iface *iface,
        UINT start_slot, UINT view_count, const D3D12_STREAM_OUTPUT_BUFFER_VIEW *views)
{
    WARN("iface %p, start_slot %u, view_count %u, views %p ignored!\n", iface, start_slot, view_count, views);
}

static void STDMETHODCALLTYPE d3d12_bundle_OMSetRenderTargets(d3d12_command_list_iface *iface,
        UINT render_target_descriptor_count, const D3D12_CPU_DESCRIPTOR_HANDLE *render_target_descriptors,
        BOOL single_descriptor_handle, const D3D12_CPU_DESCRIPTOR_HANDLE *depth_stencil_descriptor)
{
    WARN("iface %p, render_target_descriptor_count %u, render_target_descriptors %p, "
            "single_descriptor_handle %#x, depth_stencil_descriptor %p ignored!\n",
            iface, render_target_descriptor_count, render_target_descriptors,
            single_descriptor_handle, depth_stencil_descriptor);
}

static void STDMETHODCALLTYPE d3d12_bundle_ClearDepthStencilView(d3d12_command_list_iface *iface,
        D3D12_CPU_DESCRIPTOR_HANDLE dsv, D3D12_CLEAR_FLAGS flags, float depth, UINT8 stencil,
        UINT rect_count, const D3D12_RECT *rects)
{
    WARN("iface %p, dsv %#lx, flags %#x, depth %.8e, stencil 0x%02x, rect_count %u, rects %p ignored!\n",
            iface, dsv.ptr, flags, depth, stencil, rect_count, rects);
}

static void STDMETHODCALLTYPE d3d12_bundle_ClearRenderTargetView(d3d12_command_list_iface *iface,
        D3D12_CPU_DESCRIPTOR_HANDLE rtv, const FLOAT color[4], UINT rect_count, const D3D12_RECT *rects)
{
    WARN("iface %p, rtv %#lx, color %p, rect_count %u, rects %p ignored!\n",
            iface, rtv.ptr, color, rect_count, rects);
}

static void STDMETHODCALLTYPE d3d12_bundle_ClearUnorderedAccessViewUint(d3d12_command_list_iface *iface,
        D3D12_GPU_DESCRIPTOR_HANDLE gpu_handle, D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle, ID3D12Resource *resource,
        const UINT values[4], UINT rect_count, const D3D12_RECT *rects)
{
    WARN("iface %p, gpu_handle %#"PRIx64", cpu_handle %lx, resource %p, values %p, rect_count %u, rects %p ignored!\n",
            iface, gpu_handle.ptr, cpu_handle.ptr, resource, values, rect_count, rects);
}

static void STDMETHODCALLTYPE d3d12_bundle_ClearUnorderedAccessViewFloat(d3d12_command_list_iface *iface,
        D3D12_GPU_DESCRIPTOR_HANDLE gpu_handle, D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle, ID3D12Resource *resource,
        const float values[4], UINT rect_count, const D3D12_RECT *rects)
{
    WARN("iface %p, gpu_handle %#"PRIx64", cpu_handle %lx, resource %p, values %p, rect_count %u, rects %p ignored!\n",
            iface, gpu_handle.ptr, cpu_handle.ptr, resource, values, rect_count, rects);
}

static void STDMETHODCALLTYPE d3d12_bundle_DiscardResource(d3d12_command_list_iface *iface,
        ID3D12Resource *resource, const D3D12_DISCARD_REGION *region)
{
    WARN("iface %p, resource %p, region %p ignored!\n", iface, resource, region);
}

static void STDMETHODCALLTYPE d3d12_bundle_BeginQuery(d3d12_command_list_iface *iface,
        ID3D12QueryHeap *heap, D3D12_QUERY_TYPE type, UINT index)
{
    WARN("iface %p, heap %p, type %#x, index %u ignored!\n", iface, heap, type, index);
}

static void STDMETHODCALLTYPE d3d12_bundle_EndQuery(d3d12_command_list_iface *iface,
        ID3D12QueryHeap *heap, D3D12_QUERY_TYPE type, UINT index)
{
    WARN("iface %p, heap %p, type %#x, index %u ignored!\n", iface, heap, type, index);
}

static void STDMETHODCALLTYPE d3d12_bundle_ResolveQueryData(d3d12_command_list_iface *iface,
        ID3D12QueryHeap *heap, D3D12_QUERY_TYPE type, UINT start_index, UINT query_count,
        ID3D12Resource *dst_buffer, UINT64 aligned_dst_buffer_offset)
{
    WARN("iface %p, heap %p, type %#x, start_index %u, query_count %u, "
            "dst_buffer %p, aligned_dst_buffer_offset %#"PRIx64" ignored!\n",
            iface, heap, type, start_index, query_count,
            dst_buffer, aligned_dst_buffer_offset);
}

static void STDMETHODCALLTYPE d3d12_bundle_SetPredication(d3d12_command_list_iface *iface,
        ID3D12Resource *buffer, UINT64 aligned_buffer_offset, D3D12_PREDICATION_OP operation)
{
    WARN("iface %p, buffer %p, aligned_buffer_offset %#"PRIx64", operation %#x ignored!\n",
            iface, buffer, aligned_buffer_offset, operation);
}

struct d3d12_debug_marker_command
{
    struct d3d12_bundle_command command;
    UINT metadata;
    UINT data_size;
    char data[];
};

static void d3d12_bundle_exec_set_marker(d3d12_command_list_iface *list, const void *args_v)
{
    const struct d3d12_debug_marker_command *args = args_v;

    ID3D12GraphicsCommandList9_SetMarker(list, args->metadata, args->data, args->data_size);
}

static void STDMETHODCALLTYPE d3d12_bundle_SetMarker(d3d12_command_list_iface *iface,
        UINT metadata, const void *data, UINT size)
{
    struct d3d12_bundle *bundle = impl_from_ID3D12GraphicsCommandList(iface);
    struct d3d12_debug_marker_command *args;

    TRACE("iface %p, metadata %u, data %p, size %u.\n", iface, metadata, data, size);

    args = d3d12_bundle_add_command(bundle, &d3d12_bundle_exec_set_marker, sizeof(*args) + size);
    args->metadata = metadata;
    args->data_size = size;
    memcpy(args->data, data, size);
}

static void d3d12_bundle_exec_begin_event(d3d12_command_list_iface *list, const void *args_v)
{
    const struct d3d12_debug_marker_command *args = args_v;

    ID3D12GraphicsCommandList9_BeginEvent(list, args->metadata, args->data, args->data_size);
}

static void STDMETHODCALLTYPE d3d12_bundle_BeginEvent(d3d12_command_list_iface *iface,
        UINT metadata, const void *data, UINT size)
{
    struct d3d12_bundle *bundle = impl_from_ID3D12GraphicsCommandList(iface);
    struct d3d12_debug_marker_command *args;

    TRACE("iface %p, metadata %u, data %p, size %u.\n", iface, metadata, data, size);

    args = d3d12_bundle_add_command(bundle, &d3d12_bundle_exec_begin_event, sizeof(*args) + size);
    args->metadata = metadata;
    args->data_size = size;
    memcpy(args->data, data, size);
}

static void d3d12_bundle_exec_end_event(d3d12_command_list_iface *list, const void *args_v)
{
    ID3D12GraphicsCommandList9_EndEvent(list);
}

static void STDMETHODCALLTYPE d3d12_bundle_EndEvent(d3d12_command_list_iface *iface)
{
    struct d3d12_bundle *bundle = impl_from_ID3D12GraphicsCommandList(iface);

    TRACE("iface %p.\n", iface);

    d3d12_bundle_add_command(bundle, &d3d12_bundle_exec_end_event, sizeof(struct d3d12_bundle_command));
}

struct d3d12_execute_indirect_command
{
    struct d3d12_bundle_command command;
    ID3D12CommandSignature *signature;
    UINT max_count;
    ID3D12Resource *arg_buffer;
    UINT64 arg_offset;
    ID3D12Resource *count_buffer;
    UINT64 count_offset;
};

static void d3d12_bundle_exec_execute_indirect(d3d12_command_list_iface *list, const void *args_v)
{
    const struct d3d12_execute_indirect_command *args = args_v;

    ID3D12GraphicsCommandList9_ExecuteIndirect(list, args->signature, args->max_count,
            args->arg_buffer, args->arg_offset, args->count_buffer, args->count_offset);
}

static void STDMETHODCALLTYPE d3d12_bundle_ExecuteIndirect(d3d12_command_list_iface *iface,
        ID3D12CommandSignature *command_signature, UINT max_command_count, ID3D12Resource *arg_buffer,
        UINT64 arg_buffer_offset, ID3D12Resource *count_buffer, UINT64 count_buffer_offset)
{
    struct d3d12_bundle *bundle = impl_from_ID3D12GraphicsCommandList(iface);
    struct d3d12_execute_indirect_command *args;

    TRACE("iface %p, command_signature %p, max_command_count %u, arg_buffer %p, "
            "arg_buffer_offset %#"PRIx64", count_buffer %p, count_buffer_offset %#"PRIx64".\n",
            iface, command_signature, max_command_count, arg_buffer, arg_buffer_offset,
            count_buffer, count_buffer_offset);

    args = d3d12_bundle_add_command(bundle, &d3d12_bundle_exec_execute_indirect, sizeof(*args));
    args->signature = command_signature;
    args->max_count = max_command_count;
    args->arg_buffer = arg_buffer;
    args->arg_offset = arg_buffer_offset;
    args->count_buffer = count_buffer;
    args->count_offset = count_buffer_offset;
}

static void STDMETHODCALLTYPE d3d12_bundle_AtomicCopyBufferUINT(d3d12_command_list_iface *iface,
        ID3D12Resource *dst_buffer, UINT64 dst_offset,
        ID3D12Resource *src_buffer, UINT64 src_offset,
        UINT dependent_resource_count, ID3D12Resource * const *dependent_resources,
        const D3D12_SUBRESOURCE_RANGE_UINT64 *dependent_sub_resource_ranges)
{
    WARN("iface %p, dst_resource %p, dst_offset %#"PRIx64", src_resource %p, "
            "src_offset %#"PRIx64", dependent_resource_count %u, "
            "dependent_resources %p, dependent_sub_resource_ranges %p ignored!\n",
            iface, dst_buffer, dst_offset, src_buffer, src_offset,
            dependent_resource_count, dependent_resources, dependent_sub_resource_ranges);
}

static void STDMETHODCALLTYPE d3d12_bundle_AtomicCopyBufferUINT64(d3d12_command_list_iface *iface,
        ID3D12Resource *dst_buffer, UINT64 dst_offset,
        ID3D12Resource *src_buffer, UINT64 src_offset,
        UINT dependent_resource_count, ID3D12Resource * const *dependent_resources,
        const D3D12_SUBRESOURCE_RANGE_UINT64 *dependent_sub_resource_ranges)
{
    WARN("iface %p, dst_resource %p, dst_offset %#"PRIx64", src_resource %p, "
            "src_offset %#"PRIx64", dependent_resource_count %u, "
            "dependent_resources %p, dependent_sub_resource_ranges %p ignored!\n",
            iface, dst_buffer, dst_offset, src_buffer, src_offset,
            dependent_resource_count, dependent_resources, dependent_sub_resource_ranges);
}

struct d3d12_om_set_depth_bounds_command
{
    struct d3d12_bundle_command command;
    FLOAT min;
    FLOAT max;
};

static void d3d12_bundle_exec_om_set_depth_bounds(d3d12_command_list_iface *list, const void *args_v)
{
    const struct d3d12_om_set_depth_bounds_command *args = args_v;

    ID3D12GraphicsCommandList9_OMSetDepthBounds(list, args->min, args->max);
}

static void STDMETHODCALLTYPE d3d12_bundle_OMSetDepthBounds(d3d12_command_list_iface *iface,
        FLOAT min, FLOAT max)
{
    struct d3d12_bundle *bundle = impl_from_ID3D12GraphicsCommandList(iface);
    struct d3d12_om_set_depth_bounds_command *args;

    TRACE("iface %p, min %.8e, max %.8e.\n", iface, min, max);

    args = d3d12_bundle_add_command(bundle, &d3d12_bundle_exec_om_set_depth_bounds, sizeof(*args));
    args->min = min;
    args->max = max;
}

struct d3d12_set_sample_positions_command
{
    struct d3d12_bundle_command command;
    UINT sample_count;
    UINT pixel_count;
    D3D12_SAMPLE_POSITION positions[];
};

static void d3d12_bundle_exec_set_sample_positions(d3d12_command_list_iface *list, const void *args_v)
{
    const struct d3d12_set_sample_positions_command *args = args_v;

    /* The sample position array is non-const but does not get written to */
    ID3D12GraphicsCommandList9_SetSamplePositions(list, args->sample_count,
            args->pixel_count, (D3D12_SAMPLE_POSITION*)args->positions);
}

static void STDMETHODCALLTYPE d3d12_bundle_SetSamplePositions(d3d12_command_list_iface *iface,
        UINT sample_count, UINT pixel_count, D3D12_SAMPLE_POSITION *sample_positions)
{
    struct d3d12_bundle *bundle = impl_from_ID3D12GraphicsCommandList(iface);
    struct d3d12_set_sample_positions_command *args;
    size_t array_size = sample_count * pixel_count;

    TRACE("iface %p, sample_count %u, pixel_count %u, sample_positions %p.\n",
            iface, sample_count, pixel_count, sample_positions);

    args = d3d12_bundle_add_command(bundle, &d3d12_bundle_exec_set_sample_positions,
            sizeof(*args) + sizeof(*sample_positions) * array_size);
    args->sample_count = sample_count;
    args->pixel_count = pixel_count;
    memcpy(args->positions, sample_positions, sizeof(*sample_positions) * array_size);
}

static void STDMETHODCALLTYPE d3d12_bundle_ResolveSubresourceRegion(d3d12_command_list_iface *iface,
        ID3D12Resource *dst_resource, UINT dst_sub_resource_idx, UINT dst_x, UINT dst_y,
        ID3D12Resource *src_resource, UINT src_sub_resource_idx,
        D3D12_RECT *src_rect, DXGI_FORMAT format, D3D12_RESOLVE_MODE mode)
{
    WARN("iface %p, dst_resource %p, dst_sub_resource_idx %u, "
            "dst_x %u, dst_y %u, src_resource %p, src_sub_resource_idx %u, "
            "src_rect %p, format %#x, mode %#x ignored!\n",
            iface, dst_resource, dst_sub_resource_idx, dst_x, dst_y,
            src_resource, src_sub_resource_idx, src_rect, format, mode);
}

struct d3d12_set_view_instance_mask_command
{
    struct d3d12_bundle_command command;
    UINT mask;
};

static void d3d12_bundle_exec_set_view_instance_mask(d3d12_command_list_iface *list, const void *args_v)
{
    const struct d3d12_set_view_instance_mask_command *args = args_v;

    ID3D12GraphicsCommandList9_SetViewInstanceMask(list, args->mask);
}

static void STDMETHODCALLTYPE d3d12_bundle_SetViewInstanceMask(d3d12_command_list_iface *iface, UINT mask)
{
    struct d3d12_bundle *bundle = impl_from_ID3D12GraphicsCommandList(iface);
    struct d3d12_set_view_instance_mask_command *args;

    TRACE("iface %p, mask %#x.\n", iface, mask);

    args = d3d12_bundle_add_command(bundle, &d3d12_bundle_exec_set_view_instance_mask, sizeof(*args));
    args->mask = mask;
}

struct d3d12_write_buffer_immediate_command
{
    struct d3d12_bundle_command command;
    UINT count;
    D3D12_WRITEBUFFERIMMEDIATE_PARAMETER *parameters;
    D3D12_WRITEBUFFERIMMEDIATE_MODE *modes;
};

static void d3d12_bundle_exec_write_buffer_immediate(d3d12_command_list_iface *list, const void *args_v)
{
    const struct d3d12_write_buffer_immediate_command *args = args_v;

    ID3D12GraphicsCommandList9_WriteBufferImmediate(list, args->count, args->parameters, args->modes);
}

static void STDMETHODCALLTYPE d3d12_bundle_WriteBufferImmediate(d3d12_command_list_iface *iface,
        UINT count, const D3D12_WRITEBUFFERIMMEDIATE_PARAMETER *parameters,
        const D3D12_WRITEBUFFERIMMEDIATE_MODE *modes)
{
    struct d3d12_bundle *bundle = impl_from_ID3D12GraphicsCommandList(iface);
    struct d3d12_write_buffer_immediate_command *args;

    TRACE("iface %p, count %u, parameters %p, modes %p.\n", iface, count, parameters, modes);

    if (!count)
        return;

    args = d3d12_bundle_add_command(bundle, &d3d12_bundle_exec_write_buffer_immediate, sizeof(*args));
    args->count = count;
    args->parameters = d3d12_bundle_allocator_alloc_chunk_data(bundle->allocator, sizeof(*parameters) * count);
    memcpy(args->parameters, parameters, sizeof(*parameters) * count);

    if (modes)
    {
        args->modes = d3d12_bundle_allocator_alloc_chunk_data(bundle->allocator, sizeof(*modes) * count);
        memcpy(args->modes, modes, sizeof(*modes) * count);
    }
    else
        args->modes = NULL;
}

static void STDMETHODCALLTYPE d3d12_bundle_SetProtectedResourceSession(d3d12_command_list_iface *iface,
        ID3D12ProtectedResourceSession *protected_session)
{
    WARN("iface %p, protected_session %p ignored!\n", iface, protected_session);
}

static void STDMETHODCALLTYPE d3d12_bundle_BeginRenderPass(d3d12_command_list_iface *iface,
        UINT rt_count, const D3D12_RENDER_PASS_RENDER_TARGET_DESC *render_targets,
        const D3D12_RENDER_PASS_DEPTH_STENCIL_DESC *depth_stencil, D3D12_RENDER_PASS_FLAGS flags)
{
    WARN("iface %p, rt_count %u, render_targets %p, depth_stencil %p, flags %#x ignored!\n",
            iface, rt_count, render_targets, depth_stencil, flags);
}

static void STDMETHODCALLTYPE d3d12_bundle_EndRenderPass(d3d12_command_list_iface *iface)
{
    WARN("iface %p stub!\n", iface);
}

static void STDMETHODCALLTYPE d3d12_bundle_InitializeMetaCommand(d3d12_command_list_iface *iface,
        ID3D12MetaCommand *meta_command, const void *parameter_data, SIZE_T parameter_size)
{
    WARN("iface %p, meta_command %p, parameter_data %p, parameter_size %lu ignored!\n",
            iface, meta_command, parameter_data, parameter_size);
}

static void STDMETHODCALLTYPE d3d12_bundle_ExecuteMetaCommand(d3d12_command_list_iface *iface,
        ID3D12MetaCommand *meta_command, const void *parameter_data, SIZE_T parameter_size)
{
    WARN("iface %p, meta_command %p, parameter_data %p, parameter_size %lu ignored!\n",
            iface, meta_command, parameter_data, parameter_size);
}

static void STDMETHODCALLTYPE d3d12_bundle_BuildRaytracingAccelerationStructure(d3d12_command_list_iface *iface,
        const D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC *desc, UINT num_postbuild_info_descs,
        const D3D12_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO_DESC *postbuild_info_descs)
{
    WARN("iface %p, desc %p, num_postbuild_info_descs %u, postbuild_info_descs %p ignored!\n",
            iface, desc, num_postbuild_info_descs, postbuild_info_descs);
}

static void STDMETHODCALLTYPE d3d12_bundle_EmitRaytracingAccelerationStructurePostbuildInfo(d3d12_command_list_iface *iface,
        const D3D12_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO_DESC *desc, UINT num_acceleration_structures,
        const D3D12_GPU_VIRTUAL_ADDRESS *src_data)
{
    WARN("iface %p, desc %p, num_acceleration_structures %u, src_data %p ignored!\n",
            iface, desc, num_acceleration_structures, src_data);
}

static void STDMETHODCALLTYPE d3d12_bundle_CopyRaytracingAccelerationStructure(d3d12_command_list_iface *iface,
        D3D12_GPU_VIRTUAL_ADDRESS dst_data, D3D12_GPU_VIRTUAL_ADDRESS src_data,
        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_COPY_MODE mode)
{
    WARN("iface %p, dst_data %#"PRIx64", src_data %#"PRIx64", mode %u ignored!\n",
          iface, dst_data, src_data, mode);
}

struct d3d12_set_pipeline_state1_command
{
    struct d3d12_bundle_command command;
    ID3D12StateObject *state_object;
};

static void d3d12_bundle_exec_set_pipeline_state1(d3d12_command_list_iface *list, const void *args_v)
{
    const struct d3d12_set_pipeline_state1_command *args = args_v;

    ID3D12GraphicsCommandList9_SetPipelineState1(list, args->state_object);
}

static void STDMETHODCALLTYPE d3d12_bundle_SetPipelineState1(d3d12_command_list_iface *iface,
        ID3D12StateObject *state_object)
{
    struct d3d12_bundle *bundle = impl_from_ID3D12GraphicsCommandList(iface);
    struct d3d12_set_pipeline_state1_command *args;

    TRACE("iface %p, state_object %p.\n", iface, state_object);

    args = d3d12_bundle_add_command(bundle, &d3d12_bundle_exec_set_pipeline_state1, sizeof(*args));
    args->state_object = state_object;
}

struct d3d12_dispatch_rays_command
{
    struct d3d12_bundle_command command;
    D3D12_DISPATCH_RAYS_DESC desc;
};

static void d3d12_bundle_exec_dispatch_rays(d3d12_command_list_iface *list, const void *args_v)
{
    const struct d3d12_dispatch_rays_command *args = args_v;

    ID3D12GraphicsCommandList9_DispatchRays(list, &args->desc);
}

static void STDMETHODCALLTYPE d3d12_bundle_DispatchRays(d3d12_command_list_iface *iface,
        const D3D12_DISPATCH_RAYS_DESC *desc)
{
    struct d3d12_bundle *bundle = impl_from_ID3D12GraphicsCommandList(iface);
    struct d3d12_dispatch_rays_command *args;

    TRACE("iface %p, desc %p\n", iface, desc);

    args = d3d12_bundle_add_command(bundle, &d3d12_bundle_exec_dispatch_rays, sizeof(*args));
    args->desc = *desc;
}

struct d3d12_rs_set_shading_rate_command
{
    struct d3d12_bundle_command command;
    D3D12_SHADING_RATE base;
    D3D12_SHADING_RATE_COMBINER combiners[D3D12_RS_SET_SHADING_RATE_COMBINER_COUNT];
};

static void d3d12_bundle_exec_rs_set_shading_rate(d3d12_command_list_iface *list, const void *args_v)
{
    const struct d3d12_rs_set_shading_rate_command *args = args_v;

    ID3D12GraphicsCommandList9_RSSetShadingRate(list, args->base, args->combiners);
}

static void d3d12_bundle_exec_rs_set_shading_rate_base(d3d12_command_list_iface *list, const void *args_v)
{
    const struct d3d12_rs_set_shading_rate_command *args = args_v;

    ID3D12GraphicsCommandList9_RSSetShadingRate(list, args->base, NULL);
}

static void STDMETHODCALLTYPE d3d12_bundle_RSSetShadingRate(d3d12_command_list_iface *iface,
        D3D12_SHADING_RATE base, const D3D12_SHADING_RATE_COMBINER *combiners)
{
    struct d3d12_bundle *bundle = impl_from_ID3D12GraphicsCommandList(iface);
    struct d3d12_rs_set_shading_rate_command *args;

    TRACE("iface %p, base %#x, combiners %p.\n", iface, base, combiners);

    args = d3d12_bundle_add_command(bundle, combiners
            ? &d3d12_bundle_exec_rs_set_shading_rate
            : &d3d12_bundle_exec_rs_set_shading_rate_base, sizeof(*args));
    args->base = base;

    if (combiners)
        memcpy(args->combiners, combiners, sizeof(args->combiners));
}

struct d3d12_rs_set_shading_rate_image_command
{
    struct d3d12_bundle_command command;
    ID3D12Resource *image;
};

static void d3d12_bundle_exec_rs_set_shading_rate_image(d3d12_command_list_iface *list, const void *args_v)
{
    const struct d3d12_rs_set_shading_rate_image_command *args = args_v;

    ID3D12GraphicsCommandList9_RSSetShadingRateImage(list, args->image);
}

static void STDMETHODCALLTYPE d3d12_bundle_RSSetShadingRateImage(d3d12_command_list_iface *iface,
        ID3D12Resource *image)
{
    struct d3d12_bundle *bundle = impl_from_ID3D12GraphicsCommandList(iface);
    struct d3d12_rs_set_shading_rate_image_command *args;

    TRACE("iface %p, image %p.\n", iface, image);

    args = d3d12_bundle_add_command(bundle, &d3d12_bundle_exec_rs_set_shading_rate_image, sizeof(*args));
    args->image = image;
}

static void d3d12_bundle_exec_dispatch_mesh(d3d12_command_list_iface *list, const void *args_v)
{
    const struct d3d12_dispatch_command *args = args_v;

    ID3D12GraphicsCommandList9_DispatchMesh(list, args->x, args->y, args->z);
}

static void STDMETHODCALLTYPE d3d12_bundle_DispatchMesh(d3d12_command_list_iface *iface, UINT x, UINT y, UINT z)
{
    struct d3d12_bundle *bundle = impl_from_ID3D12GraphicsCommandList(iface);
    struct d3d12_dispatch_command *args;

    TRACE("iface %p, x %u, y %u, z %u.\n", iface, x, y, z);

    args = d3d12_bundle_add_command(bundle, &d3d12_bundle_exec_dispatch_mesh, sizeof(*args));
    args->x = x;
    args->y = y;
    args->z = z;
}

static void STDMETHODCALLTYPE d3d12_bundle_Barrier(d3d12_command_list_iface *iface, UINT32 NumBarrierGroups, const void *pBarrierGroups)
{
    WARN("iface %p, NumBarrierGroups %u, D3D12_BARRIER_GROUP %p ignored!\n", iface, NumBarrierGroups, pBarrierGroups);
}

static void STDMETHODCALLTYPE d3d12_bundle_OMSetFrontAndBackStencilRef(d3d12_command_list_iface *iface, UINT FrontStencilRef, UINT BackStencilRef)
{
    WARN("iface %p, FrontStencilRef %u, BackStencilRef %u ignored!\n", iface, FrontStencilRef, BackStencilRef);
}

static void STDMETHODCALLTYPE d3d12_bundle_RSSetDepthBias(d3d12_command_list_iface *iface, FLOAT DepthBias, FLOAT DepthBiasClamp, FLOAT SlopeScaledDepthBias)
{
    WARN("iface %p, DepthBias %f, DepthBiasClamp %f, SlopeScaledDepthBias %f ignored!\n", iface, DepthBias, DepthBiasClamp, SlopeScaledDepthBias);
}

static void STDMETHODCALLTYPE d3d12_bundle_IASetIndexBufferStripCutValue(d3d12_command_list_iface *iface, D3D12_INDEX_BUFFER_STRIP_CUT_VALUE IBStripCutValue)
{
    WARN("iface %p, IBStripCutValue %u ignored!\n", iface, IBStripCutValue);
}

static CONST_VTBL struct ID3D12GraphicsCommandList9Vtbl d3d12_bundle_vtbl =
{
    /* IUnknown methods */
    d3d12_bundle_QueryInterface,
    d3d12_bundle_AddRef,
    d3d12_bundle_Release,
    /* ID3D12Object methods */
    d3d12_bundle_GetPrivateData,
    d3d12_bundle_SetPrivateData,
    d3d12_bundle_SetPrivateDataInterface,
    (void *)d3d12_object_SetName,
    /* ID3D12DeviceChild methods */
    d3d12_bundle_GetDevice,
    /* ID3D12CommandList methods */
    d3d12_bundle_GetType,
    /* ID3D12GraphicsCommandList methods */
    d3d12_bundle_Close,
    d3d12_bundle_Reset,
    d3d12_bundle_ClearState,
    d3d12_bundle_DrawInstanced,
    d3d12_bundle_DrawIndexedInstanced,
    d3d12_bundle_Dispatch,
    d3d12_bundle_CopyBufferRegion,
    d3d12_bundle_CopyTextureRegion,
    d3d12_bundle_CopyResource,
    d3d12_bundle_CopyTiles,
    d3d12_bundle_ResolveSubresource,
    d3d12_bundle_IASetPrimitiveTopology,
    d3d12_bundle_RSSetViewports,
    d3d12_bundle_RSSetScissorRects,
    d3d12_bundle_OMSetBlendFactor,
    d3d12_bundle_OMSetStencilRef,
    d3d12_bundle_SetPipelineState,
    d3d12_bundle_ResourceBarrier,
    d3d12_bundle_ExecuteBundle,
    d3d12_bundle_SetDescriptorHeaps,
    d3d12_bundle_SetComputeRootSignature,
    d3d12_bundle_SetGraphicsRootSignature,
    d3d12_bundle_SetComputeRootDescriptorTable,
    d3d12_bundle_SetGraphicsRootDescriptorTable,
    d3d12_bundle_SetComputeRoot32BitConstant,
    d3d12_bundle_SetGraphicsRoot32BitConstant,
    d3d12_bundle_SetComputeRoot32BitConstants,
    d3d12_bundle_SetGraphicsRoot32BitConstants,
    d3d12_bundle_SetComputeRootConstantBufferView,
    d3d12_bundle_SetGraphicsRootConstantBufferView,
    d3d12_bundle_SetComputeRootShaderResourceView,
    d3d12_bundle_SetGraphicsRootShaderResourceView,
    d3d12_bundle_SetComputeRootUnorderedAccessView,
    d3d12_bundle_SetGraphicsRootUnorderedAccessView,
    d3d12_bundle_IASetIndexBuffer,
    d3d12_bundle_IASetVertexBuffers,
    d3d12_bundle_SOSetTargets,
    d3d12_bundle_OMSetRenderTargets,
    d3d12_bundle_ClearDepthStencilView,
    d3d12_bundle_ClearRenderTargetView,
    d3d12_bundle_ClearUnorderedAccessViewUint,
    d3d12_bundle_ClearUnorderedAccessViewFloat,
    d3d12_bundle_DiscardResource,
    d3d12_bundle_BeginQuery,
    d3d12_bundle_EndQuery,
    d3d12_bundle_ResolveQueryData,
    d3d12_bundle_SetPredication,
    d3d12_bundle_SetMarker,
    d3d12_bundle_BeginEvent,
    d3d12_bundle_EndEvent,
    d3d12_bundle_ExecuteIndirect,
    /* ID3D12GraphicsCommandList1 methods */
    d3d12_bundle_AtomicCopyBufferUINT,
    d3d12_bundle_AtomicCopyBufferUINT64,
    d3d12_bundle_OMSetDepthBounds,
    d3d12_bundle_SetSamplePositions,
    d3d12_bundle_ResolveSubresourceRegion,
    d3d12_bundle_SetViewInstanceMask,
    /* ID3D12GraphicsCommandList2 methods */
    d3d12_bundle_WriteBufferImmediate,
    /* ID3D12GraphicsCommandList3 methods */
    d3d12_bundle_SetProtectedResourceSession,
    /* ID3D12GraphicsCommandList4 methods */
    d3d12_bundle_BeginRenderPass,
    d3d12_bundle_EndRenderPass,
    d3d12_bundle_InitializeMetaCommand,
    d3d12_bundle_ExecuteMetaCommand,
    d3d12_bundle_BuildRaytracingAccelerationStructure,
    d3d12_bundle_EmitRaytracingAccelerationStructurePostbuildInfo,
    d3d12_bundle_CopyRaytracingAccelerationStructure,
    d3d12_bundle_SetPipelineState1,
    d3d12_bundle_DispatchRays,
    /* ID3D12GraphicsCommandList5 methods */
    d3d12_bundle_RSSetShadingRate,
    d3d12_bundle_RSSetShadingRateImage,
    /* ID3D12GraphicsCommandList6 methods */
    d3d12_bundle_DispatchMesh,
    /* ID3D12GraphicsCommandList7 methods */
    d3d12_bundle_Barrier,
    /* ID3D12GraphicsCommandList8 methods */
    d3d12_bundle_OMSetFrontAndBackStencilRef,
    /* ID3D12GraphicsCommandList9 methods */
    d3d12_bundle_RSSetDepthBias,
    d3d12_bundle_IASetIndexBufferStripCutValue,
};

HRESULT d3d12_bundle_create(struct d3d12_device *device,
        UINT node_mask, D3D12_COMMAND_LIST_TYPE type, struct d3d12_bundle **bundle)
{
    struct d3d12_bundle *object;
    HRESULT hr;

    if (!(object = vkd3d_calloc(1, sizeof(*object))))
        return E_OUTOFMEMORY;

    object->ID3D12GraphicsCommandList_iface.lpVtbl = &d3d12_bundle_vtbl;
    object->refcount = 1;
    object->device = device;

    if (FAILED(hr = vkd3d_private_store_init(&object->private_store)))
    {
        vkd3d_free(object);
        return hr;
    }

    d3d12_device_add_ref(device);
    *bundle = object;
    return S_OK;
}

void d3d12_bundle_execute(struct d3d12_bundle *bundle, d3d12_command_list_iface *list)
{
    struct d3d12_bundle_command *command = bundle->head;

    while (command)
    {
        command->proc(list, command);
        command = command->next;
    }
}

struct d3d12_bundle *d3d12_bundle_from_iface(ID3D12GraphicsCommandList *iface)
{
    if (!iface || iface->lpVtbl != (struct ID3D12GraphicsCommandListVtbl *)&d3d12_bundle_vtbl)
        return NULL;

    return impl_from_ID3D12GraphicsCommandList((d3d12_command_list_iface *)iface);
}
