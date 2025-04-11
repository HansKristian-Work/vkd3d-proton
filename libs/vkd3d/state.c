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
    struct d3d12_root_signature *root_signature = impl_from_ID3D12RootSignature(iface);

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

    if (IsEqualGUID(riid, &IID_ID3DDestructionNotifier))
    {
        ID3DDestructionNotifier_AddRef(&root_signature->destruction_notifier.ID3DDestructionNotifier_iface);
        *object = &root_signature->destruction_notifier.ID3DDestructionNotifier_iface;
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
        d3d_destruction_notifier_free(&root_signature->destruction_notifier);
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
        d3d_destruction_notifier_notify(&root_signature->destruction_notifier);

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
        case D3D12_SHADER_VISIBILITY_AMPLIFICATION:
            return VKD3D_SHADER_VISIBILITY_AMPLIFICATION;
        case D3D12_SHADER_VISIBILITY_MESH:
            return VKD3D_SHADER_VISIBILITY_MESH;
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
        const VkDescriptorSetLayoutBinding *bindings,
        VkDescriptorSetLayoutCreateFlags descriptor_buffer_flags,
        VkDescriptorSetLayout *set_layout)
{
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    VkDescriptorSetLayoutCreateInfo set_desc;
    VkResult vr;

    set_desc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    set_desc.pNext = NULL;
    set_desc.flags = flags;
    set_desc.bindingCount = binding_count;
    set_desc.pBindings = bindings;

    if (d3d12_device_uses_descriptor_buffers(device))
        set_desc.flags |= descriptor_buffer_flags;

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
        struct d3d12_device *device, const D3D12_ROOT_SIGNATURE_DESC2 *desc, const D3D12_DESCRIPTOR_RANGE1 *range)
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

static bool d3d12_root_signature_may_require_global_heap_binding(void)
{
#ifdef VKD3D_ENABLE_DESCRIPTOR_QA
    /* Expect-assume path always wants to see global heap binding for size query purposes. */
    return true;
#else
    /* Robustness purposes, we may access the global heap out of band of the root signature. */
    return d3d12_descriptor_heap_require_padding_descriptors();
#endif
}

static HRESULT d3d12_root_signature_info_from_desc(struct d3d12_root_signature_info *info,
        struct d3d12_device *device, const D3D12_ROOT_SIGNATURE_DESC2 *desc)
{
    bool local_root_signature;
    unsigned int i, j;
    HRESULT hr;

    memset(info, 0, sizeof(*info));

    local_root_signature = !!(desc->Flags & D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE);

    /* Need to emit bindings for the magic internal table binding. */
    if (d3d12_root_signature_may_require_global_heap_binding() ||
            (desc->Flags & D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED))
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
        /* Make sure that we won't exceed device limits.
         * Minimum spec for push descriptors is 32 descriptors, which fits exactly what we need for D3D12.
         * Worst case scenarios:
         * - 32 root CBVs -> all 32 push descriptors are used. No push constants.
         * - Root constants > 128 bytes, 15 root CBVs. 1 push descriptor for push UBO. Can hoist 16 other descriptors.
         * Just base the amount of descriptors we can hoist on the root signature cost. This is simple and is trivially correct. */
        info->hoist_descriptor_count = min(info->hoist_descriptor_count, VKD3D_MAX_HOISTED_DESCRIPTORS);
        info->hoist_descriptor_count = min(info->hoist_descriptor_count, (D3D12_MAX_ROOT_COST - info->cost) / 2);

        info->push_descriptor_count += info->hoist_descriptor_count;
        info->binding_count += info->hoist_descriptor_count;
        info->binding_count += desc->NumStaticSamplers;

        if (vkd3d_descriptor_debug_active_instruction_qa_checks())
            info->push_descriptor_count += 2;
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
        const D3D12_ROOT_SIGNATURE_DESC2 *desc, const struct d3d12_root_signature_info *info)
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
        const D3D12_ROOT_SIGNATURE_DESC2 *desc, const struct d3d12_root_signature_info *info,
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

    range_flag = vkd3d_bindless_set_flag_from_descriptor_range_type(range_type);
    binding->flags = VKD3D_SHADER_BINDING_FLAG_BINDLESS | VKD3D_SHADER_BINDING_FLAG_AUX_BUFFER;

    if (d3d12_device_use_embedded_mutable_descriptors(root_signature->device) &&
            range_type == D3D12_DESCRIPTOR_RANGE_TYPE_UAV)
    {
        /* If we're relying on embedded mutable descriptors we have to be a bit careful with aliasing raw VAs.
         * With application bugs in play, it's somewhat easy to end up aliasing a true texel buffer
         * descriptor and raw VA descriptor. Avoid this scenario by pretending the AUX_BUFFER texel buffers
         * and normal texel buffers are one and the same. This is robust against many kinds of hypothetical app bugs:
         * - App creates RWStructuredBuffer without counter: Counter will point to base address of the RWStructuredBuffer.
         * - App creates texel buffer: Counter will point to the texel buffer itself.
         * - NULL resource: Implicitly handled without shader magic.
         * - App creates RWStructuredBuffer with counter, app reads as typed buffer: Typed buffer will read from counter. */
        if (vkd3d_bindless_state_find_binding(bindless_state, range_flag | VKD3D_BINDLESS_SET_BUFFER, &binding->binding))
            out_bindings_base[(*out_index)++] = *binding;
    }
    else
    {
        /* Use raw VA for both RTAS and UAV counters. */
        binding->flags |= VKD3D_SHADER_BINDING_FLAG_RAW_VA;
        binding->binding = root_signature->raw_va_aux_buffer_binding;
        out_bindings_base[(*out_index)++] = *binding;
    }

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
        const D3D12_ROOT_SIGNATURE_DESC2 *desc, const struct d3d12_root_signature_info *info,
        struct vkd3d_descriptor_set_context *context)
{
    struct vkd3d_bindless_state *bindless_state = &root_signature->device->bindless_state;
    struct vkd3d_shader_resource_binding binding;
    struct vkd3d_shader_descriptor_table *table;
    unsigned int i, j, t, range_count;
    uint32_t range_descriptor_offset;
    bool local_root_signature;

    local_root_signature = !!(desc->Flags & D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE);

    if (d3d12_root_signature_may_require_global_heap_binding() ||
            (desc->Flags & D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED))
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

static void d3d12_root_signature_add_common_flags(struct d3d12_root_signature *root_signature,
        uint32_t common_flags)
{
    root_signature->graphics.flags |= common_flags;
    root_signature->mesh.flags |= common_flags;
    root_signature->compute.flags |= common_flags;
    root_signature->raygen.flags |= common_flags;
}

static void d3d12_root_signature_init_extra_bindings(struct d3d12_root_signature *root_signature,
        const struct d3d12_root_signature_info *info)
{
    vkd3d_bindless_state_find_binding(&root_signature->device->bindless_state,
            VKD3D_BINDLESS_SET_EXTRA_RAW_VA_AUX_BUFFER,
            &root_signature->raw_va_aux_buffer_binding);

    if (info->has_ssbo_offset_buffer || info->has_typed_offset_buffer)
    {
        if (info->has_ssbo_offset_buffer)
            d3d12_root_signature_add_common_flags(root_signature, VKD3D_ROOT_SIGNATURE_USE_SSBO_OFFSET_BUFFER);
        if (info->has_typed_offset_buffer)
            d3d12_root_signature_add_common_flags(root_signature, VKD3D_ROOT_SIGNATURE_USE_TYPED_OFFSET_BUFFER);

        vkd3d_bindless_state_find_binding(&root_signature->device->bindless_state,
                VKD3D_BINDLESS_SET_EXTRA_OFFSET_BUFFER,
                &root_signature->offset_buffer_binding);
    }

#ifdef VKD3D_ENABLE_DESCRIPTOR_QA
    if (vkd3d_descriptor_debug_active_descriptor_qa_checks())
    {
        vkd3d_bindless_state_find_binding(&root_signature->device->bindless_state,
                VKD3D_BINDLESS_SET_EXTRA_FEEDBACK_CONTROL_INFO_BUFFER,
                &root_signature->descriptor_qa_control_binding);
        vkd3d_bindless_state_find_binding(&root_signature->device->bindless_state,
                VKD3D_BINDLESS_SET_EXTRA_FEEDBACK_PAYLOAD_INFO_BUFFER,
                &root_signature->descriptor_qa_payload_binding);
    }
#endif
}

static HRESULT d3d12_root_signature_init_shader_record_descriptors(
        struct d3d12_root_signature *root_signature,
        const D3D12_ROOT_SIGNATURE_DESC2 *desc, const struct d3d12_root_signature_info *info,
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
        const D3D12_ROOT_SIGNATURE_DESC2 *desc, struct d3d12_root_signature_info *info,
        const VkPushConstantRange *push_constant_range, struct vkd3d_descriptor_set_context *context,
        VkDescriptorSetLayout *vk_set_layout)
{
    VkDescriptorSetLayoutBinding *vk_binding, *vk_binding_info = NULL;
    struct vkd3d_descriptor_hoist_desc *hoist_desc;
    struct vkd3d_shader_resource_binding *binding;
    struct vkd3d_shader_root_parameter *param;
    uint32_t raw_va_root_descriptor_count = 0;
    unsigned int hoisted_parameter_index;
    const D3D12_DESCRIPTOR_RANGE1 *range;
    unsigned int i, j, k;
    HRESULT hr = S_OK;
    uint32_t or_flags;

    or_flags = root_signature->graphics.flags |
            root_signature->compute.flags |
            root_signature->raygen.flags |
            root_signature->mesh.flags;

    if (info->push_descriptor_count || (or_flags & VKD3D_ROOT_SIGNATURE_USE_PUSH_CONSTANT_UNIFORM_BLOCK))
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

    if (or_flags & VKD3D_ROOT_SIGNATURE_USE_PUSH_CONSTANT_UNIFORM_BLOCK)
    {
        vk_binding = &vk_binding_info[j++];
        vk_binding->binding = context->vk_binding;
        vk_binding->descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        vk_binding->descriptorCount = 1;
        vk_binding->stageFlags = VK_SHADER_STAGE_ALL;
        vk_binding->pImmutableSamplers = NULL;

        root_signature->push_constant_ubo_binding.set = context->vk_set;
        root_signature->push_constant_ubo_binding.binding = context->vk_binding;

        context->vk_binding += 1;
    }

#ifdef VKD3D_ENABLE_DESCRIPTOR_QA
    if (vkd3d_descriptor_debug_active_instruction_qa_checks())
    {
        vk_binding = &vk_binding_info[j++];
        vk_binding->binding = context->vk_binding;
        vk_binding->descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        vk_binding->descriptorCount = 1;
        vk_binding->stageFlags = VK_SHADER_STAGE_ALL;
        vk_binding->pImmutableSamplers = NULL;

        root_signature->descriptor_qa_control_binding.set = context->vk_set;
        root_signature->descriptor_qa_control_binding.binding = context->vk_binding;
        context->vk_binding += 1;

        vk_binding = &vk_binding_info[j++];
        vk_binding->binding = context->vk_binding;
        vk_binding->descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        vk_binding->descriptorCount = 1;
        vk_binding->stageFlags = VK_SHADER_STAGE_ALL;
        vk_binding->pImmutableSamplers = NULL;

        root_signature->descriptor_qa_payload_binding.set = context->vk_set;
        root_signature->descriptor_qa_payload_binding.binding = context->vk_binding;
        context->vk_binding += 1;
    }
#endif

    /* This should never happen. Min requirement for push descriptors is 32 and we can always fit into that limit. */
    if (j > root_signature->device->device_info.push_descriptor_properties.maxPushDescriptors)
    {
        ERR("Number of descriptors %u exceeds push descriptor limit of %u.\n",
                j, root_signature->device->device_info.push_descriptor_properties.maxPushDescriptors);
        vkd3d_free(vk_binding_info);
        return E_OUTOFMEMORY;
    }

    if (j)
    {
        hr = vkd3d_create_descriptor_set_layout(root_signature->device,
                VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR,
                j, vk_binding_info,
                VK_DESCRIPTOR_SET_LAYOUT_CREATE_DESCRIPTOR_BUFFER_BIT_EXT,
                vk_set_layout);
    }

    vkd3d_free(vk_binding_info);
    return hr;
}

static HRESULT d3d12_root_signature_init_local_static_samplers(struct d3d12_root_signature *root_signature,
        const D3D12_ROOT_SIGNATURE_DESC2 *desc)
{
    unsigned int i;
    HRESULT hr;

    if (!desc->NumStaticSamplers)
        return S_OK;

    for (i = 0; i < desc->NumStaticSamplers; i++)
    {
        const D3D12_STATIC_SAMPLER_DESC1 *s = &desc->pStaticSamplers[i];
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
        const D3D12_ROOT_SIGNATURE_DESC2 *desc, struct vkd3d_descriptor_set_context *context,
        VkDescriptorSetLayout *vk_set_layout)
{
    VkDescriptorSetLayoutBinding *vk_binding_info, *vk_binding;
    struct vkd3d_shader_resource_binding *binding;
    unsigned int i;
    HRESULT hr;

    if (!desc->NumStaticSamplers)
        return S_OK;

    if (!(vk_binding_info = vkd3d_malloc(desc->NumStaticSamplers * sizeof(*vk_binding_info))))
        return E_OUTOFMEMORY;

    for (i = 0; i < desc->NumStaticSamplers; ++i)
    {
        const D3D12_STATIC_SAMPLER_DESC1 *s = &desc->pStaticSamplers[i];

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
            desc->NumStaticSamplers, vk_binding_info,
            VK_DESCRIPTOR_SET_LAYOUT_CREATE_DESCRIPTOR_BUFFER_BIT_EXT |
                    VK_DESCRIPTOR_SET_LAYOUT_CREATE_EMBEDDED_IMMUTABLE_SAMPLERS_BIT_EXT,
            &root_signature->vk_sampler_descriptor_layout)))
        goto cleanup;

    /* With descriptor buffers we can implicitly bind immutable samplers, and no descriptors are necessary. */
    if (!d3d12_device_uses_descriptor_buffers(root_signature->device))
    {
        hr = vkd3d_sampler_state_allocate_descriptor_set(&root_signature->device->sampler_state,
                root_signature->device, root_signature->vk_sampler_descriptor_layout,
                &root_signature->vk_sampler_set, &root_signature->vk_sampler_pool);
    }
    else
        hr = S_OK;

cleanup:
    vkd3d_free(vk_binding_info);
    return hr;
}

static HRESULT d3d12_root_signature_init_local(struct d3d12_root_signature *root_signature,
        struct d3d12_device *device, const D3D12_ROOT_SIGNATURE_DESC2 *desc)
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

static void d3d12_root_signature_update_bind_point_layout(struct d3d12_bind_point_layout *layout,
        const VkPushConstantRange *push_range, const struct vkd3d_descriptor_set_context *context,
        const struct d3d12_root_signature_info *info)
{
    /* Select push UBO style or push constants on a per-pipeline type basis. */
    if ((layout->flags & VKD3D_ROOT_SIGNATURE_USE_PUSH_CONSTANT_UNIFORM_BLOCK) || info->push_descriptor_count)
        layout->num_set_layouts = context->vk_set;

    if (!(layout->flags & VKD3D_ROOT_SIGNATURE_USE_PUSH_CONSTANT_UNIFORM_BLOCK))
        layout->push_constant_range = *push_range;
}

static HRESULT d3d12_root_signature_init_global(struct d3d12_root_signature *root_signature,
        struct d3d12_device *device, const D3D12_ROOT_SIGNATURE_DESC2 *desc)
{
    const VkPhysicalDeviceProperties *vk_device_properties = &device->device_info.properties2.properties;
    const struct vkd3d_bindless_state *bindless_state = &device->bindless_state;
    struct vkd3d_descriptor_set_context context;
    VkShaderStageFlagBits mesh_shader_stages;
    VkPushConstantRange push_constant_range;
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
            &push_constant_range)))
        return hr;

    /* If we cannot contain the push constants, fall back to push UBO everywhere. */
    if (push_constant_range.size > vk_device_properties->limits.maxPushConstantsSize)
        d3d12_root_signature_add_common_flags(root_signature, VKD3D_ROOT_SIGNATURE_USE_PUSH_CONSTANT_UNIFORM_BLOCK);
    else if (push_constant_range.size && (device->bindless_state.flags & VKD3D_FORCE_COMPUTE_ROOT_PARAMETERS_PUSH_UBO))
        root_signature->compute.flags |= VKD3D_ROOT_SIGNATURE_USE_PUSH_CONSTANT_UNIFORM_BLOCK;

    d3d12_root_signature_init_extra_bindings(root_signature, &info);

    /* Individual pipeline types may opt-in or out-of using the push UBO descriptor set. */
    root_signature->graphics.num_set_layouts = context.vk_set;
    root_signature->mesh.num_set_layouts = context.vk_set;
    root_signature->compute.num_set_layouts = context.vk_set;
    root_signature->raygen.num_set_layouts = context.vk_set;

    if (FAILED(hr = d3d12_root_signature_init_root_descriptors(root_signature, desc,
                &info, &push_constant_range, &context,
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

    /* Select push UBO style or push constants on a per-pipeline type basis. */
    d3d12_root_signature_update_bind_point_layout(&root_signature->graphics,
            &push_constant_range, &context, &info);
    d3d12_root_signature_update_bind_point_layout(&root_signature->mesh,
            &push_constant_range, &context, &info);
    d3d12_root_signature_update_bind_point_layout(&root_signature->compute,
            &push_constant_range, &context, &info);
    d3d12_root_signature_update_bind_point_layout(&root_signature->raygen,
            &push_constant_range, &context, &info);

    /* If we need to use restricted entry_points in vkCmdPushConstants,
     * we are unfortunately required to do it like this
     * since stageFlags in vkCmdPushConstants must cover at least all entry_points in the layout.
     *
     * We can pick the appropriate layout to use in PSO creation.
     * In set_root_signature we can bind the appropriate layout as well.
     *
     * For graphics we can generally rely on visibility mask, but not so for compute and raygen,
     * since they use ALL visibility. */

    if (FAILED(hr = vkd3d_create_pipeline_layout_for_stage_mask(
            device, root_signature->graphics.num_set_layouts, root_signature->set_layouts,
            &root_signature->graphics.push_constant_range,
            VK_SHADER_STAGE_ALL_GRAPHICS, &root_signature->graphics)))
        return hr;

    if (device->device_info.mesh_shader_features.meshShader && device->device_info.mesh_shader_features.taskShader)
    {
        mesh_shader_stages = VK_SHADER_STAGE_MESH_BIT_EXT |
                VK_SHADER_STAGE_TASK_BIT_EXT |
                VK_SHADER_STAGE_FRAGMENT_BIT;

        if (FAILED(hr = vkd3d_create_pipeline_layout_for_stage_mask(
                device, root_signature->mesh.num_set_layouts, root_signature->set_layouts,
                &root_signature->mesh.push_constant_range,
                mesh_shader_stages, &root_signature->mesh)))
            return hr;
    }

    if (FAILED(hr = vkd3d_create_pipeline_layout_for_stage_mask(
            device, root_signature->compute.num_set_layouts, root_signature->set_layouts,
            &root_signature->compute.push_constant_range,
            VK_SHADER_STAGE_COMPUTE_BIT, &root_signature->compute)))
        return hr;

    if (d3d12_device_supports_ray_tracing_tier_1_0(device))
    {
        if (FAILED(hr = vkd3d_create_pipeline_layout_for_stage_mask(
                device, root_signature->raygen.num_set_layouts, root_signature->set_layouts,
                &root_signature->raygen.push_constant_range,
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

    memcpy(set_layouts, root_signature->set_layouts, root_signature->raygen.num_set_layouts * sizeof(VkDescriptorSetLayout));
    set_layouts[root_signature->raygen.num_set_layouts] = vk_set_layout;

    if (FAILED(hr = vkd3d_create_pipeline_layout_for_stage_mask(
            root_signature->device, root_signature->raygen.num_set_layouts + 1, set_layouts,
            &root_signature->raygen.push_constant_range,
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

HRESULT d3d12_root_signature_create_work_graph_layout(struct d3d12_root_signature *root_signature,
        VkDescriptorSetLayout *vk_push_set_layout, VkPipelineLayout *vk_pipeline_layout)
{
    VkDescriptorSetLayout set_layouts[VKD3D_MAX_DESCRIPTOR_SETS];
    struct d3d12_bind_point_layout bind_point_layout;
    VkDescriptorSetLayoutBinding binding;
    VkPushConstantRange range;
    bool uses_push_ubo;
    HRESULT hr;

    /* If we're already using push UBO block, we just need to modify the push range. */
    /* TODO: Local sampler set. */
    uses_push_ubo = !!(root_signature->compute.flags & VKD3D_ROOT_SIGNATURE_USE_PUSH_CONSTANT_UNIFORM_BLOCK);
    range.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    range.offset = 0;
    range.size = sizeof(struct vkd3d_shader_node_input_push_signature);

    if (root_signature->root_descriptor_push_mask)
    {
        FIXME("The root signature is already using push descriptors, cannot add another push descriptor set. Make sure to use VKD3D_CONFIG=force_raw_va_cbv on NVIDIA.\n");
        return E_INVALIDARG;
    }

    if (uses_push_ubo || root_signature->compute.push_constant_range.size == 0)
    {
        if (FAILED(hr = vkd3d_create_pipeline_layout_for_stage_mask(
                root_signature->device, root_signature->compute.num_set_layouts, root_signature->set_layouts,
                &range, VK_SHADER_STAGE_COMPUTE_BIT, &bind_point_layout)))
            return hr;

        *vk_push_set_layout = VK_NULL_HANDLE;
        *vk_pipeline_layout = bind_point_layout.vk_pipeline_layout;
    }
    else
    {
        binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        binding.descriptorCount = 1;
        binding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        binding.binding = 0;
        binding.pImmutableSamplers = NULL;

        memcpy(set_layouts, root_signature->set_layouts,
                root_signature->compute.num_set_layouts * sizeof(VkDescriptorSetLayout));

        if (FAILED(hr = vkd3d_create_descriptor_set_layout(
                root_signature->device, VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR,
                1, &binding,
                VK_DESCRIPTOR_SET_LAYOUT_CREATE_DESCRIPTOR_BUFFER_BIT_EXT, vk_push_set_layout)))
            return hr;

        set_layouts[root_signature->compute.num_set_layouts] = *vk_push_set_layout;

        if (FAILED(hr = vkd3d_create_pipeline_layout_for_stage_mask(
                root_signature->device, root_signature->compute.num_set_layouts + 1, set_layouts,
                &range, VK_SHADER_STAGE_COMPUTE_BIT, &bind_point_layout)))
            return hr;

        *vk_pipeline_layout = bind_point_layout.vk_pipeline_layout;
    }

    return S_OK;
}

static HRESULT d3d12_root_signature_init(struct d3d12_root_signature *root_signature,
        struct d3d12_device *device, const D3D12_ROOT_SIGNATURE_DESC2 *desc)
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
    D3D12_ROOT_SIGNATURE_DESC2 desc;
    HRESULT hr;

    if (!(object = vkd3d_malloc(sizeof(*object))))
        return E_OUTOFMEMORY;

    memset(&desc, 0, sizeof(desc));
    hr = d3d12_root_signature_init(object, device, &desc);

    /* For pipeline libraries, (and later DXR to some degree), we need a way to
     * compare root signature objects. */
    object->pso_compatibility_hash = 0;
    object->layout_compatibility_hash = 0;

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
        if ((ret = vkd3d_shader_parse_root_signature_v_1_2_from_raw_payload(&dxbc, &root_signature_desc.vkd3d,
                &compatibility_hash)))
        {
            WARN("Failed to parse root signature, vkd3d result %d.\n", ret);
            return hresult_from_vkd3d_result(ret);
        }
    }
    else
    {
        if ((ret = vkd3d_shader_parse_root_signature_v_1_2(&dxbc, &root_signature_desc.vkd3d, &compatibility_hash)) < 0)
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

    hr = d3d12_root_signature_init(object, device, &root_signature_desc.d3d12.Desc_1_2);

    /* For pipeline libraries, (and later DXR to some degree), we need a way to
     * compare root signature objects. */
    object->pso_compatibility_hash = compatibility_hash;
    object->layout_compatibility_hash = vkd3d_root_signature_v_1_2_compute_layout_compat_hash(
            &root_signature_desc.vkd3d.v_1_2);

    vkd3d_shader_free_root_signature(&root_signature_desc.vkd3d);
    if (FAILED(hr))
    {
        vkd3d_free(object);
        return hr;
    }

    d3d_destruction_notifier_init(&object->destruction_notifier, (IUnknown*)&object->ID3D12RootSignature_iface);

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

unsigned int d3d12_root_signature_get_shader_interface_flags(const struct d3d12_root_signature *root_signature,
        enum vkd3d_pipeline_type pipeline_type)
{
    const struct d3d12_bind_point_layout *layout;
    unsigned int flags = 0;

    layout = d3d12_root_signature_get_layout(root_signature, pipeline_type);

    if (layout->flags & VKD3D_ROOT_SIGNATURE_USE_PUSH_CONSTANT_UNIFORM_BLOCK)
        flags |= VKD3D_SHADER_INTERFACE_PUSH_CONSTANTS_AS_UNIFORM_BUFFER;

    if (layout->flags & VKD3D_ROOT_SIGNATURE_USE_SSBO_OFFSET_BUFFER)
        flags |= VKD3D_SHADER_INTERFACE_SSBO_OFFSET_BUFFER;
    if (layout->flags & VKD3D_ROOT_SIGNATURE_USE_TYPED_OFFSET_BUFFER)
        flags |= VKD3D_SHADER_INTERFACE_TYPED_OFFSET_BUFFER;

    if (root_signature->device->bindless_state.flags & VKD3D_BINDLESS_CBV_AS_SSBO)
        flags |= VKD3D_SHADER_INTERFACE_BINDLESS_CBV_AS_STORAGE_BUFFER;
    if (d3d12_device_use_embedded_mutable_descriptors(root_signature->device))
        flags |= VKD3D_SHADER_INTERFACE_RAW_VA_ALIAS_DESCRIPTOR_BUFFER;

    return flags;
}

static D3D12_LINE_RASTERIZATION_MODE d3d12_line_rasteriztion_mode_from_legacy_state(BOOL MultisampleEnable, BOOL AntialiasedLineEnable)
{
    if (MultisampleEnable)
        return D3D12_LINE_RASTERIZATION_MODE_QUADRILATERAL_WIDE;

    if (AntialiasedLineEnable)
        return D3D12_LINE_RASTERIZATION_MODE_ALPHA_ANTIALIASED;

    return D3D12_LINE_RASTERIZATION_MODE_ALIASED;
}

static void d3d12_promote_rasterizer_desc(D3D12_RASTERIZER_DESC2 *out, const D3D12_RASTERIZER_DESC *in)
{
    out->FillMode = in->FillMode;
    out->CullMode = in->CullMode;
    out->FrontCounterClockwise = in->FrontCounterClockwise;
    out->DepthBias = (FLOAT)in->DepthBias;
    out->DepthBiasClamp = in->DepthBiasClamp;
    out->SlopeScaledDepthBias = in->SlopeScaledDepthBias;
    out->DepthClipEnable = in->DepthClipEnable;
    out->LineRasterizationMode = d3d12_line_rasteriztion_mode_from_legacy_state(
            in->MultisampleEnable, in->AntialiasedLineEnable);
    out->ForcedSampleCount = in->ForcedSampleCount;
    out->ConservativeRaster = in->ConservativeRaster;
}

static void d3d12_promote_rasterizer_desc1(D3D12_RASTERIZER_DESC2 *out, const D3D12_RASTERIZER_DESC1 *in)
{
    out->FillMode = in->FillMode;
    out->CullMode = in->CullMode;
    out->FrontCounterClockwise = in->FrontCounterClockwise;
    out->DepthBias = in->DepthBias;
    out->DepthBiasClamp = in->DepthBiasClamp;
    out->SlopeScaledDepthBias = in->SlopeScaledDepthBias;
    out->DepthClipEnable = in->DepthClipEnable;
    out->LineRasterizationMode = d3d12_line_rasteriztion_mode_from_legacy_state(
            in->MultisampleEnable, in->AntialiasedLineEnable);
    out->ForcedSampleCount = in->ForcedSampleCount;
    out->ConservativeRaster = in->ConservativeRaster;
}

static void d3d12_promote_depth_stencil_desc(D3D12_DEPTH_STENCIL_DESC2 *out, const D3D12_DEPTH_STENCIL_DESC *in)
{
    out->DepthEnable = in->DepthEnable;
    out->DepthWriteMask = in->DepthWriteMask;
    out->DepthFunc = in->DepthFunc;
    out->StencilEnable = in->StencilEnable;

    out->FrontFace.StencilFailOp = in->FrontFace.StencilFailOp;
    out->FrontFace.StencilDepthFailOp = in->FrontFace.StencilDepthFailOp;
    out->FrontFace.StencilPassOp = in->FrontFace.StencilPassOp;
    out->FrontFace.StencilFunc = in->FrontFace.StencilFunc;
    out->FrontFace.StencilReadMask = in->StencilReadMask;
    out->FrontFace.StencilWriteMask = in->StencilWriteMask;

    out->BackFace.StencilFailOp = in->BackFace.StencilFailOp;
    out->BackFace.StencilDepthFailOp = in->BackFace.StencilDepthFailOp;
    out->BackFace.StencilPassOp = in->BackFace.StencilPassOp;
    out->BackFace.StencilFunc = in->BackFace.StencilFunc;
    out->BackFace.StencilReadMask = in->StencilReadMask;
    out->BackFace.StencilWriteMask = in->StencilWriteMask;

    out->DepthBoundsTestEnable = FALSE;
}

static void d3d12_promote_depth_stencil_desc1(D3D12_DEPTH_STENCIL_DESC2 *out, const D3D12_DEPTH_STENCIL_DESC1 *in)
{
    out->DepthEnable = in->DepthEnable;
    out->DepthWriteMask = in->DepthWriteMask;
    out->DepthFunc = in->DepthFunc;
    out->StencilEnable = in->StencilEnable;

    out->FrontFace.StencilFailOp = in->FrontFace.StencilFailOp;
    out->FrontFace.StencilDepthFailOp = in->FrontFace.StencilDepthFailOp;
    out->FrontFace.StencilPassOp = in->FrontFace.StencilPassOp;
    out->FrontFace.StencilFunc = in->FrontFace.StencilFunc;
    out->FrontFace.StencilReadMask = in->StencilReadMask;
    out->FrontFace.StencilWriteMask = in->StencilWriteMask;

    out->BackFace.StencilFailOp = in->BackFace.StencilFailOp;
    out->BackFace.StencilDepthFailOp = in->BackFace.StencilDepthFailOp;
    out->BackFace.StencilPassOp = in->BackFace.StencilPassOp;
    out->BackFace.StencilFunc = in->BackFace.StencilFunc;
    out->BackFace.StencilReadMask = in->StencilReadMask;
    out->BackFace.StencilWriteMask = in->StencilWriteMask;

    out->DepthBoundsTestEnable = in->DepthBoundsTestEnable;
}

static void d3d12_init_pipeline_state_desc(struct d3d12_pipeline_state_desc *desc)
{
    D3D12_DEPTH_STENCIL_DESC2 *ds_state = &desc->depth_stencil_state;
    D3D12_RASTERIZER_DESC2 *rs_state = &desc->rasterizer_state;
    D3D12_BLEND_DESC *blend_state = &desc->blend_state;
    DXGI_SAMPLE_DESC *sample_desc = &desc->sample_desc;

    memset(desc, 0, sizeof(*desc));
    ds_state->DepthEnable = TRUE;
    ds_state->DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    ds_state->DepthFunc = D3D12_COMPARISON_FUNC_LESS;
    ds_state->FrontFace.StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    ds_state->FrontFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
    ds_state->FrontFace.StencilPassOp = D3D12_STENCIL_OP_KEEP;
    ds_state->FrontFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
    ds_state->FrontFace.StencilReadMask = D3D12_DEFAULT_STENCIL_READ_MASK;
    ds_state->FrontFace.StencilWriteMask = D3D12_DEFAULT_STENCIL_WRITE_MASK;
    ds_state->BackFace = ds_state->FrontFace;

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
    d3d12_promote_rasterizer_desc(&desc->rasterizer_state, &d3d12_desc->RasterizerState);
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

    /* If we use rasterizer discard, force fragment shader to not exist.
     * Required for VUID 06894. */
    if (desc->stream_output.NumEntries &&
            desc->stream_output.RasterizedStream == D3D12_SO_NO_RASTERIZED_STREAM)
    {
        result &= ~VK_SHADER_STAGE_FRAGMENT_BIT;
    }

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
            VKD3D_HANDLE_SUBOBJECT_EXPLICIT(RASTERIZER, D3D12_RASTERIZER_DESC,
                    d3d12_promote_rasterizer_desc(&desc->rasterizer_state, &subobject->data));
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
            VKD3D_HANDLE_SUBOBJECT_EXPLICIT(DEPTH_STENCIL1, D3D12_DEPTH_STENCIL_DESC1,
                    d3d12_promote_depth_stencil_desc1(&desc->depth_stencil_state, &subobject->data));
            VKD3D_HANDLE_SUBOBJECT(VIEW_INSTANCING, D3D12_VIEW_INSTANCING_DESC, desc->view_instancing_desc);
            VKD3D_HANDLE_SUBOBJECT(DEPTH_STENCIL2, D3D12_DEPTH_STENCIL_DESC2, desc->depth_stencil_state);
            VKD3D_HANDLE_SUBOBJECT_EXPLICIT(RASTERIZER1, D3D12_RASTERIZER_DESC1,
                    d3d12_promote_rasterizer_desc1(&desc->rasterizer_state, &subobject->data));
            VKD3D_HANDLE_SUBOBJECT(RASTERIZER2, D3D12_RASTERIZER_DESC2, desc->rasterizer_state);

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
    struct d3d12_pipeline_state *state = impl_from_ID3D12PipelineState(iface);

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

    if (IsEqualGUID(riid, &IID_ID3DDestructionNotifier))
    {
        ID3DDestructionNotifier_AddRef(&state->destruction_notifier.ID3DDestructionNotifier_iface);
        *object = &state->destruction_notifier.ID3DDestructionNotifier_iface;
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

HRESULT d3d12_pipeline_state_create_shader_module(struct d3d12_device *device,
        VkShaderModule *vk_module, const struct vkd3d_shader_code *code)
{
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    VkShaderModuleCreateInfo shader_desc;
    VkResult vr;

    shader_desc.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    shader_desc.pNext = NULL;
    shader_desc.flags = 0;
    shader_desc.codeSize = code->size;
    shader_desc.pCode = code->code;

    vr = VK_CALL(vkCreateShaderModule(device->vk_device, &shader_desc, NULL, vk_module));
    if (vr < 0)
    {
        WARN("Failed to create Vulkan shader module, vr %d.\n", vr);
        return hresult_from_vk_result(vr);
    }

    if (vkd3d_config_flags & VKD3D_CONFIG_FLAG_DEBUG_UTILS)
    {
        /* Helpful for tooling like RenderDoc. */
        char hash_str[16 + 1];
        sprintf(hash_str, "%016"PRIx64, code->meta.hash);
        vkd3d_set_vk_object_name(device, (uint64_t)*vk_module, VK_OBJECT_TYPE_SHADER_MODULE, hash_str);
    }

    return S_OK;
}

static void d3d12_pipeline_state_free_spirv_code_debug(struct d3d12_pipeline_state *state)
{
    unsigned int i;
    if (d3d12_pipeline_state_is_graphics(state))
    {
        for (i = 0; i < state->graphics.stage_count; i++)
            vkd3d_shader_free_shader_code_debug(&state->graphics.code_debug[i]);
    }
    else if (d3d12_pipeline_state_is_compute(state))
        vkd3d_shader_free_shader_code_debug(&state->compute.code_debug);
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
    VK_CALL(vkDestroyPipeline(device->vk_device, graphics->library, NULL));
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

    new_buffer_strides = vkd3d_malloc(so_desc->NumStrides * sizeof(*new_buffer_strides));
    if (!new_buffer_strides)
        goto fail;
    memcpy(new_buffer_strides, so_desc->pBufferStrides, so_desc->NumStrides * sizeof(*new_buffer_strides));
    xfb_info->buffer_strides = new_buffer_strides;

    new_entries = vkd3d_malloc(so_desc->NumEntries * sizeof(*new_entries));
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
        d3d_destruction_notifier_free(&state->destruction_notifier);
        vkd3d_private_store_destroy(&state->private_store);

        d3d12_pipeline_state_free_spirv_code(state);
        d3d12_pipeline_state_free_spirv_code_debug(state);
        if (d3d12_pipeline_state_is_graphics(state))
            d3d12_pipeline_state_destroy_graphics(state, device);
        else if (d3d12_pipeline_state_is_compute(state))
            VK_CALL(vkDestroyPipeline(device->vk_device, state->compute.vk_pipeline, NULL));

        VK_CALL(vkDestroyPipelineCache(device->vk_device, state->vk_pso_cache, NULL));

        if (state->root_signature)
            d3d12_root_signature_dec_ref(state->root_signature);

        if (state->pipeline_type == VKD3D_PIPELINE_TYPE_GRAPHICS || state->pipeline_type == VKD3D_PIPELINE_TYPE_MESH_GRAPHICS)
            d3d12_pipeline_state_free_cached_desc(&state->graphics.cached_desc);
        rwlock_destroy(&state->lock);
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
        d3d_destruction_notifier_notify(&state->destruction_notifier);

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

    if (!(cache_data = vkd3d_malloc(cache_size)))
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
        ERR("Failed to create blob, hr %#x.\n", hr);
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
        VkShaderStageFlagBits stage, struct vkd3d_shader_code *spirv_code,
        VkPipelineShaderStageModuleIdentifierCreateInfoEXT *identifier)
{
    HRESULT hr;

    identifier->sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_MODULE_IDENTIFIER_CREATE_INFO_EXT;
    identifier->pNext = NULL;
    identifier->identifierSize = 0;
    identifier->pIdentifier = NULL;

    if (!cached_state->blob.CachedBlobSizeInBytes)
    {
        if (vkd3d_config_flags & VKD3D_CONFIG_FLAG_PIPELINE_LIBRARY_LOG)
            INFO("SPIR-V chunk was not found due to no Cached PSO state being provided.\n");
        return E_FAIL;
    }

    if (vkd3d_config_flags & VKD3D_CONFIG_FLAG_PIPELINE_LIBRARY_IGNORE_SPIRV)
        return E_FAIL;

    hr = vkd3d_get_cached_spirv_code_from_d3d12_desc(cached_state, stage, spirv_code, identifier);

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
    shader_interface->flags = d3d12_root_signature_get_shader_interface_flags(root_signature, state->pipeline_type);
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
    shader_interface->descriptor_size_cbv_srv_uav = d3d12_device_get_descriptor_handle_increment_size(
            device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    shader_interface->descriptor_size_sampler = d3d12_device_get_descriptor_handle_increment_size(
            device, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);

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
    shader_interface->descriptor_qa_payload_binding = &root_signature->descriptor_qa_payload_binding;
    shader_interface->descriptor_qa_control_binding = &root_signature->descriptor_qa_control_binding;
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
    compile_arguments->min_subgroup_size = device->device_info.vulkan_1_3_properties.minSubgroupSize;
    compile_arguments->max_subgroup_size = device->device_info.vulkan_1_3_properties.maxSubgroupSize;
    compile_arguments->promote_wave_size_heuristics =
            d3d12_device_supports_required_subgroup_size_for_stage(device, stage);
    compile_arguments->quirks = &vkd3d_shader_quirk_info;

    if (vkd3d_config_flags & VKD3D_CONFIG_FLAG_DRIVER_VERSION_SENSITIVE_SHADERS)
    {
        compile_arguments->driver_id = device->device_info.vulkan_1_2_properties.driverID;
        compile_arguments->driver_version = device->device_info.properties2.properties.driverVersion;
    }

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

static HRESULT vkd3d_setup_shader_stage(struct d3d12_pipeline_state *state, struct d3d12_device *device,
        VkPipelineShaderStageCreateInfo *stage_desc, VkShaderStageFlagBits stage,
        VkPipelineShaderStageRequiredSubgroupSizeCreateInfo *required_subgroup_size_info,
        const VkPipelineShaderStageModuleIdentifierCreateInfoEXT *identifier_create_info,
        const struct vkd3d_shader_code *spirv_code)
{
    bool override_subgroup_size = false;

    stage_desc->sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stage_desc->pNext = NULL;
    stage_desc->flags = 0;
    stage_desc->stage = stage;
    stage_desc->pName = "main";
    stage_desc->pSpecializationInfo = NULL;
    stage_desc->module = VK_NULL_HANDLE;

    if (!d3d12_device_validate_shader_meta(device, &spirv_code->meta))
        return E_INVALIDARG;

    if (!spirv_code->size && identifier_create_info && identifier_create_info->identifierSize)
        stage_desc->pNext = identifier_create_info;

    if ((spirv_code->meta.flags & VKD3D_SHADER_META_FLAG_USES_SUBGROUP_OPERATIONS) ||
            spirv_code->meta.cs_wave_size_min)
    {
        uint32_t subgroup_size_alignment = device->device_info.vulkan_1_3_properties.maxSubgroupSize;
        stage_desc->flags |= VK_PIPELINE_SHADER_STAGE_CREATE_ALLOW_VARYING_SUBGROUP_SIZE_BIT;

        if (required_subgroup_size_info)
        {
            required_subgroup_size_info->sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_REQUIRED_SUBGROUP_SIZE_CREATE_INFO;
            required_subgroup_size_info->pNext = (void*)stage_desc->pNext;

            /* [WaveSize(min,max,preferred)] in SM 6.8, or [WaveSize(N)] in SM 6.6. */
            if (spirv_code->meta.cs_wave_size_preferred &&
                    spirv_code->meta.cs_wave_size_preferred >= device->device_info.vulkan_1_3_properties.minSubgroupSize &&
                    spirv_code->meta.cs_wave_size_preferred <= device->device_info.vulkan_1_3_properties.maxSubgroupSize)
            {
                /* Preferred wave size is non-zero and is supported by the device. */
                subgroup_size_alignment = spirv_code->meta.cs_wave_size_preferred;
                override_subgroup_size = true;
            }
            else if (spirv_code->meta.cs_wave_size_min && (
                    spirv_code->meta.cs_wave_size_min > device->device_info.vulkan_1_3_properties.minSubgroupSize ||
                    spirv_code->meta.cs_wave_size_max < device->device_info.vulkan_1_3_properties.maxSubgroupSize))
            {
                /* We generally want to let the driver decide on a subgroup size, but if the device supports
                 * subgroup sizes outside the range required by the shader, we need to specify it manually.
                 * For now, prefer small subgroups on Intel and large subgroups on AMD for performance. */
                if (device->device_info.properties2.properties.vendorID == VKD3D_VENDOR_ID_INTEL)
                {
                    subgroup_size_alignment = max(spirv_code->meta.cs_wave_size_min,
                            device->device_info.vulkan_1_3_properties.minSubgroupSize);
                }
                else
                {
                    subgroup_size_alignment = min(spirv_code->meta.cs_wave_size_max,
                            device->device_info.vulkan_1_3_properties.maxSubgroupSize);
                }

                override_subgroup_size = true;
            }
            else if ((vkd3d_config_flags & VKD3D_CONFIG_FLAG_FORCE_MINIMUM_SUBGROUP_SIZE) &&
                    d3d12_device_supports_required_subgroup_size_for_stage(device, stage))
            {
                /* GravityMark checks minSubgroupSize and based on that uses a shader variant.
                 * This shader variant unfortunately expects that a subgroup 32 variant will actually use wave32 on AMD.
                 * amdgpu-pro and AMDVLK happens to emit wave32, but RADV will emit wave64 here unless we force it to be wave32.
                 * This is an application bug, since the shader is not guaranteed a specific size, but we can only workaround ... */
                subgroup_size_alignment = device->device_info.vulkan_1_3_properties.minSubgroupSize;
                override_subgroup_size = true;
            }

            required_subgroup_size_info->requiredSubgroupSize = subgroup_size_alignment;
        }

        /* If we can, we should be explicit and enable FULL_SUBGROUPS bit as well. This should be default
         * behavior, but cannot hurt. */
        if (stage == VK_SHADER_STAGE_COMPUTE_BIT &&
                !(spirv_code->meta.cs_workgroup_size[0] % subgroup_size_alignment))
        {
            stage_desc->flags |= VK_PIPELINE_SHADER_STAGE_CREATE_REQUIRE_FULL_SUBGROUPS_BIT;
        }

        if (override_subgroup_size)
        {
            /* If min == max, we can still support WaveSize in a dummy kind of way. */
            if (device->device_info.vulkan_1_3_properties.requiredSubgroupSizeStages & stage)
                stage_desc->pNext = required_subgroup_size_info;

            stage_desc->flags &= ~VK_PIPELINE_SHADER_STAGE_CREATE_ALLOW_VARYING_SUBGROUP_SIZE_BIT;
        }
    }

    if (spirv_code->size)
        return d3d12_pipeline_state_create_shader_module(device, &stage_desc->module, spirv_code);
    else
        return S_OK;
}

static HRESULT vkd3d_compile_shader_stage(struct d3d12_pipeline_state *state, struct d3d12_device *device,
        VkShaderStageFlagBits stage, const D3D12_SHADER_BYTECODE *code,
        struct vkd3d_shader_code *spirv_code, struct vkd3d_shader_code_debug *spirv_code_debug)
{
    struct vkd3d_shader_code dxbc = {code->pShaderBytecode, code->BytecodeLength};
    struct vkd3d_shader_interface_info shader_interface;
    struct vkd3d_shader_compile_arguments compile_args;
    vkd3d_shader_hash_t recovered_hash = 0;
    vkd3d_shader_hash_t compiled_hash = 0;
    int ret;

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
        shader_interface.flags |= vkd3d_descriptor_debug_get_shader_interface_flags(
                device->descriptor_qa_global_info, dxbc.code, dxbc.size);
        d3d12_pipeline_state_init_compile_arguments(state, device, stage, &compile_args);

        if ((ret = vkd3d_shader_compile_dxbc(&dxbc, spirv_code, spirv_code_debug,
                0, &shader_interface, &compile_args)) < 0)
        {
            WARN("Failed to compile shader, vkd3d result %d.\n", ret);
            return hresult_from_vkd3d_result(ret);
        }
        TRACE("Called vkd3d_shader_compile_dxbc.\n");

        if (stage == VK_SHADER_STAGE_FRAGMENT_BIT)
        {
            /* At this point we don't need the map anymore. */
            vkd3d_shader_stage_io_map_free(&state->graphics.cached_desc.stage_io_map_ms_ps);
        }
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

    return S_OK;
}

static bool vkd3d_shader_stages_require_work_locked(struct d3d12_pipeline_state *state)
{
    struct d3d12_graphics_pipeline_state *graphics = &state->graphics;
    unsigned int i;

    for (i = 0; i < graphics->stage_count; i++)
    {
        if (!graphics->code[i].size && graphics->stages[i].module == VK_NULL_HANDLE &&
                graphics->cached_desc.bytecode[i].BytecodeLength)
        {
            return true;
        }
    }

    return false;
}

static void vkd3d_shader_code_init_empty_fs(struct vkd3d_shader_code *code)
{
#include <fs_empty.h>
    void *tmp;

    memset(&code->meta, 0, sizeof(code->meta));
    code->size = sizeof(fs_empty);
    tmp = vkd3d_malloc(sizeof(fs_empty));
    memcpy(tmp, fs_empty, sizeof(fs_empty));
    code->code = tmp;
}

static HRESULT vkd3d_late_compile_shader_stages(struct d3d12_pipeline_state *state)
{
    /* We are at risk of having to compile pipelines late if we return from CreatePipelineState without
     * either code[i] or module being non-null. */
    struct d3d12_graphics_pipeline_state *graphics = &state->graphics;
    bool need_compile;
    unsigned int i;
    HRESULT hr;

    rwlock_lock_read(&state->lock);
    need_compile = vkd3d_shader_stages_require_work_locked(state);
    rwlock_unlock_read(&state->lock);

    if (!need_compile)
        return S_OK;

    /* Taking a writer lock here is kinda horrible,
     * but we really shouldn't hit this path except in extreme circumstances. */
    hr = S_OK;
    rwlock_lock_write(&state->lock);

    /* Need to verify that need_compile did not change between unlocking reader and locking writer. */
    need_compile = vkd3d_shader_stages_require_work_locked(state);
    if (!need_compile)
        goto early_out;

    for (i = 0; i < graphics->stage_count; i++)
    {
        if (graphics->stages[i].module == VK_NULL_HANDLE && !graphics->code[i].size)
        {
            if (graphics->cached_desc.bytecode[i].BytecodeLength)
            {
                /* If we're compiling late, we don't care about debug. Debug capturing disables module identifiers. */
                if (FAILED(hr = vkd3d_compile_shader_stage(state, state->device,
                        graphics->cached_desc.bytecode_stages[i],
                        &graphics->cached_desc.bytecode[i], &graphics->code[i], NULL)))
                    break;
            }
            else if (graphics->cached_desc.bytecode_stages[i] == VK_SHADER_STAGE_FRAGMENT_BIT)
            {
                vkd3d_shader_code_init_empty_fs(&graphics->code[i]);
            }
            else
            {
                hr = E_INVALIDARG;
                break;
            }
        }

        if (graphics->stages[i].module == VK_NULL_HANDLE)
        {
            if (FAILED(hr = d3d12_pipeline_state_create_shader_module(state->device,
                    &graphics->stages[i].module,
                    &graphics->code[i])))
            {
                break;
            }

            /* When a shader module is specified, VkPipelineShaderStageModuleIdentifierCreateInfoEXT must have an
             * identifierSize of 0 or be absent. */
            vk_remove_struct(&graphics->stages[i],
                    VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_MODULE_IDENTIFIER_CREATE_INFO_EXT);
        }

        /* We'll keep the module around here, no need to keep code/size pairs around for this.
         * If we're in a situation where late compile is relevant, we're using PSO cached blobs,
         * so we never expect to serialize out SPIR-V either way. */
        vkd3d_shader_free_shader_code(&graphics->code[i]);
        graphics->code[i].code = NULL;
        graphics->code[i].size = 0;

        /* Don't need the DXBC blob anymore either. */
        if (graphics->cached_desc.bytecode_duped_mask & (1u << i))
        {
            vkd3d_free((void*)graphics->cached_desc.bytecode[i].pShaderBytecode);
            memset(&graphics->cached_desc.bytecode[i], 0, sizeof(graphics->cached_desc.bytecode[i]));
            graphics->cached_desc.bytecode_duped_mask &= ~(1u << i);
        }
    }

early_out:
    rwlock_unlock_write(&state->lock);
    return hr;
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
    VkPipelineShaderStageRequiredSubgroupSizeCreateInfo required_subgroup_size_info;
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    VkPipelineCreationFeedbackCreateInfo feedback_info;
    struct vkd3d_shader_debug_ring_spec_info spec_info;
    struct vkd3d_shader_code_debug *spirv_code_debug;
    struct vkd3d_queue_timeline_trace_cookie cookie;
    VkPipelineCreationFeedbackEXT feedbacks[1];
    VkComputePipelineCreateInfo pipeline_info;
    VkPipelineCreationFeedbackEXT feedback;
    struct vkd3d_shader_code *spirv_code;
    VkPipelineCache vk_cache;
    VkResult vr;
    HRESULT hr;

    vk_cache = state->vk_pso_cache;
    spirv_code = &state->compute.code;

    if (vkd3d_config_flags & VKD3D_CONFIG_FLAG_DEBUG_UTILS)
        spirv_code_debug = &state->compute.code_debug;
    else
        spirv_code_debug = NULL;

    pipeline_info.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipeline_info.pNext = NULL;
    pipeline_info.flags = 0;

    if (state->compute.identifier_create_info.identifierSize == 0)
    {
        if (FAILED(hr = vkd3d_compile_shader_stage(state, device,
                VK_SHADER_STAGE_COMPUTE_BIT, code, spirv_code, spirv_code_debug)))
            return hr;
    }

    if (FAILED(hr = vkd3d_setup_shader_stage(state, device,
            &pipeline_info.stage, VK_SHADER_STAGE_COMPUTE_BIT,
            &required_subgroup_size_info,
            &state->compute.identifier_create_info,
            spirv_code)))
        return hr;

    if (pipeline_info.stage.module != VK_NULL_HANDLE &&
            device->device_info.shader_module_identifier_features.shaderModuleIdentifier)
    {
        state->compute.identifier.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_IDENTIFIER_EXT;
        state->compute.identifier.pNext = NULL;
        VK_CALL(vkGetShaderModuleIdentifierEXT(device->vk_device, pipeline_info.stage.module,
                &state->compute.identifier));
    }

    pipeline_info.layout = state->root_signature->compute.vk_pipeline_layout;
    pipeline_info.basePipelineHandle = VK_NULL_HANDLE;
    pipeline_info.basePipelineIndex = -1;

    if ((spirv_code->meta.flags & VKD3D_SHADER_META_FLAG_REPLACED) && device->debug_ring.active)
    {
        vkd3d_shader_debug_ring_init_spec_constant(device, &spec_info, spirv_code->meta.hash);
        pipeline_info.stage.pSpecializationInfo = &spec_info.spec_info;
    }

    TRACE("Calling vkCreateComputePipelines.\n");

    if (vkd3d_config_flags & VKD3D_CONFIG_FLAG_PIPELINE_LIBRARY_LOG)
    {
        feedback_info.sType = VK_STRUCTURE_TYPE_PIPELINE_CREATION_FEEDBACK_CREATE_INFO;
        feedback_info.pNext = pipeline_info.pNext;
        feedback_info.pPipelineStageCreationFeedbacks = feedbacks;
        feedback_info.pipelineStageCreationFeedbackCount = 1;
        feedback_info.pPipelineCreationFeedback = &feedback;
        pipeline_info.pNext = &feedback_info;
    }
    else
        feedback_info.pipelineStageCreationFeedbackCount = 0;

    if (pipeline_info.stage.module == VK_NULL_HANDLE)
        pipeline_info.flags |= VK_PIPELINE_CREATE_FAIL_ON_PIPELINE_COMPILE_REQUIRED_BIT;

    if (d3d12_device_uses_descriptor_buffers(device))
        pipeline_info.flags |= VK_PIPELINE_CREATE_DESCRIPTOR_BUFFER_BIT_EXT;

    if (state->compute.code.meta.flags & VKD3D_SHADER_META_FLAG_DISABLE_OPTIMIZATIONS)
        pipeline_info.flags |= VK_PIPELINE_CREATE_DISABLE_OPTIMIZATION_BIT;

    cookie = vkd3d_queue_timeline_trace_register_pso_compile(&device->queue_timeline_trace);

    vr = VK_CALL(vkCreateComputePipelines(device->vk_device,
            vk_cache, 1, &pipeline_info, NULL, &state->compute.vk_pipeline));

    if (vkd3d_queue_timeline_trace_cookie_is_valid(cookie))
    {
        const char *kind;

        if (vr == VK_SUCCESS)
        {
            if (pipeline_info.flags & VK_PIPELINE_CREATE_FAIL_ON_PIPELINE_COMPILE_REQUIRED_BIT)
                kind = "COMP IDENT OK";
            else
                kind = "COMP OK";
        }
        else if (vr == VK_PIPELINE_COMPILE_REQUIRED)
            kind = "COMP MISS";
        else
            kind = "COMP ERR";

        vkd3d_queue_timeline_trace_complete_pso_compile(&device->queue_timeline_trace,
                cookie, vkd3d_pipeline_cache_compatibility_condense(&state->pipeline_cache_compat), kind);
    }

    if (vkd3d_config_flags & VKD3D_CONFIG_FLAG_PIPELINE_LIBRARY_LOG)
    {
        if (pipeline_info.flags & VK_PIPELINE_CREATE_FAIL_ON_PIPELINE_COMPILE_REQUIRED_BIT)
        {
            if (vr == VK_SUCCESS)
                INFO("[IDENTIFIER] Successfully created compute pipeline from identifier.\n");
            else if (vr == VK_PIPELINE_COMPILE_REQUIRED)
                INFO("[IDENTIFIER] Failed to create compute pipeline from identifier, falling back ...\n");
        }
        else
            INFO("[IDENTIFIER] None compute.\n");
    }

    /* Fallback. */
    if (vr == VK_PIPELINE_COMPILE_REQUIRED)
    {
        pipeline_info.flags &= ~VK_PIPELINE_CREATE_FAIL_ON_PIPELINE_COMPILE_REQUIRED_BIT;

        if (FAILED(hr = vkd3d_compile_shader_stage(state, device,
                VK_SHADER_STAGE_COMPUTE_BIT, code, spirv_code, spirv_code_debug)))
            return hr;

        if (FAILED(hr = vkd3d_setup_shader_stage(state, device,
                &pipeline_info.stage, VK_SHADER_STAGE_COMPUTE_BIT,
                &required_subgroup_size_info, NULL,
                spirv_code)))
            return hr;

        /* When a shader module is specified, VkPipelineShaderStageModuleIdentifierCreateInfoEXT must have an
         * identifierSize of 0 or be absent. */
        vk_remove_struct(&pipeline_info.stage,
                VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_MODULE_IDENTIFIER_CREATE_INFO_EXT);

        cookie = vkd3d_queue_timeline_trace_register_pso_compile(&device->queue_timeline_trace);

        vr = VK_CALL(vkCreateComputePipelines(device->vk_device,
                vk_cache, 1, &pipeline_info, NULL, &state->compute.vk_pipeline));

        if (vkd3d_queue_timeline_trace_cookie_is_valid(cookie))
        {
            const char *kind = vr == VK_SUCCESS ? "FALLBACK OK" : "FALLBACK ERR";
            vkd3d_queue_timeline_trace_complete_pso_compile(&device->queue_timeline_trace,
                    cookie, vkd3d_pipeline_cache_compatibility_condense(&state->pipeline_cache_compat), kind);
        }
    }

    TRACE("Called vkCreateComputePipelines.\n");
    VK_CALL(vkDestroyShaderModule(device->vk_device, pipeline_info.stage.module, NULL));
    if (vr < 0)
    {
        ERR("Failed to create Vulkan compute pipeline, hr %#x.\n", hr);
        ERR("  Root signature: %"PRIx64"\n", state->root_signature->pso_compatibility_hash);
        ERR("  Shader: %"PRIx64".\n", state->compute.code.meta.hash);
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
    shader_interface.flags |= vkd3d_descriptor_debug_get_shader_interface_flags(
            device->descriptor_qa_global_info, desc->cs.pShaderBytecode, desc->cs.BytecodeLength);

    vkd3d_load_spirv_from_cached_state(device, cached_pso,
            VK_SHADER_STAGE_COMPUTE_BIT, &state->compute.code,
            &state->compute.identifier_create_info);

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

    d3d_destruction_notifier_init(&state->destruction_notifier, (IUnknown*)&state->ID3D12PipelineState_iface);
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
        const D3D12_RASTERIZER_DESC2 *d3d12_desc)
{
    vk_desc->sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    vk_desc->pNext = NULL;
    vk_desc->flags = 0;
    vk_desc->depthClampEnable = !d3d12_desc->DepthClipEnable;
    vk_desc->rasterizerDiscardEnable = VK_FALSE;
    vk_desc->polygonMode = vk_polygon_mode_from_d3d12(d3d12_desc->FillMode);
    vk_desc->cullMode = vk_cull_mode_from_d3d12(d3d12_desc->CullMode);
    vk_desc->frontFace = d3d12_desc->FrontCounterClockwise ? VK_FRONT_FACE_COUNTER_CLOCKWISE : VK_FRONT_FACE_CLOCKWISE;
    vk_desc->depthBiasEnable = d3d12_desc->DepthBias != 0.0f || d3d12_desc->SlopeScaledDepthBias != 0.0f;
    vk_desc->depthBiasConstantFactor = d3d12_desc->DepthBias;
    vk_desc->depthBiasClamp = d3d12_desc->DepthBiasClamp;
    vk_desc->depthBiasSlopeFactor = d3d12_desc->SlopeScaledDepthBias;
    vk_desc->lineWidth = 1.0f;

    if (d3d12_desc->ForcedSampleCount)
        FIXME("Ignoring ForcedSampleCount %#x.\n", d3d12_desc->ForcedSampleCount);
}

static void rs_conservative_info_from_d3d12(VkPipelineRasterizationConservativeStateCreateInfoEXT *conservative_info,
        VkPipelineRasterizationStateCreateInfo *vk_rs_desc, const D3D12_RASTERIZER_DESC2 *d3d12_desc)
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
        VkPipelineRasterizationStateCreateInfo *vk_rs_desc, const D3D12_RASTERIZER_DESC2 *d3d12_desc)
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

static void rs_line_info_from_d3d12(struct d3d12_device *device, VkPipelineRasterizationLineStateCreateInfoEXT *vk_line_info,
        VkPipelineRasterizationStateCreateInfo *vk_rs_desc, const D3D12_RASTERIZER_DESC2 *d3d12_desc)
{
    vk_line_info->sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_LINE_STATE_CREATE_INFO_EXT;
    vk_line_info->pNext = NULL;
    vk_line_info->lineRasterizationMode = VK_LINE_RASTERIZATION_MODE_DEFAULT_EXT;

    switch (d3d12_desc->LineRasterizationMode)
    {
        case D3D12_LINE_RASTERIZATION_MODE_ALIASED:
            break;

        case D3D12_LINE_RASTERIZATION_MODE_ALPHA_ANTIALIASED:
            if (device->device_info.line_rasterization_features.smoothLines)
                vk_line_info->lineRasterizationMode = VK_LINE_RASTERIZATION_MODE_RECTANGULAR_SMOOTH_EXT;
            break;

        case D3D12_LINE_RASTERIZATION_MODE_QUADRILATERAL_WIDE:
            if (device->device_info.features2.features.wideLines)
                vk_rs_desc->lineWidth = 1.4f;
            /* fall through */

        case D3D12_LINE_RASTERIZATION_MODE_QUADRILATERAL_NARROW:
            if (device->device_info.line_rasterization_features.rectangularLines)
                vk_line_info->lineRasterizationMode = VK_LINE_RASTERIZATION_MODE_RECTANGULAR_EXT;
            break;
    }

    vk_line_info->stippledLineEnable = VK_FALSE;
    vk_line_info->lineStippleFactor = 0;
    vk_line_info->lineStipplePattern = 0;

    /* Deliberately do not add this structure to the rasterization state pNext chain yet
     * since we cannot know whether the pipeline actually renders lines, and using
     * non-default line rasterization modes breaks triangle rendering on some drivers. */
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
        const D3D12_DEPTH_STENCILOP_DESC1 *d3d12_desc)
{
    vk_desc->failOp = vk_stencil_op_from_d3d12(d3d12_desc->StencilFailOp);
    vk_desc->passOp = vk_stencil_op_from_d3d12(d3d12_desc->StencilPassOp);
    vk_desc->depthFailOp = vk_stencil_op_from_d3d12(d3d12_desc->StencilDepthFailOp);
    vk_desc->compareOp = vk_compare_op_from_d3d12(d3d12_desc->StencilFunc);
    vk_desc->compareMask = d3d12_desc->StencilReadMask;
    vk_desc->writeMask = d3d12_desc->StencilWriteMask;
    /* The stencil reference value is a dynamic state. Set by OMSetStencilRef(). */
    vk_desc->reference = 0;
}

static void ds_desc_from_d3d12(struct VkPipelineDepthStencilStateCreateInfo *vk_desc,
        const D3D12_DEPTH_STENCIL_DESC2 *d3d12_desc)
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
        vk_stencil_op_state_from_d3d12(&vk_desc->front, &d3d12_desc->FrontFace);
        vk_stencil_op_state_from_d3d12(&vk_desc->back, &d3d12_desc->BackFace);
    }
    else
    {
        memset(&vk_desc->front, 0, sizeof(vk_desc->front));
        memset(&vk_desc->back, 0, sizeof(vk_desc->back));
    }
    vk_desc->minDepthBounds = 0.0f;
    vk_desc->maxDepthBounds = 1.0f;
}

static enum VkBlendFactor vk_blend_factor_from_d3d12_a8(D3D12_BLEND blend)
{
    /* Rewrite any ALPHA references to COLOR since we're actually rendering to R8.
     * When alpha blending receives COLOR inputs, it's actually receiving alpha,
     * so it's really just an alias for our case. */
    switch (blend)
    {
        case D3D12_BLEND_ZERO:
            return VK_BLEND_FACTOR_ZERO;
        case D3D12_BLEND_ONE:
            return VK_BLEND_FACTOR_ONE;
        case D3D12_BLEND_SRC_COLOR:
        case D3D12_BLEND_SRC_ALPHA:
            return VK_BLEND_FACTOR_SRC_COLOR;
        case D3D12_BLEND_INV_SRC_COLOR:
        case D3D12_BLEND_INV_SRC_ALPHA:
            return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
        case D3D12_BLEND_DEST_ALPHA:
        case D3D12_BLEND_DEST_COLOR:
            return VK_BLEND_FACTOR_DST_COLOR;
        case D3D12_BLEND_INV_DEST_ALPHA:
        case D3D12_BLEND_INV_DEST_COLOR:
            return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
        case D3D12_BLEND_SRC_ALPHA_SAT:
            return VK_BLEND_FACTOR_ONE;
        case D3D12_BLEND_BLEND_FACTOR:
        case D3D12_BLEND_ALPHA_FACTOR:
            return VK_BLEND_FACTOR_CONSTANT_ALPHA;
        case D3D12_BLEND_INV_BLEND_FACTOR:
        case D3D12_BLEND_INV_ALPHA_FACTOR:
            return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA;
        case D3D12_BLEND_SRC1_COLOR:
        case D3D12_BLEND_SRC1_ALPHA:
            return VK_BLEND_FACTOR_SRC1_COLOR;
        case D3D12_BLEND_INV_SRC1_COLOR:
        case D3D12_BLEND_INV_SRC1_ALPHA:
            return VK_BLEND_FACTOR_ONE_MINUS_SRC1_COLOR;
        default:
            FIXME("Unhandled blend %#x.\n", blend);
            return VK_BLEND_FACTOR_ZERO;
    }
}

static enum VkBlendFactor vk_blend_factor_from_d3d12(D3D12_BLEND blend)
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
            return VK_BLEND_FACTOR_CONSTANT_COLOR;
        case D3D12_BLEND_INV_BLEND_FACTOR:
            return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR;
        case D3D12_BLEND_SRC1_COLOR:
            return VK_BLEND_FACTOR_SRC1_COLOR;
        case D3D12_BLEND_INV_SRC1_COLOR:
            return VK_BLEND_FACTOR_ONE_MINUS_SRC1_COLOR;
        case D3D12_BLEND_SRC1_ALPHA:
            return VK_BLEND_FACTOR_SRC1_ALPHA;
        case D3D12_BLEND_INV_SRC1_ALPHA:
            return VK_BLEND_FACTOR_ONE_MINUS_SRC1_ALPHA;
        case D3D12_BLEND_ALPHA_FACTOR:
            return VK_BLEND_FACTOR_CONSTANT_ALPHA;
        case D3D12_BLEND_INV_ALPHA_FACTOR:
            return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA;
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
        const D3D12_RENDER_TARGET_BLEND_DESC *d3d12_desc, const struct vkd3d_format *format)
{
    if (d3d12_desc->BlendEnable && d3d12_desc->RenderTargetWriteMask)
    {
        vk_desc->blendEnable = VK_TRUE;
        vk_desc->srcColorBlendFactor = vk_blend_factor_from_d3d12(d3d12_desc->SrcBlend);
        vk_desc->dstColorBlendFactor = vk_blend_factor_from_d3d12(d3d12_desc->DestBlend);
        vk_desc->colorBlendOp = vk_blend_op_from_d3d12(d3d12_desc->BlendOp);
        vk_desc->srcAlphaBlendFactor = vk_blend_factor_from_d3d12(d3d12_desc->SrcBlendAlpha);
        vk_desc->dstAlphaBlendFactor = vk_blend_factor_from_d3d12(d3d12_desc->DestBlendAlpha);
        vk_desc->alphaBlendOp = vk_blend_op_from_d3d12(d3d12_desc->BlendOpAlpha);

        if (format && format->dxgi_format == DXGI_FORMAT_A8_UNORM && format->vk_format != VK_FORMAT_A8_UNORM_KHR)
        {
            /* Alpha blend ops become color blend ops. */
            vk_desc->colorBlendOp = vk_desc->alphaBlendOp;
            vk_desc->srcColorBlendFactor = vk_blend_factor_from_d3d12_a8(d3d12_desc->SrcBlendAlpha);
            vk_desc->dstColorBlendFactor = vk_blend_factor_from_d3d12_a8(d3d12_desc->DestBlendAlpha);
            vk_desc->srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
            vk_desc->dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        }
    }
    else
    {
        memset(vk_desc, 0, sizeof(*vk_desc));
    }
    vk_desc->colorWriteMask = 0;

    if (format && format->dxgi_format == DXGI_FORMAT_A8_UNORM && format->vk_format != VK_FORMAT_A8_UNORM_KHR)
    {
        /* Redirect A8 to R8 so need to flag the R bit here.
         * There's just one component, so don't try to use partial masks. */
        if (d3d12_desc->RenderTargetWriteMask & D3D12_COLOR_WRITE_ENABLE_ALPHA)
        {
            vk_desc->colorWriteMask |= VK_COLOR_COMPONENT_R_BIT |
                    VK_COLOR_COMPONENT_G_BIT |
                    VK_COLOR_COMPONENT_B_BIT |
                    VK_COLOR_COMPONENT_A_BIT;
        }
    }
    else
    {
        if (d3d12_desc->RenderTargetWriteMask & D3D12_COLOR_WRITE_ENABLE_RED)
            vk_desc->colorWriteMask |= VK_COLOR_COMPONENT_R_BIT;
        if (d3d12_desc->RenderTargetWriteMask & D3D12_COLOR_WRITE_ENABLE_GREEN)
            vk_desc->colorWriteMask |= VK_COLOR_COMPONENT_G_BIT;
        if (d3d12_desc->RenderTargetWriteMask & D3D12_COLOR_WRITE_ENABLE_BLUE)
            vk_desc->colorWriteMask |= VK_COLOR_COMPONENT_B_BIT;
        if (d3d12_desc->RenderTargetWriteMask & D3D12_COLOR_WRITE_ENABLE_ALPHA)
            vk_desc->colorWriteMask |= VK_COLOR_COMPONENT_A_BIT;
    }
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
    if (format->dxgi_format == DXGI_FORMAT_A8_UNORM && format->vk_format != VK_FORMAT_A8_UNORM_KHR)
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
        if ((aspects & VK_IMAGE_ASPECT_DEPTH_BIT) &&
                ((graphics->ds_desc.depthTestEnable || graphics->ds_desc.depthBoundsTestEnable) && graphics->ds_desc.depthWriteEnable))
            plane_optimal_mask |= VKD3D_DEPTH_PLANE_OPTIMAL;

        if ((aspects & VK_IMAGE_ASPECT_STENCIL_BIT) &&
                (graphics->ds_desc.stencilTestEnable && (graphics->ds_desc.front.writeMask | graphics->ds_desc.back.writeMask)))
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
        case D3D_PRIMITIVE_TOPOLOGY_TRIANGLEFAN:
            return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN;
        case D3D_PRIMITIVE_TOPOLOGY_LINELIST_ADJ:
            return VK_PRIMITIVE_TOPOLOGY_LINE_LIST_WITH_ADJACENCY;
        case D3D_PRIMITIVE_TOPOLOGY_LINESTRIP_ADJ:
            return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP_WITH_ADJACENCY;
        case D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST_ADJ:
            return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST_WITH_ADJACENCY;
        case D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP_ADJ:
            return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP_WITH_ADJACENCY;
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
    { VKD3D_DYNAMIC_STATE_PATCH_CONTROL_POINTS,  VK_DYNAMIC_STATE_PATCH_CONTROL_POINTS_EXT },
    { VKD3D_DYNAMIC_STATE_DEPTH_WRITE_ENABLE,    VK_DYNAMIC_STATE_DEPTH_WRITE_ENABLE },
    { VKD3D_DYNAMIC_STATE_STENCIL_WRITE_MASK,    VK_DYNAMIC_STATE_STENCIL_WRITE_MASK },
    { VKD3D_DYNAMIC_STATE_DEPTH_BIAS,            VK_DYNAMIC_STATE_DEPTH_BIAS },
    { VKD3D_DYNAMIC_STATE_DEPTH_BIAS,            VK_DYNAMIC_STATE_DEPTH_BIAS_ENABLE },
    { VKD3D_DYNAMIC_STATE_RASTERIZATION_SAMPLES, VK_DYNAMIC_STATE_RASTERIZATION_SAMPLES_EXT },
};

uint32_t vkd3d_init_dynamic_state_array(VkDynamicState *dynamic_states, uint32_t dynamic_state_flags)
{
    uint32_t i, count;

    for (i = 0, count = 0; i < ARRAY_SIZE(vkd3d_dynamic_state_list); i++)
    {
        if (dynamic_state_flags & vkd3d_dynamic_state_list[i].flag)
            dynamic_states[count++] = vkd3d_dynamic_state_list[i].vk_state;
    }

    return count;
}

void vkd3d_vertex_input_pipeline_desc_init(struct vkd3d_vertex_input_pipeline_desc *desc,
        struct d3d12_pipeline_state *state, const struct vkd3d_pipeline_key *key, uint32_t dynamic_state_flags)
{
    struct d3d12_graphics_pipeline_state *graphics = &state->graphics;

    /* Mesh shader pipelines do not use vertex input state */
    assert(!(graphics->stage_flags & VK_SHADER_STAGE_MESH_BIT_EXT));

    /* Do not set up pointers here as they would complicate hash table lookup */
    memset(desc, 0, sizeof(*desc));

    memcpy(desc->vi_divisors, graphics->instance_divisors, graphics->instance_divisor_count * sizeof(*desc->vi_divisors));
    desc->vi_divisor_info.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_DIVISOR_STATE_CREATE_INFO_EXT;
    desc->vi_divisor_info.vertexBindingDivisorCount = graphics->instance_divisor_count;

    memcpy(desc->vi_bindings, graphics->attribute_bindings, graphics->attribute_binding_count * sizeof(*desc->vi_bindings));
    memcpy(desc->vi_attributes, graphics->attributes, graphics->attribute_count * sizeof(*desc->vi_attributes));
    desc->vi_info.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    desc->vi_info.vertexBindingDescriptionCount = graphics->attribute_binding_count;
    desc->vi_info.vertexAttributeDescriptionCount = graphics->attribute_count;

    desc->ia_info.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    desc->ia_info.topology = key && !key->dynamic_topology
            ? vk_topology_from_d3d12_topology(key->topology)
            : vk_topology_from_d3d12_topology_type(graphics->primitive_topology_type, !!graphics->index_buffer_strip_cut_value);
    desc->ia_info.primitiveRestartEnable = graphics->index_buffer_strip_cut_value && (key && !key->dynamic_topology
            ? vkd3d_topology_can_restart(desc->ia_info.topology)
            : vkd3d_topology_type_can_restart(graphics->primitive_topology_type));

    desc->dy_info.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    desc->dy_info.dynamicStateCount = vkd3d_init_dynamic_state_array(desc->dy_states,
            dynamic_state_flags & VKD3D_VERTEX_INPUT_DYNAMIC_STATE_MASK);
}

void vkd3d_vertex_input_pipeline_desc_prepare(struct vkd3d_vertex_input_pipeline_desc *desc)
{
    if (desc->vi_divisor_info.vertexBindingDivisorCount)
    {
        desc->vi_divisor_info.pVertexBindingDivisors = desc->vi_divisors;
        desc->vi_info.pNext = &desc->vi_divisor_info;
    }

    if (desc->vi_info.vertexAttributeDescriptionCount)
        desc->vi_info.pVertexAttributeDescriptions = desc->vi_attributes;

    if (desc->vi_info.vertexBindingDescriptionCount)
        desc->vi_info.pVertexBindingDescriptions = desc->vi_bindings;

    if (desc->dy_info.dynamicStateCount)
        desc->dy_info.pDynamicStates = desc->dy_states;
}

uint32_t vkd3d_vertex_input_pipeline_desc_hash(const void *key)
{
    return hash_data(key, sizeof(struct vkd3d_vertex_input_pipeline_desc));
}

bool vkd3d_vertex_input_pipeline_desc_compare(const void *key, const struct hash_map_entry *entry)
{
    const struct vkd3d_vertex_input_pipeline *pipeline = (const void*)entry;
    const struct vkd3d_vertex_input_pipeline_desc *desc = key;
    return !memcmp(desc, &pipeline->desc, sizeof(*desc));
}

VkPipeline vkd3d_vertex_input_pipeline_create(struct d3d12_device *device,
        const struct vkd3d_vertex_input_pipeline_desc *desc)
{
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    VkGraphicsPipelineLibraryCreateInfoEXT library_create_info;
    struct vkd3d_vertex_input_pipeline_desc desc_copy = *desc;
    VkGraphicsPipelineCreateInfo create_info;
    VkPipeline vk_pipeline;
    VkResult vr;

    vkd3d_vertex_input_pipeline_desc_prepare(&desc_copy);

    memset(&library_create_info, 0, sizeof(library_create_info));
    library_create_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_LIBRARY_CREATE_INFO_EXT;
    library_create_info.flags = VK_GRAPHICS_PIPELINE_LIBRARY_VERTEX_INPUT_INTERFACE_BIT_EXT;

    memset(&create_info, 0, sizeof(create_info));
    create_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    create_info.pNext = &library_create_info;
    create_info.flags = VK_PIPELINE_CREATE_LIBRARY_BIT_KHR | VK_PIPELINE_CREATE_RETAIN_LINK_TIME_OPTIMIZATION_INFO_BIT_EXT;
    create_info.pInputAssemblyState = &desc_copy.ia_info;
    create_info.pVertexInputState = &desc_copy.vi_info;
    create_info.pDynamicState = &desc_copy.dy_info;
    create_info.basePipelineIndex = -1;

    if (d3d12_device_uses_descriptor_buffers(device))
        create_info.flags |= VK_PIPELINE_CREATE_DESCRIPTOR_BUFFER_BIT_EXT;

    if ((vr = VK_CALL(vkCreateGraphicsPipelines(device->vk_device,
            VK_NULL_HANDLE, 1, &create_info, NULL, &vk_pipeline))))
    {
        ERR("Failed to create vertex input pipeline, vr %d.\n", vr);
        return VK_NULL_HANDLE;
    }

    return vk_pipeline;
}

void vkd3d_vertex_input_pipeline_free(struct hash_map_entry *entry, void *userdata)
{
    const struct vkd3d_vertex_input_pipeline *pipeline = (const void*)entry;
    const struct vkd3d_vk_device_procs *vk_procs;
    struct d3d12_device *device = userdata;

    vk_procs = &device->vk_procs;

    VK_CALL(vkDestroyPipeline(device->vk_device, pipeline->vk_pipeline, NULL));
}

void vkd3d_fragment_output_pipeline_desc_init(struct vkd3d_fragment_output_pipeline_desc *desc,
        struct d3d12_pipeline_state *state, const struct vkd3d_format *dsv_format, uint32_t dynamic_state_flags)
{
    struct d3d12_graphics_pipeline_state *graphics = &state->graphics;
    unsigned int i;

    memset(desc, 0, sizeof(*desc));

    memcpy(desc->cb_attachments, graphics->blend_attachments, graphics->rt_count * sizeof(*desc->cb_attachments));

    desc->cb_info.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    desc->cb_info.logicOpEnable = graphics->blend_desc.logicOpEnable;
    desc->cb_info.logicOp = graphics->blend_desc.logicOp;
    desc->cb_info.attachmentCount = graphics->rt_count;

    desc->ms_sample_mask = graphics->sample_mask;

    desc->ms_info.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    desc->ms_info.rasterizationSamples = graphics->ms_desc.rasterizationSamples;
    desc->ms_info.sampleShadingEnable = graphics->ms_desc.sampleShadingEnable;
    desc->ms_info.minSampleShading = graphics->ms_desc.minSampleShading;
    desc->ms_info.alphaToCoverageEnable = graphics->ms_desc.alphaToCoverageEnable;
    desc->ms_info.alphaToOneEnable = graphics->ms_desc.alphaToOneEnable;

    for (i = 0; i < D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT; i++)
    {
        desc->rt_formats[i] = graphics->rtv_active_mask & (1u << i)
            ? graphics->rtv_formats[i] : VK_FORMAT_UNDEFINED;
    }

    desc->rt_info.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR;
    desc->rt_info.colorAttachmentCount = graphics->rt_count;
    /* From spec:  If depthAttachmentFormat is not VK_FORMAT_UNDEFINED, it must be a format that includes a depth aspect. */
    desc->rt_info.depthAttachmentFormat = dsv_format && (dsv_format->vk_aspect_mask & VK_IMAGE_ASPECT_DEPTH_BIT) ? dsv_format->vk_format : VK_FORMAT_UNDEFINED;
    /* From spec:  If stencilAttachmentFormat is not VK_FORMAT_UNDEFINED, it must be a format that includes a stencil aspect. */
    desc->rt_info.stencilAttachmentFormat = dsv_format && (dsv_format->vk_aspect_mask & VK_IMAGE_ASPECT_STENCIL_BIT) ? dsv_format->vk_format : VK_FORMAT_UNDEFINED;

    desc->dy_info.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    desc->dy_info.dynamicStateCount = vkd3d_init_dynamic_state_array(desc->dy_states,
            dynamic_state_flags & VKD3D_FRAGMENT_OUTPUT_DYNAMIC_STATE_MASK);
}

void vkd3d_fragment_output_pipeline_desc_prepare(struct vkd3d_fragment_output_pipeline_desc *desc)
{
    if (desc->cb_info.attachmentCount)
        desc->cb_info.pAttachments = desc->cb_attachments;

    desc->ms_info.pSampleMask = &desc->ms_sample_mask;

    if (desc->rt_info.colorAttachmentCount)
        desc->rt_info.pColorAttachmentFormats = desc->rt_formats;

    if (desc->dy_info.dynamicStateCount)
        desc->dy_info.pDynamicStates = desc->dy_states;
}

uint32_t vkd3d_fragment_output_pipeline_desc_hash(const void *key)
{
    return hash_data(key, sizeof(struct vkd3d_fragment_output_pipeline_desc));
}

bool vkd3d_fragment_output_pipeline_desc_compare(const void *key, const struct hash_map_entry *entry)
{
    const struct vkd3d_fragment_output_pipeline *pipeline = (const void*)entry;
    const struct vkd3d_fragment_output_pipeline_desc *desc = key;
    /* We zero-initialize these structs and pointers are
     * NULL during lookup, so using a memcmp is fine */
    return !memcmp(desc, &pipeline->desc, sizeof(*desc));
}

VkPipeline vkd3d_fragment_output_pipeline_create(struct d3d12_device *device,
        const struct vkd3d_fragment_output_pipeline_desc *desc)
{
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    struct vkd3d_fragment_output_pipeline_desc desc_copy = *desc;
    VkGraphicsPipelineLibraryCreateInfoEXT library_create_info;
    VkGraphicsPipelineCreateInfo create_info;
    VkPipeline vk_pipeline;
    VkResult vr;

    vkd3d_fragment_output_pipeline_desc_prepare(&desc_copy);

    memset(&library_create_info, 0, sizeof(library_create_info));
    library_create_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_LIBRARY_CREATE_INFO_EXT;
    library_create_info.pNext = &desc_copy.rt_info;
    library_create_info.flags = VK_GRAPHICS_PIPELINE_LIBRARY_FRAGMENT_OUTPUT_INTERFACE_BIT_EXT;

    memset(&create_info, 0, sizeof(create_info));
    create_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    create_info.pNext = &library_create_info;
    create_info.flags = VK_PIPELINE_CREATE_LIBRARY_BIT_KHR | VK_PIPELINE_CREATE_RETAIN_LINK_TIME_OPTIMIZATION_INFO_BIT_EXT;
    create_info.pColorBlendState = &desc_copy.cb_info;
    create_info.pMultisampleState = &desc_copy.ms_info;
    create_info.pDynamicState = &desc_copy.dy_info;
    create_info.basePipelineIndex = -1;

    if (d3d12_device_uses_descriptor_buffers(device))
        create_info.flags |= VK_PIPELINE_CREATE_DESCRIPTOR_BUFFER_BIT_EXT;

    if ((vr = VK_CALL(vkCreateGraphicsPipelines(device->vk_device,
            VK_NULL_HANDLE, 1, &create_info, NULL, &vk_pipeline))))
    {
        ERR("Failed to create fragment output pipeline, vr %d.\n", vr);
        return VK_NULL_HANDLE;
    }

    return vk_pipeline;
}

void vkd3d_fragment_output_pipeline_free(struct hash_map_entry *entry, void *userdata)
{
    const struct vkd3d_fragment_output_pipeline *pipeline = (const void*)entry;
    const struct vkd3d_vk_device_procs *vk_procs;
    struct d3d12_device *device = userdata;

    vk_procs = &device->vk_procs;

    VK_CALL(vkDestroyPipeline(device->vk_device, pipeline->vk_pipeline, NULL));
}

bool vkd3d_debug_control_has_out_of_spec_test_behavior(VKD3D_DEBUG_CONTROL_OUT_OF_SPEC_BEHAVIOR behavior);

static bool d3d12_graphics_pipeline_needs_dynamic_rasterization_samples(const struct d3d12_graphics_pipeline_state *graphics)
{
    /* Ignore the case where the pipeline is compiled for a single sample since Vulkan drivers are robust against that. */
    if (graphics->rs_desc.rasterizerDiscardEnable ||
            (graphics->ms_desc.rasterizationSamples == VK_SAMPLE_COUNT_1_BIT &&
            !(vkd3d_config_flags & VKD3D_CONFIG_FLAG_FORCE_DYNAMIC_MSAA) &&
            !vkd3d_debug_control_has_out_of_spec_test_behavior(VKD3D_DEBUG_CONTROL_OUT_OF_SPEC_BEHAVIOR_SAMPLE_COUNT_MISMATCH)))
        return false;

    return graphics->rtv_active_mask || graphics->dsv_format ||
            (graphics->null_attachment_mask & dsv_attachment_mask(graphics));
}

uint32_t d3d12_graphics_pipeline_state_get_dynamic_state_flags(struct d3d12_pipeline_state *state,
        const struct vkd3d_pipeline_key *key)
{
    struct d3d12_graphics_pipeline_state *graphics = &state->graphics;
    bool is_mesh_pipeline, is_tess_pipeline;
    uint32_t dynamic_state_flags;
    unsigned int i;

    dynamic_state_flags = graphics->explicit_dynamic_states;

    is_mesh_pipeline = !!(graphics->stage_flags & VK_SHADER_STAGE_MESH_BIT_EXT);
    is_tess_pipeline = !!(graphics->stage_flags & VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT);

    /* Enable dynamic states as necessary */
    dynamic_state_flags |= VKD3D_DYNAMIC_STATE_VIEWPORT | VKD3D_DYNAMIC_STATE_SCISSOR;

    if (graphics->attribute_binding_count && !is_mesh_pipeline)
        dynamic_state_flags |= VKD3D_DYNAMIC_STATE_VERTEX_BUFFER_STRIDE;

    if (is_tess_pipeline && state->device->device_info.extended_dynamic_state2_features.extendedDynamicState2PatchControlPoints)
        dynamic_state_flags |= VKD3D_DYNAMIC_STATE_PATCH_CONTROL_POINTS;

    if ((!key || key->dynamic_topology) && !is_mesh_pipeline && !is_tess_pipeline)
        dynamic_state_flags |= VKD3D_DYNAMIC_STATE_TOPOLOGY;

    if (graphics->ds_desc.stencilTestEnable)
        dynamic_state_flags |= VKD3D_DYNAMIC_STATE_STENCIL_REFERENCE;

    if (graphics->ds_desc.depthBoundsTestEnable)
        dynamic_state_flags |= VKD3D_DYNAMIC_STATE_DEPTH_BOUNDS;

    /* If the DSV is read-only for a plane, writes are dynamically disabled. */
    if (graphics->ds_desc.depthTestEnable && graphics->ds_desc.depthWriteEnable)
        dynamic_state_flags |= VKD3D_DYNAMIC_STATE_DEPTH_WRITE_ENABLE;

    if (graphics->ds_desc.stencilTestEnable && (graphics->ds_desc.front.writeMask | graphics->ds_desc.back.writeMask))
        dynamic_state_flags |= VKD3D_DYNAMIC_STATE_STENCIL_WRITE_MASK;

    for (i = 0; i < graphics->rt_count; i++)
    {
        if (vk_blend_attachment_needs_blend_constants(&graphics->blend_attachments[i]))
            dynamic_state_flags |= VKD3D_DYNAMIC_STATE_BLEND_CONSTANTS;
    }

    /* We always need to enable fragment shading rate dynamic state when rasterizing.
     * D3D12 has no information about this ahead of time for a pipeline
     * unlike Vulkan.
     * Target Independent Rasterization (ForcedSampleCount) is not supported when this is used
     * so we don't need to worry about side effects when there are no render targets. */
    if (d3d12_device_supports_variable_shading_rate_tier_1(state->device) && graphics->rt_count)
    {
        /* If sample rate shading, ROVs are used, or depth stencil export is used force default VRS state.
         * Do this by not enabling the dynamic state.
         * This forces default static pipeline state to be used instead, which is what we want. */
        const uint32_t disable_flags =
                VKD3D_SHADER_META_FLAG_USES_SAMPLE_RATE_SHADING |
                VKD3D_SHADER_META_FLAG_USES_DEPTH_STENCIL_WRITE |
                VKD3D_SHADER_META_FLAG_USES_RASTERIZER_ORDERED_VIEWS;
        bool allow_vrs_combiners = true;

        for (i = 0; allow_vrs_combiners && i < graphics->stage_count; i++)
            if (graphics->code[i].meta.flags & disable_flags)
                allow_vrs_combiners = false;

        if (allow_vrs_combiners)
            dynamic_state_flags |= VKD3D_DYNAMIC_STATE_FRAGMENT_SHADING_RATE;
    }

    if (graphics->index_buffer_strip_cut_value && !is_mesh_pipeline)
        dynamic_state_flags |= VKD3D_DYNAMIC_STATE_PRIMITIVE_RESTART;

    /* Enable dynamic sample count for multisampled pipelines so we can work around
     * bugs where the app may render to a single sampled render target. */
    if (d3d12_graphics_pipeline_needs_dynamic_rasterization_samples(&state->graphics) &&
            state->device->device_info.extended_dynamic_state3_features.extendedDynamicState3RasterizationSamples)
        dynamic_state_flags |= VKD3D_DYNAMIC_STATE_RASTERIZATION_SAMPLES;

    return dynamic_state_flags;
}

static uint32_t d3d12_graphics_pipeline_state_init_dynamic_state(struct d3d12_pipeline_state *state,
        VkPipelineDynamicStateCreateInfo *dynamic_desc, VkDynamicState *dynamic_state_buffer,
        const struct vkd3d_pipeline_key *key)
{
    uint32_t dynamic_state_flags = d3d12_graphics_pipeline_state_get_dynamic_state_flags(state, key);

    dynamic_desc->sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic_desc->pNext = NULL;
    dynamic_desc->flags = 0;
    dynamic_desc->dynamicStateCount = vkd3d_init_dynamic_state_array(dynamic_state_buffer, dynamic_state_flags);
    dynamic_desc->pDynamicStates = dynamic_desc->dynamicStateCount ? dynamic_state_buffer : NULL;
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

            /* Ignore built-in outputs like SV_DEPTH etc */
            if (sig->elements[i].register_index == ~0u)
                continue;

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

static void d3d12_pipeline_state_graphics_load_spirv_from_cached_state(
        struct d3d12_pipeline_state *state, struct d3d12_device *device,
        const struct d3d12_pipeline_state_desc *desc,
        const struct d3d12_cached_pipeline_state *cached_pso)
{
    struct d3d12_graphics_pipeline_state *graphics = &state->graphics;
    unsigned int i, j;

    /* We only accept SPIR-V from cache if we can successfully load all shaders.
     * We cannot partially fall back since we cannot handle any situation where we need inter-stage code-gen fixups.
     * In this situation, just generate full SPIR-V from scratch.
     * This really shouldn't happen unless we have corrupt cache entries. */
    for (i = 0; i < graphics->stage_count; i++)
    {
        if (FAILED(vkd3d_load_spirv_from_cached_state(device, cached_pso,
                graphics->cached_desc.bytecode_stages[i], &graphics->code[i],
                &graphics->identifier_create_infos[i])))
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
}

static HRESULT d3d12_pipeline_state_graphics_create_shader_stages(
        struct d3d12_pipeline_state *state, struct d3d12_device *device,
        const struct d3d12_pipeline_state_desc *desc)
{
    struct d3d12_graphics_pipeline_state *graphics = &state->graphics;
    struct vkd3d_shader_code_debug *debug_output;
    unsigned int i;
    HRESULT hr;

    /* Now create the actual shader modules. If we managed to load SPIR-V from cache, use that directly. */
    for (i = 0; i < graphics->stage_count; i++)
    {
        if (graphics->identifier_create_infos[i].identifierSize == 0)
        {
            if (vkd3d_config_flags & VKD3D_CONFIG_FLAG_DEBUG_UTILS)
            {
                debug_output = &graphics->code_debug[i];
                if (debug_output->debug_entry_point_name)
                    debug_output = NULL;
            }
            else
                debug_output = NULL;

            if (graphics->cached_desc.bytecode_stages[i] == VK_SHADER_STAGE_FRAGMENT_BIT &&
                    graphics->cached_desc.bytecode[i].BytecodeLength == 0)
            {
                vkd3d_shader_code_init_empty_fs(&graphics->code[i]);
            }
            else if (FAILED(hr = vkd3d_compile_shader_stage(state, device,
                    graphics->cached_desc.bytecode_stages[i],
                    &graphics->cached_desc.bytecode[i], &graphics->code[i], debug_output)))
            {
                return hr;
            }
        }

        if (FAILED(hr = vkd3d_setup_shader_stage(state, device,
                &graphics->stages[i],
                graphics->cached_desc.bytecode_stages[i],
                NULL, &graphics->identifier_create_infos[i],
                &graphics->code[i])))
            return hr;
    }

    return S_OK;
}

static bool d3d12_pipeline_state_validate_gs_input_toplogy(struct d3d12_pipeline_state *state,
        const struct vkd3d_shader_meta *gs_meta, uint32_t geometry_meta)
{
    struct d3d12_graphics_pipeline_state *graphics = &state->graphics;

    if (geometry_meta & VKD3D_SHADER_META_FLAG_POINT_MODE_TESSELLATION)
        return gs_meta->gs_input_topology == VK_PRIMITIVE_TOPOLOGY_POINT_LIST;

    switch (gs_meta->gs_input_topology)
    {
        case VK_PRIMITIVE_TOPOLOGY_POINT_LIST:
            return graphics->primitive_topology_type == D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;

        case VK_PRIMITIVE_TOPOLOGY_LINE_LIST:
            return !!(geometry_meta & VKD3D_SHADER_META_FLAG_EMITS_LINES);

        case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST:
            return !!(geometry_meta & VKD3D_SHADER_META_FLAG_EMITS_TRIANGLES);

        /* Input topologies with adjacency are not allowed in tessellation
         * pipelines, so check the input topology type directly. */
        case VK_PRIMITIVE_TOPOLOGY_LINE_LIST_WITH_ADJACENCY:
            return graphics->primitive_topology_type == D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;

        case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST_WITH_ADJACENCY:
            return graphics->primitive_topology_type == D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

        default:
            ERR("Unhandled GS input topology %u.\n", gs_meta->gs_input_topology);
            return false;
    }
}

static HRESULT d3d12_pipeline_state_graphics_handle_meta(struct d3d12_pipeline_state *state,
        struct d3d12_device *device)
{
    struct d3d12_graphics_pipeline_state *graphics = &state->graphics;
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    uint32_t geometry_meta = 0;
    unsigned int i;

    if (state->pipeline_type == VKD3D_PIPELINE_TYPE_GRAPHICS)
    {
        if (graphics->primitive_topology_type == D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE)
            geometry_meta = VKD3D_SHADER_META_FLAG_EMITS_LINES;
        else if (graphics->primitive_topology_type == D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE)
            geometry_meta = VKD3D_SHADER_META_FLAG_EMITS_TRIANGLES;
    }

    for (i = 0; i < graphics->stage_count; i++)
    {
        if (graphics->cached_desc.bytecode_stages[i] == VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT)
            graphics->patch_vertex_count = graphics->code[i].meta.patch_vertex_count;

        if (graphics->code[i].meta.flags & VKD3D_SHADER_META_FLAG_USES_SAMPLE_RATE_SHADING)
        {
            graphics->ms_desc.sampleShadingEnable = VK_TRUE;
            graphics->ms_desc.minSampleShading = 1.0f;
        }

        if ((graphics->code[i].meta.flags & VKD3D_SHADER_META_FLAG_REPLACED) &&
                device->debug_ring.active)
        {
            vkd3d_shader_debug_ring_init_spec_constant(device,
                    &graphics->spec_info[i],
                    graphics->code[i].meta.hash);
            graphics->stages[i].pSpecializationInfo = &graphics->spec_info[i].spec_info;
        }

        if (graphics->stages[i].module != VK_NULL_HANDLE &&
                device->device_info.shader_module_identifier_features.shaderModuleIdentifier)
        {
            state->graphics.identifiers[i].sType = VK_STRUCTURE_TYPE_SHADER_MODULE_IDENTIFIER_EXT;
            state->graphics.identifiers[i].pNext = NULL;
            VK_CALL(vkGetShaderModuleIdentifierEXT(device->vk_device, graphics->stages[i].module,
                    &state->graphics.identifiers[i]));
        }

        /* Validate GS topology against the pipeline topology or domain shader output */
        if (graphics->stages[i].stage == VK_SHADER_STAGE_GEOMETRY_BIT)
        {
            if (!d3d12_pipeline_state_validate_gs_input_toplogy(state, &graphics->code[i].meta, geometry_meta))
            {
                WARN("GS input topology %u incompatible with pipeline topology type %u, flags %#x.\n",
                        graphics->code[i].meta.gs_input_topology, graphics->primitive_topology_type, geometry_meta);
                return E_INVALIDARG;
            }
        }

        /* The last active geometry stage determines the output topology */
        if (graphics->stages[i].stage == VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT ||
                graphics->stages[i].stage == VK_SHADER_STAGE_GEOMETRY_BIT ||
                graphics->stages[i].stage == VK_SHADER_STAGE_MESH_BIT_EXT)
            geometry_meta = graphics->code[i].meta.flags;

        /* Retain point mode override tessellation control shader */
        if (graphics->stages[i].stage == VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT)
            geometry_meta = graphics->code[i].meta.flags | (geometry_meta & VKD3D_SHADER_META_FLAG_POINT_MODE_TESSELLATION);

        /* Need to disable AToC if the fragment shader exports sample mask */
        if (graphics->stages[i].stage == VK_SHADER_STAGE_FRAGMENT_BIT &&
                (graphics->code[i].meta.flags & VKD3D_SHADER_META_FLAG_EXPORTS_SAMPLE_MASK))
            graphics->ms_desc.alphaToCoverageEnable = VK_FALSE;
    }

    if (!(geometry_meta & VKD3D_SHADER_META_FLAG_POINT_MODE_TESSELLATION))
    {
        if ((geometry_meta & VKD3D_SHADER_META_FLAG_EMITS_TRIANGLES) &&
                graphics->rs_desc.polygonMode == VK_POLYGON_MODE_LINE)
            geometry_meta = VKD3D_SHADER_META_FLAG_EMITS_LINES;

        if ((geometry_meta & VKD3D_SHADER_META_FLAG_EMITS_LINES) && device->vk_info.EXT_line_rasterization)
            vk_prepend_struct(&graphics->rs_desc, &graphics->rs_line_info);
    }

    return S_OK;
}

static bool vkd3d_shader_semantic_is_generated_for_stage(enum vkd3d_sysval_semantic sv,
        VkShaderStageFlagBits prev_stage, VkShaderStageFlagBits curr_stage)
{
    switch (sv)
    {
        case VKD3D_SV_NONE:
        case VKD3D_SV_POSITION:
        case VKD3D_SV_CLIP_DISTANCE:
        case VKD3D_SV_CULL_DISTANCE:
        case VKD3D_SV_RENDER_TARGET_ARRAY_INDEX:
        case VKD3D_SV_VIEWPORT_ARRAY_INDEX:
        case VKD3D_SV_TESS_FACTOR_QUADEDGE:
        case VKD3D_SV_TESS_FACTOR_QUADINT:
        case VKD3D_SV_TESS_FACTOR_TRIEDGE:
        case VKD3D_SV_TESS_FACTOR_TRIINT:
        case VKD3D_SV_TESS_FACTOR_LINEDET:
        case VKD3D_SV_TESS_FACTOR_LINEDEN:
            return false;

        case VKD3D_SV_VERTEX_ID:
        case VKD3D_SV_INSTANCE_ID:
            return curr_stage == VK_SHADER_STAGE_VERTEX_BIT;

        case VKD3D_SV_PRIMITIVE_ID:
            return prev_stage == VK_SHADER_STAGE_VERTEX_BIT;

        case VKD3D_SV_IS_FRONT_FACE:
        case VKD3D_SV_SAMPLE_INDEX:
        case VKD3D_SV_BARYCENTRICS:
        case VKD3D_SV_SHADING_RATE:
            return curr_stage == VK_SHADER_STAGE_FRAGMENT_BIT;

        default:
            FIXME("Unhandled system value %u.\n", sv);
            return false;
    }
}

static bool vkd3d_validate_shader_io_signatures(VkShaderStageFlagBits output_stage,
        const struct vkd3d_shader_signature *out_sig, VkShaderStageFlagBits input_stage,
        const struct vkd3d_shader_signature *in_sig)
{
    const struct vkd3d_shader_signature_element *in_element, *out_element;
    unsigned int i;
    bool mismatch;

    /* D3D12 does not appear to take the rasterized stream index from the pipeline
     * into account for interface matching, so only stream 0 works in practice. */
    for (i = 0; i < in_sig->element_count; i++)
    {
        in_element = &in_sig->elements[i];

        out_element = vkd3d_shader_find_signature_element(out_sig, in_element->semantic_name,
                in_element->semantic_index, in_element->stream_index);

        if (!out_element)
        {
            /* Some system values may or may not be provided by the previous stage, such as
             * SV_PrimitiveID in pixel shaders. Accept the input being provided, but if provided
             * by the previous stage, the register and component indices must match. */
            if (vkd3d_shader_semantic_is_generated_for_stage(in_element->sysval_semantic, output_stage, input_stage))
                continue;

            WARN("No corresponding output signature element found for %s%u.\n",
                    in_element->semantic_name, in_element->semantic_index);
            return false;
        }

        mismatch = in_element->register_index != out_element->register_index ||
                in_element->component_type != out_element->component_type ||
                in_element->min_precision != out_element->min_precision;

        if (input_stage == VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT)
        {
            /* For tessellation shaders, the component count must match exactly. */
            mismatch = mismatch || ((in_element->mask & 0xf) != (out_element->mask & 0xf));
        }
        else
        {
            /* Otherwise, it is legal to consume only a subset of components provided by the
             * previous stage, but consuming components not provided by the previous stage is not. */
            mismatch = mismatch || !!(in_element->mask & ~out_element->mask & 0xf);
        }

        if (mismatch)
        {
            WARN("Input signature element %s%u (reg %u, mask %#x) not compatible with %s%u (reg %u, mask %#x).\n",
                    in_element->semantic_name, in_element->semantic_index, in_element->register_index, in_element->mask & 0xf,
                    out_element->semantic_name, out_element->semantic_index, out_element->register_index, out_element->mask & 0xf);
            return false;
        }
    }

    /* The domain shader must consume all hull shader outputs, including
     * tessellation factors. */
    if (input_stage == VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT)
    {
        for (i = 0; i < out_sig->element_count; i++)
        {
            out_element = &out_sig->elements[i];

            if (!vkd3d_shader_find_signature_element(in_sig, out_element->semantic_name,
                    out_element->semantic_index, out_element->stream_index))
            {
                WARN("Hull shader output %s%u not consumed by domain shader.\n",
                        out_element->semantic_name, out_element->semantic_index);
                return false;
            }
        }
    }

    return true;
}

static bool vkd3d_validate_mesh_shader_io_signatures(const struct vkd3d_shader_signature *vert_sig,
        const struct vkd3d_shader_signature *prim_sig, const struct vkd3d_shader_signature *in_sig)
{
    const struct vkd3d_shader_signature_element *in_element, *out_element;
    unsigned int i;
    bool mismatch;

    for (i = 0; i < in_sig->element_count; i++)
    {
        in_element = &in_sig->elements[i];

        if (!(out_element = vkd3d_shader_find_signature_element(vert_sig, in_element->semantic_name, in_element->semantic_index, 0)) &&
                !(out_element = vkd3d_shader_find_signature_element(prim_sig, in_element->semantic_name, in_element->semantic_index, 0)))
        {
            if (vkd3d_shader_semantic_is_generated_for_stage(in_element->sysval_semantic, VK_SHADER_STAGE_MESH_BIT_EXT, VK_SHADER_STAGE_FRAGMENT_BIT))
                continue;

            WARN("No corresponding output signature element found for %s%u.\n",
                    in_element->semantic_name, in_element->semantic_index);
            return false;
        }

        mismatch = in_element->component_type != out_element->component_type ||
                vkd3d_popcount(in_element->mask & 0xf) != vkd3d_popcount(out_element->mask & 0xf) ||
                in_element->min_precision != out_element->min_precision;

        if (mismatch)
        {
            WARN("Input signature element %s%u (reg %u, mask %#x) not compatible with %s%u (reg %u, mask %#x).\n",
                    in_element->semantic_name, in_element->semantic_index, in_element->register_index, in_element->mask & 0xf,
                    out_element->semantic_name, out_element->semantic_index, out_element->register_index, out_element->mask & 0xf);
            return false;
        }
    }

    return true;
}

static bool vkd3d_validate_vertex_input_signature(const struct vkd3d_shader_signature *sig, const D3D12_INPUT_LAYOUT_DESC *input_layout)
{
    unsigned int i, j;
    bool found;

    for (i = 0; i < sig->element_count; i++)
    {
        const struct vkd3d_shader_signature_element *e = &sig->elements[i];

        if (vkd3d_shader_semantic_is_generated_for_stage(e->sysval_semantic, VK_SHADER_STAGE_FLAG_BITS_MAX_ENUM, VK_SHADER_STAGE_VERTEX_BIT))
            continue;

        found = false;

        for (j = 0; j < input_layout->NumElements; j++)
        {
            if (!ascii_strcasecmp(e->semantic_name, input_layout->pInputElementDescs[j].SemanticName) &&
                    e->semantic_index == input_layout->pInputElementDescs[j].SemanticIndex)
            {
                found = true;
                break;
            }
        }

        if (!found)
        {
            WARN("No input layout element found for VS input semantic %s%u.\n", e->semantic_name, e->semantic_index);
            return false;
        }
    }

    return true;
}

static bool d3d12_graphics_pipeline_state_needs_noop_fs(
        const struct d3d12_device *device,
        const struct d3d12_graphics_pipeline_state *graphics)
{
    return (graphics->stage_flags & VK_SHADER_STAGE_MESH_BIT_EXT) &&
            !(graphics->stage_flags & VK_SHADER_STAGE_FRAGMENT_BIT) &&
            !(vkd3d_config_flags & VKD3D_CONFIG_FLAG_SKIP_DRIVER_WORKAROUNDS) &&
            device->device_info.vulkan_1_2_properties.driverID == VK_DRIVER_ID_MESA_RADV;
}

static HRESULT d3d12_pipeline_state_init_graphics_create_info(struct d3d12_pipeline_state *state,
        struct d3d12_device *device, const struct d3d12_pipeline_state_desc *desc)
{
    struct vkd3d_shader_signature vs_input_signature, pc_input_signature, io_input_signature;
    const VkPhysicalDeviceFeatures *features = &device->device_info.features2.features;
    struct vkd3d_shader_signature pc_output_signature, io_output_signature;
    struct d3d12_graphics_pipeline_state *graphics = &state->graphics;
    const D3D12_STREAM_OUTPUT_DESC *so_desc = &desc->stream_output;
    VkVertexInputBindingDivisorDescriptionEXT *binding_divisor;
    const struct vkd3d_vulkan_info *vk_info = &device->vk_info;
    uint32_t instance_divisors[D3D12_VS_INPUT_REGISTER_COUNT];
    uint32_t aligned_offsets[D3D12_VS_INPUT_REGISTER_COUNT];
    VkShaderStageFlagBits curr_stage, prev_stage;
    VkSampleCountFlagBits sample_count;
    const struct vkd3d_format *format;
    unsigned int instance_divisor;
    VkVertexInputRate input_rate;
    bool have_attachment;
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
        /* Geometry stages must be listed in pipeline order */
        {VK_SHADER_STAGE_VERTEX_BIT,                  offsetof(struct d3d12_pipeline_state_desc, vs)},
        {VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT,    offsetof(struct d3d12_pipeline_state_desc, hs)},
        {VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT, offsetof(struct d3d12_pipeline_state_desc, ds)},
        {VK_SHADER_STAGE_GEOMETRY_BIT,                offsetof(struct d3d12_pipeline_state_desc, gs)},
        {VK_SHADER_STAGE_TASK_BIT_EXT,                offsetof(struct d3d12_pipeline_state_desc, as)},
        {VK_SHADER_STAGE_MESH_BIT_EXT,                offsetof(struct d3d12_pipeline_state_desc, ms)},
        {VK_SHADER_STAGE_FRAGMENT_BIT,                offsetof(struct d3d12_pipeline_state_desc, ps)},
    };

    graphics->stage_flags = vkd3d_pipeline_state_desc_get_shader_stages(desc);

    /* Defer taking ref-count until completion. */
    state->device = device;
    graphics->stage_count = 0;
    graphics->primitive_topology_type = desc->primitive_topology_type;

    state->pipeline_type = (graphics->stage_flags & VK_SHADER_STAGE_MESH_BIT_EXT)
            ? VKD3D_PIPELINE_TYPE_MESH_GRAPHICS
            : VKD3D_PIPELINE_TYPE_GRAPHICS;

    memset(&vs_input_signature, 0, sizeof(vs_input_signature));

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

    /* RADV screws up a bit when faced with depth-only mesh shaders.
     * - No cross-stage optimization ends up happening.
     * - Rendering glitches are observed. It's unclear why.
     * Adding a noop FS is trivial. */
    if (d3d12_graphics_pipeline_state_needs_noop_fs(device, graphics))
        graphics->stage_flags |= VK_SHADER_STAGE_FRAGMENT_BIT;

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
            format = NULL;
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
            WARN("Only one of BlendEnable or LogicOpEnable can be set to TRUE.\n");
            hr = E_INVALIDARG;
            goto fail;
        }

        blend_attachment_from_d3d12(&graphics->blend_attachments[i], rt_desc, format);

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
            graphics->ds_desc.depthTestEnable = VK_FALSE;
            graphics->ds_desc.depthBoundsTestEnable = VK_FALSE;
        }

        if (!(format->vk_aspect_mask & VK_IMAGE_ASPECT_STENCIL_BIT))
        {
            graphics->ds_desc.stencilTestEnable = VK_FALSE;
        }
    }

    /* If depth bias is enabled, do not ignore DSV since it affects depth bias scaling,
     * which in turn is relevant if the fragment shader consumes SV_POSITION.z. */
    if (graphics->ds_desc.depthTestEnable || graphics->ds_desc.stencilTestEnable || graphics->ds_desc.depthBoundsTestEnable ||
            desc->rasterizer_state.DepthBias != 0.0f || desc->rasterizer_state.SlopeScaledDepthBias ||
            (desc->flags & D3D12_PIPELINE_STATE_FLAG_DYNAMIC_DEPTH_BIAS))
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

    graphics->xfb_buffer_count = 0u;
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

        graphics->cached_desc.xfb_info = vkd3d_shader_transform_feedback_info_dup(so_desc);

        if (!graphics->cached_desc.xfb_info)
        {
            hr = E_OUTOFMEMORY;
            goto fail;
        }

        for (i = 0; i < graphics->cached_desc.xfb_info->element_count; i++)
        {
            if (graphics->cached_desc.xfb_info->elements[i].semantic_name)
            {
                graphics->xfb_buffer_count = max(graphics->xfb_buffer_count,
                        graphics->cached_desc.xfb_info->elements[i].output_slot + 1u);
            }
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
    memset(&pc_input_signature, 0, sizeof(pc_input_signature));
    memset(&pc_output_signature, 0, sizeof(pc_output_signature));
    memset(&io_output_signature, 0, sizeof(io_output_signature));

    curr_stage = VK_SHADER_STAGE_FLAG_BITS_MAX_ENUM;

    for (i = 0; i < ARRAY_SIZE(shader_stages_lut); ++i)
    {
        const D3D12_SHADER_BYTECODE *b = (const void *)((uintptr_t)desc + shader_stages_lut[i].offset);
        const struct vkd3d_shader_code dxbc = {b->pShaderBytecode, b->BytecodeLength};

        if (!(graphics->stage_flags & shader_stages_lut[i].stage))
            continue;

        /* Can happen for mesh -> fragment workaround. */
        if (!b->BytecodeLength)
        {
            graphics->cached_desc.bytecode[graphics->stage_count].BytecodeLength = 0;
            graphics->cached_desc.bytecode[graphics->stage_count].pShaderBytecode = NULL;
            graphics->cached_desc.bytecode_stages[graphics->stage_count] = shader_stages_lut[i].stage;
            ++graphics->stage_count;
            continue;
        }

        prev_stage = curr_stage;
        curr_stage = shader_stages_lut[i].stage;

        memset(&io_input_signature, 0, sizeof(io_input_signature));

        /* Ignore errors when a signature is not present. If a missing signature
         * leads to compatibility issues, validation will fail later anyway. */
        if (curr_stage == VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT ||
                curr_stage == VK_SHADER_STAGE_MESH_BIT_EXT)
            vkd3d_shader_parse_patch_constant_signature(&dxbc, &pc_output_signature);

        if (curr_stage == VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT)
        {
            vkd3d_shader_parse_patch_constant_signature(&dxbc, &pc_input_signature);

            if (!vkd3d_validate_shader_io_signatures(prev_stage, &pc_output_signature, curr_stage, &pc_input_signature))
            {
                hr = E_INVALIDARG;
                goto fail;
            }
        }

        if (curr_stage != VK_SHADER_STAGE_VERTEX_BIT &&
                curr_stage != VK_SHADER_STAGE_TASK_BIT_EXT &&
                curr_stage != VK_SHADER_STAGE_MESH_BIT_EXT)
        {
            vkd3d_shader_parse_input_signature(&dxbc, &io_input_signature);

            if (state->pipeline_type == VKD3D_PIPELINE_TYPE_MESH_GRAPHICS)
            {
                /* Mesh shaders are special since register assignment is based on semantics. */
                if (!vkd3d_validate_mesh_shader_io_signatures(&io_output_signature, &pc_output_signature, &io_input_signature))
                {
                    hr = E_INVALIDARG;
                    goto fail;
                }
            }
            else
            {
                if (!vkd3d_validate_shader_io_signatures(prev_stage,
                        &io_output_signature, curr_stage, &io_input_signature))
                {
                    hr = E_INVALIDARG;
                    goto fail;
                }
            }
        }

        vkd3d_shader_free_shader_signature(&io_input_signature);
        vkd3d_shader_free_shader_signature(&io_output_signature);

        memset(&io_output_signature, 0, sizeof(io_output_signature));

        /* Read output signature for validation purposes. */
        if (shader_stages_lut[i].stage != VK_SHADER_STAGE_TASK_BIT_EXT)
            vkd3d_shader_parse_output_signature(&dxbc, &io_output_signature);

        switch (shader_stages_lut[i].stage)
        {
            case VK_SHADER_STAGE_VERTEX_BIT:
                if ((ret = vkd3d_shader_parse_input_signature(&dxbc, &vs_input_signature)) < 0)
                {
                    hr = hresult_from_vkd3d_result(ret);
                    goto fail;
                }

                if (!vkd3d_validate_vertex_input_signature(&vs_input_signature, &desc->input_layout))
                {
                    hr = E_INVALIDARG;
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
                if (FAILED(hr = d3d12_pipeline_state_validate_blend_state(state, device, desc, &io_output_signature)))
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

    vkd3d_shader_free_shader_signature(&pc_input_signature);
    vkd3d_shader_free_shader_signature(&pc_output_signature);
    vkd3d_shader_free_shader_signature(&io_output_signature);

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

        if (!(signature_element = vkd3d_shader_find_signature_element(&vs_input_signature,
                e->SemanticName, e->SemanticIndex, 0)))
            continue;

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
                if (instance_divisor > vk_info->max_vertex_attrib_divisor)
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
    vkd3d_shader_free_shader_signature(&vs_input_signature);

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
            || (graphics->xfb_buffer_count && so_desc->RasterizedStream == D3D12_SO_NO_RASTERIZED_STREAM))
        graphics->rs_desc.rasterizerDiscardEnable = VK_TRUE;

    rs_line_info_from_d3d12(device, &graphics->rs_line_info, &graphics->rs_desc, &desc->rasterizer_state);
    rs_stream_info_from_d3d12(&graphics->rs_stream_info, &graphics->rs_desc, so_desc, vk_info);
    if (vk_info->EXT_conservative_rasterization)
        rs_conservative_info_from_d3d12(&graphics->rs_conservative_info, &graphics->rs_desc, &desc->rasterizer_state);
    if (vk_info->EXT_depth_clip_enable)
        rs_depth_clip_info_from_d3d12(&graphics->rs_depth_clip_info, &graphics->rs_desc, &desc->rasterizer_state);

    graphics->sample_mask = desc->sample_mask;

    graphics->ms_desc.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    graphics->ms_desc.pNext = NULL;
    graphics->ms_desc.flags = 0;
    graphics->ms_desc.rasterizationSamples = sample_count;
    graphics->ms_desc.sampleShadingEnable = VK_FALSE;
    graphics->ms_desc.minSampleShading = 0.0f;
    graphics->ms_desc.pSampleMask = &graphics->sample_mask;
    graphics->ms_desc.alphaToCoverageEnable = desc->blend_state.AlphaToCoverageEnable;
    graphics->ms_desc.alphaToOneEnable = VK_FALSE;

    if (desc->view_instancing_desc.ViewInstanceCount)
    {
        ERR("View instancing not supported.\n");
        hr = E_INVALIDARG;
        goto fail;
    }

    /* Tests show that D3D12 drivers behave as if D3D12_PIPELINE_STATE_FLAG_DYNAMIC_DEPTH_BIAS
     * was always set, however doing that would invalidate existing pipeline caches, so avoid
     * this until proven necessary. */
    if (desc->flags & D3D12_PIPELINE_STATE_FLAG_DYNAMIC_DEPTH_BIAS)
    {
        graphics->explicit_dynamic_states |= VKD3D_DYNAMIC_STATE_DEPTH_BIAS;
    }
    else if (device->device_info.depth_bias_control_features.depthBiasControl &&
            desc->rasterizer_state.DepthBias != 0.0f)
    {
        if (graphics->null_attachment_mask & dsv_attachment_mask(graphics))
        {
            /* Due to the way dynamic state works in D3D12, we can exploit this in
             * order to set up the correct depth bias representation at draw time. */
            graphics->explicit_dynamic_states |= VKD3D_DYNAMIC_STATE_DEPTH_BIAS;
        }
        else
        {
            vkd3d_get_depth_bias_representation(&graphics->rs_depth_bias_info, device, desc->dsv_format);
            vk_prepend_struct(&graphics->rs_desc, &graphics->rs_depth_bias_info);
        }
    }

    if ((desc->flags & D3D12_PIPELINE_STATE_FLAG_DYNAMIC_INDEX_BUFFER_STRIP_CUT) &&
            state->pipeline_type == VKD3D_PIPELINE_TYPE_GRAPHICS)
        graphics->explicit_dynamic_states |= VKD3D_DYNAMIC_STATE_PRIMITIVE_RESTART;

    return S_OK;

fail:
    vkd3d_shader_free_shader_signature(&vs_input_signature);
    return hr;
}

static HRESULT d3d12_pipeline_state_init_graphics_spirv(struct d3d12_pipeline_state *state,
        const struct d3d12_pipeline_state_desc *desc,
        const struct d3d12_cached_pipeline_state *cached_pso)
{
    struct d3d12_device *device = state->device;
    HRESULT hr;

    d3d12_pipeline_state_graphics_load_spirv_from_cached_state(state, device, desc, cached_pso);
    if (FAILED(hr = d3d12_pipeline_state_graphics_create_shader_stages(state, device, desc)))
        return hr;

    /* At this point, we will have valid meta structures set up.
     * Deduce further PSO information from these structs and perform
     * inter-stage validation. */
    return d3d12_pipeline_state_graphics_handle_meta(state, device);
}

static HRESULT d3d12_pipeline_state_init_static_pipeline(struct d3d12_pipeline_state *state,
        const struct d3d12_pipeline_state_desc *desc)
{
    struct d3d12_graphics_pipeline_state *graphics = &state->graphics;
    bool can_compile_pipeline_early, has_gpl, create_library = false;
    VkGraphicsPipelineLibraryFlagsEXT library_flags = 0;
    unsigned int i;

    for (i = 0; i < graphics->stage_count; i++)
        if (graphics->code[i].meta.flags & VKD3D_SHADER_META_FLAG_DISABLE_OPTIMIZATIONS)
            graphics->disable_optimization = true;

    has_gpl = state->device->device_info.graphics_pipeline_library_features.graphicsPipelineLibrary;

    library_flags = VK_GRAPHICS_PIPELINE_LIBRARY_VERTEX_INPUT_INTERFACE_BIT_EXT |
            VK_GRAPHICS_PIPELINE_LIBRARY_PRE_RASTERIZATION_SHADERS_BIT_EXT |
            VK_GRAPHICS_PIPELINE_LIBRARY_FRAGMENT_SHADER_BIT_EXT |
            VK_GRAPHICS_PIPELINE_LIBRARY_FRAGMENT_OUTPUT_INTERFACE_BIT_EXT;

    if (d3d12_graphics_pipeline_state_has_unknown_dsv_format_with_test(graphics))
    {
        library_flags &= ~VK_GRAPHICS_PIPELINE_LIBRARY_FRAGMENT_OUTPUT_INTERFACE_BIT_EXT;
        create_library = true;
    }

    if (graphics->stage_flags & VK_SHADER_STAGE_MESH_BIT_EXT)
    {
        can_compile_pipeline_early = true;

        library_flags &= ~VK_GRAPHICS_PIPELINE_LIBRARY_VERTEX_INPUT_INTERFACE_BIT_EXT;
        graphics->pipeline_layout = state->root_signature->mesh.vk_pipeline_layout;
    }
    else
    {
        /* Defer compilation if tessellation is enabled but the patch vertex count is not known */
        bool has_tess = !!(graphics->stage_flags & VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT);

        can_compile_pipeline_early = !has_tess || graphics->patch_vertex_count != 0 ||
                state->device->device_info.extended_dynamic_state2_features.extendedDynamicState2PatchControlPoints;

        if (desc->primitive_topology_type == D3D12_PRIMITIVE_TOPOLOGY_TYPE_UNDEFINED)
        {
            library_flags &= ~VK_GRAPHICS_PIPELINE_LIBRARY_VERTEX_INPUT_INTERFACE_BIT_EXT;
            create_library = true;

            can_compile_pipeline_early = false;
        }

        /* In case of tessellation shaders, we may have to recompile the pipeline with a
         * different patch vertex count, which is part of pre-rasterization state. Do not
         * create a pipeline library if dynamic patch control points are unsupported. */
        if (has_tess && !state->device->device_info.extended_dynamic_state2_features.extendedDynamicState2PatchControlPoints)
            create_library = false;

        graphics->pipeline_layout = state->root_signature->graphics.vk_pipeline_layout;
    }

    graphics->pipeline = VK_NULL_HANDLE;
    graphics->library = VK_NULL_HANDLE;
    graphics->library_flags = 0;
    graphics->library_create_flags = 0;

    if (create_library && has_gpl)
    {
        if (!(graphics->library = d3d12_pipeline_state_create_pipeline_variant(state, NULL, graphics->dsv_format,
                state->vk_pso_cache, library_flags, &graphics->pipeline_dynamic_states)))
            return E_OUTOFMEMORY;
    }

    if (can_compile_pipeline_early)
    {
        if (!(graphics->pipeline = d3d12_pipeline_state_create_pipeline_variant(state, NULL, graphics->dsv_format,
                state->vk_pso_cache, 0, &graphics->pipeline_dynamic_states)))
            return E_OUTOFMEMORY;
    }
    else
    {
        graphics->dsv_plane_optimal_mask = d3d12_graphics_pipeline_state_get_plane_optimal_mask(graphics, NULL);
    }

    return S_OK;
}

static HRESULT d3d12_pipeline_state_finish_graphics(struct d3d12_pipeline_state *state)
{
    struct d3d12_graphics_pipeline_state *graphics = &state->graphics;
    unsigned int i;
    void *new_code;
    HRESULT hr;

    /* We are basically forced to compile PSO late with proper DSV format.
     * This is an application bug if it happens.
     * If we couldn't compile pipeline early due to e.g. UNKNOWN topology or unknown number of patch control points,
     * we have to defer the compile, but this is esoteric behavior and should never happen in practice. */
    state->pso_is_fully_dynamic =
            graphics->pipeline &&
            !d3d12_graphics_pipeline_state_has_unknown_dsv_format_with_test(graphics);

    /* If we cannot adjust control points dynamically,
     * we are at risk of having to recompile PSO with different number of control points. */
    if (graphics->primitive_topology_type == D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH &&
            !(graphics->pipeline_dynamic_states & VKD3D_DYNAMIC_STATE_PATCH_CONTROL_POINTS))
        state->pso_is_fully_dynamic = false;

    /* Same thing if the pipeline is multisampled but we cannot dynamically set the sample count. */
    if (d3d12_graphics_pipeline_needs_dynamic_rasterization_samples(graphics) &&
            !(graphics->pipeline_dynamic_states & VKD3D_DYNAMIC_STATE_RASTERIZATION_SAMPLES))
        state->pso_is_fully_dynamic = false;

    if (!state->pso_is_fully_dynamic)
    {
        /* If we got here successfully without SPIR-V code,
         * it means we'll need to defer compilation from DXBC -> SPIR-V.
         * Dupe the DXBC code. */
        for (i = 0; i < graphics->stage_count; i++)
        {
            if (graphics->code[i].size || graphics->stages[i].module != VK_NULL_HANDLE ||
                    !graphics->cached_desc.bytecode[i].BytecodeLength)
                continue;

            new_code = vkd3d_malloc(graphics->cached_desc.bytecode[i].BytecodeLength);
            if (!new_code)
                return E_OUTOFMEMORY;
            memcpy(new_code, graphics->cached_desc.bytecode[i].pShaderBytecode,
                    graphics->cached_desc.bytecode[i].BytecodeLength);
            graphics->cached_desc.bytecode[i].pShaderBytecode = new_code;
            graphics->cached_desc.bytecode_duped_mask |= 1u << i;
        }
    }

    list_init(&graphics->compiled_fallback_pipelines);
    if (FAILED(hr = vkd3d_private_store_init(&state->private_store)))
        return hr;

    d3d_destruction_notifier_init(&state->destruction_notifier, (IUnknown*)&state->ID3D12PipelineState_iface);
    d3d12_device_add_ref(state->device);
    return S_OK;
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
    const struct D3D12_SHADER_BYTECODE *bytecode;
    ID3D12RootSignature *object = NULL;
    HRESULT hr;

    if (bind_point == VK_PIPELINE_BIND_POINT_COMPUTE)
        bytecode = &desc->cs;
    else if (desc->ms.BytecodeLength)
        bytecode = &desc->ms;
    else
        bytecode = &desc->vs;

    if (!bytecode->BytecodeLength)
        return E_INVALIDARG;

    if (FAILED(hr = ID3D12Device12_CreateRootSignature(&device->ID3D12Device_iface, 0,
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

    if (rwlock_init(&object->lock))
    {
        vkd3d_free(object);
        return E_FAIL;
    }

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
        object->pipeline_cache_compat.root_signature_compat_hash = object->root_signature->pso_compatibility_hash;

    desc_cached_pso = &desc->cached_pso;

    if (desc->cached_pso.blob.CachedBlobSizeInBytes)
    {
        hr = d3d12_cached_pipeline_state_validate(device, &desc->cached_pso, &object->pipeline_cache_compat);

        if ((vkd3d_config_flags & VKD3D_CONFIG_FLAG_PIPELINE_LIBRARY_IGNORE_MISMATCH_DRIVER) &&
                (hr == D3D12_ERROR_ADAPTER_NOT_FOUND || hr == D3D12_ERROR_DRIVER_VERSION_MISMATCH))
        {
            if (vkd3d_config_flags & VKD3D_CONFIG_FLAG_PIPELINE_LIBRARY_LOG)
                INFO("Ignoring mismatched driver for CachedPSO. Continuing as-if application did not provide cache.\n");
            hr = S_OK;
            memset(&cached_pso, 0, sizeof(cached_pso));
            desc_cached_pso = &cached_pso;
        }

        if (FAILED(hr))
        {
            if (object->root_signature)
                d3d12_root_signature_dec_ref(object->root_signature);
            rwlock_destroy(&object->lock);
            vkd3d_free(object);
            return hr;
        }
    }

    /* If we rely on internal shader cache, the PSO blob app provides us might be a pure metadata blob,
     * and therefore kinda useless. Try to use disk cache blob instead.
     * Also, consider that we might have to serialize this pipeline if we don't find anything in disk cache. */
    if (desc_cached_pso != &cached_pso && d3d12_cached_pipeline_state_is_dummy(desc_cached_pso))
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

    /* By using our own VkPipelineCache, drivers will generally not cache pipelines internally in memory.
     * For games that spam an extreme number of pipelines only to serialize them to pipeline libraries and then
     * release the pipeline state, we will run into memory issues on memory constrained systems since a driver might
     * be tempted to keep several gigabytes of PSO binaries live in memory.
     * A workaround (pilfered from Fossilize) is to create our own pipeline cache and destroy it.
     * Ideally there would be a flag to disable in-memory caching (but retain on-disk cache),
     * but that's extremely specific, so do what we gotta do. */

    if (SUCCEEDED(hr))
    {
        switch (bind_point)
        {
            case VK_PIPELINE_BIND_POINT_COMPUTE:
                hr = d3d12_pipeline_state_init_compute(object, device, desc, desc_cached_pso);
                break;

            case VK_PIPELINE_BIND_POINT_GRAPHICS:
                /* Creating a graphics PSO is more involved ... */
                hr = d3d12_pipeline_state_init_graphics_create_info(object, device, desc);
                if (SUCCEEDED(hr))
                    hr = d3d12_pipeline_state_init_graphics_spirv(object, desc, desc_cached_pso);
                if (SUCCEEDED(hr))
                    hr = d3d12_pipeline_state_init_static_pipeline(object, desc);
                if (SUCCEEDED(hr))
                    hr = d3d12_pipeline_state_finish_graphics(object);
                break;

            default:
                ERR("Invalid pipeline type %u.\n", bind_point);
                hr = E_INVALIDARG;
        }
    }

    if (FAILED(hr))
    {
        if (object->root_signature)
            d3d12_root_signature_dec_ref(object->root_signature);
        d3d12_pipeline_state_free_spirv_code(object);
        d3d12_pipeline_state_free_spirv_code_debug(object);
        d3d12_pipeline_state_destroy_shader_modules(object, device);
        if (object->pipeline_type == VKD3D_PIPELINE_TYPE_GRAPHICS || object->pipeline_type == VKD3D_PIPELINE_TYPE_MESH_GRAPHICS)
            d3d12_pipeline_state_free_cached_desc(&object->graphics.cached_desc);
        VK_CALL(vkDestroyPipelineCache(device->vk_device, object->vk_pso_cache, NULL));
        rwlock_destroy(&object->lock);

        vkd3d_free(object);
        return hr;
    }

    /* The strategy here is that we need to keep the SPIR-V alive somehow.
     * If we don't need to serialize SPIR-V from the PSO, then we don't need to keep the code alive as pointer/size pairs.
     * The scenarios for this case is:
     * - When we choose to not serialize SPIR-V at all with VKD3D_CONFIG
     * - PSO was loaded from a cached blob. It's extremely unlikely that anyone is going to try
     *   serializing that PSO again, so there should be no need to keep it alive.
     * - We are using a disk cache with SHADER_IDENTIFIER support.
     *   In this case, we'll never store the SPIR-V itself, but the identifier, so we don't need to keep the code around.
     *
     * The worst that would happen is a performance loss should that entry be reloaded later.
     * For graphics pipelines, we have to keep VkShaderModules around in case we need fallback pipelines.
     * If we keep the SPIR-V around in memory, we can always create shader modules on-demand in case we
     * need to actually create fallback pipelines. This avoids unnecessary memory bloat. */
    if (desc_cached_pso->blob.CachedBlobSizeInBytes ||
            (device->disk_cache.library && (device->disk_cache.library->flags & VKD3D_PIPELINE_LIBRARY_FLAG_SHADER_IDENTIFIER)) ||
            (vkd3d_config_flags & VKD3D_CONFIG_FLAG_PIPELINE_LIBRARY_NO_SERIALIZE_SPIRV))
        d3d12_pipeline_state_free_spirv_code(object);
    else
        d3d12_pipeline_state_destroy_shader_modules(object, device);

    /* If it is impossible for us to recompile this shader, we can free VkShaderModules. Saves a lot of memory.
     * If we are required to be able to serialize the SPIR-V, it will live as host pointers, not VkShaderModule. */
    if (object->pso_is_fully_dynamic)
        d3d12_pipeline_state_destroy_shader_modules(object, device);

    /* We don't expect to serialize the PSO blob if we loaded it from cache.
     * Free the cache now to save on memory. */
    if (desc_cached_pso->blob.CachedBlobSizeInBytes)
    {
        VK_CALL(vkDestroyPipelineCache(device->vk_device, object->vk_pso_cache, NULL));
        object->vk_pso_cache = VK_NULL_HANDLE;

        /* Set this explicitly so we avoid attempting to touch code[i] when serializing the PSO blob.
         * We are at risk of compiling code on the fly in some upcoming situations. */
        object->pso_is_loaded_from_cached_blob = true;
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

static VkPipeline d3d12_pipeline_state_find_compiled_pipeline(struct d3d12_pipeline_state *state,
        const struct vkd3d_pipeline_key *key, uint32_t *dynamic_state_flags)
{
    const struct d3d12_graphics_pipeline_state *graphics = &state->graphics;
    struct vkd3d_compiled_pipeline *current;
    VkPipeline vk_pipeline = VK_NULL_HANDLE;

    rwlock_lock_read(&state->lock);
    LIST_FOR_EACH_ENTRY(current, &graphics->compiled_fallback_pipelines, struct vkd3d_compiled_pipeline, entry)
    {
        if (!memcmp(&current->key, key, sizeof(*key)))
        {
            vk_pipeline = current->vk_pipeline;
            *dynamic_state_flags = current->dynamic_state_flags;
            break;
        }
    }
    rwlock_unlock_read(&state->lock);

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

    rwlock_lock_write(&state->lock);

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

    rwlock_unlock_write(&state->lock);
    return compiled_pipeline;
}

static void d3d12_pipeline_state_log_graphics_state(const struct d3d12_pipeline_state *state)
{
    const struct d3d12_graphics_pipeline_state *graphics = &state->graphics;
    unsigned int i, j;
    uint32_t divisor;

    ERR("Root signature: %"PRIx64"\n", state->root_signature->pso_compatibility_hash);

    for (i = 0; i < graphics->stage_count; i++)
        ERR("Shader %#x: %"PRIx64"\n", graphics->stages[i].stage, graphics->code[i].meta.hash);

    if (graphics->stage_flags & VK_SHADER_STAGE_VERTEX_BIT)
    {
        ERR("Topology: %u (patch vertex count: %u)\n", graphics->primitive_topology_type, graphics->patch_vertex_count);
        ERR("Vertex attributes: %zu\n", graphics->attribute_count);
        for (i = 0; i < graphics->attribute_count; i++)
        {
            const VkVertexInputAttributeDescription *attr = &graphics->attributes[i];
            ERR("  %u: binding %u, format %u, location %u, offset %u\n", i, attr->binding, attr->format, attr->location, attr->offset);
        }

        ERR("Vertex bindings: %zu.\n", graphics->attribute_binding_count);
        for (i = 0; i < graphics->attribute_binding_count; i++)
        {
            const VkVertexInputBindingDescription *binding = &graphics->attribute_bindings[i];

            divisor = 1u;

            for (j = 0; j < graphics->instance_divisor_count; j++)
            {
                if (graphics->instance_divisors[j].binding == binding->binding)
                    divisor = graphics->instance_divisors[j].divisor;
            }

            ERR("  %u: binding %u, input rate %u, stride %u, divisor %u\n", i, binding->binding, binding->inputRate, binding->stride, divisor);
        }
    }

    if (graphics->cached_desc.xfb_info)
    {
        ERR("XFB (stage %u):\n", graphics->cached_desc.xfb_stage);

        for (i = 0; i < graphics->cached_desc.xfb_info->element_count; i++)
        {
            const struct vkd3d_shader_transform_feedback_element *elem = &graphics->cached_desc.xfb_info->elements[i];

            ERR("  Element %u: stream %u, semantic %s%u, components %#x, output %u\n", i,
                    elem->stream_index, elem->semantic_name, elem->semantic_index,
                    ((1u << elem->component_count) - 1u) << elem->component_index,
                    elem->output_slot);
        }

        for (i = 0; i < graphics->cached_desc.xfb_info->buffer_stride_count; i++)
            ERR("  Buffer %u: stride %u\n", i, graphics->cached_desc.xfb_info->buffer_strides[i]);
    }

    if (graphics->rt_count)
    {
        ERR("RTVs: %u\n", graphics->rt_count);

        for (i = 0; i < graphics->rt_count; i++)
        {
            const VkPipelineColorBlendAttachmentState *blend = &graphics->blend_attachments[i];

            ERR("  %u: %u (blend enable %u, write mask %#x)\n", i, graphics->rtv_formats[i],
                    blend->blendEnable, blend->colorWriteMask);
        }
    }

    if (graphics->dsv_format)
    {
        ERR("DSV: #%x\n", graphics->dsv_format->dxgi_format);
        ERR("  Depth test: %u (write: %u)\n", graphics->ds_desc.depthTestEnable, graphics->ds_desc.depthWriteEnable);
        ERR("  Depth bounds test: %u\n", graphics->ds_desc.depthBoundsTestEnable);

        if (graphics->dsv_format->vk_aspect_mask & VK_IMAGE_ASPECT_STENCIL_BIT)
        {
            ERR("  Stencil test: %u (write: %u)\n", graphics->ds_desc.stencilTestEnable, graphics->ds_desc.stencilTestEnable &&
                    (graphics->ds_desc.front.writeMask || graphics->ds_desc.back.writeMask));
        }
    }

    ERR("Logic op enabled: %u (logic op: %u)\n",
            graphics->blend_desc.logicOpEnable, graphics->blend_desc.logicOp);

    ERR("Sample count: %u (mask: %#x, sample shading %u, alpha to coverage %u)\n",
            graphics->ms_desc.rasterizationSamples, graphics->sample_mask,
            graphics->ms_desc.sampleShadingEnable, graphics->ms_desc.alphaToCoverageEnable);

    if (!graphics->rs_desc.rasterizerDiscardEnable)
    {
        ERR("Rasterizer state:\n");
        ERR("  Polygon mode: %u\n", graphics->rs_desc.polygonMode);
        ERR("  Line mode: %u (width: %f)\n", graphics->rs_line_info.lineRasterizationMode, graphics->rs_desc.lineWidth);
        ERR("  Conservative: %u\n", graphics->rs_conservative_info.conservativeRasterizationMode);
    }

    ERR("Dynamic state: %#x (explicit: %#x)\n", graphics->pipeline_dynamic_states, graphics->explicit_dynamic_states);
}

static VkResult d3d12_pipeline_state_link_pipeline_variant(struct d3d12_pipeline_state *state,
        const struct vkd3d_pipeline_key *key, const struct vkd3d_format *dsv_format, VkPipelineCache vk_cache,
        uint32_t dynamic_state_flags, VkPipeline *vk_pipeline)
{
    const struct vkd3d_vk_device_procs *vk_procs = &state->device->vk_procs;
    struct d3d12_graphics_pipeline_state *graphics = &state->graphics;
    struct vkd3d_fragment_output_pipeline_desc fragment_output_desc;
    struct vkd3d_vertex_input_pipeline_desc vertex_input_desc;
    struct vkd3d_queue_timeline_trace_cookie cookie;
    VkPipelineLibraryCreateInfoKHR library_info;
    VkGraphicsPipelineCreateInfo create_info;
    VkPipeline vk_libraries[3];
    uint32_t library_count = 0;
    VkResult vr;

    vk_libraries[library_count++] = graphics->library;

    if ((!(graphics->stage_flags & VK_SHADER_STAGE_MESH_BIT_EXT)) &&
            (!(graphics->library_flags & VK_GRAPHICS_PIPELINE_LIBRARY_VERTEX_INPUT_INTERFACE_BIT_EXT)))
    {
        vkd3d_vertex_input_pipeline_desc_init(&vertex_input_desc, state, key, dynamic_state_flags);
        vk_libraries[library_count++] = d3d12_device_get_or_create_vertex_input_pipeline(state->device, &vertex_input_desc);
    }

    if (!(graphics->library_flags & VK_GRAPHICS_PIPELINE_LIBRARY_FRAGMENT_OUTPUT_INTERFACE_BIT_EXT))
    {
        vkd3d_fragment_output_pipeline_desc_init(&fragment_output_desc, state, dsv_format, dynamic_state_flags);
        vk_libraries[library_count++] = d3d12_device_get_or_create_fragment_output_pipeline(state->device, &fragment_output_desc);
    }

    memset(&library_info, 0, sizeof(library_info));
    library_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LIBRARY_CREATE_INFO_KHR;
    library_info.libraryCount = library_count;
    library_info.pLibraries = vk_libraries;

    memset(&create_info, 0, sizeof(create_info));
    create_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    create_info.pNext = &library_info;
    create_info.flags = graphics->library_create_flags;
    create_info.layout = graphics->pipeline_layout;
    create_info.basePipelineIndex = -1;

    if (d3d12_device_uses_descriptor_buffers(state->device))
        create_info.flags |= VK_PIPELINE_CREATE_DESCRIPTOR_BUFFER_BIT_EXT;

    if (graphics->disable_optimization)
        create_info.flags |= VK_PIPELINE_CREATE_DISABLE_OPTIMIZATION_BIT;

    /* Only use LINK_TIME_OPTIMIZATION for the primary pipeline for now,
     * accept a small runtime perf hit on subsequent compiles in order
     * to avoid stutter. */
    if (!key)
        create_info.flags |= VK_PIPELINE_CREATE_LINK_TIME_OPTIMIZATION_BIT_EXT;

    cookie = vkd3d_queue_timeline_trace_register_pso_compile(&state->device->queue_timeline_trace);

    vr = VK_CALL(vkCreateGraphicsPipelines(state->device->vk_device,
            vk_cache, 1, &create_info, NULL, vk_pipeline));

    if (vkd3d_queue_timeline_trace_cookie_is_valid(cookie))
    {
        const char *kind = vr == VK_SUCCESS ? "LINK OK" : "LINK ERR";
        vkd3d_queue_timeline_trace_complete_pso_compile(&state->device->queue_timeline_trace,
                cookie, vkd3d_pipeline_cache_compatibility_condense(&state->pipeline_cache_compat), kind);
    }

    if (vr != VK_SUCCESS && vr != VK_PIPELINE_COMPILE_REQUIRED)
    {
        ERR("Failed to link pipeline variant, vr %d.\n", vr);
        d3d12_pipeline_state_log_graphics_state(state);
    }

    return vr;
}

VkPipeline d3d12_pipeline_state_create_pipeline_variant(struct d3d12_pipeline_state *state,
        const struct vkd3d_pipeline_key *key, const struct vkd3d_format *dsv_format, VkPipelineCache vk_cache,
        VkGraphicsPipelineLibraryFlagsEXT library_flags, uint32_t *dynamic_state_flags)
{
    const struct vkd3d_vk_device_procs *vk_procs = &state->device->vk_procs;
    VkDynamicState dynamic_state_buffer[ARRAY_SIZE(vkd3d_dynamic_state_list)];
    struct d3d12_graphics_pipeline_state *graphics = &state->graphics;
    VkPipelineCreationFeedbackEXT feedbacks[VKD3D_MAX_SHADER_STAGES];
    struct vkd3d_fragment_output_pipeline_desc fragment_output_desc;
    VkPipelineShaderStageCreateInfo stages[VKD3D_MAX_SHADER_STAGES];
    VkGraphicsPipelineLibraryCreateInfoEXT library_create_info;
    struct vkd3d_vertex_input_pipeline_desc vertex_input_desc;
    VkPipelineTessellationStateCreateInfo tessellation_info;
    bool has_vertex_input_state, has_fragment_output_state;
    VkPipelineCreationFeedbackCreateInfoEXT feedback_info;
    VkPipelineMultisampleStateCreateInfo multisample_info;
    VkPipelineDynamicStateCreateInfo dynamic_create_info;
    struct vkd3d_queue_timeline_trace_cookie cookie;
    struct d3d12_device *device = state->device;
    VkGraphicsPipelineCreateInfo pipeline_desc;
    VkPipelineViewportStateCreateInfo vp_desc;
    VkPipelineCreationFeedbackEXT feedback;
    uint32_t stages_module_dup_mask = 0;
    VkPipeline vk_pipeline;
    unsigned int i;
    VkResult vr;
    HRESULT hr;

    *dynamic_state_flags = d3d12_graphics_pipeline_state_init_dynamic_state(state, &dynamic_create_info,
            dynamic_state_buffer, key);

    if (!library_flags && graphics->library)
    {
        if (d3d12_pipeline_state_link_pipeline_variant(state, key, dsv_format,
                vk_cache, *dynamic_state_flags, &vk_pipeline) == VK_SUCCESS)
            return vk_pipeline;
    }

    has_vertex_input_state = !(graphics->stage_flags & VK_SHADER_STAGE_MESH_BIT_EXT) &&
            (!library_flags || (library_flags & VK_GRAPHICS_PIPELINE_LIBRARY_VERTEX_INPUT_INTERFACE_BIT_EXT));

    has_fragment_output_state = !library_flags || (library_flags & VK_GRAPHICS_PIPELINE_LIBRARY_FRAGMENT_OUTPUT_INTERFACE_BIT_EXT);

    if (!(graphics->stage_flags & VK_SHADER_STAGE_MESH_BIT_EXT))
    {
        vkd3d_vertex_input_pipeline_desc_init(&vertex_input_desc, state, key, *dynamic_state_flags);
        vkd3d_vertex_input_pipeline_desc_prepare(&vertex_input_desc);

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

    vkd3d_fragment_output_pipeline_desc_init(&fragment_output_desc, state, dsv_format, *dynamic_state_flags);
    vkd3d_fragment_output_pipeline_desc_prepare(&fragment_output_desc);

    memset(&pipeline_desc, 0, sizeof(pipeline_desc));
    pipeline_desc.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipeline_desc.pNext = &fragment_output_desc.rt_info;
    pipeline_desc.stageCount = graphics->stage_count;
    pipeline_desc.pStages = graphics->stages;
    pipeline_desc.pViewportState = &vp_desc;
    pipeline_desc.pRasterizationState = &graphics->rs_desc;
    pipeline_desc.pDepthStencilState = &graphics->ds_desc;
    pipeline_desc.pDynamicState = &dynamic_create_info;
    pipeline_desc.layout = graphics->pipeline_layout;
    pipeline_desc.basePipelineIndex = -1;

    if (d3d12_device_supports_variable_shading_rate_tier_2(device))
        pipeline_desc.flags |= VK_PIPELINE_CREATE_RENDERING_FRAGMENT_SHADING_RATE_ATTACHMENT_BIT_KHR;

    if (graphics->stage_flags & VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT)
        pipeline_desc.pTessellationState = &tessellation_info;

    if (has_vertex_input_state)
    {
        pipeline_desc.pVertexInputState = &vertex_input_desc.vi_info;
        pipeline_desc.pInputAssemblyState = &vertex_input_desc.ia_info;
    }

    if (has_fragment_output_state || graphics->ms_desc.sampleShadingEnable)
    {
        multisample_info = graphics->ms_desc;

        if (key && key->rasterization_samples)
            multisample_info.rasterizationSamples = key->rasterization_samples;

        pipeline_desc.pMultisampleState = &multisample_info;
    }

    if (has_fragment_output_state)
        pipeline_desc.pColorBlendState = &fragment_output_desc.cb_info;

    if (library_flags)
    {
        TRACE("Compiling pipeline library for %p with flags %#x.\n", state, library_flags);

        library_create_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_LIBRARY_CREATE_INFO_EXT;
        /* Explicit cast to silence a constness warning, this seems to be a Vulkan header bug */
        library_create_info.pNext = (void*)pipeline_desc.pNext;
        library_create_info.flags = library_flags;

        pipeline_desc.pNext = &library_create_info;
        pipeline_desc.flags |= VK_PIPELINE_CREATE_LIBRARY_BIT_KHR |
                VK_PIPELINE_CREATE_RETAIN_LINK_TIME_OPTIMIZATION_INFO_BIT_EXT;

        graphics->library_flags = library_flags;
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
        /* Need a reader lock here since a concurrent vkd3d_late_compile_shader_stages can
         * touch the VkShaderModule and/or code[] array. When late compile has been called once,
         * we will always have a concrete VkShaderModule to work with. */
        rwlock_lock_read(&state->lock);

        /* In a fallback pipeline, we might have to re-create shader modules.
         * This can happen from multiple threads, so need temporary pStages array. */
        memcpy(stages, graphics->stages, graphics->stage_count * sizeof(stages[0]));

        for (i = 0; i < graphics->stage_count; i++)
        {
            if (stages[i].module == VK_NULL_HANDLE && graphics->code[i].code)
            {
                if (FAILED(hr = d3d12_pipeline_state_create_shader_module(device,
                        &stages[i].module, &graphics->code[i])))
                {
                    /* This is kind of fatal and should only happen for out-of-memory. */
                    ERR("Unexpected failure (hr %x) in creating fallback SPIR-V module.\n", hr);
                    rwlock_unlock_read(&state->lock);
                    vk_pipeline = VK_NULL_HANDLE;
                    goto err;
                }

                pipeline_desc.pStages = stages;
                /* Remember to free this module. */
                stages_module_dup_mask |= 1u << i;
            }
        }

        rwlock_unlock_read(&state->lock);
    }

    /* If we're using identifiers, set the appropriate flag. */
    for (i = 0; i < graphics->stage_count; i++)
        if (pipeline_desc.pStages[i].module == VK_NULL_HANDLE)
            pipeline_desc.flags |= VK_PIPELINE_CREATE_FAIL_ON_PIPELINE_COMPILE_REQUIRED_BIT;

    if (d3d12_device_uses_descriptor_buffers(device))
        pipeline_desc.flags |= VK_PIPELINE_CREATE_DESCRIPTOR_BUFFER_BIT_EXT;

    if (graphics->disable_optimization)
        pipeline_desc.flags |= VK_PIPELINE_CREATE_DISABLE_OPTIMIZATION_BIT;

    TRACE("Calling vkCreateGraphicsPipelines.\n");

    if (vkd3d_config_flags & VKD3D_CONFIG_FLAG_PIPELINE_LIBRARY_LOG)
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

    cookie = vkd3d_queue_timeline_trace_register_pso_compile(&device->queue_timeline_trace);

    vr = VK_CALL(vkCreateGraphicsPipelines(device->vk_device, vk_cache, 1, &pipeline_desc, NULL, &vk_pipeline));

    if (vkd3d_queue_timeline_trace_cookie_is_valid(cookie))
    {
        const char *kind;

        if (vr == VK_SUCCESS)
        {
            if (pipeline_desc.flags & VK_PIPELINE_CREATE_FAIL_ON_PIPELINE_COMPILE_REQUIRED_BIT)
                kind = "GFX IDENT OK";
            else
                kind = "GFX OK";
        }
        else if (vr == VK_PIPELINE_COMPILE_REQUIRED)
            kind = "GFX MISS";
        else
            kind = "GFX ERR";

        vkd3d_queue_timeline_trace_complete_pso_compile(&device->queue_timeline_trace,
                cookie, vkd3d_pipeline_cache_compatibility_condense(&state->pipeline_cache_compat), kind);
    }

    if (vkd3d_config_flags & VKD3D_CONFIG_FLAG_PIPELINE_LIBRARY_LOG)
    {
        if (pipeline_desc.flags & VK_PIPELINE_CREATE_FAIL_ON_PIPELINE_COMPILE_REQUIRED_BIT)
        {
            if (vr == VK_SUCCESS)
                INFO("[IDENTIFIER] Successfully created graphics pipeline from identifier.\n");
            else if (vr == VK_PIPELINE_COMPILE_REQUIRED)
                INFO("[IDENTIFIER] Failed to create graphics pipeline from identifier, falling back ...\n");
        }
        else
            INFO("[IDENTIFIER] No graphics identifier\n");
    }

    if (vr == VK_PIPELINE_COMPILE_REQUIRED)
    {
        if (FAILED(hr = vkd3d_late_compile_shader_stages(state)))
        {
            ERR("Late compilation of SPIR-V failed.\n");
            vk_pipeline = VK_NULL_HANDLE;
            goto err;
        }

        pipeline_desc.flags &= ~VK_PIPELINE_CREATE_FAIL_ON_PIPELINE_COMPILE_REQUIRED_BIT;
        /* Internal modules are known to be non-null now. */
        pipeline_desc.pStages = state->graphics.stages;

        cookie = vkd3d_queue_timeline_trace_register_pso_compile(&device->queue_timeline_trace);
        vr = VK_CALL(vkCreateGraphicsPipelines(device->vk_device, vk_cache, 1, &pipeline_desc, NULL, &vk_pipeline));

        if (vkd3d_queue_timeline_trace_cookie_is_valid(cookie))
        {
            const char *kind = vr == VK_SUCCESS ? "FALLBACK OK" : "FALLBACK ERR";
            vkd3d_queue_timeline_trace_complete_pso_compile(&device->queue_timeline_trace,
                    cookie, vkd3d_pipeline_cache_compatibility_condense(&state->pipeline_cache_compat), kind);
        }
    }

    TRACE("Completed vkCreateGraphicsPipelines.\n");

    if (vr < 0)
    {
        ERR("Failed to create Vulkan graphics pipeline, vr %d.\n", vr);
        d3d12_pipeline_state_log_graphics_state(state);

        vk_pipeline = VK_NULL_HANDLE;
        goto err;
    }

    if (feedback_info.pipelineStageCreationFeedbackCount)
        vkd3d_report_pipeline_creation_feedback_results(&feedback_info);

    if (library_flags)
        graphics->library_create_flags = pipeline_desc.flags & VK_PIPELINE_CREATE_FAIL_ON_PIPELINE_COMPILE_REQUIRED_BIT;

err:
    /* Clean up any temporary SPIR-V modules we created. */
    while (stages_module_dup_mask)
    {
        i = vkd3d_bitmask_iter32(&stages_module_dup_mask);
        VK_CALL(vkDestroyShaderModule(device->vk_device, stages[i].module, NULL));
    }

    return vk_pipeline;
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

    /* We are normally supposed to analyze VBO strides to ensure that stride >= offset || stride == 0,
     * otherwise fall back. However, this means that we can never use fully dynamic graphics pipelines since
     * it's impossible to guarantee that strides are sensible, which would be a shame.
     * VK_EXT_dynamic_vertex_input is a possibility, but for pragmatic reasons we just ignore this problem for now.
     * It rarely, if ever comes up in practice since it's a somewhat nonsensical API pattern.
     * It does not work properly on native D3D12 AMD drivers either based on testing,
     * and RADV implements VBOs in a way such that it works anyways.
     * This scenario is covered by our test suite and we can revisit this if this turns out to be a real problem. */

    /* It should be illegal to use different patch size for topology compared to pipeline, but be safe here. */
    if (dyn_state->vk_primitive_topology == VK_PRIMITIVE_TOPOLOGY_PATCH_LIST &&
        !(graphics->pipeline_dynamic_states & VKD3D_DYNAMIC_STATE_PATCH_CONTROL_POINTS) &&
        (dyn_state->primitive_topology - D3D_PRIMITIVE_TOPOLOGY_1_CONTROL_POINT_PATCHLIST + 1) != graphics->patch_vertex_count)
    {
        if (graphics->patch_vertex_count)
        {
            WARN("Mismatch in tessellation control points, expected %u, but got %u, ignoring app topology and using shader.\n",
                  graphics->patch_vertex_count,
                  dyn_state->primitive_topology - D3D_PRIMITIVE_TOPOLOGY_1_CONTROL_POINT_PATCHLIST + 1);
        }

        return VK_NULL_HANDLE;
    }

    /* We also need a fallback pipeline if sample counts do not match. */
    if (dyn_state->rasterization_samples && dyn_state->rasterization_samples != state->graphics.ms_desc.rasterizationSamples &&
            !(graphics->pipeline_dynamic_states & VKD3D_DYNAMIC_STATE_RASTERIZATION_SAMPLES))
    {
        WARN("Mismatch in sample count, pipeline expects %u, but render target has %u.\n",
                state->graphics.ms_desc.rasterizationSamples, dyn_state->rasterization_samples);

        if (state->graphics.ms_desc.rasterizationSamples > VK_SAMPLE_COUNT_1_BIT)
            return VK_NULL_HANDLE;
    }

    *dynamic_state_flags = state->graphics.pipeline_dynamic_states;
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
    VkPipeline vk_pipeline;

    assert(d3d12_pipeline_state_is_graphics(state));

    /* If we have a fully dynamic PSO we have released all references to code, so this code path should never be hit. */
    assert(!state->pso_is_fully_dynamic);

    memset(&pipeline_key, 0, sizeof(pipeline_key));

    /* Try to keep as much dynamic state as possible so we don't have to rebind state unnecessarily. */
    if (!(graphics->stage_flags & VK_SHADER_STAGE_MESH_BIT_EXT))
    {
        if (graphics->primitive_topology_type != D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH &&
            graphics->primitive_topology_type != D3D12_PRIMITIVE_TOPOLOGY_TYPE_UNDEFINED)
            pipeline_key.dynamic_topology = true;
        else
            pipeline_key.topology = dyn_state->primitive_topology;
    }

    pipeline_key.dsv_format = dsv_format ? dsv_format->vk_format : VK_FORMAT_UNDEFINED;

    if (!(graphics->pipeline_dynamic_states & VKD3D_DYNAMIC_STATE_RASTERIZATION_SAMPLES))
    {
        pipeline_key.rasterization_samples = dyn_state->rasterization_samples
              ? dyn_state->rasterization_samples : graphics->ms_desc.rasterizationSamples;
    }

    if ((vk_pipeline = d3d12_pipeline_state_find_compiled_pipeline(state, &pipeline_key, dynamic_state_flags)))
    {
        return vk_pipeline;
    }

    FIXME("Compiling a fallback pipeline late!\n");

    vk_pipeline = d3d12_pipeline_state_create_pipeline_variant(state,
            &pipeline_key, dsv_format, VK_NULL_HANDLE, 0, dynamic_state_flags);

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

static uint32_t d3d12_max_descriptor_count_from_heap_type(struct d3d12_device *device, D3D12_DESCRIPTOR_HEAP_TYPE heap_type)
{
    uint32_t count = d3d12_device_get_max_descriptor_heap_size(device, heap_type);

    if (heap_type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV &&
            vkd3d_descriptor_debug_active_descriptor_qa_checks())
        count += VKD3D_DESCRIPTOR_DEBUG_NUM_PAD_DESCRIPTORS;

    return count;
}

static uint32_t d3d12_max_host_descriptor_count_from_heap_type(struct d3d12_device *device, D3D12_DESCRIPTOR_HEAP_TYPE heap_type)
{
    const VkPhysicalDeviceVulkan12Properties *limits = &device->device_info.vulkan_1_2_properties;

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

static uint32_t vkd3d_bindless_build_mutable_type_list(VkDescriptorType *list, uint32_t bindless_flags, uint32_t set_flags)
{
    uint32_t count = 0;

    if (set_flags & VKD3D_BINDLESS_SET_MUTABLE_RAW)
    {
        list[count++] = (bindless_flags & VKD3D_BINDLESS_CBV_AS_SSBO) ?
                VK_DESCRIPTOR_TYPE_STORAGE_BUFFER : VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;

        if ((bindless_flags & VKD3D_BINDLESS_RAW_SSBO) && !(bindless_flags & VKD3D_BINDLESS_CBV_AS_SSBO))
            list[count++] = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    }

    if (set_flags & VKD3D_BINDLESS_SET_MUTABLE_TYPED)
    {
        list[count++] = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        list[count++] = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
        list[count++] = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        list[count++] = VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER;
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
VKD3D_DECL_DESCRIPTOR_COPY_SIZE(4)
VKD3D_DECL_DESCRIPTOR_COPY_SIZE(8)
VKD3D_DECL_DESCRIPTOR_COPY_SIZE(16)
VKD3D_DECL_DESCRIPTOR_COPY_SIZE(32)
VKD3D_DECL_DESCRIPTOR_COPY_SIZE(48)
VKD3D_DECL_DESCRIPTOR_COPY_SIZE(64)
VKD3D_DECL_DESCRIPTOR_COPY_SIZE(128)

static pfn_vkd3d_host_mapping_copy_template vkd3d_bindless_find_copy_template(uint32_t descriptor_size)
{
    switch (descriptor_size)
    {
        case 4:
            return vkd3d_descriptor_copy_desc_4;
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
        case 128:
            return vkd3d_descriptor_copy_desc_128;
        default:
            break;
    }

    return NULL;
}

static pfn_vkd3d_host_mapping_copy_template_single vkd3d_bindless_find_copy_template_single(uint32_t descriptor_size)
{
    switch (descriptor_size)
    {
        case 4:
            return vkd3d_descriptor_copy_desc_4_single;
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
        case 128:
            return vkd3d_descriptor_copy_desc_128_single;
        default:
            break;
    }

    return NULL;
}

static uint32_t vkd3d_get_descriptor_size_for_type(struct d3d12_device *device, VkDescriptorType vk_descriptor_type)
{
    const VkPhysicalDeviceDescriptorBufferPropertiesEXT *props = &device->device_info.descriptor_buffer_properties;
    switch (vk_descriptor_type)
    {
        case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
            return props->sampledImageDescriptorSize;
        case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
            return props->storageImageDescriptorSize;
        case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
            return props->robustUniformBufferDescriptorSize;
        case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
            return props->robustStorageBufferDescriptorSize;
        case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
            return props->robustUniformTexelBufferDescriptorSize;
        case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
            return props->robustStorageTexelBufferDescriptorSize;
        case VK_DESCRIPTOR_TYPE_SAMPLER:
            return props->samplerDescriptorSize;
        default:
            assert(0 && "Invalid descriptor type.");
            return 0;
    }
}

static uint32_t vkd3d_get_descriptor_size_for_binding(struct d3d12_device *device,
        const VkDescriptorSetLayoutCreateInfo *set_layout_info, uint32_t binding_index)
{
    const VkDescriptorSetLayoutBinding *vk_binding = &set_layout_info->pBindings[binding_index];
    const VkMutableDescriptorTypeCreateInfoEXT *mutable;
    const VkMutableDescriptorTypeListEXT *type_list;
    uint32_t type_size;
    uint32_t max_size;
    uint32_t i;

    if (vk_binding->descriptorType != VK_DESCRIPTOR_TYPE_MUTABLE_EXT)
        max_size = vkd3d_get_descriptor_size_for_type(device, vk_binding->descriptorType);
    else
    {
        mutable = vk_find_pnext(set_layout_info->pNext, VK_STRUCTURE_TYPE_MUTABLE_DESCRIPTOR_TYPE_CREATE_INFO_EXT);
        type_list = &mutable->pMutableDescriptorTypeLists[binding_index];

        max_size = 0;
        for (i = 0; i < type_list->descriptorTypeCount; i++)
        {
            type_size = vkd3d_get_descriptor_size_for_type(device, type_list->pDescriptorTypes[i]);
            max_size = max(max_size, type_size);
        }
    }

    return max_size;
}

static HRESULT vkd3d_bindless_state_add_binding(struct vkd3d_bindless_state *bindless_state,
        struct d3d12_device *device, uint32_t flags,
        VkDescriptorType vk_descriptor_type, VkDescriptorType vk_init_null_descriptor_type)
{
    VkMutableDescriptorTypeListEXT mutable_descriptor_list[VKD3D_BINDLESS_SET_MAX_EXTRA_BINDINGS + 1];
    struct vkd3d_bindless_set_info *set_info = &bindless_state->set_info[bindless_state->set_count];
    VkDescriptorSetLayoutBinding vk_binding_info[VKD3D_BINDLESS_SET_MAX_EXTRA_BINDINGS + 1];
    VkDescriptorBindingFlags vk_binding_flags[VKD3D_BINDLESS_SET_MAX_EXTRA_BINDINGS + 1];
    VkDescriptorType mutable_descriptor_types[VKD3D_MAX_MUTABLE_DESCRIPTOR_TYPES];
    VkDescriptorSetLayoutBindingFlagsCreateInfo vk_binding_flags_info;
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    VkDescriptorSetLayoutCreateInfo vk_set_layout_info;
    VkMutableDescriptorTypeCreateInfoEXT mutable_info;
    VkDescriptorSetLayoutBinding *vk_binding;
    VkDeviceSize desc_offset;
    unsigned int i;
    VkResult vr;

    set_info->vk_descriptor_type = vk_descriptor_type;
    set_info->vk_init_null_descriptor_type = vk_init_null_descriptor_type;
    set_info->heap_type = flags & VKD3D_BINDLESS_SET_SAMPLER
            ? D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER
            : D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    set_info->flags = flags;
    set_info->binding_index = vkd3d_popcount(flags & VKD3D_BINDLESS_SET_EXTRA_MASK);

    bindless_state->vk_descriptor_buffer_indices[bindless_state->set_count] =
            set_info->heap_type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV ? 0 : 1;

    if (set_info->heap_type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV)
        set_info->set_index = bindless_state->cbv_srv_uav_count++;
    else
        set_info->set_index = 0;

    for (i = 0; i < set_info->binding_index; i++)
    {
        /* all extra bindings are storage buffers right now */
        vk_binding_info[i].binding = i;
        vk_binding_info[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;

        /* Coerce drivers into doing what we want w.r.t. alignment.
         * When we map a host pointer (page aligned) and offset it,
         * we need the offset to be aligned to at least 32.
         * That way we can use lower bits to encode other things.
         * We cannot control exactly how drivers allocate this.
         * Even if we only access a descriptor as non-arrayed descriptor,
         * it is allowed to use descriptorCount > 1,
         * see https://registry.khronos.org/vulkan/specs/1.3-extensions/html/vkspec.html#interfaces-resources-setandbinding.
         * To improve potential false sharing if different threads poke at adjacent descriptors,
         * align to 64 byte. Should also improve write-combined performance. */
        if (d3d12_device_use_embedded_mutable_descriptors(device) &&
                set_info->binding_index == 1 &&
                device->device_info.descriptor_buffer_properties.robustStorageBufferDescriptorSize < 64)
        {
            vk_binding_info[i].descriptorCount =
                    64u / device->device_info.descriptor_buffer_properties.robustStorageBufferDescriptorSize;
        }
        else
            vk_binding_info[i].descriptorCount = 1;

        vk_binding_info[i].stageFlags = VK_SHADER_STAGE_ALL;
        vk_binding_info[i].pImmutableSamplers = NULL;

        vk_binding_flags[i] = 0;
    }

    vk_binding = &vk_binding_info[set_info->binding_index];
    vk_binding->binding = set_info->binding_index;
    vk_binding->descriptorType = set_info->vk_descriptor_type;
    vk_binding->descriptorCount = d3d12_max_descriptor_count_from_heap_type(device, set_info->heap_type);
    vk_binding->stageFlags = VK_SHADER_STAGE_ALL;
    vk_binding->pImmutableSamplers = NULL;

    if (d3d12_device_uses_descriptor_buffers(device))
    {
        /* All update-after-bind features are implied when using descriptor buffers. */
        vk_binding_flags[set_info->binding_index] = VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT;
        vk_set_layout_info.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_DESCRIPTOR_BUFFER_BIT_EXT;
    }
    else
    {
         vk_binding_flags[set_info->binding_index] = VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT |
                VK_DESCRIPTOR_BINDING_UPDATE_UNUSED_WHILE_PENDING_BIT |
                VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT |
                VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT;
        vk_set_layout_info.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
    }

    vk_binding_flags_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
    vk_binding_flags_info.pNext = NULL;
    vk_binding_flags_info.bindingCount = set_info->binding_index + 1;
    vk_binding_flags_info.pBindingFlags = vk_binding_flags;

    vk_set_layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    vk_set_layout_info.pNext = &vk_binding_flags_info;
    vk_set_layout_info.bindingCount = set_info->binding_index + 1;
    vk_set_layout_info.pBindings = vk_binding_info;

    if (vk_descriptor_type == VK_DESCRIPTOR_TYPE_MUTABLE_EXT)
    {
        vk_binding_flags_info.pNext = &mutable_info;

        mutable_info.sType = VK_STRUCTURE_TYPE_MUTABLE_DESCRIPTOR_TYPE_CREATE_INFO_EXT;
        mutable_info.pNext = NULL;
        mutable_info.pMutableDescriptorTypeLists = mutable_descriptor_list;
        mutable_info.mutableDescriptorTypeListCount = set_info->binding_index + 1;

        memset(mutable_descriptor_list, 0, sizeof(mutable_descriptor_list));
        mutable_descriptor_list[set_info->binding_index].descriptorTypeCount =
                vkd3d_bindless_build_mutable_type_list(mutable_descriptor_types, device->bindless_state.flags, flags);
        mutable_descriptor_list[set_info->binding_index].pDescriptorTypes = mutable_descriptor_types;
    }

    if ((vr = VK_CALL(vkCreateDescriptorSetLayout(device->vk_device,
            &vk_set_layout_info, NULL, &set_info->vk_set_layout))) < 0)
        ERR("Failed to create descriptor set layout, vr %d.\n", vr);

    /* If we're able, we should implement descriptor copies with functions we roll ourselves. */
    if (d3d12_device_uses_descriptor_buffers(device))
    {
        INFO("Device supports VK_EXT_descriptor_buffer!\n");
        VK_CALL(vkGetDescriptorSetLayoutBindingOffsetEXT(device->vk_device, set_info->vk_set_layout,
                set_info->binding_index, &desc_offset));
        set_info->host_mapping_offset = desc_offset;
        set_info->host_mapping_descriptor_size = vkd3d_get_descriptor_size_for_binding(device,
                &vk_set_layout_info, set_info->binding_index);

        set_info->host_copy_template =
                vkd3d_bindless_find_copy_template(set_info->host_mapping_descriptor_size);
        set_info->host_copy_template_single =
                vkd3d_bindless_find_copy_template_single(set_info->host_mapping_descriptor_size);

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

    /* If we have descriptor buffers, we don't need host descriptor set layouts at all. We'll just malloc manually. */
    if (!d3d12_device_uses_descriptor_buffers(device))
    {
        vk_binding->descriptorCount = d3d12_max_host_descriptor_count_from_heap_type(device, set_info->heap_type);

        if (device->bindless_state.flags & VKD3D_BINDLESS_MUTABLE_TYPE)
        {
            /* If we have mutable descriptor extension, we will allocate these descriptors with
             * HOST_BIT and not UPDATE_AFTER_BIND, since that is enough to get threading guarantees. */
            vk_binding_flags[set_info->binding_index] = VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT;
            vk_set_layout_info.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_HOST_ONLY_POOL_BIT_EXT;
        }

        if ((vr = VK_CALL(vkCreateDescriptorSetLayout(device->vk_device,
                &vk_set_layout_info, NULL, &set_info->vk_host_set_layout))) < 0)
            ERR("Failed to create descriptor set layout, vr %d.\n", vr);
    }

    bindless_state->set_count++;

    return hresult_from_vk_result(vr);
}

uint32_t vkd3d_bindless_get_mutable_descriptor_type_size(struct d3d12_device *device)
{
    VkDescriptorType descriptor_types[VKD3D_MAX_MUTABLE_DESCRIPTOR_TYPES];
    uint32_t descriptor_type_count, i;
    uint32_t max_size, type_size;

    descriptor_type_count = vkd3d_bindless_build_mutable_type_list(descriptor_types,
            VKD3D_BINDLESS_RAW_SSBO,
            VKD3D_BINDLESS_SET_MUTABLE_RAW |
            VKD3D_BINDLESS_SET_MUTABLE_TYPED);

    max_size = 0;
    for (i = 0; i < descriptor_type_count; i++)
    {
        type_size = vkd3d_get_descriptor_size_for_type(device, descriptor_types[i]);
        max_size = max(max_size, type_size);
    }

    return max_size;
}

static uint32_t vkd3d_bindless_embedded_mutable_packed_metadata_offset(struct d3d12_device *device)
{
    const VkPhysicalDeviceDescriptorBufferPropertiesEXT *props = &device->device_info.descriptor_buffer_properties;
    uint32_t metadata_offset;

    /* Metadata is required for UAVs to implement ClearUAV. */
    metadata_offset = vkd3d_bindless_embedded_mutable_raw_buffer_offset(device);
    metadata_offset += max(props->robustStorageBufferDescriptorSize, props->robustUniformBufferDescriptorSize);
    metadata_offset = max(metadata_offset, props->storageImageDescriptorSize);
    metadata_offset = align(metadata_offset, 16);
    return metadata_offset;
}

static bool vkd3d_bindless_supports_embedded_packed_metadata(struct d3d12_device *device)
{
    return vkd3d_bindless_embedded_mutable_packed_metadata_offset(device) +
            sizeof(struct vkd3d_descriptor_metadata_view) <=
            vkd3d_bindless_get_mutable_descriptor_type_size(device);
}

bool vkd3d_bindless_supports_embedded_mutable_type(struct d3d12_device *device, uint32_t flags)
{
    const VkPhysicalDeviceDescriptorBufferPropertiesEXT *props = &device->device_info.descriptor_buffer_properties;
    uint32_t max_size;

#ifdef VKD3D_ENABLE_PROFILING
    /* For now, we don't do vtable variant shenanigans for profiled devices.
     * This can be fixed, but it's not that important at this time. */
    if (vkd3d_uses_profiling())
        return false;
#endif

    /* If we're using descriptor QA, we need more complex CPU VA decode to decode heap, offsets, types, etc,
     * so the fast path is not feasible. */
    if (vkd3d_descriptor_debug_active_descriptor_qa_checks())
        return false;

    /* We don't want to keep metadata around for shader visible heap.
     * If this can be supported on NV later, we can remove static table hoisting. */
    if (flags & VKD3D_HOIST_STATIC_TABLE_CBV)
        return false;

    /* For now, assume we're not using mutable_single_set. Fewer code paths to test.
     * That workaround is not needed for this style anyway. */
    if (flags & VKD3D_BINDLESS_MUTABLE_TYPE_RAW_SSBO)
        return false;

    /* Assume we're actually using SSBOs and not typed buffer for everything. */
    if (!(flags & VKD3D_BINDLESS_RAW_SSBO))
        return false;

    /* It is unsure at this time if DLSS requires us to be able to create shader image view handles
     * from shader visible heap. (See d3d12_device_vkd3d_ext_GetCudaSurfaceObject.)
     * That would require metadata to stick around, which we do not want.
     * If this can be figured out, we can ignore this check on NV. */
    if (device->vk_info.NVX_image_view_handle)
        return false;

    /* Checks if we can do some interesting shenanigans. */
    max_size = vkd3d_bindless_get_mutable_descriptor_type_size(device);

    /* The mutable size has to align to POT. */
    if (max_size & (max_size - 1))
        return false;

    /* Increment size must be large enough that we don't end up mis-decoding.
     * The minimum is 32, which should match any driver that exposes the true heap.
     * Image descriptors in 16 bytes is more or less impossible ... */
    if (max_size < VKD3D_RESOURCE_EMBEDDED_METADATA_OFFSET_LOG2_MASK)
        return false;

    /* Sampler descriptor size has to align. */
    if (device->device_info.descriptor_buffer_properties.samplerDescriptorSize &
            (device->device_info.descriptor_buffer_properties.samplerDescriptorSize - 1))
        return false;

    /* Sampler descriptor has to be at least 16 byte, so we can use fast path for copies. */
    if (device->device_info.descriptor_buffer_properties.samplerDescriptorSize < 16)
        return false;

    /* If descriptor buffers must be bound at large alignment, we cannot do magic packing tricks. */
    if (device->device_info.descriptor_buffer_properties.descriptorBufferOffsetAlignment >
            device->device_info.descriptor_buffer_properties.robustStorageBufferDescriptorSize)
        return false;

    /* The goal here is to embed all descriptor information into a single mutable element.
     * We can make use of the fact that sampled images take up far more space than buffer descriptors.
     * This should work if implementations expose descriptor heaps directly instead of going through
     * indirections.
     * We can bind the same descriptor buffer, at an offset with same stride to support multiple descriptor types.
     * - set = 1, binding = 0: Bind descriptor buffer as SSBO.
     *   Pass down an extra stride parameter to shader compiler.
     *   VAs should be loaded with stride = max_size.
     * - set = 1, binding = 1: Bind descriptor buffer with all mutable types. Place typed descriptors here.
     * - set = 2, binding = 0: Bind descriptor buffer with all mutable types, but use descriptor offset equal to
     *   align(max(props->robustStorageTexelBufferSize, props->robustUniformTexelBufferDescriptorSize),
     *         props->descriptorBufferOffsetAlignment). Place untyped descriptors here.
     * - Proposed layout that can fit in 32 bytes on e.g. AMD:
     *   - Images take up their full mutable size.
     *   - CBV: { NULL texel buffer, CBV (fixed offset), padding }
     *   - SRV buffer: SSBO / texel buffers: { Texel buffer, SSBO (fixed offset), padding }
     *   - UAV buffer w/o counter: SSBO / texel buffers: { Texel buffer, SSBO (fixed offset), padding }
     *   - UAV buffer w/ counter: { Texel buffer pointing to counter, SSBO (fixed offset), padding }
     *   - SRV RTAS: { RTAS ptr, padding }
     */

    /* If UAV counter is used, we pilfer the texel buffer instead of using raw VAs.
     * This aids robustness in case where mismatched descriptor types or non-null resource, but null counter
     * is used. This scenario is UB, and it behaves oddly on native drivers, so this is fine. */

    /* We need the descriptor size to be at least 32, otherwise we cannot implement CPU VA encoding scheme.
     * To deal with metadata on CPU-side descriptors, we will pilfer the lower 5 bits to encode an offset
     * to metadata structure.
     * This should be the case on all implementations that actually expose descriptors directly. */
    if (max_size < 32)
        return false;

    if (max_size < sizeof(struct vkd3d_descriptor_metadata_view))
        return false;

    /* Make sure we can implement SRV buffer with side by side texel buffer and SSBO/UBO. */
    if (vkd3d_bindless_embedded_mutable_raw_buffer_offset(device) +
            max(props->robustStorageBufferDescriptorSize, props->robustUniformBufferDescriptorSize) > max_size)
        return false;

    return true;
}

static bool vkd3d_bindless_supports_mutable_type(struct d3d12_device *device, uint32_t bindless_flags)
{
    VkDescriptorType descriptor_types[VKD3D_MAX_MUTABLE_DESCRIPTOR_TYPES];
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    VkDescriptorSetLayoutBindingFlagsCreateInfo binding_flags;
    VkMutableDescriptorTypeCreateInfoEXT mutable_info;
    VkDescriptorSetLayoutCreateInfo set_layout_info;
    VkMutableDescriptorTypeListVALVE mutable_list;
    VkDescriptorSetLayoutSupport supported;
    VkDescriptorBindingFlags binding_flag;
    VkDescriptorSetLayoutBinding binding;

    binding_flag = VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT;
    if (!d3d12_device_uses_descriptor_buffers(device))
    {
        binding_flag |= VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT |
                VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT |
                VK_DESCRIPTOR_BINDING_UPDATE_UNUSED_WHILE_PENDING_BIT;
    }

    if (!device->device_info.mutable_descriptor_features.mutableDescriptorType)
        return false;

    mutable_info.sType = VK_STRUCTURE_TYPE_MUTABLE_DESCRIPTOR_TYPE_CREATE_INFO_EXT;
    mutable_info.pNext = NULL;
    mutable_info.pMutableDescriptorTypeLists = &mutable_list;
    mutable_info.mutableDescriptorTypeListCount = 1;

    mutable_list.descriptorTypeCount = vkd3d_bindless_build_mutable_type_list(descriptor_types, bindless_flags,
            VKD3D_BINDLESS_SET_MUTABLE_RAW | VKD3D_BINDLESS_SET_MUTABLE_TYPED);
    mutable_list.pDescriptorTypes = descriptor_types;

    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_MUTABLE_EXT;
    binding.descriptorCount = d3d12_max_descriptor_count_from_heap_type(device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    binding.pImmutableSamplers = NULL;
    binding.stageFlags = VK_SHADER_STAGE_ALL;

    binding_flags.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
    binding_flags.pNext = &mutable_info;
    binding_flags.bindingCount = 1;
    binding_flags.pBindingFlags = &binding_flag;

    set_layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    set_layout_info.pNext = &binding_flags;

    if (d3d12_device_uses_descriptor_buffers(device))
        set_layout_info.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_DESCRIPTOR_BUFFER_BIT_EXT;
    else
        set_layout_info.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;

    set_layout_info.bindingCount = 1;
    set_layout_info.pBindings = &binding;

    supported.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_SUPPORT;
    supported.pNext = NULL;
    VK_CALL(vkGetDescriptorSetLayoutSupport(device->vk_device, &set_layout_info, &supported));
    if (!supported.supported)
        return false;

    if (!d3d12_device_uses_descriptor_buffers(device))
    {
        set_layout_info.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_HOST_ONLY_POOL_BIT_EXT;
        binding_flag = VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT;
        VK_CALL(vkGetDescriptorSetLayoutSupport(device->vk_device, &set_layout_info, &supported));
    }

    return supported.supported == VK_TRUE;
}

static uint32_t vkd3d_bindless_state_get_bindless_flags(struct d3d12_device *device)
{
    const struct vkd3d_physical_device_info *device_info = &device->device_info;
    uint32_t flags = 0;

    if (!d3d12_device_uses_descriptor_buffers(device))
    {
        if (device_info->vulkan_1_2_properties.maxPerStageDescriptorUpdateAfterBindUniformBuffers < VKD3D_MIN_VIEW_DESCRIPTOR_COUNT ||
                !device_info->vulkan_1_2_features.descriptorBindingUniformBufferUpdateAfterBind ||
                !device_info->vulkan_1_2_features.shaderUniformBufferArrayNonUniformIndexing)
            flags |= VKD3D_BINDLESS_CBV_AS_SSBO;
    }

    /* 16 is the cutoff due to requirements on ByteAddressBuffer.
     * We need tight 16 byte robustness on those and trying to emulate that with offset buffers
     * is too much of an ordeal. */
    if (device_info->properties2.properties.limits.minStorageBufferOffsetAlignment <= 16)
    {
        flags |= VKD3D_BINDLESS_RAW_SSBO;

        /* Descriptor buffers do not support SINGLE_SET layout.
         * We only enable descriptor buffers if we have verified that MUTABLE_SINGLE_SET hack is not required. */
        if (!d3d12_device_uses_descriptor_buffers(device))
        {
            /* Intel GPUs have smol descriptor heaps and only way we can fit a D3D12 heap is with
             * single set mutable. */
            if ((vkd3d_config_flags & VKD3D_CONFIG_FLAG_MUTABLE_SINGLE_SET) ||
                    device_info->properties2.properties.vendorID == VKD3D_VENDOR_ID_INTEL)
            {
                INFO("Enabling single descriptor set path for MUTABLE.\n");
                flags |= VKD3D_BINDLESS_MUTABLE_TYPE_RAW_SSBO;
            }
        }

        if (device_info->properties2.properties.limits.minStorageBufferOffsetAlignment > 4)
            flags |= VKD3D_SSBO_OFFSET_BUFFER;
    }

    /* Always use a typed offset buffer. Otherwise, we risk ending up with unbounded size on view maps.
     * Fortunately, we can place descriptors directly if we have descriptor buffers, so this is not required. */
    if (!d3d12_device_uses_descriptor_buffers(device))
        flags |= VKD3D_TYPED_OFFSET_BUFFER;

    /* We must use root SRV and UAV due to alignment requirements for 16-bit storage,
     * but root CBV is more lax. */
    flags |= VKD3D_RAW_VA_ROOT_DESCRIPTOR_SRV_UAV;
    /* CBV's really require push descriptors on NVIDIA and Qualcomm to get maximum performance.
     * The difference in performance is profound (~15% in some cases).
     * On ACO, BDA with NonWritable can be promoted directly to scalar loads,
     * which is great. */
    if ((vkd3d_config_flags & VKD3D_CONFIG_FLAG_FORCE_RAW_VA_CBV) ||
            (device_info->properties2.properties.vendorID != VKD3D_VENDOR_ID_NVIDIA &&
	     device_info->properties2.properties.vendorID != VKD3D_VENDOR_ID_QUALCOMM))
        flags |= VKD3D_RAW_VA_ROOT_DESCRIPTOR_CBV;

    if (device_info->properties2.properties.vendorID == VKD3D_VENDOR_ID_NVIDIA &&
            !(flags & VKD3D_RAW_VA_ROOT_DESCRIPTOR_CBV))
    {
        /* On NVIDIA, it's preferable to hoist CBVs to push descriptors if we can.
         * Hoisting is only safe with push descriptors since we need to consider
         * robustness as well for STATIC_KEEPING_BUFFER_BOUNDS_CHECKS. */
        flags |= VKD3D_HOIST_STATIC_TABLE_CBV;
    }

    if ((vkd3d_config_flags & VKD3D_CONFIG_FLAG_REQUIRES_COMPUTE_INDIRECT_TEMPLATES) &&
            !device->device_info.device_generated_commands_compute_features_nv.deviceGeneratedCompute &&
            !device->device_info.device_generated_commands_features_ext.deviceGeneratedCommands)
    {
        INFO("Forcing push UBO path for compute root parameters.\n");
        flags |= VKD3D_FORCE_COMPUTE_ROOT_PARAMETERS_PUSH_UBO;
    }

    if (device->device_info.device_generated_commands_features_ext.deviceGeneratedCommands)
    {
        INFO("Enabling fast paths for advanced ExecuteIndirect() graphics and compute (EXT_dgc).\n");
    }
    else
    {
        if (device->device_info.device_generated_commands_compute_features_nv.deviceGeneratedCompute)
            INFO("Enabling fast paths for advanced ExecuteIndirect() compute (NV_dgc).\n");
        if (device->device_info.device_generated_commands_features_nv.deviceGeneratedCommands)
            INFO("Enabling fast paths for advanced ExecuteIndirect() graphics (NV_dgc).\n");
    }

    if (vkd3d_bindless_supports_mutable_type(device, flags))
    {
        INFO("Device supports VK_%s_mutable_descriptor_type.\n",
                device->vk_info.EXT_mutable_descriptor_type ? "EXT" : "VALVE");
        flags |= VKD3D_BINDLESS_MUTABLE_TYPE;

        /* If we can, opt in to extreme speed mode. */
        if (d3d12_device_uses_descriptor_buffers(device) &&
                vkd3d_bindless_supports_embedded_mutable_type(device, flags))
        {
            flags |= VKD3D_BINDLESS_MUTABLE_EMBEDDED;
            INFO("Device supports ultra-fast path for descriptor copies.\n");

            if (vkd3d_bindless_supports_embedded_packed_metadata(device))
            {
                flags |= VKD3D_BINDLESS_MUTABLE_EMBEDDED_PACKED_METADATA;
                INFO("Device supports packed metadata path for descriptor copies.\n");
            }
        }
    }
    else
    {
        INFO("Device does not support VK_EXT_mutable_descriptor_type (or VALVE).\n");
        flags &= ~VKD3D_BINDLESS_MUTABLE_TYPE_RAW_SSBO;
    }

    /* Shorthand formulation to make future checks nicer. */
    if ((flags & VKD3D_BINDLESS_MUTABLE_TYPE) &&
            (flags & VKD3D_BINDLESS_RAW_SSBO) &&
            !(flags & VKD3D_BINDLESS_MUTABLE_TYPE_RAW_SSBO))
    {
        flags |= VKD3D_BINDLESS_MUTABLE_TYPE_SPLIT_RAW_TYPED;
    }

    return flags;
}

static void vkd3d_bindless_state_init_null_descriptor_payloads(struct vkd3d_bindless_state *bindless_state,
        struct d3d12_device *device)
{
    const VkPhysicalDeviceDescriptorBufferPropertiesEXT *props = &device->device_info.descriptor_buffer_properties;
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;

    const struct
    {
        VkDescriptorType vk_descriptor_type;
        uint32_t size;
    } types[] = {
        { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, props->sampledImageDescriptorSize },
        { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, props->storageImageDescriptorSize },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, props->robustUniformBufferDescriptorSize },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, props->robustStorageBufferDescriptorSize },
        { VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, props->robustUniformTexelBufferDescriptorSize },
        { VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, props->robustStorageTexelBufferDescriptorSize },
    };
    VkDescriptorGetInfoEXT get_info;
    uint8_t *payload;
    uint32_t i;

    bindless_state->descriptor_buffer_cbv_srv_uav_size = 0;

    get_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_GET_INFO_EXT;
    get_info.pNext = NULL;
    memset(&get_info.data, 0, sizeof(get_info.data));

    for (i = 0; i < ARRAY_SIZE(types); i++)
    {
        payload = vkd3d_bindless_state_get_null_descriptor_payload(bindless_state, types[i].vk_descriptor_type);

        /* When we write a NULL descriptor for a given type, we actually need to embed multiple NULL descriptors
         * of different types if we're using embedded mutable.
         * On many GPUs, a NULL descriptor is just zero memory, but not necessarily the case.
         * Write UBO -> also write a NULL texel buffer in the first bytes.
         * Write SRV/UAV buffer -> Write a NULL texel buffer template.
         * That template conveniently has both texel buffer + SSBO NULL descriptors.
         *   Note that there is no SSBO template since it's ambiguous whether to use SAMPLED or STORAGE_TEXEL_BUFFER.
         * Write storage image -> potentially place a NULL SSBO in the upper half. */

        if ((bindless_state->flags & VKD3D_BINDLESS_MUTABLE_EMBEDDED) &&
                types[i].vk_descriptor_type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
        {
            get_info.type = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
            VK_CALL(vkGetDescriptorEXT(device->vk_device, &get_info,
                    device->device_info.descriptor_buffer_properties.robustUniformTexelBufferDescriptorSize,
                    payload));
            payload += bindless_state->descriptor_buffer_packed_raw_buffer_offset;
        }

        get_info.type = types[i].vk_descriptor_type;

        VK_CALL(vkGetDescriptorEXT(device->vk_device, &get_info, types[i].size, payload));

        if (bindless_state->flags & VKD3D_BINDLESS_MUTABLE_EMBEDDED)
        {
            bool write_null_ssbo = types[i].vk_descriptor_type == VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER ||
                    types[i].vk_descriptor_type == VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;

            /* If we pack SSBOs + metadata above the storage image (embedded packed metadata),
             * add NULL SSBO descriptor as well. */
            if (types[i].vk_descriptor_type == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE &&
                    bindless_state->descriptor_buffer_packed_raw_buffer_offset >=
                    device->device_info.descriptor_buffer_properties.storageImageDescriptorSize)
            {
                write_null_ssbo = true;
            }

            if (write_null_ssbo)
            {
                /* Buffer types are always emitted side by side.
                 * Emit NULL typed buffer in first half, and NULL SSBO after.
                 * When creating a NULL buffer descriptor we'll always use the typed template,
                 * since SSBO is ambiguous (we don't know UAV vs SRV necessarily). */
                payload += bindless_state->descriptor_buffer_packed_raw_buffer_offset;
                get_info.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                VK_CALL(vkGetDescriptorEXT(device->vk_device, &get_info,
                        device->device_info.descriptor_buffer_properties.robustStorageBufferDescriptorSize,
                        payload));
            }
        }

        bindless_state->descriptor_buffer_cbv_srv_uav_size =
                max(bindless_state->descriptor_buffer_cbv_srv_uav_size, types[i].size);
    }
}

HRESULT vkd3d_bindless_state_init(struct vkd3d_bindless_state *bindless_state,
        struct d3d12_device *device)
{
    const struct vkd3d_physical_device_info *device_info = &device->device_info;
    uint32_t extra_bindings = 0;
    HRESULT hr = E_FAIL;

    memset(bindless_state, 0, sizeof(*bindless_state));
    bindless_state->flags = vkd3d_bindless_state_get_bindless_flags(device);

    if (!device_info->vulkan_1_2_features.descriptorIndexing ||
            /* Some extra features not covered by descriptorIndexing meta-feature. */
            !device_info->vulkan_1_2_features.shaderStorageTexelBufferArrayNonUniformIndexing ||
            !device_info->vulkan_1_2_features.shaderStorageImageArrayNonUniformIndexing ||
            !device_info->vulkan_1_2_features.descriptorBindingVariableDescriptorCount)
    {
        ERR("Insufficient descriptor indexing support.\n");
        goto fail;
    }

    if (!d3d12_device_uses_descriptor_buffers(device))
    {
        /* UBO is optional. We can fall back to SSBO if required. */
        if (device_info->vulkan_1_2_properties.maxPerStageDescriptorUpdateAfterBindSampledImages < VKD3D_MIN_VIEW_DESCRIPTOR_COUNT ||
                device_info->vulkan_1_2_properties.maxPerStageDescriptorUpdateAfterBindStorageImages < VKD3D_MIN_VIEW_DESCRIPTOR_COUNT ||
                device_info->vulkan_1_2_properties.maxPerStageDescriptorUpdateAfterBindStorageBuffers < VKD3D_MIN_VIEW_DESCRIPTOR_COUNT)
        {
            ERR("Insufficient descriptor indexing support.\n");
            goto fail;
        }
    }

    extra_bindings |= VKD3D_BINDLESS_SET_EXTRA_RAW_VA_AUX_BUFFER;
    if (bindless_state->flags & (VKD3D_SSBO_OFFSET_BUFFER | VKD3D_TYPED_OFFSET_BUFFER))
        extra_bindings |= VKD3D_BINDLESS_SET_EXTRA_OFFSET_BUFFER;

    if (vkd3d_descriptor_debug_active_descriptor_qa_checks())
    {
        extra_bindings |= VKD3D_BINDLESS_SET_EXTRA_FEEDBACK_PAYLOAD_INFO_BUFFER |
                VKD3D_BINDLESS_SET_EXTRA_FEEDBACK_CONTROL_INFO_BUFFER;
    }

    if (FAILED(hr = vkd3d_bindless_state_add_binding(bindless_state, device,
            VKD3D_BINDLESS_SET_SAMPLER, VK_DESCRIPTOR_TYPE_SAMPLER, VK_DESCRIPTOR_TYPE_SAMPLER)))
        goto fail;

    if (bindless_state->flags & VKD3D_BINDLESS_MUTABLE_TYPE)
    {
        bool uses_raw_typed_split = !!(bindless_state->flags & VKD3D_BINDLESS_MUTABLE_TYPE_SPLIT_RAW_TYPED);
        uint32_t flags;

        flags = VKD3D_BINDLESS_SET_UAV | VKD3D_BINDLESS_SET_SRV |
                VKD3D_BINDLESS_SET_BUFFER | VKD3D_BINDLESS_SET_IMAGE |
                VKD3D_BINDLESS_SET_MUTABLE_TYPED | VKD3D_BINDLESS_SET_MUTABLE |
                extra_bindings;

        if (!uses_raw_typed_split)
        {
            flags |= VKD3D_BINDLESS_SET_CBV | VKD3D_BINDLESS_SET_MUTABLE_RAW;
            if (bindless_state->flags & VKD3D_BINDLESS_RAW_SSBO)
                flags |= VKD3D_BINDLESS_SET_RAW_SSBO;
        }

        /* Ensure that the descriptor size matches the other set, since we'll be overlaying them
         * on the same memory. */
        if (bindless_state->flags & VKD3D_BINDLESS_MUTABLE_EMBEDDED)
            flags |= VKD3D_BINDLESS_SET_MUTABLE_RAW;

        /* If we can, prefer to use one universal descriptor type which works for any descriptor.
         * The exception is SSBOs since we need to workaround buggy applications which create typed buffers,
         * but assume they can be read as untyped buffers. Move CBVs to the SSBO set as well if we go that route,
         * since it works around similar app bugs.
         * If we opt-in to it, we can move everything into the mutable set. */
        if (FAILED(hr = vkd3d_bindless_state_add_binding(bindless_state, device, flags,
                VK_DESCRIPTOR_TYPE_MUTABLE_EXT, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE)))
            goto fail;

        /* We never use CBV in second set unless SSBO does as well. */
        if (uses_raw_typed_split)
        {
            bool use_mutable = (bindless_state->flags & VKD3D_BINDLESS_MUTABLE_EMBEDDED) ||
                    !(bindless_state->flags & VKD3D_BINDLESS_CBV_AS_SSBO);

            flags = VKD3D_BINDLESS_SET_UAV |
                    VKD3D_BINDLESS_SET_SRV |
                    VKD3D_BINDLESS_SET_RAW_SSBO |
                    VKD3D_BINDLESS_SET_CBV;

            if (use_mutable)
                flags |= VKD3D_BINDLESS_SET_MUTABLE | VKD3D_BINDLESS_SET_MUTABLE_RAW;

            /* Ensure that the descriptor size matches the other set, since we'll be overlaying them
             * on the same memory. */
            if (bindless_state->flags & VKD3D_BINDLESS_MUTABLE_EMBEDDED)
                flags |= VKD3D_BINDLESS_SET_MUTABLE_TYPED;

            if (FAILED(hr = vkd3d_bindless_state_add_binding(bindless_state, device,
                    flags, use_mutable ? VK_DESCRIPTOR_TYPE_MUTABLE_EXT : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                    VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)))
                goto fail;
        }
    }
    else
    {
        if (FAILED(hr = vkd3d_bindless_state_add_binding(bindless_state, device,
                VKD3D_BINDLESS_SET_CBV | extra_bindings,
                vkd3d_bindless_state_get_cbv_descriptor_type(bindless_state),
                vkd3d_bindless_state_get_cbv_descriptor_type(bindless_state))))
            goto fail;

        if (FAILED(hr = vkd3d_bindless_state_add_binding(bindless_state, device,
                VKD3D_BINDLESS_SET_SRV | VKD3D_BINDLESS_SET_BUFFER,
                VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER)) ||
            FAILED(hr = vkd3d_bindless_state_add_binding(bindless_state, device,
                VKD3D_BINDLESS_SET_SRV | VKD3D_BINDLESS_SET_IMAGE,
                VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE)))
            goto fail;

        if (FAILED(hr = vkd3d_bindless_state_add_binding(bindless_state, device,
                VKD3D_BINDLESS_SET_UAV | VKD3D_BINDLESS_SET_BUFFER,
                VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER)) ||
            FAILED(hr = vkd3d_bindless_state_add_binding(bindless_state, device,
                VKD3D_BINDLESS_SET_UAV | VKD3D_BINDLESS_SET_IMAGE,
                VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)))
            goto fail;

        if (bindless_state->flags & VKD3D_BINDLESS_RAW_SSBO)
        {
            if (FAILED(hr = vkd3d_bindless_state_add_binding(bindless_state, device,
                    VKD3D_BINDLESS_SET_UAV | VKD3D_BINDLESS_SET_SRV | VKD3D_BINDLESS_SET_RAW_SSBO,
                    VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)))
                goto fail;
        }
    }

    if (d3d12_device_uses_descriptor_buffers(device))
    {
        vkd3d_bindless_state_init_null_descriptor_payloads(bindless_state, device);
        /* cbv_srv_uav_size is computed while setting up null payload. */
        bindless_state->descriptor_buffer_sampler_size =
                device->device_info.descriptor_buffer_properties.samplerDescriptorSize;
        bindless_state->descriptor_buffer_cbv_srv_uav_size_log2 =
                vkd3d_log2i(bindless_state->descriptor_buffer_cbv_srv_uav_size);
        bindless_state->descriptor_buffer_sampler_size_log2 =
                vkd3d_log2i(bindless_state->descriptor_buffer_sampler_size);
        bindless_state->descriptor_buffer_packed_raw_buffer_offset =
                vkd3d_bindless_embedded_mutable_raw_buffer_offset(device);
        bindless_state->descriptor_buffer_packed_metadata_offset =
                vkd3d_bindless_embedded_mutable_packed_metadata_offset(device);
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

    ERR("No set found for flags %#x.\n", flags);
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

    ERR("No set found for flags %#x.\n", flags);
    return 0;
}
