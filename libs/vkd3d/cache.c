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

struct vkd3d_pipeline_blob
{
    uint32_t version;
    uint32_t vendor_id;
    uint32_t device_id;
    uint64_t vkd3d_build;
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
    if (blob->vkd3d_build != vkd3d_build ||
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
        blob->vkd3d_build = vkd3d_build;
        memcpy(blob->cache_uuid, device_properties->pipelineCacheUUID, VK_UUID_SIZE);

        if (state->vk_pso_cache)
        {
            if ((vr = VK_CALL(vkGetPipelineCacheData(state->device->vk_device, state->vk_pso_cache, &vk_blob_size, blob->vk_blob))))
                return vr;
        }
    }

    *size = total_size;
    return VK_SUCCESS;
}

struct vkd3d_cached_pipeline_key
{
    size_t name_length;
    const void *name;
};

struct vkd3d_cached_pipeline_data
{
    size_t blob_length;
    const void *blob;
    bool is_new;
};

struct vkd3d_cached_pipeline_entry
{
    struct hash_map_entry entry;
    struct vkd3d_cached_pipeline_key key;
    struct vkd3d_cached_pipeline_data data;
};

static uint32_t vkd3d_cached_pipeline_hash(const void *key)
{
    const struct vkd3d_cached_pipeline_key *k = key;
    uint32_t hash = 0;
    size_t i;

    for (i = 0; i < k->name_length; i += 4)
    {
        uint32_t accum = 0;
        memcpy(&accum, (const char*)k->name + i,
                min(k->name_length - i, sizeof(accum)));
        hash = hash_combine(hash, accum);
    }

    return hash;
}

static bool vkd3d_cached_pipeline_compare(const void *key, const struct hash_map_entry *entry)
{
    const struct vkd3d_cached_pipeline_entry *e = (const struct vkd3d_cached_pipeline_entry*)entry;
    const struct vkd3d_cached_pipeline_key *k = key;

    return k->name_length == e->key.name_length &&
            !memcmp(k->name, e->key.name, k->name_length);
}

struct vkd3d_serialized_pipeline
{
    uint32_t name_length;
    uint32_t blob_length;
    uint8_t data[];
};

#define VKD3D_PIPELINE_LIBRARY_VERSION MAKE_MAGIC('V','K','L',1)

struct vkd3d_serialized_pipeline_library
{
    uint32_t version;
    uint32_t vendor_id;
    uint32_t device_id;
    uint32_t pipeline_count;
    uint64_t vkd3d_build;
    uint8_t cache_uuid[VK_UUID_SIZE];
    uint8_t data[];
};

/* ID3D12PipelineLibrary */
static inline struct d3d12_pipeline_library *impl_from_ID3D12PipelineLibrary(d3d12_pipeline_library_iface *iface)
{
    return CONTAINING_RECORD(iface, struct d3d12_pipeline_library, ID3D12PipelineLibrary_iface);
}

static bool d3d12_pipeline_library_serialize_entry(struct d3d12_pipeline_library *pipeline_library,
        const struct vkd3d_cached_pipeline_entry *entry, size_t *size, void *data)
{
    struct vkd3d_serialized_pipeline *header = data;
    size_t total_size;

    total_size = sizeof(header) + entry->key.name_length + entry->data.blob_length;

    if (header)
    {
        if (*size < total_size)
        {
            ERR("Not enough memory provided to store pipeline blob.\n");
            return false;
        }

        header->name_length = entry->key.name_length;
        header->blob_length = entry->data.blob_length;
        memcpy(header->data, entry->key.name, entry->key.name_length);
        memcpy(header->data + entry->key.name_length, entry->data.blob, entry->data.blob_length);
    }

    *size = total_size;
    return true;
}

static void d3d12_pipeline_library_cleanup(struct d3d12_pipeline_library *pipeline_library, struct d3d12_device *device)
{
    size_t i;

    for (i = 0; i < pipeline_library->map.entry_count; i++)
    {
        struct vkd3d_cached_pipeline_entry *e = (struct vkd3d_cached_pipeline_entry*)hash_map_get_entry(&pipeline_library->map, i);

        if ((e->entry.flags & HASH_MAP_ENTRY_OCCUPIED) && e->data.is_new)
        {
            vkd3d_free((void*)e->key.name);
            vkd3d_free((void*)e->data.blob);
        }
    }

    hash_map_clear(&pipeline_library->map);

    vkd3d_private_store_destroy(&pipeline_library->private_store);
    pthread_mutex_destroy(&pipeline_library->mutex);
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
        d3d12_pipeline_library_cleanup(pipeline_library, pipeline_library->device);
        d3d12_device_release(pipeline_library->device);
        vkd3d_free(pipeline_library);
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
    struct d3d12_pipeline_state *pipeline_state = unsafe_impl_from_ID3D12PipelineState(pipeline);
    size_t wchar_size = pipeline_library->device->wchar_size;
    struct vkd3d_cached_pipeline_entry entry;
    void *new_name, *new_blob;
    VkResult vr;
    int rc;

    TRACE("iface %p, name %s, pipeline %p.\n", iface, debugstr_w(name, pipeline_library->device->wchar_size), pipeline);

    if ((rc = pthread_mutex_lock(&pipeline_library->mutex)))
    {
        ERR("Failed to lock mutex, rc %d.\n", rc);
        return hresult_from_errno(rc);
    }

    entry.key.name_length = vkd3d_wcslen(name, wchar_size) * wchar_size;
    entry.key.name = name;

    if (hash_map_find(&pipeline_library->map, &entry.key))
    {
        WARN("Pipeline %s already exists.\n", debugstr_w(name, pipeline_library->device->wchar_size));
        pthread_mutex_unlock(&pipeline_library->mutex);
        return E_INVALIDARG;
    }

    /* We need to allocate persistent storage for the name */
    if (!(new_name = malloc(entry.key.name_length)))
    {
        pthread_mutex_unlock(&pipeline_library->mutex);
        return E_OUTOFMEMORY;
    }

    memcpy(new_name, name, entry.key.name_length);
    entry.key.name = new_name;

    if (FAILED(vr = vkd3d_serialize_pipeline_state(pipeline_state, &entry.data.blob_length, NULL)))
    {
        vkd3d_free(new_name);
        pthread_mutex_unlock(&pipeline_library->mutex);
        return hresult_from_vk_result(vr);
    }

    if (!(new_blob = malloc(entry.data.blob_length)))
    {
        vkd3d_free(new_name);
        pthread_mutex_unlock(&pipeline_library->mutex);
        return E_OUTOFMEMORY;
    }

    if (FAILED(vr = vkd3d_serialize_pipeline_state(pipeline_state, &entry.data.blob_length, new_blob)))
    {
        vkd3d_free(new_name);
        vkd3d_free(new_blob);
        pthread_mutex_unlock(&pipeline_library->mutex);
        return hresult_from_vk_result(vr);
    }

    entry.data.blob = new_blob;
    entry.data.is_new = true;

    if (!hash_map_insert(&pipeline_library->map, &entry.key, &entry.entry))
    {
        vkd3d_free(new_name);
        vkd3d_free(new_blob);
        pthread_mutex_unlock(&pipeline_library->mutex);
        return E_OUTOFMEMORY;
    }

    pthread_mutex_unlock(&pipeline_library->mutex);
    return S_OK;
}

static HRESULT d3d12_pipeline_library_load_pipeline(struct d3d12_pipeline_library *pipeline_library, LPCWSTR name,
        VkPipelineBindPoint bind_point, struct d3d12_pipeline_state_desc *desc, struct d3d12_pipeline_state **state)
{
    size_t wchar_size = pipeline_library->device->wchar_size;
    const struct vkd3d_cached_pipeline_entry *e;
    struct vkd3d_cached_pipeline_key key;
    int rc;

    if ((rc = pthread_mutex_lock(&pipeline_library->mutex)))
    {
        ERR("Failed to lock mutex, rc %d.\n", rc);
        return hresult_from_errno(rc);
    }

    key.name_length = vkd3d_wcslen(name, wchar_size) * wchar_size;
    key.name = name;

    if (!(e = (const struct vkd3d_cached_pipeline_entry*)hash_map_find(&pipeline_library->map, &key)))
    {
        WARN("Pipeline %s does not exist.\n", debugstr_w(name, pipeline_library->device->wchar_size));
        pthread_mutex_unlock(&pipeline_library->mutex);
        return E_INVALIDARG;
    }

    desc->cached_pso.CachedBlobSizeInBytes = e->data.blob_length;
    desc->cached_pso.pCachedBlob = e->data.blob;
    pthread_mutex_unlock(&pipeline_library->mutex);

    return d3d12_pipeline_state_create(pipeline_library->device, bind_point, desc, state);
}

static HRESULT STDMETHODCALLTYPE d3d12_pipeline_library_LoadGraphicsPipeline(d3d12_pipeline_library_iface *iface,
        LPCWSTR name, const D3D12_GRAPHICS_PIPELINE_STATE_DESC *desc, REFIID iid, void **pipeline_state)
{
    struct d3d12_pipeline_library *pipeline_library = impl_from_ID3D12PipelineLibrary(iface);
    struct d3d12_pipeline_state_desc pipeline_desc;
    struct d3d12_pipeline_state *object;
    HRESULT hr;

    TRACE("iface %p, name %s, desc %p, iid %s, pipeline_state %p.\n", iface,
            debugstr_w(name, pipeline_library->device->wchar_size),
            desc, debugstr_guid(iid), pipeline_state);

    if (FAILED(hr = vkd3d_pipeline_state_desc_from_d3d12_graphics_desc(&pipeline_desc, desc)))
        return hr;

    if (FAILED(hr = d3d12_pipeline_library_load_pipeline(pipeline_library,
            name, VK_PIPELINE_BIND_POINT_GRAPHICS, &pipeline_desc, &object)))
        return hr;

    return return_interface(&object->ID3D12PipelineState_iface,
            &IID_ID3D12PipelineState, iid, pipeline_state);
}

static HRESULT STDMETHODCALLTYPE d3d12_pipeline_library_LoadComputePipeline(d3d12_pipeline_library_iface *iface,
        LPCWSTR name, const D3D12_COMPUTE_PIPELINE_STATE_DESC *desc, REFIID iid, void **pipeline_state)
{
    struct d3d12_pipeline_library *pipeline_library = impl_from_ID3D12PipelineLibrary(iface);
    struct d3d12_pipeline_state_desc pipeline_desc;
    struct d3d12_pipeline_state *object;
    HRESULT hr;

    TRACE("iface %p, name %s, desc %p, iid %s, pipeline_state %p.\n", iface,
            debugstr_w(name, pipeline_library->device->wchar_size),
            desc, debugstr_guid(iid), pipeline_state);

    if (FAILED(hr = vkd3d_pipeline_state_desc_from_d3d12_compute_desc(&pipeline_desc, desc)))
        return hr;

    if (FAILED(hr = d3d12_pipeline_library_load_pipeline(pipeline_library,
            name, VK_PIPELINE_BIND_POINT_COMPUTE, &pipeline_desc, &object)))
        return hr;

    return return_interface(&object->ID3D12PipelineState_iface,
            &IID_ID3D12PipelineState, iid, pipeline_state);
}

static SIZE_T STDMETHODCALLTYPE d3d12_pipeline_library_GetSerializedSize(d3d12_pipeline_library_iface *iface)
{
    struct d3d12_pipeline_library *pipeline_library = impl_from_ID3D12PipelineLibrary(iface);
    size_t total_size = sizeof(struct vkd3d_serialized_pipeline_library);
    uint32_t i;
    int rc;

    TRACE("iface %p.\n", iface);

    if ((rc = pthread_mutex_lock(&pipeline_library->mutex)))
    {
        ERR("Failed to lock mutex, rc %d.\n", rc);
        return 0;
    }

    for (i = 0; i < pipeline_library->map.entry_count; i++)
    {
        struct vkd3d_cached_pipeline_entry *e = (struct vkd3d_cached_pipeline_entry*)hash_map_get_entry(&pipeline_library->map, i);

        if (e->entry.flags & HASH_MAP_ENTRY_OCCUPIED)
        {
            size_t pipeline_size = 0;

            if (!d3d12_pipeline_library_serialize_entry(pipeline_library, e, &pipeline_size, NULL))
                return 0;

            total_size += pipeline_size;
        }
    }

    pthread_mutex_unlock(&pipeline_library->mutex);
    return total_size;
}

static HRESULT STDMETHODCALLTYPE d3d12_pipeline_library_Serialize(d3d12_pipeline_library_iface *iface,
        void *data, SIZE_T data_size)
{
    struct d3d12_pipeline_library *pipeline_library = impl_from_ID3D12PipelineLibrary(iface);
    const VkPhysicalDeviceProperties *device_properties = &pipeline_library->device->device_info.properties2.properties;
    struct vkd3d_serialized_pipeline_library *header = data;
    size_t serialized_size = data_size - sizeof(*header);
    uint8_t *serialized_data = header->data;
    uint32_t i;
    int rc;

    TRACE("iface %p.\n", iface);

    if (data_size < sizeof(*header))
        return E_INVALIDARG;

    if ((rc = pthread_mutex_lock(&pipeline_library->mutex)))
    {
        ERR("Failed to lock mutex, rc %d.\n", rc);
        return 0;
    }

    header->version = VKD3D_PIPELINE_LIBRARY_VERSION;
    header->vendor_id = device_properties->vendorID;
    header->device_id = device_properties->deviceID;
    header->pipeline_count = pipeline_library->map.used_count;
    header->vkd3d_build = vkd3d_build;
    memcpy(header->cache_uuid, device_properties->pipelineCacheUUID, VK_UUID_SIZE);

    for (i = 0; i < pipeline_library->map.entry_count; i++)
    {
        struct vkd3d_cached_pipeline_entry *e = (struct vkd3d_cached_pipeline_entry*)hash_map_get_entry(&pipeline_library->map, i);

        if (e->entry.flags & HASH_MAP_ENTRY_OCCUPIED)
        {
            size_t pipeline_size = serialized_size;

            /* Fails if the provided buffer is too small to fit the pipeline */
            if (!d3d12_pipeline_library_serialize_entry(pipeline_library, e, &pipeline_size, serialized_data))
            {
                pthread_mutex_unlock(&pipeline_library->mutex);
                return E_INVALIDARG;
            }

            serialized_data += pipeline_size;
            serialized_size -= pipeline_size;
        }
    }

    pthread_mutex_unlock(&pipeline_library->mutex);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d3d12_pipeline_library_LoadPipeline(d3d12_pipeline_library_iface *iface,
        LPCWSTR name, const D3D12_PIPELINE_STATE_STREAM_DESC *desc, REFIID iid, void **pipeline_state)
{
    struct d3d12_pipeline_library *pipeline_library = impl_from_ID3D12PipelineLibrary(iface);
    struct d3d12_pipeline_state_desc pipeline_desc;
    struct d3d12_pipeline_state *object;
    VkPipelineBindPoint pipeline_type;
    HRESULT hr;

    TRACE("iface %p, name %s, desc %p, iid %s, pipeline_state %p.\n", iface,
            debugstr_w(name, pipeline_library->device->wchar_size),
            desc, debugstr_guid(iid), pipeline_state);

    if (FAILED(hr = vkd3d_pipeline_state_desc_from_d3d12_stream_desc(&pipeline_desc, desc, &pipeline_type)))
        return hr;

    if (FAILED(hr = d3d12_pipeline_library_load_pipeline(pipeline_library,
            name, pipeline_type, &pipeline_desc, &object)))
        return hr;

    return return_interface(&object->ID3D12PipelineState_iface,
            &IID_ID3D12PipelineState, iid, pipeline_state);
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

static HRESULT d3d12_pipeline_library_read_blob(struct d3d12_pipeline_library *pipeline_library,
        struct d3d12_device *device, const void *blob, size_t blob_length)
{
    const VkPhysicalDeviceProperties *device_properties = &device->device_info.properties2.properties;
    const struct vkd3d_serialized_pipeline_library *header = blob;
    const uint8_t *end = header->data + blob_length - sizeof(*header);
    const uint8_t *cur = header->data;
    uint32_t i;

    /* Same logic as for pipeline blobs, indicate that the app needs
     * to rebuild the pipeline library in case vkd3d itself or the
     * underlying device/driver changed */
    if (blob_length < sizeof(*header) || header->version != VKD3D_PIPELINE_LIBRARY_VERSION)
        return D3D12_ERROR_DRIVER_VERSION_MISMATCH;

    if (header->device_id != device_properties->deviceID || header->vendor_id != device_properties->vendorID)
        return D3D12_ERROR_ADAPTER_NOT_FOUND;

    if (header->vkd3d_build != vkd3d_build ||
            memcmp(header->cache_uuid, device_properties->pipelineCacheUUID, VK_UUID_SIZE))
        return D3D12_ERROR_DRIVER_VERSION_MISMATCH;

    /* The application is not allowed to free the blob, so we
     * can safely use pointers without copying the data first. */
    for (i = 0; i < header->pipeline_count; i++)
    {
        const struct vkd3d_serialized_pipeline *pipeline = (const struct vkd3d_serialized_pipeline*)cur;
        struct vkd3d_cached_pipeline_entry entry;

        if (cur + sizeof(*pipeline) > end)
            return E_INVALIDARG;

        cur += sizeof(*pipeline) + pipeline->name_length + pipeline->blob_length;

        if (cur > end)
            return E_INVALIDARG;

        entry.key.name_length = pipeline->name_length;
        entry.key.name = pipeline->data;

        entry.data.blob_length = pipeline->blob_length;
        entry.data.blob = pipeline->data + pipeline->name_length;
        entry.data.is_new = false;

        if (!hash_map_insert(&pipeline_library->map, &entry.key, &entry.entry))
            return E_OUTOFMEMORY;
    }

    return S_OK;
}

static HRESULT d3d12_pipeline_library_init(struct d3d12_pipeline_library *pipeline_library,
        struct d3d12_device *device, const void *blob, size_t blob_length)
{
    HRESULT hr;
    int rc;

    memset(pipeline_library, 0, sizeof(*pipeline_library));
    pipeline_library->ID3D12PipelineLibrary_iface.lpVtbl = &d3d12_pipeline_library_vtbl;
    pipeline_library->refcount = 1;

    if (!blob_length && blob)
        return E_INVALIDARG;

    if ((rc = pthread_mutex_init(&pipeline_library->mutex, NULL)))
        return hresult_from_errno(rc);

    hash_map_init(&pipeline_library->map, &vkd3d_cached_pipeline_hash,
            &vkd3d_cached_pipeline_compare, sizeof(struct vkd3d_cached_pipeline_entry));

    if (blob_length)
    {
        if (FAILED(hr = d3d12_pipeline_library_read_blob(pipeline_library, device, blob, blob_length)))
            goto cleanup_hash_map;
    }

    if (FAILED(hr = vkd3d_private_store_init(&pipeline_library->private_store)))
        goto cleanup_mutex;

    d3d12_device_add_ref(pipeline_library->device = device);
    return hr;

cleanup_hash_map:
    hash_map_clear(&pipeline_library->map);
cleanup_mutex:
    pthread_mutex_destroy(&pipeline_library->mutex);
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
