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

#include "vkd3d_private.h"

#include <stdio.h>

/* ID3D12RootSignature */
static inline struct d3d12_root_signature *impl_from_ID3D12RootSignature(ID3D12RootSignature *iface)
{
    return CONTAINING_RECORD(iface, struct d3d12_root_signature, ID3D12RootSignature_iface);
}

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

    return refcount;
}

static void d3d12_root_signature_cleanup(struct d3d12_root_signature *root_signature,
        struct d3d12_device *device)
{
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    unsigned int i;

    if (root_signature->vk_pipeline_layout)
        VK_CALL(vkDestroyPipelineLayout(device->vk_device, root_signature->vk_pipeline_layout, NULL));
    if (root_signature->pool_sizes)
        vkd3d_free(root_signature->pool_sizes);
    if (root_signature->vk_set_layout)
        VK_CALL(vkDestroyDescriptorSetLayout(device->vk_device, root_signature->vk_set_layout, NULL));
    if (root_signature->vk_push_set_layout)
        VK_CALL(vkDestroyDescriptorSetLayout(device->vk_device, root_signature->vk_push_set_layout, NULL));

    if (root_signature->parameters)
    {
        for (i = 0; i < root_signature->parameter_count; ++i)
        {
            if (root_signature->parameters[i].parameter_type == D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE)
                vkd3d_free(root_signature->parameters[i].u.descriptor_table.ranges);
        }
        vkd3d_free(root_signature->parameters);
    }

    if (root_signature->descriptor_mapping)
        vkd3d_free(root_signature->descriptor_mapping);
    if (root_signature->root_constants)
        vkd3d_free(root_signature->root_constants);

    for (i = 0; i < root_signature->static_sampler_count; ++i)
    {
        if (root_signature->static_samplers[i])
            VK_CALL(vkDestroySampler(device->vk_device, root_signature->static_samplers[i], NULL));
    }
    if (root_signature->static_samplers)
        vkd3d_free(root_signature->static_samplers);
}

static ULONG STDMETHODCALLTYPE d3d12_root_signature_Release(ID3D12RootSignature *iface)
{
    struct d3d12_root_signature *root_signature = impl_from_ID3D12RootSignature(iface);
    ULONG refcount = InterlockedDecrement(&root_signature->refcount);

    TRACE("%p decreasing refcount to %u.\n", root_signature, refcount);

    if (!refcount)
    {
        struct d3d12_device *device = root_signature->device;
        d3d12_root_signature_cleanup(root_signature, device);
        vkd3d_free(root_signature);
        ID3D12Device_Release(&device->ID3D12Device_iface);
    }

    return refcount;
}

static HRESULT STDMETHODCALLTYPE d3d12_root_signature_GetPrivateData(ID3D12RootSignature *iface,
        REFGUID guid, UINT *data_size, void *data)
{
    FIXME("iface %p, guid %s, data_size %p, data %p stub!", iface, debugstr_guid(guid), data_size, data);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d12_root_signature_SetPrivateData(ID3D12RootSignature *iface,
        REFGUID guid, UINT data_size, const void *data)
{
    FIXME("iface %p, guid %s, data_size %u, data %p stub!\n", iface, debugstr_guid(guid), data_size, data);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d12_root_signature_SetPrivateDataInterface(ID3D12RootSignature *iface,
        REFGUID guid, const IUnknown *data)
{
    FIXME("iface %p, guid %s, data %p stub!\n", iface, debugstr_guid(guid), data);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d12_root_signature_SetName(ID3D12RootSignature *iface, const WCHAR *name)
{
    struct d3d12_root_signature *root_signature = impl_from_ID3D12RootSignature(iface);

    FIXME("iface %p, name %s stub!\n", iface, debugstr_w(name, root_signature->device->wchar_size));

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d12_root_signature_GetDevice(ID3D12RootSignature *iface,
        REFIID riid, void **device)
{
    struct d3d12_root_signature *root_signature = impl_from_ID3D12RootSignature(iface);

    TRACE("iface %p, riid %s, device %p.\n", iface, debugstr_guid(riid), device);

    return ID3D12Device_QueryInterface(&root_signature->device->ID3D12Device_iface, riid, device);
}

static const struct ID3D12RootSignatureVtbl d3d12_root_signature_vtbl =
{
    /* IUnknown methods */
    d3d12_root_signature_QueryInterface,
    d3d12_root_signature_AddRef,
    d3d12_root_signature_Release,
    /* ID3D12Object methods */
    d3d12_root_signature_GetPrivateData,
    d3d12_root_signature_SetPrivateData,
    d3d12_root_signature_SetPrivateDataInterface,
    d3d12_root_signature_SetName,
    /* ID3D12DeviceChild methods */
    d3d12_root_signature_GetDevice,
};

struct d3d12_root_signature *unsafe_impl_from_ID3D12RootSignature(ID3D12RootSignature *iface)
{
    if (!iface)
        return NULL;
    assert(iface->lpVtbl == &d3d12_root_signature_vtbl);
    return impl_from_ID3D12RootSignature(iface);
}

static VkShaderStageFlags stage_flags_from_visibility(D3D12_SHADER_VISIBILITY visibility)
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
        default:
            return 0;
    }
}

static enum vkd3d_shader_visibility vkd3d_shader_visibility_from_d3d12(D3D12_SHADER_VISIBILITY visibility)
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

static VkDescriptorType vk_descriptor_type_from_d3d12_range_type(D3D12_DESCRIPTOR_RANGE_TYPE type,
        bool is_buffer)
{
    switch (type)
    {
        case D3D12_DESCRIPTOR_RANGE_TYPE_SRV:
            return is_buffer ? VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER : VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        case D3D12_DESCRIPTOR_RANGE_TYPE_UAV:
            return is_buffer ? VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER : VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        case D3D12_DESCRIPTOR_RANGE_TYPE_CBV:
            return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        case D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER:
            return VK_DESCRIPTOR_TYPE_SAMPLER;
        default:
            FIXME("Unhandled descriptor range type type %#x.\n", type);
            return VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    }
}

static VkDescriptorType vk_descriptor_type_from_d3d12_root_parameter(D3D12_ROOT_PARAMETER_TYPE type)
{
    switch (type)
    {
        /* SRV and UAV root parameters are buffer views. */
        case D3D12_ROOT_PARAMETER_TYPE_SRV:
            return VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
        case D3D12_ROOT_PARAMETER_TYPE_UAV:
            return VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER;
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

static bool vk_binding_from_d3d12_descriptor_range(struct VkDescriptorSetLayoutBinding *binding_desc,
        const D3D12_DESCRIPTOR_RANGE *descriptor_range, D3D12_SHADER_VISIBILITY shader_visibility,
        bool is_buffer, uint32_t vk_binding)
{
    binding_desc->binding = vk_binding;
    binding_desc->descriptorType
            = vk_descriptor_type_from_d3d12_range_type(descriptor_range->RangeType, is_buffer);
    binding_desc->descriptorCount = 1;

    if (descriptor_range->RegisterSpace)
    {
        FIXME("Unhandled register space %u.\n", descriptor_range->RegisterSpace);
        return false;
    }

    binding_desc->stageFlags = stage_flags_from_visibility(shader_visibility);
    binding_desc->pImmutableSamplers = NULL;

    return true;
}

struct d3d12_root_signature_info
{
    size_t cbv_count;
    size_t buffer_uav_count;
    size_t uav_count;
    size_t buffer_srv_count;
    size_t srv_count;
    size_t sampler_count;

    size_t descriptor_count;

    size_t root_constant_count;
    size_t root_descriptor_count;

    size_t cost;
};

static HRESULT d3d12_root_signature_info_count_descriptors(struct d3d12_root_signature_info *info,
        const D3D12_DESCRIPTOR_RANGE *range)
{
    if (range->NumDescriptors == 0xffffffff)
    {
        FIXME("Unhandled unbound descriptor range.\n");
        return E_NOTIMPL;
    }

    switch (range->RangeType)
    {
        case D3D12_DESCRIPTOR_RANGE_TYPE_SRV:
            info->srv_count += range->NumDescriptors;
            break;
        case D3D12_DESCRIPTOR_RANGE_TYPE_UAV:
            info->uav_count += range->NumDescriptors;
            break;
        case D3D12_DESCRIPTOR_RANGE_TYPE_CBV:
            info->cbv_count += range->NumDescriptors;
            break;
        case D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER:
            info->sampler_count += range->NumDescriptors;
            break;
        default:
            FIXME("Unhandled descriptor type %#x.\n", range->RangeType);
            return E_NOTIMPL;
    }

    info->descriptor_count += range->NumDescriptors;

    return S_OK;
}

static HRESULT d3d12_root_signature_info_from_desc(struct d3d12_root_signature_info *info,
        const D3D12_ROOT_SIGNATURE_DESC *desc)
{
    unsigned int i, j;
    HRESULT hr;

    memset(info, 0, sizeof(*info));

    for (i = 0; i < desc->NumParameters; ++i)
    {
        const D3D12_ROOT_PARAMETER *p = &desc->pParameters[i];

        switch (p->ParameterType)
        {
            case D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE:
                for (j = 0; j < p->u.DescriptorTable.NumDescriptorRanges; ++j)
                    if (FAILED(hr = d3d12_root_signature_info_count_descriptors(info,
                            &p->u.DescriptorTable.pDescriptorRanges[j])))
                        return hr;
                ++info->cost;
                break;

            case D3D12_ROOT_PARAMETER_TYPE_CBV:
                ++info->root_descriptor_count;
                ++info->cbv_count;
                ++info->descriptor_count;
                info->cost += 2;
                break;
            case D3D12_ROOT_PARAMETER_TYPE_SRV:
                ++info->root_descriptor_count;
                ++info->buffer_srv_count;
                ++info->descriptor_count;
                info->cost += 2;
                break;
            case D3D12_ROOT_PARAMETER_TYPE_UAV:
                ++info->root_descriptor_count;
                ++info->buffer_uav_count;
                ++info->descriptor_count;
                info->cost += 2;
                break;

            case D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS:
                ++info->root_constant_count;
                info->cost += p->u.Constants.Num32BitValues;
                break;

            default:
                FIXME("Unhandled type %#x for parameter %u.\n", p->ParameterType, i);
                return E_NOTIMPL;
        }
    }

    info->sampler_count += desc->NumStaticSamplers;
    info->descriptor_count += desc->NumStaticSamplers;

    return S_OK;
}

static HRESULT d3d12_root_signature_init_descriptor_pool_size(struct d3d12_root_signature *root_signature,
        const struct d3d12_root_signature_info *info)
{
    unsigned int i;

    root_signature->pool_size_count = 0;
    if (info->cbv_count)
        ++root_signature->pool_size_count;
    if (info->buffer_srv_count || info->srv_count)
        ++root_signature->pool_size_count;
    if (info->srv_count)
        ++root_signature->pool_size_count;
    if (info->buffer_uav_count || info->uav_count)
        ++root_signature->pool_size_count;
    if (info->uav_count)
        ++root_signature->pool_size_count;
    if (info->sampler_count)
        ++root_signature->pool_size_count;

    if (root_signature->pool_size_count)
    {
        if (!(root_signature->pool_sizes = vkd3d_calloc(root_signature->pool_size_count,
                sizeof(*root_signature->pool_sizes))))
        {
            return E_OUTOFMEMORY;
        }

        i = 0;
        if (info->cbv_count)
        {
            root_signature->pool_sizes[i].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            root_signature->pool_sizes[i++].descriptorCount = info->cbv_count;
        }
        /* Each D3D12_DESCRIPTOR_RANGE_TYPE_SRV descriptor can be either a
         * buffer or a texture view. Allocate one buffer view and one image
         * view Vulkan descriptor for each. */
        if (info->buffer_srv_count || info->srv_count)
        {
            root_signature->pool_sizes[i].type = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
            root_signature->pool_sizes[i++].descriptorCount = info->buffer_srv_count + info->srv_count;
        }
        if (info->srv_count)
        {
            root_signature->pool_sizes[i].type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
            root_signature->pool_sizes[i++].descriptorCount = info->srv_count;
        }
        /* Each D3D12_DESCRIPTOR_RANGE_TYPE_UAV descriptor can be either a
         * buffer or a texture view. Allocate one buffer view and one image
         * view Vulkan descriptor for each. */
        if (info->buffer_uav_count || info->uav_count)
        {
            root_signature->pool_sizes[i].type = VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER;
            root_signature->pool_sizes[i++].descriptorCount = info->buffer_uav_count + info->uav_count;
        }
        if (info->uav_count)
        {
            root_signature->pool_sizes[i].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            root_signature->pool_sizes[i++].descriptorCount = info->uav_count;
        }
        if (info->sampler_count)
        {
            root_signature->pool_sizes[i].type = VK_DESCRIPTOR_TYPE_SAMPLER;
            root_signature->pool_sizes[i++].descriptorCount = info->sampler_count;
        }
    }
    else
    {
        root_signature->pool_sizes = NULL;
        root_signature->pool_size_count = 0;
    }

    return S_OK;
}

static HRESULT d3d12_root_signature_init_push_constants(struct d3d12_root_signature *root_signature,
        const D3D12_ROOT_SIGNATURE_DESC *desc, const struct d3d12_root_signature_info *info,
        struct VkPushConstantRange push_constants[D3D12_SHADER_VISIBILITY_PIXEL + 1],
        uint32_t *push_constant_range_count)
{
    uint32_t push_constants_offset[D3D12_SHADER_VISIBILITY_PIXEL + 1];
    unsigned int i, j, push_constant_count;
    uint32_t offset;

    memset(push_constants, 0, (D3D12_SHADER_VISIBILITY_PIXEL + 1) * sizeof(*push_constants));
    memset(push_constants_offset, 0, sizeof(push_constants_offset));
    for (i = 0; i < desc->NumParameters; ++i)
    {
        const D3D12_ROOT_PARAMETER *p = &desc->pParameters[i];
        if (p->ParameterType != D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS)
            continue;

        assert(p->ShaderVisibility <= D3D12_SHADER_VISIBILITY_PIXEL);
        push_constants[p->ShaderVisibility].stageFlags = stage_flags_from_visibility(p->ShaderVisibility);
        push_constants[p->ShaderVisibility].size += p->u.Constants.Num32BitValues * sizeof(uint32_t);
    }
    if (push_constants[D3D12_SHADER_VISIBILITY_ALL].size)
    {
        /* When D3D12_SHADER_VISIBILITY_ALL is used we use a single push
         * constants range because the Vulkan spec states:
         *
         *   "Any two elements of pPushConstantRanges must not include the same
         *   stage in stageFlags".
         */
        push_constant_count = 1;
        for (i = 0; i <= D3D12_SHADER_VISIBILITY_PIXEL; ++i)
        {
            if (i == D3D12_SHADER_VISIBILITY_ALL)
                continue;

            push_constants[D3D12_SHADER_VISIBILITY_ALL].size += push_constants[i].size;
            push_constants[i].size = 0;
        }
    }
    else
    {
        /* Move non-empty push constants ranges to front and compute offsets. */
        offset = 0;
        for (i = 0, j = 0; i <= D3D12_SHADER_VISIBILITY_PIXEL; ++i)
        {
            if (push_constants[i].size)
            {
                push_constants[j] = push_constants[i];
                push_constants[j].offset = offset;
                push_constants_offset[i] = offset;
                offset += push_constants[j].size;
                ++j;
            }
        }
        push_constant_count = j;
    }

    for (i = 0, j = 0; i < desc->NumParameters; ++i)
    {
        struct d3d12_root_constant *root_constant = &root_signature->parameters[i].u.constant;
        const D3D12_ROOT_PARAMETER *p = &desc->pParameters[i];
        unsigned int idx;

        if (p->ParameterType != D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS)
            continue;

        if (p->u.Constants.RegisterSpace)
        {
            FIXME("Unhandled register space %u for parameter %u.\n", p->u.Constants.RegisterSpace, i);
            return E_NOTIMPL;
        }

        idx = push_constant_count == 1 ? 0 : p->ShaderVisibility;
        offset = push_constants_offset[idx];
        push_constants_offset[idx] += p->u.Constants.Num32BitValues * sizeof(uint32_t);

        root_signature->parameters[i].parameter_type = p->ParameterType;
        root_constant->stage_flags = push_constant_count == 1
                ? push_constants[0].stageFlags : stage_flags_from_visibility(p->ShaderVisibility);
        root_constant->offset = offset;

        root_signature->root_constants[j].register_index = p->u.Constants.ShaderRegister;
        root_signature->root_constants[j].shader_visibility
                = vkd3d_shader_visibility_from_d3d12(p->ShaderVisibility);
        root_signature->root_constants[j].offset = offset;
        root_signature->root_constants[j].size = p->u.Constants.Num32BitValues * sizeof(uint32_t);

        ++j;
    }

    *push_constant_range_count = push_constant_count;

    return S_OK;
}

struct vkd3d_descriptor_set_context
{
    VkDescriptorSetLayoutBinding *current_binding;
    unsigned int descriptor_index;
    uint32_t set_index;
    uint32_t descriptor_binding;
};

static void d3d12_root_signature_append_vk_binding(struct d3d12_root_signature *root_signature,
        enum vkd3d_shader_descriptor_type descriptor_type, unsigned int register_idx,
        bool buffer_descriptor, enum vkd3d_shader_visibility shader_visibility,
        struct vkd3d_descriptor_set_context *context)
{
    unsigned int i = context->descriptor_index++;

    root_signature->descriptor_mapping[i].type = descriptor_type;
    root_signature->descriptor_mapping[i].register_index = register_idx;
    root_signature->descriptor_mapping[i].shader_visibility = shader_visibility;
    root_signature->descriptor_mapping[i].is_buffer = buffer_descriptor;
    root_signature->descriptor_mapping[i].binding.set = context->set_index;
    root_signature->descriptor_mapping[i].binding.binding = context->descriptor_binding++;
}

static uint32_t d3d12_root_signature_assign_vk_bindings(struct d3d12_root_signature *root_signature,
        enum vkd3d_shader_descriptor_type descriptor_type, unsigned int base_register_idx,
        unsigned int binding_count, bool is_buffer_descriptor, bool duplicate_descriptors,
        enum vkd3d_shader_visibility shader_visibility, struct vkd3d_descriptor_set_context *context)
{
    uint32_t first_binding;
    unsigned int i;

    is_buffer_descriptor |= descriptor_type == VKD3D_SHADER_DESCRIPTOR_TYPE_CBV;
    duplicate_descriptors = (descriptor_type == VKD3D_SHADER_DESCRIPTOR_TYPE_SRV
            || descriptor_type == VKD3D_SHADER_DESCRIPTOR_TYPE_UAV)
            && duplicate_descriptors;

    first_binding = context->descriptor_binding;
    for (i = 0; i < binding_count; ++i)
    {
        if (duplicate_descriptors)
            d3d12_root_signature_append_vk_binding(root_signature, descriptor_type,
                    base_register_idx + i, true, shader_visibility, context);

        d3d12_root_signature_append_vk_binding(root_signature, descriptor_type,
                base_register_idx + i, is_buffer_descriptor, shader_visibility, context);
    }
    return first_binding;
}

static uint32_t vkd3d_descriptor_magic_from_d3d12(D3D12_DESCRIPTOR_RANGE_TYPE type)
{
    switch (type)
    {
        case D3D12_DESCRIPTOR_RANGE_TYPE_SRV:
            return VKD3D_DESCRIPTOR_MAGIC_SRV;
        case D3D12_DESCRIPTOR_RANGE_TYPE_UAV:
            return VKD3D_DESCRIPTOR_MAGIC_UAV;
        case D3D12_DESCRIPTOR_RANGE_TYPE_CBV:
            return VKD3D_DESCRIPTOR_MAGIC_CBV;
        case D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER:
            return VKD3D_DESCRIPTOR_MAGIC_SAMPLER;
        default:
            ERR("Invalid range type %#x.\n", type);
            return VKD3D_DESCRIPTOR_MAGIC_FREE;
    }
}

static HRESULT d3d12_root_signature_init_root_descriptor_tables(struct d3d12_root_signature *root_signature,
        const D3D12_ROOT_SIGNATURE_DESC *desc, struct vkd3d_descriptor_set_context *context)
{
    VkDescriptorSetLayoutBinding *cur_binding = context->current_binding;
    struct d3d12_root_descriptor_table *table;
    const D3D12_DESCRIPTOR_RANGE *range;
    unsigned int i, j, k, range_count;
    uint32_t vk_binding;

    for (i = 0; i < desc->NumParameters; ++i)
    {
        const D3D12_ROOT_PARAMETER *p = &desc->pParameters[i];
        if (p->ParameterType != D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE)
            continue;

        table = &root_signature->parameters[i].u.descriptor_table;
        range_count = p->u.DescriptorTable.NumDescriptorRanges;

        root_signature->parameters[i].parameter_type = p->ParameterType;
        table->range_count = range_count;
        if (!(table->ranges = vkd3d_calloc(table->range_count, sizeof(*table->ranges))))
            return E_OUTOFMEMORY;

        for (j = 0; j < range_count; ++j)
        {
            range = &p->u.DescriptorTable.pDescriptorRanges[j];

            vk_binding = d3d12_root_signature_assign_vk_bindings(root_signature,
                    vkd3d_descriptor_type_from_d3d12_range_type(range->RangeType),
                    range->BaseShaderRegister, range->NumDescriptors, false, true,
                    vkd3d_shader_visibility_from_d3d12(p->ShaderVisibility), context);

            /* Unroll descriptor range. */
            for (k = 0; k < range->NumDescriptors; ++k)
            {
                uint32_t vk_current_binding = vk_binding + k;

                if (range->RangeType == D3D12_DESCRIPTOR_RANGE_TYPE_SRV
                        || range->RangeType == D3D12_DESCRIPTOR_RANGE_TYPE_UAV)
                {
                    vk_current_binding = vk_binding + 2 * k;

                    /* Assign binding for image view. */
                    if (!vk_binding_from_d3d12_descriptor_range(cur_binding,
                            range, p->ShaderVisibility, false, vk_current_binding + 1))
                        return E_NOTIMPL;

                    ++cur_binding;
                }

                if (!vk_binding_from_d3d12_descriptor_range(cur_binding,
                        range, p->ShaderVisibility, true, vk_current_binding))
                    return E_NOTIMPL;

                ++cur_binding;
            }

            table->ranges[j].offset = range->OffsetInDescriptorsFromTableStart;
            table->ranges[j].descriptor_count = range->NumDescriptors;
            table->ranges[j].binding = vk_binding;
            table->ranges[j].descriptor_magic = vkd3d_descriptor_magic_from_d3d12(range->RangeType);
            table->ranges[j].base_register_idx = range->BaseShaderRegister;
        }
    }

    context->current_binding = cur_binding;
    return S_OK;
}

static HRESULT d3d12_root_signature_init_root_descriptors(struct d3d12_root_signature *root_signature,
        const D3D12_ROOT_SIGNATURE_DESC *desc, struct vkd3d_descriptor_set_context *context)
{
    VkDescriptorSetLayoutBinding *cur_binding = context->current_binding;
    unsigned int i;

    for (i = 0; i < desc->NumParameters; ++i)
    {
        const D3D12_ROOT_PARAMETER *p = &desc->pParameters[i];
        if (p->ParameterType != D3D12_ROOT_PARAMETER_TYPE_CBV
                && p->ParameterType != D3D12_ROOT_PARAMETER_TYPE_SRV
                && p->ParameterType != D3D12_ROOT_PARAMETER_TYPE_UAV)
            continue;

        if (p->u.Descriptor.RegisterSpace)
        {
            FIXME("Unhandled register space %u for parameter %u.\n", p->u.Descriptor.RegisterSpace, i);
            return E_NOTIMPL;
        }

        cur_binding->binding = d3d12_root_signature_assign_vk_bindings(root_signature,
                vkd3d_descriptor_type_from_d3d12_root_parameter_type(p->ParameterType),
                p->u.Descriptor.ShaderRegister, 1, true, false,
                vkd3d_shader_visibility_from_d3d12(p->ShaderVisibility), context);
        cur_binding->descriptorType = vk_descriptor_type_from_d3d12_root_parameter(p->ParameterType);
        cur_binding->descriptorCount = 1;
        cur_binding->stageFlags = stage_flags_from_visibility(p->ShaderVisibility);
        cur_binding->pImmutableSamplers = NULL;

        root_signature->parameters[i].parameter_type = p->ParameterType;
        root_signature->parameters[i].u.descriptor.binding = cur_binding->binding;

        ++cur_binding;
    }

    context->current_binding = cur_binding;
    return S_OK;
}

static HRESULT d3d12_root_signature_init_dummy_sampler(struct d3d12_root_signature *root_signature,
        struct d3d12_device *device, struct vkd3d_descriptor_set_context *context)
{
    VkDescriptorSetLayoutBinding *binding = context->current_binding;

    root_signature->dummy_sampler.set = context->set_index;
    root_signature->dummy_sampler.binding = context->descriptor_binding++;

    binding->binding = root_signature->dummy_sampler.binding;
    binding->descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
    binding->descriptorCount = 1;
    binding->stageFlags = VK_SHADER_STAGE_ALL;
    binding->pImmutableSamplers = &device->vk_dummy_sampler;

    ++context->current_binding;
    return S_OK;
}

static HRESULT d3d12_root_signature_init_static_samplers(struct d3d12_root_signature *root_signature,
        struct d3d12_device *device, const D3D12_ROOT_SIGNATURE_DESC *desc,
        struct vkd3d_descriptor_set_context *context)
{
    VkDescriptorSetLayoutBinding *cur_binding = context->current_binding;
    unsigned int i;
    HRESULT hr;

    assert(root_signature->static_sampler_count == desc->NumStaticSamplers);
    for (i = 0; i < desc->NumStaticSamplers; ++i)
    {
        const D3D12_STATIC_SAMPLER_DESC *s = &desc->pStaticSamplers[i];

        if (s->RegisterSpace)
            FIXME("Unhandled register space %u for static sampler %u.\n", s->RegisterSpace, i);

        if (FAILED(hr = vkd3d_create_static_sampler(device, s, &root_signature->static_samplers[i])))
            return hr;

        cur_binding->binding = d3d12_root_signature_assign_vk_bindings(root_signature,
                VKD3D_SHADER_DESCRIPTOR_TYPE_SAMPLER, s->ShaderRegister, 1, false, false,
                vkd3d_shader_visibility_from_d3d12(s->ShaderVisibility), context);
        cur_binding->descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
        cur_binding->descriptorCount = 1;
        cur_binding->stageFlags = stage_flags_from_visibility(s->ShaderVisibility);
        cur_binding->pImmutableSamplers = &root_signature->static_samplers[i];

        ++cur_binding;
    }

    context->current_binding = cur_binding;
    return S_OK;
}

static HRESULT vkd3d_create_descriptor_set_layout(struct d3d12_device *device,
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

static HRESULT vkd3d_create_pipeline_layout(struct d3d12_device *device,
        unsigned int set_layout_count, const VkDescriptorSetLayout *set_layouts,
        unsigned int push_constant_count, const VkPushConstantRange *push_constants,
        VkPipelineLayout *pipeline_layout)
{
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    struct VkPipelineLayoutCreateInfo pipeline_layout_info;
    VkResult vr;

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

static HRESULT d3d12_root_signature_init(struct d3d12_root_signature *root_signature,
        struct d3d12_device *device, const D3D12_ROOT_SIGNATURE_DESC *desc)
{
    const struct vkd3d_vulkan_info *vk_info = &device->vk_info;
    struct vkd3d_descriptor_set_context context;
    VkDescriptorSetLayoutBinding *binding_desc;
    struct d3d12_root_signature_info info;
    VkDescriptorSetLayout set_layouts[2];
    bool needs_dummy_sampler;
    HRESULT hr;

    memset(&context, 0, sizeof(context));
    binding_desc = NULL;

    root_signature->ID3D12RootSignature_iface.lpVtbl = &d3d12_root_signature_vtbl;
    root_signature->refcount = 1;

    root_signature->vk_pipeline_layout = VK_NULL_HANDLE;
    root_signature->pool_sizes = NULL;
    root_signature->vk_push_set_layout = VK_NULL_HANDLE;
    root_signature->vk_set_layout = VK_NULL_HANDLE;
    root_signature->parameters = NULL;
    root_signature->descriptor_mapping = NULL;
    root_signature->static_sampler_count = 0;
    root_signature->static_samplers = NULL;

    if (desc->Flags)
        FIXME("Ignoring root signature flags %#x.\n", desc->Flags);

    if (FAILED(hr = d3d12_root_signature_info_from_desc(&info, desc)))
        return hr;
    if (info.cost > D3D12_MAX_ROOT_COST)
    {
        WARN("Root signature cost %zu exceeds maximum allowed cost.\n", info.cost);
        return E_INVALIDARG;
    }

    /* XXX: Vulkan buffer and image descriptors have different types. In order
     * to preserve compatibility between Vulkan resource bindings for the same
     * root signature, we create descriptor set layouts with two bindings for
     * each SRV and UAV. */
    info.descriptor_count += info.srv_count + info.uav_count;

    root_signature->descriptor_count = info.descriptor_count;
    root_signature->static_sampler_count = desc->NumStaticSamplers;
    root_signature->root_descriptor_count = info.root_descriptor_count;

    /* An additional sampler is created for SpvOpImageFetch. */
    needs_dummy_sampler = info.srv_count || info.buffer_srv_count;
    if (needs_dummy_sampler)
    {
        ++info.sampler_count;
        ++info.descriptor_count;
    }

    hr = E_OUTOFMEMORY;
    root_signature->parameter_count = desc->NumParameters;
    if (!(root_signature->parameters = vkd3d_calloc(root_signature->parameter_count,
            sizeof(*root_signature->parameters))))
        goto fail;
    if (!(root_signature->descriptor_mapping = vkd3d_calloc(root_signature->descriptor_count,
            sizeof(*root_signature->descriptor_mapping))))
        goto fail;
    root_signature->root_constant_count = info.root_constant_count;
    if (!(root_signature->root_constants = vkd3d_calloc(root_signature->root_constant_count,
            sizeof(*root_signature->root_constants))))
        goto fail;
    if (!(root_signature->static_samplers = vkd3d_calloc(root_signature->static_sampler_count,
            sizeof(*root_signature->static_samplers))))
        goto fail;

    if (!(binding_desc = vkd3d_calloc(info.descriptor_count, sizeof(*binding_desc))))
        goto fail;
    context.current_binding = binding_desc;

    if (FAILED(hr = d3d12_root_signature_init_descriptor_pool_size(root_signature, &info)))
        goto fail;

    if (FAILED(hr = d3d12_root_signature_init_root_descriptors(root_signature, desc, &context)))
        goto fail;

    /* We use KHR_push_descriptor for root descriptor parameters. */
    if (vk_info->KHR_push_descriptor && context.descriptor_binding)
    {
        if (FAILED(hr = vkd3d_create_descriptor_set_layout(device,
                VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR,
                context.descriptor_binding, binding_desc, &root_signature->vk_push_set_layout)))
            goto fail;

        set_layouts[context.set_index++] = root_signature->vk_push_set_layout;
        context.current_binding = binding_desc;
        context.descriptor_binding = 0;
    }

    if (FAILED(hr = d3d12_root_signature_init_push_constants(root_signature, desc, &info,
            root_signature->push_constant_ranges, &root_signature->push_constant_range_count)))
        goto fail;
    if (FAILED(hr = d3d12_root_signature_init_root_descriptor_tables(root_signature, desc, &context)))
        goto fail;
    if (FAILED(hr = d3d12_root_signature_init_static_samplers(root_signature, device, desc, &context)))
        goto fail;
    if (needs_dummy_sampler && FAILED(hr = d3d12_root_signature_init_dummy_sampler(root_signature,
            device, &context)))
        goto fail;

    root_signature->main_set = context.set_index;
    if (context.descriptor_binding)
    {
        if (FAILED(hr = vkd3d_create_descriptor_set_layout(device,
                0, context.descriptor_binding, binding_desc, &root_signature->vk_set_layout)))
            goto fail;

        set_layouts[context.set_index++] = root_signature->vk_set_layout;
    }
    vkd3d_free(binding_desc);
    binding_desc = NULL;

    if (FAILED(hr = vkd3d_create_pipeline_layout(device, context.set_index, set_layouts,
            root_signature->push_constant_range_count, root_signature->push_constant_ranges,
            &root_signature->vk_pipeline_layout)))
        goto fail;

    root_signature->device = device;
    ID3D12Device_AddRef(&device->ID3D12Device_iface);

    return S_OK;

fail:
    vkd3d_free(binding_desc);
    d3d12_root_signature_cleanup(root_signature, device);
    return hr;
}

HRESULT d3d12_root_signature_create(struct d3d12_device *device,
        const void *bytecode, size_t bytecode_length, struct d3d12_root_signature **root_signature)
{
    const struct vkd3d_shader_code dxbc = {bytecode, bytecode_length};
    union
    {
        D3D12_ROOT_SIGNATURE_DESC d3d12;
        struct vkd3d_root_signature_desc vkd3d;
    } root_signature_desc;
    struct d3d12_root_signature *object;
    HRESULT hr;
    int ret;

    if ((ret = vkd3d_shader_parse_root_signature(&dxbc, &root_signature_desc.vkd3d)) < 0)
    {
        WARN("Failed to parse root signature, vkd3d result %d.\n", ret);
        return hresult_from_vkd3d_result(ret);
    }

    if (!(object = vkd3d_malloc(sizeof(*object))))
    {
        vkd3d_shader_free_root_signature(&root_signature_desc.vkd3d);
        return E_OUTOFMEMORY;
    }

    hr = d3d12_root_signature_init(object, device, &root_signature_desc.d3d12);
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

/* ID3D12PipelineState */
static inline struct d3d12_pipeline_state *impl_from_ID3D12PipelineState(ID3D12PipelineState *iface)
{
    return CONTAINING_RECORD(iface, struct d3d12_pipeline_state, ID3D12PipelineState_iface);
}

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

static ULONG STDMETHODCALLTYPE d3d12_pipeline_state_AddRef(ID3D12PipelineState *iface)
{
    struct d3d12_pipeline_state *state = impl_from_ID3D12PipelineState(iface);
    ULONG refcount = InterlockedIncrement(&state->refcount);

    TRACE("%p increasing refcount to %u.\n", state, refcount);

    return refcount;
}

static ULONG STDMETHODCALLTYPE d3d12_pipeline_state_Release(ID3D12PipelineState *iface)
{
    struct d3d12_pipeline_state *state = impl_from_ID3D12PipelineState(iface);
    ULONG refcount = InterlockedDecrement(&state->refcount);

    TRACE("%p decreasing refcount to %u.\n", state, refcount);

    if (!refcount)
    {
        struct d3d12_device *device = state->device;
        const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
        unsigned int i;

        if (state->vk_bind_point == VK_PIPELINE_BIND_POINT_GRAPHICS)
        {
            for (i = 0; i < state->u.graphics.stage_count; ++i)
            {
                VK_CALL(vkDestroyShaderModule(device->vk_device, state->u.graphics.stages[i].module, NULL));
            }
            VK_CALL(vkDestroyRenderPass(device->vk_device, state->u.graphics.render_pass, NULL));
        }
        else if (state->vk_bind_point == VK_PIPELINE_BIND_POINT_COMPUTE)
        {
            VK_CALL(vkDestroyPipeline(device->vk_device, state->u.compute.vk_pipeline, NULL));
        }

        if (state->vk_set_layout)
            VK_CALL(vkDestroyDescriptorSetLayout(device->vk_device, state->vk_set_layout, NULL));
        if (state->vk_pipeline_layout)
            VK_CALL(vkDestroyPipelineLayout(device->vk_device, state->vk_pipeline_layout, NULL));

        vkd3d_free(state->uav_counters);

        vkd3d_free(state);

        ID3D12Device_Release(&device->ID3D12Device_iface);
    }

    return refcount;
}

static HRESULT STDMETHODCALLTYPE d3d12_pipeline_state_GetPrivateData(ID3D12PipelineState *iface,
        REFGUID guid, UINT *data_size, void *data)
{
    FIXME("iface %p, guid %s, data_size %p, data %p stub!", iface, debugstr_guid(guid), data_size, data);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d12_pipeline_state_SetPrivateData(ID3D12PipelineState *iface,
        REFGUID guid, UINT data_size, const void *data)
{
    FIXME("iface %p, guid %s, data_size %u, data %p stub!\n", iface, debugstr_guid(guid), data_size, data);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d12_pipeline_state_SetPrivateDataInterface(ID3D12PipelineState *iface,
        REFGUID guid, const IUnknown *data)
{
    FIXME("iface %p, guid %s, data %p stub!\n", iface, debugstr_guid(guid), data);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d12_pipeline_state_SetName(ID3D12PipelineState *iface, const WCHAR *name)
{
    struct d3d12_pipeline_state *state = impl_from_ID3D12PipelineState(iface);

    FIXME("iface %p, name %s stub!\n", iface, debugstr_w(name, state->device->wchar_size));

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d12_pipeline_state_GetDevice(ID3D12PipelineState *iface,
        REFIID riid, void **device)
{
    struct d3d12_pipeline_state *state = impl_from_ID3D12PipelineState(iface);

    TRACE("iface %p, riid %s, device %p.\n", iface, debugstr_guid(riid), device);

    return ID3D12Device_QueryInterface(&state->device->ID3D12Device_iface, riid, device);
}

static HRESULT STDMETHODCALLTYPE d3d12_pipeline_state_GetCachedBlob(ID3D12PipelineState *iface,
        ID3DBlob **blob)
{
    FIXME("iface %p, blob %p stub!\n", iface, blob);

    return E_NOTIMPL;
}

static const struct ID3D12PipelineStateVtbl d3d12_pipeline_state_vtbl =
{
    /* IUnknown methods */
    d3d12_pipeline_state_QueryInterface,
    d3d12_pipeline_state_AddRef,
    d3d12_pipeline_state_Release,
    /* ID3D12Object methods */
    d3d12_pipeline_state_GetPrivateData,
    d3d12_pipeline_state_SetPrivateData,
    d3d12_pipeline_state_SetPrivateDataInterface,
    d3d12_pipeline_state_SetName,
    /* ID3D12DeviceChild methods */
    d3d12_pipeline_state_GetDevice,
    /* ID3D12PipelineState methods */
    d3d12_pipeline_state_GetCachedBlob,
};

struct d3d12_pipeline_state *unsafe_impl_from_ID3D12PipelineState(ID3D12PipelineState *iface)
{
    if (!iface)
        return NULL;
    assert(iface->lpVtbl == &d3d12_pipeline_state_vtbl);
    return impl_from_ID3D12PipelineState(iface);
}

static void dump_shader(const char *path, const char *prefix, const void *data, size_t size)
{
    static unsigned int shader_id = 0;
    char filename[1024];
    FILE *f;

    snprintf(filename, ARRAY_SIZE(filename), "%s/vkd3d-shader-%s-%u.dxbc", path, prefix, shader_id);
    if (!(f = fopen(filename, "wb")))
    {
        ERR("Failed to open %s for dumping shader.\n", filename);
        return;
    }

    if (fwrite(data, 1, size, f) != size)
        ERR("Failed to write shader to %s.\n", filename);
    if (fclose(f))
        ERR("Failed to close stream %s.\n", filename);

    ++shader_id;
}

static void dump_shader_stage(VkShaderStageFlagBits stage, const void *data, size_t size)
{
    static bool enabled = true;
    const char *prefix;
    const char *path;

    if (!enabled)
        return;

    if (!(path = getenv("VKD3D_SHADER_DUMP_PATH")))
    {
        enabled = false;
        return;
    }

    switch (stage)
    {
        case VK_SHADER_STAGE_VERTEX_BIT:
            prefix = "vs";
            break;
        case VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT:
            prefix = "hs";
            break;
        case VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT:
            prefix = "ds";
            break;
        case VK_SHADER_STAGE_GEOMETRY_BIT:
            prefix = "gs";
            break;
        case VK_SHADER_STAGE_FRAGMENT_BIT:
            prefix = "ps";
            break;
        case VK_SHADER_STAGE_COMPUTE_BIT:
            prefix = "cs";
            break;
        default:
            prefix = "unk";
            break;
    }

    dump_shader(path, prefix, data, size);
}

static HRESULT create_shader_stage(struct d3d12_device *device,
        struct VkPipelineShaderStageCreateInfo *stage_desc, enum VkShaderStageFlagBits stage,
        const D3D12_SHADER_BYTECODE *code, const struct vkd3d_shader_interface *shader_interface,
        const struct vkd3d_shader_compile_arguments *compile_args)
{
    struct vkd3d_shader_code dxbc = {code->pShaderBytecode, code->BytecodeLength};
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    struct VkShaderModuleCreateInfo shader_desc;
    struct vkd3d_shader_code spirv = {};
    VkResult vr;
    int ret;

    stage_desc->sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stage_desc->pNext = NULL;
    stage_desc->flags = 0;
    stage_desc->stage = stage;
    stage_desc->pName = "main";
    stage_desc->pSpecializationInfo = NULL;

    shader_desc.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    shader_desc.pNext = NULL;
    shader_desc.flags = 0;

    dump_shader_stage(stage, code->pShaderBytecode, code->BytecodeLength);
    if ((ret = vkd3d_shader_compile_dxbc(&dxbc, &spirv, 0, shader_interface, compile_args)) < 0)
    {
        WARN("Failed to compile shader, vkd3d result %d.\n", ret);
        return hresult_from_vkd3d_result(ret);
    }
    shader_desc.codeSize = spirv.size;
    shader_desc.pCode = spirv.code;

    vr = VK_CALL(vkCreateShaderModule(device->vk_device, &shader_desc, NULL, &stage_desc->module));
    vkd3d_shader_free_shader_code(&spirv);
    if (vr < 0)
    {
        WARN("Failed to create Vulkan shader module, vr %d.\n", vr);
        return hresult_from_vk_result(vr);
    }

    return S_OK;
}

static HRESULT d3d12_pipeline_state_init_compute_uav_counters(struct d3d12_pipeline_state *state,
        struct d3d12_device *device, const struct d3d12_root_signature *root_signature,
        const struct vkd3d_shader_scan_info *shader_info)
{
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    struct vkd3d_descriptor_set_context context;
    VkDescriptorSetLayoutBinding *binding_desc;
    VkDescriptorSetLayout set_layouts[3];
    unsigned int uav_counter_count;
    unsigned int i, j;
    HRESULT hr;

    if (!(uav_counter_count = vkd3d_popcount(shader_info->uav_counter_mask)))
        return S_OK;

    if (!(binding_desc = vkd3d_calloc(uav_counter_count, sizeof(*binding_desc))))
        return E_OUTOFMEMORY;
    if (!(state->uav_counters = vkd3d_calloc(uav_counter_count, sizeof(*state->uav_counters))))
    {
        vkd3d_free(binding_desc);
        return E_OUTOFMEMORY;
    }
    state->uav_counter_mask = shader_info->uav_counter_mask;

    memset(&context, 0, sizeof(context));
    if (root_signature->vk_push_set_layout)
        set_layouts[context.set_index++] = root_signature->vk_push_set_layout;
    if (root_signature->vk_set_layout)
        set_layouts[context.set_index++] = root_signature->vk_set_layout;

    for (i = 0, j = 0; i < VKD3D_SHADER_MAX_UNORDERED_ACCESS_VIEWS; ++i)
    {
        if (!(shader_info->uav_counter_mask & (1u << i)))
            continue;

        state->uav_counters[j].register_index = i;
        state->uav_counters[j].binding.set = context.set_index;
        state->uav_counters[j].binding.binding = context.descriptor_binding;

        /* FIXME: For graphics pipeline we have to take the shader visibility
         * into account.
         */
        binding_desc[j].binding = context.descriptor_binding;
        binding_desc[j].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER;
        binding_desc[j].descriptorCount = 1;
        binding_desc[j].stageFlags = VK_SHADER_STAGE_ALL;
        binding_desc[j].pImmutableSamplers = NULL;

        ++context.descriptor_binding;
        ++j;
    }

    /* Create a descriptor set layout for UAV counters. */
    hr = vkd3d_create_descriptor_set_layout(device,
            0, context.descriptor_binding, binding_desc, &state->vk_set_layout);
    vkd3d_free(binding_desc);
    if (FAILED(hr))
    {
        vkd3d_free(state->uav_counters);
        return hr;
    }

    /* Create a pipeline layout which is compatible for all other descriptor
     * sets with the root signature's pipeline layout.
     */
    state->set_index = context.set_index;
    set_layouts[context.set_index++] = state->vk_set_layout;
    if (FAILED(hr = vkd3d_create_pipeline_layout(device, context.set_index, set_layouts,
            root_signature->push_constant_range_count, root_signature->push_constant_ranges,
            &state->vk_pipeline_layout)))
    {
        VK_CALL(vkDestroyDescriptorSetLayout(device->vk_device, state->vk_set_layout, NULL));
        vkd3d_free(state->uav_counters);
        return hr;
    }

    return S_OK;
}

static HRESULT d3d12_pipeline_state_init_compute(struct d3d12_pipeline_state *state,
        struct d3d12_device *device, const D3D12_COMPUTE_PIPELINE_STATE_DESC *desc)
{
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    const struct d3d12_root_signature *root_signature;
    struct vkd3d_shader_interface shader_interface;
    VkComputePipelineCreateInfo pipeline_info;
    struct vkd3d_shader_scan_info shader_info;
    struct vkd3d_shader_code dxbc;
    VkResult vr;
    HRESULT hr;
    int ret;

    state->ID3D12PipelineState_iface.lpVtbl = &d3d12_pipeline_state_vtbl;
    state->refcount = 1;

    state->vk_pipeline_layout = VK_NULL_HANDLE;
    state->vk_set_layout = VK_NULL_HANDLE;
    state->uav_counters = NULL;
    state->uav_counter_mask = 0;

    if (!(root_signature = unsafe_impl_from_ID3D12RootSignature(desc->pRootSignature)))
    {
        WARN("Root signature is NULL.\n");
        return E_INVALIDARG;
    }

    dxbc.code = desc->CS.pShaderBytecode;
    dxbc.size = desc->CS.BytecodeLength;
    if ((ret = vkd3d_shader_scan_dxbc(&dxbc, &shader_info)) < 0)
    {
        WARN("Failed to scan shader bytecode, vkd3d result %d.\n", ret);
        return hresult_from_vkd3d_result(ret);
    }

    if (FAILED(hr = d3d12_pipeline_state_init_compute_uav_counters(state,
            device, root_signature, &shader_info)))
    {
        WARN("Failed to create descriptor set layout for UAV counters, hr %#x.\n", hr);
        return hr;
    }

    shader_interface.bindings = root_signature->descriptor_mapping;
    shader_interface.binding_count = root_signature->descriptor_count;
    shader_interface.push_constant_buffers = root_signature->root_constants;
    shader_interface.push_constant_buffer_count = root_signature->root_constant_count;
    shader_interface.dummy_sampler = root_signature->dummy_sampler;
    shader_interface.uav_counters = state->uav_counters;
    shader_interface.uav_counter_count = vkd3d_popcount(state->uav_counter_mask);

    pipeline_info.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipeline_info.pNext = NULL;
    pipeline_info.flags = 0;
    if (FAILED(hr = create_shader_stage(device, &pipeline_info.stage,
            VK_SHADER_STAGE_COMPUTE_BIT, &desc->CS, &shader_interface, NULL)))
    {
        if (state->vk_set_layout)
            VK_CALL(vkDestroyDescriptorSetLayout(device->vk_device, state->vk_set_layout, NULL));
        if (state->vk_pipeline_layout)
            VK_CALL(vkDestroyPipelineLayout(device->vk_device, state->vk_pipeline_layout, NULL));
        vkd3d_free(state->uav_counters);
        return hr;
    }
    pipeline_info.layout = state->vk_pipeline_layout
            ? state->vk_pipeline_layout : root_signature->vk_pipeline_layout;
    pipeline_info.basePipelineHandle = VK_NULL_HANDLE;
    pipeline_info.basePipelineIndex = -1;

    vr = VK_CALL(vkCreateComputePipelines(device->vk_device, VK_NULL_HANDLE,
            1, &pipeline_info, NULL, &state->u.compute.vk_pipeline));
    VK_CALL(vkDestroyShaderModule(device->vk_device, pipeline_info.stage.module, NULL));
    if (vr)
    {
        WARN("Failed to create Vulkan compute pipeline, vr %d.\n", vr);
        if (state->vk_set_layout)
            VK_CALL(vkDestroyDescriptorSetLayout(device->vk_device, state->vk_set_layout, NULL));
        if (state->vk_pipeline_layout)
            VK_CALL(vkDestroyPipelineLayout(device->vk_device, state->vk_pipeline_layout, NULL));
        vkd3d_free(state->uav_counters);
        return hresult_from_vk_result(vr);
    }

    state->vk_bind_point = VK_PIPELINE_BIND_POINT_COMPUTE;
    state->device = device;
    ID3D12Device_AddRef(&device->ID3D12Device_iface);

    return S_OK;
}

HRESULT d3d12_pipeline_state_create_compute(struct d3d12_device *device,
        const D3D12_COMPUTE_PIPELINE_STATE_DESC *desc, struct d3d12_pipeline_state **state)
{
    struct d3d12_pipeline_state *object;
    HRESULT hr;

    if (!(object = vkd3d_malloc(sizeof(*object))))
        return E_OUTOFMEMORY;

    if (FAILED(hr = d3d12_pipeline_state_init_compute(object, device, desc)))
    {
        vkd3d_free(object);
        return hr;
    }

    TRACE("Created compute pipeline state %p.\n", object);

    *state = object;

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

static void rs_desc_from_d3d12(struct VkPipelineRasterizationStateCreateInfo *vk_desc,
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
    vk_desc->depthBiasEnable = VK_TRUE;
    vk_desc->depthBiasConstantFactor = d3d12_desc->DepthBias;
    vk_desc->depthBiasClamp = d3d12_desc->DepthBiasClamp;
    vk_desc->depthBiasSlopeFactor = d3d12_desc->SlopeScaledDepthBias;
    vk_desc->lineWidth = 1.0f;

    if (d3d12_desc->MultisampleEnable)
        FIXME("Ignoring MultisampleEnable %#x.\n", d3d12_desc->MultisampleEnable);
    if (d3d12_desc->AntialiasedLineEnable)
        FIXME("Ignoring AntialiasedLineEnable %#x.\n", d3d12_desc->AntialiasedLineEnable);
    if (d3d12_desc->ForcedSampleCount)
        FIXME("Ignoring ForcedSampleCount %#x.\n", d3d12_desc->ForcedSampleCount);
    if (d3d12_desc->ConservativeRaster)
        FIXME("Ignoring ConservativeRaster %#x.\n", d3d12_desc->ConservativeRaster);
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
        const D3D12_DEPTH_STENCIL_DESC *d3d12_desc)
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
    vk_desc->depthBoundsTestEnable = VK_FALSE;
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
    if (d3d12_desc->BlendEnable)
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

    if (d3d12_desc->LogicOpEnable)
        FIXME("Ignoring LogicOpEnable %#x.\n", d3d12_desc->LogicOpEnable);
}

static HRESULT compute_input_layout_offsets(const D3D12_INPUT_LAYOUT_DESC *input_layout_desc,
        uint32_t *offsets)
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

        if (!(format = vkd3d_get_format(e->Format, false)))
        {
            WARN("Invalid DXGI format %#x.\n", e->Format);
            return E_INVALIDARG;
        }

        if (e->AlignedByteOffset != D3D12_APPEND_ALIGNED_ELEMENT)
            offsets[i] = e->AlignedByteOffset;
        else
            offsets[i] = input_slot_offsets[e->InputSlot];

        input_slot_offsets[e->InputSlot] = align(offsets[i] + format->byte_count, 4);
    }

    return S_OK;
}

static unsigned int vkd3d_get_rt_format_swizzle(const struct vkd3d_format *format)
{
    if (format->dxgi_format == DXGI_FORMAT_A8_UNORM)
        return VKD3D_SWIZZLE(VKD3D_SWIZZLE_W, VKD3D_SWIZZLE_X, VKD3D_SWIZZLE_Y, VKD3D_SWIZZLE_Z);

    return VKD3D_NO_SWIZZLE;
}

/*
 * This must return results in accordance with render passes created by
 * d3d12_pipeline_state_init_graphics().
 */
bool d3d12_pipeline_state_is_render_pass_compatible(const struct d3d12_pipeline_state *state_a,
        const struct d3d12_pipeline_state *state_b)
{
    const struct d3d12_graphics_pipeline_state *a = &state_a->u.graphics;
    const struct d3d12_graphics_pipeline_state *b = &state_b->u.graphics;
    unsigned int i;

    if (!state_a != !state_b)
        return false;
    if (!state_a && !state_b)
        return true;

    if (state_a->vk_bind_point != VK_PIPELINE_BIND_POINT_GRAPHICS
            || state_b->vk_bind_point != VK_PIPELINE_BIND_POINT_GRAPHICS)
        return false;

    if (a->rt_idx != b->rt_idx)
        return false;
    if (a->attachment_count != b->attachment_count)
        return false;

    for (i = 0; i < a->attachment_count; ++i)
    {
        if (a->attachments[i].format != b->attachments[i].format)
            return false;
    }

    return true;
}

static HRESULT d3d12_pipeline_state_init_graphics(struct d3d12_pipeline_state *state,
        struct d3d12_device *device, const D3D12_GRAPHICS_PIPELINE_STATE_DESC *desc)
{
    unsigned int ps_output_swizzle[D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT];
    struct d3d12_graphics_pipeline_state *graphics = &state->u.graphics;
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    const struct vkd3d_shader_compile_arguments *compile_args;
    uint32_t aligned_offsets[D3D12_VS_INPUT_REGISTER_COUNT];
    struct vkd3d_shader_compile_arguments ps_compile_args;
    const struct d3d12_root_signature *root_signature;
    struct vkd3d_shader_interface shader_interface;
    struct vkd3d_shader_signature input_signature;
    struct VkSubpassDescription sub_pass_desc;
    struct VkRenderPassCreateInfo pass_desc;
    const struct vkd3d_format *format;
    enum VkVertexInputRate input_rate;
    unsigned int i, j;
    size_t rt_count;
    uint32_t mask;
    VkResult vr;
    HRESULT hr;
    int ret;

    static const struct
    {
        enum VkShaderStageFlagBits stage;
        ptrdiff_t offset;
    }
    shader_stages[] =
    {
        {VK_SHADER_STAGE_VERTEX_BIT,                  offsetof(D3D12_GRAPHICS_PIPELINE_STATE_DESC, VS)},
        {VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT,    offsetof(D3D12_GRAPHICS_PIPELINE_STATE_DESC, HS)},
        {VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT, offsetof(D3D12_GRAPHICS_PIPELINE_STATE_DESC, DS)},
        {VK_SHADER_STAGE_GEOMETRY_BIT,                offsetof(D3D12_GRAPHICS_PIPELINE_STATE_DESC, GS)},
        {VK_SHADER_STAGE_FRAGMENT_BIT,                offsetof(D3D12_GRAPHICS_PIPELINE_STATE_DESC, PS)},
    };

    state->ID3D12PipelineState_iface.lpVtbl = &d3d12_pipeline_state_vtbl;
    state->refcount = 1;

    state->vk_pipeline_layout = VK_NULL_HANDLE;
    state->vk_set_layout = VK_NULL_HANDLE;
    state->uav_counters = NULL;
    state->uav_counter_mask = 0;

    if (!(root_signature = unsafe_impl_from_ID3D12RootSignature(desc->pRootSignature)))
    {
        WARN("Root signature is NULL.\n");
        return E_INVALIDARG;
    }

    rt_count = desc->NumRenderTargets;
    if (rt_count > ARRAY_SIZE(graphics->attachments) - 1)
    {
        FIXME("NumRenderTargets %zu > %zu, ignoring extra formats.\n",
                rt_count, ARRAY_SIZE(graphics->attachments) - 1);
        rt_count = ARRAY_SIZE(graphics->attachments) - 1;
    }

    graphics->rt_idx = 0;
    if (desc->DepthStencilState.DepthEnable || desc->DepthStencilState.StencilEnable)
    {
        const D3D12_DEPTH_STENCIL_DESC *ds_desc = &desc->DepthStencilState;
        VkImageLayout depth_layout;

        if (!(format = vkd3d_get_format(desc->DSVFormat, true)))
        {
            WARN("Invalid DXGI format %#x.\n", desc->DSVFormat);
            hr = E_INVALIDARG;
            goto fail;
        }

        if ((ds_desc->DepthEnable && ds_desc->DepthWriteMask)
                || (ds_desc->StencilEnable && ds_desc->StencilWriteMask))
            depth_layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        else
            depth_layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

        graphics->attachments[0].flags = 0;
        graphics->attachments[0].format = format->vk_format;
        graphics->attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
        if (desc->DepthStencilState.DepthEnable)
        {
            graphics->attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
            graphics->attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        }
        else
        {
            graphics->attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            graphics->attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        }
        if (desc->DepthStencilState.StencilEnable)
        {
            graphics->attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
            graphics->attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_STORE;
        }
        else
        {
            graphics->attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            graphics->attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        }
        graphics->attachments[0].initialLayout = depth_layout;
        graphics->attachments[0].finalLayout = depth_layout;

        graphics->attachment_references[0].attachment = 0;
        graphics->attachment_references[0].layout = depth_layout;
        ++graphics->rt_idx;
    }

    for (i = 0; i < rt_count; ++i)
    {
        unsigned int blend_idx = desc->BlendState.IndependentBlendEnable ? i : 0;
        size_t idx = graphics->rt_idx + i;

        if (!(format = vkd3d_get_format(desc->RTVFormats[i], false)))
        {
            WARN("Invalid DXGI format %#x.\n", desc->RTVFormats[i]);
            hr = E_INVALIDARG;
            goto fail;
        }

        ps_output_swizzle[i] = vkd3d_get_rt_format_swizzle(format);

        graphics->attachments[idx].flags = 0;
        graphics->attachments[idx].format = format->vk_format;
        graphics->attachments[idx].samples = VK_SAMPLE_COUNT_1_BIT;
        graphics->attachments[idx].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        graphics->attachments[idx].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        graphics->attachments[idx].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        graphics->attachments[idx].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        graphics->attachments[idx].initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        graphics->attachments[idx].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        graphics->attachment_references[idx].attachment = idx;
        graphics->attachment_references[idx].layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        blend_attachment_from_d3d12(&graphics->blend_attachments[i], &desc->BlendState.RenderTarget[blend_idx]);
    }
    graphics->attachment_count = graphics->rt_idx + rt_count;

    ps_compile_args.output_swizzles = ps_output_swizzle;
    ps_compile_args.output_swizzle_count = rt_count;

    shader_interface.bindings = root_signature->descriptor_mapping;
    shader_interface.binding_count = root_signature->descriptor_count;
    shader_interface.push_constant_buffers = root_signature->root_constants;
    shader_interface.push_constant_buffer_count = root_signature->root_constant_count;
    shader_interface.dummy_sampler = root_signature->dummy_sampler;
    shader_interface.uav_counters = NULL;
    shader_interface.uav_counter_count = 0;

    memset(&input_signature, 0, sizeof(input_signature));
    for (i = 0, graphics->stage_count = 0; i < ARRAY_SIZE(shader_stages); ++i)
    {
        const D3D12_SHADER_BYTECODE *b = (const void *)((uintptr_t)desc + shader_stages[i].offset);
        const struct vkd3d_shader_code dxbc = {b->pShaderBytecode, b->BytecodeLength};
        struct vkd3d_shader_scan_info shader_info;

        if (!b->pShaderBytecode)
            continue;

        if ((ret = vkd3d_shader_scan_dxbc(&dxbc, &shader_info)) < 0)
        {
            WARN("Failed to scan shader bytecode, stage %#x, vkd3d result %d.\n",
                    shader_stages[i].stage, ret);
            hr = hresult_from_vkd3d_result(ret);
            goto fail;
        }
        if (shader_info.uav_counter_mask)
            FIXME("UAV counters not implemented for graphics pipelines.\n");

        compile_args = NULL;
        switch (shader_stages[i].stage)
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
                if (desc->PrimitiveTopologyType != D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH)
                {
                    WARN("D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH must be used with tessellation shaders.\n");
                    hr = E_INVALIDARG;
                    goto fail;
                }
                break;

            case VK_SHADER_STAGE_GEOMETRY_BIT:
                break;

            case VK_SHADER_STAGE_FRAGMENT_BIT:
                compile_args = &ps_compile_args;
                break;

            default:
                hr = E_INVALIDARG;
                goto fail;
        }

        if (FAILED(hr = create_shader_stage(device, &graphics->stages[graphics->stage_count],
                shader_stages[i].stage, b, &shader_interface, compile_args)))
            goto fail;

        ++graphics->stage_count;
    }

    graphics->attribute_count = desc->InputLayout.NumElements;
    if (graphics->attribute_count > ARRAY_SIZE(graphics->attributes))
    {
        FIXME("InputLayout.NumElements %zu > %zu, ignoring extra elements.\n",
                graphics->attribute_count, ARRAY_SIZE(graphics->attributes));
        graphics->attribute_count = ARRAY_SIZE(graphics->attributes);
    }

    if (FAILED(hr = compute_input_layout_offsets(&desc->InputLayout, aligned_offsets)))
        goto fail;

    for (i = 0, j = 0, mask = 0; i < graphics->attribute_count; ++i)
    {
        const D3D12_INPUT_ELEMENT_DESC *e = &desc->InputLayout.pInputElementDescs[i];
        const struct vkd3d_shader_signature_element *signature_element;

        if (!(format = vkd3d_get_format(e->Format, false)))
        {
            WARN("Invalid DXGI format %#x.\n", e->Format);
            hr = E_INVALIDARG;
            goto fail;
        }

        if (e->InputSlot >= ARRAY_SIZE(graphics->input_rates))
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
        ++j;

        switch (e->InputSlotClass)
        {
            case D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA:
                input_rate = VK_VERTEX_INPUT_RATE_VERTEX;
                break;

            case D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA:
                if (e->InstanceDataStepRate != 1)
                    FIXME("Ignoring step rate %#x on input element %u.\n", e->InstanceDataStepRate, i);
                input_rate = VK_VERTEX_INPUT_RATE_INSTANCE;
                break;

            default:
                FIXME("Unhandled input slot class %#x on input element %u.\n", e->InputSlotClass, i);
                hr = E_INVALIDARG;
                goto fail;
        }

        if (mask & (1u << e->InputSlot) && graphics->input_rates[e->InputSlot] != input_rate)
        {
            FIXME("Input slot class %#x on input element %u conflicts with earlier input slot class %#x.\n",
                    e->InputSlotClass, e->InputSlot, graphics->input_rates[e->InputSlot]);
            hr = E_INVALIDARG;
            goto fail;
        }
        graphics->input_rates[e->InputSlot] = input_rate;
        mask |= 1u << e->InputSlot;
    }
    graphics->attribute_count = j;
    vkd3d_shader_free_shader_signature(&input_signature);

    sub_pass_desc.flags = 0;
    sub_pass_desc.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub_pass_desc.inputAttachmentCount = 0;
    sub_pass_desc.pInputAttachments = NULL;
    sub_pass_desc.colorAttachmentCount = rt_count;
    sub_pass_desc.pColorAttachments = &graphics->attachment_references[graphics->rt_idx];
    sub_pass_desc.pResolveAttachments = NULL;
    if (graphics->rt_idx)
        sub_pass_desc.pDepthStencilAttachment = &graphics->attachment_references[0];
    else
        sub_pass_desc.pDepthStencilAttachment = NULL;
    sub_pass_desc.preserveAttachmentCount = 0;
    sub_pass_desc.pPreserveAttachments = NULL;

    pass_desc.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    pass_desc.pNext = NULL;
    pass_desc.flags = 0;
    pass_desc.attachmentCount = graphics->attachment_count;
    pass_desc.pAttachments = graphics->attachments;
    pass_desc.subpassCount = 1;
    pass_desc.pSubpasses = &sub_pass_desc;
    pass_desc.dependencyCount = 0;
    pass_desc.pDependencies = NULL;
    if ((vr = VK_CALL(vkCreateRenderPass(device->vk_device, &pass_desc, NULL, &graphics->render_pass))) < 0)
    {
        WARN("Failed to create Vulkan render pass, vr %d.\n", vr);
        hr = hresult_from_vk_result(vr);
        goto fail;
    }

    rs_desc_from_d3d12(&graphics->rs_desc, &desc->RasterizerState);

    graphics->ms_desc.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    graphics->ms_desc.pNext = NULL;
    graphics->ms_desc.flags = 0;
    graphics->ms_desc.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    graphics->ms_desc.sampleShadingEnable = VK_FALSE;
    graphics->ms_desc.minSampleShading = 0.0f;
    graphics->ms_desc.pSampleMask = NULL;
    graphics->ms_desc.alphaToCoverageEnable = desc->BlendState.AlphaToCoverageEnable;
    graphics->ms_desc.alphaToOneEnable = VK_FALSE;

    ds_desc_from_d3d12(&graphics->ds_desc, &desc->DepthStencilState);

    graphics->root_signature = root_signature;

    state->vk_bind_point = VK_PIPELINE_BIND_POINT_GRAPHICS;
    state->device = device;
    ID3D12Device_AddRef(&device->ID3D12Device_iface);

    return S_OK;

fail:
    for (i = 0; i < graphics->stage_count; ++i)
    {
        VK_CALL(vkDestroyShaderModule(device->vk_device, state->u.graphics.stages[i].module, NULL));
    }
    vkd3d_shader_free_shader_signature(&input_signature);

    return hr;
}

HRESULT d3d12_pipeline_state_create_graphics(struct d3d12_device *device,
        const D3D12_GRAPHICS_PIPELINE_STATE_DESC *desc, struct d3d12_pipeline_state **state)
{
    struct d3d12_pipeline_state *object;
    HRESULT hr;

    if (!(object = vkd3d_malloc(sizeof(*object))))
        return E_OUTOFMEMORY;

    if (FAILED(hr = d3d12_pipeline_state_init_graphics(object, device, desc)))
    {
        vkd3d_free(object);
        return hr;
    }

    TRACE("Created graphics pipeline state %p.\n", object);

    *state = object;

    return S_OK;
}
