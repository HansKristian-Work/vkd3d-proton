/*
 * Copyright 2025 Rémi Bernon for Codeweavers
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
#include "vkd3d_d3dkmt.h"

#ifdef _WIN32

void d3d12_device_open_kmt(struct d3d12_device *device)
{
    D3DKMT_OPENADAPTERFROMLUID open_adapter = {0};
    open_adapter.AdapterLuid = device->adapter_luid;

    if (D3DKMTOpenAdapterFromLuid(&open_adapter) == STATUS_SUCCESS)
    {
        D3DKMT_CREATEDEVICE create_device = {0};
        D3DKMT_CLOSEADAPTER close_adapter = {0};

        close_adapter.hAdapter = open_adapter.hAdapter;
        create_device.hAdapter = open_adapter.hAdapter;
        if (D3DKMTCreateDevice(&create_device) == STATUS_SUCCESS)
            device->kmt_local = create_device.hDevice;

        D3DKMTCloseAdapter(&close_adapter);
    }
}

void d3d12_device_close_kmt(struct d3d12_device *device)
{
    D3DKMT_DESTROYDEVICE destroy = {0};
    destroy.hDevice = device->kmt_local;
    D3DKMTDestroyDevice(&destroy);
}

#else /* _WIN32 */

void d3d12_device_open_kmt(struct d3d12_device *device)
{
    WARN("Not implemented on this platform\n");
}

void d3d12_device_close_kmt(struct d3d12_device *device)
{
}

#endif /* _WIN32 */
