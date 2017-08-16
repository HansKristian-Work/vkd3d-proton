/*
 * Copyright 2016-2017 Józef Kucia for CodeWeavers
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

#define INITGUID
#include "vkd3d_private.h"

HRESULT vkd3d_create_device(const struct vkd3d_device_create_info *create_info,
        REFIID riid, void **device)
{
    struct d3d12_device *object;
    HRESULT hr;

    TRACE("create_info %p, riid %s, device %p.\n", create_info, debugstr_guid(riid), device);

    if (create_info->minimum_feature_level < D3D_FEATURE_LEVEL_11_0
            || !is_valid_feature_level(create_info->minimum_feature_level))
    {
        WARN("Invalid feature level %#x.\n", create_info->minimum_feature_level);
        return E_INVALIDARG;
    }

    if (!check_feature_level_support(create_info->minimum_feature_level))
    {
        FIXME("Unsupported feature level %#x.\n", create_info->minimum_feature_level);
        return E_INVALIDARG;
    }

    if (FAILED(hr = d3d12_device_create(create_info, &object)))
        return hr;

    return return_interface((IUnknown *)&object->ID3D12Device_iface, &IID_ID3D12Device,
            riid, device);
}

struct d3d12_root_signature_deserializer
{
    ID3D12RootSignatureDeserializer ID3D12RootSignatureDeserializer_iface;
    LONG refcount;

    D3D12_ROOT_SIGNATURE_DESC desc;
};

static struct d3d12_root_signature_deserializer *impl_from_ID3D12RootSignatureDeserializer(
        ID3D12RootSignatureDeserializer *iface)
{
    return CONTAINING_RECORD(iface, struct d3d12_root_signature_deserializer, ID3D12RootSignatureDeserializer_iface);
}

static HRESULT STDMETHODCALLTYPE d3d12_root_signature_deserializer_QueryInterface(
        ID3D12RootSignatureDeserializer *iface, REFIID riid, void **object)
{
    TRACE("iface %p, riid %s, object %p.\n", iface, debugstr_guid(riid), object);

    /* QueryInterface() implementation seems to be broken, E_NOINTERFACE is
     * returned for IUnknown. */
    if (IsEqualGUID(riid, &IID_ID3D12RootSignatureDeserializer))
    {
        ID3D12RootSignatureDeserializer_AddRef(iface);
        *object = iface;
        return S_OK;
    }

    WARN("%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid(riid));

    *object = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE d3d12_root_signature_deserializer_AddRef(ID3D12RootSignatureDeserializer *iface)
{
    struct d3d12_root_signature_deserializer *deserializer = impl_from_ID3D12RootSignatureDeserializer(iface);
    ULONG refcount = InterlockedIncrement(&deserializer->refcount);

    TRACE("%p increasing refcount to %u.\n", deserializer, refcount);

    return refcount;
}

static ULONG STDMETHODCALLTYPE d3d12_root_signature_deserializer_Release(ID3D12RootSignatureDeserializer *iface)
{
    struct d3d12_root_signature_deserializer *deserializer = impl_from_ID3D12RootSignatureDeserializer(iface);
    ULONG refcount = InterlockedDecrement(&deserializer->refcount);

    TRACE("%p decreasing refcount to %u.\n", deserializer, refcount);

    if (!refcount)
    {
        vkd3d_shader_free_root_signature(&deserializer->desc);
        vkd3d_free(deserializer);
    }

    return refcount;
}

static const D3D12_ROOT_SIGNATURE_DESC * STDMETHODCALLTYPE d3d12_root_signature_deserializer_GetRootSignatureDesc(
        ID3D12RootSignatureDeserializer *iface)
{
    struct d3d12_root_signature_deserializer *deserializer = impl_from_ID3D12RootSignatureDeserializer(iface);

    TRACE("iface %p.\n", iface);

    return &deserializer->desc;
}

static const struct ID3D12RootSignatureDeserializerVtbl d3d12_root_signature_deserializer_vtbl =
{
    /* IUnknown methods */
    d3d12_root_signature_deserializer_QueryInterface,
    d3d12_root_signature_deserializer_AddRef,
    d3d12_root_signature_deserializer_Release,
    /* ID3D12RootSignatureDeserializer methods */
    d3d12_root_signature_deserializer_GetRootSignatureDesc,
};

static HRESULT d3d12_root_signature_deserializer_init(struct d3d12_root_signature_deserializer *deserializer,
        const struct vkd3d_shader_code *dxbc)
{
    HRESULT hr;

    deserializer->ID3D12RootSignatureDeserializer_iface.lpVtbl = &d3d12_root_signature_deserializer_vtbl;
    deserializer->refcount = 1;

    if (FAILED(hr = vkd3d_shader_parse_root_signature(dxbc, &deserializer->desc)))
    {
        WARN("Failed to parse root signature, hr %#x.\n", hr);
        return hr;
    }

    return S_OK;
}

HRESULT WINAPI D3D12CreateRootSignatureDeserializer(const void *data, SIZE_T data_size,
        REFIID riid, void **deserializer)
{
    struct vkd3d_shader_code dxbc = {data, data_size};
    struct d3d12_root_signature_deserializer *object;
    HRESULT hr;

    TRACE("data %p, data_size %lu, riid %s, deserializer %p.\n",
            data, data_size, debugstr_guid(riid), deserializer);

    if (!(object = vkd3d_malloc(sizeof(*object))))
        return E_OUTOFMEMORY;

    if (FAILED(hr = d3d12_root_signature_deserializer_init(object, &dxbc)))
    {
        vkd3d_free(object);
        return hr;
    }

    return return_interface((IUnknown *)&object->ID3D12RootSignatureDeserializer_iface,
            &IID_ID3D12RootSignatureDeserializer, riid, deserializer);
}
