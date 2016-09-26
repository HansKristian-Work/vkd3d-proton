/*
 * Copyright 2016 Józef Kucia for CodeWeavers
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

static HRESULT d3d12_root_signature_init(struct d3d12_root_signature *root_signature,
        struct d3d12_device *device, const D3D12_ROOT_SIGNATURE_DESC *desc)
{
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    VkPipelineLayoutCreateInfo pipeline_layout_info;
    VkResult vr;

    root_signature->ID3D12RootSignature_iface.lpVtbl = &d3d12_root_signature_vtbl;
    root_signature->refcount = 1;

    if (desc->NumParameters)
        FIXME("Non-empty root signatures not supported yet.\n");
    if (desc->NumStaticSamplers)
        FIXME("Static samplers not implemented yet.\n");
    if (desc->Flags)
        FIXME("Ignoring root signature flags %#x.\n", desc->Flags);

    pipeline_layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipeline_layout_info.pNext = NULL;
    pipeline_layout_info.flags = 0;
    pipeline_layout_info.setLayoutCount = 0;
    pipeline_layout_info.pSetLayouts = NULL;
    pipeline_layout_info.pushConstantRangeCount = 0;
    pipeline_layout_info.pPushConstantRanges = NULL;

    if ((vr = VK_CALL(vkCreatePipelineLayout(device->vk_device, &pipeline_layout_info, NULL,
            &root_signature->vk_pipeline_layout))))
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

        VK_CALL(vkDestroyPipeline(device->vk_device, state->vk_pipeline, NULL));

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

static HRESULT d3d12_pipeline_state_init_compute(struct d3d12_pipeline_state *state,
        struct d3d12_device *device, const D3D12_COMPUTE_PIPELINE_STATE_DESC *desc)
{
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    struct d3d12_root_signature *root_signature;
    VkComputePipelineCreateInfo pipeline_info;
    VkShaderModuleCreateInfo shader_info;
    VkShaderModule shader;
    VkResult vr;

    state->ID3D12PipelineState_iface.lpVtbl = &d3d12_pipeline_state_vtbl;
    state->refcount = 1;

    if (!(root_signature = unsafe_impl_from_ID3D12RootSignature(desc->pRootSignature)))
    {
        WARN("Root signature is NULL.\n");
        return E_INVALIDARG;
    }

    shader_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    shader_info.pNext = NULL;
    shader_info.flags = 0;
    shader_info.codeSize = desc->CS.BytecodeLength;
    shader_info.pCode = desc->CS.pShaderBytecode;

    if ((vr = VK_CALL(vkCreateShaderModule(device->vk_device, &shader_info, NULL, &shader))))
    {
        WARN("Failed to create Vulkan shader module, vr %d.\n", vr);
        return hresult_from_vk_result(vr);
    }

    pipeline_info.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipeline_info.pNext = NULL;
    pipeline_info.flags = 0;
    pipeline_info.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    pipeline_info.stage.pNext = NULL;
    pipeline_info.stage.flags = 0;
    pipeline_info.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    pipeline_info.stage.module = shader;
    pipeline_info.stage.pName = "main";
    pipeline_info.stage.pSpecializationInfo = NULL;
    pipeline_info.layout = root_signature->vk_pipeline_layout;
    pipeline_info.basePipelineHandle = VK_NULL_HANDLE;
    pipeline_info.basePipelineIndex = -1;

    vr = VK_CALL(vkCreateComputePipelines(device->vk_device, VK_NULL_HANDLE,
            1, &pipeline_info, NULL, &state->vk_pipeline));
    VK_CALL(vkDestroyShaderModule(device->vk_device, shader, NULL));
    if (vr)
    {
        WARN("Failed to create Vulkan compute pipeline, vr %d.\n", vr);
        return hresult_from_vk_result(vr);
    }

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
