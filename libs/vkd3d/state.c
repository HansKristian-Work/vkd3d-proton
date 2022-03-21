/*
 * Copyright 2016 Józef Kucia for CodeWeavers
 * Copyright 2016 Henri Verbeet for CodeWeavers
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
#include "vkd3d_descriptor_debug.h"
#include "vkd3d_rw_spinlock.h"
#include <stdio.h>

/* ID3D12RootSignature */
static HRESULT STDMETHODCALLTYPE d3d12_root_signature_QueryInterface(ID3D12RootSignature *iface,
        REFIID riid, void **object)
{
    TRACE("iface %p, riid %s, object %p.\n", iface, debugstr_guid(riid), object);

    if (IsEqualGUID(riid, &IID_ID3D12RootSignature)
            || IsEqualGUID(riid, &IID_ID3D12DeviceChild)
            || IsEqualGUID(riid, &IID_ID3D12Object)
            || IsEqualGUID(riid, &IID_IUnknown))
    {
        ID3D12RootSignature_AddRef(iface);
        *object = iface;
        return S_OK;
    }

    WARN("%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid(riid));

    *object = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE d3d12_root_signature_AddRef(ID3D12RootSignature *iface)
{
    struct d3d12_root_signature *root_signature = impl_from_ID3D12RootSignature(iface);
    ULONG refcount = InterlockedIncrement(&root_signature->refcount);

    TRACE("%p increasing refcount to %u.\n", root_signature, refcount);

    if (refcount == 1)
    {
        d3d12_root_signature_inc_ref(root_signature);
        d3d12_device_add_ref(root_signature->device);
    }

    return refcount;
}

static void d3d12_root_signature_cleanup(struct d3d12_root_signature *root_signature,
        struct d3d12_device *device)
{
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;

    vkd3d_sampler_state_free_descriptor_set(&device->sampler_state, device,
            root_signature->vk_sampler_set, root_signature->vk_sampler_pool);

    VK_CALL(vkDestroyPipelineLayout(device->vk_device, root_signature->graphics.vk_pipeline_layout, NULL));
    VK_CALL(vkDestroyPipelineLayout(device->vk_device, root_signature->mesh.vk_pipeline_layout, NULL));
    VK_CALL(vkDestroyPipelineLayout(device->vk_device, root_signature->compute.vk_pipeline_layout, NULL));
    VK_CALL(vkDestroyPipelineLayout(device->vk_device, root_signature->raygen.vk_pipeline_layout, NULL));
    VK_CALL(vkDestroyDescriptorSetLayout(device->vk_device, root_signature->vk_sampler_descriptor_layout, NULL));
    VK_CALL(vkDestroyDescriptorSetLayout(device->vk_device, root_signature->vk_root_descriptor_layout, NULL));

    vkd3d_free(root_signature->parameters);
    vkd3d_free(root_signature->bindings);
    vkd3d_free(root_signature->root_constants);
    vkd3d_free(root_signature->static_samplers);
    vkd3d_free(root_signature->static_samplers_desc);
}

void d3d12_root_signature_inc_ref(struct d3d12_root_signature *root_signature)
{
    InterlockedIncrement(&root_signature->internal_refcount);
}

void d3d12_root_signature_dec_ref(struct d3d12_root_signature *root_signature)
{
    struct d3d12_device *device = root_signature->device;
    ULONG refcount = InterlockedDecrement(&root_signature->internal_refcount);

    if (refcount == 0)
    {
        vkd3d_private_store_destroy(&root_signature->private_store);
        d3d12_root_signature_cleanup(root_signature, device);
        vkd3d_free(root_signature);
    }
}

static ULONG STDMETHODCALLTYPE d3d12_root_signature_Release(ID3D12RootSignature *iface)
{
    struct d3d12_root_signature *root_signature = impl_from_ID3D12RootSignature(iface);
    struct d3d12_device *device = root_signature->device;
    ULONG refcount = InterlockedDecrement(&root_signature->refcount);

    TRACE("%p decreasing refcount to %u.\n", root_signature, refcount);

    if (!refcount)
    {
        d3d12_root_signature_dec_ref(root_signature);
        d3d12_device_release(device);
    }

    return refcount;
}

static HRESULT STDMETHODCALLTYPE d3d12_root_signature_GetPrivateData(ID3D12RootSignature *iface,
        REFGUID guid, UINT *data_size, void *data)
{
    struct d3d12_root_signature *root_signature = impl_from_ID3D12RootSignature(iface);

    TRACE("iface %p, guid %s, data_size %p, data %p.\n", iface, debugstr_guid(guid), data_size, data);

    return vkd3d_get_private_data(&root_signature->private_store, guid, data_size, data);
}

static HRESULT STDMETHODCALLTYPE d3d12_root_signature_SetPrivateData(ID3D12RootSignature *iface,
        REFGUID guid, UINT data_size, const void *data)
{
    struct d3d12_root_signature *root_signature = impl_from_ID3D12RootSignature(iface);

    TRACE("iface %p, guid %s, data_size %u, data %p.\n", iface, debugstr_guid(guid), data_size, data);

    return vkd3d_set_private_data(&root_signature->private_store, guid, data_size, data,
            NULL, NULL);
}

static HRESULT STDMETHODCALLTYPE d3d12_root_signature_SetPrivateDataInterface(ID3D12RootSignature *iface,
        REFGUID guid, const IUnknown *data)
{
    struct d3d12_root_signature *root_signature = impl_from_ID3D12RootSignature(iface);

    TRACE("iface %p, guid %s, data %p.\n", iface, debugstr_guid(guid), data);

    return vkd3d_set_private_data_interface(&root_signature->private_store, guid, data,
            NULL, NULL);
}

static HRESULT STDMETHODCALLTYPE d3d12_root_signature_GetDevice(ID3D12RootSignature *iface,
        REFIID iid, void **device)
{
    struct d3d12_root_signature *root_signature = impl_from_ID3D12RootSignature(iface);

    TRACE("iface %p, iid %s, device %p.\n", iface, debugstr_guid(iid), device);

    return d3d12_device_query_interface(root_signature->device, iid, device);
}

CONST_VTBL struct ID3D12RootSignatureVtbl d3d12_root_signature_vtbl =
{
    /* IUnknown methods */
    d3d12_root_signature_QueryInterface,
    d3d12_root_signature_AddRef,
    d3d12_root_signature_Release,
    /* ID3D12Object methods */
    d3d12_root_signature_GetPrivateData,
    d3d12_root_signature_SetPrivateData,
    d3d12_root_signature_SetPrivateDataInterface,
    (void *)d3d12_object_SetName,
    /* ID3D12DeviceChild methods */
    d3d12_root_signature_GetDevice,
};

VkShaderStageFlags vkd3d_vk_stage_flags_from_visibility(D3D12_SHADER_VISIBILITY visibility)
{
    switch (visibility)
    {
        case D3D12_SHADER_VISIBILITY_ALL:
            return VK_SHADER_STAGE_ALL;
        case D3D12_SHADER_VISIBILITY_VERTEX:
            return VK_SHADER_STAGE_VERTEX_BIT;
        case D3D12_SHADER_VISIBILITY_HULL:
            return VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT;
        case D3D12_SHADER_VISIBILITY_DOMAIN:
            return VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;
        case D3D12_SHADER_VISIBILITY_GEOMETRY:
            return VK_SHADER_STAGE_GEOMETRY_BIT;
        case D3D12_SHADER_VISIBILITY_PIXEL:
            return VK_SHADER_STAGE_FRAGMENT_BIT;
        case D3D12_SHADER_VISIBILITY_AMPLIFICATION:
            return VK_SHADER_STAGE_TASK_BIT_EXT;
        case D3D12_SHADER_VISIBILITY_MESH:
            return VK_SHADER_STAGE_MESH_BIT_EXT;
        default:
            return 0;
    }
}

enum vkd3d_shader_visibility vkd3d_shader_visibility_from_d3d12(D3D12_SHADER_VISIBILITY visibility)
{
    switch (visibility)
    {
        case D3D12_SHADER_VISIBILITY_ALL:
            return VKD3D_SHADER_VISIBILITY_ALL;
        case D3D12_SHADER_VISIBILITY_VERTEX:
            return VKD3D_SHADER_VISIBILITY_VERTEX;
        case D3D12_SHADER_VISIBILITY_HULL:
            return VKD3D_SHADER_VISIBILITY_HULL;
        case D3D12_SHADER_VISIBILITY_DOMAIN:
            return VKD3D_SHADER_VISIBILITY_DOMAIN;
        case D3D12_SHADER_VISIBILITY_GEOMETRY:
            return VKD3D_SHADER_VISIBILITY_GEOMETRY;
        case D3D12_SHADER_VISIBILITY_PIXEL:
            return VKD3D_SHADER_VISIBILITY_PIXEL;
        default:
            FIXME("Unhandled visibility %#x.\n", visibility);
            return VKD3D_SHADER_VISIBILITY_ALL;
    }
}

static VkDescriptorType vk_descriptor_type_from_d3d12_root_parameter(struct d3d12_device *device, D3D12_ROOT_PARAMETER_TYPE type)
{
    bool use_ssbo = d3d12_device_use_ssbo_root_descriptors(device);

    switch (type)
    {
        /* SRV and UAV root parameters are buffer views. */
        case D3D12_ROOT_PARAMETER_TYPE_SRV:
            return use_ssbo ? VK_DESCRIPTOR_TYPE_STORAGE_BUFFER : VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
        case D3D12_ROOT_PARAMETER_TYPE_UAV:
            return use_ssbo ? VK_DESCRIPTOR_TYPE_STORAGE_BUFFER : VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER;
        case D3D12_ROOT_PARAMETER_TYPE_CBV:
            return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        default:
            FIXME("Unhandled descriptor root parameter type %#x.\n", type);
            return VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    }
}

static enum vkd3d_shader_descriptor_type vkd3d_descriptor_type_from_d3d12_range_type(
        D3D12_DESCRIPTOR_RANGE_TYPE type)
{
    switch (type)
    {
        case D3D12_DESCRIPTOR_RANGE_TYPE_SRV:
            return VKD3D_SHADER_DESCRIPTOR_TYPE_SRV;
        case D3D12_DESCRIPTOR_RANGE_TYPE_UAV:
            return VKD3D_SHADER_DESCRIPTOR_TYPE_UAV;
        case D3D12_DESCRIPTOR_RANGE_TYPE_CBV:
            return VKD3D_SHADER_DESCRIPTOR_TYPE_CBV;
        case D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER:
            return VKD3D_SHADER_DESCRIPTOR_TYPE_SAMPLER;
        default:
            FIXME("Unhandled descriptor range type type %#x.\n", type);
            return VKD3D_SHADER_DESCRIPTOR_TYPE_SRV;
    }
}

static enum vkd3d_shader_descriptor_type vkd3d_descriptor_type_from_d3d12_root_parameter_type(
        D3D12_ROOT_PARAMETER_TYPE type)
{
    switch (type)
    {
        case D3D12_ROOT_PARAMETER_TYPE_SRV:
            return VKD3D_SHADER_DESCRIPTOR_TYPE_SRV;
        case D3D12_ROOT_PARAMETER_TYPE_UAV:
            return VKD3D_SHADER_DESCRIPTOR_TYPE_UAV;
        case D3D12_ROOT_PARAMETER_TYPE_CBV:
            return VKD3D_SHADER_DESCRIPTOR_TYPE_CBV;
        default:
            FIXME("Unhandled descriptor root parameter type %#x.\n", type);
            return VKD3D_SHADER_DESCRIPTOR_TYPE_SRV;
    }
}

HRESULT vkd3d_create_descriptor_set_layout(struct d3d12_device *device,
        VkDescriptorSetLayoutCreateFlags flags, unsigned int binding_count,
        const VkDescriptorSetLayoutBinding *bindings, VkDescriptorSetLayout *set_layout)
{
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    VkDescriptorSetLayoutCreateInfo set_desc;
    VkResult vr;

    set_desc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    set_desc.pNext = NULL;
    set_desc.flags = flags;
    set_desc.bindingCount = binding_count;
    set_desc.pBindings = bindings;

    if ((vr = VK_CALL(vkCreateDescriptorSetLayout(device->vk_device, &set_desc, NULL, set_layout))) < 0)
    {
        WARN("Failed to create Vulkan descriptor set layout, vr %d.\n", vr);
        return hresult_from_vk_result(vr);
    }

    return S_OK;
}

HRESULT vkd3d_create_pipeline_layout(struct d3d12_device *device,
        unsigned int set_layout_count, const VkDescriptorSetLayout *set_layouts,
        unsigned int push_constant_count, const VkPushConstantRange *push_constants,
        VkPipelineLayout *pipeline_layout)
{
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    struct VkPipelineLayoutCreateInfo pipeline_layout_info;
    VkResult vr;

    if (set_layout_count > device->vk_info.device_limits.maxBoundDescriptorSets)
    {
        ERR("Root signature requires %u descriptor sets, but device only supports %u.\n",
            set_layout_count, device->vk_info.device_limits.maxBoundDescriptorSets);
        return E_INVALIDARG;
    }

    pipeline_layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipeline_layout_info.pNext = NULL;
    pipeline_layout_info.flags = 0;
    pipeline_layout_info.setLayoutCount = set_layout_count;
    pipeline_layout_info.pSetLayouts = set_layouts;
    pipeline_layout_info.pushConstantRangeCount = push_constant_count;
    pipeline_layout_info.pPushConstantRanges = push_constants;
    if ((vr = VK_CALL(vkCreatePipelineLayout(device->vk_device,
            &pipeline_layout_info, NULL, pipeline_layout))) < 0)
    {
        WARN("Failed to create Vulkan pipeline layout, vr %d.\n", vr);
        return hresult_from_vk_result(vr);
    }

    return S_OK;
}

static HRESULT vkd3d_create_pipeline_layout_for_stage_mask(struct d3d12_device *device,
        unsigned int set_layout_count, const VkDescriptorSetLayout *set_layouts,
        const VkPushConstantRange *push_constants,
        VkShaderStageFlags stages,
        struct d3d12_bind_point_layout *bind_point_layout)
{
    VkPushConstantRange range;
    /* Can just mask directly since STAGE_ALL and ALL_GRAPHICS are OR masks. */
    range.stageFlags = push_constants->stageFlags & stages;
    range.offset = push_constants->offset;
    range.size = push_constants->size;

    bind_point_layout->vk_push_stages = range.stageFlags;
    return vkd3d_create_pipeline_layout(device, set_layout_count, set_layouts,
            range.stageFlags ? 1 : 0, &range,
            &bind_point_layout->vk_pipeline_layout);
}

struct d3d12_root_signature_info
{
    uint32_t binding_count;
    uint32_t descriptor_count;
    uint32_t parameter_count;

    uint32_t push_descriptor_count;
    uint32_t root_constant_count;
    uint32_t hoist_descriptor_count;
    bool has_raw_va_aux_buffer;
    bool has_ssbo_offset_buffer;
    bool has_typed_offset_buffer;

    uint32_t cost;
};

static bool d3d12_descriptor_range_can_hoist_cbv_descriptor(
        struct d3d12_device *device, const D3D12_DESCRIPTOR_RANGE1 *range)
{
    /* Cannot/should not hoist arrays.
     * We only care about CBVs. SRVs and UAVs are too fiddly
     * since they don't necessary map to buffers at all. */
    if (!(device->bindless_state.flags & VKD3D_HOIST_STATIC_TABLE_CBV) ||
            range->RangeType != D3D12_DESCRIPTOR_RANGE_TYPE_CBV ||
            range->NumDescriptors != 1)
    {
        return false;
    }

    /* If descriptors are not marked volatile, we are guaranteed that the descriptors are
     * set before updating the root table parameter in the command list.
     * We can latch the descriptor at draw time.
     * As a speed hack, we can pretend that all CBVs have this flag set.
     * Basically no applications set this flag, even though they really could. */
    return !(range->Flags & D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE) ||
            (vkd3d_config_flags & VKD3D_CONFIG_FLAG_FORCE_STATIC_CBV);
}

static void d3d12_root_signature_info_count_srv_uav_table(struct d3d12_root_signature_info *info,
        struct d3d12_device *device)
{
    /* separate image + buffer descriptors + aux buffer descriptor. */
    info->binding_count += 3;

    if (device->bindless_state.flags & VKD3D_BINDLESS_RAW_SSBO)
        info->binding_count += 1;

    if (device->bindless_state.flags & VKD3D_RAW_VA_AUX_BUFFER)
        info->has_raw_va_aux_buffer = true;
    if (device->bindless_state.flags & VKD3D_SSBO_OFFSET_BUFFER)
        info->has_ssbo_offset_buffer = true;
    if (device->bindless_state.flags & VKD3D_TYPED_OFFSET_BUFFER)
        info->has_typed_offset_buffer = true;
}

static void d3d12_root_signature_info_count_cbv_table(struct d3d12_root_signature_info *info)
{
    info->binding_count += 1;
}

static void d3d12_root_signature_info_count_sampler_table(struct d3d12_root_signature_info *info)
{
    info->binding_count += 1;
}

static HRESULT d3d12_root_signature_info_count_descriptors(struct d3d12_root_signature_info *info,
        struct d3d12_device *device, const D3D12_ROOT_SIGNATURE_DESC1 *desc, const D3D12_DESCRIPTOR_RANGE1 *range)
{
    switch (range->RangeType)
    {
        case D3D12_DESCRIPTOR_RANGE_TYPE_SRV:
        case D3D12_DESCRIPTOR_RANGE_TYPE_UAV:
            d3d12_root_signature_info_count_srv_uav_table(info, device);
            break;
        case D3D12_DESCRIPTOR_RANGE_TYPE_CBV:
            if (!(desc->Flags & D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE) &&
                    d3d12_descriptor_range_can_hoist_cbv_descriptor(device, range))
            {
                info->hoist_descriptor_count += 1;
            }
            d3d12_root_signature_info_count_cbv_table(info);
            break;
        case D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER:
            d3d12_root_signature_info_count_sampler_table(info);
            break;
        default:
            FIXME("Unhandled descriptor type %#x.\n", range->RangeType);
            return E_NOTIMPL;
    }

    return S_OK;
}

static HRESULT d3d12_root_signature_info_from_desc(struct d3d12_root_signature_info *info,
        struct d3d12_device *device, const D3D12_ROOT_SIGNATURE_DESC1 *desc)
{
    bool local_root_signature;
    unsigned int i, j;
    HRESULT hr;

    memset(info, 0, sizeof(*info));

    local_root_signature = !!(desc->Flags & D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE);

    /* Need to emit bindings for the magic internal table binding. */
    if (desc->Flags & D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED)
    {
        d3d12_root_signature_info_count_srv_uav_table(info, device);
        d3d12_root_signature_info_count_srv_uav_table(info, device);
        d3d12_root_signature_info_count_cbv_table(info);
    }

    if (desc->Flags & D3D12_ROOT_SIGNATURE_FLAG_SAMPLER_HEAP_DIRECTLY_INDEXED)
        d3d12_root_signature_info_count_sampler_table(info);

    for (i = 0; i < desc->NumParameters; ++i)
    {
        const D3D12_ROOT_PARAMETER1 *p = &desc->pParameters[i];

        switch (p->ParameterType)
        {
            case D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE:
                for (j = 0; j < p->DescriptorTable.NumDescriptorRanges; ++j)
                    if (FAILED(hr = d3d12_root_signature_info_count_descriptors(info,
                            device, desc, &p->DescriptorTable.pDescriptorRanges[j])))
                        return hr;

                /* Local root signature directly affects memory layout. */
                if (local_root_signature)
                    info->cost = (info->cost + 1u) & ~1u;
                info->cost += local_root_signature ? 2 : 1;
                break;

            case D3D12_ROOT_PARAMETER_TYPE_CBV:

                /* Local root signature directly affects memory layout. */
                if (local_root_signature)
                    info->cost = (info->cost + 1u) & ~1u;
                else if (!(device->bindless_state.flags & VKD3D_RAW_VA_ROOT_DESCRIPTOR_CBV))
                    info->push_descriptor_count += 1;

                info->binding_count += 1;
                info->cost += 2;
                break;

            case D3D12_ROOT_PARAMETER_TYPE_SRV:
            case D3D12_ROOT_PARAMETER_TYPE_UAV:
                /* Local root signature directly affects memory layout. */
                if (local_root_signature)
                    info->cost = (info->cost + 1u) & ~1u;
                else if (!(device->bindless_state.flags & VKD3D_RAW_VA_ROOT_DESCRIPTOR_SRV_UAV))
                    info->push_descriptor_count += 1;

                info->binding_count += 1;
                info->cost += 2;
                break;

            case D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS:
                info->root_constant_count += 1;
                info->cost += p->Constants.Num32BitValues;
                break;

            default:
                FIXME("Unhandled type %#x for parameter %u.\n", p->ParameterType, i);
                return E_NOTIMPL;
        }
    }

    if (!local_root_signature)
    {
        info->push_descriptor_count += info->hoist_descriptor_count;
        info->hoist_descriptor_count = min(info->hoist_descriptor_count, VKD3D_MAX_HOISTED_DESCRIPTORS);
        info->hoist_descriptor_count = min(info->hoist_descriptor_count, D3D12_MAX_ROOT_COST - desc->NumParameters);
        info->binding_count += info->hoist_descriptor_count;
        info->binding_count += desc->NumStaticSamplers;
    }

    info->parameter_count = desc->NumParameters + info->hoist_descriptor_count;
    return S_OK;
}

static bool d3d12_root_signature_parameter_is_raw_va(struct d3d12_root_signature *root_signature,
        D3D12_ROOT_PARAMETER_TYPE type)
{
    if (type == D3D12_ROOT_PARAMETER_TYPE_CBV)
        return !!(root_signature->device->bindless_state.flags & VKD3D_RAW_VA_ROOT_DESCRIPTOR_CBV);
    else if (type == D3D12_ROOT_PARAMETER_TYPE_SRV || type == D3D12_ROOT_PARAMETER_TYPE_UAV)
        return !!(root_signature->device->bindless_state.flags & VKD3D_RAW_VA_ROOT_DESCRIPTOR_SRV_UAV);
    else
        return false;
}

static HRESULT d3d12_root_signature_init_shader_record_constants(
        struct d3d12_root_signature *root_signature,
        const D3D12_ROOT_SIGNATURE_DESC1 *desc, const struct d3d12_root_signature_info *info)
{
    unsigned int i, j;

    for (i = 0, j = 0; i < desc->NumParameters; ++i)
    {
        const D3D12_ROOT_PARAMETER1 *p = &desc->pParameters[i];

        if (p->ParameterType != D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS)
            continue;

        root_signature->parameters[i].parameter_type = p->ParameterType;
        root_signature->parameters[i].constant.constant_index = j;
        root_signature->parameters[i].constant.constant_count = p->Constants.Num32BitValues;

        root_signature->root_constants[j].register_space = p->Constants.RegisterSpace;
        root_signature->root_constants[j].register_index = p->Constants.ShaderRegister;
        root_signature->root_constants[j].shader_visibility = vkd3d_shader_visibility_from_d3d12(p->ShaderVisibility);
        root_signature->root_constants[j].offset = 0;
        root_signature->root_constants[j].size = p->Constants.Num32BitValues * sizeof(uint32_t);

        ++j;
    }

    return S_OK;
}

static HRESULT d3d12_root_signature_init_push_constants(struct d3d12_root_signature *root_signature,
        const D3D12_ROOT_SIGNATURE_DESC1 *desc, const struct d3d12_root_signature_info *info,
        struct VkPushConstantRange *push_constant_range)
{
    unsigned int i, j;

    /* Stages set later. */
    push_constant_range->stageFlags = 0;
    push_constant_range->offset = 0;
    push_constant_range->size = 0;

    /* Put root descriptor VAs at the start to avoid alignment issues */
    for (i = 0; i < desc->NumParameters; ++i)
    {
        const D3D12_ROOT_PARAMETER1 *p = &desc->pParameters[i];

        if (d3d12_root_signature_parameter_is_raw_va(root_signature, p->ParameterType))
        {
            push_constant_range->stageFlags |= vkd3d_vk_stage_flags_from_visibility(p->ShaderVisibility);
            push_constant_range->size += sizeof(VkDeviceSize);
        }
    }

    /* Append actual root constants */
    for (i = 0, j = 0; i < desc->NumParameters; ++i)
    {
        const D3D12_ROOT_PARAMETER1 *p = &desc->pParameters[i];

        if (p->ParameterType != D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS)
            continue;

        root_signature->root_constant_mask |= 1ull << i;

        root_signature->parameters[i].parameter_type = p->ParameterType;
        root_signature->parameters[i].constant.constant_index = push_constant_range->size / sizeof(uint32_t);
        root_signature->parameters[i].constant.constant_count = p->Constants.Num32BitValues;

        root_signature->root_constants[j].register_space = p->Constants.RegisterSpace;
        root_signature->root_constants[j].register_index = p->Constants.ShaderRegister;
        root_signature->root_constants[j].shader_visibility = vkd3d_shader_visibility_from_d3d12(p->ShaderVisibility);
        root_signature->root_constants[j].offset = push_constant_range->size;
        root_signature->root_constants[j].size = p->Constants.Num32BitValues * sizeof(uint32_t);

        push_constant_range->stageFlags |= vkd3d_vk_stage_flags_from_visibility(p->ShaderVisibility);
        push_constant_range->size += p->Constants.Num32BitValues * sizeof(uint32_t);

        ++j;
    }

    /* Append one 32-bit push constant for each descriptor table offset */
    if (root_signature->device->bindless_state.flags)
    {
        root_signature->descriptor_table_offset = push_constant_range->size;

        for (i = 0; i < desc->NumParameters; ++i)
        {
            const D3D12_ROOT_PARAMETER1 *p = &desc->pParameters[i];

            if (p->ParameterType != D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE)
                continue;

            root_signature->descriptor_table_count += 1;

            push_constant_range->stageFlags |= vkd3d_vk_stage_flags_from_visibility(p->ShaderVisibility);
            push_constant_range->size += sizeof(uint32_t);
        }
    }

    return S_OK;
}

struct vkd3d_descriptor_set_context
{
    uint32_t binding_index;
    uint32_t vk_set;
    uint32_t vk_binding;
};

static enum vkd3d_bindless_set_flag vkd3d_bindless_set_flag_from_descriptor_range_type(D3D12_DESCRIPTOR_RANGE_TYPE range_type)
{
    switch (range_type)
    {
        case D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER:
            return VKD3D_BINDLESS_SET_SAMPLER;
        case D3D12_DESCRIPTOR_RANGE_TYPE_CBV:
            return VKD3D_BINDLESS_SET_CBV;
        case D3D12_DESCRIPTOR_RANGE_TYPE_SRV:
            return VKD3D_BINDLESS_SET_SRV;
        case D3D12_DESCRIPTOR_RANGE_TYPE_UAV:
            return VKD3D_BINDLESS_SET_UAV;
        default:
            ERR("Unhandled descriptor range type %u.\n", range_type);
            return VKD3D_BINDLESS_SET_SRV;
    }
}

static void vkd3d_shader_resource_binding_init_global_heap(struct vkd3d_shader_resource_binding *binding,
        D3D12_DESCRIPTOR_RANGE_TYPE range_type)
{
    binding->type = vkd3d_descriptor_type_from_d3d12_range_type(range_type);
    binding->register_space = UINT32_MAX;
    binding->register_index = UINT32_MAX;
    binding->register_count = UINT32_MAX;
    binding->shader_visibility = VKD3D_SHADER_VISIBILITY_ALL;
    binding->descriptor_table = 0; /* Ignored. */
    binding->descriptor_offset = 0; /* Ignored. */
}

static void d3d12_root_signature_init_srv_uav_binding(struct d3d12_root_signature *root_signature,
        struct vkd3d_descriptor_set_context *context, D3D12_DESCRIPTOR_RANGE_TYPE range_type,
        struct vkd3d_shader_resource_binding *binding,
        struct vkd3d_shader_resource_binding *out_bindings_base, uint32_t *out_index)
{
    struct vkd3d_bindless_state *bindless_state = &root_signature->device->bindless_state;
    enum vkd3d_bindless_set_flag range_flag;
    bool has_aux_buffer;

    range_flag = vkd3d_bindless_set_flag_from_descriptor_range_type(range_type);
    binding->flags = VKD3D_SHADER_BINDING_FLAG_BINDLESS | VKD3D_SHADER_BINDING_FLAG_AUX_BUFFER;
    has_aux_buffer = false;

    if (root_signature->flags & VKD3D_ROOT_SIGNATURE_USE_RAW_VA_AUX_BUFFER)
    {
        binding->flags |= VKD3D_SHADER_BINDING_FLAG_RAW_VA;
        binding->binding = root_signature->raw_va_aux_buffer_binding;
        has_aux_buffer = true;
    }
    else if (range_type == D3D12_DESCRIPTOR_RANGE_TYPE_UAV)
    {
        /* There is no fallback heap for RTAS (SRV), this is only relevant for UAV counters. */
        if (vkd3d_bindless_state_find_binding(bindless_state, range_flag | VKD3D_BINDLESS_SET_AUX_BUFFER, &binding->binding))
            has_aux_buffer = true;
        else
            ERR("Failed to find aux buffer binding.\n");
    }

    if (has_aux_buffer)
        out_bindings_base[(*out_index)++] = *binding;

    if (vkd3d_bindless_state_find_binding(bindless_state, range_flag | VKD3D_BINDLESS_SET_BUFFER, &binding->binding))
    {
        binding->flags = VKD3D_SHADER_BINDING_FLAG_BINDLESS | VKD3D_SHADER_BINDING_FLAG_BUFFER;
        out_bindings_base[(*out_index)++] = *binding;
    }

    if (vkd3d_bindless_state_find_binding(bindless_state, range_flag | VKD3D_BINDLESS_SET_RAW_SSBO, &binding->binding))
    {
        binding->flags = VKD3D_SHADER_BINDING_FLAG_BINDLESS | VKD3D_SHADER_BINDING_FLAG_BUFFER | VKD3D_SHADER_BINDING_FLAG_RAW_SSBO;
        out_bindings_base[(*out_index)++] = *binding;
    }

    if (vkd3d_bindless_state_find_binding(bindless_state, range_flag | VKD3D_BINDLESS_SET_IMAGE, &binding->binding))
    {
        binding->flags = VKD3D_SHADER_BINDING_FLAG_BINDLESS | VKD3D_SHADER_BINDING_FLAG_IMAGE;
        out_bindings_base[(*out_index)++] = *binding;
    }
}

static void d3d12_root_signature_init_srv_uav_heap_bindings(struct d3d12_root_signature *root_signature,
        struct vkd3d_descriptor_set_context *context, D3D12_DESCRIPTOR_RANGE_TYPE range_type)
{
    struct vkd3d_shader_resource_binding binding;
    vkd3d_shader_resource_binding_init_global_heap(&binding, range_type);
    d3d12_root_signature_init_srv_uav_binding(root_signature, context, range_type, &binding,
            root_signature->bindings, &context->binding_index);
}

static void d3d12_root_signature_init_cbv_srv_uav_heap_bindings(struct d3d12_root_signature *root_signature,
        struct vkd3d_descriptor_set_context *context)
{
    struct vkd3d_bindless_state *bindless_state = &root_signature->device->bindless_state;
    struct vkd3d_shader_resource_binding binding;

    d3d12_root_signature_init_srv_uav_heap_bindings(root_signature, context, D3D12_DESCRIPTOR_RANGE_TYPE_SRV);
    d3d12_root_signature_init_srv_uav_heap_bindings(root_signature, context, D3D12_DESCRIPTOR_RANGE_TYPE_UAV);

    vkd3d_shader_resource_binding_init_global_heap(&binding, D3D12_DESCRIPTOR_RANGE_TYPE_CBV);
    if (vkd3d_bindless_state_find_binding(bindless_state, VKD3D_BINDLESS_SET_CBV, &binding.binding))
    {
        binding.flags = VKD3D_SHADER_BINDING_FLAG_BINDLESS | VKD3D_SHADER_BINDING_FLAG_BUFFER;
        root_signature->bindings[context->binding_index++] = binding;
    }
}

static void d3d12_root_signature_init_sampler_heap_bindings(struct d3d12_root_signature *root_signature,
        struct vkd3d_descriptor_set_context *context)
{
    struct vkd3d_bindless_state *bindless_state = &root_signature->device->bindless_state;
    struct vkd3d_shader_resource_binding binding;

    vkd3d_shader_resource_binding_init_global_heap(&binding, D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER);
    if (vkd3d_bindless_state_find_binding(bindless_state, VKD3D_BINDLESS_SET_SAMPLER, &binding.binding))
    {
        binding.flags = VKD3D_SHADER_BINDING_FLAG_BINDLESS | VKD3D_SHADER_BINDING_FLAG_IMAGE;
        root_signature->bindings[context->binding_index++] = binding;
    }
}

static HRESULT d3d12_root_signature_init_root_descriptor_tables(struct d3d12_root_signature *root_signature,
        const D3D12_ROOT_SIGNATURE_DESC1 *desc, const struct d3d12_root_signature_info *info,
        struct vkd3d_descriptor_set_context *context)
{
    struct vkd3d_bindless_state *bindless_state = &root_signature->device->bindless_state;
    struct vkd3d_shader_resource_binding binding;
    struct vkd3d_shader_descriptor_table *table;
    unsigned int i, j, t, range_count;
    uint32_t range_descriptor_offset;
    bool local_root_signature;

    local_root_signature = !!(desc->Flags & D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE);

    if (desc->Flags & D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED)
        d3d12_root_signature_init_cbv_srv_uav_heap_bindings(root_signature, context);
    if (desc->Flags & D3D12_ROOT_SIGNATURE_FLAG_SAMPLER_HEAP_DIRECTLY_INDEXED)
        d3d12_root_signature_init_sampler_heap_bindings(root_signature, context);

    for (i = 0, t = 0; i < desc->NumParameters; ++i)
    {
        const D3D12_ROOT_PARAMETER1 *p = &desc->pParameters[i];
        if (p->ParameterType != D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE)
            continue;

        if (!local_root_signature)
            root_signature->descriptor_table_mask |= 1ull << i;

        table = &root_signature->parameters[i].descriptor_table;
        range_count = p->DescriptorTable.NumDescriptorRanges;
        range_descriptor_offset = 0;

        root_signature->parameters[i].parameter_type = p->ParameterType;

        if (local_root_signature)
            table->table_index = i;
        else
            table->table_index = t++;

        table->binding_count = 0;
        table->first_binding = &root_signature->bindings[context->binding_index];

        for (j = 0; j < range_count; ++j)
        {
            const D3D12_DESCRIPTOR_RANGE1 *range = &p->DescriptorTable.pDescriptorRanges[j];
            enum vkd3d_bindless_set_flag range_flag = vkd3d_bindless_set_flag_from_descriptor_range_type(range->RangeType);

            if (range->OffsetInDescriptorsFromTableStart != D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND)
                range_descriptor_offset = range->OffsetInDescriptorsFromTableStart;

            binding.type = vkd3d_descriptor_type_from_d3d12_range_type(range->RangeType);
            binding.register_space = range->RegisterSpace;
            binding.register_index = range->BaseShaderRegister;
            binding.register_count = range->NumDescriptors;
            binding.descriptor_table = table->table_index;
            binding.descriptor_offset = range_descriptor_offset;
            binding.shader_visibility = vkd3d_shader_visibility_from_d3d12(p->ShaderVisibility);

            switch (range->RangeType)
            {
                case D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER:
                    if (vkd3d_bindless_state_find_binding(bindless_state, range_flag, &binding.binding))
                    {
                        binding.flags = VKD3D_SHADER_BINDING_FLAG_BINDLESS | VKD3D_SHADER_BINDING_FLAG_IMAGE;
                        table->first_binding[table->binding_count++] = binding;
                    }
                    break;
                case D3D12_DESCRIPTOR_RANGE_TYPE_CBV:
                    if (vkd3d_bindless_state_find_binding(bindless_state, range_flag, &binding.binding))
                    {
                        binding.flags = VKD3D_SHADER_BINDING_FLAG_BINDLESS | VKD3D_SHADER_BINDING_FLAG_BUFFER;
                        table->first_binding[table->binding_count++] = binding;
                    }
                    break;
                case D3D12_DESCRIPTOR_RANGE_TYPE_UAV:
                case D3D12_DESCRIPTOR_RANGE_TYPE_SRV:
                    d3d12_root_signature_init_srv_uav_binding(root_signature, context, range->RangeType,
                            &binding, table->first_binding, &table->binding_count);
                    break;
                default:
                    FIXME("Unhandled descriptor range type %u.\n", range->RangeType);
            }

            range_descriptor_offset = binding.descriptor_offset + binding.register_count;
        }

        context->binding_index += table->binding_count;
    }

    return S_OK;
}

static void d3d12_root_signature_init_extra_bindings(struct d3d12_root_signature *root_signature,
        const struct d3d12_root_signature_info *info)
{
    if (info->has_raw_va_aux_buffer)
    {
        root_signature->flags |= VKD3D_ROOT_SIGNATURE_USE_RAW_VA_AUX_BUFFER;

        vkd3d_bindless_state_find_binding(&root_signature->device->bindless_state,
                VKD3D_BINDLESS_SET_EXTRA_RAW_VA_AUX_BUFFER,
                &root_signature->raw_va_aux_buffer_binding);
    }

    if (info->has_ssbo_offset_buffer || info->has_typed_offset_buffer)
    {
        if (info->has_ssbo_offset_buffer)
            root_signature->flags |= VKD3D_ROOT_SIGNATURE_USE_SSBO_OFFSET_BUFFER;
        if (info->has_typed_offset_buffer)
            root_signature->flags |= VKD3D_ROOT_SIGNATURE_USE_TYPED_OFFSET_BUFFER;

        vkd3d_bindless_state_find_binding(&root_signature->device->bindless_state,
                VKD3D_BINDLESS_SET_EXTRA_OFFSET_BUFFER,
                &root_signature->offset_buffer_binding);
    }

#ifdef VKD3D_ENABLE_DESCRIPTOR_QA
    if (vkd3d_descriptor_debug_active_qa_checks())
    {
        vkd3d_bindless_state_find_binding(&root_signature->device->bindless_state,
                VKD3D_BINDLESS_SET_EXTRA_DESCRIPTOR_HEAP_INFO_BUFFER,
                &root_signature->descriptor_qa_heap_binding);
        vkd3d_bindless_state_find_binding(&root_signature->device->bindless_state,
                VKD3D_BINDLESS_SET_EXTRA_GLOBAL_HEAP_INFO_BUFFER,
                &root_signature->descriptor_qa_global_info);
    }
#endif
}

static HRESULT d3d12_root_signature_init_shader_record_descriptors(
        struct d3d12_root_signature *root_signature,
        const D3D12_ROOT_SIGNATURE_DESC1 *desc, const struct d3d12_root_signature_info *info,
        struct vkd3d_descriptor_set_context *context)
{
    struct vkd3d_shader_resource_binding *binding;
    struct vkd3d_shader_root_parameter *param;
    unsigned int i;

    for (i = 0; i < desc->NumParameters; ++i)
    {
        const D3D12_ROOT_PARAMETER1 *p = &desc->pParameters[i];

        if (p->ParameterType != D3D12_ROOT_PARAMETER_TYPE_CBV
            && p->ParameterType != D3D12_ROOT_PARAMETER_TYPE_SRV
            && p->ParameterType != D3D12_ROOT_PARAMETER_TYPE_UAV)
            continue;

        binding = &root_signature->bindings[context->binding_index];
        binding->type = vkd3d_descriptor_type_from_d3d12_root_parameter_type(p->ParameterType);
        binding->register_space = p->Descriptor.RegisterSpace;
        binding->register_index = p->Descriptor.ShaderRegister;
        binding->register_count = 1;
        binding->descriptor_table = 0;  /* ignored */
        binding->descriptor_offset = 0; /* ignored */
        binding->shader_visibility = vkd3d_shader_visibility_from_d3d12(p->ShaderVisibility);
        binding->flags = VKD3D_SHADER_BINDING_FLAG_BUFFER;
        binding->binding.binding = 0; /* ignored */
        binding->binding.set = 0; /* ignored */
        binding->flags |= VKD3D_SHADER_BINDING_FLAG_RAW_VA;

        param = &root_signature->parameters[i];
        param->parameter_type = p->ParameterType;
        param->descriptor.binding = binding;

        context->binding_index++;
    }

    return S_OK;
}

static HRESULT d3d12_root_signature_init_root_descriptors(struct d3d12_root_signature *root_signature,
        const D3D12_ROOT_SIGNATURE_DESC1 *desc, struct d3d12_root_signature_info *info,
        const VkPushConstantRange *push_constant_range, struct vkd3d_descriptor_set_context *context,
        VkDescriptorSetLayout *vk_set_layout)
{
    VkDescriptorSetLayoutBinding *vk_binding, *vk_binding_info = NULL;
    struct vkd3d_descriptor_hoist_desc *hoist_desc;
    struct vkd3d_shader_resource_binding *binding;
    VkDescriptorSetLayoutCreateFlags vk_flags;
    struct vkd3d_shader_root_parameter *param;
    uint32_t raw_va_root_descriptor_count = 0;
    unsigned int hoisted_parameter_index;
    const D3D12_DESCRIPTOR_RANGE1 *range;
    unsigned int i, j, k;
    HRESULT hr = S_OK;

    if (info->push_descriptor_count || (root_signature->flags & VKD3D_ROOT_SIGNATURE_USE_INLINE_UNIFORM_BLOCK))
    {
        if (!(vk_binding_info = vkd3d_malloc(sizeof(*vk_binding_info) * (info->push_descriptor_count + 1))))
            return E_OUTOFMEMORY;
    }
    else if (!(root_signature->device->bindless_state.flags &
            (VKD3D_RAW_VA_ROOT_DESCRIPTOR_CBV | VKD3D_RAW_VA_ROOT_DESCRIPTOR_SRV_UAV)))
    {
        return S_OK;
    }

    hoisted_parameter_index = desc->NumParameters;

    for (i = 0, j = 0; i < desc->NumParameters; ++i)
    {
        const D3D12_ROOT_PARAMETER1 *p = &desc->pParameters[i];
        bool raw_va;

        if (!(desc->Flags & D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE) &&
                p->ParameterType == D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE)
        {
            unsigned int range_descriptor_offset = 0;
            for (k = 0; k < p->DescriptorTable.NumDescriptorRanges && info->hoist_descriptor_count; k++)
            {
                range = &p->DescriptorTable.pDescriptorRanges[k];
                if (range->OffsetInDescriptorsFromTableStart != D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND)
                    range_descriptor_offset = range->OffsetInDescriptorsFromTableStart;

                if (d3d12_descriptor_range_can_hoist_cbv_descriptor(root_signature->device, range))
                {
                    vk_binding = &vk_binding_info[j++];
                    vk_binding->binding = context->vk_binding;

                    vk_binding->descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                    vk_binding->descriptorCount = 1;
                    vk_binding->stageFlags = vkd3d_vk_stage_flags_from_visibility(p->ShaderVisibility);
                    vk_binding->pImmutableSamplers = NULL;

                    root_signature->root_descriptor_push_mask |= 1ull << hoisted_parameter_index;
                    hoist_desc = &root_signature->hoist_info.desc[root_signature->hoist_info.num_desc];
                    hoist_desc->table_index = i;
                    hoist_desc->parameter_index = hoisted_parameter_index;
                    hoist_desc->table_offset = range_descriptor_offset;
                    root_signature->hoist_info.num_desc++;

                    binding = &root_signature->bindings[context->binding_index];
                    binding->type = vkd3d_descriptor_type_from_d3d12_range_type(range->RangeType);
                    binding->register_space = range->RegisterSpace;
                    binding->register_index = range->BaseShaderRegister;
                    binding->register_count = 1;
                    binding->descriptor_table = 0;  /* ignored */
                    binding->descriptor_offset = 0; /* ignored */
                    binding->shader_visibility = vkd3d_shader_visibility_from_d3d12(p->ShaderVisibility);
                    binding->flags = VKD3D_SHADER_BINDING_FLAG_BUFFER;
                    binding->binding.binding = context->vk_binding;
                    binding->binding.set = context->vk_set;

                    param = &root_signature->parameters[hoisted_parameter_index];
                    param->parameter_type = D3D12_ROOT_PARAMETER_TYPE_CBV;
                    param->descriptor.binding = binding;

                    context->binding_index += 1;
                    context->vk_binding += 1;
                    hoisted_parameter_index += 1;
                    info->hoist_descriptor_count -= 1;
                }

                range_descriptor_offset += range->NumDescriptors;
            }
        }

        if (p->ParameterType != D3D12_ROOT_PARAMETER_TYPE_CBV
                && p->ParameterType != D3D12_ROOT_PARAMETER_TYPE_SRV
                && p->ParameterType != D3D12_ROOT_PARAMETER_TYPE_UAV)
            continue;

        raw_va = d3d12_root_signature_parameter_is_raw_va(root_signature, p->ParameterType);

        if (!raw_va)
        {
            vk_binding = &vk_binding_info[j++];
            vk_binding->binding = context->vk_binding;
            vk_binding->descriptorType = vk_descriptor_type_from_d3d12_root_parameter(root_signature->device, p->ParameterType);
            vk_binding->descriptorCount = 1;
            vk_binding->stageFlags = vkd3d_vk_stage_flags_from_visibility(p->ShaderVisibility);
            vk_binding->pImmutableSamplers = NULL;
            root_signature->root_descriptor_push_mask |= 1ull << i;
        }
        else
            root_signature->root_descriptor_raw_va_mask |= 1ull << i;

        binding = &root_signature->bindings[context->binding_index];
        binding->type = vkd3d_descriptor_type_from_d3d12_root_parameter_type(p->ParameterType);
        binding->register_space = p->Descriptor.RegisterSpace;
        binding->register_index = p->Descriptor.ShaderRegister;
        binding->register_count = 1;
        binding->descriptor_table = 0;  /* ignored */
        binding->descriptor_offset = 0; /* ignored */
        binding->shader_visibility = vkd3d_shader_visibility_from_d3d12(p->ShaderVisibility);
        binding->flags = VKD3D_SHADER_BINDING_FLAG_BUFFER;
        binding->binding.binding = context->vk_binding;
        binding->binding.set = context->vk_set;

        if (raw_va)
            binding->flags |= VKD3D_SHADER_BINDING_FLAG_RAW_VA;
        else if (vk_binding->descriptorType == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
            binding->flags |= VKD3D_SHADER_BINDING_FLAG_RAW_SSBO;

        param = &root_signature->parameters[i];
        param->parameter_type = p->ParameterType;
        param->descriptor.binding = binding;
        param->descriptor.raw_va_root_descriptor_index = raw_va_root_descriptor_count;

        context->binding_index += 1;

        if (raw_va)
            raw_va_root_descriptor_count += 1;
        else
            context->vk_binding += 1;
    }

    if (root_signature->flags & VKD3D_ROOT_SIGNATURE_USE_INLINE_UNIFORM_BLOCK)
    {
        vk_binding = &vk_binding_info[j++];
        vk_binding->binding = context->vk_binding;
        vk_binding->descriptorType = VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK_EXT;
        vk_binding->descriptorCount = push_constant_range->size;
        vk_binding->stageFlags = VK_SHADER_STAGE_ALL;
        vk_binding->pImmutableSamplers = NULL;

        root_signature->push_constant_ubo_binding.set = context->vk_set;
        root_signature->push_constant_ubo_binding.binding = context->vk_binding;

        context->vk_binding += 1;
    }

    if (j)
    {
        vk_flags = root_signature->flags & VKD3D_ROOT_SIGNATURE_USE_ROOT_DESCRIPTOR_SET
                ? 0 : VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR;

        hr = vkd3d_create_descriptor_set_layout(root_signature->device, vk_flags,
                j, vk_binding_info, vk_set_layout);
    }

    vkd3d_free(vk_binding_info);
    return hr;
}

static HRESULT d3d12_root_signature_init_local_static_samplers(struct d3d12_root_signature *root_signature,
        const D3D12_ROOT_SIGNATURE_DESC1 *desc)
{
    unsigned int i;
    HRESULT hr;

    if (!desc->NumStaticSamplers)
        return S_OK;

    for (i = 0; i < desc->NumStaticSamplers; i++)
    {
        const D3D12_STATIC_SAMPLER_DESC *s = &desc->pStaticSamplers[i];
        if (FAILED(hr = vkd3d_sampler_state_create_static_sampler(&root_signature->device->sampler_state,
                root_signature->device, s, &root_signature->static_samplers[i])))
            return hr;
    }

    /* Cannot assign bindings until we've seen all local root signatures which go into an RTPSO.
     * For now, just copy the static samplers. RTPSO creation will build appropriate bindings. */
    memcpy(root_signature->static_samplers_desc, desc->pStaticSamplers,
            sizeof(*root_signature->static_samplers_desc) * desc->NumStaticSamplers);

    return S_OK;
}

static HRESULT d3d12_root_signature_init_static_samplers(struct d3d12_root_signature *root_signature,
        const D3D12_ROOT_SIGNATURE_DESC1 *desc, struct vkd3d_descriptor_set_context *context,
        VkDescriptorSetLayout *vk_set_layout)
{
    VkDescriptorSetLayoutBinding *vk_binding_info, *vk_binding;
    struct vkd3d_shader_resource_binding *binding;
    unsigned int i;
    HRESULT hr;

    if (!desc->NumStaticSamplers)
        return S_OK;

    if (!(vk_binding_info = malloc(desc->NumStaticSamplers * sizeof(*vk_binding_info))))
        return E_OUTOFMEMORY;

    for (i = 0; i < desc->NumStaticSamplers; ++i)
    {
        const D3D12_STATIC_SAMPLER_DESC *s = &desc->pStaticSamplers[i];

        if (FAILED(hr = vkd3d_sampler_state_create_static_sampler(&root_signature->device->sampler_state,
                root_signature->device, s, &root_signature->static_samplers[i])))
            goto cleanup;

        vk_binding = &vk_binding_info[i];
        vk_binding->binding = context->vk_binding;
        vk_binding->descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
        vk_binding->descriptorCount = 1;
        vk_binding->stageFlags = vkd3d_vk_stage_flags_from_visibility(s->ShaderVisibility);
        vk_binding->pImmutableSamplers = &root_signature->static_samplers[i];

        binding = &root_signature->bindings[context->binding_index];
        binding->type = VKD3D_SHADER_DESCRIPTOR_TYPE_SAMPLER;
        binding->register_space = s->RegisterSpace;
        binding->register_index = s->ShaderRegister;
        binding->register_count = 1;
        binding->descriptor_table = 0;  /* ignored */
        binding->descriptor_offset = 0; /* ignored */
        binding->shader_visibility = vkd3d_shader_visibility_from_d3d12(s->ShaderVisibility);
        binding->flags = VKD3D_SHADER_BINDING_FLAG_IMAGE;
        binding->binding.binding = context->vk_binding;
        binding->binding.set = context->vk_set;

        context->binding_index += 1;
        context->vk_binding += 1;
    }

    if (FAILED(hr = vkd3d_create_descriptor_set_layout(root_signature->device, 0,
            desc->NumStaticSamplers, vk_binding_info, &root_signature->vk_sampler_descriptor_layout)))
        goto cleanup;

    hr = vkd3d_sampler_state_allocate_descriptor_set(&root_signature->device->sampler_state,
            root_signature->device, root_signature->vk_sampler_descriptor_layout,
            &root_signature->vk_sampler_set, &root_signature->vk_sampler_pool);

cleanup:
    vkd3d_free(vk_binding_info);
    return hr;
}

static HRESULT d3d12_root_signature_init_local(struct d3d12_root_signature *root_signature,
        struct d3d12_device *device, const D3D12_ROOT_SIGNATURE_DESC1 *desc)
{
    /* Local root signatures map to the ShaderRecordBufferKHR. */
    struct vkd3d_descriptor_set_context context;
    struct d3d12_root_signature_info info;
    HRESULT hr;

    memset(&context, 0, sizeof(context));

    if (FAILED(hr = d3d12_root_signature_info_from_desc(&info, device, desc)))
        return hr;

#define D3D12_MAX_SHADER_RECORD_SIZE 4096
    if (info.cost * 4 + D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES > D3D12_MAX_SHADER_RECORD_SIZE)
    {
        ERR("Local root signature is too large.\n");
        hr = E_INVALIDARG;
        goto fail;
    }

    root_signature->binding_count = info.binding_count;
    root_signature->parameter_count = info.parameter_count;
    root_signature->static_sampler_count = desc->NumStaticSamplers;

    hr = E_OUTOFMEMORY;
    if (!(root_signature->parameters = vkd3d_calloc(root_signature->parameter_count,
            sizeof(*root_signature->parameters))))
        return hr;
    if (!(root_signature->bindings = vkd3d_calloc(root_signature->binding_count,
            sizeof(*root_signature->bindings))))
        return hr;
    root_signature->root_constant_count = info.root_constant_count;
    if (!(root_signature->root_constants = vkd3d_calloc(root_signature->root_constant_count,
            sizeof(*root_signature->root_constants))))
        return hr;
    if (!(root_signature->static_samplers = vkd3d_calloc(root_signature->static_sampler_count,
            sizeof(*root_signature->static_samplers))))
        return hr;
    if (!(root_signature->static_samplers_desc = vkd3d_calloc(root_signature->static_sampler_count,
            sizeof(*root_signature->static_samplers_desc))))
        return hr;

    if (FAILED(hr = d3d12_root_signature_init_local_static_samplers(root_signature, desc)))
        return hr;

    d3d12_root_signature_init_extra_bindings(root_signature, &info);

    if (FAILED(hr = d3d12_root_signature_init_shader_record_constants(root_signature, desc, &info)))
        return hr;
    if (FAILED(hr = d3d12_root_signature_init_shader_record_descriptors(root_signature, desc, &info, &context)))
        return hr;
    if (FAILED(hr = d3d12_root_signature_init_root_descriptor_tables(root_signature, desc, &info, &context)))
        return hr;

    if (FAILED(hr = vkd3d_private_store_init(&root_signature->private_store)))
        goto fail;

    return S_OK;

fail:
    return hr;
}

static HRESULT d3d12_root_signature_init_global(struct d3d12_root_signature *root_signature,
        struct d3d12_device *device, const D3D12_ROOT_SIGNATURE_DESC1 *desc)
{
    const VkPhysicalDeviceProperties *vk_device_properties = &device->device_info.properties2.properties;
    const struct vkd3d_bindless_state *bindless_state = &device->bindless_state;
    struct vkd3d_descriptor_set_context context;
    VkShaderStageFlagBits mesh_shader_stages;
    struct d3d12_root_signature_info info;
    unsigned int i;
    HRESULT hr;

    memset(&context, 0, sizeof(context));

    if (desc->Flags & ~(D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT
            | D3D12_ROOT_SIGNATURE_FLAG_ALLOW_STREAM_OUTPUT
            | D3D12_ROOT_SIGNATURE_FLAG_DENY_AMPLIFICATION_SHADER_ROOT_ACCESS
            | D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS
            | D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS
            | D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS
            | D3D12_ROOT_SIGNATURE_FLAG_DENY_MESH_SHADER_ROOT_ACCESS
            | D3D12_ROOT_SIGNATURE_FLAG_DENY_PIXEL_SHADER_ROOT_ACCESS
            | D3D12_ROOT_SIGNATURE_FLAG_DENY_VERTEX_SHADER_ROOT_ACCESS
            | D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED
            | D3D12_ROOT_SIGNATURE_FLAG_SAMPLER_HEAP_DIRECTLY_INDEXED
            | D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE))
        FIXME("Ignoring root signature flags %#x.\n", desc->Flags);

    if (FAILED(hr = d3d12_root_signature_info_from_desc(&info, device, desc)))
        return hr;

    if (info.cost > D3D12_MAX_ROOT_COST)
    {
        WARN("Root signature cost %u exceeds maximum allowed cost.\n", info.cost);
        return E_INVALIDARG;
    }

    root_signature->binding_count = info.binding_count;
    root_signature->parameter_count = info.parameter_count;
    root_signature->static_sampler_count = desc->NumStaticSamplers;

    hr = E_OUTOFMEMORY;
    if (!(root_signature->parameters = vkd3d_calloc(root_signature->parameter_count,
            sizeof(*root_signature->parameters))))
        return hr;
    if (!(root_signature->bindings = vkd3d_calloc(root_signature->binding_count,
            sizeof(*root_signature->bindings))))
        return hr;
    root_signature->root_constant_count = info.root_constant_count;
    if (!(root_signature->root_constants = vkd3d_calloc(root_signature->root_constant_count,
            sizeof(*root_signature->root_constants))))
        return hr;
    if (!(root_signature->static_samplers = vkd3d_calloc(root_signature->static_sampler_count,
            sizeof(*root_signature->static_samplers))))
        return hr;

    for (i = 0; i < bindless_state->set_count; i++)
        root_signature->set_layouts[context.vk_set++] = bindless_state->set_info[i].vk_set_layout;

    if (FAILED(hr = d3d12_root_signature_init_static_samplers(root_signature, desc,
                &context, &root_signature->vk_sampler_descriptor_layout)))
        return hr;

    if (root_signature->vk_sampler_descriptor_layout)
    {
        assert(context.vk_set < VKD3D_MAX_DESCRIPTOR_SETS);
        root_signature->set_layouts[context.vk_set] = root_signature->vk_sampler_descriptor_layout;
        root_signature->sampler_descriptor_set = context.vk_set;

        context.vk_binding = 0;
        context.vk_set += 1;
    }

    if (FAILED(hr = d3d12_root_signature_init_push_constants(root_signature, desc, &info,
            &root_signature->push_constant_range)))
        return hr;

    if (root_signature->push_constant_range.size <= vk_device_properties->limits.maxPushConstantsSize)
    {
        if (info.push_descriptor_count > device->device_info.push_descriptor_properties.maxPushDescriptors)
            root_signature->flags |= VKD3D_ROOT_SIGNATURE_USE_ROOT_DESCRIPTOR_SET;
    }
    else if (device->device_info.inline_uniform_block_features.inlineUniformBlock)
    {
        /* Stores push constant data with the root descriptor set,
         * so we can't use push descriptors in this case. */
        root_signature->flags |= VKD3D_ROOT_SIGNATURE_USE_INLINE_UNIFORM_BLOCK |
                VKD3D_ROOT_SIGNATURE_USE_ROOT_DESCRIPTOR_SET;
    }
    else
    {
        ERR("Root signature requires %d bytes of push constant space, but device only supports %d bytes.\n",
                root_signature->push_constant_range.size, vk_device_properties->limits.maxPushConstantsSize);
        return hr;
    }

    d3d12_root_signature_init_extra_bindings(root_signature, &info);

    if (FAILED(hr = d3d12_root_signature_init_root_descriptors(root_signature, desc,
                &info, &root_signature->push_constant_range, &context,
                &root_signature->vk_root_descriptor_layout)))
        return hr;

    if (root_signature->vk_root_descriptor_layout)
    {
        assert(context.vk_set < VKD3D_MAX_DESCRIPTOR_SETS);
        root_signature->set_layouts[context.vk_set] = root_signature->vk_root_descriptor_layout;
        root_signature->root_descriptor_set = context.vk_set;

        context.vk_binding = 0;
        context.vk_set += 1;
    }

    if (FAILED(hr = d3d12_root_signature_init_root_descriptor_tables(root_signature, desc, &info, &context)))
        return hr;

    if (root_signature->flags & VKD3D_ROOT_SIGNATURE_USE_INLINE_UNIFORM_BLOCK)
        root_signature->push_constant_range.stageFlags = 0;

    /* If we need to use restricted entry_points in vkCmdPushConstants,
     * we are unfortunately required to do it like this
     * since stageFlags in vkCmdPushConstants must cover at least all entry_points in the layout.
     *
     * We can pick the appropriate layout to use in PSO creation.
     * In set_root_signature we can bind the appropriate layout as well.
     *
     * For graphics we can generally rely on visibility mask, but not so for compute and raygen,
     * since they use ALL visibility. */

    root_signature->num_set_layouts = context.vk_set;

    if (FAILED(hr = vkd3d_create_pipeline_layout_for_stage_mask(
            device, root_signature->num_set_layouts, root_signature->set_layouts,
            &root_signature->push_constant_range,
            VK_SHADER_STAGE_ALL_GRAPHICS, &root_signature->graphics)))
        return hr;

    if (device->device_info.mesh_shader_features.meshShader && device->device_info.mesh_shader_features.taskShader)
    {
        mesh_shader_stages = VK_SHADER_STAGE_MESH_BIT_EXT |
                VK_SHADER_STAGE_TASK_BIT_EXT |
                VK_SHADER_STAGE_FRAGMENT_BIT;

        if (FAILED(hr = vkd3d_create_pipeline_layout_for_stage_mask(
                device, root_signature->num_set_layouts, root_signature->set_layouts,
                &root_signature->push_constant_range,
                mesh_shader_stages, &root_signature->mesh)))
            return hr;
    }

    if (FAILED(hr = vkd3d_create_pipeline_layout_for_stage_mask(
            device, root_signature->num_set_layouts, root_signature->set_layouts,
            &root_signature->push_constant_range,
            VK_SHADER_STAGE_COMPUTE_BIT, &root_signature->compute)))
        return hr;

    if (d3d12_device_supports_ray_tracing_tier_1_0(device))
    {
        if (FAILED(hr = vkd3d_create_pipeline_layout_for_stage_mask(
                device, root_signature->num_set_layouts, root_signature->set_layouts,
                &root_signature->push_constant_range,
                VK_SHADER_STAGE_RAYGEN_BIT_KHR |
                VK_SHADER_STAGE_MISS_BIT_KHR |
                VK_SHADER_STAGE_INTERSECTION_BIT_KHR |
                VK_SHADER_STAGE_CALLABLE_BIT_KHR |
                VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR |
                VK_SHADER_STAGE_ANY_HIT_BIT_KHR, &root_signature->raygen)))
            return hr;
    }

    return S_OK;
}

HRESULT d3d12_root_signature_create_local_static_samplers_layout(struct d3d12_root_signature *root_signature,
        VkDescriptorSetLayout vk_set_layout, VkPipelineLayout *vk_pipeline_layout)
{
    /* For RTPSOs we might have to bind a secondary static sampler set. To stay compatible with the base global RS,
     * just add the descriptor set layout after the other ones.
     * With this scheme, it's valid to bind resources with global RS layout,
     * and then add a final vkCmdBindDescriptorSets with vk_pipeline_layout which is tied to the RTPSO. */
    VkDescriptorSetLayout set_layouts[VKD3D_MAX_DESCRIPTOR_SETS];
    struct d3d12_bind_point_layout bind_point_layout;
    HRESULT hr;

    if (!d3d12_device_supports_ray_tracing_tier_1_0(root_signature->device))
        return E_INVALIDARG;

    memcpy(set_layouts, root_signature->set_layouts, root_signature->num_set_layouts * sizeof(VkDescriptorSetLayout));
    set_layouts[root_signature->num_set_layouts] = vk_set_layout;

    if (FAILED(hr = vkd3d_create_pipeline_layout_for_stage_mask(
            root_signature->device, root_signature->num_set_layouts + 1, set_layouts,
            &root_signature->push_constant_range,
            VK_SHADER_STAGE_RAYGEN_BIT_KHR |
            VK_SHADER_STAGE_MISS_BIT_KHR |
            VK_SHADER_STAGE_INTERSECTION_BIT_KHR |
            VK_SHADER_STAGE_CALLABLE_BIT_KHR |
            VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR |
            VK_SHADER_STAGE_ANY_HIT_BIT_KHR, &bind_point_layout)))
        return hr;

    *vk_pipeline_layout = bind_point_layout.vk_pipeline_layout;
    return S_OK;
}

static HRESULT d3d12_root_signature_init(struct d3d12_root_signature *root_signature,
        struct d3d12_device *device, const D3D12_ROOT_SIGNATURE_DESC1 *desc)
{
    HRESULT hr;

    memset(root_signature, 0, sizeof(*root_signature));
    root_signature->ID3D12RootSignature_iface.lpVtbl = &d3d12_root_signature_vtbl;
    root_signature->refcount = 1;
    root_signature->internal_refcount = 1;

    root_signature->d3d12_flags = desc->Flags;
    /* needed by some methods, increment ref count later */
    root_signature->device = device;

    if (desc->Flags & D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE)
        hr = d3d12_root_signature_init_local(root_signature, device, desc);
    else
        hr = d3d12_root_signature_init_global(root_signature, device, desc);

    if (FAILED(hr))
        goto fail;

    if (FAILED(hr = vkd3d_private_store_init(&root_signature->private_store)))
        goto fail;

    if (SUCCEEDED(hr))
        d3d12_device_add_ref(root_signature->device);

    return S_OK;

fail:
    d3d12_root_signature_cleanup(root_signature, device);
    return hr;
}

HRESULT d3d12_root_signature_create_empty(struct d3d12_device *device,
        struct d3d12_root_signature **root_signature)
{
    struct d3d12_root_signature *object;
    D3D12_ROOT_SIGNATURE_DESC1 desc;
    HRESULT hr;

    if (!(object = vkd3d_malloc(sizeof(*object))))
        return E_OUTOFMEMORY;

    memset(&desc, 0, sizeof(desc));
    hr = d3d12_root_signature_init(object, device, &desc);

    /* For pipeline libraries, (and later DXR to some degree), we need a way to
     * compare root signature objects. */
    object->compatibility_hash = 0;

    if (FAILED(hr))
    {
        vkd3d_free(object);
        return hr;
    }

    *root_signature = object;
    return S_OK;
}

static HRESULT d3d12_root_signature_create_from_blob(struct d3d12_device *device,
        const void *bytecode, size_t bytecode_length, bool raw_payload,
        struct d3d12_root_signature **root_signature)
{
    const struct vkd3d_shader_code dxbc = {bytecode, bytecode_length};
    union
    {
        D3D12_VERSIONED_ROOT_SIGNATURE_DESC d3d12;
        struct vkd3d_versioned_root_signature_desc vkd3d;
    } root_signature_desc;
    vkd3d_shader_hash_t compatibility_hash;
    struct d3d12_root_signature *object;
    HRESULT hr;
    int ret;

    if (raw_payload)
    {
        if ((ret = vkd3d_parse_root_signature_v_1_1_from_raw_payload(&dxbc, &root_signature_desc.vkd3d, &compatibility_hash)))
        {
            WARN("Failed to parse root signature, vkd3d result %d.\n", ret);
            return hresult_from_vkd3d_result(ret);
        }
    }
    else
    {
        if ((ret = vkd3d_parse_root_signature_v_1_1(&dxbc, &root_signature_desc.vkd3d, &compatibility_hash)) < 0)
        {
            WARN("Failed to parse root signature, vkd3d result %d.\n", ret);
            return hresult_from_vkd3d_result(ret);
        }
    }

    if (!(object = vkd3d_malloc(sizeof(*object))))
    {
        vkd3d_shader_free_root_signature(&root_signature_desc.vkd3d);
        return E_OUTOFMEMORY;
    }

    hr = d3d12_root_signature_init(object, device, &root_signature_desc.d3d12.Desc_1_1);

    /* For pipeline libraries, (and later DXR to some degree), we need a way to
     * compare root signature objects. */
    object->compatibility_hash = compatibility_hash;

    vkd3d_shader_free_root_signature(&root_signature_desc.vkd3d);
    if (FAILED(hr))
    {
        vkd3d_free(object);
        return hr;
    }

    TRACE("Created root signature %p.\n", object);

    *root_signature = object;

    return S_OK;
}

HRESULT d3d12_root_signature_create(struct d3d12_device *device,
        const void *bytecode, size_t bytecode_length,
        struct d3d12_root_signature **root_signature)
{
    return d3d12_root_signature_create_from_blob(device, bytecode, bytecode_length, false, root_signature);
}

HRESULT d3d12_root_signature_create_raw(struct d3d12_device *device,
        const void *payload, size_t payload_length,
        struct d3d12_root_signature **root_signature)
{
    return d3d12_root_signature_create_from_blob(device, payload, payload_length, true, root_signature);
}

unsigned int d3d12_root_signature_get_shader_interface_flags(const struct d3d12_root_signature *root_signature)
{
    unsigned int flags = 0;

    if (root_signature->flags & VKD3D_ROOT_SIGNATURE_USE_INLINE_UNIFORM_BLOCK)
        flags |= VKD3D_SHADER_INTERFACE_PUSH_CONSTANTS_AS_UNIFORM_BUFFER;

    if (root_signature->flags & VKD3D_ROOT_SIGNATURE_USE_SSBO_OFFSET_BUFFER)
        flags |= VKD3D_SHADER_INTERFACE_SSBO_OFFSET_BUFFER;
    if (root_signature->flags & VKD3D_ROOT_SIGNATURE_USE_TYPED_OFFSET_BUFFER)
        flags |= VKD3D_SHADER_INTERFACE_TYPED_OFFSET_BUFFER;

    if (root_signature->device->bindless_state.flags & VKD3D_BINDLESS_CBV_AS_SSBO)
        flags |= VKD3D_SHADER_INTERFACE_BINDLESS_CBV_AS_STORAGE_BUFFER;

    if (vkd3d_descriptor_debug_active_qa_checks())
        flags |= VKD3D_SHADER_INTERFACE_DESCRIPTOR_QA_BUFFER;

    return flags;
}

static void d3d12_promote_depth_stencil_desc(D3D12_DEPTH_STENCIL_DESC1 *out, const D3D12_DEPTH_STENCIL_DESC *in)
{
    out->DepthEnable = in->DepthEnable;
    out->DepthWriteMask = in->DepthWriteMask;
    out->DepthFunc = in->DepthFunc;
    out->StencilEnable = in->StencilEnable;
    out->StencilReadMask = in->StencilReadMask;
    out->StencilWriteMask = in->StencilWriteMask;
    out->FrontFace = in->FrontFace;
    out->BackFace = in->BackFace;
    out->DepthBoundsTestEnable = FALSE;
}

static void d3d12_init_pipeline_state_desc(struct d3d12_pipeline_state_desc *desc)
{
    D3D12_DEPTH_STENCIL_DESC1 *ds_state = &desc->depth_stencil_state;
    D3D12_RASTERIZER_DESC *rs_state = &desc->rasterizer_state;
    D3D12_BLEND_DESC *blend_state = &desc->blend_state;
    DXGI_SAMPLE_DESC *sample_desc = &desc->sample_desc;

    memset(desc, 0, sizeof(*desc));
    ds_state->DepthEnable = TRUE;
    ds_state->DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    ds_state->DepthFunc = D3D12_COMPARISON_FUNC_LESS;
    ds_state->StencilReadMask = D3D12_DEFAULT_STENCIL_READ_MASK;
    ds_state->StencilWriteMask = D3D12_DEFAULT_STENCIL_WRITE_MASK;
    ds_state->FrontFace.StencilFunc = ds_state->BackFace.StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    ds_state->FrontFace.StencilDepthFailOp = ds_state->BackFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
    ds_state->FrontFace.StencilPassOp = ds_state->BackFace.StencilPassOp = D3D12_STENCIL_OP_KEEP;
    ds_state->FrontFace.StencilFailOp = ds_state->BackFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;

    rs_state->FillMode = D3D12_FILL_MODE_SOLID;
    rs_state->CullMode = D3D12_CULL_MODE_BACK;
    rs_state->DepthClipEnable = TRUE;
    rs_state->ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;

    blend_state->RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

    sample_desc->Count = 1;
    sample_desc->Quality = 0;

    desc->sample_mask = D3D12_DEFAULT_SAMPLE_MASK;
}

HRESULT vkd3d_pipeline_state_desc_from_d3d12_graphics_desc(struct d3d12_pipeline_state_desc *desc,
        const D3D12_GRAPHICS_PIPELINE_STATE_DESC *d3d12_desc)
{
    unsigned int i;

    memset(desc, 0, sizeof(*desc));
    desc->root_signature = d3d12_desc->pRootSignature;
    desc->vs = d3d12_desc->VS;
    desc->ps = d3d12_desc->PS;
    desc->ds = d3d12_desc->DS;
    desc->hs = d3d12_desc->HS;
    desc->gs = d3d12_desc->GS;
    desc->stream_output = d3d12_desc->StreamOutput;
    desc->blend_state = d3d12_desc->BlendState;
    desc->sample_mask = d3d12_desc->SampleMask;
    desc->rasterizer_state = d3d12_desc->RasterizerState;
    d3d12_promote_depth_stencil_desc(&desc->depth_stencil_state, &d3d12_desc->DepthStencilState);
    desc->input_layout = d3d12_desc->InputLayout;
    desc->strip_cut_value = d3d12_desc->IBStripCutValue;
    desc->primitive_topology_type = d3d12_desc->PrimitiveTopologyType;
    desc->rtv_formats.NumRenderTargets = d3d12_desc->NumRenderTargets;
    for (i = 0; i < ARRAY_SIZE(d3d12_desc->RTVFormats); i++)
        desc->rtv_formats.RTFormats[i] = d3d12_desc->RTVFormats[i];
    desc->dsv_format = d3d12_desc->DSVFormat;
    desc->sample_desc = d3d12_desc->SampleDesc;
    desc->node_mask = d3d12_desc->NodeMask;
    desc->cached_pso.blob = d3d12_desc->CachedPSO;
    desc->cached_pso.library = NULL;
    desc->flags = d3d12_desc->Flags;
    return S_OK;
}

HRESULT vkd3d_pipeline_state_desc_from_d3d12_compute_desc(struct d3d12_pipeline_state_desc *desc,
        const D3D12_COMPUTE_PIPELINE_STATE_DESC *d3d12_desc)
{
    memset(desc, 0, sizeof(*desc));
    desc->root_signature = d3d12_desc->pRootSignature;
    desc->cs = d3d12_desc->CS;
    desc->node_mask = d3d12_desc->NodeMask;
    desc->cached_pso.blob = d3d12_desc->CachedPSO;
    desc->cached_pso.library = NULL;
    desc->flags = d3d12_desc->Flags;
    return S_OK;
}

#define VKD3D_HANDLE_SUBOBJECT_EXPLICIT(type_enum, type_name, assignment) \
    case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_ ## type_enum: \
    {\
        const struct {\
            D3D12_PIPELINE_STATE_SUBOBJECT_TYPE type; \
            type_name data; \
        } *subobject = (void *)stream_ptr; \
        if (stream_ptr + sizeof(*subobject) > stream_end) \
        { \
            ERR("Invalid pipeline state stream.\n"); \
            return E_INVALIDARG; \
        } \
        stream_ptr += align(sizeof(*subobject), sizeof(void*)); \
        assignment; \
        break;\
    }

#define VKD3D_HANDLE_SUBOBJECT(type_enum, type, left_side) \
    VKD3D_HANDLE_SUBOBJECT_EXPLICIT(type_enum, type, left_side = subobject->data)

static VkShaderStageFlags vkd3d_pipeline_state_desc_get_shader_stages(const struct d3d12_pipeline_state_desc *desc)
{
    VkShaderStageFlags result = 0;

    if (desc->vs.BytecodeLength && desc->vs.pShaderBytecode)
        result |= VK_SHADER_STAGE_VERTEX_BIT;
    if (desc->hs.BytecodeLength && desc->hs.pShaderBytecode)
        result |= VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT;
    if (desc->ds.BytecodeLength && desc->ds.pShaderBytecode)
        result |= VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;
    if (desc->gs.BytecodeLength && desc->gs.pShaderBytecode)
        result |= VK_SHADER_STAGE_GEOMETRY_BIT;
    if (desc->ps.BytecodeLength && desc->ps.pShaderBytecode)
        result |= VK_SHADER_STAGE_FRAGMENT_BIT;
    if (desc->as.BytecodeLength && desc->as.pShaderBytecode)
        result |= VK_SHADER_STAGE_TASK_BIT_EXT;
    if (desc->ms.BytecodeLength && desc->ms.pShaderBytecode)
        result |= VK_SHADER_STAGE_MESH_BIT_EXT;
    if (desc->cs.BytecodeLength && desc->cs.pShaderBytecode)
        result |= VK_SHADER_STAGE_COMPUTE_BIT;

    return result;
}

HRESULT vkd3d_pipeline_state_desc_from_d3d12_stream_desc(struct d3d12_pipeline_state_desc *desc,
        const D3D12_PIPELINE_STATE_STREAM_DESC *d3d12_desc, VkPipelineBindPoint *vk_bind_point)
{
    VkShaderStageFlags defined_stages, disallowed_stages;
    D3D12_PIPELINE_STATE_SUBOBJECT_TYPE subobject_type;
    const char *stream_ptr, *stream_end;
    uint64_t defined_subobjects = 0;
    uint64_t subobject_bit;

    /* Initialize defaults for undefined subobjects */
    d3d12_init_pipeline_state_desc(desc);

    /* Structs are packed, but padded so that their size
     * is always a multiple of the size of a pointer. */
    stream_ptr = d3d12_desc->pPipelineStateSubobjectStream;
    stream_end = stream_ptr + d3d12_desc->SizeInBytes;

    while (stream_ptr < stream_end)
    {
        if (stream_ptr + sizeof(subobject_type) > stream_end)
        {
            ERR("Invalid pipeline state stream.\n");
            return E_INVALIDARG;
        }

        subobject_type = *(const D3D12_PIPELINE_STATE_SUBOBJECT_TYPE *)stream_ptr;
        subobject_bit = 1ull << subobject_type;

        if (defined_subobjects & subobject_bit)
        {
            ERR("Duplicate pipeline subobject type %u.\n", subobject_type);
            return E_INVALIDARG;
        }

        defined_subobjects |= subobject_bit;

        switch (subobject_type)
        {
            VKD3D_HANDLE_SUBOBJECT(ROOT_SIGNATURE, ID3D12RootSignature*, desc->root_signature);
            VKD3D_HANDLE_SUBOBJECT(VS, D3D12_SHADER_BYTECODE, desc->vs);
            VKD3D_HANDLE_SUBOBJECT(PS, D3D12_SHADER_BYTECODE, desc->ps);
            VKD3D_HANDLE_SUBOBJECT(DS, D3D12_SHADER_BYTECODE, desc->ds);
            VKD3D_HANDLE_SUBOBJECT(HS, D3D12_SHADER_BYTECODE, desc->hs);
            VKD3D_HANDLE_SUBOBJECT(GS, D3D12_SHADER_BYTECODE, desc->gs);
            VKD3D_HANDLE_SUBOBJECT(CS, D3D12_SHADER_BYTECODE, desc->cs);
            VKD3D_HANDLE_SUBOBJECT(AS, D3D12_SHADER_BYTECODE, desc->as);
            VKD3D_HANDLE_SUBOBJECT(MS, D3D12_SHADER_BYTECODE, desc->ms);
            VKD3D_HANDLE_SUBOBJECT(STREAM_OUTPUT, D3D12_STREAM_OUTPUT_DESC, desc->stream_output);
            VKD3D_HANDLE_SUBOBJECT(BLEND, D3D12_BLEND_DESC, desc->blend_state);
            VKD3D_HANDLE_SUBOBJECT(SAMPLE_MASK, UINT, desc->sample_mask);
            VKD3D_HANDLE_SUBOBJECT(RASTERIZER, D3D12_RASTERIZER_DESC, desc->rasterizer_state);
            VKD3D_HANDLE_SUBOBJECT_EXPLICIT(DEPTH_STENCIL, D3D12_DEPTH_STENCIL_DESC,
                    d3d12_promote_depth_stencil_desc(&desc->depth_stencil_state, &subobject->data));
            VKD3D_HANDLE_SUBOBJECT(INPUT_LAYOUT, D3D12_INPUT_LAYOUT_DESC, desc->input_layout);
            VKD3D_HANDLE_SUBOBJECT(IB_STRIP_CUT_VALUE, D3D12_INDEX_BUFFER_STRIP_CUT_VALUE, desc->strip_cut_value);
            VKD3D_HANDLE_SUBOBJECT(PRIMITIVE_TOPOLOGY, D3D12_PRIMITIVE_TOPOLOGY_TYPE, desc->primitive_topology_type);
            VKD3D_HANDLE_SUBOBJECT(RENDER_TARGET_FORMATS, D3D12_RT_FORMAT_ARRAY, desc->rtv_formats);
            VKD3D_HANDLE_SUBOBJECT(DEPTH_STENCIL_FORMAT, DXGI_FORMAT, desc->dsv_format);
            VKD3D_HANDLE_SUBOBJECT(SAMPLE_DESC, DXGI_SAMPLE_DESC, desc->sample_desc);
            VKD3D_HANDLE_SUBOBJECT(NODE_MASK, UINT, desc->node_mask);
            VKD3D_HANDLE_SUBOBJECT(CACHED_PSO, D3D12_CACHED_PIPELINE_STATE, desc->cached_pso.blob);
            VKD3D_HANDLE_SUBOBJECT(FLAGS, D3D12_PIPELINE_STATE_FLAGS, desc->flags);
            VKD3D_HANDLE_SUBOBJECT(DEPTH_STENCIL1, D3D12_DEPTH_STENCIL_DESC1, desc->depth_stencil_state);
            VKD3D_HANDLE_SUBOBJECT(VIEW_INSTANCING, D3D12_VIEW_INSTANCING_DESC, desc->view_instancing_desc);

            default:
                ERR("Unhandled pipeline subobject type %u.\n", subobject_type);
                return E_INVALIDARG;
        }
    }

    /* Deduce pipeline type from specified shaders */
    defined_stages = vkd3d_pipeline_state_desc_get_shader_stages(desc);

    if (defined_stages & VK_SHADER_STAGE_VERTEX_BIT)
    {
        disallowed_stages = VK_SHADER_STAGE_MESH_BIT_EXT | VK_SHADER_STAGE_COMPUTE_BIT;
        *vk_bind_point = VK_PIPELINE_BIND_POINT_GRAPHICS;
    }
    else if (defined_stages & VK_SHADER_STAGE_MESH_BIT_EXT)
    {
        disallowed_stages = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_COMPUTE_BIT;
        *vk_bind_point = VK_PIPELINE_BIND_POINT_GRAPHICS;
    }
    else if (defined_stages & VK_SHADER_STAGE_COMPUTE_BIT)
    {
        disallowed_stages = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_MESH_BIT_EXT;
        *vk_bind_point = VK_PIPELINE_BIND_POINT_COMPUTE;
    }
    else
    {
        ERR("Cannot deduce pipeline type from shader stages 0x%#x.\n", defined_stages);
        return E_INVALIDARG;
    }

    if (defined_stages & disallowed_stages)
    {
        ERR("Invalid combination of shader stages 0x%#x.\n", defined_stages);
        return E_INVALIDARG;
    }

    return S_OK;
}

#undef VKD3D_HANDLE_SUBOBJECT
#undef VKD3D_HANDLE_SUBOBJECT_EXPLICIT

struct vkd3d_compiled_pipeline
{
    struct list entry;
    struct vkd3d_pipeline_key key;
    VkPipeline vk_pipeline;
    uint32_t dynamic_state_flags;
};

/* ID3D12PipelineState */
static HRESULT STDMETHODCALLTYPE d3d12_pipeline_state_QueryInterface(ID3D12PipelineState *iface,
        REFIID riid, void **object)
{
    TRACE("iface %p, riid %s, object %p.\n", iface, debugstr_guid(riid), object);

    if (IsEqualGUID(riid, &IID_ID3D12PipelineState)
            || IsEqualGUID(riid, &IID_ID3D12Pageable)
            || IsEqualGUID(riid, &IID_ID3D12DeviceChild)
            || IsEqualGUID(riid, &IID_ID3D12Object)
            || IsEqualGUID(riid, &IID_IUnknown))
    {
        ID3D12PipelineState_AddRef(iface);
        *object = iface;
        return S_OK;
    }

    WARN("%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid(riid));

    *object = NULL;
    return E_NOINTERFACE;
}

void d3d12_pipeline_state_inc_ref(struct d3d12_pipeline_state *state)
{
    InterlockedIncrement(&state->internal_refcount);
}

ULONG d3d12_pipeline_state_inc_public_ref(struct d3d12_pipeline_state *state)
{
    ULONG refcount = InterlockedIncrement(&state->refcount);
    if (refcount == 1)
    {
        d3d12_pipeline_state_inc_ref(state);
        /* Bring device reference back to life. */
        d3d12_device_add_ref(state->device);
    }
    TRACE("%p increasing refcount to %u.\n", state, refcount);
    return refcount;
}

static ULONG STDMETHODCALLTYPE d3d12_pipeline_state_AddRef(ID3D12PipelineState *iface)
{
    struct d3d12_pipeline_state *state = impl_from_ID3D12PipelineState(iface);
    return d3d12_pipeline_state_inc_public_ref(state);
}

static HRESULT d3d12_pipeline_state_create_shader_module(struct d3d12_device *device,
        VkPipelineShaderStageCreateInfo *stage_desc, const struct vkd3d_shader_code *code)
{
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    VkShaderModuleCreateInfo shader_desc;
    char hash_str[16 + 1];
    VkResult vr;

    /* If we kept the module around, no need to create it again. */
    if (stage_desc->module != VK_NULL_HANDLE)
        return S_OK;

    shader_desc.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    shader_desc.pNext = NULL;
    shader_desc.flags = 0;
    shader_desc.codeSize = code->size;
    shader_desc.pCode = code->code;

    vr = VK_CALL(vkCreateShaderModule(device->vk_device, &shader_desc, NULL, &stage_desc->module));
    if (vr < 0)
    {
        WARN("Failed to create Vulkan shader module, vr %d.\n", vr);
        return hresult_from_vk_result(vr);
    }

    /* Helpful for tooling like RenderDoc. */
    sprintf(hash_str, "%016"PRIx64, code->meta.hash);
    vkd3d_set_vk_object_name(device, (uint64_t)stage_desc->module, VK_OBJECT_TYPE_SHADER_MODULE, hash_str);
    return S_OK;
}

static void d3d12_pipeline_state_free_spirv_code(struct d3d12_pipeline_state *state)
{
    unsigned int i;
    if (d3d12_pipeline_state_is_graphics(state))
    {
        for (i = 0; i < state->graphics.stage_count; i++)
        {
            vkd3d_shader_free_shader_code(&state->graphics.code[i]);
            /* Keep meta. */
            state->graphics.code[i].code = NULL;
            state->graphics.code[i].size = 0;
        }
    }
    else if (d3d12_pipeline_state_is_compute(state))
    {
        vkd3d_shader_free_shader_code(&state->compute.code);
        /* Keep meta. */
        state->compute.code.code = NULL;
        state->compute.code.size = 0;
    }
}

static void d3d12_pipeline_state_destroy_shader_modules(struct d3d12_pipeline_state *state, struct d3d12_device *device)
{
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    unsigned int i;

    if (d3d12_pipeline_state_is_graphics(state))
    {
        for (i = 0; i < state->graphics.stage_count; i++)
        {
            VK_CALL(vkDestroyShaderModule(device->vk_device, state->graphics.stages[i].module, NULL));
            state->graphics.stages[i].module = VK_NULL_HANDLE;
        }
    }
}

static void d3d12_pipeline_state_destroy_graphics(struct d3d12_pipeline_state *state,
        struct d3d12_device *device)
{
    struct d3d12_graphics_pipeline_state *graphics = &state->graphics;
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    struct vkd3d_compiled_pipeline *current, *e;

    d3d12_pipeline_state_destroy_shader_modules(state, device);

    LIST_FOR_EACH_ENTRY_SAFE(current, e, &graphics->compiled_fallback_pipelines, struct vkd3d_compiled_pipeline, entry)
    {
        VK_CALL(vkDestroyPipeline(device->vk_device, current->vk_pipeline, NULL));
        vkd3d_free(current);
    }

    VK_CALL(vkDestroyPipeline(device->vk_device, graphics->pipeline, NULL));
}

static void d3d12_pipeline_state_set_name(struct d3d12_pipeline_state *state, const char *name)
{
    if (d3d12_pipeline_state_is_compute(state))
    {
        vkd3d_set_vk_object_name(state->device, (uint64_t)state->compute.vk_pipeline,
                VK_OBJECT_TYPE_PIPELINE, name);
    }
}

static void vkd3d_shader_transform_feedback_info_free(struct vkd3d_shader_transform_feedback_info *xfb_info)
{
    unsigned int i;

    if (!xfb_info)
        return;

    for (i = 0; i < xfb_info->element_count; i++)
        vkd3d_free((void*)xfb_info->elements[i].semantic_name);
    vkd3d_free((void*)xfb_info->elements);
    vkd3d_free((void*)xfb_info->buffer_strides);
    vkd3d_free(xfb_info);
}

static void d3d12_pipeline_state_free_cached_desc(struct d3d12_graphics_pipeline_state_cached_desc *cached_desc)
{
    unsigned int i;
    vkd3d_shader_transform_feedback_info_free(cached_desc->xfb_info);
    vkd3d_shader_stage_io_map_free(&cached_desc->stage_io_map_ms_ps);
    while (cached_desc->bytecode_duped_mask)
    {
        i = vkd3d_bitmask_iter32(&cached_desc->bytecode_duped_mask);
        vkd3d_free((void*)cached_desc->bytecode[i].pShaderBytecode);
    }
}

static struct vkd3d_shader_transform_feedback_info *vkd3d_shader_transform_feedback_info_dup(
        const D3D12_STREAM_OUTPUT_DESC *so_desc)
{
    struct vkd3d_shader_transform_feedback_element *new_entries = NULL;
    struct vkd3d_shader_transform_feedback_info *xfb_info;
    unsigned int *new_buffer_strides = NULL;
    unsigned int num_duped = 0;
    unsigned int i;

    xfb_info = vkd3d_calloc(1, sizeof(*xfb_info));
    if (!xfb_info)
        return NULL;

    new_buffer_strides = malloc(so_desc->NumStrides * sizeof(*new_buffer_strides));
    if (!new_buffer_strides)
        goto fail;
    memcpy(new_buffer_strides, so_desc->pBufferStrides, so_desc->NumStrides * sizeof(*new_buffer_strides));
    xfb_info->buffer_strides = new_buffer_strides;

    new_entries = malloc(so_desc->NumEntries * sizeof(*new_entries));
    if (!new_entries)
        goto fail;
    memcpy(new_entries, so_desc->pSODeclaration, so_desc->NumEntries * sizeof(*new_entries));
    xfb_info->elements = new_entries;

    for (i = 0; i < so_desc->NumEntries; i++, num_duped++)
        if (!(new_entries[i].semantic_name = vkd3d_strdup(new_entries[i].semantic_name)))
            goto fail;

    xfb_info->buffer_stride_count = so_desc->NumStrides;
    xfb_info->element_count = so_desc->NumEntries;

    return xfb_info;

fail:
    for (i = 0; i < num_duped; i++)
        vkd3d_free((void*)new_entries[i].semantic_name);
    vkd3d_free(new_buffer_strides);
    vkd3d_free(new_entries);
    vkd3d_free(xfb_info);
    return NULL;
}

void d3d12_pipeline_state_dec_ref(struct d3d12_pipeline_state *state)
{
    struct d3d12_device *device = state->device;
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    ULONG refcount = InterlockedDecrement(&state->internal_refcount);

    if (!refcount)
    {
        vkd3d_private_store_destroy(&state->private_store);

        d3d12_pipeline_state_free_spirv_code(state);
        if (d3d12_pipeline_state_is_graphics(state))
            d3d12_pipeline_state_destroy_graphics(state, device);
        else if (d3d12_pipeline_state_is_compute(state))
            VK_CALL(vkDestroyPipeline(device->vk_device, state->compute.vk_pipeline, NULL));

        VK_CALL(vkDestroyPipelineCache(device->vk_device, state->vk_pso_cache, NULL));

        if (state->root_signature)
            d3d12_root_signature_dec_ref(state->root_signature);

        if (state->pipeline_type == VKD3D_PIPELINE_TYPE_GRAPHICS || state->pipeline_type == VKD3D_PIPELINE_TYPE_MESH_GRAPHICS)
            d3d12_pipeline_state_free_cached_desc(&state->graphics.cached_desc);
        vkd3d_free(state);
    }
}

static ULONG STDMETHODCALLTYPE d3d12_pipeline_state_Release(ID3D12PipelineState *iface)
{
    struct d3d12_pipeline_state *state = impl_from_ID3D12PipelineState(iface);
    struct d3d12_device *device = state->device;
    ULONG refcount = InterlockedDecrement(&state->refcount);

    TRACE("%p decreasing refcount to %u.\n", state, refcount);

    if (!refcount)
    {
        d3d12_pipeline_state_dec_ref(state);
        /* When public ref-count hits zero, we have to release the device too. */
        d3d12_device_release(device);
    }

    return refcount;
}

static HRESULT STDMETHODCALLTYPE d3d12_pipeline_state_GetPrivateData(ID3D12PipelineState *iface,
        REFGUID guid, UINT *data_size, void *data)
{
    struct d3d12_pipeline_state *state = impl_from_ID3D12PipelineState(iface);

    TRACE("iface %p, guid %s, data_size %p, data %p.\n", iface, debugstr_guid(guid), data_size, data);

    return vkd3d_get_private_data(&state->private_store, guid, data_size, data);
}

static HRESULT STDMETHODCALLTYPE d3d12_pipeline_state_SetPrivateData(ID3D12PipelineState *iface,
        REFGUID guid, UINT data_size, const void *data)
{
    struct d3d12_pipeline_state *state = impl_from_ID3D12PipelineState(iface);

    TRACE("iface %p, guid %s, data_size %u, data %p.\n", iface, debugstr_guid(guid), data_size, data);

    return vkd3d_set_private_data(&state->private_store, guid, data_size, data,
            (vkd3d_set_name_callback) d3d12_pipeline_state_set_name, state);
}

static HRESULT STDMETHODCALLTYPE d3d12_pipeline_state_SetPrivateDataInterface(ID3D12PipelineState *iface,
        REFGUID guid, const IUnknown *data)
{
    struct d3d12_pipeline_state *state = impl_from_ID3D12PipelineState(iface);

    TRACE("iface %p, guid %s, data %p.\n", iface, debugstr_guid(guid), data);

    return vkd3d_set_private_data_interface(&state->private_store, guid, data,
            (vkd3d_set_name_callback) d3d12_pipeline_state_set_name, state);
}

static HRESULT STDMETHODCALLTYPE d3d12_pipeline_state_GetDevice(ID3D12PipelineState *iface,
        REFIID iid, void **device)
{
    struct d3d12_pipeline_state *state = impl_from_ID3D12PipelineState(iface);

    TRACE("iface %p, iid %s, device %p.\n", iface, debugstr_guid(iid), device);

    return d3d12_device_query_interface(state->device, iid, device);
}

static HRESULT STDMETHODCALLTYPE d3d12_pipeline_state_GetCachedBlob(ID3D12PipelineState *iface,
        ID3DBlob **blob)
{
    struct d3d12_pipeline_state *state = impl_from_ID3D12PipelineState(iface);
    struct d3d_blob *blob_object;
    void *cache_data = NULL;
    size_t cache_size = 0;
    VkResult vr;
    HRESULT hr;

    TRACE("iface %p, blob %p.\n", iface, blob);

    if ((vr = vkd3d_serialize_pipeline_state(NULL, state, &cache_size, NULL)))
        return hresult_from_vk_result(vr);

    if (!(cache_data = malloc(cache_size)))
        return E_OUTOFMEMORY;

    if ((vr = vkd3d_serialize_pipeline_state(NULL, state, &cache_size, cache_data)))
    {
        vkd3d_free(cache_data);
        return hresult_from_vk_result(vr);
    }

    if (vkd3d_config_flags & VKD3D_CONFIG_FLAG_PIPELINE_LIBRARY_LOG)
        INFO("Serializing cached blob: %zu bytes.\n", cache_size);

    if (FAILED(hr = d3d_blob_create(cache_data, cache_size, &blob_object)))
    {
        ERR("Failed to create blob, hr %#x.", hr);
        vkd3d_free(cache_data);
        return hr;
    }

    *blob = &blob_object->ID3DBlob_iface;
    return S_OK;
}

CONST_VTBL struct ID3D12PipelineStateVtbl d3d12_pipeline_state_vtbl =
{
    /* IUnknown methods */
    d3d12_pipeline_state_QueryInterface,
    d3d12_pipeline_state_AddRef,
    d3d12_pipeline_state_Release,
    /* ID3D12Object methods */
    d3d12_pipeline_state_GetPrivateData,
    d3d12_pipeline_state_SetPrivateData,
    d3d12_pipeline_state_SetPrivateDataInterface,
    (void *)d3d12_object_SetName,
    /* ID3D12DeviceChild methods */
    d3d12_pipeline_state_GetDevice,
    /* ID3D12PipelineState methods */
    d3d12_pipeline_state_GetCachedBlob,
};

static HRESULT vkd3d_load_spirv_from_cached_state(struct d3d12_device *device,
        const struct d3d12_cached_pipeline_state *cached_state,
        VkShaderStageFlagBits stage, struct vkd3d_shader_code *spirv_code)
{
    HRESULT hr;

    if (!cached_state->blob.CachedBlobSizeInBytes)
    {
        if (vkd3d_config_flags & VKD3D_CONFIG_FLAG_PIPELINE_LIBRARY_LOG)
            INFO("SPIR-V chunk was not found due to no Cached PSO state being provided.\n");
        return E_FAIL;
    }

    if (vkd3d_config_flags & VKD3D_CONFIG_FLAG_PIPELINE_LIBRARY_IGNORE_SPIRV)
        return E_FAIL;

    hr = vkd3d_get_cached_spirv_code_from_d3d12_desc(cached_state, stage, spirv_code);

    if (vkd3d_config_flags & VKD3D_CONFIG_FLAG_PIPELINE_LIBRARY_LOG)
    {
        if (SUCCEEDED(hr))
        {
            INFO("SPIR-V (stage: %x) for blob hash %016"PRIx64" received from cached pipeline state.\n",
                    stage, spirv_code->meta.hash);
        }
        else if (hr == E_FAIL)
            INFO("SPIR-V chunk was not found in cached PSO state.\n");
        else if (hr == E_INVALIDARG)
            INFO("Pipeline could not be created to mismatch in either root signature or DXBC blobs.\n");
        else
            INFO("Unexpected error when unserializing SPIR-V (hr %x).\n", hr);
    }

    return hr;
}

static void d3d12_pipeline_state_init_shader_interface(struct d3d12_pipeline_state *state,
        struct d3d12_device *device,
        VkShaderStageFlagBits stage,
        struct vkd3d_shader_interface_info *shader_interface)
{
    const struct d3d12_root_signature *root_signature = state->root_signature;
    memset(shader_interface, 0, sizeof(*shader_interface));
    shader_interface->flags = d3d12_root_signature_get_shader_interface_flags(root_signature);
    shader_interface->min_ssbo_alignment = d3d12_device_get_ssbo_alignment(device);
    shader_interface->descriptor_tables.offset = root_signature->descriptor_table_offset;
    shader_interface->descriptor_tables.count = root_signature->descriptor_table_count;
    shader_interface->bindings = root_signature->bindings;
    shader_interface->binding_count = root_signature->binding_count;
    shader_interface->push_constant_buffers = root_signature->root_constants;
    shader_interface->push_constant_buffer_count = root_signature->root_constant_count;
    shader_interface->push_constant_ubo_binding = &root_signature->push_constant_ubo_binding;
    shader_interface->offset_buffer_binding = &root_signature->offset_buffer_binding;
    shader_interface->stage = stage;
    shader_interface->xfb_info = state->pipeline_type == VKD3D_PIPELINE_TYPE_GRAPHICS &&
            stage == state->graphics.cached_desc.xfb_stage ?
            state->graphics.cached_desc.xfb_info : NULL;

    if (stage == VK_SHADER_STAGE_MESH_BIT_EXT)
    {
        shader_interface->stage_output_map = &state->graphics.cached_desc.stage_io_map_ms_ps;
    }
    else if ((stage == VK_SHADER_STAGE_FRAGMENT_BIT) &&
            (state->graphics.stage_flags & VK_SHADER_STAGE_MESH_BIT_EXT))
    {
        shader_interface->stage_input_map = &state->graphics.cached_desc.stage_io_map_ms_ps;
    }

#ifdef VKD3D_ENABLE_DESCRIPTOR_QA
    shader_interface->descriptor_qa_global_binding = &root_signature->descriptor_qa_global_info;
    shader_interface->descriptor_qa_heap_binding = &root_signature->descriptor_qa_heap_binding;
#endif
}

static void d3d12_pipeline_state_init_compile_arguments(struct d3d12_pipeline_state *state,
        struct d3d12_device *device, VkShaderStageFlagBits stage,
        struct vkd3d_shader_compile_arguments *compile_arguments)
{
    memset(compile_arguments, 0, sizeof(*compile_arguments));
    compile_arguments->target = VKD3D_SHADER_TARGET_SPIRV_VULKAN_1_0;
    compile_arguments->target_extension_count = device->vk_info.shader_extension_count;
    compile_arguments->target_extensions = device->vk_info.shader_extensions;
    compile_arguments->quirks = &vkd3d_shader_quirk_info;

    if (stage == VK_SHADER_STAGE_FRAGMENT_BIT)
    {
        /* Options which are exclusive to PS. Especially output swizzles must only be used in PS. */
        compile_arguments->parameter_count = ARRAY_SIZE(state->graphics.cached_desc.ps_shader_parameters);
        compile_arguments->parameters = state->graphics.cached_desc.ps_shader_parameters;
        compile_arguments->dual_source_blending = state->graphics.cached_desc.is_dual_source_blending;
        compile_arguments->output_swizzles = state->graphics.cached_desc.ps_output_swizzle;
        compile_arguments->output_swizzle_count = state->graphics.rt_count;
    }
}

static HRESULT vkd3d_create_shader_stage(struct d3d12_pipeline_state *state, struct d3d12_device *device,
        VkPipelineShaderStageCreateInfo *stage_desc, VkShaderStageFlagBits stage,
        VkPipelineShaderStageRequiredSubgroupSizeCreateInfoEXT *required_subgroup_size_info,
        const D3D12_SHADER_BYTECODE *code, struct vkd3d_shader_code *spirv_code)
{
    struct vkd3d_shader_code dxbc = {code->pShaderBytecode, code->BytecodeLength};
    struct vkd3d_shader_interface_info shader_interface;
    struct vkd3d_shader_compile_arguments compile_args;
    vkd3d_shader_hash_t recovered_hash = 0;
    vkd3d_shader_hash_t compiled_hash = 0;
    int ret;

    stage_desc->sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stage_desc->pNext = NULL;
    stage_desc->flags = 0;
    stage_desc->stage = stage;
    stage_desc->pName = "main";
    stage_desc->pSpecializationInfo = NULL;

    if (spirv_code->code && (vkd3d_config_flags & VKD3D_CONFIG_FLAG_PIPELINE_LIBRARY_SANITIZE_SPIRV))
    {
        recovered_hash = vkd3d_shader_hash(spirv_code);
        vkd3d_shader_free_shader_code(spirv_code);
        memset(spirv_code, 0, sizeof(*spirv_code));
    }

    if (!spirv_code->code)
    {
        TRACE("Calling vkd3d_shader_compile_dxbc.\n");

        d3d12_pipeline_state_init_shader_interface(state, device, stage, &shader_interface);
        d3d12_pipeline_state_init_compile_arguments(state, device, stage, &compile_args);

        if ((ret = vkd3d_shader_compile_dxbc(&dxbc, spirv_code, 0, &shader_interface, &compile_args)) < 0)
        {
            WARN("Failed to compile shader, vkd3d result %d.\n", ret);
            return hresult_from_vkd3d_result(ret);
        }
        TRACE("Called vkd3d_shader_compile_dxbc.\n");
    }

    /* Debug compare SPIR-V we got from cache, and SPIR-V we got from compilation. */
    if (recovered_hash)
    {
        compiled_hash = vkd3d_shader_hash(spirv_code);
        if (compiled_hash == recovered_hash)
            INFO("SPIR-V match for cache reference OK!\n");
        else
            INFO("SPIR-V mismatch for cache reference!\n");
    }

    if (!d3d12_device_validate_shader_meta(device, &spirv_code->meta))
        return E_INVALIDARG;

    if (((spirv_code->meta.flags & VKD3D_SHADER_META_FLAG_USES_SUBGROUP_SIZE) &&
            device->device_info.subgroup_size_control_features.subgroupSizeControl) ||
            spirv_code->meta.cs_required_wave_size)
    {
        uint32_t subgroup_size_alignment = device->device_info.subgroup_size_control_properties.maxSubgroupSize;
        stage_desc->flags |= VK_PIPELINE_SHADER_STAGE_CREATE_ALLOW_VARYING_SUBGROUP_SIZE_BIT_EXT;

        if (required_subgroup_size_info)
        {
            if (spirv_code->meta.cs_required_wave_size)
            {
                /* [WaveSize(N)] attribute in SM 6.6. */
                subgroup_size_alignment = spirv_code->meta.cs_required_wave_size;
                stage_desc->pNext = required_subgroup_size_info;
                stage_desc->flags &= ~VK_PIPELINE_SHADER_STAGE_CREATE_ALLOW_VARYING_SUBGROUP_SIZE_BIT_EXT;
            }
            else if ((vkd3d_config_flags & VKD3D_CONFIG_FLAG_FORCE_MINIMUM_SUBGROUP_SIZE) &&
                    (device->device_info.subgroup_size_control_properties.requiredSubgroupSizeStages & stage))
            {
                /* GravityMark checks minSubgroupSize and based on that uses a shader variant.
                 * This shader variant unfortunately expects that a subgroup 32 variant will actually use wave32 on AMD.
                 * amdgpu-pro and AMDVLK happens to emit wave32, but RADV will emit wave64 here unless we force it to be wave32.
                 * This is an application bug, since the shader is not guaranteed a specific size, but we can only workaround ... */
                subgroup_size_alignment = device->device_info.subgroup_size_control_properties.minSubgroupSize;
                stage_desc->pNext = required_subgroup_size_info;
                stage_desc->flags &= ~VK_PIPELINE_SHADER_STAGE_CREATE_ALLOW_VARYING_SUBGROUP_SIZE_BIT_EXT;
            }

            required_subgroup_size_info->sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_REQUIRED_SUBGROUP_SIZE_CREATE_INFO_EXT;
            required_subgroup_size_info->pNext = NULL;
            required_subgroup_size_info->requiredSubgroupSize = subgroup_size_alignment;
        }

        /* If we can, we should be explicit and enable FULL_SUBGROUPS bit as well. This should be default
         * behavior, but cannot hurt. */
        if (stage == VK_SHADER_STAGE_COMPUTE_BIT &&
                device->device_info.subgroup_size_control_features.computeFullSubgroups &&
                !(spirv_code->meta.cs_workgroup_size[0] % subgroup_size_alignment))
        {
            stage_desc->flags |= VK_PIPELINE_SHADER_STAGE_CREATE_REQUIRE_FULL_SUBGROUPS_BIT_EXT;
        }
    }

    stage_desc->module = VK_NULL_HANDLE;
    return d3d12_pipeline_state_create_shader_module(device, stage_desc, spirv_code);
}

static void vkd3d_report_pipeline_creation_feedback_results(const VkPipelineCreationFeedbackCreateInfoEXT *feedback)
{
    uint32_t i;

    if (feedback->pPipelineCreationFeedback->flags & VK_PIPELINE_CREATION_FEEDBACK_VALID_BIT_EXT)
    {
        if (feedback->pPipelineCreationFeedback->flags &
                VK_PIPELINE_CREATION_FEEDBACK_APPLICATION_PIPELINE_CACHE_HIT_BIT_EXT)
        {
            INFO("Pipeline compilation reused pipeline cache.\n");
        }
        else
        {
            INFO("Pipeline compilation did not reuse pipeline cache data, compilation took %"PRIu64" ns.\n",
                    feedback->pPipelineCreationFeedback->duration);
        }
    }
    else
        INFO("Global feedback is not marked valid.\n");

    for (i = 0; i < feedback->pipelineStageCreationFeedbackCount; i++)
    {
        if (feedback->pPipelineStageCreationFeedbacks[i].flags &
                VK_PIPELINE_CREATION_FEEDBACK_VALID_BIT_EXT)
        {
            if (feedback->pPipelineStageCreationFeedbacks[i].flags &
                    VK_PIPELINE_CREATION_FEEDBACK_APPLICATION_PIPELINE_CACHE_HIT_BIT_EXT)
            {
                INFO("  Stage %u: Pipeline compilation reused pipeline cache.\n", i);
            }
            else
            {
                INFO("  Stage %u: compilation took %"PRIu64" ns.\n",
                        i, feedback->pPipelineCreationFeedback->duration);
            }
        }
        else
            INFO("  Stage %u: Feedback is not marked valid.\n", i);
    }
}

static HRESULT vkd3d_create_compute_pipeline(struct d3d12_pipeline_state *state,
        struct d3d12_device *device,
        const D3D12_SHADER_BYTECODE *code)
{
    VkPipelineShaderStageRequiredSubgroupSizeCreateInfoEXT required_subgroup_size_info;
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    VkPipelineCreationFeedbackCreateInfoEXT feedback_info;
    struct vkd3d_shader_debug_ring_spec_info spec_info;
    VkPipelineCreationFeedbackEXT feedbacks[1];
    VkComputePipelineCreateInfo pipeline_info;
    VkPipelineCreationFeedbackEXT feedback;
    struct vkd3d_shader_code *spirv_code;
    VkPipelineCache vk_cache;
    VkResult vr;
    HRESULT hr;

    vk_cache = state->vk_pso_cache;
    spirv_code = &state->compute.code;

    pipeline_info.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipeline_info.pNext = NULL;
    pipeline_info.flags = 0;
    if (FAILED(hr = vkd3d_create_shader_stage(state, device,
            &pipeline_info.stage,
            VK_SHADER_STAGE_COMPUTE_BIT, &required_subgroup_size_info,
            code, spirv_code)))
        return hr;
    pipeline_info.layout = state->root_signature->compute.vk_pipeline_layout;
    pipeline_info.basePipelineHandle = VK_NULL_HANDLE;
    pipeline_info.basePipelineIndex = -1;

    if ((spirv_code->meta.flags & VKD3D_SHADER_META_FLAG_REPLACED) && device->debug_ring.active)
    {
        vkd3d_shader_debug_ring_init_spec_constant(device, &spec_info, spirv_code->meta.hash);
        pipeline_info.stage.pSpecializationInfo = &spec_info.spec_info;
    }

    TRACE("Calling vkCreateComputePipelines.\n");

    if ((vkd3d_config_flags & VKD3D_CONFIG_FLAG_PIPELINE_LIBRARY_LOG) &&
            device->vk_info.EXT_pipeline_creation_feedback)
    {
        feedback_info.sType = VK_STRUCTURE_TYPE_PIPELINE_CREATION_FEEDBACK_CREATE_INFO_EXT;
        feedback_info.pNext = pipeline_info.pNext;
        feedback_info.pPipelineStageCreationFeedbacks = feedbacks;
        feedback_info.pipelineStageCreationFeedbackCount = 1;
        feedback_info.pPipelineCreationFeedback = &feedback;
        pipeline_info.pNext = &feedback_info;
    }
    else
        feedback_info.pipelineStageCreationFeedbackCount = 0;

    vr = VK_CALL(vkCreateComputePipelines(device->vk_device,
            vk_cache, 1, &pipeline_info, NULL, &state->compute.vk_pipeline));

    TRACE("Called vkCreateComputePipelines.\n");
    VK_CALL(vkDestroyShaderModule(device->vk_device, pipeline_info.stage.module, NULL));
    if (vr < 0)
    {
        WARN("Failed to create Vulkan compute pipeline, hr %#x.", hr);
        return hresult_from_vk_result(vr);
    }

    if (feedback_info.pipelineStageCreationFeedbackCount)
        vkd3d_report_pipeline_creation_feedback_results(&feedback_info);

    return S_OK;
}

static HRESULT d3d12_pipeline_state_init_compute(struct d3d12_pipeline_state *state,
        struct d3d12_device *device, const struct d3d12_pipeline_state_desc *desc,
        const struct d3d12_cached_pipeline_state *cached_pso)
{
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    struct vkd3d_shader_interface_info shader_interface;
    HRESULT hr;

    state->pipeline_type = VKD3D_PIPELINE_TYPE_COMPUTE;
    d3d12_pipeline_state_init_shader_interface(state, device,
            VK_SHADER_STAGE_COMPUTE_BIT, &shader_interface);

    vkd3d_load_spirv_from_cached_state(device, cached_pso,
            VK_SHADER_STAGE_COMPUTE_BIT, &state->compute.code);

    hr = vkd3d_create_compute_pipeline(state, device, &desc->cs);

    if (FAILED(hr))
    {
        WARN("Failed to create Vulkan compute pipeline, hr %#x.\n", hr);
        return hr;
    }

    if (FAILED(hr = vkd3d_private_store_init(&state->private_store)))
    {
        VK_CALL(vkDestroyPipeline(device->vk_device, state->compute.vk_pipeline, NULL));
        return hr;
    }

    d3d12_device_add_ref(state->device = device);

    return S_OK;
}

static enum VkPolygonMode vk_polygon_mode_from_d3d12(D3D12_FILL_MODE mode)
{
    switch (mode)
    {
        case D3D12_FILL_MODE_WIREFRAME:
            return VK_POLYGON_MODE_LINE;
        case D3D12_FILL_MODE_SOLID:
            return VK_POLYGON_MODE_FILL;
        default:
            FIXME("Unhandled fill mode %#x.\n", mode);
            return VK_POLYGON_MODE_FILL;
    }
}

static enum VkCullModeFlagBits vk_cull_mode_from_d3d12(D3D12_CULL_MODE mode)
{
    switch (mode)
    {
        case D3D12_CULL_MODE_NONE:
            return VK_CULL_MODE_NONE;
        case D3D12_CULL_MODE_FRONT:
            return VK_CULL_MODE_FRONT_BIT;
        case D3D12_CULL_MODE_BACK:
            return VK_CULL_MODE_BACK_BIT;
        default:
            FIXME("Unhandled cull mode %#x.\n", mode);
            return VK_CULL_MODE_NONE;
    }
}

static void rs_desc_from_d3d12(VkPipelineRasterizationStateCreateInfo *vk_desc,
        const D3D12_RASTERIZER_DESC *d3d12_desc)
{
    vk_desc->sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    vk_desc->pNext = NULL;
    vk_desc->flags = 0;
    vk_desc->depthClampEnable = !d3d12_desc->DepthClipEnable;
    vk_desc->rasterizerDiscardEnable = VK_FALSE;
    vk_desc->polygonMode = vk_polygon_mode_from_d3d12(d3d12_desc->FillMode);
    vk_desc->cullMode = vk_cull_mode_from_d3d12(d3d12_desc->CullMode);
    vk_desc->frontFace = d3d12_desc->FrontCounterClockwise ? VK_FRONT_FACE_COUNTER_CLOCKWISE : VK_FRONT_FACE_CLOCKWISE;
    vk_desc->depthBiasEnable = d3d12_desc->DepthBias || d3d12_desc->SlopeScaledDepthBias;
    vk_desc->depthBiasConstantFactor = d3d12_desc->DepthBias;
    vk_desc->depthBiasClamp = d3d12_desc->DepthBiasClamp;
    vk_desc->depthBiasSlopeFactor = d3d12_desc->SlopeScaledDepthBias;
    vk_desc->lineWidth = 1.0f;

    if (d3d12_desc->MultisampleEnable)
        FIXME_ONCE("Ignoring MultisampleEnable %#x.\n", d3d12_desc->MultisampleEnable);
    if (d3d12_desc->AntialiasedLineEnable)
        FIXME_ONCE("Ignoring AntialiasedLineEnable %#x.\n", d3d12_desc->AntialiasedLineEnable);
    if (d3d12_desc->ForcedSampleCount)
        FIXME("Ignoring ForcedSampleCount %#x.\n", d3d12_desc->ForcedSampleCount);
}

static void rs_conservative_info_from_d3d12(VkPipelineRasterizationConservativeStateCreateInfoEXT *conservative_info,
        VkPipelineRasterizationStateCreateInfo *vk_rs_desc, const D3D12_RASTERIZER_DESC *d3d12_desc)
{
    if (d3d12_desc->ConservativeRaster == D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF)
        return;

    conservative_info->sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_CONSERVATIVE_STATE_CREATE_INFO_EXT;
    conservative_info->pNext = NULL;
    conservative_info->flags = 0;
    conservative_info->conservativeRasterizationMode = VK_CONSERVATIVE_RASTERIZATION_MODE_OVERESTIMATE_EXT;
    conservative_info->extraPrimitiveOverestimationSize = 0.0f;

    vk_prepend_struct(vk_rs_desc, conservative_info);
}

static void rs_depth_clip_info_from_d3d12(VkPipelineRasterizationDepthClipStateCreateInfoEXT *depth_clip_info,
        VkPipelineRasterizationStateCreateInfo *vk_rs_desc, const D3D12_RASTERIZER_DESC *d3d12_desc)
{
    vk_rs_desc->depthClampEnable = VK_TRUE;

    depth_clip_info->sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_DEPTH_CLIP_STATE_CREATE_INFO_EXT;
    depth_clip_info->pNext = NULL;
    depth_clip_info->flags = 0;
    depth_clip_info->depthClipEnable = d3d12_desc->DepthClipEnable;

    vk_prepend_struct(vk_rs_desc, depth_clip_info);
}

static void rs_stream_info_from_d3d12(VkPipelineRasterizationStateStreamCreateInfoEXT *stream_info,
        VkPipelineRasterizationStateCreateInfo *vk_rs_desc, const D3D12_STREAM_OUTPUT_DESC *so_desc,
        const struct vkd3d_vulkan_info *vk_info)
{
    if (!so_desc->NumEntries || !so_desc->RasterizedStream
            || so_desc->RasterizedStream == D3D12_SO_NO_RASTERIZED_STREAM)
        return;

    if (!vk_info->rasterization_stream)
    {
        FIXME("Rasterization stream select is not supported by Vulkan implementation.\n");
        return;
    }

    stream_info->sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_STREAM_CREATE_INFO_EXT;
    stream_info->pNext = NULL;
    stream_info->flags = 0;
    stream_info->rasterizationStream = so_desc->RasterizedStream;

    vk_prepend_struct(vk_rs_desc, stream_info);
}

static enum VkStencilOp vk_stencil_op_from_d3d12(D3D12_STENCIL_OP op)
{
    switch (op)
    {
        case D3D12_STENCIL_OP_KEEP:
            return VK_STENCIL_OP_KEEP;
        case D3D12_STENCIL_OP_ZERO:
            return VK_STENCIL_OP_ZERO;
        case D3D12_STENCIL_OP_REPLACE:
            return VK_STENCIL_OP_REPLACE;
        case D3D12_STENCIL_OP_INCR_SAT:
            return VK_STENCIL_OP_INCREMENT_AND_CLAMP;
        case D3D12_STENCIL_OP_DECR_SAT:
            return VK_STENCIL_OP_DECREMENT_AND_CLAMP;
        case D3D12_STENCIL_OP_INVERT:
            return VK_STENCIL_OP_INVERT;
        case D3D12_STENCIL_OP_INCR:
            return VK_STENCIL_OP_INCREMENT_AND_WRAP;
        case D3D12_STENCIL_OP_DECR:
            return VK_STENCIL_OP_DECREMENT_AND_WRAP;
        default:
            FIXME("Unhandled stencil op %#x.\n", op);
            return VK_STENCIL_OP_KEEP;
    }
}

enum VkCompareOp vk_compare_op_from_d3d12(D3D12_COMPARISON_FUNC op)
{
    switch (op)
    {
        case D3D12_COMPARISON_FUNC_NEVER:
            return VK_COMPARE_OP_NEVER;
        case D3D12_COMPARISON_FUNC_LESS:
            return VK_COMPARE_OP_LESS;
        case D3D12_COMPARISON_FUNC_EQUAL:
            return VK_COMPARE_OP_EQUAL;
        case D3D12_COMPARISON_FUNC_LESS_EQUAL:
            return VK_COMPARE_OP_LESS_OR_EQUAL;
        case D3D12_COMPARISON_FUNC_GREATER:
            return VK_COMPARE_OP_GREATER;
        case D3D12_COMPARISON_FUNC_NOT_EQUAL:
            return VK_COMPARE_OP_NOT_EQUAL;
        case D3D12_COMPARISON_FUNC_GREATER_EQUAL:
            return VK_COMPARE_OP_GREATER_OR_EQUAL;
        case D3D12_COMPARISON_FUNC_ALWAYS:
            return VK_COMPARE_OP_ALWAYS;
        default:
            FIXME("Unhandled compare op %#x.\n", op);
            return VK_COMPARE_OP_NEVER;
    }
}

static void vk_stencil_op_state_from_d3d12(struct VkStencilOpState *vk_desc,
        const D3D12_DEPTH_STENCILOP_DESC *d3d12_desc, uint32_t compare_mask, uint32_t write_mask)
{
    vk_desc->failOp = vk_stencil_op_from_d3d12(d3d12_desc->StencilFailOp);
    vk_desc->passOp = vk_stencil_op_from_d3d12(d3d12_desc->StencilPassOp);
    vk_desc->depthFailOp = vk_stencil_op_from_d3d12(d3d12_desc->StencilDepthFailOp);
    vk_desc->compareOp = vk_compare_op_from_d3d12(d3d12_desc->StencilFunc);
    vk_desc->compareMask = compare_mask;
    vk_desc->writeMask = write_mask;
    /* The stencil reference value is a dynamic state. Set by OMSetStencilRef(). */
    vk_desc->reference = 0;
}

static void ds_desc_from_d3d12(struct VkPipelineDepthStencilStateCreateInfo *vk_desc,
        const D3D12_DEPTH_STENCIL_DESC1 *d3d12_desc)
{
    memset(vk_desc, 0, sizeof(*vk_desc));
    vk_desc->sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    vk_desc->pNext = NULL;
    vk_desc->flags = 0;
    if ((vk_desc->depthTestEnable = d3d12_desc->DepthEnable))
    {
        vk_desc->depthWriteEnable = d3d12_desc->DepthWriteMask & D3D12_DEPTH_WRITE_MASK_ALL;
        vk_desc->depthCompareOp = vk_compare_op_from_d3d12(d3d12_desc->DepthFunc);
    }
    else
    {
        vk_desc->depthWriteEnable = VK_FALSE;
        vk_desc->depthCompareOp = VK_COMPARE_OP_NEVER;
    }
    vk_desc->depthBoundsTestEnable = d3d12_desc->DepthBoundsTestEnable;
    if ((vk_desc->stencilTestEnable = d3d12_desc->StencilEnable))
    {
        vk_stencil_op_state_from_d3d12(&vk_desc->front, &d3d12_desc->FrontFace,
                d3d12_desc->StencilReadMask, d3d12_desc->StencilWriteMask);
        vk_stencil_op_state_from_d3d12(&vk_desc->back, &d3d12_desc->BackFace,
                d3d12_desc->StencilReadMask, d3d12_desc->StencilWriteMask);
    }
    else
    {
        memset(&vk_desc->front, 0, sizeof(vk_desc->front));
        memset(&vk_desc->back, 0, sizeof(vk_desc->back));
    }
    vk_desc->minDepthBounds = 0.0f;
    vk_desc->maxDepthBounds = 1.0f;
}

static enum VkBlendFactor vk_blend_factor_from_d3d12(D3D12_BLEND blend, bool alpha)
{
    switch (blend)
    {
        case D3D12_BLEND_ZERO:
            return VK_BLEND_FACTOR_ZERO;
        case D3D12_BLEND_ONE:
            return VK_BLEND_FACTOR_ONE;
        case D3D12_BLEND_SRC_COLOR:
            return VK_BLEND_FACTOR_SRC_COLOR;
        case D3D12_BLEND_INV_SRC_COLOR:
            return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
        case D3D12_BLEND_SRC_ALPHA:
            return VK_BLEND_FACTOR_SRC_ALPHA;
        case D3D12_BLEND_INV_SRC_ALPHA:
            return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        case D3D12_BLEND_DEST_ALPHA:
            return VK_BLEND_FACTOR_DST_ALPHA;
        case D3D12_BLEND_INV_DEST_ALPHA:
            return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
        case D3D12_BLEND_DEST_COLOR:
            return VK_BLEND_FACTOR_DST_COLOR;
        case D3D12_BLEND_INV_DEST_COLOR:
            return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
        case D3D12_BLEND_SRC_ALPHA_SAT:
            return VK_BLEND_FACTOR_SRC_ALPHA_SATURATE;
        case D3D12_BLEND_BLEND_FACTOR:
            if (alpha)
                return VK_BLEND_FACTOR_CONSTANT_ALPHA;
            return VK_BLEND_FACTOR_CONSTANT_COLOR;
        case D3D12_BLEND_INV_BLEND_FACTOR:
            if (alpha)
                return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA;
            return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR;
        case D3D12_BLEND_SRC1_COLOR:
            return VK_BLEND_FACTOR_SRC1_COLOR;
        case D3D12_BLEND_INV_SRC1_COLOR:
            return VK_BLEND_FACTOR_ONE_MINUS_SRC1_COLOR;
        case D3D12_BLEND_SRC1_ALPHA:
            return VK_BLEND_FACTOR_SRC1_ALPHA;
        case D3D12_BLEND_INV_SRC1_ALPHA:
            return VK_BLEND_FACTOR_ONE_MINUS_SRC1_ALPHA;
        default:
            FIXME("Unhandled blend %#x.\n", blend);
            return VK_BLEND_FACTOR_ZERO;
    }
}

static enum VkBlendOp vk_blend_op_from_d3d12(D3D12_BLEND_OP op)
{
    switch (op)
    {
        case D3D12_BLEND_OP_ADD:
            return VK_BLEND_OP_ADD;
        case D3D12_BLEND_OP_SUBTRACT:
            return VK_BLEND_OP_SUBTRACT;
        case D3D12_BLEND_OP_REV_SUBTRACT:
            return VK_BLEND_OP_REVERSE_SUBTRACT;
        case D3D12_BLEND_OP_MIN:
            return VK_BLEND_OP_MIN;
        case D3D12_BLEND_OP_MAX:
            return VK_BLEND_OP_MAX;
        default:
            FIXME("Unhandled blend op %#x.\n", op);
            return VK_BLEND_OP_ADD;
    }
}

static void blend_attachment_from_d3d12(struct VkPipelineColorBlendAttachmentState *vk_desc,
        const D3D12_RENDER_TARGET_BLEND_DESC *d3d12_desc)
{
    if (d3d12_desc->BlendEnable && d3d12_desc->RenderTargetWriteMask)
    {
        vk_desc->blendEnable = VK_TRUE;
        vk_desc->srcColorBlendFactor = vk_blend_factor_from_d3d12(d3d12_desc->SrcBlend, false);
        vk_desc->dstColorBlendFactor = vk_blend_factor_from_d3d12(d3d12_desc->DestBlend, false);
        vk_desc->colorBlendOp = vk_blend_op_from_d3d12(d3d12_desc->BlendOp);
        vk_desc->srcAlphaBlendFactor = vk_blend_factor_from_d3d12(d3d12_desc->SrcBlendAlpha, true);
        vk_desc->dstAlphaBlendFactor = vk_blend_factor_from_d3d12(d3d12_desc->DestBlendAlpha, true);
        vk_desc->alphaBlendOp = vk_blend_op_from_d3d12(d3d12_desc->BlendOpAlpha);
    }
    else
    {
        memset(vk_desc, 0, sizeof(*vk_desc));
    }
    vk_desc->colorWriteMask = 0;
    if (d3d12_desc->RenderTargetWriteMask & D3D12_COLOR_WRITE_ENABLE_RED)
        vk_desc->colorWriteMask |= VK_COLOR_COMPONENT_R_BIT;
    if (d3d12_desc->RenderTargetWriteMask & D3D12_COLOR_WRITE_ENABLE_GREEN)
        vk_desc->colorWriteMask |= VK_COLOR_COMPONENT_G_BIT;
    if (d3d12_desc->RenderTargetWriteMask & D3D12_COLOR_WRITE_ENABLE_BLUE)
        vk_desc->colorWriteMask |= VK_COLOR_COMPONENT_B_BIT;
    if (d3d12_desc->RenderTargetWriteMask & D3D12_COLOR_WRITE_ENABLE_ALPHA)
        vk_desc->colorWriteMask |= VK_COLOR_COMPONENT_A_BIT;
}

static VkLogicOp vk_logic_op_from_d3d12(D3D12_LOGIC_OP op)
{
    switch (op)
    {
        case D3D12_LOGIC_OP_CLEAR:
            return VK_LOGIC_OP_CLEAR;
        case D3D12_LOGIC_OP_SET:
            return VK_LOGIC_OP_SET;
        case D3D12_LOGIC_OP_COPY:
            return VK_LOGIC_OP_COPY;
        case D3D12_LOGIC_OP_COPY_INVERTED:
            return VK_LOGIC_OP_COPY_INVERTED;
        case D3D12_LOGIC_OP_NOOP:
            return VK_LOGIC_OP_NO_OP;
        case D3D12_LOGIC_OP_INVERT:
            return VK_LOGIC_OP_INVERT;
        case D3D12_LOGIC_OP_AND:
            return VK_LOGIC_OP_AND;
        case D3D12_LOGIC_OP_NAND:
            return VK_LOGIC_OP_NAND;
        case D3D12_LOGIC_OP_OR:
            return VK_LOGIC_OP_OR;
        case D3D12_LOGIC_OP_NOR:
            return VK_LOGIC_OP_NOR;
        case D3D12_LOGIC_OP_XOR:
            return VK_LOGIC_OP_XOR;
        case D3D12_LOGIC_OP_EQUIV:
            return VK_LOGIC_OP_EQUIVALENT;
        case D3D12_LOGIC_OP_AND_REVERSE:
            return VK_LOGIC_OP_AND_REVERSE;
        case D3D12_LOGIC_OP_AND_INVERTED:
            return VK_LOGIC_OP_AND_INVERTED;
        case D3D12_LOGIC_OP_OR_REVERSE:
            return VK_LOGIC_OP_OR_REVERSE;
        case D3D12_LOGIC_OP_OR_INVERTED:
            return VK_LOGIC_OP_OR_INVERTED;
        default:
            FIXME("Unhandled logic op %#x.\n", op);
            return VK_LOGIC_OP_NO_OP;
    }
}

static void blend_desc_from_d3d12(VkPipelineColorBlendStateCreateInfo *vk_desc, const D3D12_BLEND_DESC *d3d12_desc,
        uint32_t attachment_count, const VkPipelineColorBlendAttachmentState *attachments)
{
    vk_desc->sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    vk_desc->pNext = NULL;
    vk_desc->flags = 0;
    vk_desc->logicOpEnable = d3d12_desc->RenderTarget[0].LogicOpEnable;
    vk_desc->logicOp = vk_logic_op_from_d3d12(d3d12_desc->RenderTarget[0].LogicOp);
    vk_desc->attachmentCount = attachment_count;
    vk_desc->pAttachments = attachments;
    /* Blend constants are dynamic state */
    memset(vk_desc->blendConstants, 0, sizeof(vk_desc->blendConstants));
}

static bool is_dual_source_blending_blend(D3D12_BLEND b)
{
    return b == D3D12_BLEND_SRC1_COLOR || b == D3D12_BLEND_INV_SRC1_COLOR
            || b == D3D12_BLEND_SRC1_ALPHA || b == D3D12_BLEND_INV_SRC1_ALPHA;
}

static bool is_dual_source_blending(const D3D12_RENDER_TARGET_BLEND_DESC *desc)
{
    return desc->BlendEnable && desc->RenderTargetWriteMask
            && (is_dual_source_blending_blend(desc->SrcBlend)
            || is_dual_source_blending_blend(desc->DestBlend)
            || is_dual_source_blending_blend(desc->SrcBlendAlpha)
            || is_dual_source_blending_blend(desc->DestBlendAlpha));
}

static HRESULT compute_input_layout_offsets(const struct d3d12_device *device,
        const D3D12_INPUT_LAYOUT_DESC *input_layout_desc, uint32_t *offsets)
{
    uint32_t input_slot_offsets[D3D12_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT] = {0};
    const D3D12_INPUT_ELEMENT_DESC *e;
    const struct vkd3d_format *format;
    unsigned int i;

    if (input_layout_desc->NumElements > D3D12_VS_INPUT_REGISTER_COUNT)
    {
        FIXME("InputLayout.NumElements %u > %u, ignoring extra elements.\n",
                input_layout_desc->NumElements, D3D12_VS_INPUT_REGISTER_COUNT);
    }

    for (i = 0; i < min(input_layout_desc->NumElements, D3D12_VS_INPUT_REGISTER_COUNT); ++i)
    {
        e = &input_layout_desc->pInputElementDescs[i];

        if (e->InputSlot >= ARRAY_SIZE(input_slot_offsets))
        {
            WARN("Invalid input slot %#x.\n", e->InputSlot);
            return E_INVALIDARG;
        }

        if (!(format = vkd3d_get_format(device, e->Format, false)))
        {
            WARN("Invalid input element format %#x.\n", e->Format);
            return E_INVALIDARG;
        }

        if (e->AlignedByteOffset != D3D12_APPEND_ALIGNED_ELEMENT)
            offsets[i] = e->AlignedByteOffset;
        else
            offsets[i] = align(input_slot_offsets[e->InputSlot], min(4, format->byte_count));

        input_slot_offsets[e->InputSlot] = offsets[i] + format->byte_count;
    }

    return S_OK;
}

static unsigned int vkd3d_get_rt_format_swizzle(const struct vkd3d_format *format)
{
    if (format->dxgi_format == DXGI_FORMAT_A8_UNORM)
        return VKD3D_SWIZZLE(VKD3D_SWIZZLE_W, VKD3D_SWIZZLE_X, VKD3D_SWIZZLE_Y, VKD3D_SWIZZLE_Z);

    return VKD3D_NO_SWIZZLE;
}

STATIC_ASSERT(sizeof(struct vkd3d_shader_transform_feedback_element) == sizeof(D3D12_SO_DECLARATION_ENTRY));

static uint32_t d3d12_graphics_pipeline_state_get_plane_optimal_mask(
        struct d3d12_graphics_pipeline_state *graphics, const struct vkd3d_format *dynamic_dsv_format)
{
    VkFormat dsv_format = VK_FORMAT_UNDEFINED;
    uint32_t plane_optimal_mask = 0;
    VkImageAspectFlags aspects = 0;

    if (dynamic_dsv_format)
    {
        if (graphics->dsv_format)
        {
            dsv_format = graphics->dsv_format->vk_format;
            aspects = graphics->dsv_format->vk_aspect_mask;
        }
        else if (d3d12_graphics_pipeline_state_has_unknown_dsv_format_with_test(graphics))
        {
            dsv_format = dynamic_dsv_format->vk_format;
            aspects = dynamic_dsv_format->vk_aspect_mask;
        }
    }

    if (dsv_format)
    {
        assert(graphics->ds_desc.front.writeMask == graphics->ds_desc.back.writeMask);

        if ((aspects & VK_IMAGE_ASPECT_DEPTH_BIT) &&
                ((graphics->ds_desc.depthTestEnable || graphics->ds_desc.depthBoundsTestEnable) && graphics->ds_desc.depthWriteEnable))
            plane_optimal_mask |= VKD3D_DEPTH_PLANE_OPTIMAL;

        if ((aspects & VK_IMAGE_ASPECT_STENCIL_BIT) &&
                (graphics->ds_desc.stencilTestEnable && graphics->ds_desc.front.writeMask))
            plane_optimal_mask |= VKD3D_STENCIL_PLANE_OPTIMAL;

        /* If our format does not have both aspects, use same state across the aspects so that we are more likely
         * to match one of our common formats, DS_READ_ONLY or DS_OPTIMAL.
         * Otherwise, we are very likely to hit the DS write / stencil read layout. */
        if (!(aspects & VK_IMAGE_ASPECT_DEPTH_BIT))
            plane_optimal_mask |= (plane_optimal_mask & VKD3D_STENCIL_PLANE_OPTIMAL) ? VKD3D_DEPTH_PLANE_OPTIMAL : 0;

        if (!(aspects & VK_IMAGE_ASPECT_STENCIL_BIT))
            plane_optimal_mask |= (plane_optimal_mask & VKD3D_DEPTH_PLANE_OPTIMAL) ? VKD3D_STENCIL_PLANE_OPTIMAL : 0;
    }

    return plane_optimal_mask;
}

static bool vk_blend_factor_needs_blend_constants(VkBlendFactor blend_factor)
{
    return blend_factor == VK_BLEND_FACTOR_CONSTANT_COLOR ||
            blend_factor == VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR ||
            blend_factor == VK_BLEND_FACTOR_CONSTANT_ALPHA ||
            blend_factor == VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA;
}

static bool vk_blend_attachment_needs_blend_constants(const VkPipelineColorBlendAttachmentState *attachment)
{
    return attachment->blendEnable && (
            vk_blend_factor_needs_blend_constants(attachment->srcColorBlendFactor) ||
            vk_blend_factor_needs_blend_constants(attachment->dstColorBlendFactor) ||
            vk_blend_factor_needs_blend_constants(attachment->srcAlphaBlendFactor) ||
            vk_blend_factor_needs_blend_constants(attachment->dstAlphaBlendFactor));
}

static const struct
{
    enum vkd3d_dynamic_state_flag flag;
    VkDynamicState vk_state;
}
vkd3d_dynamic_state_list[] =
{
    { VKD3D_DYNAMIC_STATE_VIEWPORT,              VK_DYNAMIC_STATE_VIEWPORT_WITH_COUNT_EXT },
    { VKD3D_DYNAMIC_STATE_SCISSOR,               VK_DYNAMIC_STATE_SCISSOR_WITH_COUNT_EXT },
    { VKD3D_DYNAMIC_STATE_BLEND_CONSTANTS,       VK_DYNAMIC_STATE_BLEND_CONSTANTS },
    { VKD3D_DYNAMIC_STATE_STENCIL_REFERENCE,     VK_DYNAMIC_STATE_STENCIL_REFERENCE },
    { VKD3D_DYNAMIC_STATE_DEPTH_BOUNDS,          VK_DYNAMIC_STATE_DEPTH_BOUNDS },
    { VKD3D_DYNAMIC_STATE_TOPOLOGY,              VK_DYNAMIC_STATE_PRIMITIVE_TOPOLOGY_EXT },
    { VKD3D_DYNAMIC_STATE_VERTEX_BUFFER_STRIDE,  VK_DYNAMIC_STATE_VERTEX_INPUT_BINDING_STRIDE_EXT },
    { VKD3D_DYNAMIC_STATE_FRAGMENT_SHADING_RATE, VK_DYNAMIC_STATE_FRAGMENT_SHADING_RATE_KHR },
    { VKD3D_DYNAMIC_STATE_PRIMITIVE_RESTART,     VK_DYNAMIC_STATE_PRIMITIVE_RESTART_ENABLE_EXT },
};

static uint32_t d3d12_graphics_pipeline_state_init_dynamic_state(struct d3d12_pipeline_state *state,
        VkPipelineDynamicStateCreateInfo *dynamic_desc, VkDynamicState *dynamic_state_buffer,
        const struct vkd3d_pipeline_key *key)
{
    struct d3d12_graphics_pipeline_state *graphics = &state->graphics;
    uint32_t dynamic_state_flags;
    unsigned int i, count;
    bool is_mesh_pipeline;

    is_mesh_pipeline = !!(graphics->stage_flags & VK_SHADER_STAGE_MESH_BIT_EXT);

    dynamic_state_flags = 0;

    /* Enable dynamic states as necessary */
    dynamic_state_flags |= VKD3D_DYNAMIC_STATE_VIEWPORT | VKD3D_DYNAMIC_STATE_SCISSOR;

    if (graphics->attribute_binding_count && !is_mesh_pipeline)
    {
        if (!key || key->dynamic_stride)
            dynamic_state_flags |= VKD3D_DYNAMIC_STATE_VERTEX_BUFFER_STRIDE;
        else
            dynamic_state_flags |= VKD3D_DYNAMIC_STATE_VERTEX_BUFFER;
    }

    if ((!key || key->dynamic_topology) && !is_mesh_pipeline)
        dynamic_state_flags |= VKD3D_DYNAMIC_STATE_TOPOLOGY;

    if (graphics->ds_desc.stencilTestEnable)
    {
        dynamic_state_flags |= VKD3D_DYNAMIC_STATE_STENCIL_REFERENCE;
    }

    if (graphics->ds_desc.depthBoundsTestEnable)
    {
        dynamic_state_flags |= VKD3D_DYNAMIC_STATE_DEPTH_BOUNDS;
    }

    for (i = 0; i < graphics->rt_count; i++)
    {
        if (vk_blend_attachment_needs_blend_constants(&graphics->blend_attachments[i]))
        {
            dynamic_state_flags |= VKD3D_DYNAMIC_STATE_BLEND_CONSTANTS;
        }
    }

    /* We always need to enable fragment shading rate dynamic state when rasterizing.
     * D3D12 has no information about this ahead of time for a pipeline
     * unlike Vulkan.
     * Target Independent Rasterization (ForcedSampleCount) is not supported when this is used
     * so we don't need to worry about side effects when there are no render targets. */
    if (d3d12_device_supports_variable_shading_rate_tier_1(state->device) && graphics->rt_count)
        dynamic_state_flags |= VKD3D_DYNAMIC_STATE_FRAGMENT_SHADING_RATE;

    if (graphics->index_buffer_strip_cut_value && !is_mesh_pipeline)
        dynamic_state_flags |= VKD3D_DYNAMIC_STATE_PRIMITIVE_RESTART;

    /* Build dynamic state create info */
    for (i = 0, count = 0; i < ARRAY_SIZE(vkd3d_dynamic_state_list); i++)
    {
        if (dynamic_state_flags & vkd3d_dynamic_state_list[i].flag)
            dynamic_state_buffer[count++] = vkd3d_dynamic_state_list[i].vk_state;
    }

    dynamic_desc->sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic_desc->pNext = NULL;
    dynamic_desc->flags = 0;
    dynamic_desc->dynamicStateCount = count;
    dynamic_desc->pDynamicStates = dynamic_state_buffer;

    return dynamic_state_flags;
}

static HRESULT d3d12_pipeline_state_validate_blend_state(struct d3d12_pipeline_state *state,
        const struct d3d12_device *device,
        const struct d3d12_pipeline_state_desc *desc, const struct vkd3d_shader_signature *sig)
{
    const struct vkd3d_format *format;
    unsigned int i, j, register_index;

    if (is_dual_source_blending(&desc->blend_state.RenderTarget[0]))
    {
        /* If we enable dual source blending, we must fail an RT index > 0 which has
         * an IO-sig entry with non-NULL format. */
        for (i = 0; i < sig->element_count; i++)
        {
            register_index = sig->elements[i].register_index;
            if (register_index >= ARRAY_SIZE(desc->rtv_formats.RTFormats))
            {
                WARN("Register index %u out of bounds.\n", register_index);
                return E_INVALIDARG;
            }

            if (register_index > 0)
            {
                if (desc->rtv_formats.RTFormats[register_index] != DXGI_FORMAT_UNKNOWN)
                {
                    WARN("Cannot enable dual-source blending for active RT %u.\n", register_index);
                    return E_INVALIDARG;
                }

                if (desc->blend_state.IndependentBlendEnable && desc->blend_state.RenderTarget[i].BlendEnable)
                {
                    WARN("Blend enable cannot be set for render target %u when dual source blending is used.\n", i);
                    return E_INVALIDARG;
                }
            }
        }
    }

    for (i = 0; i < desc->rtv_formats.NumRenderTargets; i++)
    {
        if (!state->graphics.blend_attachments[i].blendEnable)
            continue;

        format = vkd3d_get_format(device, desc->rtv_formats.RTFormats[i], false);
        /* Blending on integer formats are not supported, and we are supposed to validate this
         * when creating pipelines. However, if the shader does not declare the render target
         * in the output signature, we're supposed to just ignore it. We can just force blending to false
         * in this case. If there is an output, fail pipeline compilation. */
        if (format && (format->type == VKD3D_FORMAT_TYPE_SINT || format->type == VKD3D_FORMAT_TYPE_UINT))
        {
            for (j = 0; j < sig->element_count; j++)
            {
                if (sig->elements[j].register_index == i)
                {
                    ERR("Enabling blending on RT %u with format %s, but using integer format is not supported.\n", i,
                            debug_dxgi_format(desc->rtv_formats.RTFormats[i]));
                    return E_INVALIDARG;
                }
            }

            /* The output does not exist, but we have to pass the pipeline. Just nop out any invalid blend state. */
            WARN("Enabling blending on RT %u with format %s, but using integer format is not supported. "
                 "The output is not written, so nop-ing out blending.\n",
                    i, debug_dxgi_format(desc->rtv_formats.RTFormats[i]));
            state->graphics.blend_attachments[i].blendEnable = VK_FALSE;
        }
    }

    return S_OK;
}

static HRESULT d3d12_pipeline_state_init_graphics(struct d3d12_pipeline_state *state,
        struct d3d12_device *device, const struct d3d12_pipeline_state_desc *desc,
        const struct d3d12_cached_pipeline_state *cached_pso)
{
    const VkPhysicalDeviceFeatures *features = &device->device_info.features2.features;
    struct d3d12_graphics_pipeline_state *graphics = &state->graphics;
    const D3D12_STREAM_OUTPUT_DESC *so_desc = &desc->stream_output;
    VkVertexInputBindingDivisorDescriptionEXT *binding_divisor;
    const struct vkd3d_vulkan_info *vk_info = &device->vk_info;
    uint32_t instance_divisors[D3D12_VS_INPUT_REGISTER_COUNT];
    uint32_t aligned_offsets[D3D12_VS_INPUT_REGISTER_COUNT];
    bool have_attachment, can_compile_pipeline_early;
    struct vkd3d_shader_signature output_signature;
    struct vkd3d_shader_signature input_signature;
    VkSampleCountFlagBits sample_count;
    const struct vkd3d_format *format;
    unsigned int instance_divisor;
    VkVertexInputRate input_rate;
    unsigned int i, j;
    size_t rt_count;
    uint32_t mask;
    HRESULT hr;
    int ret;

    static const struct
    {
        enum VkShaderStageFlagBits stage;
        ptrdiff_t offset;
    }
    shader_stages_lut[] =
    {
        {VK_SHADER_STAGE_VERTEX_BIT,                  offsetof(struct d3d12_pipeline_state_desc, vs)},
        {VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT,    offsetof(struct d3d12_pipeline_state_desc, hs)},
        {VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT, offsetof(struct d3d12_pipeline_state_desc, ds)},
        {VK_SHADER_STAGE_GEOMETRY_BIT,                offsetof(struct d3d12_pipeline_state_desc, gs)},
        {VK_SHADER_STAGE_TASK_BIT_EXT,                offsetof(struct d3d12_pipeline_state_desc, as)},
        {VK_SHADER_STAGE_MESH_BIT_EXT,                offsetof(struct d3d12_pipeline_state_desc, ms)},
        {VK_SHADER_STAGE_FRAGMENT_BIT,                offsetof(struct d3d12_pipeline_state_desc, ps)},
    };

    graphics->stage_flags = vkd3d_pipeline_state_desc_get_shader_stages(desc);
    graphics->stage_count = 0;
    graphics->primitive_topology_type = desc->primitive_topology_type;

    state->pipeline_type = (graphics->stage_flags & VK_SHADER_STAGE_MESH_BIT_EXT)
            ? VKD3D_PIPELINE_TYPE_MESH_GRAPHICS
            : VKD3D_PIPELINE_TYPE_GRAPHICS;

    memset(&input_signature, 0, sizeof(input_signature));
    memset(&output_signature, 0, sizeof(output_signature));

    for (i = desc->rtv_formats.NumRenderTargets; i < ARRAY_SIZE(desc->rtv_formats.RTFormats); ++i)
    {
        if (desc->rtv_formats.RTFormats[i] != DXGI_FORMAT_UNKNOWN)
        {
            WARN("Format must be set to DXGI_FORMAT_UNKNOWN for inactive render targets.\n");
            return E_INVALIDARG;
        }
    }

    sample_count = vk_samples_from_dxgi_sample_desc(&desc->sample_desc);
    if (desc->sample_desc.Count != 1 && desc->sample_desc.Quality)
        WARN("Ignoring sample quality %u.\n", desc->sample_desc.Quality);

    rt_count = desc->rtv_formats.NumRenderTargets;
    if (rt_count > ARRAY_SIZE(graphics->blend_attachments))
    {
        FIXME("NumRenderTargets %zu > %zu, ignoring extra formats.\n",
                rt_count, ARRAY_SIZE(graphics->blend_attachments));
        rt_count = ARRAY_SIZE(graphics->blend_attachments);
    }
    graphics->rt_count = rt_count;

    if (!(graphics->stage_flags & VK_SHADER_STAGE_FRAGMENT_BIT))
    {
        /* Avoids validation errors where app might bind bogus RTV format which does not match the PSO.
         * D3D12 validation does not complain about this when PS is NULL since RTVs are not accessed to begin with.
         * We can just pretend we have no render targets in this case, which is fine. */
        rt_count = 0;
    }

    graphics->null_attachment_mask = 0;
    graphics->rtv_active_mask = 0;
    for (i = 0; i < rt_count; ++i)
    {
        const D3D12_RENDER_TARGET_BLEND_DESC *rt_desc;

        if (desc->rtv_formats.RTFormats[i] == DXGI_FORMAT_UNKNOWN)
        {
            graphics->null_attachment_mask |= 1u << i;
            graphics->cached_desc.ps_output_swizzle[i] = VKD3D_NO_SWIZZLE;
            graphics->rtv_formats[i] = VK_FORMAT_UNDEFINED;
        }
        else if ((format = vkd3d_get_format(device, desc->rtv_formats.RTFormats[i], false)))
        {
            graphics->cached_desc.ps_output_swizzle[i] = vkd3d_get_rt_format_swizzle(format);
            graphics->rtv_formats[i] = format->vk_format;
            graphics->rtv_active_mask |= 1u << i;
        }
        else
        {
            WARN("Invalid RTV format %#x.\n", desc->rtv_formats.RTFormats[i]);
            hr = E_INVALIDARG;
            goto fail;
        }

        rt_desc = &desc->blend_state.RenderTarget[desc->blend_state.IndependentBlendEnable ? i : 0];
        if (desc->blend_state.IndependentBlendEnable && rt_desc->LogicOpEnable)
        {
            WARN("IndependentBlendEnable must be FALSE when logic operations are enabled.\n");
            hr = E_INVALIDARG;
            goto fail;
        }
        if (rt_desc->BlendEnable && rt_desc->LogicOpEnable)
        {
            WARN("Only one of BlendEnable or LogicOpEnable can be set to TRUE.");
            hr = E_INVALIDARG;
            goto fail;
        }

        blend_attachment_from_d3d12(&graphics->blend_attachments[i], rt_desc);

        if (graphics->null_attachment_mask & (1u << i))
            memset(&graphics->blend_attachments[i], 0, sizeof(graphics->blend_attachments[i]));
    }

    for (i = rt_count; i < ARRAY_SIZE(graphics->rtv_formats); ++i)
        graphics->rtv_formats[i] = VK_FORMAT_UNDEFINED;

    blend_desc_from_d3d12(&graphics->blend_desc, &desc->blend_state,
            graphics->rt_count, graphics->blend_attachments);

    if (graphics->blend_desc.logicOpEnable && !features->logicOp)
    {
        ERR("Logic op not supported by device.\n");
        hr = E_INVALIDARG;
        goto fail;
    }

    ds_desc_from_d3d12(&graphics->ds_desc, &desc->depth_stencil_state);
    if (graphics->ds_desc.depthBoundsTestEnable && !features->depthBounds)
    {
        ERR("Depth bounds test not supported by device.\n");
        hr = E_INVALIDARG;
        goto fail;
    }

    if (desc->dsv_format == DXGI_FORMAT_UNKNOWN
            && graphics->ds_desc.depthTestEnable && !graphics->ds_desc.depthWriteEnable
            && graphics->ds_desc.depthCompareOp == VK_COMPARE_OP_ALWAYS && !graphics->ds_desc.stencilTestEnable)
    {
        TRACE("Disabling depth test.\n");
        graphics->ds_desc.depthTestEnable = VK_FALSE;
    }

    graphics->dsv_format = NULL;
    format = vkd3d_get_format(device, desc->dsv_format, true);

    /* F1 2021 enables stencil test on a D16_UNORM.
     * Filter out any tests which are irrelevant for the DS format in question. */
    if (format)
    {
        if (!(format->vk_aspect_mask & VK_IMAGE_ASPECT_DEPTH_BIT))
        {
            WARN("Ignoring depthTestEnable due to lack of depth aspect.\n");
            graphics->ds_desc.depthTestEnable = VK_FALSE;
            graphics->ds_desc.depthBoundsTestEnable = VK_FALSE;
        }

        if (!(format->vk_aspect_mask & VK_IMAGE_ASPECT_STENCIL_BIT))
        {
            WARN("Ignoring stencilTestEnable due to lack of stencil aspect.\n");
            graphics->ds_desc.stencilTestEnable = VK_FALSE;
        }
    }

    if (graphics->ds_desc.depthTestEnable || graphics->ds_desc.stencilTestEnable || graphics->ds_desc.depthBoundsTestEnable)
    {
        if (desc->dsv_format == DXGI_FORMAT_UNKNOWN)
        {
            WARN("DSV format is DXGI_FORMAT_UNKNOWN.\n");
            graphics->null_attachment_mask |= dsv_attachment_mask(graphics);
        }
        else if (format)
        {
            if (format->vk_aspect_mask & (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT))
                graphics->dsv_format = format;
            else
                FIXME("Format %#x is not depth/stencil format.\n", format->dxgi_format);
        }
        else
        {
            WARN("Invalid DSV format %#x.\n", desc->dsv_format);
            hr = E_INVALIDARG;
            goto fail;
        }
    }

    graphics->cached_desc.ps_shader_parameters[0].name = VKD3D_SHADER_PARAMETER_NAME_RASTERIZER_SAMPLE_COUNT;
    graphics->cached_desc.ps_shader_parameters[0].type = VKD3D_SHADER_PARAMETER_TYPE_IMMEDIATE_CONSTANT;
    graphics->cached_desc.ps_shader_parameters[0].data_type = VKD3D_SHADER_PARAMETER_DATA_TYPE_UINT32;
    graphics->cached_desc.ps_shader_parameters[0].immediate_constant.u32 = sample_count;
    graphics->cached_desc.is_dual_source_blending = is_dual_source_blending(&desc->blend_state.RenderTarget[0]);

    if (graphics->cached_desc.is_dual_source_blending)
    {
        /* If we're using dual source blending, we can only safely write to MRT 0.
         * Be defensive about programs which do not do this for us. */
        memset(graphics->blend_attachments + 1, 0,
                sizeof(graphics->blend_attachments[0]) * (ARRAY_SIZE(graphics->blend_attachments) - 1));

        /* Only allow RT 0 to be active for dual source blending. */
        graphics->rtv_active_mask &= 1u << 0;
    }

    graphics->xfb_enabled = false;
    if (so_desc->NumEntries)
    {
        if (!(state->root_signature->d3d12_flags & D3D12_ROOT_SIGNATURE_FLAG_ALLOW_STREAM_OUTPUT))
        {
            WARN("Stream output is used without D3D12_ROOT_SIGNATURE_FLAG_ALLOW_STREAM_OUTPUT.\n");
            hr = E_INVALIDARG;
            goto fail;
        }

        if (!vk_info->EXT_transform_feedback)
        {
            FIXME("Transform feedback is not supported by Vulkan implementation.\n");
            hr = E_NOTIMPL;
            goto fail;
        }

        graphics->xfb_enabled = true;
        graphics->cached_desc.xfb_info = vkd3d_shader_transform_feedback_info_dup(so_desc);

        if (!graphics->cached_desc.xfb_info)
        {
            hr = E_OUTOFMEMORY;
            goto fail;
        }

        if (graphics->stage_flags & VK_SHADER_STAGE_GEOMETRY_BIT)
            graphics->cached_desc.xfb_stage = VK_SHADER_STAGE_GEOMETRY_BIT;
        else if (graphics->stage_flags & VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT)
            graphics->cached_desc.xfb_stage = VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;
        else
            graphics->cached_desc.xfb_stage = VK_SHADER_STAGE_VERTEX_BIT;
    }

    graphics->patch_vertex_count = 0;

    /* Parse interface data from DXBC blobs. */
    for (i = 0; i < ARRAY_SIZE(shader_stages_lut); ++i)
    {
        const D3D12_SHADER_BYTECODE *b = (const void *)((uintptr_t)desc + shader_stages_lut[i].offset);
        const struct vkd3d_shader_code dxbc = {b->pShaderBytecode, b->BytecodeLength};

        if (!(graphics->stage_flags & shader_stages_lut[i].stage))
            continue;

        switch (shader_stages_lut[i].stage)
        {
            case VK_SHADER_STAGE_VERTEX_BIT:
                if ((ret = vkd3d_shader_parse_input_signature(&dxbc, &input_signature)) < 0)
                {
                    hr = hresult_from_vkd3d_result(ret);
                    goto fail;
                }
                break;

            case VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT:
            case VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT:
                if (desc->primitive_topology_type != D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH)
                {
                    WARN("D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH must be used with tessellation shaders.\n");
                    hr = E_INVALIDARG;
                    goto fail;
                }
                break;

            case VK_SHADER_STAGE_GEOMETRY_BIT:
            case VK_SHADER_STAGE_TASK_BIT_EXT:
            case VK_SHADER_STAGE_MESH_BIT_EXT:
                break;

            case VK_SHADER_STAGE_FRAGMENT_BIT:
                if ((ret = vkd3d_shader_parse_output_signature(&dxbc, &output_signature)) < 0)
                {
                    hr = hresult_from_vkd3d_result(ret);
                    goto fail;
                }

                if (FAILED(hr = d3d12_pipeline_state_validate_blend_state(state, device, desc, &output_signature)))
                    goto fail;
                break;

            default:
                hr = E_INVALIDARG;
                goto fail;
        }

        /* Not owned yet. If we return from pipeline creation without having concrete SPIR-V,
         * we'll have to dupe the bytecode and potentially compile to SPIR-V late. */
        graphics->cached_desc.bytecode[graphics->stage_count] = *b;
        graphics->cached_desc.bytecode_stages[graphics->stage_count] = shader_stages_lut[i].stage;

        ++graphics->stage_count;
    }

    /* We only accept SPIR-V from cache if we can successfully load all shaders.
     * We cannot partially fall back since we cannot handle any situation where we need inter-stage code-gen fixups.
     * In this situation, just generate full SPIR-V from scratch.
     * This really shouldn't happen unless we have corrupt cache entries. */
    for (i = 0; i < graphics->stage_count; i++)
    {
        if (FAILED(vkd3d_load_spirv_from_cached_state(device, cached_pso,
                graphics->cached_desc.bytecode_stages[i], &graphics->code[i])))
        {
            for (j = 0; j < i; j++)
            {
                if (vkd3d_config_flags & VKD3D_CONFIG_FLAG_PIPELINE_LIBRARY_LOG)
                    INFO("Discarding cached SPIR-V for stage #%x.\n", graphics->cached_desc.bytecode_stages[j]);
                vkd3d_shader_free_shader_code(&graphics->code[j]);
                memset(&graphics->code[j], 0, sizeof(graphics->code[j]));
            }
            break;
        }
    }

    /* Now create the actual shader modules. If we managed to load SPIR-V from cache, use that directly.
     * Make sure we don't reset graphics->stage_count since that is a potential memory leak if
     * we fail to create shader module for whatever reason. */
    for (i = 0; i < graphics->stage_count; i++)
    {
        if (FAILED(hr = vkd3d_create_shader_stage(state, device,
                &graphics->stages[i],
                graphics->cached_desc.bytecode_stages[i], NULL,
                &graphics->cached_desc.bytecode[i], &graphics->code[i])))
            goto fail;

        if (graphics->cached_desc.bytecode_stages[i] == VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT)
        {
            graphics->patch_vertex_count = graphics->code[i].meta.patch_vertex_count;
        }
        else if (graphics->cached_desc.bytecode_stages[i] == VK_SHADER_STAGE_FRAGMENT_BIT)
        {
            /* We have consumed the MS/PS map at this point. */
            vkd3d_shader_stage_io_map_free(&state->graphics.cached_desc.stage_io_map_ms_ps);
        }

        if ((graphics->code[i].meta.flags & VKD3D_SHADER_META_FLAG_REPLACED) &&
                device->debug_ring.active)
        {
            vkd3d_shader_debug_ring_init_spec_constant(device,
                    &graphics->spec_info[i],
                    graphics->code[i].meta.hash);
            graphics->stages[i].pSpecializationInfo = &graphics->spec_info[i].spec_info;
        }
    }

    graphics->attribute_count = (graphics->stage_flags & VK_PIPELINE_STAGE_MESH_SHADER_BIT_EXT)
            ? 0 : desc->input_layout.NumElements;
    if (graphics->attribute_count > ARRAY_SIZE(graphics->attributes))
    {
        FIXME("InputLayout.NumElements %zu > %zu, ignoring extra elements.\n",
                graphics->attribute_count, ARRAY_SIZE(graphics->attributes));
        graphics->attribute_count = ARRAY_SIZE(graphics->attributes);
    }

    if (graphics->attribute_count
            && !(state->root_signature->d3d12_flags & D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT))
    {
        WARN("Input layout is used without D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT.\n");
        hr = E_INVALIDARG;
        goto fail;
    }

    if (FAILED(hr = compute_input_layout_offsets(device, &desc->input_layout, aligned_offsets)))
        goto fail;

    graphics->instance_divisor_count = 0;
    graphics->attribute_binding_count = 0;
    memset(graphics->minimum_vertex_buffer_dynamic_stride, 0, sizeof(graphics->minimum_vertex_buffer_dynamic_stride));
    memset(graphics->vertex_buffer_stride_align_mask, 0, sizeof(graphics->vertex_buffer_stride_align_mask));

    for (i = 0, j = 0, mask = 0; i < graphics->attribute_count; ++i)
    {
        const D3D12_INPUT_ELEMENT_DESC *e = &desc->input_layout.pInputElementDescs[i];
        const struct vkd3d_shader_signature_element *signature_element;

        if (!(format = vkd3d_get_format(device, e->Format, false)))
        {
            WARN("Invalid input element format %#x.\n", e->Format);
            hr = E_INVALIDARG;
            goto fail;
        }

        if (e->InputSlot >= ARRAY_SIZE(graphics->input_rates)
                || e->InputSlot >= ARRAY_SIZE(instance_divisors))
        {
            WARN("Invalid input slot %#x.\n", e->InputSlot);
            hr = E_INVALIDARG;
            goto fail;
        }

        if (!(signature_element = vkd3d_shader_find_signature_element(&input_signature,
                e->SemanticName, e->SemanticIndex, 0)))
        {
            WARN("Unused input element %u.\n", i);
            continue;
        }

        graphics->attributes[j].location = signature_element->register_index;
        graphics->attributes[j].binding = e->InputSlot;
        graphics->attributes[j].format = format->vk_format;
        if (e->AlignedByteOffset != D3D12_APPEND_ALIGNED_ELEMENT)
            graphics->attributes[j].offset = e->AlignedByteOffset;
        else
            graphics->attributes[j].offset = aligned_offsets[i];

        graphics->minimum_vertex_buffer_dynamic_stride[e->InputSlot] =
                max(graphics->minimum_vertex_buffer_dynamic_stride[e->InputSlot],
                    graphics->attributes[j].offset + format->byte_count);

        graphics->vertex_buffer_stride_align_mask[e->InputSlot] =
                max(graphics->vertex_buffer_stride_align_mask[e->InputSlot], format->byte_count);

        ++j;

        switch (e->InputSlotClass)
        {
            case D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA:
                input_rate = VK_VERTEX_INPUT_RATE_VERTEX;
                instance_divisor = 1;
                break;

            case D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA:
                input_rate = VK_VERTEX_INPUT_RATE_INSTANCE;
                instance_divisor = e->InstanceDataStepRate;
                if (instance_divisor > vk_info->max_vertex_attrib_divisor
                        || (!instance_divisor && !vk_info->vertex_attrib_zero_divisor))
                {
                    FIXME("Instance divisor %u not supported by Vulkan implementation.\n", instance_divisor);
                    instance_divisor = 1;
                }
                break;

            default:
                FIXME("Unhandled input slot class %#x on input element %u.\n", e->InputSlotClass, i);
                hr = E_INVALIDARG;
                goto fail;
        }

        if (mask & (1u << e->InputSlot) && (graphics->input_rates[e->InputSlot] != input_rate
                || instance_divisors[e->InputSlot] != instance_divisor))
        {
            FIXME("Input slot rate %#x, instance divisor %u on input element %u conflicts "
                    "with earlier input slot rate %#x, instance divisor %u.\n",
                    input_rate, instance_divisor, e->InputSlot,
                    graphics->input_rates[e->InputSlot], instance_divisors[e->InputSlot]);
            hr = E_INVALIDARG;
            goto fail;
        }

        graphics->input_rates[e->InputSlot] = input_rate;
        instance_divisors[e->InputSlot] = instance_divisor;
        if (instance_divisor != 1 && !(mask & (1u << e->InputSlot)))
        {
            binding_divisor = &graphics->instance_divisors[graphics->instance_divisor_count++];
            binding_divisor->binding = e->InputSlot;
            binding_divisor->divisor = instance_divisor;
        }

        if (!(mask & (1u << e->InputSlot)))
        {
            VkVertexInputBindingDescription *binding = &graphics->attribute_bindings[graphics->attribute_binding_count++];
            binding->binding = e->InputSlot;
            binding->inputRate = input_rate;
            binding->stride = 0; /* To be filled in later. */
        }
        mask |= 1u << e->InputSlot;
    }
    graphics->attribute_count = j;
    graphics->vertex_buffer_mask = mask;
    vkd3d_shader_free_shader_signature(&input_signature);
    vkd3d_shader_free_shader_signature(&output_signature);

    for (i = 0; i < D3D12_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT; i++)
    {
        if (mask & (1u << i))
        {
            /* In D3D12, VBO strides must be aligned to min(4, max-for-all(format->byte_size)) for a given input slot.
             * Native drivers and Windows Vulkan drivers happen to be robust against buggy applications
             * which use unaligned formats, but Vulkan drivers are not always robust here (they don't have to be).
             * AMD Windows D3D12/Vulkan drivers return the wrong result for unaligned VBO strides,
             * but it doesn't crash at least!
             * RDNA will hang if we emit tbuffer_loads which don't conform to D3D12 rules. */

            /* Essentially all VBO types in D3D12 are POT sized, so this works.
             * The exception to this is RGB32, which will clamp to 4 anyways.
             * Two potential formats that could mess this analysis up is RGB8 and RGB16,
             * but neither of these formats exist. */
            graphics->vertex_buffer_stride_align_mask[i] = min(graphics->vertex_buffer_stride_align_mask[i], 4u);
            /* POT - 1 for an alignment mask. */
            graphics->vertex_buffer_stride_align_mask[i] -= 1;
        }
    }

    switch (desc->strip_cut_value)
    {
        case D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_DISABLED:
        case D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_0xFFFF:
        case D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_0xFFFFFFFF:
            graphics->index_buffer_strip_cut_value = desc->strip_cut_value;
            break;
        default:
            WARN("Invalid index buffer strip cut value %#x.\n", desc->strip_cut_value);
            hr = E_INVALIDARG;
            goto fail;
    }

    rs_desc_from_d3d12(&graphics->rs_desc, &desc->rasterizer_state);
    have_attachment = graphics->rt_count || graphics->dsv_format ||
            d3d12_graphics_pipeline_state_has_unknown_dsv_format_with_test(graphics);
    if ((!have_attachment && !(graphics->stage_flags & VK_SHADER_STAGE_FRAGMENT_BIT))
            || (graphics->xfb_enabled && so_desc->RasterizedStream == D3D12_SO_NO_RASTERIZED_STREAM))
        graphics->rs_desc.rasterizerDiscardEnable = VK_TRUE;

    rs_stream_info_from_d3d12(&graphics->rs_stream_info, &graphics->rs_desc, so_desc, vk_info);
    if (vk_info->EXT_conservative_rasterization)
        rs_conservative_info_from_d3d12(&graphics->rs_conservative_info, &graphics->rs_desc, &desc->rasterizer_state);
    if (vk_info->EXT_depth_clip_enable)
        rs_depth_clip_info_from_d3d12(&graphics->rs_depth_clip_info, &graphics->rs_desc, &desc->rasterizer_state);

    graphics->ms_desc.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    graphics->ms_desc.pNext = NULL;
    graphics->ms_desc.flags = 0;
    graphics->ms_desc.rasterizationSamples = sample_count;
    graphics->ms_desc.sampleShadingEnable = VK_FALSE;
    graphics->ms_desc.minSampleShading = 0.0f;
    graphics->ms_desc.pSampleMask = NULL;
    if (desc->sample_mask != ~0u)
    {
        assert(DIV_ROUND_UP(sample_count, 32) <= ARRAY_SIZE(graphics->sample_mask));
        graphics->sample_mask[0] = desc->sample_mask;
        graphics->sample_mask[1] = 0xffffffffu;
        graphics->ms_desc.pSampleMask = graphics->sample_mask;
    }
    graphics->ms_desc.alphaToCoverageEnable = desc->blend_state.AlphaToCoverageEnable;
    graphics->ms_desc.alphaToOneEnable = VK_FALSE;

    if (desc->view_instancing_desc.ViewInstanceCount)
    {
        ERR("View instancing not supported.\n");
        hr = E_INVALIDARG;
        goto fail;
    }

    if (graphics->stage_flags & VK_SHADER_STAGE_MESH_BIT_EXT)
    {
        can_compile_pipeline_early = true;
        graphics->pipeline_layout = state->root_signature->mesh.vk_pipeline_layout;
    }
    else
    {
        /* If we don't know vertex count for tessellation shaders, we need to defer compilation, but this should
         * be exceedingly rare. */
        can_compile_pipeline_early =
                (desc->primitive_topology_type != D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH || graphics->patch_vertex_count != 0) &&
                desc->primitive_topology_type != D3D12_PRIMITIVE_TOPOLOGY_TYPE_UNDEFINED;
        graphics->pipeline_layout = state->root_signature->graphics.vk_pipeline_layout;
    }

    graphics->pipeline = VK_NULL_HANDLE;
    state->device = device;

    if (can_compile_pipeline_early)
    {
        if (!(graphics->pipeline = d3d12_pipeline_state_create_pipeline_variant(state, NULL, graphics->dsv_format,
                state->vk_pso_cache, &graphics->dynamic_state_flags)))
            goto fail;
    }
    else
    {
        graphics->dsv_plane_optimal_mask = d3d12_graphics_pipeline_state_get_plane_optimal_mask(graphics, NULL);
    }

    list_init(&graphics->compiled_fallback_pipelines);

    if (FAILED(hr = vkd3d_private_store_init(&state->private_store)))
        goto fail;

    d3d12_device_add_ref(state->device);
    return S_OK;

fail:
    vkd3d_shader_free_shader_signature(&input_signature);
    vkd3d_shader_free_shader_signature(&output_signature);
    return hr;
}

bool d3d12_pipeline_state_has_replaced_shaders(struct d3d12_pipeline_state *state)
{
    unsigned int i;
    if (state->pipeline_type == VKD3D_PIPELINE_TYPE_COMPUTE)
        return !!(state->compute.code.meta.flags & VKD3D_SHADER_META_FLAG_REPLACED);
    else if (state->pipeline_type == VKD3D_PIPELINE_TYPE_GRAPHICS ||
            state->pipeline_type == VKD3D_PIPELINE_TYPE_MESH_GRAPHICS)
    {
        for (i = 0; i < state->graphics.stage_count; i++)
            if (state->graphics.code[i].meta.flags & VKD3D_SHADER_META_FLAG_REPLACED)
                return true;
        return false;
    }
    else
        return false;
}

static HRESULT d3d12_pipeline_create_private_root_signature(struct d3d12_device *device,
        VkPipelineBindPoint bind_point, const struct d3d12_pipeline_state_desc *desc,
        struct d3d12_root_signature **root_signature)
{
    const struct D3D12_SHADER_BYTECODE *bytecode = bind_point == VK_PIPELINE_BIND_POINT_GRAPHICS ? &desc->vs : &desc->cs;
    ID3D12RootSignature *object = NULL;
    HRESULT hr;

    if (!bytecode->BytecodeLength)
        return E_INVALIDARG;

    if (FAILED(hr = ID3D12Device_CreateRootSignature(&device->ID3D12Device_iface, 0,
            bytecode->pShaderBytecode, bytecode->BytecodeLength,
            &IID_ID3D12RootSignature, (void**)&object)))
        return hr;

    *root_signature = impl_from_ID3D12RootSignature(object);
    d3d12_root_signature_inc_ref(*root_signature);
    ID3D12RootSignature_Release(object);
    return hr;
}

HRESULT d3d12_pipeline_state_create(struct d3d12_device *device, VkPipelineBindPoint bind_point,
        const struct d3d12_pipeline_state_desc *desc, struct d3d12_pipeline_state **state)
{
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    const struct d3d12_cached_pipeline_state *desc_cached_pso;
    struct d3d12_cached_pipeline_state cached_pso;
    struct d3d12_pipeline_state *object;
    HRESULT hr;

    if (!(object = vkd3d_malloc(sizeof(*object))))
        return E_OUTOFMEMORY;

    memset(object, 0, sizeof(*object));

    if (!desc->root_signature)
    {
        if (FAILED(hr = d3d12_pipeline_create_private_root_signature(device,
                bind_point, desc, &object->root_signature)))
        {
            ERR("No root signature for pipeline.\n");
            vkd3d_free(object);
            return hr;
        }
        object->root_signature_compat_hash_is_dxbc_derived = true;
    }
    else
    {
        object->root_signature = impl_from_ID3D12RootSignature(desc->root_signature);
        /* Hold a private reference on this root signature in case we have to create fallback PSOs. */
        d3d12_root_signature_inc_ref(object->root_signature);
    }

    vkd3d_pipeline_cache_compat_from_state_desc(&object->pipeline_cache_compat, desc);
    if (object->root_signature)
        object->pipeline_cache_compat.root_signature_compat_hash = object->root_signature->compatibility_hash;

    desc_cached_pso = &desc->cached_pso;

    if (desc->cached_pso.blob.CachedBlobSizeInBytes)
    {
        if (FAILED(hr = d3d12_cached_pipeline_state_validate(device, &desc->cached_pso,
                &object->pipeline_cache_compat)))
        {
            if (object->root_signature)
                d3d12_root_signature_dec_ref(object->root_signature);
            vkd3d_free(object);
            return hr;
        }
    }

    /* If we rely on internal shader cache, the PSO blob app provides us might be a pure metadata blob,
     * and therefore kinda useless. Try to use disk cache blob instead.
     * Also, consider that we might have to serialize this pipeline if we don't find anything in disk cache. */
    if (d3d12_cached_pipeline_state_is_dummy(&desc->cached_pso))
    {
        memset(&cached_pso, 0, sizeof(cached_pso));
        desc_cached_pso = &cached_pso;
    }

    if (desc_cached_pso->blob.CachedBlobSizeInBytes == 0 && device->disk_cache.library)
    {
        if (SUCCEEDED(vkd3d_pipeline_library_find_cached_blob_from_disk_cache(&device->disk_cache,
                &object->pipeline_cache_compat, &cached_pso)))
        {
            /* Validation is redundant. We only accept disk cache entries if checksum of disk blob passes.
             * The key is also entirely based on the PSO desc itself. */
            if ((vkd3d_config_flags & VKD3D_CONFIG_FLAG_PIPELINE_LIBRARY_LOG) &&
                    desc->cached_pso.blob.CachedBlobSizeInBytes)
            {
                INFO("Application provided cached PSO blob, but we opted for disk cache blob instead.\n");
            }
            desc_cached_pso = &cached_pso;
        }
    }

    object->ID3D12PipelineState_iface.lpVtbl = &d3d12_pipeline_state_vtbl;
    object->refcount = 1;
    object->internal_refcount = 1;

    hr = S_OK;

    if (!(vkd3d_config_flags & VKD3D_CONFIG_FLAG_GLOBAL_PIPELINE_CACHE))
        if (FAILED(hr = vkd3d_create_pipeline_cache_from_d3d12_desc(device, desc_cached_pso, &object->vk_pso_cache)))
            ERR("Failed to create pipeline cache, hr %d.\n", hr);

    if (SUCCEEDED(hr))
    {
        switch (bind_point)
        {
            case VK_PIPELINE_BIND_POINT_COMPUTE:
                hr = d3d12_pipeline_state_init_compute(object, device, desc, desc_cached_pso);
                break;

            case VK_PIPELINE_BIND_POINT_GRAPHICS:
                hr = d3d12_pipeline_state_init_graphics(object, device, desc, desc_cached_pso);
                break;

            default:
                ERR("Invalid pipeline type %u.", bind_point);
                hr = E_INVALIDARG;
        }
    }

    if (FAILED(hr))
    {
        if (object->root_signature)
            d3d12_root_signature_dec_ref(object->root_signature);
        d3d12_pipeline_state_free_spirv_code(object);
        d3d12_pipeline_state_destroy_shader_modules(object, device);
        if (object->pipeline_type == VKD3D_PIPELINE_TYPE_GRAPHICS || object->pipeline_type == VKD3D_PIPELINE_TYPE_MESH_GRAPHICS)
            d3d12_pipeline_state_free_cached_desc(&object->graphics.cached_desc);
        VK_CALL(vkDestroyPipelineCache(device->vk_device, object->vk_pso_cache, NULL));

        vkd3d_free(object);
        return hr;
    }

    /* The strategy here is that we need to keep the SPIR-V alive somehow.
     * If we don't need to serialize SPIR-V from the PSO, then we don't need to keep the code alive as pointer/size pairs.
     * The scenarios for this case is when we choose to not serialize SPIR-V at all with VKD3D_CONFIG,
     * or the PSO was loaded from a cached blob. It's extremely unlikely that anyone is going to try
     * serializing that PSO again, so there should be no need to keep it alive.
     * The worst that would happen is a performance loss should that entry be reloaded later.
     * For graphics pipelines, we have to keep VkShaderModules around in case we need fallback pipelines.
     * If we keep the SPIR-V around in memory, we can always create shader modules on-demand in case we
     * need to actually create fallback pipelines. This avoids unnecessary memory bloat. */
    if (desc_cached_pso->blob.CachedBlobSizeInBytes ||
            (vkd3d_config_flags & VKD3D_CONFIG_FLAG_PIPELINE_LIBRARY_NO_SERIALIZE_SPIRV))
        d3d12_pipeline_state_free_spirv_code(object);
    else
        d3d12_pipeline_state_destroy_shader_modules(object, device);

    /* We don't expect to serialize the PSO blob if we loaded it from cache.
     * Free the cache now to save on memory. */
    if (desc_cached_pso->blob.CachedBlobSizeInBytes)
    {
        VK_CALL(vkDestroyPipelineCache(device->vk_device, object->vk_pso_cache, NULL));
        object->vk_pso_cache = VK_NULL_HANDLE;
    }
    else if (device->disk_cache.library)
    {
        /* We compiled this PSO without any cache (internal or app-provided),
         * so we should serialize this to internal disk cache.
         * Pushes work to disk$ thread. */
        vkd3d_pipeline_library_store_pipeline_to_disk_cache(&device->disk_cache, object);
    }

    TRACE("Created pipeline state %p.\n", object);

    *state = object;
    return S_OK;
}

static bool vkd3d_topology_type_can_restart(D3D12_PRIMITIVE_TOPOLOGY_TYPE type)
{
    return type == D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE ||
           type == D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
}

static bool vkd3d_topology_can_restart(VkPrimitiveTopology topology)
{
    switch (topology)
    {
    case VK_PRIMITIVE_TOPOLOGY_POINT_LIST:
    case VK_PRIMITIVE_TOPOLOGY_LINE_LIST:
    case VK_PRIMITIVE_TOPOLOGY_PATCH_LIST:
    case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST:
    case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST_WITH_ADJACENCY:
    case VK_PRIMITIVE_TOPOLOGY_LINE_LIST_WITH_ADJACENCY:
        return false;

    default:
        return true;
    }
}

static enum VkPrimitiveTopology vk_topology_from_d3d12_topology_type(D3D12_PRIMITIVE_TOPOLOGY_TYPE type, bool restart)
{
    /* Technically shouldn't need to know restart state here, but there is a VU banning use of primitiveRestartEnable
     * with list types. Using a strip type is harmless and is likely to dodge driver bugs. */
    switch (type)
    {
        case D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE:
            return restart ? VK_PRIMITIVE_TOPOLOGY_LINE_STRIP : VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
        case D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE:
            return restart ? VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP : VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        case D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT:
            return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
        case D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH:
            return VK_PRIMITIVE_TOPOLOGY_PATCH_LIST;
        default:
            ERR("Invalid primitive topology type #%x.\n", (unsigned)type);
            return VK_PRIMITIVE_TOPOLOGY_MAX_ENUM;
    }
}

enum VkPrimitiveTopology vk_topology_from_d3d12_topology(D3D12_PRIMITIVE_TOPOLOGY topology)
{
    switch (topology)
    {
        case D3D_PRIMITIVE_TOPOLOGY_POINTLIST:
            return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
        case D3D_PRIMITIVE_TOPOLOGY_LINELIST:
            return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
        case D3D_PRIMITIVE_TOPOLOGY_LINESTRIP:
            return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
        case D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST:
            return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        case D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP:
            return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
        case D3D_PRIMITIVE_TOPOLOGY_1_CONTROL_POINT_PATCHLIST:
        case D3D_PRIMITIVE_TOPOLOGY_2_CONTROL_POINT_PATCHLIST:
        case D3D_PRIMITIVE_TOPOLOGY_3_CONTROL_POINT_PATCHLIST:
        case D3D_PRIMITIVE_TOPOLOGY_4_CONTROL_POINT_PATCHLIST:
        case D3D_PRIMITIVE_TOPOLOGY_5_CONTROL_POINT_PATCHLIST:
        case D3D_PRIMITIVE_TOPOLOGY_6_CONTROL_POINT_PATCHLIST:
        case D3D_PRIMITIVE_TOPOLOGY_7_CONTROL_POINT_PATCHLIST:
        case D3D_PRIMITIVE_TOPOLOGY_8_CONTROL_POINT_PATCHLIST:
        case D3D_PRIMITIVE_TOPOLOGY_9_CONTROL_POINT_PATCHLIST:
        case D3D_PRIMITIVE_TOPOLOGY_10_CONTROL_POINT_PATCHLIST:
        case D3D_PRIMITIVE_TOPOLOGY_11_CONTROL_POINT_PATCHLIST:
        case D3D_PRIMITIVE_TOPOLOGY_12_CONTROL_POINT_PATCHLIST:
        case D3D_PRIMITIVE_TOPOLOGY_13_CONTROL_POINT_PATCHLIST:
        case D3D_PRIMITIVE_TOPOLOGY_14_CONTROL_POINT_PATCHLIST:
        case D3D_PRIMITIVE_TOPOLOGY_15_CONTROL_POINT_PATCHLIST:
        case D3D_PRIMITIVE_TOPOLOGY_16_CONTROL_POINT_PATCHLIST:
        case D3D_PRIMITIVE_TOPOLOGY_17_CONTROL_POINT_PATCHLIST:
        case D3D_PRIMITIVE_TOPOLOGY_18_CONTROL_POINT_PATCHLIST:
        case D3D_PRIMITIVE_TOPOLOGY_19_CONTROL_POINT_PATCHLIST:
        case D3D_PRIMITIVE_TOPOLOGY_20_CONTROL_POINT_PATCHLIST:
        case D3D_PRIMITIVE_TOPOLOGY_21_CONTROL_POINT_PATCHLIST:
        case D3D_PRIMITIVE_TOPOLOGY_22_CONTROL_POINT_PATCHLIST:
        case D3D_PRIMITIVE_TOPOLOGY_23_CONTROL_POINT_PATCHLIST:
        case D3D_PRIMITIVE_TOPOLOGY_24_CONTROL_POINT_PATCHLIST:
        case D3D_PRIMITIVE_TOPOLOGY_25_CONTROL_POINT_PATCHLIST:
        case D3D_PRIMITIVE_TOPOLOGY_26_CONTROL_POINT_PATCHLIST:
        case D3D_PRIMITIVE_TOPOLOGY_27_CONTROL_POINT_PATCHLIST:
        case D3D_PRIMITIVE_TOPOLOGY_28_CONTROL_POINT_PATCHLIST:
        case D3D_PRIMITIVE_TOPOLOGY_29_CONTROL_POINT_PATCHLIST:
        case D3D_PRIMITIVE_TOPOLOGY_30_CONTROL_POINT_PATCHLIST:
        case D3D_PRIMITIVE_TOPOLOGY_31_CONTROL_POINT_PATCHLIST:
        case D3D_PRIMITIVE_TOPOLOGY_32_CONTROL_POINT_PATCHLIST:
            return VK_PRIMITIVE_TOPOLOGY_PATCH_LIST;
        default:
            FIXME("Unhandled primitive topology %#x.\n", topology);
            return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
    }
}

static VkPipeline d3d12_pipeline_state_find_compiled_pipeline(struct d3d12_pipeline_state *state,
        const struct vkd3d_pipeline_key *key, uint32_t *dynamic_state_flags)
{
    const struct d3d12_graphics_pipeline_state *graphics = &state->graphics;
    struct vkd3d_compiled_pipeline *current;
    VkPipeline vk_pipeline = VK_NULL_HANDLE;

    rw_spinlock_acquire_read(&state->lock);
    LIST_FOR_EACH_ENTRY(current, &graphics->compiled_fallback_pipelines, struct vkd3d_compiled_pipeline, entry)
    {
        if (!memcmp(&current->key, key, sizeof(*key)))
        {
            vk_pipeline = current->vk_pipeline;
            *dynamic_state_flags = current->dynamic_state_flags;
            break;
        }
    }
    rw_spinlock_release_read(&state->lock);

    return vk_pipeline;
}

static bool d3d12_pipeline_state_put_pipeline_to_cache(struct d3d12_pipeline_state *state,
        const struct vkd3d_pipeline_key *key, VkPipeline vk_pipeline, uint32_t dynamic_state_flags)
{
    struct d3d12_graphics_pipeline_state *graphics = &state->graphics;
    struct vkd3d_compiled_pipeline *compiled_pipeline, *current;

    if (!(compiled_pipeline = vkd3d_malloc(sizeof(*compiled_pipeline))))
        return false;

    compiled_pipeline->key = *key;
    compiled_pipeline->vk_pipeline = vk_pipeline;
    compiled_pipeline->dynamic_state_flags = dynamic_state_flags;

    rw_spinlock_acquire_write(&state->lock);

    LIST_FOR_EACH_ENTRY(current, &graphics->compiled_fallback_pipelines, struct vkd3d_compiled_pipeline, entry)
    {
        if (!memcmp(&current->key, key, sizeof(*key)))
        {
            vkd3d_free(compiled_pipeline);
            compiled_pipeline = NULL;
            break;
        }
    }

    if (compiled_pipeline)
        list_add_tail(&graphics->compiled_fallback_pipelines, &compiled_pipeline->entry);

    rw_spinlock_release_write(&state->lock);
    return compiled_pipeline;
}

VkPipeline d3d12_pipeline_state_create_pipeline_variant(struct d3d12_pipeline_state *state,
        const struct vkd3d_pipeline_key *key, const struct vkd3d_format *dsv_format, VkPipelineCache vk_cache,
        uint32_t *dynamic_state_flags)
{
    VkVertexInputBindingDescription bindings[D3D12_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT];
    const struct vkd3d_vk_device_procs *vk_procs = &state->device->vk_procs;
    VkDynamicState dynamic_state_buffer[ARRAY_SIZE(vkd3d_dynamic_state_list)];
    struct d3d12_graphics_pipeline_state *graphics = &state->graphics;
    VkPipelineVertexInputDivisorStateCreateInfoEXT input_divisor_info;
    VkPipelineCreationFeedbackEXT feedbacks[VKD3D_MAX_SHADER_STAGES];
    VkPipelineShaderStageCreateInfo stages[VKD3D_MAX_SHADER_STAGES];
    VkFormat rtv_formats[D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT];
    VkPipelineTessellationStateCreateInfo tessellation_info;
    VkPipelineCreationFeedbackCreateInfoEXT feedback_info;
    VkPipelineDynamicStateCreateInfo dynamic_create_info;
    VkPipelineVertexInputStateCreateInfo input_desc;
    VkPipelineRenderingCreateInfoKHR rendering_info;
    VkPipelineInputAssemblyStateCreateInfo ia_desc;
    struct d3d12_device *device = state->device;
    VkGraphicsPipelineCreateInfo pipeline_desc;
    VkPipelineViewportStateCreateInfo vp_desc;
    VkPipelineCreationFeedbackEXT feedback;
    VkPipeline vk_pipeline;
    unsigned int i;
    VkResult vr;
    HRESULT hr;

    memcpy(bindings, graphics->attribute_bindings, graphics->attribute_binding_count * sizeof(*bindings));
    *dynamic_state_flags = d3d12_graphics_pipeline_state_init_dynamic_state(state, &dynamic_create_info,
            dynamic_state_buffer, key);

    if (key && !key->dynamic_stride)
    {
        /* If not using extended dynamic state, set static vertex stride. */
        for (i = 0; i < graphics->attribute_binding_count; i++)
            bindings[i].stride = key->strides[i];
    }

    if (!(graphics->stage_flags & VK_SHADER_STAGE_MESH_BIT_EXT))
    {
        input_desc.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        input_desc.pNext = NULL;
        input_desc.flags = 0;
        input_desc.vertexBindingDescriptionCount = graphics->attribute_binding_count;
        input_desc.pVertexBindingDescriptions = bindings;
        input_desc.vertexAttributeDescriptionCount = graphics->attribute_count;
        input_desc.pVertexAttributeDescriptions = graphics->attributes;

        if (graphics->instance_divisor_count)
        {
            input_desc.pNext = &input_divisor_info;
            input_divisor_info.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_DIVISOR_STATE_CREATE_INFO_EXT;
            input_divisor_info.pNext = NULL;
            input_divisor_info.vertexBindingDivisorCount = graphics->instance_divisor_count;
            input_divisor_info.pVertexBindingDivisors = graphics->instance_divisors;
        }

        ia_desc.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        ia_desc.pNext = NULL;
        ia_desc.flags = 0;
        ia_desc.topology = key && !key->dynamic_topology ?
                vk_topology_from_d3d12_topology(key->topology) :
                vk_topology_from_d3d12_topology_type(graphics->primitive_topology_type, !!graphics->index_buffer_strip_cut_value);
        ia_desc.primitiveRestartEnable = graphics->index_buffer_strip_cut_value &&
                                        (key && !key->dynamic_topology ?
                                          vkd3d_topology_can_restart(ia_desc.topology) :
                                          vkd3d_topology_type_can_restart(graphics->primitive_topology_type));

        tessellation_info.sType = VK_STRUCTURE_TYPE_PIPELINE_TESSELLATION_STATE_CREATE_INFO;
        tessellation_info.pNext = NULL;
        tessellation_info.flags = 0;
        tessellation_info.patchControlPoints = key && !key->dynamic_topology ?
                max(key->topology - D3D_PRIMITIVE_TOPOLOGY_1_CONTROL_POINT_PATCHLIST + 1, 1) :
                graphics->patch_vertex_count;
    }

    vp_desc.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vp_desc.pNext = NULL;
    vp_desc.flags = 0;
    vp_desc.viewportCount = 0;
    vp_desc.pViewports = NULL;
    vp_desc.scissorCount = 0;
    vp_desc.pScissors = NULL;

    rendering_info.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR;
    rendering_info.pNext = NULL;
    rendering_info.viewMask = 0;
    rendering_info.colorAttachmentCount = graphics->rt_count;
    rendering_info.pColorAttachmentFormats = rtv_formats;

    /* From spec:  If depthAttachmentFormat is not VK_FORMAT_UNDEFINED, it must be a format that includes a depth aspect. */
    rendering_info.depthAttachmentFormat = dsv_format && (dsv_format->vk_aspect_mask & VK_IMAGE_ASPECT_DEPTH_BIT) ?
            dsv_format->vk_format : VK_FORMAT_UNDEFINED;
    /* From spec:  If stencilAttachmentFormat is not VK_FORMAT_UNDEFINED, it must be a format that includes a stencil aspect. */
    rendering_info.stencilAttachmentFormat = dsv_format && (dsv_format->vk_aspect_mask & VK_IMAGE_ASPECT_STENCIL_BIT) ?
            dsv_format->vk_format : VK_FORMAT_UNDEFINED;

    for (i = 0; i < D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT; i++)
    {
        if (graphics->rtv_active_mask & (1u << i))
            rtv_formats[i] = graphics->rtv_formats[i];
        else
            rtv_formats[i] = VK_FORMAT_UNDEFINED;
    }

    memset(&pipeline_desc, 0, sizeof(pipeline_desc));
    pipeline_desc.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipeline_desc.pNext = &rendering_info;
    pipeline_desc.stageCount = graphics->stage_count;
    pipeline_desc.pStages = graphics->stages;
    pipeline_desc.pViewportState = &vp_desc;
    pipeline_desc.pRasterizationState = &graphics->rs_desc;
    pipeline_desc.pMultisampleState = &graphics->ms_desc;
    pipeline_desc.pDepthStencilState = &graphics->ds_desc;
    pipeline_desc.pColorBlendState = &graphics->blend_desc;
    pipeline_desc.pDynamicState = &dynamic_create_info;
    pipeline_desc.layout = graphics->pipeline_layout;
    pipeline_desc.basePipelineIndex = -1;

    if (d3d12_device_supports_variable_shading_rate_tier_2(device))
        pipeline_desc.flags |= VK_PIPELINE_CREATE_RENDERING_FRAGMENT_SHADING_RATE_ATTACHMENT_BIT_KHR;

    if (!(graphics->stage_flags & VK_SHADER_STAGE_MESH_BIT_EXT))
    {
        pipeline_desc.pVertexInputState = &input_desc;
        pipeline_desc.pInputAssemblyState = &ia_desc;
        pipeline_desc.pTessellationState = &tessellation_info;
    }

    /* A workaround for SottR, which creates pipelines with DSV_UNKNOWN, but still insists on using a depth buffer.
     * If we notice that the base pipeline's DSV format does not match the dynamic DSV format, we fall-back to create a new render pass. */
    if (d3d12_graphics_pipeline_state_has_unknown_dsv_format_with_test(graphics) && dsv_format)
        TRACE("Compiling %p with fallback DSV format %#x.\n", state, dsv_format->vk_format);

    /* FIXME: This gets modified on late recompilation, could there be thread safety issues here?
     * For GENERAL depth-stencil, this mask should not matter at all, but there might be edge cases for tracked DSV. */
    graphics->dsv_plane_optimal_mask = d3d12_graphics_pipeline_state_get_plane_optimal_mask(graphics, dsv_format);

    if (key)
    {
        /* In a fallback pipeline, we might have to re-create shader modules.
         * This can happen from multiple threads, so need temporary pStages array. */
        memcpy(stages, graphics->stages, graphics->stage_count * sizeof(stages[0]));

        for (i = 0; i < graphics->stage_count; i++)
        {
            if (stages[i].module == VK_NULL_HANDLE && graphics->code[i].code)
            {
                if (FAILED(hr = d3d12_pipeline_state_create_shader_module(device, &stages[i], &graphics->code[i])))
                {
                    /* This is kind of fatal and should only happen for out-of-memory. */
                    ERR("Unexpected failure (hr %x) in creating fallback SPIR-V module.\n", hr);
                    return VK_NULL_HANDLE;
                }

                pipeline_desc.pStages = stages;
            }
        }
    }

    TRACE("Calling vkCreateGraphicsPipelines.\n");

    if ((vkd3d_config_flags & VKD3D_CONFIG_FLAG_PIPELINE_LIBRARY_LOG) &&
            device->vk_info.EXT_pipeline_creation_feedback)
    {
        feedback_info.sType = VK_STRUCTURE_TYPE_PIPELINE_CREATION_FEEDBACK_CREATE_INFO_EXT;
        feedback_info.pNext = pipeline_desc.pNext;
        feedback_info.pPipelineStageCreationFeedbacks = feedbacks;
        feedback_info.pipelineStageCreationFeedbackCount = pipeline_desc.stageCount;
        feedback_info.pPipelineCreationFeedback = &feedback;
        pipeline_desc.pNext = &feedback_info;
    }
    else
        feedback_info.pipelineStageCreationFeedbackCount = 0;

    if ((vr = VK_CALL(vkCreateGraphicsPipelines(device->vk_device,
            vk_cache, 1, &pipeline_desc, NULL, &vk_pipeline))) < 0)
    {
        WARN("Failed to create Vulkan graphics pipeline, vr %d.\n", vr);
        return VK_NULL_HANDLE;
    }
    TRACE("Completed vkCreateGraphicsPipelines.\n");

    /* Clean up any temporary SPIR-V modules we created. */
    if (pipeline_desc.pStages == stages)
        for (i = 0; i < graphics->stage_count; i++)
            if (stages[i].module != graphics->stages[i].module)
                VK_CALL(vkDestroyShaderModule(device->vk_device, stages[i].module, NULL));

    if (feedback_info.pipelineStageCreationFeedbackCount)
        vkd3d_report_pipeline_creation_feedback_results(&feedback_info);

    return vk_pipeline;
}

static bool d3d12_pipeline_state_can_use_dynamic_stride(struct d3d12_pipeline_state *state,
        const struct vkd3d_dynamic_state *dyn_state)
{
    struct d3d12_graphics_pipeline_state *graphics = &state->graphics;
    uint32_t vertex_mask = graphics->vertex_buffer_mask;
    unsigned int slot;

    while (vertex_mask)
    {
        slot = vkd3d_bitmask_iter32(&vertex_mask);
        /* The vertex buffer stride must be larger than any attribute offset + format size which accesses a buffer binding.
         * This is somewhat awkward, since D3D12 does not have this restriction, although the validation layers do warn about this.
         * There might also be similar fallback paths on certain native drivers, who knows ... */

        /* Allow stride == 0 to pass through. This is allowed by the specification.
         * The stride >= offset + sizeof(format) rule is for AMD and OOB checks, since
         * we need compiler to adjust vtx index and offset in this scenario since checks are against vtx index
         * not byte address, but this path is irrelevant for stride == 0.
         * This is fairly common to see in games. Scarlet Nexus hits it pretty hard, and
         * we really should try to avoid late pipeline compiles here. */
        if (dyn_state->vertex_strides[slot] &&
                dyn_state->vertex_strides[slot] < graphics->minimum_vertex_buffer_dynamic_stride[slot])
        {
            TRACE("Stride for slot %u is %u bytes, but need at least %u.\n", slot,
                  (unsigned int)dyn_state->vertex_strides[slot],
                  graphics->minimum_vertex_buffer_dynamic_stride[slot]);
            return false;
        }
    }

    return true;
}

VkPipeline d3d12_pipeline_state_get_pipeline(struct d3d12_pipeline_state *state,
        const struct vkd3d_dynamic_state *dyn_state, const struct vkd3d_format *dsv_format,
        uint32_t *dynamic_state_flags)
{
    struct d3d12_graphics_pipeline_state *graphics = &state->graphics;

    if (!graphics->pipeline)
        return VK_NULL_HANDLE;

    if (d3d12_graphics_pipeline_state_has_unknown_dsv_format_with_test(graphics) && dsv_format)
    {
        TRACE("Applying unknown DSV workaround with format %u buggy application!\n", dsv_format->vk_format);
        return VK_NULL_HANDLE;
    }

    if (!(graphics->stage_flags & VK_PIPELINE_STAGE_MESH_SHADER_BIT_EXT) &&
            !d3d12_pipeline_state_can_use_dynamic_stride(state, dyn_state))
    {
        TRACE("Cannot use dynamic stride, falling back ...\n");
        return VK_NULL_HANDLE;
    }

    /* It should be illegal to use different patch size for topology compared to pipeline, but be safe here. */
    if (dyn_state->vk_primitive_topology == VK_PRIMITIVE_TOPOLOGY_PATCH_LIST &&
        (dyn_state->primitive_topology - D3D_PRIMITIVE_TOPOLOGY_1_CONTROL_POINT_PATCHLIST + 1) != graphics->patch_vertex_count)
    {
        if (graphics->patch_vertex_count)
        {
            TRACE("Mismatch in tessellation control points, expected %u, but got %u.\n",
                  graphics->patch_vertex_count,
                  dyn_state->primitive_topology - D3D_PRIMITIVE_TOPOLOGY_1_CONTROL_POINT_PATCHLIST + 1);
        }
        return VK_NULL_HANDLE;
    }

    *dynamic_state_flags = state->graphics.dynamic_state_flags;
    return state->graphics.pipeline;
}

VkPipeline d3d12_pipeline_state_get_or_create_pipeline(struct d3d12_pipeline_state *state,
        const struct vkd3d_dynamic_state *dyn_state, const struct vkd3d_format *dsv_format,
        uint32_t *dynamic_state_flags)
{
    const struct vkd3d_vk_device_procs *vk_procs = &state->device->vk_procs;
    struct d3d12_graphics_pipeline_state *graphics = &state->graphics;
    struct d3d12_device *device = state->device;
    struct vkd3d_pipeline_key pipeline_key;
    uint32_t stride, stride_align_mask;
    VkPipeline vk_pipeline;
    unsigned int i;

    assert(d3d12_pipeline_state_is_graphics(state));

    memset(&pipeline_key, 0, sizeof(pipeline_key));

    /* Try to keep as much dynamic state as possible so we don't have to rebind state unnecessarily. */
    if (!(graphics->stage_flags & VK_SHADER_STAGE_MESH_BIT_EXT))
    {
        if (graphics->primitive_topology_type != D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH &&
            graphics->primitive_topology_type != D3D12_PRIMITIVE_TOPOLOGY_TYPE_UNDEFINED)
            pipeline_key.dynamic_topology = true;
        else
            pipeline_key.topology = dyn_state->primitive_topology;

        if (d3d12_pipeline_state_can_use_dynamic_stride(state, dyn_state))
        {
            pipeline_key.dynamic_stride = true;
        }
        else
        {
            for (i = 0; i < graphics->attribute_binding_count; ++i)
            {
                stride = dyn_state->vertex_strides[graphics->attribute_bindings[i].binding];
                stride_align_mask = state->graphics.vertex_buffer_stride_align_mask[graphics->attribute_bindings[i].binding];
                if (stride & stride_align_mask)
                {
                    FIXME("Attempting to use VBO stride of %u bytes, but D3D12 requires alignment of %u bytes. VBO stride will be adjusted.\n",
                            stride, stride_align_mask + 1);
                    stride &= ~stride_align_mask;
                }
                pipeline_key.strides[i] = stride;
            }
        }
    }

    pipeline_key.dsv_format = dsv_format ? dsv_format->vk_format : VK_FORMAT_UNDEFINED;

    if ((vk_pipeline = d3d12_pipeline_state_find_compiled_pipeline(state, &pipeline_key, dynamic_state_flags)))
    {
        return vk_pipeline;
    }

    FIXME("Compiling a fallback pipeline late!\n");

    vk_pipeline = d3d12_pipeline_state_create_pipeline_variant(state,
            &pipeline_key, dsv_format, VK_NULL_HANDLE, dynamic_state_flags);

    if (!vk_pipeline)
    {
        ERR("Failed to create pipeline.\n");
        return VK_NULL_HANDLE;
    }

    if (d3d12_pipeline_state_put_pipeline_to_cache(state, &pipeline_key, vk_pipeline, *dynamic_state_flags))
        return vk_pipeline;

    /* Other thread compiled the pipeline before us. */
    VK_CALL(vkDestroyPipeline(device->vk_device, vk_pipeline, NULL));
    vk_pipeline = d3d12_pipeline_state_find_compiled_pipeline(state, &pipeline_key, dynamic_state_flags);
    if (!vk_pipeline)
        ERR("Could not get the pipeline compiled by other thread from the cache.\n");
    return vk_pipeline;
}

static uint32_t d3d12_max_descriptor_count_from_heap_type(D3D12_DESCRIPTOR_HEAP_TYPE heap_type)
{
    switch (heap_type)
    {
        case D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV:
            if (vkd3d_descriptor_debug_active_qa_checks())
                return 1000000 + VKD3D_DESCRIPTOR_DEBUG_NUM_PAD_DESCRIPTORS;
            return 1000000;

        case D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER:
            return 2048;

        default:
            ERR("Invalid descriptor heap type %d.\n", heap_type);
            return 0;
    }
}

static uint32_t d3d12_max_host_descriptor_count_from_heap_type(struct d3d12_device *device, D3D12_DESCRIPTOR_HEAP_TYPE heap_type)
{
    const VkPhysicalDeviceDescriptorIndexingPropertiesEXT *limits = &device->device_info.descriptor_indexing_properties;

    switch (heap_type)
    {
        case D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV:
        {
            uint32_t cbv_count = device->bindless_state.flags & VKD3D_BINDLESS_CBV_AS_SSBO
                    ? limits->maxDescriptorSetUpdateAfterBindStorageBuffers
                    : limits->maxDescriptorSetUpdateAfterBindUniformBuffers;
            uint32_t srv_count = limits->maxDescriptorSetUpdateAfterBindSampledImages;
            uint32_t uav_count = min(limits->maxDescriptorSetUpdateAfterBindStorageBuffers,
                    limits->maxDescriptorSetUpdateAfterBindStorageImages);
            return min(cbv_count, min(srv_count, uav_count));
        }

        case D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER:
            return limits->maxDescriptorSetUpdateAfterBindSamplers;

        default:
            ERR("Invalid descriptor heap type %d.\n", heap_type);
            return 0;
    }
}

static uint32_t vkd3d_bindless_build_mutable_type_list(VkDescriptorType *list, uint32_t flags)
{
    uint32_t count = 0;
    if (flags & VKD3D_BINDLESS_CBV)
    {
        list[count++] = flags & VKD3D_BINDLESS_CBV_AS_SSBO ?
                VK_DESCRIPTOR_TYPE_STORAGE_BUFFER : VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    }

    if (flags & VKD3D_BINDLESS_UAV)
    {
        list[count++] = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        list[count++] = VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER;

        /* This behavior should be default, but there are too many broken games.
         * Can be used as a perf/memory opt-in.
         * Will likely be required on Intel as well due to anemic bindless sizes. */
        if ((flags & VKD3D_BINDLESS_MUTABLE_TYPE_RAW_SSBO) && !(flags & VKD3D_BINDLESS_CBV_AS_SSBO))
            list[count++] = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    }

    if (flags & VKD3D_BINDLESS_SRV)
    {
        list[count++] = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        list[count++] = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
    }

    return count;
}

/* Make sure copy sizes are deducible to constants by compiler, especially the single descriptor case.
 * We can get a linear stream of SIMD copies this way.
 * Potentially we can also use alignment hints to get aligned moves here,
 * but it doesn't seem to matter at all for perf, so don't bother adding the extra complexity. */
#define VKD3D_DECL_DESCRIPTOR_COPY_SIZE(bytes) \
static inline void vkd3d_descriptor_copy_desc_##bytes(void * restrict dst_, const void * restrict src_, \
        size_t dst_index, size_t src_index, size_t count) \
{ \
    uint8_t *dst = dst_; \
    const uint8_t *src = src_; \
    memcpy(dst + dst_index * (bytes), src + src_index * (bytes), count * (bytes)); \
} \
static inline void vkd3d_descriptor_copy_desc_##bytes##_single(void * restrict dst_, const void * restrict src_, \
        size_t dst_index, size_t src_index) \
{ \
    vkd3d_descriptor_copy_desc_##bytes(dst_, src_, dst_index, src_index, 1); \
}
VKD3D_DECL_DESCRIPTOR_COPY_SIZE(8)
VKD3D_DECL_DESCRIPTOR_COPY_SIZE(16)
VKD3D_DECL_DESCRIPTOR_COPY_SIZE(32)
VKD3D_DECL_DESCRIPTOR_COPY_SIZE(48)
VKD3D_DECL_DESCRIPTOR_COPY_SIZE(64)

static pfn_vkd3d_host_mapping_copy_template vkd3d_bindless_find_copy_template(uint32_t descriptor_size)
{
    switch (descriptor_size)
    {
        case 8:
            return vkd3d_descriptor_copy_desc_8;
        case 16:
            return vkd3d_descriptor_copy_desc_16;
        case 32:
            return vkd3d_descriptor_copy_desc_32;
        case 48:
            return vkd3d_descriptor_copy_desc_48;
        case 64:
            return vkd3d_descriptor_copy_desc_64;
        default:
            break;
    }

    return NULL;
}

static pfn_vkd3d_host_mapping_copy_template_single vkd3d_bindless_find_copy_template_single(uint32_t descriptor_size)
{
    switch (descriptor_size)
    {
        case 8:
            return vkd3d_descriptor_copy_desc_8_single;
        case 16:
            return vkd3d_descriptor_copy_desc_16_single;
        case 32:
            return vkd3d_descriptor_copy_desc_32_single;
        case 48:
            return vkd3d_descriptor_copy_desc_48_single;
        case 64:
            return vkd3d_descriptor_copy_desc_64_single;
        default:
            break;
    }

    return NULL;
}

static HRESULT vkd3d_bindless_state_add_binding(struct vkd3d_bindless_state *bindless_state,
        struct d3d12_device *device, uint32_t flags, VkDescriptorType vk_descriptor_type)
{
    VkMutableDescriptorTypeListVALVE mutable_descriptor_list[VKD3D_BINDLESS_SET_MAX_EXTRA_BINDINGS + 1];
    struct vkd3d_bindless_set_info *set_info = &bindless_state->set_info[bindless_state->set_count++];
    VkDescriptorSetLayoutBinding vk_binding_info[VKD3D_BINDLESS_SET_MAX_EXTRA_BINDINGS + 1];
    VkDescriptorBindingFlagsEXT vk_binding_flags[VKD3D_BINDLESS_SET_MAX_EXTRA_BINDINGS + 1];
    VkDescriptorType mutable_descriptor_types[VKD3D_MAX_MUTABLE_DESCRIPTOR_TYPES];
    VkDescriptorSetLayoutBindingFlagsCreateInfoEXT vk_binding_flags_info;
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    VkDescriptorSetLayoutHostMappingInfoVALVE mapping_info;
    VkDescriptorSetBindingReferenceVALVE binding_reference;
    VkMutableDescriptorTypeCreateInfoVALVE mutable_info;
    VkDescriptorSetLayoutCreateInfo vk_set_layout_info;
    VkDescriptorSetLayoutBinding *vk_binding;
    unsigned int i;
    VkResult vr;

    set_info->vk_descriptor_type = vk_descriptor_type;
    set_info->heap_type = flags & VKD3D_BINDLESS_SET_SAMPLER
            ? D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER
            : D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    set_info->flags = flags;
    set_info->binding_index = vkd3d_popcount(flags & VKD3D_BINDLESS_SET_EXTRA_MASK);

    if (set_info->heap_type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV)
        set_info->set_index = bindless_state->cbv_srv_uav_count++;
    else
        set_info->set_index = 0;

    for (i = 0; i < set_info->binding_index; i++)
    {
        /* all extra bindings are storage buffers right now */
        vk_binding_info[i].binding = i;
        vk_binding_info[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        vk_binding_info[i].descriptorCount = 1;
        vk_binding_info[i].stageFlags = VK_SHADER_STAGE_ALL;
        vk_binding_info[i].pImmutableSamplers = NULL;

        vk_binding_flags[i] = 0;
    }

    vk_binding = &vk_binding_info[set_info->binding_index];
    vk_binding->binding = set_info->binding_index;
    vk_binding->descriptorType = set_info->vk_descriptor_type;
    vk_binding->descriptorCount = d3d12_max_descriptor_count_from_heap_type(set_info->heap_type);
    vk_binding->stageFlags = VK_SHADER_STAGE_ALL;
    vk_binding->pImmutableSamplers = NULL;

    vk_binding_flags[set_info->binding_index] = VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT_EXT |
            VK_DESCRIPTOR_BINDING_UPDATE_UNUSED_WHILE_PENDING_BIT_EXT |
            VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT_EXT |
            VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT_EXT;

    vk_binding_flags_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO_EXT;
    vk_binding_flags_info.pNext = NULL;
    vk_binding_flags_info.bindingCount = set_info->binding_index + 1;
    vk_binding_flags_info.pBindingFlags = vk_binding_flags;

    vk_set_layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    vk_set_layout_info.pNext = &vk_binding_flags_info;
    vk_set_layout_info.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT_EXT;
    vk_set_layout_info.bindingCount = set_info->binding_index + 1;
    vk_set_layout_info.pBindings = vk_binding_info;

    if (vk_descriptor_type == VK_DESCRIPTOR_TYPE_MUTABLE_VALVE)
    {
        vk_binding_flags_info.pNext = &mutable_info;

        mutable_info.sType = VK_STRUCTURE_TYPE_MUTABLE_DESCRIPTOR_TYPE_CREATE_INFO_VALVE;
        mutable_info.pNext = NULL;
        mutable_info.pMutableDescriptorTypeLists = mutable_descriptor_list;
        mutable_info.mutableDescriptorTypeListCount = set_info->binding_index + 1;

        memset(mutable_descriptor_list, 0, sizeof(mutable_descriptor_list));
        mutable_descriptor_list[set_info->binding_index].descriptorTypeCount =
                vkd3d_bindless_build_mutable_type_list(mutable_descriptor_types, device->bindless_state.flags);
        mutable_descriptor_list[set_info->binding_index].pDescriptorTypes = mutable_descriptor_types;
    }

    if ((vr = VK_CALL(vkCreateDescriptorSetLayout(device->vk_device,
            &vk_set_layout_info, NULL, &set_info->vk_set_layout))) < 0)
        ERR("Failed to create descriptor set layout, vr %d.\n", vr);

    /* If we're able, we should implement descriptor copies with functions we roll ourselves. */
    if (device->device_info.descriptor_set_host_mapping_features.descriptorSetHostMapping)
    {
        INFO("Device supports VK_VALVE_descriptor_set_host_mapping!\n");
        binding_reference.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_BINDING_REFERENCE_VALVE;
        binding_reference.pNext = NULL;
        binding_reference.descriptorSetLayout = set_info->vk_set_layout;
        binding_reference.binding = set_info->binding_index;
        mapping_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_HOST_MAPPING_INFO_VALVE;
        mapping_info.pNext = NULL;

        VK_CALL(vkGetDescriptorSetLayoutHostMappingInfoVALVE(device->vk_device,
                &binding_reference, &mapping_info));

        set_info->host_mapping_offset = mapping_info.descriptorOffset;
        set_info->host_mapping_descriptor_size = mapping_info.descriptorSize;
        set_info->host_copy_template = vkd3d_bindless_find_copy_template(mapping_info.descriptorSize);
        set_info->host_copy_template_single = vkd3d_bindless_find_copy_template_single(mapping_info.descriptorSize);

        if (!set_info->host_copy_template || !set_info->host_copy_template_single)
        {
            FIXME("Couldn't find suitable host copy template.\n");
            set_info->host_copy_template = NULL;
            set_info->host_copy_template_single = NULL;
        }
    }
    else
    {
        set_info->host_mapping_offset = 0;
        set_info->host_mapping_descriptor_size = 0;
        set_info->host_copy_template = NULL;
        set_info->host_copy_template_single = NULL;
    }

    vk_binding->descriptorCount = d3d12_max_host_descriptor_count_from_heap_type(device, set_info->heap_type);

    if (device->bindless_state.flags & VKD3D_BINDLESS_MUTABLE_TYPE)
    {
        /* If we have mutable descriptor extension, we will allocate these descriptors with
         * HOST_BIT and not UPDATE_AFTER_BIND, since that is enough to get threading guarantees. */
        vk_binding_flags[set_info->binding_index] = VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT_EXT;
        vk_set_layout_info.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_HOST_ONLY_POOL_BIT_VALVE;
    }

    if ((vr = VK_CALL(vkCreateDescriptorSetLayout(device->vk_device,
            &vk_set_layout_info, NULL, &set_info->vk_host_set_layout))) < 0)
        ERR("Failed to create descriptor set layout, vr %d.\n", vr);

    return hresult_from_vk_result(vr);
}

static bool vkd3d_bindless_supports_mutable_type(struct d3d12_device *device, uint32_t flags)
{
    VkDescriptorType descriptor_types[VKD3D_MAX_MUTABLE_DESCRIPTOR_TYPES];
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    VkDescriptorSetLayoutBindingFlagsCreateInfoEXT binding_flags;
    VkMutableDescriptorTypeCreateInfoVALVE mutable_info;
    VkDescriptorSetLayoutCreateInfo set_layout_info;
    VkMutableDescriptorTypeListVALVE mutable_list;
    VkDescriptorSetLayoutSupport supported;
    VkDescriptorSetLayoutBinding binding;

    VkDescriptorBindingFlagsEXT binding_flag =
            VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT_EXT |
            VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT_EXT |
            VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT_EXT |
            VK_DESCRIPTOR_BINDING_UPDATE_UNUSED_WHILE_PENDING_BIT_EXT;

    if (!device->device_info.mutable_descriptor_features.mutableDescriptorType)
        return false;

    mutable_info.sType = VK_STRUCTURE_TYPE_MUTABLE_DESCRIPTOR_TYPE_CREATE_INFO_VALVE;
    mutable_info.pNext = NULL;
    mutable_info.pMutableDescriptorTypeLists = &mutable_list;
    mutable_info.mutableDescriptorTypeListCount = 1;

    mutable_list.descriptorTypeCount = vkd3d_bindless_build_mutable_type_list(descriptor_types, flags);
    mutable_list.pDescriptorTypes = descriptor_types;

    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_MUTABLE_VALVE;
    binding.descriptorCount = d3d12_max_descriptor_count_from_heap_type(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    binding.pImmutableSamplers = NULL;
    binding.stageFlags = VK_SHADER_STAGE_ALL;

    binding_flags.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO_EXT;
    binding_flags.pNext = &mutable_info;
    binding_flags.bindingCount = 1;
    binding_flags.pBindingFlags = &binding_flag;

    set_layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    set_layout_info.pNext = &binding_flags;
    set_layout_info.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
    set_layout_info.bindingCount = 1;
    set_layout_info.pBindings = &binding;

    supported.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_SUPPORT;
    supported.pNext = NULL;
    VK_CALL(vkGetDescriptorSetLayoutSupport(device->vk_device, &set_layout_info, &supported));
    if (!supported.supported)
        return false;

    set_layout_info.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_HOST_ONLY_POOL_BIT_VALVE;
    binding_flag = VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT_EXT;
    VK_CALL(vkGetDescriptorSetLayoutSupport(device->vk_device, &set_layout_info, &supported));
    return supported.supported == VK_TRUE;
}

static uint32_t vkd3d_bindless_state_get_bindless_flags(struct d3d12_device *device)
{
    const struct vkd3d_physical_device_info *device_info = &device->device_info;
    const struct vkd3d_vulkan_info *vk_info = &device->vk_info;
    uint32_t flags = 0;

    if (!vk_info->EXT_descriptor_indexing ||
            !device_info->descriptor_indexing_features.runtimeDescriptorArray ||
            !device_info->descriptor_indexing_features.descriptorBindingPartiallyBound ||
            !device_info->descriptor_indexing_features.descriptorBindingUpdateUnusedWhilePending ||
            !device_info->descriptor_indexing_features.descriptorBindingVariableDescriptorCount)
        return 0;

    if (device_info->descriptor_indexing_properties.maxPerStageDescriptorUpdateAfterBindSampledImages >= 1000000 &&
            device_info->descriptor_indexing_features.descriptorBindingSampledImageUpdateAfterBind &&
            device_info->descriptor_indexing_features.descriptorBindingUniformTexelBufferUpdateAfterBind &&
            device_info->descriptor_indexing_features.shaderSampledImageArrayNonUniformIndexing &&
            device_info->descriptor_indexing_features.shaderUniformTexelBufferArrayNonUniformIndexing)
        flags |= VKD3D_BINDLESS_SAMPLER | VKD3D_BINDLESS_SRV;

    if (device_info->descriptor_indexing_properties.maxPerStageDescriptorUpdateAfterBindStorageImages >= 1000000 &&
            device_info->descriptor_indexing_features.descriptorBindingStorageImageUpdateAfterBind &&
            device_info->descriptor_indexing_features.descriptorBindingStorageTexelBufferUpdateAfterBind &&
            device_info->descriptor_indexing_features.shaderStorageImageArrayNonUniformIndexing &&
            device_info->descriptor_indexing_features.shaderStorageTexelBufferArrayNonUniformIndexing)
        flags |= VKD3D_BINDLESS_UAV;

    if (device_info->descriptor_indexing_properties.maxPerStageDescriptorUpdateAfterBindUniformBuffers >= 1000000 &&
            device_info->descriptor_indexing_features.descriptorBindingUniformBufferUpdateAfterBind &&
            device_info->descriptor_indexing_features.shaderUniformBufferArrayNonUniformIndexing)
        flags |= VKD3D_BINDLESS_CBV;
    else if (device_info->descriptor_indexing_properties.maxPerStageDescriptorUpdateAfterBindStorageBuffers >= 1000000 &&
            device_info->descriptor_indexing_features.descriptorBindingStorageBufferUpdateAfterBind &&
            device_info->descriptor_indexing_features.shaderStorageBufferArrayNonUniformIndexing)
        flags |= VKD3D_BINDLESS_CBV | VKD3D_BINDLESS_CBV_AS_SSBO;

    /* Normally, we would be able to use SSBOs conditionally even when maxSSBOAlignment > 4, but
     * applications (RE2 being one example) are of course buggy and don't match descriptor and shader usage of resources,
     * so we cannot rely on alignment analysis to select the appropriate resource type. */
    if (device_info->descriptor_indexing_properties.maxPerStageDescriptorUpdateAfterBindStorageBuffers >= 1000000 &&
        device_info->descriptor_indexing_features.descriptorBindingStorageBufferUpdateAfterBind &&
        device_info->properties2.properties.limits.minStorageBufferOffsetAlignment <= 16)
    {
        flags |= VKD3D_BINDLESS_RAW_SSBO;

        /* Intel GPUs have smol descriptor heaps and only way we can fit a D3D12 heap is with
         * single set mutable. */
        if ((vkd3d_config_flags & VKD3D_CONFIG_FLAG_MUTABLE_SINGLE_SET) ||
                device_info->properties2.properties.vendorID == VKD3D_VENDOR_ID_INTEL)
        {
            INFO("Enabling single descriptor set path for MUTABLE.\n");
            flags |= VKD3D_BINDLESS_MUTABLE_TYPE_RAW_SSBO;
        }

        if (device_info->properties2.properties.limits.minStorageBufferOffsetAlignment > 4)
            flags |= VKD3D_SSBO_OFFSET_BUFFER;
    }

    /* Always use a typed offset buffer. Otherwise, we risk ending up with unbounded size on view maps. */
    flags |= VKD3D_TYPED_OFFSET_BUFFER;

    if (device_info->buffer_device_address_features.bufferDeviceAddress && (flags & VKD3D_BINDLESS_UAV))
        flags |= VKD3D_RAW_VA_AUX_BUFFER;

    /* We must use root SRV and UAV due to alignment requirements for 16-bit storage,
     * but root CBV is more lax. */
    if (device_info->buffer_device_address_features.bufferDeviceAddress)
    {
        flags |= VKD3D_RAW_VA_ROOT_DESCRIPTOR_SRV_UAV;
        /* CBV's really require push descriptors on NVIDIA to get maximum performance.
         * The difference in performance is profound (~15% in some cases).
         * On ACO, BDA with NonWritable can be promoted directly to scalar loads,
         * which is great. */
        if ((vkd3d_config_flags & VKD3D_CONFIG_FLAG_FORCE_RAW_VA_CBV) ||
                device_info->properties2.properties.vendorID != VKD3D_VENDOR_ID_NVIDIA)
            flags |= VKD3D_RAW_VA_ROOT_DESCRIPTOR_CBV;
    }

    if (device_info->properties2.properties.vendorID == VKD3D_VENDOR_ID_NVIDIA &&
            !(flags & VKD3D_RAW_VA_ROOT_DESCRIPTOR_CBV))
    {
        /* On NVIDIA, it's preferable to hoist CBVs to push descriptors if we can.
         * Hoisting is only safe with push descriptors since we need to consider
         * robustness as well for STATIC_KEEPING_BUFFER_BOUNDS_CHECKS. */
        flags |= VKD3D_HOIST_STATIC_TABLE_CBV;
    }

    if (vkd3d_bindless_supports_mutable_type(device, flags))
    {
        INFO("Device supports VK_VALVE_mutable_descriptor_type.\n");
        flags |= VKD3D_BINDLESS_MUTABLE_TYPE;
    }
    else
    {
        INFO("Device does not support VK_VALVE_mutable_descriptor_type.\n");
        flags &= ~VKD3D_BINDLESS_MUTABLE_TYPE_RAW_SSBO;
    }

    return flags;
}

HRESULT vkd3d_bindless_state_init(struct vkd3d_bindless_state *bindless_state,
        struct d3d12_device *device)
{
    const uint32_t required_flags = VKD3D_BINDLESS_SRV |
            VKD3D_BINDLESS_UAV | VKD3D_BINDLESS_CBV | VKD3D_BINDLESS_SAMPLER;
    uint32_t extra_bindings = 0;
    bool use_raw_ssbo_binding;
    HRESULT hr = E_FAIL;

    memset(bindless_state, 0, sizeof(*bindless_state));
    bindless_state->flags = vkd3d_bindless_state_get_bindless_flags(device);

    if ((bindless_state->flags & required_flags) != required_flags)
    {
        ERR("Insufficient descriptor indexing support.\n");
        goto fail;
    }

    if (bindless_state->flags & VKD3D_RAW_VA_AUX_BUFFER)
        extra_bindings |= VKD3D_BINDLESS_SET_EXTRA_RAW_VA_AUX_BUFFER;

    if (bindless_state->flags & (VKD3D_SSBO_OFFSET_BUFFER | VKD3D_TYPED_OFFSET_BUFFER))
        extra_bindings |= VKD3D_BINDLESS_SET_EXTRA_OFFSET_BUFFER;

    if (vkd3d_descriptor_debug_active_qa_checks())
    {
        extra_bindings |= VKD3D_BINDLESS_SET_EXTRA_GLOBAL_HEAP_INFO_BUFFER |
                VKD3D_BINDLESS_SET_EXTRA_DESCRIPTOR_HEAP_INFO_BUFFER;
    }

    if (FAILED(hr = vkd3d_bindless_state_add_binding(bindless_state, device,
            VKD3D_BINDLESS_SET_SAMPLER, VK_DESCRIPTOR_TYPE_SAMPLER)))
        goto fail;

    if (bindless_state->flags & VKD3D_BINDLESS_MUTABLE_TYPE)
    {
        use_raw_ssbo_binding = !!(bindless_state->flags & VKD3D_BINDLESS_MUTABLE_TYPE_RAW_SSBO);

        /* If we can, prefer to use one universal descriptor type which works for any descriptor.
         * The exception is SSBOs since we need to workaround buggy applications which create typed buffers,
         * but assume they can be read as untyped buffers.
         * If we opt-in to it, we can move everything into the mutable set. */
        if (FAILED(hr = vkd3d_bindless_state_add_binding(bindless_state, device,
                VKD3D_BINDLESS_SET_CBV | VKD3D_BINDLESS_SET_UAV | VKD3D_BINDLESS_SET_SRV |
                VKD3D_BINDLESS_SET_BUFFER | VKD3D_BINDLESS_SET_IMAGE |
                (use_raw_ssbo_binding ? VKD3D_BINDLESS_SET_RAW_SSBO : 0) |
                VKD3D_BINDLESS_SET_MUTABLE | extra_bindings,
                VK_DESCRIPTOR_TYPE_MUTABLE_VALVE)))
            goto fail;

        use_raw_ssbo_binding = !use_raw_ssbo_binding && (bindless_state->flags & VKD3D_BINDLESS_RAW_SSBO);
    }
    else
    {
        if (FAILED(hr = vkd3d_bindless_state_add_binding(bindless_state, device,
                VKD3D_BINDLESS_SET_CBV | extra_bindings,
                vkd3d_bindless_state_get_cbv_descriptor_type(bindless_state))))
            goto fail;

        if (FAILED(hr = vkd3d_bindless_state_add_binding(bindless_state, device,
                VKD3D_BINDLESS_SET_SRV | VKD3D_BINDLESS_SET_BUFFER,
                VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER)) ||
            FAILED(hr = vkd3d_bindless_state_add_binding(bindless_state, device,
                VKD3D_BINDLESS_SET_SRV | VKD3D_BINDLESS_SET_IMAGE,
                VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE)))
            goto fail;

        if (FAILED(hr = vkd3d_bindless_state_add_binding(bindless_state, device,
                VKD3D_BINDLESS_SET_UAV | VKD3D_BINDLESS_SET_BUFFER,
                VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER)) ||
            FAILED(hr = vkd3d_bindless_state_add_binding(bindless_state, device,
                VKD3D_BINDLESS_SET_UAV | VKD3D_BINDLESS_SET_IMAGE,
                VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)))
            goto fail;

        use_raw_ssbo_binding = !!(bindless_state->flags & VKD3D_BINDLESS_RAW_SSBO);
    }

    if (use_raw_ssbo_binding)
    {
        if (FAILED(hr = vkd3d_bindless_state_add_binding(bindless_state, device,
                VKD3D_BINDLESS_SET_UAV | VKD3D_BINDLESS_SET_SRV |
                VKD3D_BINDLESS_SET_RAW_SSBO,
                VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)))
            goto fail;
    }

    if (!(bindless_state->flags & VKD3D_RAW_VA_AUX_BUFFER))
    {
        if (FAILED(hr = vkd3d_bindless_state_add_binding(bindless_state, device,
                VKD3D_BINDLESS_SET_UAV | VKD3D_BINDLESS_SET_AUX_BUFFER,
                VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER)))
            goto fail;
    }

    return S_OK;

fail:
    vkd3d_bindless_state_cleanup(bindless_state, device);
    return hr;
}

void vkd3d_bindless_state_cleanup(struct vkd3d_bindless_state *bindless_state,
        struct d3d12_device *device)
{
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    unsigned int i;

    for (i = 0; i < bindless_state->set_count; i++)
    {
        VK_CALL(vkDestroyDescriptorSetLayout(device->vk_device, bindless_state->set_info[i].vk_set_layout, NULL));
        VK_CALL(vkDestroyDescriptorSetLayout(device->vk_device, bindless_state->set_info[i].vk_host_set_layout, NULL));
    }
}

static inline uint32_t vkd3d_bindless_state_get_extra_binding_index(uint32_t extra_flag, uint32_t set_flags)
{
    return vkd3d_popcount(set_flags & VKD3D_BINDLESS_SET_EXTRA_MASK & (extra_flag - 1));
}

bool vkd3d_bindless_state_find_binding(const struct vkd3d_bindless_state *bindless_state,
        uint32_t flags, struct vkd3d_shader_descriptor_binding *binding)
{
    unsigned int i;

    for (i = 0; i < bindless_state->set_count; i++)
    {
        const struct vkd3d_bindless_set_info *set_info = &bindless_state->set_info[i];

        if ((set_info->flags & flags) == flags)
        {
            binding->set = i;
            binding->binding = set_info->binding_index;

            if (flags & VKD3D_BINDLESS_SET_EXTRA_MASK)
                binding->binding = vkd3d_bindless_state_get_extra_binding_index(flags, set_info->flags);
            return true;
        }
    }

    return false;
}

struct vkd3d_descriptor_binding vkd3d_bindless_state_find_set(const struct vkd3d_bindless_state *bindless_state, uint32_t flags)
{
    struct vkd3d_descriptor_binding binding;
    D3D12_DESCRIPTOR_HEAP_TYPE heap_type;
    unsigned int i, set_index = 0;

    heap_type = flags & VKD3D_BINDLESS_SET_SAMPLER
            ? D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER
            : D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;

    for (i = 0; i < bindless_state->set_count; i++)
    {
        const struct vkd3d_bindless_set_info *set_info = &bindless_state->set_info[i];

        if (set_info->heap_type == heap_type)
        {
            if ((set_info->flags & flags) == flags)
            {
                binding.set = set_index;
                binding.binding = set_info->binding_index;

                if (flags & VKD3D_BINDLESS_SET_EXTRA_MASK)
                    binding.binding = vkd3d_bindless_state_get_extra_binding_index(flags, set_info->flags);
                return binding;
            }

            set_index++;
        }
    }

    ERR("No set found for flags %#x.", flags);
    binding.set = 0;
    binding.binding = 0;
    return binding;
}

uint32_t vkd3d_bindless_state_find_set_info_index(const struct vkd3d_bindless_state *bindless_state, uint32_t flags)
{
    D3D12_DESCRIPTOR_HEAP_TYPE heap_type;
    unsigned int i;

    heap_type = flags & VKD3D_BINDLESS_SET_SAMPLER
                ? D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER
                : D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;

    for (i = 0; i < bindless_state->set_count; i++)
    {
        const struct vkd3d_bindless_set_info *set_info = &bindless_state->set_info[i];
        if (set_info->heap_type == heap_type && (set_info->flags & flags) == flags)
            return i;
    }

    ERR("No set found for flags %#x.", flags);
    return 0;
}
