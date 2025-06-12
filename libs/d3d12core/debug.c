/*
 * Copyright 2024 Philip Rebohle for Valve Software
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

#include "vkd3d_debug.h"
#include "vkd3d_threads.h"

#include "debug.h"

static struct d3d12_dred_settings *d3d12_dred_settings_instance;

static pthread_mutex_t debug_singleton_lock = PTHREAD_MUTEX_INITIALIZER;


static struct d3d12_dred_settings *impl_from_ID3D12DeviceRemovedExtendedDataSettings(ID3D12DeviceRemovedExtendedDataSettings *iface);

static HRESULT STDMETHODCALLTYPE d3d12_dred_settings_QueryInterface(
        d3d12_dred_settings_iface *iface, REFIID riid, void **object)
{
    TRACE("iface %p, riid %s, object %p.\n", iface, debugstr_guid(riid), object);

    if (!object)
        return E_POINTER;

    if (IsEqualGUID(riid, &IID_IUnknown)
            || IsEqualGUID(riid, &IID_ID3D12DeviceRemovedExtendedDataSettings))
    {
        ID3D12DeviceRemovedExtendedDataSettings_AddRef(iface);
        *object = iface;
        return S_OK;
    }

    WARN("%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid(riid));

    *object = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE d3d12_dred_settings_AddRef(d3d12_dred_settings_iface *iface)
{
    struct d3d12_dred_settings *dred_settings = impl_from_ID3D12DeviceRemovedExtendedDataSettings(iface);
    ULONG refcount = InterlockedIncrement(&dred_settings->refcount);

    TRACE("%p increasing refcount to %u.\n", dred_settings, refcount);

    return refcount;
}

static ULONG STDMETHODCALLTYPE d3d12_dred_settings_Release(d3d12_dred_settings_iface *iface)
{
    struct d3d12_dred_settings *dred_settings = impl_from_ID3D12DeviceRemovedExtendedDataSettings(iface);
    ULONG refcount;

    pthread_mutex_lock(&debug_singleton_lock);
    refcount = InterlockedDecrement(&dred_settings->refcount);

    TRACE("%p decreasing refcount to %u.\n", dred_settings, refcount);

    if (!refcount)
    {
        assert(d3d12_dred_settings_instance == dred_settings);
        vkd3d_free(dred_settings);
        d3d12_dred_settings_instance = NULL;
    }

    pthread_mutex_unlock(&debug_singleton_lock);
    return refcount;
}

static void STDMETHODCALLTYPE d3d12_dred_settings_SetAutoBreadcrumbsEnablement( 
        d3d12_dred_settings_iface *iface, D3D12_DRED_ENABLEMENT enablement)
{
    FIXME_ONCE("iface %p, enablement %u stub!\n", iface, enablement);
}

static void STDMETHODCALLTYPE d3d12_dred_settings_SetPageFaultEnablement( 
        d3d12_dred_settings_iface *iface, D3D12_DRED_ENABLEMENT enablement)
{
    FIXME_ONCE("iface %p, enablement %u stub!\n", iface, enablement);
}

static void STDMETHODCALLTYPE d3d12_dred_settings_SetWatsonDumpEnablement( 
        d3d12_dred_settings_iface *iface, D3D12_DRED_ENABLEMENT enablement)
{
    FIXME_ONCE("iface %p, enablement %u stub!\n", iface, enablement);
}

CONST_VTBL struct ID3D12DeviceRemovedExtendedDataSettingsVtbl d3d12_dred_settings_vtbl =
{
    /* IUnknown methods */
    d3d12_dred_settings_QueryInterface,
    d3d12_dred_settings_AddRef,
    d3d12_dred_settings_Release,
    /* ID3D12DeviceRemovedExtendedDataSettings methods */
    d3d12_dred_settings_SetAutoBreadcrumbsEnablement,
    d3d12_dred_settings_SetPageFaultEnablement,
    d3d12_dred_settings_SetWatsonDumpEnablement,
};

static void d3d12_dred_settings_init(struct d3d12_dred_settings *object)
{
    object->ID3D12DeviceRemovedExtendedDataSettings_iface.lpVtbl = &d3d12_dred_settings_vtbl;
    object->refcount = 0; /* incremented on retrieval */
}

HRESULT d3d12_dred_settings_create(ID3D12DeviceRemovedExtendedDataSettings **object)
{
    ID3D12DeviceRemovedExtendedDataSettings *iface;
    HRESULT hr = S_OK;

    pthread_mutex_lock(&debug_singleton_lock);

    if (!d3d12_dred_settings_instance)
    {
        d3d12_dred_settings_instance = vkd3d_malloc(sizeof(*d3d12_dred_settings_instance));
        d3d12_dred_settings_init(d3d12_dred_settings_instance);
    }

    iface = &d3d12_dred_settings_instance->ID3D12DeviceRemovedExtendedDataSettings_iface;
    ID3D12DeviceRemovedExtendedDataSettings_AddRef(iface);

    pthread_mutex_unlock(&debug_singleton_lock);

    *object = iface;
    return hr;
}

static struct d3d12_dred_settings *impl_from_ID3D12DeviceRemovedExtendedDataSettings(ID3D12DeviceRemovedExtendedDataSettings *iface)
{
    if (!iface)
        return NULL;
    assert(iface->lpVtbl == &d3d12_dred_settings_vtbl);
    return CONTAINING_RECORD(iface, struct d3d12_dred_settings, ID3D12DeviceRemovedExtendedDataSettings_iface);
}
