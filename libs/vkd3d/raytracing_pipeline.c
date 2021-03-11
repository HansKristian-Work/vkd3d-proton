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

struct d3d12_state_object *impl_from_ID3D12StateObject(ID3D12StateObject *iface)
{
    return CONTAINING_RECORD(iface, struct d3d12_state_object, ID3D12StateObject_iface);
}

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

    VK_CALL(vkDestroyPipeline(object->device->vk_device, object->pipeline, NULL));
}

static ULONG d3d12_state_object_release(struct d3d12_state_object *state_object)
{
    ULONG refcount = InterlockedDecrement(&state_object->refcount);

    TRACE("%p decreasing refcount to %u.\n", state_object, refcount);

    if (!refcount)
    {
        struct d3d12_device *device = state_object->device;
        vkd3d_private_store_destroy(&state_object->private_store);
        d3d12_state_object_cleanup(state_object);
        vkd3d_free(state_object);
        d3d12_device_release(device);
    }

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

static void * STDMETHODCALLTYPE d3d12_state_object_properties_GetShaderIdentifier(ID3D12StateObjectProperties *iface,
        LPCWSTR export_name)
{
    struct d3d12_state_object *object = impl_from_ID3D12StateObjectProperties(iface);
    size_t i;

    TRACE("iface %p, export_name %p.\n", iface, export_name);

    for (i = 0; i < object->exports_count; i++)
    {
        if (vkd3d_export_strequal(export_name, object->exports[i].mangled_export) ||
            vkd3d_export_strequal(export_name, object->exports[i].plain_export))
        {
            return object->exports[i].identifier;
        }
    }

    ERR("Could not find entry point.\n");
    return NULL;
}

static UINT64 STDMETHODCALLTYPE d3d12_state_object_properties_GetShaderStackSize(ID3D12StateObjectProperties *iface,
        LPCWSTR export_name)
{
    struct d3d12_state_object *object = impl_from_ID3D12StateObjectProperties(iface);
    const WCHAR *subtype = NULL;
    size_t i, n;

    TRACE("iface %p, export_name %p!\n", iface, export_name);

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
            if (subtype)
            {
                if (vkd3d_export_strequal(subtype, u"::intersection"))
                    return object->exports[i].stack_size_intersection;
                else if (vkd3d_export_strequal(subtype, u"::anyhit"))
                    return object->exports[i].stack_size_any;
                else if (vkd3d_export_strequal(subtype, u"::closesthit"))
                    return object->exports[i].stack_size_closest;
                else
                    return 0xffffffff;
            }
            else
            {
                return object->exports[i].stack_size_general;
            }
        }
    }

    return 0xffffffffu;
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
    TRACE("iface %p, stack_size_in_bytes %llu!\n", iface);

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

struct d3d12_state_object_pipeline_data
{
    const D3D12_RAYTRACING_PIPELINE_CONFIG *pipeline_config;
    const D3D12_RAYTRACING_SHADER_CONFIG *shader_config;
    ID3D12RootSignature *global_root_signature;
    ID3D12RootSignature *high_priority_local_root_signature;
    ID3D12RootSignature *low_priority_local_root_signature;

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
}

static HRESULT d3d12_state_object_parse_subobjects(struct d3d12_state_object *object,
        const D3D12_STATE_OBJECT_DESC *desc, struct d3d12_state_object_pipeline_data *data)
{
    unsigned int i, j;

    for (i = 0; i < desc->NumSubobjects; i++)
    {
        const D3D12_STATE_SUBOBJECT *obj = &desc->pSubobjects[i];
        switch (obj->Type)
        {
            case D3D12_STATE_SUBOBJECT_TYPE_STATE_OBJECT_CONFIG:
            {
                const D3D12_STATE_OBJECT_CONFIG *object_config = obj->pDesc;
                if (object_config->Flags != D3D12_STATE_OBJECT_FLAG_NONE)
                {
                    FIXME("Object config flag #%x is not supported.\n", object_config->Flags);
                    return E_INVALIDARG;
                }
                break;
            }

            case D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE:
            {
                const D3D12_GLOBAL_ROOT_SIGNATURE *rs = obj->pDesc;
                if (data->global_root_signature)
                {
                    /* Simplicity for now. */
                    FIXME("More than one global root signature is used.\n");
                    return E_INVALIDARG;
                }
                data->global_root_signature = rs->pGlobalRootSignature;
                break;
            }

            case D3D12_STATE_SUBOBJECT_TYPE_LOCAL_ROOT_SIGNATURE:
            {
                const D3D12_LOCAL_ROOT_SIGNATURE *rs = obj->pDesc;
                /* This is only chosen as default if there is nothing else.
                 * Conflicting definitions seem to cause runtime to choose something
                 * arbitrary. Just override the low priority default.
                 * A high priority default association takes precedence if it exists. */
                data->low_priority_local_root_signature = rs->pLocalRootSignature;
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
                if (data->pipeline_config && memcmp(data->pipeline_config, obj->pDesc, sizeof(*data->pipeline_config)) != 0)
                {
                    ERR("RAYTRACING_PIPELINE_CONFIG must match if multiple objects are present.\n");
                    return E_INVALIDARG;
                }
                data->pipeline_config = obj->pDesc;
                break;
            }

            case D3D12_STATE_SUBOBJECT_TYPE_SUBOBJECT_TO_EXPORTS_ASSOCIATION:
            {
                const D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION *association = obj->pDesc;
                const D3D12_LOCAL_ROOT_SIGNATURE *local_rs;

                if (association->pSubobjectToAssociate->Type != D3D12_STATE_SUBOBJECT_TYPE_LOCAL_ROOT_SIGNATURE)
                {
                    FIXME("Can only associate local root signatures to exports.\n");
                    return E_INVALIDARG;
                }

                local_rs = association->pSubobjectToAssociate->pDesc;
                if (association->NumExports)
                {
                    vkd3d_array_reserve((void **)&data->associations, &data->associations_size,
                            data->associations_count + association->NumExports,
                            sizeof(*data->associations));
                    for (j = 0; j < association->NumExports; j++)
                    {
                        data->associations[data->associations_count].export = association->pExports[j];
                        data->associations[data->associations_count].root_signature =
                                unsafe_impl_from_ID3D12RootSignature(local_rs->pLocalRootSignature);
                        data->associations_count++;
                    }
                }
                else
                {
                    /* Local root signatures being exported to NULL takes priority as the default local RS. */
                    data->high_priority_local_root_signature = local_rs->pLocalRootSignature;
                }
                break;
            }

            default:
                FIXME("Unrecognized subobject type: %u.\n", obj->Type);
                return E_INVALIDARG;
        }
    }

    return S_OK;
}

static uint32_t d3d12_state_object_pipeline_data_find_entry(
        const struct d3d12_state_object_pipeline_data *data,
        const WCHAR *import)
{
    uint32_t i;
    if (!import)
        return VK_SHADER_UNUSED_KHR;

    for (i = 0; i < data->entry_points_count; i++)
    {
        if (vkd3d_export_strequal(import, data->entry_points[i].mangled_entry_point) ||
            vkd3d_export_strequal(import, data->entry_points[i].plain_entry_point))
            return i;
    }

    ERR("Failed to find entry point.\n");
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
        const struct d3d12_state_object_pipeline_data *data, uint32_t recursion_depth)
{
    const struct VkPipelineShaderStageCreateInfo *stage;
    const struct d3d12_state_object_identifier *export;
    VkDeviceSize pipeline_stack_size = 0;
    uint32_t max_intersect_stack = 0;
    uint32_t max_callable_stack = 0;
    uint32_t max_closest_stack = 0;
    uint32_t max_anyhit_stack = 0;
    uint32_t max_raygen_stack = 0;
    uint32_t max_miss_stack = 0;
    size_t i;

    for (i = 0; i < data->exports_count; i++)
    {
        export = &data->exports[i];
        if (export->stack_size_general != UINT32_MAX)
        {
            stage = &data->stages[data->groups[i].generalShader];
            switch (stage->stage)
            {
                case VK_SHADER_STAGE_RAYGEN_BIT_KHR:
                    max_raygen_stack = max(max_raygen_stack, export->stack_size_general);
                    break;

                case VK_SHADER_STAGE_CALLABLE_BIT_KHR:
                    max_callable_stack = max(max_callable_stack, export->stack_size_general);
                    break;

                case VK_SHADER_STAGE_MISS_BIT_KHR:
                    max_miss_stack = max(max_miss_stack, export->stack_size_general);
                    break;

                default:
                    ERR("Unexpected stage #%x.\n", stage->stage);
                    return 0;
            }
        }

        if (export->stack_size_closest != UINT32_MAX)
            max_closest_stack = max(max_closest_stack, export->stack_size_closest);
        if (export->stack_size_intersection != UINT32_MAX)
            max_intersect_stack = max(max_intersect_stack, export->stack_size_intersection);
        if (export->stack_size_any != UINT32_MAX)
            max_anyhit_stack = max(max_anyhit_stack, export->stack_size_intersection);
    }

    /* Vulkan and DXR specs outline this same formula. We will use this as the default pipeline stack size. */
    pipeline_stack_size += max_raygen_stack;
    pipeline_stack_size += 2 * max_callable_stack;
    pipeline_stack_size += (max(1, recursion_depth) - 1) * max(max_closest_stack, max_miss_stack);
    pipeline_stack_size += recursion_depth * max(max(max_closest_stack, max_miss_stack), max_intersect_stack + max_anyhit_stack);
    return pipeline_stack_size;
}

static struct d3d12_root_signature *d3d12_state_object_pipeline_data_get_local_root_signature(
        struct d3d12_state_object_pipeline_data *data,
        const struct vkd3d_shader_library_entry_point *entry)
{
    size_t i;

    for (i = 0; i < data->associations_count; i++)
    {
        if (vkd3d_export_strequal(data->associations[i].export, entry->mangled_entry_point) ||
                vkd3d_export_strequal(data->associations[i].export, entry->plain_entry_point))
        {
            return data->associations[i].root_signature;
        }
    }

    if (data->high_priority_local_root_signature)
        return unsafe_impl_from_ID3D12RootSignature(data->high_priority_local_root_signature);
    else if (data->low_priority_local_root_signature)
        return unsafe_impl_from_ID3D12RootSignature(data->low_priority_local_root_signature);
    else
        return NULL;
}

static HRESULT d3d12_state_object_compile_pipeline(struct d3d12_state_object *object,
        struct d3d12_state_object_pipeline_data *data)
{
    const struct vkd3d_vk_device_procs *vk_procs = &object->device->vk_procs;
    struct vkd3d_shader_interface_local_info shader_interface_local_info;
    struct vkd3d_shader_interface_info shader_interface_info;
    VkRayTracingPipelineCreateInfoKHR pipeline_create_info;
    VkRayTracingShaderGroupCreateInfoKHR *group_create;
    struct vkd3d_shader_compile_arguments compile_args;
    VkPipelineDynamicStateCreateInfo dynamic_state;
    struct vkd3d_shader_library_entry_point *entry;
    struct d3d12_root_signature *global_signature;
    struct d3d12_root_signature *local_signature;
    const struct D3D12_HIT_GROUP_DESC *hit_group;
    VkPipelineShaderStageCreateInfo *stage;
    struct vkd3d_shader_code spirv;
    struct vkd3d_shader_code dxil;
    VkResult vr;
    size_t i;

    static const VkDynamicState dynamic_states[] = { VK_DYNAMIC_STATE_RAY_TRACING_PIPELINE_STACK_SIZE_KHR };

    memset(&compile_args, 0, sizeof(compile_args));
    compile_args.type = VKD3D_SHADER_STRUCTURE_TYPE_COMPILE_ARGUMENTS;
    compile_args.target_extensions = object->device->vk_info.shader_extensions;
    compile_args.target_extension_count = object->device->vk_info.shader_extension_count;
    compile_args.target = VKD3D_SHADER_TARGET_SPIRV_VULKAN_1_0;

    /* TODO: Allow different root signatures per module. */
    memset(&shader_interface_info, 0, sizeof(shader_interface_info));
    shader_interface_info.type = VKD3D_SHADER_STRUCTURE_TYPE_SHADER_INTERFACE_INFO;
    shader_interface_info.min_ssbo_alignment = d3d12_device_get_ssbo_alignment(object->device);

    global_signature = unsafe_impl_from_ID3D12RootSignature(data->global_root_signature);

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
    }

    shader_interface_local_info.descriptor_size = sizeof(struct d3d12_desc);

    for (i = 0; i < data->entry_points_count; i++)
    {
        entry = &data->entry_points[i];

        local_signature = d3d12_state_object_pipeline_data_get_local_root_signature(data, entry);
        if (local_signature)
        {
            shader_interface_local_info.local_root_parameters = local_signature->parameters;
            shader_interface_local_info.local_root_parameter_count = local_signature->parameter_count;
            shader_interface_local_info.shader_record_constant_buffers = local_signature->root_constants;
            shader_interface_local_info.shader_record_buffer_count = local_signature->root_constant_count;
            shader_interface_local_info.bindings = local_signature->bindings;
            shader_interface_local_info.binding_count = local_signature->binding_count;

            /* Promote state which might only be active in local root signature. */
            shader_interface_info.flags |= local_signature->flags;
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
            data->exports[data->exports_count].mangled_export = entry->mangled_entry_point;
            data->exports[data->exports_count].plain_export = entry->plain_entry_point;
            entry->mangled_entry_point = NULL;
            entry->plain_entry_point = NULL;

            vkd3d_array_reserve((void **) &data->groups, &data->groups_size,
                    data->groups_count + 1, sizeof(*data->groups));

            group_create = &data->groups[data->groups_count];
            group_create->sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
            group_create->pNext = NULL;
            group_create->type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
            /* Index into pStages. */
            group_create->generalShader = data->stages_count;
            group_create->closestHitShader = VK_SHADER_UNUSED_KHR;
            group_create->intersectionShader = VK_SHADER_UNUSED_KHR;
            group_create->anyHitShader = VK_SHADER_UNUSED_KHR;
            group_create->pShaderGroupCaptureReplayHandle = NULL;

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
            return E_OUTOFMEMORY;
        }

        stage->module = create_shader_module(object->device, spirv.code, spirv.size);
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
        data->exports[data->exports_count].mangled_export = NULL;
        data->exports[data->exports_count].plain_export = vkd3d_wstrdup(hit_group->HitGroupExport);

        vkd3d_array_reserve((void **) &data->groups, &data->groups_size,
                data->groups_count + 1, sizeof(*data->groups));

        group_create = &data->groups[data->groups_count];
        group_create->sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
        group_create->pNext = NULL;
        group_create->type = hit_group->Type == D3D12_HIT_GROUP_TYPE_TRIANGLES ?
                VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR :
                VK_RAY_TRACING_SHADER_GROUP_TYPE_PROCEDURAL_HIT_GROUP_KHR;

        group_create->generalShader = VK_SHADER_UNUSED_KHR;
        group_create->closestHitShader =
                d3d12_state_object_pipeline_data_find_entry(data, hit_group->ClosestHitShaderImport);
        group_create->intersectionShader =
                d3d12_state_object_pipeline_data_find_entry(data, hit_group->IntersectionShaderImport);
        group_create->anyHitShader =
                d3d12_state_object_pipeline_data_find_entry(data, hit_group->AnyHitShaderImport);
        group_create->pShaderGroupCaptureReplayHandle = NULL;

        data->exports_count++;
        data->groups_count++;
    }

    pipeline_create_info.sType = VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR;
    pipeline_create_info.pNext = NULL;
    pipeline_create_info.flags = 0;
    /* FIXME: What if we have no global root signature? */
    if (!global_signature)
        return E_INVALIDARG;
    pipeline_create_info.layout = global_signature->raygen.vk_pipeline_layout;
    pipeline_create_info.basePipelineHandle = VK_NULL_HANDLE;
    pipeline_create_info.basePipelineIndex = -1;
    pipeline_create_info.pGroups = data->groups;
    pipeline_create_info.groupCount = data->groups_count;
    pipeline_create_info.pStages = data->stages;
    pipeline_create_info.stageCount = data->stages_count;
    pipeline_create_info.pLibraryInfo = NULL;
    pipeline_create_info.pLibraryInterface = NULL;
    pipeline_create_info.pDynamicState = &dynamic_state;
    pipeline_create_info.maxPipelineRayRecursionDepth = data->pipeline_config->MaxTraceRecursionDepth;

    dynamic_state.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic_state.pNext = NULL;
    dynamic_state.flags = 0;
    dynamic_state.dynamicStateCount = 1;
    dynamic_state.pDynamicStates = dynamic_states;

    vr = VK_CALL(vkCreateRayTracingPipelinesKHR(object->device->vk_device, VK_NULL_HANDLE,
            VK_NULL_HANDLE, 1, &pipeline_create_info, NULL, &object->pipeline));
    if (vr)
        return hresult_from_vk_result(vr);

    for (i = 0; i < data->groups_count; i++)
    {
        vr = VK_CALL(vkGetRayTracingShaderGroupHandlesKHR(object->device->vk_device,
                object->pipeline, i, 1, sizeof(data->exports[i].identifier), data->exports[i].identifier));
        if (vr)
            return hresult_from_vk_result(vr);

        data->exports[i].stack_size_general = UINT32_MAX;
        data->exports[i].stack_size_any = UINT32_MAX;
        data->exports[i].stack_size_closest = UINT32_MAX;
        data->exports[i].stack_size_intersection = UINT32_MAX;

        if (data->groups[i].type == VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR)
        {
            data->exports[i].stack_size_general = get_shader_stack_size(object, i, VK_SHADER_GROUP_SHADER_GENERAL_KHR);
        }
        else
        {
            if (data->groups[i].anyHitShader != VK_SHADER_UNUSED_KHR)
                data->exports[i].stack_size_any = get_shader_stack_size(object, i, VK_SHADER_GROUP_SHADER_ANY_HIT_KHR);
            if (data->groups[i].closestHitShader != VK_SHADER_UNUSED_KHR)
                data->exports[i].stack_size_closest = get_shader_stack_size(object, i, VK_SHADER_GROUP_SHADER_CLOSEST_HIT_KHR);
            if (data->groups[i].intersectionShader != VK_SHADER_UNUSED_KHR)
                data->exports[i].stack_size_intersection = get_shader_stack_size(object, i, VK_SHADER_GROUP_SHADER_INTERSECTION_KHR);
        }
    }

    object->pipeline_stack_size = d3d12_state_object_pipeline_data_compute_default_stack_size(data,
            pipeline_create_info.maxPipelineRayRecursionDepth);

    /* Pilfer the export table. */
    object->exports = data->exports;
    object->exports_size = data->exports_size;
    object->exports_count = data->exports_count;
    data->exports = NULL;
    data->exports_size = 0;
    data->exports_count = 0;

    return S_OK;
}

static HRESULT d3d12_state_object_init(struct d3d12_state_object *object,
        struct d3d12_device *device,
        const D3D12_STATE_OBJECT_DESC *desc)
{
    struct d3d12_state_object_pipeline_data data;
    HRESULT hr = S_OK;
    object->ID3D12StateObject_iface.lpVtbl = &d3d12_state_object_vtbl;
    object->ID3D12StateObjectProperties_iface.lpVtbl = &d3d12_state_object_properties_vtbl;
    object->refcount = 1;
    object->device = device;
    memset(&data, 0, sizeof(data));

    if (desc->Type != D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE)
    {
        FIXME("Unsupported state object type: %u.\n", desc->Type);
        hr = E_INVALIDARG;
        goto fail;
    }

    if (FAILED(hr = d3d12_state_object_parse_subobjects(object, desc, &data)))
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
        struct d3d12_state_object **state_object)
{
    struct d3d12_state_object *object;
    HRESULT hr;

    if (!(object = vkd3d_calloc(1, sizeof(*object))))
        return E_OUTOFMEMORY;

    hr = d3d12_state_object_init(object, device, desc);
    if (FAILED(hr))
    {
        vkd3d_free(object);
        return hr;
    }

    *state_object = object;
    return S_OK;
}
