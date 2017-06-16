/*
 * Copyright 2016 Józef Kucia for CodeWeavers
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
