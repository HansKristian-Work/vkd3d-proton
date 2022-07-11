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

#define VKD3D_DBG_CHANNEL VKD3D_DBG_CHANNEL_API

#define INITGUID
#include "vkd3d_private.h"

VKD3D_EXPORT HRESULT vkd3d_create_device(const struct vkd3d_device_create_info *create_info,
        REFIID iid, void **device)
{
    struct vkd3d_instance *instance;
    struct d3d12_device *object;
    HRESULT hr;

    TRACE("create_info %p, iid %s, device %p.\n", create_info, debugstr_guid(iid), device);

    if (!create_info)
        return E_INVALIDARG;
    if (!create_info->instance && !create_info->instance_create_info)
    {
        ERR("Instance or instance create info is required.\n");
        return E_INVALIDARG;
    }
    if (create_info->instance && create_info->instance_create_info)
    {
        ERR("Instance and instance create info are mutually exclusive parameters.\n");
        return E_INVALIDARG;
    }

    if (create_info->minimum_feature_level < D3D_FEATURE_LEVEL_11_0
            || !is_valid_feature_level(create_info->minimum_feature_level))
    {
        WARN("Invalid feature level %#x.\n", create_info->minimum_feature_level);
        return E_INVALIDARG;
    }

    if ((instance = create_info->instance))
    {
        vkd3d_instance_incref(instance);
    }
    else if (FAILED(hr = vkd3d_create_instance(create_info->instance_create_info, &instance)))
    {
        WARN("Failed to create instance, hr %#x.\n", hr);
        return E_FAIL;
    }

    hr = d3d12_device_create(instance, create_info, &object);
    vkd3d_instance_decref(instance);
    if (FAILED(hr))
        return hr;

    if (!device)
    {
        ID3D12Device_Release(&object->ID3D12Device_iface);
        return S_FALSE;
    }

    return return_interface(&object->ID3D12Device_iface, &IID_ID3D12Device, iid, device);
}

/* ID3D12RootSignatureDeserializer */
struct d3d12_root_signature_deserializer
{
    ID3D12RootSignatureDeserializer ID3D12RootSignatureDeserializer_iface;
    LONG refcount;

    union
    {
        D3D12_VERSIONED_ROOT_SIGNATURE_DESC d3d12;
        struct vkd3d_versioned_root_signature_desc vkd3d;
    } desc;
};

STATIC_ASSERT(sizeof(D3D12_ROOT_SIGNATURE_DESC) == sizeof(struct vkd3d_root_signature_desc));

static struct d3d12_root_signature_deserializer *impl_from_ID3D12RootSignatureDeserializer(
        ID3D12RootSignatureDeserializer *iface)
{
    return CONTAINING_RECORD(iface, struct d3d12_root_signature_deserializer, ID3D12RootSignatureDeserializer_iface);
}

static HRESULT STDMETHODCALLTYPE d3d12_root_signature_deserializer_QueryInterface(
        ID3D12RootSignatureDeserializer *iface, REFIID riid, void **object)
{
    TRACE("iface %p, riid %s, object %p.\n", iface, debugstr_guid(riid), object);

    /* QueryInterface() implementation is broken, E_NOINTERFACE is returned for
     * IUnknown.
     */
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
        vkd3d_shader_free_root_signature(&deserializer->desc.vkd3d);
        vkd3d_free(deserializer);
    }

    return refcount;
}

static const D3D12_ROOT_SIGNATURE_DESC * STDMETHODCALLTYPE d3d12_root_signature_deserializer_GetRootSignatureDesc(
        ID3D12RootSignatureDeserializer *iface)
{
    struct d3d12_root_signature_deserializer *deserializer = impl_from_ID3D12RootSignatureDeserializer(iface);

    TRACE("iface %p.\n", iface);

    assert(deserializer->desc.d3d12.Version == D3D_ROOT_SIGNATURE_VERSION_1_0);
    return &deserializer->desc.d3d12.Desc_1_0;
}

static CONST_VTBL struct ID3D12RootSignatureDeserializerVtbl d3d12_root_signature_deserializer_vtbl =
{
    /* IUnknown methods */
    d3d12_root_signature_deserializer_QueryInterface,
    d3d12_root_signature_deserializer_AddRef,
    d3d12_root_signature_deserializer_Release,
    /* ID3D12RootSignatureDeserializer methods */
    d3d12_root_signature_deserializer_GetRootSignatureDesc,
};

static int vkd3d_parse_root_signature_for_version(const struct vkd3d_shader_code *dxbc,
        struct vkd3d_versioned_root_signature_desc *out_desc,
        enum vkd3d_root_signature_version target_version,
        bool raw_payload,
        vkd3d_shader_hash_t *compatibility_hash)
{
    struct vkd3d_versioned_root_signature_desc desc, converted_desc;
    int ret;

    if (raw_payload)
    {
        if ((ret = vkd3d_shader_parse_root_signature_raw(dxbc->code, dxbc->size, &desc, compatibility_hash)) < 0)
        {
            WARN("Failed to parse root signature, vkd3d result %d.\n", ret);
            return ret;
        }
    }
    else
    {
        if ((ret = vkd3d_shader_parse_root_signature(dxbc, &desc, compatibility_hash)) < 0)
        {
            WARN("Failed to parse root signature, vkd3d result %d.\n", ret);
            return ret;
        }
    }

    if (desc.version == target_version)
    {
        *out_desc = desc;
    }
    else
    {
        ret = vkd3d_shader_convert_root_signature(&converted_desc, target_version, &desc);
        vkd3d_shader_free_root_signature(&desc);
        if (ret < 0)
        {
            WARN("Failed to convert from version %#x, vkd3d result %d.\n", desc.version, ret);
            return ret;
        }

        *out_desc = converted_desc;
    }

    return ret;
}

int vkd3d_parse_root_signature_v_1_0(const struct vkd3d_shader_code *dxbc,
        struct vkd3d_versioned_root_signature_desc *out_desc,
        vkd3d_shader_hash_t *compatibility_hash)
{
    return vkd3d_parse_root_signature_for_version(dxbc, out_desc, VKD3D_ROOT_SIGNATURE_VERSION_1_0, false,
            compatibility_hash);
}

int vkd3d_parse_root_signature_v_1_1(const struct vkd3d_shader_code *dxbc,
        struct vkd3d_versioned_root_signature_desc *out_desc,
        vkd3d_shader_hash_t *compatibility_hash)
{
    return vkd3d_parse_root_signature_for_version(dxbc, out_desc, VKD3D_ROOT_SIGNATURE_VERSION_1_1, false,
            compatibility_hash);
}

int vkd3d_parse_root_signature_v_1_1_from_raw_payload(const struct vkd3d_shader_code *dxbc,
        struct vkd3d_versioned_root_signature_desc *out_desc,
        vkd3d_shader_hash_t *compatibility_hash)
{
    return vkd3d_parse_root_signature_for_version(dxbc, out_desc, VKD3D_ROOT_SIGNATURE_VERSION_1_1, true,
            compatibility_hash);
}

static HRESULT d3d12_root_signature_deserializer_init(struct d3d12_root_signature_deserializer *deserializer,
        const struct vkd3d_shader_code *dxbc)
{
    int ret;

    deserializer->ID3D12RootSignatureDeserializer_iface.lpVtbl = &d3d12_root_signature_deserializer_vtbl;
    deserializer->refcount = 1;

    if ((ret = vkd3d_parse_root_signature_v_1_0(dxbc, &deserializer->desc.vkd3d, NULL)) < 0)
        return hresult_from_vkd3d_result(ret);

    return S_OK;
}

VKD3D_EXPORT HRESULT vkd3d_create_root_signature_deserializer(const void *data, SIZE_T data_size,
        REFIID iid, void **deserializer)
{
    struct vkd3d_shader_code dxbc = {data, data_size};
    struct d3d12_root_signature_deserializer *object;
    HRESULT hr;

    TRACE("data %p, data_size %lu, iid %s, deserializer %p.\n",
            data, data_size, debugstr_guid(iid), deserializer);

    if (!(object = vkd3d_malloc(sizeof(*object))))
        return E_OUTOFMEMORY;

    if (FAILED(hr = d3d12_root_signature_deserializer_init(object, &dxbc)))
    {
        vkd3d_free(object);
        return hr;
    }

    return return_interface(&object->ID3D12RootSignatureDeserializer_iface,
            &IID_ID3D12RootSignatureDeserializer, iid, deserializer);
}

/* ID3D12VersionedRootSignatureDeserializer */
struct d3d12_versioned_root_signature_deserializer
{
    ID3D12VersionedRootSignatureDeserializer ID3D12VersionedRootSignatureDeserializer_iface;
    LONG refcount;

    union
    {
        D3D12_VERSIONED_ROOT_SIGNATURE_DESC d3d12;
        struct vkd3d_versioned_root_signature_desc vkd3d;
    } desc, other_desc;
};

STATIC_ASSERT(sizeof(D3D12_VERSIONED_ROOT_SIGNATURE_DESC) == sizeof(struct vkd3d_versioned_root_signature_desc));

static struct d3d12_versioned_root_signature_deserializer *impl_from_ID3D12VersionedRootSignatureDeserializer(
        ID3D12VersionedRootSignatureDeserializer *iface)
{
    return CONTAINING_RECORD(iface, struct d3d12_versioned_root_signature_deserializer,
            ID3D12VersionedRootSignatureDeserializer_iface);
}

static HRESULT STDMETHODCALLTYPE d3d12_versioned_root_signature_deserializer_QueryInterface(
        ID3D12VersionedRootSignatureDeserializer *iface, REFIID iid, void **object)
{
    TRACE("iface %p, iid %s, object %p.\n", iface, debugstr_guid(iid), object);

    /* QueryInterface() implementation is broken, E_NOINTERFACE is returned for
     * IUnknown.
     */
    if (IsEqualGUID(iid, &IID_ID3D12VersionedRootSignatureDeserializer))
    {
        ID3D12VersionedRootSignatureDeserializer_AddRef(iface);
        *object = iface;
        return S_OK;
    }

    WARN("%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid(iid));

    *object = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE d3d12_versioned_root_signature_deserializer_AddRef(ID3D12VersionedRootSignatureDeserializer *iface)
{
    struct d3d12_versioned_root_signature_deserializer *deserializer = impl_from_ID3D12VersionedRootSignatureDeserializer(iface);
    ULONG refcount = InterlockedIncrement(&deserializer->refcount);

    TRACE("%p increasing refcount to %u.\n", deserializer, refcount);

    return refcount;
}

static ULONG STDMETHODCALLTYPE d3d12_versioned_root_signature_deserializer_Release(ID3D12VersionedRootSignatureDeserializer *iface)
{
    struct d3d12_versioned_root_signature_deserializer *deserializer = impl_from_ID3D12VersionedRootSignatureDeserializer(iface);
    ULONG refcount = InterlockedDecrement(&deserializer->refcount);

    TRACE("%p decreasing refcount to %u.\n", deserializer, refcount);

    if (!refcount)
    {
        vkd3d_shader_free_root_signature(&deserializer->desc.vkd3d);
        vkd3d_shader_free_root_signature(&deserializer->other_desc.vkd3d);
        vkd3d_free(deserializer);
    }

    return refcount;
}

static enum vkd3d_root_signature_version vkd3d_root_signature_version_from_d3d12(D3D_ROOT_SIGNATURE_VERSION version)
{
    switch (version)
    {
        case D3D_ROOT_SIGNATURE_VERSION_1_0:
            return VKD3D_ROOT_SIGNATURE_VERSION_1_0;
        case D3D_ROOT_SIGNATURE_VERSION_1_1:
            return VKD3D_ROOT_SIGNATURE_VERSION_1_1;
        default:
            WARN("Unknown root signature version %#x.\n", version);
            return 0;
    }
}

static HRESULT STDMETHODCALLTYPE d3d12_versioned_root_signature_deserializer_GetRootSignatureDescAtVersion(
        ID3D12VersionedRootSignatureDeserializer *iface, D3D_ROOT_SIGNATURE_VERSION version,
        const D3D12_VERSIONED_ROOT_SIGNATURE_DESC **desc)
{
    struct d3d12_versioned_root_signature_deserializer *deserializer = impl_from_ID3D12VersionedRootSignatureDeserializer(iface);
    int ret;

    TRACE("iface %p, version %#x, desc %p.\n", iface, version, desc);

    if (version != D3D_ROOT_SIGNATURE_VERSION_1_0 && version != D3D_ROOT_SIGNATURE_VERSION_1_1)
    {
        WARN("Root signature version %#x not supported.\n", version);
        return E_INVALIDARG;
    }

    if (deserializer->desc.d3d12.Version == version)
    {
        *desc = &deserializer->desc.d3d12;
        return S_OK;
    }

    if (!deserializer->other_desc.d3d12.Version)
    {
        if ((ret = vkd3d_shader_convert_root_signature(&deserializer->other_desc.vkd3d,
                vkd3d_root_signature_version_from_d3d12(version), &deserializer->desc.vkd3d)) < 0)
        {
            WARN("Failed to convert versioned root signature, vkd3d result %d.\n", ret);
            return hresult_from_vkd3d_result(ret);
        }
    }

    assert(deserializer->other_desc.d3d12.Version == version);
    *desc = &deserializer->other_desc.d3d12;
    return S_OK;
}

static const D3D12_VERSIONED_ROOT_SIGNATURE_DESC * STDMETHODCALLTYPE
d3d12_versioned_root_signature_deserializer_GetUnconvertedRootSignatureDesc(ID3D12VersionedRootSignatureDeserializer *iface)
{
    struct d3d12_versioned_root_signature_deserializer *deserializer = impl_from_ID3D12VersionedRootSignatureDeserializer(iface);

    TRACE("iface %p.\n", iface);

    return &deserializer->desc.d3d12;
}

static CONST_VTBL struct ID3D12VersionedRootSignatureDeserializerVtbl d3d12_versioned_root_signature_deserializer_vtbl =
{
    /* IUnknown methods */
    d3d12_versioned_root_signature_deserializer_QueryInterface,
    d3d12_versioned_root_signature_deserializer_AddRef,
    d3d12_versioned_root_signature_deserializer_Release,
    /* ID3D12VersionedRootSignatureDeserializer methods */
    d3d12_versioned_root_signature_deserializer_GetRootSignatureDescAtVersion,
    d3d12_versioned_root_signature_deserializer_GetUnconvertedRootSignatureDesc,
};

static HRESULT d3d12_versioned_root_signature_deserializer_init(struct d3d12_versioned_root_signature_deserializer *deserializer,
        const struct vkd3d_shader_code *dxbc)
{
    int ret;

    deserializer->ID3D12VersionedRootSignatureDeserializer_iface.lpVtbl = &d3d12_versioned_root_signature_deserializer_vtbl;
    deserializer->refcount = 1;

    if ((ret = vkd3d_shader_parse_root_signature(dxbc, &deserializer->desc.vkd3d, NULL)) < 0)
    {
        WARN("Failed to parse root signature, vkd3d result %d.\n", ret);
        return hresult_from_vkd3d_result(ret);
    }

    memset(&deserializer->other_desc, 0, sizeof(deserializer->other_desc));

    return S_OK;
}

VKD3D_EXPORT HRESULT vkd3d_create_versioned_root_signature_deserializer(const void *data, SIZE_T data_size,
        REFIID iid, void **deserializer)
{
    struct d3d12_versioned_root_signature_deserializer *object;
    struct vkd3d_shader_code dxbc = {data, data_size};
    HRESULT hr;

    TRACE("data %p, data_size %lu, iid %s, deserializer %p.\n",
            data, data_size, debugstr_guid(iid), deserializer);

    if (!(object = vkd3d_malloc(sizeof(*object))))
        return E_OUTOFMEMORY;

    if (FAILED(hr = d3d12_versioned_root_signature_deserializer_init(object, &dxbc)))
    {
        vkd3d_free(object);
        return hr;
    }

    return return_interface(&object->ID3D12VersionedRootSignatureDeserializer_iface,
            &IID_ID3D12VersionedRootSignatureDeserializer, iid, deserializer);
}

VKD3D_EXPORT HRESULT vkd3d_serialize_root_signature(const D3D12_ROOT_SIGNATURE_DESC *desc,
        D3D_ROOT_SIGNATURE_VERSION version, ID3DBlob **blob, ID3DBlob **error_blob)
{
    struct vkd3d_versioned_root_signature_desc vkd3d_desc;
    struct vkd3d_shader_code dxbc;
    struct d3d_blob *blob_object;
    HRESULT hr;
    int ret;

    TRACE("desc %p, version %#x, blob %p, error_blob %p.\n", desc, version, blob, error_blob);

    if (version != D3D_ROOT_SIGNATURE_VERSION_1_0)
    {
        WARN("Unexpected Root signature version %#x.\n", version);
        return E_INVALIDARG;
    }

    if (!blob)
    {
        WARN("Invalid blob parameter.\n");
        return E_INVALIDARG;
    }

    if (error_blob)
        *error_blob = NULL;

    vkd3d_desc.version = VKD3D_ROOT_SIGNATURE_VERSION_1_0;
    vkd3d_desc.v_1_0 = *(const struct vkd3d_root_signature_desc *)desc;
    if ((ret = vkd3d_shader_serialize_root_signature(&vkd3d_desc, &dxbc)) < 0)
    {
        WARN("Failed to serialize root signature, vkd3d result %d.\n", ret);
        if (error_blob)
            FIXME("Ignoring error blob %p.\n", error_blob);
        return hresult_from_vkd3d_result(ret);
    }

    if (FAILED(hr = d3d_blob_create((void *)dxbc.code, dxbc.size, &blob_object)))
    {
        WARN("Failed to create blob object, hr %#x.\n", hr);
        vkd3d_shader_free_shader_code(&dxbc);
        return hr;
    }

    *blob = &blob_object->ID3DBlob_iface;

    return S_OK;
}

VKD3D_EXPORT HRESULT vkd3d_serialize_versioned_root_signature(const D3D12_VERSIONED_ROOT_SIGNATURE_DESC *desc,
        ID3DBlob **blob, ID3DBlob **error_blob)
{
    const struct vkd3d_versioned_root_signature_desc *vkd3d_desc;
    struct vkd3d_shader_code dxbc;
    struct d3d_blob *blob_object;
    HRESULT hr;
    int ret;

    TRACE("desc %p, blob %p, error_blob %p.\n", desc, blob, error_blob);

    if (!blob)
    {
        WARN("Invalid blob parameter.\n");
        return E_INVALIDARG;
    }

    if (error_blob)
        *error_blob = NULL;

    vkd3d_desc = (const struct vkd3d_versioned_root_signature_desc *)desc;
    if ((ret = vkd3d_shader_serialize_root_signature(vkd3d_desc, &dxbc)) < 0)
    {
        WARN("Failed to serialize root signature, vkd3d result %d.\n", ret);
        if (error_blob)
            FIXME("Ignoring error blob %p.\n", error_blob);
        return hresult_from_vkd3d_result(ret);
    }

    if (FAILED(hr = d3d_blob_create((void *)dxbc.code, dxbc.size, &blob_object)))
    {
        WARN("Failed to create blob object, hr %#x.\n", hr);
        vkd3d_shader_free_shader_code(&dxbc);
        return hr;
    }

    *blob = &blob_object->ID3DBlob_iface;

    return S_OK;
}
