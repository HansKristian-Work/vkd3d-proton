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
#include "vkd3d_descriptor_debug.h"

#define RT_TRACE TRACE

static inline struct d3d12_rt_state_object *impl_from_ID3D12StateObjectProperties(d3d12_state_object_properties_iface *iface)
{
    return CONTAINING_RECORD(iface, struct d3d12_rt_state_object, ID3D12StateObjectProperties1_iface);
}

static HRESULT STDMETHODCALLTYPE d3d12_state_object_QueryInterface(ID3D12StateObject *iface,
        REFIID riid, void **object)
{
    struct d3d12_rt_state_object *state_object = rt_impl_from_ID3D12StateObject(iface);

    TRACE("iface %p, riid %s, object %p.\n", iface, debugstr_guid(riid), object);

    if (!object)
        return E_POINTER;

    if (IsEqualGUID(riid, &IID_ID3D12StateObject)
        || IsEqualGUID(riid, &IID_ID3D12DeviceChild)
        || IsEqualGUID(riid, &IID_ID3D12Object)
        || IsEqualGUID(riid, &IID_IUnknown))
    {
        ID3D12StateObject_AddRef(iface);
        *object = iface;
        return S_OK;
    }

    if (IsEqualGUID(riid, &IID_ID3D12StateObjectProperties) || IsEqualGUID(riid, &IID_ID3D12StateObjectProperties1))
    {
        ID3D12StateObjectProperties1_AddRef(&state_object->ID3D12StateObjectProperties1_iface);
        *object = &state_object->ID3D12StateObjectProperties1_iface;
        return S_OK;
    }

    if (IsEqualGUID(riid, &IID_ID3DDestructionNotifier))
    {
        ID3DDestructionNotifier_AddRef(&state_object->destruction_notifier.ID3DDestructionNotifier_iface);
        *object = &state_object->destruction_notifier.ID3DDestructionNotifier_iface;
        return S_OK;
    }

    WARN("%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid(riid));

    *object = NULL;
    return E_NOINTERFACE;
}

static HRESULT STDMETHODCALLTYPE d3d12_state_object_properties_QueryInterface(d3d12_state_object_properties_iface *iface,
        REFIID riid, void **object)
{
    struct d3d12_rt_state_object *state_object = impl_from_ID3D12StateObjectProperties(iface);
    TRACE("iface %p, riid %s, object %p.\n", iface, debugstr_guid(riid), object);
    return d3d12_state_object_QueryInterface(&state_object->ID3D12StateObject_iface, riid, object);
}

static ULONG STDMETHODCALLTYPE d3d12_state_object_AddRef(ID3D12StateObject *iface)
{
    struct d3d12_rt_state_object *state_object = rt_impl_from_ID3D12StateObject(iface);
    ULONG refcount = InterlockedIncrement(&state_object->refcount);

    TRACE("%p increasing refcount to %u.\n", state_object, refcount);

    return refcount;
}

static ULONG STDMETHODCALLTYPE d3d12_state_object_properties_AddRef(d3d12_state_object_properties_iface *iface)
{
    struct d3d12_rt_state_object *state_object = impl_from_ID3D12StateObjectProperties(iface);
    ULONG refcount = InterlockedIncrement(&state_object->refcount);

    TRACE("%p increasing refcount to %u.\n", state_object, refcount);

    return refcount;
}

static void d3d12_state_object_cleanup(struct d3d12_rt_state_object *object);

static void d3d12_state_object_inc_ref(struct d3d12_rt_state_object *state_object)
{
    InterlockedIncrement(&state_object->internal_refcount);
}

static void d3d12_state_object_dec_ref(struct d3d12_rt_state_object *state_object)
{
    ULONG refcount = InterlockedDecrement(&state_object->internal_refcount);

    TRACE("%p decreasing internal refcount to %u.\n", state_object, refcount);

    if (!refcount)
    {
        struct d3d12_device *device = state_object->device;
        d3d_destruction_notifier_free(&state_object->destruction_notifier);
        vkd3d_private_store_destroy(&state_object->private_store);
        d3d12_state_object_cleanup(state_object);
        vkd3d_free(state_object);
        d3d12_device_release(device);
    }
}

static void d3d12_state_object_pipeline_data_cleanup(struct d3d12_rt_state_object_pipeline_data *data,
        struct d3d12_device *device);

static void d3d12_state_object_cleanup(struct d3d12_rt_state_object *object)
{
    const struct vkd3d_vk_device_procs *vk_procs = &object->device->vk_procs;
    struct d3d12_rt_state_object_variant *variant;
    size_t i;

    for (i = 0; i < object->exports_count; i++)
    {
        vkd3d_free(object->exports[i].mangled_export);
        vkd3d_free(object->exports[i].plain_export);
    }
    vkd3d_free(object->exports);
    object->exports = NULL;
    object->exports_count = 0;
    object->exports_size = 0;

    vkd3d_free(object->entry_points);
    object->entry_points = NULL;
    object->entry_points_count = 0;
    /* This is pilfered from data struct, and we don't copy the size. */

    for (i = 0; i < object->collections_count; i++)
        d3d12_state_object_dec_ref(object->collections[i]);
    vkd3d_free(object->collections);
    object->collections = NULL;
    object->collections_count = 0;
    /* This is pilfered from data struct, and we don't copy the size. */

    for (i = 0; i < object->pipelines_count; i++)
    {
        variant = &object->pipelines[i];
        VK_CALL(vkDestroyPipeline(object->device->vk_device, variant->pipeline, NULL));
        VK_CALL(vkDestroyPipeline(object->device->vk_device, variant->pipeline_library, NULL));
        if (variant->global_root_signature)
            d3d12_root_signature_dec_ref(variant->global_root_signature);

        if (variant->local_static_sampler.owned_handles)
        {
            VK_CALL(vkDestroyPipelineLayout(object->device->vk_device,
                    variant->local_static_sampler.pipeline_layout, NULL));
            VK_CALL(vkDestroyDescriptorSetLayout(object->device->vk_device,
                    variant->local_static_sampler.set_layout, NULL));
            if (variant->local_static_sampler.desc_set)
            {
                vkd3d_sampler_state_free_descriptor_set(&object->device->sampler_state, object->device,
                        variant->local_static_sampler.desc_set, variant->local_static_sampler.desc_pool);
            }
        }
    }
    vkd3d_free(object->pipelines);
    object->pipelines = NULL;
    object->pipelines_count = 0;
    object->pipelines_size = 0;

    if (object->deferred_data)
    {
        d3d12_state_object_pipeline_data_cleanup(object->deferred_data, object->device);
        vkd3d_free(object->deferred_data);
        object->deferred_data = NULL;
    }

#ifdef VKD3D_ENABLE_BREADCRUMBS
    vkd3d_free(object->breadcrumb_shaders);
    object->breadcrumb_shaders = NULL;
    object->breadcrumb_shaders_count = 0;
    object->breadcrumb_shaders_size = 0;
#endif
}

static ULONG d3d12_state_object_release(struct d3d12_rt_state_object *state_object)
{
    ULONG refcount = InterlockedDecrement(&state_object->refcount);

    TRACE("%p decreasing refcount to %u.\n", state_object, refcount);

    if (!refcount)
        d3d12_state_object_dec_ref(state_object);
    return refcount;
}

static ULONG STDMETHODCALLTYPE d3d12_state_object_Release(ID3D12StateObject *iface)
{
    struct d3d12_rt_state_object *state_object = rt_impl_from_ID3D12StateObject(iface);
    return d3d12_state_object_release(state_object);
}

static ULONG STDMETHODCALLTYPE d3d12_state_object_properties_Release(d3d12_state_object_properties_iface *iface)
{
    struct d3d12_rt_state_object *state_object = impl_from_ID3D12StateObjectProperties(iface);
    return d3d12_state_object_release(state_object);
}

static HRESULT STDMETHODCALLTYPE d3d12_state_object_GetPrivateData(ID3D12StateObject *iface,
        REFGUID guid, UINT *data_size, void *data)
{
    struct d3d12_rt_state_object *state_object = rt_impl_from_ID3D12StateObject(iface);

    TRACE("iface %p, guid %s, data_size %p, data %p.\n", iface, debugstr_guid(guid), data_size, data);

    return vkd3d_get_private_data(&state_object->private_store, guid, data_size, data);
}

static HRESULT STDMETHODCALLTYPE d3d12_state_object_SetPrivateData(ID3D12StateObject *iface,
        REFGUID guid, UINT data_size, const void *data)
{
    struct d3d12_rt_state_object *state_object = rt_impl_from_ID3D12StateObject(iface);

    TRACE("iface %p, guid %s, data_size %u, data %p.\n", iface, debugstr_guid(guid), data_size, data);

    return vkd3d_set_private_data(&state_object->private_store, guid, data_size, data,
            NULL, NULL);
}

static HRESULT STDMETHODCALLTYPE d3d12_state_object_SetPrivateDataInterface(ID3D12StateObject *iface,
        REFGUID guid, const IUnknown *data)
{
    struct d3d12_rt_state_object *state_object = rt_impl_from_ID3D12StateObject(iface);

    TRACE("iface %p, guid %s, data %p.\n", iface, debugstr_guid(guid), data);

    return vkd3d_set_private_data_interface(&state_object->private_store, guid, data,
            NULL, NULL);
}

static HRESULT STDMETHODCALLTYPE d3d12_state_object_GetDevice(ID3D12StateObject *iface,
        REFIID iid, void **device)
{
    struct d3d12_rt_state_object *state_object = rt_impl_from_ID3D12StateObject(iface);

    TRACE("iface %p, iid %s, device %p.\n", iface, debugstr_guid(iid), device);

    return d3d12_device_query_interface(state_object->device, iid, device);
}

static uint32_t d3d12_state_object_get_export_index(struct d3d12_rt_state_object *object,
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

static void * STDMETHODCALLTYPE d3d12_state_object_properties_GetShaderIdentifier(d3d12_state_object_properties_iface *iface,
        LPCWSTR export_name)
{
    struct d3d12_rt_state_object *object = impl_from_ID3D12StateObjectProperties(iface);
    struct d3d12_rt_state_object_identifier *export;
    const WCHAR *subtype = NULL;
    uint32_t index;

    RT_TRACE("iface %p, export_name %s.\n", iface, debugstr_w(export_name));

    if (object->type == D3D12_STATE_OBJECT_TYPE_COLLECTION &&
            !object->device->device_info.pipeline_library_group_handles_features.pipelineLibraryGroupHandles)
    {
        FIXME("Cannot query identifiers from COLLECTIONs.\n");
        return NULL;
    }

    index = d3d12_state_object_get_export_index(object, export_name, &subtype);

    /* Cannot query shader identifier for non-group names. */
    if (!subtype && index != UINT32_MAX)
    {
        export = &object->exports[index];
        /* Need to return the parent SBT pointer if it exists */
        while (export->inherited_collection_index >= 0)
        {
            RT_TRACE("  chaining into collection %d.\n", export->inherited_collection_index);
            object = object->collections[export->inherited_collection_index];
            export = &object->exports[export->inherited_collection_export_index];
        }
        RT_TRACE("  identifier { %016"PRIx64", %016"PRIx64", %016"PRIx64", %016"PRIx64 " }\n",
                *(const uint64_t*)(export->identifier + 0), *(const uint64_t*)(export->identifier + 8),
                *(const uint64_t*)(export->identifier + 16), *(const uint64_t*)(export->identifier + 24));
        return export->identifier;
    }
    else
    {
        ERR("Could not find entry point.\n");
        return NULL;
    }
}

static UINT64 STDMETHODCALLTYPE d3d12_state_object_properties_GetShaderStackSize(d3d12_state_object_properties_iface *iface,
        LPCWSTR export_name)
{
    struct d3d12_rt_state_object *object = impl_from_ID3D12StateObjectProperties(iface);
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

static UINT64 STDMETHODCALLTYPE d3d12_state_object_properties_GetPipelineStackSize(d3d12_state_object_properties_iface *iface)
{
    struct d3d12_rt_state_object *object = impl_from_ID3D12StateObjectProperties(iface);
    TRACE("iface %p\n", iface);
    return object->pipeline_stack_size;
}

static void STDMETHODCALLTYPE d3d12_state_object_properties_SetPipelineStackSize(d3d12_state_object_properties_iface *iface,
        UINT64 stack_size_in_bytes)
{
    struct d3d12_rt_state_object *object = impl_from_ID3D12StateObjectProperties(iface);
    TRACE("iface %p, stack_size_in_bytes %llu!\n", iface, (unsigned long long)stack_size_in_bytes);

    /* This behavior seems to match what I'm seeing on NV Windows driver. */
    object->pipeline_stack_size = stack_size_in_bytes;
}

static D3D12_PROGRAM_IDENTIFIER * STDMETHODCALLTYPE d3d12_state_object_properties_GetProgramIdentifier(
        d3d12_state_object_properties_iface *iface,
        D3D12_PROGRAM_IDENTIFIER *ret,
        LPCWSTR pProgramName)
{
    TRACE("iface %p, ret %p, pProgramName %s!\n", iface, ret, debugstr_w(pProgramName));
    memset(ret, 0, sizeof(*ret));
    return ret;
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

static CONST_VTBL struct ID3D12StateObjectProperties1Vtbl d3d12_state_object_properties_vtbl =
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
    /* ID3D12StateObjectProperties1 methods */
    d3d12_state_object_properties_GetProgramIdentifier,
};

struct d3d12_state_object_collection
{
    struct d3d12_rt_state_object *object;
    unsigned int num_exports;
    const D3D12_EXPORT_DESC *exports;
};

struct d3d12_rt_state_object_pipeline_data
{
    /* Map 1:1 with VkShaderModule. */
    struct vkd3d_shader_library_entry_point *entry_points;
    size_t entry_points_size;
    size_t entry_points_count;

    struct vkd3d_shader_library_subobject *subobjects;
    size_t subobjects_size;
    size_t subobjects_count;

    struct d3d12_root_signature **subobject_root_signatures;
    size_t subobject_root_signatures_size;
    size_t subobject_root_signatures_count;

    /* Resolve these to group + export name later. */
    const struct D3D12_HIT_GROUP_DESC **hit_groups;
    size_t hit_groups_size;
    size_t hit_groups_count;

    /* Used to finalize compilation later. */
    const struct D3D12_DXIL_LIBRARY_DESC **dxil_libraries;
    size_t dxil_libraries_size;
    size_t dxil_libraries_count;

    /* Maps 1:1 to groups. */
    struct d3d12_rt_state_object_identifier *exports;
    size_t exports_size;
    size_t exports_count;

    /* Each group in Vulkan is either a generic group or hit group. */
    VkRayTracingShaderGroupCreateInfoKHR *groups;
    size_t groups_size;
    size_t groups_count;

    struct d3d12_state_object_association *associations;
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

    struct vkd3d_shader_debug_ring_spec_info *spec_info_buffer;
    bool has_deep_duplication;
};

static void d3d12_state_object_pipeline_data_cleanup_modules(struct d3d12_rt_state_object_pipeline_data *data,
        struct d3d12_device *device)
{
    unsigned int i;
    for (i = 0; i < data->stages_count; i++)
    {
        const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
        VK_CALL(vkDestroyShaderModule(device->vk_device, data->stages[i].module, NULL));
    }
    data->stages_count = 0;
}

static void d3d12_state_object_pipeline_data_cleanup_compile_temporaries(struct d3d12_rt_state_object_pipeline_data *data,
        struct d3d12_device *device)
{
    unsigned int i;

    for (i = 0; i < data->subobject_root_signatures_count; i++)
        d3d12_root_signature_dec_ref(data->subobject_root_signatures[i]);
    vkd3d_free(data->subobject_root_signatures);
    data->subobject_root_signatures = NULL;
    data->subobject_root_signatures_count = 0;
    data->subobject_root_signatures_size = 0;

    for (i = 0; i < data->exports_count; i++)
    {
        vkd3d_free(data->exports[i].mangled_export);
        vkd3d_free(data->exports[i].plain_export);
    }
    vkd3d_free(data->exports);
    data->exports = NULL;
    data->exports_count = 0;
    data->exports_size = 0;

    vkd3d_free(data->groups);
    data->groups = NULL;
    data->groups_count = 0;
    data->groups_size = 0;

    d3d12_state_object_pipeline_data_cleanup_modules(data, device);
    vkd3d_free(data->stages);
    data->stages = 0;
    data->stages_count = 0;
    data->stages_size = 0;

    vkd3d_free(data->collections);
    data->collections = NULL;
    data->collections_count = 0;
    data->collections_size = 0;

    vkd3d_free(data->vk_libraries);
    data->vk_libraries = NULL;
    data->vk_libraries_count = 0;
    data->vk_libraries_size = 0;

    vkd3d_free(data->spec_info_buffer);
    data->spec_info_buffer = NULL;
}

static void d3d12_state_object_pipeline_data_cleanup(struct d3d12_rt_state_object_pipeline_data *data,
        struct d3d12_device *device)
{
    unsigned int i;

    vkd3d_shader_dxil_free_library_entry_points(data->entry_points, data->entry_points_count);
    vkd3d_shader_dxil_free_library_subobjects(data->subobjects, data->subobjects_count);

    if (data->has_deep_duplication)
    {
        /* TODO: Should consider a linear allocator here so we can just yoink,
         * but only one known game hits this case, so ... eh. */
        for (i = 0; i < data->hit_groups_count; i++)
        {
            vkd3d_free((void*)data->hit_groups[i]->AnyHitShaderImport);
            vkd3d_free((void*)data->hit_groups[i]->ClosestHitShaderImport);
            vkd3d_free((void*)data->hit_groups[i]->IntersectionShaderImport);
            vkd3d_free((void*)data->hit_groups[i]->HitGroupExport);
            vkd3d_free((void*)data->hit_groups[i]);
        }

        for (i = 0; i < data->dxil_libraries_count; i++)
        {
            vkd3d_free((void*)data->dxil_libraries[i]->DXILLibrary.pShaderBytecode);
            vkd3d_free((void*)data->dxil_libraries[i]);
        }
    }
    vkd3d_free((void*)data->hit_groups);
    vkd3d_free((void*)data->dxil_libraries);

    for (i = 0; i < data->associations_count; i++)
    {
        if ((data->associations[i].kind == VKD3D_SHADER_SUBOBJECT_KIND_GLOBAL_ROOT_SIGNATURE ||
                data->associations[i].kind == VKD3D_SHADER_SUBOBJECT_KIND_LOCAL_ROOT_SIGNATURE) &&
                data->associations[i].root_signature)
        {
            d3d12_root_signature_dec_ref(data->associations[i].root_signature);
        }

        if (data->has_deep_duplication)
            vkd3d_free((void*)data->associations[i].export);
    }

    vkd3d_free(data->associations);

    d3d12_state_object_pipeline_data_cleanup_compile_temporaries(data, device);
}

static void *dup_memory(const void *data, size_t size)
{
    /* TODO: Should consider a linear allocator here so we can just yoink,
     * but only one known game hits this case, so ... eh. */
    void *new_data = vkd3d_malloc(size);
    if (new_data)
        memcpy(new_data, data, size);
    return new_data;
}

static struct d3d12_rt_state_object_pipeline_data *d3d12_state_object_pipeline_data_defer(
        struct d3d12_rt_state_object_pipeline_data *old_data,
        struct d3d12_device *device)
{
    struct d3d12_rt_state_object_pipeline_data *data;
    unsigned int i;

    /* hit_groups and dxil_libraries need deep copies since they currently only reference app-provided memory. */
    data = dup_memory(old_data, sizeof(*old_data));
    data->has_deep_duplication = true;

    for (i = 0; i < data->hit_groups_count; i++)
    {
        D3D12_HIT_GROUP_DESC *desc;
        data->hit_groups[i] = dup_memory(data->hit_groups[i], sizeof(*data->hit_groups[i]));
        desc = (D3D12_HIT_GROUP_DESC *)data->hit_groups[i];
        if (desc->HitGroupExport)
            desc->HitGroupExport = vkd3d_wstrdup(desc->HitGroupExport);
        if (desc->AnyHitShaderImport)
            desc->AnyHitShaderImport = vkd3d_wstrdup(desc->AnyHitShaderImport);
        if (desc->ClosestHitShaderImport)
            desc->ClosestHitShaderImport = vkd3d_wstrdup(desc->ClosestHitShaderImport);
        if (desc->IntersectionShaderImport)
            desc->IntersectionShaderImport = vkd3d_wstrdup(desc->IntersectionShaderImport);
    }

    for (i = 0; i < data->dxil_libraries_count; i++)
    {
        D3D12_DXIL_LIBRARY_DESC *desc;
        data->dxil_libraries[i] = dup_memory(data->dxil_libraries[i], sizeof(*data->dxil_libraries[i]));
        desc = (D3D12_DXIL_LIBRARY_DESC *)data->dxil_libraries[i];

        desc->DXILLibrary.pShaderBytecode = dup_memory(desc->DXILLibrary.pShaderBytecode, desc->DXILLibrary.BytecodeLength);
        /* We have already parsed the module entry points and subobjects.
         * Only thing we need to do is to keep the raw DXIL alive for later. */
        desc->NumExports = 0;
        desc->pExports = NULL;
    }

    for (i = 0; i < data->associations_count; i++)
        if (data->associations[i].export)
            data->associations[i].export = vkd3d_wstrdup(data->associations[i].export);

    /* Some data structures are not needed after we have parsed the RTPSO.
     * Clean them up early, so we're not wasting memory. */
    d3d12_state_object_pipeline_data_cleanup_compile_temporaries(data, device);

    return data;
}

static HRESULT d3d12_state_object_add_collection_library(
        struct d3d12_rt_state_object *collection,
        struct d3d12_rt_state_object_pipeline_data *data,
        const D3D12_EXPORT_DESC *exports, unsigned int num_exports)
{
    VKD3D_UNUSED size_t i;

    if (!vkd3d_array_reserve((void **)&data->collections, &data->collections_size,
            data->collections_count + 1, sizeof(*data->collections)))
        return E_OUTOFMEMORY;

    if (!vkd3d_array_reserve((void **)&data->associations, &data->associations_size,
            data->associations_count + 2, sizeof(*data->associations)))
        return E_OUTOFMEMORY;

    RT_TRACE("EXISTING_COLLECTION %p (library):\n", (void *)collection);
    for (i = 0; i < collection->exports_count; i++)
    {
        if (collection->exports[i].plain_export)
            RT_TRACE("  Plain export: %s\n", debugstr_w(collection->exports[i].plain_export));
        if (collection->exports[i].mangled_export)
            RT_TRACE("  Mangled export: %s\n", debugstr_w(collection->exports[i].mangled_export));
    }

    data->associations[data->associations_count].kind = VKD3D_SHADER_SUBOBJECT_KIND_RAYTRACING_PIPELINE_CONFIG1;
    data->associations[data->associations_count].pipeline_config = collection->pipeline_config;
    data->associations[data->associations_count].priority = VKD3D_ASSOCIATION_PRIORITY_INHERITED_COLLECTION;
    data->associations[data->associations_count].export = NULL;
    data->associations_count++;

    data->associations[data->associations_count].kind = VKD3D_SHADER_SUBOBJECT_KIND_RAYTRACING_SHADER_CONFIG;
    data->associations[data->associations_count].shader_config = collection->shader_config;
    data->associations[data->associations_count].priority = VKD3D_ASSOCIATION_PRIORITY_INHERITED_COLLECTION;
    data->associations[data->associations_count].export = NULL;
    data->associations_count++;

    data->collections[data->collections_count].object = collection;
    data->collections[data->collections_count].num_exports = num_exports;
    data->collections[data->collections_count].exports = exports;

    data->collections_count += 1;
    return S_OK;
}

static HRESULT d3d12_state_object_add_collection_deferred(
        struct d3d12_rt_state_object_pipeline_data *deferred,
        struct d3d12_rt_state_object_pipeline_data *data,
        const D3D12_EXPORT_DESC *exports, unsigned int num_exports)
{
    VKD3D_UNUSED size_t i, j;

    /* Unlike with library linking, we have to only include objects that are exported,
     * since they will be considered part of the main object, and we'll have no way of "hiding"
     * unwanted exports unless we filter them here.
     * For libraries, we filter them by using data->collections[] export list. */

    for (i = 0; i < deferred->entry_points_count; i++)
    {
        const struct vkd3d_shader_library_entry_point *entry = &deferred->entry_points[i];
        struct vkd3d_shader_library_entry_point *dup_entry;
        const D3D12_EXPORT_DESC *export_desc = NULL;
        bool accept_entry_point = false;

        if (!num_exports)
        {
            accept_entry_point = true;
        }
        else
        {
            for (j = 0; j < num_exports; j++)
                if (vkd3d_export_equal(exports[i].ExportToRename ? exports[i].ExportToRename : exports[i].Name, entry))
                    break;

            if (j < num_exports)
            {
                export_desc = &exports[i];
                accept_entry_point = true;
            }
        }

        if (accept_entry_point)
        {
            if (!vkd3d_array_reserve((void **)&data->entry_points, &data->entry_points_size,
                    data->entry_points_count + 1, sizeof(*data->entry_points)))
                return E_OUTOFMEMORY;

            if (!vkd3d_array_reserve((void **)&data->dxil_libraries, &data->dxil_libraries_size,
                    data->dxil_libraries_count + 1, sizeof(*data->dxil_libraries)))
                return E_OUTOFMEMORY;

            data->dxil_libraries[data->dxil_libraries_count] = deferred->dxil_libraries[entry->identifier];

            dup_entry = &data->entry_points[data->entry_points_count];
            *dup_entry = *entry;
            dup_entry->identifier = data->dxil_libraries_count;

            /* Reset these values so that they get compiled properly. They might have been clobbered
             * while attempting to compile the old COLLECTION. */
            dup_entry->pipeline_variant_index = UINT32_MAX;
            dup_entry->stage_index = UINT32_MAX;

            if (dup_entry->real_entry_point)
                dup_entry->real_entry_point = vkd3d_strdup(dup_entry->real_entry_point);
            if (dup_entry->debug_entry_point)
                dup_entry->debug_entry_point = vkd3d_strdup(dup_entry->debug_entry_point);

            if (export_desc)
            {
                dup_entry->mangled_entry_point = NULL;
                dup_entry->plain_entry_point = vkd3d_wstrdup(export_desc->Name);
            }
            else
            {
                /* Forward the names directly if we exported everything. */
                if (dup_entry->plain_entry_point)
                    dup_entry->plain_entry_point = vkd3d_wstrdup(dup_entry->plain_entry_point);
                if (dup_entry->mangled_entry_point)
                    dup_entry->mangled_entry_point = vkd3d_wstrdup(dup_entry->mangled_entry_point);
            }

            data->dxil_libraries_count++;
            data->entry_points_count++;
        }
    }

    for (i = 0; i < deferred->hit_groups_count; i++)
    {
        bool accept_hit_group = false;

        if (!num_exports)
        {
            accept_hit_group = true;
        }
        else
        {
            for (j = 0; j < num_exports; j++)
            {
                if (exports[i].ExportToRename)
                    FIXME("Cannot rename deferred COLLECTION hit group export name.\n");

                if (vkd3d_export_strequal(
                        exports[i].ExportToRename ? exports[i].ExportToRename : exports[i].Name,
                        deferred->hit_groups[i]->HitGroupExport))
                {
                    break;
                }
            }

            if (j < num_exports)
                accept_hit_group = true;
        }

        if (accept_hit_group)
        {
            if (!vkd3d_array_reserve((void **)&data->hit_groups, &data->hit_groups_size,
                    data->hit_groups_count + 1, sizeof(*data->hit_groups)))
                return E_OUTOFMEMORY;

            /* If exports[i].ExportToRename is used, we need a way to partially dup hit group desc.
             * This is esoteric enough that we'll defer until needed. */
            data->hit_groups[data->hit_groups_count++] = deferred->hit_groups[i];
        }
    }

    for (i = 0; i < deferred->subobjects_count; i++)
    {
        const D3D12_EXPORT_DESC *export_desc = NULL;
        bool accept_subobject = false;

        if (!num_exports)
        {
            accept_subobject = true;
        }
        else
        {
            for (j = 0; j < num_exports; j++)
            {
                if (exports[i].ExportToRename)
                    FIXME("Cannot rename deferred COLLECTION subobject export name.\n");

                if (vkd3d_export_strequal(
                        exports[i].ExportToRename ? exports[i].ExportToRename : exports[i].Name,
                        deferred->subobjects[i].name))
                {
                    break;
                }
            }

            if (j < num_exports)
            {
                accept_subobject = true;
                export_desc = &exports[i];
            }
        }

        if (accept_subobject)
        {
            struct vkd3d_shader_library_subobject *dup;
            if (!vkd3d_array_reserve((void **)&data->subobjects, &data->subobjects_size,
                    data->subobjects_count + 1, sizeof(*data->subobjects)))
                return E_OUTOFMEMORY;

            dup = &data->subobjects[data->subobjects_count++];
            *dup = deferred->subobjects[i];
            dup->name = vkd3d_wstrdup(export_desc ? export_desc->Name : dup->name);
            dup->borrowed_payloads = true;
        }
    }

    /* We will inherit all associations. There shouldn't be any problem here since
     * adding associations do not imply more exports being made, we'll just have associations that lead nowhere.
     * However, if an association was considered non-explicit in the collection, consider it INHERITED_COLLECTION here.
     * This way, we ensure that unrelated associations that were made in the COLLECTION don't conflict with
     * any DEFAULT association in the proper RTPSO. Explicit associations only apply to
     * specific exports and should not be able to cause conflicts. */
    if (!vkd3d_array_reserve((void **)&data->associations, &data->associations_size,
            data->associations_count + deferred->associations_count, sizeof(*data->associations)))
        return E_OUTOFMEMORY;

    for (i = 0; i < deferred->associations_count; i++)
    {
        struct d3d12_state_object_association *dup = &data->associations[data->associations_count++];
        *dup = deferred->associations[i];

        if ((dup->kind == VKD3D_SHADER_SUBOBJECT_KIND_LOCAL_ROOT_SIGNATURE ||
                dup->kind == VKD3D_SHADER_SUBOBJECT_KIND_GLOBAL_ROOT_SIGNATURE) &&
                dup->root_signature)
        {
            d3d12_root_signature_inc_ref(dup->root_signature);
        }

        if (dup->priority != VKD3D_ASSOCIATION_PRIORITY_EXPLICIT &&
                dup->priority != VKD3D_ASSOCIATION_PRIORITY_DXIL_SUBOBJECT_ASSIGNMENT_EXPLICIT)
            dup->priority = VKD3D_ASSOCIATION_PRIORITY_INHERITED_COLLECTION;
    }

    /* Do not need to hold reference to the original collection since we effectively dup all data we need. */
    return S_OK;
}

static HRESULT d3d12_state_object_add_collection(
        struct d3d12_rt_state_object *collection,
        struct d3d12_rt_state_object_pipeline_data *data,
        const D3D12_EXPORT_DESC *exports, unsigned int num_exports)
{
    if (collection->deferred_data)
        return d3d12_state_object_add_collection_deferred(collection->deferred_data, data, exports, num_exports);
    else
        return d3d12_state_object_add_collection_library(collection, data, exports, num_exports);
}

static void d3d12_state_object_set_association_data(struct d3d12_state_object_association *association,
        const D3D12_STATE_SUBOBJECT *object)
{
    union
    {
        const D3D12_RAYTRACING_PIPELINE_CONFIG1 *pipeline_config1;
        const D3D12_GLOBAL_ROOT_SIGNATURE *global_root_signature;
        const D3D12_RAYTRACING_PIPELINE_CONFIG *pipeline_config;
        const D3D12_LOCAL_ROOT_SIGNATURE *local_root_signature;
        const D3D12_RAYTRACING_SHADER_CONFIG *shader_config;
    } types;

    switch (object->Type)
    {
        case D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE:
            association->kind = VKD3D_SHADER_SUBOBJECT_KIND_GLOBAL_ROOT_SIGNATURE;
            types.global_root_signature = object->pDesc;
            association->root_signature =
                    impl_from_ID3D12RootSignature(types.global_root_signature->pGlobalRootSignature);
            if (association->root_signature)
                d3d12_root_signature_inc_ref(association->root_signature);
            break;

        case D3D12_STATE_SUBOBJECT_TYPE_LOCAL_ROOT_SIGNATURE:
            association->kind = VKD3D_SHADER_SUBOBJECT_KIND_LOCAL_ROOT_SIGNATURE;
            types.local_root_signature = object->pDesc;
            association->root_signature =
                    impl_from_ID3D12RootSignature(types.local_root_signature->pLocalRootSignature);
            if (association->root_signature)
                d3d12_root_signature_inc_ref(association->root_signature);
            break;

        case D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG:
            association->kind = VKD3D_SHADER_SUBOBJECT_KIND_RAYTRACING_SHADER_CONFIG;
            types.shader_config = object->pDesc;
            association->shader_config = *types.shader_config;
            break;

        case D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG1:
            association->kind = VKD3D_SHADER_SUBOBJECT_KIND_RAYTRACING_PIPELINE_CONFIG1;
            types.pipeline_config1 = object->pDesc;
            association->pipeline_config = *types.pipeline_config1;
            break;

        case D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG:
            association->kind = VKD3D_SHADER_SUBOBJECT_KIND_RAYTRACING_PIPELINE_CONFIG1;
            types.pipeline_config = object->pDesc;
            association->pipeline_config.MaxTraceRecursionDepth = types.pipeline_config->MaxTraceRecursionDepth;
            association->pipeline_config.Flags = 0;
            break;

        default:
            assert(0 && "Unreachable.");
            break;
    }
}

static HRESULT d3d12_state_object_parse_subobject(struct d3d12_rt_state_object *object,
        const D3D12_STATE_SUBOBJECT *obj,
        struct d3d12_rt_state_object_pipeline_data *data,
        unsigned int association_priority)
{
    unsigned int i;
    HRESULT hr;

    switch (obj->Type)
    {
        case D3D12_STATE_SUBOBJECT_TYPE_STATE_OBJECT_CONFIG:
        {
            /* TODO: We might have to do global object assignment similar to SHADER_CONFIG / PIPELINE_CONFIG,
             * but STATE_OBJECT_CONFIG doesn't change any functionality or compatibility rules really,
             * so just append flags. */
            const uint32_t supported_flags =
                    D3D12_STATE_OBJECT_FLAG_ALLOW_LOCAL_DEPENDENCIES_ON_EXTERNAL_DEFINITIONS |
                    D3D12_STATE_OBJECT_FLAG_ALLOW_EXTERNAL_DEPENDENCIES_ON_LOCAL_DEFINITIONS |
                    D3D12_STATE_OBJECT_FLAG_ALLOW_STATE_OBJECT_ADDITIONS;
            const D3D12_STATE_OBJECT_CONFIG *object_config = obj->pDesc;
            object->flags |= object_config->Flags;
            if (object->flags & ~supported_flags)
            {
                FIXME("Object config flag #%x is not supported.\n", object->flags);
                return E_INVALIDARG;
            }

            /* Need to ignore these flags on RTPSOs to avoid us doing funny things. */
            if (object->type == D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE)
            {
                object->flags &= ~(D3D12_STATE_OBJECT_FLAG_ALLOW_LOCAL_DEPENDENCIES_ON_EXTERNAL_DEFINITIONS |
                        D3D12_STATE_OBJECT_FLAG_ALLOW_EXTERNAL_DEPENDENCIES_ON_LOCAL_DEFINITIONS);
            }

            RT_TRACE("%p || STATE_OBJECT_CONFIG: #%x.\n", obj->pDesc, object_config->Flags);
            break;
        }

        case D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE:
        case D3D12_STATE_SUBOBJECT_TYPE_LOCAL_ROOT_SIGNATURE:
        {
            /* LOCAL_ROOT_SIGNATURE and GLOBAL_ROOT_SIGNATURE alias. */
            const D3D12_LOCAL_ROOT_SIGNATURE *rs = obj->pDesc;

            /* This is only chosen as default if there is nothing else.
             * Conflicting definitions seem to cause runtime to choose something
             * arbitrary. Just override the low priority default.
             * A high priority default association takes precedence if it exists. */
            vkd3d_array_reserve((void **)&data->associations, &data->associations_size,
                    data->associations_count + 1,
                    sizeof(*data->associations));

            /* Root signatures being exported to NULL takes priority as the default local RS.
             * They do however, take precedence over DXIL exported subobjects ... */
            data->associations[data->associations_count].export = NULL;
            data->associations[data->associations_count].kind =
                    obj->Type == D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE ?
                            VKD3D_SHADER_SUBOBJECT_KIND_GLOBAL_ROOT_SIGNATURE :
                            VKD3D_SHADER_SUBOBJECT_KIND_LOCAL_ROOT_SIGNATURE;
            data->associations[data->associations_count].root_signature =
                    impl_from_ID3D12RootSignature(rs->pLocalRootSignature);
            data->associations[data->associations_count].priority = association_priority;

            RT_TRACE("%p || %s (hash %016"PRIx64") (layout hash %016"PRIx64") (prio %u).\n",
                    obj->pDesc,
                    obj->Type == D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE ?
                            "GLOBAL_ROOT_SIGNATURE" : "LOCAL_ROOT_SIGNATURE",
                    data->associations[data->associations_count].root_signature->pso_compatibility_hash,
                    data->associations[data->associations_count].root_signature->layout_compatibility_hash,
                    association_priority);

            /* Hold reference in case we need to duplicate the data structure due to compile defer. */
            if (data->associations[data->associations_count].root_signature)
                d3d12_root_signature_inc_ref(data->associations[data->associations_count].root_signature);
            data->associations_count++;
            break;
        }

        case D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY:
        {
            const D3D12_DXIL_LIBRARY_DESC *lib = obj->pDesc;
            VKD3D_UNUSED size_t old_obj_count, old_count, j;

            old_count = data->entry_points_count;
            old_obj_count = data->subobjects_count;

            if (vkd3d_shader_dxil_append_library_entry_points_and_subobjects(lib, data->dxil_libraries_count,
                    &data->entry_points, &data->entry_points_size,
                    &data->entry_points_count,
                    &data->subobjects, &data->subobjects_size,
                    &data->subobjects_count) != VKD3D_OK)
            {
                ERR("Failed to parse DXIL library.\n");
                return E_OUTOFMEMORY;
            }
            vkd3d_array_reserve((void**)&data->dxil_libraries, &data->dxil_libraries_size,
                    data->dxil_libraries_count + 1, sizeof(*data->dxil_libraries));
            data->dxil_libraries[data->dxil_libraries_count++] = lib;

            RT_TRACE("Adding DXIL library:\n");
            for (j = old_count; j < data->entry_points_count; j++)
            {
                RT_TRACE("  Entry point: %s (%s) (stage #%x).\n",
                        data->entry_points[j].real_entry_point,
                        data->entry_points[j].debug_entry_point,
                        data->entry_points[j].stage);
            }

            for (j = old_obj_count; j < data->subobjects_count; j++)
                RT_TRACE("  RDAT subobject: %s (type #%x).\n", debugstr_w(data->subobjects[j].name), data->subobjects[j].kind);
            break;
        }

        case D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP:
        {
            const D3D12_HIT_GROUP_DESC *group = obj->pDesc;
            vkd3d_array_reserve((void**)&data->hit_groups, &data->hit_groups_size,
                    data->hit_groups_count + 1, sizeof(*data->hit_groups));
            data->hit_groups[data->hit_groups_count++] = group;
            RT_TRACE("Adding HIT_GROUP:\n");
            RT_TRACE("  Type: %s\n", group->Type == D3D12_HIT_GROUP_TYPE_TRIANGLES ? "Triangle" : "Procedural");
            RT_TRACE("  Name: %s\n", debugstr_w(group->HitGroupExport));
            RT_TRACE("  AnyHit: %s\n", debugstr_w(group->AnyHitShaderImport));
            RT_TRACE("  ClosestHit: %s\n", debugstr_w(group->ClosestHitShaderImport));
            RT_TRACE("  Intersection: %s\n", debugstr_w(group->IntersectionShaderImport));
            break;
        }

        case D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG:
        {
            const D3D12_RAYTRACING_SHADER_CONFIG *config = obj->pDesc;

            vkd3d_array_reserve((void **)&data->associations, &data->associations_size,
                    data->associations_count + 1,
                    sizeof(*data->associations));

            data->associations[data->associations_count].kind = VKD3D_SHADER_SUBOBJECT_KIND_RAYTRACING_SHADER_CONFIG;
            data->associations[data->associations_count].priority = association_priority;
            data->associations[data->associations_count].shader_config = *config;
            data->associations[data->associations_count].export = NULL;
            RT_TRACE("%p || Adding SHADER_CONFIG: MaxPayloadSize = %u, MaxAttributeSize = %u\n",
                    obj->pDesc,
                    config->MaxPayloadSizeInBytes, config->MaxAttributeSizeInBytes);
            data->associations_count++;
            break;
        }

        case D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG:
        {
            const D3D12_RAYTRACING_PIPELINE_CONFIG *pipeline_config = obj->pDesc;

            vkd3d_array_reserve((void **)&data->associations, &data->associations_size,
                    data->associations_count + 1,
                    sizeof(*data->associations));

            data->associations[data->associations_count].kind = VKD3D_SHADER_SUBOBJECT_KIND_RAYTRACING_PIPELINE_CONFIG1;
            data->associations[data->associations_count].priority = association_priority;
            data->associations[data->associations_count].pipeline_config.MaxTraceRecursionDepth =
                    pipeline_config->MaxTraceRecursionDepth;
            data->associations[data->associations_count].pipeline_config.Flags = 0;
            data->associations[data->associations_count].export = NULL;
            RT_TRACE("%p || Adding PIPELINE_CONFIG: MaxRecursion = %u\n", obj->pDesc, pipeline_config->MaxTraceRecursionDepth);
            data->associations_count++;
            break;
        }

        case D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG1:
        {
            const D3D12_RAYTRACING_PIPELINE_CONFIG1 *pipeline_config = obj->pDesc;

            vkd3d_array_reserve((void **)&data->associations, &data->associations_size,
                    data->associations_count + 1,
                    sizeof(*data->associations));

            data->associations[data->associations_count].kind = VKD3D_SHADER_SUBOBJECT_KIND_RAYTRACING_PIPELINE_CONFIG1;
            data->associations[data->associations_count].priority = association_priority;
            data->associations[data->associations_count].pipeline_config = *pipeline_config;
            data->associations[data->associations_count].export = NULL;
            RT_TRACE("%p || Adding PIPELINE_CONFIG1: MaxRecursion = %u, Flags = #%x\n",
                    obj->pDesc,
                    pipeline_config->MaxTraceRecursionDepth, pipeline_config->Flags);
            data->associations_count++;
            break;
        }

        case D3D12_STATE_SUBOBJECT_TYPE_DXIL_SUBOBJECT_TO_EXPORTS_ASSOCIATION:
        {
            const D3D12_DXIL_SUBOBJECT_TO_EXPORTS_ASSOCIATION *association = obj->pDesc;
            unsigned int num_associations = max(association->NumExports, 1);
            const struct vkd3d_shader_library_subobject *subobject;
            unsigned int root_signature_index = 0;

            RT_TRACE("Adding DXIL_SUBOBJECT_TO_EXPORTS: %s\n", debugstr_w(association->SubobjectToAssociate));

            for (i = 0; i < data->subobjects_count; i++)
            {
                if (vkd3d_export_strequal(association->SubobjectToAssociate, data->subobjects[i].name))
                    break;

                if (data->subobjects[i].kind == VKD3D_SHADER_SUBOBJECT_KIND_GLOBAL_ROOT_SIGNATURE ||
                        data->subobjects[i].kind == VKD3D_SHADER_SUBOBJECT_KIND_LOCAL_ROOT_SIGNATURE)
                {
                    root_signature_index++;
                }
            }

            if (i == data->subobjects_count)
            {
                ERR("Cannot find subobject %s.\n", debugstr_w(association->SubobjectToAssociate));
                return E_INVALIDARG;
            }

            subobject = &data->subobjects[i];

            vkd3d_array_reserve((void **)&data->associations, &data->associations_size,
                    data->associations_count + num_associations,
                    sizeof(*data->associations));

            for (i = 0; i < num_associations; i++)
            {
                switch (subobject->kind)
                {
                    case VKD3D_SHADER_SUBOBJECT_KIND_GLOBAL_ROOT_SIGNATURE:
                    case VKD3D_SHADER_SUBOBJECT_KIND_LOCAL_ROOT_SIGNATURE:
                        data->associations[data->associations_count].root_signature =
                                data->subobject_root_signatures[root_signature_index];
                        if (data->associations[data->associations_count].root_signature)
                            d3d12_root_signature_inc_ref(data->associations[data->associations_count].root_signature);
                        break;

                    case VKD3D_SHADER_SUBOBJECT_KIND_RAYTRACING_PIPELINE_CONFIG1:
                        data->associations[data->associations_count].pipeline_config = subobject->data.pipeline_config;
                        break;

                    case VKD3D_SHADER_SUBOBJECT_KIND_RAYTRACING_SHADER_CONFIG:
                        data->associations[data->associations_count].shader_config = subobject->data.shader_config;
                        break;

                    default:
                        ERR("Unexpected type %u for DXIL -> object association.\n", subobject->kind);
                        return E_INVALIDARG;
                }

                data->associations[data->associations_count].kind = subobject->kind;
                data->associations[data->associations_count].export =
                        association->NumExports ? association->pExports[i] : NULL;

                if (association_priority == VKD3D_ASSOCIATION_PRIORITY_DECLARED_STATE_OBJECT &&
                        association->NumExports)
                {
                    data->associations[data->associations_count].priority = VKD3D_ASSOCIATION_PRIORITY_EXPLICIT;
                }
                else if (association_priority == VKD3D_ASSOCIATION_PRIORITY_DECLARED_STATE_OBJECT)
                {
                    data->associations[data->associations_count].priority = VKD3D_ASSOCIATION_PRIORITY_EXPLICIT_DEFAULT;
                }
                else if (association->NumExports)
                {
                    data->associations[data->associations_count].priority =
                            VKD3D_ASSOCIATION_PRIORITY_DXIL_SUBOBJECT_ASSIGNMENT_EXPLICIT;
                }
                else
                {
                    data->associations[data->associations_count].priority =
                            VKD3D_ASSOCIATION_PRIORITY_DXIL_SUBOBJECT_ASSIGNMENT_DEFAULT;
                }

                RT_TRACE("  Export: %s (prio %u)\n",
                        association->NumExports ? debugstr_w(association->pExports[i]) : "NULL",
                        data->associations[data->associations_count].priority);

                data->associations_count++;
            }

            break;
        }

        case D3D12_STATE_SUBOBJECT_TYPE_SUBOBJECT_TO_EXPORTS_ASSOCIATION:
        {
            const D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION *association = obj->pDesc;
            unsigned int num_associations = max(association->NumExports, 1);

            vkd3d_array_reserve((void **)&data->associations, &data->associations_size,
                    data->associations_count + num_associations,
                    sizeof(*data->associations));

            RT_TRACE("SUBOBJECT_TO_EXPORTS: %p (type %u):\n",
                    association->pSubobjectToAssociate->pDesc,
                    association->pSubobjectToAssociate->Type);

            switch (association->pSubobjectToAssociate->Type)
            {
                case D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG:
                case D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG:
                case D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG1:
                case D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE:
                case D3D12_STATE_SUBOBJECT_TYPE_LOCAL_ROOT_SIGNATURE:
                {
                    for (i = 0; i < num_associations; i++)
                    {
                        data->associations[data->associations_count].export =
                                association->NumExports ? association->pExports[i] : NULL;
                        d3d12_state_object_set_association_data(&data->associations[data->associations_count],
                                association->pSubobjectToAssociate);
                        data->associations[data->associations_count].priority = association->NumExports ?
                                VKD3D_ASSOCIATION_PRIORITY_EXPLICIT :
                                VKD3D_ASSOCIATION_PRIORITY_EXPLICIT_DEFAULT;

                        RT_TRACE("  Export: %s (priority %u):\n",
                                association->NumExports ? debugstr_w(association->pExports[i]) : "NULL",
                                data->associations[data->associations_count].priority);

                        data->associations_count++;
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
            struct d3d12_rt_state_object *library_state;
            library_state = rt_impl_from_ID3D12StateObject(collection->pExistingCollection);
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

    return S_OK;
}

static HRESULT d3d12_state_object_parse_subobjects(struct d3d12_rt_state_object *object,
        const D3D12_STATE_OBJECT_DESC *desc,
        struct d3d12_rt_state_object *parent,
        struct d3d12_rt_state_object_pipeline_data *data)
{
    struct d3d12_root_signature *root_signature;
    unsigned int i;
    HRESULT hr;

    if (parent && FAILED(hr = d3d12_state_object_add_collection(parent, data, NULL, 0)))
        return hr;

    /* Make sure all state has been parsed. Ignore DXIL subobject associations for now.
     * We'll have to parse subobjects first. */
    for (i = 0; i < desc->NumSubobjects; i++)
    {
        const D3D12_STATE_SUBOBJECT *obj = &desc->pSubobjects[i];
        if (obj->Type != D3D12_STATE_SUBOBJECT_TYPE_DXIL_SUBOBJECT_TO_EXPORTS_ASSOCIATION &&
                obj->Type != D3D12_STATE_SUBOBJECT_TYPE_NODE_MASK)
        {
            if (FAILED(hr = d3d12_state_object_parse_subobject(object, obj, data,
                    VKD3D_ASSOCIATION_PRIORITY_DECLARED_STATE_OBJECT)))
                return hr;
        }
    }

    /* Make sure all child state has been parsed. */
    for (i = 0; i < data->subobjects_count; i++)
    {
        D3D12_GLOBAL_ROOT_SIGNATURE obj_root_signature;
        D3D12_STATE_SUBOBJECT obj;
        obj.pDesc = NULL;

        switch (data->subobjects[i].kind)
        {
            case VKD3D_SHADER_SUBOBJECT_KIND_STATE_OBJECT_CONFIG:
                obj.Type = D3D12_STATE_SUBOBJECT_TYPE_STATE_OBJECT_CONFIG;
                obj.pDesc = &data->subobjects[i].data.object_config;
                break;

            case VKD3D_SHADER_SUBOBJECT_KIND_RAYTRACING_SHADER_CONFIG:
                obj.Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG;
                obj.pDesc = &data->subobjects[i].data.shader_config;
                break;

            case VKD3D_SHADER_SUBOBJECT_KIND_RAYTRACING_PIPELINE_CONFIG1:
                obj.Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG1;
                obj.pDesc = &data->subobjects[i].data.pipeline_config;
                break;

            case VKD3D_SHADER_SUBOBJECT_KIND_HIT_GROUP:
                obj.Type = D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP;
                obj.pDesc = &data->subobjects[i].data.hit_group;
                break;

            case VKD3D_SHADER_SUBOBJECT_KIND_GLOBAL_ROOT_SIGNATURE:
            case VKD3D_SHADER_SUBOBJECT_KIND_LOCAL_ROOT_SIGNATURE:
                /* No DXBC header here, just raw root signature binary. */
                if (FAILED(hr = d3d12_root_signature_create_raw(object->device,
                        data->subobjects[i].data.payload.data,
                        data->subobjects[i].data.payload.size, &root_signature)))
                    return hr;

                d3d12_root_signature_inc_ref(root_signature);
                ID3D12RootSignature_Release(&root_signature->ID3D12RootSignature_iface);

                obj_root_signature.pGlobalRootSignature = &root_signature->ID3D12RootSignature_iface;
                obj.Type = data->subobjects[i].kind == VKD3D_SHADER_SUBOBJECT_KIND_GLOBAL_ROOT_SIGNATURE ?
                        D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE :
                        D3D12_STATE_SUBOBJECT_TYPE_LOCAL_ROOT_SIGNATURE;
                obj.pDesc = &obj_root_signature;

                vkd3d_array_reserve((void**)&data->subobject_root_signatures, &data->subobject_root_signatures_size,
                        data->subobject_root_signatures_count + 1, sizeof(*data->subobject_root_signatures));
                data->subobject_root_signatures[data->subobject_root_signatures_count++] = root_signature;
                break;

            default:
                break;
        }

        if (obj.pDesc && FAILED(hr = d3d12_state_object_parse_subobject(
                object, &obj, data, VKD3D_ASSOCIATION_PRIORITY_DXIL_SUBOBJECT)))
            return hr;
    }

    for (i = 0; i < desc->NumSubobjects; i++)
    {
        const D3D12_STATE_SUBOBJECT *obj = &desc->pSubobjects[i];
        /* Now we can parse DXIL subobject -> export associations. */
        if (obj->Type == D3D12_STATE_SUBOBJECT_TYPE_DXIL_SUBOBJECT_TO_EXPORTS_ASSOCIATION)
        {
            if (FAILED(hr = d3d12_state_object_parse_subobject(object, obj, data,
                    VKD3D_ASSOCIATION_PRIORITY_DECLARED_STATE_OBJECT)))
                return hr;
        }
    }

    /* Finally, parse subobject version of DXIL subobject to export. */
    for (i = 0; i < data->subobjects_count; i++)
    {
        if (data->subobjects[i].kind == VKD3D_SHADER_SUBOBJECT_KIND_SUBOBJECT_TO_EXPORTS_ASSOCIATION)
        {
            D3D12_STATE_SUBOBJECT obj;
            obj.Type = D3D12_STATE_SUBOBJECT_TYPE_DXIL_SUBOBJECT_TO_EXPORTS_ASSOCIATION;
            obj.pDesc = &data->subobjects[i].data.association;
            if (FAILED(hr = d3d12_state_object_parse_subobject(object, &obj, data,
                    VKD3D_ASSOCIATION_PRIORITY_DXIL_SUBOBJECT)))
                return hr;
        }
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

static uint32_t d3d12_state_object_find_collection_variant(const struct d3d12_rt_state_object_variant *variant,
        const struct d3d12_rt_state_object *collection)
{
    uint32_t i;

    for (i = 0; i < collection->pipelines_count; i++)
    {
        if (d3d12_root_signature_is_layout_compatible(
                variant->global_root_signature, collection->pipelines[i].global_root_signature))
        {
            return i;
        }
    }

    return UINT32_MAX;
}

static uint32_t d3d12_state_object_pipeline_data_find_entry(
        const struct d3d12_rt_state_object_pipeline_data *data,
        const struct d3d12_rt_state_object_variant *variant,
        unsigned int pipeline_variant_index,
        const WCHAR *import)
{
    const struct vkd3d_shader_library_entry_point *entry_point;
    uint32_t variant_index;
    uint32_t offset = 0;
    uint32_t index;
    char *duped;
    size_t i;

    if (!import)
        return VK_SHADER_UNUSED_KHR;

    index = d3d12_state_object_pipeline_data_find_entry_inner(data->entry_points, data->entry_points_count, import);
    if (index != VK_SHADER_UNUSED_KHR)
    {
        entry_point = &data->entry_points[index];
        if (entry_point->pipeline_variant_index == pipeline_variant_index)
            return entry_point->stage_index;
        else
            return VK_SHADER_UNUSED_KHR;
    }

    offset += data->stages_count;

    /* Try to look in collections. We'll only find something in the ALLOW_EXTERNAL_DEPENDENCIES_ON_LOCAL
     * situation. Otherwise, entry_points will be NULL. */
    for (i = 0; i < data->collections_count; i++)
    {
        variant_index = d3d12_state_object_find_collection_variant(variant, data->collections[i].object);
        if (variant_index == UINT32_MAX)
            continue;

        index = d3d12_state_object_pipeline_data_find_entry_inner(data->collections[i].object->entry_points,
                data->collections[i].object->entry_points_count,
                import);

        if (index != VK_SHADER_UNUSED_KHR)
        {
            entry_point = &data->collections[i].object->entry_points[index];
            if (entry_point->pipeline_variant_index == variant_index)
                return offset + entry_point->stage_index;
            else
                return VK_SHADER_UNUSED_KHR;
        }

        offset += data->collections[i].object->pipelines[variant_index].stages_count;
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

static VkDeviceSize get_shader_stack_size(struct d3d12_rt_state_object *object,
        uint32_t pipeline_variant_index, uint32_t index, VkShaderGroupShaderKHR shader)
{
    const struct d3d12_rt_state_object_variant *variant = &object->pipelines[pipeline_variant_index];
    const struct vkd3d_vk_device_procs *vk_procs = &object->device->vk_procs;
    return VK_CALL(vkGetRayTracingShaderGroupStackSizeKHR(object->device->vk_device,
            variant->pipeline ? variant->pipeline : variant->pipeline_library,
            index, shader));
}

static VkDeviceSize d3d12_state_object_pipeline_data_compute_default_stack_size(
        const struct d3d12_rt_state_object_pipeline_data *data,
        struct d3d12_rt_state_object_stack_info *stack_info, uint32_t recursion_depth)
{
    const struct d3d12_rt_state_object_identifier *export;
    struct d3d12_rt_state_object_stack_info stack;
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
        const struct d3d12_rt_state_object_stack_info *info = &data->collections[i].object->stack;
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

static struct d3d12_root_signature *d3d12_state_object_pipeline_data_get_root_signature(
        enum vkd3d_shader_subobject_kind kind,
        struct d3d12_rt_state_object_pipeline_data *data,
        const struct vkd3d_shader_library_entry_point *entry)
{
    const struct d3d12_state_object_association *association;
    association = d3d12_state_object_find_association(kind,
            data->associations, data->associations_count,
            data->hit_groups, data->hit_groups_count,
            entry, NULL);
    return association ? association->root_signature : NULL;
}

static HRESULT d3d12_state_object_get_group_handles(struct d3d12_rt_state_object *object,
        const struct d3d12_rt_state_object_pipeline_data *data)
{
    const struct vkd3d_vk_device_procs *vk_procs = &object->device->vk_procs;
    const struct d3d12_rt_state_object_variant *variant;
    unsigned int pipeline_variant_index;
    uint32_t collection_export;
    VkPipeline vk_pipeline;
    int collection_index;
    uint32_t group_index;
    VkResult vr;
    size_t i;

    for (i = 0; i < data->exports_count; i++)
    {
        pipeline_variant_index = data->exports[i].pipeline_variant_index;
        variant = &object->pipelines[pipeline_variant_index];
        group_index = data->exports[i].per_variant_group_index;

        if (variant->pipeline)
            vk_pipeline = variant->pipeline;
        else if (object->device->device_info.pipeline_library_group_handles_features.pipelineLibraryGroupHandles)
            vk_pipeline = variant->pipeline_library;
        else
            vk_pipeline = VK_NULL_HANDLE;

        if (vk_pipeline)
        {
            vr = VK_CALL(vkGetRayTracingShaderGroupHandlesKHR(object->device->vk_device,
                    vk_pipeline, group_index, 1,
                    sizeof(data->exports[i].identifier),
                    data->exports[i].identifier));
        }
        else
        {
            memset(data->exports[i].identifier, 0, sizeof(data->exports[i].identifier));
            vr = VK_SUCCESS;
        }

        if (vr)
            return hresult_from_vk_result(vr);

        RT_TRACE("Queried export %zu, variant %u, group handle %u -> { %016"PRIx64", %016"PRIx64", %016"PRIx64", %016"PRIx64" }\n",
                i, pipeline_variant_index, group_index,
                *(const uint64_t *)(data->exports[i].identifier + 0),
                *(const uint64_t *)(data->exports[i].identifier + 8),
                *(const uint64_t *)(data->exports[i].identifier + 16),
                *(const uint64_t *)(data->exports[i].identifier + 24));

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
                if (object->device->device_info.pipeline_library_group_handles_features.pipelineLibraryGroupHandles)
                {
                    ERR("Driver bug, SBT identifiers do not match for parent and child pipelines. "
                        "This is supposed to be guaranteed with VK_EXT_pipeline_library_group_handles.\n");
                }
                else
                {
                    FIXME("SBT identifiers do not match for parent and child pipelines. "
                          "Vulkan does not guarantee this, but DXR 1.1 requires this. Cannot use pipeline.\n");
                }
                return E_NOTIMPL;
            }
        }

        /* We can still query shader stack sizes even if we cannot get identifiers.
         * There is no VU against using this on pipeline libraries and DXR spec says
         * we can call it on COLLECTION objects as well. */
        data->exports[i].stack_size_general = UINT32_MAX;
        data->exports[i].stack_size_any = UINT32_MAX;
        data->exports[i].stack_size_closest = UINT32_MAX;
        data->exports[i].stack_size_intersection = UINT32_MAX;

        if (data->exports[i].general_stage_index != VK_SHADER_UNUSED_KHR)
        {
            data->exports[i].stack_size_general = get_shader_stack_size(object,
                    pipeline_variant_index, group_index,
                    VK_SHADER_GROUP_SHADER_GENERAL_KHR);
        }
        else
        {
            if (data->exports[i].anyhit_stage_index != VK_SHADER_UNUSED_KHR)
            {
                data->exports[i].stack_size_any = get_shader_stack_size(object,
                        pipeline_variant_index, group_index,
                        VK_SHADER_GROUP_SHADER_ANY_HIT_KHR);
            }

            if (data->exports[i].closest_stage_index != VK_SHADER_UNUSED_KHR)
            {
                data->exports[i].stack_size_closest = get_shader_stack_size(object,
                        pipeline_variant_index, group_index,
                        VK_SHADER_GROUP_SHADER_CLOSEST_HIT_KHR);
            }

            if (data->exports[i].intersection_stage_index != VK_SHADER_UNUSED_KHR)
            {
                data->exports[i].stack_size_intersection = get_shader_stack_size(object,
                        pipeline_variant_index, group_index,
                        VK_SHADER_GROUP_SHADER_INTERSECTION_KHR);
            }
        }
    }

    return S_OK;
}

static void d3d12_state_object_build_group_create_info(
        VkRayTracingShaderGroupCreateInfoKHR *group_create,
        VkRayTracingShaderGroupTypeKHR group_type,
        const struct d3d12_rt_state_object_identifier *export)
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
        struct d3d12_rt_state_object_variant *variant,
        VkDescriptorSetLayoutBinding **out_vk_bindings, size_t *out_vk_bindings_size, size_t *out_vk_bindings_count,
        struct vkd3d_shader_resource_binding *local_bindings,
        const D3D12_STATIC_SAMPLER_DESC1 *sampler_desc, const VkSampler *vk_samplers,
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
        local_bindings[i].binding.set = variant->local_static_sampler.set_index;
    }

    *out_vk_bindings = vk_bindings;
    *out_vk_bindings_size = vk_bindings_size;
    *out_vk_bindings_count = vk_bindings_count;
}

static bool d3d12_state_object_pipeline_data_find_global_state_object(
        struct d3d12_rt_state_object_pipeline_data *data,
        enum vkd3d_shader_subobject_kind kind,
        const struct d3d12_state_object_association **out_association)
{
    const struct d3d12_state_object_association *association = NULL;
    const struct d3d12_state_object_association *candidate;
    size_t i;

    /* All inherited associations have to agree. */
    for (i = 0; i < data->associations_count; i++)
    {
        if (data->associations[i].kind == kind &&
                data->associations[i].priority == VKD3D_ASSOCIATION_PRIORITY_INHERITED_COLLECTION)
        {
            if (!association)
                association = &data->associations[i];
            else if (!d3d12_state_object_association_data_equal(association, &data->associations[i]))
            {
                ERR("Mismatch in inherited associations for kind %u.\n", kind);
                return false;
            }
        }
    }

    for (i = 0; i < data->entry_points_count; i++)
    {
        candidate = d3d12_state_object_find_association(kind,
                data->associations, data->associations_count,
                data->hit_groups, data->hit_groups_count,
                &data->entry_points[i], NULL);

        if (!association)
        {
            association = candidate;
        }
        else if (!d3d12_state_object_association_data_equal(association, candidate))
        {
            /* To hypothetically support this for global root signatures,
             * we'd have to partition any RTPSO into N VkPipelines and select
             * the appropriate pipeline at DispatchRays() time based on the currently bound root signature ...
             * Leave this as a TODO until we observe that applications rely on this esoteric behavior. */
            ERR("Mismatch in inherited associations for kind %u.\n", kind);
            return false;
        }
    }

    *out_association = association;
    return true;
}

static HRESULT d3d12_state_object_pipeline_data_find_global_state_objects(
        struct d3d12_rt_state_object_pipeline_data *data,
        D3D12_RAYTRACING_SHADER_CONFIG *out_shader_config,
        D3D12_RAYTRACING_PIPELINE_CONFIG1 *out_pipeline_config)
{
    const struct d3d12_state_object_association *pipeline_config = NULL;
    const struct d3d12_state_object_association *shader_config = NULL;

    if (!d3d12_state_object_pipeline_data_find_global_state_object(data,
            VKD3D_SHADER_SUBOBJECT_KIND_RAYTRACING_PIPELINE_CONFIG1, &pipeline_config))
        return E_INVALIDARG;

    if (!d3d12_state_object_pipeline_data_find_global_state_object(data,
            VKD3D_SHADER_SUBOBJECT_KIND_RAYTRACING_SHADER_CONFIG, &shader_config))
        return E_INVALIDARG;

    if (!pipeline_config)
    {
        ERR("No pipeline config was declared or inherited. This is required state.\n");
        return E_INVALIDARG;
    }

    if (!shader_config)
    {
        ERR("No shader config was declared or inherited. This is required state.\n");
        return E_INVALIDARG;
    }

    *out_pipeline_config = pipeline_config->pipeline_config;
    *out_shader_config = shader_config->shader_config;

    return S_OK;
}

static HRESULT d3d12_state_object_compile_pipeline_variant(struct d3d12_rt_state_object *object,
        unsigned pipeline_variant_index,
        struct d3d12_rt_state_object_pipeline_data *data)
{
    const struct vkd3d_vk_device_procs *vk_procs = &object->device->vk_procs;
    struct vkd3d_shader_interface_local_info shader_interface_local_info;
    VkRayTracingPipelineInterfaceCreateInfoKHR interface_create_info;
    VkDescriptorSetLayoutBinding *local_static_sampler_bindings;
    struct d3d12_root_signature *default_global_root_signature;
    struct vkd3d_shader_interface_info shader_interface_info;
    VkRayTracingPipelineCreateInfoKHR pipeline_create_info;
    struct d3d12_root_signature *compat_global_signature;
    struct vkd3d_shader_resource_binding *local_bindings;
    struct vkd3d_shader_compile_arguments compile_args;
    struct d3d12_state_object_collection *collection;
    struct d3d12_rt_state_object_identifier *export;
    VkPipelineDynamicStateCreateInfo dynamic_state;
    struct vkd3d_shader_library_entry_point *entry;
    struct d3d12_rt_state_object_variant *variant;
    const struct D3D12_HIT_GROUP_DESC *hit_group;
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

    variant = &object->pipelines[pipeline_variant_index];

    memset(&compile_args, 0, sizeof(compile_args));
    compile_args.target_extensions = object->device->vk_info.shader_extensions;
    compile_args.target_extension_count = object->device->vk_info.shader_extension_count;
    compile_args.target = VKD3D_SHADER_TARGET_SPIRV_VULKAN_1_0;
    compile_args.min_subgroup_size = object->device->device_info.vulkan_1_3_properties.minSubgroupSize;
    compile_args.max_subgroup_size = object->device->device_info.vulkan_1_3_properties.maxSubgroupSize;
    /* Don't care about wave size promotion in RT. */
    compile_args.quirks = &vkd3d_shader_quirk_info;

    if (vkd3d_config_flags & VKD3D_CONFIG_FLAG_DRIVER_VERSION_SENSITIVE_SHADERS)
    {
        compile_args.driver_id = object->device->device_info.vulkan_1_2_properties.driverID;
        compile_args.driver_version = object->device->device_info.properties2.properties.driverVersion;
    }

    memset(&shader_interface_info, 0, sizeof(shader_interface_info));
    shader_interface_info.min_ssbo_alignment = d3d12_device_get_ssbo_alignment(object->device);

    /* Effectively ignored. */
    shader_interface_info.stage = VK_SHADER_STAGE_ALL;
    shader_interface_info.xfb_info = NULL;

    shader_interface_info.descriptor_size_cbv_srv_uav = d3d12_device_get_descriptor_handle_increment_size(
            object->device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    shader_interface_info.descriptor_size_sampler = d3d12_device_get_descriptor_handle_increment_size(
            object->device, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);

    compat_global_signature = variant->global_root_signature;

    if (!compat_global_signature)
    {
        /* We have to create a dummy root signature in this scenario.
         * Add a special entry point for this since otherwise we have to serialize root signatures
         * to dummy blobs and stuff which is only defined in d3d12.dll and the outer modules that
         * we shouldn't have access to. */
        if (FAILED(hr = d3d12_root_signature_create_empty(object->device, &default_global_root_signature)))
            return E_OUTOFMEMORY;
        compat_global_signature = default_global_root_signature;
        d3d12_root_signature_inc_ref(variant->global_root_signature = compat_global_signature);
        ID3D12RootSignature_Release(&default_global_root_signature->ID3D12RootSignature_iface);
    }

    RT_TRACE("Compiling for Global Root Signature (hash %016"PRIx64") (layout hash %016"PRIx64").\n",
            compat_global_signature->pso_compatibility_hash,
            compat_global_signature->layout_compatibility_hash);

    local_static_sampler_bindings = NULL;
    local_static_sampler_bindings_count = 0;
    local_static_sampler_bindings_size = 0;
    variant->local_static_sampler.set_index = compat_global_signature ?
            compat_global_signature->raygen.num_set_layouts : 0;

    if (object->device->debug_ring.active)
        data->spec_info_buffer = vkd3d_calloc(data->entry_points_count, sizeof(*data->spec_info_buffer));

    for (i = 0; i < data->entry_points_count; i++)
    {
        struct d3d12_root_signature *per_entry_global_signature;
        struct d3d12_root_signature *local_signature;

        entry = &data->entry_points[i];

        /* We have already compiled this entry point. */
        if (entry->pipeline_variant_index != UINT32_MAX)
            continue;

        /* Skip any entry point that is incompatible, we'll compile it another iteration. */
        per_entry_global_signature = d3d12_state_object_pipeline_data_get_root_signature(
                VKD3D_SHADER_SUBOBJECT_KIND_GLOBAL_ROOT_SIGNATURE, data, entry);
        if (!d3d12_root_signature_is_layout_compatible(variant->global_root_signature, per_entry_global_signature))
            continue;

        RT_TRACE("Compiling entry point: %s (stage = #%x).\n", debugstr_w(entry->plain_entry_point), entry->stage);

        local_signature = d3d12_state_object_pipeline_data_get_root_signature(
                VKD3D_SHADER_SUBOBJECT_KIND_LOCAL_ROOT_SIGNATURE, data, entry);
        local_bindings = NULL;

        if (per_entry_global_signature)
        {
            /* We might have different bindings per PSO, even if they are considered pipeline layout compatible.
             * Register/space declaration could differ, but those don't change the Vulkan pipeline layout. */
            shader_interface_info.flags = d3d12_root_signature_get_shader_interface_flags(
                    per_entry_global_signature, VKD3D_PIPELINE_TYPE_RAY_TRACING);
            shader_interface_info.descriptor_tables.offset = per_entry_global_signature->descriptor_table_offset;
            shader_interface_info.descriptor_tables.count = per_entry_global_signature->descriptor_table_count;
            shader_interface_info.bindings = per_entry_global_signature->bindings;
            shader_interface_info.binding_count = per_entry_global_signature->binding_count;
            shader_interface_info.push_constant_buffers = per_entry_global_signature->root_constants;
            shader_interface_info.push_constant_buffer_count = per_entry_global_signature->root_constant_count;
            shader_interface_info.push_constant_ubo_binding = &per_entry_global_signature->push_constant_ubo_binding;
            shader_interface_info.offset_buffer_binding = &per_entry_global_signature->offset_buffer_binding;
#ifdef VKD3D_ENABLE_DESCRIPTOR_QA
            shader_interface_info.descriptor_qa_payload_binding = &per_entry_global_signature->descriptor_qa_payload_binding;
            shader_interface_info.descriptor_qa_control_binding = &per_entry_global_signature->descriptor_qa_control_binding;
#endif
        }
        else
        {
            shader_interface_info.flags = 0;
            shader_interface_info.push_constant_buffer_count = 0;
            shader_interface_info.binding_count = 0;
        }

        if (local_signature)
        {
            RT_TRACE("  Local root signature: (hash %016"PRIx64") (compat hash %016"PRIx64").\n",
                    local_signature->pso_compatibility_hash,
                    local_signature->layout_compatibility_hash);

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

                d3d12_state_object_append_local_static_samplers(variant,
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
            shader_interface_info.flags |= d3d12_root_signature_get_shader_interface_flags(local_signature, VKD3D_PIPELINE_TYPE_RAY_TRACING);

            if (!per_entry_global_signature)
            {
                /* We won't have any root signature with push descriptors.
                 * This is a potential hole, but ray tracing shaders without a global root
                 * signature is questionable at best.
                 * The outer raygen shader will usually be the one with true side effects. */
                shader_interface_info.flags &= ~VKD3D_SHADER_INTERFACE_INSTRUCTION_QA_BUFFER;
            }

            if (local_signature->raygen.flags & (VKD3D_ROOT_SIGNATURE_USE_SSBO_OFFSET_BUFFER | VKD3D_ROOT_SIGNATURE_USE_TYPED_OFFSET_BUFFER))
                shader_interface_info.offset_buffer_binding = &local_signature->offset_buffer_binding;
        }
        else
        {
            RT_TRACE("  Local root signature: N/A\n");
            memset(&shader_interface_local_info, 0, sizeof(shader_interface_local_info));
        }

        if (vkd3d_stage_is_global_group(entry->stage))
        {
            /* Directly export this as a group. */
            vkd3d_array_reserve((void **) &data->exports, &data->exports_size,
                    data->exports_count + 1, sizeof(*data->exports));

            export = &data->exports[data->exports_count];

            if (object->flags & D3D12_STATE_OBJECT_FLAG_ALLOW_LOCAL_DEPENDENCIES_ON_EXTERNAL_DEFINITIONS)
            {
                /* Cannot pilfer since we might have to keep entry[] alive. */
                export->mangled_export = entry->mangled_entry_point ? vkd3d_wstrdup(entry->mangled_entry_point) : NULL;
                export->plain_export = entry->plain_entry_point ? vkd3d_wstrdup(entry->plain_entry_point) : NULL;
            }
            else
            {
                /* Pilfer */
                export->mangled_export = entry->mangled_entry_point;
                export->plain_export = entry->plain_entry_point;
                entry->mangled_entry_point = NULL;
                entry->plain_entry_point = NULL;
            }

            export->pipeline_variant_index = pipeline_variant_index;
            export->per_variant_group_index = data->groups_count;
            export->general_stage_index = data->stages_count;
            export->closest_stage_index = VK_SHADER_UNUSED_KHR;
            export->anyhit_stage_index = VK_SHADER_UNUSED_KHR;
            export->intersection_stage_index = VK_SHADER_UNUSED_KHR;
            export->inherited_collection_index = -1;
            export->inherited_collection_export_index = 0;
            export->general_stage = entry->stage;

            vkd3d_array_reserve((void **) &data->groups, &data->groups_size,
                    data->groups_count + 1, sizeof(*data->groups));

            d3d12_state_object_build_group_create_info(&data->groups[data->groups_count],
                    VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR,
                    export);

            data->exports_count++;
            data->groups_count++;
        }

        entry->pipeline_variant_index = pipeline_variant_index;
        entry->stage_index = data->stages_count;

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

        shader_interface_info.flags |= vkd3d_descriptor_debug_get_shader_interface_flags(
                object->device->descriptor_qa_global_info,
                dxil.code, dxil.size);

        if (vkd3d_shader_compile_dxil_export(&dxil, entry->real_entry_point, entry->debug_entry_point,
                &spirv, NULL,
                &shader_interface_info, &shader_interface_local_info, &compile_args) != VKD3D_OK)
        {
            ERR("Failed to convert DXIL export: %s (%s)\n",
                    entry->real_entry_point, entry->debug_entry_point);
            vkd3d_free(local_bindings);
            return E_OUTOFMEMORY;
        }

        if ((spirv.meta.flags & VKD3D_SHADER_META_FLAG_REPLACED) && data->spec_info_buffer)
        {
            vkd3d_shader_debug_ring_init_spec_constant(object->device, &data->spec_info_buffer[i], spirv.meta.hash);
            stage->pSpecializationInfo = &data->spec_info_buffer[i].spec_info;
        }

        RT_TRACE("  DXIL hash: %016"PRIx64".\n", spirv.meta.hash);

#ifdef VKD3D_ENABLE_BREADCRUMBS
        vkd3d_array_reserve((void**)&object->breadcrumb_shaders, &object->breadcrumb_shaders_size,
                object->breadcrumb_shaders_count + 1, sizeof(*object->breadcrumb_shaders));
        object->breadcrumb_shaders[object->breadcrumb_shaders_count].hash = spirv.meta.hash;
        object->breadcrumb_shaders[object->breadcrumb_shaders_count].stage = entry->stage;
        snprintf(object->breadcrumb_shaders[object->breadcrumb_shaders_count].name,
                sizeof(object->breadcrumb_shaders[object->breadcrumb_shaders_count].name),
                "%s", entry->debug_entry_point);
        object->breadcrumb_shaders_count++;
#endif

        vkd3d_free(local_bindings);
        if (!d3d12_device_validate_shader_meta(object->device, &spirv.meta))
            return E_INVALIDARG;

        if (FAILED(hr = d3d12_pipeline_state_create_shader_module(object->device, &stage->module, &spirv)))
            return hr;

        if (spirv.meta.flags & VKD3D_SHADER_META_FLAG_USES_SUBGROUP_OPERATIONS)
        {
            stage->flags |= VK_PIPELINE_SHADER_STAGE_CREATE_ALLOW_VARYING_SUBGROUP_SIZE_BIT;
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
        export->per_variant_group_index = data->groups_count;
        export->general_stage = VK_SHADER_STAGE_ALL; /* ignored */

        export->general_stage_index = VK_SHADER_UNUSED_KHR;
        export->closest_stage_index =
                d3d12_state_object_pipeline_data_find_entry(data, variant,
                        pipeline_variant_index, hit_group->ClosestHitShaderImport);
        export->intersection_stage_index =
                d3d12_state_object_pipeline_data_find_entry(data, variant,
                        pipeline_variant_index, hit_group->IntersectionShaderImport);
        export->anyhit_stage_index =
                d3d12_state_object_pipeline_data_find_entry(data, variant,
                        pipeline_variant_index, hit_group->AnyHitShaderImport);

        /* If this hit group does not belong to this global root signature,
         * we'll get VK_SHADER_UNUSED_KHR for the shader references. */
        if ((hit_group->ClosestHitShaderImport && export->closest_stage_index == VK_SHADER_UNUSED_KHR) ||
                (hit_group->IntersectionShaderImport && export->intersection_stage_index == VK_SHADER_UNUSED_KHR) ||
                (hit_group->AnyHitShaderImport && export->anyhit_stage_index == VK_SHADER_UNUSED_KHR))
        {
            continue;
        }

        export->plain_export = vkd3d_wstrdup(hit_group->HitGroupExport);

        vkd3d_array_reserve((void **) &data->groups, &data->groups_size,
                data->groups_count + 1, sizeof(*data->groups));

        d3d12_state_object_build_group_create_info(&data->groups[data->groups_count],
                hit_group->Type == D3D12_HIT_GROUP_TYPE_TRIANGLES ?
                        VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR :
                        VK_RAY_TRACING_SHADER_GROUP_TYPE_PROCEDURAL_HIT_GROUP_KHR,
                export);

        export->pipeline_variant_index = pipeline_variant_index;
        export->per_variant_group_index = data->groups_count;

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
        const struct d3d12_rt_state_object_variant *collection_variant;
        uint32_t collection_variant_index;
        collection = &data->collections[i];

        /* Only include pipeline libraries which are compatible with current global root signature. */
        collection_variant_index = d3d12_state_object_find_collection_variant(variant, collection->object);
        if (collection_variant_index == UINT32_MAX)
            continue;

        collection_variant = &collection->object->pipelines[collection_variant_index];

        /* Skip degenerates. */
        if (!collection_variant->pipeline_library)
            continue;

        if (collection->num_exports)
            num_groups_to_export = collection->num_exports;
        else
            num_groups_to_export = collection->object->exports_count;

        for (j = 0; j < num_groups_to_export; j++)
        {
            const struct d3d12_rt_state_object_identifier *input_export;
            uint32_t input_index;

            if (collection->num_exports == 0)
            {
                input_export = &collection->object->exports[j];

                /* An export might not be compatible with this global root signature. */
                if (input_export->pipeline_variant_index != collection_variant_index)
                    continue;

                input_index = j;
                vkd3d_array_reserve((void **)&data->exports, &data->exports_size,
                        data->exports_count + 1, sizeof(*data->exports));

                export = &data->exports[data->exports_count];
                memset(export, 0, sizeof(*export));

                if (input_export->plain_export)
                    export->plain_export = vkd3d_wstrdup(input_export->plain_export);
                if (input_export->mangled_export)
                    export->mangled_export = vkd3d_wstrdup(input_export->mangled_export);
            }
            else
            {
                const WCHAR *original_export;
                const WCHAR *subtype = NULL;

                if (collection->exports[j].ExportToRename)
                    original_export = collection->exports[j].ExportToRename;
                else
                    original_export = collection->exports[j].Name;

                input_index = d3d12_state_object_get_export_index(collection->object, original_export, &subtype);
                if (subtype || input_index == UINT32_MAX)
                {
                    /* If we import things, but don't use them, this can happen. Just ignore it. */
                    continue;
                }

                input_export = &collection->object->exports[input_index];

                /* An export might not be compatible with this global root signature. */
                if (input_export->pipeline_variant_index != collection_variant_index)
                    continue;

                vkd3d_array_reserve((void **)&data->exports, &data->exports_size,
                        data->exports_count + 1, sizeof(*data->exports));

                export = &data->exports[data->exports_count];
                memset(export, 0, sizeof(*export));

                export->plain_export = vkd3d_wstrdup(collection->exports[j].Name);
                export->mangled_export = NULL;
            }

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
             * Vulkan does not guarantee this without VK_EXT_pipeline_library_group_handles,
             * but we can validate and accept the pipeline if implementation happens to satisfy this rule. */
            if (collection->object->type == D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE ||
                    object->device->device_info.pipeline_library_group_handles_features.pipelineLibraryGroupHandles)
            {
                export->inherited_collection_index = (int)i;
            }
            else
                export->inherited_collection_index = -1;
            export->inherited_collection_export_index = input_index;

            export->pipeline_variant_index = pipeline_variant_index;
            export->per_variant_group_index = pgroup_offset + input_export->per_variant_group_index;

            data->exports_count += 1;
        }

        vkd3d_array_reserve((void **)&data->vk_libraries, &data->vk_libraries_size,
                data->vk_libraries_count + 1, sizeof(*data->vk_libraries));
        data->vk_libraries[data->vk_libraries_count++] = collection_variant->pipeline_library;

        /* Including a library implicitly adds all their pStages[] and pGroups[]. */
        pgroup_offset += collection_variant->groups_count;
        pstage_offset += collection_variant->stages_count;
    }

    if (local_static_sampler_bindings_count)
    {
        uint64_t hash = hash_fnv1_init();
        hash = hash_fnv1_iterate_u32(hash, local_static_sampler_bindings_count);
        for (i = 0; i < local_static_sampler_bindings_count; i++)
        {
            /* Immutable samplers are deduplicated, so this is fine. */
            hash = hash_fnv1_iterate_u64(hash, (uint64_t)local_static_sampler_bindings[i].pImmutableSamplers[0]);
            hash = hash_fnv1_iterate_u32(hash, local_static_sampler_bindings[i].binding);
            hash = hash_fnv1_iterate_u32(hash, local_static_sampler_bindings[i].stageFlags);
        }

        variant->local_static_sampler.compatibility_hash = hash;
        variant->local_static_sampler.owned_handles = true;

        if (FAILED(hr = vkd3d_create_descriptor_set_layout(object->device, 0, local_static_sampler_bindings_count,
                local_static_sampler_bindings,
                VK_DESCRIPTOR_SET_LAYOUT_CREATE_DESCRIPTOR_BUFFER_BIT_EXT |
                        VK_DESCRIPTOR_SET_LAYOUT_CREATE_EMBEDDED_IMMUTABLE_SAMPLERS_BIT_EXT,
                &variant->local_static_sampler.set_layout)))
        {
            vkd3d_free(local_static_sampler_bindings);
            return hr;
        }

        vkd3d_free(local_static_sampler_bindings);

        if (compat_global_signature)
        {
            if (FAILED(hr = d3d12_root_signature_create_local_static_samplers_layout(compat_global_signature,
                    variant->local_static_sampler.set_layout, &variant->local_static_sampler.pipeline_layout)))
                return hr;
        }
        else
        {
            if (FAILED(hr = vkd3d_create_pipeline_layout(object->device, 1, &variant->local_static_sampler.set_layout,
                    0, NULL, &variant->local_static_sampler.pipeline_layout)))
                return hr;
        }

        /* Implicitly allocated and bound if we have descriptor buffer support. */
        if (!d3d12_device_uses_descriptor_buffers(object->device))
        {
            if (FAILED(hr = vkd3d_sampler_state_allocate_descriptor_set(&object->device->sampler_state,
                    object->device, variant->local_static_sampler.set_layout,
                    &variant->local_static_sampler.desc_set, &variant->local_static_sampler.desc_pool)))
                return hr;
        }
    }

    /* If we have collections, we need to make sure that every pipeline layout is compatible.
     * We validate that global root signature is compatible, so we only need to ensure the
     * local root signature is either unused (compatible with appended local sampler sets)
     * or the same compat hash. */
    for (i = 0; i < data->collections_count; i++)
    {
        struct d3d12_rt_state_object *child = data->collections[i].object;
        const struct d3d12_rt_state_object_variant *collection_variant;
        uint32_t collection_variant_index;

        /* Only include pipeline libraries which are compatible with current global root signature. */
        collection_variant_index = d3d12_state_object_find_collection_variant(variant, child);
        if (collection_variant_index == UINT32_MAX)
            continue;

        collection_variant = &child->pipelines[collection_variant_index];

        if (collection_variant->local_static_sampler.pipeline_layout && !variant->local_static_sampler.pipeline_layout)
        {
            /* Borrow these handles. */
            variant->local_static_sampler.pipeline_layout = collection_variant->local_static_sampler.pipeline_layout;
            variant->local_static_sampler.desc_set = collection_variant->local_static_sampler.desc_set;
            variant->local_static_sampler.set_layout = collection_variant->local_static_sampler.set_layout;
            variant->local_static_sampler.compatibility_hash = collection_variant->local_static_sampler.compatibility_hash;
        }

        if (collection_variant->local_static_sampler.pipeline_layout)
        {
            if (collection_variant->local_static_sampler.compatibility_hash !=
                    variant->local_static_sampler.compatibility_hash)
            {
                FIXME("COLLECTION and RTPSO declares local static sampler set layouts with different definitions. "
                      "This is unsupported.\n");
                return E_NOTIMPL;
            }
        }
    }

    pipeline_create_info.sType = VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR;
    pipeline_create_info.pNext = NULL;

    /* If we allow state object additions, we must first lower this pipeline to a library, and
     * then link it to itself so we can use it a library in subsequent PSO creations, but we
     * must also be able to trace rays from the library. */
    pipeline_create_info.flags = (object->type == D3D12_STATE_OBJECT_TYPE_COLLECTION ||
            (object->flags & D3D12_STATE_OBJECT_FLAG_ALLOW_STATE_OBJECT_ADDITIONS)) ?
            VK_PIPELINE_CREATE_LIBRARY_BIT_KHR : 0;

    if (variant->local_static_sampler.pipeline_layout)
        pipeline_create_info.layout = variant->local_static_sampler.pipeline_layout;
    else
        pipeline_create_info.layout = compat_global_signature->raygen.vk_pipeline_layout;

    pipeline_create_info.basePipelineHandle = VK_NULL_HANDLE;
    pipeline_create_info.basePipelineIndex = -1;
    pipeline_create_info.pGroups = data->groups;
    pipeline_create_info.groupCount = data->groups_count;
    pipeline_create_info.pStages = data->stages;
    pipeline_create_info.stageCount = data->stages_count;
    pipeline_create_info.pLibraryInfo = &library_info;
    pipeline_create_info.pLibraryInterface = &interface_create_info;
    pipeline_create_info.pDynamicState = &dynamic_state;
    pipeline_create_info.maxPipelineRayRecursionDepth = object->pipeline_config.MaxTraceRecursionDepth;

    if (object->pipeline_config.Flags & D3D12_RAYTRACING_PIPELINE_FLAG_SKIP_TRIANGLES)
        pipeline_create_info.flags |= VK_PIPELINE_CREATE_RAY_TRACING_SKIP_TRIANGLES_BIT_KHR;
    if (object->pipeline_config.Flags & D3D12_RAYTRACING_PIPELINE_FLAG_SKIP_PROCEDURAL_PRIMITIVES)
        pipeline_create_info.flags |= VK_PIPELINE_CREATE_RAY_TRACING_SKIP_AABBS_BIT_KHR;
    if (object->pipeline_config.Flags & D3D12_RAYTRACING_PIPELINE_FLAG_ALLOW_OPACITY_MICROMAPS)
        pipeline_create_info.flags |= VK_PIPELINE_CREATE_RAY_TRACING_OPACITY_MICROMAP_BIT_EXT;

    library_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LIBRARY_CREATE_INFO_KHR;
    library_info.pNext = NULL;
    library_info.libraryCount = data->vk_libraries_count;
    library_info.pLibraries = data->vk_libraries;

    /* Degenerate RTPSO. Eliminate it.
     * Can happen if we add a global root signature that is never referenced.
     * No group is okay if we're exposing individual entry points with
     * ALLOW_EXTERNAL_DEPENDENCIES_ON_LOCAL_DEFINITIONS. */
    if (pstage_offset == 0)
        return S_OK;

    interface_create_info.sType = VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_INTERFACE_CREATE_INFO_KHR;
    interface_create_info.pNext = NULL;
    interface_create_info.maxPipelineRayPayloadSize = object->shader_config.MaxPayloadSizeInBytes;
    interface_create_info.maxPipelineRayHitAttributeSize = object->shader_config.MaxAttributeSizeInBytes;

    if (object->pipeline_config.MaxTraceRecursionDepth >
            object->device->device_info.ray_tracing_pipeline_properties.maxRayRecursionDepth)
    {
        /* We cannot do anything about this, since we let sub-minspec devices through,
         * and this content actually tries to use recursion. */
        ERR("MaxTraceRecursionDepth %u exceeds device limit of %u.\n",
                object->pipeline_config.MaxTraceRecursionDepth,
                object->device->device_info.ray_tracing_pipeline_properties.maxRayRecursionDepth);
        return E_INVALIDARG;
    }

    dynamic_state.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic_state.pNext = NULL;
    dynamic_state.flags = 0;
    dynamic_state.dynamicStateCount = 1;
    dynamic_state.pDynamicStates = dynamic_states;

    if (d3d12_device_uses_descriptor_buffers(object->device))
        pipeline_create_info.flags |= VK_PIPELINE_CREATE_DESCRIPTOR_BUFFER_BIT_EXT;

    TRACE("Calling vkCreateRayTracingPipelinesKHR.\n");

    vr = VK_CALL(vkCreateRayTracingPipelinesKHR(object->device->vk_device, VK_NULL_HANDLE,
            VK_NULL_HANDLE, 1, &pipeline_create_info, NULL,
            (pipeline_create_info.flags & VK_PIPELINE_CREATE_LIBRARY_BIT_KHR) ?
                    &variant->pipeline_library : &variant->pipeline));

    if (vr == VK_SUCCESS && (object->flags & D3D12_STATE_OBJECT_FLAG_ALLOW_STATE_OBJECT_ADDITIONS) &&
            object->type == D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE)
    {
        /* It is valid to inherit pipeline libraries into other pipeline libraries. */
        pipeline_create_info.flags &= ~VK_PIPELINE_CREATE_LIBRARY_BIT_KHR;
        pipeline_create_info.pStages = NULL;
        pipeline_create_info.pGroups = NULL;
        pipeline_create_info.stageCount = 0;
        pipeline_create_info.groupCount = 0;
        library_info.libraryCount = 1;
        library_info.pLibraries = &variant->pipeline_library;

        /* Self-link the pipeline library. */
        vr = VK_CALL(vkCreateRayTracingPipelinesKHR(object->device->vk_device, VK_NULL_HANDLE,
                VK_NULL_HANDLE, 1, &pipeline_create_info, NULL, &variant->pipeline));
    }

    TRACE("Completed vkCreateRayTracingPipelinesKHR.\n");

    if (vr)
        return hresult_from_vk_result(vr);

    /* Always set this, since parent needs to be able to offset pStages[] and pGroups[]. */
    variant->stages_count = pstage_offset;
    variant->groups_count = pgroup_offset;

    return S_OK;
}

static void d3d12_state_object_add_global_root_signature_variant(
        struct d3d12_rt_state_object *object, struct d3d12_root_signature *rs)
{
    unsigned int i;
    for (i = 0; i < object->pipelines_count; i++)
        if (d3d12_root_signature_is_layout_compatible(object->pipelines[i].global_root_signature, rs))
            return;

    vkd3d_array_reserve((void **)&object->pipelines, &object->pipelines_size,
            object->pipelines_count + 1, sizeof(*object->pipelines));
    memset(&object->pipelines[object->pipelines_count], 0, sizeof(object->pipelines[object->pipelines_count]));
    assert(rs);
    d3d12_root_signature_inc_ref(object->pipelines[object->pipelines_count].global_root_signature = rs);
    object->pipelines_count++;
}

static void d3d12_state_object_collect_variants(struct d3d12_rt_state_object *object,
        const struct d3d12_rt_state_object_pipeline_data *data)
{
    const struct d3d12_rt_state_object_variant *variant;
    unsigned int i, j;

    /* Always allocate an entry for NULL global root signature.
     * If an entry point happens to be associated with NULL due to conflicts,
     * we have a fallback. Usually, this variant will be eliminated after parsing. */
    object->pipelines_count = 1;
    vkd3d_array_reserve((void **)&object->pipelines, &object->pipelines_size,
            object->pipelines_count, sizeof(*object->pipelines));
    memset(&object->pipelines[0], 0, sizeof(object->pipelines[0]));

    for (i = 0; i < data->associations_count; i++)
        if (data->associations[i].kind == VKD3D_SHADER_SUBOBJECT_KIND_GLOBAL_ROOT_SIGNATURE)
            d3d12_state_object_add_global_root_signature_variant(object, data->associations[i].root_signature);

    /* Forward any global root signatures we pulled from a collection. */
    for (i = 0; i < data->collections_count; i++)
    {
        for (j = 0; j < data->collections[i].object->pipelines_count; j++)
        {
            variant = &data->collections[i].object->pipelines[j];

            /* Skip degenerates. */
            if (variant->pipeline_library)
            {
                d3d12_state_object_add_global_root_signature_variant(
                        object, variant->global_root_signature);
            }
        }
    }
}

static HRESULT d3d12_state_object_init(struct d3d12_rt_state_object *object,
        struct d3d12_device *device,
        const D3D12_STATE_OBJECT_DESC *desc,
        struct d3d12_rt_state_object *parent)
{
    struct d3d12_rt_state_object_pipeline_data data;
    HRESULT hr = S_OK;
    unsigned int i;

    object->ID3D12StateObject_iface.lpVtbl = &d3d12_state_object_vtbl;
    object->ID3D12StateObjectProperties1_iface.lpVtbl = &d3d12_state_object_properties_vtbl;
    object->refcount = 1;
    object->internal_refcount = 1;
    object->device = device;
    object->type = desc->Type;

    if (object->type == D3D12_STATE_OBJECT_TYPE_COLLECTION &&
            (vkd3d_config_flags & VKD3D_CONFIG_FLAG_ALLOW_SBT_COLLECTION) &&
            !device->device_info.pipeline_library_group_handles_features.pipelineLibraryGroupHandles)
    {
        /* It seems to be valid to query shader identifiers from a COLLECTION which is pure insanity.
         * We can fake this behavior if we pretend we have ALLOW_STATE_OBJECT_ADDITIONS and RTPSO.
         * We will validate that the "COLLECTION" matches the consuming RTPSO.
         * If the collection does not contain an RGEN shader, we're technically out of spec here. */

        /* If we have pipeline library group handles feature however,
         * we can ignore this workaround and do it properly. */
        INFO("Promoting COLLECTION to RAYTRACING_PIPELINE as workaround.\n");
        object->type = D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE;
        object->flags |= D3D12_STATE_OBJECT_FLAG_ALLOW_STATE_OBJECT_ADDITIONS;
    }

    memset(&data, 0, sizeof(data));

    if (FAILED(hr = d3d12_state_object_parse_subobjects(object, desc, parent, &data)))
        goto fail;

    if (FAILED(hr = d3d12_state_object_pipeline_data_find_global_state_objects(&data,
            &object->shader_config, &object->pipeline_config)))
        goto fail;

    /* Figure out how many variants we need. We'll need one pipeline variant
     * for every unique global root signature. */
    d3d12_state_object_collect_variants(object, &data);

    for (i = 0; i < object->pipelines_count; i++)
    {
        if (FAILED(hr = d3d12_state_object_compile_pipeline_variant(object, i, &data)))
            goto fail;

        d3d12_state_object_pipeline_data_cleanup_modules(&data, object->device);
        data.groups_count = 0;
        data.vk_libraries_count = 0;
    }

    if (FAILED(hr = d3d12_state_object_get_group_handles(object, &data)))
        goto fail;

    if (object->type == D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE)
    {
        object->pipeline_stack_size = d3d12_state_object_pipeline_data_compute_default_stack_size(&data,
                &object->stack, object->pipeline_config.MaxTraceRecursionDepth);
    }
    else
    {
        /* This should be 0 for COLLECTION objects. */
        object->pipeline_stack_size = 0;
    }

    /* Spec says we need to hold a reference to the collection object, but it doesn't show up in API,
     * so we must assume private reference. */
    if (data.collections_count)
    {
        object->collections = vkd3d_malloc(data.collections_count * sizeof(*object->collections));
        object->collections_count = data.collections_count;
        for (i = 0; i < data.collections_count; i++)
        {
            object->collections[i] = data.collections[i].object;
            d3d12_state_object_inc_ref(object->collections[i]);

#ifdef VKD3D_ENABLE_BREADCRUMBS
            vkd3d_array_reserve((void**)&object->breadcrumb_shaders, &object->breadcrumb_shaders_size,
                    object->breadcrumb_shaders_count + object->collections[i]->breadcrumb_shaders_count,
                    sizeof(*object->breadcrumb_shaders));
            memcpy(object->breadcrumb_shaders + object->breadcrumb_shaders_count,
                    object->collections[i]->breadcrumb_shaders,
                    object->collections[i]->breadcrumb_shaders_count * sizeof(*object->breadcrumb_shaders));
            object->breadcrumb_shaders_count += object->collections[i]->breadcrumb_shaders_count;
#endif
        }
    }

    /* Pilfer the export table. */
    object->exports = data.exports;
    object->exports_size = data.exports_size;
    object->exports_count = data.exports_count;
    data.exports = NULL;
    data.exports_size = 0;
    data.exports_count = 0;

    /* If parent object can depend on individual shaders, keep the entry point list around. */
    if (object->flags & D3D12_STATE_OBJECT_FLAG_ALLOW_EXTERNAL_DEPENDENCIES_ON_LOCAL_DEFINITIONS)
    {
        object->entry_points = data.entry_points;
        object->entry_points_count = data.entry_points_count;
        data.entry_points = NULL;
        data.entry_points_count = 0;
    }

    if (FAILED(hr = vkd3d_private_store_init(&object->private_store)))
        goto fail;

    d3d12_state_object_pipeline_data_cleanup(&data, object->device);
    d3d_destruction_notifier_init(&object->destruction_notifier, (IUnknown*)&object->ID3D12StateObject_iface);
    d3d12_device_add_ref(object->device);
    return S_OK;

fail:
    if (object->flags & D3D12_STATE_OBJECT_FLAG_ALLOW_LOCAL_DEPENDENCIES_ON_EXTERNAL_DEFINITIONS)
    {
        /* If we allow local dependencies on external definitions, it's very plausible we'll fail
         * a compilation. Defer this compilation. Need to keep alive what we parsed,
         * and hand that over to any pipeline that includes the collection.
         * Native drivers tend to compile what they can up-front, but partially compiling a pipeline
         * will be too painful and there seems to be no spec requirement to do that, so
         * we'll do the easiest thing. If we can compile everything, go for it. Otherwise, punt to link time. */
        WARN("Deferring compilation of COLLECTION due to ALLOW_LOCAL_DEPENDENCIES_ON_EXTERNAL_DEFINITIONS.\n");
        d3d12_state_object_cleanup(object);
        object->deferred_data = d3d12_state_object_pipeline_data_defer(&data, object->device);
        if (FAILED(hr = vkd3d_private_store_init(&object->private_store)))
            d3d12_state_object_cleanup(object);
        else
            d3d12_device_add_ref(object->device);
    }
    else
    {
        d3d12_state_object_pipeline_data_cleanup(&data, object->device);
        d3d12_state_object_cleanup(object);
    }

    return hr;
}

HRESULT d3d12_rt_state_object_create(struct d3d12_device *device, const D3D12_STATE_OBJECT_DESC *desc,
        struct d3d12_rt_state_object *parent,
        struct d3d12_rt_state_object **state_object)
{
    struct d3d12_rt_state_object *object;
    HRESULT hr;

    if (!(object = vkd3d_calloc(1, sizeof(*object))))
        return E_OUTOFMEMORY;

    RT_TRACE("==== Create %s ====\n",
            desc->Type == D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE ? "RTPSO" : "Collection");
    hr = d3d12_state_object_init(object, device, desc, parent);
    RT_TRACE("==== Done %p (hr = #%x) ====\n", (void *)object, hr);

    if (FAILED(hr))
    {
        vkd3d_free(object);
        return hr;
    }

    *state_object = object;
    return S_OK;
}

HRESULT d3d12_rt_state_object_add(struct d3d12_device *device, const D3D12_STATE_OBJECT_DESC *desc,
        struct d3d12_rt_state_object *parent, struct d3d12_rt_state_object **object)
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

    hr = d3d12_rt_state_object_create(device, desc, parent, object);
    return hr;
}
