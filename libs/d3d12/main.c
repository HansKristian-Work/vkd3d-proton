/*
 * Copyright 2018 Józef Kucia for CodeWeavers
 * Copyright 2020 Joshua Ashton for Valve Software
 * Copyright 2023 Hans-Kristian Arntzen for Valve Corporation
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
 *
 */

#define VKD3D_DBG_CHANNEL VKD3D_DBG_CHANNEL_API

/* Make sure that we don't create a log file intended for d3d12core.dll.
 * These modules are now split, and we'll either block d3d12core log file from being created (Win32),
 * or the log file for d3d12.dll disappears into the aether. */
#define VKD3D_DBG_NO_FILE

#define INITGUID

#define VK_NO_PROTOTYPES
#ifdef _WIN32
#include "vkd3d_win32.h"
#endif
#include "vkd3d_sonames.h"
#include "vkd3d.h"
#include "vkd3d_atomic.h"
#include "vkd3d_debug.h"
#include "vkd3d_threads.h"
#include "vkd3d_string.h"
#include "vkd3d_platform.h"

#if defined(__WINE__) || !defined(_WIN32)
#define DLLEXPORT __attribute__((visibility("default")))
#include <dlfcn.h>
#else
#define DLLEXPORT
#endif

static pthread_once_t library_once = PTHREAD_ONCE_INIT;

static vkd3d_module_t d3d12core_module = NULL;

static IVKD3DCoreInterface* core = NULL;

static const struct
{
    const char *vuid;
    const char *explanation;
} ignored_validation_ids[] = {
    { "07791", "Unimplementable requirement to not overlap memory due to how we need to place RTAS" },
};

static bool load_d3d12core_module(const char *module_name)
{
    if (!d3d12core_module)
    {
        PFN_D3D12_GET_INTERFACE d3d12core_D3D12GetInterface = NULL;
        IVKD3DDebugControlInterface *dbg = NULL;
        unsigned int i;

        /* We link directly to d3d12core, however we still need to dlopen + dlsym
         * as both shared libraries export D3D12GetInterface, so we need to do this
         * to avoid confusing the linker. */
        if ((d3d12core_module = vkd3d_dlopen(module_name)))
            d3d12core_D3D12GetInterface = (PFN_D3D12_GET_INTERFACE)vkd3d_dlsym(d3d12core_module, "D3D12GetInterface");

        if (!d3d12core_D3D12GetInterface)
        {
            WARN("Did not find d3d12core implementation.\n");
            goto fail;
        }

        if (FAILED(d3d12core_D3D12GetInterface(&CLSID_VKD3DCore, &IID_IVKD3DCoreInterface, (void**)&core)))
        {
            goto fail;
        }

        if (SUCCEEDED(d3d12core_D3D12GetInterface(&CLSID_VKD3DDebugControl, &IID_IVKD3DDebugControlInterface, (void**)&dbg)))
        {
            for (i = 0; i < ARRAY_SIZE(ignored_validation_ids); i++)
            {
                if (FAILED(IVKD3DDebugControlInterface_MuteValidationMessageID(
                        dbg, ignored_validation_ids[i].vuid, ignored_validation_ids[i].explanation)))
                    goto fail;
            }
        }

        return true;
    }

fail:
    core = NULL;
    if (d3d12core_module)
        vkd3d_dlclose(d3d12core_module);
    d3d12core_module = NULL;
    return false;
}

static void load_d3d12core_once(void)
{
    bool ret;

    ret = load_d3d12core_module(SONAME_D3D12CORE);
#ifdef _WIN32
    if (!ret)
    {
        /* Fallback to loading directly from the system32 dir, to handle
         * the case where a game ships a D3D12Core.dll next to
         * their executable. */
        char buf[VKD3D_PATH_MAX];
        GetSystemDirectoryA(buf, sizeof(buf));
        vkd3d_strlcat(buf, sizeof(buf), "\\" SONAME_D3D12CORE);

        ret = load_d3d12core_module(buf);
    }
#endif

    if (!ret)
    {
        ERR("Failed to find vkd3d-proton d3d12core interfaces. Make sure " SONAME_D3D12CORE " is installed as well.\n");
    }
}

static bool load_d3d12core(void)
{
    pthread_once(&library_once, load_d3d12core_once);
    return core != NULL;
}

HRESULT WINAPI DLLEXPORT D3D12CreateDevice(IUnknown *adapter, D3D_FEATURE_LEVEL minimum_feature_level,
        REFIID iid, void **device)
{
    TRACE("adapter %p, minimum_feature_level %#x, iid %s, device %p.\n",
            adapter, minimum_feature_level, debugstr_guid(iid), device);

    if (!load_d3d12core())
        return E_NOINTERFACE;
    return IVKD3DCoreInterface_CreateDevice(core, adapter, minimum_feature_level, iid, device);
}

HRESULT WINAPI DLLEXPORT D3D12CreateRootSignatureDeserializer(const void *data, SIZE_T data_size,
        REFIID iid, void **deserializer)
{
    TRACE("data %p, data_size %lu, iid %s, deserializer %p.\n",
            data, data_size, debugstr_guid(iid), deserializer);

    if (!load_d3d12core())
        return E_NOINTERFACE;
    return IVKD3DCoreInterface_CreateRootSignatureDeserializer(core, data, data_size, iid, deserializer);
}

HRESULT WINAPI DLLEXPORT D3D12SerializeRootSignature(const D3D12_ROOT_SIGNATURE_DESC *root_signature_desc,
        D3D_ROOT_SIGNATURE_VERSION version, ID3DBlob **blob, ID3DBlob **error_blob)
{
    TRACE("root_signature_desc %p, version %#x, blob %p, error_blob %p.\n",
            root_signature_desc, version, blob, error_blob);

    if (!load_d3d12core())
        return E_NOINTERFACE;
    return IVKD3DCoreInterface_SerializeRootSignature(core, root_signature_desc, version, blob, error_blob);
}

HRESULT WINAPI DLLEXPORT D3D12CreateVersionedRootSignatureDeserializer(const void *data, SIZE_T data_size,
        REFIID iid, void **deserializer)
{
    TRACE("data %p, data_size %lu, iid %s, deserializer %p.\n",
            data, data_size, debugstr_guid(iid), deserializer);

    if (!load_d3d12core())
        return E_NOINTERFACE;
    return IVKD3DCoreInterface_CreateVersionedRootSignatureDeserializer(core, data, data_size, iid, deserializer);
}

HRESULT WINAPI DLLEXPORT D3D12SerializeVersionedRootSignature(const D3D12_VERSIONED_ROOT_SIGNATURE_DESC *desc,
        ID3DBlob **blob, ID3DBlob **error_blob)
{
    TRACE("desc %p, blob %p, error_blob %p.\n", desc, blob, error_blob);

    if (!load_d3d12core())
        return E_NOINTERFACE;
    return IVKD3DCoreInterface_SerializeVersionedRootSignature(core, desc, blob, error_blob);
}

HRESULT WINAPI DLLEXPORT D3D12GetDebugInterface(REFIID iid, void **debug)
{
    TRACE("iid %s, debug %p.\n", debugstr_guid(iid), debug);

    if (!load_d3d12core())
        return E_NOINTERFACE;
    return IVKD3DCoreInterface_GetDebugInterface(core, iid, debug);
}

HRESULT WINAPI DLLEXPORT D3D12EnableExperimentalFeatures(UINT feature_count,
        const IID *iids, void *configurations, UINT *configurations_sizes)
{
    FIXME("feature_count %u, iids %p, configurations %p, configurations_sizes %p stub!\n",
            feature_count, iids, configurations, configurations_sizes);

    if (!load_d3d12core())
        return E_NOINTERFACE;
    return IVKD3DCoreInterface_EnableExperimentalFeatures(core, feature_count, iids, configurations, configurations_sizes);
}

struct d3d12_sdk_configuration
{
    ID3D12SDKConfiguration1 iface;
    LONG refcount;
};

static struct d3d12_sdk_configuration *impl_from_ID3D12SDKConfiguration1(ID3D12SDKConfiguration1 *iface)
{
    if (!iface)
        return NULL;
    return CONTAINING_RECORD(iface, struct d3d12_sdk_configuration, iface);
}

static ULONG STDMETHODCALLTYPE d3d12_sdk_configuration_AddRef(ID3D12SDKConfiguration1 *iface)
{
    struct d3d12_sdk_configuration *config = impl_from_ID3D12SDKConfiguration1(iface);
    TRACE("iface %p\n", iface);
    return InterlockedIncrement(&config->refcount);
}

static ULONG STDMETHODCALLTYPE d3d12_sdk_configuration_Release(ID3D12SDKConfiguration1 *iface)
{
    struct d3d12_sdk_configuration *config = impl_from_ID3D12SDKConfiguration1(iface);
    ULONG refcount;
    TRACE("iface %p\n", iface);
    refcount = InterlockedDecrement(&config->refcount);
    if (refcount == 0)
        vkd3d_free(config);
    return refcount;
}

static HRESULT STDMETHODCALLTYPE d3d12_sdk_configuration_QueryInterface(ID3D12SDKConfiguration1 *iface, REFIID iid, void **object)
{
    TRACE("iface %p, iid %s, object %p.\n", iface, debugstr_guid(iid), object);

    if (!object)
        return E_POINTER;

    if (IsEqualGUID(iid, &IID_ID3D12SDKConfiguration) ||
        IsEqualGUID(iid, &IID_ID3D12SDKConfiguration1) ||
        IsEqualGUID(iid, &IID_IUnknown))
    {
        d3d12_sdk_configuration_AddRef(iface);
        *object = iface;
        return S_OK;
    }

    return E_NOINTERFACE;
}

static HRESULT STDMETHODCALLTYPE d3d12_sdk_configuration_SetSDKVersion(
    ID3D12SDKConfiguration1 *iface, UINT SDKVersion, LPCSTR SDKPath)
{
    FIXME("iface %p, SDKVersion %u, SDKPath %s stub!\n", iface, SDKVersion, SDKPath);
    /* Native seems to just return S_OK here even for garbage inputs, *shrug*. */
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d3d12_sdk_configuration_CreateDeviceFactory(ID3D12SDKConfiguration1 *iface,
    UINT SDKVersion, LPCSTR SDKPath, REFIID iid, void **ppvFactory)
{
    TRACE("iface %p, SDKVersion %u, SDKPath %s, iid %s, ppvFactory %p!\n", iface, SDKVersion, SDKPath,
            debugstr_guid(iid), ppvFactory);

    if (!load_d3d12core())
        return E_NOINTERFACE;

    return IVKD3DCoreInterface_GetInterface(core, &CLSID_D3D12DeviceFactory, iid, ppvFactory);
}

static void STDMETHODCALLTYPE d3d12_sdk_configuration_FreeUnusedSDKs(ID3D12SDKConfiguration1 *iface)
{
    FIXME("iface %p stub!\n", iface);
}

static CONST_VTBL ID3D12SDKConfiguration1Vtbl d3d12_sdk_configuration_vtbl =
{
    /* IUnknown methods */
    d3d12_sdk_configuration_QueryInterface,
    d3d12_sdk_configuration_AddRef,
    d3d12_sdk_configuration_Release,
    /* ID3D12SDKConfiguration methods */
    d3d12_sdk_configuration_SetSDKVersion,
    /* ID3D12SDKConfiguration1 methods */
    d3d12_sdk_configuration_CreateDeviceFactory,
    d3d12_sdk_configuration_FreeUnusedSDKs,
};

static ID3D12SDKConfiguration1 *create_sdk_configuration_interface(void)
{
    struct d3d12_sdk_configuration *config = vkd3d_calloc(1, sizeof(*config));
    if (!config)
        return NULL;

    config->refcount = 1;
    config->iface.lpVtbl = &d3d12_sdk_configuration_vtbl;
    return &config->iface;
}

HRESULT WINAPI DLLEXPORT D3D12GetInterface(REFCLSID rcslid, REFIID iid, void **debug)
{
    TRACE("rcslid %s iid %s, debug %p.\n", debugstr_guid(rcslid), debugstr_guid(iid), debug);

    if (IsEqualGUID(rcslid, &CLSID_D3D12SDKConfiguration))
    {
        /* The vtable for this must live in d3d12.dll. d3d12core.dll should not be loaded yet. */
        ID3D12SDKConfiguration1 *iface;
        HRESULT hr;

        if (!debug)
            return S_FALSE;

        iface = create_sdk_configuration_interface();
        if (!iface)
            return E_OUTOFMEMORY;

        hr = ID3D12SDKConfiguration1_QueryInterface(iface, iid, debug);
        ID3D12SDKConfiguration1_Release(iface);
        return hr;
    }

    if (!load_d3d12core())
        return E_NOINTERFACE;
    return IVKD3DCoreInterface_GetInterface(core, rcslid, iid, debug);
}
