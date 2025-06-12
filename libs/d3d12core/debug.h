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
#ifndef __VKD3D_CORE_DEBUG_H
#define __VKD3D_CORE_DEBUG_H

#include "vkd3d.h"

typedef ID3D12DeviceRemovedExtendedDataSettings d3d12_dred_settings_iface;

struct d3d12_dred_settings
{
    d3d12_dred_settings_iface ID3D12DeviceRemovedExtendedDataSettings_iface;
    LONG refcount;
};

HRESULT d3d12_dred_settings_create(ID3D12DeviceRemovedExtendedDataSettings **dred_settings);

#endif
