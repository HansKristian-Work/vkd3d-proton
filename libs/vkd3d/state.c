/*
 * Copyright 2016 Józef Kucia for CodeWeavers
 * Copyright 2016 Henri Verbeet for CodeWeavers
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include "vkd3d_private.h"

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

static ULONG STDMETHODCALLTYPE d3d12_root_signature_Release(ID3D12RootSignature *iface)
{
    struct d3d12_root_signature *root_signature = impl_from_ID3D12RootSignature(iface);
    ULONG refcount = InterlockedDecrement(&root_signature->refcount);

    TRACE("%p decreasing refcount to %u.\n", root_signature, refcount);

    if (!refcount)
    {
        struct d3d12_device *device = root_signature->device;
        const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;

        VK_CALL(vkDestroyPipelineLayout(device->vk_device, root_signature->vk_pipeline_layout, NULL));
        VK_CALL(vkDestroyDescriptorSetLayout(device->vk_device, root_signature->vk_set_layout, NULL));

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
    FIXME("iface %p, name %s stub!\n", iface, debugstr_w(name));

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

static struct d3d12_root_signature *unsafe_impl_from_ID3D12RootSignature(ID3D12RootSignature *iface)
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

static HRESULT d3d12_root_signature_init(struct d3d12_root_signature *root_signature,
        struct d3d12_device *device, const D3D12_ROOT_SIGNATURE_DESC *desc)
{
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    struct VkPipelineLayoutCreateInfo pipeline_layout_info;
    struct VkDescriptorSetLayoutBinding *binding_desc;
    struct VkDescriptorSetLayoutCreateInfo set_desc;
    unsigned int i;
    VkResult vr;

    root_signature->ID3D12RootSignature_iface.lpVtbl = &d3d12_root_signature_vtbl;
    root_signature->refcount = 1;

    if (desc->NumStaticSamplers)
        FIXME("Static samplers not implemented yet.\n");
    if (desc->Flags)
        FIXME("Ignoring root signature flags %#x.\n", desc->Flags);

    if (!(binding_desc = vkd3d_calloc(desc->NumParameters, sizeof(*binding_desc))))
        return E_OUTOFMEMORY;

    for (i = 0; i < desc->NumParameters; ++i)
    {
        const D3D12_ROOT_PARAMETER *p = &desc->pParameters[i];

        switch (p->ParameterType)
        {
            case D3D12_ROOT_PARAMETER_TYPE_CBV:
                /* FIXME: Register spaces should map to set layouts. */
                if (p->u.Descriptor.RegisterSpace)
                {
                    FIXME("Unhandled register space %u for parameter %u.\n", p->u.Descriptor.RegisterSpace, i);
                    vkd3d_free(binding_desc);
                    return E_NOTIMPL;
                }
                binding_desc[i].binding = p->u.Descriptor.ShaderRegister;
                binding_desc[i].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                binding_desc[i].descriptorCount = 1;
                binding_desc[i].stageFlags = stage_flags_from_visibility(p->ShaderVisibility);
                binding_desc[i].pImmutableSamplers = NULL;
                break;

            default:
                FIXME("Unhandled type %#x for parameter %u.\n", p->ParameterType, i);
                vkd3d_free(binding_desc);
                return E_NOTIMPL;
        }
    }

    set_desc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    set_desc.pNext = NULL;
    set_desc.flags = 0;
    set_desc.bindingCount = desc->NumParameters;
    set_desc.pBindings = binding_desc;

    vr = VK_CALL(vkCreateDescriptorSetLayout(device->vk_device, &set_desc, NULL, &root_signature->vk_set_layout));
    vkd3d_free(binding_desc);
    if (vr < 0)
    {
        WARN("Failed to create Vulkan descriptor set layout, vr %d.\n", vr);
        return hresult_from_vk_result(vr);
    }

    pipeline_layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipeline_layout_info.pNext = NULL;
    pipeline_layout_info.flags = 0;
    pipeline_layout_info.setLayoutCount = 1;
    pipeline_layout_info.pSetLayouts = &root_signature->vk_set_layout;
    pipeline_layout_info.pushConstantRangeCount = 0;
    pipeline_layout_info.pPushConstantRanges = NULL;

    if ((vr = VK_CALL(vkCreatePipelineLayout(device->vk_device, &pipeline_layout_info, NULL,
            &root_signature->vk_pipeline_layout))) < 0)
    {
        WARN("Failed to create Vulkan pipeline layout, vr %d.\n", vr);
        return hresult_from_vk_result(vr);
    }

    root_signature->device = device;
    ID3D12Device_AddRef(&device->ID3D12Device_iface);

    return S_OK;
}

HRESULT d3d12_root_signature_create(struct d3d12_device *device,
        const D3D12_ROOT_SIGNATURE_DESC *desc, struct d3d12_root_signature **root_signature)
{
    struct d3d12_root_signature *object;
    HRESULT hr;

    if (!(object = vkd3d_malloc(sizeof(*object))))
        return E_OUTOFMEMORY;

    if (FAILED(hr = d3d12_root_signature_init(object, device, desc)))
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
    FIXME("iface %p, name %s stub!\n", iface, debugstr_w(name));

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

static HRESULT create_shader_stage(struct d3d12_device *device, struct VkPipelineShaderStageCreateInfo *stage_desc,
        enum VkShaderStageFlagBits stage, const D3D12_SHADER_BYTECODE *code)
{
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    struct VkShaderModuleCreateInfo shader_desc;
    VkResult vr;

    stage_desc->sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stage_desc->pNext = NULL;
    stage_desc->flags = 0;
    stage_desc->stage = stage;
    stage_desc->pName = "main";
    stage_desc->pSpecializationInfo = NULL;

    shader_desc.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    shader_desc.pNext = NULL;
    shader_desc.flags = 0;
    shader_desc.codeSize = code->BytecodeLength;
    shader_desc.pCode = code->pShaderBytecode;
    if ((vr = VK_CALL(vkCreateShaderModule(device->vk_device, &shader_desc, NULL, &stage_desc->module))) < 0)
    {
        WARN("Failed to create Vulkan shader module, vr %d.\n", vr);
        return hresult_from_vk_result(vr);
    }

    return S_OK;
}

static HRESULT d3d12_pipeline_state_init_compute(struct d3d12_pipeline_state *state,
        struct d3d12_device *device, const D3D12_COMPUTE_PIPELINE_STATE_DESC *desc)
{
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    struct d3d12_root_signature *root_signature;
    VkComputePipelineCreateInfo pipeline_info;
    VkResult vr;
    HRESULT hr;

    state->ID3D12PipelineState_iface.lpVtbl = &d3d12_pipeline_state_vtbl;
    state->refcount = 1;

    if (!(root_signature = unsafe_impl_from_ID3D12RootSignature(desc->pRootSignature)))
    {
        WARN("Root signature is NULL.\n");
        return E_INVALIDARG;
    }

    pipeline_info.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipeline_info.pNext = NULL;
    pipeline_info.flags = 0;
    if (FAILED(hr = create_shader_stage(device, &pipeline_info.stage, VK_SHADER_STAGE_COMPUTE_BIT, &desc->CS)))
        return hr;
    pipeline_info.layout = root_signature->vk_pipeline_layout;
    pipeline_info.basePipelineHandle = VK_NULL_HANDLE;
    pipeline_info.basePipelineIndex = -1;

    vr = VK_CALL(vkCreateComputePipelines(device->vk_device, VK_NULL_HANDLE,
            1, &pipeline_info, NULL, &state->u.compute.vk_pipeline));
    VK_CALL(vkDestroyShaderModule(device->vk_device, pipeline_info.stage.module, NULL));
    if (vr)
    {
        WARN("Failed to create Vulkan compute pipeline, vr %d.\n", vr);
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

static enum VkCompareOp vk_compare_op_from_d3d12(D3D12_COMPARISON_FUNC op)
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
    vk_desc->reference = 0; /* FIXME: From OMSetStencilRef(). */
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

static HRESULT d3d12_pipeline_state_init_graphics(struct d3d12_pipeline_state *state,
        struct d3d12_device *device, const D3D12_GRAPHICS_PIPELINE_STATE_DESC *desc)
{
    struct d3d12_graphics_pipeline_state *graphics = &state->u.graphics;
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    struct VkSubpassDescription sub_pass_desc;
    struct VkRenderPassCreateInfo pass_desc;
    const struct vkd3d_format *format;
    enum VkVertexInputRate input_rate;
    size_t rt_count;
    unsigned int i;
    uint32_t mask;
    VkResult vr;
    HRESULT hr;

    static const struct
    {
        enum VkShaderStageFlagBits stage;
        ptrdiff_t offset;
    }
    shader_stages[] =
    {
        {VK_SHADER_STAGE_VERTEX_BIT,                  offsetof(D3D12_GRAPHICS_PIPELINE_STATE_DESC, VS)},
        {VK_SHADER_STAGE_FRAGMENT_BIT,                offsetof(D3D12_GRAPHICS_PIPELINE_STATE_DESC, PS)},
        {VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT, offsetof(D3D12_GRAPHICS_PIPELINE_STATE_DESC, DS)},
        {VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT,    offsetof(D3D12_GRAPHICS_PIPELINE_STATE_DESC, HS)},
        {VK_SHADER_STAGE_GEOMETRY_BIT,                offsetof(D3D12_GRAPHICS_PIPELINE_STATE_DESC, GS)},
    };

    state->ID3D12PipelineState_iface.lpVtbl = &d3d12_pipeline_state_vtbl;
    state->refcount = 1;

    for (i = 0, graphics->stage_count = 0; i < ARRAY_SIZE(shader_stages); ++i)
    {
        const D3D12_SHADER_BYTECODE *b = (const void *)((uintptr_t)desc + shader_stages[i].offset);

        if (!b->pShaderBytecode)
            continue;

        if (FAILED(hr = create_shader_stage(device, &graphics->stages[graphics->stage_count],
                shader_stages[i].stage, b)))
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

    for (i = 0, mask = 0; i < graphics->attribute_count; ++i)
    {
        const D3D12_INPUT_ELEMENT_DESC *e = &desc->InputLayout.pInputElementDescs[i];

        if (!(format = vkd3d_get_format(e->Format)))
        {
            WARN("Invalid DXGI format %#x.\n", e->Format);
            hr = E_FAIL;
            goto fail;
        }

        if (e->InputSlot >= ARRAY_SIZE(graphics->input_rates))
        {
            WARN("Invalid input slot %#x.\n", e->InputSlot);
            hr = E_FAIL;
            goto fail;
        }

        graphics->attributes[i].location = i;
        graphics->attributes[i].binding = e->InputSlot;
        graphics->attributes[i].format = format->vk_format;
        if (e->AlignedByteOffset == D3D12_APPEND_ALIGNED_ELEMENT)
            FIXME("D3D12_APPEND_ALIGNED_ELEMENT not implemented.\n");
        graphics->attributes[i].offset = e->AlignedByteOffset;

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
                hr = E_FAIL;
                goto fail;
        }

        if (mask & (1u << e->InputSlot) && graphics->input_rates[e->InputSlot] != input_rate)
        {
            FIXME("Input slot class %#x on input element %u conflicts with earlier input slot class %#x.\n",
                    e->InputSlotClass, e->InputSlot, graphics->input_rates[e->InputSlot]);
            hr = E_FAIL;
            goto fail;
        }
        graphics->input_rates[e->InputSlot] = input_rate;
        mask |= 1u << e->InputSlot;
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
        if (!(format = vkd3d_get_format(desc->DSVFormat)))
        {
            WARN("Invalid DXGI format %#x.\n", desc->DSVFormat);
            hr = E_FAIL;
            goto fail;
        }

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
        graphics->attachments[0].initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        graphics->attachments[0].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        graphics->attachment_references[0].attachment = 0;
        graphics->attachment_references[0].layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        ++graphics->rt_idx;
    }

    for (i = 0; i < rt_count; ++i)
    {
        unsigned int blend_idx = desc->BlendState.IndependentBlendEnable ? i : 0;
        size_t idx = graphics->rt_idx + i;

        if (!(format = vkd3d_get_format(desc->RTVFormats[i])))
        {
            WARN("Invalid DXGI format %#x.\n", desc->RTVFormats[i]);
            hr = E_FAIL;
            goto fail;
        }

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

    graphics->root_signature = unsafe_impl_from_ID3D12RootSignature(desc->pRootSignature);

    state->vk_bind_point = VK_PIPELINE_BIND_POINT_GRAPHICS;
    state->device = device;
    ID3D12Device_AddRef(&device->ID3D12Device_iface);

    return S_OK;

fail:
    for (i = 0; i < graphics->stage_count; ++i)
    {
        VK_CALL(vkDestroyShaderModule(device->vk_device, state->u.graphics.stages[i].module, NULL));
    }

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
