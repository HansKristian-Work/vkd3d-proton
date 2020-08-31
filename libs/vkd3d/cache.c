/*
 * Copyright 2020 Philip Rebohle for Valve Corporation
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

static VkResult vkd3d_create_pipeline_cache(struct d3d12_device *device,
        size_t size, const void *data, VkPipelineCache *cache)
{
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;

    VkPipelineCacheCreateInfo info;
    info.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
    info.pNext = NULL;
    info.flags = 0;
    info.initialDataSize = size;
    info.pInitialData = data;

    return VK_CALL(vkCreatePipelineCache(device->vk_device, &info, NULL, cache));
}

#define VKD3D_CACHE_BLOB_VERSION MAKE_MAGIC('V','K','B',1)
#define VKD3D_CACHE_BUILD_SIZE 8

struct vkd3d_pipeline_blob
{
    uint32_t version;
    uint32_t vendor_id;
    uint32_t device_id;
    char vkd3d_build[VKD3D_CACHE_BUILD_SIZE];
    uint8_t cache_uuid[VK_UUID_SIZE];
    uint8_t vk_blob[];
};

HRESULT vkd3d_create_pipeline_cache_from_d3d12_desc(struct d3d12_device *device,
        const D3D12_CACHED_PIPELINE_STATE *state, VkPipelineCache *cache)
{
    const VkPhysicalDeviceProperties *device_properties = &device->device_info.properties2.properties;
    const struct vkd3d_pipeline_blob *blob = state->pCachedBlob;
    VkResult vr;

    if (!state->CachedBlobSizeInBytes)
    {
        vr = vkd3d_create_pipeline_cache(device, 0, NULL, cache);
        return hresult_from_vk_result(vr);
    }

    /* Avoid E_INVALIDARG with an invalid header size, since that may confuse some games */
    if (state->CachedBlobSizeInBytes < sizeof(*blob) || blob->version != VKD3D_CACHE_BLOB_VERSION)
        return D3D12_ERROR_DRIVER_VERSION_MISMATCH;

    /* Indicate that the cached data is not useful if we're running on a different device or driver */
    if (blob->vendor_id != device_properties->vendorID || blob->device_id != device_properties->deviceID)
        return D3D12_ERROR_ADAPTER_NOT_FOUND;

    /* Check the vkd3d build since the shader compiler itself may change,
     * and the driver since that will affect the generated pipeline cache */
    if (strncmp(blob->vkd3d_build, vkd3d_build, VKD3D_CACHE_BUILD_SIZE) ||
            memcmp(blob->cache_uuid, device_properties->pipelineCacheUUID, VK_UUID_SIZE))
        return D3D12_ERROR_DRIVER_VERSION_MISMATCH;

    vr = vkd3d_create_pipeline_cache(device, state->CachedBlobSizeInBytes - sizeof(*blob), blob->vk_blob, cache);
    return hresult_from_vk_result(vr);
}

VkResult vkd3d_serialize_pipeline_state(const struct d3d12_pipeline_state *state, size_t *size, void *data)
{
    const VkPhysicalDeviceProperties *device_properties = &state->device->device_info.properties2.properties;
    const struct vkd3d_vk_device_procs *vk_procs = &state->device->vk_procs;
    struct vkd3d_pipeline_blob *blob = data;
    size_t total_size = sizeof(*blob);
    size_t vk_blob_size = 0;
    VkResult vr;

    if (state->vk_pso_cache)
    {
        if ((vr = VK_CALL(vkGetPipelineCacheData(state->device->vk_device, state->vk_pso_cache, &vk_blob_size, NULL))))
        {
            ERR("Failed to retrieve pipeline cache size, vr %d.\n", vr);
            return vr;
        }
    }

    total_size += vk_blob_size;

    if (blob && *size < total_size)
        return VK_INCOMPLETE;

    if (blob)
    {
        blob->version = VKD3D_CACHE_BLOB_VERSION;
        blob->vendor_id = device_properties->vendorID;
        blob->device_id = device_properties->deviceID;
        strncpy(blob->vkd3d_build, vkd3d_build, VKD3D_CACHE_BUILD_SIZE);
        memcpy(blob->cache_uuid, device_properties->pipelineCacheUUID, VK_UUID_SIZE);

        if ((vr = VK_CALL(vkGetPipelineCacheData(state->device->vk_device, state->vk_pso_cache, &vk_blob_size, blob->vk_blob))))
            return vr;
    }

    *size = total_size;
    return VK_SUCCESS;
}

/* ID3D12PipelineLibrary */
static inline struct d3d12_pipeline_library *impl_from_ID3D12PipelineLibrary(d3d12_pipeline_library_iface *iface)
{
    return CONTAINING_RECORD(iface, struct d3d12_pipeline_library, ID3D12PipelineLibrary_iface);
}

static HRESULT STDMETHODCALLTYPE d3d12_pipeline_library_QueryInterface(d3d12_pipeline_library_iface *iface,
        REFIID riid, void **object)
{
    TRACE("iface %p, riid %s, object %p.\n", iface, debugstr_guid(riid), object);

    if (IsEqualGUID(riid, &IID_ID3D12PipelineLibrary)
            || IsEqualGUID(riid, &IID_ID3D12PipelineLibrary1)
            || IsEqualGUID(riid, &IID_ID3D12DeviceChild)
            || IsEqualGUID(riid, &IID_ID3D12Object)
            || IsEqualGUID(riid, &IID_IUnknown))
    {
        ID3D12PipelineLibrary_AddRef(iface);
        *object = iface;
        return S_OK;
    }

    WARN("%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid(riid));

    *object = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE d3d12_pipeline_library_AddRef(d3d12_pipeline_library_iface *iface)
{
    struct d3d12_pipeline_library *pipeline_library = impl_from_ID3D12PipelineLibrary(iface);
    ULONG refcount = InterlockedIncrement(&pipeline_library->refcount);

    TRACE("%p increasing refcount to %u.\n", pipeline_library, refcount);

    return refcount;
}

static ULONG STDMETHODCALLTYPE d3d12_pipeline_library_Release(d3d12_pipeline_library_iface *iface)
{
    struct d3d12_pipeline_library *pipeline_library = impl_from_ID3D12PipelineLibrary(iface);
    ULONG refcount = InterlockedDecrement(&pipeline_library->refcount);

    TRACE("%p decreasing refcount to %u.\n", pipeline_library, refcount);

    if (!refcount)
    {
        struct d3d12_device *device = pipeline_library->device;
        vkd3d_private_store_destroy(&pipeline_library->private_store);
        vkd3d_free(pipeline_library);
        d3d12_device_release(device);
    }

    return refcount;
}

static HRESULT STDMETHODCALLTYPE d3d12_pipeline_library_GetPrivateData(d3d12_pipeline_library_iface *iface,
        REFGUID guid, UINT *data_size, void *data)
{
    struct d3d12_pipeline_library *pipeline_library = impl_from_ID3D12PipelineLibrary(iface);

    TRACE("iface %p, guid %s, data_size %p, data %p.\n", iface, debugstr_guid(guid), data_size, data);

    return vkd3d_get_private_data(&pipeline_library->private_store, guid, data_size, data);
}

static HRESULT STDMETHODCALLTYPE d3d12_pipeline_library_SetPrivateData(d3d12_pipeline_library_iface *iface,
        REFGUID guid, UINT data_size, const void *data)
{
    struct d3d12_pipeline_library *pipeline_library = impl_from_ID3D12PipelineLibrary(iface);

    TRACE("iface %p, guid %s, data_size %u, data %p.\n", iface, debugstr_guid(guid), data_size, data);

    return vkd3d_set_private_data(&pipeline_library->private_store, guid, data_size, data);
}

static HRESULT STDMETHODCALLTYPE d3d12_pipeline_library_SetPrivateDataInterface(d3d12_pipeline_library_iface *iface,
        REFGUID guid, const IUnknown *data)
{
    struct d3d12_pipeline_library *pipeline_library = impl_from_ID3D12PipelineLibrary(iface);

    TRACE("iface %p, guid %s, data %p.\n", iface, debugstr_guid(guid), data);

    return vkd3d_set_private_data_interface(&pipeline_library->private_store, guid, data);
}

static HRESULT STDMETHODCALLTYPE d3d12_pipeline_library_SetName(d3d12_pipeline_library_iface *iface, const WCHAR *name)
{
    struct d3d12_pipeline_library *pipeline_library = impl_from_ID3D12PipelineLibrary(iface);

    TRACE("iface %p, name %s.\n", iface, debugstr_w(name, pipeline_library->device->wchar_size));

    return name ? S_OK : E_INVALIDARG;
}

static HRESULT STDMETHODCALLTYPE d3d12_pipeline_library_GetDevice(d3d12_pipeline_library_iface *iface,
        REFIID iid, void **device)
{
    struct d3d12_pipeline_library *pipeline_library = impl_from_ID3D12PipelineLibrary(iface);

    TRACE("iface %p, iid %s, device %p.\n", iface, debugstr_guid(iid), device);

    return d3d12_device_query_interface(pipeline_library->device, iid, device);
}

static HRESULT STDMETHODCALLTYPE d3d12_pipeline_library_StorePipeline(d3d12_pipeline_library_iface *iface,
        LPCWSTR name, ID3D12PipelineState *pipeline)
{
    struct d3d12_pipeline_library *pipeline_library = impl_from_ID3D12PipelineLibrary(iface);

    FIXME("iface %p, name %s, pipeline %p stub!\n", iface, debugstr_w(name, pipeline_library->device->wchar_size), pipeline);

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d3d12_pipeline_library_LoadGraphicsPipeline(d3d12_pipeline_library_iface *iface,
        LPCWSTR name, const D3D12_GRAPHICS_PIPELINE_STATE_DESC *desc, REFIID iid, void **pipeline_state)
{
    struct d3d12_pipeline_library *pipeline_library = impl_from_ID3D12PipelineLibrary(iface);

    FIXME("iface %p, name %s, desc %p, iid %s, pipeline_state %p stub!\n", iface,
            debugstr_w(name, pipeline_library->device->wchar_size),
            desc, debugstr_guid(iid), pipeline_state);

    return E_INVALIDARG;
}

static HRESULT STDMETHODCALLTYPE d3d12_pipeline_library_LoadComputePipeline(d3d12_pipeline_library_iface *iface,
        LPCWSTR name, const D3D12_COMPUTE_PIPELINE_STATE_DESC *desc, REFIID iid, void **pipeline_state)
{
    struct d3d12_pipeline_library *pipeline_library = impl_from_ID3D12PipelineLibrary(iface);

    FIXME("iface %p, name %s, desc %p, iid %s, pipeline_state %p stub!\n", iface,
            debugstr_w(name, pipeline_library->device->wchar_size),
            desc, debugstr_guid(iid), pipeline_state);

    return E_INVALIDARG;
}

static SIZE_T STDMETHODCALLTYPE d3d12_pipeline_library_GetSerializedSize(d3d12_pipeline_library_iface *iface)
{
    FIXME("iface %p stub!\n", iface);

    return 0;
}

static HRESULT STDMETHODCALLTYPE d3d12_pipeline_library_Serialize(d3d12_pipeline_library_iface *iface,
        void *data, SIZE_T data_size)
{
    FIXME("iface %p, data %p, data_size %lu stub!\n", iface, data, data_size);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d12_pipeline_library_LoadPipeline(d3d12_pipeline_library_iface *iface,
        LPCWSTR name, const D3D12_PIPELINE_STATE_STREAM_DESC *desc, REFIID iid, void **pipeline_state)
{
    struct d3d12_pipeline_library *pipeline_library = impl_from_ID3D12PipelineLibrary(iface);

    FIXME("iface %p, name %s, desc %p, iid %s, pipeline_state %p stub!\n", iface,
            debugstr_w(name, pipeline_library->device->wchar_size),
            desc, debugstr_guid(iid), pipeline_state);

    return E_INVALIDARG;
}

static CONST_VTBL struct ID3D12PipelineLibrary1Vtbl d3d12_pipeline_library_vtbl =
{
    /* IUnknown methods */
    d3d12_pipeline_library_QueryInterface,
    d3d12_pipeline_library_AddRef,
    d3d12_pipeline_library_Release,
    /* ID3D12Object methods */
    d3d12_pipeline_library_GetPrivateData,
    d3d12_pipeline_library_SetPrivateData,
    d3d12_pipeline_library_SetPrivateDataInterface,
    d3d12_pipeline_library_SetName,
    /* ID3D12DeviceChild methods */
    d3d12_pipeline_library_GetDevice,
    /* ID3D12PipelineLibrary methods */
    d3d12_pipeline_library_StorePipeline,
    d3d12_pipeline_library_LoadGraphicsPipeline,
    d3d12_pipeline_library_LoadComputePipeline,
    d3d12_pipeline_library_GetSerializedSize,
    d3d12_pipeline_library_Serialize,
    /* ID3D12PipelineLibrary1 methods */
    d3d12_pipeline_library_LoadPipeline,
};

static HRESULT d3d12_pipeline_library_init(struct d3d12_pipeline_library *pipeline_library,
        struct d3d12_device *device, const void *blob, size_t blob_length)
{
    HRESULT hr;

    memset(pipeline_library, 0, sizeof(*pipeline_library));
    pipeline_library->ID3D12PipelineLibrary_iface.lpVtbl = &d3d12_pipeline_library_vtbl;
    pipeline_library->refcount = 1;

    if (FAILED(hr = vkd3d_private_store_init(&pipeline_library->private_store)))
        goto fail;

    d3d12_device_add_ref(pipeline_library->device = device);
    return S_OK;

fail:
    return hr;
}

HRESULT d3d12_pipeline_library_create(struct d3d12_device *device, const void *blob,
        size_t blob_length, struct d3d12_pipeline_library **pipeline_library)
{
    struct d3d12_pipeline_library *object;
    HRESULT hr;

    if (!(object = vkd3d_malloc(sizeof(*object))))
        return E_OUTOFMEMORY;

    if (FAILED(hr = d3d12_pipeline_library_init(object, device, blob, blob_length)))
    {
        vkd3d_free(object);
        return hr;
    }

    TRACE("Created pipeline library %p.\n", object);

    *pipeline_library = object;
    return S_OK;
}
