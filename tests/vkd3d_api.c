/*
 * Copyright 2018 Józef Kucia for CodeWeavers
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

#define COBJMACROS
#define INITGUID
#define WIDL_C_INLINE_WRAPPERS
#include "vkd3d_test.h"
#include <vkd3d.h>

static bool signal_event(HANDLE event)
{
    trace("Signal event %p.\n", event);
    return true;
}

static void test_create_instance(void)
{
    struct vkd3d_instance_create_info create_info;
    struct vkd3d_instance *instance;
    ULONG refcount;
    HRESULT hr;

    memset(&create_info, 0, sizeof(create_info));
    create_info.wchar_size = sizeof(WCHAR);

    hr = vkd3d_create_instance(&create_info, &instance);
    ok(hr == S_OK, "Failed to create instance, hr %#x.\n", hr);
    refcount = vkd3d_instance_incref(instance);
    ok(refcount == 2, "Got unexpected refcount %u.\n", refcount);
    vkd3d_instance_decref(instance);
    refcount = vkd3d_instance_decref(instance);
    ok(!refcount, "Instance has %u references left.\n", refcount);

    create_info.signal_event_pfn = signal_event;
    hr = vkd3d_create_instance(&create_info, &instance);
    ok(hr == S_OK, "Failed to create instance, hr %#x.\n", hr);
    refcount = vkd3d_instance_decref(instance);
    ok(!refcount, "Instance has %u references left.\n", refcount);
}

static bool have_d3d12_device(void)
{
    struct vkd3d_instance_create_info instance_create_info =
    {
        .wchar_size = sizeof(WCHAR),
    };
    struct vkd3d_device_create_info device_create_info =
    {
        .minimum_feature_level = D3D_FEATURE_LEVEL_11_0,
        .instance_create_info = &instance_create_info,
    };
    ID3D12Device *device;
    HRESULT hr;

    if (SUCCEEDED(hr = vkd3d_create_device(&device_create_info, &IID_ID3D12Device, (void **)&device)))
        ID3D12Device_Release(device);
    return hr == S_OK;
}

START_TEST(vkd3d_api)
{
    if (!have_d3d12_device())
    {
        skip("D3D12 device cannot be created.\n");
        return;
    }

    run_test(test_create_instance);
}
