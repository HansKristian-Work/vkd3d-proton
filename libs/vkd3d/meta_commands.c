/*
 * Copyright 2023 Philip Rebohle for Valve Corporation
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

/* Determined by calling EnumerateMetaCommands on Windows drivers */
const GUID IID_META_COMMAND_DSTORAGE = {0x1bddd090,0xc47e,0x459c,{0x8f,0x81,0x42,0xc9,0xf9,0x7a,0x53,0x08}};

struct d3d12_meta_command_dstorage_create_args
{
    UINT64 version;
    UINT64 format;
    UINT64 max_streams;
    UINT64 flags;
};

struct d3d12_meta_command_dstorage_exec_args
{
    D3D12_GPU_VIRTUAL_ADDRESS input_buffer_va;
    UINT64 input_buffer_size;
    D3D12_GPU_VIRTUAL_ADDRESS output_buffer_va;
    UINT64 output_buffer_size;
    D3D12_GPU_VIRTUAL_ADDRESS control_buffer_va;
    UINT64 control_buffer_size;
    D3D12_GPU_VIRTUAL_ADDRESS scratch_buffer_va;
    UINT64 scratch_buffer_size;
    UINT64 stream_count;
    D3D12_GPU_VIRTUAL_ADDRESS status_buffer_va;
    UINT64 status_buffer_size;
};

struct d3d12_meta_command_parameter_info
{
    D3D12_META_COMMAND_PARAMETER_STAGE stage;
    D3D12_META_COMMAND_PARAMETER_DESC desc;
};

struct d3d12_meta_command_info
{
    REFGUID command_id;
    LPCWSTR name;
    D3D12_GRAPHICS_STATES init_dirty_states;
    D3D12_GRAPHICS_STATES exec_dirty_states;
    unsigned int parameter_count;
    const struct d3d12_meta_command_parameter_info *parameters;
    d3d12_meta_command_create_proc create_proc;
};

/* Determined by calling EnumerateMetaCommandParameters on Windows drivers */
static const struct d3d12_meta_command_parameter_info d3d12_meta_command_dstorage_parameter_infos[] =
{
    { D3D12_META_COMMAND_PARAMETER_STAGE_CREATION,
        { u"Version", D3D12_META_COMMAND_PARAMETER_TYPE_UINT64,
                D3D12_META_COMMAND_PARAMETER_FLAG_INPUT, 0,
                offsetof(struct d3d12_meta_command_dstorage_create_args, version) } },
    { D3D12_META_COMMAND_PARAMETER_STAGE_CREATION,
        { u"Format", D3D12_META_COMMAND_PARAMETER_TYPE_UINT64,
                D3D12_META_COMMAND_PARAMETER_FLAG_INPUT, 0,
                offsetof(struct d3d12_meta_command_dstorage_create_args, format) } },
    { D3D12_META_COMMAND_PARAMETER_STAGE_CREATION,
        { u"MaxStreams", D3D12_META_COMMAND_PARAMETER_TYPE_UINT64,
                D3D12_META_COMMAND_PARAMETER_FLAG_INPUT, 0,
                offsetof(struct d3d12_meta_command_dstorage_create_args, max_streams) } },
    { D3D12_META_COMMAND_PARAMETER_STAGE_CREATION,
        { u"Flags", D3D12_META_COMMAND_PARAMETER_TYPE_UINT64,
                D3D12_META_COMMAND_PARAMETER_FLAG_INPUT, 0,
                offsetof(struct d3d12_meta_command_dstorage_create_args, flags) } },

    { D3D12_META_COMMAND_PARAMETER_STAGE_EXECUTION,
        { u"InputBuffer", D3D12_META_COMMAND_PARAMETER_TYPE_GPU_VIRTUAL_ADDRESS,
                D3D12_META_COMMAND_PARAMETER_FLAG_INPUT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                offsetof(struct d3d12_meta_command_dstorage_exec_args, input_buffer_va) } },
    { D3D12_META_COMMAND_PARAMETER_STAGE_EXECUTION,
        { u"InputBufferSize", D3D12_META_COMMAND_PARAMETER_TYPE_UINT64,
                D3D12_META_COMMAND_PARAMETER_FLAG_INPUT, 0,
                offsetof(struct d3d12_meta_command_dstorage_exec_args, input_buffer_size) } },
    { D3D12_META_COMMAND_PARAMETER_STAGE_EXECUTION,
        { u"OutputBuffer", D3D12_META_COMMAND_PARAMETER_TYPE_GPU_VIRTUAL_ADDRESS,
                D3D12_META_COMMAND_PARAMETER_FLAG_OUTPUT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                offsetof(struct d3d12_meta_command_dstorage_exec_args, output_buffer_va) } },
    { D3D12_META_COMMAND_PARAMETER_STAGE_EXECUTION,
        { u"OutputBufferSize", D3D12_META_COMMAND_PARAMETER_TYPE_UINT64,
                D3D12_META_COMMAND_PARAMETER_FLAG_INPUT, 0,
                offsetof(struct d3d12_meta_command_dstorage_exec_args, output_buffer_size) } },
    { D3D12_META_COMMAND_PARAMETER_STAGE_EXECUTION,
        { u"ControlBuffer", D3D12_META_COMMAND_PARAMETER_TYPE_GPU_VIRTUAL_ADDRESS,
                D3D12_META_COMMAND_PARAMETER_FLAG_INPUT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                offsetof(struct d3d12_meta_command_dstorage_exec_args, control_buffer_va) } },
    { D3D12_META_COMMAND_PARAMETER_STAGE_EXECUTION,
        { u"ControlBufferSize", D3D12_META_COMMAND_PARAMETER_TYPE_UINT64,
                D3D12_META_COMMAND_PARAMETER_FLAG_INPUT, 0,
                offsetof(struct d3d12_meta_command_dstorage_exec_args, control_buffer_size) } },
    { D3D12_META_COMMAND_PARAMETER_STAGE_EXECUTION,
        { u"ScratchBuffer", D3D12_META_COMMAND_PARAMETER_TYPE_GPU_VIRTUAL_ADDRESS,
                D3D12_META_COMMAND_PARAMETER_FLAG_OUTPUT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                offsetof(struct d3d12_meta_command_dstorage_exec_args, scratch_buffer_va) } },
    { D3D12_META_COMMAND_PARAMETER_STAGE_EXECUTION,
        { u"ScratchBufferSize", D3D12_META_COMMAND_PARAMETER_TYPE_UINT64,
                D3D12_META_COMMAND_PARAMETER_FLAG_INPUT, 0,
                offsetof(struct d3d12_meta_command_dstorage_exec_args, scratch_buffer_size) } },
    { D3D12_META_COMMAND_PARAMETER_STAGE_EXECUTION,
        { u"StreamCount", D3D12_META_COMMAND_PARAMETER_TYPE_UINT64,
                D3D12_META_COMMAND_PARAMETER_FLAG_INPUT, 0,
                offsetof(struct d3d12_meta_command_dstorage_exec_args, stream_count) } },
    { D3D12_META_COMMAND_PARAMETER_STAGE_EXECUTION,
        { u"StatusBuffer", D3D12_META_COMMAND_PARAMETER_TYPE_GPU_VIRTUAL_ADDRESS,
                D3D12_META_COMMAND_PARAMETER_FLAG_OUTPUT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                offsetof(struct d3d12_meta_command_dstorage_exec_args, status_buffer_va) } },
    { D3D12_META_COMMAND_PARAMETER_STAGE_EXECUTION,
        { u"StatusBufferSize", D3D12_META_COMMAND_PARAMETER_TYPE_UINT64,
                D3D12_META_COMMAND_PARAMETER_FLAG_INPUT, 0,
                offsetof(struct d3d12_meta_command_dstorage_exec_args, status_buffer_size) } },
};

const struct d3d12_meta_command_info d3d12_meta_command_infos[] =
{
    { &IID_META_COMMAND_DSTORAGE, u"DirectStorage", 0,
          D3D12_GRAPHICS_STATE_COMPUTE_ROOT_SIGNATURE | D3D12_GRAPHICS_STATE_PIPELINE_STATE,
          ARRAY_SIZE(d3d12_meta_command_dstorage_parameter_infos),
          d3d12_meta_command_dstorage_parameter_infos, NULL },
};

static void d3d12_meta_command_destroy(struct d3d12_meta_command *meta_command)
{
    vkd3d_private_store_destroy(&meta_command->private_store);

    vkd3d_free(meta_command);
}

static HRESULT STDMETHODCALLTYPE d3d12_meta_command_QueryInterface(d3d12_meta_command_iface *iface,
        REFIID riid, void **object)
{
    TRACE("iface %p, riid %s, object %p.\n", iface, debugstr_guid(riid), object);

    if (!object)
        return E_POINTER;

    if (IsEqualGUID(riid, &IID_ID3D12MetaCommand)
            || IsEqualGUID(riid, &IID_ID3D12Pageable)
            || IsEqualGUID(riid, &IID_ID3D12DeviceChild)
            || IsEqualGUID(riid, &IID_ID3D12Object)
            || IsEqualGUID(riid, &IID_IUnknown))
    {
        ID3D12MetaCommand_AddRef(iface);
        *object = iface;
        return S_OK;
    }

    WARN("%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid(riid));

    *object = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE d3d12_meta_command_AddRef(d3d12_meta_command_iface *iface)
{
    struct d3d12_meta_command *meta_command = impl_from_ID3D12MetaCommand(iface);
    ULONG refcount = InterlockedIncrement(&meta_command->refcount);

    TRACE("%p increasing refcount to %u.\n", meta_command, refcount);

    if (refcount == 1)
    {
        struct d3d12_device *device = meta_command->device;

        d3d12_device_add_ref(device);
    }

    return refcount;
}

static ULONG STDMETHODCALLTYPE d3d12_meta_command_Release(d3d12_meta_command_iface *iface)
{
    struct d3d12_meta_command *meta_command = impl_from_ID3D12MetaCommand(iface);
    struct d3d12_device *device = meta_command->device;
    ULONG refcount;

    refcount = InterlockedDecrement(&meta_command->refcount);

    TRACE("%p decreasing refcount to %u.\n", meta_command, refcount);

    if (!refcount)
    {
        d3d12_device_release(device);
        d3d12_meta_command_destroy(meta_command);
    }

    return refcount;
}

static HRESULT STDMETHODCALLTYPE d3d12_meta_command_GetPrivateData(d3d12_meta_command_iface *iface,
        REFGUID guid, UINT *data_size, void *data)
{
    struct d3d12_meta_command *meta_command = impl_from_ID3D12MetaCommand(iface);

    TRACE("iface %p, guid %s, data_size %p, data %p.\n", iface, debugstr_guid(guid), data_size, data);

    return vkd3d_get_private_data(&meta_command->private_store, guid, data_size, data);
}

static HRESULT STDMETHODCALLTYPE d3d12_meta_command_SetPrivateData(d3d12_meta_command_iface *iface,
        REFGUID guid, UINT data_size, const void *data)
{
    struct d3d12_meta_command *meta_command = impl_from_ID3D12MetaCommand(iface);

    TRACE("iface %p, guid %s, data_size %u, data %p.\n", iface, debugstr_guid(guid), data_size, data);

    return vkd3d_set_private_data(&meta_command->private_store, guid, data_size, data, NULL, NULL);
}

static HRESULT STDMETHODCALLTYPE d3d12_meta_command_SetPrivateDataInterface(d3d12_meta_command_iface *iface,
        REFGUID guid, const IUnknown *data)
{
    struct d3d12_meta_command *meta_command = impl_from_ID3D12MetaCommand(iface);

    TRACE("iface %p, guid %s, data %p.\n", iface, debugstr_guid(guid), data);

    return vkd3d_set_private_data_interface(&meta_command->private_store, guid, data, NULL, NULL);
}

static HRESULT STDMETHODCALLTYPE d3d12_meta_command_GetDevice(d3d12_meta_command_iface *iface, REFIID iid, void **device)
{
    struct d3d12_meta_command *meta_command = impl_from_ID3D12MetaCommand(iface);

    TRACE("iface %p, iid %s, device %p.\n", iface, debugstr_guid(iid), device);

    return d3d12_device_query_interface(meta_command->device, iid, device);
}

static UINT64 STDMETHODCALLTYPE d3d12_meta_command_GetRequiredParameterResourceSize(d3d12_meta_command_iface *iface,
        D3D12_META_COMMAND_PARAMETER_STAGE stage, UINT parameter_index)
{
    FIXME("iface %p, stage %u, parameter_index %u, stub!\n", iface, stage, parameter_index);

    return 0;
}

CONST_VTBL struct ID3D12MetaCommandVtbl d3d12_meta_command_vtbl =
{
    /* IUnknown methods */
    d3d12_meta_command_QueryInterface,
    d3d12_meta_command_AddRef,
    d3d12_meta_command_Release,
    /* ID3D12Object methods */
    d3d12_meta_command_GetPrivateData,
    d3d12_meta_command_SetPrivateData,
    d3d12_meta_command_SetPrivateDataInterface,
    (void *)d3d12_object_SetName,
    /* ID3D12DeviceChild methods */
    d3d12_meta_command_GetDevice,
    /* ID3D12MetaCommand methods */
    d3d12_meta_command_GetRequiredParameterResourceSize,
};

struct d3d12_meta_command *impl_from_ID3D12MetaCommand(ID3D12MetaCommand *iface)
{
    if (!iface)
        return NULL;
    assert(iface->lpVtbl == &d3d12_meta_command_vtbl);
    return CONTAINING_RECORD(iface, struct d3d12_meta_command, ID3D12MetaCommand_iface);
}
