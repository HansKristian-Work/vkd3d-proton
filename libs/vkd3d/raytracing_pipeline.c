/*
 * Copyright 2021 Hans-Kristian Arntzen for Valve Corporation
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
#include "vkd3d_string.h"

static inline struct d3d12_state_object *impl_from_ID3D12StateObjectProperties(ID3D12StateObjectProperties *iface)
{
    return CONTAINING_RECORD(iface, struct d3d12_state_object, ID3D12StateObjectProperties_iface);
}

static HRESULT STDMETHODCALLTYPE d3d12_state_object_QueryInterface(ID3D12StateObject *iface,
        REFIID riid, void **object)
{
    TRACE("iface %p, riid %s, object %p.\n", iface, debugstr_guid(riid), object);

    if (IsEqualGUID(riid, &IID_ID3D12StateObject)
        || IsEqualGUID(riid, &IID_ID3D12DeviceChild)
        || IsEqualGUID(riid, &IID_ID3D12Object)
        || IsEqualGUID(riid, &IID_IUnknown))
    {
        ID3D12StateObject_AddRef(iface);
        *object = iface;
        return S_OK;
    }

    if (IsEqualGUID(riid, &IID_ID3D12StateObjectProperties))
    {
        struct d3d12_state_object *state_object = impl_from_ID3D12StateObject(iface);
        ID3D12StateObjectProperties_AddRef(&state_object->ID3D12StateObjectProperties_iface);
        *object = &state_object->ID3D12StateObjectProperties_iface;
        return S_OK;
    }

    WARN("%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid(riid));

    *object = NULL;
    return E_NOINTERFACE;
}

static HRESULT STDMETHODCALLTYPE d3d12_state_object_properties_QueryInterface(ID3D12StateObjectProperties *iface,
        REFIID riid, void **object)
{
    struct d3d12_state_object *state_object = impl_from_ID3D12StateObjectProperties(iface);
    TRACE("iface %p, riid %s, object %p.\n", iface, debugstr_guid(riid), object);
    return d3d12_state_object_QueryInterface(&state_object->ID3D12StateObject_iface, riid, object);
}

static ULONG STDMETHODCALLTYPE d3d12_state_object_AddRef(ID3D12StateObject *iface)
{
    struct d3d12_state_object *state_object = impl_from_ID3D12StateObject(iface);
    ULONG refcount = InterlockedIncrement(&state_object->refcount);

    TRACE("%p increasing refcount to %u.\n", state_object, refcount);

    return refcount;
}

static ULONG STDMETHODCALLTYPE d3d12_state_object_properties_AddRef(ID3D12StateObjectProperties *iface)
{
    struct d3d12_state_object *state_object = impl_from_ID3D12StateObjectProperties(iface);
    ULONG refcount = InterlockedIncrement(&state_object->refcount);

    TRACE("%p increasing refcount to %u.\n", state_object, refcount);

    return refcount;
}

static void d3d12_state_object_cleanup(struct d3d12_state_object *object);

static void d3d12_state_object_inc_ref(struct d3d12_state_object *state_object)
{
    InterlockedIncrement(&state_object->internal_refcount);
}

static void d3d12_state_object_dec_ref(struct d3d12_state_object *state_object)
{
    ULONG refcount = InterlockedDecrement(&state_object->internal_refcount);

    TRACE("%p decreasing internal refcount to %u.\n", state_object, refcount);

    if (!refcount)
    {
        struct d3d12_device *device = state_object->device;
        vkd3d_private_store_destroy(&state_object->private_store);
        d3d12_state_object_cleanup(state_object);
        vkd3d_free(state_object);
        d3d12_device_release(device);
    }
}

static void d3d12_state_object_cleanup(struct d3d12_state_object *object)
{
    const struct vkd3d_vk_device_procs *vk_procs = &object->device->vk_procs;
    size_t i;

    for (i = 0; i < object->exports_count; i++)
    {
        vkd3d_free(object->exports[i].mangled_export);
        vkd3d_free(object->exports[i].plain_export);
    }
    vkd3d_free(object->exports);
    vkd3d_free(object->entry_points);

    for (i = 0; i < object->collections_count; i++)
        d3d12_state_object_dec_ref(object->collections[i]);
    vkd3d_free(object->collections);

    if (object->global_root_signature)
        d3d12_root_signature_dec_ref(object->global_root_signature);

    VK_CALL(vkDestroyPipeline(object->device->vk_device, object->pipeline, NULL));
    VK_CALL(vkDestroyPipeline(object->device->vk_device, object->pipeline_library, NULL));

    VK_CALL(vkDestroyPipelineLayout(object->device->vk_device,
            object->local_static_sampler.pipeline_layout, NULL));
    VK_CALL(vkDestroyDescriptorSetLayout(object->device->vk_device,
            object->local_static_sampler.set_layout, NULL));
    if (object->local_static_sampler.desc_set)
    {
        vkd3d_sampler_state_free_descriptor_set(&object->device->sampler_state, object->device,
                object->local_static_sampler.desc_set, object->local_static_sampler.desc_pool);
    }
}

static ULONG d3d12_state_object_release(struct d3d12_state_object *state_object)
{
    ULONG refcount = InterlockedDecrement(&state_object->refcount);

    TRACE("%p decreasing refcount to %u.\n", state_object, refcount);

    if (!refcount)
        d3d12_state_object_dec_ref(state_object);
    return refcount;
}

static ULONG STDMETHODCALLTYPE d3d12_state_object_Release(ID3D12StateObject *iface)
{
    struct d3d12_state_object *state_object = impl_from_ID3D12StateObject(iface);
    return d3d12_state_object_release(state_object);
}

static ULONG STDMETHODCALLTYPE d3d12_state_object_properties_Release(ID3D12StateObjectProperties *iface)
{
    struct d3d12_state_object *state_object = impl_from_ID3D12StateObjectProperties(iface);
    return d3d12_state_object_release(state_object);
}

static HRESULT STDMETHODCALLTYPE d3d12_state_object_GetPrivateData(ID3D12StateObject *iface,
        REFGUID guid, UINT *data_size, void *data)
{
    struct d3d12_state_object *state_object = impl_from_ID3D12StateObject(iface);

    TRACE("iface %p, guid %s, data_size %p, data %p.\n", iface, debugstr_guid(guid), data_size, data);

    return vkd3d_get_private_data(&state_object->private_store, guid, data_size, data);
}

static HRESULT STDMETHODCALLTYPE d3d12_state_object_SetPrivateData(ID3D12StateObject *iface,
        REFGUID guid, UINT data_size, const void *data)
{
    struct d3d12_state_object *state_object = impl_from_ID3D12StateObject(iface);

    TRACE("iface %p, guid %s, data_size %u, data %p.\n", iface, debugstr_guid(guid), data_size, data);

    return vkd3d_set_private_data(&state_object->private_store, guid, data_size, data,
            NULL, NULL);
}

static HRESULT STDMETHODCALLTYPE d3d12_state_object_SetPrivateDataInterface(ID3D12StateObject *iface,
        REFGUID guid, const IUnknown *data)
{
    struct d3d12_state_object *state_object = impl_from_ID3D12StateObject(iface);

    TRACE("iface %p, guid %s, data %p.\n", iface, debugstr_guid(guid), data);

    return vkd3d_set_private_data_interface(&state_object->private_store, guid, data,
            NULL, NULL);
}

static HRESULT STDMETHODCALLTYPE d3d12_state_object_GetDevice(ID3D12StateObject *iface,
        REFIID iid, void **device)
{
    struct d3d12_state_object *state_object = impl_from_ID3D12StateObject(iface);

    TRACE("iface %p, iid %s, device %p.\n", iface, debugstr_guid(iid), device);

    return d3d12_device_query_interface(state_object->device, iid, device);
}

static bool vkd3d_export_equal(LPCWSTR export, const struct vkd3d_shader_library_entry_point *entry)
{
    return vkd3d_export_strequal(export, entry->mangled_entry_point) ||
            vkd3d_export_strequal(export, entry->plain_entry_point);
}

static uint32_t d3d12_state_object_get_export_index(struct d3d12_state_object *object,
        const WCHAR *export_name, const WCHAR **out_subtype)
{
    const WCHAR *subtype = NULL;
    size_t i, n;

    /* Need to check for hitgroup::{closesthit,anyhit,intersection}. */
    n = 0;
    while (export_name[n] != 0 && export_name[n] != ':')
        n++;

    if (export_name[n] == ':')
        subtype = export_name + n;

    for (i = 0; i < object->exports_count; i++)
    {
        if (vkd3d_export_strequal_substr(export_name, n, object->exports[i].mangled_export) ||
                vkd3d_export_strequal_substr(export_name, n, object->exports[i].plain_export))
        {
            *out_subtype = subtype;
            return i;
        }
    }

    return UINT32_MAX;
}

static void * STDMETHODCALLTYPE d3d12_state_object_properties_GetShaderIdentifier(ID3D12StateObjectProperties *iface,
        LPCWSTR export_name)
{
    struct d3d12_state_object *object = impl_from_ID3D12StateObjectProperties(iface);
    struct d3d12_state_object_identifier *export;
    const WCHAR *subtype = NULL;
    uint32_t index;

    TRACE("iface %p, export_name %p.\n", iface, export_name);

    index = d3d12_state_object_get_export_index(object, export_name, &subtype);

    /* Cannot query shader identifier for non-group names. */
    if (!subtype && index != UINT32_MAX)
    {
        export = &object->exports[index];
        /* Need to return the parent SBT pointer if it exists */
        while (export->inherited_collection_index >= 0)
        {
            object = object->collections[export->inherited_collection_index];
            export = &object->exports[export->inherited_collection_export_index];
        }
        return export->identifier;
    }
    else
    {
        ERR("Could not find entry point.\n");
        return NULL;
    }
}

static UINT64 STDMETHODCALLTYPE d3d12_state_object_properties_GetShaderStackSize(ID3D12StateObjectProperties *iface,
        LPCWSTR export_name)
{
    struct d3d12_state_object *object = impl_from_ID3D12StateObjectProperties(iface);
    const WCHAR *subtype = NULL;
    uint32_t index;

    TRACE("iface %p, export_name %p!\n", iface, export_name);

    index = d3d12_state_object_get_export_index(object, export_name, &subtype);
    if (index == UINT32_MAX)
        return UINT32_MAX;

    if (subtype)
    {
        if (vkd3d_export_strequal(subtype, u"::intersection"))
            return object->exports[index].stack_size_intersection;
        else if (vkd3d_export_strequal(subtype, u"::anyhit"))
            return object->exports[index].stack_size_any;
        else if (vkd3d_export_strequal(subtype, u"::closesthit"))
            return object->exports[index].stack_size_closest;
        else
            return UINT32_MAX;
    }
    else
    {
        return object->exports[index].stack_size_general;
    }
}

static UINT64 STDMETHODCALLTYPE d3d12_state_object_properties_GetPipelineStackSize(ID3D12StateObjectProperties *iface)
{
    struct d3d12_state_object *object = impl_from_ID3D12StateObjectProperties(iface);
    TRACE("iface %p\n", iface);
    return object->pipeline_stack_size;
}

static void STDMETHODCALLTYPE d3d12_state_object_properties_SetPipelineStackSize(ID3D12StateObjectProperties *iface,
        UINT64 stack_size_in_bytes)
{
    struct d3d12_state_object *object = impl_from_ID3D12StateObjectProperties(iface);
    TRACE("iface %p, stack_size_in_bytes %llu!\n", iface, stack_size_in_bytes);

    /* This behavior seems to match what I'm seeing on NV Windows driver. */
    object->pipeline_stack_size = stack_size_in_bytes;
}

static CONST_VTBL struct ID3D12StateObjectVtbl d3d12_state_object_vtbl =
{
    /* IUnknown methods */
    d3d12_state_object_QueryInterface,
    d3d12_state_object_AddRef,
    d3d12_state_object_Release,
    /* ID3D12Object methods */
    d3d12_state_object_GetPrivateData,
    d3d12_state_object_SetPrivateData,
    d3d12_state_object_SetPrivateDataInterface,
    (void *)d3d12_object_SetName,
    /* ID3D12DeviceChild methods */
    d3d12_state_object_GetDevice,
};

static CONST_VTBL struct ID3D12StateObjectPropertiesVtbl d3d12_state_object_properties_vtbl =
{
    /* IUnknown methods */
    d3d12_state_object_properties_QueryInterface,
    d3d12_state_object_properties_AddRef,
    d3d12_state_object_properties_Release,
    /* ID3D12StateObjectProperties methods */
    d3d12_state_object_properties_GetShaderIdentifier,
    d3d12_state_object_properties_GetShaderStackSize,
    d3d12_state_object_properties_GetPipelineStackSize,
    d3d12_state_object_properties_SetPipelineStackSize,
};

struct d3d12_state_object_root_signature_association
{
    struct d3d12_root_signature *root_signature;
    const WCHAR *export;
};

struct d3d12_state_object_collection
{
    struct d3d12_state_object *object;
    unsigned int num_exports;
    const D3D12_EXPORT_DESC *exports;
};

struct d3d12_state_object_pipeline_data
{
    D3D12_RAYTRACING_PIPELINE_CONFIG1 pipeline_config;
    bool has_pipeline_config;

    const D3D12_RAYTRACING_SHADER_CONFIG *shader_config;
    struct d3d12_root_signature *global_root_signature;
    struct d3d12_root_signature *high_priority_local_root_signature;
    struct d3d12_root_signature *low_priority_local_root_signature;

    /* Map 1:1 with VkShaderModule. */
    struct vkd3d_shader_library_entry_point *entry_points;
    size_t entry_points_size;
    size_t entry_points_count;

    /* Resolve these to group + export name later. */
    const struct D3D12_HIT_GROUP_DESC **hit_groups;
    size_t hit_groups_size;
    size_t hit_groups_count;

    /* Used to finalize compilation later. */
    const struct D3D12_DXIL_LIBRARY_DESC **dxil_libraries;
    size_t dxil_libraries_size;
    size_t dxil_libraries_count;

    /* Maps 1:1 to groups. */
    struct d3d12_state_object_identifier *exports;
    size_t exports_size;
    size_t exports_count;

    /* Each group in Vulkan is either a generic group or hit group. */
    VkRayTracingShaderGroupCreateInfoKHR *groups;
    size_t groups_size;
    size_t groups_count;

    struct d3d12_state_object_root_signature_association *associations;
    size_t associations_size;
    size_t associations_count;

    VkPipelineShaderStageCreateInfo *stages;
    size_t stages_size;
    size_t stages_count;

    struct d3d12_state_object_collection *collections;
    size_t collections_size;
    size_t collections_count;

    VkPipeline *vk_libraries;
    size_t vk_libraries_size;
    size_t vk_libraries_count;
};

static void d3d12_state_object_pipeline_data_cleanup(struct d3d12_state_object_pipeline_data *data,
        struct d3d12_device *device)
{
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    unsigned int i;

    vkd3d_shader_dxil_free_library_entry_points(data->entry_points, data->entry_points_count);
    vkd3d_free((void*)data->hit_groups);
    vkd3d_free((void*)data->dxil_libraries);

    for (i = 0; i < data->exports_count; i++)
    {
        vkd3d_free(data->exports[i].mangled_export);
        vkd3d_free(data->exports[i].plain_export);
    }
    vkd3d_free(data->exports);
    vkd3d_free(data->groups);

    for (i = 0; i < data->stages_count; i++)
        VK_CALL(vkDestroyShaderModule(device->vk_device, data->stages[i].module, NULL));
    vkd3d_free(data->stages);

    vkd3d_free(data->associations);
    vkd3d_free(data->collections);
    vkd3d_free(data->vk_libraries);
}

static HRESULT d3d12_state_object_add_collection(
        struct d3d12_state_object *collection,
        struct d3d12_state_object_pipeline_data *data,
        const D3D12_EXPORT_DESC *exports, unsigned int num_exports)
{
    if (!vkd3d_array_reserve((void **)&data->collections, &data->collections_size,
            data->collections_count + 1, sizeof(*data->collections)))
        return E_OUTOFMEMORY;

    /* If a PSO only declares collections, but no pipelines, just inherit various state.
     * Also, validates that we have a match across different PSOs. */
    if (data->global_root_signature)
    {
        if (!collection->global_root_signature ||
                data->global_root_signature->compatibility_hash != collection->global_root_signature->compatibility_hash)
        {
            FIXME("Mismatch in global root signature state for PSO and collection.\n");
            return E_INVALIDARG;
        }
    }
    else
        data->global_root_signature = collection->global_root_signature;

    if (data->has_pipeline_config)
    {
        if (memcmp(&data->pipeline_config, &collection->pipeline_config, sizeof(data->pipeline_config)) != 0)
        {
            FIXME("Mismatch in pipeline config state for collection and PSO.\n");
            return E_INVALIDARG;
        }
    }
    else
    {
        data->pipeline_config = collection->pipeline_config;
        data->has_pipeline_config = true;
    }

    if (data->shader_config)
    {
        if (memcmp(data->shader_config, &collection->shader_config, sizeof(*data->shader_config)) != 0)
        {
            FIXME("Mismatch in shader config state for collection and PSO.\n");
            return E_INVALIDARG;
        }
    }
    else
        data->shader_config = &collection->shader_config;

    data->collections[data->collections_count].object = collection;
    data->collections[data->collections_count].num_exports = num_exports;
    data->collections[data->collections_count].exports = exports;

    vkd3d_array_reserve((void **)&data->vk_libraries, &data->vk_libraries_size,
            data->vk_libraries_count + 1, sizeof(*data->vk_libraries));
    data->vk_libraries[data->vk_libraries_count] =
            data->collections[data->collections_count].object->pipeline_library;

    data->collections_count += 1;
    data->vk_libraries_count += 1;
    return S_OK;
}

static HRESULT d3d12_state_object_parse_subobjects(struct d3d12_state_object *object,
        const D3D12_STATE_OBJECT_DESC *desc,
        struct d3d12_state_object *parent,
        struct d3d12_state_object_pipeline_data *data)
{
    unsigned int i, j;
    HRESULT hr;

    if (parent && FAILED(hr = d3d12_state_object_add_collection(parent, data, NULL, 0)))
        return hr;

    for (i = 0; i < desc->NumSubobjects; i++)
    {
        const D3D12_STATE_SUBOBJECT *obj = &desc->pSubobjects[i];
        switch (obj->Type)
        {
            case D3D12_STATE_SUBOBJECT_TYPE_STATE_OBJECT_CONFIG:
            {
                const uint32_t supported_flags =
                        D3D12_STATE_OBJECT_FLAG_ALLOW_EXTERNAL_DEPENDENCIES_ON_LOCAL_DEFINITIONS |
                        D3D12_STATE_OBJECT_FLAG_ALLOW_STATE_OBJECT_ADDITIONS;
                const D3D12_STATE_OBJECT_CONFIG *object_config = obj->pDesc;
                object->flags = object_config->Flags;
                if (object->flags & ~supported_flags)
                {
                    FIXME("Object config flag #%x is not supported.\n", object->flags);
                    return E_INVALIDARG;
                }
                break;
            }

            case D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE:
            {
                const D3D12_GLOBAL_ROOT_SIGNATURE *rs = obj->pDesc;
                struct d3d12_root_signature *new_rs;
                struct d3d12_root_signature *old_rs;

                new_rs = impl_from_ID3D12RootSignature(rs->pGlobalRootSignature);
                old_rs = data->global_root_signature;

                if (new_rs && old_rs && new_rs->compatibility_hash != old_rs->compatibility_hash)
                {
                    /* Simplicity for now. */
                    FIXME("More than one unique global root signature is used.\n");
                    return E_INVALIDARG;
                }

                if (!data->global_root_signature)
                    data->global_root_signature = new_rs;
                break;
            }

            case D3D12_STATE_SUBOBJECT_TYPE_LOCAL_ROOT_SIGNATURE:
            {
                const D3D12_LOCAL_ROOT_SIGNATURE *rs = obj->pDesc;
                /* This is only chosen as default if there is nothing else.
                 * Conflicting definitions seem to cause runtime to choose something
                 * arbitrary. Just override the low priority default.
                 * A high priority default association takes precedence if it exists. */
                data->low_priority_local_root_signature = impl_from_ID3D12RootSignature(rs->pLocalRootSignature);
                break;
            }

            case D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY:
            {
                const D3D12_DXIL_LIBRARY_DESC *lib = obj->pDesc;
                if (vkd3d_shader_dxil_append_library_entry_points(lib, data->dxil_libraries_count,
                        &data->entry_points, &data->entry_points_size,
                        &data->entry_points_count) != VKD3D_OK)
                {
                    ERR("Failed to parse DXIL library.\n");
                    return E_OUTOFMEMORY;
                }
                vkd3d_array_reserve((void**)&data->dxil_libraries, &data->dxil_libraries_size,
                        data->dxil_libraries_count + 1, sizeof(*data->dxil_libraries));
                data->dxil_libraries[data->dxil_libraries_count++] = lib;
                break;
            }

            case D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP:
            {
                const D3D12_HIT_GROUP_DESC *group = obj->pDesc;
                vkd3d_array_reserve((void**)&data->hit_groups, &data->hit_groups_size,
                        data->hit_groups_count + 1, sizeof(*data->hit_groups));
                data->hit_groups[data->hit_groups_count++] = group;
                break;
            }

            case D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG:
            {
                if (data->shader_config && memcmp(data->shader_config, obj->pDesc, sizeof(*data->shader_config)) != 0)
                {
                    ERR("RAYTRACING_SHADER_CONFIG must match if multiple objects are present.\n");
                    return E_INVALIDARG;
                }
                data->shader_config = obj->pDesc;
                break;
            }

            case D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG:
            {
                const D3D12_RAYTRACING_PIPELINE_CONFIG *pipeline_config;
                D3D12_RAYTRACING_PIPELINE_CONFIG1 config1;

                pipeline_config = obj->pDesc;
                config1.Flags = 0;
                config1.MaxTraceRecursionDepth = pipeline_config->MaxTraceRecursionDepth;

                if (data->has_pipeline_config &&
                        memcmp(&data->pipeline_config, &config1, sizeof(config1)) != 0)
                {
                    ERR("RAYTRACING_PIPELINE_CONFIG must match if multiple objects are present.\n");
                    return E_INVALIDARG;
                }
                data->has_pipeline_config = true;
                data->pipeline_config = config1;
                break;
            }

            case D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG1:
            {
                const D3D12_RAYTRACING_PIPELINE_CONFIG1 *pipeline_config = obj->pDesc;
                if (data->has_pipeline_config &&
                        memcmp(&data->pipeline_config, pipeline_config, sizeof(*pipeline_config)) != 0)
                {
                    ERR("RAYTRACING_PIPELINE_CONFIG1 must match if multiple objects are present.\n");
                    return E_INVALIDARG;
                }
                data->has_pipeline_config = true;
                data->pipeline_config = *pipeline_config;
                break;
            }

            case D3D12_STATE_SUBOBJECT_TYPE_SUBOBJECT_TO_EXPORTS_ASSOCIATION:
            {
                const D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION *association = obj->pDesc;
                const D3D12_LOCAL_ROOT_SIGNATURE *local_rs;

                switch (association->pSubobjectToAssociate->Type)
                {
                    /* These are irrelevant. There can only be one unique config,
                     * and what can happen here is that app redundantly assigns the same config.
                     * The associated object must be part of the PSO anyways, so it should be safe to ignore
                     * here. */
                    case D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG:
                    case D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG:
                    case D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE:
                        break;

                    case D3D12_STATE_SUBOBJECT_TYPE_LOCAL_ROOT_SIGNATURE:
                    {
                        local_rs = association->pSubobjectToAssociate->pDesc;
                        if (association->NumExports)
                        {
                            vkd3d_array_reserve((void **) &data->associations, &data->associations_size,
                                    data->associations_count + association->NumExports,
                                    sizeof(*data->associations));
                            for (j = 0; j < association->NumExports; j++)
                            {
                                data->associations[data->associations_count].export = association->pExports[j];
                                data->associations[data->associations_count].root_signature =
                                        impl_from_ID3D12RootSignature(local_rs->pLocalRootSignature);
                                data->associations_count++;
                            }
                        }
                        else
                        {
                            /* Local root signatures being exported to NULL takes priority as the default local RS. */
                            data->high_priority_local_root_signature =
                                    impl_from_ID3D12RootSignature(local_rs->pLocalRootSignature);
                        }
                        break;
                    }

                    default:
                        FIXME("Got unsupported subobject association type %u.\n", association->pSubobjectToAssociate->Type);
                        return E_INVALIDARG;
                }
                break;
            }

            case D3D12_STATE_SUBOBJECT_TYPE_EXISTING_COLLECTION:
            {
                const D3D12_EXISTING_COLLECTION_DESC *collection = obj->pDesc;
                struct d3d12_state_object *library_state;
                library_state = impl_from_ID3D12StateObject(collection->pExistingCollection);
                if (FAILED(hr = d3d12_state_object_add_collection(library_state, data,
                        collection->pExports, collection->NumExports)))
                {
                    return hr;
                }
                break;
            }

            case D3D12_STATE_SUBOBJECT_TYPE_NODE_MASK:
                /* Just ignore this. It's irrelevant for us. */
                break;

            default:
                FIXME("Unrecognized subobject type: %u.\n", obj->Type);
                return E_INVALIDARG;
        }
    }

    if (!data->has_pipeline_config)
    {
        ERR("Must have pipeline config.\n");
        return E_INVALIDARG;
    }

    if (!data->shader_config)
    {
        ERR("Must have shader config.\n");
        return E_INVALIDARG;
    }

    return S_OK;
}

static uint32_t d3d12_state_object_pipeline_data_find_entry_inner(
        const struct vkd3d_shader_library_entry_point *entry_points,
        size_t count, const WCHAR *import)
{
    uint32_t i;

    if (!import)
        return VK_SHADER_UNUSED_KHR;

    for (i = 0; i < count; i++)
        if (vkd3d_export_equal(import, &entry_points[i]))
            return i;

    return VK_SHADER_UNUSED_KHR;
}

static uint32_t d3d12_state_object_pipeline_data_find_entry(
        const struct d3d12_state_object_pipeline_data *data,
        const WCHAR *import)
{
    uint32_t offset = 0;
    uint32_t index;
    char *duped;
    size_t i;

    if (!import)
        return VK_SHADER_UNUSED_KHR;

    index = d3d12_state_object_pipeline_data_find_entry_inner(data->entry_points, data->entry_points_count, import);
    if (index != VK_SHADER_UNUSED_KHR)
        return index;

    offset += data->stages_count;

    /* Try to look in collections. We'll only find something in the ALLOW_EXTERNAL_DEPENDENCIES_ON_LOCAL
     * situation. Otherwise entry_points will be NULL. */
    for (i = 0; i < data->collections_count; i++)
    {
        index = d3d12_state_object_pipeline_data_find_entry_inner(data->collections[i].object->entry_points,
                data->collections[i].object->entry_points_count,
                import);
        if (index != VK_SHADER_UNUSED_KHR)
            return offset + index;

        offset += data->collections[i].object->stages_count;
    }

    duped = vkd3d_strdup_w_utf8(import, 0);
    ERR("Failed to find DXR entry point: %s.\n", duped);
    vkd3d_free(duped);
    return VK_SHADER_UNUSED_KHR;
}

static bool vkd3d_stage_is_global_group(VkShaderStageFlagBits stage)
{
    switch (stage)
    {
        case VK_SHADER_STAGE_CALLABLE_BIT_KHR:
        case VK_SHADER_STAGE_MISS_BIT_KHR:
        case VK_SHADER_STAGE_RAYGEN_BIT_KHR:
            return true;

        default:
            return false;
    }
}

static VkShaderModule create_shader_module(struct d3d12_device *device, const void *data, size_t size)
{
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    VkShaderModuleCreateInfo info;
    VkShaderModule module;

    info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    info.pNext = NULL;
    info.flags = 0;
    info.pCode = data;
    info.codeSize = size;

    if (VK_CALL(vkCreateShaderModule(device->vk_device, &info, NULL, &module)) == VK_SUCCESS)
        return module;
    else
        return VK_NULL_HANDLE;
}

static VkDeviceSize get_shader_stack_size(struct d3d12_state_object *object,
        uint32_t index, VkShaderGroupShaderKHR shader)
{
    const struct vkd3d_vk_device_procs *vk_procs = &object->device->vk_procs;
    return VK_CALL(vkGetRayTracingShaderGroupStackSizeKHR(object->device->vk_device,
            object->pipeline, index, shader));
}

static VkDeviceSize d3d12_state_object_pipeline_data_compute_default_stack_size(
        const struct d3d12_state_object_pipeline_data *data,
        struct d3d12_state_object_stack_info *stack_info, uint32_t recursion_depth)
{
    const struct d3d12_state_object_identifier *export;
    struct d3d12_state_object_stack_info stack;
    VkDeviceSize pipeline_stack_size = 0;
    size_t i;

    memset(&stack, 0, sizeof(stack));

    for (i = 0; i < data->exports_count; i++)
    {
        export = &data->exports[i];
        if (export->general_stage_index != UINT32_MAX)
        {
            switch (export->general_stage)
            {
                case VK_SHADER_STAGE_RAYGEN_BIT_KHR:
                    stack.max_raygen = max(stack.max_raygen, export->stack_size_general);
                    break;

                case VK_SHADER_STAGE_CALLABLE_BIT_KHR:
                    stack.max_callable = max(stack.max_callable, export->stack_size_general);
                    break;

                case VK_SHADER_STAGE_MISS_BIT_KHR:
                    stack.max_miss = max(stack.max_miss, export->stack_size_general);
                    break;

                default:
                    ERR("Unexpected stage #%x.\n", export->general_stage);
                    return 0;
            }
        }

        if (export->stack_size_closest != UINT32_MAX)
            stack.max_closest = max(stack.max_closest, export->stack_size_closest);
        if (export->stack_size_intersection != UINT32_MAX)
            stack.max_intersect = max(stack.max_intersect, export->stack_size_intersection);
        if (export->stack_size_any != UINT32_MAX)
            stack.max_anyhit = max(stack.max_anyhit, export->stack_size_any);
    }

    for (i = 0; i < data->collections_count; i++)
    {
        const struct d3d12_state_object_stack_info *info = &data->collections[i].object->stack;
        stack.max_closest = max(stack.max_closest, info->max_closest);
        stack.max_callable = max(stack.max_callable, info->max_callable);
        stack.max_miss = max(stack.max_miss, info->max_miss);
        stack.max_anyhit = max(stack.max_anyhit, info->max_anyhit);
        stack.max_intersect = max(stack.max_intersect, info->max_intersect);
        stack.max_raygen = max(stack.max_raygen, info->max_raygen);
    }

    *stack_info = stack;

    /* Vulkan and DXR specs outline this same formula. We will use this as the default pipeline stack size. */
    pipeline_stack_size += stack.max_raygen;
    pipeline_stack_size += 2 * stack.max_callable;
    pipeline_stack_size += (max(1, recursion_depth) - 1) * max(stack.max_closest, stack.max_miss);
    pipeline_stack_size += recursion_depth * max(max(stack.max_closest, stack.max_miss),
            stack.max_intersect + stack.max_anyhit);
    return pipeline_stack_size;
}

static struct d3d12_root_signature *d3d12_state_object_find_associated_root_signature_entry(
        struct d3d12_state_object_pipeline_data *data,
        const struct vkd3d_shader_library_entry_point *entry)
{
    size_t i;
    for (i = 0; i < data->associations_count; i++)
        if (vkd3d_export_equal(data->associations[i].export, entry))
            return data->associations[i].root_signature;
    return NULL;
}

static struct d3d12_root_signature *d3d12_state_object_find_associated_root_signature_export(
        struct d3d12_state_object_pipeline_data *data, LPCWSTR export)
{
    size_t i;
    for (i = 0; i < data->associations_count; i++)
        if (vkd3d_export_strequal(data->associations[i].export, export))
            return data->associations[i].root_signature;
    return NULL;
}

static struct d3d12_root_signature *d3d12_state_object_pipeline_data_get_local_root_signature(
        struct d3d12_state_object_pipeline_data *data,
        const struct vkd3d_shader_library_entry_point *entry)
{
    const D3D12_HIT_GROUP_DESC *hit_group;
    struct d3d12_root_signature *rs;
    size_t i;

    rs = d3d12_state_object_find_associated_root_signature_entry(data, entry);

    /* If we didn't find an association for this entry point, we might have an association
     * in a hit group export.
     * FIXME: Is it possible to have multiple hit groups, all referring to same entry point, while using
     * different root signatures for the different instances of the entry point? :| */
    for (i = 0; i < data->hit_groups_count && !rs; i++)
    {
        hit_group = data->hit_groups[i];
        if (vkd3d_export_equal(hit_group->ClosestHitShaderImport, entry) ||
                vkd3d_export_equal(hit_group->AnyHitShaderImport, entry) ||
                vkd3d_export_equal(hit_group->IntersectionShaderImport, entry))
        {
            rs = d3d12_state_object_find_associated_root_signature_export(data, hit_group->HitGroupExport);
        }
    }

    if (!rs)
    {
        if (data->high_priority_local_root_signature)
            rs = data->high_priority_local_root_signature;
        else if (data->low_priority_local_root_signature)
            rs = data->low_priority_local_root_signature;
        else
            rs = NULL;
    }

    return rs;
}

static HRESULT d3d12_state_object_get_group_handles(struct d3d12_state_object *object,
        const struct d3d12_state_object_pipeline_data *data)
{
    const struct vkd3d_vk_device_procs *vk_procs = &object->device->vk_procs;
    uint32_t collection_export;
    int collection_index;
    uint32_t group_index;
    VkResult vr;
    size_t i;

    for (i = 0; i < data->exports_count; i++)
    {
        group_index = data->exports[i].group_index;

        vr = VK_CALL(vkGetRayTracingShaderGroupHandlesKHR(object->device->vk_device,
                object->pipeline, group_index, 1,
                sizeof(data->exports[i].identifier),
                data->exports[i].identifier));
        if (vr)
            return hresult_from_vk_result(vr);

        collection_export = data->exports[i].inherited_collection_export_index;
        collection_index = data->exports[i].inherited_collection_index;

        if (collection_index >= 0)
        {
            const uint8_t *parent_identifier;
            const uint8_t *child_identifier;

            parent_identifier = data->collections[collection_index].object->exports[collection_export].identifier;
            child_identifier = data->exports[i].identifier;

            /* Validate that we get an exact match for SBT handle.
             * It appears to work just fine on NV. */
            if (memcmp(parent_identifier, child_identifier, D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES) != 0)
            {
                FIXME("SBT identifiers do not match for parent and child pipelines. "
                      "Vulkan does not guarantee this, but DXR 1.1 requires this. Cannot use pipeline.\n");
                return E_NOTIMPL;
            }
        }

        data->exports[i].stack_size_general = UINT32_MAX;
        data->exports[i].stack_size_any = UINT32_MAX;
        data->exports[i].stack_size_closest = UINT32_MAX;
        data->exports[i].stack_size_intersection = UINT32_MAX;

        if (data->exports[i].general_stage_index != VK_SHADER_UNUSED_KHR)
        {
            data->exports[i].stack_size_general = get_shader_stack_size(object, group_index,
                    VK_SHADER_GROUP_SHADER_GENERAL_KHR);
        }
        else
        {
            if (data->exports[i].anyhit_stage_index != VK_SHADER_UNUSED_KHR)
                data->exports[i].stack_size_any = get_shader_stack_size(object, group_index,
                        VK_SHADER_GROUP_SHADER_ANY_HIT_KHR);
            if (data->exports[i].closest_stage_index != VK_SHADER_UNUSED_KHR)
                data->exports[i].stack_size_closest = get_shader_stack_size(object, group_index,
                        VK_SHADER_GROUP_SHADER_CLOSEST_HIT_KHR);
            if (data->exports[i].intersection_stage_index != VK_SHADER_UNUSED_KHR)
                data->exports[i].stack_size_intersection = get_shader_stack_size(object, group_index,
                        VK_SHADER_GROUP_SHADER_INTERSECTION_KHR);
        }
    }

    return S_OK;
}

static void d3d12_state_object_build_group_create_info(
        VkRayTracingShaderGroupCreateInfoKHR *group_create,
        VkRayTracingShaderGroupTypeKHR group_type,
        const struct d3d12_state_object_identifier *export)
{
    group_create->sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
    group_create->pNext = NULL;
    group_create->type = group_type;
    /* Index into pStages. */
    group_create->generalShader = export->general_stage_index;
    group_create->closestHitShader = export->closest_stage_index;
    group_create->intersectionShader = export->intersection_stage_index;
    group_create->anyHitShader = export->anyhit_stage_index;
    group_create->pShaderGroupCaptureReplayHandle = NULL;
}

static void d3d12_state_object_append_local_static_samplers(
        struct d3d12_state_object *object,
        VkDescriptorSetLayoutBinding **out_vk_bindings, size_t *out_vk_bindings_size, size_t *out_vk_bindings_count,
        struct vkd3d_shader_resource_binding *local_bindings,
        const D3D12_STATIC_SAMPLER_DESC *sampler_desc, const VkSampler *vk_samplers,
        unsigned int sampler_count)
{
    VkDescriptorSetLayoutBinding *vk_bindings = *out_vk_bindings;
    size_t vk_bindings_count = *out_vk_bindings_count;
    size_t vk_bindings_size = *out_vk_bindings_size;
    unsigned int i, j;

    for (i = 0; i < sampler_count; i++)
    {
        for (j = 0; j < vk_bindings_count; j++)
            if (*vk_bindings[j].pImmutableSamplers == vk_samplers[i])
                break;

        /* Allocate a new sampler if unique so far. */
        if (j == vk_bindings_count)
        {
            vk_bindings_count++;
            vkd3d_array_reserve((void**)&vk_bindings, &vk_bindings_size, vk_bindings_count, sizeof(*vk_bindings));
            vk_bindings[j].stageFlags = vkd3d_vk_stage_flags_from_visibility(sampler_desc[i].ShaderVisibility);
            vk_bindings[j].pImmutableSamplers = &vk_samplers[i];
            vk_bindings[j].binding = j;
            vk_bindings[j].descriptorCount = 1;
            vk_bindings[j].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
        }

        local_bindings[i].type = VKD3D_SHADER_DESCRIPTOR_TYPE_SAMPLER;
        local_bindings[i].register_space = sampler_desc[i].RegisterSpace;
        local_bindings[i].register_index = sampler_desc[i].ShaderRegister;
        local_bindings[i].register_count = 1;
        local_bindings[i].descriptor_table = 0; /* ignored */
        local_bindings[i].descriptor_offset = 0; /* ignored */
        local_bindings[i].shader_visibility = vkd3d_shader_visibility_from_d3d12(sampler_desc[i].ShaderVisibility);
        local_bindings[i].flags = VKD3D_SHADER_BINDING_FLAG_IMAGE;
        local_bindings[i].binding.binding = j;
        local_bindings[i].binding.set = object->local_static_sampler.set_index;
    }

    *out_vk_bindings = vk_bindings;
    *out_vk_bindings_size = vk_bindings_size;
    *out_vk_bindings_count = vk_bindings_count;
}

static HRESULT d3d12_state_object_compile_pipeline(struct d3d12_state_object *object,
        struct d3d12_state_object_pipeline_data *data)
{
    const struct vkd3d_vk_device_procs *vk_procs = &object->device->vk_procs;
    struct vkd3d_shader_interface_local_info shader_interface_local_info;
    VkRayTracingPipelineInterfaceCreateInfoKHR interface_create_info;
    VkDescriptorSetLayoutBinding *local_static_sampler_bindings;
    struct vkd3d_shader_interface_info shader_interface_info;
    VkRayTracingPipelineCreateInfoKHR pipeline_create_info;
    struct vkd3d_shader_resource_binding *local_bindings;
    struct vkd3d_shader_compile_arguments compile_args;
    struct d3d12_state_object_collection *collection;
    VkPipelineDynamicStateCreateInfo dynamic_state;
    struct vkd3d_shader_library_entry_point *entry;
    struct d3d12_root_signature *global_signature;
    struct d3d12_root_signature *local_signature;
    const struct D3D12_HIT_GROUP_DESC *hit_group;
    struct d3d12_state_object_identifier *export;
    VkPipelineLibraryCreateInfoKHR library_info;
    size_t local_static_sampler_bindings_count;
    size_t local_static_sampler_bindings_size;
    VkPipelineShaderStageCreateInfo *stage;
    uint32_t pgroup_offset, pstage_offset;
    unsigned int num_groups_to_export;
    struct vkd3d_shader_code spirv;
    struct vkd3d_shader_code dxil;
    size_t i, j;
    VkResult vr;
    HRESULT hr;

    static const VkDynamicState dynamic_states[] = { VK_DYNAMIC_STATE_RAY_TRACING_PIPELINE_STACK_SIZE_KHR };

    memset(&compile_args, 0, sizeof(compile_args));
    compile_args.target_extensions = object->device->vk_info.shader_extensions;
    compile_args.target_extension_count = object->device->vk_info.shader_extension_count;
    compile_args.target = VKD3D_SHADER_TARGET_SPIRV_VULKAN_1_0;
    compile_args.quirks = &vkd3d_shader_quirk_info;

    /* TODO: Allow different root signatures per module. */
    memset(&shader_interface_info, 0, sizeof(shader_interface_info));
    shader_interface_info.min_ssbo_alignment = d3d12_device_get_ssbo_alignment(object->device);

    /* Effectively ignored. */
    shader_interface_info.stage = VK_SHADER_STAGE_ALL;
    shader_interface_info.xfb_info = NULL;

    global_signature = data->global_root_signature;

    if (global_signature)
    {
        shader_interface_info.flags = d3d12_root_signature_get_shader_interface_flags(global_signature);
        shader_interface_info.descriptor_tables.offset = global_signature->descriptor_table_offset;
        shader_interface_info.descriptor_tables.count = global_signature->descriptor_table_count;
        shader_interface_info.bindings = global_signature->bindings;
        shader_interface_info.binding_count = global_signature->binding_count;
        shader_interface_info.push_constant_buffers = global_signature->root_constants;
        shader_interface_info.push_constant_buffer_count = global_signature->root_constant_count;
        shader_interface_info.push_constant_ubo_binding = &global_signature->push_constant_ubo_binding;
        shader_interface_info.offset_buffer_binding = &global_signature->offset_buffer_binding;
#ifdef VKD3D_ENABLE_DESCRIPTOR_QA
        shader_interface_info.descriptor_qa_global_binding = &global_signature->descriptor_qa_global_info;
        shader_interface_info.descriptor_qa_heap_binding = &global_signature->descriptor_qa_heap_binding;
#endif
    }

    shader_interface_local_info.descriptor_size = VKD3D_RESOURCE_DESC_INCREMENT;

    local_static_sampler_bindings = NULL;
    local_static_sampler_bindings_count = 0;
    local_static_sampler_bindings_size = 0;
    object->local_static_sampler.set_index = global_signature ? global_signature->num_set_layouts : 0;

    for (i = 0; i < data->entry_points_count; i++)
    {
        entry = &data->entry_points[i];

        local_signature = d3d12_state_object_pipeline_data_get_local_root_signature(data, entry);
        local_bindings = NULL;

        if (local_signature)
        {
            shader_interface_local_info.local_root_parameters = local_signature->parameters;
            shader_interface_local_info.local_root_parameter_count = local_signature->parameter_count;
            shader_interface_local_info.shader_record_constant_buffers = local_signature->root_constants;
            shader_interface_local_info.shader_record_buffer_count = local_signature->root_constant_count;

            if (local_signature->static_sampler_count)
            {
                /* If we have static samplers, we need to add additional bindings. */
                shader_interface_local_info.binding_count = local_signature->binding_count + local_signature->static_sampler_count;
                local_bindings = vkd3d_malloc(sizeof(*local_bindings) * shader_interface_local_info.binding_count);
                shader_interface_local_info.bindings = local_bindings;

                d3d12_state_object_append_local_static_samplers(object,
                        &local_static_sampler_bindings,
                        &local_static_sampler_bindings_size, &local_static_sampler_bindings_count,
                        local_bindings,
                        local_signature->static_samplers_desc, local_signature->static_samplers,
                        local_signature->static_sampler_count);

                memcpy(local_bindings + local_signature->static_sampler_count, local_signature->bindings,
                        sizeof(*local_bindings) * local_signature->binding_count);
            }
            else
            {
                shader_interface_local_info.bindings = local_signature->bindings;
                shader_interface_local_info.binding_count = local_signature->binding_count;
            }

            /* Promote state which might only be active in local root signature. */
            shader_interface_info.flags |= d3d12_root_signature_get_shader_interface_flags(local_signature);
            if (local_signature->flags & (VKD3D_ROOT_SIGNATURE_USE_SSBO_OFFSET_BUFFER | VKD3D_ROOT_SIGNATURE_USE_TYPED_OFFSET_BUFFER))
                shader_interface_info.offset_buffer_binding = &local_signature->offset_buffer_binding;
        }
        else
            memset(&shader_interface_local_info, 0, sizeof(shader_interface_local_info));

        if (vkd3d_stage_is_global_group(entry->stage))
        {
            /* Directly export this as a group. */
            vkd3d_array_reserve((void **) &data->exports, &data->exports_size,
                    data->exports_count + 1, sizeof(*data->exports));

            export = &data->exports[data->exports_count];
            export->mangled_export = entry->mangled_entry_point;
            export->plain_export = entry->plain_entry_point;
            export->group_index = data->groups_count;
            export->general_stage_index = data->stages_count;
            export->closest_stage_index = VK_SHADER_UNUSED_KHR;
            export->anyhit_stage_index = VK_SHADER_UNUSED_KHR;
            export->intersection_stage_index = VK_SHADER_UNUSED_KHR;
            export->inherited_collection_index = -1;
            export->inherited_collection_export_index = 0;
            export->general_stage = entry->stage;
            entry->mangled_entry_point = NULL;
            entry->plain_entry_point = NULL;

            vkd3d_array_reserve((void **) &data->groups, &data->groups_size,
                    data->groups_count + 1, sizeof(*data->groups));

            d3d12_state_object_build_group_create_info(&data->groups[data->groups_count],
                    VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR,
                    export);

            data->exports_count++;
            data->groups_count++;
        }

        vkd3d_array_reserve((void **)&data->stages, &data->stages_size,
                data->stages_count + 1, sizeof(*data->stages));
        stage = &data->stages[data->stages_count];
        stage->sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stage->pNext = NULL;
        stage->flags = 0;
        stage->stage = entry->stage;
        stage->module = VK_NULL_HANDLE;
        stage->pName = "main";
        stage->pSpecializationInfo = NULL;

        memset(&dxil, 0, sizeof(dxil));
        memset(&spirv, 0, sizeof(spirv));

        /* TODO: If we're exporting multiple entry points from one DXIL library,
         * we can amortize the parsing cost. */
        dxil.code = data->dxil_libraries[entry->identifier]->DXILLibrary.pShaderBytecode;
        dxil.size = data->dxil_libraries[entry->identifier]->DXILLibrary.BytecodeLength;

        if (vkd3d_shader_compile_dxil_export(&dxil, entry->real_entry_point, &spirv,
                &shader_interface_info, &shader_interface_local_info, &compile_args) != VKD3D_OK)
        {
            ERR("Failed to convert DXIL export: %s\n", entry->real_entry_point);
            vkd3d_free(local_bindings);
            return E_OUTOFMEMORY;
        }

        vkd3d_free(local_bindings);
        if (!d3d12_device_validate_shader_meta(object->device, &spirv.meta))
            return E_INVALIDARG;

        stage->module = create_shader_module(object->device, spirv.code, spirv.size);

        if ((spirv.meta.flags & VKD3D_SHADER_META_FLAG_USES_SUBGROUP_SIZE) &&
                object->device->device_info.subgroup_size_control_features.subgroupSizeControl)
        {
            stage->flags |= VK_PIPELINE_SHADER_STAGE_CREATE_ALLOW_VARYING_SUBGROUP_SIZE_BIT_EXT;
        }

        vkd3d_shader_free_shader_code(&spirv);
        if (!stage->module)
            return E_OUTOFMEMORY;

        data->stages_count++;
    }

    for (i = 0; i < data->hit_groups_count; i++)
    {
        hit_group = data->hit_groups[i];

        /* Directly export this as a group. */
        vkd3d_array_reserve((void **) &data->exports, &data->exports_size,
                data->exports_count + 1, sizeof(*data->exports));

        export = &data->exports[data->exports_count];
        export->mangled_export = NULL;
        export->plain_export = vkd3d_wstrdup(hit_group->HitGroupExport);
        export->group_index = data->groups_count;
        export->general_stage = VK_SHADER_STAGE_ALL; /* ignored */

        export->general_stage_index = VK_SHADER_UNUSED_KHR;
        export->closest_stage_index =
                d3d12_state_object_pipeline_data_find_entry(data, hit_group->ClosestHitShaderImport);
        export->intersection_stage_index =
                d3d12_state_object_pipeline_data_find_entry(data, hit_group->IntersectionShaderImport);
        export->anyhit_stage_index =
                d3d12_state_object_pipeline_data_find_entry(data, hit_group->AnyHitShaderImport);

        if (hit_group->ClosestHitShaderImport && export->closest_stage_index == UINT32_MAX)
            return E_INVALIDARG;
        if (hit_group->IntersectionShaderImport && export->intersection_stage_index == UINT32_MAX)
            return E_INVALIDARG;
        if (hit_group->AnyHitShaderImport && export->anyhit_stage_index == UINT32_MAX)
            return E_INVALIDARG;

        vkd3d_array_reserve((void **) &data->groups, &data->groups_size,
                data->groups_count + 1, sizeof(*data->groups));

        d3d12_state_object_build_group_create_info(&data->groups[data->groups_count],
                hit_group->Type == D3D12_HIT_GROUP_TYPE_TRIANGLES ?
                        VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR :
                        VK_RAY_TRACING_SHADER_GROUP_TYPE_PROCEDURAL_HIT_GROUP_KHR,
                export);

        export->inherited_collection_index = -1;
        export->inherited_collection_export_index = 0;
        data->exports_count += 1;
        data->groups_count += 1;
    }

    /* Adding libraries will implicitly consume indices.
     * Need to keep track of this so we can assign the correct export.
     * This is relevant when querying shader group handles. */
    pstage_offset = data->stages_count;
    pgroup_offset = data->groups_count;

    for (i = 0; i < data->collections_count; i++)
    {
        collection = &data->collections[i];

        if (collection->num_exports)
            num_groups_to_export = collection->num_exports;
        else
            num_groups_to_export = collection->object->exports_count;

        for (j = 0; j < num_groups_to_export; j++)
        {
            const struct d3d12_state_object_identifier *input_export;
            if (collection->num_exports == 0)
            {
                vkd3d_array_reserve((void **)&data->exports, &data->exports_size,
                        data->exports_count + 1, sizeof(*data->exports));

                export = &data->exports[data->exports_count];
                memset(export, 0, sizeof(*export));

                input_export = &collection->object->exports[j];
                if (input_export->plain_export)
                    export->plain_export = vkd3d_wstrdup(input_export->plain_export);
                if (input_export->mangled_export)
                    export->mangled_export = vkd3d_wstrdup(input_export->mangled_export);
            }
            else
            {
                const WCHAR *original_export;
                const WCHAR *subtype = NULL;
                uint32_t index;

                if (collection->exports[j].ExportToRename)
                    original_export = collection->exports[j].ExportToRename;
                else
                    original_export = collection->exports[j].Name;

                index = d3d12_state_object_get_export_index(collection->object, original_export, &subtype);
                if (subtype || index == UINT32_MAX)
                {
                    /* If we import things, but don't use them, this can happen. Just ignore it. */
                    continue;
                }

                vkd3d_array_reserve((void **)&data->exports, &data->exports_size,
                        data->exports_count + 1, sizeof(*data->exports));

                export = &data->exports[data->exports_count];
                memset(export, 0, sizeof(*export));

                export->plain_export = vkd3d_wstrdup(collection->exports[j].Name);
                export->mangled_export = NULL;
                input_export = &collection->object->exports[index];
            }

            export->group_index = pgroup_offset + input_export->group_index;

            export->general_stage_index = input_export->general_stage_index;
            export->closest_stage_index = input_export->closest_stage_index;
            export->anyhit_stage_index = input_export->anyhit_stage_index;
            export->intersection_stage_index = input_export->intersection_stage_index;
            export->general_stage = input_export->general_stage;

            if (export->general_stage_index != VK_SHADER_UNUSED_KHR)
                export->general_stage_index += pstage_offset;
            if (export->closest_stage_index != VK_SHADER_UNUSED_KHR)
                export->closest_stage_index += pstage_offset;
            if (export->anyhit_stage_index != VK_SHADER_UNUSED_KHR)
                export->anyhit_stage_index += pstage_offset;
            if (export->intersection_stage_index != VK_SHADER_UNUSED_KHR)
                export->intersection_stage_index += pstage_offset;

            /* If we inherited from a real pipeline, we must observe the rules of AddToStateObject().
             * SBT pointer must be invariant as well as its contents.
             * Vulkan does not guarantee this, but we can validate and accept the pipeline if
             * implementation happens to satisfy this rule. */
            if (collection->object->type == D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE)
                export->inherited_collection_index = (int)i;
            else
                export->inherited_collection_index = -1;
            export->inherited_collection_export_index = input_export->group_index;

            data->exports_count += 1;
        }

        pgroup_offset += collection->object->exports_count;
        pstage_offset += collection->object->stages_count;
    }

    if (local_static_sampler_bindings_count)
    {
        if (FAILED(hr = vkd3d_create_descriptor_set_layout(object->device, 0, local_static_sampler_bindings_count,
                local_static_sampler_bindings, &object->local_static_sampler.set_layout)))
        {
            vkd3d_free(local_static_sampler_bindings);
            return hr;
        }

        vkd3d_free(local_static_sampler_bindings);

        if (global_signature)
        {
            if (FAILED(hr = d3d12_root_signature_create_local_static_samplers_layout(global_signature,
                    object->local_static_sampler.set_layout, &object->local_static_sampler.pipeline_layout)))
                return hr;
        }
        else
        {
            if (FAILED(hr = vkd3d_create_pipeline_layout(object->device, 1, &object->local_static_sampler.set_layout,
                    0, NULL, &object->local_static_sampler.pipeline_layout)))
                return hr;
        }

        if (FAILED(hr = vkd3d_sampler_state_allocate_descriptor_set(&object->device->sampler_state,
                object->device, object->local_static_sampler.set_layout,
                &object->local_static_sampler.desc_set, &object->local_static_sampler.desc_pool)))
            return hr;
    }

    pipeline_create_info.sType = VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR;
    pipeline_create_info.pNext = NULL;

    /* If we allow state object additions, we must first lower this pipeline to a library, and
     * then link it to itself so we can use it a library in subsequent PSO creations, but we
     * must also be able to trace rays from the library. */
    pipeline_create_info.flags = (object->type == D3D12_STATE_OBJECT_TYPE_COLLECTION ||
            (object->flags & D3D12_STATE_OBJECT_FLAG_ALLOW_STATE_OBJECT_ADDITIONS)) ?
            VK_PIPELINE_CREATE_LIBRARY_BIT_KHR : 0;

    /* FIXME: What if we have no global root signature? */
    if (!global_signature)
    {
        FIXME("No global root signature was found. Is this allowed?\n");
        return E_INVALIDARG;
    }

    /* TODO: What happens here if we have local static samplers with COLLECTIONS? :| */
    if (object->local_static_sampler.pipeline_layout)
        pipeline_create_info.layout = object->local_static_sampler.pipeline_layout;
    else
        pipeline_create_info.layout = global_signature->raygen.vk_pipeline_layout;

    pipeline_create_info.basePipelineHandle = VK_NULL_HANDLE;
    pipeline_create_info.basePipelineIndex = -1;
    pipeline_create_info.pGroups = data->groups;
    pipeline_create_info.groupCount = data->groups_count;
    pipeline_create_info.pStages = data->stages;
    pipeline_create_info.stageCount = data->stages_count;
    pipeline_create_info.pLibraryInfo = &library_info;
    pipeline_create_info.pLibraryInterface = &interface_create_info;
    pipeline_create_info.pDynamicState = &dynamic_state;
    pipeline_create_info.maxPipelineRayRecursionDepth = data->pipeline_config.MaxTraceRecursionDepth;

    if (data->pipeline_config.Flags & D3D12_RAYTRACING_PIPELINE_FLAG_SKIP_TRIANGLES)
        pipeline_create_info.flags |= VK_PIPELINE_CREATE_RAY_TRACING_SKIP_TRIANGLES_BIT_KHR;
    if (data->pipeline_config.Flags & D3D12_RAYTRACING_PIPELINE_FLAG_SKIP_PROCEDURAL_PRIMITIVES)
        pipeline_create_info.flags |= VK_PIPELINE_CREATE_RAY_TRACING_SKIP_AABBS_BIT_KHR;

    library_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LIBRARY_CREATE_INFO_KHR;
    library_info.pNext = NULL;
    library_info.libraryCount = data->vk_libraries_count;
    library_info.pLibraries = data->vk_libraries;

    interface_create_info.sType = VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_INTERFACE_CREATE_INFO_KHR;
    interface_create_info.pNext = NULL;
    interface_create_info.maxPipelineRayPayloadSize = data->shader_config->MaxPayloadSizeInBytes;
    interface_create_info.maxPipelineRayHitAttributeSize = data->shader_config->MaxAttributeSizeInBytes;

    if (data->pipeline_config.MaxTraceRecursionDepth >
            object->device->device_info.ray_tracing_pipeline_properties.maxRayRecursionDepth)
    {
        /* We cannot do anything about this, since we let sub-minspec devices through,
         * and this content actually tries to use recursion. */
        ERR("MaxTraceRecursionDepth %u exceeds device limit of %u.\n",
                data->pipeline_config.MaxTraceRecursionDepth,
                object->device->device_info.ray_tracing_pipeline_properties.maxRayRecursionDepth);
        return E_INVALIDARG;
    }

    dynamic_state.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic_state.pNext = NULL;
    dynamic_state.flags = 0;
    dynamic_state.dynamicStateCount = 1;
    dynamic_state.pDynamicStates = dynamic_states;

    vr = VK_CALL(vkCreateRayTracingPipelinesKHR(object->device->vk_device, VK_NULL_HANDLE,
            VK_NULL_HANDLE, 1, &pipeline_create_info, NULL,
            (pipeline_create_info.flags & VK_PIPELINE_CREATE_LIBRARY_BIT_KHR) ?
                    &object->pipeline_library : &object->pipeline));

    if (vr == VK_SUCCESS && (object->flags & D3D12_STATE_OBJECT_FLAG_ALLOW_STATE_OBJECT_ADDITIONS) &&
            object->type == D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE)
    {
        /* TODO: Is it actually valid to inherit other pipeline libraries while creating a pipeline library? */
        pipeline_create_info.flags &= ~VK_PIPELINE_CREATE_LIBRARY_BIT_KHR;
        pipeline_create_info.pStages = NULL;
        pipeline_create_info.pGroups = NULL;
        pipeline_create_info.stageCount = 0;
        pipeline_create_info.groupCount = 0;
        library_info.libraryCount = 1;
        library_info.pLibraries = &object->pipeline_library;

        /* Self-link the pipeline library. */
        vr = VK_CALL(vkCreateRayTracingPipelinesKHR(object->device->vk_device, VK_NULL_HANDLE,
                VK_NULL_HANDLE, 1, &pipeline_create_info, NULL, &object->pipeline));
    }

    if (vr)
        return hresult_from_vk_result(vr);

    if (object->type == D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE)
    {
        if (FAILED(hr = d3d12_state_object_get_group_handles(object, data)))
            return hr;

        object->pipeline_stack_size = d3d12_state_object_pipeline_data_compute_default_stack_size(data,
                &object->stack,
                pipeline_create_info.maxPipelineRayRecursionDepth);
    }
    else
        object->pipeline_stack_size = 0;

    /* Pilfer the export table. */
    object->exports = data->exports;
    object->exports_size = data->exports_size;
    object->exports_count = data->exports_count;
    data->exports = NULL;
    data->exports_size = 0;
    data->exports_count = 0;

    d3d12_root_signature_inc_ref(object->global_root_signature = global_signature);

    object->shader_config = *data->shader_config;
    object->pipeline_config = data->pipeline_config;

    /* Spec says we need to hold a reference to the collection object, but it doesn't show up in API,
     * so we must assume private reference. */
    if (data->collections_count)
    {
        object->collections = vkd3d_malloc(data->collections_count * sizeof(*object->collections));
        object->collections_count = data->collections_count;
        for (i = 0; i < data->collections_count; i++)
        {
            object->collections[i] = data->collections[i].object;
            d3d12_state_object_inc_ref(object->collections[i]);
        }
    }

    /* Always set this, since parent needs to be able to offset pStages[]. */
    object->stages_count = data->stages_count;

    /* If parent object can depend on individual shaders, keep the entry point list around. */
    if (object->flags & D3D12_STATE_OBJECT_FLAG_ALLOW_EXTERNAL_DEPENDENCIES_ON_LOCAL_DEFINITIONS)
    {
        object->entry_points = data->entry_points;
        object->entry_points_count = data->entry_points_count;
        data->entry_points = NULL;
        data->entry_points_count = 0;
    }

    return S_OK;
}

static HRESULT d3d12_state_object_init(struct d3d12_state_object *object,
        struct d3d12_device *device,
        const D3D12_STATE_OBJECT_DESC *desc,
        struct d3d12_state_object *parent)
{
    struct d3d12_state_object_pipeline_data data;
    HRESULT hr = S_OK;
    object->ID3D12StateObject_iface.lpVtbl = &d3d12_state_object_vtbl;
    object->ID3D12StateObjectProperties_iface.lpVtbl = &d3d12_state_object_properties_vtbl;
    object->refcount = 1;
    object->internal_refcount = 1;
    object->device = device;
    object->type = desc->Type;
    memset(&data, 0, sizeof(data));

    if (FAILED(hr = d3d12_state_object_parse_subobjects(object, desc, parent, &data)))
        goto fail;

    if (FAILED(hr = d3d12_state_object_compile_pipeline(object, &data)))
        goto fail;

    if (FAILED(hr = vkd3d_private_store_init(&object->private_store)))
        goto fail;

    d3d12_state_object_pipeline_data_cleanup(&data, object->device);
    d3d12_device_add_ref(object->device);
    return S_OK;

fail:
    d3d12_state_object_pipeline_data_cleanup(&data, object->device);
    d3d12_state_object_cleanup(object);
    return hr;
}

HRESULT d3d12_state_object_create(struct d3d12_device *device, const D3D12_STATE_OBJECT_DESC *desc,
        struct d3d12_state_object *parent,
        struct d3d12_state_object **state_object)
{
    struct d3d12_state_object *object;
    HRESULT hr;

    if (!(object = vkd3d_calloc(1, sizeof(*object))))
        return E_OUTOFMEMORY;

    hr = d3d12_state_object_init(object, device, desc, parent);
    if (FAILED(hr))
    {
        vkd3d_free(object);
        return hr;
    }

    *state_object = object;
    return S_OK;
}

HRESULT d3d12_state_object_add(struct d3d12_device *device, const D3D12_STATE_OBJECT_DESC *desc,
        struct d3d12_state_object *parent, struct d3d12_state_object **state_object)
{
    unsigned int i;
    HRESULT hr;

    if (!parent)
        return E_INVALIDARG;
    if (!(parent->flags & D3D12_STATE_OBJECT_FLAG_ALLOW_STATE_OBJECT_ADDITIONS))
        return E_INVALIDARG;
    if (desc->Type != D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE)
        return E_INVALIDARG;

    /* Addition must also allow this scenario. */
    for (i = 0; i < desc->NumSubobjects; i++)
    {
        if (desc->pSubobjects[i].Type == D3D12_STATE_SUBOBJECT_TYPE_STATE_OBJECT_CONFIG)
        {
            const D3D12_STATE_OBJECT_CONFIG *config = desc->pSubobjects[i].pDesc;
            if (config->Flags & D3D12_STATE_OBJECT_FLAG_ALLOW_STATE_OBJECT_ADDITIONS)
                break;
        }
    }

    if (i == desc->NumSubobjects)
        return E_INVALIDARG;

    hr = d3d12_state_object_create(device, desc, parent, state_object);
    return hr;
}
